#include "phone_ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "clothes_shop.hpp"
#include "common_utils.hpp"
#include "integrations/bass_audio.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "phone_config.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "wardrobe.hpp"

namespace phoneui {

namespace {

enum class Screen : uint8_t {
    Closed = 0,
    Home,
    Contacts,
    WardrobeMenu,
    WardrobeSlots,
    WardrobeSlotItems,
    OutfitPresets,
    OutfitPresetActions,
    Settings,
    CallingAnimation
};

struct PhoneItem {
    int         actionId;
    int         arg;
    std::string leftGame;
    std::string rightGame;
    sa::CRGBA   rightCol = {204, 204, 208, 255};
    bool        enabled  = true;
};

Screen                 g_screen       = Screen::Closed;
int                    g_cursor       = 0;
int                    g_activeSlot   = 0;
int                    g_activePreset = 0;
std::vector<PhoneItem> g_items;
bool                   g_itemsDirty   = true;

std::string g_callingNameGame;
int         g_pendingCallAction = 0;
int         g_pendingCallArg    = 0;
uint32_t    g_callFinishTickMs  = 0;

float g_trackHp         = 100.0f;
float g_trackArmor      = 0.0f;
bool  g_phoneAnimActive      = false;
bool  g_pendingStopPhoneAnim = false;
bool  g_cellModelLoaded      = false;

uint64_t g_wantedClearReadyMs      = 0;
uint64_t g_medicReadyMs            = 0;

int32_t  g_prevWantedStars         = 0;
bool     g_prevOnMission           = false;
bool     g_localClearedWanted      = false;
int      g_prevTimesCheated        = -1;
bool     g_prevNeverWanted         = false;
int      g_prevEvadedStars         = -1;
uint32_t g_prevParoleTime          = 0;
int32_t  g_prevInterior            = 0;
uint64_t g_ignoreWantedDropUntilMs = 0;
int      g_saveSlot                = 0;

using FnSetHelpMsg  = void(__cdecl*)(const char*, bool, bool, bool);
using FnCheatWanted = void(__thiscall*)(void*, int32_t);
using FnClearParole = void(__thiscall*)(void*);

FnSetHelpMsg  g_origCheatHelpMsg   = nullptr;
FnCheatWanted g_origNotWantedCheat = nullptr;
FnCheatWanted g_origWantedDown     = nullptr;
FnClearParole g_origPayNSprayClear = nullptr;

void SuppressCoopCooldownDetection(uint64_t durationMs) {
    const uint64_t tgt = GetTickCount64() + durationMs;
    if (tgt > g_ignoreWantedDropUntilMs) {
        g_ignoreWantedDropUntilMs = tgt;
    }
}

void __cdecl HookedCheatHelpMsg(const char* text, bool quick, bool permanent, bool addToBrief) {
    SuppressCoopCooldownDetection(4500);
    if (g_origCheatHelpMsg) g_origCheatHelpMsg(text, quick, permanent, addToBrief);
}

void __fastcall HookedNotWantedCheat(void* self, void*, int32_t level) {
    SuppressCoopCooldownDetection(4500);
    if (g_origNotWantedCheat) g_origNotWantedCheat(self, level);
}

void __fastcall HookedWantedDownCheat(void* self, void*, int32_t level) {
    SuppressCoopCooldownDetection(4500);
    if (g_origWantedDown) g_origWantedDown(self, level);
}

void __fastcall HookedPayNSprayClear(void* self, void*) {
    SuppressCoopCooldownDetection(4500);
    if (g_origPayNSprayClear) g_origPayNSprayClear(self);
}

bool IsActiveHelpCheatMessage() {
    const char* help = sa::GetActiveHelpTextRaw();
    if (!help || !help[0]) return false;
    static constexpr const char* kCheatKeys[] = {"CHEAT1", "CHEAT8"};
    for (const char* key : kCheatKeys) {
        const char* gxt = sa::TextGet(key);
        if (gxt && gxt[0]) {
            const size_t cmpLen = std::min<size_t>(std::strlen(gxt), 18);
            if (cmpLen >= 6 && std::strncmp(help, gxt, cmpLen) == 0) return true;
        }
    }
    return false;
}

bass::HSAMPLE g_sndKeyBeep      = 0;
bass::HSAMPLE g_sndRingtones[4] = {0, 0, 0, 0};
bool          g_audioInited     = false;

void EnsurePhoneAudio() {
    if (g_audioInited) return;
    g_audioInited = true;
    if (!bass::EnsureLoaded()) return;

    constexpr float kTwoPi = 6.28318530f;
    constexpr int   kRate  = 22050;

    {
        std::vector<int16_t> pcm(kRate * 48 / 1000);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            float env = std::exp(-t * 48.0f);
            float s   = (std::sin(kTwoPi * 941.0f * t) + std::sin(kTwoPi * 1336.0f * t)) * 0.24f * env;
            pcm[i]    = bass::FloatToPcm16(s * 0.80f);
        }
        g_sndKeyBeep = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    {
        std::vector<int16_t> pcm(kRate * 65 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            float env = std::min(1.0f, t * 25.0f) * std::min(1.0f, (0.65f - t) * 25.0f);
            float s   = (std::sin(kTwoPi * 440.0f * t) + std::sin(kTwoPi * 480.0f * t)) * 0.22f * env;
            pcm[i]    = bass::FloatToPcm16(s * 0.74f);
        }
        g_sndRingtones[0] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    {
        std::vector<int16_t> pcm(kRate * 62 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            bool  alt = (static_cast<int>(t * 16.0f) % 2) == 0;
            float f   = alt ? 1568.0f : 1975.0f;
            float env = (t < 0.26f || (t > 0.34f && t < 0.60f)) ? 1.0f : 0.0f;
            float raw = std::sin(kTwoPi * f * t) + 0.33f * std::sin(kTwoPi * f * 3.0f * t);
            pcm[i]    = bass::FloatToPcm16(raw * 0.16f * env);
        }
        g_sndRingtones[1] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    {
        constexpr float notes[4] = {1318.5f, 1174.7f, 740.0f, 830.6f};
        std::vector<int16_t> pcm(kRate * 68 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            int   idx = std::clamp(static_cast<int>(t / 0.165f), 0, 3);
            float lt  = t - idx * 0.165f;
            float env = std::min(1.0f, lt * 60.0f) * std::exp(-lt * 8.0f);
            float f   = notes[idx];
            float raw = std::sin(kTwoPi * f * t) + 0.28f * std::sin(kTwoPi * f * 2.0f * t);
            pcm[i]    = bass::FloatToPcm16(raw * 0.18f * env);
        }
        g_sndRingtones[2] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    {
        std::vector<int16_t> pcm(kRate * 58 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t  = static_cast<float>(i) / static_cast<float>(kRate);
            bool  on = (t < 0.16f) || (t > 0.24f && t < 0.42f);
            float s  = on ? (std::sin(kTwoPi * 660.0f * t) * 0.15f + std::sin(kTwoPi * 220.0f * t) * 0.12f) : 0.0f;
            pcm[i]   = bass::FloatToPcm16(s);
        }
        g_sndRingtones[3] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }
}

void PlayKeySound() {
    const auto& cfg = pcfg::Get();
    if (cfg.silentMode || !cfg.enableKeyBeeps) return;
    EnsurePhoneAudio();
    if (g_sndKeyBeep) bass::PlaySample(g_sndKeyBeep, 0.34f, 22050.0f);
    else              sa::PlayFrontendSound(sa::SFX_SCROLL);
}

void PlayDialSound(int ringtoneOverride = -1) {
    const auto& cfg = pcfg::Get();
    if (cfg.silentMode && ringtoneOverride < 0) return;
    EnsurePhoneAudio();
    int id = std::clamp((ringtoneOverride >= 0) ? ringtoneOverride : cfg.ringtoneId, 0, 3);
    if (g_sndRingtones[id]) bass::PlaySample(g_sndRingtones[id], 0.44f, 22050.0f);
    else                    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void PlayBackSound() {
    if (pcfg::Get().silentMode) return;
    sa::PlayFrontendSound(sa::SFX_BACK);
}

// Strict multi-layer check: NEVER run 0x0729 (TASK_USE_MOBILE_PHONE) inside or while entering/exiting ANY vehicle!
// Calling 0x0729 during car walk-up/entry or inside a car freezes CJ permanently in the seat.
bool CanUseHandsetAnim(const sa::CPed* ped, bool requireIdlePrimaryTasks = false) {
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 1.0f || ped->IsInWater()) return false;
    if (ped->IsInVehicle() || sa::FindPlayerVehicle(0, true) != nullptr) return false;
    if (ped->GetPedState() != sa::PEDSTATE_IDLE) return false;
    if (requireIdlePrimaryTasks && ped->HasActivePrimaryTask()) return false;
    int32_t pedH = sa::GetPedHandle(ped);
    if (pedH <= 0) return false;
    if (sa::ScriptCmd(0x00DF).Int(pedH).Run()) return false; // IS_CHAR_IN_ANY_CAR
    return true;
}

void StartPhoneAnimIfOnFoot() {
    if (!pcfg::Get().enablePhoneAnim || g_phoneAnimActive) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!CanUseHandsetAnim(ped, true)) return;
    int32_t pedH = sa::GetPedHandle(ped);
    if (pedH <= 0) return;

    if (!g_cellModelLoaded) {
        sa::ScriptCmd(0x0247).Int(330).Run();
        sa::LoadAllRequestedModels(true);
        g_cellModelLoaded = true;
    }
    sa::ScriptCmd(0x0729).Int(pedH).Int(1).Run();
    g_phoneAnimActive      = true;
    g_pendingStopPhoneAnim = false;
}

void StopPhoneAnim() {
    if (g_phoneAnimActive) {
        if (sa::IsPauseMenuActive()) {
            // Defer putting handset away until the first live unpaused frame so 0x0729 is NEVER dropped!
            g_pendingStopPhoneAnim = true;
            return;
        }
        sa::CPed* ped = sa::FindPlayerPed(0);
        int32_t pedH  = sa::GetPedHandle(ped);
        if (pedH > 0) {
            sa::ScriptCmd(0x0729).Int(pedH).Int(0).Run();
        }
        g_phoneAnimActive      = false;
        g_pendingStopPhoneAnim = false;
    }
    if (g_cellModelLoaded) {
        sa::ScriptCmd(0x0249).Int(330).Run();
        g_cellModelLoaded = false;
    }
}

void ClosePhone(bool immediateCombatInterrupt) {
    StopPhoneAnim();
    g_screen     = Screen::Closed;
    g_itemsDirty = true;
    if (!immediateCombatInterrupt) {
        ui::TriggerPostCloseShield(160);
    }
}

void OpenPhoneToHome() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (sa::IsPedValid(ped)) {
        g_trackHp    = ped->GetHealth();
        g_trackArmor = ped->GetArmor();
    }
    g_screen     = Screen::Home;
    g_cursor     = 0;
    g_itemsDirty = true;
    ui::LockMenuInputBriefly(180);
    StartPhoneAnimIfOnFoot();
    PlayKeySound();
}

int RemainingSeconds(uint64_t readyMs) {
    uint64_t now = GetTickCount64();
    if (now >= readyMs) return 0;
    return static_cast<int>((readyMs - now + 999) / 1000);
}

std::string FormatCountdown(int sec) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60, sec % 60);
    return std::string(buf);
}

std::string FormatClockStr() {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u", sa::GetClockHours(), sa::GetClockMinutes());
    return std::string(buf);
}

std::string GetCarrierName90s() {
    std::string name;
    if (integrations::sacoop::IsInMultiplayer()) {
        name = "SA-COOP";
    } else {
        sa::CPed* ped = sa::FindPlayerPed(0);
        if (sa::IsPedValid(ped)) {
            sa::CVector p = ped->GetPosition();
            if (p.x < -900.0f && p.y > -600.0f)      name = ui::Loc("СЕТЬ СФ", "SF CELL");
            else if (p.y > 550.0f && p.x > 600.0f)   name = ui::Loc("СЕТЬ ЛВ", "LV CELL");
            else if (p.x > -100.0f && p.y < 100.0f)  name = ui::Loc("СЕТЬ ЛС", "LS CELL");
            else                                     name = ui::Loc("СЕТЬ СА", "SA CELL");
        } else {
            name = ui::Loc("СЕТЬ СА", "SA CELL");
        }
    }
    if (pcfg::Get().silentMode) name += " [X]";
    return name;
}

const char* ThemeNameRus(int id) {
    switch (id) {
        case 1:  return "Nokia 92";
        case 2:  return "Янтарный";
        case 3:  return "Синий ЖК";
        default: return "Классика";
    }
}
const char* ThemeNameEng(int id) {
    switch (id) {
        case 1:  return "Nokia 92";
        case 2:  return "Amber 90s";
        case 3:  return "Night Blue";
        default: return "Classic SA";
    }
}

const char* RingtoneNameRus(int id) {
    switch (id) {
        case 1:  return "Ретро-писк";
        case 2:  return "Мелодия 92";
        case 3:  return "Пейджер";
        default: return "Стандарт";
    }
}
const char* RingtoneNameEng(int id) {
    switch (id) {
        case 1:  return "Retro Chirp";
        case 2:  return "Melody 92";
        case 3:  return "Pager Buzz";
        default: return "Classic US";
    }
}

std::string GetCooldownSavePath(int slot, bool forLoading = false) {
    const char* ov = integrations::sacoop::GetProfileOverride();
    if (forLoading) {
        return integrations::sacoop::ResolveExistingLoadPath("GTA5PhoneSaves", pcfg::Get().modDir, "phone", slot, ov);
    }
    return integrations::sacoop::BuildSaveProfilePath("GTA5PhoneSaves", pcfg::Get().modDir, "phone", slot, ov);
}

void PersistCooldownsNow() {
    std::string path = GetCooldownSavePath(g_saveSlot);
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "[Cooldowns]\nWantedClearRemSec=%d\nMedicRemSec=%d\n",
                 RemainingSeconds(g_wantedClearReadyMs),
                 RemainingSeconds(g_medicReadyMs));
    std::fclose(f);
}

void TriggerCoopPlayerList() {
    if (!integrations::sacoop::IsInMultiplayer()) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~y~Вы не в сетевой сессии SA-COOP.", "~y~Not in an active SA-COOP session.");
        return;
    }

    int vk = pcfg::Get().coopPlayerListKey;
    if (vk <= 0 || vk >= 255) vk = VK_F2;
    ClosePhone(false);

    if (integrations::sacoop::TriggerPlayerListKey(vk)) {
        ui::ShowHelp("~g~Список игроков открыт.", "~g~Player list opened.");
    }
}

void ExecuteWantedClearCall() {
    int rem = RemainingSeconds(g_wantedClearReadyMs);
    if (rem > 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        char rb[128], eb[128];
        std::snprintf(rb, sizeof(rb), "~r~Старый знакомый занят.~w~ Попробуйте через ~y~%s~w~.", FormatCountdown(rem).c_str());
        std::snprintf(eb, sizeof(eb), "~r~Old Acquaintance is busy.~w~ Try again in ~y~%s~w~.", FormatCountdown(rem).c_str());
        ui::ShowHelp(rb, eb);
        return;
    }

    int32_t curStars = sa::wanted::GetLevel(0);
    if (curStars <= 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("У вас нет звёзд розыска.", "You have no wanted level.");
        return;
    }

    int maxRem   = pcfg::Get().wantedClearMaxStars;
    int toRemove = std::min<int>(curStars, maxRem);
    int cost     = toRemove * pcfg::Get().wantedClearCostPerStar;

    if (cost > 0 && sa::GetPlayerMoney() < cost) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        char rb[128], eb[128];
        std::snprintf(rb, sizeof(rb), "~r~Нужно $%d для подкупа полиции.", cost);
        std::snprintf(eb, sizeof(eb), "~r~Need $%d to bribe the cops.", cost);
        ui::ShowHelp(rb, eb);
        return;
    }

    if (cost > 0) sa::AddPlayerMoney(-cost);
    int32_t nextStars = std::max<int32_t>(0, curStars - toRemove);
    g_localClearedWanted = true;
    sa::wanted::SetLevel(nextStars, 0);
    g_prevWantedStars    = nextStars;
    g_wantedClearReadyMs = GetTickCount64() + static_cast<uint64_t>(pcfg::Get().wantedClearCooldownSec) * 1000ull;
    PersistCooldownsNow();

    sa::PlayFrontendSound(sa::SFX_BUY);
    std::string cdStr = FormatCountdown(pcfg::Get().wantedClearCooldownSec);
    char rb[160], eb[160];
    std::snprintf(rb, sizeof(rb), "~g~Старый знакомый уладил дело.~w~ Снято звёзд: ~g~%d~w~. Повтор через %s.", toRemove, cdStr.c_str());
    std::snprintf(eb, sizeof(eb), "~g~Old Acquaintance handled it.~w~ Cleared ~g~%d~w~. Next call in %s.", toRemove, cdStr.c_str());
    ui::ShowHelp(rb, eb);
    modlog::Info("[Phone] Old Acquaintance removed %d wanted stars (%d -> %d).", toRemove, curStars, nextStars);
}

void ExecuteMedicCall() {
    int rem = RemainingSeconds(g_medicReadyMs);
    if (rem > 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        char rb[128], eb[128];
        std::snprintf(rb, sizeof(rb), "~r~Скорая занята.~w~ Ожидайте ~y~%s~w~.", FormatCountdown(rem).c_str());
        std::snprintf(eb, sizeof(eb), "~r~Paramedics busy.~w~ Wait ~y~%s~w~.", FormatCountdown(rem).c_str());
        ui::ShowHelp(rb, eb);
        return;
    }

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;
    float maxHp = std::max(100.0f, ped->GetMaxHealth());
    if (ped->GetHealth() >= maxHp - 1.0f) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Здоровье уже полное.", "Health is already full.");
        return;
    }

    int cost = pcfg::Get().medicHealCost;
    if (cost > 0 && sa::GetPlayerMoney() < cost) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Недостаточно денег.", "~r~Not enough money.");
        return;
    }

    if (cost > 0) sa::AddPlayerMoney(-cost);
    ped->SetHealth(maxHp);
    g_trackHp      = maxHp;
    g_medicReadyMs = GetTickCount64() + static_cast<uint64_t>(pcfg::Get().medicCooldownSec) * 1000ull;
    PersistCooldownsNow();
    sa::PlayFrontendSound(sa::SFX_BUY);
    ui::ShowHelp("~g~Экстренная помощь оказана.~w~ Здоровье восстановлено.",
                 "~g~Emergency medical aid applied.~w~ Health restored.");
}

void StartCall(const std::string& contactNameGame, int actionId, int arg = 0) {
    g_callingNameGame   = contactNameGame;
    g_pendingCallAction = actionId;
    g_pendingCallArg    = arg;
    g_callFinishTickMs  = GetTickCount() + 650;
    g_screen            = Screen::CallingAnimation;
    g_itemsDirty        = true;
    PlayDialSound();
}

void FinishPendingCall() {
    auto* bus = modbus::GetBus();
    int act   = g_pendingCallAction;
    int arg   = g_pendingCallArg;
    ClosePhone(false);
    ui::LockMenuInputBriefly(200);

    switch (act) {
        case 1:
            if (bus && bus->garageInstalled && bus->garageOpenMechanic) bus->garageOpenMechanic(true);
            break;
        case 2:
            if (bus && bus->garageInstalled && bus->garageOpenDealership) bus->garageOpenDealership(true);
            break;
        case 3:
            if (bus && bus->garageInstalled && bus->garageOpenInsurance) bus->garageOpenInsurance(true);
            break;
        case 4:
            ExecuteWantedClearCall();
            break;
        case 5:
            ExecuteMedicCall();
            break;
        case 99:
            if (bus && arg >= 0 && arg < bus->phoneAddonCount && bus->phoneAddons[arg].onActivate) {
                bus->phoneAddons[arg].onActivate(true);
            }
            break;
        default:
            break;
    }
}

void RebuildCurrentItems() {
    g_items.clear();
    g_itemsDirty = false;
    auto* bus = modbus::GetBus();

    if (g_screen == Screen::Home) {
        g_items.push_back({10, 0, ui::Loc("Контакты", "Contacts"), "->", ui::ColorNormal(), true});

        if (bus && bus->inventoryInstalled && bus->inventoryOpenMenu) {
            int food = bus->inventoryGetTotalFood ? bus->inventoryGetTotalFood() : 0;
            int arm  = 0;
            if (bus->inventoryGetArmorCount) {
                for (int t = 0; t < 4; ++t) arm += bus->inventoryGetArmorCount(t);
            }
            char b[32];
            std::snprintf(b, sizeof(b), "%d / %d", food, arm);
            g_items.push_back({13, 0, ui::Loc("Еда и броня", "Snacks & Armor"), b, ui::ColorSuccess(), true});
        }

        g_items.push_back({14, 0, ui::Loc("Гардероб", "Wardrobe"), "->", ui::ColorNormal(), true});

        if (bus && bus->garageInstalled && bus->garageOpenDealership) {
            int cnt = bus->garageGetOwnedCount ? bus->garageGetOwnedCount() : 0;
            char b[24];
            std::snprintf(b, sizeof(b), "%d/10", cnt);
            g_items.push_back({12, 0, ui::Loc("Автосалон", "Dealership"), b, ui::ColorSuccess(), true});
        }

        if (bus && bus->weaponsInstalled && bus->weaponsGetOwnedCount) {
            char wb[32];
            std::snprintf(wb, sizeof(wb), "%d", bus->weaponsGetOwnedCount());
            g_items.push_back({16, 0, ui::Loc("Арсенал", "Arsenal"), wb, ui::ColorWarning(), true});
        }

        bool coopOn = integrations::sacoop::IsInMultiplayer();
        g_items.push_back({15, 0, ui::Loc("Игроки SA-COOP", "SA-COOP Players"),
                           coopOn ? ui::Loc("В сети", "Online") : ui::Loc("Не в сети", "Offline"),
                           coopOn ? ui::ColorSuccess() : ui::ColorDisabled(), true});

        if (bus) {
            for (int i = 0; i < bus->phoneAddonCount; ++i) {
                const auto& a = bus->phoneAddons[i];
                if (a.section == 0 || a.section == 2) {
                    g_items.push_back({98, i, ui::Loc(a.labelRus, a.labelEng),
                                       ui::Loc(a.badgeRus, a.badgeEng), ui::ColorWarning(), true});
                }
            }
        }

        g_items.push_back({17, 0, ui::Loc("Настройки", "Settings"), "->", ui::ColorMuted(), true});
    } else if (g_screen == Screen::Contacts) {
        if (bus && bus->garageInstalled && bus->garageOpenMechanic) {
            int cnt = bus->garageGetOwnedCount ? bus->garageGetOwnedCount() : 0;
            char mb[24];
            std::snprintf(mb, sizeof(mb), "%d/10", cnt);
            g_items.push_back({1, 0, ui::Loc("Механик", "Mechanic"), mb, ui::ColorSuccess(), true});
        }

        int remW      = RemainingSeconds(g_wantedClearReadyMs);
        int32_t stars = sa::wanted::GetLevel(0);
        std::string wBadge;
        sa::CRGBA wCol;
        if (remW > 0) {
            wBadge = FormatCountdown(remW);
            wCol   = ui::ColorDanger();
        } else if (stars > 0) {
            int rm = std::min<int>(stars, pcfg::Get().wantedClearMaxStars);
            char wb[32];
            std::snprintf(wb, sizeof(wb), "-%d* $%d", rm, rm * pcfg::Get().wantedClearCostPerStar);
            wBadge = wb;
            wCol   = ui::ColorSuccess();
        } else {
            wBadge = ui::Loc("Готов", "Ready");
            wCol   = ui::ColorMuted();
        }
        g_items.push_back({4, 0, ui::Loc("Старый знакомый", "Old Acquaintance"), wBadge, wCol, true});

        if (bus && bus->garageInstalled && bus->garageOpenInsurance) {
            int wrecked = bus->garageGetWreckedCount ? bus->garageGetWreckedCount() : 0;
            char ib[24];
            if (wrecked > 0) std::snprintf(ib, sizeof(ib), "%d %s", wrecked, ui::Loc("уничт.", "wreck").c_str());
            else             std::snprintf(ib, sizeof(ib), "%s", ui::Loc("В норме", "OK").c_str());
            g_items.push_back({3, 0, ui::Loc("Страховая", "Mors Mutual"), ib,
                               wrecked > 0 ? ui::ColorDanger() : ui::ColorMuted(), true});
        }

        int remM = RemainingSeconds(g_medicReadyMs);
        std::string mBadge = (remM > 0) ? FormatCountdown(remM)
                                        : common::FormatPrice(pcfg::Get().medicHealCost, ui::UseRussianFlag());
        g_items.push_back({5, 0, ui::Loc("Скорая помощь", "Paramedic"), mBadge,
                           (remM > 0) ? ui::ColorDanger() : ui::ColorSuccess(), true});

        if (bus) {
            for (int i = 0; i < bus->phoneAddonCount; ++i) {
                const auto& a = bus->phoneAddons[i];
                if (a.section == 1) {
                    g_items.push_back({99, i, ui::Loc(a.labelRus, a.labelEng),
                                       ui::Loc(a.badgeRus, a.badgeEng), ui::ColorWarning(), true});
                }
            }
        }
    } else if (g_screen == Screen::WardrobeMenu) {
        g_items.push_back({20, 0, ui::Loc("Одежда по частям", "Change by Piece"), "->", ui::ColorNormal(), true});
        g_items.push_back({21, 0, ui::Loc("Наборы одежды", "Outfit Presets"), "->", ui::ColorSuccess(), true});
    } else if (g_screen == Screen::WardrobeSlots) {
        const auto& cat = wardrobe::Catalog();
        for (int s = 0; s < wardrobe::CLOTH_SLOT_COUNT; ++s) {
            uint32_t bp = wardrobe::kSlotBodyParts[s];
            int own = 0;
            for (size_t i = 0; i < cat.size(); ++i) {
                if (cat[i].bodyPart == bp && wardrobe::IsClothOwned(static_cast<int>(i))) ++own;
            }
            char cb[20];
            std::snprintf(cb, sizeof(cb), "%d", own);
            g_items.push_back({30, s, ui::Loc(wardrobe::SlotNameRus(s), wardrobe::SlotNameEng(s)),
                               cb, (own > 0) ? ui::ColorSuccess() : ui::ColorDisabled(), true});
        }
    } else if (g_screen == Screen::WardrobeSlotItems) {
        const auto& cat = wardrobe::Catalog();
        sa::CPed* ped   = sa::FindPlayerPed(0);
        uint32_t bp     = wardrobe::kSlotBodyParts[std::clamp(g_activeSlot, 0, wardrobe::CLOTH_SLOT_COUNT - 1)];

        g_items.push_back({31, -1, ui::Loc("Снять / Стандарт", "Remove / Default"), "", ui::ColorMuted(), true});

        for (size_t i = 0; i < cat.size(); ++i) {
            if (cat[i].bodyPart == bp && wardrobe::IsClothOwned(static_cast<int>(i))) {
                int idx = static_cast<int>(i);
                bool worn = wardrobe::IsClothWorn(ped, idx);
                g_items.push_back({32, idx, wardrobe::GetClothDisplayNameGame(cat[i]),
                                   worn ? ui::Loc("Надето", "Worn") : "",
                                   ui::ColorSuccess(), true});
            }
        }
    } else if (g_screen == Screen::OutfitPresets) {
        const auto& presets = wardrobe::Presets();
        for (int i = 0; i < wardrobe::MAX_OUTFIT_PRESETS; ++i) {
            char lb[48];
            if (presets[i].used) {
                std::snprintf(lb, sizeof(lb), "%d. %s", i + 1, ui::Loc("Набор", "Outfit").c_str());
                std::string right = ui::Loc(wardrobe::TriggerNameRus(presets[i].trigger),
                                            wardrobe::TriggerNameEng(presets[i].trigger));
                sa::CRGBA col = (presets[i].trigger != wardrobe::AutoTrigger::Manual)
                    ? ui::ColorSuccess() : ui::ColorNormal();
                g_items.push_back({40, i, lb, right, col, true});
            } else {
                std::snprintf(lb, sizeof(lb), "%d. %s", i + 1, ui::Loc("Свободно", "Empty").c_str());
                g_items.push_back({40, i, lb, ui::Loc("Сохр.", "Save"), ui::ColorDisabled(), true});
            }
        }
    } else if (g_screen == Screen::OutfitPresetActions) {
        const auto& pr = wardrobe::Presets()[std::clamp(g_activePreset, 0, wardrobe::MAX_OUTFIT_PRESETS - 1)];
        if (pr.used) {
            g_items.push_back({50, g_activePreset, ui::Loc("Надеть набор", "Equip Outfit"), "", ui::ColorSuccess(), true});
            g_items.push_back({51, g_activePreset, ui::Loc("Авто-надевание", "Auto-Equip"),
                               ui::Loc(wardrobe::TriggerNameRus(pr.trigger), wardrobe::TriggerNameEng(pr.trigger)),
                               ui::ColorWarning(), true});
            g_items.push_back({52, g_activePreset, ui::Loc("Перезаписать текущим", "Overwrite Current"), "", ui::ColorNormal(), true});
            g_items.push_back({53, g_activePreset, ui::Loc("Удалить набор", "Delete Preset"), "", ui::ColorDanger(), true});
        } else {
            g_items.push_back({52, g_activePreset, ui::Loc("Сохранить одежду", "Save Current Outfit"), "", ui::ColorSuccess(), true});
        }
    } else if (g_screen == Screen::Settings) {
        const auto& cfg = pcfg::Get();
        auto onOffStr = [](bool v) { return v ? ui::Loc("Вкл", "On") : ui::Loc("Выкл", "Off"); };
        auto onOffCol = [](bool v) -> sa::CRGBA { return v ? ui::ColorSuccess() : ui::ColorMuted(); };

        g_items.push_back({60, 0, ui::Loc("Тема экрана", "Screen Theme"),
                           ui::Loc(ThemeNameRus(cfg.phoneTheme), ThemeNameEng(cfg.phoneTheme)), ui::ColorWarning(), true});
        g_items.push_back({61, 0, ui::Loc("Мелодия звонка", "Ringtone"),
                           ui::Loc(RingtoneNameRus(cfg.ringtoneId), RingtoneNameEng(cfg.ringtoneId)), ui::ColorWarning(), true});
        g_items.push_back({62, 0, ui::Loc("Звук кнопок", "Key Beeps"),
                           onOffStr(cfg.enableKeyBeeps), onOffCol(cfg.enableKeyBeeps), true});
        g_items.push_back({63, 0, ui::Loc("Тихий режим", "Silent Mode"),
                           onOffStr(cfg.silentMode), cfg.silentMode ? ui::ColorWarning() : ui::ColorMuted(), true});
        g_items.push_back({64, 0, ui::Loc("Анимация", "Handset Anim"),
                           onOffStr(cfg.enablePhoneAnim), onOffCol(cfg.enablePhoneAnim), true});
        g_items.push_back({65, 0, ui::Loc("Авто-одежда", "Auto-Outfits"),
                           onOffStr(cfg.enableAutoOutfits), onOffCol(cfg.enableAutoOutfits), true});
    }

    if (g_cursor >= static_cast<int>(g_items.size())) {
        g_cursor = std::max(0, static_cast<int>(g_items.size()) - 1);
    }
}

void AdjustSetting(int actionId, int delta) {
    auto& cfg = pcfg::Get();
    switch (actionId) {
        case 60:
            cfg.phoneTheme = (cfg.phoneTheme + delta + 4) % 4;
            PlayKeySound();
            break;
        case 61:
            cfg.ringtoneId = (cfg.ringtoneId + delta + 4) % 4;
            PlayDialSound(cfg.ringtoneId);
            break;
        case 62:
            cfg.enableKeyBeeps = !cfg.enableKeyBeeps;
            PlayKeySound();
            break;
        case 63:
            cfg.silentMode = !cfg.silentMode;
            if (!cfg.silentMode) PlayKeySound();
            break;
        case 64:
            cfg.enablePhoneAnim = !cfg.enablePhoneAnim;
            if (cfg.enablePhoneAnim) StartPhoneAnimIfOnFoot();
            else                     StopPhoneAnim();
            PlayKeySound();
            break;
        case 65:
            cfg.enableAutoOutfits = !cfg.enableAutoOutfits;
            PlayKeySound();
            break;
        default:
            return;
    }
    pcfg::SaveSettings();
    g_itemsDirty = true;
}

void HandleSelect() {
    if (g_cursor < 0 || g_cursor >= static_cast<int>(g_items.size())) return;
    const auto item = g_items[g_cursor];
    auto* bus       = modbus::GetBus();
    sa::CPed* ped   = sa::FindPlayerPed(0);

    if (g_screen == Screen::Home) {
        switch (item.actionId) {
            case 10: g_screen = Screen::Contacts;     g_cursor = 0; g_itemsDirty = true; PlayKeySound(); break;
            case 12: ClosePhone(false); ui::LockMenuInputBriefly(200); if (bus && bus->garageOpenDealership) bus->garageOpenDealership(true); break;
            case 13: ClosePhone(false); ui::LockMenuInputBriefly(200); if (bus && bus->inventoryOpenMenu)    bus->inventoryOpenMenu(true, 0); break;
            case 14: g_screen = Screen::WardrobeMenu; g_cursor = 0; g_itemsDirty = true; PlayKeySound(); break;
            case 15: TriggerCoopPlayerList(); break;
            case 16:
                if (bus && bus->weaponsGetSummary) {
                    char buf[160] = {};
                    bus->weaponsGetSummary(buf, sizeof(buf));
                    if (buf[0]) {
                        sa::SetHelpMessage(ui::Loc(buf, buf).c_str(), true, false, false);
                    }
                    PlayKeySound();
                }
                break;
            case 17: g_screen = Screen::Settings; g_cursor = 0; g_itemsDirty = true; PlayKeySound(); break;
            case 98:
                ClosePhone(false);
                ui::LockMenuInputBriefly(200);
                if (bus && item.arg >= 0 && item.arg < bus->phoneAddonCount && bus->phoneAddons[item.arg].onActivate) {
                    bus->phoneAddons[item.arg].onActivate(true);
                }
                break;
        }
        return;
    }

    if (g_screen == Screen::Contacts) {
        StartCall(item.leftGame, item.actionId, item.arg);
        return;
    }

    if (g_screen == Screen::WardrobeMenu) {
        if (item.actionId == 20)      { g_screen = Screen::WardrobeSlots; g_cursor = 0; g_itemsDirty = true; PlayKeySound(); }
        else if (item.actionId == 21) { g_screen = Screen::OutfitPresets; g_cursor = 0; g_itemsDirty = true; PlayKeySound(); }
        return;
    }

    if (g_screen == Screen::WardrobeSlots) {
        g_activeSlot = item.arg;
        g_screen     = Screen::WardrobeSlotItems;
        g_cursor     = 0;
        g_itemsDirty = true;
        PlayKeySound();
        return;
    }

    if (g_screen == Screen::WardrobeSlotItems) {
        if (item.actionId == 31) {
            wardrobe::ClearClothSlot(ped, g_activeSlot, false);
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else if (item.actionId == 32) {
            wardrobe::ApplyClothItem(ped, item.arg, false);
            sa::PlayFrontendSound(sa::SFX_SELECT);
        }
        g_itemsDirty = true;
        return;
    }

    if (g_screen == Screen::OutfitPresets) {
        g_activePreset = item.arg;
        g_screen       = Screen::OutfitPresetActions;
        g_cursor       = 0;
        g_itemsDirty   = true;
        PlayKeySound();
        return;
    }

    if (g_screen == Screen::OutfitPresetActions) {
        if (item.actionId == 50) {
            wardrobe::EquipPreset(ped, item.arg, true);
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else if (item.actionId == 51) {
            wardrobe::CyclePresetTrigger(item.arg);
            g_itemsDirty = true;
            PlayKeySound();
        } else if (item.actionId == 52) {
            if (wardrobe::SaveCurrentToPreset(ped, item.arg)) {
                g_itemsDirty = true;
                sa::PlayFrontendSound(sa::SFX_BUY);
                ui::ShowHelp("~g~Текущая одежда сохранена.", "~g~Current outfit saved.");
            }
        } else if (item.actionId == 53) {
            wardrobe::DeletePreset(item.arg);
            g_screen     = Screen::OutfitPresets;
            g_cursor     = item.arg;
            g_itemsDirty = true;
            PlayBackSound();
        }
        return;
    }

    if (g_screen == Screen::Settings) {
        AdjustSetting(item.actionId, +1);
    }
}

void HandleBack() {
    PlayBackSound();
    g_itemsDirty = true;
    switch (g_screen) {
        case Screen::Home:
            ClosePhone(false);
            break;
        case Screen::Contacts:
        case Screen::WardrobeMenu:
        case Screen::Settings:
            g_screen = Screen::Home;
            g_cursor = 0;
            break;
        case Screen::WardrobeSlots:
        case Screen::OutfitPresets:
            g_screen = Screen::WardrobeMenu;
            g_cursor = 0;
            break;
        case Screen::WardrobeSlotItems:
            g_screen = Screen::WardrobeSlots;
            g_cursor = g_activeSlot;
            break;
        case Screen::OutfitPresetActions:
            g_screen = Screen::OutfitPresets;
            g_cursor = g_activePreset;
            break;
        case Screen::CallingAnimation:
            g_screen = Screen::Contacts;
            break;
        default:
            ClosePhone(false);
            break;
    }
}

// Monitor SA-COOP shared wanted-clear cooldown.
// Strictly ignores cheats (AEZAKMI, ASNAEB, CHEAT1/8 toast, STAT_TIMES_CHEATED), Pay 'n' Spray
// (ClearWantedLevelAndGoOnParole hook, parole timestamp/stars, evaded stars stat, respray garage state),
// Wasted/Busted, missions, interiors, cutscenes, disabled controls (+0x10E), and local clears via grace window!
void MonitorCoopSharedCooldown() {
    if (!integrations::sacoop::IsInMultiplayer()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    const int32_t  curStars         = sa::wanted::GetLevel(0);
    const bool     onMission        = sa::IsOnMission();
    const int      curCheats        = static_cast<int>(sa::stats::Get(sa::stats::STAT_TIMES_CHEATED));
    const bool     neverWanted      = sa::IsNeverWantedCheatActive();
    const int      curEvaded        = static_cast<int>(sa::stats::Get(sa::stats::STAT_WANTED_STARS_EVADED));
    const uint32_t paroleTime       = sa::wanted::GetTimeOfParole(0);
    const int32_t  paroleStars      = sa::wanted::GetStarsBeforeParole(0);
    const bool     resprayBusy      = sa::garages::IsAnyResprayGarageBusy();
    const bool     controlsDisabled = sa::IsPlayerControlsDisabled();
    const uint8_t  playerState      = sa::GetPlayerState(0);
    const int32_t  interior         = sa::GetActiveInterior();
    const bool     cutscene         = sa::IsCutsceneRunning();
    const bool     cheatToast       = IsActiveHelpCheatMessage();

    if (g_prevTimesCheated < 0 || g_prevEvadedStars < 0) {
        g_prevTimesCheated = curCheats;
        g_prevNeverWanted  = neverWanted;
        g_prevEvadedStars  = curEvaded;
        g_prevParoleTime   = paroleTime;
        g_prevWantedStars  = curStars;
        g_prevOnMission    = onMission;
        g_prevInterior     = interior;
        return;
    }

    if (g_localClearedWanted) {
        g_localClearedWanted = false;
        SuppressCoopCooldownDetection(2500);
    }

    // Any cheat activity (AEZAKMI, ASNAEB, typed cheat toast, or SA-COOP delayed cheat sync)
    if (curCheats != g_prevTimesCheated || neverWanted != g_prevNeverWanted || neverWanted || cheatToast) {
        SuppressCoopCooldownDetection(4500);
    }

    // Any Pay 'n' Spray or natural wanted evasion (parole timestamp/stars, evaded stat, active respray garage)
    const bool recentParole = (paroleTime != 0) &&
                              (static_cast<int32_t>(sa::GetTimeMs() - paroleTime) >= 0) &&
                              (static_cast<int32_t>(sa::GetTimeMs() - paroleTime) < 4500);
    if (curEvaded != g_prevEvadedStars || paroleTime != g_prevParoleTime || recentParole || paroleStars > 0 || resprayBusy) {
        SuppressCoopCooldownDetection(4500);
    }

    // Wasted / Busted recovery
    if (playerState != 0 || ped->GetHealth() <= 1.0f) {
        SuppressCoopCooldownDetection(5000);
    }

    // Missions, interiors, cutscenes, or scripted player control locks (CPad +0x10E)
    if (onMission || onMission != g_prevOnMission ||
        interior != 0 || interior != g_prevInterior ||
        cutscene || controlsDisabled)
    {
        SuppressCoopCooldownDetection(3000);
    }

    if (GetTickCount64() >= g_ignoreWantedDropUntilMs &&
        g_prevWantedStars >= 2 && curStars < g_prevWantedStars)
    {
        const int drop = g_prevWantedStars - curStars;
        if (drop >= 2 && drop <= pcfg::Get().wantedClearMaxStars && RemainingSeconds(g_wantedClearReadyMs) == 0) {
            g_wantedClearReadyMs = GetTickCount64() + static_cast<uint64_t>(pcfg::Get().wantedClearCooldownSec) * 1000ull;
            PersistCooldownsNow();
            std::string cdStr = FormatCountdown(pcfg::Get().wantedClearCooldownSec);
            char rb[140], eb[140];
            std::snprintf(rb, sizeof(rb), "~y~Старый знакомый помог напарнику.~w~ Ожидание %s.", cdStr.c_str());
            std::snprintf(eb, sizeof(eb), "~y~Old Acquaintance used in SA-COOP.~w~ Cooldown %s.", cdStr.c_str());
            ui::ShowHelp(rb, eb);
            modlog::Info("[Phone/Coop] Detected shared wanted drop (%d -> %d), synced cooldown.", g_prevWantedStars, curStars);
        }
    }

    g_prevTimesCheated = curCheats;
    g_prevNeverWanted  = neverWanted;
    g_prevEvadedStars  = curEvaded;
    g_prevParoleTime   = paroleTime;
    g_prevWantedStars  = curStars;
    g_prevOnMission    = onMission;
    g_prevInterior     = interior;
}

} // namespace

void InstallHooks() {
    // Verified in gta_sa.exe 1.0 US:
    //   0x00438553: CCheat::AddToCheatString -> CHud::SetHelpMessage (ALL typed cheats!)
    //   0x004396CE: CCheat::NotWantedCheat (AEZAKMI) -> CPlayerPed::CheatWantedLevel(0)
    //   0x00438F2E: CCheat::WantedLevelDownCheat (ASNAEB) -> CPlayerPed::CheatWantedLevel(0)
    //   0x0044AF10: CGarage::Update (Pay 'n' Spray) -> CWanted::ClearWantedLevelAndGoOnParole()
    g_origCheatHelpMsg   = reinterpret_cast<FnSetHelpMsg>(
        mem::HookCall(0x00438553, reinterpret_cast<uintptr_t>(&HookedCheatHelpMsg)));
    g_origNotWantedCheat = reinterpret_cast<FnCheatWanted>(
        mem::HookCall(0x004396CE, reinterpret_cast<uintptr_t>(&HookedNotWantedCheat)));
    g_origWantedDown     = reinterpret_cast<FnCheatWanted>(
        mem::HookCall(0x00438F2E, reinterpret_cast<uintptr_t>(&HookedWantedDownCheat)));
    g_origPayNSprayClear = reinterpret_cast<FnClearParole>(
        mem::HookCall(0x0044AF10, reinterpret_cast<uintptr_t>(&HookedPayNSprayClear)));
    modlog::Info("[Phone] Installed cheat (AEZAKMI/ASNAEB) & Pay 'n' Spray cooldown-guard hooks.");
}

void PreScriptsTick() {
    if (sa::GetGameState() != 9) return;

    auto* bus = modbus::GetBus();
    bool storyPhone = sa::IsStoryPhoneRinging() || (bus && bus->storyPhonePriority);
    if (storyPhone) {
        if (g_screen != Screen::Closed) ClosePhone(true);
        return;
    }

    // Keep phone state & handset animation intact while Pause Menu is open (ESC is native GTA SA Pause Menu!)
    if (sa::IsPauseMenuActive()) return;

    if (g_pendingStopPhoneAnim) {
        StopPhoneAnim();
    }

    if (sa::IsCutsceneRunning() || clothesshop::IsShopActive()) {
        if (g_screen != Screen::Closed) ClosePhone(true);
        return;
    }

    const auto& k = ui::Keys();

    if (!integrations::sacoop::IsOverlayActive() && k.JustPressed(pcfg::Get().phoneKey)) {
        if (g_screen != Screen::Closed) {
            PlayBackSound();
            ClosePhone(false);
            sa::ClearPadModalInputs(false);
            return;
        }
        if (!modbus::IsAnyModalMenuOpen("phone") && sa::GetNumMenusInUse() == 0) {
            sa::CPed* ped = sa::FindPlayerPed(0);
            if (sa::IsPedValid(ped) && ped->GetHealth() > 1.0f && !ped->IsInWater()) {
                OpenPhoneToHome();
                sa::ClearPadModalInputs(false);
            }
            return;
        }
    }

    if (g_screen == Screen::Closed) {
        ui::UpdatePostCloseShield();
        return;
    }

    if (modbus::IsAnyModalMenuOpen("phone") || sa::GetNumMenusInUse() > 0) {
        ClosePhone(true);
        return;
    }

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f || ped->IsInWater()) {
        ClosePhone(true);
        return;
    }

    const float curHp  = ped->GetHealth();
    const float curArm = ped->GetArmor();
    if (curHp < g_trackHp - 0.5f || curArm < g_trackArmor - 0.5f) {
        ClosePhone(true);
        ui::ShowHelp("~r~Вас атакуют.~w~ Телефон убран.", "~r~Under attack.~w~ Phone put away.");
        return;
    }
    g_trackHp    = curHp;
    g_trackArmor = curArm;

    if (g_phoneAnimActive && !CanUseHandsetAnim(ped)) {
        StopPhoneAnim();
    }

    auto in = ui::ListInput().Poll(true);

    if (g_screen == Screen::CallingAnimation) {
        if (in.back) {
            HandleBack();
        } else if (static_cast<int32_t>(GetTickCount() - g_callFinishTickMs) >= 0) {
            FinishPendingCall();
        }
        return;
    }

    // Contacts/Home refresh once per tick for live countdowns/counts; wardrobe screens rebuild only when dirty
    if (g_itemsDirty || g_screen == Screen::Home || g_screen == Screen::Contacts) {
        RebuildCurrentItems();
    }
    const int n = static_cast<int>(g_items.size());
    if (n > 0) {
        if (ui::ListInput().ApplyToCursor(g_cursor, n, in)) {
            PlayKeySound();
        } else if (g_screen == Screen::Settings && (in.left || in.right)) {
            AdjustSetting(g_items[g_cursor].actionId, in.right ? +1 : -1);
        } else if (in.accept) {
            HandleSelect();
        }
    }
    if (in.back) {
        HandleBack();
    }
}

void PostScriptsTick() {
    if (sa::GetGameState() != 9) return;
    MonitorCoopSharedCooldown();
}

void Render2D() {
    if (g_screen == Screen::Closed || sa::IsPauseMenuActive()) return;

    ui::PhoneFrame phone;
    phone.SetTheme(static_cast<ui::PhoneTheme>(std::clamp(pcfg::Get().phoneTheme, 0, 3)));
    phone.SetStatusLine(GetCarrierName90s(), FormatClockStr());

    if (g_screen == Screen::CallingAnimation) {
        phone.SetHeader(ui::Loc("ВЫЗОВ...", "CALLING..."), g_callingNameGame)
             .SetBanner(ui::Loc("Соединение...", "Dialing..."), ui::ColorSuccess())
             .SetFooter(ui::Loc("ПКМ/Backsp - Сброс", "RMB/Backsp - Hang Up"))
             .AddItem(ui::Loc("Идёт гудок...", "Ringing..."), "...", ui::ColorNormal(), true)
             .SetSelected(0);
        phone.Render();
        return;
    }

    if (g_itemsDirty) {
        RebuildCurrentItems();
    }

    switch (g_screen) {
        case Screen::Home: {
            std::string homeSub = ui::Loc("Главное меню", "Main Menu");
            if (auto* bus = modbus::GetBus(); bus && bus->progressionInstalled) {
                char rb[64];
                if (bus->playerNextLevelXp > 0) {
                    std::snprintf(rb, sizeof(rb), "%s %d - %d/%d %s",
                                  ui::Loc("Ур.", "Rank").c_str(), bus->playerRank,
                                  bus->playerLevelXp, bus->playerNextLevelXp,
                                  ui::Loc("ОП", "XP").c_str());
                } else {
                    std::snprintf(rb, sizeof(rb), "%s %d",
                                  ui::Loc("Макс. ур.", "Max Rank").c_str(), bus->playerRank);
                }
                homeSub = rb;
            } else if (integrations::sacoop::IsInMultiplayer()) {
                homeSub = ui::Loc("Профиль: ", "Profile: ") + integrations::sacoop::GetPlayerProfileName();
            }
            phone.SetHeader(ui::Loc("ТЕЛЕФОН", "MOBILE"), homeSub)
                 .SetFooter(ui::StandardFooter("Откр.", "Open", "Убрать", "Close"));
            break;
        }
        case Screen::Contacts:
            phone.SetHeader(ui::Loc("КОНТАКТЫ", "CONTACTS"), ui::Loc("Записная книжка", "Phonebook"))
                 .SetFooter(ui::StandardFooter("Позв.", "Call", "Назад", "Back"));
            break;
        case Screen::WardrobeMenu:
            phone.SetHeader(ui::Loc("ГАРДЕРОБ", "WARDROBE"), ui::Loc("Стиль и наборы", "Style & Presets"))
                 .SetFooter(ui::StandardFooter("Выбр.", "Pick", "Назад", "Back"));
            break;
        case Screen::WardrobeSlots:
            phone.SetHeader(ui::Loc("ОДЕЖДА", "CLOTHES"), ui::Loc("Купленные вещи", "Owned Items"))
                 .SetFooter(ui::StandardFooter("Откр.", "Open", "Назад", "Back"));
            break;
        case Screen::WardrobeSlotItems:
            phone.SetHeader(ui::Loc(wardrobe::SlotNameRus(g_activeSlot), wardrobe::SlotNameEng(g_activeSlot)),
                            ui::Loc("Из гардероба", "From Wardrobe"))
                 .SetFooter(ui::StandardFooter("Надеть", "Wear", "Назад", "Back"));
            break;
        case Screen::OutfitPresets:
            phone.SetHeader(ui::Loc("НАБОРЫ ОДЕЖДЫ", "OUTFIT PRESETS"), ui::Loc("Авто-переодевание", "Auto-Equip Rules"))
                 .SetFooter(ui::StandardFooter("Настр.", "Edit", "Назад", "Back"));
            break;
        case Screen::OutfitPresetActions: {
            char sub[32];
            std::snprintf(sub, sizeof(sub), "%s #%d", ui::Loc("Набор", "Outfit").c_str(), g_activePreset + 1);
            phone.SetHeader(ui::Loc("НАСТРОЙКА НАБОРА", "OUTFIT SETTINGS"), sub)
                 .SetFooter(ui::StandardFooter("Прим.", "Apply", "Назад", "Back"));
            break;
        }
        case Screen::Settings:
            phone.SetHeader(ui::Loc("НАСТРОЙКИ", "SETTINGS"), ui::Loc("Параметры трубки", "Handset Options"))
                 .SetFooter(ui::StandardFooter("Изм.", "Change", "Назад", "Back", "A/D", "A/D"));
            break;
        default:
            break;
    }

    for (const auto& it : g_items) {
        phone.AddItem(it.leftGame, it.rightGame, it.rightCol, it.enabled);
    }
    phone.SetSelected(g_cursor);
    phone.Render();
}

bool IsOpen() { return g_screen != Screen::Closed; }
void Close()  { ClosePhone(false); }

void ReturnToMain() {
    if (sa::GetGameState() == 9 && !sa::IsPauseMenuActive() && !sa::IsCutsceneRunning()) {
        OpenPhoneToHome();
    }
}

void OnGameSave(int slot1to8) {
    if (slot1to8 >= 1 && slot1to8 <= 8) g_saveSlot = slot1to8;
    PersistCooldownsNow();
}

void OnGameLoad(int slot1to8) {
    g_saveSlot           = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
    g_wantedClearReadyMs = 0;
    g_medicReadyMs       = 0;
    g_prevWantedStars    = sa::wanted::GetLevel(0);
    g_prevOnMission      = sa::IsOnMission();
    g_prevTimesCheated   = static_cast<int>(sa::stats::Get(sa::stats::STAT_TIMES_CHEATED));
    g_prevNeverWanted    = sa::IsNeverWantedCheatActive();
    g_prevEvadedStars    = static_cast<int>(sa::stats::Get(sa::stats::STAT_WANTED_STARS_EVADED));
    g_prevParoleTime     = sa::wanted::GetTimeOfParole(0);
    g_prevInterior       = sa::GetActiveInterior();
    SuppressCoopCooldownDetection(4000);

    std::string path = GetCooldownSavePath(g_saveSlot, true);
    if (path.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    int wRem = common::IniReadInt(path.c_str(), "Cooldowns", "WantedClearRemSec", 0);
    int mRem = common::IniReadInt(path.c_str(), "Cooldowns", "MedicRemSec", 0);
    uint64_t now = GetTickCount64();
    if (wRem > 0) g_wantedClearReadyMs = now + static_cast<uint64_t>(wRem) * 1000ull;
    if (mRem > 0) g_medicReadyMs       = now + static_cast<uint64_t>(mRem) * 1000ull;
}

void OnNewGame() {
    g_saveSlot           = 0;
    g_wantedClearReadyMs = 0;
    g_medicReadyMs       = 0;
    g_prevWantedStars    = 0;
    g_prevOnMission      = false;
    g_prevTimesCheated   = -1;
    g_prevNeverWanted    = false;
    g_prevEvadedStars    = -1;
    g_prevParoleTime     = 0;
    g_prevInterior       = 0;
    SuppressCoopCooldownDetection(4000);
}

void Reset() {
    g_pendingStopPhoneAnim = false;
    StopPhoneAnim();
    g_screen           = Screen::Closed;
    g_prevWantedStars  = 0;
    g_prevOnMission    = false;
    g_prevTimesCheated = -1;
    g_prevNeverWanted  = false;
    g_prevEvadedStars  = -1;
    g_prevParoleTime   = 0;
    g_prevInterior     = 0;
    SuppressCoopCooldownDetection(4000);
    g_itemsDirty       = true;
}

void Shutdown() {
    Reset();
    bass::FreeSample(g_sndKeyBeep);
    for (int i = 0; i < 4; ++i) bass::FreeSample(g_sndRingtones[i]);
}

} // namespace phoneui
