#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <windows.h>

// ============================================================================
// Cross-ASI Shared Service Bus, Input Priority & Addon Registry
// (Pure C-ABI over process-local shared memory — Zero Static DLL Deps!)
// ============================================================================
namespace modbus {

constexpr uint32_t BUS_MAGIC        = 0x47544135u; // 'GTA5'
constexpr uint32_t BUS_VERSION      = 5;
constexpr int      MAX_PANEL_CLAIMS = 16;
constexpr int      MAX_PHONE_ADDONS = 24;

// Centralized Input Priority Levels (Rule 1d):
//   1 = StoryPhone   (native GTA SA incoming call prompt — owns TAB, never blocked!)
//   2 = ModalMenu    (Phone, Shops, Workshops, Inventory, Vanilla CMenuSystem — locks CJ controls)
//   3 = QuickOverlay (Weapon Wheel — yields to StoryPhone & ModalMenu)
//   4 = PassiveHud   (HelpToast, passive status cards — never blocks CJ movement)
enum class InputPriority : uint8_t {
    StoryPhone   = 1,
    ModalMenu    = 2,
    QuickOverlay = 3,
    PassiveHud   = 4
};

using FnBoolNoArgs  = bool(__cdecl*)();
using FnVoidNoArgs  = void(__cdecl*)();
using FnVoidBool    = void(__cdecl*)(bool);
using FnVoidBoolInt = void(__cdecl*)(bool, int);
using FnIntNoArgs   = int(__cdecl*)();
using FnIntInt      = int(__cdecl*)(int);
using FnBoolIntInt  = bool(__cdecl*)(int, int);
using FnVoidCharInt = void(__cdecl*)(char*, int);

struct PanelClaim {
    char         titlePrefix[16];
    FnBoolNoArgs isActiveFn;
};

// Generic extensible Phone Addon entry (section: 0=Main App, 1=Contact, 2=Quick Action)
struct PhoneAddonEntry {
    char         id[32];
    char         labelRus[64];
    char         labelEng[64];
    char         badgeRus[32];
    char         badgeEng[32];
    uint8_t      section; // 0 = Main Menu, 1 = Contacts, 2 = Quick Actions
    uint8_t      _pad[3];
    FnVoidBool   onActivate; // arg: bool fromPhone
    FnBoolNoArgs isOpen;
};

struct SharedBus {
    uint32_t magic;
    uint32_t version;

    // --- 1. Global UI Kit Theme, Config & Input Priority ---
    bool     uiKitInstalled;
    bool     useRussian;
    bool     useMph;
    bool     restyleVanillaMenus;
    bool     restyleHelpBoxes;
    float    uiScale;
    char     coopProfileName[48]; // Optional profile override for SA-COOP per-player storage
    uint32_t menuInputUnlockMs;   // Cross-DLL debounce after switching between menus
    uint32_t postCloseShieldMs;   // Brief shield after closing a menu so LMB/RMB/Esc don't bleed
    bool     storyPhonePriority;  // Priority 1: native GTA SA story phone call ringing/active
    bool     vanillaMenuOpen;     // Priority 2: unclaimed vanilla CMenuSystem driven by GTA5UIKit

    // --- 2. Panel Claim Table (Prevents vanilla restyler from overlapping custom shops) ---
    int        panelClaimCount;
    PanelClaim panelClaims[MAX_PANEL_CLAIMS];

    // --- 3. Phone Module Registration ---
    bool         phoneInstalled;
    int          phoneKey;               // Configured phone hotkey (default VK_F6)
    FnBoolNoArgs phoneIsOpen;
    FnVoidNoArgs phoneClose;
    FnVoidNoArgs phoneReturnToMain;

    // --- 4. Garage / Mechanic / Dealership Registration ---
    bool         garageInstalled;
    FnVoidBool   garageOpenMechanic;     // (bool fromPhone)
    FnVoidBool   garageOpenDealership;   // (bool fromPhone)
    FnVoidBool   garageOpenInsurance;    // (bool fromPhone)
    FnBoolNoArgs garageIsAnyMenuOpen;
    FnIntNoArgs  garageGetOwnedCount;
    FnIntNoArgs  garageGetWreckedCount;

    // --- 5. Food & Spare Armor Inventory Registration ---
    bool          inventoryInstalled;
    FnVoidBoolInt inventoryOpenMenu;         // (bool fromPhone, int tab: 0=All, 1=Food, 2=Armor)
    FnBoolNoArgs  inventoryIsMenuOpen;
    FnIntInt      inventoryGetArmorCount;    // (int tier: 0..3)
    FnIntInt      inventoryGetArmorMax;      // (int tier: 0..3) -> max possible at current free space
    FnBoolIntInt  inventoryAddArmor;         // (int tier: 0..3, int delta)
    FnIntNoArgs   inventoryGetTotalFood;
    FnIntNoArgs   inventoryGetArmorUsedCap;
    FnIntNoArgs   inventoryGetArmorMaxCap;
    FnIntInt      inventoryGetArmorWeight;
    FnIntInt      inventoryGetArmorPrice;

    // --- 6. Weapons / Ammu-Nation Registration ---
    bool          weaponsInstalled;
    FnBoolNoArgs  weaponsIsAnyMenuOpen;
    FnIntNoArgs   weaponsGetOwnedCount;
    FnVoidCharInt weaponsGetSummary;     // (char* outUtf8, int bufSize)

    // --- 7. Progression / Rank System Registration ---
    bool          progressionInstalled;
    int           playerRank;
    int           playerTotalXp;
    int           playerLevelXp;
    int           playerNextLevelXp;
    FnBoolNoArgs  progressionIsMenuOpen;
    FnVoidBool    progressionOpenMenu;   // (bool fromPhone)
    FnVoidNoArgs  progressionPulseHud;   // briefly reveal top XP bar HUD

    // --- 8. Generic Third-Party Phone Addons ---
    int             phoneAddonCount;
    PhoneAddonEntry phoneAddons[MAX_PHONE_ADDONS];
};

inline void InitBusDefaults(SharedBus* bus) {
    std::memset(bus, 0, sizeof(SharedBus));
    bus->magic               = BUS_MAGIC;
    bus->version             = BUS_VERSION;
    bus->useRussian          = true;
    bus->useMph              = true;
    bus->uiScale             = 1.0f;
    bus->restyleVanillaMenus = true;
    bus->restyleHelpBoxes    = true;
    bus->phoneKey            = VK_F6;
    bus->playerRank          = 1;
    bus->playerNextLevelXp   = 200;
}

// Process-scoped shared memory pointer holder (immune to DLL load order or renaming!)
inline SharedBus* GetBus() {
    static SharedBus* s_bus = nullptr;
    if (s_bus) return s_bus;

    static SharedBus s_fallback{};
    auto useFallback = [&]() -> SharedBus* {
        if (s_fallback.magic != BUS_MAGIC || s_fallback.version != BUS_VERSION) {
            InitBusDefaults(&s_fallback);
        }
        s_bus = &s_fallback;
        return s_bus;
    };

    char mapName[96];
    std::snprintf(mapName, sizeof(mapName), "Local\\GTASA_GTA5ModSuite_Bus_v%u_%lu",
                  BUS_VERSION, static_cast<unsigned long>(GetCurrentProcessId()));

    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, sizeof(SharedBus), mapName);
    if (!hMap) return useFallback();

    bool alreadyExists = (GetLastError() == ERROR_ALREADY_EXISTS);
    void* view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBus));
    if (!view) {
        CloseHandle(hMap);
        return useFallback();
    }

    auto* bus = reinterpret_cast<SharedBus*>(view);
    if (!alreadyExists || bus->magic != BUS_MAGIC || bus->version != BUS_VERSION) {
        InitBusDefaults(bus);
    }
    // Keep hMap open for the process lifetime so the shared view stays alive across all ASIs!
    s_bus = bus;
    return s_bus;
}

inline void RegisterPanelClaim(const char* titlePrefix, FnBoolNoArgs isActiveFn = nullptr) {
    SharedBus* b = GetBus();
    if (!b || !titlePrefix || !titlePrefix[0]) return;
    for (int i = 0; i < b->panelClaimCount; ++i) {
        if (_stricmp(b->panelClaims[i].titlePrefix, titlePrefix) == 0) {
            b->panelClaims[i].isActiveFn = isActiveFn;
            return;
        }
    }
    if (b->panelClaimCount >= MAX_PANEL_CLAIMS) return;
    auto& dst = b->panelClaims[b->panelClaimCount++];
    std::strncpy(dst.titlePrefix, titlePrefix, sizeof(dst.titlePrefix) - 1);
    dst.titlePrefix[sizeof(dst.titlePrefix) - 1] = '\0';
    dst.isActiveFn = isActiveFn;
}

inline bool IsPanelClaimed(const char* panelTitle) {
    SharedBus* b = GetBus();
    if (!b || !panelTitle || !panelTitle[0]) return false;
    for (int i = 0; i < b->panelClaimCount; ++i) {
        const auto& c = b->panelClaims[i];
        size_t len = std::strlen(c.titlePrefix);
        if (len > 0 && _strnicmp(panelTitle, c.titlePrefix, len) == 0) {
            if (!c.isActiveFn || c.isActiveFn()) return true;
        }
    }
    return false;
}

inline bool RegisterPhoneAddon(const PhoneAddonEntry& entry) {
    SharedBus* b = GetBus();
    if (!b || !entry.id[0]) return false;
    for (int i = 0; i < b->phoneAddonCount; ++i) {
        if (std::strcmp(b->phoneAddons[i].id, entry.id) == 0) {
            b->phoneAddons[i] = entry;
            return true;
        }
    }
    if (b->phoneAddonCount >= MAX_PHONE_ADDONS) return false;
    b->phoneAddons[b->phoneAddonCount++] = entry;
    return true;
}

inline bool IsAnyModalMenuOpen(const char* exceptOwner = nullptr) {
    SharedBus* b = GetBus();
    if (!b) return false;
    auto check = [&](const char* name, bool installed, FnBoolNoArgs fn) {
        if (!installed || !fn) return false;
        if (exceptOwner && std::strcmp(exceptOwner, name) == 0) return false;
        return fn();
    };
    if (check("phone",       b->phoneInstalled,       b->phoneIsOpen))         return true;
    if (check("garage",      b->garageInstalled,      b->garageIsAnyMenuOpen)) return true;
    if (check("inventory",   b->inventoryInstalled,   b->inventoryIsMenuOpen)) return true;
    if (check("weapons",     b->weaponsInstalled,     b->weaponsIsAnyMenuOpen))return true;
    if (check("progression", b->progressionInstalled, b->progressionIsMenuOpen))return true;
    if (b->vanillaMenuOpen && (!exceptOwner || std::strcmp(exceptOwner, "vanilla") != 0)) {
        return true;
    }
    for (int i = 0; i < b->phoneAddonCount; ++i) {
        const auto& a = b->phoneAddons[i];
        if (a.isOpen && (!exceptOwner || std::strcmp(exceptOwner, a.id) != 0)) {
            if (a.isOpen()) return true;
        }
    }
    return false;
}

inline InputPriority GetHighestActiveInputPriority(const char* exceptModalOwner = nullptr) {
    SharedBus* b = GetBus();
    if (b && b->storyPhonePriority) return InputPriority::StoryPhone;
    if (IsAnyModalMenuOpen(exceptModalOwner)) return InputPriority::ModalMenu;
    return InputPriority::PassiveHud;
}

inline bool IsStoryPhoneBlocking() {
    SharedBus* b = GetBus();
    return b && b->storyPhonePriority;
}

inline int GetPhoneKey() {
    SharedBus* b = GetBus();
    return (b && b->phoneKey > 0) ? b->phoneKey : VK_F6;
}

inline int GetPlayerRank() {
    SharedBus* b = GetBus();
    return (b && b->playerRank >= 1) ? b->playerRank : 1;
}

inline bool IsRankUnlocked(int requiredRank) {
    return requiredRank <= 1 || GetPlayerRank() >= requiredRank;
}

inline void PulseProgressionHud() {
    if (SharedBus* b = GetBus(); b && b->progressionInstalled && b->progressionPulseHud) {
        b->progressionPulseHud();
    }
}

} // namespace modbus
