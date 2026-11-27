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
// Every offset and function address below is verified directly against gta_sa.exe.
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

constexpr int MAX_MENU_PAGES = 44;

inline CMenuScreen* GetScreens() {
    return reinterpret_cast<CMenuScreen*>(0x008CE008);
}

inline const CMenuScreen* GetScreen(int page) {
    if (page < 0 || page >= MAX_MENU_PAGES) return nullptr;
    return &GetScreens()[page];
}

// Verified CMenuManager (0x00BA6748) accessors
inline uint8_t* GetMenuManager() { return reinterpret_cast<uint8_t*>(0x00BA6748); }

inline int8_t&   CurrentPage(uint8_t* mm)       { return *reinterpret_cast<int8_t*>(mm + 0x15D); }
inline int8_t&   PreviousPage(uint8_t* mm)      { return *reinterpret_cast<int8_t*>(mm + 0x15E); }
inline int32_t&  SelectedItem(uint8_t* mm)      { return *reinterpret_cast<int32_t*>(mm + 0x54); }
inline uint8_t&  MenuActive(uint8_t* mm)        { return mm[0x5C]; }
inline uint8_t&  InGame(uint8_t* mm)            { return mm[0xE9]; }
inline uint8_t&  AllowBack(uint8_t* mm)         { return mm[0x1AE9]; }
inline uint8_t&  LoadingScreenFlag(uint8_t* mm) { return mm[0x1AEA]; }
inline uint8_t&  DontDrawFrontEnd(uint8_t* mm)  { return mm[0x32]; }
inline uint8_t&  TexturesLoaded(uint8_t* mm)    { return mm[0x15C]; }
inline uint8_t&  ShowMouse(uint8_t* mm)         { return mm[0xB8]; }
inline int32_t&  MouseWinX(uint8_t* mm)         { return *reinterpret_cast<int32_t*>(mm + 0xBC); }
inline int32_t&  MouseWinY(uint8_t* mm)         { return *reinterpret_cast<int32_t*>(mm + 0xC0); }
inline int32_t&  OldMouseWinX(uint8_t* mm)      { return *reinterpret_cast<int32_t*>(mm + 0x1AF8); }
inline int32_t&  OldMouseWinY(uint8_t* mm)      { return *reinterpret_cast<int32_t*>(mm + 0x1AFC); }

// Verified Option Fields on CMenuManager
inline uint8_t&  HudOn(uint8_t* mm)             { return mm[0x21]; }
inline int32_t&  RadarMode(uint8_t* mm)         { return *reinterpret_cast<int32_t*>(mm + 0x24); }
inline int32_t&  Brightness(uint8_t* mm)        { return *reinterpret_cast<int32_t*>(mm + 0x3C); } // 0..384
inline float&    DrawDistance(uint8_t* mm)      { return *reinterpret_cast<float*>(mm + 0x40); }   // 0.925..1.8
inline uint8_t&  ShowSubtitles(uint8_t* mm)     { return mm[0x44]; }
inline uint8_t&  FrameLimiter(uint8_t* mm)      { return mm[0x4A]; }
inline uint8_t&  Widescreen(uint8_t* mm)        { return mm[0x4B]; }
inline uint8_t&  MipMapping(uint8_t* mm)        { return mm[0x4C]; }
inline uint8_t&  RadioEq(uint8_t* mm)           { return mm[0x4D]; }
inline uint8_t&  SfxVolume(uint8_t* mm)         { return mm[0x4F]; } // 0..64
inline uint8_t&  RadioVolume(uint8_t* mm)       { return mm[0x50]; } // 0..64
inline uint8_t&  RadioAutoSelect(uint8_t* mm)   { return mm[0x51]; }
inline uint8_t&  RadioStation(uint8_t* mm)      { return mm[0x52]; }
inline uint8_t&  MapLoaded(uint8_t* mm)         { return mm[0x78]; }
inline int8_t&   Language(uint8_t* mm)          { return *reinterpret_cast<int8_t*>(mm + 0x84); }
inline int8_t&   RadioMode(uint8_t* mm)         { return *reinterpret_cast<int8_t*>(mm + 0xB0); }
inline uint8_t&  JoypadFlag(uint8_t* mm, int i) { return mm[0xB1 + std::clamp(i, 0, 5)]; }
inline uint8_t&  SavePhotos(uint8_t* mm)        { return mm[0xC4]; }
inline uint8_t&  ShowLegend(uint8_t* mm)        { return mm[0xC5]; }
inline int32_t&  PrefsAntialiasing(uint8_t* mm) { return *reinterpret_cast<int32_t*>(mm + 0xCC); } // 1..4
inline uint8_t&  ControlMethod(uint8_t* mm)     { return mm[0xD0]; } // 0=Mouse+Keys, 1=Joypad
inline int32_t&  DisplayVideoMode(uint8_t* mm)  { return *reinterpret_cast<int32_t*>(mm + 0xD4); }
inline int32_t&  PrefsVideoMode(uint8_t* mm)    { return *reinterpret_cast<int32_t*>(mm + 0xD8); }
inline uint8_t&  RadioAutoScan(uint8_t* mm)     { return mm[0xE8]; }

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

// Safely switches to ANY menu page including page 0 (MENUPAGE_STATS), where vanilla SwitchToNewScreen(0)
// reads m_aItems[curSel].targetMenu instead of navigating to page 0!
inline void NavigateToPage(uint8_t* mm, int8_t targetPage) {
    if (!mm) return;
    if (targetPage == 0) {
        // Run native SwitchToNewScreen cleanup first (saves settings, resets display preview, stops audio preview)
        int8_t anchor = InGame(mm) ? 41 : 34;
        if (CurrentPage(mm) != anchor) {
            SwitchToNewScreen(mm, anchor);
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
    if (!mm) return;
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
    // Verified CRect layout: {left, bottom, right, top}
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

} // namespace modern_menu::sdk
