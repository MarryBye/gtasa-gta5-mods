#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include "common_utils.hpp"

namespace modern_menu {

struct Config {
    bool    enableModernMenu    = true;
    uint8_t backdropOpacity     = 194;
    bool    showPlayerCard      = true;
    bool    showClassicTitleArt = true;
    bool    enableLog           = true;
};

inline Config g_cfg{};

inline void LoadConfig(const std::string& iniPath) {
    g_cfg.enableModernMenu    = common::IniReadBool(iniPath.c_str(), "General", "EnableModernMenu", true);
    int op                    = common::IniReadInt(iniPath.c_str(), "General", "BackdropOpacity", 194);
    g_cfg.backdropOpacity     = static_cast<uint8_t>(std::clamp(op, 60, 245));
    g_cfg.showPlayerCard      = common::IniReadBool(iniPath.c_str(), "General", "ShowPlayerCard", true);
    g_cfg.showClassicTitleArt = common::IniReadBool(iniPath.c_str(), "General", "ShowClassicTitleArt", true);
    g_cfg.enableLog           = common::IniReadBool(iniPath.c_str(), "General", "EnableLog", true);
}

} // namespace modern_menu
