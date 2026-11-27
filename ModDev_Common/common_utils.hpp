#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
#include "integrations/mskl_rus.hpp"

// ============================================================================
// Shared DRY Utilities (Paths, INI Readers, Save-Slot Tracker, Formatters)
// Reused across ALL 6 ASI plugins — zero copy-pasted boilerplate!
// ============================================================================
namespace common {

inline std::string GetModuleDir(HMODULE hModule) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(hModule, buf, MAX_PATH);
    std::string full(buf);
    size_t slash = full.find_last_of("\\/");
    return (slash != std::string::npos) ? full.substr(0, slash) : ".";
}

inline std::string GetExeDir() {
    return GetModuleDir(nullptr);
}

// ----------------------------------------------------------------------------
// INI Helpers
// ----------------------------------------------------------------------------
inline std::string IniReadString(const char* path, const char* section, const char* key, const char* defVal = "") {
    char buf[256] = {};
    GetPrivateProfileStringA(section, key, defVal, buf, sizeof(buf), path);
    return std::string(buf);
}

inline int IniReadInt(const char* path, const char* section, const char* key, int defVal) {
    return static_cast<int>(GetPrivateProfileIntA(section, key, defVal, path));
}

inline bool IniReadBool(const char* path, const char* section, const char* key, bool defVal) {
    return GetPrivateProfileIntA(section, key, defVal ? 1 : 0, path) != 0;
}

inline float IniReadFloat(const char* path, const char* section, const char* key, float defVal) {
    char defBuf[32];
    std::snprintf(defBuf, sizeof(defBuf), "%.4f", defVal);
    char buf[64] = {};
    GetPrivateProfileStringA(section, key, defBuf, buf, sizeof(buf), path);
    if (!buf[0]) return defVal;
    char* end = nullptr;
    float v = std::strtof(buf, &end);
    return (end != buf) ? v : defVal;
}

inline void IniWriteInt(const char* path, const char* section, const char* key, int val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", val);
    WritePrivateProfileStringA(section, key, buf, path);
}

inline void IniWriteFloat(const char* path, const char* section, const char* key, float val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", val);
    WritePrivateProfileStringA(section, key, buf, path);
}

inline void IniWriteString(const char* path, const char* section, const char* key, const char* val) {
    WritePrivateProfileStringA(section, key, val ? val : "", path);
}

// ----------------------------------------------------------------------------
// Commerce & Key Label Formatters (Domain helpers kept out of dumb ui_kit.hpp)
// ----------------------------------------------------------------------------
inline std::string FormatPrice(int price, bool useRus) {
    if (price <= 0) {
        return useRus ? integrations::mskl::ToGameRussian("Бесплатно") : std::string("Free");
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%d", price);
    return std::string(buf);
}

inline std::string GetVirtualKeyLabel(int vk) {
    if (vk >= VK_F1 && vk <= VK_F12) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "F%d", (vk - VK_F1) + 1);
        return std::string(buf);
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk == VK_TAB)    return "TAB";
    if (vk == VK_SPACE)  return "SPACE";
    if (vk == VK_INSERT) return "INS";
    if (vk == VK_HOME)   return "HOME";
    return "KEY";
}

// ----------------------------------------------------------------------------
// Shared GTA SA 1.0 US Save-Slot Detection & Lifecycle Tracker
// Eliminates duplicated raw 0x00C16F18 / 0x00BA6748+0x15F boilerplate across
// Garage, Weapons, Inventory, and Phone!
// ----------------------------------------------------------------------------
inline int ParseSaveSlotFromPath(const char* path) {
    if (!path) return 0;
    const char* p = std::strstr(path, "GTASAsf");
    if (!p) p = std::strstr(path, "gtasasf");
    if (p && p[7] >= '1' && p[7] <= '8') {
        return p[7] - '0';
    }
    return 0;
}

inline const char* GetSaveFileNameBuffer() {
    return reinterpret_cast<const char*>(0x00C16F18); // CGenericGameStorage::ms_ValidSaveName (written during Save)
}

inline const char* GetLoadFileNameBuffer() {
    return reinterpret_cast<const char*>(0x00C15FC8); // CGenericGameStorage::ms_LoadFileNameWithPath (opened by GenericLoad!)
}

inline int GetCurrentMenuSaveSlot() {
    return static_cast<int>(*reinterpret_cast<const uint8_t*>(0x00BA6748 + 0x15F)) + 1; // FrontEndMenuManager.m_nSelectedSaveGame + 1
}

struct SaveSlotTracker {
    int  pendingSaveSlot1to8 = 0;
    bool loadedSaveThisBoot  = false;
    bool initializedInWorld  = false;
    int  lastGameState       = -1;

    void OnMakeValidSaveName(int slotNum0Based) {
        if (slotNum0Based >= 0 && slotNum0Based <= 7) {
            pendingSaveSlot1to8 = slotNum0Based + 1;
        }
    }

    // Used after CGenericGameStorage::GenericSave (0x00619081)
    int ResolveCurrentSaveSlot() {
        int slot = pendingSaveSlot1to8;
        if (slot < 1 || slot > 8) slot = ParseSaveSlotFromPath(GetSaveFileNameBuffer());
        if (slot < 1 || slot > 8) {
            int menuSlot = GetCurrentMenuSaveSlot();
            if (menuSlot >= 1 && menuSlot <= 8) slot = menuSlot;
        }
        return slot;
    }

    // Used after CGenericGameStorage::GenericLoad (0x0053C70B)
    // Reads ms_LoadFileNameWithPath (0x00C15FC8) FIRST — ms_ValidSaveName (0x00C16F18) is stale during loads!
    int ResolveCurrentLoadSlot(bool isCoopClient = false) {
        if (isCoopClient) return 0; // SA-COOP client always uses its own isolated coop profile
        const char* loadBuf = GetLoadFileNameBuffer();
        if (loadBuf && loadBuf[0] != '\0') {
            int slot = ParseSaveSlotFromPath(loadBuf);
            if (slot >= 1 && slot <= 8) return slot;
            // Non-GTASAsf file (e.g. SA-COOP host_snapshot.b / world.b): never misattribute to a local slot!
            if (std::strstr(loadBuf, ".b") != nullptr || std::strstr(loadBuf, ".B") != nullptr) {
                return 0;
            }
        }
        int menuSlot = GetCurrentMenuSaveSlot();
        if (menuSlot >= 1 && menuSlot <= 8) return menuSlot;
        return ParseSaveSlotFromPath(GetSaveFileNameBuffer());
    }

    // Returns resolved save slot (1..8, or 0 for coop/non-slot snapshot) after CGenericGameStorage::GenericLoad succeeds
    int OnGenericLoadSuccess(bool isCoopClient = false) {
        int slot = ResolveCurrentLoadSlot(isCoopClient);
        loadedSaveThisBoot  = true;
        initializedInWorld  = true;
        pendingSaveSlot1to8 = 0;
        return slot;
    }

    // Called from CGame::ReInitGameObjectVariables (0x0053C6DB) before EVERY in-game New Game OR Load Save!
    void OnWorldRestart() {
        initializedInWorld  = false;
        loadedSaveThisBoot  = false;
        pendingSaveSlot1to8 = 0;
    }

    void OnLeftWorld() {
        initializedInWorld  = false;
        loadedSaveThisBoot  = false;
        pendingSaveSlot1to8 = 0;
    }
};

} // namespace common
