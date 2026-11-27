#include <windows.h>
#include <cstdint>
#include <string>

#include "sa_sdk.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "common_utils.hpp"
#include "mod_bus.hpp"
#include "integrations/sacoop.hpp"
#include "integrations/widescreen_fix.hpp"
#include "menu_config.hpp"
#include "menu_sdk.hpp"
#include "menu_ui.hpp"

namespace modern_menu {

namespace {

constexpr uintptr_t ADDR_DRAW_FRONTEND_TAIL_JMP  = 0x0057C324; // jmp CMenuManager::DrawBackground (0x0057B750)
constexpr uintptr_t ADDR_PROCESS_USERINPUT_CALL  = 0x0057B457; // call CMenuManager::UserInput (0x0057FD70)
constexpr uintptr_t ADDR_MAP_HELPER_CALL_1       = 0x005762EC; // call CMenuManager::DisplayHelperText (0x0057E240) in DrawMap
constexpr uintptr_t ADDR_MAP_HELPER_CALL_2       = 0x00576302; // call CMenuManager::DisplayHelperText (0x0057E240) in DrawMap

using FnDisplayHelper = void(__thiscall*)(void*, const char*);

FnDrawBackground g_origDrawBg      = nullptr;
FnUserInput      g_origUserInput   = nullptr;
FnDisplayHelper  g_origMapHelper1  = nullptr;
FnDisplayHelper  g_origMapHelper2  = nullptr;
bool             g_initialized     = false;
HMODULE          g_hModule         = nullptr;

void __fastcall HookedDrawBackground(void* mm, void*) {
    if (!mm) return;
    RenderModernMenu(reinterpret_cast<uint8_t*>(mm), g_origDrawBg);
}

void __fastcall HookedUserInput(void* mm, void*) {
    if (!mm) return;
    HandleModernMenuInput(reinterpret_cast<uint8_t*>(mm), g_origUserInput);
}

void __fastcall HookedMapHelper1(void* mm, void*, const char* gxtKey) {
    if (!g_cfg.enableModernMenu && g_origMapHelper1) {
        g_origMapHelper1(mm, gxtKey);
    }
}

void __fastcall HookedMapHelper2(void* mm, void*, const char* gxtKey) {
    if (!g_cfg.enableModernMenu && g_origMapHelper2) {
        g_origMapHelper2(mm, gxtKey);
    }
}

} // anonymous namespace

bool Init(HMODULE hModule) {
    if (g_initialized) return true;
    g_hModule = hModule;

    if (!sa::IsSA10US()) return false;

    const std::string dir     = common::GetModuleDir(hModule);
    const std::string iniPath = dir + "\\GTA5ModernMenu.ini";
    LoadConfig(iniPath);

    modlog::Enabled() = g_cfg.enableLog;
    modlog::Init(hModule, "GTA5ModernMenu");
    modlog::Info("GTA5ModernMenu initializing (ModernMenu=%d, Opacity=%u, PlayerCard=%d, TitleArt=%d)",
                 g_cfg.enableModernMenu ? 1 : 0,
                 static_cast<unsigned>(g_cfg.backdropOpacity),
                 g_cfg.showPlayerCard ? 1 : 0,
                 g_cfg.showClassicTitleArt ? 1 : 0);

    // Ensure shared bus exists & UI language/scale are synced
    modbus::GetBus();

    // Suppress SA-COOP automatic startup popup & apply dark UI Kit theme if coop.asi is already loaded
    if (g_cfg.enableModernMenu && integrations::sacoop::IsLoaded()) {
        bool suppressed = integrations::sacoop::SuppressStartupConnectPopup();
        int  patched    = integrations::sacoop::ApplyDarkUiKitTheme();
        modlog::Info("SA-COOP integration ready (suppressStartup=%d, themePatches=%d)",
                     suppressed ? 1 : 0, patched);
    }

    uintptr_t origBg = mem::HookJump(ADDR_DRAW_FRONTEND_TAIL_JMP,
                                     reinterpret_cast<uintptr_t>(&HookedDrawBackground));
    uintptr_t origIn = mem::HookCall(ADDR_PROCESS_USERINPUT_CALL,
                                     reinterpret_cast<uintptr_t>(&HookedUserInput));

    if (!origBg || !origIn) {
        modlog::Info("ERROR: Hook installation failed (DrawBg=0x%08X, UserInput=0x%08X) - aborting!",
                     static_cast<unsigned>(origBg), static_cast<unsigned>(origIn));
        return false;
    }

    g_origDrawBg    = reinterpret_cast<FnDrawBackground>(origBg);
    g_origUserInput = reinterpret_cast<FnUserInput>(origIn);
    if (uintptr_t h1 = mem::HookCall(ADDR_MAP_HELPER_CALL_1, reinterpret_cast<uintptr_t>(&HookedMapHelper1))) {
        g_origMapHelper1 = reinterpret_cast<FnDisplayHelper>(h1);
    }
    if (uintptr_t h2 = mem::HookCall(ADDR_MAP_HELPER_CALL_2, reinterpret_cast<uintptr_t>(&HookedMapHelper2))) {
        g_origMapHelper2 = reinterpret_cast<FnDisplayHelper>(h2);
    }
    g_initialized   = true;
    modlog::Info("Hooks installed cleanly (origDrawBg=0x%08X, origUserInput=0x%08X, WSF=%d)",
                 static_cast<unsigned>(origBg), static_cast<unsigned>(origIn),
                 integrations::widescreen::IsLoaded() ? 1 : 0);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;
    ResetMenuUiState();
    modlog::Info("GTA5ModernMenu shutting down");
    g_initialized = false;
}

} // namespace modern_menu

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        modern_menu::Init(hModule);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        modern_menu::Shutdown();
    }
    return TRUE;
}
