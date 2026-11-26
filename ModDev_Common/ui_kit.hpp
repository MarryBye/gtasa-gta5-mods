#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "sa_sdk.hpp"
#include "text_rus.hpp"
#include "mod_bus.hpp"

// ============================================================================
// Shared GTA SA Atomic Dumb-Component UI Kit (3-Tier Web-Style Architecture)
//
// Strict Layer Hierarchy:
//   Tier 0 — ThemeTokens (`ui::Theme()`)
//            Single source of truth for palette, spacing, fonts & global scale
//            (synchronized live across ALL mods from GTA5UIKit.ini via ModBus).
//
//   Tier 1 — Dumb Primitives (`ui::prim::*`)
//            THE ONLY LAYER THAT TOUCHES `sa::font::DrawRect` / `CFont::PrintString`!
//            Primitives: Rect, Panel, Divider, AccentStrip, Label (auto-fit),
//            WrappedText, Header, Row, Footer, ProgressBar, TextField, Banner, Swatch.
//            Styling a primitive automatically restyles EVERY window in EVERY mod!
//
//   Tier 2 — Composite Dumb Components (100% composed from `ui::prim::*`)
//            - ui::ListMenu        (scrollable list menus, shops, vanilla restyle)
//            - ui::StatsCard       (vehicle/weapon/vitals stat bars + ETS2 red wear)
//            - ui::PhoneFrame      (authentic 1990s keypad handset with LCD themes)
//            - ui::TextInputModal  (sleek monochrome text entry modal, e.g. plates)
//            - ui::HelpToast       (restyled vanilla notifications / phone prompts / Q&A)
//            - ui::ColorGrid       (restyled vanilla CMenuSystem DisplayGridMenu swatches)
//            - ui::RadialWheel     (8-sector monochrome GTA SA weapon wheel)
// ============================================================================
namespace ui {

// ----------------------------------------------------------------------------
// Localization & Native HUD Helpers
// ----------------------------------------------------------------------------
inline bool& UseRussianFlag() {
    static bool useRus = true;
    return useRus;
}

inline void SetUseRussian(bool rus) { UseRussianFlag() = rus; }

inline std::string Loc(const char* rusUtf8, const char* engAscii) {
    return UseRussianFlag() ? text::ToGameRussian(rusUtf8) : std::string(engAscii);
}

inline void ShowHelp(const char* rusUtf8, const char* engAscii, bool quick = true) {
    std::string s = Loc(rusUtf8, engAscii);
    sa::SetHelpMessage(s.c_str(), quick, false, false);
}

inline void ShowHelpGameEncoded(const std::string& encoded, bool quick = true) {
    sa::SetHelpMessage(encoded.c_str(), quick, false, false);
}

inline void ShowMessage(const char* rusUtf8, const char* engAscii, uint32_t ms = 3500) {
    std::string s = Loc(rusUtf8, engAscii);
    sa::AddMessageJumpQ(s.c_str(), ms, 0, false);
}

inline std::string FormatPrice(int price) {
    if (price <= 0) return Loc("Беспл.", "Free");
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%d", price);
    return std::string(buf);
}

// ----------------------------------------------------------------------------
// Keyboard Input Tracker
// ----------------------------------------------------------------------------
class KeyTracker {
public:
    void Update() {
        uint32_t now = GetTickCount();
        for (int vk = 0; vk < 256; ++vk) {
            bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            m_justPressed[vk] = down && !m_wasDown[vk];
            m_repeated[vk]    = false;

            if (m_justPressed[vk]) {
                m_repeated[vk]   = true;
                m_nextRepeat[vk] = now + 340;
            } else if (down && now >= m_nextRepeat[vk]) {
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
    bool     m_wasDown[256]     = {};
    bool     m_justPressed[256] = {};
    bool     m_repeated[256]    = {};
    uint32_t m_nextRepeat[256]  = {};
};

inline KeyTracker& Keys() {
    static KeyTracker kt;
    return kt;
}

// ============================================================================
// TIER 0: Root Theme Tokens & WidescreenFix-Matched Scale Engine
// ============================================================================
struct ThemeTokens {
    float uiScale        = 1.0f;
    float screenMarginX  = 14.0f;
    float screenMarginY  = 14.0f;

    // Shared geometry (in 640x448 virtual coordinates)
    float menuDefaultW   = 176.0f;
    float menuHeaderH    = 17.5f;
    float menuColBarH    = 12.0f;
    float menuRowH       = 13.0f;
    float menuFooterH    = 14.0f;
    float menuPadX       = 6.0f;
    float menuPadBot     = 4.5f;

    float cardDefaultW   = 148.0f;
    float cardHeaderH    = 16.0f;
    float cardRowH       = 17.5f;
    float cardBarH       = 3.6f;
    float cardNoteH      = 12.5f;

    // Typography base scales (before auto-fitting!)
    float fontTitleX     = 0.34f, fontTitleY  = 0.66f;
    float fontColX       = 0.26f, fontColY    = 0.52f;
    float fontRowX       = 0.28f, fontRowY    = 0.56f;
    float fontFooterX    = 0.24f, fontFooterY = 0.48f;
    float fontStatX      = 0.25f, fontStatY   = 0.50f;
    float fontToastX     = 0.27f, fontToastY  = 0.54f;
    float fontMinRatio   = 0.36f;

    // Unified Vanilla GTA SA Monochrome Palette
    sa::CRGBA bgBody       = {0,   0,   0,   195};
    sa::CRGBA bgHeader     = {0,   0,   0,   232};
    sa::CRGBA bgFooter     = {0,   0,   0,   185};
    sa::CRGBA bgInput      = {14,  14,  16,  240};
    sa::CRGBA borderSubtle = {160, 160, 165, 90};
    sa::CRGBA divider      = {175, 175, 175, 95};
    sa::CRGBA selectFill   = {215, 215, 215, 62};
    sa::CRGBA textTitle    = {232, 232, 232, 255};
    sa::CRGBA textCol      = {175, 175, 175, 255};
    sa::CRGBA textNormal   = {195, 195, 195, 255};
    sa::CRGBA textSelected = {255, 255, 255, 255};
    sa::CRGBA textDisabled = {105, 105, 105, 255};
    sa::CRGBA textFooter   = {200, 200, 200, 255};
    sa::CRGBA barTrack     = {62,  62,  62,  190};
    sa::CRGBA barFill      = {218, 218, 218, 240};
    sa::CRGBA barGain      = {82,  222, 105, 255};
    sa::CRGBA barLoss      = {228, 78,  78,  255};
    sa::CRGBA accentWarn   = {235, 200, 95,  255};
};

inline ThemeTokens& Theme() {
    static ThemeTokens t;
    return t;
}

inline void SyncFromSharedBus() {
    if (auto* b = modbus::GetBus(); b && b->uiKitInstalled) {
        UseRussianFlag() = b->useRussian;
        Theme().uiScale  = std::clamp(b->uiScale, 0.70f, 1.40f);
    }
}

inline void GetScreenScales(float& sw, float& sh, float& xScale, float& sy) {
    SyncFromSharedBus();
    sw = static_cast<float>(sa::GetScreenWidth());
    sh = static_cast<float>(sa::GetScreenHeight());
    const float mult = Theme().uiScale;
    sy = (sh / 448.0f) * mult;
    xScale = ((sw / sh > 1.4f) ? (sh * (4.0f / 3.0f) / 640.0f) : (sw / 640.0f)) * mult;
}

inline sa::CRGBA WithAlpha(sa::CRGBA c, float alphaFrac) {
    float a = std::clamp(static_cast<float>(c.a) * std::clamp(alphaFrac, 0.0f, 1.0f), 0.0f, 255.0f);
    return {c.r, c.g, c.b, static_cast<uint8_t>(a)};
}

// ============================================================================
// TIER 1: Dumb Atomic UI Primitives (`ui::prim`)
// ALL low-level rectangles, borders, headers, dividers, rows, footers, bars,
// input fields, swatches, and typography live strictly here!
// ============================================================================
namespace prim {

inline void PrepareFontBase() {
    sa::font::SetBackground(false, false);
    sa::font::SetProportional(true);
    sa::font::SetJustify(false);
    sa::font::SetDropShadowPosition(1);
    sa::font::SetEdge(0);
    sa::font::SetDropColor({0, 0, 0, 255});
    sa::font::SetRightJustifyWrap(0.0f);
}

// 1. Rect: Standard top-left (x, y, w, h) rectangle primitive
inline void Rect(float x, float y, float w, float h, sa::CRGBA col) {
    if (col.a == 0 || w <= 0.0f || h <= 0.0f) return;
    sa::font::DrawRect({x, y + h, x + w, y}, col);
}

// 2. Panel: Styled surface box with optional subtle outline
inline void Panel(float x, float y, float w, float h, sa::CRGBA bg, sa::CRGBA border = {0, 0, 0, 0},
                  float borderPxX = 0.7f, float borderPxY = 0.7f)
{
    if (border.a > 0) {
        Rect(x - borderPxX, y - borderPxY, w + borderPxX * 2.0f, h + borderPxY * 2.0f, border);
    }
    Rect(x, y, w, h, bg);
}

// 3. Divider: Thin horizontal separator rule
inline void Divider(float x, float y, float w, float h, sa::CRGBA col) {
    Rect(x, y, w, h, col);
}

// 4. AccentStrip: Left/top semantic highlight strip (used by HelpToast & cards)
inline void AccentStrip(float x, float y, float w, float h, sa::CRGBA col) {
    Rect(x, y, w, h, col);
}

// 5. Label: Single-line auto-fitting text (NEVER overflows or wraps into columns!)
// align: 0 = Center, 1 = Left, 2 = Right
inline void Label(float x, float y, float maxWidthPx, const char* gameText,
                  uint8_t fontStyle, float scaleX, float scaleY,
                  uint8_t align, sa::CRGBA color)
{
    if (!gameText || !gameText[0] || color.a == 0) return;
    PrepareFontBase();
    sa::font::SetFontStyle(fontStyle);
    sa::font::SetScale(scaleX, scaleY);

    float fitX = scaleX;
    float fitY = scaleY;
    if (maxWidthPx > 4.0f) {
        float measured = sa::font::GetStringWidth(gameText, true, false);
        if (measured > maxWidthPx && measured > 1.0f) {
            float ratio = std::max(Theme().fontMinRatio, (maxWidthPx / measured) * 0.96f);
            fitX = scaleX * ratio;
            fitY = scaleY * (0.65f + 0.35f * ratio);
            sa::font::SetScale(fitX, fitY);
        }
    }

    sa::font::SetColor(color);
    sa::font::SetDropColor({0, 0, 0, color.a});
    sa::font::SetOrientation(align);
    const float sw = static_cast<float>(std::max(640, sa::GetScreenWidth()));
    if (align == 0)      sa::font::SetCentreSize(sw);
    else if (align == 1) sa::font::SetWrapx(sw);
    else                 sa::font::SetRightJustifyWrap(0.0f);
    sa::font::PrintString(x, y, gameText);
}

// 6. MeasureWrappedLines & WrappedText: Multi-line wrapped paragraph primitive (for HelpToast!)
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
    // Reset wrapx to full screen width afterward so nothing downstream gets clipped
    sa::font::SetWrapx(static_cast<float>(std::max(640, sa::GetScreenWidth())));
}

// 7. Header: Standard dark header bar with title, optional right badge/counter, and bottom divider
inline void Header(float x, float y, float w, float h, float padX,
                   const std::string& title, const std::string& rightBadge,
                   float titleSx, float titleSy, float badgeSx, float badgeSy,
                   float xScale, float sy,
                   sa::CRGBA bgCol, sa::CRGBA divCol, sa::CRGBA titleCol, sa::CRGBA badgeCol,
                   bool drawDivider = true, float badgeReserveRatio = 0.38f)
{
    Rect(x, y, w, h, bgCol);
    const float badgeRes = rightBadge.empty() ? 0.0f : (w * badgeReserveRatio);
    Label(x + padX, y + 2.2f * sy, w - padX * 2.0f - badgeRes, title.c_str(),
          2, titleSx, titleSy, 1, titleCol);
    if (!rightBadge.empty()) {
        Label(x + w - padX, y + 3.2f * sy, badgeRes, rightBadge.c_str(),
              1, badgeSx, badgeSy, 2, badgeCol);
    }
    if (drawDivider && divCol.a > 0) {
        Divider(x + 2.5f * xScale, y + h - 0.7f * sy, w - 5.0f * xScale, 0.7f * sy, divCol);
    }
}

// 8. Row: Standard interactive list row with highlight strip + left/right auto-fitted labels
inline void Row(float x, float y, float w, float h, float padX,
                const std::string& leftText, const std::string& rightText,
                bool selected, bool enabled,
                float fontSx, float fontSy, float xScale, float sy,
                sa::CRGBA selFill, sa::CRGBA leftCol, sa::CRGBA rightCol,
                sa::CRGBA selTextCol, sa::CRGBA disTextCol)
{
    if (selected && selFill.a > 0) {
        Rect(x + 1.8f * xScale, y + 0.4f * sy, w - 3.6f * xScale, h - 0.8f * sy, selFill);
    }
    const float rightRes = rightText.empty() ? 0.0f : (w * 0.38f);
    const float leftMaxW = w - padX * 2.0f - rightRes - (rightText.empty() ? 0.0f : 3.8f * xScale);

    sa::CRGBA lCol = selected ? selTextCol : (enabled ? leftCol : disTextCol);
    Label(x + padX, y + 1.4f * sy, leftMaxW, leftText.c_str(), 1, fontSx, fontSy, 1, lCol);

    if (!rightText.empty()) {
        sa::CRGBA rCol = selected ? selTextCol : (enabled ? rightCol : disTextCol);
        Label(x + w - padX, y + 1.4f * sy, rightRes, rightText.c_str(), 1, fontSx * 0.96f, fontSy * 0.96f, 2, rCol);
    }
}

// 9. Footer: Standard single-line hint bar with optional top divider rule
inline void Footer(float x, float y, float w, float h, float padX,
                   const std::string& text, float fontSx, float fontSy,
                   float xScale, float sy,
                   sa::CRGBA bgCol, sa::CRGBA textCol, sa::CRGBA topDivCol = {0, 0, 0, 0})
{
    Rect(x, y, w, h, bgCol);
    if (topDivCol.a > 0) {
        Divider(x + 2.0f * xScale, y, w - 4.0f * xScale, 0.6f * sy, topDivCol);
    }
    if (!text.empty()) {
        Label(x + padX, y + 2.1f * sy, w - padX * 2.0f, text.c_str(), 1, fontSx, fontSy, 1, textCol);
    }
}

// 10. ProgressBar: Metric bar with track, main fill, green delta gain & red wear/delta loss!
inline void ProgressBar(float x, float y, float w, float h,
                        float curFrac, float prevFrac, float prisFrac,
                        int delta, int wearLoss,
                        sa::CRGBA trackCol, sa::CRGBA fillCol, sa::CRGBA gainCol, sa::CRGBA lossCol)
{
    Rect(x, y, w, h, trackCol);
    curFrac  = std::clamp(curFrac,  0.0f, 1.0f);
    prevFrac = std::clamp(prevFrac, 0.0f, 1.0f);
    prisFrac = std::clamp(prisFrac, 0.0f, 1.0f);

    if (delta > 0) {
        Rect(x, y, w * curFrac, h, fillCol);
        Rect(x + w * curFrac, y, w * (prevFrac - curFrac), h, gainCol);
        if (prisFrac > prevFrac) {
            Rect(x + w * prevFrac, y, w * (prisFrac - prevFrac), h, lossCol);
        }
    } else if (delta < 0) {
        Rect(x, y, w * prevFrac, h, fillCol);
        Rect(x + w * prevFrac, y, w * (curFrac - prevFrac), h, lossCol);
    } else {
        Rect(x, y, w * curFrac, h, fillCol);
        if (wearLoss > 0 && prisFrac > curFrac) {
            Rect(x + w * curFrac, y, w * (prisFrac - curFrac), h, lossCol);
        }
    }
}

// 11. TextField: Compact bordered monochrome text entry field with blinking caret & char counter
inline void TextField(float x, float y, float w, float h,
                      const std::string& text, const std::string& placeholder, int maxLen,
                      float xScale, float sy,
                      sa::CRGBA borderCol, sa::CRGBA bgCol, sa::CRGBA textCol, sa::CRGBA phCol, sa::CRGBA cntCol)
{
    Panel(x, y, w, h, bgCol, borderCol, 0.7f * xScale, 0.7f * sy);
    const bool  blink    = ((GetTickCount() / 420) % 2) == 0;
    const float cntW     = 24.0f * xScale;
    const float textMaxW = w - 10.0f * xScale - cntW;

    if (text.empty()) {
        std::string ph = placeholder.empty() ? std::string("_") : (placeholder + (blink ? "_" : ""));
        Label(x + 4.5f * xScale, y + 3.2f * sy, textMaxW, ph.c_str(), 1, 0.27f * xScale, 0.54f * sy, 1, phCol);
    } else {
        std::string shown = text;
        if (static_cast<int>(shown.size()) < maxLen && blink) shown.push_back('_');
        Label(x + 4.5f * xScale, y + 2.6f * sy, textMaxW, shown.c_str(), 2, 0.36f * xScale, 0.68f * sy, 1, textCol);
    }

    char cntBuf[16];
    std::snprintf(cntBuf, sizeof(cntBuf), "%d/%d", static_cast<int>(text.size()), maxLen);
    Label(x + w - 4.0f * xScale, y + 4.2f * sy, cntW, cntBuf, 1, 0.22f * xScale, 0.44f * sy, 2, cntCol);
}

// 12. Banner: Centered alert/status strip
inline void Banner(float x, float y, float w, float h, float padX,
                   const std::string& text, float fontSx, float fontSy,
                   float xScale, float sy, sa::CRGBA bgCol, sa::CRGBA textCol)
{
    Rect(x + 2.0f * xScale, y + 0.8f * sy, w - 4.0f * xScale, h - 1.6f * sy, bgCol);
    Label(x + w * 0.5f, y + 1.8f * sy, w - padX * 2.0f, text.c_str(), 1, fontSx, fontSy, 0, textCol);
}

// 13. Swatch: Color tile primitive (for ColorGrid / paint menus)
inline void Swatch(float x, float y, float w, float h, sa::CRGBA fillCol,
                   bool selected, float xScale, float sy,
                   sa::CRGBA selCol = {255, 255, 255, 255}, sa::CRGBA borderCol = {90, 90, 95, 180})
{
    if (selected) {
        Panel(x, y, w, h, fillCol, selCol, 1.4f * xScale, 1.4f * sy);
    } else {
        Panel(x + 0.8f * xScale, y + 0.8f * sy, w - 1.6f * xScale, h - 1.6f * sy, fillCol, borderCol, 0.5f * xScale, 0.5f * sy);
    }
}

} // namespace prim

// Backward-compatible alias for code referencing ui::PrintFittedText
inline void PrintFittedText(float x, float y, float maxWidthPx, const char* gameText,
                            uint8_t fontStyle, float scaleX, float scaleY,
                            uint8_t align, sa::CRGBA color)
{
    prim::Label(x, y, maxWidthPx, gameText, fontStyle, scaleX, scaleY, align, color);
}

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 1: ListMenu (Built 100% from ui::prim::*)
// ============================================================================
struct MenuRow {
    std::string leftGame;
    std::string rightGame;
    bool        enabled     = true;
    bool        highlighted = false;
    sa::CRGBA   rightColor  = {200, 200, 200, 255};
    int         userId      = 0;
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
    ListMenu& SetSelected(int idx) { m_selected = idx; ClampSelection(); return *this; }
    ListMenu& SetAccent(sa::CRGBA col) { m_accent = col; m_useCustomAccent = true; return *this; }

    ListMenu& AddItem(const std::string& leftGame, const std::string& rightGame = "",
                      sa::CRGBA rightCol = {200, 200, 200, 255}, bool enabled = true, int userId = 0)
    {
        m_rows.push_back({leftGame, rightGame, enabled, false, rightCol, userId});
        ClampSelection();
        return *this;
    }

    int GetSelected() const { return m_selected; }
    int GetCount() const    { return static_cast<int>(m_rows.size()); }

    MenuEvent Update(const KeyTracker& k, bool allowBack = true) {
        ClampSelection();
        const int n = static_cast<int>(m_rows.size());
        if (n > 0) {
            if (k.Repeated(VK_UP) || k.Repeated('W')) {
                m_selected = (m_selected - 1 + n) % n;
                return {MenuSignalType::HoverChanged, m_selected, m_rows[m_selected].userId};
            }
            if (k.Repeated(VK_DOWN) || k.Repeated('S')) {
                m_selected = (m_selected + 1) % n;
                return {MenuSignalType::HoverChanged, m_selected, m_rows[m_selected].userId};
            }
            if (k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE)) {
                return {MenuSignalType::Activated, m_selected, m_rows[m_selected].userId};
            }
        }
        if (allowBack && (k.JustPressed(VK_BACK) || k.JustPressed(VK_ESCAPE) || k.JustPressed('F'))) {
            return {MenuSignalType::Back, m_selected, (n > 0) ? m_rows[m_selected].userId : 0};
        }
        return {};
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        const float w640 = (m_w640 > 1.0f) ? ((m_w640 > 205.0f) ? (m_w640 * 0.70f) : m_w640) : th.menuDefaultW;
        const float pw   = w640 * xScale;

        const int totalRows = static_cast<int>(m_rows.size());
        const int visible   = std::min(totalRows, m_maxVisible);
        int scrollTop = 0;
        if (totalRows > visible) {
            scrollTop = std::clamp(m_selected - (visible / 2), 0, totalRows - visible);
        }

        const float headerH = th.menuHeaderH * sy;
        const float colBarH = (m_colLeft.empty() && m_colRight.empty()) ? 0.0f : (th.menuColBarH * sy);
        const float rowH    = th.menuRowH    * sy;
        const float padX    = th.menuPadX    * xScale;
        const float padBot  = th.menuPadBot  * sy;
        const float bodyH   = headerH + colBarH + (visible * rowH) + padBot;
        const float footH   = m_footer.empty() ? 0.0f : (th.menuFooterH * sy + 2.5f * sy);

        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float px = std::clamp(m_x640 * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float py = std::clamp(m_y448 * sy,     marginY, std::max(marginY, sh - (bodyH + footH) - marginY));

        // 1. Body Panel primitive
        prim::Panel(px, py, pw, bodyH, th.bgBody);

        // 2. Header primitive (with optional scroll counter badge)
        std::string countBadge;
        if (totalRows > visible) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d/%d", m_selected + 1, totalRows);
            countBadge = buf;
        }
        prim::Header(px, py, pw, headerH, padX, m_title, countBadge,
                     th.fontTitleX * xScale, th.fontTitleY * sy,
                     th.fontColX * xScale, th.fontColY * sy,
                     xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol,
                     colBarH <= 0.0f, 0.22f);

        // 3. Column labels + Divider primitive
        const float listTop = py + headerH + colBarH;
        if (colBarH > 0.0f) {
            const float colY      = py + headerH + 1.5f * sy;
            const float colRightW = m_colRight.empty() ? 0.0f : (pw * 0.36f);
            prim::Label(px + padX, colY, pw - padX * 2.0f - colRightW, m_colLeft.c_str(),
                        1, th.fontColX * xScale, th.fontColY * sy, 1, th.textCol);
            if (!m_colRight.empty()) {
                prim::Label(px + pw - padX, colY, colRightW, m_colRight.c_str(),
                            1, th.fontColX * xScale, th.fontColY * sy, 2, th.textCol);
            }
            prim::Divider(px + 3.0f * xScale, listTop - 0.8f * sy, pw - 6.0f * xScale, 0.8f * sy, th.divider);
        }

        // 4. List Row primitives
        const sa::CRGBA selFill = m_useCustomAccent ? m_accent : th.selectFill;
        for (int i = 0; i < visible; ++i) {
            int idx = scrollTop + i;
            const auto& r = m_rows[idx];
            prim::Row(px, listTop + i * rowH, pw, rowH, padX,
                      r.leftGame, r.rightGame, (idx == m_selected), r.enabled,
                      th.fontRowX * xScale, th.fontRowY * sy, xScale, sy,
                      selFill, th.textNormal, r.rightColor, th.textSelected, th.textDisabled);
        }

        // 5. Footer primitive
        if (!m_footer.empty()) {
            prim::Footer(px, py + bodyH + 2.5f * sy, pw, th.menuFooterH * sy, padX,
                         m_footer, th.fontFooterX * xScale, th.fontFooterY * sy,
                         xScale, sy, th.bgFooter, th.textFooter);
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
    sa::CRGBA            m_accent          = {215, 215, 215, 62};
    bool                 m_useCustomAccent = false;
};

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 2: StatsCard (Built 100% from ui::prim::*)
// ============================================================================
struct StatBarRow {
    std::string labelGame;
    int         curVal      = 0;
    int         prevVal     = 0;
    int         maxScale    = 100;
    std::string unitGame;
    int         pristineVal = -1;
};

class StatsCard {
public:
    StatsCard& Clear() { m_rows.clear(); m_note.clear(); return *this; }
    StatsCard& SetTitle(const std::string& titleGame, const std::string& subtitleGame = "") {
        m_title = titleGame; m_subtitle = subtitleGame; return *this;
    }
    StatsCard& SetNote(const std::string& noteGame) { m_note = noteGame; return *this; }
    StatsCard& SetAnchor(bool bottomRight, float w640 = 0.0f, float rightMargin640 = 16.0f, float yOffset448 = 18.0f) {
        m_bottomRight = bottomRight; m_w640 = w640; m_rightMargin = rightMargin640; m_yOffset = yOffset448; return *this;
    }
    StatsCard& AddBar(const std::string& labelGame, int curVal, int prevVal, int maxScale,
                      const std::string& unitGame = "", int pristineVal = -1) {
        m_rows.push_back({labelGame, curVal, prevVal, std::max(1, maxScale), unitGame, pristineVal});
        return *this;
    }

    void Render() const {
        if (m_rows.empty()) return;

        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        const float w640    = (m_w640 > 1.0f) ? ((m_w640 > 175.0f) ? (m_w640 * 0.72f) : m_w640) : th.cardDefaultW;
        const float pw      = w640 * xScale;
        const float padX    = 5.5f * xScale;
        const float headerH = th.cardHeaderH * sy;
        const float rowH    = th.cardRowH * sy;
        const float noteH   = m_note.empty() ? 0.0f : (th.cardNoteH * sy);
        const float padBot  = 4.5f * sy;
        const float bodyH   = headerH + static_cast<float>(m_rows.size()) * rowH + noteH + padBot;

        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float px      = std::clamp(sw - pw - m_rightMargin * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float pyRaw   = m_bottomRight ? (sh - bodyH - m_yOffset * sy) : (m_yOffset * sy);
        const float py      = std::clamp(pyRaw, marginY, std::max(marginY, sh - bodyH - marginY));

        // 1. Body Panel + Header primitives
        prim::Panel(px, py, pw, bodyH, th.bgBody);
        prim::Header(px, py, pw, headerH, padX, m_title, m_subtitle,
                     0.29f * xScale, 0.56f * sy, th.fontStatX * xScale, th.fontStatY * sy,
                     xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textNormal, true, 0.52f);

        // 2. Stat rows (Label + ProgressBar primitives)
        const float barX = px + padX;
        const float barW = pw - padX * 2.0f;
        const float barH = th.cardBarH * sy;

        for (size_t i = 0; i < m_rows.size(); ++i) {
            const auto& it     = m_rows[i];
            const float ry     = py + headerH + 2.0f * sy + static_cast<float>(i) * rowH;
            const int delta    = it.prevVal - it.curVal;
            const int wearLoss = (it.pristineVal > it.curVal) ? (it.pristineVal - it.curVal) : 0;

            char valBuf[80];
            if (delta > 0)           std::snprintf(valBuf, sizeof(valBuf), "%d%s ~g~(+%d)", it.prevVal, it.unitGame.c_str(), delta);
            else if (delta < 0)      std::snprintf(valBuf, sizeof(valBuf), "%d%s ~r~(%d)",  it.prevVal, it.unitGame.c_str(), delta);
            else if (wearLoss > 0)   std::snprintf(valBuf, sizeof(valBuf), "%d%s ~r~(-%d)", it.curVal,  it.unitGame.c_str(), wearLoss);
            else                     std::snprintf(valBuf, sizeof(valBuf), "%d%s",          it.curVal,  it.unitGame.c_str());

            prim::Label(barX, ry, barW * 0.52f, it.labelGame.c_str(),
                        1, th.fontStatX * xScale, th.fontStatY * sy, 1, th.textNormal);
            prim::Label(px + pw - padX, ry, barW * 0.46f, valBuf,
                        1, th.fontStatX * xScale, th.fontStatY * sy, 2, th.textSelected);

            const float curFrac  = static_cast<float>(it.curVal)  / static_cast<float>(it.maxScale);
            const float prevFrac = static_cast<float>(it.prevVal) / static_cast<float>(it.maxScale);
            const float prisFrac = (wearLoss > 0)
                ? (static_cast<float>(it.pristineVal) / static_cast<float>(it.maxScale))
                : curFrac;
            prim::ProgressBar(barX, ry + 9.5f * sy, barW, barH, curFrac, prevFrac, prisFrac,
                              delta, wearLoss, th.barTrack, th.barFill, th.barGain, th.barLoss);
        }

        if (!m_note.empty()) {
            const float ny = py + headerH + static_cast<float>(m_rows.size()) * rowH + 2.0f * sy;
            prim::Label(barX, ny, barW, m_note.c_str(),
                        1, th.fontFooterX * xScale, th.fontFooterY * sy, 1, {145, 225, 155, 255});
        }
    }

private:
    std::string             m_title;
    std::string             m_subtitle;
    std::string             m_note;
    std::vector<StatBarRow> m_rows;
    bool                    m_bottomRight = false;
    float                   m_w640        = 0.0f;
    float                   m_rightMargin = 16.0f;
    float                   m_yOffset     = 18.0f;
};

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 3: PhoneFrame (1990s Keypad Handset from prim::*)
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
};

inline PhonePalette GetPhonePalette(PhoneTheme theme) {
    if (theme == PhoneTheme::Nokia92) {
        return {{18, 30, 16, 225}, {12, 22, 10, 242}, {135, 195, 110, 78},
                {120, 150, 110, 180}, {205, 245, 185, 255}, {172, 215, 152, 255}, {160, 245, 130, 255}};
    }
    if (theme == PhoneTheme::Amber90s) {
        return {{26, 17, 6, 225}, {18, 11, 4, 242}, {235, 175, 70, 75},
                {175, 135, 75, 180}, {255, 220, 145, 255}, {230, 188, 115, 255}, {255, 210, 110, 255}};
    }
    if (theme == PhoneTheme::NightBlue) {
        return {{10, 18, 34, 225}, {6, 12, 24, 242}, {110, 165, 240, 76},
                {115, 145, 195, 180}, {215, 235, 255, 255}, {180, 205, 238, 255}, {135, 205, 255, 255}};
    }
    return {{4, 4, 6, 218}, {12, 12, 15, 240}, {215, 215, 215, 62},
            {145, 145, 150, 170}, {238, 238, 238, 255}, {200, 200, 200, 255}, {135, 230, 145, 255}};
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
    PhoneFrame& SetBanner(const std::string& bannerGame, sa::CRGBA col = {120, 225, 130, 255}) {
        m_banner = bannerGame; m_bannerCol = col; return *this;
    }
    PhoneFrame& SetFooter(const std::string& footerGame) { m_footer = footerGame; return *this; }
    PhoneFrame& SetMaxVisible(int n) { m_maxVisible = std::max(1, n); return *this; }
    PhoneFrame& SetSelected(int idx) { m_selected = idx; return *this; }
    PhoneFrame& AddItem(const std::string& leftGame, const std::string& rightGame = "",
                        sa::CRGBA rightCol = {200, 200, 200, 255}, bool enabled = true, int userId = 0) {
        m_rows.push_back({leftGame, rightGame, enabled, false, rightCol, userId});
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

        // 1. 1990s stub antenna + rugged charcoal handset chassis + 3-dot earpiece (via prim::Panel/Rect)
        const float antW = 5.5f * xScale, antH = 12.5f * sy;
        const float antX = ox + totalW - 15.0f * xScale;
        prim::Panel(antX, oy - antH, antW, antH, {24, 24, 28, 242}, {100, 100, 106, 175}, 0.6f * xScale, 0.5f * sy);
        prim::Rect(antX - 0.8f * xScale, oy - antH - 1.2f * sy, antW + 1.6f * xScale, 3.4f * sy, {38, 38, 44, 245});

        prim::Panel(ox, oy, totalW, totalH, {22, 22, 26, 242}, pal.lcdBorder, 0.9f * xScale, 0.9f * sy);
        for (int d = -1; d <= 1; ++d) {
            float dx = ox + totalW * 0.5f + d * 4.5f * xScale;
            prim::Rect(dx - 1.1f * xScale, oy + 2.6f * sy, 2.2f * xScale, 1.6f * sy, {120, 120, 128, 210});
        }

        // 2. Recessed LCD screen Panel
        const float sx = ox + bezelX, syTop = oy + bezelTop;
        prim::Panel(sx, syTop, pw, screenH, pal.lcdBg, pal.lcdBorder, 0.7f * xScale, 0.7f * sy);

        // 3. Status bar (Signal bars + Carrier + Clock + Battery)
        prim::Rect(sx, syTop, pw, statusH, pal.lcdTop);
        for (int b = 0; b < 4; ++b) {
            float bh = (2.0f + b * 1.3f) * sy;
            prim::Rect(sx + padX + b * 2.3f * xScale, syTop + statusH - 2.0f * sy - bh, 1.4f * xScale, bh, pal.lcdText);
        }
        if (!m_statusLeft.empty()) {
            prim::Label(sx + padX + 11.5f * xScale, syTop + 1.4f * sy, pw * 0.44f, m_statusLeft.c_str(),
                        1, 0.21f * xScale, 0.42f * sy, 1, pal.lcdText);
        }
        const float batW = 8.2f * xScale, batH = 4.4f * sy;
        const float batX = sx + pw - padX - batW, batY = syTop + 3.2f * sy;
        prim::Panel(batX + 0.9f * xScale, batY + 0.8f * sy, batW - 2.3f * xScale, batH - 1.6f * sy,
                    pal.lcdAccent, pal.lcdBorder, 0.9f * xScale, 0.8f * sy);
        if (!m_clockStr.empty()) {
            prim::Label(batX - 3.2f * xScale, syTop + 1.4f * sy, 34.0f * xScale, m_clockStr.c_str(),
                        1, 0.22f * xScale, 0.44f * sy, 2, pal.lcdTitle);
        }

        // 4. App Header primitive
        float curY = syTop + statusH;
        std::string cntBadge;
        if (totalRows > visible) {
            char cbuf[20];
            std::snprintf(cbuf, sizeof(cbuf), "%d/%d", sel + 1, totalRows);
            cntBadge = cbuf;
        }
        prim::Header(sx, curY, pw, headerH, padX, m_title, cntBadge,
                     0.29f * xScale, 0.56f * sy, 0.21f * xScale, 0.42f * sy,
                     xScale, sy, pal.lcdTop, pal.lcdBorder, pal.lcdTitle, pal.lcdText,
                     m_subtitle.empty(), 0.20f);
        curY += headerH;

        if (!m_subtitle.empty()) {
            prim::Label(sx + padX, curY + 0.8f * sy, pw - padX * 2.0f, m_subtitle.c_str(),
                        1, 0.21f * xScale, 0.42f * sy, 1, pal.lcdText);
            curY += subH;
            prim::Divider(sx + 2.0f * xScale, curY - 0.6f * sy, pw - 4.0f * xScale, 0.6f * sy, pal.lcdBorder);
        }

        if (!m_banner.empty()) {
            prim::Banner(sx, curY, pw, bannerH, padX, m_banner, 0.22f * xScale, 0.44f * sy,
                         xScale, sy, {20, 32, 24, 210}, m_bannerCol);
            curY += bannerH;
        }

        // 5. Row primitives
        for (int i = 0; i < visible && (scrollTop + i) < totalRows; ++i) {
            int idx = scrollTop + i;
            const auto& r = m_rows[idx];
            prim::Row(sx, curY + i * rowH, pw, rowH, padX,
                      r.leftGame, r.rightGame, (idx == sel), r.enabled,
                      0.25f * xScale, 0.50f * sy, xScale, sy,
                      pal.lcdSel, pal.lcdText, r.rightColor, {255, 255, 255, 255}, th.textDisabled);
        }

        // 6. Softkey Footer primitive
        prim::Footer(sx, syTop + screenH - footerH, pw, footerH, padX,
                     m_footer, 0.20f * xScale, 0.40f * sy, xScale, sy,
                     pal.lcdTop, pal.lcdText, pal.lcdBorder);

        // 7. Physical 1990s keypad cluster (Green Call / Center Nav / Red End + 3x3 numeric keys)
        const float kpTop = syTop + screenH + 3.2f * sy;
        prim::Rect(sx + 8.0f * xScale, kpTop, 18.0f * xScale, 3.4f * sy, {65, 165, 80, 215});
        prim::Rect(sx + pw * 0.5f - 14.0f * xScale, kpTop - 0.6f * sy, 28.0f * xScale, 4.8f * sy, {78, 78, 86, 220});
        prim::Rect(sx + pw - 26.0f * xScale, kpTop, 18.0f * xScale, 3.4f * sy, {185, 65, 65, 215});

        const float gridTop = kpTop + 6.8f * sy;
        const float btnW = 24.0f * xScale, btnH = 3.2f * sy, gapX = 12.0f * xScale;
        const float gx0 = sx + (pw - (btnW * 3.0f + gapX * 2.0f)) * 0.5f;
        for (int r = 0; r < 3; ++r) {
            float by = gridTop + r * (btnH + 2.2f * sy);
            for (int c = 0; c < 3; ++c) {
                prim::Rect(gx0 + c * (btnW + gapX), by, btnW, btnH, {52, 52, 58, 215});
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
    sa::CRGBA            m_bannerCol = {120, 225, 130, 255};
    std::string          m_footer;
    std::vector<MenuRow> m_rows;
    int                  m_selected   = 0;
    int                  m_maxVisible = 9;
};

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 4: HelpToast (Vanilla Notifications / Phone / Q&A!)
// Restyles ALL vanilla GTA SA CHud::DrawHelpText boxes using ui::prim::*
// ============================================================================
class HelpToast {
public:
    HelpToast& SetText(const std::string& gameEncodedText) { m_text = gameEncodedText; return *this; }
    HelpToast& SetAlpha(float alpha01) { m_alpha = std::clamp(alpha01, 0.0f, 1.0f); return *this; }
    HelpToast& SetWidth640(float w640) { m_w640 = std::clamp(w640, 145.0f, 260.0f); return *this; }

    void Render() const {
        if (m_text.empty() || m_alpha <= 0.01f) return;

        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th = Theme();
        const float accentW = 2.6f * xScale;
        const float padX    = 6.5f * xScale;
        const float padY    = 5.0f * sy;
        const float pw      = m_w640 * xScale;
        const float textW   = pw - accentW - padX * 2.0f;

        const float px    = 20.0f * xScale;
        const float py    = 22.0f * sy;
        const float fontX = th.fontToastX * xScale;
        const float fontY = th.fontToastY * sy;

        const float textX = px + accentW + padX;
        const float textY = py + padY;
        const int   lines = prim::MeasureWrappedLines(textX, textY, textW, m_text.c_str(), 1, fontX, fontY);
        const float lineH = 11.4f * sy;
        const float boxH  = padY * 2.0f + static_cast<float>(lines) * lineH + 2.0f * sy;

        // Semantic left accent strip matching GTA SA color codes (~r~ error, ~g~ success, ~y~ action/prompt)
        sa::CRGBA accent = th.textTitle;
        if (m_text.find("~r~") != std::string::npos)      accent = th.barLoss;
        else if (m_text.find("~g~") != std::string::npos) accent = th.barGain;
        else if (m_text.find("~y~") != std::string::npos) accent = th.accentWarn;

        prim::Panel(px, py, pw, boxH, WithAlpha(th.bgBody, m_alpha), WithAlpha(th.borderSubtle, m_alpha),
                    0.6f * xScale, 0.6f * sy);
        prim::AccentStrip(px, py, accentW, boxH, WithAlpha(accent, m_alpha));
        prim::WrappedText(textX, textY, textW, m_text.c_str(), 1, fontX, fontY, WithAlpha(th.textSelected, m_alpha));
    }

private:
    std::string m_text;
    float       m_alpha = 1.0f;
    float       m_w640  = 196.0f;
};

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 5: ColorGrid (Vanilla DisplayGridMenu Restyler!)
// ============================================================================
class ColorGrid {
public:
    ColorGrid& SetTitle(const std::string& titleGame) { m_title = titleGame; return *this; }
    ColorGrid& SetFooter(const std::string& footerGame) { m_footer = footerGame; return *this; }
    ColorGrid& SetGrid(int cols, int rows, int selectedIdx) {
        m_cols = std::clamp(cols, 1, 8);
        m_rows = std::clamp(rows, 1, 8);
        m_selected = selectedIdx;
        return *this;
    }
    ColorGrid& SetColors(const sa::CRGBA* colors, int count) {
        m_colors.assign(colors, colors + std::max(0, count));
        return *this;
    }
    ColorGrid& SetPosition(float x640, float y448) { m_x640 = x640; m_y448 = y448; return *this; }

    void Render() const {
        if (m_colors.empty()) return;
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        const auto& th   = Theme();
        const float padX = th.menuPadX * xScale;
        const float cell = 17.0f * xScale;
        const float cellH= 15.0f * sy;
        const float gapX = 3.0f * xScale;
        const float gapY = 3.0f * sy;
        const float hdrH = th.menuHeaderH * sy;
        const float gridW = m_cols * cell + (m_cols - 1) * gapX;
        const float gridH = m_rows * cellH + (m_rows - 1) * gapY;
        const float pw    = std::max(144.0f * xScale, gridW + padX * 2.0f);
        const float bodyH = hdrH + gridH + 10.0f * sy;
        const float footH = m_footer.empty() ? 0.0f : (th.menuFooterH * sy + 2.5f * sy);

        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float px = std::clamp(m_x640 * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float py = std::clamp(m_y448 * sy,     marginY, std::max(marginY, sh - (bodyH + footH) - marginY));

        prim::Panel(px, py, pw, bodyH, th.bgBody);
        char idxBuf[24];
        std::snprintf(idxBuf, sizeof(idxBuf), "#%d", std::max(0, m_selected) + 1);
        prim::Header(px, py, pw, hdrH, padX, m_title, idxBuf,
                     th.fontTitleX * xScale, th.fontTitleY * sy,
                     th.fontColX * xScale, th.fontColY * sy,
                     xScale, sy, th.bgHeader, th.divider, th.textTitle, th.textCol, true, 0.25f);

        const float gx0 = px + (pw - gridW) * 0.5f;
        const float gy0 = py + hdrH + 5.0f * sy;
        const int total = static_cast<int>(m_colors.size());
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                int idx = r * m_cols + c;
                if (idx >= total) break;
                prim::Swatch(gx0 + c * (cell + gapX), gy0 + r * (cellH + gapY), cell, cellH,
                             m_colors[idx], idx == m_selected, xScale, sy);
            }
        }

        if (!m_footer.empty()) {
            prim::Footer(px, py + bodyH + 2.5f * sy, pw, th.menuFooterH * sy, padX,
                         m_footer, th.fontFooterX * xScale, th.fontFooterY * sy,
                         xScale, sy, th.bgFooter, th.textFooter);
        }
    }

private:
    std::string            m_title;
    std::string            m_footer;
    std::vector<sa::CRGBA> m_colors;
    int                    m_cols     = 8;
    int                    m_rows     = 8;
    int                    m_selected = 0;
    float                  m_x640     = 24.0f;
    float                  m_y448     = 72.0f;
};

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 6: RadialWheel (8-Sector Monochrome GTA SA Wheel)
// ============================================================================
struct WheelSectorProps {
    bool        hasItems   = false;
    bool        isEquipped = false;
    void*       iconTex    = nullptr;
    std::string ammoGame;
    int         subIndex   = 0;
    int         subCount   = 0;
};

class RadialWheel {
public:
    void SetSector(int idx, const WheelSectorProps& props) {
        if (idx >= 0 && idx < 8) m_sectors[idx] = props;
    }
    void SetHovered(int idx) { m_hovered = std::clamp(idx, 0, 7); }
    void SetCenter(const std::string& itemNameGame, const std::string& ammoGame) {
        m_centerName = itemNameGame;
        m_centerAmmo = ammoGame;
    }

    void Render() const {
        float sw, sh, xScale, sy;
        GetScreenScales(sw, sh, xScale, sy);
        if (sw < 100.0f || sh < 100.0f) return;

        constexpr float kPi  = 3.14159265358979323846f;
        constexpr float kPi2 = 6.28318530718530717959f;

        const float cx      = sw * 0.5f;
        const float cy      = sh * 0.48f;
        const float innerR  = 40.0f * sy;
        const float outerR  = 98.0f * sy;
        const float rimR    = 101.5f * sy;
        const float gapRad  = 0.032f;
        const float stepRad = kPi2 / 8.0f;

        uintptr_t sTex, sZT, sZW, sVA, sSB, sDB, sSh, sCu;
        sa::rw::RenderStateGet(sa::rw::STATE_TEXTURERASTER,     &sTex);
        sa::rw::RenderStateGet(sa::rw::STATE_ZTESTENABLE,       &sZT);
        sa::rw::RenderStateGet(sa::rw::STATE_ZWRITEENABLE,      &sZW);
        sa::rw::RenderStateGet(sa::rw::STATE_VERTEXALPHAENABLE, &sVA);
        sa::rw::RenderStateGet(sa::rw::STATE_SRCBLEND,          &sSB);
        sa::rw::RenderStateGet(sa::rw::STATE_DESTBLEND,         &sDB);
        sa::rw::RenderStateGet(sa::rw::STATE_SHADEMODE,         &sSh);
        sa::rw::RenderStateGet(sa::rw::STATE_CULLMODE,          &sCu);

        sa::rw::RenderStateSet(sa::rw::STATE_TEXTURERASTER,     0);
        sa::rw::RenderStateSet(sa::rw::STATE_ZTESTENABLE,       0);
        sa::rw::RenderStateSet(sa::rw::STATE_ZWRITEENABLE,      0);
        sa::rw::RenderStateSet(sa::rw::STATE_VERTEXALPHAENABLE, 1);
        sa::rw::RenderStateSet(sa::rw::STATE_SRCBLEND,          5);
        sa::rw::RenderStateSet(sa::rw::STATE_DESTBLEND,         6);
        sa::rw::RenderStateSet(sa::rw::STATE_SHADEMODE,         2);
        sa::rw::RenderStateSet(sa::rw::STATE_CULLMODE,          1);

        auto drawArc = [&](float rIn, float rOut, float a0, float a1, int steps, uint32_t col) {
            const float z = sa::rw::GetNearScreenZ(), rhw = sa::rw::GetRecipNearClip();
            std::vector<sa::rw::RwIm2DVertex> v;
            v.reserve(static_cast<size_t>((steps + 1) * 2));
            for (int i = 0; i <= steps; ++i) {
                float a = a0 + (a1 - a0) * (static_cast<float>(i) / static_cast<float>(steps));
                float ca = std::cos(a), sv = std::sin(a);
                v.push_back({cx + ca * rOut, cy + sv * rOut, z, rhw, col, 0, 0});
                v.push_back({cx + ca * rIn,  cy + sv * rIn,  z, rhw, col, 0, 0});
            }
            sa::rw::Im2DRenderPrimitive(sa::rw::PRIM_TRISTRIP, v.data(), static_cast<int32_t>(v.size()));
        };

        auto drawDisc = [&](float r, int steps, uint32_t col) {
            const float z = sa::rw::GetNearScreenZ(), rhw = sa::rw::GetRecipNearClip();
            std::vector<sa::rw::RwIm2DVertex> v;
            v.reserve(static_cast<size_t>(steps + 2));
            v.push_back({cx, cy, z, rhw, col, 0, 0});
            for (int i = 0; i <= steps; ++i) {
                float a = kPi2 * static_cast<float>(i) / static_cast<float>(steps);
                v.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r, z, rhw, col, 0, 0});
            }
            sa::rw::Im2DRenderPrimitive(sa::rw::PRIM_TRIFAN, v.data(), static_cast<int32_t>(v.size()));
        };

        for (int i = 0; i < 8; ++i) {
            const float mid = -kPi * 0.5f + static_cast<float>(i) * stepRad;
            const float a0  = mid - stepRad * 0.5f + gapRad;
            const float a1  = mid + stepRad * 0.5f - gapRad;
            const auto& s   = m_sectors[i];
            const bool hov  = (i == m_hovered);

            uint32_t fill = hov
                ? sa::rw::PackColor(78, 78, 78, 215)
                : (s.isEquipped ? sa::rw::PackColor(28, 28, 28, 195)
                                : (s.hasItems ? sa::rw::PackColor(0, 0, 0, 185) : sa::rw::PackColor(0, 0, 0, 115)));
            drawArc(innerR, outerR, a0, a1, 14, fill);

            uint32_t rim = hov
                ? sa::rw::PackColor(245, 245, 245, 240)
                : (s.hasItems ? sa::rw::PackColor(145, 145, 145, 115) : sa::rw::PackColor(70, 70, 70, 50));
            drawArc(outerR, hov ? rimR : (outerR + 1.6f * sy), a0, a1, 14, rim);
        }

        drawDisc(innerR - 2.0f * sy, 36, sa::rw::PackColor(0, 0, 0, 212));
        drawArc(innerR - 3.2f * sy, innerR - 2.0f * sy, 0.0f, kPi2, 40, sa::rw::PackColor(180, 180, 180, 140));

        sa::rw::RenderStateSet(sa::rw::STATE_TEXTURERASTER,     sTex);
        sa::rw::RenderStateSet(sa::rw::STATE_ZTESTENABLE,       sZT);
        sa::rw::RenderStateSet(sa::rw::STATE_ZWRITEENABLE,      sZW);
        sa::rw::RenderStateSet(sa::rw::STATE_VERTEXALPHAENABLE, sVA);
        sa::rw::RenderStateSet(sa::rw::STATE_SRCBLEND,          sSB);
        sa::rw::RenderStateSet(sa::rw::STATE_DESTBLEND,         sDB);
        sa::rw::RenderStateSet(sa::rw::STATE_SHADEMODE,         sSh);
        sa::rw::RenderStateSet(sa::rw::STATE_CULLMODE,          sCu);

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
                sa::CRGBA tint = hov ? sa::CRGBA{255, 255, 255, 255} : sa::CRGBA{210, 210, 210, 220};
                sa::DrawSpriteTexture(s.iconTex, {ix - half, iy + half, ix + half, iy - half}, tint);
            }

            if (!s.ammoGame.empty()) {
                prim::Label(ix, iy + half - 1.5f * sy, 44.0f * xScale, s.ammoGame.c_str(),
                            1, 0.22f * xScale, 0.44f * sy, 0,
                            (s.ammoGame == "0") ? sa::CRGBA{230, 80, 80, 240} : sa::CRGBA{220, 220, 220, 240});
            }
            if (s.subCount > 1) {
                char cbuf[16];
                std::snprintf(cbuf, sizeof(cbuf), "<%d/%d>", s.subIndex + 1, s.subCount);
                prim::Label(ix, iy + half + 6.5f * sy, 44.0f * xScale, cbuf,
                            1, 0.20f * xScale, 0.40f * sy, 0,
                            hov ? sa::CRGBA{135, 230, 145, 255} : sa::CRGBA{175, 175, 175, 210});
            }
        }

        const float hubMaxW = (innerR - 7.0f * sy) * 1.85f;
        if (!m_centerName.empty()) {
            prim::Label(cx, cy - (m_centerAmmo.empty() ? 5.5f : 11.0f) * sy, hubMaxW, m_centerName.c_str(),
                        2, 0.27f * xScale, 0.54f * sy, 0, {255, 255, 255, 255});
        }
        if (!m_centerAmmo.empty()) {
            prim::Label(cx, cy + 4.5f * sy, hubMaxW, m_centerAmmo.c_str(),
                        1, 0.23f * xScale, 0.46f * sy, 0,
                        (m_centerAmmo == "0") ? sa::CRGBA{230, 80, 80, 255} : sa::CRGBA{205, 205, 205, 255});
        }
    }

private:
    WheelSectorProps m_sectors[8];
    int              m_hovered = 0;
    std::string      m_centerName;
    std::string      m_centerAmmo;
};

// ============================================================================
// TIER 2 — COMPOSITE COMPONENT 7: TextInputModal (Built 100% from ui::prim::*)
// ============================================================================
struct TextInputBoxSpec {
    std::string titleGame;
    std::string badgeGame;
    std::string text;
    std::string placeholderGame;
    int         maxLen = 8;
    std::string line1HintGame;
    std::string line2HintGame;
    bool        plateStyle = false;
};

inline void DrawTextInputBox(const TextInputBoxSpec& spec) {
    float sw, sh, xScale, sy;
    GetScreenScales(sw, sh, xScale, sy);
    if (sw < 100.0f || sh < 100.0f) return;

    const auto& th    = Theme();
    const float padX  = th.menuPadX * xScale;
    const float pw    = 176.0f * xScale;
    const float hdrH  = th.menuHeaderH * sy;
    const float inpH  = 26.0f * sy;
    const float subH  = spec.line2HintGame.empty() ? 0.0f : (11.0f * sy);
    const float bodyH = hdrH + inpH + subH + 8.0f * sy;
    const float footH = spec.line1HintGame.empty() ? 0.0f : (th.menuFooterH * sy);

    const float px = (sw - pw) * 0.5f;
    const float py = std::clamp(sh * 0.68f, th.screenMarginY * sy, sh - (bodyH + footH + 4.0f * sy) - th.screenMarginY * sy);

    prim::Panel(px, py, pw, bodyH, th.bgBody);
    prim::Header(px, py, pw, hdrH, padX, spec.titleGame, spec.badgeGame,
                 0.30f * xScale, 0.58f * sy, th.fontColX * xScale, th.fontColY * sy,
                 xScale, sy, th.bgHeader, th.divider, th.textTitle, {125, 235, 135, 255}, true, 0.25f);

    const float bx = px + padX, bw = pw - padX * 2.0f, bh = 19.5f * sy, by = py + hdrH + 5.0f * sy;
    prim::TextField(bx, by, bw, bh, spec.text, spec.placeholderGame, spec.maxLen,
                    xScale, sy, th.divider, th.bgInput, th.textSelected, {125, 125, 130, 255}, th.textCol);

    if (!spec.line2HintGame.empty()) {
        prim::Label(bx, by + bh + 2.5f * sy, bw, spec.line2HintGame.c_str(),
                    1, 0.22f * xScale, 0.44f * sy, 1, th.textCol);
    }

    if (!spec.line1HintGame.empty()) {
        prim::Footer(px, py + bodyH + 2.5f * sy, pw, footH, padX,
                     spec.line1HintGame, th.fontFooterX * xScale, th.fontFooterY * sy,
                     xScale, sy, th.bgFooter, th.textFooter);
    }
}

// ============================================================================
// Compatibility Wrappers (Delegate straight into Tier 2 Composite Components!)
// ============================================================================
struct MenuDrawSpec {
    std::string          titleGame;
    std::string          colLeftGame;
    std::string          colRightGame;
    std::string          footerGame;
    std::vector<MenuRow> rows;
    int   selectedIndex   = 0;
    int   maxVisibleRows  = 11;
    float x640            = 20.0f;
    float y448            = 68.0f;
    float w640            = 176.0f;
    sa::CRGBA accentColor = {215, 215, 215, 62};
};

inline void DrawVanillaMenu(const MenuDrawSpec& spec) {
    ListMenu m;
    m.SetTitle(spec.titleGame)
     .SetColumns(spec.colLeftGame, spec.colRightGame)
     .SetFooter(spec.footerGame)
     .SetPosition(spec.x640, spec.y448, spec.w640)
     .SetMaxVisible(spec.maxVisibleRows)
     .SetAccent(spec.accentColor);
    for (const auto& r : spec.rows) {
        m.AddItem(r.leftGame, r.rightGame, r.rightColor, r.enabled, r.userId);
    }
    m.SetSelected(spec.selectedIndex);
    m.Render();
}

struct StatsPanelSpec {
    std::string             titleGame;
    std::string             subtitleGame;
    std::string             footerNoteGame;
    std::vector<StatBarRow> rows;
    bool  anchorBottomRight = false;
    float w640              = 148.0f;
    float rightMargin640    = 16.0f;
    float yTop448           = 104.0f;
    float bottomMargin448   = 18.0f;
};

inline void DrawStatsPanel(const StatsPanelSpec& spec) {
    StatsCard c;
    c.SetTitle(spec.titleGame, spec.subtitleGame)
     .SetNote(spec.footerNoteGame)
     .SetAnchor(spec.anchorBottomRight, spec.w640, spec.rightMargin640,
                spec.anchorBottomRight ? spec.bottomMargin448 : spec.yTop448);
    for (const auto& r : spec.rows) {
        c.AddBar(r.labelGame, r.curVal, r.prevVal, r.maxScale, r.unitGame, r.pristineVal);
    }
    c.Render();
}

} // namespace ui
