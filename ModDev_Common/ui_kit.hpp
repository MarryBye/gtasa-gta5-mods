#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "sa_sdk.hpp"
#include "mod_bus.hpp"
#include "integrations/mskl_rus.hpp"
#include "integrations/sacoop.hpp"
#include "integrations/widescreen_fix.hpp"
#include "integrations/proper_shaders.hpp"

// ============================================================================
// Shared GTA SA React-Style Dumb-Component UI Kit (Golden Rule #1)
//
// Strict Bottom-Up Layer Architecture (Zero Business Logic!):
//   Tier 0   — ui::ThemeTokens (`ui::Theme()`)
//              Modernized Classic GTA SA palette, typography scales, borders &
//              WidescreenFix-matched scaling (synced live from GTA5UIKit.ini).
//   Layout   — ui::PositionMode (Relative / Absolute), ui::RectBox, ui::VStack
//   Tier 1   — ui::prim::* (True Atomic Primitives — ONLY layer touching
//              sa::font::DrawRect / CFont::PrintString / DrawSprite / Im2D!)
//              Rect, Border (4-edge, zero alpha-bleed!), Panel, Divider,
//              AccentStrip, Label (auto-fit), MeasureWrappedLines, WrappedText,
//              Icon, Arc, Disc.
//   Tier 1.5 — ui::widget::* (Reusable Micro-Components composed strictly from
//              Tier 1 atoms): Header, Row, Footer, ProgressBar, TextField,
//              Banner, Swatch, Badge, Button.
//   Tier 2   — Composite Dumb Containers & Windows (composed strictly from
//              Tier 1 + Tier 1.5): ListMenu, Grid, ColorGrid, StatsCard,
//              PhoneFrame, HelpToast (titled!), RadialWheel, TextInputModal.
// ============================================================================
namespace ui {

// ----------------------------------------------------------------------------
// Localization & Native Help Dispatch
// ----------------------------------------------------------------------------
inline bool& UseRussianFlag() {
    static bool useRus = true;
    return useRus;
}

inline void SetUseRussian(bool rus) { UseRussianFlag() = rus; }

inline std::string Loc(const char* rusUtf8, const char* engAscii) {
    return UseRussianFlag()
        ? integrations::mskl::ToGameRussian(rusUtf8 ? rusUtf8 : "")
        : integrations::mskl::ToGameAscii(engAscii ? engAscii : "");
}

inline void ShowHelp(const char* rusUtf8, const char* engAscii, bool quick = true) {
    std::string s = Loc(rusUtf8, engAscii);
    sa::SetHelpMessage(s.c_str(), quick, false, false);
}

// ----------------------------------------------------------------------------
// Frame-Deduplicated Keyboard Input Tracker (Safe for future Single-ASI merge!)
// ----------------------------------------------------------------------------
class KeyTracker {
public:
    void Update() {
        const uint32_t frame = sa::GetFrameCounter();
        if (m_lastFrame == frame && m_hasUpdatedOnce) return;
        m_lastFrame      = frame;
        m_hasUpdatedOnce = true;

        const uint32_t now = GetTickCount();
        for (int vk = 0; vk < 256; ++vk) {
            bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            m_justPressed[vk] = down && !m_wasDown[vk];
            m_repeated[vk]    = false;

            if (m_justPressed[vk]) {
                m_repeated[vk]   = true;
                m_nextRepeat[vk] = now + 340;
            } else if (down && static_cast<int32_t>(now - m_nextRepeat[vk]) >= 0) {
                m_repeated[vk]   = true;
                m_nextRepeat[vk] = now + 95;
            }
            m_wasDown[vk] = down;
        }
    }

    bool JustPressed(int vk) const { return (vk >= 0 && vk < 256) ? m_justPressed[vk] : false; }
    bool Repeated(int vk) const    { return (vk >= 0 && vk < 256) ? m_repeated[vk]    : false; }
    bool IsDown(int vk) const      { return (vk >= 0 && vk < 256) ? m_wasDown[vk]     : false; }

private:
    uint32_t m_lastFrame        = 0;
    bool     m_hasUpdatedOnce   = false;
    bool     m_wasDown[256]     = {};
    bool     m_justPressed[256] = {};
    bool     m_repeated[256]    = {};
    uint32_t m_nextRepeat[256]  = {};
};

inline KeyTracker& Keys() {
    static KeyTracker kt;
    return kt;
}

// ----------------------------------------------------------------------------
// Unified List & Camera Input Controller + Post-Close Shield (Rule 1d)
// ----------------------------------------------------------------------------
struct MenuInput {
    bool up     = false;
    bool down   = false;
    bool left   = false;
    bool right  = false;
    bool accept = false;
    bool back   = false;
    bool del    = false;
};

struct CameraInput {
    float orbitDelta = 0.0f; // Q (-) / E (+)
    float zoomDelta  = 0.0f; // Z (-) zoom in / X (+) zoom out
    bool  resetCam   = false; // C
};

inline void LockMenuInputBriefly(uint32_t ms = 200) {
    if (auto* b = modbus::GetBus()) {
        uint32_t now = GetTickCount();
        uint32_t tgt = now + ms;
        if (b->menuInputUnlockMs == 0 || static_cast<int32_t>(tgt - b->menuInputUnlockMs) > 0) {
            b->menuInputUnlockMs = tgt;
        }
    }
}

inline void TriggerPostCloseShield(uint32_t ms = 180) {
    if (auto* b = modbus::GetBus()) {
        uint32_t now = GetTickCount();
        uint32_t tgt = now + ms;
        if (b->postCloseShieldMs == 0 || static_cast<int32_t>(tgt - b->postCloseShieldMs) > 0) {
            b->postCloseShieldMs = tgt;
        }
    }
}

inline void UpdatePostCloseShield() {
    // CRITICAL: Never wipe pad inputs while ANY modal menu (including GTA5UIKit's vanilla menu driver
    // or a shop's forward-exit pulse) is open and injecting ButtonCross / ButtonTriangle into CTheScripts!
    if (modbus::IsAnyModalMenuOpen()) return;

    if (auto* b = modbus::GetBus(); b && b->postCloseShieldMs != 0) {
        const auto& k = Keys();
        bool stillHolding = k.IsDown(VK_RETURN) || k.IsDown('F') ||
                            k.IsDown(VK_BACK) || k.IsDown(VK_LBUTTON) || k.IsDown(VK_RBUTTON);
        bool timerActive = (static_cast<int32_t>(GetTickCount() - b->postCloseShieldMs) < 0);
        if (timerActive || stillHolding) {
            uint8_t* pad = sa::GetPad0();
            *reinterpret_cast<int16_t*>(pad + 0x0C) = 0; // RightShoulder1 (Aim)
            *reinterpret_cast<int16_t*>(pad + 0x22) = 0; // ButtonCircle (Fire/Punch)
            if (timerActive || k.IsDown(VK_RETURN) || k.IsDown('F')) {
                *reinterpret_cast<int16_t*>(pad + 0x1E) = 0; // ButtonTriangle
                *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
            }
            if (auto* ms = sa::GetNewMouseState()) {
                ms->lmb = 0;
                ms->rmb = 0;
            }
        } else {
            b->postCloseShieldMs = 0;
        }
    }
}

class ListInputController {
public:
    // Polls unified controls (gated on game focus & external SA-COOP overlays) and optionally locks CJ pad.
    // Preserves TAB automatically if Priority 1 Story Phone Call is ringing!
    // Note: ESC is intentionally NOT bound here so GTA SA's native Pause Menu stays 100% untouched.
    MenuInput Poll(bool blockPad = true, bool respectStoryPhone = true) const {
        MenuInput out{};
        auto* ms = sa::GetNewMouseState();
        const bool whUp = ms && (ms->wheelUp != 0);
        const bool whDn = ms && (ms->wheelDown != 0);

        bool debounced = false;
        if (auto* b = modbus::GetBus(); b && b->menuInputUnlockMs != 0) {
            if (static_cast<int32_t>(GetTickCount() - b->menuInputUnlockMs) < 0) {
                debounced = true;
            } else {
                b->menuInputUnlockMs = 0;
            }
        }

        const bool canAcceptInput = integrations::sacoop::IsGameWindowFocused() &&
                                    !integrations::sacoop::IsOverlayActive();
        if (!debounced && canAcceptInput) {
            const auto& k = Keys();
            const bool lmbJust = k.JustPressed(VK_LBUTTON);
            const bool rmbJust = k.JustPressed(VK_RBUTTON);

            out.up     = whUp || k.Repeated(VK_UP)   || k.Repeated('W');
            out.down   = whDn || k.Repeated(VK_DOWN) || k.Repeated('S');
            out.left   = k.JustPressed(VK_LEFT)  || k.JustPressed('A');
            out.right  = k.JustPressed(VK_RIGHT) || k.JustPressed('D');
            out.accept = lmbJust || k.JustPressed(VK_RETURN) || k.JustPressed('F');
            out.back   = rmbJust || k.JustPressed(VK_BACK);
            out.del    = k.JustPressed(VK_DELETE);

            if (out.accept || out.back) {
                LockMenuInputBriefly(135);
            }
        }

        if (blockPad) {
            bool preserveTab = respectStoryPhone && (modbus::IsStoryPhoneBlocking() || sa::IsStoryPhoneRinging());
            sa::ClearPadModalInputs(preserveTab);
        }
        return out;
    }

    // Pure dumb cursor step helper (no side-effect sound calls inside UI Kit)
    bool ApplyToCursor(int& cursor, int itemCount, const MenuInput& in) const {
        if (itemCount <= 0) return false;
        int prev = std::clamp(cursor, 0, itemCount - 1);
        int next = prev;
        if (in.up)        next = (prev - 1 + itemCount) % itemCount;
        else if (in.down) next = (prev + 1) % itemCount;
        if (next != prev) {
            cursor = next;
            return true;
        }
        return false;
    }

    // Unified 3D preview camera controls (Q/E orbit, Z/X zoom, C reset) — focus/overlay guarded
    static CameraInput PollCamera(float dt, float orbitSpeed = 1.75f, float zoomSpeed = 2.2f) {
        CameraInput cam{};
        if (!integrations::sacoop::IsGameWindowFocused() || integrations::sacoop::IsOverlayActive()) {
            return cam;
        }
        const auto& k = Keys();
        if (k.IsDown('Q'))      cam.orbitDelta -= dt * orbitSpeed;
        else if (k.IsDown('E')) cam.orbitDelta += dt * orbitSpeed;

        if (k.IsDown('Z') || k.IsDown(VK_OEM_PLUS) || k.IsDown(VK_ADD))          cam.zoomDelta -= dt * zoomSpeed;
        else if (k.IsDown('X') || k.IsDown(VK_OEM_MINUS) || k.IsDown(VK_SUBTRACT)) cam.zoomDelta += dt * zoomSpeed;

        cam.resetCam = k.JustPressed('C');
        return cam;
    }
};

inline const ListInputController& ListInput() {
    static const ListInputController s_ctrl;
    return s_ctrl;
}

// Reusable 3D Preview Orbit Camera Rig (Q/E 360° orbit, Z/X zoom, C reset + wall raycast)
// Shared across GTA5UIKit vanilla previews (Barber, Tattoo) and custom shops (Clothes)
struct OrbitCameraRig {
    bool  active         = false;
    bool  collideObjects = true;
    float angle          = 0.0f;
    float baseAngle      = 0.0f;
    float dist           = 1.2f;
    float baseDist       = 1.2f;
    float zoomOffset     = 0.0f;
    float heightOffset   = 0.08f;
    float minDist        = 0.55f;
    float maxDist        = 2.25f;
    float minClearDist   = 0.48f;
    float wallMargin     = 0.20f;
    float sideShiftRatio = 0.0f;

    void Reset() { active = false; }

    void Begin(float initAngle, float initDist, float initHeightOff,
               float minD = 0.55f, float maxD = 2.25f, float sideShift = 0.0f,
               bool checkObjects = true)
    {
        angle          = initAngle;
        baseAngle      = initAngle;
        minDist        = minD;
        maxDist        = maxD;
        baseDist       = std::clamp(initDist, minD, maxD);
        dist           = baseDist;
        zoomOffset     = 0.0f;
        heightOffset   = initHeightOff;
        sideShiftRatio = sideShift;
        collideObjects = checkObjects;
        active         = true;
    }

    // Seamlessly takes over from whatever fixed camera GTA SA's script already placed, centered on pivot
    void BeginFromCurrentCamera(const sa::CVector& pivot, float minD = 0.55f, float maxD = 2.25f,
                                float sideShift = 0.0f, bool checkObjects = false)
    {
        const sa::CVector cur = sa::camera::GetPosition();
        const float dx = cur.x - pivot.x;
        const float dy = cur.y - pivot.y;
        const float dz = cur.z - pivot.z;
        const float horiz = std::sqrt(dx * dx + dy * dy);
        const float initAng = (horiz > 0.05f) ? std::atan2(dy, dx) : 0.0f;
        const float initD   = (horiz > 0.15f && horiz < 6.0f) ? horiz : 1.05f;
        const float initH   = std::clamp(dz, -0.45f, 0.70f);
        Begin(initAng, initD, initH, minD, maxD, sideShift, checkObjects);
    }

    void Update(const CameraInput& cam) {
        if (!active) return;
        angle     += cam.orbitDelta;
        zoomOffset = std::clamp(zoomOffset + cam.zoomDelta, minDist - baseDist, maxDist - baseDist);
        if (cam.resetCam) {
            angle      = baseAngle;
            zoomOffset = 0.0f;
        }
        dist = std::clamp(baseDist + zoomOffset, minDist, maxDist);
    }

    void Apply(const sa::CVector& pivot, float lookZOffset = 0.0f, float rayZOffset = 0.0f) const {
        if (!active) return;
        const float desiredDist = std::clamp(dist, minDist, maxDist);
        // For seated interiors (collideObjects=false), trace horizontally at the higher of camera/ray height
        // so static CBuilding furniture (e.g. tattoo/barber chairs) around torso height never traps zoom,
        // while floor-to-ceiling interior walls still keep the camera inside the room!
        const sa::CVector rayOrigin{
            pivot.x,
            pivot.y,
            collideObjects ? (pivot.z + rayZOffset)
                           : std::max(pivot.z + rayZOffset, pivot.z + heightOffset)
        };
        const float probeDz = collideObjects ? (heightOffset - rayZOffset) : 0.0f;
        const float clearD  = sa::ProbeLineOfSightClearDist(rayOrigin, angle, desiredDist + wallMargin,
                                                            probeDz, collideObjects);
        const float safeD   = std::clamp(clearD - wallMargin, minClearDist, desiredDist);

        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);
        const float camX = pivot.x + cosA * safeD;
        const float camY = pivot.y + sinA * safeD;
        const float camZ = pivot.z + heightOffset;

        const float sideShift = safeD * sideShiftRatio;
        const float lookX     = pivot.x + sinA * sideShift;
        const float lookY     = pivot.y - cosA * sideShift;
        const float lookZ     = pivot.z + lookZOffset;

        sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
        sa::ScriptCmd(0x0160).Float(lookX).Float(lookY).Float(lookZ).Int(2).Run();
    }
};

// Concise, minimalist footer control hint (MSKL-safe, zero '|' pipes, zero anglicisms)
inline std::string StandardFooter(const char* acceptRus = "Выбрать", const char* acceptEng = "Select",
                                  const char* backRus   = "Назад",   const char* backEng   = "Back",
                                  const char* extraRus  = nullptr,   const char* extraEng  = nullptr)
{
    std::string s = Loc("ЛКМ/F/Enter - ", "LMB/F/Enter - ") + Loc(acceptRus, acceptEng);
    if (extraRus && extraRus[0] && extraEng && extraEng[0]) {
        s += "   " + Loc(extraRus, extraEng);
    }
    s += "   " + Loc("ПКМ/Backsp - ", "RMB/Backsp - ") + Loc(backRus, backEng);
    return s;
}

// Shared concise rank-lock badge & notice formatting (zero pipes, zero anglicisms)
inline std::string FormatRankLock(int requiredRank) {
    return Loc("Ур. ", "Rank ") + std::to_string(std::max(1, requiredRank));
}

inline void ShowRankLockedHelp(int requiredRank) {
    char rb[96], eb[96];
    std::snprintf(rb, sizeof(rb), "~r~Требуется %d уровень.", requiredRank);
    std::snprintf(eb, sizeof(eb), "~r~Requires Rank %d.", requiredRank);
    ShowHelp(rb, eb);
}

// ============================================================================
// TIER 0: Root Theme Tokens & WidescreenFix-Matched Scaling
// Modernized Classic GTA SA Aesthetic: translucent dark surfaces, subtle light
// perimeter border, crisp white typography, strict green/red/gray/yellow accents
// ============================================================================
struct ThemeTokens {
    float uiScale        = 1.0f;
    float screenMarginX  = 14.0f;
    float screenMarginY  = 14.0f;

    // Shared geometry (640x448 virtual units)
    float menuDefaultW   = 186.0f;
    float menuHeaderH    = 18.0f;
    float menuColBarH    = 12.0f;
    float menuRowH       = 13.0f;
    float menuFooterH    = 14.0f;
    float menuPadX       = 6.0f;
    float menuPadBot     = 4.5f;

    float cardDefaultW   = 152.0f;
    float cardHeaderH    = 16.5f;
    float cardRowH       = 17.5f;
    float cardBarH       = 3.6f;
    float cardNoteH      = 12.5f;

    float toastHeaderH   = 14.5f;
    float borderThickness = 0.75f;

    // Typography base scales (before auto-fit)
    float fontTitleX     = 0.33f, fontTitleY  = 0.64f;
    float fontSubX       = 0.25f, fontSubY    = 0.50f;
    float fontColX       = 0.25f, fontColY    = 0.50f;
    float fontRowX       = 0.27f, fontRowY    = 0.54f;
    float fontFooterX    = 0.23f, fontFooterY = 0.46f;
    float fontStatX      = 0.25f, fontStatY   = 0.50f;
    float fontToastX     = 0.26f, fontToastY  = 0.52f;
    float fontBadgeX     = 0.21f, fontBadgeY  = 0.42f;
    float fontInputX     = 0.34f, fontInputY  = 0.66f;
    float fontMinRatio   = 0.38f;

    // Unified Modern Classic GTA SA Palette
    sa::CRGBA bgBody            = {8,   8,   10,  204};
    sa::CRGBA bgHeader          = {4,   4,   6,   238};
    sa::CRGBA bgFooter          = {6,   6,   8,   198};
    sa::CRGBA bgInput           = {16,  16,  19,  242};
    sa::CRGBA bgBanner          = {18,  24,  20,  218};
    sa::CRGBA borderSubtle      = {185, 185, 192, 118};
    sa::CRGBA divider           = {175, 175, 182, 95};
    sa::CRGBA selectFill        = {225, 225, 230, 58};
    sa::CRGBA selectMuted       = {140, 140, 145, 34};

    // Strict semantic text palette (white / gray / green / red / yellow)
    sa::CRGBA textTitle         = {242, 242, 245, 255};
    sa::CRGBA textCol           = {175, 175, 180, 255};
    sa::CRGBA textNormal        = {204, 204, 208, 255};
    sa::CRGBA textSelected      = {255, 255, 255, 255};
    sa::CRGBA textDisabled      = {112, 112, 118, 255};
    sa::CRGBA textSuccess       = {115, 232, 130, 255};
    sa::CRGBA textDanger        = {238, 86,  86,  255};
    sa::CRGBA textWarning       = {240, 208, 92,  255};
    sa::CRGBA textFooter        = {198, 198, 204, 255};

    // Progress bars
    sa::CRGBA barTrack          = {62,  62,  66,  195};
    sa::CRGBA barFill           = {225, 225, 230, 242};
    sa::CRGBA barGain           = {92,  228, 114, 255};
    sa::CRGBA barLoss           = {232, 82,  82,  255};
    sa::CRGBA barBonus          = {242, 208, 76,  255};

    // Swatches & RadialWheel monochrome tokens
    sa::CRGBA swatchBorder      = {100, 100, 106, 185};
    sa::CRGBA swatchSelected    = {255, 255, 255, 255};
    sa::CRGBA wheelFill         = {6,   6,   8,   192};
    sa::CRGBA wheelFillEmpty    = {6,   6,   8,   118};
    sa::CRGBA wheelFillEquipped = {30,  30,  34,  204};
    sa::CRGBA wheelFillHover    = {82,  82,  88,  220};
    sa::CRGBA wheelRim          = {155, 155, 162, 125};
    sa::CRGBA wheelRimEmpty     = {75,  75,  80,  58};
    sa::CRGBA wheelRimHover     = {245, 245, 248, 245};
    sa::CRGBA wheelHubBg        = {6,   6,   8,   218};
    sa::CRGBA wheelHubRim       = {185, 185, 192, 145};
};

inline ThemeTokens& Theme() {
    static ThemeTokens t;
    return t;
}

// Semantic color accessors (feature mods MUST use these instead of hardcoded CRGBAs!)
inline sa::CRGBA ColorNormal()   { return Theme().textNormal; }
inline sa::CRGBA ColorSelected() { return Theme().textSelected; }
inline sa::CRGBA ColorDisabled() { return Theme().textDisabled; }
inline sa::CRGBA ColorSuccess()  { return Theme().textSuccess; }
inline sa::CRGBA ColorDanger()   { return Theme().textDanger; }
inline sa::CRGBA ColorWarning()  { return Theme().textWarning; }
inline sa::CRGBA ColorMuted()    { return Theme().textCol; }

inline void SyncFromSharedBus() {
    if (auto* b = modbus::GetBus(); b && b->uiKitInstalled) {
        UseRussianFlag() = b->useRussian;
        Theme().uiScale  = std::clamp(b->uiScale, 0.70f, 1.40f);
    }
}

inline void GetScreenScales(float& sw, float& sh, float& xScale, float& sy) {
    SyncFromSharedBus();
    integrations::widescreen::GetScreenScales(sw, sh, xScale, sy, Theme().uiScale);
}

inline sa::CRGBA WithAlpha(sa::CRGBA c, float alphaFrac) {
    float a = std::clamp(static_cast<float>(c.a) * std::clamp(alphaFrac, 0.0f, 1.0f), 0.0f, 255.0f);
    return {c.r, c.g, c.b, static_cast<uint8_t>(a)};
}

// ============================================================================
// CSS-Like Layout Engine: Relative (Flow + Auto-Fit) & Absolute Child Placement
// ============================================================================
enum class PositionMode : uint8_t {
    Relative = 0, // Default flow inside parent bounds (auto-fits width to parent)
    Absolute = 1  // Explicit offset pinned relative to parent origin (e.g. corner badge)
};

struct RectBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct LayoutStyle {
    PositionMode position = PositionMode::Relative;
    float        offsetX  = 0.0f;
    float        offsetY  = 0.0f;
    float        width    = 0.0f; // 0 = auto-fit to parent content width
    float        height   = 0.0f; // 0 = intrinsic height
};

inline RectBox ResolveChildRect(const RectBox& parent, float flowY, float defaultH,
                                const LayoutStyle& style, float padX = 0.0f)
{
    const float h = (style.height > 0.0f) ? style.height : defaultH;
    if (style.position == PositionMode::Absolute) {
        const float w = (style.width > 0.0f) ? style.width : std::max(0.0f, parent.w - style.offsetX);
        return {parent.x + style.offsetX, parent.y + style.offsetY, w, h};
    }
    const float innerW = std::max(0.0f, parent.w - padX * 2.0f);
    const float w      = (style.width > 0.0f) ? std::min(style.width, innerW) : innerW;
    return {parent.x + padX + style.offsetX, flowY + style.offsetY, w, h};
}

class VStack {
public:
    VStack(const RectBox& parent, float padX = 0.0f)
        : m_parent(parent), m_cursorY(parent.y), m_padX(padX) {}

    RectBox Next(float height, const LayoutStyle& style = {}) {
        RectBox r = ResolveChildRect(m_parent, m_cursorY, height, style, m_padX);
        if (style.position == PositionMode::Relative) {
            m_cursorY = r.y + r.h;
        }
        return r;
    }

    void Advance(float deltaY) { m_cursorY += deltaY; }
    float CursorY() const      { return m_cursorY; }
    const RectBox& Bounds() const { return m_parent; }

private:
    RectBox m_parent;
    float   m_cursorY = 0.0f;
    float   m_padX    = 0.0f;
};

// ============================================================================
// TIER 1: True Atomic Primitives (`ui::prim`)
// THE ONLY LAYER permitted to invoke sa::font::*, DrawSpriteTexture, or Im2D!
// ============================================================================
namespace prim {

inline void PrepareFontBase() {
    const float sw = static_cast<float>(std::max(640, sa::GetScreenWidth()));
    sa::font::SetBackground(false, false);
    sa::font::SetProportional(true);
    sa::font::SetJustify(false);
    sa::font::SetDropShadowPosition(1);
    sa::font::SetEdge(0);
    sa::font::SetDropColor({0, 0, 0, 255});
    sa::font::SetWrapx(sw);
    sa::font::SetCentreSize(sw);
    sa::font::SetRightJustifyWrap(-10000.0f);
}

// 1. Rect: Filled top-left (x, y, w, h) rectangle atom
inline void Rect(float x, float y, float w, float h, sa::CRGBA col) {
    if (col.a == 0 || w <= 0.0f || h <= 0.0f) return;
    sa::font::DrawRect({x, y + h, x + w, y}, col);
}

// 2. Border: 4 perimeter edge strips (ZERO alpha-bleed behind translucent surfaces!)
inline void Border(float x, float y, float w, float h, float thickX, float thickY, sa::CRGBA col) {
    if (col.a == 0 || w <= 0.0f || h <= 0.0f) return;
    const float tx = std::max(0.5f, thickX);
    const float ty = std::max(0.5f, thickY);
    Rect(x - tx, y - ty, w + tx * 2.0f, ty, col); // Top edge
    Rect(x - tx, y + h,  w + tx * 2.0f, ty, col); // Bottom edge
    Rect(x - tx, y,      tx,            h,  col); // Left edge
    Rect(x + w,  y,      tx,            h,  col); // Right edge
}

// 3. Panel: Translucent surface + crisp non-bleeding perimeter border
inline void Panel(float x, float y, float w, float h, sa::CRGBA bg,
                  sa::CRGBA border = {0, 0, 0, 0},
                  float borderPxX = 0.75f, float borderPxY = 0.75f)
{
    Rect(x, y, w, h, bg);
    if (border.a > 0) {
        Border(x, y, w, h, borderPxX, borderPxY, border);
    }
}

// 4. Divider: Thin horizontal separator rule
inline void Divider(float x, float y, float w, float h, sa::CRGBA col) {
    Rect(x, y, w, h, col);
}

// 5. AccentStrip: Left/top semantic highlight strip
inline void AccentStrip(float x, float y, float w, float h, sa::CRGBA col) {
    Rect(x, y, w, h, col);
}

// 5b. BarSegment: Clamped normalized [startFrac..endFrac] horizontal bar segment atom
inline void BarSegment(float x, float y, float w, float h, float startFrac, float endFrac, sa::CRGBA col) {
    float s = std::clamp(startFrac, 0.0f, 1.0f);
    float e = std::clamp(endFrac,   0.0f, 1.0f);
    if (e <= s || w <= 0.0f || h <= 0.0f || col.a == 0) return;
    Rect(x + w * s, y, w * (e - s), h, col);
}

// 6. Label: Single-line auto-fitting text atom (adapts automatically to parent width!)
// align: 0 = Center, 1 = Left, 2 = Right
inline void Label(float x, float y, float maxWidthPx, const char* gameText,
                  uint8_t fontStyle, float scaleX, float scaleY,
                  uint8_t align, sa::CRGBA color)
{
    if (!gameText || !gameText[0] || color.a == 0) return;
    PrepareFontBase();
    sa::font::SetFontStyle(fontStyle);
    sa::font::SetScale(scaleX, scaleY);

    if (maxWidthPx > 4.0f) {
        float measured = sa::font::GetStringWidth(gameText, true, false);
        if (measured > maxWidthPx && measured > 1.0f) {
            float ratio = std::max(Theme().fontMinRatio, (maxWidthPx / measured) * 0.96f);
            sa::font::SetScale(scaleX * ratio, scaleY * (0.65f + 0.35f * ratio));
        }
    }

    sa::font::SetColor(color);
    sa::font::SetDropColor({0, 0, 0, color.a});
    sa::font::SetOrientation(align);
    const float sw = static_cast<float>(std::max(640, sa::GetScreenWidth()));
    if (align == 0)      sa::font::SetCentreSize(sw);
    else if (align == 1) sa::font::SetWrapx(sw);
    else                 sa::font::SetRightJustifyWrap(-10000.0f);
    sa::font::PrintString(x, y, gameText);
}

// 7. MeasureWrappedLines & WrappedText: Multi-line auto-wrapping paragraph atom
inline int MeasureWrappedLines(float x, float y, float maxWidthPx, const char* gameText,
                               uint8_t fontStyle, float scaleX, float scaleY)
{
    if (!gameText || !gameText[0]) return 0;
    PrepareFontBase();
    sa::font::SetFontStyle(fontStyle);
    sa::font::SetScale(scaleX, scaleY);
    sa::font::SetOrientation(1);
    sa::font::SetWrapx(x + std::max(32.0f, maxWidthPx));
    int lines = static_cast<int>(sa::font::GetNumberLines(x, y, gameText));
    return std::max(1, lines);
}

inline void WrappedText(float x, float y, float maxWidthPx, const char* gameText,
                        uint8_t fontStyle, float scaleX, float scaleY, sa::CRGBA color)
{
    if (!gameText || !gameText[0] || color.a == 0) return;
    PrepareFontBase();
    sa::font::SetFontStyle(fontStyle);
    sa::font::SetScale(scaleX, scaleY);
    sa::font::SetColor(color);
    sa::font::SetDropColor({0, 0, 0, color.a});
    sa::font::SetOrientation(1);
    sa::font::SetWrapx(x + std::max(32.0f, maxWidthPx));
    sa::font::PrintString(x, y, gameText);
    sa::font::SetWrapx(static_cast<float>(std::max(640, sa::GetScreenWidth())));
}

// 7b. FlushText: Immediately flushes GTA SA's 944-byte CFont deferred buffer so dense screens never drop strings
// and subsequent Rect/Panel calls composite in exact painter's-algorithm Z-order!
inline void FlushText() {
    sa::font::RenderFontBuffer();
}

// 8. Icon: Textured sprite atom (wraps CSprite2d::Draw)
inline void Icon(void* rwTex, float cx, float cy, float halfW, float halfH, sa::CRGBA tint) {
    if (!rwTex || tint.a == 0) return;
    sa::DrawSpriteTexture(rwTex, {cx - halfW, cy + halfH, cx + halfW, cy - halfH}, tint);
}

// 9. Arc & Disc: 2D RenderWare Im2D atoms (caller holds proper_shaders::RwRenderStateGuard)
inline void Arc(float cx, float cy, float rIn, float rOut, float a0, float a1, int steps, sa::CRGBA col) {
    if (col.a == 0 || steps < 1) return;
    const float z   = sa::rw::GetNearScreenZ();
    const float rhw = sa::rw::GetRecipNearClip();
    const uint32_t packed = sa::rw::PackColor(col.r, col.g, col.b, col.a);
    std::vector<sa::rw::RwIm2DVertex> v;
    v.reserve(static_cast<size_t>((steps + 1) * 2));
    for (int i = 0; i <= steps; ++i) {
        float a  = a0 + (a1 - a0) * (static_cast<float>(i) / static_cast<float>(steps));
        float ca = std::cos(a), sv = std::sin(a);
        v.push_back({cx + ca * rOut, cy + sv * rOut, z, rhw, packed, 0.0f, 0.0f});
        v.push_back({cx + ca * rIn,  cy + sv * rIn,  z, rhw, packed, 0.0f, 0.0f});
    }
    sa::rw::Im2DRenderPrimitive(sa::rw::PRIM_TRISTRIP, v.data(), static_cast<int32_t>(v.size()));
}

inline void Disc(float cx, float cy, float r, int steps, sa::CRGBA col) {
    if (col.a == 0 || steps < 3) return;
    constexpr float kPi2 = 6.28318530718530717959f;
    const float z   = sa::rw::GetNearScreenZ();
    const float rhw = sa::rw::GetRecipNearClip();
    const uint32_t packed = sa::rw::PackColor(col.r, col.g, col.b, col.a);
    std::vector<sa::rw::RwIm2DVertex> v;
    v.reserve(static_cast<size_t>(steps + 2));
    v.push_back({cx, cy, z, rhw, packed, 0.0f, 0.0f});
    for (int i = 0; i <= steps; ++i) {
        float a = kPi2 * static_cast<float>(i) / static_cast<float>(steps);
        v.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r, z, rhw, packed, 0.0f, 0.0f});
    }
    sa::rw::Im2DRenderPrimitive(sa::rw::PRIM_TRIFAN, v.data(), static_cast<int32_t>(v.size()));
}

} // namespace prim

// ============================================================================
// TIER 1.5: Reusable Micro-Components (`ui::widget`)
// Built STRICTLY from Tier 1 `ui::prim::*` atoms — ZERO raw engine draw calls!
// ============================================================================
namespace widget {

// Header: Dark title bar with optional right badge/counter + bottom divider
inline void Header(float x, float y, float w, float h, float padX,
                   const std::string& title, const std::string& rightBadge,
                   float titleSx, float titleSy, float badgeSx, float badgeSy,
                   float xScale, float sy,
                   sa::CRGBA bgCol, sa::CRGBA divCol, sa::CRGBA titleCol, sa::CRGBA badgeCol,
                   bool drawDivider = true, float badgeReserveRatio = 0.36f)
{
    prim::Rect(x, y, w, h, bgCol);
    const float gapX     = rightBadge.empty() ? 0.0f : (5.0f * xScale);
    const float badgeRes = rightBadge.empty() ? 0.0f : (w * badgeReserveRatio);
    const float titleW   = std::max(16.0f * xScale, w - padX * 2.0f - badgeRes - gapX);
    prim::Label(x + padX, y + 2.2f * sy, titleW, title.c_str(), 2, titleSx, titleSy, 1, titleCol);
    if (!rightBadge.empty()) {
        prim::Label(x + w - padX, y + 3.2f * sy, std::max(16.0f * xScale, badgeRes - gapX * 0.5f),
                    rightBadge.c_str(), 1, badgeSx, badgeSy, 2, badgeCol);
    }
    if (drawDivider && divCol.a > 0) {
        prim::Divider(x + 2.5f * xScale, y + h - 0.7f * sy, w - 5.0f * xScale, 0.7f * sy, divCol);
    }
}

// Row: Interactive list row with selection highlight + auto-fit left & right labels
inline void Row(float x, float y, float w, float h, float padX,
                const std::string& leftText, const std::string& rightText,
                bool selected, bool enabled,
                float fontSx, float fontSy, float xScale, float sy,
                sa::CRGBA selFill, sa::CRGBA leftCol, sa::CRGBA rightCol,
                sa::CRGBA selTextCol, sa::CRGBA disTextCol)
{
    if (selected && selFill.a > 0) {
        prim::Rect(x + 1.8f * xScale, y + 0.4f * sy, w - 3.6f * xScale, h - 0.8f * sy, selFill);
    }
    const float rightRes = rightText.empty() ? 0.0f : (w * 0.38f);
    const float leftMaxW = w - padX * 2.0f - rightRes - (rightText.empty() ? 0.0f : 3.8f * xScale);

    sa::CRGBA lCol = selected ? selTextCol : (enabled ? leftCol : disTextCol);
    prim::Label(x + padX, y + 1.4f * sy, leftMaxW, leftText.c_str(), 1, fontSx, fontSy, 1, lCol);

    if (!rightText.empty()) {
        sa::CRGBA rCol = selected ? selTextCol : (enabled ? rightCol : disTextCol);
        prim::Label(x + w - padX, y + 1.4f * sy, rightRes, rightText.c_str(),
                    1, fontSx * 0.96f, fontSy * 0.96f, 2, rCol);
    }
}

// Footer: Minimalist framed bottom control hint bar
inline void Footer(float x, float y, float w, float h, float padX,
                   const std::string& text, float fontSx, float fontSy,
                   float xScale, float sy,
                   sa::CRGBA bgCol, sa::CRGBA textCol, sa::CRGBA borderCol = {0, 0, 0, 0},
                   sa::CRGBA topDivCol = {0, 0, 0, 0})
{
    const float bth = Theme().borderThickness;
    prim::Panel(x, y, w, h, bgCol, borderCol, bth * xScale, bth * sy);
    if (topDivCol.a > 0) {
        prim::Divider(x + 2.0f * xScale, y, w - 4.0f * xScale, 0.6f * sy, topDivCol);
    }
    if (!text.empty()) {
        prim::Label(x + padX, y + 2.1f * sy, w - padX * 2.0f, text.c_str(), 1, fontSx, fontSy, 1, textCol);
    }
}

// ProgressBar: Dumb metric bar built strictly from Tier 1 prim::Rect + prim::BarSegment
// Supports base fill, green preview gain, red downgrade/wear loss, and yellow bonus segment.
// When base + bonus exceeds 100% (full bar), the yellow bonus anchors at the right end (1.0)
// and grows smoothly inward from end toward start while keeping any green/red preview visible!
struct ProgressBarProps {
    float     curFrac   = 0.0f;
    float     prevFrac  = 0.0f;
    float     prisFrac  = 0.0f;
    float     bonusFrac = 0.0f;
    int       delta     = 0;
    int       wearLoss  = 0;
    sa::CRGBA trackCol  = {62,  62,  66,  195};
    sa::CRGBA fillCol   = {225, 225, 230, 242};
    sa::CRGBA gainCol   = {92,  228, 114, 255};
    sa::CRGBA lossCol   = {232, 82,  82,  255};
    sa::CRGBA bonusCol  = {242, 208, 76,  255};
};

inline void ProgressBar(float x, float y, float w, float h, const ProgressBarProps& p) {
    prim::Rect(x, y, w, h, p.trackCol);

    const float curF   = std::clamp(p.curFrac,   0.0f, 1.0f);
    const float prevF  = std::clamp(p.prevFrac,  0.0f, 1.0f);
    const float prisF  = std::clamp(p.prisFrac,  0.0f, 1.0f);
    const float bonusF = std::clamp(p.bonusFrac, 0.0f, 1.0f);

    const float baseEnd = (p.delta > 0) ? prevF : ((p.delta < 0) ? prevF : curF);
    const bool  hasBonus = (bonusF > 0.0001f);

    if (!hasBonus) {
        if (p.delta > 0) {
            prim::BarSegment(x, y, w, h, 0.0f,  curF,  p.fillCol);
            prim::BarSegment(x, y, w, h, curF,  prevF, p.gainCol);
            if (prisF > prevF) {
                prim::BarSegment(x, y, w, h, prevF, prisF, p.lossCol);
            }
        } else if (p.delta < 0) {
            prim::BarSegment(x, y, w, h, 0.0f,  prevF, p.fillCol);
            prim::BarSegment(x, y, w, h, prevF, curF,  p.lossCol);
        } else {
            prim::BarSegment(x, y, w, h, 0.0f, curF, p.fillCol);
            if (p.wearLoss > 0 && prisF > curF) {
                prim::BarSegment(x, y, w, h, curF, prisF, p.lossCol);
            }
        }
        return;
    }

    // Continuous yellow bonus layout:
    //  - If baseEnd + bonusF <= 1.0: yellow extends rightward [baseEnd .. baseEnd + bonusF]
    //  - If baseEnd + bonusF >  1.0: yellow anchors at right end [1.0 - bonusF .. 1.0] (grows end -> start!)
    const float yEnd   = std::min(1.0f, baseEnd + bonusF);
    const float yStart = std::max(0.0f, yEnd - bonusF);
    const float avail  = yStart; // Space available to the left of the yellow bonus bar

    if (p.delta > 0) {
        const float gainW  = std::max(0.0f, prevF - curF);
        const float gEnd   = std::min(prevF, avail);
        const float gStart = std::max(0.0f, gEnd - gainW);
        prim::BarSegment(x, y, w, h, 0.0f,   gStart, p.fillCol);
        prim::BarSegment(x, y, w, h, gStart, gEnd,   p.gainCol);
    } else if (p.delta < 0) {
        const float lossW = std::max(0.0f, curF - prevF);
        if (yEnd < 0.9999f) {
            prim::BarSegment(x, y, w, h, 0.0f, std::min(prevF, avail), p.fillCol);
        } else {
            const float lStart = std::max(0.0f, avail - lossW);
            prim::BarSegment(x, y, w, h, 0.0f,   lStart, p.fillCol);
            prim::BarSegment(x, y, w, h, lStart, avail,  p.lossCol);
        }
    } else {
        prim::BarSegment(x, y, w, h, 0.0f, std::min(curF, avail), p.fillCol);
    }

    // Yellow bonus segment
    prim::BarSegment(x, y, w, h, yStart, yEnd, p.bonusCol);

    // Red downgrade loss beyond yellow when room remains on the right
    if (p.delta < 0 && yEnd < 0.9999f) {
        const float lossW = std::max(0.0f, curF - prevF);
        if (lossW > 0.0001f) {
            prim::BarSegment(x, y, w, h, yEnd, std::min(1.0f, yEnd + lossW), p.lossCol);
        }
    }

    // Red wear-loss segment beyond the effective (base + bonus) end, if room remains
    if (p.wearLoss > 0 && p.delta >= 0) {
        const float effPrisEnd = std::min(1.0f, prisF + bonusF);
        if (effPrisEnd > yEnd) {
            prim::BarSegment(x, y, w, h, yEnd, effPrisEnd, p.lossCol);
        }
    }
}

// TextField: Framed monochrome input field built strictly from prim::Panel + prim::Label
inline void TextField(float x, float y, float w, float h,
                      const std::string& text, const std::string& placeholder, int maxLen,
                      float xScale, float sy,
                      sa::CRGBA borderCol, sa::CRGBA bgCol, sa::CRGBA textCol, sa::CRGBA phCol, sa::CRGBA cntCol)
{
    const auto& th = Theme();
    prim::Panel(x, y, w, h, bgCol, borderCol, th.borderThickness * xScale, th.borderThickness * sy);
    const bool  blink    = ((GetTickCount() / 420) % 2) == 0;
    const float cntW     = 24.0f * xScale;
    const float textMaxW = w - 10.0f * xScale - cntW;

    if (text.empty()) {
        std::string ph = placeholder.empty() ? std::string("_") : (placeholder + (blink ? "_" : ""));
        prim::Label(x + 4.5f * xScale, y + 3.2f * sy, textMaxW, ph.c_str(),
                    1, th.fontRowX * xScale, th.fontRowY * sy, 1, phCol);
    } else {
        std::string shown = text;
        if (static_cast<int>(shown.size()) < maxLen && blink) shown.push_back('_');
        prim::Label(x + 4.5f * xScale, y + 2.6f * sy, textMaxW, shown.c_str(),
                    2, th.fontInputX * xScale, th.fontInputY * sy, 1, textCol);
    }

    char cntBuf[16];
    std::snprintf(cntBuf, sizeof(cntBuf), "%d/%d", static_cast<int>(text.size()), maxLen);
    prim::Label(x + w - 4.0f * xScale, y + 4.2f * sy, cntW, cntBuf,
                1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 2, cntCol);
}

// Banner: Centered status strip built from prim::Rect + prim::Label
inline void Banner(float x, float y, float w, float h, float padX,
                   const std::string& text, float fontSx, float fontSy,
                   float xScale, float sy, sa::CRGBA bgCol, sa::CRGBA textCol)
{
    prim::Rect(x + 2.0f * xScale, y + 0.8f * sy, w - 4.0f * xScale, h - 1.6f * sy, bgCol);
    prim::Label(x + w * 0.5f, y + 1.8f * sy, w - padX * 2.0f, text.c_str(), 1, fontSx, fontSy, 0, textCol);
}

// Swatch: Color tile built strictly from prim::Panel
inline void Swatch(float x, float y, float w, float h, sa::CRGBA fillCol,
                   bool selected, float xScale, float sy)
{
    const auto& th = Theme();
    if (selected) {
        prim::Panel(x, y, w, h, fillCol, th.swatchSelected, 1.3f * xScale, 1.3f * sy);
    } else {
        prim::Panel(x + 0.8f * xScale, y + 0.8f * sy, w - 1.6f * xScale, h - 1.6f * sy,
                    fillCol, th.swatchBorder, 0.55f * xScale, 0.55f * sy);
    }
}

// Badge: Compact status pill (works in Relative or Absolute parent placement)
inline void Badge(const RectBox& box, const std::string& text,
                  float xScale, float sy, sa::CRGBA bgCol, sa::CRGBA borderCol, sa::CRGBA textCol)
{
    const auto& th = Theme();
    prim::Panel(box.x, box.y, box.w, box.h, bgCol, borderCol, 0.6f * xScale, 0.6f * sy);
    prim::Label(box.x + box.w * 0.5f, box.y + 1.6f * sy, box.w - 4.0f * xScale, text.c_str(),
                1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 0, textCol);
}

// Button: Interactive button tile built strictly from prim::Panel + prim::Label
inline void Button(const RectBox& box, const std::string& label, bool focused, bool enabled,
                   float xScale, float sy)
{
    const auto& th = Theme();
    sa::CRGBA bg  = focused ? th.selectFill : th.bgInput;
    sa::CRGBA bdr = focused ? th.textSelected : th.borderSubtle;
    sa::CRGBA txt = !enabled ? th.textDisabled : (focused ? th.textSelected : th.textNormal);
    prim::Panel(box.x, box.y, box.w, box.h, bg, bdr, th.borderThickness * xScale, th.borderThickness * sy);
    prim::Label(box.x + box.w * 0.5f, box.y + 2.0f * sy, box.w - 6.0f * xScale, label.c_str(),
                1, th.fontRowX * xScale, th.fontRowY * sy, 0, txt);
}

} // namespace widget

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 1: ListMenu (Built 100% from Tier 1 + Tier 1.5)
// ============================================================================
struct MenuRow {
    std::string leftGame;
    std::string rightGame;
    bool        enabled    = true;
    sa::CRGBA   rightColor = {204, 204, 208, 255};
    int         userId     = 0;
};

enum class MenuSignalType : uint8_t {
    None = 0,
    HoverChanged,
    Activated,
    Back
};

struct MenuEvent {
    MenuSignalType type   = MenuSignalType::None;
    int            index  = -1;
    int            userId = 0;
};

class ListMenu {
public:
    ListMenu& Clear() { m_rows.clear(); return *this; }
    ListMenu& SetTitle(const std::string& gameText) { m_title = gameText; return *this; }
    ListMenu& SetColumns(const std::string& leftGame, const std::string& rightGame) {
        m_colLeft = leftGame; m_colRight = rightGame; return *this;
    }
    ListMenu& SetFooter(const std::string& gameText) { m_footer = gameText; return *this; }
    ListMenu& SetPosition(float x640, float y448, float w640 = 0.0f) {
        m_x640 = x640; m_y448 = y448; m_w640 = w640; return *this;
    }
    ListMenu& SetMaxVisible(int n) { m_maxVisible = std::max(1, n); return *this; }
    ListMenu& SetSelected(int idx) { m_selected = std::max(0, idx); return *this; }
    ListMenu& SetAccent(sa::CRGBA col) { m_accent = col; m_useCustomAccent = true; return *this; }

    ListMenu& AddItem(const std::string& leftGame, const std::string& rightGame = "",
                      sa::CRGBA rightCol = Theme().textNormal, bool enabled = true, int userId = 0)
    {
        m_rows.push_back({leftGame, rightGame, enabled, rightCol, userId});
        return *this;
    }

    int GetSelected() const {
        const int n = static_cast<int>(m_rows.size());
        return (n > 0) ? std::clamp(m_selected, 0, n - 1) : 0;
    }
    int GetCount() const    { return static_cast<int>(m_rows.size()); }

    MenuEvent Update(bool allowBack = true) {
        ClampSelection();
        const int n = static_cast<int>(m_rows.size());
        auto in = ListInput().Poll(true);
        if (n > 0) {
            if (ListInput().ApplyToCursor(m_selected, n, in)) {
                return {MenuSignalType::HoverChanged, m_selected, m_rows[m_selected].userId};
            }
            if (in.accept) {
                return {MenuSignalType::Activated, m_selected, m_rows[m_selected].userId};
            }
        }
        if (allowBack && in.back) {
            return {MenuSignalType::Back, m_selected, (n > 0) ? m_rows[m_selected].userId : 0};
        }
        return {};
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        const float w640 = (m_w640 > 1.0f) ? std::clamp(m_w640, 130.0f, 236.0f) : th.menuDefaultW;
        const float pw   = w640 * xScale;

        const int totalRows = static_cast<int>(m_rows.size());
        const int selIdx    = (totalRows > 0) ? std::clamp(m_selected, 0, totalRows - 1) : 0;
        const int visible   = std::min(totalRows, m_maxVisible);
        int scrollTop = 0;
        if (totalRows > visible) {
            scrollTop = std::clamp(selIdx - (visible / 2), 0, totalRows - visible);
        }

        const float headerH = th.menuHeaderH * sy;
        const float colBarH = (m_colLeft.empty() && m_colRight.empty()) ? 0.0f : (th.menuColBarH * sy);
        const float rowH    = th.menuRowH    * sy;
        const float padX    = th.menuPadX    * xScale;
        const float padBot  = th.menuPadBot  * sy;
        const float bodyH   = headerH + colBarH + (visible * rowH) + padBot;
        const float footH   = m_footer.empty() ? 0.0f : (th.menuFooterH * sy + 3.0f * sy);

        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float px = std::clamp(m_x640 * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float py = std::clamp(m_y448 * sy,     marginY, std::max(marginY, sh - (bodyH + footH) - marginY));

        // 1. Framed Body Panel (Modern GTA SA subtle light border, zero alpha-bleed!)
        prim::Panel(px, py, pw, bodyH, th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);

        VStack stack({px, py, pw, bodyH}, 0.0f);

        // 2. Header
        std::string countBadge;
        if (totalRows > visible) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d/%d", selIdx + 1, totalRows);
            countBadge = buf;
        }
        RectBox hdrBox = stack.Next(headerH);
        widget::Header(hdrBox.x, hdrBox.y, hdrBox.w, hdrBox.h, padX, m_title, countBadge,
                       th.fontTitleX * xScale, th.fontTitleY * sy,
                       th.fontColX * xScale, th.fontColY * sy,
                       xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol,
                       colBarH <= 0.0f, 0.22f);

        // 3. Column labels + Divider
        if (colBarH > 0.0f) {
            RectBox colBox = stack.Next(colBarH);
            const float colY      = colBox.y + 1.5f * sy;
            const float colRightW = m_colRight.empty() ? 0.0f : (pw * 0.36f);
            prim::Label(colBox.x + padX, colY, pw - padX * 2.0f - colRightW, m_colLeft.c_str(),
                        1, th.fontColX * xScale, th.fontColY * sy, 1, th.textCol);
            if (!m_colRight.empty()) {
                prim::Label(colBox.x + pw - padX, colY, colRightW, m_colRight.c_str(),
                            1, th.fontColX * xScale, th.fontColY * sy, 2, th.textCol);
            }
            prim::Divider(colBox.x + 3.0f * xScale, colBox.y + colBox.h - 0.8f * sy,
                          pw - 6.0f * xScale, 0.8f * sy, th.divider);
        }

        // 4. Rows
        const sa::CRGBA selFill = m_useCustomAccent ? m_accent : th.selectFill;
        for (int i = 0; i < visible; ++i) {
            int idx = scrollTop + i;
            const auto& r = m_rows[idx];
            RectBox rowBox = stack.Next(rowH);
            widget::Row(rowBox.x, rowBox.y, rowBox.w, rowBox.h, padX,
                        r.leftGame, r.rightGame, (idx == selIdx), r.enabled,
                        th.fontRowX * xScale, th.fontRowY * sy, xScale, sy,
                        selFill, th.textNormal, r.rightColor, th.textSelected, th.textDisabled);
        }

        // 5. Framed Concise Footer
        if (!m_footer.empty()) {
            widget::Footer(px, py + bodyH + 3.0f * sy, pw, th.menuFooterH * sy, padX,
                           m_footer, th.fontFooterX * xScale, th.fontFooterY * sy,
                           xScale, sy, th.bgFooter, th.textFooter, th.borderSubtle);
        }
    }

private:
    void ClampSelection() {
        int n = static_cast<int>(m_rows.size());
        if (n <= 0) m_selected = 0;
        else m_selected = std::clamp(m_selected, 0, n - 1);
    }

    std::string          m_title;
    std::string          m_colLeft;
    std::string          m_colRight;
    std::string          m_footer;
    std::vector<MenuRow> m_rows;
    int                  m_selected        = 0;
    int                  m_maxVisible      = 11;
    float                m_x640            = 20.0f;
    float                m_y448            = 68.0f;
    float                m_w640            = 0.0f;
    sa::CRGBA            m_accent          = {225, 225, 230, 58};
    bool                 m_useCustomAccent = false;
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 2: Generic Reusable Grid + ColorGrid (Rule 1b)
// ============================================================================
struct GridCell {
    std::string labelGame;
    std::string badgeGame;
    bool        useSwatch = false;
    sa::CRGBA   swatchCol = {0, 0, 0, 0};
    void*       iconTex   = nullptr;
    bool        enabled   = true;
    int         userId    = 0;
};

class Grid {
public:
    Grid& Clear() { m_cells.clear(); return *this; }
    Grid& SetTitle(const std::string& titleGame, const std::string& badgeGame = "") {
        m_title = titleGame; m_badge = badgeGame; return *this;
    }
    Grid& SetFooter(const std::string& footerGame) { m_footer = footerGame; return *this; }
    Grid& SetLayout(int cols, int rows, float cellW640 = 42.0f, float cellH448 = 24.0f) {
        m_cols   = std::clamp(cols, 1, 8);
        m_rows   = std::clamp(rows, 1, 8);
        m_cellW  = std::max(12.0f, cellW640);
        m_cellH  = std::max(12.0f, cellH448);
        return *this;
    }
    Grid& SetPosition(float x640, float y448) { m_x640 = x640; m_y448 = y448; return *this; }
    Grid& SetSelected(int idx) { m_selected = std::max(0, idx); return *this; }
    Grid& AddCell(const GridCell& cell) { m_cells.push_back(cell); return *this; }

    int GetSelected() const {
        const int n = static_cast<int>(m_cells.size());
        return (n > 0) ? std::clamp(m_selected, 0, n - 1) : 0;
    }
    int GetCount() const    { return static_cast<int>(m_cells.size()); }

    MenuEvent Update(bool allowBack = true) {
        ClampSelection();
        const int total = static_cast<int>(m_cells.size());
        auto in = ListInput().Poll(true);
        if (total > 0) {
            int next = m_selected;
            if (in.left)       next = (m_selected - 1 + total) % total;
            else if (in.right) next = (m_selected + 1) % total;
            else if (in.up)    next = (m_selected - m_cols + total) % total;
            else if (in.down)  next = (m_selected + m_cols) % total;
            if (next != m_selected) {
                m_selected = next;
                return {MenuSignalType::HoverChanged, m_selected, m_cells[m_selected].userId};
            }
            if (in.accept) {
                return {MenuSignalType::Activated, m_selected, m_cells[m_selected].userId};
            }
        }
        if (allowBack && in.back) {
            return {MenuSignalType::Back, m_selected, (total > 0) ? m_cells[m_selected].userId : 0};
        }
        return {};
    }

    void Render() const {
        if (m_cells.empty()) return;
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th   = Theme();
        const float padX = th.menuPadX * xScale;
        const float cw   = m_cellW * xScale;
        const float ch   = m_cellH * sy;
        const float gapX = 3.0f * xScale;
        const float gapY = 3.0f * sy;
        const float hdrH = th.menuHeaderH * sy;
        const float gridW = m_cols * cw + (m_cols - 1) * gapX;
        const float gridH = m_rows * ch + (m_rows - 1) * gapY;
        const float pw    = std::max(148.0f * xScale, gridW + padX * 2.0f);
        const float bodyH = hdrH + gridH + 10.0f * sy;
        const float footH = m_footer.empty() ? 0.0f : (th.menuFooterH * sy + 3.0f * sy);

        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float px = std::clamp(m_x640 * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float py = std::clamp(m_y448 * sy,     marginY, std::max(marginY, sh - (bodyH + footH) - marginY));

        prim::Panel(px, py, pw, bodyH, th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);

        const int total  = static_cast<int>(m_cells.size());
        const int selIdx = std::clamp(m_selected, 0, total - 1);

        std::string badge = m_badge;
        if (badge.empty()) {
            char idxBuf[24];
            std::snprintf(idxBuf, sizeof(idxBuf), "#%d", selIdx + 1);
            badge = idxBuf;
        }
        widget::Header(px, py, pw, hdrH, padX, m_title, badge,
                       th.fontTitleX * xScale, th.fontTitleY * sy,
                       th.fontColX * xScale, th.fontColY * sy,
                       xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol, true, 0.25f);

        const float gx0 = px + (pw - gridW) * 0.5f;
        const float gy0 = py + hdrH + 5.0f * sy;
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                int idx = r * m_cols + c;
                if (idx >= total) break;
                const auto& cell = m_cells[idx];
                const float cx = gx0 + c * (cw + gapX);
                const float cy = gy0 + r * (ch + gapY);
                const bool sel = (idx == selIdx);

                if (cell.useSwatch) {
                    widget::Swatch(cx, cy, cw, ch, cell.swatchCol, sel, xScale, sy);
                } else {
                    widget::Button({cx, cy, cw, ch}, cell.labelGame, sel, cell.enabled, xScale, sy);
                    if (cell.iconTex) {
                        const float h = std::min(cw, ch) * 0.34f;
                        prim::Icon(cell.iconTex, cx + cw * 0.5f, cy + ch * 0.5f, h, h,
                                   sel ? th.textSelected : th.textNormal);
                    }
                    if (!cell.badgeGame.empty()) {
                        // Absolute corner badge pinned inside the cell container!
                        RectBox bBox = ResolveChildRect({cx, cy, cw, ch}, cy, 8.5f * sy,
                                                        {PositionMode::Absolute, cw - 18.0f * xScale, 1.0f * sy, 17.0f * xScale, 8.5f * sy});
                        widget::Badge(bBox, cell.badgeGame, xScale, sy, th.bgHeader, th.borderSubtle, th.textCol);
                    }
                }
            }
        }

        if (!m_footer.empty()) {
            widget::Footer(px, py + bodyH + 3.0f * sy, pw, th.menuFooterH * sy, padX,
                           m_footer, th.fontFooterX * xScale, th.fontFooterY * sy,
                           xScale, sy, th.bgFooter, th.textFooter, th.borderSubtle);
        }
    }

private:
    void ClampSelection() {
        int n = static_cast<int>(m_cells.size());
        if (n <= 0) m_selected = 0;
        else m_selected = std::clamp(m_selected, 0, n - 1);
    }

    std::string           m_title;
    std::string           m_badge;
    std::string           m_footer;
    std::vector<GridCell> m_cells;
    int                   m_cols     = 4;
    int                   m_rows     = 4;
    float                 m_cellW    = 42.0f;
    float                 m_cellH    = 24.0f;
    int                   m_selected = 0;
    float                 m_x640     = 24.0f;
    float                 m_y448     = 72.0f;
};

// ColorGrid: Swatch specialization built strictly on top of Tier 2 ui::Grid!
class ColorGrid {
public:
    ColorGrid& SetTitle(const std::string& titleGame) { m_grid.SetTitle(titleGame); return *this; }
    ColorGrid& SetFooter(const std::string& footerGame) { m_grid.SetFooter(footerGame); return *this; }
    ColorGrid& SetGrid(int cols, int rows, int selectedIdx) {
        m_grid.SetLayout(cols, rows, 17.0f, 15.0f).SetSelected(selectedIdx);
        return *this;
    }
    ColorGrid& SetColors(const sa::CRGBA* colors, int count) {
        m_grid.Clear();
        for (int i = 0; i < std::max(0, count); ++i) {
            GridCell c{};
            c.useSwatch = true;
            c.swatchCol = colors[i];
            c.userId    = i;
            m_grid.AddCell(c);
        }
        return *this;
    }
    ColorGrid& SetPosition(float x640, float y448) { m_grid.SetPosition(x640, y448); return *this; }
    void Render() const { m_grid.Render(); }

private:
    Grid m_grid;
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 3: StatsCard (Built 100% from Tier 1 + Tier 1.5)
// ============================================================================
struct StatBarRow {
    std::string labelGame;
    int         curVal      = 0;
    int         prevVal     = 0;
    int         maxScale    = 100;
    std::string unitGame;
    int         pristineVal = -1;
    int         bonusVal    = 0;
    int         bonusPct    = 0;
};

class StatsCard {
public:
    StatsCard& Clear() { m_rows.clear(); m_note.clear(); return *this; }
    StatsCard& SetTitle(const std::string& titleGame, const std::string& subtitleGame = "") {
        m_title = titleGame; m_subtitle = subtitleGame; return *this;
    }
    StatsCard& SetNote(const std::string& noteGame) { m_note = noteGame; return *this; }
    StatsCard& SetAnchor(bool bottomRight, float w640 = 0.0f, float rightMargin640 = 16.0f, float yOffset448 = 18.0f) {
        m_bottomRight = bottomRight; m_bottomLeft = false; m_w640 = w640; m_rightMargin = rightMargin640; m_yOffset = yOffset448; return *this;
    }
    StatsCard& SetAnchorBottomLeft(float w640 = 0.0f, float leftMargin640 = 16.0f, float yOffset448 = 18.0f) {
        m_bottomRight = true; m_bottomLeft = true; m_w640 = w640; m_rightMargin = leftMargin640; m_yOffset = yOffset448; return *this;
    }
    StatsCard& AddBar(const std::string& labelGame, int curVal, int prevVal, int maxScale,
                      const std::string& unitGame = "", int pristineVal = -1,
                      int bonusVal = 0, int bonusPct = 0) {
        m_rows.push_back({labelGame, curVal, prevVal, std::max(1, maxScale), unitGame,
                          pristineVal, std::max(0, bonusVal), std::max(0, bonusPct)});
        return *this;
    }

    void Render() const {
        if (m_rows.empty()) return;

        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        const float w640    = (m_w640 > 1.0f) ? std::clamp(m_w640, 128.0f, 196.0f) : th.cardDefaultW;
        const float pw      = w640 * xScale;
        const float padX    = 5.5f * xScale;
        const float headerH = th.cardHeaderH * sy;
        const float rowH    = th.cardRowH * sy;
        const float noteH   = m_note.empty() ? 0.0f : (th.cardNoteH * sy);
        const float padBot  = 4.5f * sy;
        const float bodyH   = headerH + static_cast<float>(m_rows.size()) * rowH + noteH + padBot;

        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float pxRaw   = m_bottomLeft ? (m_rightMargin * xScale) : (sw - pw - m_rightMargin * xScale);
        const float px      = std::clamp(pxRaw, marginX, std::max(marginX, sw - pw - marginX));
        const float pyRaw   = m_bottomRight ? (sh - bodyH - m_yOffset * sy) : (m_yOffset * sy);
        const float py      = std::clamp(pyRaw, marginY, std::max(marginY, sh - bodyH - marginY));

        // 1. Framed Panel + Header
        prim::Panel(px, py, pw, bodyH, th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);
        widget::Header(px, py, pw, headerH, padX, m_title, m_subtitle,
                       th.fontTitleX * 0.88f * xScale, th.fontTitleY * 0.88f * sy,
                       th.fontStatX * xScale, th.fontStatY * sy,
                       xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textNormal, true, 0.50f);

        // 2. Stat Rows (Label + ProgressBar)
        const float barX = px + padX;
        const float barW = pw - padX * 2.0f;
        const float barH = th.cardBarH * sy;

        for (size_t i = 0; i < m_rows.size(); ++i) {
            const auto& it     = m_rows[i];
            const float ry     = py + headerH + 2.0f * sy + static_cast<float>(i) * rowH;
            const int delta    = it.prevVal - it.curVal;
            const int wearLoss = (it.pristineVal > it.curVal) ? (it.pristineVal - it.curVal) : 0;
            const int shownVal = ((delta != 0) ? it.prevVal : it.curVal) + it.bonusVal;

            char valBuf[96];
            int pos = std::snprintf(valBuf, sizeof(valBuf), "%d%s", shownVal, it.unitGame.c_str());
            if (pos > 0 && static_cast<size_t>(pos) < sizeof(valBuf)) {
                if (delta > 0) {
                    pos += std::snprintf(valBuf + pos, sizeof(valBuf) - static_cast<size_t>(pos), " ~g~+%d", delta);
                } else if (delta < 0) {
                    pos += std::snprintf(valBuf + pos, sizeof(valBuf) - static_cast<size_t>(pos), " ~r~%d", delta);
                } else if (wearLoss > 0) {
                    pos += std::snprintf(valBuf + pos, sizeof(valBuf) - static_cast<size_t>(pos), " ~r~-%d", wearLoss);
                }
            }
            if (it.bonusPct > 0 && pos > 0 && static_cast<size_t>(pos) < sizeof(valBuf)) {
                std::snprintf(valBuf + pos, sizeof(valBuf) - static_cast<size_t>(pos), " ~y~+%d%%", it.bonusPct);
            }

            prim::Label(barX, ry, barW * 0.44f, it.labelGame.c_str(),
                        1, th.fontStatX * xScale, th.fontStatY * sy, 1, th.textNormal);
            prim::Label(px + pw - padX, ry, barW * 0.54f, valBuf,
                        1, th.fontStatX * xScale, th.fontStatY * sy, 2, th.textSelected);

            const float invMax   = 1.0f / static_cast<float>(it.maxScale);
            const float curFrac  = static_cast<float>(it.curVal)  * invMax;
            const float prevFrac = static_cast<float>(it.prevVal) * invMax;
            const float prisFrac = (wearLoss > 0) ? (static_cast<float>(it.pristineVal) * invMax) : curFrac;
            float bonusFrac      = 0.0f;
            if (it.bonusVal > 0) {
                bonusFrac = std::clamp(static_cast<float>(it.bonusVal) * invMax, 0.018f, 1.0f);
            } else if (it.bonusPct > 0) {
                bonusFrac = std::clamp(static_cast<float>(it.bonusPct) * 0.01f, 0.018f, 1.0f);
            }

            widget::ProgressBarProps bp;
            bp.curFrac   = curFrac;
            bp.prevFrac  = prevFrac;
            bp.prisFrac  = prisFrac;
            bp.bonusFrac = bonusFrac;
            bp.delta     = delta;
            bp.wearLoss  = wearLoss;
            bp.trackCol  = th.barTrack;
            bp.fillCol   = th.barFill;
            bp.gainCol   = th.barGain;
            bp.lossCol   = th.barLoss;
            bp.bonusCol  = th.barBonus;
            widget::ProgressBar(barX, ry + 9.5f * sy, barW, barH, bp);
        }

        if (!m_note.empty()) {
            const float ny = py + headerH + static_cast<float>(m_rows.size()) * rowH + 2.0f * sy;
            prim::Label(barX, ny, barW, m_note.c_str(),
                        1, th.fontFooterX * xScale, th.fontFooterY * sy, 1, th.textSuccess);
        }
    }

private:
    std::string             m_title;
    std::string             m_subtitle;
    std::string             m_note;
    std::vector<StatBarRow> m_rows;
    bool                    m_bottomRight = false;
    bool                    m_bottomLeft  = false;
    float                   m_w640        = 0.0f;
    float                   m_rightMargin = 16.0f;
    float                   m_yOffset     = 18.0f;
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 4: PhoneFrame (1990s Keypad Handset)
// ============================================================================
enum class PhoneTheme : uint8_t {
    ClassicSA = 0,
    Nokia92   = 1,
    Amber90s  = 2,
    NightBlue = 3,
    Count
};

struct PhonePalette {
    sa::CRGBA lcdBg, lcdTop, lcdSel, lcdBorder, lcdTitle, lcdText, lcdAccent;
    sa::CRGBA chassisBg, chassisCap, earpiece, navKey, numKey, callKey, endKey;
};

inline PhonePalette GetPhonePalette(PhoneTheme theme) {
    const sa::CRGBA chassisBg  = {22, 22, 26, 242};
    const sa::CRGBA chassisCap = {38, 38, 44, 245};
    const sa::CRGBA earpiece   = {120, 120, 128, 210};
    const sa::CRGBA navKey     = {78, 78, 86, 220};
    const sa::CRGBA numKey     = {52, 52, 58, 215};
    const sa::CRGBA callKey    = {65, 165, 80, 215};
    const sa::CRGBA endKey     = {185, 65, 65, 215};

    if (theme == PhoneTheme::Nokia92) {
        return {{18, 30, 16, 225}, {12, 22, 10, 242}, {135, 195, 110, 78},
                {120, 150, 110, 180}, {205, 245, 185, 255}, {172, 215, 152, 255}, {160, 245, 130, 255},
                chassisBg, chassisCap, earpiece, navKey, numKey, callKey, endKey};
    }
    if (theme == PhoneTheme::Amber90s) {
        return {{26, 17, 6, 225}, {18, 11, 4, 242}, {235, 175, 70, 75},
                {175, 135, 75, 180}, {255, 220, 145, 255}, {230, 188, 115, 255}, {255, 210, 110, 255},
                chassisBg, chassisCap, earpiece, navKey, numKey, callKey, endKey};
    }
    if (theme == PhoneTheme::NightBlue) {
        return {{10, 18, 34, 225}, {6, 12, 24, 242}, {110, 165, 240, 76},
                {115, 145, 195, 180}, {215, 235, 255, 255}, {180, 205, 238, 255}, {135, 205, 255, 255},
                chassisBg, chassisCap, earpiece, navKey, numKey, callKey, endKey};
    }
    const auto& th = Theme();
    return {th.bgBody, th.bgHeader, th.selectFill, th.borderSubtle, th.textTitle, th.textNormal, th.textSuccess,
            chassisBg, chassisCap, earpiece, navKey, numKey, callKey, endKey};
}

class PhoneFrame {
public:
    PhoneFrame& Clear() { m_rows.clear(); m_banner.clear(); return *this; }
    PhoneFrame& SetTheme(PhoneTheme t) { m_theme = t; return *this; }
    PhoneFrame& SetHeader(const std::string& appTitleGame, const std::string& subTitleGame = "") {
        m_title = appTitleGame; m_subtitle = subTitleGame; return *this;
    }
    PhoneFrame& SetStatusLine(const std::string& leftStatusGame, const std::string& clockStr) {
        m_statusLeft = leftStatusGame; m_clockStr = clockStr; return *this;
    }
    PhoneFrame& SetBanner(const std::string& bannerGame, sa::CRGBA col = Theme().textSuccess) {
        m_banner = bannerGame; m_bannerCol = col; return *this;
    }
    PhoneFrame& SetFooter(const std::string& footerGame) { m_footer = footerGame; return *this; }
    PhoneFrame& SetMaxVisible(int n) { m_maxVisible = std::max(1, n); return *this; }
    PhoneFrame& SetSelected(int idx) { m_selected = idx; return *this; }
    PhoneFrame& AddItem(const std::string& leftGame, const std::string& rightGame = "",
                        sa::CRGBA rightCol = Theme().textNormal, bool enabled = true, int userId = 0) {
        m_rows.push_back({leftGame, rightGame, enabled, rightCol, userId});
        return *this;
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto pal = GetPhonePalette(m_theme);
        const auto& th = Theme();

        const float pw       = 148.0f * xScale;
        const float bezelX   = 4.5f * xScale;
        const float bezelTop = 7.0f * sy;
        const float keypadH  = 28.0f * sy;
        const float statusH  = 11.0f * sy;
        const float headerH  = 15.5f * sy;
        const float subH     = m_subtitle.empty() ? 0.0f : (9.8f * sy);
        const float bannerH  = m_banner.empty()   ? 0.0f : (12.5f * sy);
        const float rowH     = 12.6f * sy;
        const float footerH  = 13.0f * sy;
        const float padX     = 5.0f * xScale;

        const int totalRows = static_cast<int>(m_rows.size());
        const int visible   = std::min(std::max(1, totalRows), m_maxVisible);
        int sel             = (totalRows > 0) ? std::clamp(m_selected, 0, totalRows - 1) : 0;
        int scrollTop       = (totalRows > visible) ? std::clamp(sel - (visible / 2), 0, totalRows - visible) : 0;

        const float screenH = statusH + headerH + subH + bannerH + (visible * rowH) + 3.5f * sy + footerH;
        const float totalW  = pw + bezelX * 2.0f;
        const float totalH  = bezelTop + screenH + keypadH;

        const float marginX = 16.0f * xScale;
        const float marginY = 14.0f * sy;
        const float ox = std::clamp(sw - totalW - marginX, marginX, std::max(marginX, sw - totalW - marginX));
        const float oy = std::clamp(sh - totalH - marginY, marginY + 14.0f * sy, std::max(marginY + 14.0f * sy, sh - totalH - marginY));

        // 1. Handset stub antenna, chassis & earpiece
        const float antW = 5.5f * xScale, antH = 12.5f * sy;
        const float antX = ox + totalW - 15.0f * xScale;
        prim::Panel(antX, oy - antH, antW, antH, pal.chassisBg, pal.lcdBorder, 0.6f * xScale, 0.5f * sy);
        prim::Rect(antX - 0.8f * xScale, oy - antH - 1.2f * sy, antW + 1.6f * xScale, 3.4f * sy, pal.chassisCap);

        prim::Panel(ox, oy, totalW, totalH, pal.chassisBg, pal.lcdBorder, 0.9f * xScale, 0.9f * sy);
        for (int d = -1; d <= 1; ++d) {
            float dx = ox + totalW * 0.5f + d * 4.5f * xScale;
            prim::Rect(dx - 1.1f * xScale, oy + 2.6f * sy, 2.2f * xScale, 1.6f * sy, pal.earpiece);
        }

        // 2. Recessed LCD Screen Panel
        const float sx = ox + bezelX, syTop = oy + bezelTop;
        prim::Panel(sx, syTop, pw, screenH, pal.lcdBg, pal.lcdBorder, 0.7f * xScale, 0.7f * sy);

        VStack lcd({sx, syTop, pw, screenH}, 0.0f);

        // 3. Status Bar
        RectBox stBox = lcd.Next(statusH);
        prim::Rect(stBox.x, stBox.y, stBox.w, stBox.h, pal.lcdTop);
        for (int b = 0; b < 4; ++b) {
            float bh = (2.0f + b * 1.3f) * sy;
            prim::Rect(stBox.x + padX + b * 2.3f * xScale, stBox.y + statusH - 2.0f * sy - bh,
                       1.4f * xScale, bh, pal.lcdText);
        }
        if (!m_statusLeft.empty()) {
            prim::Label(stBox.x + padX + 11.5f * xScale, stBox.y + 1.4f * sy, pw * 0.44f, m_statusLeft.c_str(),
                        1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 1, pal.lcdText);
        }
        const float batW = 8.2f * xScale, batH = 4.4f * sy;
        const float batX = stBox.x + pw - padX - batW, batY = stBox.y + 3.2f * sy;
        prim::Panel(batX + 0.9f * xScale, batY + 0.8f * sy, batW - 2.3f * xScale, batH - 1.6f * sy,
                    pal.lcdAccent, pal.lcdBorder, 0.8f * xScale, 0.7f * sy);
        if (!m_clockStr.empty()) {
            prim::Label(batX - 3.2f * xScale, stBox.y + 1.4f * sy, 34.0f * xScale, m_clockStr.c_str(),
                        1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 2, pal.lcdTitle);
        }

        // 4. App Header
        std::string cntBadge;
        if (totalRows > visible) {
            char cbuf[20];
            std::snprintf(cbuf, sizeof(cbuf), "%d/%d", sel + 1, totalRows);
            cntBadge = cbuf;
        }
        RectBox hdrBox = lcd.Next(headerH);
        widget::Header(hdrBox.x, hdrBox.y, hdrBox.w, hdrBox.h, padX, m_title, cntBadge,
                       th.fontTitleX * 0.86f * xScale, th.fontTitleY * 0.86f * sy,
                       th.fontBadgeX * xScale, th.fontBadgeY * sy,
                       xScale, sy, pal.lcdTop, pal.lcdBorder, pal.lcdTitle, pal.lcdText,
                       m_subtitle.empty(), 0.20f);

        if (!m_subtitle.empty()) {
            RectBox subBox = lcd.Next(subH);
            prim::Label(subBox.x + padX, subBox.y + 0.8f * sy, pw - padX * 2.0f, m_subtitle.c_str(),
                        1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 1, pal.lcdText);
            prim::Divider(subBox.x + 2.0f * xScale, subBox.y + subBox.h - 0.6f * sy,
                          pw - 4.0f * xScale, 0.6f * sy, pal.lcdBorder);
        }

        if (!m_banner.empty()) {
            RectBox banBox = lcd.Next(bannerH);
            widget::Banner(banBox.x, banBox.y, banBox.w, banBox.h, padX, m_banner,
                           th.fontBadgeX * xScale, th.fontBadgeY * sy,
                           xScale, sy, th.bgBanner, m_bannerCol);
        }

        // 5. Rows
        for (int i = 0; i < visible && (scrollTop + i) < totalRows; ++i) {
            int idx = scrollTop + i;
            const auto& r = m_rows[idx];
            RectBox rBox = lcd.Next(rowH);
            widget::Row(rBox.x, rBox.y, rBox.w, rBox.h, padX,
                        r.leftGame, r.rightGame, (idx == sel), r.enabled,
                        th.fontSubX * xScale, th.fontSubY * sy, xScale, sy,
                        pal.lcdSel, pal.lcdText, r.rightColor, th.textSelected, th.textDisabled);
        }

        // 6. Softkey Footer
        widget::Footer(sx, syTop + screenH - footerH, pw, footerH, padX,
                       m_footer, th.fontBadgeX * 0.95f * xScale, th.fontBadgeY * 0.95f * sy,
                       xScale, sy, pal.lcdTop, pal.lcdText, {0, 0, 0, 0}, pal.lcdBorder);

        // 7. Physical 1990s keypad cluster
        const float kpTop = syTop + screenH + 3.2f * sy;
        prim::Rect(sx + 8.0f * xScale, kpTop, 18.0f * xScale, 3.4f * sy, pal.callKey);
        prim::Rect(sx + pw * 0.5f - 14.0f * xScale, kpTop - 0.6f * sy, 28.0f * xScale, 4.8f * sy, pal.navKey);
        prim::Rect(sx + pw - 26.0f * xScale, kpTop, 18.0f * xScale, 3.4f * sy, pal.endKey);

        const float gridTop = kpTop + 6.8f * sy;
        const float btnW = 24.0f * xScale, btnH = 3.2f * sy, gapX = 12.0f * xScale;
        const float gx0 = sx + (pw - (btnW * 3.0f + gapX * 2.0f)) * 0.5f;
        for (int r = 0; r < 3; ++r) {
            float by = gridTop + r * (btnH + 2.2f * sy);
            for (int c = 0; c < 3; ++c) {
                prim::Rect(gx0 + c * (btnW + gapX), by, btnW, btnH, pal.numKey);
            }
        }
    }

private:
    PhoneTheme           m_theme = PhoneTheme::ClassicSA;
    std::string          m_title;
    std::string          m_subtitle;
    std::string          m_statusLeft;
    std::string          m_clockStr;
    std::string          m_banner;
    sa::CRGBA            m_bannerCol = {115, 232, 130, 255};
    std::string          m_footer;
    std::vector<MenuRow> m_rows;
    int                  m_selected   = 0;
    int                  m_maxVisible = 9;
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 5: HelpToast (Modern GTA SA Titled Notification!)
// 100% dumb: receives title + body + semantic tone as props (NO tag-parsing logic!)
// ============================================================================
enum class ToastTone : uint8_t {
    Neutral = 0,
    Success,
    Warning,
    Danger
};

class HelpToast {
public:
    HelpToast& SetTitle(const std::string& gameTitle) { m_title = gameTitle; return *this; }
    HelpToast& SetText(const std::string& gameEncodedText) { m_text = gameEncodedText; return *this; }
    HelpToast& SetTone(ToastTone tone) { m_tone = tone; return *this; }
    HelpToast& SetAlpha(float alpha01) { m_alpha = std::clamp(alpha01, 0.0f, 1.0f); return *this; }
    HelpToast& SetWidth640(float w640) { m_w640 = std::clamp(w640, 150.0f, 250.0f); return *this; }

    void Render() const {
        if (m_text.empty() || m_alpha <= 0.01f) return;

        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        const float accentW = 2.6f * xScale;
        const float padX    = 6.5f * xScale;
        const float padY    = 4.5f * sy;
        const float hdrH    = m_title.empty() ? 0.0f : (th.toastHeaderH * sy);
        const float pw      = m_w640 * xScale;
        const float textW   = pw - accentW - padX * 2.0f;

        const float px    = 20.0f * xScale;
        const float py    = 22.0f * sy;
        const float fontX = th.fontToastX * xScale;
        const float fontY = th.fontToastY * sy;

        const float textX = px + accentW + padX;
        const float textY = py + hdrH + padY;
        const int   lines = prim::MeasureWrappedLines(textX, textY, textW, m_text.c_str(), 1, fontX, fontY);
        const float lineH = 11.4f * sy;
        const float boxH  = hdrH + padY * 2.0f + static_cast<float>(lines) * lineH + 1.5f * sy;

        sa::CRGBA accent = th.textTitle;
        if (m_tone == ToastTone::Danger)       accent = th.textDanger;
        else if (m_tone == ToastTone::Success) accent = th.textSuccess;
        else if (m_tone == ToastTone::Warning) accent = th.textWarning;

        // 1. Framed Modern GTA SA translucent box
        prim::Panel(px, py, pw, boxH, WithAlpha(th.bgBody, m_alpha), WithAlpha(th.borderSubtle, m_alpha),
                    th.borderThickness * xScale, th.borderThickness * sy);

        // 2. Clean Header bar ("Звонок" / "Обучение" / "Уведомление")
        if (hdrH > 0.0f) {
            widget::Header(px + accentW, py, pw - accentW, hdrH, padX, m_title, "",
                           th.fontSubX * 1.06f * xScale, th.fontSubY * 1.06f * sy,
                           th.fontBadgeX * xScale, th.fontBadgeY * sy,
                           xScale, sy, WithAlpha(th.bgHeader, m_alpha), WithAlpha(th.divider, m_alpha),
                           WithAlpha(accent, m_alpha), WithAlpha(th.textCol, m_alpha), true, 0.0f);
        }

        // 3. Semantic left accent strip + auto-wrapped body text
        prim::AccentStrip(px, py, accentW, boxH, WithAlpha(accent, m_alpha));
        prim::WrappedText(textX, textY, textW, m_text.c_str(), 1, fontX, fontY, WithAlpha(th.textSelected, m_alpha));
    }

private:
    std::string m_title;
    std::string m_text;
    ToastTone   m_tone  = ToastTone::Neutral;
    float       m_alpha = 1.0f;
    float       m_w640  = 196.0f;
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 6: RadialWheel (100% Dumb & Tier-1 Composed!)
// Uses prim::Arc, prim::Disc, prim::Icon, prim::Label & Proper Shaders RW Guard
// ============================================================================
struct WheelSectorProps {
    bool        hasItems      = false;
    bool        isActive      = false;
    void*       iconTex       = nullptr;
    std::string fallbackLabel;
    std::string subLabel;
    sa::CRGBA   subLabelColor = {204, 204, 208, 255};
    int         subIndex      = 0;
    int         subCount      = 0;
};

class RadialWheel {
public:
    void SetSector(int idx, const WheelSectorProps& props) {
        if (idx >= 0 && idx < 8) m_sectors[idx] = props;
    }
    void SetHovered(int idx) { m_hovered = std::clamp(idx, 0, 7); }
    void SetCenter(const std::string& titleGame, const std::string& subGame,
                   sa::CRGBA subCol = Theme().textNormal) {
        m_centerTitle    = titleGame;
        m_centerSub      = subGame;
        m_centerSubColor = subCol;
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        constexpr float kPi  = 3.14159265358979323846f;
        constexpr float kPi2 = 6.28318530718530717959f;

        const auto& th      = Theme();
        const float cx      = sw * 0.5f;
        const float cy      = sh * 0.48f;
        const float innerR  = 40.0f * sy;
        const float outerR  = 98.0f * sy;
        const float rimR    = 101.5f * sy;
        const float gapRad  = 0.032f;
        const float stepRad = kPi2 / 8.0f;

        // 1. Sector arcs & center hub rendered cleanly via Tier 1 prim::Arc / prim::Disc + Proper Shaders RW guard
        {
            integrations::proper_shaders::RwRenderStateGuard rwGuard;
            rwGuard.Configure2DFlat();

            for (int i = 0; i < 8; ++i) {
                const float mid = -kPi * 0.5f + static_cast<float>(i) * stepRad;
                const float a0  = mid - stepRad * 0.5f + gapRad;
                const float a1  = mid + stepRad * 0.5f - gapRad;
                const auto& s   = m_sectors[i];
                const bool hov  = (i == m_hovered);

                sa::CRGBA fill = hov ? th.wheelFillHover
                                     : (s.isActive ? th.wheelFillEquipped
                                                   : (s.hasItems ? th.wheelFill : th.wheelFillEmpty));
                prim::Arc(cx, cy, innerR, outerR, a0, a1, 14, fill);

                sa::CRGBA rim = hov ? th.wheelRimHover
                                    : (s.hasItems ? th.wheelRim : th.wheelRimEmpty);
                prim::Arc(cx, cy, outerR, hov ? rimR : (outerR + 1.6f * sy), a0, a1, 14, rim);
            }

            prim::Disc(cx, cy, innerR - 2.0f * sy, 36, th.wheelHubBg);
            prim::Arc(cx, cy, innerR - 3.2f * sy, innerR - 2.0f * sy, 0.0f, kPi2, 40, th.wheelHubRim);
        }

        // 2. Icons & sector labels via Tier 1 prim::Icon + prim::Label
        for (int i = 0; i < 8; ++i) {
            const auto& s = m_sectors[i];
            if (!s.hasItems) continue;
            const float mid   = -kPi * 0.5f + static_cast<float>(i) * stepRad;
            const float iconR = (innerR + outerR) * 0.50f;
            const float ix    = cx + std::cos(mid) * iconR;
            const float iy    = cy + std::sin(mid) * iconR - 3.5f * sy;
            const bool hov    = (i == m_hovered);
            const float half  = (hov ? 18.0f : 16.0f) * sy;

            if (s.iconTex) {
                prim::Icon(s.iconTex, ix, iy, half, half,
                           hov ? th.textSelected : WithAlpha(th.textNormal, 0.88f));
            } else if (!s.fallbackLabel.empty()) {
                prim::Label(ix, iy - 4.0f * sy, 48.0f * xScale, s.fallbackLabel.c_str(),
                            1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 0,
                            hov ? th.textSelected : th.textNormal);
            }
            if (!s.subLabel.empty()) {
                prim::Label(ix, iy + half - 1.5f * sy, 44.0f * xScale, s.subLabel.c_str(),
                            1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 0, s.subLabelColor);
            }
            if (s.subCount > 1) {
                char cbuf[16];
                std::snprintf(cbuf, sizeof(cbuf), "<%d/%d>", s.subIndex + 1, s.subCount);
                prim::Label(ix, iy + half + 6.5f * sy, 44.0f * xScale, cbuf,
                            1, th.fontBadgeX * 0.95f * xScale, th.fontBadgeY * 0.95f * sy, 0,
                            hov ? th.textSuccess : th.textCol);
            }
        }

        // 3. Center hub labels
        const float hubMaxW = (innerR - 7.0f * sy) * 1.85f;
        if (!m_centerTitle.empty()) {
            prim::Label(cx, cy - (m_centerSub.empty() ? 5.5f : 11.0f) * sy, hubMaxW, m_centerTitle.c_str(),
                        2, th.fontRowX * xScale, th.fontRowY * sy, 0, th.textSelected);
        }
        if (!m_centerSub.empty()) {
            prim::Label(cx, cy + 4.5f * sy, hubMaxW, m_centerSub.c_str(),
                        1, th.fontFooterX * xScale, th.fontFooterY * sy, 0, m_centerSubColor);
        }
    }

private:
    WheelSectorProps m_sectors[8];
    int              m_hovered        = 0;
    std::string      m_centerTitle;
    std::string      m_centerSub;
    sa::CRGBA        m_centerSubColor = {204, 204, 208, 255};
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 7: TextInputModal (100% Dumb Tier 2 Class)
// ============================================================================
class TextInputModal {
public:
    TextInputModal& SetTitle(const std::string& titleGame, const std::string& badgeGame = "") {
        m_title = titleGame; m_badge = badgeGame; return *this;
    }
    TextInputModal& SetText(const std::string& text) { m_text = text; return *this; }
    TextInputModal& SetPlaceholder(const std::string& phGame) { m_placeholder = phGame; return *this; }
    TextInputModal& SetMaxLength(int maxLen) { m_maxLen = std::max(1, maxLen); return *this; }
    TextInputModal& SetHint(const std::string& hintGame) { m_hint = hintGame; return *this; }
    TextInputModal& SetFooter(const std::string& footerGame) { m_footer = footerGame; return *this; }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th    = Theme();
        const float padX  = th.menuPadX * xScale;
        const float pw    = 182.0f * xScale;
        const float hdrH  = th.menuHeaderH * sy;
        const float inpH  = 26.0f * sy;
        const float subH  = m_hint.empty() ? 0.0f : (11.0f * sy);
        const float bodyH = hdrH + inpH + subH + 8.0f * sy;
        const float footH = m_footer.empty() ? 0.0f : (th.menuFooterH * sy);

        const float px = (sw - pw) * 0.5f;
        const float py = std::clamp(sh * 0.68f, th.screenMarginY * sy,
                                    sh - (bodyH + footH + 5.0f * sy) - th.screenMarginY * sy);

        prim::Panel(px, py, pw, bodyH, th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);
        widget::Header(px, py, pw, hdrH, padX, m_title, m_badge,
                       th.fontTitleX * 0.92f * xScale, th.fontTitleY * 0.92f * sy,
                       th.fontColX * xScale, th.fontColY * sy,
                       xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textSuccess, true, 0.25f);

        const float bx = px + padX, bw = pw - padX * 2.0f, bh = 19.5f * sy, by = py + hdrH + 5.0f * sy;
        widget::TextField(bx, by, bw, bh, m_text, m_placeholder, m_maxLen,
                          xScale, sy, th.borderSubtle, th.bgInput, th.textSelected, th.textDisabled, th.textCol);

        if (!m_hint.empty()) {
            prim::Label(bx, by + bh + 2.5f * sy, bw, m_hint.c_str(),
                        1, th.fontBadgeX * xScale, th.fontBadgeY * sy, 1, th.textCol);
        }

        if (!m_footer.empty()) {
            widget::Footer(px, py + bodyH + 3.0f * sy, pw, footH, padX,
                           m_footer, th.fontFooterX * xScale, th.fontFooterY * sy,
                           xScale, sy, th.bgFooter, th.textFooter, th.borderSubtle);
        }
    }

private:
    std::string m_title;
    std::string m_badge;
    std::string m_text;
    std::string m_placeholder;
    std::string m_hint;
    std::string m_footer;
    int         m_maxLen = 8;
};

// ============================================================================
// TIER 2 — COMPOSITE CONTAINER 8: RankBarHUD (100% Dumb GTA Online Top XP Bar)
// Composed strictly from Tier 1 prim::Panel, prim::Rect, prim::Disc, prim::Arc,
// prim::Label + Proper Shaders RW State Guard!
// ============================================================================
struct RankPopupProps {
    std::string textGame;          // e.g. "+35 ОП" / "+35 XP"
    float       progress01 = 0.0f; // 0.0 = spawn below bar -> 1.0 = risen & faded out
    float       offsetX640 = 0.0f; // slight horizontal stagger for rapid kill chains
    sa::CRGBA   color      = {120, 224, 145, 255};
};

struct RankBarProps {
    int         currentRank   = 1;
    int         nextRank      = 2;
    bool        isMaxRank     = false;
    float       baseFrac      = 0.0f; // settled XP fraction [0..1]
    float       fillFrac      = 0.0f; // animated/target XP fraction [0..1]
    float       alpha         = 1.0f; // overall bar visibility alpha [0..1]
    float       slideOffsetY  = 0.0f; // virtual-448 upward slide offset when entering/leaving
    bool        levelUpFlash  = false;
    float       levelUpAlpha  = 0.0f; // 0..1 glow & caption intensity on rank-up
    std::string centerTextGame;       // e.g. "420 / 560 ОП" / "420 / 560 XP"
    std::string captionGame;          // optional caption below bar (e.g. "НОВЫЙ УРОВЕНЬ 6")
};

class RankBarHUD {
public:
    RankBarHUD& SetProps(const RankBarProps& props) {
        m_props = props;
        return *this;
    }
    RankBarHUD& ClearPopups() {
        m_popups.clear();
        return *this;
    }
    RankBarHUD& AddPopup(const RankPopupProps& p) {
        m_popups.push_back(p);
        return *this;
    }

    void Render() const {
        if (m_props.alpha <= 0.01f && m_popups.empty()) return;

        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        constexpr float kPi2 = 6.28318530718530717959f;
        const auto& th       = Theme();
        const float a        = std::clamp(m_props.alpha, 0.0f, 1.0f);

        // Minimalist, ultra-thin GTA Online top RP bar (compact & unobtrusive)
        const float cx      = sw * 0.5f;
        const float barW    = 132.0f * xScale;
        const float barH    = 3.8f * sy;
        const float barX    = cx - barW * 0.5f;
        const float barY    = (14.5f - m_props.slideOffsetY) * sy;
        const float badgeR  = 6.4f * sy;
        const float badgeY  = barY + barH * 0.5f;
        const float leftCX  = barX;
        const float rightCX = barX + barW;

        if (a > 0.01f) {
            // 1. Razor-thin progress bar track + settled fill + green newly earned surge
            const float trackX = leftCX + badgeR - 0.5f * xScale;
            const float trackW = std::max(20.0f * xScale, barW - (badgeR * 2.0f) + 1.0f * xScale);
            const float bThX   = 0.50f * xScale;
            const float bThY   = 0.50f * sy;

            prim::Panel(trackX, barY, trackW, barH,
                        WithAlpha(th.barTrack, a * 0.92f), WithAlpha(th.borderSubtle, a * 0.72f),
                        bThX, bThY);

            const float inX   = trackX + bThX;
            const float inY   = barY + bThY;
            const float inW   = std::max(1.0f, trackW - bThX * 2.0f);
            const float inH   = std::max(1.0f, barH - bThY * 2.0f);
            const float baseF = std::clamp(m_props.baseFrac, 0.0f, 1.0f);
            const float fillF = std::clamp(m_props.fillFrac, 0.0f, 1.0f);

            if (baseF > 0.002f) {
                sa::CRGBA fillCol = m_props.levelUpFlash ? th.barGain : th.barFill;
                prim::Rect(inX, inY, inW * baseF, inH, WithAlpha(fillCol, a));
            }
            if (fillF > baseF + 0.002f) {
                prim::Rect(inX + inW * baseF, inY, inW * (fillF - baseF), inH,
                           WithAlpha(th.barGain, a));
            }

            // 2. Compact circular current / next rank badges via Tier 1 Disc + Arc + RW Guard
            {
                integrations::proper_shaders::RwRenderStateGuard rwGuard;
                rwGuard.Configure2DFlat();

                const sa::CRGBA leftRing = m_props.levelUpFlash ? th.textSuccess : th.borderSubtle;
                prim::Disc(leftCX, badgeY, badgeR, 24, WithAlpha(th.bgHeader, a * 0.94f));
                prim::Arc(leftCX, badgeY, badgeR - 0.85f * sy, badgeR, 0.0f, kPi2, 26, WithAlpha(leftRing, a));

                prim::Disc(rightCX, badgeY, badgeR, 24, WithAlpha(th.bgBody, a * 0.86f));
                prim::Arc(rightCX, badgeY, badgeR - 0.70f * sy, badgeR, 0.0f, kPi2, 26, WithAlpha(th.borderSubtle, a * 0.75f));
            }

            // 3. Crisp small rank digits centered inside both circles & minimalist XP label beneath bar
            char leftBuf[16], rightBuf[16];
            std::snprintf(leftBuf, sizeof(leftBuf), "%d", m_props.currentRank);
            if (m_props.isMaxRank) std::snprintf(rightBuf, sizeof(rightBuf), "*");
            else                   std::snprintf(rightBuf, sizeof(rightBuf), "%d", m_props.nextRank);

            // Upright font 1 centered geometrically inside badgeR circle (verified on 3x live crops!)
            const float numScaleX = th.fontBadgeX * 1.08f * xScale;
            const float numScaleY = th.fontBadgeY * 1.08f * sy;
            const float numY      = badgeY - numScaleY * 11.4f;
            const float numShiftX = 0.45f * xScale;
            prim::Label(leftCX + numShiftX, numY, badgeR * 1.75f, leftBuf,
                        1, numScaleX, numScaleY, 0,
                        WithAlpha(m_props.levelUpFlash ? th.textSuccess : th.textSelected, a));

            prim::Label(rightCX + numShiftX, numY, badgeR * 1.75f, rightBuf,
                        1, numScaleX * 0.96f, numScaleY * 0.96f, 0,
                        WithAlpha(th.textCol, a));

            const float subY = barY + barH + 3.2f * sy;
            if (!m_props.captionGame.empty() && m_props.levelUpAlpha > 0.05f) {
                prim::Label(cx, subY, trackW, m_props.captionGame.c_str(),
                            1, th.fontBadgeX * 1.00f * xScale, th.fontBadgeY * 1.00f * sy, 0,
                            WithAlpha(th.textSuccess, a * std::clamp(m_props.levelUpAlpha, 0.0f, 1.0f)));
            } else if (!m_props.centerTextGame.empty()) {
                prim::Label(cx, subY, trackW, m_props.centerTextGame.c_str(),
                            1, th.fontBadgeX * 0.94f * xScale, th.fontBadgeY * 0.94f * sy, 0,
                            WithAlpha(th.textNormal, a * 0.92f));
            }
        }

        // 4. Compact rising green "+X ОП" / "+X XP" popups feeding smoothly upward
        for (const auto& pop : m_popups) {
            if (pop.textGame.empty()) continue;
            const float t = std::clamp(pop.progress01, 0.0f, 1.0f);
            float popA = 1.0f;
            if (t < 0.14f)      popA = t / 0.14f;
            else if (t > 0.62f) popA = (1.0f - t) / 0.38f;
            popA = std::clamp(popA, 0.0f, 1.0f);
            if (popA <= 0.02f) continue;

            const float px = cx + pop.offsetX640 * xScale;
            const float py = (barY + barH + 24.5f * sy) - t * 8.5f * sy;

            prim::Label(px, py, 72.0f * xScale, pop.textGame.c_str(),
                        1, th.fontBadgeX * 1.06f * xScale, th.fontBadgeY * 1.06f * sy, 0,
                        WithAlpha(pop.color, popA));
        }
    }

private:
    RankBarProps                m_props{};
    std::vector<RankPopupProps> m_popups;
};

// ============================================================================
// TIER 2 — FRONTEND MENU COMPONENTS (Dedicated Dumb Components for Game Menus)
// Modernized Classic GTA SA + Definitive Edition + GTA V Top-Tab Layout
// Strictly composed from Tier 1 prim::* + widget::* + ThemeTokens (ZERO logic!)
// ============================================================================
struct MenuShellMetrics {
    float sw        = 0.0f;
    float sh        = 0.0f;
    float xScale    = 1.0f;
    float sy        = 1.0f;
    float frameX    = 0.0f; // Left edge of centered content frame
    float frameW    = 0.0f; // Responsive centered width (clean across 4:3, 16:9, 21:9!)
    float mastY     = 0.0f;
    float mastH     = 0.0f;
    float tabsY     = 0.0f;
    float tabsH     = 0.0f;
    float crumbY    = 0.0f;
    float contentY  = 0.0f;
    float contentH  = 0.0f;
    float footerY   = 0.0f;
    float footerH   = 0.0f;
    float colGap    = 0.0f;
    float leftColW  = 0.0f;
    float rightColX = 0.0f;
    float rightColW = 0.0f;
};

inline MenuShellMetrics ComputeMenuShellMetrics(bool hasTabs = true, bool hasBreadcrumb = false) {
    MenuShellMetrics m{};
    GetScreenScales(m.sw, m.sh, m.xScale, m.sy);
    if (m.sw < 100.0f || m.sh < 100.0f) return m;

    // Responsive centered frame: fills ~86% on 16:9, capped nicely on ultrawide so tabs never sprawl
    const float marginX = std::max(28.0f * m.xScale, (m.sw - 580.0f * m.xScale) * 0.5f);
    m.frameX   = marginX;
    m.frameW   = std::max(300.0f * m.xScale, m.sw - marginX * 2.0f);
    m.mastY    = 13.0f * m.sy;
    m.mastH    = 14.5f * m.sy;
    m.tabsY    = m.mastY + m.mastH + 4.0f * m.sy;
    m.tabsH    = hasTabs ? (19.5f * m.sy) : 0.0f;
    m.crumbY   = m.tabsY + m.tabsH + (hasTabs ? 5.5f * m.sy : 2.0f * m.sy);
    m.contentY = m.crumbY + (hasBreadcrumb ? 14.5f * m.sy : 4.5f * m.sy);
    m.footerH  = 18.0f * m.sy;
    m.footerY  = m.sh - m.footerH - 10.0f * m.sy;
    m.contentH = std::max(120.0f * m.sy, m.footerY - m.contentY - 10.0f * m.sy);
    m.colGap   = 10.0f * m.xScale;
    m.leftColW = m.frameW * 0.45f;
    m.rightColX = m.frameX + m.leftColW + m.colGap;
    m.rightColW = m.frameW - m.leftColW - m.colGap;
    return m;
}

// 1. MenuBackdrop: Sleek dark scrim + top/bottom subtle vignette bands over live 3D world, title art, or Map
inline void RenderMenuBackdrop(uint8_t baseAlpha = 178, bool drawVignetteBands = true) {
    // Flush any previously queued engine text (e.g. native DrawMap labels) BEFORE painting scrim/bands!
    prim::FlushText();
    const auto m = ComputeMenuShellMetrics(true, false);
    if (m.sw < 100.0f || m.sh < 100.0f) return;
    if (baseAlpha > 0) {
        prim::Rect(0.0f, 0.0f, m.sw, m.sh, {5, 6, 9, baseAlpha});
    }
    if (drawVignetteBands) {
        const uint8_t bandAlpha = (baseAlpha > 0)
                                      ? static_cast<uint8_t>(std::min(238, static_cast<int>(baseAlpha) + 42))
                                      : static_cast<uint8_t>(224);
        prim::Rect(0.0f, 0.0f, m.sw, m.tabsY + m.tabsH + 6.0f * m.sy, {4, 5, 8, bandAlpha});
        const float botTop = m.footerY - 5.5f * m.sy;
        prim::Rect(0.0f, botTop, m.sw, m.sh - botTop, {4, 5, 8, bandAlpha});
    }
}

// 2. MenuTopBar: GTA V / Definitive Edition horizontal top tab bar + masthead + breadcrumb
struct MenuTabItem {
    std::string labelGame;
    bool        active  = false;
    bool        hovered = false;
    bool        enabled = true;
};

class MenuTopBar {
public:
    MenuTopBar& SetMasthead(const std::string& leftGame, const std::string& rightGame = "",
                            sa::CRGBA rightColor = {175, 175, 180, 255}) {
        m_mastLeft  = leftGame;
        m_mastRight = rightGame;
        m_mastRightCol = rightColor;
        return *this;
    }
    MenuTopBar& SetBreadcrumb(const std::string& crumbGame) {
        m_breadcrumb = crumbGame;
        return *this;
    }
    MenuTopBar& SetShowSwitchHints(bool show) {
        m_showHints = show;
        return *this;
    }
    MenuTopBar& ClearTabs() {
        m_tabs.clear();
        return *this;
    }
    MenuTopBar& AddTab(const std::string& labelGame, bool active = false, bool hovered = false, bool enabled = true) {
        m_tabs.push_back({labelGame, active, hovered, enabled});
        return *this;
    }

    int HitTestTab(float mouseX, float mouseY) const {
        if (m_tabs.empty()) return -1;
        const auto m = ComputeMenuShellMetrics(true, !m_breadcrumb.empty());
        if (m.sw < 100.0f) return -1;
        if (mouseY < m.tabsY || mouseY > m.tabsY + m.tabsH) return -1;

        const float hintW = (m_showHints && m_tabs.size() > 1) ? (18.0f * m.xScale) : 0.0f;
        const float stripX = m.frameX + hintW;
        const float stripW = std::max(40.0f, m.frameW - hintW * 2.0f);
        if (mouseX < stripX || mouseX > stripX + stripW) return -1;

        const float tabW = stripW / static_cast<float>(m_tabs.size());
        int idx = static_cast<int>((mouseX - stripX) / tabW);
        if (idx < 0 || idx >= static_cast<int>(m_tabs.size())) return -1;
        return m_tabs[idx].enabled ? idx : -1;
    }

    void Render() const {
        const bool hasTabs = !m_tabs.empty();
        const auto m = ComputeMenuShellMetrics(hasTabs, !m_breadcrumb.empty());
        if (m.sw < 100.0f || m.sh < 100.0f) return;

        const auto& th = Theme();

        // 1. Sleek Masthead row (Title on left, status/money/rank/clock on right)
        if (!m_mastLeft.empty()) {
            prim::Label(m.frameX, m.mastY, m.frameW * 0.56f, m_mastLeft.c_str(),
                        2, th.fontTitleX * 0.90f * m.xScale, th.fontTitleY * 0.90f * m.sy, 1, th.textTitle);
        }
        if (!m_mastRight.empty()) {
            prim::Label(m.frameX + m.frameW, m.mastY + 2.2f * m.sy, m.frameW * 0.44f, m_mastRight.c_str(),
                        1, th.fontRowX * 0.92f * m.xScale, th.fontRowY * 0.92f * m.sy, 2, m_mastRightCol);
        }

        // 2. Horizontal Top Tab Bar
        if (hasTabs) {
            prim::Panel(m.frameX, m.tabsY, m.frameW, m.tabsH,
                        th.bgHeader, th.borderSubtle,
                        th.borderThickness * m.xScale, th.borderThickness * m.sy);

            const bool drawHints = m_showHints && m_tabs.size() > 1;
            const float hintW    = drawHints ? (18.0f * m.xScale) : 0.0f;
            const float stripX   = m.frameX + hintW;
            const float stripW   = std::max(40.0f, m.frameW - hintW * 2.0f);
            const float tabW     = stripW / static_cast<float>(m_tabs.size());

            if (drawHints) {
                prim::Rect(m.frameX, m.tabsY, hintW, m.tabsH, th.bgFooter);
                prim::Rect(m.frameX + m.frameW - hintW, m.tabsY, hintW, m.tabsH, th.bgFooter);
                prim::Label(m.frameX + hintW * 0.5f, m.tabsY + 4.8f * m.sy, hintW - 2.0f * m.xScale, "Q",
                            1, th.fontFooterX * 0.96f * m.xScale, th.fontFooterY * 0.96f * m.sy, 0, th.textCol);
                prim::Label(m.frameX + m.frameW - hintW * 0.5f, m.tabsY + 4.8f * m.sy, hintW - 2.0f * m.xScale, "E",
                            1, th.fontFooterX * 0.96f * m.xScale, th.fontFooterY * 0.96f * m.sy, 0, th.textCol);
                prim::Rect(stripX, m.tabsY + 2.0f * m.sy, 0.65f * m.xScale, m.tabsH - 4.0f * m.sy, th.divider);
                prim::Rect(stripX + stripW, m.tabsY + 2.0f * m.sy, 0.65f * m.xScale, m.tabsH - 4.0f * m.sy, th.divider);
            }

            for (size_t i = 0; i < m_tabs.size(); ++i) {
                const auto& t = m_tabs[i];
                const float tx = stripX + static_cast<float>(i) * tabW;
                if (t.active) {
                    prim::Rect(tx, m.tabsY, tabW, m.tabsH, th.selectFill);
                    // Clean Definitive Edition / GTA V top indicator strip
                    prim::AccentStrip(tx, m.tabsY, tabW, 2.0f * m.sy, th.textSelected);
                } else if (t.hovered && t.enabled) {
                    prim::Rect(tx, m.tabsY, tabW, m.tabsH, th.selectMuted);
                }
                if (i > 0) {
                    prim::Rect(tx, m.tabsY + 3.0f * m.sy, 0.55f * m.xScale, m.tabsH - 6.0f * m.sy,
                               WithAlpha(th.divider, 0.65f));
                }
                sa::CRGBA col = !t.enabled ? th.textDisabled
                              : (t.active || t.hovered ? th.textSelected : th.textNormal);
                prim::Label(tx + tabW * 0.5f, m.tabsY + 4.8f * m.sy, tabW - 7.0f * m.xScale,
                            t.labelGame.c_str(), 1,
                            th.fontSubX * 0.98f * m.xScale, th.fontSubY * 0.98f * m.sy, 0, col);
            }
        }

        // 3. Optional Breadcrumb Subheader ("SETTINGS / DISPLAY / ADVANCED")
        if (!m_breadcrumb.empty()) {
            prim::Label(m.frameX + 2.0f * m.xScale, m.crumbY, m.frameW - 4.0f * m.xScale,
                        m_breadcrumb.c_str(), 1,
                        th.fontFooterX * 0.96f * m.xScale, th.fontFooterY * 0.96f * m.sy, 1, th.textCol);
        }
        prim::FlushText();
    }

private:
    std::string              m_mastLeft;
    std::string              m_mastRight;
    sa::CRGBA                m_mastRightCol = {175, 175, 180, 255};
    std::string              m_breadcrumb;
    bool                     m_showHints = true;
    std::vector<MenuTabItem> m_tabs;
};

// 3. MenuOptionList: Sleek Definitive Edition action & settings list with inline sliders
struct MenuOptionItem {
    std::string labelGame;
    std::string valueGame;
    sa::CRGBA   valueColor   = {204, 204, 208, 255};
    bool        hasSlider    = false;
    float       sliderFrac01 = 0.0f;
    bool        enabled      = true;
    bool        isBack       = false;
};

class MenuOptionList {
public:
    MenuOptionList& SetBounds(float xPx, float yPx, float wPx, float maxHPx = 0.0f) {
        m_x = xPx; m_y = yPx; m_w = wPx; m_maxH = maxHPx;
        return *this;
    }
    MenuOptionList& SetHeader(const std::string& titleGame, const std::string& rightBadgeGame = "") {
        m_title = titleGame;
        m_badge = rightBadgeGame;
        return *this;
    }
    MenuOptionList& SetSelection(int selectedIdx, int hoveredIdx = -1) {
        m_selected = selectedIdx;
        m_hovered  = hoveredIdx;
        return *this;
    }
    MenuOptionList& ClearItems() {
        m_items.clear();
        return *this;
    }
    MenuOptionList& AddItem(const MenuOptionItem& item) {
        m_items.push_back(item);
        return *this;
    }

    int HitTestRow(float mouseX, float mouseY) const {
        if (m_items.empty()) return -1;
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f) return -1;

        const float hdrH = m_title.empty() ? 0.0f : (17.0f * sy);
        const float rowH = 17.5f * sy;
        const float topY = m_y + hdrH + 3.0f * sy;
        if (mouseX < m_x || mouseX > m_x + m_w) return -1;
        if (mouseY < topY) return -1;

        int idx = static_cast<int>((mouseY - topY) / rowH);
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) return -1;
        return m_items[idx].enabled ? idx : -1;
    }

    bool HitTestSlider(int rowIdx, float mouseX, float mouseY, float& outFrac01) const {
        if (rowIdx < 0 || rowIdx >= static_cast<int>(m_items.size())) return false;
        if (!m_items[rowIdx].hasSlider || !m_items[rowIdx].enabled) return false;
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f) return false;

        const float hdrH = m_title.empty() ? 0.0f : (17.0f * sy);
        const float rowH = 17.5f * sy;
        const float ry   = m_y + hdrH + 3.0f * sy + static_cast<float>(rowIdx) * rowH;
        if (mouseY < ry || mouseY > ry + rowH) return false;

        const float padX   = 8.5f * xScale;
        const float barW   = std::min(84.0f * xScale, m_w * 0.36f);
        const float barX   = m_x + m_w - padX - barW;
        if (mouseX < barX - 4.0f * xScale || mouseX > barX + barW + 4.0f * xScale) return false;

        outFrac01 = std::clamp((mouseX - barX) / std::max(1.0f, barW), 0.0f, 1.0f);
        return true;
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th   = Theme();
        const float padX = 8.5f * xScale;
        const float hdrH = m_title.empty() ? 0.0f : (17.0f * sy);
        const float rowH = 17.5f * sy;
        const float padB = 5.0f * sy;
        const float naturalH = hdrH + 3.0f * sy + static_cast<float>(m_items.size()) * rowH + padB;
        const float panelH   = (m_maxH > 0.0f) ? std::min(m_maxH, std::max(naturalH, 48.0f * sy)) : naturalH;

        prim::Panel(m_x, m_y, m_w, panelH,
                    th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);

        if (hdrH > 0.0f) {
            widget::Header(m_x, m_y, m_w, hdrH, padX, m_title, m_badge,
                           th.fontSubX * 1.02f * xScale, th.fontSubY * 1.02f * sy,
                           th.fontBadgeX * 1.08f * xScale, th.fontBadgeY * 1.08f * sy,
                           xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol, true, 0.34f);
            prim::FlushText();
        }

        const float topY = m_y + hdrH + 3.0f * sy;
        const float fx   = th.fontRowX * 0.94f * xScale;
        const float fy   = th.fontRowY * 0.94f * sy;
        const float vfx  = th.fontSubX * 0.94f * xScale;
        const float vfy  = th.fontSubY * 0.94f * sy;

        for (size_t i = 0; i < m_items.size(); ++i) {
            const float ry = topY + static_cast<float>(i) * rowH;
            if (ry + rowH > m_y + panelH + 1.0f) break;

            const auto& it   = m_items[i];
            const bool isSel = (static_cast<int>(i) == m_selected);
            const bool isHov = (static_cast<int>(i) == m_hovered) && !isSel && it.enabled;

            if (isSel) {
                prim::Rect(m_x + 1.0f * xScale, ry, m_w - 2.0f * xScale, rowH,
                           it.enabled ? th.selectFill : th.selectMuted);
                prim::AccentStrip(m_x + 1.0f * xScale, ry + 1.0f * sy, 2.2f * xScale, rowH - 2.0f * sy,
                                  it.enabled ? th.textSelected : th.textDisabled);
            } else if (isHov) {
                prim::Rect(m_x + 1.0f * xScale, ry, m_w - 2.0f * xScale, rowH, th.selectMuted);
            }

            const bool hasRight = it.hasSlider || !it.valueGame.empty();
            const float leftMaxW = hasRight ? (m_w * 0.56f - padX) : (m_w - padX * 2.0f);
            sa::CRGBA leftCol = !it.enabled ? th.textDisabled
                              : (isSel ? th.textSelected : (it.isBack ? th.textCol : th.textNormal));

            prim::Label(m_x + padX + (isSel ? 2.5f * xScale : 0.0f), ry + 3.4f * sy, leftMaxW,
                        it.labelGame.c_str(), 1, fx, fy, 1, leftCol);

            if (it.hasSlider) {
                const float barW = std::min(84.0f * xScale, m_w * 0.36f);
                const float barH = 3.6f * sy;
                const float barX = m_x + m_w - padX - barW;
                const float barY = ry + (rowH - barH) * 0.5f;
                const float f01  = std::clamp(it.sliderFrac01, 0.0f, 1.0f);
                widget::ProgressBarProps bp{};
                bp.curFrac  = f01;
                bp.prevFrac = f01;
                bp.trackCol = th.barTrack;
                bp.fillCol  = isSel ? th.textSelected : th.barFill;
                bp.gainCol  = th.barGain;
                bp.lossCol  = th.barLoss;
                bp.bonusCol = th.barBonus;
                widget::ProgressBar(barX, barY, barW, barH, bp);
            } else if (!it.valueGame.empty()) {
                sa::CRGBA rCol = !it.enabled ? th.textDisabled
                               : (isSel ? th.textSelected : it.valueColor);
                prim::Label(m_x + m_w - padX, ry + 3.8f * sy, m_w * 0.42f - padX,
                            it.valueGame.c_str(), 1, vfx, vfy, 2, rCol);
            }
            if ((i & 3u) == 3u) prim::FlushText();
        }
        prim::FlushText();
    }

private:
    float                       m_x        = 0.0f;
    float                       m_y        = 0.0f;
    float                       m_w        = 200.0f;
    float                       m_maxH     = 0.0f;
    std::string                 m_title;
    std::string                 m_badge;
    int                         m_selected = 0;
    int                         m_hovered  = -1;
    std::vector<MenuOptionItem> m_items;
};

// 4. MenuInfoCard: Clean right-hand contextual detail / stats / brief card
struct MenuInfoRow {
    std::string leftGame;
    std::string rightGame;
    sa::CRGBA   rightColor = {204, 204, 208, 255};
};

struct MenuInfoBar {
    std::string labelGame;
    std::string valueGame;
    float       frac01   = 0.0f;
    sa::CRGBA   barColor = {225, 225, 230, 242};
};

class MenuInfoCard {
public:
    MenuInfoCard& SetBounds(float xPx, float yPx, float wPx, float hPx) {
        m_x = xPx; m_y = yPx; m_w = wPx; m_h = hPx;
        return *this;
    }
    MenuInfoCard& SetHeader(const std::string& titleGame, const std::string& badgeGame = "") {
        m_title = titleGame;
        m_badge = badgeGame;
        return *this;
    }
    MenuInfoCard& SetDescription(const std::string& descGame) {
        m_desc = descGame;
        return *this;
    }
    MenuInfoCard& SetFooterNote(const std::string& noteGame) {
        m_note = noteGame;
        return *this;
    }
    MenuInfoCard& AddRow(const std::string& leftGame, const std::string& rightGame,
                         sa::CRGBA rightColor = {204, 204, 208, 255}) {
        m_rows.push_back({leftGame, rightGame, rightColor});
        return *this;
    }
    MenuInfoCard& AddBar(const std::string& labelGame, const std::string& valueGame,
                         float frac01, sa::CRGBA barColor = {225, 225, 230, 242}) {
        m_bars.push_back({labelGame, valueGame, std::clamp(frac01, 0.0f, 1.0f), barColor});
        return *this;
    }
    MenuInfoCard& AddParagraph(const std::string& paraGame) {
        if (!paraGame.empty()) m_paragraphs.push_back(paraGame);
        return *this;
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th   = Theme();
        const float padX = 9.0f * xScale;
        const float hdrH = m_title.empty() ? 0.0f : (17.0f * sy);
        const float noteH = m_note.empty() ? 0.0f : (14.0f * sy);

        prim::Panel(m_x, m_y, m_w, m_h,
                    th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);

        if (hdrH > 0.0f) {
            widget::Header(m_x, m_y, m_w, hdrH, padX, m_title, m_badge,
                           th.fontSubX * 1.02f * xScale, th.fontSubY * 1.02f * sy,
                           th.fontBadgeX * 1.08f * xScale, th.fontBadgeY * 1.08f * sy,
                           xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol, true, 0.34f);
            prim::FlushText();
        }

        float curY   = m_y + hdrH + 7.0f * sy;
        const float maxY = m_y + m_h - noteH - 5.0f * sy;
        const float innerW = std::max(20.0f, m_w - padX * 2.0f);

        // Optional summary description
        if (!m_desc.empty() && curY < maxY) {
            const float fx = th.fontSubX * 0.96f * xScale;
            const float fy = th.fontSubY * 0.96f * sy;
            int lines = prim::MeasureWrappedLines(m_x + padX, curY, innerW, m_desc.c_str(), 1, fx, fy);
            prim::WrappedText(m_x + padX, curY, innerW, m_desc.c_str(), 1, fx, fy, th.textNormal);
            prim::FlushText();
            curY += static_cast<float>(lines) * (10.5f * sy) + 6.0f * sy;
            if (!m_rows.empty() || !m_bars.empty() || !m_paragraphs.empty()) {
                prim::Divider(m_x + padX, curY - 2.5f * sy, innerW, 0.6f * sy, th.divider);
                curY += 3.0f * sy;
            }
        }

        // Progress bars (e.g. Player Stats / Progression XP / Skills)
        for (size_t i = 0; i < m_bars.size(); ++i) {
            const auto& b = m_bars[i];
            if (curY + 17.0f * sy > maxY) break;
            prim::Label(m_x + padX, curY, innerW * 0.68f, b.labelGame.c_str(),
                        1, th.fontSubX * 0.92f * xScale, th.fontSubY * 0.92f * sy, 1, th.textNormal);
            if (!b.valueGame.empty()) {
                prim::Label(m_x + m_w - padX, curY, innerW * 0.30f, b.valueGame.c_str(),
                            1, th.fontSubX * 0.92f * xScale, th.fontSubY * 0.92f * sy, 2, th.textSelected);
            }
            widget::ProgressBarProps bp{};
            bp.curFrac  = b.frac01;
            bp.prevFrac = b.frac01;
            bp.trackCol = th.barTrack;
            bp.fillCol  = b.barColor;
            bp.gainCol  = th.barGain;
            bp.lossCol  = th.barLoss;
            bp.bonusCol = th.barBonus;
            widget::ProgressBar(m_x + padX, curY + 10.0f * sy, innerW, 3.6f * sy, bp);
            if ((i % 3u) == 2u) prim::FlushText();
            curY += 17.5f * sy;
        }
        if (!m_bars.empty()) prim::FlushText();

        // Key-value detail rows
        for (size_t i = 0; i < m_rows.size(); ++i) {
            if (curY + 13.5f * sy > maxY) break;
            const auto& r = m_rows[i];
            if ((i & 1u) == 1u) {
                prim::Rect(m_x + 2.0f * xScale, curY - 1.2f * sy, m_w - 4.0f * xScale, 13.5f * sy,
                           {255, 255, 255, 8});
            }
            prim::Label(m_x + padX, curY + 0.6f * sy, innerW * 0.62f, r.leftGame.c_str(),
                        1, th.fontSubX * 0.92f * xScale, th.fontSubY * 0.92f * sy, 1, th.textNormal);
            prim::Label(m_x + m_w - padX, curY + 0.6f * sy, innerW * 0.36f, r.rightGame.c_str(),
                        1, th.fontSubX * 0.92f * xScale, th.fontSubY * 0.92f * sy, 2, r.rightColor);
            if ((i & 3u) == 3u) prim::FlushText();
            curY += 13.5f * sy;
        }
        if (!m_rows.empty()) prim::FlushText();

        // Wrapped paragraphs (e.g. Brief log entries)
        for (size_t i = 0; i < m_paragraphs.size(); ++i) {
            if (curY + 12.0f * sy > maxY) break;
            const float fx = th.fontSubX * 0.92f * xScale;
            const float fy = th.fontSubY * 0.92f * sy;
            int lines = prim::MeasureWrappedLines(m_x + padX, curY, innerW, m_paragraphs[i].c_str(), 1, fx, fy);
            float blockH = static_cast<float>(lines) * (10.2f * sy);
            if (curY + blockH > maxY) break;
            prim::WrappedText(m_x + padX, curY, innerW, m_paragraphs[i].c_str(), 1, fx, fy,
                              (i == 0) ? th.textSelected : th.textNormal);
            prim::FlushText();
            curY += blockH + 5.5f * sy;
            if (i + 1 < m_paragraphs.size() && curY < maxY) {
                prim::Divider(m_x + padX, curY - 2.8f * sy, innerW, 0.55f * sy, WithAlpha(th.divider, 0.6f));
            }
        }

        // Optional bottom status note
        if (!m_note.empty()) {
            prim::Divider(m_x + padX, m_y + m_h - noteH, innerW, 0.6f * sy, th.divider);
            prim::Label(m_x + padX, m_y + m_h - noteH + 3.0f * sy, innerW, m_note.c_str(),
                        1, th.fontBadgeX * 1.02f * xScale, th.fontBadgeY * 1.02f * sy, 1, th.textCol);
            prim::FlushText();
        }
    }

private:
    float                    m_x = 0.0f, m_y = 0.0f, m_w = 240.0f, m_h = 200.0f;
    std::string              m_title;
    std::string              m_badge;
    std::string              m_desc;
    std::string              m_note;
    std::vector<MenuInfoRow> m_rows;
    std::vector<MenuInfoBar> m_bars;
    std::vector<std::string> m_paragraphs;
};

// 5. MenuConfirmModal: Centered Definitive Edition Yes/No or OK dialog
class MenuConfirmModal {
public:
    MenuConfirmModal& SetTitle(const std::string& titleGame) { m_title = titleGame; return *this; }
    MenuConfirmModal& SetPrompt(const std::string& promptGame) { m_prompt = promptGame; return *this; }
    MenuConfirmModal& ClearButtons() { m_buttons.clear(); return *this; }
    MenuConfirmModal& AddButton(const std::string& labelGame, bool isDanger = false) {
        m_buttons.push_back({labelGame, isDanger});
        return *this;
    }
    MenuConfirmModal& SetSelection(int selectedIdx, int hoveredIdx = -1) {
        m_selected = selectedIdx;
        m_hovered  = hoveredIdx;
        return *this;
    }

    int HitTestButton(float mouseX, float mouseY) const {
        if (m_buttons.empty()) return -1;
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f) return -1;

        float bx, by, bw, bh;
        ComputeLayout(sw, sh, xScale, sy, bx, by, bw, bh);
        const float padX   = 10.0f * xScale;
        const float btnH   = 19.0f * sy;
        const float btnY   = by + bh - btnH - 8.5f * sy;
        if (mouseY < btnY || mouseY > btnY + btnH) return -1;

        const int   n     = static_cast<int>(m_buttons.size());
        const float gap   = 7.0f * xScale;
        const float totW  = bw - padX * 2.0f;
        const float eachW = (totW - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            float x0 = bx + padX + static_cast<float>(i) * (eachW + gap);
            if (mouseX >= x0 && mouseX <= x0 + eachW) return i;
        }
        return -1;
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        float bx, by, bw, bh;
        ComputeLayout(sw, sh, xScale, sy, bx, by, bw, bh);
        const float padX = 10.0f * xScale;
        const float hdrH = 18.0f * sy;

        prim::Panel(bx, by, bw, bh, th.bgBody, th.borderSubtle,
                    th.borderThickness * xScale, th.borderThickness * sy);
        widget::Header(bx, by, bw, hdrH, padX, m_title, "",
                       th.fontTitleX * 0.82f * xScale, th.fontTitleY * 0.82f * sy,
                       th.fontBadgeX * xScale, th.fontBadgeY * sy,
                       xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol, true, 0.0f);
        prim::FlushText();

        if (!m_prompt.empty()) {
            prim::WrappedText(bx + padX, by + hdrH + 9.0f * sy, bw - padX * 2.0f,
                              m_prompt.c_str(), 1,
                              th.fontRowX * 0.94f * xScale, th.fontRowY * 0.94f * sy, th.textNormal);
            prim::FlushText();
        }

        if (!m_buttons.empty()) {
            const int   n     = static_cast<int>(m_buttons.size());
            const float btnH  = 19.0f * sy;
            const float btnY  = by + bh - btnH - 8.5f * sy;
            const float gap   = 7.0f * xScale;
            const float totW  = bw - padX * 2.0f;
            const float eachW = (totW - gap * static_cast<float>(n - 1)) / static_cast<float>(n);

            for (int i = 0; i < n; ++i) {
                const float x0   = bx + padX + static_cast<float>(i) * (eachW + gap);
                const bool isSel = (i == m_selected);
                const bool isHov = (i == m_hovered) && !isSel;
                sa::CRGBA bg     = isSel ? th.selectFill : (isHov ? th.selectMuted : th.bgHeader);
                sa::CRGBA border = isSel ? (m_buttons[i].isDanger ? th.textDanger : th.textSelected)
                                         : th.borderSubtle;
                prim::Panel(x0, btnY, eachW, btnH, bg, border, 0.7f * xScale, 0.7f * sy);
                sa::CRGBA txtCol = isSel ? th.textSelected
                                         : (m_buttons[i].isDanger ? th.textDanger : th.textNormal);
                prim::Label(x0 + eachW * 0.5f, btnY + 4.3f * sy, eachW - 6.0f * xScale,
                            m_buttons[i].labelGame.c_str(), 1,
                            th.fontSubX * 0.98f * xScale, th.fontSubY * 0.98f * sy, 0, txtCol);
            }
            prim::FlushText();
        }
    }

private:
    struct Btn { std::string labelGame; bool isDanger = false; };

    void ComputeLayout(float sw, float sh, float xScale, float sy,
                       float& outX, float& outY, float& outW, float& outH) const {
        const auto& th = Theme();
        outW = std::min(256.0f * xScale, sw - 48.0f * xScale);
        int lines = 1;
        if (!m_prompt.empty()) {
            lines = prim::MeasureWrappedLines(0.0f, 0.0f, outW - 20.0f * xScale,
                                              m_prompt.c_str(), 1,
                                              th.fontRowX * 0.94f * xScale, th.fontRowY * 0.94f * sy);
        }
        outH = (18.0f + 16.0f + static_cast<float>(lines) * 11.2f + 29.0f) * sy;
        outX = (sw - outW) * 0.5f;
        outY = (sh - outH) * 0.48f;
    }

    std::string      m_title;
    std::string      m_prompt;
    std::vector<Btn> m_buttons;
    int              m_selected = 0;
    int              m_hovered  = -1;
};

// 6. MenuFooterBar: Clean minimalist full-width footer control strip
class MenuFooterBar {
public:
    MenuFooterBar& SetHints(const std::string& leftGame, const std::string& rightGame = "") {
        m_left  = leftGame;
        m_right = rightGame;
        return *this;
    }

    void Render() const {
        const auto m = ComputeMenuShellMetrics(false, false);
        if (m.sw < 100.0f || m.sh < 100.0f) return;

        const auto& th   = Theme();
        const float padX = 8.0f * m.xScale;
        prim::Panel(m.frameX, m.footerY, m.frameW, m.footerH,
                    th.bgFooter, th.borderSubtle,
                    th.borderThickness * m.xScale, th.borderThickness * m.sy);
        if (!m_left.empty()) {
            prim::Label(m.frameX + padX, m.footerY + 4.2f * m.sy, m.frameW * 0.62f, m_left.c_str(),
                        1, th.fontFooterX * 0.98f * m.xScale, th.fontFooterY * 0.98f * m.sy, 1, th.textFooter);
        }
        if (!m_right.empty()) {
            prim::Label(m.frameX + m.frameW - padX, m.footerY + 4.2f * m.sy, m.frameW * 0.34f, m_right.c_str(),
                        1, th.fontFooterX * 0.96f * m.xScale, th.fontFooterY * 0.96f * m.sy, 2, th.textCol);
        }
        prim::FlushText();
    }

private:
    std::string m_left;
    std::string m_right;
};

// 7. MenuCursor: Crisp vector arrow pointer (never blurry or aspect-stretched!)
inline void RenderMenuCursor(float mouseX, float mouseY) {
    float sw, sh, xScale, sy;
    GetScreenScales(sw, sh, xScale, sy);
    if (sw < 100.0f || sh < 100.0f) return;

    const float s   = 9.5f * sy;
    const float z   = sa::rw::GetNearScreenZ();
    const float rhw = sa::rw::GetRecipNearClip();
    const uint32_t colFill = sa::rw::PackColor(250, 250, 252, 245);
    const uint32_t colEdge = sa::rw::PackColor(8, 8, 12, 235);

    auto makeTri = [&](float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col) {
        sa::rw::RwIm2DVertex v[3]{};
        v[0] = {x0, y0, z, rhw, col, 0.0f, 0.0f};
        v[1] = {x1, y1, z, rhw, col, 0.0f, 0.0f};
        v[2] = {x2, y2, z, rhw, col, 0.0f, 0.0f};
        sa::rw::Im2DRenderPrimitive(3 /*rwPRIMTYPETRILIST*/, v, 3);
    };

    integrations::proper_shaders::RwRenderStateGuard rwGuard;
    rwGuard.Configure2DFlat();
    // Subtle dark shadow backing + crisp white modern pointer arrow
    makeTri(mouseX - 0.8f, mouseY - 0.8f, mouseX + s * 0.82f + 1.2f, mouseY + s * 0.56f + 0.8f,
            mouseX + s * 0.24f + 0.6f, mouseY + s * 0.96f + 1.2f, colEdge);
    makeTri(mouseX, mouseY, mouseX + s * 0.78f, mouseY + s * 0.52f,
            mouseX + s * 0.22f, mouseY + s * 0.90f, colFill);
}

} // namespace ui
