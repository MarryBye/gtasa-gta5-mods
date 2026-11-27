#include <cstdio>
#include <cstring>
#include <windows.h>
#include "ammu_shop.hpp"
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "weapon_audio.hpp"
#include "weapon_config.hpp"
#include "weapon_data.hpp"
#include "weapon_mods.hpp"
#include "weapon_storage.hpp"
#include "weapon_visuals.hpp"
#include "weapon_wheel.hpp"

namespace weapons {

namespace {

bool __cdecl BusIsWeaponsUiOpen() { return ammu::IsShopActive() || wheel::IsHoldWheelOpen(); }
bool __cdecl BusIsAmmuClaimed()   { return ammu::ShouldClaimAmmuPanel(); }

int __cdecl BusGetOwnedWeaponCount() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    int own = 0;
    for (size_t i = 0; i < wdata::kWeaponCount; ++i) {
        if (wdata::kWeapons[i].soldInShop && wstore::Store().PlayerHasWeaponOrOwned(ped, wdata::kWeapons[i].id)) {
            ++own;
        }
    }
    return own;
}

void __cdecl BusGetArsenalSummary(char* outBuf, int bufSize) {
    if (!outBuf || bufSize <= 0) return;
    int own = BusGetOwnedWeaponCount();
    sa::CPed* ped = sa::FindPlayerPed(0);
    int arm = sa::IsPedValid(ped) ? static_cast<int>(ped->GetArmor() + 0.5f) : 0;
    std::snprintf(outBuf, static_cast<size_t>(bufSize), "%d / %d%%", own, arm);
}

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

common::SaveSlotTracker g_saveTracker{};

void __cdecl HookedReInitGame() {
    ammu::Reset();
    wheel::Reset();
    wmods::RestoreAllStock();
    wstore::Store().StartFreshNewGame();
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
        wstore::Store().SyncFromPlayerInventory(sa::FindPlayerPed(0));
        modlog::Info("[SaveHook] GenericSave -> saving weapon arsenal for slot %d", slot);
        wstore::Store().SaveToSlot(slot);
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        const bool isClient = integrations::sacoop::IsCoopClient(true);
        int slot = g_saveTracker.OnGenericLoadSuccess(isClient);
        ammu::Reset();
        wheel::Reset();
        wmods::RestoreAllStock();
        wstore::Store().SetSlotAndLoad(slot);
        modlog::Info("[SaveHook] GenericLoad -> loaded weapon arsenal for slot %d (file='%s')",
                     slot, common::GetLoadFileNameBuffer());
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
                    wstore::Store().SetSlotAndLoad(0);
                } else {
                    wstore::Store().StartFreshNewGame();
                }
            }
        }
        if (g_saveTracker.lastGameState != 9 && g_saveTracker.lastGameState != -1) {
            ammu::Reset();
            wheel::Reset();
            wmods::RestoreAllStock();
        }
        ui::Keys().Update();
        ammu::PreScriptsTick();
        wheel::PreScriptsTick();
    } else if (g_saveTracker.lastGameState == 9 && gs != 9) {
        ammu::Reset();
        wheel::Reset();
        wmods::RestoreAllStock();
        g_saveTracker.OnLeftWorld();
    }
    g_saveTracker.lastGameState = gs;

    if (g_origScriptsProcess) {
        g_origScriptsProcess();
    }

    if (gs == 9) {
        ammu::PostScriptsTick();
        wheel::PostScriptsTick();
        wmods::Tick();
    }
}

void __cdecl HookedDrawAfterFade() {
    if (g_origDrawAfterFade) {
        g_origDrawAfterFade();
    }
    if (sa::GetGameState() == 9) {
        ammu::Render2D();
        wheel::Render2D();
    }
}

} // namespace

void Init(HMODULE hModule) {
    wcfg::Load(hModule);
    modlog::Enabled() = wcfg::Get().enableLog;
    modlog::Init(hModule, "GTA5AmmuNation");
    modlog::Info("GTA5AmmuNation attached.");

    if (!sa::IsSA10US()) {
        modlog::Info("[Init] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
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

    ammu::InstallHooks();
    wheel::InstallHooks();
    wvis::InstallHooks();
    waudio::InstallHooks();

    if (auto* b = modbus::GetBus()) {
        b->weaponsInstalled     = true;
        b->weaponsIsAnyMenuOpen = &BusIsWeaponsUiOpen;
        b->weaponsGetOwnedCount = &BusGetOwnedWeaponCount;
        b->weaponsGetSummary    = &BusGetArsenalSummary;
    }
    modbus::RegisterPanelClaim("AMMU", &BusIsAmmuClaimed);

    modlog::Info("[Init] GTA5AmmuNation hooks and shared bus registered.");
}

void Shutdown() {
    ammu::Reset();
    wheel::Reset();
    wmods::RestoreAllStock();
    waudio::Shutdown();
    modlog::Info("GTA5AmmuNation detached.");
}

} // namespace weapons

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        weapons::Init(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        weapons::Shutdown();
    }
    return TRUE;
}
