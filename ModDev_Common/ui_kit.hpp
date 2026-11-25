#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include "sa_sdk.hpp"
#include "text_rus.hpp"

// ============================================================================
// Shared GTA SA Vanilla Dumb-Component UI Kit (Web-Style Architecture)
//
// Core Design:
//   1. Single root ui::Theme controls ALL sizing, fonts, margins & colors across all mods.
//   2. Automatic text fitting via native CFont::GetStringWidth (0x0071A0E0):
//      EVERY label, title, right-badge, footer & wheel label automatically shrinks
//      to fit inside its container width (and screen bounds) — ZERO overflow ever!
//   3. Dumb, stateless/signal-emitting UI components:
//      - ui::ListMenu     (populates via .AddItem(), emits MenuEvent signals)
//      - ui::StatsCard    (populates via .AddBar(), renders anchored card)
//      - ui::TextPrompt   (populates via .SetText(), emits PromptEvent signals)
//      - ui::RadialWheel  (populates via .SetSector(), emits WheelEvent signals)
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

// ----------------------------------------------------------------------------
// Root Theme & WidescreenFix-Matched Scale Engine
// ----------------------------------------------------------------------------
struct ThemeTokens {
    // Global root scale multiplier (1.0 = exact vanilla GTA SA + WidescreenFix scale!)
    float uiScale        = 1.0f;
    float screenMarginX  = 14.0f; // 640-space safety margin from left/right screen edges
    float screenMarginY  = 14.0f; // 448-space safety margin from top/bottom screen edges

    // ListMenu geometry (in 640x448 virtual space)
    float menuDefaultW   = 176.0f;
    float menuHeaderH    = 17.5f;
    float menuColBarH    = 12.0f;
    float menuRowH       = 13.0f;
    float menuFooterH    = 14.0f;
    float menuPadX       = 6.0f;
    float menuPadBot     = 4.5f;

    // StatsCard geometry (in 640x448 virtual space)
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
    float fontMinRatio   = 0.55f; // Never squish text below 55% width

    // Authentic Vanilla GTA San Andreas Monochrome Palette (Black / Grey / White + HUD Green/Red)
    sa::CRGBA bgBody       = {0, 0, 0, 195};
    sa::CRGBA bgHeader     = {0, 0, 0, 232};
    sa::CRGBA bgFooter     = {0, 0, 0, 185};
    sa::CRGBA divider      = {175, 175, 175, 95};
    sa::CRGBA selectFill   = {215, 215, 215, 62};
    sa::CRGBA textTitle    = {232, 232, 232, 255};
    sa::CRGBA textCol      = {175, 175, 175, 255};
    sa::CRGBA textNormal   = {195, 195, 195, 255};
    sa::CRGBA textSelected = {255, 255, 255, 255};
    sa::CRGBA textDisabled = {105, 105, 105, 255};
    sa::CRGBA textFooter   = {200, 200, 200, 255};
    sa::CRGBA barTrack     = {62, 62, 62, 190};
    sa::CRGBA barFill      = {218, 218, 218, 240};
    sa::CRGBA barGain      = {82, 222, 105, 255};
    sa::CRGBA barLoss      = {228, 78, 78, 255};
};

inline ThemeTokens& Theme() {
    static ThemeTokens t;
    return t;
}

inline void GetScreenScales(float& sw, float& sh, float& xScale, float& sy) {
    sw = static_cast<float>(sa::GetScreenWidth());
    sh = static_cast<float>(sa::GetScreenHeight());
    const float mult = Theme().uiScale;
    sy = (sh / 448.0f) * mult;
    xScale = ((sw / sh > 1.4f) ? (sh * (4.0f / 3.0f) / 640.0f) : (sw / 640.0f)) * mult;
}

inline void PrepareFontBase() {
    sa::font::SetBackground(false, false);
    sa::font::SetProportional(true);
    sa::font::SetJustify(false);
    sa::font::SetDropShadowPosition(1);
    sa::font::SetEdge(0);
    sa::font::SetDropColor({0, 0, 0, 255});
    sa::font::SetRightJustifyWrap(0.0f);
}

// ============================================================================
// Auto-Fitting Text Renderer (Uses native CFont::GetStringWidth @ 0x0071A0E0!)
// Automatically shrinks font scale if text exceeds maxWidthPx -> ZERO overflow!
// align: 0 = Center, 1 = Left, 2 = Right
// ============================================================================
inline void PrintFittedText(float x, float y, float maxWidthPx, const char* gameText,
                            uint8_t fontStyle, float scaleX, float scaleY,
                            uint8_t align, sa::CRGBA color)
{
    if (!gameText || !gameText[0]) return;
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
            // Gently trim Y height too so narrowed text stays naturally proportioned
            fitY = scaleY * (0.65f + 0.35f * ratio);
            sa::font::SetScale(fitX, fitY);
        }
    }

    sa::font::SetColor(color);
    sa::font::SetOrientation(align);
    if (align == 0) {
        sa::font::SetCentreSize(std::max(10.0f, maxWidthPx + 4.0f));
    } else if (align == 1) {
        sa::font::SetWrapx(x + std::max(10.0f, maxWidthPx + 8.0f));
    } else {
        sa::font::SetRightJustifyWrap(std::max(0.0f, x - std::max(10.0f, maxWidthPx + 8.0f)));
    }
    sa::font::PrintString(x, y, gameText);
}

// ============================================================================
// DUMB COMPONENT 1: ListMenu (Web-style Props + Event Signals + Auto-Fit Render)
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
            scrollTop = m_selected - (visible / 2);
            scrollTop = std::clamp(scrollTop, 0, totalRows - visible);
        }

        const float headerH = th.menuHeaderH * sy;
        const float colBarH = th.menuColBarH * sy;
        const float rowH    = th.menuRowH    * sy;
        const float padX    = th.menuPadX    * xScale;
        const float padBot  = th.menuPadBot  * sy;
        const float bodyH   = headerH + colBarH + (visible * rowH) + padBot;
        const float footH   = m_footer.empty() ? 0.0f : (th.menuFooterH * sy + 2.5f * sy);

        // Clamp strictly inside screen bounds so menu & footer NEVER clip off-screen!
        const float marginX = th.screenMarginX * xScale;
        const float marginY = th.screenMarginY * sy;
        const float px = std::clamp(m_x640 * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float py = std::clamp(m_y448 * sy,     marginY, std::max(marginY, sh - (bodyH + footH) - marginY));

        sa::font::DrawRect({px, py + bodyH, px + pw, py}, th.bgBody);
        sa::font::DrawRect({px, py + headerH, px + pw, py}, th.bgHeader);

        const float countReserve = (totalRows > visible) ? (28.0f * xScale) : 0.0f;
        PrintFittedText(px + padX, py + 2.5f * sy, pw - padX * 2.0f - countReserve, m_title.c_str(),
                        2, th.fontTitleX * xScale, th.fontTitleY * sy, 1, th.textTitle);

        if (totalRows > visible) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d/%d", m_selected + 1, totalRows);
            PrintFittedText(px + pw - padX, py + 4.0f * sy, 26.0f * xScale, buf,
                            1, th.fontColX * xScale, th.fontColY * sy, 2, th.textCol);
        }

        const float colY = py + headerH + 1.5f * sy;
        const float colRightW = m_colRight.empty() ? 0.0f : (pw * 0.36f);
        PrintFittedText(px + padX, colY, pw - padX * 2.0f - colRightW, m_colLeft.c_str(),
                        1, th.fontColX * xScale, th.fontColY * sy, 1, th.textCol);
        if (!m_colRight.empty()) {
            PrintFittedText(px + pw - padX, colY, colRightW, m_colRight.c_str(),
                            1, th.fontColX * xScale, th.fontColY * sy, 2, th.textCol);
        }

        const float listTop = py + headerH + colBarH;
        sa::font::DrawRect({px + 3.0f * xScale, listTop, px + pw - 3.0f * xScale, listTop - 0.8f * sy}, th.divider);

        const sa::CRGBA selFill = m_useCustomAccent ? m_accent : th.selectFill;
        for (int i = 0; i < visible; ++i) {
            int idx = scrollTop + i;
            const auto& r = m_rows[idx];
            const float ry = listTop + i * rowH;
            const bool isSel = (idx == m_selected);

            if (isSel) {
                sa::font::DrawRect({px + 2.0f * xScale, ry + rowH - 0.4f * sy, px + pw - 2.0f * xScale, ry + 0.4f * sy}, selFill);
            }

            const float rightReserve = r.rightGame.empty() ? 0.0f : (pw * 0.38f);
            const float leftMaxW     = pw - padX * 2.0f - rightReserve - (r.rightGame.empty() ? 0.0f : 4.0f * xScale);

            sa::CRGBA leftCol = isSel ? th.textSelected : (r.enabled ? th.textNormal : th.textDisabled);
            PrintFittedText(px + padX, ry + 1.5f * sy, leftMaxW, r.leftGame.c_str(),
                            1, th.fontRowX * xScale, th.fontRowY * sy, 1, leftCol);

            if (!r.rightGame.empty()) {
                sa::CRGBA rightCol = isSel ? th.textSelected : (r.enabled ? r.rightColor : th.textDisabled);
                PrintFittedText(px + pw - padX, ry + 1.5f * sy, rightReserve, r.rightGame.c_str(),
                                1, th.fontRowX * xScale, th.fontRowY * sy, 2, rightCol);
            }
        }

        if (!m_footer.empty()) {
            const float fy = py + bodyH + 2.5f * sy;
            const float fh = th.menuFooterH * sy;
            sa::font::DrawRect({px, fy + fh, px + pw, fy}, th.bgFooter);
            PrintFittedText(px + padX, fy + 2.2f * sy, pw - padX * 2.0f, m_footer.c_str(),
                            1, th.fontFooterX * xScale, th.fontFooterY * sy, 1, th.textFooter);
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
    int                  m_selected      = 0;
    int                  m_maxVisible    = 11;
    float                m_x640          = 20.0f;
    float                m_y448          = 68.0f;
    float                m_w640          = 0.0f;
    sa::CRGBA            m_accent        = {215, 215, 215, 62};
    bool                 m_useCustomAccent = false;
};

// ============================================================================
// DUMB COMPONENT 2: StatsCard (Live Metric Bars + Auto-Fit Text)
// ============================================================================
struct StatBarRow {
    std::string labelGame;
    int         curVal   = 0;
    int         prevVal  = 0;
    int         maxScale = 100;
    std::string unitGame;
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
    StatsCard& AddBar(const std::string& labelGame, int curVal, int prevVal, int maxScale, const std::string& unitGame = "") {
        m_rows.push_back({labelGame, curVal, prevVal, std::max(1, maxScale), unitGame});
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
        const float px = std::clamp(sw - pw - m_rightMargin * xScale, marginX, std::max(marginX, sw - pw - marginX));
        const float pyRaw = m_bottomRight ? (sh - bodyH - m_yOffset * sy) : (m_yOffset * sy);
        const float py = std::clamp(pyRaw, marginY, std::max(marginY, sh - bodyH - marginY));

        sa::font::DrawRect({px, py + bodyH, px + pw, py}, th.bgBody);
        sa::font::DrawRect({px, py + headerH, px + pw, py}, th.bgHeader);
        sa::font::DrawRect({px + 3.0f * xScale, py + headerH, px + pw - 3.0f * xScale, py + headerH - 0.7f * sy}, th.divider);

        const float subReserve = m_subtitle.empty() ? 0.0f : (pw * 0.54f);
        PrintFittedText(px + padX, py + 2.2f * sy, pw - padX * 2.0f - subReserve, m_title.c_str(),
                        2, 0.29f * xScale, 0.56f * sy, 1, th.textTitle);
        if (!m_subtitle.empty()) {
            PrintFittedText(px + pw - padX, py + 3.0f * sy, subReserve, m_subtitle.c_str(),
                            1, th.fontStatX * xScale, th.fontStatY * sy, 2, th.textNormal);
        }

        const float barX = px + padX;
        const float barW = pw - padX * 2.0f;
        const float barH = th.cardBarH * sy;

        for (size_t i = 0; i < m_rows.size(); ++i) {
            const auto& it = m_rows[i];
            const float ry = py + headerH + 2.0f * sy + static_cast<float>(i) * rowH;
            const int delta = it.prevVal - it.curVal;

            char valBuf[64];
            if (delta > 0)      std::snprintf(valBuf, sizeof(valBuf), "%d%s ~g~(+%d)", it.prevVal, it.unitGame.c_str(), delta);
            else if (delta < 0) std::snprintf(valBuf, sizeof(valBuf), "%d%s ~r~(%d)", it.prevVal, it.unitGame.c_str(), delta);
            else                std::snprintf(valBuf, sizeof(valBuf), "%d%s", it.curVal, it.unitGame.c_str());

            PrintFittedText(barX, ry, barW * 0.52f, it.labelGame.c_str(),
                            1, th.fontStatX * xScale, th.fontStatY * sy, 1, th.textNormal);
            PrintFittedText(px + pw - padX, ry, barW * 0.46f, valBuf,
                            1, th.fontStatX * xScale, th.fontStatY * sy, 2, th.textSelected);

            const float by = ry + 9.5f * sy;
            sa::font::DrawRect({barX, by + barH, barX + barW, by}, th.barTrack);

            float curFrac  = std::clamp(static_cast<float>(it.curVal)  / static_cast<float>(it.maxScale), 0.0f, 1.0f);
            float prevFrac = std::clamp(static_cast<float>(it.prevVal) / static_cast<float>(it.maxScale), 0.0f, 1.0f);

            if (delta > 0) {
                sa::font::DrawRect({barX, by + barH, barX + barW * curFrac, by}, th.barFill);
                sa::font::DrawRect({barX + barW * curFrac, by + barH, barX + barW * prevFrac, by}, th.barGain);
            } else if (delta < 0) {
                sa::font::DrawRect({barX, by + barH, barX + barW * prevFrac, by}, th.barFill);
                sa::font::DrawRect({barX + barW * prevFrac, by + barH, barX + barW * curFrac, by}, th.barLoss);
            } else {
                sa::font::DrawRect({barX, by + barH, barX + barW * curFrac, by}, th.barFill);
            }
        }

        if (!m_note.empty()) {
            const float ny = py + headerH + static_cast<float>(m_rows.size()) * rowH + 2.0f * sy;
            PrintFittedText(barX, ny, barW, m_note.c_str(),
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
// DUMB COMPONENT 3: RadialWheel (8-Sector Vanilla GTA SA Monochrome Wheel!)
// Pure presentation component: Addon supplies icon textures & badges, Render() draws!
// ============================================================================
struct WheelSectorProps {
    bool        hasItems   = false;
    bool        isEquipped = false;
    void*       iconTex    = nullptr; // Native GTA SA RwTexture* (drawn via CSprite2d::Draw!)
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

        // Save RenderWare 2D states
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

        // Draw 8 authentic vanilla GTA SA monochrome black/grey/white sectors
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

        // Center disc (pure black + thin silver border)
        drawDisc(innerR - 2.0f * sy, 36, sa::rw::PackColor(0, 0, 0, 212));
        drawArc(innerR - 3.2f * sy, innerR - 2.0f * sy, 0.0f, kPi2, 40, sa::rw::PackColor(180, 180, 180, 140));

        // Restore RW states BEFORE drawing native CSprite2d icons & CFont text!
        sa::rw::RenderStateSet(sa::rw::STATE_TEXTURERASTER,     sTex);
        sa::rw::RenderStateSet(sa::rw::STATE_ZTESTENABLE,       sZT);
        sa::rw::RenderStateSet(sa::rw::STATE_ZWRITEENABLE,      sZW);
        sa::rw::RenderStateSet(sa::rw::STATE_VERTEXALPHAENABLE, sVA);
        sa::rw::RenderStateSet(sa::rw::STATE_SRCBLEND,          sSB);
        sa::rw::RenderStateSet(sa::rw::STATE_DESTBLEND,         sDB);
        sa::rw::RenderStateSet(sa::rw::STATE_SHADEMODE,         sSh);
        sa::rw::RenderStateSet(sa::rw::STATE_CULLMODE,          sCu);

        // Draw native GTA SA HUD weapon icons via CSprite2d::Draw (0x00728350)!
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
                PrintFittedText(ix, iy + half - 1.5f * sy, 44.0f * xScale, s.ammoGame.c_str(),
                                1, 0.22f * xScale, 0.44f * sy, 0,
                                (s.ammoGame == "0") ? sa::CRGBA{230, 80, 80, 240} : sa::CRGBA{220, 220, 220, 240});
            }
            if (s.subCount > 1) {
                char cbuf[16];
                std::snprintf(cbuf, sizeof(cbuf), "<%d/%d>", s.subIndex + 1, s.subCount);
                PrintFittedText(ix, iy + half + 6.5f * sy, 44.0f * xScale, cbuf,
                                1, 0.20f * xScale, 0.40f * sy, 0,
                                hov ? sa::CRGBA{135, 230, 145, 255} : sa::CRGBA{175, 175, 175, 210});
            }
        }

        // Minimalist auto-fitted center hub (GUARANTEED to stay cleanly inside the inner circle!)
        const float hubMaxW = (innerR - 7.0f * sy) * 1.85f;
        if (!m_centerName.empty()) {
            PrintFittedText(cx, cy - (m_centerAmmo.empty() ? 5.5f : 11.0f) * sy, hubMaxW, m_centerName.c_str(),
                            2, 0.27f * xScale, 0.54f * sy, 0, {255, 255, 255, 255});
        }
        if (!m_centerAmmo.empty()) {
            PrintFittedText(cx, cy + 4.5f * sy, hubMaxW, m_centerAmmo.c_str(),
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
// Compatibility Wrappers (Delegate straight into the new auto-fitting Dumb Components!)
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
        c.AddBar(r.labelGame, r.curVal, r.prevVal, r.maxScale, r.unitGame);
    }
    c.Render();
}

struct TextInputBoxSpec {
    std::string titleGame;
    std::string text;
    int         maxLen = 8;
    std::string line1HintGame;
    std::string line2HintGame;
    bool        plateStyle = true;
};

inline void DrawTextInputBox(const TextInputBoxSpec& spec) {
    float sw, sh, xScale, sy;
    GetScreenScales(sw, sh, xScale, sy);
    if (sw < 100.0f || sh < 100.0f) return;

    const auto& th = Theme();
    const float pw = 196.0f * xScale;
    const float ph = 78.0f * sy;
    const float px = (sw - pw) * 0.5f;
    const float py = sh * 0.60f;

    sa::font::DrawRect({px, py + ph, px + pw, py}, {0, 0, 0, 215});
    sa::font::DrawRect({px, py + 16.5f * sy, px + pw, py}, {0, 0, 0, 238});

    PrintFittedText(px + pw * 0.5f, py + 2.5f * sy, pw - 10.0f * xScale, spec.titleGame.c_str(),
                    2, 0.30f * xScale, 0.58f * sy, 0, th.textTitle);

    const float boxW = 130.0f * xScale;
    const float boxH = 23.0f * sy;
    const float bx   = px + (pw - boxW) * 0.5f;
    const float by   = py + 22.0f * sy;
    if (spec.plateStyle) {
        sa::font::DrawRect({bx, by + boxH, bx + boxW, by}, {235, 232, 218, 245});
        sa::font::DrawRect({bx + 1.5f * xScale, by + boxH - 1.5f * sy, bx + boxW - 1.5f * xScale, by + 1.5f * sy}, {25, 45, 90, 230});
    } else {
        sa::font::DrawRect({bx, by + boxH, bx + boxW, by}, {170, 170, 170, 220});
        sa::font::DrawRect({bx + 1.5f * xScale, by + boxH - 1.5f * sy, bx + boxW - 1.5f * xScale, by + 1.5f * sy}, {15, 15, 15, 245});
    }

    bool blink = ((GetTickCount() / 400) % 2) == 0;
    std::string shown = spec.text;
    if (static_cast<int>(shown.size()) < spec.maxLen && blink) shown.push_back('_');
    if (shown.empty()) shown = "_";

    PrintFittedText(px + pw * 0.5f, by + 3.0f * sy, boxW - 8.0f * xScale, shown.c_str(),
                    2, 0.48f * xScale, 0.88f * sy, 0, {245, 245, 250, 255});

    if (!spec.line1HintGame.empty()) {
        PrintFittedText(px + pw * 0.5f, py + 52.0f * sy, pw - 10.0f * xScale, spec.line1HintGame.c_str(),
                        1, th.fontFooterX * xScale, th.fontFooterY * sy, 0, th.textFooter);
    }
    if (!spec.line2HintGame.empty()) {
        PrintFittedText(px + pw * 0.5f, py + 63.5f * sy, pw - 10.0f * xScale, spec.line2HintGame.c_str(),
                        1, th.fontFooterX * xScale, th.fontFooterY * sy, 0, th.textCol);
    }
}

} // namespace ui
