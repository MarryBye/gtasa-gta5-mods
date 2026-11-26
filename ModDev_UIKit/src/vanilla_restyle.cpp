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

using FnVoid        = void(__cdecl*)();
using FnDisplayMenu = void(__cdecl*)(uint8_t, uint8_t);
using FnInputMenu   = void(__cdecl*)(uint8_t);
using FnPrintString = void(__cdecl*)(float, float, const char*);
using FnInsertKeys  = void(__cdecl*)(char*);

FnVoid        g_origScriptsProcess  = nullptr;
FnDisplayMenu g_origStd1            = nullptr;
FnDisplayMenu g_origStd2            = nullptr;
FnDisplayMenu g_origStd3            = nullptr;
FnDisplayMenu g_origStd4            = nullptr;
FnDisplayMenu g_origGrid1           = nullptr;
FnDisplayMenu g_origGrid2           = nullptr;
FnDisplayMenu g_origGrid3           = nullptr;
FnDisplayMenu g_origGrid4           = nullptr;
FnInputMenu   g_origInStd1          = nullptr;
FnInputMenu   g_origInStd2          = nullptr;
FnInputMenu   g_origInGrid1         = nullptr;
FnInputMenu   g_origInGrid2         = nullptr;
FnPrintString g_origHelpPrintString = nullptr;
FnInsertKeys  g_origInsertHelpKeys  = nullptr;

bool     g_wasManagingVanillaPanel       = false;
bool     g_blockTriangleAfterVanillaMenu = false;
uint32_t g_blockTriangleUntilMs          = 0;

// Captured right BEFORE CHud::SetHelpMessage substitutes ~k~~...~ control tokens into localized text!
bool     g_lastHelpIsStoryPhone          = false;
bool     g_lastHelpHasMenuControlTokens  = false;

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

    // CRITICAL: CMessages::InsertNumberInString (0x0069DE90) zero-fills 400 bytes of dst!
    char buf[512] = {};
    int32_t n1 = p->m_aadwNumberInRowTitle[col][row];
    int32_t n2 = p->m_aadw2ndNumberInRowTitle[col][row];
    sa::InsertNumberInString(raw, n1, n2, -1, -1, -1, -1, buf);
    buf[511] = '\0';
    sa::InsertPlayerControlKeysInString(buf);
    buf[511] = '\0';
    return std::string(buf);
}

bool IsPanelRowSelectable(const sa::tMenuPanel* p, int row) {
    if (!p || row < 0 || row >= p->m_nNumRows || row >= 12) return false;
    if (p->m_anRowEnabled[row] == 0) return false;
    return p->m_aaacRowItems[0][row][0] != '\0';
}

bool IsPanelUnclaimed(const sa::tMenuPanel* p) {
    if (!p) return false;
    char titleBuf[12] = {};
    std::memcpy(titleBuf, p->m_acTitle, 10);
    titleBuf[10] = '\0';
    return !modbus::IsPanelClaimed(titleBuf);
}

sa::tMenuPanel* GetActiveUnclaimedVanillaPanel() {
    auto* bus = modbus::GetBus();
    if (!bus || !bus->restyleVanillaMenus) return nullptr;
    if (sa::GetNumMenusInUse() == 0) return nullptr;

    int8_t focus = sa::GetCurrentMenuInFocus();
    sa::tMenuPanel* p = (focus >= 0 && focus <= 1) ? sa::GetMenuPanel(focus) : nullptr;
    if (!p || !p->m_bInteractive) p = sa::GetMenuPanel(0);
    if (!p || !p->m_bInteractive) p = sa::GetMenuPanel(1);
    if (!p || !p->m_bInteractive || p->m_nNumRows == 0) return nullptr;
    if (!IsPanelUnclaimed(p)) return nullptr;
    return p;
}

// Intercepts CMessages::InsertPlayerControlKeysInString @ 0x00588C55 inside CHud::SetHelpMessage!
// Sees the EXACT raw ~k~~...~ tokens BEFORE GTA SA replaces them with localized "Пробел"/"Space"/"F"!
void __cdecl HookedInsertHelpKeys(char* rawHelpText) {
    g_lastHelpIsStoryPhone         = false;
    g_lastHelpHasMenuControlTokens = false;

    if (rawHelpText && rawHelpText[0]) {
        if (std::strstr(rawHelpText, "PED_ANSWER_PHONE") != nullptr) {
            g_lastHelpIsStoryPhone = true;
        } else if (std::strstr(rawHelpText, "PED_SPRINT") != nullptr ||
                   std::strstr(rawHelpText, "VEHICLE_ENTER_EXIT") != nullptr ||
                   std::strstr(rawHelpText, "GO_FORWARD") != nullptr ||
                   std::strstr(rawHelpText, "GO_BACK") != nullptr ||
                   std::strstr(rawHelpText, "GO_LEFT") != nullptr ||
                   std::strstr(rawHelpText, "GO_RIGHT") != nullptr ||
                   std::strstr(rawHelpText, "PED_CYCLE_WEAPON") != nullptr) {
            g_lastHelpHasMenuControlTokens = true;
        }
    }

    if (g_origInsertHelpKeys) g_origInsertHelpKeys(rawHelpText);
}

// Unified Controls Injector for ALL Unclaimed Vanilla CMenuSystem Menus (Gym, Tattoo, Barber on mission, Betting, Color Grids!):
// Makes Enter / Space / LMB = SELECT (ButtonCross) and Backspace / Esc / RMB / F = BACK/EXIT (ButtonTriangle),
// plus Mouse Wheel / Arrows / WASD scrolling!
// Crucially holds ButtonTriangle suppressed after Accept until Enter/LMB/F are released so Gym scripts (gymbench/dumb/tread/bike)
// NEVER mistake the tail end of an Enter press on Frame 2 for "Exit Equipment"!
void __cdecl HookedScriptsProcess() {
    if (sa::GetGameState() == 9) {
        const bool helpVisible = (*reinterpret_cast<const int32_t*>(0x00BAA474) != 0);
        if (!helpVisible) {
            g_lastHelpIsStoryPhone         = false;
            g_lastHelpHasMenuControlTokens = false;
        }

        if (auto* bus = modbus::GetBus()) {
            bus->storyPhonePriority = (helpVisible && g_lastHelpIsStoryPhone) || sa::IsStoryPhoneRinging();
        }

        if (!sa::IsPauseMenuActive() && !sa::IsCutsceneRunning()) {
            ui::Keys().Update();

            sa::tMenuPanel* p = GetActiveUnclaimedVanillaPanel();
            if (p && !modbus::IsAnyModalMenuOpen()) {
                if (!g_wasManagingVanillaPanel) {
                    g_wasManagingVanillaPanel = true;
                    g_blockTriangleAfterVanillaMenu = false;
                    ui::LockMenuInputBriefly(150);
                }

                // Poll unified ListInputController: Wheel/Arrows/WASD = Scroll, F/Enter/LMB = Select, Backspace/Esc/RMB = Back/Exit!
                auto in = ui::ListInput().Poll(true);
                p->m_nAcceptedRow = static_cast<int8_t>(-99);

                if (p->m_nType == 0) {
                    int n   = std::clamp<int>(p->m_nNumRows, 1, 12);
                    int cur = std::clamp<int>(p->m_nSelectedRow, 0, n - 1);
                    if (in.up || in.down) {
                        int step = in.down ? +1 : -1;
                        int next = cur;
                        for (int tries = 0; tries < n; ++tries) {
                            next = (next + step + n) % n;
                            if (IsPanelRowSelectable(p, next)) break;
                        }
                        if (next != cur) {
                            p->m_nSelectedRow = static_cast<int8_t>(next);
                            sa::PlayFrontendSound(sa::SFX_SCROLL);
                        }
                    }
                } else if (p->m_nType == 1) {
                    int cols  = std::clamp<int>(p->m_nNumColumns, 1, 8);
                    int rows  = std::clamp<int>(p->m_nNumRows,    1, 8);
                    int total = std::min(64, cols * rows);
                    int cur   = std::clamp<int>(p->m_nSelectedRow, 0, std::max(0, total - 1));
                    int next  = cur;
                    if (in.left)       next = (cur - 1 + total) % total;
                    else if (in.right) next = (cur + 1) % total;
                    else if (in.up)    next = (cur - cols + total) % total;
                    else if (in.down)  next = (cur + cols) % total;
                    if (next != cur) {
                        p->m_nSelectedRow = static_cast<int8_t>(next);
                        sa::PlayFrontendSound(sa::SFX_SCROLL);
                    }
                }

                uint8_t* pad = sa::GetPad0();
                if (in.accept) {
                    // Feed BOTH CMenuSystem m_nAcceptedRow (0x08D8) AND ButtonCross rising-edge (0x00E1(0,16) for GYM!)
                    if (p->m_nType == 1 || IsPanelRowSelectable(p, p->m_nSelectedRow)) {
                        p->m_nAcceptedRow = p->m_nSelectedRow;
                    }
                    *reinterpret_cast<int16_t*>(pad + 0x20) = 255; // NewState.ButtonCross
                    *reinterpret_cast<int16_t*>(pad + 0x50) = 0;   // OldState.ButtonCross
                    *reinterpret_cast<int16_t*>(pad + 0x1E) = 0;   // NewState.ButtonTriangle = 0 even if Enter was pressed!
                    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
                    sa::PlayFrontendSound(sa::SFX_SELECT);

                    // CRITICAL GYM FIX: After gymbench/dumb/tread/bike deletes its menu on Frame 1,
                    // keep ButtonTriangle suppressed until Enter/LMB/F are physically released so Frame 2 NEVER aborts the workout!
                    g_blockTriangleAfterVanillaMenu = true;
                    g_blockTriangleUntilMs          = GetTickCount() + 260;
                    ui::TriggerPostCloseShield(260);
                } else if (in.back) {
                    // Feed clean native ButtonTriangle (Cancel/Back) rising-edge!
                    *reinterpret_cast<int16_t*>(pad + 0x1E) = 255; // NewState.ButtonTriangle
                    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;   // OldState.ButtonTriangle
                    *reinterpret_cast<int16_t*>(pad + 0x20) = 0;
                    *reinterpret_cast<int16_t*>(pad + 0x50) = 0;
                    sa::PlayFrontendSound(sa::SFX_BACK);

                    g_blockTriangleAfterVanillaMenu = true;
                    g_blockTriangleUntilMs          = GetTickCount() + 220;
                    ui::TriggerPostCloseShield(220);
                }
            } else {
                g_wasManagingVanillaPanel = false;
                ui::SyncIdleMouseEdges();

                // Hold ButtonTriangle & Esc suppressed until F/Enter/Esc/Mouse are released after accepting/exiting a vanilla menu!
                if (g_blockTriangleAfterVanillaMenu) {
                    const auto& k = ui::Keys();
                    bool keysHeld = k.IsDown(VK_RETURN) || k.IsDown('F') ||
                                    k.IsDown(VK_LBUTTON) || k.IsDown(VK_RBUTTON) ||
                                    k.IsDown(VK_BACK) || k.IsDown(VK_ESCAPE);
                    uint8_t* pad = sa::GetPad0();
                    *reinterpret_cast<int16_t*>(pad + 0x1E) = 0; // ButtonTriangle
                    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
                    *reinterpret_cast<int16_t*>(pad + 0x22) = 0; // ButtonCircle (prevent LMB punch!)
                    *reinterpret_cast<int16_t*>(0x00B733A8) = 0; // NewKeyState.esc (prevent Pause Menu!)
                    if (!keysHeld && static_cast<int32_t>(GetTickCount() - g_blockTriangleUntilMs) >= 0) {
                        g_blockTriangleAfterVanillaMenu = false;
                    }
                }

                if (!modbus::IsAnyModalMenuOpen()) {
                    ui::UpdatePostCloseShield();
                }
            }
        } else {
            g_wasManagingVanillaPanel       = false;
            g_blockTriangleAfterVanillaMenu = false;
        }
    } else {
        g_wasManagingVanillaPanel       = false;
        g_blockTriangleAfterVanillaMenu = false;
    }

    if (g_origScriptsProcess) g_origScriptsProcess();
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
        menu.SetFooter(isActive ? ui::StandardFooter("Выбрать", "Select", "Выход", "Exit") : "");
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
        .SetFooter(isActive ? ui::StandardFooter("Выбрать", "Apply", "Назад", "Back") : "");
    grid.Render();
}

void HandleDisplayStandardMenu(uint8_t panelId, uint8_t isActive, FnDisplayMenu origFn) {
    auto* bus = modbus::GetBus();
    if (bus && bus->restyleVanillaMenus && sa::GetGameState() == 9 && !sa::IsPauseMenuActive()) {
        sa::tMenuPanel* p = sa::GetMenuPanel(panelId);
        if (p && p->m_nType == 0) {
            if (!IsPanelUnclaimed(p)) return;
            // Suppress 1-frame vanilla blue-box flash while script populates rows between 0x08D4 & 0x08DB!
            if (p->m_nNumRows == 0 || p->m_nNumColumns == 0) return;
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
        if (p && p->m_nType == 1) {
            if (!IsPanelUnclaimed(p)) return;
            if (p->m_nNumRows == 0 || p->m_nNumColumns == 0) return;
            RenderGridPanelWithUIKit(panelId, isActive);
            return;
        }
    }
    if (origFn) origFn(panelId, isActive);
}

// Suppress vanilla CMenuSystem::InputStandardMenu / InputGridMenu whenever our unified input controller
// is actively driving an unclaimed vanilla panel (eliminates double scrolling, raw Space/Enter conflicts & m_nAcceptedRow resets!).
void HandleInputMenu(uint8_t panelId, FnInputMenu origFn) {
    auto* bus = modbus::GetBus();
    if (bus && bus->restyleVanillaMenus && sa::GetGameState() == 9 && !sa::IsPauseMenuActive()) {
        sa::tMenuPanel* p = sa::GetMenuPanel(panelId);
        if (p && p->m_bInteractive && IsPanelUnclaimed(p)) {
            return;
        }
    }
    if (origFn) origFn(panelId);
}

// Intercepts CFont::PrintString @ 0x0058C00F inside CHud::DrawHelpText (0x0058B6E0)!
void __cdecl HookedHelpPrintString(float x, float y, const char* text) {
    auto* bus = modbus::GetBus();
    const bool restyleHelp = (bus && bus->restyleHelpBoxes);
    const uint16_t statId  = *reinterpret_cast<const uint16_t*>(0x00BAA470);

    if (restyleHelp && statId == 0 && text && text[0] &&
        sa::GetGameState() == 9 && !sa::IsPauseMenuActive())
    {
        // Suppress redundant vanilla top-left Space/F control-hint boxes while ANY UI Kit menu is showing its own unified footer!
        // (Still lets gameplay workout hints like GYM1_25 / GYM1_37 render normally once the weight/speed menu closes!)
        bool anyMenuOpen  = (sa::GetNumMenusInUse() > 0) || modbus::IsAnyModalMenuOpen();
        bool isStoryPhone = g_lastHelpIsStoryPhone || sa::IsStoryPhoneRinging();
        if (anyMenuOpen && !isStoryPhone && (g_lastHelpHasMenuControlTokens || sa::IsVanillaMenuControlHint(text))) {
            return;
        }

        const int32_t state = *reinterpret_cast<const int32_t*>(0x00BAA474);
        const int32_t fade  = *reinterpret_cast<const int32_t*>(0x00BAA478);
        const float alpha   = (state == 1) ? 1.0f : std::clamp(static_cast<float>(fade) / 200.0f, 0.0f, 1.0f);
        const float rawW    = *reinterpret_cast<const float*>(0x008D0934);
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
void __cdecl HookedStd4(uint8_t id, uint8_t act)  { HandleDisplayStandardMenu(id, act, g_origStd4); }
void __cdecl HookedGrid1(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid1); }
void __cdecl HookedGrid2(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid2); }
void __cdecl HookedGrid3(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid3); }
void __cdecl HookedGrid4(uint8_t id, uint8_t act) { HandleDisplayGridMenu(id, act, g_origGrid4); }

void __cdecl HookedInStd1(uint8_t id)  { HandleInputMenu(id, g_origInStd1); }
void __cdecl HookedInStd2(uint8_t id)  { HandleInputMenu(id, g_origInStd2); }
void __cdecl HookedInGrid1(uint8_t id) { HandleInputMenu(id, g_origInGrid1); }
void __cdecl HookedInGrid2(uint8_t id) { HandleInputMenu(id, g_origInGrid2); }

} // namespace

void InstallHooks() {
    g_origScriptsProcess = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053BFC7, reinterpret_cast<uintptr_t>(&HookedScriptsProcess)));

    // ALL 4 DisplayStandardMenu sites + ALL 4 DisplayGridMenu sites (eliminates any vanilla flash!)
    g_origStd1  = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x00582687, reinterpret_cast<uintptr_t>(&HookedStd1)));
    g_origStd2  = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x005826D9, reinterpret_cast<uintptr_t>(&HookedStd2)));
    g_origStd3  = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x00582757, reinterpret_cast<uintptr_t>(&HookedStd3)));
    g_origStd4  = reinterpret_cast<FnDisplayMenu>(mem::HookJump(0x00582627, reinterpret_cast<uintptr_t>(&HookedStd4)));
    g_origGrid1 = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x00582679, reinterpret_cast<uintptr_t>(&HookedGrid1)));
    g_origGrid2 = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x005826CF, reinterpret_cast<uintptr_t>(&HookedGrid2)));
    g_origGrid3 = reinterpret_cast<FnDisplayMenu>(mem::HookCall(0x0058274D, reinterpret_cast<uintptr_t>(&HookedGrid3)));
    g_origGrid4 = reinterpret_cast<FnDisplayMenu>(mem::HookJump(0x0058261E, reinterpret_cast<uintptr_t>(&HookedGrid4)));

    // ALL 4 InputStandardMenu / InputGridMenu tail-jump sites (sole unified input ownership!)
    g_origInStd1  = reinterpret_cast<FnInputMenu>(mem::HookJump(0x00582722, reinterpret_cast<uintptr_t>(&HookedInStd1)));
    g_origInStd2  = reinterpret_cast<FnInputMenu>(mem::HookJump(0x005825F7, reinterpret_cast<uintptr_t>(&HookedInStd2)));
    g_origInGrid1 = reinterpret_cast<FnInputMenu>(mem::HookJump(0x00582717, reinterpret_cast<uintptr_t>(&HookedInGrid1)));
    g_origInGrid2 = reinterpret_cast<FnInputMenu>(mem::HookJump(0x005825EE, reinterpret_cast<uintptr_t>(&HookedInGrid2)));

    // Pre-substitution hook inside CHud::SetHelpMessage (0x00588C55) + DrawHelpText render hook (0x0058C00F)
    g_origInsertHelpKeys = reinterpret_cast<FnInsertKeys>(
        mem::HookCall(0x00588C55, reinterpret_cast<uintptr_t>(&HookedInsertHelpKeys)));
    g_origHelpPrintString = reinterpret_cast<FnPrintString>(
        mem::HookCall(0x0058C00F, reinterpret_cast<uintptr_t>(&HookedHelpPrintString)));

    modlog::Info("[UIKit] Installed complete Unified Vanilla Input (4x), Display (8x), Pre-Key Help & DrawHelpText hooks.");
}

} // namespace uikit_restyle
