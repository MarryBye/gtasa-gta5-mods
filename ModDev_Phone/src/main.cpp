#include <cstring>
#include <windows.h>
#include "clothes_shop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "phone_config.hpp"
#include "phone_ui.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "wardrobe.hpp"

namespace {

using FnVoid      = void(__cdecl*)();
using FnIntArg    = void(__cdecl*)(int32_t);
using FnGenericOp = bool(__cdecl*)(int32_t);
using FnLoadOp    = bool(__cdecl*)(char*);

FnVoid      g_origScriptsProcess = nullptr;
FnVoid      g_origDrawAfterFade  = nullptr;
FnIntArg    g_origMakeSaveName   = nullptr;
FnGenericOp g_origGenericSave    = nullptr;
FnLoadOp    g_origGenericLoad    = nullptr;

int  g_pendingSaveSlot1to8 = 0;
bool g_loadedSaveThisBoot  = false;
bool g_initializedInWorld  = false;
int  g_lastGameState       = -1;

// Bus callbacks exposed to all other mods!
bool __cdecl BusIsPhoneOpen()       { return phoneui::IsOpen() || clothesshop::IsShopActive(); }
void __cdecl BusClosePhone()        { phoneui::Close(); }
void __cdecl BusReturnToPhone()     { phoneui::ReturnToMain(); }
bool __cdecl BusIsClothesClaimed()  { return clothesshop::IsShopActive(); }

void RegisterWithBus() {
    if (auto* b = modbus::GetBus()) {
        b->phoneInstalled    = true; // Automatically suppresses Garage F6 & Inventory F7!
        b->phoneIsOpen       = &BusIsPhoneOpen;
        b->phoneClose        = &BusClosePhone;
        b->phoneReturnToMain = &BusReturnToPhone;
    }
    modbus::RegisterPanelClaim("CLOT", &BusIsClothesClaimed);
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
            slot = sa::ParseSaveSlotFromPath(reinterpret_cast<const char*>(0x00C16F18));
        }
        wardrobe::OnGameSave(slot);
        phoneui::OnGameSave(slot);
    }
    return ok;
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        int slot = sa::ParseSaveSlotFromPath(reinterpret_cast<const char*>(0x00C16F18));
        if (slot < 1 || slot > 8) {
            int8_t menuSlot = *reinterpret_cast<const int8_t*>(0x00BA6748 + 0x15F);
            if (menuSlot >= 0 && menuSlot <= 7) slot = menuSlot + 1;
        }
        g_loadedSaveThisBoot = true;
        g_initializedInWorld = true;
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
        if (!g_initializedInWorld) {
            g_initializedInWorld = true;
            wardrobe::EnsureCatalogLoaded();
            if (!g_loadedSaveThisBoot) {
                if (sa::IsCoopModLoaded()) {
                    wardrobe::OnGameLoad(0);
                    phoneui::OnGameLoad(0);
                } else {
                    wardrobe::OnNewGame();
                    phoneui::OnNewGame();
                }
            }
        }
        if (g_lastGameState != 9 && g_lastGameState != -1) {
            clothesshop::Reset();
            phoneui::Reset();
            wardrobe::Reset();
        }
        ui::Keys().Update();
        clothesshop::PreScriptsTick();
        phoneui::PreScriptsTick();
    } else if (g_lastGameState == 9 && gs != 9) {
        clothesshop::Reset();
        phoneui::Reset();
        wardrobe::Reset();
        g_initializedInWorld = false;
        g_loadedSaveThisBoot = false;
    }
    g_lastGameState = gs;

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

void InstallAllHooks() {
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

    clothesshop::InstallHooks();
    RegisterWithBus();

    modlog::Info("[Phone] GTA5Phone v1.0 hooks & Addon Hub Bus registered (F6 Phone + Contacts + Wardrobe + Clothes Shops)!");
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        pcfg::Load(hModule);
        ui::SetUseRussian(pcfg::Get().useRussian);
        modlog::Enabled() = pcfg::Get().enableLog;
        modlog::Init(hModule, "GTA5Phone");
        InstallAllHooks();
    } else if (reason == DLL_PROCESS_DETACH) {
        clothesshop::Reset();
        phoneui::Reset();
    }
    return TRUE;
}
