#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
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
    int         defaultWeight; // Pocket carry capacity units (total pocket capacity = 20)
    int         defaultPrice;
    uint8_t     shopMask;      // Bit0=All, Bit1=BurgerShot, Bit2=Pizza, Bit3=CluckinBell
};

constexpr uint8_t SHOP_ALL    = 0x01;
constexpr uint8_t SHOP_BURGER = 0x02;
constexpr uint8_t SHOP_PIZZA  = 0x04;
constexpr uint8_t SHOP_CLUCK  = 0x08;

// Rebalanced GTA 5 / realistic economy:
// Cheap light snacks = small HP & small space (1-2); hot restaurant meals = large HP, expensive, take more space (3-4).
constexpr FoodDef kFoodDefs[FOOD_COUNT] = {
    {"PsQs",      "Конфеты P's & Q's",   "P's & Q's",        8,  1.0f, 1,  15, SHOP_ALL},
    {"Sprunk",    "Напиток Sprunk",      "Sprunk Can",      10,  0.5f, 1,  20, SHOP_ALL},
    {"Meteorite", "Батончик Meteorite",  "Meteorite Bar",   15,  1.5f, 2,  30, SHOP_ALL},
    {"EgoChaser", "Шоколад EgoChaser",   "EgoChaser",       22,  2.5f, 2,  45, SHOP_ALL},
    {"Salad",     "Здоровый салат",      "Healthy Salad",   55,  0.0f, 3, 180, SHOP_ALL},
    {"Burger",    "Бургер (на вынос)",   "Bleeder Burger",  45,  6.0f, 4, 120, SHOP_BURGER},
    {"Chicken",   "Курица (на вынос)",   "Cluckin' Meal",   48,  5.5f, 4, 130, SHOP_CLUCK},
    {"Pizza",     "Пицца (на вынос)",    "Pizza Box",       50,  6.5f, 4, 140, SHOP_PIZZA},
};

struct ArmorDef {
    const char* key;
    const char* rus;
    const char* eng;
    float       armorValue;
    int         defaultWeight; // Spare armor carry capacity units (total armor capacity = 10)
    int         defaultPrice;
};

// Rebalanced spare armor economy (bought ONLY in Ammu-Nation by default!):
// Capacity = 10 -> up to 5x Light (2), or 2x Standard (4) + 1x Light (2), or 1x Heavy (7) + 1x Light (2), or 1x Super Heavy (10).
constexpr ArmorDef kArmorDefs[ARMOR_COUNT] = {
    {"LightArmor",      "Лёгкий броник (25%)",       "Light Armor (25%)",       25.0f,  2,  350},
    {"StandardArmor",   "Средний броник (50%)",      "Standard Armor (50%)",    50.0f,  4,  700},
    {"HeavyArmor",      "Тяжёлый броник (75%)",      "Heavy Armor (75%)",       75.0f,  7, 1200},
    {"SuperHeavyArmor", "Сверхтяжёлый броник (100%)","Super Heavy Armor (100%)",100.0f, 10, 1800},
};

struct InvConfig {
    bool useRussian                = true;
    int  openKey                   = VK_F7; // 118 (auto-disabled when GTA5Phone.asi is present!)
    bool allowArmorInFoodShops     = false; // Armor is sold ONLY in Ammu-Nation by default!
    bool enableLog                 = true;
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
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(hModule, buf, MAX_PATH);
    std::string full(buf);
    size_t slash = full.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";

    auto& c = Cfg();
    c.modDir = dir;
    std::string ini = dir + "\\GTA5Inventory.ini";
    const char* p = ini.c_str();

    char lang[32] = {};
    GetPrivateProfileStringA("General", "Language", "rus", lang, sizeof(lang), p);
    c.useRussian            = (_stricmp(lang, "eng") != 0);
    c.openKey               = GetPrivateProfileIntA("General", "OpenKey", VK_F7, p);
    c.allowArmorInFoodShops = GetPrivateProfileIntA("General", "AllowArmorInFoodShops", 0, p) != 0;
    c.enableLog             = GetPrivateProfileIntA("General", "EnableLog", 1, p) != 0;
    c.maxFoodCapacity       = std::clamp<int>(GetPrivateProfileIntA("General", "MaxFoodCapacity", 20, p), 5, 200);
    c.maxArmorCapacity      = std::clamp<int>(GetPrivateProfileIntA("General", "MaxArmorCapacity", 10, p), 4, 100);

    for (int i = 0; i < FOOD_COUNT; ++i) {
        c.foodWeight[i] = std::clamp<int>(GetPrivateProfileIntA("FoodWeights", kFoodDefs[i].key, kFoodDefs[i].defaultWeight, p), 1, c.maxFoodCapacity);
        c.foodPrice[i]  = std::max<int>(1, GetPrivateProfileIntA("FoodPrices", kFoodDefs[i].key, kFoodDefs[i].defaultPrice, p));
    }
    for (int i = 0; i < ARMOR_COUNT; ++i) {
        c.armorWeight[i] = std::clamp<int>(GetPrivateProfileIntA("ArmorWeights", kArmorDefs[i].key, kArmorDefs[i].defaultWeight, p), 1, c.maxArmorCapacity);
        c.armorPrice[i]  = std::max<int>(50, GetPrivateProfileIntA("ArmorPrices", kArmorDefs[i].key, kArmorDefs[i].defaultPrice, p));
    }
}

class InventoryStore {
public:
    int food[FOOD_COUNT]   = {};
    int armor[ARMOR_COUNT] = {};

    int GetSlot() const { return m_slot; }

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

    bool CanAddFood(int idx, int count = 1) const {
        if (idx < 0 || idx >= FOOD_COUNT || count <= 0) return false;
        return (count * Cfg().foodWeight[idx]) <= GetFreeFoodCapacity();
    }

    int GetMaxBuyableFood(int idx) const {
        if (idx < 0 || idx >= FOOD_COUNT) return 0;
        int w = std::max(1, Cfg().foodWeight[idx]);
        return GetFreeFoodCapacity() / w;
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
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            modlog::Info("[Inv] No saved inventory for slot %d -> starting empty.", m_slot);
            return;
        }
        const char* p = path.c_str();
        // Clamp loaded items cleanly into current capacity budget (safe migration from older saves!)
        int foodCapLeft = Cfg().maxFoodCapacity;
        for (int i = FOOD_COUNT - 1; i >= 0; --i) {
            int raw = std::max<int>(0, static_cast<int>(GetPrivateProfileIntA("Food", kFoodDefs[i].key, 0, p)));
            int w   = std::max(1, Cfg().foodWeight[i]);
            int fit = std::min(raw, foodCapLeft / w);
            food[i] = fit;
            foodCapLeft -= fit * w;
        }
        int armorCapLeft = Cfg().maxArmorCapacity;
        for (int i = ARMOR_COUNT - 1; i >= 0; --i) {
            int raw = std::max<int>(0, static_cast<int>(GetPrivateProfileIntA("Armor", kArmorDefs[i].key, 0, p)));
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
        const char* ov = nullptr;
        if (auto* b = modbus::GetBus(); b && b->coopProfileName[0]) ov = b->coopProfileName;
        if (forLoading) {
            return sa::ResolveExistingLoadPath("GTA5InventorySaves", Cfg().modDir, "inv", slot, ov);
        }
        return sa::BuildSaveProfilePath("GTA5InventorySaves", Cfg().modDir, "inv", slot, ov);
    }

    int m_slot = 0;
};

inline InventoryStore& Store() {
    static InventoryStore st;
    return st;
}

} // namespace inv
