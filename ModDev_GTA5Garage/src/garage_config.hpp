#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <windows.h>
#include "common_utils.hpp"
#include "ui_kit.hpp"

namespace mod {

struct ModConfig {
    bool    useRussian          = true;
    bool    useMph              = true;
    int     callMenuKey         = VK_F6;
    int     maxVehicles         = 10;
    bool    enableLog           = true;

    float   spawnDistance       = 100.0f;
    float   maxRoadDistance     = 45.0f;
    bool    allowOnMission      = true;

    int     restorePercent      = 5;
    int     minRestoreCost      = 500;
    int     maxRestoreCost      = 25000;

    // Wear & Maintenance (ETS 2-style mechanical wear)
    bool    enableWearSystem    = true;
    float   wearRateMult        = 1.0f;
    float   maintCostMult       = 1.0f;
    int     maintBaseEngine     = 14000;
    int     maintBaseTrans      = 10500;
    int     maintBaseBrakes     = 7000;
    int     maintBaseTires      = 6500;
    int     maintBaseSusp       = 8500;
    int     maintBaseChassis    = 11500;

    // Dealership
    bool    enableDealership    = true;
    float   dealershipPriceMult = 1.0f;

    // Audio
    bool    enableTurboAudio    = true;

    // Prices
    int     priceRegister       = 7000;
    int     priceRepair         = 1200;
    int     pricePaint          = 1200;
    int     priceEngine[4]      = {11000, 24000, 42000, 68000};
    int     priceTrans[3]       = {9000, 20000, 38000};
    int     priceBrakes[3]      = {7500, 16500, 30000};
    int     priceTiresGrip[3]   = {6500, 15000, 27000};
    int     priceSusp[4]        = {5500, 11000, 20000, 32000};
    int     priceBulletproof    = 28000;
    int     priceTurbo          = 48000;
    int     priceArmor[5]       = {9000, 18000, 32000, 48000, 75000};
    int     pricePlate          = 3500;
    int     priceHorn           = 4500;

    std::string gameDir;
    std::string modDir;
    std::string iniPath;
};

inline ModConfig& Config() {
    static ModConfig cfg;
    return cfg;
}

inline void LoadConfig(HMODULE selfModule) {
    auto& c   = Config();
    c.gameDir = common::GetExeDir();
    c.modDir  = common::GetModuleDir(selfModule);
    c.iniPath = c.modDir + "\\GTA5GarageAndTuning.ini";

    const char* p = c.iniPath.c_str();
    std::string lang = common::IniReadString(p, "General", "Language", "rus");
    c.useRussian  = (_stricmp(lang.c_str(), "eng") != 0);
    std::string spdUnit = common::IniReadString(p, "General", "SpeedUnit", "mph");
    c.useMph      = (_stricmp(spdUnit.c_str(), "kmh") != 0);
    c.callMenuKey = common::IniReadInt(p, "General", "CallMenuKey", VK_F6);
    c.maxVehicles = std::clamp(common::IniReadInt(p, "General", "MaxVehicles", 10), 1, 10);
    c.enableLog   = common::IniReadBool(p, "General", "EnableLog", true);

    c.spawnDistance   = std::clamp(common::IniReadFloat(p, "Delivery", "SpawnDistance", 100.0f), 40.0f, 220.0f);
    c.maxRoadDistance = common::IniReadFloat(p, "Delivery", "MaxRoadDistance", 45.0f);
    c.allowOnMission  = common::IniReadBool(p, "Delivery", "AllowOnMission", true);

    c.restorePercent  = common::IniReadInt(p, "Insurance", "RestorePercent", 5);
    c.minRestoreCost  = common::IniReadInt(p, "Insurance", "MinRestoreCost", 500);
    c.maxRestoreCost  = common::IniReadInt(p, "Insurance", "MaxRestoreCost", 25000);

    c.enableWearSystem = common::IniReadBool(p, "Wear", "EnableWearSystem", true);
    c.wearRateMult     = std::max(0.05f, common::IniReadFloat(p, "Wear", "WearRateMultiplier", 1.0f));
    c.maintCostMult    = std::max(0.10f, common::IniReadFloat(p, "Wear", "MaintenanceCostMultiplier", 1.0f));
    c.maintBaseEngine  = common::IniReadInt(p, "Wear", "BaseOverhaulEngine",  14000);
    c.maintBaseTrans   = common::IniReadInt(p, "Wear", "BaseOverhaulTrans",   10500);
    c.maintBaseBrakes  = common::IniReadInt(p, "Wear", "BaseOverhaulBrakes",   7000);
    c.maintBaseTires   = common::IniReadInt(p, "Wear", "BaseOverhaulTires",    6500);
    c.maintBaseSusp    = common::IniReadInt(p, "Wear", "BaseOverhaulSusp",     8500);
    c.maintBaseChassis = common::IniReadInt(p, "Wear", "BaseOverhaulChassis", 11500);

    c.enableDealership    = common::IniReadBool(p, "Dealership", "EnableDealership", true);
    c.dealershipPriceMult = std::max(0.10f, common::IniReadFloat(p, "Dealership", "PriceMultiplier", 1.0f));

    c.enableTurboAudio = common::IniReadBool(p, "Effects", "EnableTurboAudio", true);

    c.priceRegister     = common::IniReadInt(p, "Prices", "RegisterVehicle", 7000);
    c.priceRepair       = common::IniReadInt(p, "Prices", "Repair", 1200);
    c.pricePaint        = common::IniReadInt(p, "Prices", "Paint", 1200);
    c.priceEngine[0]    = common::IniReadInt(p, "Prices", "Engine1", 11000);
    c.priceEngine[1]    = common::IniReadInt(p, "Prices", "Engine2", 24000);
    c.priceEngine[2]    = common::IniReadInt(p, "Prices", "Engine3", 42000);
    c.priceEngine[3]    = common::IniReadInt(p, "Prices", "Engine4", 68000);
    c.priceTrans[0]     = common::IniReadInt(p, "Prices", "Transmission1", 9000);
    c.priceTrans[1]     = common::IniReadInt(p, "Prices", "Transmission2", 20000);
    c.priceTrans[2]     = common::IniReadInt(p, "Prices", "Transmission3", 38000);
    c.priceBrakes[0]    = common::IniReadInt(p, "Prices", "Brakes1", 7500);
    c.priceBrakes[1]    = common::IniReadInt(p, "Prices", "Brakes2", 16500);
    c.priceBrakes[2]    = common::IniReadInt(p, "Prices", "Brakes3", 30000);
    c.priceTiresGrip[0] = common::IniReadInt(p, "Prices", "TiresGrip1", 6500);
    c.priceTiresGrip[1] = common::IniReadInt(p, "Prices", "TiresGrip2", 15000);
    c.priceTiresGrip[2] = common::IniReadInt(p, "Prices", "TiresGrip3", 27000);
    c.priceSusp[0]      = common::IniReadInt(p, "Prices", "Suspension1", 5500);
    c.priceSusp[1]      = common::IniReadInt(p, "Prices", "Suspension2", 11000);
    c.priceSusp[2]      = common::IniReadInt(p, "Prices", "Suspension3", 20000);
    c.priceSusp[3]      = common::IniReadInt(p, "Prices", "Suspension4", 32000);
    c.priceBulletproof  = common::IniReadInt(p, "Prices", "BulletproofTires", 28000);
    c.priceTurbo        = common::IniReadInt(p, "Prices", "Turbo", 48000);
    c.priceArmor[0]     = common::IniReadInt(p, "Prices", "Armor1", 9000);
    c.priceArmor[1]     = common::IniReadInt(p, "Prices", "Armor2", 18000);
    c.priceArmor[2]     = common::IniReadInt(p, "Prices", "Armor3", 32000);
    c.priceArmor[3]     = common::IniReadInt(p, "Prices", "Armor4", 48000);
    c.priceArmor[4]     = common::IniReadInt(p, "Prices", "Armor5", 75000);
    c.pricePlate        = common::IniReadInt(p, "Prices", "CustomPlate", 3500);
    c.priceHorn         = common::IniReadInt(p, "Prices", "Horn", 4500);

    ui::SetUseRussian(c.useRussian);
}

inline int ComputeRestoreCost(int32_t playerMoney) {
    const auto& c = Config();
    int64_t pct = (static_cast<int64_t>(playerMoney) * c.restorePercent) / 100;
    if (pct < c.minRestoreCost) pct = c.minRestoreCost;
    if (pct > c.maxRestoreCost) pct = c.maxRestoreCost;
    return static_cast<int>(pct);
}

} // namespace mod
