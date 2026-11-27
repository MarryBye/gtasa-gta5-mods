#pragma once
#include <algorithm>
#include <string>
#include <windows.h>
#include "common_utils.hpp"

namespace progression {

struct ProgConfig {
    bool        useRussian   = true;
    float       xpMultiplier = 1.0f;
    int         peekKey      = VK_F8;
    bool        enableSound  = true;
    bool        enableLog    = true;
    std::string modDir;
};

inline ProgConfig& Cfg() {
    static ProgConfig c;
    return c;
}

inline void LoadConfig(HMODULE hModule) {
    auto& c = Cfg();
    c.modDir = common::GetModuleDir(hModule);
    const std::string ini = c.modDir + "\\GTA5Progression.ini";
    const char* p = ini.c_str();

    std::string lang = common::IniReadString(p, "General", "Language", "rus");
    c.useRussian   = (_stricmp(lang.c_str(), "eng") != 0);
    c.xpMultiplier = std::clamp<float>(common::IniReadFloat(p, "General", "XpMultiplier", 1.0f), 0.1f, 10.0f);
    c.peekKey      = common::IniReadInt(p, "General", "PeekKey", VK_F8);
    c.enableSound  = common::IniReadBool(p, "General", "EnableSound", true);
    c.enableLog    = common::IniReadBool(p, "General", "EnableLog", true);
}

} // namespace progression
