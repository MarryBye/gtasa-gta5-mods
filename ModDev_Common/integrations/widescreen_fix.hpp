#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

// ============================================================================
// Standalone Integration Module: ThirteenAG Widescreen Fix (Latest v25112026+)
//
// Provides:
//  - Cached detection of GTASA.WidescreenFix.asi
//  - Aspect-ratio-corrected 640x448 virtual screen scaling (zero horizontal
//    stretching on 16:9 / 21:9 / 32:9 monitors, guarded against sh <= 0)
//  - ScopeFovGuard: captures & restores live Widescreen Fix dynamic FOV
//    instead of clobbering it back to vanilla 70.0 deg on scope un-zoom!
//  - Live menu patch detection for extended Frame Limiter (OFF / 30..240 FPS
//    hooked at 0x0057CEC7) and Cutscene Borders (OFF / Letterbox / Pillarbox /
//    Both hooked at 0x0057706B) + GTASA.WidescreenFix.ini feature summary.
// ============================================================================
namespace integrations::widescreen {

inline bool IsLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        s_state = (GetModuleHandleA("GTASA.WidescreenFix.asi") != nullptr ||
                   GetModuleHandleA("WidescreenFix.asi") != nullptr) ? 1 : 0;
    }
    return s_state == 1;
}

inline HMODULE GetModule() {
    HMODULE h = GetModuleHandleA("GTASA.WidescreenFix.asi");
    if (!h) h = GetModuleHandleA("WidescreenFix.asi");
    return h;
}

// Verified against GTASA.WidescreenFix.asi (v25112026):
//  - 0x0057CEC7 (CMenuManager::ProcessPCMenu -> mm[0x4C] FrameLimiter) patched with E9 JMP
//    to cycle 0..11 (OFF, 30, 40, 50, 60, 75, 100, 120, 144, 165, 200, 240 FPS).
//  - 0x0057706B (CMenuManager::ProcessMenuOptions -> mm[0x4B] Widescreen) patched with E9 JMP
//    to cycle 0..3 (OFF, LETTERBOX, PILLARBOX, LETTERBOX + PILLARBOX) as "CUTSCENE BORDERS".
inline bool IsFrameLimiterExtended() {
    if (!IsLoaded()) return false;
    return (*reinterpret_cast<const uint8_t*>(0x0057CEC7) == 0xE9);
}

inline bool IsCutsceneBordersActive() {
    if (!IsLoaded()) return false;
    return (*reinterpret_cast<const uint8_t*>(0x0057706B) == 0xE9);
}

struct LocalizedPair {
    const char* rus;
    const char* eng;
};

inline LocalizedPair GetFrameLimiterLabelPair(uint8_t rawVal) {
    if (!IsFrameLimiterExtended()) {
        return rawVal ? LocalizedPair{"ВКЛ", "ON"} : LocalizedPair{"ВЫКЛ", "OFF"};
    }
    static constexpr const char* kFpsLabels[12] = {
        nullptr, "30 FPS", "40 FPS", "50 FPS", "60 FPS", "75 FPS",
        "100 FPS", "120 FPS", "144 FPS", "165 FPS", "200 FPS", "240 FPS"
    };
    if (rawVal == 0 || rawVal >= 12) {
        return {"ВЫКЛ", "OFF"};
    }
    return {kFpsLabels[rawVal], kFpsLabels[rawVal]};
}

inline LocalizedPair GetCutsceneBordersLabelPair(uint8_t rawVal) {
    if (!IsCutsceneBordersActive()) {
        return rawVal ? LocalizedPair{"ВКЛ", "ON"} : LocalizedPair{"ВЫКЛ", "OFF"};
    }
    switch (rawVal) {
        case 1:  return {"Сверху и снизу", "Letterbox"};
        case 2:  return {"По бокам",       "Pillarbox"};
        case 3:  return {"Со всех сторон", "Letterbox + Pillarbox"};
        default: return {"ВЫКЛ",           "OFF"};
    }
}

// ASCII-safe English helper for diagnostic logs
inline std::string FormatFrameLimiterValue(uint8_t rawVal, bool /*useRus*/ = false) {
    return GetFrameLimiterLabelPair(rawVal).eng;
}

inline std::string FormatCutsceneBordersValue(uint8_t rawVal, bool /*useRus*/ = false) {
    return GetCutsceneBordersLabelPair(rawVal).eng;
}

struct WsfIniSummary {
    bool loaded          = false;
    int  menuFrameLimit  = 0;
    bool smaa            = false;
    bool transparentMenu = false;
    bool fullscreenMap   = false;
    std::string iniPath;
};

inline const WsfIniSummary& GetIniSummary(bool forceRefresh = false) {
    static WsfIniSummary s_sum{};
    static bool s_init = false;
    if (s_init && !forceRefresh) return s_sum;
    s_init = true;
    s_sum = {};

    HMODULE h = GetModule();
    if (!h) return s_sum;

    char modPath[MAX_PATH] = {};
    if (GetModuleFileNameA(h, modPath, MAX_PATH) == 0) return s_sum;
    std::string p(modPath);
    size_t dot = p.find_last_of('.');
    if (dot != std::string::npos) {
        p = p.substr(0, dot) + ".ini";
    }
    if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return s_sum;

    s_sum.loaded          = true;
    s_sum.iniPath         = p;
    s_sum.menuFrameLimit  = GetPrivateProfileIntA("MAIN", "MenuFrameLimit", 0, p.c_str());
    s_sum.smaa            = GetPrivateProfileIntA("GRAPHICS", "SMAA", 0, p.c_str()) != 0;
    s_sum.transparentMenu = GetPrivateProfileIntA("GRAPHICS", "TransparentMenu", 0, p.c_str()) != 0;
    s_sum.fullscreenMap   = GetPrivateProfileIntA("MISC", "FullscreenMap", 0, p.c_str()) != 0;
    return s_sum;
}

inline void GetScreenScales(float& sw, float& sh, float& xScale, float& sy, float uiScale = 1.0f) {
    sw = static_cast<float>(*reinterpret_cast<const int32_t*>(0x00C17044)); // RsGlobal.maximumWidth
    sh = static_cast<float>(*reinterpret_cast<const int32_t*>(0x00C17048)); // RsGlobal.maximumHeight
    if (sw <= 1.0f || sh <= 1.0f) {
        sw = 640.0f;
        sh = 448.0f;
    }

    const float mult = std::clamp(uiScale, 0.70f, 1.40f);
    sy = (sh / 448.0f) * mult;

    // Match ThirteenAG Widescreen Fix HUD aspect scaling (4:3 virtual width on widescreen displays)
    const float aspect = sw / sh;
    xScale = ((aspect > 1.4f) ? (sh * (4.0f / 3.0f) / 640.0f) : (sw / 640.0f)) * mult;
}

// Preserves Widescreen Fix's dynamic FOVControl value across temporary ADS scope zoom!
class ScopeFovGuard {
public:
    void CaptureBeforeZoom(float currentFov) {
        if (!m_captured && currentFov >= 50.0f && currentFov <= 120.0f) {
            m_baseFov  = currentFov;
            m_captured = true;
        }
    }

    float GetRestoreFov() const {
        return m_captured ? m_baseFov : 70.0f;
    }

    void Clear() {
        m_captured = false;
    }

    bool IsCaptured() const { return m_captured; }

private:
    float m_baseFov  = 70.0f;
    bool  m_captured = false;
};

} // namespace integrations::widescreen
