#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "vanilla_restyle.hpp"

extern "C" __declspec(dllexport) modbus::SharedBus* __cdecl UIKit_GetBus() {
    return modbus::GetBus();
}

namespace {

void LoadUIKitConfig(HMODULE hModule) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(hModule, buf, MAX_PATH);
    std::string full(buf);
    size_t slash = full.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";
    std::string ini = dir + "\\GTA5UIKit.ini";
    const char* p = ini.c_str();

    char lang[32] = {};
    GetPrivateProfileStringA("General", "Language", "rus", lang, sizeof(lang), p);
    bool useRus = (_stricmp(lang, "eng") != 0);

    char unit[32] = {};
    GetPrivateProfileStringA("General", "SpeedUnit", "mph", unit, sizeof(unit), p);
    bool useMph = (_stricmp(unit, "kmh") != 0);

    char scaleBuf[32] = {};
    GetPrivateProfileStringA("General", "UIScale", "1.0", scaleBuf, sizeof(scaleBuf), p);
    float uiScale = std::clamp(static_cast<float>(std::atof(scaleBuf)), 0.70f, 1.40f);
    if (uiScale < 0.70f) uiScale = 1.0f;

    bool restyleVanilla = GetPrivateProfileIntA("Features", "RestyleVanillaMenus", 1, p) != 0;
    bool restyleHelp    = GetPrivateProfileIntA("Features", "RestyleHelpBoxes",    1, p) != 0;
    bool enableLog      = GetPrivateProfileIntA("General",  "EnableLog", 1, p) != 0;

    char coopProf[48] = {};
    GetPrivateProfileStringA("Coop", "ProfileName", "", coopProf, sizeof(coopProf), p);

    modlog::Enabled() = enableLog;
    modlog::Init(hModule, "GTA5UIKit");

    ui::SetUseRussian(useRus);
    ui::Theme().uiScale = uiScale;

    if (auto* bus = modbus::GetBus()) {
        bus->uiKitInstalled      = true;
        bus->useRussian          = useRus;
        bus->useMph              = useMph;
        bus->uiScale             = uiScale;
        bus->restyleVanillaMenus = restyleVanilla;
        bus->restyleHelpBoxes    = restyleHelp;
        std::strncpy(bus->coopProfileName, coopProf, sizeof(bus->coopProfileName) - 1);
        bus->coopProfileName[sizeof(bus->coopProfileName) - 1] = '\0';
    }

    modlog::Info("GTA5UIKit v2.0 attached (Lang=%s, Unit=%s, Scale=%.2f, RestyleMenus=%d, RestyleHelp=%d, CoopProfile='%s').",
                 useRus ? "rus" : "eng", useMph ? "mph" : "kmh", uiScale,
                 restyleVanilla ? 1 : 0, restyleHelp ? 1 : 0,
                 sa::GetCoopPlayerProfileName(coopProf).c_str());
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadUIKitConfig(hModule);
        if (!sa::IsSA10US()) {
            modlog::Info("[UIKit] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
            return TRUE;
        }
        uikit_restyle::InstallHooks();
    }
    return TRUE;
}
