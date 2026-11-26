#include "vanilla_restyle.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace uikit_restyle {

namespace {

using FnDisplayMenu = void(__cdecl*)(uint8_t, uint8_t);
using FnPrintString = void(__cdecl*)(float, float, const char*);

FnDisplayMenu g_origStd1  = nullptr;
FnDisplayMenu g_origStd2  = nullptr;
FnDisplayMenu g_origStd3  = nullptr;
FnDisplayMenu g_origGrid1 = nullptr;
FnDisplayMenu g_origGrid2 = nullptr;
FnDisplayMenu g_origGrid3 = nullptr;
FnPrintString g_origHelpPrintString = nullptr;

std::string ResolveGxtKey(const char* rawKey, int maxBytes = 10) {
    if (!rawKey || !rawKey[0]) return "";
    char key[16] = {};
    int len = 0;
    while (len < maxBytes && rawKey[len] != '\0') {
        key[len] = rawKey[len];
        ++len;
    }
    key[len] = '\0';
    if (key[0] == '\0' || std::strcmp(key, "DUMMY") == 0) return "";

    const char* txt = sa::TextGet(key);
    if (!txt || !txt[0]) return "";
    return std::string(txt);
}

std::string FormatMenuCell(const sa::tMenuPanel* p, int col, int row) {
    if (!p || col < 0 || col >= 4 || row < 0 || row >= 12) return "";
    char key[12] = {};
    std::memcpy(key, p->m_aaacRowItems[col][row], 10);
    key[10] = '\0';
    if (key[0] == '\0' || std::strcmp(key, "DUMMY") == 0) return "";

    const char* raw = sa::TextGet(key);
    if (!raw || !raw[0]) return "";

    // CRITICAL: GTA SA's CMessages::InsertNumberInString (0x0069DE90) unconditionally
    // zero-fills 0x190 (400) bytes into dst! Must be >= 400 bytes to prevent stack overflow!
    char buf[512] = {};
    int32_t n1 = p->m_aadwNumberInRowTitle[col][row];
    int32_t n2 = p->m_aadw2ndNumberInRowTitle[col][row];
    sa::InsertNumberInString(raw, n1, n2, -1, -1, -1, -1, buf);
    buf[511] = '\0';
    sa::InsertPlayerControlKeysInString(buf);
    buf[511] = '\0';
    return std::string(buf);
}

void RenderStandardPanelWithUIKit(uint8_t panelId, uint8_t isActive) {
    sa::tMenuPanel* p = sa::GetMenuPanel(panelId);
    if (!p || p->m_nNumRows == 0 || p->m_nNumColumns == 0) return;

    const int numRows = std::clamp<int>(p->m_nNumRows, 0, 12);
    const int numCols = std::clamp<int>(p->m_nNumColumns, 1, 4);

    std::string title = ResolveGxtKey(p->m_acTitle, 10);
    if (title.empty()) title = ui::Loc("МЕНЮ", "MENU");

    std::string colLeft = ResolveGxtKey(p->m_aacColumnHeaders[0], 10);
    std::string colRight;
    for (int c = 1; c < numCols; ++c) {
        std::string hdr = ResolveGxtKey(p->m_aacColumnHeaders[c], 10);
        if (!hdr.empty()) {
            if (!colRight.empty()) colRight += "  ";
            colRight += hdr;
        }
    }

    float x640 = 24.0f;
    float y448 = 72.0f;
    float w640 = (numCols >= 2) ? 196.0f : 178.0f;
    if (panelId == 1) {
        x640 = (p->m_fPosX > 220.0f) ? std::clamp(p->m_fPosX * 0.88f, 224.0f, 440.0f) : 228.0f;
        y448 = std::clamp(p->m_fPosY, 68.0f, 260.0f);
        w640 = 166.0f;
    } else if (p->m_fPosX > 260.0f) {
        x640 = std::clamp(p->m_fPosX * 0.85f, 24.0f, 430.0f);
    }

    ui::ListMenu menu;
    menu.SetTitle(title)
        .SetColumns(colLeft, colRight)
        .SetPosition(x640, y448, w640)
        .SetMaxVisible(12);

    if (p->m_bInteractive) {
        menu.SetFooter(isActive
            ? ui::Loc("Enter/Space - Выбрать | F - Выход", "Enter/Space - Select | F - Exit")
            : "");
        if (!isActive) menu.SetAccent({140, 140, 140, 35});
    } else {
        menu.SetAccent({0, 0, 0, 0});
    }

    for (int r = 0; r < numRows; ++r) {
        std::string leftTxt = FormatMenuCell(p, 0, r);
        std::string rightTxt;
        for (int c = 1; c < numCols; ++c) {
            std::string cell = FormatMenuCell(p, c, r);
            if (!cell.empty()) {
                if (!rightTxt.empty()) rightTxt += "  ";
                rightTxt += cell;
            }
        }

        bool enabled     = (p->m_anRowEnabled[r] != 0);
        bool highlighted = (p->m_anRowHighlighted[r] != 0);
        sa::CRGBA rightCol = highlighted
            ? sa::CRGBA{115, 225, 130, 255}
            : (enabled ? sa::CRGBA{218, 218, 218, 255} : sa::CRGBA{120, 120, 120, 255});

        if (leftTxt.empty() && rightTxt.empty()) leftTxt = " ";
        menu.AddItem(leftTxt, rightTxt, rightCol, enabled || !p->m_bInteractive, r);
    }

    int sel = p->m_bInteractive ? std::clamp<int>(p->m_nSelectedRow, 0, std::max(0, numRows - 1)) : 0;
    menu.SetSelected(sel);
    menu.Render();
}

void RenderGridPanelWithUIKit(uint8_t panelId, uint8_t isActive) {
    sa::tMenuPanel* p = sa::GetMenuPanel(panelId);
    if (!p || p->m_nNumRows == 0 || p->m_nNumColumns == 0) return;

    int cols  = std::clamp<int>(p->m_nNumColumns, 1, 8);
    int rows  = std::clamp<int>(p->m_nNumRows,    1, 8);
    int total = std::min(64, cols * rows);

    std::vector<sa::CRGBA> colors;
    colors.reserve(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        colors.push_back(sa::GetVehicleColorRGBA(p->m_anUsedCarColors[i]));
    }

    std::string title = ResolveGxtKey(p->m_acTitle, 10);
    if (title.empty()) title = ui::Loc("ПАЛИТРА ЦВЕТОВ", "COLOR PALETTE");

    ui::ColorGrid grid;
    grid.SetTitle(title)
        .SetGrid(cols, rows, std::clamp<int>(p->m_nSelectedRow, 0, std::max(0, total - 1)))
        .SetColors(colors.data(), total)
        .SetPosition(24.0f, 72.0f)
        .SetFooter(isActive
            ? ui::Loc("Стрелки - Цвет | Enter - Выбр. | F - Назад", "Arrows - Pick | Enter - Apply | F - Back")
            : "");
    grid.Render();
}

void HandleDisplayStandardMenu(uint8_t panelId, uint8_t isActive, FnDisplayMenu origFn) {
    auto* bus = modbus::GetBus();
    if (bus && bus->restyleVanillaMenus && sa::GetGameState() == 9 && !sa::IsPauseMenuActive()) {
        sa::tMenuPanel* p = sa::GetMenuPanel(panelId);
        if (p && p->m_nType == 0 && p->m_nNumRows > 0) {
            char titleBuf[12] = {};
            std::memcpy(titleBuf, p->m_acTitle, 10);
            titleBuf[10] = '\0';
            if (modbus::IsPanelClaimed(titleBuf)) return;
            RenderStandardPanelWithUIKit(panelId, isActive);
            return;
        }
    }
    if (origFn) origFn(panelId, isActive);
}

void HandleDisplayGridMenu(uint8_t panelId, uint8_t isActive, FnDisplayMenu origFn) {
    auto* bus = modbus::GetBus();
    if (bus && bus->restyleVanillaMenus && sa::GetGameState() == 9 && !sa::IsPauseMenuActive()) {
        sa::tMenuPanel* p = sa::GetMenuPanel(panelId);
        if (p && p->m_nType == 1 && p->m_nNumRows > 0 && p->m_nNumColumns > 0) {
            char titleBuf[12] = {};
            std::memcpy(titleBuf, p->m_acTitle, 10);
            titleBuf[10] = '\0';
            if (modbus::IsPanelClaimed(titleBuf)) return;
            RenderGridPanelWithUIKit(panelId, isActive);
            return;
        }
    }
    if (origFn) origFn(panelId, isActive);
}

// Intercepts CFont::PrintString @ 0x0058C00F inside CHud::DrawHelpText (0x0058B6E0)!
// Restyles ALL vanilla top-left notifications, story phone-call prompts, Yes/No questions,
// shop/mission tips, and our own ui::ShowHelp() messages using ui::HelpToast (built on ui::prim::*)!
// Preserves 100% of GTA SA's native fade timers, sounds, state machine & RPG stat bars!
void __cdecl HookedHelpPrintString(float x, float y, const char* text) {
    auto* bus = modbus::GetBus();
    const bool restyleHelp = (bus && bus->restyleHelpBoxes);
    const uint16_t statId  = *reinterpret_cast<const uint16_t*>(0x00BAA470); // CHud::m_nHelpMessageStatId

    if (restyleHelp && statId == 0 && text && text[0] &&
        sa::GetGameState() == 9 && !sa::IsPauseMenuActive())
    {
        const int32_t state = *reinterpret_cast<const int32_t*>(0x00BAA474); // 1=SHOWING, 2=FADE_IN, 3=FADE_OUT
        const int32_t fade  = *reinterpret_cast<const int32_t*>(0x00BAA478); // 0..200
        const float alpha   = (state == 1) ? 1.0f : std::clamp(static_cast<float>(fade) / 200.0f, 0.0f, 1.0f);
        const float rawW    = *reinterpret_cast<const float*>(0x008D0934);   // CHud::m_fHelpMessageBoxWidth
        const float boxW    = (rawW >= 120.0f && rawW <= 320.0f) ? std::clamp(rawW, 170.0f, 235.0f) : 196.0f;

        ui::HelpToast toast;
        toast.SetText(text).SetAlpha(alpha).SetWidth640(boxW);
        toast.Render();
        return;
    }

    if (g_origHelpPrintString) g_origHelpPrintString(x, y, text);
}

void __cdecl HookedStd1(uint8_t id, uint8_t act)  { HandleDisplayStandardMenu(id, act, g_origStd1); }
void __cdecl HookedStd2(uint8_t id, uint8_t act)  { HandleDisplayStandardMenu(id, act, g_origStd2); }
void __cdecl HookedStd3(uint8_t id, uint8_t act)  { HandleDisplayStandardMenu(id, act, g_origStd3); }
void __cdecl HookedGrid1(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid1); }
void __cdecl HookedGrid2(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid2); }
void __cdecl HookedGrid3(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid3); }

} // namespace

void InstallHooks() {
    // Verified in gta_sa.exe 1.0 US inside CMenuSystem::Process (0x00582630):
    //   Standard menus (0x00580E00): 0x00582687, 0x005826D9, 0x00582757
    //   Grid menus     (0x005816E0): 0x00582679, 0x005826CF, 0x0058274D
    g_origStd1  = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x00582687, reinterpret_cast<uintptr_t>(&HookedStd1)));
    g_origStd2  = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x005826D9, reinterpret_cast<uintptr_t>(&HookedStd2)));
    g_origStd3  = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x00582757, reinterpret_cast<uintptr_t>(&HookedStd3)));
    g_origGrid1 = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x00582679, reinterpret_cast<uintptr_t>(&HookedGrid1)));
    g_origGrid2 = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x005826CF, reinterpret_cast<uintptr_t>(&HookedGrid2)));
    g_origGrid3 = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x0058274D, reinterpret_cast<uintptr_t>(&HookedGrid3)));

    // Verified in gta_sa.exe 1.0 US inside CHud::DrawHelpText (0x0058B6E0):
    //   0x0058C00F: CALL 0x0071A700 (CFont::PrintString)
    g_origHelpPrintString = reinterpret_cast<FnPrintString>(
        mem::HookCall(0x0058C00F, reinterpret_cast<uintptr_t>(&HookedHelpPrintString)));

    modlog::Info("[UIKit] Installed Vanilla Restyler hooks: StandardMenu + GridMenu + CHud::DrawHelpText (0x58C00F).");
}

} // namespace uikit_restyle
