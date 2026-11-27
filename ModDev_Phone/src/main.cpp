#include <windows.h>
#include "clothes_shop.hpp"
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "phone_config.hpp"
#include "phone_ui.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "wardrobe.hpp"

namespace phone {

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

bool __cdecl BusIsPhoneOpen()      { return phoneui::IsOpen() || clothesshop::IsShopActive(); }
void __cdecl BusClosePhone()       { phoneui::Close(); }
void __cdecl BusReturnToPhone()    { phoneui::ReturnToMain(); }
bool __cdecl BusIsClothesClaimed() { return clothesshop::ShouldClaimShopPanel(); }

void RegisterWithBus() {
    if (auto* b = modbus::GetBus()) {
        b->phoneInstalled    = true;
        b->phoneKey          = pcfg::Get().phoneKey;
        b->phoneIsOpen       = &BusIsPhoneOpen;
        b->phoneClose        = &BusClosePhone;
        b->phoneReturnToMain = &BusReturnToPhone;
    }
    modbus::RegisterPanelClaim("CLOT", &BusIsClothesClaimed);
}

void __cdecl HookedReInitGame() {
    clothesshop::Reset();
    phoneui::Reset();
    wardrobe::Reset();
    wardrobe::OnNewGame();
    phoneui::OnNewGame();
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
        int slot = g_save.ResolveCurrentSaveSlot();
        wardrobe::OnGameSave(slot);
        phoneui::OnGameSave(slot);
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        int slot = g_save.OnGenericLoadSuccess(integrations::sacoop::IsCoopClient(true));
        clothesshop::Reset();
        phoneui::Reset();
        wardrobe::OnGameLoad(slot);
        phoneui::OnGameLoad(slot);
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    const int gs = sa::GetGameState();
    if (gs == 9) {
        if (!g_save.initializedInWorld) {
            g_save.initializedInWorld = true;
            wardrobe::EnsureCatalogLoaded();
            if (!g_save.loadedSaveThisBoot) {
                if (integrations::sacoop::IsCoopClient(true)) {
                    wardrobe::OnGameLoad(0);
                    phoneui::OnGameLoad(0);
                } else {
                    wardrobe::OnNewGame();
                    phoneui::OnNewGame();
                }
            }
        }
        if (g_save.lastGameState != 9 && g_save.lastGameState != -1) {
            clothesshop::Reset();
            phoneui::Reset();
            wardrobe::Reset();
        }
        ui::SyncFromSharedBus();
        ui::Keys().Update();
        clothesshop::PreScriptsTick();
        phoneui::PreScriptsTick();
    } else if (g_save.lastGameState == 9 && gs != 9) {
        clothesshop::Reset();
        phoneui::Reset();
        wardrobe::Reset();
        g_save.OnLeftWorld();
    }
    g_save.lastGameState = gs;

    if (g_origScriptsProcess) g_origScriptsProcess();

    if (gs == 9) {
        clothesshop::PostScriptsTick();
        wardrobe::Tick();
        phoneui::PostScriptsTick();
    }
}

void __cdecl HookedDrawAfterFade() {
    if (g_origDrawAfterFade) g_origDrawAfterFade();
    if (sa::GetGameState() == 9) {
        clothesshop::Render2D();
        phoneui::Render2D();
    }
}

} // namespace

void Init(HMODULE hModule) {
    pcfg::Load(hModule);
    ui::SetUseRussian(pcfg::Get().useRussian);
    modlog::Enabled() = pcfg::Get().enableLog;
    modlog::Init(hModule, "GTA5Phone");
    if (!sa::IsSA10US()) {
        modlog::Info("[Phone] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
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

    clothesshop::InstallHooks();
    phoneui::InstallHooks();
    RegisterWithBus();
    modlog::Info("[Phone] GTA5Phone hooks & Addon Hub Bus registered.");
}

void Shutdown() {
    clothesshop::Reset();
    wardrobe::Reset();
    phoneui::Shutdown();
}

} // namespace phone

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        phone::Init(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        phone::Shutdown();
    }
    return TRUE;
}
