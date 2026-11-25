#include <cctype>
#include <cstring>
#include <windows.h>
#include "audio.hpp"
#include "config.hpp"
#include "delivery.hpp"
#include "garage.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "sa_sdk.hpp"
#include "tuning_physics.hpp"
#include "ui_menu.hpp"
#include "workshop.hpp"

namespace {

HMODULE g_selfModule = nullptr;

using FnVoid      = void(__cdecl*)();
using FnIntArg    = void(__cdecl*)(int32_t);
using FnGenericOp = bool(__cdecl*)(int32_t);
using FnLoadOp    = bool(__cdecl*)(char*);

FnVoid      g_origScriptsProcess = nullptr;
FnVoid      g_origDrawAfterFade  = nullptr;
FnVoid      g_origIdleAfterFade  = nullptr;
FnIntArg    g_origMakeSaveName   = nullptr;
FnGenericOp g_origGenericSave    = nullptr;
FnLoadOp    g_origGenericLoad    = nullptr;

int  g_pendingSaveSlot1to8 = 0;
bool g_loadedSaveThisBoot  = false;
bool g_initializedInWorld  = false;
int  g_lastGameState       = -1;

// Parse slot number 1..8 from GTA SA save filename buffer (e.g., "...\GTASAsf3.b")
int ParseSaveSlotFromPath(const char* path) {
    if (!path) return 0;
    const char* p = std::strstr(path, "GTASAsf");
    if (!p) p = std::strstr(path, "gtasasf");
    if (p && p[7] >= '1' && p[7] <= '8') {
        return p[7] - '0';
    }
    return 0;
}

void __cdecl HookedMakeValidSaveName(int32_t slot0to7) {
    if (slot0to7 >= 0 && slot0to7 <= 7) {
        g_pendingSaveSlot1to8 = slot0to7 + 1;
    }
    if (g_origMakeSaveName) {
        g_origMakeSaveName(slot0to7);
    }
}

bool __cdecl HookedGenericSave(int32_t arg) {
    bool ok = g_origGenericSave ? g_origGenericSave(arg) : false;
    if (ok) {
        int slot = g_pendingSaveSlot1to8;
        if (slot < 1 || slot > 8) {
            slot = ParseSaveSlotFromPath(reinterpret_cast<const char*>(0x00C16F18));
        }
        modlog::Info("[SaveHook] GenericSave succeeded -> saving garage for slot %d", slot);
        garage::OnGameSave(slot);
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        int slot = ParseSaveSlotFromPath(reinterpret_cast<const char*>(0x00C16F18));
        if (slot < 1 || slot > 8) {
            // Fallback: FrontEndMenuManager selected save slot (0..7)
            int8_t menuSlot = *reinterpret_cast<const int8_t*>(0x00BA6748 + 0x15F);
            if (menuSlot >= 0 && menuSlot <= 7) slot = menuSlot + 1;
        }
        g_loadedSaveThisBoot = true;
        g_initializedInWorld = true;
        workshop::Reset();
        delivery::Reset();
        modlog::Info("[SaveHook] GenericLoad succeeded -> loading garage for slot %d", slot);
        garage::OnGameLoad(slot);
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    const int gs = sa::GetGameState();
    if (gs == 9) {
        if (!g_initializedInWorld) {
            g_initializedInWorld = true;
            if (!g_loadedSaveThisBoot) {
                if (sa::IsCoopModLoaded()) {
                    // Joined SA-COOP without local save load -> load coop/autosave slot 0
                    garage::OnGameLoad(0);
                } else {
                    // Fresh Singleplayer New Game -> start with an empty garage!
                    garage::OnNewGame();
                }
            }
        }

        // Handle game reload / restart transitions cleanly
        if (g_lastGameState != 9 && g_lastGameState != -1) {
            workshop::Reset();
            delivery::Reset();
            audio::StopAll();
        }

        ui::Keys().Update();
        workshop::PreScriptsTick();
    } else if (g_lastGameState == 9 && gs != 9) {
        // Returning to main menu / loading screen
        workshop::Reset();
        delivery::Reset();
        tuning::ResetAll();
        g_initializedInWorld = false;
        g_loadedSaveThisBoot = false;
    }
    g_lastGameState = gs;

    if (g_origScriptsProcess) {
        g_origScriptsProcess();
    }

    if (gs == 9) {
        workshop::PostScriptsTick();
        delivery::Tick();
        garage::Tick();
        tuning::TickAll();
    }
}

void __cdecl HookedDrawAfterFade() {
    if (g_origDrawAfterFade) {
        g_origDrawAfterFade();
    }
    if (sa::GetGameState() == 9) {
        workshop::Render2D();
        delivery::Render2D();
    }
}

// Runs EVERY frame inside Idle() — INCLUDING Pause Menu & unfocused window!
void __cdecl HookedIdleAfterFade() {
    if (g_origIdleAfterFade) {
        g_origIdleAfterFade();
    }
    if (sa::IsPauseMenuActive() || !sa::IsGameWindowFocused() || sa::GetGameState() != 9) {
        audio::StopAll();
    }
}

void InstallAllHooks() {
    if (!sa::IsSA10US()) {
        modlog::Info("[Init] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
        return;
    }

    // 1. CGame::Process -> CTheScripts::Process (0x0053BFC7: CALL 0x0046A000)
    g_origScriptsProcess = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053BFC7, reinterpret_cast<uintptr_t>(&HookedScriptsProcess)));

    // 2. Render2dStuff -> CHud::DrawAfterFade (0x0053E4FF: CALL 0x0058FAE0)
    g_origDrawAfterFade = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053E4FF, reinterpret_cast<uintptr_t>(&HookedDrawAfterFade)));

    // 2b. Idle -> Render2dStuffAfterFade (0x0053EB9D: CALL 0x0053E600) — runs in Pause Menu too!
    g_origIdleAfterFade = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053EB9D, reinterpret_cast<uintptr_t>(&HookedIdleAfterFade)));

    // 3. Per-save slot hooks (C_PcSave::SaveSlot & GenericLoad)
    g_origMakeSaveName = reinterpret_cast<FnIntArg>(
        mem::HookCall(0x00619066, reinterpret_cast<uintptr_t>(&HookedMakeValidSaveName)));
    g_origGenericSave = reinterpret_cast<FnGenericOp>(
        mem::HookCall(0x00619081, reinterpret_cast<uintptr_t>(&HookedGenericSave)));
    g_origGenericLoad = reinterpret_cast<FnLoadOp>(
        mem::HookCall(0x0053C70B, reinterpret_cast<uintptr_t>(&HookedGenericLoad)));

    // 4. Workshop CMenuSystem::Process hook (0x0058FCDB)
    workshop::InstallHooks();

    modlog::Info("[Init] All hooks installed successfully! (Scripts=0x%08X, Draw=0x%08X, Save=0x%08X, Load=0x%08X)",
                 reinterpret_cast<uintptr_t>(g_origScriptsProcess),
                 reinterpret_cast<uintptr_t>(g_origDrawAfterFade),
                 reinterpret_cast<uintptr_t>(g_origGenericSave),
                 reinterpret_cast<uintptr_t>(g_origGenericLoad));
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_selfModule = hModule;
            DisableThreadLibraryCalls(hModule);
            mod::LoadConfig(hModule);
            ui::SetUseRussian(mod::Config().useRussian);
            modlog::Enabled() = mod::Config().enableLog;
            modlog::Init(hModule, "GTA5GarageAndTuning");
            modlog::Info("GTA5GarageAndTuning v1.1 attached (Shared UI Kit, SA 1.0 US, SA-COOP & Proper Shaders compatible).");
            InstallAllHooks();
            break;
        case DLL_PROCESS_DETACH:
            audio::StopAll();
            tuning::ResetAll();
            modlog::Info("GTA5GarageAndTuning detached.");
            break;
        default:
            break;
    }
    return TRUE;
}
