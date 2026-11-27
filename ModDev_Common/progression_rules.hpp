#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

// ============================================================================
// Shared GTA Online-Style Character Progression & Unlock Rules (Single Source)
// Pure stateless helpers used by GTA5Progression and every shop/tuning module
// so all unlocks stay 100% consistent now and after merging into one super-mod!
// ============================================================================
namespace prog_rules {

constexpr int MAX_UNLOCK_RANK = 25; // Every gameplay unlock is earned by Rank 25 (non-grindy!)
constexpr int MAX_RANK        = 50; // Soft prestige cap beyond Rank 25

struct RankProgress {
    int  rank       = 1;
    int  totalXp    = 0;
    int  levelXp    = 0;   // XP collected within the current rank
    int  nextNeed   = 200; // Total XP span required for the next rank
    bool isMaxRank  = false;
};

// Smooth, non-grindy XP curve:
//   Rank 1->2:  200 XP   | Rank 5:  1,520 XP  | Rank 10: 6,120 XP
//   Rank 15: 13,720 XP   | Rank 20: 24,320 XP | Rank 25: 37,920 XP
inline int XpSpanForRank(int rank) {
    if (rank < 1) rank = 1;
    if (rank >= MAX_RANK) return 0;
    if (rank < MAX_UNLOCK_RANK) {
        return 200 + (rank - 1) * 120;
    }
    return 3200;
}

inline int TotalXpForRank(int targetRank) {
    int clamped = std::clamp(targetRank, 1, MAX_RANK);
    int total   = 0;
    for (int r = 1; r < clamped; ++r) {
        total += XpSpanForRank(r);
    }
    return total;
}

inline RankProgress ComputeProgress(int totalXp) {
    RankProgress out{};
    if (totalXp < 0) totalXp = 0;
    out.totalXp = totalXp;

    int rem  = totalXp;
    int rank = 1;
    while (rank < MAX_RANK) {
        int span = XpSpanForRank(rank);
        if (rem < span) {
            out.rank      = rank;
            out.levelXp   = rem;
            out.nextNeed  = span;
            out.isMaxRank = false;
            return out;
        }
        rem -= span;
        ++rank;
    }

    out.rank      = MAX_RANK;
    out.levelXp   = 0;
    out.nextNeed  = 0;
    out.isMaxRank = true;
    return out;
}

// ============================================================================
// 1. Workshop Performance & Visual Upgrade Ranks (GTA5GarageAndTuning)
// ============================================================================
inline int EngineUpgradeRank(int level) {
    static constexpr int kTbl[5] = {1, 2, 6, 12, 18};
    return (level >= 0 && level <= 4) ? kTbl[level] : 1;
}

inline int TransmissionUpgradeRank(int level) {
    static constexpr int kTbl[4] = {1, 3, 8, 15};
    return (level >= 0 && level <= 3) ? kTbl[level] : 1;
}

inline int SuspensionUpgradeRank(int level) {
    static constexpr int kTbl[5] = {1, 3, 6, 11, 16};
    return (level >= 0 && level <= 4) ? kTbl[level] : 1;
}

inline int BrakesUpgradeRank(int level) {
    static constexpr int kTbl[4] = {1, 2, 7, 13};
    return (level >= 0 && level <= 3) ? kTbl[level] : 1;
}

inline int TiresGripUpgradeRank(int level) {
    static constexpr int kTbl[4] = {1, 2, 8, 14};
    return (level >= 0 && level <= 3) ? kTbl[level] : 1;
}

inline int BulletproofTiresRank() { return 17; }
inline int TurboUpgradeRank()     { return 14; }
inline int CustomPlateRank()      { return 4; }
inline int CustomHornRank()       { return 5; }
inline int PaintjobRank()         { return 5; }
inline int HydraulicsRank()       { return 7; }
inline int BodyKitRank()          { return 3; }

inline int NitroUpgradeRank(int16_t upgradeModelId) {
    if (upgradeModelId == 1009) return 6;  // 2x Nitro
    if (upgradeModelId == 1008) return 11; // 5x Nitro
    if (upgradeModelId == 1010) return 16; // 10x Nitro
    return 1;
}

inline int VehicleArmorUpgradeRank(int level) {
    static constexpr int kTbl[6] = {1, 3, 7, 12, 17, 22};
    return (level >= 0 && level <= 5) ? kTbl[level] : 1;
}

// ============================================================================
// 2. Dealership Vehicle Ranks (classIdx matches dealership::VehClass 0..9)
// ============================================================================
inline int DealershipVehicleRank(int classIdx, int32_t modelId, int basePrice) {
    // Signature endgame military hardware
    if (modelId == 432 || modelId == 520) return 25; // Rhino Tank, Hydra Jet
    if (modelId == 425)                   return 24; // Hunter Gunship

    switch (classIdx) {
        case 0: // Supercars
            if (basePrice >= 600000) return 23;
            if (basePrice >= 380000) return 20;
            return 16;
        case 1: // Sports & Tuners
            if (basePrice >= 200000) return 14;
            if (basePrice >= 150000) return 10;
            return 5;
        case 2: // Muscle & Lowriders
            if (basePrice >= 200000) return 15;
            if (basePrice >= 130000) return 10;
            if (basePrice >=  90000) return 6;
            return 2;
        case 3: // Sedans & Coupes
            if (basePrice >= 220000) return 14;
            if (basePrice >= 150000) return 10;
            if (basePrice >=  72000) return 4;
            return 1;
        case 4: // SUVs & Off-Road
            if (basePrice >= 350000) return 20;
            if (basePrice >= 220000) return 13;
            if (basePrice >= 110000) return 7;
            return 2;
        case 5: // Bikes & Quads
            if (basePrice >= 140000) return 16; // NRG-500
            if (basePrice >= 100000) return 11; // FCR-900
            if (basePrice >=  82000) return 7;  // PCJ-600
            if (basePrice >=  68000) return 4;  // BF-400 / Freeway / Wayfarer
            return 1;
        case 6: // Vans & Trucks
            if (basePrice >= 230000) return 18;
            if (basePrice >= 175000) return 12;
            if (basePrice >=  95000) return 6;
            return 1;
        case 7: // Special & Tactical
            if (basePrice >= 340000) return 20;
            if (basePrice >= 260000) return 16;
            if (basePrice >= 150000) return 10;
            return 1;
        case 8: // Boats & Yachts
            if (basePrice >= 360000) return 16;
            if (basePrice >= 200000) return 10;
            return 5;
        case 9: // Aircraft & Helis
            if (basePrice >= 1100000) return 22;
            if (basePrice >=  680000) return 19;
            if (basePrice >=  500000) return 16;
            return 12;
        default:
            return 1;
    }
}

// ============================================================================
// 3. Ammu-Nation Weapons, Attachments & Body Armor Ranks
// ============================================================================
inline int WeaponUnlockRank(uint32_t weaponId) {
    switch (weaponId) {
        // Starter Tier (Rank 1)
        case 0: case 1: case 2: case 3: case 4: case 5:
        case 6: case 7: case 14: case 15: case 22: case 18: return 1;
        // Street Tier
        case 23: case 28: case 32:                        return 2;  // Silenced 9mm, Micro-SMG, Tec-9
        case 25: case 16:                                 return 4;  // Pump Shotgun, Grenade
        case 26: case 8:                                  return 6;  // Sawnoff, Katana
        case 24: case 29:                                 return 8;  // Desert Eagle, MP5
        case 33: case 9:                                  return 10; // Country Rifle, Chainsaw
        // Tactical & Military Tier
        case 30: case 39:                                 return 12; // AK-47, Satchel
        case 31: case 27:                                 return 15; // M4, Combat Shotgun
        case 34:                                          return 18; // Sniper Rifle
        case 37:                                          return 20; // Flamethrower
        case 35:                                          return 22; // RPG
        case 36:                                          return 24; // Heat-Seeking Rocket
        case 38:                                          return 25; // Minigun
        default:                                          return 1;
    }
}

// modSlotType: 0=Muzzle, 1=Mag, 2=Scope, 3=Grip, 4=Barrel, 5=Laser
inline int WeaponAttachmentUnlockRank(int modSlotType, uint8_t optVal) {
    if (optVal == 0) return 1; // Stock / Remove is always unlocked
    switch (modSlotType) {
        case 0: return (optVal == 1) ? 3  : 7;  // Suppressor (3), Compensator (7)
        case 1: return (optVal == 1) ? 4  : 16; // Extended Mag (4), Drum Mag (16)
        case 2: return (optVal == 1) ? 5  : 12; // Reflex Sight (5), Tactical Scope (12)
        case 3: return 10;                      // Tactical Foregrip (10)
        case 4: return 13;                      // Heavy Barrel (13)
        case 5: return (optVal == 1) ? 8  : 15; // Red Laser (8), Green Laser (15)
        default: return 1;
    }
}

inline int AmmuEquipArmorRank(int idx) {
    return (idx == 0) ? 1 : 8; // 50% Vest (1), 100% Heavy Vest (8)
}

inline int SpareArmorRank(int tier) {
    static constexpr int kTbl[4] = {1, 5, 11, 18}; // 25%, 50%, 75%, 100%
    return (tier >= 0 && tier < 4) ? kTbl[tier] : 1;
}

// ============================================================================
// 4. Pocket Food & Takeaway Restaurant Ranks (GTA5Inventory)
//    0=PsQs, 1=Sprunk, 2=Meteorite, 3=EgoChaser, 4=Salad, 5=Burger, 6=Chicken, 7=Pizza
// ============================================================================
inline int FoodItemRank(int foodIdx) {
    static constexpr int kTbl[8] = {1, 1, 3, 5, 11, 7, 7, 7};
    return (foodIdx >= 0 && foodIdx < 8) ? kTbl[foodIdx] : 1;
}

// ============================================================================
// 5. Clothing Shop Chain & Luxury Apparel Ranks (GTA5Phone / ClothesShop)
// ============================================================================
inline int ClothItemRank(const char* shopKey, int price) {
    if (!shopKey || !shopKey[0]) return 1;
    if (_stricmp(shopKey, "CSchp") == 0)   return 1;                        // Binco (always Rank 1, mission-safe!)
    if (_stricmp(shopKey, "LACS1") == 0)   return (price >= 100)  ? 3  : 1; // Sub Urban
    if (_stricmp(shopKey, "CSsprt") == 0)  return (price >= 120)  ? 6  : 4; // ProLaps
    if (_stricmp(shopKey, "clothgp") == 0) return (price >= 250)  ? 10 : 8; // ZIP
    if (_stricmp(shopKey, "Csdesgn") == 0) return (price >= 600)  ? 16 : 13;// Victim
    if (_stricmp(shopKey, "Csexl") == 0)   return (price >= 1500) ? 22 : 19;// Didier Sachs
    return 1;
}

// ============================================================================
// 6. Concise Unlock Milestones Table (Displayed on Level Up & Phone Unlocks!)
// ============================================================================
struct UnlockMilestone {
    int         rank;
    const char* rus;
    const char* eng;
};

constexpr UnlockMilestone kMilestones[] = {
    { 2, "Двигатель 1, тормоза, шины, пистолет с глуш., ПП",      "Engine 1, Brakes, Tires, Silenced 9mm, SMGs"},
    { 3, "Трансмиссия 1, подвеска 1, броня 20%, обвесы, глуш.",   "Street Trans, Susp 1, Armor 20%, Bodykits, Suppressor"},
    { 4, "Дробовик, гранаты, увел. магазин, ProLaps, номера",     "Shotgun, Grenades, Ext. Mag, ProLaps, Plates"},
    { 5, "Коллиматор, броня 50% в запас, EgoChaser, спорт-авто",  "Reflex Sight, Spare 50% Armor, EgoChaser, Sports"},
    { 6, "Двигатель 2, подвеска 2, обрез, катана, нитро 2x",      "Engine 2, Street Susp, Sawnoff, Katana, Nitro 2x"},
    { 7, "Тормоза 2, броня авто 40%, гидравлика, бургеры/пицца",  "Sport Brakes, Car Armor 40%, Hydraulics, Hot Meals"},
    { 8, "Desert Eagle, MP5, броня 100%, ZIP, трансмиссия 2",     "Desert Eagle, MP5, 100% Vest, ZIP, Sport Trans"},
    {10, "Рукоять, винтовка, бензопила, полиц. авто, лодки",      "Foregrip, Rifle, Chainsaw, Police Cars, Boats"},
    {11, "Запасная броня 75%, здоровый салат, подвеска 3, FCR",   "Spare 75% Armor, Healthy Salad, Sport Susp, FCR-900"},
    {12, "AK-47, оптика, двигатель 3, броня авто 60%, авиация",   "AK-47, Scope, Engine 3, Car Armor 60%, Light Aircraft"},
    {13, "Усиленный ствол, гоночные тормоза, одежда Victim",      "Heavy Barrel, Race Brakes, Victim Apparel"},
    {14, "Турбонаддув, гоночные шины, Elegy, Sultan, Jester",     "Turbocharger, Race Slicks, Elegy, Sultan, Jester"},
    {15, "M4, боевой дробовик, гоночная трансмиссия, зел. ЛЦУ",   "M4, Combat Shotgun, Race Trans, Green Laser"},
    {16, "Суперкары, NRG-500, барабанный маг., подвеска 4",       "Supercars, NRG-500, Drum Mag, Comp. Suspension"},
    {17, "Пулестойкие шины, броня авто 80%",                      "Bulletproof Tires, Vehicle Armor 80%"},
    {18, "Снайперка, двигатель 4, запасная броня 100%",           "Sniper Rifle, Engine 4, Spare 100% Armor"},
    {19, "Одежда Didier Sachs, тяжёлые вертолёты",                "Didier Sachs Luxury, Heavy Helicopters"},
    {20, "Огнемёт, Cheetah, Bullet, Turismo, Infernus, SWAT",     "Flamethrower, Top Supercars, SWAT"},
    {22, "РПГ, броня авто 100%, элитный Didier Sachs, Shamal",    "RPG, Car Armor 100%, Elite Didier Sachs, Shamal"},
    {23, "Гоночные суперкары Hotring Racer",                      "Hotring Racer Supercars"},
    {24, "РПГ с наведением, боевой вертолёт Hunter",              "Heat-Seeking Rocket, Hunter Gunship"},
    {25, "Миниган, танк Rhino, истребитель Hydra",                "Minigun, Rhino Tank, Hydra Jet"},
};
constexpr size_t kMilestoneCount = sizeof(kMilestones) / sizeof(kMilestones[0]);

inline const UnlockMilestone* FindMilestoneForRank(int rank) {
    for (size_t i = 0; i < kMilestoneCount; ++i) {
        if (kMilestones[i].rank == rank) return &kMilestones[i];
    }
    return nullptr;
}

} // namespace prog_rules
