#include "phone_ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "bass_audio.hpp"
#include "clothes_shop.hpp"
#include "log.hpp"
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
    int         actionId; // screen-specific action code
    int         arg;      // addon index / slot index / preset index
    std::string leftGame;
    std::string rightGame;
    sa::CRGBA   rightCol = {200, 200, 200, 255};
    bool        enabled  = true;
};

Screen                 g_screen       = Screen::Closed;
int                    g_cursor       = 0;
int                    g_activeSlot   = 0;
int                    g_activePreset = 0;
std::vector<PhoneItem> g_items;
std::vector<int>       g_ownedSlotItems;

// Active call animation state (brief authentic 90s dial tone before executing contact action!)
std::string g_callingNameGame;
int         g_pendingCallAction  = 0;
int         g_pendingCallArg     = 0;
uint32_t    g_callFinishTickMs   = 0;

// Combat / Damage auto-put-away tracking
float       g_trackHp                = 100.0f;
float       g_trackArmor             = 0.0f;
bool        g_phoneAnimActive        = false;
bool        g_cellModelLoaded        = false;

// Cooldowns (real-time seconds tracked via GetTickCount64!)
uint64_t g_wantedClearReadyMs = 0;
uint64_t g_medicReadyMs       = 0;

// SA-COOP shared wanted-clear cooldown tracker!
int32_t  g_prevWantedStars    = 0;
bool     g_localClearedWanted = false;
int      g_saveSlot           = 0;

// Synthesized 1990s cell phone keypad + 4 selectable retro ringtones via bass_audio.hpp
bass::HSAMPLE g_sndKeyBeep     = 0;
bass::HSAMPLE g_sndRingtones[4] = {0, 0, 0, 0};

void EnsurePhoneAudio() {
    static bool s_inited = false;
    if (s_inited) return;
    s_inited = true;
    if (!bass::EnsureLoaded()) return;

    constexpr float kTwoPi = 6.28318530f;
    constexpr int   kRate  = 22050;

    // 1. Crisp 1990s DTMF keypad beep (48ms)
    {
        std::vector<int16_t> pcm(kRate * 48 / 1000);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            float env = std::exp(-t * 48.0f);
            float s   = (std::sin(kTwoPi * 941.0f * t) + std::sin(kTwoPi * 1336.0f * t)) * 0.24f * env;
            pcm[i]    = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 26000.0f);
        }
        auto wav     = bass::BuildWav16Mono(pcm, kRate);
        g_sndKeyBeep = bass::LoadSample("", wav, 2);
    }

    // Ringtone 0: Classic US Ringback (0.65s 440Hz + 480Hz)
    {
        std::vector<int16_t> pcm(kRate * 65 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            float env = std::min(1.0f, t * 25.0f) * std::min(1.0f, (0.65f - t) * 25.0f);
            float s   = (std::sin(kTwoPi * 440.0f * t) + std::sin(kTwoPi * 480.0f * t)) * 0.22f * env;
            pcm[i]    = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 24000.0f);
        }
        g_sndRingtones[0] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    // Ringtone 1: Retro 90s Brick Phone Digital Chirp (rapid 2-tone square-ish pulse)
    {
        std::vector<int16_t> pcm(kRate * 62 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            bool  alt = (static_cast<int>(t * 16.0f) % 2) == 0;
            float f   = alt ? 1568.0f : 1975.0f;
            float env = (t < 0.26f || (t > 0.34f && t < 0.60f)) ? 1.0f : 0.0f;
            float raw = std::sin(kTwoPi * f * t) + 0.33f * std::sin(kTwoPi * f * 3.0f * t);
            float s   = raw * 0.20f * env;
            pcm[i]    = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 24000.0f);
        }
        g_sndRingtones[1] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    // Ringtone 2: 1992 Monophonic Keypad Melody (4-note ascending chime E6-D6-F#5-G#5)
    {
        constexpr float notes[4] = {1318.5f, 1174.7f, 740.0f, 830.6f};
        std::vector<int16_t> pcm(kRate * 68 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t    = static_cast<float>(i) / static_cast<float>(kRate);
            int   idx  = std::clamp(static_cast<int>(t / 0.165f), 0, 3);
            float lt   = t - idx * 0.165f;
            float env  = std::min(1.0f, lt * 60.0f) * std::exp(-lt * 8.0f);
            float f    = notes[idx];
            float raw  = std::sin(kTwoPi * f * t) + 0.28f * std::sin(kTwoPi * f * 2.0f * t);
            float s    = raw * 0.24f * env;
            pcm[i]     = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 24000.0f);
        }
        g_sndRingtones[2] = bass::LoadSample("", bass::BuildWav16Mono(pcm, kRate), 2);
    }

    // Ringtone 3: Low Tactile Pager Buzz / Double Beep
    {
        std::vector<int16_t> pcm(kRate * 58 / 100);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t   = static_cast<float>(i) / static_cast<float>(kRate);
            bool  on  = (t < 0.16f) || (t > 0.24f && t < 0.42f);
            float s   = on ? (std::sin(kTwoPi * 660.0f * t) * 0.18f + std::sin(kTwoPi * 220.0f * t) * 0.14f) : 0.0f;
            pcm[i]    = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 24000.0f);
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
    int id = (ringtoneOverride >= 0) ? ringtoneOverride : cfg.ringtoneId;
    id = std::clamp(id, 0, 3);
    if (g_sndRingtones[id]) bass::PlaySample(g_sndRingtones[id], 0.44f, 22050.0f);
    else                    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void PlayBackSound() {
    if (pcfg::Get().silentMode) return;
    sa::PlayFrontendSound(sa::SFX_BACK);
}

// ============================================================================
// Native 1990s CJ Brick Cellphone Hand Animation (TASK_USE_MOBILE_PHONE 0x0729)
// ============================================================================
void StartPhoneAnimIfOnFoot() {
    if (!pcfg::Get().enablePhoneAnim || g_phoneAnimActive) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 1.0f || ped->IsInVehicle() || ped->IsInWater()) return;
    int32_t pedH = sa::GetPedHandle(ped);
    if (pedH <= 0) return;

    // Model 330 = cellphone (authentic 1992 GTA SA brick handset!)
    if (!g_cellModelLoaded) {
        sa::ScriptCmd(0x0247).Int(330).Run(); // REQUEST_MODEL 330
        sa::LoadAllRequestedModels(true);
        g_cellModelLoaded = true;
    }
    sa::ScriptCmd(0x0729).Int(pedH).Int(1).Run(); // TASK_USE_MOBILE_PHONE(ped, TRUE)
    g_phoneAnimActive = true;
}

void StopPhoneAnim() {
    if (g_phoneAnimActive) {
        sa::CPed* ped = sa::FindPlayerPed(0);
        int32_t pedH  = sa::GetPedHandle(ped);
        if (pedH > 0) {
            sa::ScriptCmd(0x0729).Int(pedH).Int(0).Run(); // TASK_USE_MOBILE_PHONE(ped, FALSE)
        }
        g_phoneAnimActive = false;
    }
    if (g_cellModelLoaded) {
        sa::ScriptCmd(0x0249).Int(330).Run(); // MARK_MODEL_AS_NO_LONGER_NEEDED
        g_cellModelLoaded = false;
    }
}

void ClosePhone(bool immediateCombatInterrupt) {
    StopPhoneAnim();
    g_screen = Screen::Closed;
    // Brief input shield after normal close prevents releasing LMB/RMB from punching/aiming;
    // skip shield during combat interrupt so CJ can fight back immediately!
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
    g_screen = Screen::Home;
    g_cursor = 0;
    ui::LockMenuInputBriefly(180);
    ui::SyncIdleMouseEdges();
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

// Authentic 1992 San Andreas regional cellular carrier name (no modern 4G/5G!)
std::string GetCarrierName90s() {
    std::string name;
    if (sa::IsCoopModLoaded()) {
        name = "SA-COOP";
    } else {
        sa::CPed* ped = sa::FindPlayerPed(0);
        if (sa::IsPedValid(ped)) {
            sa::CVector p = ped->GetPosition();
            if (p.x < -900.0f && p.y > -600.0f)      name = "SF CELL";
            else if (p.y > 550.0f && p.x > 600.0f)   name = "LV CELL";
            else if (p.x > -100.0f && p.y < 100.0f)  name = "LS CELL";
            else                                     name = "SA CELL";
        } else {
            name = "SA CELL";
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
        default: return "GTA SA";
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

// Send configured hotkey cleanly so SA-COOP opens its native Player List!
void TriggerCoopPlayerList() {
    if (!sa::IsCoopModLoaded()) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~y~GTA SA COOP не обнаружен.~w~ В кооперативе откроет список игроков.",
                     "~y~GTA SA COOP not detected.~w~ Opens player list in SA-COOP.");
        return;
    }

    int vk = pcfg::Get().coopPlayerListKey;
    if (vk <= 0 || vk >= 255) vk = VK_F2;
    ClosePhone(false);

    INPUT inputs[2] = {};
    inputs[0].type       = INPUT_KEYBOARD;
    inputs[0].ki.wVk     = static_cast<WORD>(vk);
    inputs[1].type       = INPUT_KEYBOARD;
    inputs[1].ki.wVk     = static_cast<WORD>(vk);
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));

    ui::ShowHelp("~g~Список игроков SA-COOP открыт!", "~g~SA-COOP Player List opened!");
}

// Execute Old Acquaintance (Lester-style wanted star removal up to 3 stars, 10-min shared COOP cooldown!)
void ExecuteWantedClearCall() {
    int rem = RemainingSeconds(g_wantedClearReadyMs);
    if (rem > 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        char rb[128], eb[128];
        std::snprintf(rb, sizeof(rb), "~r~Старый знакомый занят!~w~ Попробуйте через ~y~%s~w~.", FormatCountdown(rem).c_str());
        std::snprintf(eb, sizeof(eb), "~r~Old Acquaintance is busy!~w~ Try again in ~y~%s~w~.", FormatCountdown(rem).c_str());
        ui::ShowHelp(rb, eb);
        return;
    }

    int32_t curStars = sa::wanted::GetLevel(0);
    if (curStars <= 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("У вас нет звёзд розыска!", "You have no wanted level!");
        return;
    }

    int maxRem   = pcfg::Get().wantedClearMaxStars;
    int toRemove = std::min<int>(curStars, maxRem);
    int cost     = toRemove * pcfg::Get().wantedClearCostPerStar;

    if (cost > 0 && sa::GetPlayerMoney() < cost) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        char rb[128], eb[128];
        std::snprintf(rb, sizeof(rb), "~r~Нужно $%d для подкупа полиции!", cost);
        std::snprintf(eb, sizeof(eb), "~r~Need $%d to bribe the cops!", cost);
        ui::ShowHelp(rb, eb);
        return;
    }

    if (cost > 0) sa::AddPlayerMoney(-cost);
    int32_t nextStars = std::max<int32_t>(0, curStars - toRemove);
    g_localClearedWanted = true;
    sa::wanted::SetLevel(nextStars, 0);
    g_prevWantedStars    = nextStars;
    g_wantedClearReadyMs = GetTickCount64() + static_cast<uint64_t>(pcfg::Get().wantedClearCooldownSec) * 1000ull;

    sa::PlayFrontendSound(sa::SFX_BUY);
    char rb[160], eb[160];
    std::snprintf(rb, sizeof(rb), "~g~Старый знакомый уладил дело!~w~ Снято звёзд: ~g~%d~w~ (КД 10 мин).", toRemove);
    std::snprintf(eb, sizeof(eb), "~g~Old Acquaintance handled it!~w~ Cleared ~g~%d~w~ star(s) (10m CD).", toRemove);
    ui::ShowHelp(rb, eb);
    modlog::Info("[Phone] Old Acquaintance removed %d wanted stars (%d -> %d).", toRemove, curStars, nextStars);
}

void ExecuteMedicCall() {
    int rem = RemainingSeconds(g_medicReadyMs);
    if (rem > 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        char rb[128], eb[128];
        std::snprintf(rb, sizeof(rb), "~r~Скорая занята!~w~ Ожидайте ~y~%s~w~.", FormatCountdown(rem).c_str());
        std::snprintf(eb, sizeof(eb), "~r~Paramedics busy!~w~ Wait ~y~%s~w~.", FormatCountdown(rem).c_str());
        ui::ShowHelp(rb, eb);
        return;
    }

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;
    float maxHp = std::max(100.0f, ped->GetMaxHealth());
    if (ped->GetHealth() >= maxHp - 1.0f) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Здоровье уже полное!", "Health is already full!");
        return;
    }

    int cost = pcfg::Get().medicHealCost;
    if (cost > 0 && sa::GetPlayerMoney() < cost) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
        return;
    }

    if (cost > 0) sa::AddPlayerMoney(-cost);
    ped->SetHealth(maxHp);
    g_trackHp      = maxHp;
    g_medicReadyMs = GetTickCount64() + static_cast<uint64_t>(pcfg::Get().medicCooldownSec) * 1000ull;
    sa::PlayFrontendSound(sa::SFX_BUY);
    ui::ShowHelp("~g~Экстренная помощь оказана!~w~ Здоровье восстановлено.",
                 "~g~Emergency medical aid applied!~w~ Health restored.");
}

void StartCall(const std::string& contactNameGame, int actionId, int arg = 0) {
    g_callingNameGame   = contactNameGame;
    g_pendingCallAction = actionId;
    g_pendingCallArg    = arg;
    g_callFinishTickMs  = GetTickCount() + 650;
    g_screen            = Screen::CallingAnimation;
    PlayDialSound();
}

void FinishPendingCall() {
    auto* bus = modbus::GetBus();
    int act   = g_pendingCallAction;
    int arg   = g_pendingCallArg;
    ClosePhone(false);
    ui::LockMenuInputBriefly(200);

    switch (act) {
        case 1: // Mechanic
            if (bus && bus->garageInstalled && bus->garageOpenMechanic) bus->garageOpenMechanic(true);
            break;
        case 2: // Dealership
            if (bus && bus->garageInstalled && bus->garageOpenDealership) bus->garageOpenDealership(true);
            break;
        case 3: // Insurance
            if (bus && bus->garageInstalled && bus->garageOpenInsurance) bus->garageOpenInsurance(true);
            break;
        case 4: // Old Acquaintance (Wanted Clear)
            ExecuteWantedClearCall();
            break;
        case 5: // Paramedic
            ExecuteMedicCall();
            break;
        case 99: // Dynamic Third-party Contact Addon
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
    auto* bus = modbus::GetBus();

    if (g_screen == Screen::Home) {
        // Clean, logical 1990s keypad menu order (no duplicates!):
        // 1. Contacts (Mechanic, Old Acquaintance, Insurance, Paramedic, third-party contacts)
        g_items.push_back({10, 0, ui::Loc("Контакты", "Contacts"), "->", {205, 205, 205, 255}, true});

        // 2. Pocket Food & Body Armor (if GTA5Inventory installed)
        if (bus && bus->inventoryInstalled && bus->inventoryOpenMenu) {
            int food = bus->inventoryGetTotalFood ? bus->inventoryGetTotalFood() : 0;
            int arm  = 0;
            if (bus->inventoryGetArmorCount) {
                for (int t = 0; t < 4; ++t) arm += bus->inventoryGetArmorCount(t);
            }
            char b[32];
            std::snprintf(b, sizeof(b), "%d / %d", food, arm);
            g_items.push_back({13, 0, ui::Loc("Еда и Броня", "Snacks & Armor"), b, {140, 200, 255, 255}, true});
        }

        // 3. Built-in CJ Wardrobe & Auto-Equip Outfit Presets
        g_items.push_back({14, 0, ui::Loc("Гардероб CJ", "CJ Wardrobe"), "->", {215, 215, 215, 255}, true});

        // 4. Auto Dealership Catalog (if GTA5GarageAndTuning installed)
        if (bus && bus->garageInstalled && bus->garageOpenDealership) {
            int cnt = bus->garageGetOwnedCount ? bus->garageGetOwnedCount() : 0;
            char b[24];
            std::snprintf(b, sizeof(b), "%d/10", cnt);
            g_items.push_back({12, 0, ui::Loc("Автосалон", "Auto Dealership"), b, {135, 225, 145, 255}, true});
        }

        // 5. SA-COOP Player List
        bool coopOn = sa::IsCoopModLoaded();
        g_items.push_back({15, 0, ui::Loc("Игроки SA-COOP", "SA-COOP Players"),
                           coopOn ? ui::Loc("Онлайн", "Online") : ui::Loc("Оффлайн", "Offline"),
                           coopOn ? sa::CRGBA{120, 230, 130, 255} : sa::CRGBA{150, 150, 150, 255}, true});

        // 6. Dynamically registered third-party Main Menu / Quick addons
        if (bus) {
            for (int i = 0; i < bus->phoneAddonCount; ++i) {
                const auto& a = bus->phoneAddons[i];
                if (a.section == 0 || a.section == 2) {
                    g_items.push_back({98, i, ui::Loc(a.labelRus, a.labelEng),
                                       ui::Loc(a.badgeRus, a.badgeEng), {180, 220, 255, 255}, true});
                }
            }
        }

        // 7. 1990s Phone Settings (always neatly at bottom!)
        g_items.push_back({17, 0, ui::Loc("Настройки", "Settings"), "->", {190, 190, 190, 255}, true});
    } else if (g_screen == Screen::Contacts) {
        // 1. Mechanic (most frequently called contact, right at the top!)
        if (bus && bus->garageInstalled && bus->garageOpenMechanic) {
            int cnt = bus->garageGetOwnedCount ? bus->garageGetOwnedCount() : 0;
            char mb[24];
            std::snprintf(mb, sizeof(mb), "%d/10", cnt);
            g_items.push_back({1, 0, ui::Loc("Механик", "Mechanic"), mb, {125, 235, 135, 255}, true});
        }

        // 2. Old Acquaintance (Wanted Star Removal, 10-min shared COOP cooldown!)
        int remW      = RemainingSeconds(g_wantedClearReadyMs);
        int32_t stars = sa::wanted::GetLevel(0);
        std::string wBadge;
        sa::CRGBA wCol;
        if (remW > 0) {
            wBadge = FormatCountdown(remW);
            wCol   = {235, 110, 100, 255};
        } else if (stars > 0) {
            int rm = std::min<int>(stars, pcfg::Get().wantedClearMaxStars);
            char wb[32];
            std::snprintf(wb, sizeof(wb), "-%d* $%d", rm, rm * pcfg::Get().wantedClearCostPerStar);
            wBadge = wb;
            wCol   = {120, 235, 135, 255};
        } else {
            wBadge = ui::Loc("Готов", "Ready");
            wCol   = {170, 170, 170, 255};
        }
        g_items.push_back({4, 0, ui::Loc("Старый знакомый", "Old Acquaintance"), wBadge, wCol, true});

        // 3. Mors Mutual Insurance (if Garage installed)
        if (bus && bus->garageInstalled && bus->garageOpenInsurance) {
            int wrecked = bus->garageGetWreckedCount ? bus->garageGetWreckedCount() : 0;
            char ib[24];
            if (wrecked > 0) std::snprintf(ib, sizeof(ib), "%d %s", wrecked, ui::Loc("уничт.", "wreck").c_str());
            else             std::snprintf(ib, sizeof(ib), "%s", ui::Loc("Ок", "OK").c_str());
            g_items.push_back({3, 0, ui::Loc("Страховая", "Mors Mutual"), ib,
                               wrecked > 0 ? sa::CRGBA{240, 110, 100, 255} : sa::CRGBA{150, 150, 150, 255}, true});
        }

        // 4. Paramedic (Built-in Emergency Medkit)
        int remM = RemainingSeconds(g_medicReadyMs);
        std::string mBadge = (remM > 0) ? FormatCountdown(remM) : ui::FormatPrice(pcfg::Get().medicHealCost);
        g_items.push_back({5, 0, ui::Loc("Скорая помощь", "Paramedic"), mBadge,
                           (remM > 0) ? sa::CRGBA{235, 110, 100, 255} : sa::CRGBA{135, 230, 145, 255}, true});

        // 5. Dynamic Third-party Contacts!
        if (bus) {
            for (int i = 0; i < bus->phoneAddonCount; ++i) {
                const auto& a = bus->phoneAddons[i];
                if (a.section == 1) {
                    g_items.push_back({99, i, ui::Loc(a.labelRus, a.labelEng),
                                       ui::Loc(a.badgeRus, a.badgeEng), {180, 220, 255, 255}, true});
                }
            }
        }
    } else if (g_screen == Screen::WardrobeMenu) {
        g_items.push_back({20, 0, ui::Loc("Переодеться по частям", "Change by Piece"), "->", {205, 205, 205, 255}, true});
        g_items.push_back({21, 0, ui::Loc("Наборы одежды (Авто)", "Outfit Presets (Auto)"), "->", {135, 230, 145, 255}, true});
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
                               cb, (own > 0) ? sa::CRGBA{135, 225, 145, 255} : sa::CRGBA{130, 130, 130, 255}, true});
        }
    } else if (g_screen == Screen::WardrobeSlotItems) {
        g_ownedSlotItems.clear();
        const auto& cat = wardrobe::Catalog();
        sa::CPed* ped   = sa::FindPlayerPed(0);
        uint32_t bp     = wardrobe::kSlotBodyParts[std::clamp(g_activeSlot, 0, wardrobe::CLOTH_SLOT_COUNT - 1)];

        g_items.push_back({31, -1, ui::Loc("Снять / Стандарт", "Remove / Default"), "", {190, 190, 190, 255}, true});

        for (size_t i = 0; i < cat.size(); ++i) {
            if (cat[i].bodyPart == bp && wardrobe::IsClothOwned(static_cast<int>(i))) {
                int idx = static_cast<int>(i);
                g_ownedSlotItems.push_back(idx);
                bool worn = wardrobe::IsClothWorn(ped, idx);
                g_items.push_back({32, idx, wardrobe::GetClothDisplayNameGame(cat[i]),
                                   worn ? ui::Loc("Надето", "Worn") : "",
                                   {120, 235, 130, 255}, true});
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
                    ? sa::CRGBA{125, 235, 135, 255} : sa::CRGBA{195, 195, 195, 255};
                g_items.push_back({40, i, lb, right, col, true});
            } else {
                std::snprintf(lb, sizeof(lb), "%d. %s", i + 1, ui::Loc("Пустой слот", "Empty Slot").c_str());
                g_items.push_back({40, i, lb, ui::Loc("Сохр.", "Save"), {130, 130, 130, 255}, true});
            }
        }
    } else if (g_screen == Screen::OutfitPresetActions) {
        const auto& pr = wardrobe::Presets()[std::clamp(g_activePreset, 0, wardrobe::MAX_OUTFIT_PRESETS - 1)];
        if (pr.used) {
            g_items.push_back({50, g_activePreset, ui::Loc("Надеть набор", "Equip Outfit"), "", {125, 235, 135, 255}, true});
            g_items.push_back({51, g_activePreset, ui::Loc("Авто-надевание", "Auto-Equip"),
                               ui::Loc(wardrobe::TriggerNameRus(pr.trigger), wardrobe::TriggerNameEng(pr.trigger)),
                               {140, 205, 255, 255}, true});
            g_items.push_back({52, g_activePreset, ui::Loc("Перезаписать текущим", "Overwrite w/ Current"), "", {210, 210, 210, 255}, true});
            g_items.push_back({53, g_activePreset, ui::Loc("Удалить набор", "Delete Preset"), "", {235, 100, 100, 255}, true});
        } else {
            g_items.push_back({52, g_activePreset, ui::Loc("Сохранить текущую одежду", "Save Current Outfit"), "", {125, 235, 135, 255}, true});
        }
    } else if (g_screen == Screen::Settings) {
        const auto& cfg = pcfg::Get();
        auto onOffStr = [](bool v) { return v ? ui::Loc("Вкл", "On") : ui::Loc("Выкл", "Off"); };
        auto onOffCol = [](bool v) -> sa::CRGBA { return v ? sa::CRGBA{125, 235, 135, 255} : sa::CRGBA{165, 165, 165, 255}; };

        g_items.push_back({60, 0, ui::Loc("Экран (Тема)", "LCD Theme"),
                           ui::Loc(ThemeNameRus(cfg.phoneTheme), ThemeNameEng(cfg.phoneTheme)), {140, 210, 255, 255}, true});
        g_items.push_back({61, 0, ui::Loc("Мелодия звонка", "Ringtone"),
                           ui::Loc(RingtoneNameRus(cfg.ringtoneId), RingtoneNameEng(cfg.ringtoneId)), {140, 210, 255, 255}, true});
        g_items.push_back({62, 0, ui::Loc("Звук кнопок", "Key Beeps"),
                           onOffStr(cfg.enableKeyBeeps), onOffCol(cfg.enableKeyBeeps), true});
        g_items.push_back({63, 0, ui::Loc("Тихий режим", "Silent Mode"),
                           onOffStr(cfg.silentMode), cfg.silentMode ? sa::CRGBA{235, 180, 95, 255} : sa::CRGBA{165, 165, 165, 255}, true});
        g_items.push_back({64, 0, ui::Loc("Анимация CJ", "CJ Phone Anim"),
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
        case 60: // LCD Theme
            cfg.phoneTheme = (cfg.phoneTheme + delta + 4) % 4;
            PlayKeySound();
            break;
        case 61: // Ringtone (plays live preview even when switching!)
            cfg.ringtoneId = (cfg.ringtoneId + delta + 4) % 4;
            PlayDialSound(cfg.ringtoneId);
            break;
        case 62: // Key Beeps
            cfg.enableKeyBeeps = !cfg.enableKeyBeeps;
            PlayKeySound();
            break;
        case 63: // Silent Mode
            cfg.silentMode = !cfg.silentMode;
            if (!cfg.silentMode) PlayKeySound();
            break;
        case 64: // CJ Hand Animation
            cfg.enablePhoneAnim = !cfg.enablePhoneAnim;
            if (cfg.enablePhoneAnim) StartPhoneAnimIfOnFoot();
            else                     StopPhoneAnim();
            PlayKeySound();
            break;
        case 65: // Auto-Outfits
            cfg.enableAutoOutfits = !cfg.enableAutoOutfits;
            PlayKeySound();
            break;
        default:
            return;
    }
    pcfg::SaveSettings();
}

void HandleSelect() {
    if (g_cursor < 0 || g_cursor >= static_cast<int>(g_items.size())) return;
    const auto item = g_items[g_cursor];
    auto* bus       = modbus::GetBus();
    sa::CPed* ped   = sa::FindPlayerPed(0);

    if (g_screen == Screen::Home) {
        switch (item.actionId) {
            case 10: g_screen = Screen::Contacts;     g_cursor = 0; PlayKeySound(); break;
            case 12: ClosePhone(false); ui::LockMenuInputBriefly(200); if (bus && bus->garageOpenDealership) bus->garageOpenDealership(true); break;
            case 13: ClosePhone(false); ui::LockMenuInputBriefly(200); if (bus && bus->inventoryOpenMenu)    bus->inventoryOpenMenu(true, 0); break;
            case 14: g_screen = Screen::WardrobeMenu; g_cursor = 0; PlayKeySound(); break;
            case 15: TriggerCoopPlayerList(); break;
            case 17: g_screen = Screen::Settings;     g_cursor = 0; PlayKeySound(); break;
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
        if (item.actionId == 20) { g_screen = Screen::WardrobeSlots; g_cursor = 0; PlayKeySound(); }
        else if (item.actionId == 21) { g_screen = Screen::OutfitPresets; g_cursor = 0; PlayKeySound(); }
        return;
    }

    if (g_screen == Screen::WardrobeSlots) {
        g_activeSlot = item.arg;
        g_screen     = Screen::WardrobeSlotItems;
        g_cursor     = 0;
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
        return;
    }

    if (g_screen == Screen::OutfitPresets) {
        g_activePreset = item.arg;
        g_screen       = Screen::OutfitPresetActions;
        g_cursor       = 0;
        PlayKeySound();
        return;
    }

    if (g_screen == Screen::OutfitPresetActions) {
        if (item.actionId == 50) {
            wardrobe::EquipPreset(ped, item.arg, true);
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else if (item.actionId == 51) {
            wardrobe::CyclePresetTrigger(item.arg);
            PlayKeySound();
        } else if (item.actionId == 52) {
            if (wardrobe::SaveCurrentToPreset(ped, item.arg)) {
                sa::PlayFrontendSound(sa::SFX_BUY);
                ui::ShowHelp("~g~Текущая одежда сохранена в набор!", "~g~Current outfit saved to preset!");
            }
        } else if (item.actionId == 53) {
            wardrobe::DeletePreset(item.arg);
            g_screen = Screen::OutfitPresets;
            g_cursor = item.arg;
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

// Monitor SA-COOP shared wanted-clear cooldown!
void MonitorCoopSharedCooldown() {
    if (!sa::IsCoopModLoaded()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    int32_t curStars = sa::wanted::GetLevel(0);
    if (g_localClearedWanted) {
        g_localClearedWanted = false;
        g_prevWantedStars    = curStars;
        return;
    }

    if (g_prevWantedStars > 0 && curStars < g_prevWantedStars &&
        ped->GetHealth() > 1.0f && sa::GetActiveInterior() == 0 && !sa::IsCutsceneRunning()) {
        int drop = g_prevWantedStars - curStars;
        if (drop >= 1 && drop <= pcfg::Get().wantedClearMaxStars && RemainingSeconds(g_wantedClearReadyMs) == 0) {
            if (curStars > 0 || drop >= 2) {
                g_wantedClearReadyMs = GetTickCount64() + static_cast<uint64_t>(pcfg::Get().wantedClearCooldownSec) * 1000ull;
                ui::ShowHelp("~y~Старый знакомый помог в SA-COOP~w~ — общее КД 10:00.",
                             "~y~Old Acquaintance used in SA-COOP~w~ — shared 10:00 cooldown.");
                modlog::Info("[Phone/Coop] Detected shared wanted drop (%d -> %d), synced 10m cooldown.",
                             g_prevWantedStars, curStars);
            }
        }
    }
    g_prevWantedStars = curStars;
}

std::string GetCooldownSavePath(int slot, bool forLoading = false) {
    const char* ov = nullptr;
    if (auto* b = modbus::GetBus(); b && b->coopProfileName[0]) ov = b->coopProfileName;
    if (forLoading) {
        return sa::ResolveExistingLoadPath("GTA5PhoneSaves", pcfg::Get().modDir, "phone", slot, ov);
    }
    return sa::BuildSaveProfilePath("GTA5PhoneSaves", pcfg::Get().modDir, "phone", slot, ov);
}

} // namespace

// Runs BEFORE CTheScripts::Process & CWorld::Process so Phone consumes Mouse Wheel, LMB/RMB,
// WASD & Arrows while completely blocking CJ movement, attacks, aiming, and camera spin!
void PreScriptsTick() {
    if (sa::GetGameState() != 9) return;

    // Priority 1: Native GTA SA story phone calls ALWAYS win over custom Phone (F6)!
    auto* bus = modbus::GetBus();
    bool storyPhone = sa::IsStoryPhoneRinging() || (bus && bus->storyPhonePriority);
    if (storyPhone) {
        if (g_screen != Screen::Closed) ClosePhone(true);
        return;
    }

    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning() || clothesshop::IsShopActive()) {
        if (g_screen != Screen::Closed) ClosePhone(true);
        return;
    }

    const auto& k = ui::Keys();

    // Toggle Phone on F6 (suppressed while SA-COOP F9/F2/F4 overlay or chat is active!)
    if (!sa::IsExternalOverlayActive() && k.JustPressed(pcfg::Get().phoneKey)) {
        if (g_screen != Screen::Closed) {
            PlayBackSound();
            ClosePhone(false);
            sa::ClearPadPhoneInputs();
            return;
        }
        if (!modbus::IsAnyModalMenuOpen("phone") && sa::GetNumMenusInUse() == 0) {
            sa::CPed* ped = sa::FindPlayerPed(0);
            if (sa::IsPedValid(ped) && ped->GetHealth() > 1.0f && !ped->IsInWater()) {
                OpenPhoneToHome();
                sa::ClearPadPhoneInputs();
            }
            return;
        }
    }

    if (g_screen == Screen::Closed) {
        ui::UpdatePostCloseShield();
        ui::SyncIdleMouseEdges();
        return;
    }

    // Auto-close immediately if another modal opens, CJ dies/enters water, or CJ takes damage (shot/hit)!
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
        ui::ShowHelp("~r~Вас атакуют!~w~ Телефон убран.",
                     "~r~Under attack!~w~ Phone put away.");
        return;
    }
    g_trackHp    = curHp;
    g_trackArmor = curArm;

    // If CJ got into a car while phone was open, cancel the on-foot brick phone animation cleanly
    if (g_phoneAnimActive && ped->IsInVehicle()) {
        StopPhoneAnim();
    }

    auto in = ui::ListInput().Poll(true);

    // Calling animation screen: allow Backspace/Esc/RMB to hang up early, block all player inputs
    if (g_screen == Screen::CallingAnimation) {
        if (in.back) {
            HandleBack();
        } else if (GetTickCount() >= g_callFinishTickMs) {
            FinishPendingCall();
        }
        return;
    }

    RebuildCurrentItems();
    const int n = static_cast<int>(g_items.size());
    if (n > 0) {
        if (in.up) {
            g_cursor = (g_cursor - 1 + n) % n;
            PlayKeySound();
        } else if (in.down) {
            g_cursor = (g_cursor + 1) % n;
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
             .SetBanner(ui::Loc("Соединение...", "Dialing..."), {125, 235, 135, 255})
             .SetFooter(ui::Loc("Back/Esc/ПКМ - Сброс", "Back/Esc/RMB - Hang Up"))
             .AddItem(ui::Loc("Идёт гудок...", "Ringing..."), "...", {200, 200, 200, 255}, true)
             .SetSelected(0);
        phone.Render();
        return;
    }

    RebuildCurrentItems();

    switch (g_screen) {
        case Screen::Home: {
            std::string homeSub = ui::Loc("Главное меню", "Main Menu");
            if (sa::IsCoopModLoaded()) {
                const char* ov = nullptr;
                if (auto* b = modbus::GetBus(); b && b->coopProfileName[0]) ov = b->coopProfileName;
                homeSub = ui::Loc("Профиль: ", "Profile: ") + sa::GetCoopPlayerProfileName(ov);
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
            phone.SetHeader(ui::Loc("ГАРДЕРОБ CJ", "CJ WARDROBE"), ui::Loc("Стиль и Наборы", "Style & Presets"))
                 .SetFooter(ui::StandardFooter("Выбр.", "Pick", "Назад", "Back"));
            break;
        case Screen::WardrobeSlots:
            phone.SetHeader(ui::Loc("ОДЕЖДА CJ", "CLOTHES"), ui::Loc("Купленные вещи", "Owned Items"))
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
    std::string path = GetCooldownSavePath(g_saveSlot);
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "[Cooldowns]\nWantedClearRemSec=%d\nMedicRemSec=%d\n",
                 RemainingSeconds(g_wantedClearReadyMs),
                 RemainingSeconds(g_medicReadyMs));
    std::fclose(f);
}

void OnGameLoad(int slot1to8) {
    g_saveSlot = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
    g_wantedClearReadyMs = 0;
    g_medicReadyMs       = 0;
    g_prevWantedStars    = sa::wanted::GetLevel(0);

    std::string path = GetCooldownSavePath(g_saveSlot, true);
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    int wRem = GetPrivateProfileIntA("Cooldowns", "WantedClearRemSec", 0, path.c_str());
    int mRem = GetPrivateProfileIntA("Cooldowns", "MedicRemSec", 0, path.c_str());
    uint64_t now = GetTickCount64();
    if (wRem > 0) g_wantedClearReadyMs = now + static_cast<uint64_t>(wRem) * 1000ull;
    if (mRem > 0) g_medicReadyMs       = now + static_cast<uint64_t>(mRem) * 1000ull;
}

void OnNewGame() {
    g_saveSlot           = 0;
    g_wantedClearReadyMs = 0;
    g_medicReadyMs       = 0;
    g_prevWantedStars    = 0;
}

void Reset() {
    StopPhoneAnim();
    g_screen          = Screen::Closed;
    g_prevWantedStars = 0;
}

} // namespace phoneui
