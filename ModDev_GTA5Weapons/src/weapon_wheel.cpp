#include "weapon_wheel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include "ammu_shop.hpp"
#include "config.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "weapon_data.hpp"
#include "weapon_mods.hpp"
#include "weapon_storage.hpp"

namespace wheel {

namespace {

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kPi2 = 6.28318530718530717959f;

constexpr wdata::WeaponCategory kSectorCats[8] = {
    wdata::WeaponCategory::Pistols,  // 0: 12:00
    wdata::WeaponCategory::SMGs,     // 1:  1:30
    wdata::WeaponCategory::Rifles,   // 2:  3:00
    wdata::WeaponCategory::Snipers,  // 3:  4:30
    wdata::WeaponCategory::Heavy,    // 4:  6:00
    wdata::WeaponCategory::Shotguns, // 5:  7:30
    wdata::WeaponCategory::Thrown,   // 6:  9:00
    wdata::WeaponCategory::Melee,    // 7: 10:30
};

struct SectorRuntime {
    std::vector<const wdata::WeaponDef*> owned;
    int selectedIdx = 0;
};

enum class WheelMode : uint8_t {
    Closed = 0,
    HoldTab,
    QuickScroll
};

WheelMode     g_mode              = WheelMode::Closed;
int           g_hoveredSector     = 7;
float         g_cursorVecX        = 0.0f;
float         g_cursorVecY        = -1.0f;
SectorRuntime g_sectors[8];
uint32_t      g_lastSyncMs        = 0;
uint32_t      g_quickCloseAtMs    = 0;
uint32_t      g_quickEquipAtMs    = 0;
bool          g_quickPendingEquip = false;
uint32_t      g_tabDownSinceMs    = 0;
uint32_t      g_lastPickupCollectMs = 0;
bool          g_wasWastedOrBusted = false;

using FnGetDisplayVitalStats = int16_t(__thiscall*)(void*, void*);
FnGetDisplayVitalStats g_origGetDisplayVitalStats = nullptr;

// Suppresses vanilla GTA SA's TAB Character Vital Stats box so it NEVER flashes before Weapon Wheel opens!
// Our clean UI Kit character stats card renders bottom-left whenever Weapon Wheel is held on TAB.
int16_t __fastcall HookedGetDisplayVitalStats(void* pad, void*, void* ped) {
    if (wcfg::Get().enableWeaponWheel) {
        return 0;
    }
    return g_origGetDisplayVitalStats ? g_origGetDisplayVitalStats(pad, ped) : 0;
}

// Auto-collect ground weapon pickups into our multi-weapon arsenal when CJ holds a DIFFERENT weapon
// in the same GTA SA weapon slot (fixes vanilla swap-sound loop when walking over e.g. 9mm while holding Deagle!).
void ProcessGroundWeaponPickups(sa::CPed* ped) {
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f || ped->IsInVehicle()) return;
    uint32_t nowMs = sa::GetTimeMs();
    if (nowMs - g_lastPickupCollectMs < 120) return;

    int32_t pedH = sa::GetPedHandle(ped);
    if (pedH <= 0) return;

    sa::CVector ppos = ped->GetPosition();
    sa::CPickup* pickups = sa::GetPickupsArray();
    if (!pickups) return;

    for (int i = 0; i < sa::MAX_PICKUPS; ++i) {
        auto& pk = pickups[i];
        // Collect street spawns & dead-ped drops (3=ON_STREET, 4=ONCE, 5=ONCE_TIMEOUT, 15=ON_STREET_SLOW).
        // Skip type 2 (IN_SHOP) and type 8 (ONCE_FOR_MISSION) so mission HAS_PICKUP_BEEN_COLLECTED scripts stay 100% vanilla!
        uint8_t ptype = pk.m_nPickupType;
        if (ptype != 3 && ptype != 4 && ptype != 5 && ptype != 15) continue;
        if (!pk.m_pObject || (pk.m_nFlags & 1) != 0) continue;
        if (pk.m_nModelIndex <= 0) continue;

        sa::CVector pos = pk.GetPos();
        float dx = pos.x - ppos.x;
        float dy = pos.y - ppos.y;
        float dz = std::fabs(pos.z - ppos.z);
        if (dz > 1.65f || (dx * dx + dy * dy) > (1.45f * 1.45f)) continue;

        // Match pickup model ID against our weapon catalog
        const wdata::WeaponDef* def = nullptr;
        for (size_t w = 0; w < wdata::kWeaponCount; ++w) {
            if (wdata::kWeapons[w].id != 0 && wdata::kWeapons[w].modelId == pk.m_nModelIndex) {
                def = &wdata::kWeapons[w];
                break;
            }
        }
        if (!def) continue;

        // Only step in when CJ already holds a DIFFERENT weapon in def->gtaSlot
        // (vanilla GTA SA already auto-picks up empty slots & identical weapons cleanly!)
        const sa::CWeapon& slotW = ped->Weapons()[def->gtaSlot];
        if (slotW.m_nType == 0 || slotW.m_nType == def->id) continue;

        bool alreadyOwned = wstore::Store().PlayerHasWeaponOrOwned(ped, def->id);
        int  pickupAmmo   = (pk.m_nAmmo > 0) ? static_cast<int>(pk.m_nAmmo) : def->initAmmo;
        int  addedAmmo    = 0;

        if (def->category == wdata::WeaponCategory::Melee) {
            if (alreadyOwned) continue; // Already in arsenal; leave pickup on floor
            wstore::Store().Get(def->id).owned = true;
        } else if (def->sharesSlotAmmo) {
            int curAmmo = static_cast<int>(slotW.m_nTotalAmmo);
            int newAmmo = std::min(def->maxAmmo, curAmmo + pickupAmmo);
            addedAmmo   = newAmmo - curAmmo;
            if (alreadyOwned && addedAmmo <= 0) continue; // Already owned & full ammo
            wstore::Store().Get(def->id).owned = true;
            if (addedAmmo > 0) {
                sa::ScriptCmd(0x017B).Int(pedH).Int(static_cast<int32_t>(slotW.m_nType)).Int(newAmmo).Run();
            }
        } else {
            // Heavy / Thrown individual ammo stored cleanly in arsenal!
            auto& L   = wstore::Store().Get(def->id);
            int curA  = std::max(0, L.storedAmmo);
            int newA  = std::min(def->maxAmmo, curA + pickupAmmo);
            addedAmmo = newA - curA;
            if (alreadyOwned && addedAmmo <= 0) continue;
            L.owned      = true;
            L.storedAmmo = newA;
        }

        // Remove pickup cleanly matching GTA SA's exact CPickup::Update (0x0045759E / 0x00458740) & CPickup::Remove (0x004556C0)!
        if (ptype == 3 || ptype == 15) {
            // Street spawn: destroy 3D object, set bDisabled (bit 0) so CPickup::Update holds off until regen time!
            pk.GetRidOfObjects();
            pk.m_nFlags |= 0x01;
            pk.m_nRegenerationTime = nowMs + ((ptype == 3) ? 30000u : 360000u);
        } else {
            // One-shot / dead-ped drop (4=ONCE, 5=ONCE_TIMEOUT): permanently remove object, blip, type & disable!
            pk.Remove();
        }

        wstore::Store().SaveToDisk();
        sa::PlayFrontendSound(sa::SFX_SELECT);
        g_lastPickupCollectMs = nowMs;

        char rbuf[128], ebuf[128];
        if (addedAmmo > 0) {
            std::snprintf(rbuf, sizeof(rbuf), "~g~Подобрано:~w~ %s (+%d патр.)", def->nameRus, addedAmmo);
            std::snprintf(ebuf, sizeof(ebuf), "~g~Picked up:~w~ %s (+%d ammo)", def->nameEng, addedAmmo);
        } else {
            std::snprintf(rbuf, sizeof(rbuf), "~g~Подобрано в арсенал:~w~ %s", def->nameRus);
            std::snprintf(ebuf, sizeof(ebuf), "~g~Added to arsenal:~w~ %s", def->nameEng);
        }
        ui::ShowHelp(rbuf, ebuf);
        modlog::Info("[Pickup] Collected ground weapon '%s' (id=%u, +%d ammo, slot held id=%u).",
                     def->nameEng, def->id, addedAmmo, slotW.m_nType);
        break;
    }
}

int FindSectorForCategory(wdata::WeaponCategory cat) {
    for (int i = 0; i < 8; ++i) {
        if (kSectorCats[i] == cat) return i;
    }
    return 7;
}

void EnsureOwnedIconsLoaded() {
    bool requestedAny = false;
    for (int i = 0; i < 8; ++i) {
        for (const auto* def : g_sectors[i].owned) {
            if (def->modelId > 0 && !sa::rw::GetWeaponIconTexture(def->modelId)) {
                sa::ScriptCmd(0x0247).Int(def->modelId).Run(); // REQUEST_MODEL
                requestedAny = true;
            }
        }
    }
    if (requestedAny) {
        sa::LoadAllRequestedModels(false);
    }
}

void PopulateWheelFromArsenal(const sa::CPed* ped) {
    wstore::Store().SyncFromPlayerInventory(ped);

    uint32_t activeId = 0;
    if (sa::IsPedValid(ped)) {
        if (const auto* aw = ped->GetActiveWeapon()) activeId = aw->m_nType;
    }
    const auto* activeDef = wdata::FindDef(activeId);
    g_hoveredSector = activeDef ? FindSectorForCategory(activeDef->category) : 7;

    float midAngle = -kPi * 0.5f + static_cast<float>(g_hoveredSector) * (kPi2 / 8.0f);
    g_cursorVecX = std::cos(midAngle) * 40.0f;
    g_cursorVecY = std::sin(midAngle) * 40.0f;

    for (int i = 0; i < 8; ++i) {
        auto& sr = g_sectors[i];
        sr.owned = wstore::Store().GetOwnedInCategory(ped, kSectorCats[i]);
        sr.selectedIdx = 0;

        uint32_t pref = wstore::Store().GetPreferredWeapon(kSectorCats[i]);
        if (activeDef && activeDef->category == kSectorCats[i]) {
            pref = activeId;
        }
        // Note: For Melee, pref == 0 legitimately means Unarmed (Fists), so never overwrite it with live slot!
        if (kSectorCats[i] != wdata::WeaponCategory::Melee && pref == 0 && !sr.owned.empty() && sa::IsPedValid(ped)) {
            uint8_t gtaSlot = sr.owned[0]->gtaSlot;
            uint32_t liveId = ped->Weapons()[gtaSlot].m_nType;
            if (liveId != 0) pref = liveId;
        }
        for (size_t k = 0; k < sr.owned.size(); ++k) {
            if (sr.owned[k]->id == pref) {
                sr.selectedIdx = static_cast<int>(k);
                break;
            }
        }
    }

    EnsureOwnedIconsLoaded();
}

void CycleSectorWeapon(int sectorIdx, int delta) {
    if (sectorIdx < 0 || sectorIdx >= 8) return;
    auto& sr = g_sectors[sectorIdx];
    int n = static_cast<int>(sr.owned.size());
    if (n <= 1) return;
    sr.selectedIdx = (sr.selectedIdx + delta + n) % n;
    wstore::Store().SetPreferredWeapon(kSectorCats[sectorIdx], sr.owned[sr.selectedIdx]->id);
    sa::PlayFrontendSound(sa::SFX_SCROLL);
}

void StepAllOwnedWeapons(int delta) {
    struct FlatEntry { int sec; int idx; };
    std::vector<FlatEntry> flat;
    int curFlat = -1;
    for (int s = 0; s < 8; ++s) {
        for (int k = 0; k < static_cast<int>(g_sectors[s].owned.size()); ++k) {
            if (s == g_hoveredSector && k == g_sectors[s].selectedIdx) {
                curFlat = static_cast<int>(flat.size());
            }
            flat.push_back({s, k});
        }
    }
    if (flat.empty()) return;
    if (curFlat < 0) curFlat = 0;
    int next = (curFlat + delta + static_cast<int>(flat.size())) % static_cast<int>(flat.size());
    g_hoveredSector = flat[next].sec;
    g_sectors[g_hoveredSector].selectedIdx = flat[next].idx;
    wstore::Store().SetPreferredWeapon(kSectorCats[g_hoveredSector],
                                       g_sectors[g_hoveredSector].owned[flat[next].idx]->id);
    sa::PlayFrontendSound(sa::SFX_SCROLL);
}

void CommitWheelSelection(bool playSound = true) {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;
    if (g_hoveredSector < 0 || g_hoveredSector >= 8) return;

    const auto& sr = g_sectors[g_hoveredSector];
    if (sr.owned.empty()) return;

    int idx = std::clamp(sr.selectedIdx, 0, static_cast<int>(sr.owned.size()) - 1);
    const wdata::WeaponDef* chosen = sr.owned[idx];
    if (!chosen) return;

    if (chosen->category != wdata::WeaponCategory::Melee) {
        int ammo = wstore::Store().GetWeaponAmmo(ped, chosen);
        if (ammo <= 0) {
            if (playSound) sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Нет патронов!", "Out of ammo!");
            return;
        }
    }

    wstore::Store().EquipWeaponPreservingAmmo(ped, chosen, 0, true);
    wmods::ForceRefresh(false);
    if (playSound) sa::PlayFrontendSound(sa::SFX_SELECT);
}

std::string BuildAttachmentSummary(uint32_t weaponId) {
    const auto* L = wstore::Store().Find(weaponId);
    if (!L || !L->HasAnyMod()) return "";
    std::string out;
    auto add = [&](const char* rus, const char* eng) {
        if (!out.empty()) out += " ";
        out += ui::Loc(rus, eng);
    };
    if (L->muzzle == 1)      add("Глуш.",  "Supp.");
    else if (L->muzzle == 2) add("Комп.",  "Comp.");
    if (L->mag == 1)         add("Ув.Маг", "Ext");
    else if (L->mag == 2)    add("Бараб.", "Drum");
    if (L->scope == 1)       add("Колл.",  "Dot");
    else if (L->scope == 2)  add("Опт.",   "Scope");
    if (L->grip == 1)        add("Рук.",   "Grip");
    if (L->barrel == 1)      add("Ств+",   "Hvy");
    if (L->laser != 0)       add("ЛЦУ",    "Las");
    return out;
}

} // namespace

void InstallHooks() {
    g_origGetDisplayVitalStats = reinterpret_cast<FnGetDisplayVitalStats>(
        mem::HookCall(0x0058FC24, reinterpret_cast<uintptr_t>(&HookedGetDisplayVitalStats)));
    modlog::Info("[Wheel] Hooked CHud::Draw -> CPad::GetDisplayVitalStats (0x0058FC24) to replace vanilla TAB stats box.");
}

void PreScriptsTick() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (sa::GetGameState() == 9 && !sa::IsPauseMenuActive() && !sa::IsCutsceneRunning() &&
        !ammu::IsShopActive() && !modbus::IsAnyModalMenuOpen("weapons") && sa::GetNumMenusInUse() == 0) {
        ProcessGroundWeaponPickups(ped);
    }

    if (!wcfg::Get().enableWeaponWheel) return;

    // Priority 1: Native GTA SA story phone calls ALWAYS win over Weapon Wheel TAB!
    auto* bus = modbus::GetBus();
    bool storyPhone = sa::IsStoryPhoneRinging() || (bus && bus->storyPhonePriority);
    if (storyPhone) {
        g_mode              = WheelMode::Closed;
        g_quickPendingEquip = false;
        g_tabDownSinceMs    = 0;
        return;
    }

    bool canOpen = (sa::GetGameState() == 9) &&
                   !sa::IsPauseMenuActive() &&
                   !sa::IsCutsceneRunning() &&
                   !sa::IsExternalOverlayActive() &&
                   !ammu::IsShopActive() &&
                   !modbus::IsAnyModalMenuOpen("weapons") &&
                   (sa::GetNumMenusInUse() == 0) &&
                   sa::IsPedValid(ped) &&
                   ped->GetHealth() > 0.0f &&
                   !ped->IsInVehicle();

    if (!canOpen) {
        g_mode              = WheelMode::Closed;
        g_quickPendingEquip = false;
        g_tabDownSinceMs    = 0;
        return;
    }

    auto* ms = sa::GetNewMouseState();
    const bool wheelUp   = (ms->wheelUp != 0);
    const bool wheelDown = (ms->wheelDown != 0);
    const bool tabHeld   = ui::Keys().IsDown(wcfg::Get().wheelKey);
    const uint32_t now   = sa::GetTimeMs();

    // 1. Hold TAB (>= 140ms) -> Detailed radial wheel + bottom-left CJ StatsCard + bottom-right Weapon StatsCard!
    // Quick TAB tap (< 140ms) still passes cleanly to GTA SA for story phone calls!
    if (tabHeld) {
        uint32_t tickNow = GetTickCount();
        if (g_tabDownSinceMs == 0) {
            g_tabDownSinceMs = tickNow;
        }
        if (g_mode != WheelMode::HoldTab && (tickNow - g_tabDownSinceMs) < 140) {
            return;
        }

        if (g_mode != WheelMode::HoldTab) {
            if (g_mode == WheelMode::Closed) {
                PopulateWheelFromArsenal(ped);
            }
            g_mode = WheelMode::HoldTab;
            g_quickPendingEquip = false;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }

        float dx = ms->X;
        float dy = -ms->Y;
        if (std::fabs(dx) > 0.25f || std::fabs(dy) > 0.25f) {
            g_cursorVecX += dx * 1.35f;
            g_cursorVecY += dy * 1.35f;
            float len = std::sqrt(g_cursorVecX * g_cursorVecX + g_cursorVecY * g_cursorVecY);
            if (len > 60.0f) {
                g_cursorVecX = (g_cursorVecX / len) * 60.0f;
                g_cursorVecY = (g_cursorVecY / len) * 60.0f;
            }
            if (len >= 12.0f) {
                float ang = std::atan2(g_cursorVecY, g_cursorVecX) + kPi * 0.5f + (kPi2 / 16.0f);
                while (ang < 0.0f)  ang += kPi2;
                while (ang >= kPi2) ang -= kPi2;
                int sec = static_cast<int>(ang / (kPi2 / 8.0f)) % 8;
                if (sec != g_hoveredSector) {
                    g_hoveredSector = sec;
                    sa::PlayFrontendSound(sa::SFX_SCROLL);
                }
            }
        }

        const auto& k = ui::Keys();
        if (wheelUp || k.JustPressed('E') || k.JustPressed(VK_RIGHT)) {
            CycleSectorWeapon(g_hoveredSector, +1);
        } else if (wheelDown || k.JustPressed('Q') || k.JustPressed(VK_LEFT)) {
            CycleSectorWeapon(g_hoveredSector, -1);
        }

        for (int d = 0; d < 8; ++d) {
            if (k.JustPressed('1' + d)) {
                if (g_hoveredSector == d) CycleSectorWeapon(d, +1);
                else {
                    g_hoveredSector = d;
                    float mid = -kPi * 0.5f + static_cast<float>(d) * (kPi2 / 8.0f);
                    g_cursorVecX = std::cos(mid) * 40.0f;
                    g_cursorVecY = std::sin(mid) * 40.0f;
                    sa::PlayFrontendSound(sa::SFX_SCROLL);
                }
            }
        }
        if (k.Repeated(VK_UP) || k.Repeated('W')) {
            g_hoveredSector = (g_hoveredSector + 7) % 8;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (k.Repeated(VK_DOWN) || k.Repeated('S')) {
            g_hoveredSector = (g_hoveredSector + 1) % 8;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }

        sa::ClearPadWheelInputs();
        return;
    }

    g_tabDownSinceMs = 0;

    if (g_mode == WheelMode::HoldTab) {
        CommitWheelSelection(true);
        g_mode = WheelMode::Closed;
        return;
    }

    // 2. Quick Mouse Wheel Scroll -> opens compact wheel & steps cleanly through ALL owned weapons!
    if (wheelUp || wheelDown) {
        if (g_mode == WheelMode::Closed) {
            PopulateWheelFromArsenal(ped);
        }
        g_mode = WheelMode::QuickScroll;
        StepAllOwnedWeapons(wheelUp ? +1 : -1);
        g_quickEquipAtMs    = now + 240;
        g_quickCloseAtMs    = now + 1200;
        g_quickPendingEquip = true;

        ms->wheelUp   = 0;
        ms->wheelDown = 0;
        uint8_t* pad  = sa::GetPad0();
        *reinterpret_cast<int16_t*>(pad + 0x0A) = 0;
        *reinterpret_cast<int16_t*>(pad + 0x0E) = 0;
        return;
    }

    if (g_mode == WheelMode::QuickScroll) {
        if (g_quickPendingEquip && (now >= g_quickEquipAtMs || ms->lmb || ms->rmb)) {
            CommitWheelSelection(false);
            g_quickPendingEquip = false;
            if (ms->lmb || ms->rmb) {
                g_mode = WheelMode::Closed;
                return;
            }
        }
        if (now >= g_quickCloseAtMs) {
            g_mode = WheelMode::Closed;
        }
    }
}

void PostScriptsTick() {
    if (sa::GetGameState() != 9 || sa::IsPauseMenuActive()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (sa::IsPedValid(ped)) {
        uint8_t pstate = sa::GetPlayerState(0);
        if (pstate == 1 || pstate == 2 || ped->GetHealth() <= 0.0f) {
            g_wasWastedOrBusted = true;
        } else if (g_wasWastedOrBusted && pstate == 0 && ped->GetHealth() > 0.0f) {
            // CJ just respawned at hospital / police station!
            // If GTA SA stripped live weapons (slots 2..8 all empty -> no girlfriend keep-weapons perk),
            // also zero stored ammo on Heavy/Thrown weapons (RPG, Grenades, etc.) while keeping owned = true!
            uint32_t liveAmmo = 0;
            const sa::CWeapon* w = ped->Weapons();
            for (int s = 2; s <= 8; ++s) {
                liveAmmo += w[s].m_nTotalAmmo;
            }
            if (liveAmmo == 0) {
                wstore::Store().ClearAllStoredAmmo();
                wstore::Store().SaveToDisk();
                modlog::Info("[Wheel] Respawned after Wasted/Busted (weapons stripped) -> cleared stored Heavy/Thrown ammo.");
            }
            g_wasWastedOrBusted = false;
        }
    }

    uint32_t now = sa::GetTimeMs();
    if (now - g_lastSyncMs >= 500) {
        g_lastSyncMs = now;
        wstore::Store().SyncFromPlayerInventory(ped);
    }
}

void Render2D() {
    if (g_mode == WheelMode::Closed || sa::IsPauseMenuActive()) return;

    const sa::CPed* ped = sa::FindPlayerPed(0);
    uint32_t liveWeaponId = 0;
    if (sa::IsPedValid(ped)) {
        if (const auto* aw = ped->GetActiveWeapon()) liveWeaponId = aw->m_nType;
    }

    // Populate dumb ui::RadialWheel component from ModDev_Common/ui_kit.hpp!
    ui::RadialWheel wheelUi;
    wheelUi.SetHovered(g_hoveredSector);

    for (int i = 0; i < 8; ++i) {
        const auto& sr = g_sectors[i];
        ui::WheelSectorProps props{};
        props.hasItems = !sr.owned.empty();
        if (props.hasItems) {
            for (const auto* d : sr.owned) {
                if (d->id == liveWeaponId) { props.isEquipped = true; break; }
            }
            int idx = std::clamp(sr.selectedIdx, 0, static_cast<int>(sr.owned.size()) - 1);
            const wdata::WeaponDef* def = sr.owned[idx];
            props.iconTex  = sa::rw::GetWeaponIconTexture(def->modelId);
            props.subIndex = idx;
            props.subCount = static_cast<int>(sr.owned.size());
            if (def->category != wdata::WeaponCategory::Melee) {
                char abuf[20];
                std::snprintf(abuf, sizeof(abuf), "%d", wstore::Store().GetWeaponAmmo(ped, def));
                props.ammoGame = abuf;
            }
        }
        wheelUi.SetSector(i, props);
    }

    const auto& hov = g_sectors[g_hoveredSector];
    const wdata::WeaponDef* hovDef = hov.owned.empty()
        ? nullptr
        : hov.owned[std::clamp(hov.selectedIdx, 0, static_cast<int>(hov.owned.size()) - 1)];

    if (hovDef) {
        std::string ammoStr;
        if (hovDef->category != wdata::WeaponCategory::Melee) {
            char abuf[20];
            std::snprintf(abuf, sizeof(abuf), "%d", wstore::Store().GetWeaponAmmo(ped, hovDef));
            ammoStr = abuf;
        }
        wheelUi.SetCenter(ui::Loc(hovDef->nameRus, hovDef->nameEng), ammoStr);
    } else {
        wheelUi.SetCenter("", "");
    }

    wheelUi.Render();

    // Bottom-Left CJ Character StatsCard (replaces vanilla TAB stats box cleanly while holding TAB!)
    if (g_mode == WheelMode::HoldTab && sa::IsPedValid(ped)) {
        auto statPct = [](uint16_t sid) -> int {
            return std::clamp(static_cast<int>(std::round(sa::stats::Get(sid) * 0.1f)), 0, 100);
        };
        int maxHp   = std::max(100, static_cast<int>(std::round(ped->GetMaxHealth())));
        int hp      = std::clamp(static_cast<int>(std::round(ped->GetHealth())), 0, maxHp);
        int arm     = std::clamp(static_cast<int>(std::round(ped->GetArmor())), 0, 100);
        int respect = statPct(sa::stats::STAT_RESPECT);
        int stamina = statPct(sa::stats::STAT_STAMINA);
        int muscle  = statPct(sa::stats::STAT_MUSCLE);
        int fat     = statPct(sa::stats::STAT_FAT);
        int sexy    = statPct(sa::stats::STAT_SEX_APPEAL);

        ui::StatsCard cjCard;
        cjCard.SetTitle(ui::Loc("ПЕРСОНАЖ", "CJ STATS"))
              .SetAnchorBottomLeft(154.0f, 16.0f, 18.0f)
              .AddBar(ui::Loc("Здоровье",     "Health"),     hp,      hp,      maxHp, "")
              .AddBar(ui::Loc("Броня",        "Armor"),      arm,     arm,     100,   "%")
              .AddBar(ui::Loc("Уважение",     "Respect"),    respect, respect, 100,   "%")
              .AddBar(ui::Loc("Выносливость", "Stamina"),    stamina, stamina, 100,   "%")
              .AddBar(ui::Loc("Мускулы",      "Muscle"),     muscle,  muscle,  100,   "%")
              .AddBar(ui::Loc("Жир",          "Fat"),        fat,     fat,     100,   "%")
              .AddBar(ui::Loc("Привлекат.",   "Sex Appeal"), sexy,    sexy,    100,   "%");
        cjCard.Render();
    }

    // Populate dumb ui::StatsCard component (bottom-right) for hovered firearm!
    if (hovDef && hovDef->category != wdata::WeaponCategory::Melee) {
        wdata::WeaponLoadout curL{};
        if (const auto* f = wstore::Store().Find(hovDef->id)) curL = *f;
        auto st = wmods::ComputeStats(hovDef->id, curL);

        ui::StatsCard card;
        std::string mUnit = ui::Loc("м", "m");
        card.SetTitle(ui::Loc("СТАТЫ", "STATS"), ui::Loc(hovDef->nameRus, hovDef->nameEng))
            .SetAnchor(true, 148.0f, 16.0f, 18.0f)
            .SetNote(BuildAttachmentSummary(hovDef->id))
            .AddBar(ui::Loc("Урон",      "Damage"),   st.damage,   st.damage,   120)
            .AddBar(ui::Loc("Темп",      "FireRate"), st.fireRate, st.fireRate, 120)
            .AddBar(ui::Loc("Точность",  "Accuracy"), st.accuracy, st.accuracy, 120)
            .AddBar(ui::Loc("Дальность", "Range"),    st.range,    st.range,    140, mUnit)
            .AddBar(ui::Loc("Обойма",    "Clip"),     st.clipSize, st.clipSize, std::max(30, st.clipSize));
        card.Render();
    }
}

bool IsWheelOpen() {
    return g_mode != WheelMode::Closed;
}

void Reset() {
    g_mode              = WheelMode::Closed;
    g_quickPendingEquip = false;
    g_tabDownSinceMs    = 0;
    g_wasWastedOrBusted = false;
}

} // namespace wheel
