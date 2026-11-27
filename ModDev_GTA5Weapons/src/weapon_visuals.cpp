#include "weapon_visuals.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "integrations/proper_shaders.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "sa_sdk.hpp"
#include "weapon_config.hpp"
#include "weapon_storage.hpp"

namespace wvis {

namespace {

using FnVoid        = void(__cdecl*)();
using FnClumpRender = void* (__cdecl*)(void*);

FnVoid        g_origWeaponEffectsRender = nullptr;
FnClumpRender g_origClumpRenderRight    = nullptr;
FnClumpRender g_origClumpRenderLeft     = nullptr;

bool                 g_previewActive   = false;
uint32_t             g_previewWeaponId = 0;
wdata::WeaponLoadout g_previewLoadout{};

// Captured exact live hand LTM matrices from CVisibilityPlugins::RenderWeaponPedsForPC!
// Supports BOTH single-hand weapons (count=1) AND Hitman Dual-Wield (right + left hand, count=2)!
sa::CMatrix g_capturedLtms[2]  = {};
int         g_capturedLtmCount = 0;
uint32_t    g_capturedFrameMs  = 0;

inline sa::CVector Cross(const sa::CVector& a, const sa::CVector& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline sa::CVector Normalize(const sa::CVector& v, const sa::CVector& fallback) {
    float len = v.Length();
    if (len < 0.0001f) return fallback;
    return {v.x / len, v.y / len, v.z / len};
}

// Barrel-aligned orthonormal basis inside weapon clump local space:
//   origin = receiver breech (rx, ry, rz)
//   fwd    = true barrel axis from receiver to muzzle tip
//   side   = horizontal perpendicular (+Y side)
//   up     = perpendicular up (+Z top rail)
struct BarrelFrame {
    sa::CVector origin;
    sa::CVector fwd;
    sa::CVector side;
    sa::CVector up;
    float       barrelLen;

    static BarrelFrame FromMount(const wdata::MountProfile& m) {
        BarrelFrame bf;
        bf.origin = {m.rx, m.ry, m.rz};
        sa::CVector diff = {m.mx - m.rx, m.my - m.ry, m.mz - m.rz};
        bf.barrelLen = std::max(0.10f, diff.Length());
        bf.fwd  = Normalize(diff, {1.0f, 0.0f, 0.0f});
        sa::CVector worldUp{0.0f, 0.0f, 1.0f};
        bf.side = Normalize(Cross(worldUp, bf.fwd), {0.0f, 1.0f, 0.0f});
        bf.up   = Normalize(Cross(bf.fwd, bf.side), {0.0f, 0.0f, 1.0f});
        return bf;
    }

    // Convert barrel-space (along, rightSide, above) to weapon clump local coordinates!
    sa::CVector Pt(float along, float s, float u) const {
        return {
            origin.x + fwd.x * along + side.x * s + up.x * u,
            origin.y + fwd.y * along + side.y * s + up.y * u,
            origin.z + fwd.z * along + side.z * s + up.z * u
        };
    }
    sa::CVector Dir(float df, float ds, float du) const {
        return {
            fwd.x * df + side.x * ds + up.x * du,
            fwd.y * df + side.y * ds + up.y * du,
            fwd.z * df + side.z * ds + up.z * du
        };
    }
    // Map a clump-local X coordinate to barrel-line distance from origin.x
    float AlongFromX(float localX) const {
        float denom = (std::fabs(fwd.x) > 0.2f) ? fwd.x : 1.0f;
        return (localX - origin.x) / denom;
    }
};

struct MeshBuilder {
    std::vector<sa::rw::RwIm3DVertex> verts;
    std::vector<uint16_t>             indices;

    void Clear() {
        verts.clear();
        indices.clear();
    }

    static uint32_t Shade(uint8_t r, uint8_t g, uint8_t b, const sa::CVector& localN, const sa::CMatrix& ltm, uint8_t a = 255) {
        sa::CVector wn = Normalize(ltm.TransformDir(localN), {0.0f, 0.0f, 1.0f});
        float ndotl = wn.x * 0.35f + wn.y * 0.25f + wn.z * 0.88f;
        float lit   = std::clamp(0.46f + 0.54f * (ndotl * 0.5f + 0.5f), 0.30f, 1.15f);
        auto cl = [&](uint8_t c) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(static_cast<int>(c * lit), 0, 255));
        };
        return sa::rw::PackColor(cl(r), cl(g), cl(b), a);
    }

    void AddQuad(const sa::CVector& p0, const sa::CVector& p1, const sa::CVector& p2, const sa::CVector& p3,
                 const sa::CVector& localN, uint8_t r, uint8_t g, uint8_t b, const sa::CMatrix& ltm, uint8_t a = 255)
    {
        uint32_t col = Shade(r, g, b, localN, ltm, a);
        uint16_t base = static_cast<uint16_t>(verts.size());
        verts.push_back({p0.x, p0.y, p0.z, localN.x, localN.y, localN.z, col, 0.0f, 0.0f});
        verts.push_back({p1.x, p1.y, p1.z, localN.x, localN.y, localN.z, col, 0.0f, 0.0f});
        verts.push_back({p2.x, p2.y, p2.z, localN.x, localN.y, localN.z, col, 0.0f, 0.0f});
        verts.push_back({p3.x, p3.y, p3.z, localN.x, localN.y, localN.z, col, 0.0f, 0.0f});
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    }

    // Box in barrel-aligned coordinates (a0..a1 along barrel, s0..s1 sideways, u0..u1 up)
    void AddBoxB(const BarrelFrame& bf, float a0, float a1, float s0, float s1, float u0, float u1,
                 uint8_t r, uint8_t g, uint8_t b, const sa::CMatrix& ltm, uint8_t a = 255)
    {
        AddQuad(bf.Pt(a1,s0,u0), bf.Pt(a1,s1,u0), bf.Pt(a1,s1,u1), bf.Pt(a1,s0,u1), bf.Dir( 1, 0, 0), r,g,b, ltm, a);
        AddQuad(bf.Pt(a0,s1,u0), bf.Pt(a0,s0,u0), bf.Pt(a0,s0,u1), bf.Pt(a0,s1,u1), bf.Dir(-1, 0, 0), r,g,b, ltm, a);
        AddQuad(bf.Pt(a0,s1,u0), bf.Pt(a1,s1,u0), bf.Pt(a1,s1,u1), bf.Pt(a0,s1,u1), bf.Dir( 0, 1, 0), r,g,b, ltm, a);
        AddQuad(bf.Pt(a1,s0,u0), bf.Pt(a0,s0,u0), bf.Pt(a0,s0,u1), bf.Pt(a1,s0,u1), bf.Dir( 0,-1, 0), r,g,b, ltm, a);
        AddQuad(bf.Pt(a0,s0,u1), bf.Pt(a1,s0,u1), bf.Pt(a1,s1,u1), bf.Pt(a0,s1,u1), bf.Dir( 0, 0, 1), r,g,b, ltm, a);
        AddQuad(bf.Pt(a0,s1,u0), bf.Pt(a1,s1,u0), bf.Pt(a1,s0,u0), bf.Pt(a0,s0,u0), bf.Dir( 0, 0,-1), r,g,b, ltm, a);
    }

    // Cylinder coaxial with barrel (+fwd)
    void AddCylBarrel(const BarrelFrame& bf, float a0, float a1, float cs, float cu, float radius, int sides,
                      uint8_t r, uint8_t g, uint8_t b, const sa::CMatrix& ltm)
    {
        constexpr float PI2 = 6.28318530718f;
        sa::CVector nf = bf.Dir( 1, 0, 0);
        sa::CVector nb = bf.Dir(-1, 0, 0);
        uint32_t capF  = Shade(r / 2, g / 2, b / 2, nf, ltm);
        uint32_t capB  = Shade(r, g, b,             nb, ltm);

        for (int i = 0; i < sides; ++i) {
            float t0 = PI2 * static_cast<float>(i) / static_cast<float>(sides);
            float t1 = PI2 * static_cast<float>(i + 1) / static_cast<float>(sides);
            float s0 = cs + std::cos(t0) * radius, u0 = cu + std::sin(t0) * radius;
            float s1 = cs + std::cos(t1) * radius, u1 = cu + std::sin(t1) * radius;
            float tm = 0.5f * (t0 + t1);
            sa::CVector nSide = bf.Dir(0.0f, std::cos(tm), std::sin(tm));
            AddQuad(bf.Pt(a0,s0,u0), bf.Pt(a1,s0,u0), bf.Pt(a1,s1,u1), bf.Pt(a0,s1,u1), nSide, r, g, b, ltm);

            sa::CVector cF = bf.Pt(a1, cs, cu), pF0 = bf.Pt(a1, s0, u0), pF1 = bf.Pt(a1, s1, u1);
            uint16_t bfIdx = static_cast<uint16_t>(verts.size());
            verts.push_back({cF.x,  cF.y,  cF.z,  nf.x,nf.y,nf.z, capF, 0,0});
            verts.push_back({pF0.x, pF0.y, pF0.z, nf.x,nf.y,nf.z, capF, 0,0});
            verts.push_back({pF1.x, pF1.y, pF1.z, nf.x,nf.y,nf.z, capF, 0,0});
            indices.push_back(bfIdx); indices.push_back(bfIdx + 1); indices.push_back(bfIdx + 2);

            sa::CVector cB = bf.Pt(a0, cs, cu), pB0 = bf.Pt(a0, s0, u0), pB1 = bf.Pt(a0, s1, u1);
            uint16_t bbIdx = static_cast<uint16_t>(verts.size());
            verts.push_back({cB.x,  cB.y,  cB.z,  nb.x,nb.y,nb.z, capB, 0,0});
            verts.push_back({pB1.x, pB1.y, pB1.z, nb.x,nb.y,nb.z, capB, 0,0});
            verts.push_back({pB0.x, pB0.y, pB0.z, nb.x,nb.y,nb.z, capB, 0,0});
            indices.push_back(bbIdx); indices.push_back(bbIdx + 1); indices.push_back(bbIdx + 2);
        }
    }

    // Cylinder pointing vertically along weapon up (-up -> +up) — for Vertical Foregrip
    void AddCylUp(const BarrelFrame& bf, float ca, float cs, float u0, float u1, float radius, int sides,
                  uint8_t r, uint8_t g, uint8_t b, const sa::CMatrix& ltm)
    {
        constexpr float PI2 = 6.28318530718f;
        for (int i = 0; i < sides; ++i) {
            float t0 = PI2 * static_cast<float>(i) / static_cast<float>(sides);
            float t1 = PI2 * static_cast<float>(i + 1) / static_cast<float>(sides);
            float a0 = ca + std::cos(t0) * radius, s0 = cs + std::sin(t0) * radius;
            float a1 = ca + std::cos(t1) * radius, s1 = cs + std::sin(t1) * radius;
            float tm = 0.5f * (t0 + t1);
            AddQuad(bf.Pt(a0,s0,u0), bf.Pt(a1,s1,u0), bf.Pt(a1,s1,u1), bf.Pt(a0,s0,u1),
                    bf.Dir(std::cos(tm), std::sin(tm), 0.0f), r, g, b, ltm);
        }
    }

    // Cylinder oriented laterally (sideways along weapon side axis) — for Drum Magazine!
    void AddCylSide(const BarrelFrame& bf, float ca, float s0, float s1, float cu, float radius, int sides,
                    uint8_t r, uint8_t g, uint8_t b, const sa::CMatrix& ltm)
    {
        constexpr float PI2 = 6.28318530718f;
        sa::CVector n0 = bf.Dir(0,-1,0);
        sa::CVector n1 = bf.Dir(0, 1,0);
        uint32_t c0 = Shade(r, g, b, n0, ltm);
        uint32_t c1 = Shade(r, g, b, n1, ltm);

        for (int i = 0; i < sides; ++i) {
            float t0 = PI2 * static_cast<float>(i) / static_cast<float>(sides);
            float t1 = PI2 * static_cast<float>(i + 1) / static_cast<float>(sides);
            float a0 = ca + std::cos(t0) * radius, u0 = cu + std::sin(t0) * radius;
            float a1 = ca + std::cos(t1) * radius, u1 = cu + std::sin(t1) * radius;
            float tm = 0.5f * (t0 + t1);
            AddQuad(bf.Pt(a0,s0,u0), bf.Pt(a0,s1,u0), bf.Pt(a1,s1,u1), bf.Pt(a1,s0,u1),
                    bf.Dir(std::cos(tm), 0.0f, std::sin(tm)), r, g, b, ltm);

            sa::CVector ctr0 = bf.Pt(ca, s0, cu), p0 = bf.Pt(a0, s0, u0), p1 = bf.Pt(a1, s0, u1);
            uint16_t b0 = static_cast<uint16_t>(verts.size());
            verts.push_back({ctr0.x, ctr0.y, ctr0.z, n0.x,n0.y,n0.z, c0, 0,0});
            verts.push_back({p0.x,   p0.y,   p0.z,   n0.x,n0.y,n0.z, c0, 0,0});
            verts.push_back({p1.x,   p1.y,   p1.z,   n0.x,n0.y,n0.z, c0, 0,0});
            indices.push_back(b0); indices.push_back(b0 + 1); indices.push_back(b0 + 2);

            sa::CVector ctr1 = bf.Pt(ca, s1, cu), q0 = bf.Pt(a0, s1, u0), q1 = bf.Pt(a1, s1, u1);
            uint16_t b1 = static_cast<uint16_t>(verts.size());
            verts.push_back({ctr1.x, ctr1.y, ctr1.z, n1.x,n1.y,n1.z, c1, 0,0});
            verts.push_back({q1.x,   q1.y,   q1.z,   n1.x,n1.y,n1.z, c1, 0,0});
            verts.push_back({q0.x,   q0.y,   q0.z,   n1.x,n1.y,n1.z, c1, 0,0});
            indices.push_back(b1); indices.push_back(b1 + 1); indices.push_back(b1 + 2);
        }
    }

    void Flush(const sa::CMatrix* ltm) {
        if (verts.empty() || indices.empty()) return;
        if (sa::rw::Im3DTransform(verts.data(), static_cast<uint32_t>(verts.size()), ltm, sa::rw::IM3D_VERTEXRGBA)) {
            sa::rw::Im3DRenderIndexedPrimitive(sa::rw::PRIM_TRILIST, indices.data(), static_cast<int32_t>(indices.size()));
            sa::rw::Im3DEnd();
        }
    }
};

void RenderAttachmentsAndLaser() {
    if (sa::GetGameState() != 9 || sa::IsCutsceneRunning() || sa::IsPauseMenuActive()) return;
    if (!wcfg::Get().enable3DAttachments && !wcfg::Get().enableLaserSight) return;

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;
    if (ped->IsInVehicle() && !sa::camera::IsAimingWeaponCam()) return;

    sa::CWeapon* active = ped->GetActiveWeapon();
    if (!active || active->m_nType == 0) return;

    uint32_t wid = active->m_nType;
    const wdata::WeaponDef* def = wdata::FindDef(wid);
    if (!def || def->supportMask == 0) return;

    const wdata::WeaponLoadout* L = nullptr;
    if (g_previewActive && g_previewWeaponId == wid) {
        L = &g_previewLoadout;
    } else {
        L = wstore::Store().Find(wid);
    }
    const wdata::WeaponLoadout cleanL = wdata::SanitizeLoadout(wid, *L);
    if (!cleanL.HasAnyMod()) return;
    L = &cleanL;

    void* clump = ped->GetWeaponClump();
    if (!clump) return;

    // Gather 1 matrix for normal guns, OR 2 matrices (Right + Left Hand!) for Dual-Wield Hitman weapons!
    sa::CMatrix ltms[2];
    int ltmCount = 0;
    if (g_capturedLtmCount > 0 && (sa::GetTimeMs() - g_capturedFrameMs <= 100)) {
        ltmCount = std::min(g_capturedLtmCount, 2);
        for (int i = 0; i < ltmCount; ++i) ltms[i] = g_capturedLtms[i];
    } else {
        void* frame = sa::rw::GetClumpFrame(clump);
        if (!frame) return;
        const sa::CMatrix* fallbackLtm = sa::rw::FrameGetLTM(frame);
        if (!fallbackLtm) return;
        ltms[0]  = *fallbackLtm;
        ltmCount = 1;
    }

    const auto& m        = def->mount;
    const BarrelFrame bf = BarrelFrame::FromMount(m);
    const float br       = std::max(0.012f, m.barrelRadius);
    const float tipA     = bf.barrelLen;

    // RAII Proper Shaders / RenderWare 3.6 state guard (preserves SHADEMODE=7, FOG, ALPHATEST, FILTER!)
    integrations::proper_shaders::RwRenderStateGuard rwGuard;

    for (int hand = 0; hand < ltmCount; ++hand) {
        const sa::CMatrix* ltm = &ltms[hand];
        rwGuard.Configure3DUnlit(true, true, false);

        if (wcfg::Get().enable3DAttachments) {
            static MeshBuilder mb;
            mb.Clear();

            // 1. MUZZLE: Coaxial Suppressor (1) or Machined Compensator (2)
            if (L->muzzle == 1 && wid != 23) {
                float sLen = std::max(0.13f, m.suppressorLen);
                mb.AddCylBarrel(bf, tipA - 0.020f, tipA + 0.004f, 0.0f, 0.0f, br * 0.96f, 10, 62, 66, 72, *ltm);
                mb.AddCylBarrel(bf, tipA + 0.002f, tipA + sLen,   0.0f, 0.0f, br * 1.24f, 12, 32, 34, 38, *ltm);
                mb.AddCylBarrel(bf, tipA + sLen - 0.012f, tipA + sLen + 0.003f, 0.0f, 0.0f, br * 1.30f, 12, 54, 58, 64, *ltm);
            } else if (L->muzzle == 2) {
                float cLen = 0.068f;
                float hw   = br * 1.18f;
                mb.AddBoxB(bf, tipA - 0.016f, tipA + cLen, -hw, hw, -br * 1.05f, br * 1.05f, 44, 48, 54, *ltm);
                mb.AddBoxB(bf, tipA + 0.008f, tipA + 0.020f, -hw * 1.12f, hw * 1.12f, -br * 0.55f, br * 0.55f, 92, 98, 108, *ltm);
                mb.AddBoxB(bf, tipA + 0.032f, tipA + 0.044f, -hw * 1.12f, hw * 1.12f, -br * 0.55f, br * 0.55f, 92, 98, 108, *ltm);
            }

            // 2. HEAVY BARREL: Flush fluted sleeve hugging the front barrel
            if (L->barrel == 1 && tipA > 0.25f) {
                mb.AddCylBarrel(bf, tipA * 0.62f, tipA - 0.012f, 0.0f, 0.0f, br * 1.06f, 10, 44, 47, 52, *ltm);
            }

            // 3. OPTICS: Seated flush on DFF receiver top rail
            if (L->scope == 1) {
                float sa_x = bf.AlongFromX(m.scopeX);
                float sz   = m.scopeRailDz;
                mb.AddBoxB(bf, sa_x - 0.042f, sa_x + 0.042f, -0.016f, 0.016f, sz - 0.012f, sz + 0.002f, 38, 41, 46, *ltm);
                mb.AddBoxB(bf, sa_x - 0.028f, sa_x + 0.028f, -0.018f,-0.012f, sz + 0.002f, sz + 0.030f, 30, 33, 38, *ltm);
                mb.AddBoxB(bf, sa_x - 0.028f, sa_x + 0.028f,  0.012f, 0.018f, sz + 0.002f, sz + 0.030f, 30, 33, 38, *ltm);
                mb.AddBoxB(bf, sa_x - 0.028f, sa_x + 0.028f, -0.018f, 0.018f, sz + 0.026f, sz + 0.032f, 30, 33, 38, *ltm);
                mb.AddBoxB(bf, sa_x - 0.004f, sa_x + 0.004f, -0.003f, 0.003f, sz + 0.013f, sz + 0.019f, 255, 40, 40, *ltm);
            } else if (L->scope == 2) {
                float sa_x = bf.AlongFromX(m.scopeX);
                float sz   = m.scopeRailDz + 0.010f;
                mb.AddBoxB(bf, sa_x - 0.060f, sa_x + 0.060f, -0.014f, 0.014f, sz - 0.026f, sz - 0.014f, 38, 41, 46, *ltm);
                mb.AddBoxB(bf, sa_x - 0.048f, sa_x - 0.032f, -0.015f, 0.015f, sz - 0.018f, sz + 0.004f, 44, 48, 54, *ltm);
                mb.AddBoxB(bf, sa_x + 0.032f, sa_x + 0.048f, -0.015f, 0.015f, sz - 0.018f, sz + 0.004f, 44, 48, 54, *ltm);
                mb.AddCylBarrel(bf, sa_x - 0.090f, sa_x + 0.088f, 0.0f, sz + 0.010f, 0.017f, 12, 28, 31, 36, *ltm);
                mb.AddCylBarrel(bf, sa_x + 0.072f, sa_x + 0.125f, 0.0f, sz + 0.010f, 0.022f, 12, 36, 40, 46, *ltm);
                mb.AddCylBarrel(bf, sa_x - 0.108f, sa_x - 0.076f, 0.0f, sz + 0.010f, 0.020f, 12, 36, 40, 46, *ltm);
                mb.AddBoxB(bf, sa_x + 0.123f, sa_x + 0.127f, -0.014f, 0.014f, sz - 0.004f, sz + 0.024f, 55, 130, 210, *ltm);
            }

            // 4. VERTICAL FOREGRIP
            if (L->grip == 1) {
                float ga = bf.AlongFromX(m.gripX);
                float gu = m.gripUnderDz;
                mb.AddBoxB(bf, ga - 0.024f, ga + 0.024f, -0.017f, 0.017f, gu - 0.014f, gu + 0.012f, 42, 46, 50, *ltm);
                mb.AddCylUp(bf, ga, 0.0f, gu - 0.115f, gu - 0.010f, 0.016f, 10, 30, 32, 36, *ltm);
                mb.AddCylUp(bf, ga, 0.0f, gu - 0.122f, gu - 0.108f, 0.019f, 10, 56, 60, 66, *ltm);
            }

            // 5. MAGAZINE: Extended Mag (1) or Full-Coverage Tactical Drum Mag (2)
            if (L->mag == 1) {
                const bool isPistol = (wid == 22 || wid == 23 || wid == 24);
                float ma   = bf.AlongFromX(m.magX);
                float topU = m.magTopZ - m.rz;
                float botU = m.magBotZ - m.rz - (isPistol ? 0.052f : 0.055f);
                float hw   = isPistol ? std::max(0.016f, m.magHalfX) : std::max(0.020f, m.magHalfX + 0.006f);
                float hs   = isPistol ? 0.014f : 0.024f;
                if (wid == 25 || wid == 26) {
                    mb.AddCylBarrel(bf, ma - m.magHalfX, tipA - 0.015f, 0.0f, -br * 1.9f, br * 0.85f, 10, 38, 42, 48, *ltm);
                } else {
                    mb.AddBoxB(bf, ma - hw, ma + hw, -hs, hs, botU + 0.015f, topU + 0.015f, 32, 35, 40, *ltm);
                    mb.AddBoxB(bf, ma - hw * 1.12f, ma + hw * 1.12f, -hs * 1.12f, hs * 1.12f, botU, botU + 0.017f, 64, 68, 76, *ltm);
                }
            } else if (L->mag == 2) {
                float ma   = bf.AlongFromX(m.magX);
                float topU = m.magTopZ - m.rz;
                float botU = m.magBotZ - m.rz;
                float span = std::max(0.11f, (topU - botU));
                // Generous RPK/C-Mag drum radius + full-height feed tower covering 100% of vanilla stick/banana mag!
                float rad  = std::clamp(span * 0.56f, 0.062f, 0.108f);
                float ctrU = botU + rad - 0.012f;
                float hw   = std::max(0.028f, m.magHalfX + 0.006f);
                mb.AddBoxB(bf, ma - hw, ma + hw, -0.028f, 0.028f, ctrU, topU + 0.032f, 34, 37, 42, *ltm);
                mb.AddCylSide(bf, ma, -0.048f, 0.048f, ctrU, rad, 16, 28, 31, 36, *ltm);
                mb.AddCylSide(bf, ma, -0.052f, 0.052f, ctrU, rad * 0.34f, 12, 68, 72, 80, *ltm);
            }

            // 6. TACTICAL LASER MODULE
            if (L->laser != 0) {
                float la = bf.AlongFromX(m.laserX);
                float ls = m.laserSideDy;
                float lu = -0.004f;
                mb.AddBoxB(bf, la - 0.036f, la + 0.036f, ls - 0.013f, ls + 0.013f, lu - 0.012f, lu + 0.012f, 40, 44, 48, *ltm);
                uint8_t er = (L->laser == 1) ? 255 : 35;
                uint8_t eg = (L->laser == 1) ? 35  : 255;
                uint8_t eb = (L->laser == 1) ? 35  : 70;
                mb.AddBoxB(bf, la + 0.034f, la + 0.040f, ls - 0.005f, ls + 0.005f, lu - 0.005f, lu + 0.005f, er, eg, eb, *ltm);
            }

            mb.Flush(ltm);
        }

        // 7. 3D LASER BEAM + TARGET DOT (from BOTH hands if dual-wielding!)
        if (wcfg::Get().enableLaserSight && L->laser != 0) {
            const bool isAiming = sa::camera::IsAimingWeaponCam();
            if (isAiming || g_previewActive) {
                float la = bf.AlongFromX(m.laserX) + 0.042f;
                sa::CVector startLocal = bf.Pt(la, m.laserSideDy, -0.004f);
                sa::CVector startW     = ltm->TransformPoint(startLocal);

                sa::CVector endW{};
                sa::CVector rayDir{};
                bool hit = false;

                if (isAiming && !g_previewActive) {
                    sa::CVector camSrc{}, aimEnd{};
                    sa::camera::Find3rdPersonCamTargetVector(95.0f, startW, &camSrc, &aimEnd);

                    sa::CColPoint cp{};
                    sa::CEntity* hitEnt = nullptr;
                    hit = sa::ProcessLineOfSight(camSrc, aimEnd, cp, hitEnt,
                                                 true, true, true, true, false, false, false, false);
                    endW   = hit ? cp.m_vecPoint : aimEnd;
                    rayDir = Normalize(endW - startW, {1.0f, 0.0f, 0.0f});

                    sa::CColPoint cp2{};
                    sa::CEntity* hitEnt2 = nullptr;
                    if (sa::ProcessLineOfSight(startW, endW, cp2, hitEnt2,
                                               true, true, true, true, false, false, false, false))
                    {
                        endW = cp2.m_vecPoint;
                        hit  = true;
                    }
                } else {
                    rayDir = Normalize(ltm->TransformDir(bf.fwd), {1.0f, 0.0f, 0.0f});
                    endW   = startW + rayDir * 2.2f;
                    sa::CColPoint cp{};
                    sa::CEntity* hitEnt = nullptr;
                    if (sa::ProcessLineOfSight(startW, endW, cp, hitEnt,
                                               true, true, true, true, false, false, false, false))
                    {
                        endW = cp.m_vecPoint;
                        hit  = true;
                    }
                }

                uint8_t lr = (L->laser == 1) ? 255 : 40;
                uint8_t lg = (L->laser == 1) ? 28  : 255;
                uint8_t lb = (L->laser == 1) ? 28  : 75;

                rwGuard.Configure3DUnlit(true, false, true);

                sa::rw::RwIm3DVertex lineVerts[2] = {};
                lineVerts[0].x = startW.x; lineVerts[0].y = startW.y; lineVerts[0].z = startW.z;
                lineVerts[0].color = sa::rw::PackColor(lr, lg, lb, 215);
                lineVerts[1].x = endW.x;   lineVerts[1].y = endW.y;   lineVerts[1].z = endW.z;
                lineVerts[1].color = sa::rw::PackColor(lr, lg, lb, hit ? 185 : 35);

                if (sa::rw::Im3DTransform(lineVerts, 2, nullptr, sa::rw::IM3D_VERTEXRGBA)) {
                    sa::rw::Im3DRenderLine(0, 1);
                    sa::rw::Im3DEnd();
                }

                if (hit && hand == 0) {
                    sa::CVector dotPos = endW - rayDir * 0.035f;
                    sa::RegisterCorona(
                        0x5A7A5E01u, nullptr,
                        lr, lg, lb, 240,
                        dotPos, 0.085f, 120.0f,
                        0, 0,
                        false, false, 0, 0.0f, false,
                        0.05f, 0, 255.0f, false, false);
                }
            }
        }
    }
}

void CapturePlayerWeaponHandLtm(void* clump, int handSlot) {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || clump != ped->GetWeaponClump()) return;
    void* frame = sa::rw::GetClumpFrame(clump);
    if (!frame) return;
    const sa::CMatrix* ltm = sa::rw::FrameGetLTM(frame);
    if (!ltm) return;

    if (handSlot == 0) {
        g_capturedLtms[0]  = *ltm;
        g_capturedLtmCount = 1;
        g_capturedFrameMs  = sa::GetTimeMs();
    } else if (handSlot == 1) {
        g_capturedLtms[1]  = *ltm;
        g_capturedLtmCount = 2;
        g_capturedFrameMs  = sa::GetTimeMs();
    }
}

void* __cdecl HookedClumpRenderRight(void* clump) {
    CapturePlayerWeaponHandLtm(clump, 0);
    return g_origClumpRenderRight ? g_origClumpRenderRight(clump) : clump;
}

void* __cdecl HookedClumpRenderLeft(void* clump) {
    CapturePlayerWeaponHandLtm(clump, 1);
    return g_origClumpRenderLeft ? g_origClumpRenderLeft(clump) : clump;
}

void __cdecl HookedWeaponEffectsRender() {
    if (g_origWeaponEffectsRender) {
        g_origWeaponEffectsRender();
    }
    RenderAttachmentsAndLaser();
}

} // namespace

void InstallHooks() {
    g_origWeaponEffectsRender = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053E1E0, reinterpret_cast<uintptr_t>(&HookedWeaponEffectsRender)));
    // Capture BOTH Right-Hand (0x00733087) and Left-Hand Dual-Wield (0x00733119) weapon matrices!
    g_origClumpRenderRight = reinterpret_cast<FnClumpRender>(
        mem::HookCall(0x00733087, reinterpret_cast<uintptr_t>(&HookedClumpRenderRight)));
    g_origClumpRenderLeft = reinterpret_cast<FnClumpRender>(
        mem::HookCall(0x00733119, reinterpret_cast<uintptr_t>(&HookedClumpRenderLeft)));
    modlog::Info("[WVis] Hooked RenderEffects + Dual-Wield Right/Left weapon hands cleanly.");
}

void SetPreviewOverride(bool active, uint32_t weaponId, const wdata::WeaponLoadout& previewLoadout) {
    g_previewActive   = active;
    g_previewWeaponId = weaponId;
    g_previewLoadout  = previewLoadout;
}

} // namespace wvis
