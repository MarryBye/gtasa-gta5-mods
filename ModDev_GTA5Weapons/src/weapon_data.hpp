#pragma once
#include <cstdint>
#include <cmath>
#include <vector>

namespace wdata {

enum class WeaponCategory : uint8_t {
    Melee = 0,
    Pistols,
    SMGs,
    Rifles,
    Snipers,
    Heavy,
    Shotguns,
    Thrown,
    Armor
};

constexpr int kWheelCategoryCount = 8;

enum class AudioClass : uint8_t {
    None = 0,
    Pistol,
    SMG,
    Rifle,
    Shotgun,
    Sniper
};

constexpr uint32_t SUP_SUPPRESSOR = 1u << 0;
constexpr uint32_t SUP_COMPENSATOR= 1u << 1;
constexpr uint32_t SUP_EXT_MAG    = 1u << 2;
constexpr uint32_t SUP_DRUM_MAG   = 1u << 3;
constexpr uint32_t SUP_REFLEX     = 1u << 4;
constexpr uint32_t SUP_SCOPE      = 1u << 5;
constexpr uint32_t SUP_GRIP       = 1u << 6;
constexpr uint32_t SUP_HEAVY_BRL  = 1u << 7;
constexpr uint32_t SUP_LASER      = 1u << 8;

// Exact DFF-calibrated local mount profile (measured directly from models\gta3.img geometry!)
// Local weapon clump axes: +X = barrel forward, +Y = lateral, +Z = up
struct MountProfile {
    // Barrel line from receiver breech (rx,ry,rz) to muzzle tip (mx,my,mz)
    float rx, ry, rz;
    float mx, my, mz;
    float barrelRadius;
    float suppressorLen;
    // Top rail height above barrel centerline (along local +Z) & rail X position
    float scopeX;
    float scopeRailDz;
    // Foregrip mount X & underside depth below barrel centerline (negative Z offset)
    float gripX;
    float gripUnderDz;
    // Magazine mount: X center, half-width along X, top Z (receiver underside), bottom Z (covers vanilla mag!)
    float magX;
    float magHalfX;
    float magTopZ;
    float magBotZ;
    // Laser module mount X & side offset along Y
    float laserX;
    float laserSideDy;
};

struct WeaponDef {
    uint32_t       id;
    int32_t        modelId;
    uint8_t        gtaSlot;
    bool           sharesSlotAmmo;
    bool           soldInShop;
    WeaponCategory category;
    AudioClass     audioClass;
    const char*    nameRus;
    const char*    nameEng;
    int            buyPrice;
    int            initAmmo;
    int            ammoPackCount;
    int            ammoPackPrice;
    int            maxAmmo;
    float          modPriceMult;
    uint32_t       supportMask;
    MountProfile   mount;
};

constexpr MountProfile kNoMount = {};

constexpr WeaponDef kWeapons[] = {
    // --- MELEE / UNARMED ---
    {0, 0, 0, false, false, WeaponCategory::Melee, AudioClass::None,
     "Кулаки", "Unarmed", 0, 0, 0, 0, 0, 1.0f, 0, kNoMount},
    {1, 331, 0, false, true, WeaponCategory::Melee, AudioClass::None,
     "Кастет", "Brass Knuckles", 120, 1, 0, 0, 1, 1.0f, 0, kNoMount},
    {4, 335, 1, false, true, WeaponCategory::Melee, AudioClass::None,
     "Нож", "Knife", 180, 1, 0, 0, 1, 1.0f, 0, kNoMount},
    {5, 336, 1, false, true, WeaponCategory::Melee, AudioClass::None,
     "Бита", "Baseball Bat", 150, 1, 0, 0, 1, 1.0f, 0, kNoMount},
    {8, 339, 1, false, true, WeaponCategory::Melee, AudioClass::None,
     "Катана", "Katana", 650, 1, 0, 0, 1, 1.0f, 0, kNoMount},
    {9, 341, 1, false, true, WeaponCategory::Melee, AudioClass::None,
     "Бензопила", "Chainsaw", 900, 1, 0, 0, 1, 1.0f, 0, kNoMount},

    // --- PISTOLS (Slot 2, Shared Ammo) ---
    // colt45.dff Frame[1] exact axis: (+0.985, +0.065, +0.158) (+9.1 deg pitch!), muzzle=(0.240,0.039,0.108)
    {22, 346, 2, true, true, WeaponCategory::Pistols, AudioClass::Pistol,
     "9mm", "9mm Pistol", 350, 51, 34, 60, 600, 1.0f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_REFLEX | SUP_HEAVY_BRL | SUP_LASER,
     {0.023f, 0.025f, 0.073f,  0.240f, 0.039f, 0.108f,  0.013f, 0.15f,
      0.10f, 0.017f,  0.15f, -0.020f,  0.026f, 0.016f, -0.025f, -0.060f,  0.17f, 0.016f}},

    // silenced.dff Frame[1] exact axis: (+0.985, +0.065, +0.158) (+9.1 deg pitch!), muzzle=(0.360,0.046,0.133)
    {23, 347, 2, true, true, WeaponCategory::Pistols, AudioClass::Pistol,
     "9mm с глуш.", "Silenced 9mm", 650, 51, 34, 70, 600, 1.0f,
     SUP_EXT_MAG | SUP_REFLEX | SUP_HEAVY_BRL | SUP_LASER,
     {0.025f, 0.024f, 0.079f,  0.360f, 0.046f, 0.133f,  0.014f, 0.13f,
      0.11f, 0.013f,  0.17f, -0.020f,  0.026f, 0.016f, -0.025f, -0.060f,  0.18f, 0.017f}},

    // desert_eagle.dff Frame[1] exact axis: (+0.999, -0.037, -0.003), muzzle=(0.353,0.013,0.090), grip X=[0.02..0.09] Z_bot=-0.067
    {24, 348, 2, true, true, WeaponCategory::Pistols, AudioClass::Pistol,
     "Desert Eagle", "Desert Eagle", 1400, 28, 14, 120, 600, 1.15f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_REFLEX | SUP_HEAVY_BRL | SUP_LASER,
     {0.030f, 0.025f, 0.091f,  0.353f, 0.013f, 0.090f,  0.016f, 0.17f,
      0.14f, 0.030f,  0.20f, -0.025f,  0.052f, 0.018f, -0.045f, -0.072f,  0.24f, 0.019f}},

    // --- SHOTGUNS (Slot 3, Shared Ammo) ---
    // chromegun.dff Frame[1] axis: (0.981, -0.120, +0.150)
    {25, 349, 3, true, true, WeaponCategory::Shotguns, AudioClass::Shotgun,
     "Дробовик", "Pump Shotgun", 950, 24, 16, 120, 320, 1.15f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_REFLEX | SUP_GRIP | SUP_HEAVY_BRL | SUP_LASER,
     {0.10f, 0.036f, 0.112f,  0.80f, -0.048f, 0.219f,  0.017f, 0.19f,
      0.24f, 0.028f,  0.46f, -0.035f, 0.48f, 0.14f, -0.025f, -0.055f,  0.58f, 0.022f}},

    // sawnoff.dff: X=[-0.21..0.56], midY=0.00, muzz=(0.56,0.0,0.15)
    {26, 350, 3, true, true, WeaponCategory::Shotguns, AudioClass::Shotgun,
     "Обрез", "Sawnoff", 1250, 20, 12, 130, 320, 1.15f,
     SUP_EXT_MAG | SUP_HEAVY_BRL | SUP_LASER,
     {0.08f, 0.00f, 0.12f,  0.56f, 0.00f, 0.15f,  0.020f, 0.16f,
      0.18f, 0.025f,  0.30f, -0.03f,  0.32f, 0.10f, -0.025f, -0.055f,  0.38f, 0.025f}},

    // shotgspa.dff: X=[0.01..0.73], midY=0.01, muzz=(0.73,-0.035,0.19), railTopZ=0.21, hgBotZ=0.08
    {27, 351, 3, true, true, WeaponCategory::Shotguns, AudioClass::Shotgun,
     "Боевой дробовик", "Combat Shotgun", 2900, 28, 21, 200, 320, 1.25f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_DRUM_MAG | SUP_REFLEX | SUP_GRIP | SUP_HEAVY_BRL | SUP_LASER,
     {0.12f, 0.015f, 0.145f,  0.73f, -0.035f, 0.19f,  0.019f, 0.20f,
      0.24f, 0.038f,  0.44f, -0.045f, 0.28f, 0.035f, 0.04f, -0.07f,  0.56f, 0.024f}},

    // --- SMGs (Slot 4, Shared Ammo) ---
    // micro_uzi.dff Frame[1] exact axis: (+0.992, -0.087, +0.087), muzzle=(0.244,0.005,0.095), stick mag down to Z=-0.264 @ X~0.062!
    {28, 352, 4, true, true, WeaponCategory::SMGs, AudioClass::SMG,
     "Micro-SMG", "Micro-SMG", 800, 150, 100, 150, 1500, 1.10f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_DRUM_MAG | SUP_REFLEX | SUP_HEAVY_BRL | SUP_LASER,
     {-0.004f, 0.027f, 0.073f,  0.244f, 0.005f, 0.095f,  0.015f, 0.16f,
      0.08f, 0.044f,  0.15f, -0.035f,  0.062f, 0.024f, -0.055f, -0.272f,  0.17f, 0.020f}},

    // tec9.dff Frame[1] exact axis: (+0.996, -0.043, +0.072), muzzle=(0.409,0.015,0.115), mag down to Z=-0.231
    {32, 372, 4, true, true, WeaponCategory::SMGs, AudioClass::SMG,
     "Tec-9", "Tec-9", 750, 150, 100, 140, 1500, 1.10f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_DRUM_MAG | SUP_REFLEX | SUP_HEAVY_BRL | SUP_LASER,
     {0.040f, 0.031f, 0.088f,  0.409f, 0.015f, 0.115f,  0.016f, 0.16f,
      0.12f, 0.032f,  0.28f, -0.035f,  0.195f, 0.025f, 0.040f, -0.238f,  0.30f, 0.022f}},

    // mp5lng.dff Frame[1] exact axis: (+0.985, -0.121, +0.122), muzzle=(0.457,-0.028,0.195)
    {29, 353, 4, true, true, WeaponCategory::SMGs, AudioClass::SMG,
     "MP5", "MP5", 2400, 120, 90, 220, 1500, 1.25f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_DRUM_MAG | SUP_REFLEX | SUP_SCOPE | SUP_GRIP | SUP_HEAVY_BRL | SUP_LASER,
     {0.060f, 0.021f, 0.146f,  0.457f, -0.028f, 0.195f,  0.017f, 0.18f,
      0.16f, 0.044f,  0.33f, -0.040f,  0.22f, 0.028f, 0.05f, -0.095f,  0.36f, 0.022f}},

    // --- ASSAULT RIFLES (Slot 5, Shared Ammo) ---
    // ak47.dff Frame[1] exact axis: (-R) = (0.994, -0.098, +0.050), gunflash @ (0.925,-0.056,0.129) -> muzzle @ (0.775,-0.041,0.121)
    // Banana mag spans X=[0.25..0.39], Z=[0.04..-0.155] -> drum & ext mag calibrated to cover 100% of banana mag!
    {30, 355, 5, true, true, WeaponCategory::Rifles, AudioClass::Rifle,
     "AK-47", "AK-47", 4200, 120, 90, 280, 1500, 1.35f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_DRUM_MAG | SUP_REFLEX | SUP_SCOPE | SUP_GRIP | SUP_HEAVY_BRL | SUP_LASER,
     {0.10f, 0.025f, 0.087f,  0.775f, -0.041f, 0.121f,  0.015f, 0.20f,
      0.20f, 0.052f,  0.46f, -0.036f, 0.335f, 0.062f, 0.040f, -0.172f,  0.54f, 0.023f}},

    // m4.dff: calibrated and verified dead-on!
    {31, 356, 5, true, true, WeaponCategory::Rifles, AudioClass::Rifle,
     "M4", "M4", 5200, 150, 100, 320, 1500, 1.40f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_DRUM_MAG | SUP_REFLEX | SUP_SCOPE | SUP_GRIP | SUP_HEAVY_BRL | SUP_LASER,
     {0.10f, 0.005f, 0.100f,  0.73f, -0.040f, 0.112f,  0.016f, 0.20f,
      0.20f, 0.052f,  0.44f, -0.038f, 0.25f, 0.038f, 0.03f, -0.115f,  0.52f, 0.024f}},

    // --- SNIPERS (Slot 6, Shared Ammo) ---
    // cuntgun.dff Frame[1] exact axis: (0.981, -0.120, +0.150)
    {33, 357, 6, true, true, WeaponCategory::Snipers, AudioClass::Sniper,
     "Охот. винтовка", "Country Rifle", 2200, 30, 20, 180, 300, 1.30f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_REFLEX | SUP_SCOPE | SUP_HEAVY_BRL | SUP_LASER,
     {0.10f, 0.035f, 0.098f,  0.74f, -0.043f, 0.195f,  0.015f, 0.20f,
      0.24f, 0.028f,  0.44f, -0.032f, 0.26f, 0.030f, 0.06f, -0.025f,  0.52f, 0.020f}},

    // sniper.dff Frame[1] exact axis: (0.992, -0.080, +0.098), gunflash @ (0.996,-0.062,0.189) -> muzzle @ (0.745,-0.042,0.164)
    {34, 358, 6, true, true, WeaponCategory::Snipers, AudioClass::Sniper,
     "Снайперка", "Sniper Rifle", 6500, 20, 15, 350, 300, 1.45f,
     SUP_SUPPRESSOR | SUP_COMPENSATOR | SUP_EXT_MAG | SUP_HEAVY_BRL | SUP_LASER,
     {0.10f, 0.010f, 0.100f,  0.745f, -0.042f, 0.164f,  0.015f, 0.22f,
      0.24f, 0.044f,  0.46f, -0.032f, 0.27f, 0.030f, 0.075f, -0.015f,  0.54f, 0.020f}},

    // --- HEAVY (Slot 7) ---
    {35, 359, 7, false, true, WeaponCategory::Heavy, AudioClass::None,
     "РПГ", "RPG", 12000, 4, 3, 1500, 30, 1.0f, 0, kNoMount},
    {36, 360, 7, false, true, WeaponCategory::Heavy, AudioClass::None,
     "РПГ с наведением", "HS Rocket", 18000, 4, 3, 2200, 30, 1.0f, 0, kNoMount},
    {37, 361, 7, false, true, WeaponCategory::Heavy, AudioClass::None,
     "Огнемёт", "Flamethrower", 8500, 150, 100, 450, 800, 1.0f, 0, kNoMount},
    {38, 362, 7, false, true, WeaponCategory::Heavy, AudioClass::None,
     "Миниган", "Minigun", 25000, 500, 500, 1200, 3000, 1.0f, 0, kNoMount},

    // --- THROWN (Slot 8) ---
    {16, 342, 8, false, true, WeaponCategory::Thrown, AudioClass::None,
     "Граната", "Grenade", 300, 3, 3, 300, 50, 1.0f, 0, kNoMount},
    {18, 344, 8, false, true, WeaponCategory::Thrown, AudioClass::None,
     "Молотов", "Molotov", 200, 3, 3, 200, 50, 1.0f, 0, kNoMount},
    {39, 363, 8, false, true, WeaponCategory::Thrown, AudioClass::None,
     "Взрывпакет (C4)", "Satchel", 1800, 3, 3, 600, 30, 1.0f, 0, kNoMount},
};

constexpr size_t kWeaponCount = sizeof(kWeapons) / sizeof(kWeapons[0]);

inline const WeaponDef* FindDef(uint32_t weaponId) {
    for (size_t i = 0; i < kWeaponCount; ++i) {
        if (kWeapons[i].id == weaponId) return &kWeapons[i];
    }
    return nullptr;
}

struct WeaponLoadout {
    bool    owned           = false;
    uint8_t muzzle          = 0;
    uint8_t mag             = 0;
    uint8_t scope           = 0;
    uint8_t grip            = 0;
    uint8_t barrel          = 0;
    uint8_t laser           = 0;

    uint8_t ownedMuzzleMask = 1;
    uint8_t ownedMagMask    = 1;
    uint8_t ownedScopeMask  = 1;
    uint8_t ownedGripMask   = 1;
    uint8_t ownedBarrelMask = 1;
    uint8_t ownedLaserMask  = 1;

    int     storedAmmo      = 0;

    bool HasAnyMod() const {
        return muzzle != 0 || mag != 0 || scope != 0 || grip != 0 || barrel != 0 || laser != 0;
    }
};

struct WeaponStats {
    int damage   = 0;
    int fireRate = 0;
    int accuracy = 0;
    int range    = 0;
    int clipSize = 0;
};

inline int ScalePrice(int basePrice, float mult) {
    int p = static_cast<int>(std::round(static_cast<float>(basePrice) * mult));
    return (p / 50) * 50;
}

} // namespace wdata
