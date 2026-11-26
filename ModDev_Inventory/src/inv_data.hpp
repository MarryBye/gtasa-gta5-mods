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
    int         defaultMax;
    int         defaultPrice;
    uint8_t     shopMask; // Bit0=All, Bit1=BurgerShot, Bit2=Pizza, Bit3=CluckinBell
};

constexpr uint8_t SHOP_ALL    = 0x01;
constexpr uint8_t SHOP_BURGER = 0x02;
constexpr uint8_t SHOP_PIZZA  = 0x04;
constexpr uint8_t SHOP_CLUCK  = 0x08;

constexpr FoodDef kFoodDefs[FOOD_COUNT] = {
    {"PsQs",      "Конфеты P's & Q's",   "P's & Q's",        15, 1.5f, 30,  5, SHOP_ALL},
    {"EgoChaser", "Шоколад EgoChaser",   "EgoChaser",        28, 3.0f, 15, 12, SHOP_ALL},
    {"Meteorite", "Батончик Meteorite",  "Meteorite Bar",    22, 2.0f, 15, 10, SHOP_ALL},
    {"Sprunk",    "Напиток Sprunk",      "Sprunk Can",       20, 1.0f, 10,  8, SHOP_ALL},
    {"Burger",    "Бургер (на вынос)",   "Bleeder Burger",   45, 7.0f,  5, 25, SHOP_BURGER},
    {"Pizza",     "Пицца (на вынос)",    "Pizza Box",        45, 7.0f,  5, 25, SHOP_PIZZA},
    {"Chicken",   "Курица (на вынос)",   "Cluckin' Meal",    45, 6.0f,  5, 25, SHOP_CLUCK},
    {"Salad",     "Здоровый салат",      "Healthy Salad",    40, 0.0f,  5, 30, SHOP_ALL},
};

struct ArmorDef {
    const char* key;
    const char* rus;
    const char* eng;
    float       armorValue;
    int         defaultMax;
    int         defaultPrice;
};

constexpr ArmorDef kArmorDefs[ARMOR_COUNT] = {
    {"LightArmor",      "Лёгкий броник (25%)",       "Light Armor (25%)",       25.0f, 5, 200},
    {"StandardArmor",   "Средний броник (50%)",      "Standard Armor (50%)",    50.0f, 5, 350},
    {"HeavyArmor",      "Тяжёлый броник (75%)",      "Heavy Armor (75%)",       75.0f, 5, 500},
    {"SuperHeavyArmor", "Сверхтяжёлый броник (100%)","Super Heavy Armor (100%)",100.0f, 5, 650},
};

struct InvConfig {
    bool useRussian             = true;
    int  openKey                = VK_F7; // 118 (auto-disabled when GTA5Phone.asi is present!)
    bool allowArmorInFoodShops  = true;
    bool enableLog              = true;
    int  foodMax[FOOD_COUNT]    = {30, 15, 15, 10, 5, 5, 5, 5};
    int  foodPrice[FOOD_COUNT]  = {5, 12, 10, 8, 25, 25, 25, 30};
    int  armorMax[ARMOR_COUNT]  = {5, 5, 5, 5};
    int  armorPrice[ARMOR_COUNT]= {200, 350, 500, 650};
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
    c.allowArmorInFoodShops = GetPrivateProfileIntA("General", "AllowArmorInFoodShops", 1, p) != 0;
    c.enableLog             = GetPrivateProfileIntA("General", "EnableLog", 1, p) != 0;

    for (int i = 0; i < FOOD_COUNT; ++i) {
        c.foodMax[i]   = std::clamp<int>(GetPrivateProfileIntA("FoodLimits", kFoodDefs[i].key, kFoodDefs[i].defaultMax, p), 1, 99);
        c.foodPrice[i] = std::max<int>(1, GetPrivateProfileIntA("FoodPrices", kFoodDefs[i].key, kFoodDefs[i].defaultPrice, p));
    }
    for (int i = 0; i < ARMOR_COUNT; ++i) {
        c.armorMax[i]   = std::clamp<int>(GetPrivateProfileIntA("ArmorLimits", kArmorDefs[i].key, kArmorDefs[i].defaultMax, p), 1, 50);
        c.armorPrice[i] = std::max<int>(50, GetPrivateProfileIntA("ArmorPrices", kArmorDefs[i].key, kArmorDefs[i].defaultPrice, p));
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
        for (int i = 0; i < FOOD_COUNT; ++i) {
            food[i] = std::clamp<int>(GetPrivateProfileIntA("Food", kFoodDefs[i].key, 0, p), 0, Cfg().foodMax[i]);
        }
        for (int i = 0; i < ARMOR_COUNT; ++i) {
            armor[i] = std::clamp<int>(GetPrivateProfileIntA("Armor", kArmorDefs[i].key, 0, p), 0, Cfg().armorMax[i]);
        }
        modlog::Info("[Inv] Loaded inventory slot %d (food=%d, armor=%d).", m_slot, GetTotalFood(), GetTotalArmor());
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
