#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

namespace pcfg {

struct PhoneConfig {
    bool useRussian              = true;
    int  phoneKey                = VK_F6; // 117
    int  coopPlayerListKey       = VK_F2; // 113 (SA-COOP default Player List key!)
    int  wantedClearCooldownSec  = 600;   // 10 minutes
    int  wantedClearMaxStars     = 3;     // Up to 3 stars removed per call
    int  wantedClearCostPerStar  = 500;
    int  medicHealCost           = 400;
    int  medicCooldownSec        = 180;
    bool enableClothesShopUI     = true;
    bool enableAutoOutfits       = true;
    int  phoneTheme              = 0;     // 0=ClassicSA, 1=Nokia92, 2=Amber90s, 3=NightBlue
    int  ringtoneId              = 0;     // 0=Classic, 1=RetroBeep, 2=Melody92, 3=USRing
    bool enableKeyBeeps          = true;
    bool enablePhoneAnim         = true;
    bool silentMode              = false;
    bool enableLog               = true;
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
    std::string ini = c.modDir + "\\GTA5Phone.ini";
    const char* p = ini.c_str();
    auto writeInt = [&](const char* sec, const char* key, int val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", val);
        WritePrivateProfileStringA(sec, key, buf, p);
    };
    writeInt("Settings", "Theme",      c.phoneTheme);
    writeInt("Settings", "Ringtone",   c.ringtoneId);
    writeInt("Settings", "KeyBeeps",   c.enableKeyBeeps  ? 1 : 0);
    writeInt("Settings", "PhoneAnim",  c.enablePhoneAnim ? 1 : 0);
    writeInt("Settings", "SilentMode", c.silentMode      ? 1 : 0);
    writeInt("Wardrobe", "EnableAutoOutfits", c.enableAutoOutfits ? 1 : 0);
}

inline void Load(HMODULE hModule) {
    char exeBuf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    std::string exeFull(exeBuf);
    size_t exeSlash = exeFull.find_last_of("\\/");
    std::string gDir = (exeSlash != std::string::npos) ? exeFull.substr(0, exeSlash) : ".";

    char buf[MAX_PATH] = {};
    GetModuleFileNameA(hModule, buf, MAX_PATH);
    std::string full(buf);
    size_t slash = full.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";

    auto& c = Get();
    c.gameDir = gDir;
    c.modDir  = dir;
    std::string ini = dir + "\\GTA5Phone.ini";
    const char* p = ini.c_str();

    char lang[32] = {};
    GetPrivateProfileStringA("General", "Language", "rus", lang, sizeof(lang), p);
    c.useRussian             = (_stricmp(lang, "eng") != 0);
    c.phoneKey               = GetPrivateProfileIntA("General", "PhoneKey", VK_F6, p);
    c.coopPlayerListKey      = GetPrivateProfileIntA("Coop",    "PlayerListKey", VK_F2, p);
    c.wantedClearCooldownSec = std::max<int>(30, GetPrivateProfileIntA("Contacts", "WantedClearCooldownSec", 600, p));
    c.wantedClearMaxStars    = std::clamp<int>(GetPrivateProfileIntA("Contacts", "WantedClearMaxStars", 3, p), 1, 6);
    c.wantedClearCostPerStar = std::max<int>(0, GetPrivateProfileIntA("Contacts", "WantedClearCostPerStar", 500, p));
    c.medicHealCost          = std::max<int>(0, GetPrivateProfileIntA("Contacts", "MedicHealCost", 400, p));
    c.medicCooldownSec       = std::max<int>(10, GetPrivateProfileIntA("Contacts", "MedicCooldownSec", 180, p));
    c.enableClothesShopUI    = GetPrivateProfileIntA("Wardrobe", "EnableClothesShopUI", 1, p) != 0;
    c.enableAutoOutfits      = GetPrivateProfileIntA("Wardrobe", "EnableAutoOutfits", 1, p) != 0;
    c.phoneTheme             = std::clamp<int>(GetPrivateProfileIntA("Settings", "Theme", 0, p), 0, 3);
    c.ringtoneId             = std::clamp<int>(GetPrivateProfileIntA("Settings", "Ringtone", 0, p), 0, 3);
    c.enableKeyBeeps         = GetPrivateProfileIntA("Settings", "KeyBeeps", 1, p) != 0;
    c.enablePhoneAnim        = GetPrivateProfileIntA("Settings", "PhoneAnim", 1, p) != 0;
    c.silentMode             = GetPrivateProfileIntA("Settings", "SilentMode", 0, p) != 0;
    c.enableLog              = GetPrivateProfileIntA("General",  "EnableLog", 1, p) != 0;
}

} // namespace pcfg
