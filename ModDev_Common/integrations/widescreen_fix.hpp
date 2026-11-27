#pragma once
#include <algorithm>
#include <cstdint>
#include <windows.h>

// ============================================================================
// Standalone Integration Module: ThirteenAG Widescreen Fix (Latest)
//
// Provides:
//  - Cached detection of GTASA.WidescreenFix.asi
//  - Aspect-ratio-corrected 640x448 virtual screen scaling (zero horizontal
//    stretching on 16:9 / 21:9 / 32:9 monitors, guarded against sh <= 0)
//  - ScopeFovGuard: captures & restores live Widescreen Fix dynamic FOV
//    instead of clobbering it back to vanilla 70.0 deg on scope un-zoom!
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
