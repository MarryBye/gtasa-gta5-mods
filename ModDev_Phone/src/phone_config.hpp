#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include <windows.h>
#include "common_utils.hpp"

namespace pcfg {

struct PhoneConfig {
    bool useRussian             = true;
    int  phoneKey               = VK_F6;
    int  coopPlayerListKey      = VK_F2;
    int  wantedClearCooldownSec = 600;
    int  wantedClearMaxStars    = 3;
    int  wantedClearCostPerStar = 500;
    int  medicHealCost          = 400;
    int  medicCooldownSec       = 180;
    bool enableClothesShopUI    = true;
    bool enableAutoOutfits      = true;
    int  phoneTheme             = 0;
    int  ringtoneId             = 0;
    bool enableKeyBeeps         = true;
    bool enablePhoneAnim        = true;
    bool silentMode             = false;
    bool enableLog              = true;
    std::string gameDir;
    std::string modDir;
};

inline PhoneConfig& Get() {
    static PhoneConfig c;
    return c;
}

inline void SaveSettings() {
    const auto& c = Get();
    if (c.modDir.empty()) return;
    const std::string ini = c.modDir + "\\GTA5Phone.ini";
    const char* p = ini.c_str();
    common::IniWriteInt(p, "Settings", "Theme",             c.phoneTheme);
    common::IniWriteInt(p, "Settings", "Ringtone",          c.ringtoneId);
    common::IniWriteInt(p, "Settings", "KeyBeeps",          c.enableKeyBeeps    ? 1 : 0);
    common::IniWriteInt(p, "Settings", "PhoneAnim",         c.enablePhoneAnim   ? 1 : 0);
    common::IniWriteInt(p, "Settings", "SilentMode",        c.silentMode        ? 1 : 0);
    common::IniWriteInt(p, "Wardrobe", "EnableAutoOutfits", c.enableAutoOutfits ? 1 : 0);
}

inline void Load(HMODULE hModule) {
    auto& c = Get();
    c.gameDir = common::GetExeDir();
    c.modDir  = common::GetModuleDir(hModule);
    const std::string ini = c.modDir + "\\GTA5Phone.ini";
    const char* p = ini.c_str();

    std::string lang = common::IniReadString(p, "General", "Language", "rus");
    c.useRussian             = (_stricmp(lang.c_str(), "eng") != 0);
    c.phoneKey               = common::IniReadInt(p, "General", "PhoneKey", VK_F6);
    c.coopPlayerListKey      = common::IniReadInt(p, "Coop",    "PlayerListKey", VK_F2);
    c.wantedClearCooldownSec = std::max<int>(30, common::IniReadInt(p, "Contacts", "WantedClearCooldownSec", 600));
    c.wantedClearMaxStars    = std::clamp<int>(common::IniReadInt(p, "Contacts", "WantedClearMaxStars", 3), 1, 6);
    c.wantedClearCostPerStar = std::max<int>(0, common::IniReadInt(p, "Contacts", "WantedClearCostPerStar", 500));
    c.medicHealCost          = std::max<int>(0, common::IniReadInt(p, "Contacts", "MedicHealCost", 400));
    c.medicCooldownSec       = std::max<int>(10, common::IniReadInt(p, "Contacts", "MedicCooldownSec", 180));
    c.enableClothesShopUI    = common::IniReadBool(p, "Wardrobe", "EnableClothesShopUI", true);
    c.enableAutoOutfits      = common::IniReadBool(p, "Wardrobe", "EnableAutoOutfits",   true);
    c.phoneTheme             = std::clamp<int>(common::IniReadInt(p, "Settings", "Theme",    0), 0, 3);
    c.ringtoneId             = std::clamp<int>(common::IniReadInt(p, "Settings", "Ringtone", 0), 0, 3);
    c.enableKeyBeeps         = common::IniReadBool(p, "Settings", "KeyBeeps",   true);
    c.enablePhoneAnim        = common::IniReadBool(p, "Settings", "PhoneAnim",  true);
    c.silentMode             = common::IniReadBool(p, "Settings", "SilentMode", false);
    c.enableLog              = common::IniReadBool(p, "General",  "EnableLog",  true);
}

} // namespace pcfg
