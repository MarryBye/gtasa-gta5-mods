#include <algorithm>
#include <cstring>
#include <windows.h>
#include "audio.hpp"
#include "common_utils.hpp"
#include "dealership.hpp"
#include "delivery.hpp"
#include "garage.hpp"
#include "garage_config.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "tuning_physics.hpp"
#include "ui_kit.hpp"
#include "workshop.hpp"

namespace garage_mod {

namespace {

using FnVoid      = void(__cdecl*)();
using FnIntArg    = void(__cdecl*)(int32_t);
using FnGenericOp = bool(__cdecl*)(int32_t);
using FnLoadOp    = bool(__cdecl*)(char*);

FnVoid      g_origScriptsProcess = nullptr;
FnVoid      g_origDrawAfterFade  = nullptr;
FnVoid      g_origIdleAfterFade  = nullptr;
FnVoid      g_origReInitGame     = nullptr;
FnIntArg    g_origMakeSaveName   = nullptr;
FnGenericOp g_origGenericSave    = nullptr;
FnLoadOp    g_origGenericLoad    = nullptr;

common::SaveSlotTracker g_saveTracker{};

void __cdecl BusOpenMechanic(bool fromPhone)   { delivery::OpenMechanicMenu(fromPhone); }
void __cdecl BusOpenDealership(bool fromPhone) { dealership::OpenRemote(fromPhone); }
void __cdecl BusOpenInsurance(bool fromPhone)  { delivery::OpenInsuranceMenu(fromPhone); }
bool __cdecl BusIsAnyGarageMenuOpen()          { return workshop::IsWorkshopActive() || delivery::IsMenuOpen() || dealership::IsActive(); }
bool __cdecl BusIsWorkshopClaimed()            { return workshop::ShouldClaimWorkshopPanel(); }
int  __cdecl BusGetOwnedCount()                { return garage::CountOccupied(); }
int  __cdecl BusGetWreckedCount() {
    int n = 0;
    const auto& slots = garage::Slots();
    int lim = std::min(mod::Config().maxVehicles, mod::MAX_OWNED_VEHICLES);
    for (int i = 0; i < lim; ++i) {
        if (slots[i].occupied && slots[i].destroyed) ++n;
    }
    return n;
}

void RegisterWithBus() {
    if (auto* b = modbus::GetBus()) {
        b->garageInstalled       = true;
        b->garageOpenMechanic    = &BusOpenMechanic;
        b->garageOpenDealership  = &BusOpenDealership;
        b->garageOpenInsurance   = &BusOpenInsurance;
        b->garageIsAnyMenuOpen   = &BusIsAnyGarageMenuOpen;
        b->garageGetOwnedCount   = &BusGetOwnedCount;
        b->garageGetWreckedCount = &BusGetWreckedCount;
    }
    modbus::RegisterPanelClaim("UPGRADE", &BusIsWorkshopClaimed);
}

void __cdecl HookedReInitGame() {
    workshop::Reset();
    delivery::Reset();
    dealership::Reset();
    tuning::ResetAll();
    garage::OnNewGame();
    g_saveTracker.OnWorldRestart();
    if (g_origReInitGame) g_origReInitGame();
}

void __cdecl HookedMakeValidSaveName(int32_t slot0to7) {
    g_saveTracker.OnMakeValidSaveName(slot0to7);
    if (g_origMakeSaveName) g_origMakeSaveName(slot0to7);
}

bool __cdecl HookedGenericSave(int32_t arg) {
    bool ok = g_origGenericSave ? g_origGenericSave(arg) : false;
    if (ok) {
        int slot = g_saveTracker.ResolveCurrentSaveSlot();
        modlog::Info("[SaveHook] GenericSave succeeded -> saving garage for slot %d", slot);
        garage::OnGameSave(slot);
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        int slot = g_saveTracker.OnGenericLoadSuccess(integrations::sacoop::IsCoopClient(true));
        workshop::Reset();
        delivery::Reset();
        dealership::Reset();
        modlog::Info("[SaveHook] GenericLoad succeeded -> loading garage for slot %d (file='%s')",
                     slot, common::GetLoadFileNameBuffer());
        garage::OnGameLoad(slot);
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    const int gs = sa::GetGameState();
    if (gs == 9) {
        ui::SyncFromSharedBus();

        if (!g_saveTracker.initializedInWorld) {
            g_saveTracker.initializedInWorld = true;
            if (!g_saveTracker.loadedSaveThisBoot) {
                if (integrations::sacoop::IsCoopClient(true)) {
                    garage::OnGameLoad(0);
                } else {
                    garage::OnNewGame();
                }
            }
        }

        if (g_saveTracker.lastGameState != 9 && g_saveTracker.lastGameState != -1) {
            workshop::Reset();
            delivery::Reset();
            dealership::Reset();
            audio::StopAll();
        }

        ui::Keys().Update();
        workshop::PreScriptsTick();
        dealership::PreScriptsTick();
        delivery::PreScriptsTick();
        if (!BusIsAnyGarageMenuOpen()) {
            ui::UpdatePostCloseShield();
        }
    } else if (g_saveTracker.lastGameState == 9 && gs != 9) {
        workshop::Reset();
        delivery::Reset();
        dealership::Reset();
        tuning::ResetAll();
        g_saveTracker.OnLeftWorld();
    }
    g_saveTracker.lastGameState = gs;

    if (g_origScriptsProcess) {
        g_origScriptsProcess();
    }

    if (gs == 9) {
        workshop::PostScriptsTick();
        dealership::Tick();
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
        dealership::Render2D();
        delivery::Render2D();
    }
}

void __cdecl HookedIdleAfterFade() {
    if (g_origIdleAfterFade) {
        g_origIdleAfterFade();
    }
    if (sa::IsPauseMenuActive() || !integrations::sacoop::IsGameWindowFocused() || sa::GetGameState() != 9) {
        audio::StopAll();
    }
}

} // namespace

void Init(HMODULE hModule) {
    mod::LoadConfig(hModule);
    modlog::Enabled() = mod::Config().enableLog;
    modlog::Init(hModule, "GTA5GarageAndTuning");
    modlog::Info("GTA5GarageAndTuning attached.");

    if (!sa::IsSA10US()) {
        modlog::Info("[Init] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
        return;
    }

    g_origScriptsProcess = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053BFC7, reinterpret_cast<uintptr_t>(&HookedScriptsProcess)));
    g_origDrawAfterFade = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053E4FF, reinterpret_cast<uintptr_t>(&HookedDrawAfterFade)));
    g_origIdleAfterFade = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053EB9D, reinterpret_cast<uintptr_t>(&HookedIdleAfterFade)));
    g_origMakeSaveName = reinterpret_cast<FnIntArg>(
        mem::HookCall(0x00619066, reinterpret_cast<uintptr_t>(&HookedMakeValidSaveName)));
    g_origGenericSave = reinterpret_cast<FnGenericOp>(
        mem::HookCall(0x00619081, reinterpret_cast<uintptr_t>(&HookedGenericSave)));
    g_origGenericLoad = reinterpret_cast<FnLoadOp>(
        mem::HookCall(0x0053C70B, reinterpret_cast<uintptr_t>(&HookedGenericLoad)));
    g_origReInitGame = reinterpret_cast<FnVoid>(
        mem::HookCall(0x0053C6DB, reinterpret_cast<uintptr_t>(&HookedReInitGame)));

    workshop::InstallHooks();
    garage::InstallSafehouseHooks();
    RegisterWithBus();

    modlog::Info("[Init] GTA5GarageAndTuning hooks and shared bus registered.");
}

void Shutdown() {
    workshop::Reset();
    delivery::Reset();
    dealership::Reset();
    tuning::ResetAll();
    audio::Shutdown();
    modlog::Info("GTA5GarageAndTuning detached.");
}

} // namespace garage_mod

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        garage_mod::Init(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        garage_mod::Shutdown();
    }
    return TRUE;
}
