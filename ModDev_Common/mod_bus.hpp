#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <windows.h>

// ============================================================================
// Cross-ASI Shared Service Bus & Addon Registry (Pure C-ABI, Zero Static DLL Deps!)
//
// Allows completely independent ASI plugins to discover & integrate with each
// other in ANY load order, regardless of whether GTA5UIKit.asi is installed:
//   - GTA5UIKit.asi           (Shared Theme, Vanilla Menu Restyler, Bus Host)
//   - GTA5Phone.asi           (Improvised Phone on F6, Contacts, Wardrobe)
//   - GTA5GarageAndTuning.asi (Workshop, Tuning, Wear & Tear, Mechanic, Dealership)
//   - GTA5Inventory.asi       (Restaurants Take-Out, Pocket Snacks & Spare Armor on F7)
//   - GTA5AmmuNation.asi      (Weapon Customization, 3D Attachments, Weapon Wheel)
//   - Third-party addons      (RegisterPhoneAddon() adds custom Phone apps/contacts!)
// ============================================================================
namespace modbus {

constexpr uint32_t BUS_MAGIC   = 0x47544135u; // 'GTA5'
constexpr uint32_t BUS_VERSION = 3;
constexpr int      MAX_PANEL_CLAIMS = 16;
constexpr int      MAX_PHONE_ADDONS = 24;

using FnBoolNoArgs      = bool(__cdecl*)();
using FnVoidNoArgs      = void(__cdecl*)();
using FnVoidBool        = void(__cdecl*)(bool);
using FnVoidBoolInt     = void(__cdecl*)(bool, int);
using FnIntNoArgs       = int(__cdecl*)();
using FnIntInt          = int(__cdecl*)(int);
using FnBoolIntInt      = bool(__cdecl*)(int, int);
using FnVoidCharInt     = void(__cdecl*)(char*, int);

struct PanelClaim {
    char         titlePrefix[16];
    FnBoolNoArgs isActiveFn;
};

// Generic extensible Phone Addon entry (section: 0=Main App, 1=Contact, 2=Quick Action)
struct PhoneAddonEntry {
    char          id[32];
    char          labelRus[64];
    char          labelEng[64];
    char          badgeRus[32];
    char          badgeEng[32];
    uint8_t       section; // 0 = Main Menu, 1 = Contacts, 2 = Quick Actions
    uint8_t       _pad[3];
    FnVoidBool    onActivate; // arg: bool fromPhone
    FnBoolNoArgs  isOpen;
};

struct SharedBus {
    uint32_t magic;
    uint32_t version;

    // --- 1. Global UI Kit Theme & Config (Authoritative when uiKitInstalled == true) ---
    bool     uiKitInstalled;
    bool     useRussian;
    bool     useMph;
    bool     restyleVanillaMenus;
    bool     restyleHelpBoxes;
    float    uiScale;
    char     coopProfileName[48]; // Optional profile override for SA-COOP per-player storage
    uint32_t menuInputUnlockMs;   // Cross-DLL debounce after switching between menus (e.g. Phone -> Inventory)
    uint32_t postCloseShieldMs;   // Brief shield after closing a menu so LMB/RMB doesn't punch/aim
    bool     storyPhonePriority;  // True while GTA SA story phone call is ringing/active (Priority 1!)

    // --- 2. Panel Claim Table (Prevents vanilla restyler from overlapping custom shops) ---
    int        panelClaimCount;
    PanelClaim panelClaims[MAX_PANEL_CLAIMS];

    // --- 3. Phone Module Registration ---
    bool         phoneInstalled;
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
    FnIntNoArgs   inventoryGetArmorUsedCap;  // Used armor carry capacity (e.g. 0..10)
    FnIntNoArgs   inventoryGetArmorMaxCap;   // Total armor carry capacity (default 10)
    FnIntInt      inventoryGetArmorWeight;   // Weight of armor tier 0..3 (2, 4, 7, 10)
    FnIntInt      inventoryGetArmorPrice;    // Price of armor tier 0..3

    // --- 6. Weapons / Ammu-Nation Registration ---
    bool          weaponsInstalled;
    FnBoolNoArgs  weaponsIsAnyMenuOpen;
    FnIntNoArgs   weaponsGetOwnedCount;
    FnVoidCharInt weaponsGetSummary;     // (char* outUtf8, int bufSize)

    // --- 7. Generic Third-Party Phone Addons ---
    int             phoneAddonCount;
    PhoneAddonEntry phoneAddons[MAX_PHONE_ADDONS];
};

// Process-scoped shared memory pointer holder (100% immune to DLL load order or renaming!)
inline SharedBus* GetBus() {
    static SharedBus* s_bus = nullptr;
    if (s_bus) return s_bus;

    char mapName[96];
    std::snprintf(mapName, sizeof(mapName), "Local\\GTASA_GTA5ModSuite_Bus_v3_%lu",
                  static_cast<unsigned long>(GetCurrentProcessId()));

    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, sizeof(SharedBus), mapName);
    if (!hMap) {
        static SharedBus s_fallback{};
        if (s_fallback.magic != BUS_MAGIC) {
            std::memset(&s_fallback, 0, sizeof(s_fallback));
            s_fallback.magic   = BUS_MAGIC;
            s_fallback.version = BUS_VERSION;
            s_fallback.useRussian = true;
            s_fallback.useMph     = true;
            s_fallback.uiScale    = 1.0f;
            s_fallback.restyleVanillaMenus = true;
            s_fallback.restyleHelpBoxes    = true;
        }
        s_bus = &s_fallback;
        return s_bus;
    }

    bool alreadyExists = (GetLastError() == ERROR_ALREADY_EXISTS);
    void* view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBus));
    if (!view) {
        CloseHandle(hMap);
        return nullptr;
    }

    auto* bus = reinterpret_cast<SharedBus*>(view);
    if (!alreadyExists || bus->magic != BUS_MAGIC || bus->version != BUS_VERSION) {
        std::memset(bus, 0, sizeof(SharedBus));
        bus->magic               = BUS_MAGIC;
        bus->version             = BUS_VERSION;
        bus->useRussian          = true;
        bus->useMph              = true;
        bus->uiScale             = 1.0f;
        bus->restyleVanillaMenus = true;
        bus->restyleHelpBoxes    = true;
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
    if (check("phone",     b->phoneInstalled,     b->phoneIsOpen))         return true;
    if (check("garage",    b->garageInstalled,    b->garageIsAnyMenuOpen)) return true;
    if (check("inventory", b->inventoryInstalled, b->inventoryIsMenuOpen)) return true;
    if (check("weapons",   b->weaponsInstalled,   b->weaponsIsAnyMenuOpen))return true;
    for (int i = 0; i < b->phoneAddonCount; ++i) {
        const auto& a = b->phoneAddons[i];
        if (a.isOpen && (!exceptOwner || std::strcmp(exceptOwner, a.id) != 0)) {
            if (a.isOpen()) return true;
        }
    }
    return false;
}

} // namespace modbus
