#include <cctype>
#include <cstring>
#include <windows.h>
#include "audio.hpp"
#include "config.hpp"
#include "dealership.hpp"
#include "delivery.hpp"
#include "garage.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
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

// Bus callbacks exported cleanly to GTA5Phone & GTA5UIKit!
void __cdecl BusOpenMechanic(bool fromPhone)   { delivery::OpenMechanicMenu(fromPhone); }
void __cdecl BusOpenDealership(bool fromPhone) { dealership::OpenRemote(fromPhone); }
void __cdecl BusOpenInsurance(bool fromPhone)  { delivery::OpenInsuranceMenu(fromPhone); }
bool __cdecl BusIsAnyGarageMenuOpen()          { return workshop::IsWorkshopActive() || delivery::IsMenuOpen() || dealership::IsActive(); }
bool __cdecl BusIsWorkshopClaimed()            { return workshop::IsCustomWorkshopMenuDrawing(); }
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

int ParseSaveSlotFromPath(const char* path) {
    if (!path) return 0;
    const char* p = std::strstr(path, "GTASAsf");
    if (!p) p = std::strstr(path, "gtasasf");
    if (p && p[7] >= '1' && p[7] <= '8') return p[7] - '0';
    return 0;
}

void __cdecl HookedMakeValidSaveName(int32_t slot0to7) {
    if (slot0to7 >= 0 && slot0to7 <= 7) g_pendingSaveSlot1to8 = slot0to7 + 1;
    if (g_origMakeSaveName) g_origMakeSaveName(slot0to7);
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
            int8_t menuSlot = *reinterpret_cast<const int8_t*>(0x00BA6748 + 0x15F);
            if (menuSlot >= 0 && menuSlot <= 7) slot = menuSlot + 1;
        }
        g_loadedSaveThisBoot = true;
        g_initializedInWorld = true;
        workshop::Reset();
        delivery::Reset();
        dealership::Reset();
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
                    garage::OnGameLoad(0);
                } else {
                    garage::OnNewGame();
                }
            }
        }

        if (g_lastGameState != 9 && g_lastGameState != -1) {
            workshop::Reset();
            delivery::Reset();
            dealership::Reset();
            audio::StopAll();
        }

        ui::Keys().Update();
        workshop::PreScriptsTick();
        dealership::PreScriptsTick();
        delivery::PreScriptsTick();
    } else if (g_lastGameState == 9 && gs != 9) {
        workshop::Reset();
        delivery::Reset();
        dealership::Reset();
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
    if (sa::IsPauseMenuActive() || !sa::IsGameWindowFocused() || sa::GetGameState() != 9) {
        audio::StopAll();
    }
}

void InstallAllHooks() {
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

    workshop::InstallHooks();
    garage::InstallSafehouseHooks();
    RegisterWithBus();

    modlog::Info("[Init] GTA5GarageAndTuning v2.0 hooks & Bus registered (Tuning + Wear + Dealership + Mechanic)!");
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
            modlog::Info("GTA5GarageAndTuning v2.0 attached (UIKit Bus, Wear & Tear, Official Dealership, Mechanic).");
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
