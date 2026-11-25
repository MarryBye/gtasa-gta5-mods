#pragma once
#include <cstdlib>
#include <string>
#include <windows.h>
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
    int   wheelKey            = VK_TAB; // 9 = TAB (hold for detailed wheel; Mouse Wheel also opens!)

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
    char exeBuf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    std::string exeFull(exeBuf);
    size_t exeSlash = exeFull.find_last_of("\\/");
    std::string gDir = (exeSlash != std::string::npos) ? exeFull.substr(0, exeSlash) : ".";

    char buf[MAX_PATH] = {};
    GetModuleFileNameA(selfModule, buf, MAX_PATH);
    std::string full(buf);
    size_t slash = full.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";

    auto& c = Get();
    c.gameDir = gDir;
    c.modDir  = dir;
    c.iniPath = dir + "\\GTA5AmmuNation.ini";

    const char* p = c.iniPath.c_str();
    char lang[32] = {};
    GetPrivateProfileStringA("General", "Language", "rus", lang, sizeof(lang), p);
    c.useRussian = (_stricmp(lang, "eng") != 0);
    c.enableLog  = GetPrivateProfileIntA("General", "EnableLog", 1, p) != 0;

    c.enable3DAttachments = GetPrivateProfileIntA("Features", "Enable3DAttachments", 0, p) != 0;
    c.enableLaserSight    = GetPrivateProfileIntA("Features", "EnableLaserSight", 1, p) != 0;
    c.enableScopeZoom     = GetPrivateProfileIntA("Features", "EnableScopeZoom", 1, p) != 0;
    c.enableWeaponAudio   = GetPrivateProfileIntA("Features", "EnableWeaponAudio", 1, p) != 0;

    char fbuf[32] = {};
    GetPrivateProfileStringA("Features", "AudioVolume", "0.65", fbuf, sizeof(fbuf), p);
    c.audioVolume = static_cast<float>(std::atof(fbuf));
    if (c.audioVolume < 0.1f) c.audioVolume = 0.1f;
    if (c.audioVolume > 1.0f) c.audioVolume = 1.0f;

    c.enableWeaponWheel = GetPrivateProfileIntA("WeaponWheel", "EnableWeaponWheel", 1, p) != 0;
    c.wheelKey          = GetPrivateProfileIntA("WeaponWheel", "WheelKey", VK_TAB, p);

    c.priceSuppressor    = GetPrivateProfileIntA("Prices", "Suppressor",    2200, p);
    c.priceCompensator   = GetPrivateProfileIntA("Prices", "Compensator",   1800, p);
    c.priceExtendedMag   = GetPrivateProfileIntA("Prices", "ExtendedMag",   1600, p);
    c.priceDrumMag       = GetPrivateProfileIntA("Prices", "DrumMag",       3200, p);
    c.priceReflexSight   = GetPrivateProfileIntA("Prices", "ReflexSight",   1900, p);
    c.priceTacticalScope = GetPrivateProfileIntA("Prices", "TacticalScope", 3400, p);
    c.priceVerticalGrip  = GetPrivateProfileIntA("Prices", "VerticalGrip",  1500, p);
    c.priceHeavyBarrel   = GetPrivateProfileIntA("Prices", "HeavyBarrel",   2800, p);
    c.priceLaserRed      = GetPrivateProfileIntA("Prices", "LaserRed",      1400, p);
    c.priceLaserGreen    = GetPrivateProfileIntA("Prices", "LaserGreen",    1800, p);

    ui::SetUseRussian(c.useRussian);
}

} // namespace wcfg
