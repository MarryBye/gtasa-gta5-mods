#include <algorithm>
#include <windows.h>
#include "common_utils.hpp"
#include "food_shop.hpp"
#include "integrations/sacoop.hpp"
#include "inv_data.hpp"
#include "inv_menu.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace inventory {

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

void __cdecl BusOpenInventory(bool fromPhone, int tab) { invmenu::Open(fromPhone, tab); }
bool __cdecl BusIsInventoryOpen()                      { return invmenu::IsOpen() || foodshop::IsShopActive(); }
bool __cdecl BusIsFoodShopClaimed()                    { return foodshop::ShouldClaimFoodPanel(); }
int  __cdecl BusGetTotalFood()                         { return inv::Store().GetTotalFood(); }

int __cdecl BusGetArmorCount(int tier) {
    if (tier < 0 || tier >= inv::ARMOR_COUNT) return 0;
    return inv::Store().armor[tier];
}
int __cdecl BusGetArmorMax(int tier) {
    if (tier < 0 || tier >= inv::ARMOR_COUNT) return 0;
    const auto& st = inv::Store();
    return st.armor[tier] + st.GetMaxBuyableArmor(tier);
}
bool __cdecl BusAddArmor(int tier, int delta) {
    if (tier < 0 || tier >= inv::ARMOR_COUNT) return false;
    auto& st = inv::Store();
    if (delta > 0) {
        if (!st.CanAddArmor(tier, delta)) return false;
        st.armor[tier] += delta;
    } else if (delta < 0) {
        st.armor[tier] = std::max(0, st.armor[tier] + delta);
    }
    st.SaveNow();
    return true;
}
int __cdecl BusGetArmorUsedCap() { return inv::Store().GetUsedArmorCapacity(); }
int __cdecl BusGetArmorMaxCap()  { return inv::Cfg().maxArmorCapacity; }
int __cdecl BusGetArmorWeight(int tier) {
    return (tier >= 0 && tier < inv::ARMOR_COUNT) ? inv::Cfg().armorWeight[tier] : 2;
}
int __cdecl BusGetArmorPrice(int tier) {
    return (tier >= 0 && tier < inv::ARMOR_COUNT) ? inv::Cfg().armorPrice[tier] : 350;
}

void RegisterWithBus() {
    if (auto* b = modbus::GetBus()) {
        b->inventoryInstalled       = true;
        b->inventoryOpenMenu        = &BusOpenInventory;
        b->inventoryIsMenuOpen      = &BusIsInventoryOpen;
        b->inventoryGetArmorCount   = &BusGetArmorCount;
        b->inventoryGetArmorMax     = &BusGetArmorMax;
        b->inventoryAddArmor        = &BusAddArmor;
        b->inventoryGetTotalFood    = &BusGetTotalFood;
        b->inventoryGetArmorUsedCap = &BusGetArmorUsedCap;
        b->inventoryGetArmorMaxCap  = &BusGetArmorMaxCap;
        b->inventoryGetArmorWeight  = &BusGetArmorWeight;
        b->inventoryGetArmorPrice   = &BusGetArmorPrice;
    }
    modbus::RegisterPanelClaim("FOOD", &BusIsFoodShopClaimed);
}

void __cdecl HookedReInitGame() {
    foodshop::Reset();
    invmenu::Reset();
    inv::Store().StartFreshNewGame();
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
        inv::Store().SaveToSlot(g_save.ResolveCurrentSaveSlot());
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        int slot = g_save.OnGenericLoadSuccess(integrations::sacoop::IsCoopClient(true));
        foodshop::Reset();
        invmenu::Reset();
        inv::Store().LoadFromSlot(slot);
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    const int gs = sa::GetGameState();
    if (gs == 9) {
        if (!g_save.initializedInWorld) {
            g_save.initializedInWorld = true;
            if (!g_save.loadedSaveThisBoot) {
                if (integrations::sacoop::IsCoopClient(true)) inv::Store().LoadFromSlot(0);
                else                                          inv::Store().StartFreshNewGame();
            }
        }
        if (g_save.lastGameState != 9 && g_save.lastGameState != -1) {
            foodshop::Reset();
            invmenu::Reset();
        }
        ui::SyncFromSharedBus();
        ui::Keys().Update();
        foodshop::PreScriptsTick();
        invmenu::PreScriptsTick();
    } else if (g_save.lastGameState == 9 && gs != 9) {
        foodshop::Reset();
        invmenu::Reset();
        g_save.OnLeftWorld();
    }
    g_save.lastGameState = gs;

    if (g_origScriptsProcess) g_origScriptsProcess();
}

void __cdecl HookedDrawAfterFade() {
    if (g_origDrawAfterFade) g_origDrawAfterFade();
    if (sa::GetGameState() == 9) {
        foodshop::Render2D();
        invmenu::Render2D();
    }
}

} // namespace

void Init(HMODULE hModule) {
    inv::LoadConfig(hModule);
    ui::SetUseRussian(inv::Cfg().useRussian);
    modlog::Enabled() = inv::Cfg().enableLog;
    modlog::Init(hModule, "GTA5Inventory");
    if (!sa::IsSA10US()) {
        modlog::Info("[Inv] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
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

    foodshop::InstallHooks();
    RegisterWithBus();
    modlog::Info("[Inv] GTA5Inventory hooks & Bus registered.");
}

void Shutdown() {
    foodshop::Reset();
    invmenu::Shutdown();
}

} // namespace inventory

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        inventory::Init(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        inventory::Shutdown();
    }
    return TRUE;
}
