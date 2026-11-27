#pragma once
#include <algorithm>
#include <string>
#include <windows.h>
#include "common_utils.hpp"
#include "ui_kit.hpp"

namespace wcfg {

struct ConfigData {
    bool  useRussian          = true;
    bool  enableLog           = true;
    bool  enable3DAttachments = false; // Optional visual 3D attachments on weapons (OFF by default)
    bool  enableLaserSight    = true;
    bool  enableScopeZoom     = true;
    bool  enableWeaponAudio   = true;
    float audioVolume         = 0.65f;

    bool  enableWeaponWheel   = true;
    int   wheelKey            = VK_TAB; // 9 = TAB (hold for wheel; Mouse Wheel also opens)

    int priceSuppressor    = 2200;
    int priceCompensator   = 1800;
    int priceExtendedMag   = 1600;
    int priceDrumMag       = 3200;
    int priceReflexSight   = 1900;
    int priceTacticalScope = 3400;
    int priceVerticalGrip  = 1500;
    int priceHeavyBarrel   = 2800;
    int priceLaserRed      = 1400;
    int priceLaserGreen    = 1800;

    std::string gameDir;
    std::string modDir;
    std::string iniPath;
};

inline ConfigData& Get() {
    static ConfigData cfg;
    return cfg;
}

inline void Load(HMODULE selfModule) {
    auto& c   = Get();
    c.gameDir = common::GetExeDir();
    c.modDir  = common::GetModuleDir(selfModule);
    c.iniPath = c.modDir + "\\GTA5AmmuNation.ini";

    const char* p = c.iniPath.c_str();
    std::string lang = common::IniReadString(p, "General", "Language", "rus");
    c.useRussian = (_stricmp(lang.c_str(), "eng") != 0);
    c.enableLog  = common::IniReadBool(p, "General", "EnableLog", true);

    c.enable3DAttachments = common::IniReadBool(p, "Features", "Enable3DAttachments", false);
    c.enableLaserSight    = common::IniReadBool(p, "Features", "EnableLaserSight", true);
    c.enableScopeZoom     = common::IniReadBool(p, "Features", "EnableScopeZoom", true);
    c.enableWeaponAudio   = common::IniReadBool(p, "Features", "EnableWeaponAudio", true);
    c.audioVolume         = std::clamp(common::IniReadFloat(p, "Features", "AudioVolume", 0.65f), 0.1f, 1.0f);

    c.enableWeaponWheel = common::IniReadBool(p, "WeaponWheel", "EnableWeaponWheel", true);
    c.wheelKey          = common::IniReadInt(p, "WeaponWheel", "WheelKey", VK_TAB);

    c.priceSuppressor    = common::IniReadInt(p, "Prices", "Suppressor",    2200);
    c.priceCompensator   = common::IniReadInt(p, "Prices", "Compensator",   1800);
    c.priceExtendedMag   = common::IniReadInt(p, "Prices", "ExtendedMag",   1600);
    c.priceDrumMag       = common::IniReadInt(p, "Prices", "DrumMag",       3200);
    c.priceReflexSight   = common::IniReadInt(p, "Prices", "ReflexSight",   1900);
    c.priceTacticalScope = common::IniReadInt(p, "Prices", "TacticalScope", 3400);
    c.priceVerticalGrip  = common::IniReadInt(p, "Prices", "VerticalGrip",  1500);
    c.priceHeavyBarrel   = common::IniReadInt(p, "Prices", "HeavyBarrel",   2800);
    c.priceLaserRed      = common::IniReadInt(p, "Prices", "LaserRed",      1400);
    c.priceLaserGreen    = common::IniReadInt(p, "Prices", "LaserGreen",    1800);

    ui::SetUseRussian(c.useRussian);
}

} // namespace wcfg
