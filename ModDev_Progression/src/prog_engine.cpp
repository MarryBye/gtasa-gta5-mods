#include "prog_engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "prog_config.hpp"
#include "prog_store.hpp"
#include "progression_rules.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace progression {

namespace {

// Verified at 0x004B5B27: call 0x004B3210 = CPedDamageResponseCalculator::ComputeWillKillPed(CPed*, CPedDamageResponseInfo*, bool)
using FnComputeWillKill = void(__thiscall*)(void*, sa::CPed*, void*, uint8_t);
FnComputeWillKill g_origComputeWillKill = nullptr;

struct RewardedPedEntry {
    const sa::CPed* ped    = nullptr;
    uint32_t        timeMs = 0;
};
constexpr int    kRecentPedSlots = 20;
RewardedPedEntry g_recentKilledPeds[kRecentPedSlots]{};
int              g_recentPedWriteIdx = 0;

// Smooth HUD animation & popup state
struct ActivePopup {
    int       xpAmount   = 0;
    bool      accentGold = false;
    float     ageSec     = 0.0f;
    float     duration   = 1.45f;
    float     offsetX640 = 0.0f;
};

std::vector<ActivePopup> g_popups;
int      g_popupStaggerIdx   = 0;

float    g_displayXp         = 0.0f;   // Smoothly animated total XP
float    g_settledXp         = 0.0f;   // Baseline XP prior to current burst (creates green surge bar)
int      g_displayRank       = 1;      // Last rank shown on HUD (detects crossing into rank-up flash)
float    g_hudAlpha          = 0.0f;
float    g_hudHoldSec        = 0.0f;
float    g_levelUpFlashSec   = 0.0f;
int      g_levelUpShownRank  = 1;
uint32_t g_lastGainTickMs    = 0;
uint32_t g_suppressStatsUntilMs = 0;
uint32_t g_lastSaveTickMs    = 0;
bool     g_dirtySave         = false;

// Phone Addon "Rank & Unlocks" modal state
bool     g_menuOpen          = false;
bool     g_menuFromPhone     = false;
int      g_menuCursor        = 0;

// Authentic native GTA SA audio feedback (zero harsh synthetic sine beeps!)
void PlayXpTickSound() {
    // Keep regular combat XP clean and quiet (just a super subtle native UI tick at low volume)
    if (!Cfg().enableSound) return;
    sa::PlayFrontendSound(sa::SFX_SCROLL, -10.0f, 1.12f);
}

void PlayRankUpSound() {
    if (!Cfg().enableSound) return;
    // Native GTA SA SOUND_PART_MISSION_COMPLETE (1057): classic crisp Rockstar milestone chime!
    sa::ScriptCmd(0x018C).Float(0.0f).Float(0.0f).Float(0.0f).Int(1057).Run();
}

void UpdatePhoneAddonBadge() {
    auto pr = Store().GetProgress();
    modbus::PhoneAddonEntry e{};
    std::strncpy(e.id,       "progression",      sizeof(e.id) - 1);
    std::strncpy(e.labelRus, "Уровень и награды", sizeof(e.labelRus) - 1);
    std::strncpy(e.labelEng, "Rank & Unlocks",   sizeof(e.labelEng) - 1);
    std::snprintf(e.badgeRus, sizeof(e.badgeRus), "Ур. %d",   pr.rank);
    std::snprintf(e.badgeEng, sizeof(e.badgeEng), "Rank %d",  pr.rank);
    e.section    = 0; // Main Phone Menu
    e.onActivate = &OpenUnlocksMenu;
    e.isOpen     = &IsUnlocksMenuOpen;
    modbus::RegisterPhoneAddon(e);
}

void AwardXpInternal(int baseXp, bool goldPopup, const char* reasonLog) {
    if (baseXp <= 0) return;
    int scaled = std::max(1, static_cast<int>(std::round(static_cast<float>(baseXp) * Cfg().xpMultiplier)));

    auto& st = Store();
    auto before = st.GetProgress();
    if (g_hudAlpha < 0.08f) {
        g_displayXp = static_cast<float>(st.totalXp);
        g_settledXp = static_cast<float>(st.totalXp);
        g_displayRank = before.rank;
    }

    st.totalXp += scaled;
    auto after = st.GetProgress();
    g_dirtySave      = true;
    g_lastGainTickMs = GetTickCount();
    g_hudHoldSec     = std::max(g_hudHoldSec, (after.rank > before.rank) ? 5.6f : 4.2f);

    static constexpr float kStaggerOffsets[5] = {0.0f, -24.0f, 24.0f, -12.0f, 12.0f};
    ActivePopup pop{};
    pop.xpAmount   = scaled;
    pop.accentGold = goldPopup;
    pop.ageSec     = 0.0f;
    pop.duration   = goldPopup ? 1.75f : 1.40f;
    pop.offsetX640 = kStaggerOffsets[g_popupStaggerIdx % 5];
    g_popupStaggerIdx = (g_popupStaggerIdx + 1) % 5;
    if (g_popups.size() >= 8) g_popups.erase(g_popups.begin());
    g_popups.push_back(pop);

    if (after.rank > before.rank) {
        g_levelUpFlashSec  = 4.6f;
        g_levelUpShownRank = after.rank;
        PlayRankUpSound();

        // Notify newly unlocked gear/upgrades cleanly via UI Kit HelpToast (avoid double trailing dots!)
        const auto* ms = prog_rules::FindMilestoneForRank(after.rank);
        char rb[220], eb[220];
        if (ms) {
            const size_t rLen = std::strlen(ms->rus);
            const size_t eLen = std::strlen(ms->eng);
            const char* rDot  = (rLen > 0 && ms->rus[rLen - 1] == '.') ? "" : ".";
            const char* eDot  = (eLen > 0 && ms->eng[eLen - 1] == '.') ? "" : ".";
            std::snprintf(rb, sizeof(rb), "~g~Новый уровень %d.~w~ Открыто: %s%s", after.rank, ms->rus, rDot);
            std::snprintf(eb, sizeof(eb), "~g~Rank %d reached.~w~ Unlocked: %s%s", after.rank, ms->eng, eDot);
        } else {
            std::snprintf(rb, sizeof(rb), "~g~Достигнут %d уровень.", after.rank);
            std::snprintf(eb, sizeof(eb), "~g~Reached Rank %d.", after.rank);
        }
        ui::ShowHelp(rb, eb);
        st.SaveNow();
        g_dirtySave = false;
        g_lastSaveTickMs = GetTickCount();
        modlog::Info("[Prog] RANK UP! %d -> %d (TotalXP=%d, source=%s).", before.rank, after.rank, st.totalXp, reasonLog);
    } else {
        PlayXpTickSound();
    }

    SyncBusState();
}

bool WasPedRecentlyRewarded(const sa::CPed* ped, uint32_t nowMs) {
    for (const auto& r : g_recentKilledPeds) {
        if (r.ped == ped && (nowMs - r.timeMs) < 4000u) return true;
    }
    return false;
}

void MarkPedRewarded(const sa::CPed* ped, uint32_t nowMs) {
    g_recentKilledPeds[g_recentPedWriteIdx] = {ped, nowMs};
    g_recentPedWriteIdx = (g_recentPedWriteIdx + 1) % kRecentPedSlots;
}

// Strict hostile-only classification:
//   - Civilians (CIVMALE/CIVFEMALE/MEDIC/FIREMAN/BUM/PROSTITUTE) = 0 XP (unless hostile mission enemy!)
//   - Friendly Grove Street (GANG2) & Triads (GANG7) = 0 XP (unless mission traitor hating player!)
//   - Police / SWAT / FBI / Army, Enemy Gangs, Mafia, Dealers, Criminals & Mission Targets = XP!
int EvaluateHostileKillXp(const sa::CPed* victim) {
    if (!sa::IsPedValid(victim)) return 0;
    if (victim->IsPlayerPedType()) return 0;

    const uint32_t ptype     = victim->GetPedType();
    const uint8_t  createdBy = victim->GetCreatedBy(); // 2 = PED_MISSION
    const bool     hatesPlr  = (victim->GetHateMask() & 1u) != 0;
    const bool     respPlr   = (victim->GetRespectMask() & 1u) != 0;
    const int32_t  modelId   = victim->GetModelIndex();

    // 1. Law Enforcement (Police / SWAT / FBI / Army)
    if (ptype == sa::PEDTYPE_COP) {
        if (modelId == 285 || modelId == 286 || modelId == 287) return 45; // SWAT, FBI, Army
        return 25;
    }

    // 2. Italian Mafia (GANG6)
    if (ptype == sa::PEDTYPE_GANG6) {
        return 30;
    }

    // 3. Friendly Ally Gangs (Grove Street GANG2, Triads GANG7): NEVER award XP unless hostile mission traitor!
    if (ptype == sa::PEDTYPE_GANG2 || ptype == sa::PEDTYPE_GANG7) {
        return (createdBy == 2 && hatesPlr) ? 30 : 0;
    }

    // 4. Hostile Street Gangs, Cartels, Drug Dealers & Criminals
    if (ptype == sa::PEDTYPE_GANG1 || ptype == sa::PEDTYPE_GANG3 ||
        ptype == sa::PEDTYPE_GANG4 || ptype == sa::PEDTYPE_GANG5 ||
        ptype == sa::PEDTYPE_GANG8 || ptype == sa::PEDTYPE_GANG9 ||
        ptype == sa::PEDTYPE_GANG10 || ptype == sa::PEDTYPE_DEALER ||
        ptype == sa::PEDTYPE_CRIMINAL) {
        return 20;
    }

    // 5. Mission Targets (PEDTYPE_MISSION1..8)
    if (ptype >= sa::PEDTYPE_MISSION1 && ptype <= sa::PEDTYPE_MISSION8) {
        if (respPlr && !hatesPlr) return 0; // Friendly mission ally!
        return 30;
    }

    // 6. Explicit hostile mission NPCs spawned under CIVMALE/SPECIAL with HATE PLAYER1
    if (createdBy == 2 && hatesPlr && !respPlr) {
        return 30;
    }

    return 0;
}

void __fastcall HookedComputeWillKillPed(void* calc, void*, sa::CPed* victim, void* response, uint8_t bSpeak) {
    const bool liveWorld = (sa::GetGameState() == 9) && !sa::IsCutsceneRunning();
    const bool wasAlive  = liveWorld && sa::IsPedValid(victim) && (victim->GetHealth() > 0.0f);

    if (g_origComputeWillKill) {
        g_origComputeWillKill(calc, victim, response, bSpeak);
    }

    if (!wasAlive || !calc || !response) return;

    const auto* respBytes = reinterpret_cast<const uint8_t*>(response);
    const bool killedNow  = (respBytes[0x08] != 0) || (respBytes[0x09] != 0) || (victim->GetHealth() <= 0.0f);
    if (!killedNow) return;

    sa::CPed* playerPed = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(playerPed) || victim == playerPed) return;

    // Verify kill credit belongs to the local player (on foot OR driving a vehicle/tank/heli)
    const auto* calcBytes = reinterpret_cast<const uint8_t*>(calc);
    const auto* damager   = *reinterpret_cast<const sa::CEntity* const*>(calcBytes + 0x00);
    bool byPlayer = (damager == static_cast<const sa::CEntity*>(playerPed));
    if (!byPlayer && damager && damager->GetEntityType() == sa::ENTITY_TYPE_VEHICLE) {
        const auto* veh = reinterpret_cast<const sa::CVehicle*>(damager);
        if (sa::IsVehicleValid(veh) && veh->GetDriver() == playerPed) {
            byPlayer = true;
        }
    }
    if (!byPlayer) return;

    const uint32_t nowMs = GetTickCount();
    if (WasPedRecentlyRewarded(victim, nowMs)) return;

    int killXp = EvaluateHostileKillXp(victim);
    if (killXp <= 0) return;

    MarkPedRewarded(victim, nowMs);

    const uint32_t bodyPart = *reinterpret_cast<const uint32_t*>(calcBytes + 0x08);
    const bool isHeadshot   = (bodyPart == sa::PED_PIECE_HEAD);
    if (isHeadshot) killXp += 10;

    auto& st = Store();
    st.hostileKills++;
    if (isHeadshot) st.headshots++;

    AwardXpInternal(killXp, isHeadshot, isHeadshot ? "headshot_kill" : "hostile_kill");
}

void PollWorldActivityRewards() {
    auto& st = Store();
    StatSnapshot cur = CaptureWorldStats();
    if (!st.baseline.valid) {
        st.baseline = cur;
        return;
    }

    const uint32_t nowMs = GetTickCount();
    if (static_cast<int32_t>(nowMs - g_suppressStatsUntilMs) < 0) {
        st.baseline = cur;
        return;
    }

    const bool cheatedNow = (cur.timesCheated > st.baseline.timesCheated) || sa::IsNeverWantedCheatActive();
    const bool paroleNow  = (cur.paroleTime != st.baseline.paroleTime);

    // 1. Story / Side Missions Passed — scales higher as you progress deeper into the game!
    if (cur.missionsPassed > st.baseline.missionsPassed) {
        int delta = std::min(cur.missionsPassed - st.baseline.missionsPassed, 3);
        int sumXp = 0;
        for (int i = 1; i <= delta; ++i) {
            sumXp += ComputeMissionXpReward(st.baseline.missionsPassed + i);
        }
        st.missionsCount += delta;
        AwardXpInternal(sumXp, true, "mission_passed");
    }

    // 2. Gang Territories Captured
    if (cur.territories > st.baseline.territories) {
        int delta = std::min(cur.territories - st.baseline.territories, 3);
        st.territories += delta;
        AwardXpInternal(delta * 180, true, "territory_captured");
    }

    // 3. Unique Stunt Jumps Completed
    if (cur.stunts > st.baseline.stunts) {
        int delta = std::min(cur.stunts - st.baseline.stunts, 3);
        AwardXpInternal(delta * 120, true, "stunt_jump");
    }

    // 4. Collectibles (Spray Tags, Snapshots, Horseshoes, Oysters)
    int colDelta = 0;
    if (cur.tags       > st.baseline.tags)       colDelta += std::min(cur.tags       - st.baseline.tags,       2);
    if (cur.snapshots  > st.baseline.snapshots)  colDelta += std::min(cur.snapshots  - st.baseline.snapshots,  2);
    if (cur.horseshoes > st.baseline.horseshoes) colDelta += std::min(cur.horseshoes - st.baseline.horseshoes, 2);
    if (cur.oysters    > st.baseline.oysters)    colDelta += std::min(cur.oysters    - st.baseline.oysters,    2);
    if (colDelta > 0) {
        AwardXpInternal(colDelta * 75, true, "collectible");
    }

    // 5. Wanted Stars Evaded (strictly cheat-guarded & Pay-'n'-Spray guarded!)
    if (cur.evadedStars > st.baseline.evadedStars && !cheatedNow && !paroleNow) {
        int delta = std::min(cur.evadedStars - st.baseline.evadedStars, 6);
        st.evadedCount += delta;
        AwardXpInternal(delta * 35, false, "wanted_evaded");
    }

    // 6. Side Jobs (Vigilante bonus, Firefighter, Burglary, Trucking, Pimping)
    int sideXp = 0;
    if (cur.vigilanteKills > st.baseline.vigilanteKills) sideXp += std::min(cur.vigilanteKills - st.baseline.vigilanteKills, 5) * 15;
    if (cur.firesOut       > st.baseline.firesOut)       sideXp += std::min(cur.firesOut       - st.baseline.firesOut,       5) * 25;
    if (cur.housesBurgled  > st.baseline.housesBurgled)  sideXp += std::min(cur.housesBurgled  - st.baseline.housesBurgled,  3) * 90;
    if (cur.truckMissions  > st.baseline.truckMissions)  sideXp += std::min(cur.truckMissions  - st.baseline.truckMissions,  2) * 220;
    if (cur.girlsPimped    > st.baseline.girlsPimped)    sideXp += std::min(cur.girlsPimped    - st.baseline.girlsPimped,    3) * 50;
    if (sideXp > 0) {
        AwardXpInternal(sideXp, sideXp >= 90, "side_activity");
    }

    st.baseline = cur;
}

void UpdateHudAnimation(float dt) {
    const float targetXp = static_cast<float>(Store().totalXp);

    // Smoothly catch up display XP toward target XP (faster when gap is large, crisp eased tail)
    if (g_displayXp < targetXp) {
        float diff  = targetXp - g_displayXp;
        float speed = std::max(65.0f, diff * 4.2f);
        g_displayXp = std::min(targetXp, g_displayXp + speed * dt);
        g_hudHoldSec = std::max(g_hudHoldSec, 2.2f);
    } else if (g_displayXp > targetXp) {
        g_displayXp = targetXp;
        g_settledXp = targetXp;
    }

    // Settle the green surge bar smoothly ~0.65s after last XP gain
    const uint32_t sinceGain = GetTickCount() - g_lastGainTickMs;
    if (sinceGain > 650u && g_settledXp < g_displayXp) {
        float diff = g_displayXp - g_settledXp;
        g_settledXp = std::min(g_displayXp, g_settledXp + std::max(80.0f, diff * 3.8f) * dt);
    }

    auto dispProg = prog_rules::ComputeProgress(static_cast<int>(std::floor(g_displayXp)));
    if (dispProg.rank > g_displayRank) {
        g_displayRank = dispProg.rank;
        g_settledXp   = static_cast<float>(prog_rules::TotalXpForRank(dispProg.rank));
    }

    if (g_levelUpFlashSec > 0.0f) {
        g_levelUpFlashSec = std::max(0.0f, g_levelUpFlashSec - dt);
    }

    if (g_hudHoldSec > 0.0f) {
        g_hudHoldSec = std::max(0.0f, g_hudHoldSec - dt);
        g_hudAlpha   = std::min(1.0f, g_hudAlpha + dt * 5.2f);
    } else {
        g_hudAlpha   = std::max(0.0f, g_hudAlpha - dt * 2.4f);
        if (g_hudAlpha <= 0.01f) {
            g_displayXp = targetXp;
            g_settledXp = targetXp;
        }
    }

    for (auto it = g_popups.begin(); it != g_popups.end();) {
        it->ageSec += dt;
        if (it->ageSec >= it->duration) it = g_popups.erase(it);
        else ++it;
    }
}

void CloseMenu(bool returnToPhone) {
    bool backPhone  = returnToPhone && g_menuFromPhone;
    g_menuOpen      = false;
    g_menuFromPhone = false;
    if (backPhone) {
        ui::LockMenuInputBriefly(180);
        if (auto* b = modbus::GetBus(); b && b->phoneInstalled && b->phoneReturnToMain) {
            b->phoneReturnToMain();
        }
    } else {
        ui::TriggerPostCloseShield(160);
    }
}

void ProcessMenuInput() {
    auto in = ui::ListInput().Poll(true, true);
    const int n = static_cast<int>(prog_rules::kMilestoneCount);
    if (ui::ListInput().ApplyToCursor(g_menuCursor, n, in)) {
        sa::PlayFrontendSound(sa::SFX_SCROLL);
    }
    const auto& k = ui::Keys();
    bool phoneClose = (modbus::GetBus() && modbus::GetBus()->phoneInstalled &&
                       !integrations::sacoop::IsOverlayActive() && k.JustPressed(modbus::GetPhoneKey()));
    if (in.back || phoneClose) {
        sa::PlayFrontendSound(sa::SFX_BACK);
        CloseMenu(!phoneClose);
    }
}

void RenderUnlocksMenu() {
    if (!g_menuOpen || sa::IsPauseMenuActive()) return;

    const auto& st = Store();
    const auto  pr = st.GetProgress();

    char colR[36];
    std::snprintf(colR, sizeof(colR), "%s %d", ui::Loc("УР.", "RANK").c_str(), pr.rank);

    ui::ListMenu menu;
    menu.SetTitle(ui::Loc("УРОВЕНЬ И НАГРАДЫ", "RANK & UNLOCKS"))
        .SetColumns(ui::Loc("ОТКРЫВАЕТСЯ", "UNLOCKS"), colR)
        .SetPosition(24.0f, 72.0f, 232.0f)
        .SetMaxVisible(10)
        .SetFooter(g_menuFromPhone
                       ? ui::StandardFooter("Просмотр", "View", "Телефон", "Phone")
                       : ui::StandardFooter("Просмотр", "View", "Закрыть", "Close"))
        .SetSelected(g_menuCursor);

    for (size_t i = 0; i < prog_rules::kMilestoneCount; ++i) {
        const auto& ms = prog_rules::kMilestones[i];
        bool unlocked  = (pr.rank >= ms.rank);
        std::string right = unlocked ? ui::Loc("Открыто", "Unlocked") : ui::FormatRankLock(ms.rank);
        sa::CRGBA   col   = unlocked ? ui::ColorSuccess() : ui::ColorDisabled();
        menu.AddItem(ui::Loc(ms.rus, ms.eng), right, col, unlocked);
    }
    menu.Render();

    char rankSub[48];
    if (pr.isMaxRank) {
        std::snprintf(rankSub, sizeof(rankSub), "%s %d", ui::Loc("Макс. уровень", "Max Rank").c_str(), pr.rank);
    } else {
        std::snprintf(rankSub, sizeof(rankSub), "%s %d - %d / %d %s",
                      ui::Loc("Уровень", "Rank").c_str(), pr.rank,
                      pr.levelXp, pr.nextNeed, ui::Loc("ОП", "XP").c_str());
    }

    char noteBuf[96];
    std::snprintf(noteBuf, sizeof(noteBuf), "%s: %d, %s: %d",
                  ui::Loc("Миссии", "Missions").c_str(), st.missionsCount,
                  ui::Loc("Враги", "Hostiles").c_str(), st.hostileKills);

    int barCur = pr.isMaxRank ? 100 : pr.levelXp;
    int barMax = pr.isMaxRank ? 100 : std::max(1, pr.nextNeed);

    ui::StatsCard card;
    card.SetTitle(ui::Loc("ПРОГРЕСС", "PROGRESS"), rankSub)
        .SetAnchor(true, 0.0f, 16.0f, 18.0f)
        .SetNote(noteBuf)
        .AddBar(ui::Loc("Опыт уровня", "Rank XP"),     barCur, barCur, barMax, pr.isMaxRank ? "%" : "")
        .AddBar(ui::Loc("Все награды", "All Unlocks"), std::min(pr.rank, prog_rules::MAX_UNLOCK_RANK),
                std::min(pr.rank, prog_rules::MAX_UNLOCK_RANK), prog_rules::MAX_UNLOCK_RANK, "");
    card.Render();
}

} // namespace

void InstallHooks() {
    g_origComputeWillKill = reinterpret_cast<FnComputeWillKill>(
        mem::HookCall(0x004B5B27, reinterpret_cast<uintptr_t>(&HookedComputeWillKillPed)));
    modlog::Info("[Prog] Hooked CPedDamageResponseCalculator::ComputeWillKillPed at 0x004B5B27.");
}

void SyncBusState() {
    auto pr = Store().GetProgress();
    if (auto* b = modbus::GetBus()) {
        b->progressionInstalled  = true;
        b->playerRank            = pr.rank;
        b->playerTotalXp         = pr.totalXp;
        b->playerLevelXp         = pr.levelXp;
        b->playerNextLevelXp     = pr.nextNeed;
        b->progressionIsMenuOpen = &IsUnlocksMenuOpen;
        b->progressionOpenMenu   = &OpenUnlocksMenu;
        b->progressionPulseHud   = &PulseHudBar;
    }
    UpdatePhoneAddonBadge();
}

void OnWorldEntered(bool loadedSaveThisBoot) {
    if (!loadedSaveThisBoot) {
        if (integrations::sacoop::IsCoopClient(true)) Store().LoadFromSlot(0);
        else                                          Store().StartFreshNewGame();
    }
    auto pr = Store().GetProgress();
    g_displayXp            = static_cast<float>(pr.totalXp);
    g_settledXp            = static_cast<float>(pr.totalXp);
    g_displayRank          = pr.rank;
    g_hudAlpha             = 0.0f;
    g_hudHoldSec           = 0.0f;
    g_levelUpFlashSec      = 0.0f;
    g_popups.clear();
    g_suppressStatsUntilMs = GetTickCount() + 1400u;
    SyncBusState();
}

void OnSaveLoaded(int slot1to8) {
    g_menuOpen      = false;
    g_menuFromPhone = false;
    Store().LoadFromSlot(slot1to8);
    auto pr = Store().GetProgress();
    g_displayXp            = static_cast<float>(pr.totalXp);
    g_settledXp            = static_cast<float>(pr.totalXp);
    g_displayRank          = pr.rank;
    g_hudAlpha             = 0.0f;
    g_hudHoldSec           = 0.0f;
    g_levelUpFlashSec      = 0.0f;
    g_popups.clear();
    g_suppressStatsUntilMs = GetTickCount() + 1800u;
    SyncBusState();
}

void OnSaveWritten(int slot1to8) {
    Store().SaveToSlot(slot1to8);
    g_dirtySave = false;
    g_lastSaveTickMs = GetTickCount();
}

void OnLeftWorld() {
    g_menuOpen      = false;
    g_menuFromPhone = false;
    g_popups.clear();
    g_hudAlpha        = 0.0f;
    g_hudHoldSec      = 0.0f;
    g_levelUpFlashSec = 0.0f;
}

void PreScriptsTick() {
    const float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);

    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) {
        if (g_menuOpen) CloseMenu(false);
        return;
    }

    PollWorldActivityRewards();
    UpdateHudAnimation(dt);

    // Periodic safe persistence in SA-COOP / long free-roam sessions
    if (g_dirtySave && (GetTickCount() - g_lastSaveTickMs) >= 12000u) {
        Store().SaveNow();
        g_dirtySave = false;
        g_lastSaveTickMs = GetTickCount();
    }

    if (g_menuOpen) {
        if (modbus::IsStoryPhoneBlocking() && !g_menuFromPhone) {
            CloseMenu(false);
            return;
        }
        ProcessMenuInput();
        return;
    }

    ui::UpdatePostCloseShield();

    // Quick-peek hotkey (default F8) outside menus reveals the top Rank XP Bar smoothly
    const int peekVk = Cfg().peekKey;
    if (peekVk > 0 && peekVk < 255 &&
        integrations::sacoop::IsGameWindowFocused() &&
        !integrations::sacoop::IsOverlayActive() &&
        !modbus::IsAnyModalMenuOpen() &&
        sa::GetNumMenusInUse() == 0 &&
        ui::Keys().JustPressed(peekVk)) {
        PulseHudBar();
    }
}

void Render2D() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    RenderUnlocksMenu();

    if (g_hudAlpha <= 0.01f && g_popups.empty()) return;

    const auto dispProg = prog_rules::ComputeProgress(static_cast<int>(std::floor(g_displayXp)));
    const int  rankStartXp = prog_rules::TotalXpForRank(dispProg.rank);
    const float spanF = static_cast<float>(std::max(1, dispProg.nextNeed));

    float fillFrac = dispProg.isMaxRank ? 1.0f : std::clamp((g_displayXp - static_cast<float>(rankStartXp)) / spanF, 0.0f, 1.0f);
    float baseFrac = dispProg.isMaxRank ? 1.0f : std::clamp((g_settledXp - static_cast<float>(rankStartXp)) / spanF, 0.0f, fillFrac);

    char centerBuf[48];
    if (dispProg.isMaxRank) {
        std::snprintf(centerBuf, sizeof(centerBuf), "%d %s",
                      static_cast<int>(std::round(g_displayXp)), ui::Loc("ОП", "XP").c_str());
    } else {
        int curInRank = std::clamp(static_cast<int>(std::round(g_displayXp - static_cast<float>(rankStartXp))),
                                   0, dispProg.nextNeed);
        std::snprintf(centerBuf, sizeof(centerBuf), "%d / %d %s",
                      curInRank, dispProg.nextNeed, ui::Loc("ОП", "XP").c_str());
    }

    ui::RankBarProps props{};
    props.currentRank    = dispProg.rank;
    props.nextRank       = std::min(prog_rules::MAX_RANK, dispProg.rank + 1);
    props.isMaxRank      = dispProg.isMaxRank;
    props.baseFrac       = baseFrac;
    props.fillFrac       = fillFrac;
    props.alpha          = g_hudAlpha;
    props.slideOffsetY   = (1.0f - g_hudAlpha) * 9.0f;
    props.levelUpFlash   = (g_levelUpFlashSec > 0.0f);
    props.levelUpAlpha   = std::clamp(g_levelUpFlashSec / 1.2f, 0.0f, 1.0f);
    props.centerTextGame = centerBuf;

    if (g_levelUpFlashSec > 0.0f) {
        char capBuf[48];
        std::snprintf(capBuf, sizeof(capBuf), "%s %d",
                      ui::Loc("НОВЫЙ УРОВЕНЬ", "RANK UP").c_str(), g_levelUpShownRank);
        props.captionGame = capBuf;
    }

    ui::RankBarHUD hud;
    hud.SetProps(props);
    for (const auto& p : g_popups) {
        char pb[32];
        std::snprintf(pb, sizeof(pb), "+%d %s", p.xpAmount, ui::Loc("ОП", "XP").c_str());
        ui::RankPopupProps rp{};
        rp.textGame   = pb;
        rp.progress01 = std::clamp(p.ageSec / std::max(0.1f, p.duration), 0.0f, 1.0f);
        rp.offsetX640 = p.offsetX640;
        rp.color      = p.accentGold ? ui::ColorWarning() : ui::ColorSuccess();
        hud.AddPopup(rp);
    }
    hud.Render();
}

void OpenUnlocksMenu(bool fromPhone) {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    if (!fromPhone && modbus::IsAnyModalMenuOpen("progression")) return;
    g_menuOpen      = true;
    g_menuFromPhone = fromPhone;

    // Auto-focus the player's next upcoming unlock milestone!
    int curRank = Store().GetProgress().rank;
    g_menuCursor = 0;
    for (size_t i = 0; i < prog_rules::kMilestoneCount; ++i) {
        if (prog_rules::kMilestones[i].rank > curRank) {
            g_menuCursor = static_cast<int>(i);
            break;
        }
        g_menuCursor = static_cast<int>(i);
    }

    PulseHudBar();
    ui::LockMenuInputBriefly(180);
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

bool IsUnlocksMenuOpen() {
    return g_menuOpen;
}

void PulseHudBar() {
    auto pr = Store().GetProgress();
    if (g_hudAlpha < 0.08f) {
        g_displayXp   = static_cast<float>(pr.totalXp);
        g_settledXp   = static_cast<float>(pr.totalXp);
        g_displayRank = pr.rank;
    }
    g_hudHoldSec = std::max(g_hudHoldSec, 3.8f);
}

void Shutdown() {
    if (g_dirtySave) {
        Store().SaveNow();
        g_dirtySave = false;
    }
    OnLeftWorld();
}

} // namespace progression
