#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

namespace mod {

struct ModConfig {
    bool    useRussian        = true;
    bool    useMph            = true;  // Matches SA Styled Speedometer (mph or kmh)
    int     callMenuKey       = VK_F6; // 117
    int     maxVehicles       = 10;
    bool    enableLog         = true;

    float   spawnDistance     = 100.0f;
    float   maxRoadDistance   = 45.0f;
    bool    allowOnMission    = true;

    int     restorePercent    = 5;
    int     minRestoreCost    = 500;
    int     maxRestoreCost    = 25000;

    // Sound toggles
    bool    enableTurboAudio  = true;
    bool    enableEngineTone  = true;

    // Prices
    int     priceRegister     = 2000;
    int     priceRepair       = 400;
    int     pricePaint        = 300;
    int     priceEngine[4]    = {3000, 6500, 11000, 17500};
    int     priceTrans[3]     = {2500, 5500, 10000};
    int     priceBrakes[3]    = {2000, 4500, 8000};
    int     priceTiresGrip[3] = {1800, 4000, 7200};
    int     priceSusp[4]      = {1500, 3000, 5500, 8500};
    int     priceBulletproof  = 7500;
    int     priceTurbo        = 12500;
    int     priceArmor[5]     = {2500, 5000, 8500, 13000, 20000};
    int     priceXenon        = 2000;
    int     priceNeon         = 4500;
    int     pricePlate        = 1000;
    int     priceHorn         = 1200;

    std::string gameDir;
    std::string modDir;
    std::string iniPath;
};

inline ModConfig& Config() {
    static ModConfig cfg;
    return cfg;
}

inline void LoadConfig(HMODULE selfModule) {
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

    auto& c = Config();
    c.gameDir = gDir;
    c.modDir  = dir;
    c.iniPath = dir + "\\GTA5GarageAndTuning.ini";

    const char* p = c.iniPath.c_str();
    char lang[32] = {};
    GetPrivateProfileStringA("General", "Language", "rus", lang, sizeof(lang), p);
    c.useRussian  = (_stricmp(lang, "eng") != 0);
    char spdUnit[32] = {};
    GetPrivateProfileStringA("General", "SpeedUnit", "mph", spdUnit, sizeof(spdUnit), p);
    c.useMph      = (_stricmp(spdUnit, "kmh") != 0);
    c.callMenuKey = GetPrivateProfileIntA("General", "CallMenuKey", VK_F6, p);
    c.maxVehicles = GetPrivateProfileIntA("General", "MaxVehicles", 10, p);
    if (c.maxVehicles < 1) c.maxVehicles = 1;
    if (c.maxVehicles > 10) c.maxVehicles = 10;
    c.enableLog         = GetPrivateProfileIntA("General", "EnableLog", 1, p) != 0;

    char fbuf[32] = {};
    GetPrivateProfileStringA("Delivery", "SpawnDistance", "100.0", fbuf, sizeof(fbuf), p);
    c.spawnDistance = static_cast<float>(std::atof(fbuf));
    if (c.spawnDistance < 40.0f) c.spawnDistance = 40.0f;
    if (c.spawnDistance > 220.0f) c.spawnDistance = 220.0f;

    GetPrivateProfileStringA("Delivery", "MaxRoadDistance", "45.0", fbuf, sizeof(fbuf), p);
    c.maxRoadDistance = static_cast<float>(std::atof(fbuf));
    c.allowOnMission  = GetPrivateProfileIntA("Delivery", "AllowOnMission", 1, p) != 0;

    c.restorePercent  = GetPrivateProfileIntA("Insurance", "RestorePercent", 5, p);
    c.minRestoreCost  = GetPrivateProfileIntA("Insurance", "MinRestoreCost", 500, p);
    c.maxRestoreCost  = GetPrivateProfileIntA("Insurance", "MaxRestoreCost", 25000, p);

    c.enableTurboAudio = GetPrivateProfileIntA("Effects", "EnableTurboAudio", 1, p) != 0;
    c.enableEngineTone = GetPrivateProfileIntA("Effects", "EnableEngineTone", 1, p) != 0;

    c.priceRegister    = GetPrivateProfileIntA("Prices", "RegisterVehicle", 2000, p);
    c.priceRepair      = GetPrivateProfileIntA("Prices", "Repair", 400, p);
    c.pricePaint       = GetPrivateProfileIntA("Prices", "Paint", 300, p);
    c.priceEngine[0]   = GetPrivateProfileIntA("Prices", "Engine1", 3000, p);
    c.priceEngine[1]   = GetPrivateProfileIntA("Prices", "Engine2", 6500, p);
    c.priceEngine[2]   = GetPrivateProfileIntA("Prices", "Engine3", 11000, p);
    c.priceEngine[3]   = GetPrivateProfileIntA("Prices", "Engine4", 17500, p);
    c.priceTrans[0]    = GetPrivateProfileIntA("Prices", "Transmission1", 2500, p);
    c.priceTrans[1]    = GetPrivateProfileIntA("Prices", "Transmission2", 5500, p);
    c.priceTrans[2]    = GetPrivateProfileIntA("Prices", "Transmission3", 10000, p);
    c.priceBrakes[0]   = GetPrivateProfileIntA("Prices", "Brakes1", 2000, p);
    c.priceBrakes[1]   = GetPrivateProfileIntA("Prices", "Brakes2", 4500, p);
    c.priceBrakes[2]   = GetPrivateProfileIntA("Prices", "Brakes3", 8000, p);
    c.priceTiresGrip[0]= GetPrivateProfileIntA("Prices", "TiresGrip1", 1800, p);
    c.priceTiresGrip[1]= GetPrivateProfileIntA("Prices", "TiresGrip2", 4000, p);
    c.priceTiresGrip[2]= GetPrivateProfileIntA("Prices", "TiresGrip3", 7200, p);
    c.priceSusp[0]     = GetPrivateProfileIntA("Prices", "Suspension1", 1500, p);
    c.priceSusp[1]     = GetPrivateProfileIntA("Prices", "Suspension2", 3000, p);
    c.priceSusp[2]     = GetPrivateProfileIntA("Prices", "Suspension3", 5500, p);
    c.priceSusp[3]     = GetPrivateProfileIntA("Prices", "Suspension4", 8500, p);
    c.priceBulletproof = GetPrivateProfileIntA("Prices", "BulletproofTires", 7500, p);
    c.priceTurbo       = GetPrivateProfileIntA("Prices", "Turbo", 12500, p);
    c.priceArmor[0]    = GetPrivateProfileIntA("Prices", "Armor1", 2500, p);
    c.priceArmor[1]    = GetPrivateProfileIntA("Prices", "Armor2", 5000, p);
    c.priceArmor[2]    = GetPrivateProfileIntA("Prices", "Armor3", 8500, p);
    c.priceArmor[3]    = GetPrivateProfileIntA("Prices", "Armor4", 13000, p);
    c.priceArmor[4]    = GetPrivateProfileIntA("Prices", "Armor5", 20000, p);
    c.priceXenon       = GetPrivateProfileIntA("Prices", "XenonLights", 2000, p);
    c.priceNeon        = GetPrivateProfileIntA("Prices", "NeonKit", 4500, p);
    c.pricePlate       = GetPrivateProfileIntA("Prices", "CustomPlate", 1000, p);
    c.priceHorn        = GetPrivateProfileIntA("Prices", "Horn", 1200, p);
}

inline int ComputeRestoreCost(int32_t playerMoney) {
    const auto& c = Config();
    int64_t pct = (static_cast<int64_t>(playerMoney) * c.restorePercent) / 100;
    if (pct < c.minRestoreCost) pct = c.minRestoreCost;
    if (pct > c.maxRestoreCost) pct = c.maxRestoreCost;
    return static_cast<int>(pct);
}

} // namespace mod
