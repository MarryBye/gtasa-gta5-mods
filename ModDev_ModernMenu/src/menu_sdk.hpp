#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "sa_sdk.hpp"

// ============================================================================
// Verified GTA SA 1.0 US (Hoodlum) Frontend Menu SDK (CMenuManager @ 0x00BA6748)
// Every offset, jump table, and function address below is verified directly
// against gta_sa.exe 1.0 US disassembly.
// ============================================================================
namespace modern_menu::sdk {

#pragma pack(push, 1)
struct CMenuScreenEntry {
    uint8_t  actionType; // +0x00 (0=NONE, 1=STATIC_TEXT, 2=BACK, 3=YES, 4=NO, 5=SWITCH, ...)
    char     gxtKey[8];  // +0x01 (up to 8 chars, not guaranteed null-terminated)
    uint8_t  entryType;  // +0x09 (1..8 = SAVE_SLOT_1..8, 12 = OPTION)
    int8_t   targetMenu; // +0x0A (target page 0..43)
    uint8_t  _pad0B;     // +0x0B
    int16_t  posX;       // +0x0C
    int16_t  posY;       // +0x0E
    uint16_t align;      // +0x10
};
static_assert(sizeof(CMenuScreenEntry) == 0x12, "CMenuScreenEntry must be 18 bytes");

struct CMenuScreen {
    char             titleGxt[8];  // +0x00
    int8_t           prevPage;     // +0x08
    int8_t           parentEntry;  // +0x09
    CMenuScreenEntry items[12];    // +0x0A
};
static_assert(sizeof(CMenuScreen) == 0xE2, "CMenuScreen must be 226 bytes");
#pragma pack(pop)

constexpr int VANILLA_MENU_PAGES = 44;
constexpr int RELOC_MENU_PAGES   = 127;

// Dynamically reads the LIVE aScreens[] pointer (automatically picks up ModLoader's relocated
// 127-page aScreens[] heap array patched into 0x00577017, or vanilla 0x008CE008 if absent!).
inline CMenuScreen* GetScreens() {
    uintptr_t imm = 0;
    std::memcpy(&imm, reinterpret_cast<const void*>(0x00577017), sizeof(uintptr_t));
    if (imm > 0x00400000u && imm != 0x008CE012u) {
        return reinterpret_cast<CMenuScreen*>(imm - 0x0Au);
    }
    return reinterpret_cast<CMenuScreen*>(0x008CE008);
}

inline bool IsScreensRelocated() {
    return reinterpret_cast<uintptr_t>(GetScreens()) != 0x008CE008u;
}

inline int GetMaxMenuPages() {
    return IsScreensRelocated() ? RELOC_MENU_PAGES : VANILLA_MENU_PAGES;
}

inline const CMenuScreen* GetScreen(int page) {
    if (page < 0 || page >= GetMaxMenuPages()) return nullptr;
    return &GetScreens()[page];
}

// Detects ModLoader's root configuration page (e.g. page 44 "ML_F0HH") inside relocated aScreens[33]
inline int8_t GetModLoaderRootPage() {
    if (!IsScreensRelocated()) return -1;
    const auto* opt = GetScreen(33);
    if (!opt) return -1;
    for (int i = 0; i < 12; ++i) {
        if (opt->items[i].actionType == 5 && opt->items[i].targetMenu >= 44) {
            char key[9] = {};
            std::memcpy(key, opt->items[i].gxtKey, 8);
            if (std::strncmp(key, "ML_", 3) == 0) {
                return opt->items[i].targetMenu;
            }
        }
    }
    return -1;
}

// Verified CMenuManager (0x00BA6748) accessors
inline uint8_t* GetMenuManager() { return reinterpret_cast<uint8_t*>(0x00BA6748); }

inline int8_t&   CurrentPage(uint8_t* mm)       { return *reinterpret_cast<int8_t*>(mm + 0x15D); }
inline int8_t&   PreviousPage(uint8_t* mm)      { return *reinterpret_cast<int8_t*>(mm + 0x15E); }
inline int32_t&  SelectedItem(uint8_t* mm)      { return *reinterpret_cast<int32_t*>(mm + 0x54); }
inline uint8_t&  MenuActive(uint8_t* mm)        { return mm[0x5C]; }

// VERIFIED @ 0x0057C2E6 (DrawFrontEnd) & live Title Screen probe:
// mm[0xE9] == 1 means Title / Main Menu (NOT in-game!); mm[0xE9] == 0 means In-Game Pause Menu!
inline uint8_t&  MainMenuSwitch(uint8_t* mm)    { return mm[0xE9]; }
inline bool      IsInGame(uint8_t* mm) {
    return mm && (mm[0xE9] == 0) && (sa::GetGameState() == 9);
}

inline uint8_t&  AllowBack(uint8_t* mm)         { return mm[0x1AE9]; }
inline uint8_t&  LoadingScreenFlag(uint8_t* mm) { return mm[0x1AEA]; }
inline uint8_t&  DontDrawFrontEnd(uint8_t* mm)  { return mm[0x32]; }
inline uint8_t&  TexturesLoaded(uint8_t* mm)    { return mm[0x15C]; }
inline uint8_t&  ShowMouse(uint8_t* mm)         { return mm[0xB8]; }
inline int32_t&  MouseWinX(uint8_t* mm)         { return *reinterpret_cast<int32_t*>(mm + 0xBC); }
inline int32_t&  MouseWinY(uint8_t* mm)         { return *reinterpret_cast<int32_t*>(mm + 0xC0); }
inline int32_t&  OldMouseWinX(uint8_t* mm)      { return *reinterpret_cast<int32_t*>(mm + 0x1AF8); }
inline int32_t&  OldMouseWinY(uint8_t* mm)      { return *reinterpret_cast<int32_t*>(mm + 0x1AFC); }

// Verified Option Fields on CMenuManager (confirmed against ProcessMenuOptions 0x576FE0 & ProcessPCMenu 0x57CD50!)
inline uint8_t&  HudOn(uint8_t* mm)             { return mm[0x21]; }                               // act 35 @ 0x577083
inline int32_t&  RadarMode(uint8_t* mm)         { return *reinterpret_cast<int32_t*>(mm + 0x24); } // act 34 @ 0x57720C
inline int32_t&  Brightness(uint8_t* mm)        { return *reinterpret_cast<int32_t*>(mm + 0x3C); } // act 27 (0..384)
inline float&    DrawDistance(uint8_t* mm)      { return *reinterpret_cast<float*>(mm + 0x40); }   // act 61 (0.925..1.8)
inline uint8_t&  ShowSubtitles(uint8_t* mm)     { return mm[0x44]; }                               // act 25 @ 0x577042
inline uint8_t&  ShowLegend(uint8_t* mm)        { return mm[0x4A]; }                               // act 33 @ 0x57705A
inline uint8_t&  Widescreen(uint8_t* mm)        { return mm[0x4B]; }                               // act 26 @ 0x57706B (WSF: Cutscene Borders 0..3)
inline uint8_t&  FrameLimiter(uint8_t* mm)      { return mm[0x4C]; }                               // act 24 @ 0x57CEC7 (WSF: 0..11 = OFF, 30..240 FPS)
inline uint8_t&  RadioAutoSelect(uint8_t* mm)   { return mm[0x4D]; }                               // act 31 @ 0x5770C2
inline uint8_t&  SfxVolume(uint8_t* mm)         { return mm[0x4F]; }                               // act 29 (0..64)
inline uint8_t&  RadioVolume(uint8_t* mm)       { return mm[0x50]; }                               // act 28 (0..64)
inline uint8_t&  RadioEq(uint8_t* mm)           { return mm[0x51]; }                               // act 30 @ 0x5770E5
inline uint8_t&  RadioStation(uint8_t* mm)      { return mm[0x52]; }                               // act 32 @ 0x5771AA
inline uint8_t&  MapLoaded(uint8_t* mm)         { return mm[0x78]; }
inline int8_t&   Language(uint8_t* mm)          { return *reinterpret_cast<int8_t*>(mm + 0x84); }
inline int8_t&   RadioMode(uint8_t* mm)         { return *reinterpret_cast<int8_t*>(mm + 0xB0); }
inline uint8_t&  JoypadFlag(uint8_t* mm, int i) { return mm[0xB1 + std::clamp(i, 0, 5)]; }
inline uint8_t&  MipMapping(uint8_t* mm)        { return mm[0xC4]; }                               // act 43 @ 0x57CEE1 (Main Menu only!)
inline uint8_t&  SavePhotos(uint8_t* mm)        { return mm[0xC5]; }                               // act 65 @ 0x57CF1B
inline int32_t&  CurrentAntialiasing(uint8_t* mm){ return *reinterpret_cast<int32_t*>(mm + 0xC8); }
inline int32_t&  PrefsAntialiasing(uint8_t* mm) { return *reinterpret_cast<int32_t*>(mm + 0xCC); } // act 44 (1..4)
inline uint8_t&  ControlMethod(uint8_t* mm)     { return mm[0xD0]; }                               // 0=Mouse+Keys, 1=Joypad
inline int32_t&  DisplayVideoMode(uint8_t* mm)  { return *reinterpret_cast<int32_t*>(mm + 0xD4); }
inline int32_t&  PrefsVideoMode(uint8_t* mm)    { return *reinterpret_cast<int32_t*>(mm + 0xD8); }
inline uint8_t&  RadioAutoScan(uint8_t* mm)     { return mm[0xE8]; }

// Verified Redefine Controls (page 38) fields on CMenuManager (0x57B644, 0x57E4D0, 0x57E6E0)
inline uint8_t&  ControlSubMode(uint8_t* mm)     { return mm[0xB7]; }                               // 0=Foot, 1=Vehicle
inline int32_t&  SelectedControlRow(uint8_t* mm) { return *reinterpret_cast<int32_t*>(mm + 0x90); }
inline uint8_t&  KeybindWaitFlag(uint8_t* mm)    { return mm[0x1B09]; }
inline uint8_t&  KeybindDebounce(uint8_t* mm)    { return mm[0x1B08]; }
inline uint8_t&  KeybindDeleteMode(uint8_t* mm)  { return mm[0x1B0A]; }
inline uint8_t&  KeybindClearFlash(uint8_t* mm)  { return mm[0x1B0B]; }
inline int32_t&  KeybindActionId(uint8_t* mm)    { return *reinterpret_cast<int32_t*>(mm + 0x1B0C); }
inline uint8_t&  KeybindActiveFlag(uint8_t* mm)  { return mm[0x1B14]; }

inline bool IsWaitingForKeybind(uint8_t* mm) {
    return mm && (KeybindWaitFlag(mm) != 0 || KeybindDebounce(mm) != 0);
}

inline void BeginKeybindCapture(uint8_t* mm, int32_t rowIdx, int32_t controllerActionId) {
    if (!mm || controllerActionId < 0) return;
    SelectedControlRow(mm) = rowIdx;
    KeybindActionId(mm)    = controllerActionId;
    KeybindWaitFlag(mm)    = 1;
    KeybindDebounce(mm)    = 1;
    KeybindActiveFlag(mm)  = 1;
    *reinterpret_cast<void**>(mm + 0xF0)     = mm + 0x38;
    *reinterpret_cast<int32_t*>(mm + 0x1B00) = 16;
}

// Verified against 0x0057E4D0..0x0057E6D6: CheckRedefineControlInput returns 1 (blocking ALL
// ProcessUserInput calls!) whenever mm[0x1B09], mm[0xEC], or mm[0xF0] is nonzero!
inline void CancelKeybindCapture(uint8_t* mm) {
    if (!mm) return;
    KeybindWaitFlag(mm)   = 0;
    KeybindDebounce(mm)   = 0;
    KeybindActiveFlag(mm) = 0;
    KeybindDeleteMode(mm) = 0;
    *reinterpret_cast<int32_t*>(mm + 0xEC)   = 0;
    *reinterpret_cast<void**>(mm + 0xF0)     = nullptr;
    *reinterpret_cast<int32_t*>(mm + 0x1AE0) = 0;
    *reinterpret_cast<int32_t*>(mm + 0x1AE4) = 0;
    *reinterpret_cast<int32_t*>(mm + 0x1B00) = 0;
}

// Verified CControllerConfigManager::GetControllerSettingTextKey @ 0x00531E20 (this = 0x00B70198)
inline std::string GetBoundKeyName(int32_t controllerActionId, int32_t order1to4) {
    if (controllerActionId < 0 || order1to4 < 1 || order1to4 > 4) return "";
    using Fn = const char*(__thiscall*)(void*, int32_t, int32_t);
    const char* s = reinterpret_cast<Fn>(0x00531E20)(
        reinterpret_cast<void*>(0x00B70198), controllerActionId, order1to4);
    return (s && s[0]) ? std::string(s) : "";
}

// Verified CControllerConfigManager slot helpers (0=KEY1, 1=KEY2, 2=MOUSE, 3=JOY)
// Action table at 0x00B70198 + 0xB74 + actionId*32: 4x { int32_t priority(1..4); int32_t keyCode; }
inline std::string GetSlotBoundKeyName(int32_t controllerActionId, int32_t slot0to3) {
    if (controllerActionId < 0 || controllerActionId >= 59 || slot0to3 < 0 || slot0to3 > 3) return "";
    const uintptr_t entryAddr = 0x00B70198u + 0xB74u +
                                static_cast<uintptr_t>(controllerActionId) * 32u +
                                static_cast<uintptr_t>(slot0to3) * 8u;
    int32_t prio = 0;
    std::memcpy(&prio, reinterpret_cast<const void*>(entryAddr), sizeof(int32_t));
    if (prio < 1 || prio > 4) return "";
    return GetBoundKeyName(controllerActionId, prio);
}

inline void ClearControllerSlot(int32_t controllerActionId, int32_t slot0to3) {
    if (controllerActionId < 0 || controllerActionId >= 59 || slot0to3 < 0 || slot0to3 > 3) return;
    using Fn = void(__thiscall*)(void*, int32_t, int32_t);
    reinterpret_cast<Fn>(0x0052FD70)(
        reinterpret_cast<void*>(0x00B70198), controllerActionId, slot0to3);
}

inline void ClearAllControllerSlots(int32_t controllerActionId) {
    for (int s = 0; s < 4; ++s) {
        ClearControllerSlot(controllerActionId, s);
    }
}

// Returns true when D3D9 device is running in exclusive fullscreen (D3DPRESENT_PARAMETERS.Windowed == 0)
inline bool IsExclusiveFullscreen() {
    uint32_t windowed = 1;
    std::memcpy(&windowed, reinterpret_cast<const void*>(0x00C9C040 + 8 * 4), sizeof(uint32_t));
    return windowed == 0;
}

// Verified External Settings Globals
inline bool&     InvertMouseY()    { return *reinterpret_cast<bool*>(0x00BA6745); }
inline bool&     SteerWithMouse()  { return *reinterpret_cast<bool*>(0x00C1CC02); }
inline bool&     FlyWithMouse()    { return *reinterpret_cast<bool*>(0x00C1CC03); }
inline float&    MouseSens()       { return *reinterpret_cast<float*>(0x00B6EC1C); } // 0.0003125..0.005

inline int GetFxQuality() {
    using Fn = int(__thiscall*)(void*);
    return reinterpret_cast<Fn>(0x0049EA50)(reinterpret_cast<void*>(0x00A9AE00));
}

inline const char* GetRadioStationName(uint8_t stationId) {
    using Fn = const char*(__thiscall*)(void*, uint8_t);
    const char* s = reinterpret_cast<Fn>(0x00507000)(reinterpret_cast<void*>(0x00B6BC90), stationId);
    return (s && s[0]) ? s : "";
}

inline std::string GetVideoModeName(int32_t modeIdx) {
    using Fn = char**(__cdecl*)();
    char** modes = reinterpret_cast<Fn>(0x00745AF0)();
    if (!modes || modeIdx < 0 || modeIdx > 256) return "";
    if (!modes[modeIdx]) return "";
    return std::string(modes[modeIdx]);
}

// Verified Native Menu Calls
inline void ProcessUserInput(uint8_t* mm, uint8_t down, uint8_t up, uint8_t accept, uint8_t cancel, int8_t sliderDir) {
    using Fn = void(__thiscall*)(void*, uint8_t, uint8_t, uint8_t, uint8_t, int8_t);
    reinterpret_cast<Fn>(0x0057B480)(mm, down, up, accept, cancel, sliderDir);
}

inline void SwitchToNewScreen(uint8_t* mm, int8_t targetPage) {
    using Fn = void(__thiscall*)(void*, int8_t);
    reinterpret_cast<Fn>(0x00573680)(mm, targetPage);
}

inline void SaveSettingsNative(uint8_t* mm) {
    if (!mm) return;
    using Fn = void(__thiscall*)(void*);
    reinterpret_cast<Fn>(0x0057C660)(mm);
}

// Safely switches to ANY menu page with strict Main Menu vs In-Game safety guards!
inline void NavigateToPage(uint8_t* mm, int8_t targetPage) {
    if (!mm) return;
    const bool inGame = IsInGame(mm);

    // HARD GUARD: In-game-only screens (Stats=0, Brief=2, Map=5, Pause=41) MUST NEVER open before game starts!
    if (!inGame && (targetPage == 0 || targetPage == 2 || targetPage == 5 || targetPage == 41)) {
        targetPage = 34;
    }
    if (inGame && targetPage == 34) {
        targetPage = 41;
    }

    if (targetPage == 0) {
        // Native SwitchToNewScreen(0) reads m_aItems[curSel].targetMenu instead of navigating to page 0!
        if (CurrentPage(mm) != 41) {
            SwitchToNewScreen(mm, 41);
        }
        PreviousPage(mm)      = CurrentPage(mm);
        CurrentPage(mm)       = 0;
        SelectedItem(mm)      = 0;
        LoadingScreenFlag(mm) = 0;
        return;
    }
    SwitchToNewScreen(mm, targetPage);
}

// Verified at 0x0057BA11..0x0057BA3F: saves & restores map zoom/pan fields around CMenuManager::DrawMap!
inline void DrawMapNative(uint8_t* mm) {
    if (!mm || !IsInGame(mm) || !sa::FindPlayerPed(0)) return;
    float savedZoom = *reinterpret_cast<float*>(mm + 0x64);
    float savedX    = *reinterpret_cast<float*>(mm + 0x68);
    float savedY    = *reinterpret_cast<float*>(mm + 0x6C);
    using Fn = void(__thiscall*)(void*);
    reinterpret_cast<Fn>(0x00575130)(mm);
    *reinterpret_cast<float*>(mm + 0x64) = savedZoom;
    *reinterpret_cast<float*>(mm + 0x68) = savedX;
    *reinterpret_cast<float*>(mm + 0x6C) = savedY;
}

// Draws classic GTA SA background art sprite cleanly behind Title Menu scrim when textures are loaded
inline void DrawTitleBackgroundArt(uint8_t* mm, float sw, float sh) {
    if (!mm || !TexturesLoaded(mm)) return;
    uint8_t bgIdx = *reinterpret_cast<const uint8_t*>(0x00BA8298);
    if (bgIdx == 0 || bgIdx >= 25) bgIdx = 14;
    void* tex = *reinterpret_cast<void* const*>(mm + 0xF8 + static_cast<size_t>(bgIdx) * 4);
    if (!tex) {
        for (uint8_t fallback = 14; fallback <= 19; ++fallback) {
            tex = *reinterpret_cast<void* const*>(mm + 0xF8 + static_cast<size_t>(fallback) * 4);
            if (tex) break;
        }
    }
    if (!tex) return;
    sa::CRect fullRect{-4.0f, sh + 4.0f, sw + 4.0f, -4.0f};
    sa::CRGBA tint{255, 255, 255, 255};
    sa::DrawSpriteTexture(tex, fullRect, tint);
}

// Safe GXT key reader from fixed-size 8-byte aScreens key
inline std::string ReadGxtKey8(const char key8[8]) {
    char buf[9] = {};
    std::memcpy(buf, key8, 8);
    buf[8] = '\0';
    return std::string(buf);
}

inline std::string GetGxtText(const char* key) {
    if (!key || !key[0]) return "";
    const char* s = sa::TextGet(key);
    return (s && s[0]) ? std::string(s) : "";
}

// Save Slot Info (0..7) verified against 0x00C16EBC, 0x00619030 & 0x00618D00
struct SaveSlotInfo {
    int         slot1to8 = 1;
    int32_t     status   = 1; // 0 = valid, 1 = empty, 2 = corrupt
    std::string nameGame;
    std::string dateGame;
};

inline SaveSlotInfo GetSaveSlotInfo(int slot0to7) {
    SaveSlotInfo out{};
    if (slot0to7 < 0 || slot0to7 > 7) return out;
    out.slot1to8 = slot0to7 + 1;
    const int32_t* states = reinterpret_cast<const int32_t*>(0x00C16EBC);
    out.status = states[slot0to7];
    if (out.status == 0) {
        using NameFn = char*(__cdecl*)(int);
        const char* nm = reinterpret_cast<NameFn>(0x00619030)(slot0to7);
        if (nm && nm[0]) out.nameGame = nm;
        const char* dt = reinterpret_cast<NameFn>(0x00618D00)(slot0to7);
        if (dt && dt[0]) out.dateGame = dt;
    }
    return out;
}

// Stats reader (Verified against CMenuManager::DrawStats 0x0057494E & 0x00574DC6..0x00574E61)
struct StatRowData {
    std::string labelGame;
    std::string valueGame;
    bool        isBar  = false;
    float       frac01 = 0.0f;
};

inline int GetStatCategoryCount(uint8_t category0to7) {
    using Fn = int(__cdecl*)(int, uint8_t);
    int n = reinterpret_cast<Fn>(0x0055A780)(99999, category0to7);
    return std::clamp(n, 0, 256);
}

inline bool ReadStatLine(int lineIdx, uint8_t category0to7, StatRowData& out) {
    out = {};
    char* nameBuf = reinterpret_cast<char*>(0x00C1B100);
    char* valBuf  = reinterpret_cast<char*>(0x00C1AED8);
    nameBuf[0] = '\0';
    valBuf[0]  = '\0';
    *reinterpret_cast<uint16_t*>(0x00B794CC) = 0;

    using Fn = int(__cdecl*)(int, uint8_t);
    reinterpret_cast<Fn>(0x0055A780)(lineIdx, category0to7);
    if (!nameBuf[0]) return false;

    out.labelGame = nameBuf;
    uint16_t barStatId = *reinterpret_cast<const uint16_t*>(0x00B794CC);
    if (barStatId != 0) {
        out.isBar = true;
        float rawVal = sa::stats::Get(barStatId);
        out.frac01   = std::clamp(rawVal * 0.001f, 0.0f, 1.0f);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::round(out.frac01 * 100.0f)));
        out.valueGame = pct;
    } else {
        out.isBar     = false;
        out.valueGame = valBuf[0] ? std::string(valBuf) : std::string("-");
    }
    return true;
}

// Brief History reader (Verified against CMenuManager::DrawBrief 0x00576440..0x00576485)
inline std::vector<std::string> ReadPreviousBriefs(int maxEntries = 20) {
    std::vector<std::string> out;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(0x00C1A570);
    char* scratch = reinterpret_cast<char*>(0x00C1B100);
    using InsertStrFn = void(__cdecl*)(char*, const char*);
    auto insertStr = reinterpret_cast<InsertStrFn>(0x0069E040);

    int limit = std::clamp(maxEntries, 1, 20);
    for (int i = 0; i < 20 && static_cast<int>(out.size()) < limit; ++i) {
        const uint8_t* entry = base + static_cast<size_t>(i) * 0x20;
        const char* rawText  = *reinterpret_cast<const char* const*>(entry + 0x00);
        if (!rawText || !rawText[0]) continue;
        int32_t n1 = *reinterpret_cast<const int32_t*>(entry + 0x04);
        int32_t n2 = *reinterpret_cast<const int32_t*>(entry + 0x08);
        int32_t n3 = *reinterpret_cast<const int32_t*>(entry + 0x0C);
        int32_t n4 = *reinterpret_cast<const int32_t*>(entry + 0x10);
        int32_t n5 = *reinterpret_cast<const int32_t*>(entry + 0x14);
        int32_t n6 = *reinterpret_cast<const int32_t*>(entry + 0x18);
        const char* strArg = *reinterpret_cast<const char* const*>(entry + 0x1C);

        scratch[0] = '\0';
        sa::InsertNumberInString(rawText, n1, n2, n3, n4, n5, n6, scratch);
        if (strArg && strArg[0]) {
            insertStr(scratch, strArg);
        }
        sa::InsertPlayerControlKeysInString(scratch);
        if (scratch[0]) {
            out.emplace_back(scratch);
        }
    }
    return out;
}

// Region name helper for Pause Overview card (safe in all interiors & exteriors)
inline void GetPlayerRegionNames(const sa::CPed* ped, const char*& outRus, const char*& outEng) {
    if (!sa::IsPedValid(ped)) {
        outRus = "Сан-Андреас";
        outEng = "San Andreas";
        return;
    }
    if (sa::GetActiveInterior() != 0) {
        outRus = "Помещение";
        outEng = "Interior";
        return;
    }
    sa::CVector p = ped->GetPosition();
    if (p.x > 650.0f && p.y > 400.0f) {
        outRus = "Лас-Вентурас";
        outEng = "Las Venturas";
    } else if (p.x < -1200.0f && p.y > -1200.0f && p.y < 1600.0f) {
        outRus = "Сан-Фиерро";
        outEng = "San Fierro";
    } else if (p.x > -200.0f && p.y < -450.0f) {
        outRus = "Лос-Сантос";
        outEng = "Los Santos";
    } else if (p.y > 500.0f) {
        outRus = "Пустыня";
        outEng = "Desert";
    } else {
        outRus = "Пригород";
        outEng = "Countryside";
    }
}

// ============================================================================
// Verified Redefine Controls (page 38) Action Tables (0x57F300 & 0x57EDF8/EEA4)
// ============================================================================
struct ControlRowDef {
    int32_t     actionId;
    const char* gxtKey;
    const char* rusLabel;
    const char* engLabel;
};

inline std::vector<ControlRowDef> BuildControlRows(bool isVehicle, bool isJoypad) {
    if (isVehicle) {
        return {
            {18, "FEC_FIR", "Огонь",                "Fire"},
            {19, "FEC_FIA", "Альтернативный огонь", "Secondary Fire"},
            {24, "FEC_ACC", "Газ",                  "Accelerate"},
            {25, "FEC_BRA", "Тормоз и задний ход",  "Brake / Reverse"},
            {20, "FEC_LEF", "Поворот влево",        "Steer Left"},
            {21, "FEC_RIG", "Поворот вправо",       "Steer Right"},
            {22, "FEC_PLU", "Наклон вперед",        "Steer Forward / Down"},
            {23, "FEC_PLD", "Наклон назад",         "Steer Back / Up"},
            {10, "FEC_EEX", "Войти / выйти",        "Enter / Exit"},
            {47, "FEC_TSK", "Пропуск поездки",      "Trip Skip"},
            {26, "FEC_RSC", "След. радиостанция",   "Next Radio Station"},
            {27, "FEC_RSP", "Пред. радиостанция",   "Prev Radio Station"},
            {28, "FEC_RTS", "След. свой трек",      "User Track Skip"},
            {29, "FEC_HRN", "Сигнал",               "Horn"},
            {30, "FEC_SUB", "Доп. миссия",          "Sub-Mission"},
            {11, "FEC_CMR", "Смена камеры",         "Change Camera"},
            {31, "FEC_HND", "Ручной тормоз",        "Handbrake"},
            {36, "FEC_LBA", "Взгляд назад",         "Look Behind"},
            {37, "FEC_VML", "Обзор мышью",          "Mouse Look"},
            {34, "FEC_LOL", "Взгляд влево",         "Look Left"},
            {35, "FEC_LOR", "Взгляд вправо",        "Look Right"},
            {38, "FEC_TFL", "Спец. влево",          "Special Ctrl Left"},
            {39, "FEC_TFR", "Спец. вправо",         "Special Ctrl Right"},
            {41, "FEC_TFU", "Спец. вверх",          "Special Ctrl Up"},
            {40, "FEC_TFD", "Спец. вниз",           "Special Ctrl Down"},
        };
    }

    std::vector<ControlRowDef> foot = {
        {0,  "FEC_FIR", "Огонь",                "Fire"},
        {2,  "FEC_NWE", "Следующее оружие",     "Next Weapon"},
        {3,  "FEC_PWE", "Предыдущее оружие",    "Previous Weapon"},
        {49, "FEC_GPF", "Отряд - вперед",       "Group Control Forward"},
        {50, "FEC_GPB", "Отряд - назад",        "Group Control Back"},
        {48, "FEC_CON", "Ответ - нет",          "Conversation No"},
        {47, "FEC_COY", "Ответ - да",           "Conversation Yes"},
        {4,  "FEC_FOR", "Вперед",               "Forwards"},
        {5,  "FEC_BAC", "Назад",                "Backwards"},
        {6,  "FEC_LEF", "Влево",                "Left"},
        {7,  "FEC_RIG", "Вправо",               "Right"},
        {8,  "FEC_ZIN", "Приблизить прицел",    "Zoom In"},
        {9,  "FEC_ZOT", "Отдалить прицел",      "Zoom Out"},
        {10, "FEC_EEX", "Войти / выйти",        "Enter / Exit"},
        {11, "FEC_CMR", "Смена камеры",         "Change Camera"},
        {12, "FEC_JMP", "Прыжок",               "Jump"},
        {13, "FEC_SPN", "Бег",                  "Sprint"},
        {45, "FEC_TAR", "Прицелиться",          "Aim Weapon"},
        {15, "FEC_CRO", "Присесть",             "Crouch"},
        {16, "FEC_ANS", "Действие / звонок",    "Action / Answer Phone"},
        {17, "FEC_PDW", "Шаг",                  "Walk"},
        {14, "FEC_LBA", "Взгляд назад",         "Look Behind"},
    };
    if (isJoypad) {
        foot.push_back({32, "FEC_LOL", "Обзор влево",       "Look Left"});
        foot.push_back({33, "FEC_LOR", "Обзор вправо",      "Look Right"});
        foot.push_back({51, "FEC_LUD", "Обзор вверх",       "Look Up"});
        foot.push_back({52, "FEC_LDU", "Обзор вниз",        "Look Down"});
        foot.push_back({44, "FEC_CEN", "Центр камеры",      "Center Camera"});
        foot.push_back({1,  "FEC_FIA", "Альт. огонь",       "Secondary Fire"});
    }
    return foot;
}

} // namespace modern_menu::sdk
