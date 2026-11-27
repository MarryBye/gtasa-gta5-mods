#pragma once
#include <cstdint>

// ============================================================================
// Standalone Integration Module: Proper Shaders / RenderWare 3.6 State Guard
//
// RAII guard saving & restoring ALL RenderWare 3.6 render states touched by
// custom 2D/3D Im2D/Im3D drawing (RadialWheel, 3D weapon attachments, laser
// sight), preventing shader, fog, alpha-test, or texture-filter corruption
// under Proper Shaders and SkyGFX.
//
// Verified RenderWare 3.6 rwRenderState enum:
//   1 = TEXTURERASTER, 6 = ZTESTENABLE, 7 = SHADEMODE, 8 = ZWRITEENABLE,
//   9 = TEXTUREFILTER, 10 = SRCBLEND, 11 = DESTBLEND, 12 = VERTEXALPHAENABLE,
//   14 = FOGENABLE, 20 = CULLMODE, 22 = ALPHATESTFUNCTION, 23 = ALPHATESTFUNCTIONREF
// ============================================================================
namespace integrations::proper_shaders {

constexpr int32_t RW_TEXTURERASTER      = 1;
constexpr int32_t RW_ZTESTENABLE        = 6;
constexpr int32_t RW_SHADEMODE          = 7;  // 1=FLAT, 2=GOURAUD
constexpr int32_t RW_ZWRITEENABLE       = 8;
constexpr int32_t RW_TEXTUREFILTER      = 9;  // 1=NEAREST, 2=LINEAR
constexpr int32_t RW_SRCBLEND           = 10; // 5=SRCALPHA
constexpr int32_t RW_DESTBLEND          = 11; // 2=ONE, 6=INVSRCALPHA
constexpr int32_t RW_VERTEXALPHAENABLE  = 12;
constexpr int32_t RW_FOGENABLE          = 14;
constexpr int32_t RW_CULLMODE           = 20; // 1=NONE, 2=BACK
constexpr int32_t RW_ALPHATESTFUNCTION  = 22; // 8=ALWAYS
constexpr int32_t RW_ALPHATESTREF       = 23;

inline int32_t RwStateSet(int32_t state, uintptr_t value) {
    uint8_t* inst = *reinterpret_cast<uint8_t**>(0x00C97B24);
    if (!inst) return 0;
    using Fn = int32_t(__cdecl*)(int32_t, uintptr_t);
    auto fp = *reinterpret_cast<Fn*>(inst + 0x20);
    return fp ? fp(state, value) : 0;
}

inline int32_t RwStateGet(int32_t state, uintptr_t* outValue) {
    uint8_t* inst = *reinterpret_cast<uint8_t**>(0x00C97B24);
    if (!inst || !outValue) return 0;
    using Fn = int32_t(__cdecl*)(int32_t, uintptr_t*);
    auto fp = *reinterpret_cast<Fn*>(inst + 0x24);
    return fp ? fp(state, outValue) : 0;
}

class RwRenderStateGuard {
public:
    RwRenderStateGuard() {
        RwStateGet(RW_TEXTURERASTER,     &m_tex);
        RwStateGet(RW_ZTESTENABLE,       &m_zTest);
        RwStateGet(RW_SHADEMODE,         &m_shade);
        RwStateGet(RW_ZWRITEENABLE,      &m_zWrite);
        RwStateGet(RW_TEXTUREFILTER,     &m_filter);
        RwStateGet(RW_SRCBLEND,          &m_srcBlend);
        RwStateGet(RW_DESTBLEND,         &m_destBlend);
        RwStateGet(RW_VERTEXALPHAENABLE, &m_vertAlpha);
        RwStateGet(RW_FOGENABLE,         &m_fog);
        RwStateGet(RW_CULLMODE,          &m_cull);
        RwStateGet(RW_ALPHATESTFUNCTION, &m_alphaFunc);
        RwStateGet(RW_ALPHATESTREF,      &m_alphaRef);
    }

    ~RwRenderStateGuard() {
        RwStateSet(RW_TEXTURERASTER,     m_tex);
        RwStateSet(RW_ZTESTENABLE,       m_zTest);
        RwStateSet(RW_SHADEMODE,         m_shade);
        RwStateSet(RW_ZWRITEENABLE,      m_zWrite);
        RwStateSet(RW_TEXTUREFILTER,     m_filter);
        RwStateSet(RW_SRCBLEND,          m_srcBlend);
        RwStateSet(RW_DESTBLEND,         m_destBlend);
        RwStateSet(RW_VERTEXALPHAENABLE, m_vertAlpha);
        RwStateSet(RW_FOGENABLE,         m_fog);
        RwStateSet(RW_CULLMODE,          m_cull);
        RwStateSet(RW_ALPHATESTFUNCTION, m_alphaFunc);
        RwStateSet(RW_ALPHATESTREF,      m_alphaRef);
    }

    RwRenderStateGuard(const RwRenderStateGuard&) = delete;
    RwRenderStateGuard& operator=(const RwRenderStateGuard&) = delete;

    // Clean untextured alpha-blended 2D geometry (e.g. RadialWheel arcs & discs)
    void Configure2DFlat() const {
        RwStateSet(RW_TEXTURERASTER,     0);
        RwStateSet(RW_ZTESTENABLE,       0);
        RwStateSet(RW_ZWRITEENABLE,      0);
        RwStateSet(RW_SHADEMODE,         2); // GOURAUD
        RwStateSet(RW_VERTEXALPHAENABLE, 1);
        RwStateSet(RW_SRCBLEND,          5); // SRCALPHA
        RwStateSet(RW_DESTBLEND,         6); // INVSRCALPHA
        RwStateSet(RW_FOGENABLE,         0);
        RwStateSet(RW_CULLMODE,          1); // NONE
        RwStateSet(RW_ALPHATESTFUNCTION, 8); // ALWAYS
        RwStateSet(RW_ALPHATESTREF,      0);
    }

    // Clean untextured 3D Im3D rendering safe under Proper Shaders & dense fog
    void Configure3DUnlit(bool zTest, bool zWrite, bool additiveBlend) const {
        RwStateSet(RW_TEXTURERASTER,     0);
        RwStateSet(RW_ZTESTENABLE,       zTest  ? 1 : 0);
        RwStateSet(RW_ZWRITEENABLE,      zWrite ? 1 : 0);
        RwStateSet(RW_SHADEMODE,         2); // GOURAUD
        RwStateSet(RW_VERTEXALPHAENABLE, 1);
        RwStateSet(RW_SRCBLEND,          5); // SRCALPHA
        RwStateSet(RW_DESTBLEND,         additiveBlend ? 2 : 6); // ONE vs INVSRCALPHA
        RwStateSet(RW_FOGENABLE,         0);
        RwStateSet(RW_CULLMODE,          1); // NONE
        RwStateSet(RW_ALPHATESTFUNCTION, 8); // ALWAYS
        RwStateSet(RW_ALPHATESTREF,      0);
    }

private:
    uintptr_t m_tex       = 0;
    uintptr_t m_zTest     = 0;
    uintptr_t m_shade     = 0;
    uintptr_t m_zWrite    = 0;
    uintptr_t m_filter    = 0;
    uintptr_t m_srcBlend  = 0;
    uintptr_t m_destBlend = 0;
    uintptr_t m_vertAlpha = 0;
    uintptr_t m_fog       = 0;
    uintptr_t m_cull      = 0;
    uintptr_t m_alphaFunc = 0;
    uintptr_t m_alphaRef  = 0;
};

} // namespace integrations::proper_shaders
