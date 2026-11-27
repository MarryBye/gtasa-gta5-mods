#include <windows.h>
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "prog_config.hpp"
#include "prog_engine.hpp"
#include "prog_store.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace progression {

namespace {

using FnVoid      = void(__cdecl*)();
using FnIntArg    = void(__cdecl*)(int32_t);
using FnGenericOp = bool(__cdecl*)(int32_t);
using FnLoadOp    = bool(__cdecl*)(char*);

FnVoid      g_origScriptsProcess = nullptr;
FnVoid      g_origDrawAfterFade  = nullptr;
FnVoid      g_origReInitGame     = nullptr;
FnIntArg    g_origMakeSaveName   = nullptr;
FnGenericOp g_origGenericSave    = nullptr;
FnLoadOp    g_origGenericLoad    = nullptr;

common::SaveSlotTracker g_save;

void __cdecl HookedReInitGame() {
    OnLeftWorld();
    Store().StartFreshNewGame();
    SyncBusState();
    g_save.OnWorldRestart();
    if (g_origReInitGame) g_origReInitGame();
}

void __cdecl HookedMakeValidSaveName(int32_t slot0to7) {
    g_save.OnMakeValidSaveName(slot0to7);
    if (g_origMakeSaveName) g_origMakeSaveName(slot0to7);
}

bool __cdecl HookedGenericSave(int32_t arg) {
    bool ok = g_origGenericSave ? g_origGenericSave(arg) : false;
    if (ok) {
        OnSaveWritten(g_save.ResolveCurrentSaveSlot());
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        int slot = g_save.OnGenericLoadSuccess(integrations::sacoop::IsCoopClient(true));
        OnSaveLoaded(slot);
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    const int gs = sa::GetGameState();
    if (gs == 9) {
        if (!g_save.initializedInWorld) {
            g_save.initializedInWorld = true;
            OnWorldEntered(g_save.loadedSaveThisBoot);
        }
        ui::SyncFromSharedBus();
        ui::Keys().Update();
        PreScriptsTick();
    } else if (g_save.lastGameState == 9 && gs != 9) {
        OnLeftWorld();
        g_save.OnLeftWorld();
    }
    g_save.lastGameState = gs;

    if (g_origScriptsProcess) g_origScriptsProcess();
}

void __cdecl HookedDrawAfterFade() {
    if (g_origDrawAfterFade) g_origDrawAfterFade();
    if (sa::GetGameState() == 9) {
        Render2D();
    }
}

} // namespace

void Init(HMODULE hModule) {
    LoadConfig(hModule);
    ui::SetUseRussian(Cfg().useRussian);
    modlog::Enabled() = Cfg().enableLog;
    modlog::Init(hModule, "GTA5Progression");
    if (!sa::IsSA10US()) {
        modlog::Info("[Prog] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
        return;
    }

    g_origScriptsProcess = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053BFC7, reinterpret_cast<uintptr_t>(&HookedScriptsProcess)));
    g_origDrawAfterFade = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053E4FF, reinterpret_cast<uintptr_t>(&HookedDrawAfterFade)));
    g_origMakeSaveName = reinterpret_cast<FnIntArg>(
        mem::HookCall(0x00619066, reinterpret_cast<uintptr_t>(&HookedMakeValidSaveName)));
    g_origGenericSave = reinterpret_cast<FnGenericOp>(
        mem::HookCall(0x00619081, reinterpret_cast<uintptr_t>(&HookedGenericSave)));
    g_origGenericLoad = reinterpret_cast<FnLoadOp>(
        mem::HookCall(0x0053C70B, reinterpret_cast<uintptr_t>(&HookedGenericLoad)));
    g_origReInitGame = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053C6DB, reinterpret_cast<uintptr_t>(&HookedReInitGame)));

    InstallHooks();
    SyncBusState();
    modlog::Info("[Prog] GTA5Progression initialized and registered on shared bus.");
}

} // namespace progression

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        progression::Init(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        progression::Shutdown();
    }
    return TRUE;
}
