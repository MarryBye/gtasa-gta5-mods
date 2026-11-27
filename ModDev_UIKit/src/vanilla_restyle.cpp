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

bool                  g_wasManagingVanillaPanel       = false;
bool                  g_blockTriangleAfterVanillaMenu = false;
uint32_t              g_blockTriangleUntilMs          = 0;
const sa::tMenuPanel* g_lastManagedPanel              = nullptr;
char                  g_lastManagedTitle[12]          = {};
char                  g_lastManagedRow0[12]           = {};
uint8_t               g_lastManagedRows               = 0;
uint8_t               g_lastManagedCols               = 0;
uint8_t               g_lastManagedInteractive        = 0;
ui::OrbitCameraRig    g_vanillaOrbit{};

// Captured right BEFORE CHud::SetHelpMessage substitutes ~k~~...~ control tokens into localized text!
bool                  g_lastHelpIsStoryPhone          = false;
bool                  g_lastHelpHasMenuControlTokens  = false;

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

// Non-interactive buy/cancel confirmation panels used by vanilla barber.scm ($25C4 HAIRCHO) & mission clothes.scm:
// panel has m_bInteractive == 0, player controls are locked, and a Space/F confirm help prompt is active!
bool IsConfirmOnlyVanillaPanel(const sa::tMenuPanel* p, bool helpVisible) {
    if (!p || p->m_bInteractive || p->m_nNumRows == 0) return false;
    if (!IsPanelUnclaimed(p)) return false;
    if (!sa::IsPlayerControlsDisabled()) return false;
    if (std::strncmp(p->m_aaacRowItems[0][0], "BOUGHT", 6) == 0) return false;
    const bool isKnownShopConfirm =
        (std::strncmp(p->m_acTitle, "HAIR", 4) == 0) ||
        (std::strncmp(p->m_acTitle, "TATT", 4) == 0) ||
        (std::strncmp(p->m_acTitle, "CLOT", 4) == 0);
    return isKnownShopConfirm || (helpVisible && g_lastHelpHasMenuControlTokens);
}

bool SupportsVanillaOrbitCamera(const sa::tMenuPanel* p) {
    if (!p) return false;
    if (sa::camera::GetActiveCamMode() != 15) return false; // 15 = MODE_FIXED (shop preview camera)
    return (std::strncmp(p->m_acTitle, "HAIR", 4) == 0) ||
           (std::strncmp(p->m_acTitle, "TATT", 4) == 0);
}

sa::tMenuPanel* GetActiveUnclaimedVanillaPanel(bool helpVisible) {
    auto* bus = modbus::GetBus();
    if (!bus || !bus->restyleVanillaMenus) return nullptr;
    if (sa::GetNumMenusInUse() == 0) return nullptr;

    int8_t focus = sa::GetCurrentMenuInFocus();
    sa::tMenuPanel* p = (focus >= 0 && focus <= 1) ? sa::GetMenuPanel(focus) : nullptr;
    if (!p || (!p->m_bInteractive && !IsConfirmOnlyVanillaPanel(p, helpVisible))) p = sa::GetMenuPanel(0);
    if (!p || (!p->m_bInteractive && !IsConfirmOnlyVanillaPanel(p, helpVisible))) p = sa::GetMenuPanel(1);
    if (!p || p->m_nNumRows == 0) return nullptr;
    if (!p->m_bInteractive && !IsConfirmOnlyVanillaPanel(p, helpVisible)) return nullptr;
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

// Unified Controls Injector for ALL Unclaimed Vanilla CMenuSystem Menus (Gym, Tattoo, Barber, Betting, Mission Menus, Color Grids!):
// Uses unified ListInputController (Wheel/Arrows/WASD = Scroll, LMB/F/Enter = Select, RMB/Backspace = Back/Exit)
// and preserves exact CMenuSystem::InputStandardMenu m_nAcceptedRow semantics (0x0058094F) so 2-frame
// press->release->GET_MENU_ITEM_ACCEPTED scripts (barber.scm, tattoo.scm) always read the chosen row!
void __cdecl HookedScriptsProcess() {
    const bool inWorld = (sa::GetGameState() == 9);
    const bool livePlay = inWorld && !sa::IsPauseMenuActive() && !sa::IsCutsceneRunning();

    if (inWorld) {
        const bool helpVisible = (*reinterpret_cast<const int32_t*>(0x00BAA474) != 0);
        if (!helpVisible) {
            g_lastHelpIsStoryPhone         = false;
            g_lastHelpHasMenuControlTokens = false;
        }

        if (auto* bus = modbus::GetBus()) {
            bus->storyPhonePriority = (helpVisible && g_lastHelpIsStoryPhone) || sa::IsStoryPhoneRinging();
        }

        if (livePlay) {
            ui::Keys().Update();

            sa::tMenuPanel* p = GetActiveUnclaimedVanillaPanel(helpVisible);
            if (p && !modbus::IsAnyModalMenuOpen("vanilla")) {
                const bool panelChanged =
                    !g_wasManagingVanillaPanel ||
                    (p != g_lastManagedPanel) ||
                    (std::strncmp(p->m_acTitle, g_lastManagedTitle, 10) != 0) ||
                    (std::strncmp(p->m_aaacRowItems[0][0], g_lastManagedRow0, 10) != 0) ||
                    (p->m_nNumRows != g_lastManagedRows) ||
                    (p->m_nNumColumns != g_lastManagedCols) ||
                    (p->m_bInteractive != g_lastManagedInteractive);

                if (!g_wasManagingVanillaPanel) {
                    g_wasManagingVanillaPanel       = true;
                    g_blockTriangleAfterVanillaMenu = false;
                }
                if (panelChanged) {
                    g_lastManagedPanel       = p;
                    std::memcpy(g_lastManagedTitle, p->m_acTitle, 10);
                    g_lastManagedTitle[10]   = '\0';
                    std::memcpy(g_lastManagedRow0, p->m_aaacRowItems[0][0], 10);
                    g_lastManagedRow0[10]    = '\0';
                    g_lastManagedRows        = p->m_nNumRows;
                    g_lastManagedCols        = p->m_nNumColumns;
                    g_lastManagedInteractive = p->m_bInteractive;
                    g_vanillaOrbit.Reset();
                    ui::LockMenuInputBriefly(150);
                }

                if (auto* bus = modbus::GetBus()) {
                    bus->vanillaMenuOpen = true;
                }

                // Poll unified ListInputController: Wheel/Arrows/WASD = Scroll, LMB/F/Enter = Select, RMB/Backspace = Back/Exit!
                auto in = ui::ListInput().Poll(true);

                if (p->m_bInteractive) {
                    // Match vanilla CMenuSystem::InputStandardMenu @ 0x0058094F: keep m_nAcceptedRow intact
                    // while selection hasn't moved so Frame-2 release->GET_MENU_ITEM_ACCEPTED reads the chosen row!
                    if (p->m_nAcceptedRow != p->m_nSelectedRow) {
                        p->m_nAcceptedRow = static_cast<int8_t>(-99);
                    }

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
                                p->m_nAcceptedRow = static_cast<int8_t>(-99);
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
                            p->m_nAcceptedRow = static_cast<int8_t>(-99);
                            sa::PlayFrontendSound(sa::SFX_SCROLL);
                        }
                    }
                }

                uint8_t* pad = sa::GetPad0();
                if (in.accept) {
                    // Feed BOTH CMenuSystem m_nAcceptedRow (0x08D8) AND ButtonCross rising-edge (0x00E1(0,16) for Gym/Barber/Tattoo!)
                    if (p->m_bInteractive && (p->m_nType == 1 || IsPanelRowSelectable(p, p->m_nSelectedRow))) {
                        p->m_nAcceptedRow = p->m_nSelectedRow;
                    }
                    *reinterpret_cast<int16_t*>(pad + 0x20) = 255; // NewState.ButtonCross
                    *reinterpret_cast<int16_t*>(pad + 0x50) = 0;   // OldState.ButtonCross
                    *reinterpret_cast<int16_t*>(pad + 0x1E) = 0;   // NewState.ButtonTriangle = 0 even if F/Enter was pressed!
                    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
                    sa::PlayFrontendSound(sa::SFX_SELECT);

                    // Hold ButtonTriangle suppressed AFTER the menu closes so Gym/Shop scripts never abort on Frame 2,
                    // and debounce input across multi-step submenus (Tattoo Body -> List -> Buy, Barber List -> Confirm)!
                    g_blockTriangleAfterVanillaMenu = true;
                    g_blockTriangleUntilMs          = GetTickCount() + 260;
                    ui::LockMenuInputBriefly(160);
                } else if (in.back) {
                    p->m_nAcceptedRow = static_cast<int8_t>(-99);
                    *reinterpret_cast<int16_t*>(pad + 0x1E) = 255; // NewState.ButtonTriangle
                    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;   // OldState.ButtonTriangle
                    *reinterpret_cast<int16_t*>(pad + 0x20) = 0;
                    *reinterpret_cast<int16_t*>(pad + 0x50) = 0;
                    sa::PlayFrontendSound(sa::SFX_BACK);

                    g_blockTriangleAfterVanillaMenu = true;
                    g_blockTriangleUntilMs          = GetTickCount() + 220;
                    ui::LockMenuInputBriefly(160);
                }
            } else {
                const bool justClosed = g_wasManagingVanillaPanel;
                g_wasManagingVanillaPanel = false;
                g_lastManagedPanel        = nullptr;
                g_vanillaOrbit.Reset();
                if (auto* bus = modbus::GetBus()) {
                    bus->vanillaMenuOpen = false;
                }
                if (justClosed) {
                    ui::TriggerPostCloseShield(220);
                }

                // Hold ButtonTriangle suppressed until F/Enter/Back/Mouse are released after accepting/exiting a vanilla menu!
                if (g_blockTriangleAfterVanillaMenu) {
                    const auto& k = ui::Keys();
                    bool keysHeld = k.IsDown(VK_RETURN) || k.IsDown('F') ||
                                    k.IsDown(VK_LBUTTON) || k.IsDown(VK_RBUTTON) ||
                                    k.IsDown(VK_BACK);
                    uint8_t* pad = sa::GetPad0();
                    *reinterpret_cast<int16_t*>(pad + 0x1E) = 0; // ButtonTriangle
                    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
                    *reinterpret_cast<int16_t*>(pad + 0x22) = 0; // ButtonCircle (prevent LMB punch!)
                    if (!keysHeld && static_cast<int32_t>(GetTickCount() - g_blockTriangleUntilMs) >= 0) {
                        g_blockTriangleAfterVanillaMenu = false;
                    }
                }

                if (!modbus::IsAnyModalMenuOpen("vanilla")) {
                    ui::UpdatePostCloseShield();
                }
            }
        } else {
            g_wasManagingVanillaPanel       = false;
            g_blockTriangleAfterVanillaMenu = false;
            g_lastManagedPanel              = nullptr;
            g_vanillaOrbit.Reset();
            if (auto* bus = modbus::GetBus()) bus->vanillaMenuOpen = false;
        }
    } else {
        g_wasManagingVanillaPanel       = false;
        g_blockTriangleAfterVanillaMenu = false;
        g_lastManagedPanel              = nullptr;
        g_vanillaOrbit.Reset();
        if (auto* bus = modbus::GetBus()) bus->vanillaMenuOpen = false;
    }

    if (g_origScriptsProcess) g_origScriptsProcess();

    // Post-scripts 3D preview orbit camera for vanilla Barber (HAIR*) & Tattoo (TATT*) menus:
    // Leaves vanilla script camera 100% intact until user presses Q/E/Z/X, then orbits symmetrically
    // around Carl's actual live animated bone (BONE_HEAD for Barber, BONE_SPINE1 for Tattoo)!
    if (livePlay && g_wasManagingVanillaPanel && !modbus::IsAnyModalMenuOpen("vanilla")) {
        const bool helpNow = (*reinterpret_cast<const int32_t*>(0x00BAA474) != 0);
        sa::tMenuPanel* activeP = GetActiveUnclaimedVanillaPanel(helpNow);
        if (activeP && SupportsVanillaOrbitCamera(activeP)) {
            sa::CPed* ped = sa::FindPlayerPed(0);
            if (sa::IsPedValid(ped)) {
                const bool isBarber = (std::strncmp(activeP->m_acTitle, "HAIR", 4) == 0);
                sa::CVector headPos{};
                sa::CVector pivot{};
                ped->GetBonePosition(headPos, sa::CPed::BONE_HEAD, true);
                if (isBarber) {
                    pivot = headPos;
                } else {
                    ped->GetBonePosition(pivot, sa::CPed::BONE_SPINE1, true);
                }
                const float wallRayZOff = isBarber ? 0.05f : std::max(0.32f, headPos.z - pivot.z);

                const float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
                const auto camIn = ui::ListInputController::PollCamera(dt, 1.85f, 1.45f);
                const bool wantsOrbit = (camIn.orbitDelta != 0.0f || camIn.zoomDelta != 0.0f);

                if (!g_vanillaOrbit.active && wantsOrbit) {
                    // Use buildings-only horizontal wall probe at head height so seated chairs (even CBuilding chairs!)
                    // never trap zoom while exterior room walls stay solid.
                    g_vanillaOrbit.BeginFromCurrentCamera(pivot, isBarber ? 0.52f : 0.48f, isBarber ? 2.00f : 2.60f, 0.0f, false);
                    g_vanillaOrbit.minClearDist = 0.42f;
                }
                if (g_vanillaOrbit.active) {
                    g_vanillaOrbit.Update(camIn);
                    g_vanillaOrbit.Apply(pivot, 0.0f, wallRayZOff);
                }
            }
        } else {
            g_vanillaOrbit.Reset();
        }
    } else {
        g_vanillaOrbit.Reset();
    }
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

    const bool helpVisible = (*reinterpret_cast<const int32_t*>(0x00BAA474) != 0);
    const bool confirmOnly = IsConfirmOnlyVanillaPanel(p, helpVisible);
    const bool hasOrbitCam = SupportsVanillaOrbitCamera(p);
    const char* camRus = hasOrbitCam ? "Q/E Кам. Z/X Прибл." : nullptr;
    const char* camEng = hasOrbitCam ? "Q/E Cam Z/X Zoom"    : nullptr;

    if (p->m_bInteractive) {
        menu.SetFooter(isActive ? ui::StandardFooter("Выбрать", "Select", "Выход", "Exit", camRus, camEng) : "");
        if (!isActive) menu.SetAccent(ui::Theme().selectMuted);
    } else if (confirmOnly) {
        menu.SetFooter(ui::StandardFooter("Купить", "Buy", "Назад", "Back", camRus, camEng));
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
            ? ui::ColorSuccess()
            : (enabled ? ui::ColorNormal() : ui::ColorDisabled());

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
        .SetFooter(isActive ? ui::StandardFooter("Применить", "Apply", "Назад", "Back") : "");
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
        const float boxW    = (rawW >= 120.0f && rawW <= 320.0f) ? std::clamp(rawW, 175.0f, 235.0f) : 198.0f;

        // Classify toast header & semantic tone cleanly outside dumb ui::HelpToast (Rule 1a & 1c!)
        std::string toastTitle;
        ui::ToastTone tone = ui::ToastTone::Neutral;
        if (isStoryPhone) {
            toastTitle = ui::Loc("ЗВОНОК", "CALL");
            tone       = ui::ToastTone::Warning;
        } else if (std::strstr(text, "~r~") != nullptr) {
            toastTitle = ui::Loc("ВНИМАНИЕ", "WARNING");
            tone       = ui::ToastTone::Danger;
        } else if (std::strstr(text, "~g~") != nullptr) {
            toastTitle = ui::Loc("УВЕДОМЛЕНИЕ", "NOTICE");
            tone       = ui::ToastTone::Success;
        } else if (g_lastHelpHasMenuControlTokens || std::strstr(text, "~k~") != nullptr) {
            toastTitle = ui::Loc("ОБУЧЕНИЕ", "TUTORIAL");
            tone       = ui::ToastTone::Warning;
        } else if (std::strstr(text, "~y~") != nullptr) {
            toastTitle = ui::Loc("ПОДСКАЗКА", "HINT");
            tone       = ui::ToastTone::Warning;
        } else {
            toastTitle = ui::Loc("УВЕДОМЛЕНИЕ", "NOTICE");
            tone       = ui::ToastTone::Neutral;
        }

        ui::HelpToast toast;
        toast.SetTitle(toastTitle).SetText(text).SetTone(tone).SetAlpha(alpha).SetWidth640(boxW);
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
