#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"

namespace inv {

constexpr int FOOD_COUNT  = 8;
constexpr int ARMOR_COUNT = 4;

struct FoodDef {
    const char* key;
    const char* rus;
    const char* eng;
    int         healHp;
    float       fatGain;
    int         defaultWeight;
    int         defaultPrice;
    uint8_t     shopMask; // Bit0=All, Bit1=BurgerShot, Bit2=Pizza, Bit3=CluckinBell
};

constexpr uint8_t SHOP_ALL    = 0x01;
constexpr uint8_t SHOP_BURGER = 0x02;
constexpr uint8_t SHOP_PIZZA  = 0x04;
constexpr uint8_t SHOP_CLUCK  = 0x08;

constexpr FoodDef kFoodDefs[FOOD_COUNT] = {
    {"PsQs",      "Конфеты P's & Q's",  "P's & Q's",       8,   8.0f, 1,  15, SHOP_ALL},
    {"Sprunk",    "Напиток Sprunk",     "Sprunk Can",     10,   6.0f, 1,  20, SHOP_ALL},
    {"Meteorite", "Батончик Meteorite", "Meteorite Bar",  15,  14.0f, 2,  30, SHOP_ALL},
    {"EgoChaser", "Шоколад EgoChaser",  "EgoChaser",      22,  20.0f, 2,  45, SHOP_ALL},
    {"Salad",     "Здоровый салат",     "Healthy Salad",  55,   0.0f, 3, 180, SHOP_ALL},
    {"Burger",    "Бургер",             "Bleeder Burger", 45,  35.0f, 4, 120, SHOP_BURGER},
    {"Chicken",   "Курица",             "Cluckin' Meal",  48,  32.0f, 4, 130, SHOP_CLUCK},
    {"Pizza",     "Пицца",              "Pizza Box",      50,  40.0f, 4, 140, SHOP_PIZZA},
};

struct ArmorDef {
    const char* key;
    const char* rus;
    const char* eng;
    float       armorValue;
    int         defaultWeight;
    int         defaultPrice;
};

constexpr ArmorDef kArmorDefs[ARMOR_COUNT] = {
    {"LightArmor",      "Лёгкий бронежилет 25%",       "Light Armor 25%",        25.0f,  2,  350},
    {"StandardArmor",   "Средний бронежилет 50%",      "Standard Armor 50%",     50.0f,  4,  700},
    {"HeavyArmor",      "Тяжёлый бронежилет 75%",      "Heavy Armor 75%",        75.0f,  7, 1200},
    {"SuperHeavyArmor", "Сверхтяжёлый бронежилет 100%","Super Heavy Armor 100%",100.0f, 10, 1800},
};

struct InvConfig {
    bool useRussian                = true;
    int  openKey                   = VK_F7;
    bool allowArmorInFoodShops     = false;
    bool enableLog                 = true;
    float fatGainMultiplier        = 1.0f;
    int  maxFoodCapacity           = 20;
    int  maxArmorCapacity          = 10;
    int  foodWeight[FOOD_COUNT]    = {1, 1, 2, 2, 3, 4, 4, 4};
    int  foodPrice[FOOD_COUNT]     = {15, 20, 30, 45, 180, 120, 130, 140};
    int  armorWeight[ARMOR_COUNT]  = {2, 4, 7, 10};
    int  armorPrice[ARMOR_COUNT]   = {350, 700, 1200, 1800};
    std::string modDir;
};

inline InvConfig& Cfg() {
    static InvConfig c;
    return c;
}

inline void LoadConfig(HMODULE hModule) {
    auto& c = Cfg();
    c.modDir = common::GetModuleDir(hModule);
    const std::string ini = c.modDir + "\\GTA5Inventory.ini";
    const char* p = ini.c_str();

    std::string lang = common::IniReadString(p, "General", "Language", "rus");
    c.useRussian            = (_stricmp(lang.c_str(), "eng") != 0);
    c.openKey               = common::IniReadInt(p,  "General", "OpenKey",               VK_F7);
    c.allowArmorInFoodShops = common::IniReadBool(p, "General", "AllowArmorInFoodShops", false);
    c.enableLog             = common::IniReadBool(p, "General", "EnableLog",             true);
    c.fatGainMultiplier     = std::clamp<float>(common::IniReadFloat(p, "General", "FatGainMultiplier", 1.0f), 0.0f, 5.0f);
    c.maxFoodCapacity       = std::clamp<int>(common::IniReadInt(p, "General", "MaxFoodCapacity",  20), 5, 200);
    c.maxArmorCapacity      = std::clamp<int>(common::IniReadInt(p, "General", "MaxArmorCapacity", 10), 4, 100);

    for (int i = 0; i < FOOD_COUNT; ++i) {
        c.foodWeight[i] = std::clamp<int>(common::IniReadInt(p, "FoodWeights", kFoodDefs[i].key, kFoodDefs[i].defaultWeight), 1, c.maxFoodCapacity);
        c.foodPrice[i]  = std::max<int>(1, common::IniReadInt(p, "FoodPrices", kFoodDefs[i].key, kFoodDefs[i].defaultPrice));
    }
    for (int i = 0; i < ARMOR_COUNT; ++i) {
        c.armorWeight[i] = std::clamp<int>(common::IniReadInt(p, "ArmorWeights", kArmorDefs[i].key, kArmorDefs[i].defaultWeight), 1, c.maxArmorCapacity);
        c.armorPrice[i]  = std::max<int>(50, common::IniReadInt(p, "ArmorPrices", kArmorDefs[i].key, kArmorDefs[i].defaultPrice));
    }
}

class InventoryStore {
public:
    int food[FOOD_COUNT]   = {};
    int armor[ARMOR_COUNT] = {};

    int GetTotalFood() const {
        int s = 0;
        for (int i = 0; i < FOOD_COUNT; ++i) s += food[i];
        return s;
    }

    int GetTotalArmor() const {
        int s = 0;
        for (int i = 0; i < ARMOR_COUNT; ++i) s += armor[i];
        return s;
    }

    int GetUsedFoodCapacity() const {
        int used = 0;
        for (int i = 0; i < FOOD_COUNT; ++i) used += food[i] * Cfg().foodWeight[i];
        return used;
    }

    int GetFreeFoodCapacity() const {
        return std::max(0, Cfg().maxFoodCapacity - GetUsedFoodCapacity());
    }

    int GetUsedArmorCapacity() const {
        int used = 0;
        for (int i = 0; i < ARMOR_COUNT; ++i) used += armor[i] * Cfg().armorWeight[i];
        return used;
    }

    int GetFreeArmorCapacity() const {
        return std::max(0, Cfg().maxArmorCapacity - GetUsedArmorCapacity());
    }

    bool CanAddArmor(int tier, int count = 1) const {
        if (tier < 0 || tier >= ARMOR_COUNT || count <= 0) return false;
        return (count * Cfg().armorWeight[tier]) <= GetFreeArmorCapacity();
    }

    int GetMaxBuyableArmor(int tier) const {
        if (tier < 0 || tier >= ARMOR_COUNT) return 0;
        int w = std::max(1, Cfg().armorWeight[tier]);
        return GetFreeArmorCapacity() / w;
    }

    void ClearAll() {
        for (int i = 0; i < FOOD_COUNT; ++i)  food[i] = 0;
        for (int i = 0; i < ARMOR_COUNT; ++i) armor[i] = 0;
    }

    void StartFreshNewGame() {
        m_slot = 0;
        ClearAll();
        modlog::Info("[Inv] Started fresh new game -> cleared pocket inventory.");
    }

    void LoadFromSlot(int slot1to8) {
        m_slot = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
        ClearAll();
        std::string path = GetFilePath(m_slot, true);
        if (path.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            modlog::Info("[Inv] No saved inventory for slot %d -> starting empty.", m_slot);
            return;
        }
        const char* p = path.c_str();
        int foodCapLeft = Cfg().maxFoodCapacity;
        for (int i = FOOD_COUNT - 1; i >= 0; --i) {
            int raw = std::max<int>(0, common::IniReadInt(p, "Food", kFoodDefs[i].key, 0));
            int w   = std::max(1, Cfg().foodWeight[i]);
            int fit = std::min(raw, foodCapLeft / w);
            food[i] = fit;
            foodCapLeft -= fit * w;
        }
        int armorCapLeft = Cfg().maxArmorCapacity;
        for (int i = ARMOR_COUNT - 1; i >= 0; --i) {
            int raw = std::max<int>(0, common::IniReadInt(p, "Armor", kArmorDefs[i].key, 0));
            int w   = std::max(1, Cfg().armorWeight[i]);
            int fit = std::min(raw, armorCapLeft / w);
            armor[i] = fit;
            armorCapLeft -= fit * w;
        }
        modlog::Info("[Inv] Loaded inventory slot %d (food=%d [%d/%d], armor=%d [%d/%d]).",
                     m_slot, GetTotalFood(), GetUsedFoodCapacity(), Cfg().maxFoodCapacity,
                     GetTotalArmor(), GetUsedArmorCapacity(), Cfg().maxArmorCapacity);
    }

    void SaveToSlot(int slot1to8) {
        if (slot1to8 >= 1 && slot1to8 <= 8) m_slot = slot1to8;
        SaveNow();
    }

    void SaveNow() {
        std::string path = GetFilePath(m_slot);
        if (path.empty()) return; // Unsaved Singleplayer/Host New Game: keep in RAM until saved to slot 1..8
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return;
        std::fprintf(f, "; GTA5Inventory Per-Save Pocket Data (Slot %d)\n\n[Food]\n", m_slot);
        for (int i = 0; i < FOOD_COUNT; ++i) {
            std::fprintf(f, "%s=%d\n", kFoodDefs[i].key, food[i]);
        }
        std::fprintf(f, "\n[Armor]\n");
        for (int i = 0; i < ARMOR_COUNT; ++i) {
            std::fprintf(f, "%s=%d\n", kArmorDefs[i].key, armor[i]);
        }
        std::fclose(f);
    }

private:
    std::string GetFilePath(int slot, bool forLoading = false) const {
        const char* ov = integrations::sacoop::GetProfileOverride();
        if (forLoading) {
            return integrations::sacoop::ResolveExistingLoadPath("GTA5InventorySaves", Cfg().modDir, "inv", slot, ov);
        }
        return integrations::sacoop::BuildSaveProfilePath("GTA5InventorySaves", Cfg().modDir, "inv", slot, ov);
    }

    int m_slot = 0;
};

inline InventoryStore& Store() {
    static InventoryStore st;
    return st;
}

} // namespace inv
