#include <algorithm>
#include <cstring>
#include <string>
#include <windows.h>
#include "common_utils.hpp"
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "vanilla_restyle.hpp"

namespace uikit {

namespace {

void LoadConfig(HMODULE hModule) {
    const std::string ini = common::GetModuleDir(hModule) + "\\GTA5UIKit.ini";
    const char* p = ini.c_str();

    std::string lang = common::IniReadString(p, "General", "Language", "rus");
    bool useRus = (_stricmp(lang.c_str(), "eng") != 0);

    std::string unit = common::IniReadString(p, "General", "SpeedUnit", "mph");
    bool useMph = (_stricmp(unit.c_str(), "kmh") != 0);

    float rawScale = common::IniReadFloat(p, "General", "UIScale", 1.0f);
    if (rawScale <= 0.0f) rawScale = 1.0f;
    float uiScale = std::clamp(rawScale, 0.70f, 1.40f);

    bool restyleVanilla = common::IniReadBool(p, "Features", "RestyleVanillaMenus", true);
    bool restyleHelp    = common::IniReadBool(p, "Features", "RestyleHelpBoxes",    true);
    bool enableLog      = common::IniReadBool(p, "General",  "EnableLog",           true);
    std::string coopProf = common::IniReadString(p, "Coop", "ProfileName", "");

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
        std::strncpy(bus->coopProfileName, coopProf.c_str(), sizeof(bus->coopProfileName) - 1);
        bus->coopProfileName[sizeof(bus->coopProfileName) - 1] = '\0';
    }

    modlog::Info("GTA5UIKit attached (Lang=%s, Unit=%s, Scale=%.2f, RestyleMenus=%d, RestyleHelp=%d, CoopProfile='%s').",
                 useRus ? "rus" : "eng", useMph ? "mph" : "kmh", uiScale,
                 restyleVanilla ? 1 : 0, restyleHelp ? 1 : 0,
                 integrations::sacoop::GetPlayerProfileName(coopProf.c_str()).c_str());
}

} // namespace

void Init(HMODULE hModule) {
    LoadConfig(hModule);
    if (!sa::IsSA10US()) {
        modlog::Info("[UIKit] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
        return;
    }
    uikit_restyle::InstallHooks();
}

void Shutdown() {
    if (auto* bus = modbus::GetBus()) {
        bus->vanillaMenuOpen    = false;
        bus->storyPhonePriority = false;
    }
}

} // namespace uikit

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        uikit::Init(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        uikit::Shutdown();
    }
    return TRUE;
}
