#include <cstring>
#include <windows.h>
#include "ammu_shop.hpp"
#include "config.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "weapon_audio.hpp"
#include "weapon_mods.hpp"
#include "weapon_storage.hpp"
#include "weapon_visuals.hpp"
#include "weapon_wheel.hpp"

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

void __cdecl HookedMakeValidSaveName(int32_t slot0to7) {
    if (slot0to7 >= 0 && slot0to7 <= 7) {
        g_pendingSaveSlot1to8 = slot0to7 + 1;
    }
    if (g_origMakeSaveName) g_origMakeSaveName(slot0to7);
}

bool __cdecl HookedGenericSave(int32_t arg) {
    bool ok = g_origGenericSave ? g_origGenericSave(arg) : false;
    if (ok) {
        int slot = g_pendingSaveSlot1to8;
        if (slot < 1 || slot > 8) {
            slot = sa::ParseSaveSlotFromPath(reinterpret_cast<const char*>(0x00C16F18));
        }
        wstore::Store().SyncFromPlayerInventory(sa::FindPlayerPed(0));
        modlog::Info("[SaveHook] GenericSave -> saving weapon arsenal for slot %d", slot);
        wstore::Store().SaveToSlot(slot);
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
        ammu::Reset();
        wheel::Reset();
        wmods::RestoreAllStock();
        wstore::Store().SetSlotAndLoad(slot);
        modlog::Info("[SaveHook] GenericLoad -> loaded weapon arsenal for slot %d", slot);
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    const int gs = sa::GetGameState();
    if (gs == 9) {
        if (!g_initializedInWorld) {
            g_initializedInWorld = true;
            if (!g_loadedSaveThisBoot) {
                // Singleplayer New Game ALWAYS starts completely clean!
                // Only load shared slot-0 profile if SA-COOP is loaded without a local save.
                if (sa::IsCoopModLoaded()) {
                    wstore::Store().SetSlotAndLoad(0);
                } else {
                    wstore::Store().StartFreshNewGame();
                }
            }
        }
        if (g_lastGameState != 9 && g_lastGameState != -1) {
            ammu::Reset();
            wheel::Reset();
            wmods::RestoreAllStock();
        }
        ui::Keys().Update();
        ammu::PreScriptsTick();
        wheel::PreScriptsTick();
    } else if (g_lastGameState == 9 && gs != 9) {
        ammu::Reset();
        wheel::Reset();
        wmods::RestoreAllStock();
        g_initializedInWorld = false;
        g_loadedSaveThisBoot = false;
    }
    g_lastGameState = gs;

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

void InstallAllHooks() {
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

    ammu::InstallHooks();
    wvis::InstallHooks();
    waudio::InstallHooks();

    modlog::Info("[Init] All GTA5AmmuNation v1.1 hooks installed cleanly (Wheel + Multi-Weapon + 3D Attachments + Audio)!");
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        wcfg::Load(hModule);
        modlog::Enabled() = wcfg::Get().enableLog;
        modlog::Init(hModule, "GTA5AmmuNation");
        modlog::Info("GTA5AmmuNation v1.1 attached (Shared UI Kit, Weapon Wheel, 3D Attachments, Audio, Scope Zoom).");
        InstallAllHooks();
    } else if (reason == DLL_PROCESS_DETACH) {
        wheel::Reset();
        wmods::RestoreAllStock();
        modlog::Info("GTA5AmmuNation detached.");
    }
    return TRUE;
}
