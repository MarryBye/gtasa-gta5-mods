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

    // Wear & Maintenance (ETS 2-style mechanical wear)
    bool    enableWearSystem  = true;
    float   wearRateMult      = 1.0f;
    float   maintCostMult     = 1.0f;
    int     maintBaseEngine   = 14000;
    int     maintBaseTrans    = 10500;
    int     maintBaseBrakes   = 7000;
    int     maintBaseTires    = 6500;
    int     maintBaseSusp     = 8500;
    int     maintBaseChassis  = 11500;

    // Dealership
    bool    enableDealership  = true;
    float   dealershipPriceMult = 1.0f;

    // Sound toggles
    bool    enableTurboAudio  = true;
    bool    enableEngineTone  = true;

    // Prices (rebalanced ~3.5x-4x so upgrades genuinely impact progression!)
    int     priceRegister     = 7000;
    int     priceRepair       = 1200;
    int     pricePaint        = 1200;
    int     priceEngine[4]    = {11000, 24000, 42000, 68000};
    int     priceTrans[3]     = {9000, 20000, 38000};
    int     priceBrakes[3]    = {7500, 16500, 30000};
    int     priceTiresGrip[3] = {6500, 15000, 27000};
    int     priceSusp[4]      = {5500, 11000, 20000, 32000};
    int     priceBulletproof  = 28000;
    int     priceTurbo        = 48000;
    int     priceArmor[5]     = {9000, 18000, 32000, 48000, 75000};
    int     priceXenon        = 7000;
    int     priceNeon         = 16000;
    int     pricePlate        = 3500;
    int     priceHorn         = 4500;

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

    c.enableWearSystem = GetPrivateProfileIntA("Wear", "EnableWearSystem", 1, p) != 0;
    GetPrivateProfileStringA("Wear", "WearRateMultiplier", "1.0", fbuf, sizeof(fbuf), p);
    c.wearRateMult     = std::max(0.05f, static_cast<float>(std::atof(fbuf)));
    GetPrivateProfileStringA("Wear", "MaintenanceCostMultiplier", "1.0", fbuf, sizeof(fbuf), p);
    c.maintCostMult    = std::max(0.10f, static_cast<float>(std::atof(fbuf)));
    c.maintBaseEngine  = GetPrivateProfileIntA("Wear", "BaseOverhaulEngine",  14000, p);
    c.maintBaseTrans   = GetPrivateProfileIntA("Wear", "BaseOverhaulTrans",   10500, p);
    c.maintBaseBrakes  = GetPrivateProfileIntA("Wear", "BaseOverhaulBrakes",   7000, p);
    c.maintBaseTires   = GetPrivateProfileIntA("Wear", "BaseOverhaulTires",    6500, p);
    c.maintBaseSusp    = GetPrivateProfileIntA("Wear", "BaseOverhaulSusp",     8500, p);
    c.maintBaseChassis = GetPrivateProfileIntA("Wear", "BaseOverhaulChassis", 11500, p);

    c.enableDealership = GetPrivateProfileIntA("Dealership", "EnableDealership", 1, p) != 0;
    GetPrivateProfileStringA("Dealership", "PriceMultiplier", "1.0", fbuf, sizeof(fbuf), p);
    c.dealershipPriceMult = std::max(0.10f, static_cast<float>(std::atof(fbuf)));

    c.enableTurboAudio = GetPrivateProfileIntA("Effects", "EnableTurboAudio", 1, p) != 0;
    c.enableEngineTone = GetPrivateProfileIntA("Effects", "EnableEngineTone", 1, p) != 0;

    c.priceRegister    = GetPrivateProfileIntA("Prices", "RegisterVehicle", 7000, p);
    c.priceRepair      = GetPrivateProfileIntA("Prices", "Repair", 1200, p);
    c.pricePaint       = GetPrivateProfileIntA("Prices", "Paint", 1200, p);
    c.priceEngine[0]   = GetPrivateProfileIntA("Prices", "Engine1", 11000, p);
    c.priceEngine[1]   = GetPrivateProfileIntA("Prices", "Engine2", 24000, p);
    c.priceEngine[2]   = GetPrivateProfileIntA("Prices", "Engine3", 42000, p);
    c.priceEngine[3]   = GetPrivateProfileIntA("Prices", "Engine4", 68000, p);
    c.priceTrans[0]    = GetPrivateProfileIntA("Prices", "Transmission1", 9000, p);
    c.priceTrans[1]    = GetPrivateProfileIntA("Prices", "Transmission2", 20000, p);
    c.priceTrans[2]    = GetPrivateProfileIntA("Prices", "Transmission3", 38000, p);
    c.priceBrakes[0]   = GetPrivateProfileIntA("Prices", "Brakes1", 7500, p);
    c.priceBrakes[1]   = GetPrivateProfileIntA("Prices", "Brakes2", 16500, p);
    c.priceBrakes[2]   = GetPrivateProfileIntA("Prices", "Brakes3", 30000, p);
    c.priceTiresGrip[0]= GetPrivateProfileIntA("Prices", "TiresGrip1", 6500, p);
    c.priceTiresGrip[1]= GetPrivateProfileIntA("Prices", "TiresGrip2", 15000, p);
    c.priceTiresGrip[2]= GetPrivateProfileIntA("Prices", "TiresGrip3", 27000, p);
    c.priceSusp[0]     = GetPrivateProfileIntA("Prices", "Suspension1", 5500, p);
    c.priceSusp[1]     = GetPrivateProfileIntA("Prices", "Suspension2", 11000, p);
    c.priceSusp[2]     = GetPrivateProfileIntA("Prices", "Suspension3", 20000, p);
    c.priceSusp[3]     = GetPrivateProfileIntA("Prices", "Suspension4", 32000, p);
    c.priceBulletproof = GetPrivateProfileIntA("Prices", "BulletproofTires", 28000, p);
    c.priceTurbo       = GetPrivateProfileIntA("Prices", "Turbo", 48000, p);
    c.priceArmor[0]    = GetPrivateProfileIntA("Prices", "Armor1", 9000, p);
    c.priceArmor[1]    = GetPrivateProfileIntA("Prices", "Armor2", 18000, p);
    c.priceArmor[2]    = GetPrivateProfileIntA("Prices", "Armor3", 32000, p);
    c.priceArmor[3]    = GetPrivateProfileIntA("Prices", "Armor4", 48000, p);
    c.priceArmor[4]    = GetPrivateProfileIntA("Prices", "Armor5", 75000, p);
    c.priceXenon       = GetPrivateProfileIntA("Prices", "XenonLights", 7000, p);
    c.priceNeon        = GetPrivateProfileIntA("Prices", "NeonKit", 16000, p);
    c.pricePlate       = GetPrivateProfileIntA("Prices", "CustomPlate", 3500, p);
    c.priceHorn        = GetPrivateProfileIntA("Prices", "Horn", 4500, p);
}

inline int ComputeRestoreCost(int32_t playerMoney) {
    const auto& c = Config();
    int64_t pct = (static_cast<int64_t>(playerMoney) * c.restorePercent) / 100;
    if (pct < c.minRestoreCost) pct = c.minRestoreCost;
    if (pct > c.maxRestoreCost) pct = c.maxRestoreCost;
    return static_cast<int>(pct);
}

} // namespace mod
