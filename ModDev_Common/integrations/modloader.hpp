#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

// ============================================================================
// Standalone Integration Module: TheLink2012 Mod Loader (modloader.asi)
//
// Provides:
//  - Cached detection of modloader.asi
//  - Live config reader & writer for modloader\.data\config.ini ([Config])
//  - Live profile, installed mod folder scanner, and per-mod Enabled/Priority
//    reader & writer for modloader\modloader.ini
//  - Human-readable byte size formatting
// ============================================================================
namespace integrations::modloader {

inline bool IsLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        s_state = (GetModuleHandleA("modloader.asi") != nullptr) ? 1 : 0;
    }
    return s_state == 1;
}

inline std::string GetGameExeDir() {
    static std::string s_dir;
    if (!s_dir.empty()) return s_dir;
    char exeBuf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    std::string full(exeBuf);
    size_t slash = full.find_last_of("\\/");
    s_dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";
    return s_dir;
}

inline std::string GetModLoaderDir() {
    return GetGameExeDir() + "\\modloader";
}

namespace detail {

inline std::string Trim(std::string s) {
    auto isWs = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && isWs(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isWs(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

inline std::string StripCommentAndTrim(const std::string& line) {
    size_t semi = line.find(';');
    std::string sub = (semi != std::string::npos) ? line.substr(0, semi) : line;
    return Trim(sub);
}

inline std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline bool ParseBoolValue(const std::string& v, bool fallback) {
    std::string low = ToLowerAscii(Trim(v));
    if (low == "true" || low == "1" || low == "yes" || low == "on") return true;
    if (low == "false" || low == "0" || low == "no" || low == "off") return false;
    return fallback;
}

inline bool ReadAllLines(const std::string& path, std::vector<std::string>& outLines) {
    outLines.clear();
    std::ifstream f(path, std::ios::in);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        outLines.push_back(std::move(line));
    }
    return true;
}

inline bool WriteAllLines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i) {
        f << lines[i];
        if (i + 1 < lines.size()) f << "\n";
    }
    return f.good();
}

inline int CountDirectChildren(const std::string& dirPath) {
    std::string pat = dirPath + "\\*";
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int cnt = 0;
    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0'))) {
            continue;
        }
        ++cnt;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return cnt;
}

} // namespace detail

struct ConfigSnapshot {
    bool        loaded         = false;
    bool        enableMenu     = true;
    bool        enablePlugins  = true;
    bool        enableLog      = true;
    bool        immediateFlush = false;
    uint64_t    maxLogSize     = 5242880ull;
    bool        autoRefresh    = true;
    std::string activeProfile  = "Default";
    int         priorityLimit  = 100;
    bool        ignoreAllMods  = false;
    bool        excludeAllMods = false;
};

inline ConfigSnapshot ReadConfig() {
    ConfigSnapshot cfg{};
    if (!IsLoaded()) return cfg;

    {
        std::ifstream f(GetModLoaderDir() + "\\.data\\config.ini");
        if (f.is_open()) {
            cfg.loaded = true;
            std::string line, section;
            while (std::getline(f, line)) {
                std::string clean = detail::StripCommentAndTrim(line);
                if (clean.empty()) continue;
                if (clean.front() == '[' && clean.back() == ']') {
                    section = detail::ToLowerAscii(detail::Trim(clean.substr(1, clean.size() - 2)));
                    continue;
                }
                if (section != "config") continue;
                size_t eq = clean.find('=');
                if (eq == std::string::npos) continue;
                std::string k = detail::ToLowerAscii(detail::Trim(clean.substr(0, eq)));
                std::string v = detail::Trim(clean.substr(eq + 1));
                if (k == "enablemenu")             cfg.enableMenu     = detail::ParseBoolValue(v, cfg.enableMenu);
                else if (k == "enableplugins")     cfg.enablePlugins  = detail::ParseBoolValue(v, cfg.enablePlugins);
                else if (k == "enablelog")         cfg.enableLog      = detail::ParseBoolValue(v, cfg.enableLog);
                else if (k == "immediateflushlog") cfg.immediateFlush = detail::ParseBoolValue(v, cfg.immediateFlush);
                else if (k == "autorefresh")       cfg.autoRefresh    = detail::ParseBoolValue(v, cfg.autoRefresh);
                else if (k == "maxlogsize")        cfg.maxLogSize     = std::strtoull(v.c_str(), nullptr, 10);
            }
        }
    }

    {
        std::ifstream f(GetModLoaderDir() + "\\modloader.ini");
        if (f.is_open()) {
            cfg.loaded = true;
            std::string line, section;
            while (std::getline(f, line)) {
                std::string clean = detail::StripCommentAndTrim(line);
                if (clean.empty()) continue;
                if (clean.front() == '[' && clean.back() == ']') {
                    section = detail::ToLowerAscii(detail::Trim(clean.substr(1, clean.size() - 2)));
                    continue;
                }
                size_t eq = clean.find('=');
                if (eq == std::string::npos) continue;
                std::string k = detail::ToLowerAscii(detail::Trim(clean.substr(0, eq)));
                std::string v = detail::Trim(clean.substr(eq + 1));
                if (section == "folder.config") {
                    if (k == "profile" && !v.empty()) cfg.activeProfile = v;
                    else if (k == "prioritylimit")    cfg.priorityLimit = std::clamp(std::atoi(v.c_str()), 1, 1000);
                } else {
                    std::string profSec = "profiles." + detail::ToLowerAscii(cfg.activeProfile) + ".config";
                    if (section == profSec) {
                        if (k == "ignoreallmods")       cfg.ignoreAllMods  = detail::ParseBoolValue(v, false);
                        else if (k == "excludeallmods") cfg.excludeAllMods = detail::ParseBoolValue(v, false);
                    }
                }
            }
        }
    }
    return cfg;
}

struct ModFolderItem {
    std::string name;
    bool        enabled   = true;
    int         priority  = 50;
    int         itemCount = 0;
};

inline std::vector<ModFolderItem> ListInstalledMods(ConfigSnapshot* outCfg = nullptr) {
    std::vector<ModFolderItem> out;
    if (!IsLoaded()) return out;

    const ConfigSnapshot cfg = ReadConfig();
    if (outCfg) *outCfg = cfg;

    // 1. Parse IgnoreMods, IncludeMods, Priority once from modloader.ini
    std::unordered_set<std::string> ignoredSet;
    std::unordered_set<std::string> includedSet;
    std::unordered_map<std::string, int> prioMap;

    const std::string profLower  = detail::ToLowerAscii(cfg.activeProfile);
    const std::string secIgnore  = "profiles." + profLower + ".ignoremods";
    const std::string secInclude = "profiles." + profLower + ".includemods";
    const std::string secPrio    = "profiles." + profLower + ".priority";

    {
        std::ifstream f(GetModLoaderDir() + "\\modloader.ini");
        if (f.is_open()) {
            std::string line, section;
            while (std::getline(f, line)) {
                std::string clean = detail::StripCommentAndTrim(line);
                if (clean.empty()) continue;
                if (clean.front() == '[' && clean.back() == ']') {
                    section = detail::ToLowerAscii(detail::Trim(clean.substr(1, clean.size() - 2)));
                    continue;
                }
                if (section == secIgnore) {
                    ignoredSet.insert(detail::ToLowerAscii(clean));
                } else if (section == secInclude) {
                    includedSet.insert(detail::ToLowerAscii(clean));
                } else if (section == secPrio) {
                    size_t eq = clean.find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = detail::ToLowerAscii(detail::Trim(clean.substr(0, eq)));
                    int v = std::clamp(std::atoi(detail::Trim(clean.substr(eq + 1)).c_str()),
                                       0, std::max(100, cfg.priorityLimit));
                    prioMap[k] = v;
                }
            }
        }
    }

    // 2. Enumerate top-level mod folders
    const std::string mlDir = GetModLoaderDir();
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA((mlDir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        std::string nm(fd.cFileName);
        if (nm.empty() || nm[0] == '.' || detail::ToLowerAscii(nm) == "_ignore") continue;
        std::string low = detail::ToLowerAscii(nm);

        ModFolderItem item{};
        item.name      = nm;
        item.enabled   = !(cfg.ignoreAllMods || cfg.excludeAllMods);
        if (ignoredSet.count(low) > 0)  item.enabled = false;
        if (includedSet.count(low) > 0) item.enabled = true;
        auto pit = prioMap.find(low);
        item.priority  = (pit != prioMap.end()) ? pit->second : 50;
        item.itemCount = detail::CountDirectChildren(mlDir + "\\" + nm);
        out.push_back(std::move(item));
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    std::sort(out.begin(), out.end(), [](const ModFolderItem& a, const ModFolderItem& b) {
        return detail::ToLowerAscii(a.name) < detail::ToLowerAscii(b.name);
    });
    return out;
}

inline bool SetConfigBool(const char* keyName, bool value) {
    if (!IsLoaded() || !keyName || !keyName[0]) return false;
    const std::string path = GetModLoaderDir() + "\\.data\\config.ini";
    std::vector<std::string> lines;
    if (!detail::ReadAllLines(path, lines)) return false;

    const std::string targetKey = detail::ToLowerAscii(keyName);
    const std::string valStr    = value ? "true" : "false";
    std::string section;
    bool updated = false;
    int configSectionEnd = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string clean = detail::StripCommentAndTrim(lines[i]);
        if (!clean.empty() && clean.front() == '[' && clean.back() == ']') {
            if (section == "config" && configSectionEnd < 0) configSectionEnd = static_cast<int>(i);
            section = detail::ToLowerAscii(detail::Trim(clean.substr(1, clean.size() - 2)));
            continue;
        }
        if (section != "config") continue;
        size_t eq = clean.find('=');
        if (eq == std::string::npos) continue;
        if (detail::ToLowerAscii(detail::Trim(clean.substr(0, eq))) == targetKey) {
            size_t semi = lines[i].find(';');
            std::string comment = (semi != std::string::npos) ? ("  " + lines[i].substr(semi)) : "";
            lines[i] = std::string(keyName) + " = " + valStr + comment;
            updated = true;
            break;
        }
    }
    if (!updated) {
        if (configSectionEnd >= 0) {
            lines.insert(lines.begin() + configSectionEnd, std::string(keyName) + " = " + valStr);
        } else {
            lines.push_back(std::string(keyName) + " = " + valStr);
        }
    }
    return detail::WriteAllLines(path, lines);
}

inline bool SetModEnabled(const std::string& modName, bool enabled) {
    if (!IsLoaded() || modName.empty()) return false;
    const ConfigSnapshot cfg = ReadConfig();
    const std::string path = GetModLoaderDir() + "\\modloader.ini";
    std::vector<std::string> lines;
    if (!detail::ReadAllLines(path, lines)) return false;

    const std::string profHeader = "Profiles." + cfg.activeProfile + ".IgnoreMods";
    const std::string secIgnore  = detail::ToLowerAscii(profHeader);
    const std::string targetLow  = detail::ToLowerAscii(detail::Trim(modName));

    std::string section;
    int secStart = -1;
    int secEnd   = static_cast<int>(lines.size());
    std::vector<size_t> toRemove;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string clean = detail::StripCommentAndTrim(lines[i]);
        if (!clean.empty() && clean.front() == '[' && clean.back() == ']') {
            if (secStart >= 0 && section == secIgnore) {
                secEnd = static_cast<int>(i);
            }
            section = detail::ToLowerAscii(detail::Trim(clean.substr(1, clean.size() - 2)));
            if (section == secIgnore) secStart = static_cast<int>(i);
            continue;
        }
        if (section == secIgnore && !clean.empty()) {
            if (detail::ToLowerAscii(clean) == targetLow) {
                toRemove.push_back(i);
            }
        }
    }

    if (enabled) {
        for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
            lines.erase(lines.begin() + static_cast<ptrdiff_t>(*it));
        }
    } else if (toRemove.empty()) {
        if (secStart >= 0) {
            int insertPos = secEnd;
            while (insertPos > secStart + 1 && detail::Trim(lines[insertPos - 1]).empty()) {
                --insertPos;
            }
            lines.insert(lines.begin() + insertPos, targetLow);
        } else {
            lines.push_back("");
            lines.push_back("[" + profHeader + "]");
            lines.push_back(targetLow);
        }
    }
    return detail::WriteAllLines(path, lines);
}

inline bool SetModPriority(const std::string& modName, int priority) {
    if (!IsLoaded() || modName.empty()) return false;
    const ConfigSnapshot cfg = ReadConfig();
    priority = std::clamp(priority, 0, std::max(100, cfg.priorityLimit));
    const std::string path = GetModLoaderDir() + "\\modloader.ini";
    std::vector<std::string> lines;
    if (!detail::ReadAllLines(path, lines)) return false;

    const std::string profHeader = "Profiles." + cfg.activeProfile + ".Priority";
    const std::string secPrio    = detail::ToLowerAscii(profHeader);
    const std::string targetLow  = detail::ToLowerAscii(detail::Trim(modName));

    std::string section;
    int secStart  = -1;
    int secEnd    = static_cast<int>(lines.size());
    int matchLine = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string clean = detail::StripCommentAndTrim(lines[i]);
        if (!clean.empty() && clean.front() == '[' && clean.back() == ']') {
            if (secStart >= 0 && section == secPrio) {
                secEnd = static_cast<int>(i);
            }
            section = detail::ToLowerAscii(detail::Trim(clean.substr(1, clean.size() - 2)));
            if (section == secPrio) secStart = static_cast<int>(i);
            continue;
        }
        if (section == secPrio) {
            size_t eq = clean.find('=');
            if (eq != std::string::npos && detail::ToLowerAscii(detail::Trim(clean.substr(0, eq))) == targetLow) {
                matchLine = static_cast<int>(i);
            }
        }
    }

    if (priority == 50) {
        if (matchLine >= 0) {
            lines.erase(lines.begin() + matchLine);
        }
    } else {
        std::string newLine = targetLow + " = " + std::to_string(priority);
        if (matchLine >= 0) {
            lines[matchLine] = newLine;
        } else if (secStart >= 0) {
            int insertPos = secEnd;
            while (insertPos > secStart + 1 && detail::Trim(lines[insertPos - 1]).empty()) {
                --insertPos;
            }
            lines.insert(lines.begin() + insertPos, newLine);
        } else {
            lines.push_back("");
            lines.push_back("[" + profHeader + "]");
            lines.push_back(newLine);
        }
    }
    return detail::WriteAllLines(path, lines);
}

inline std::string FormatBytesShort(uint64_t bytes) {
    char buf[32];
    if (bytes == 0) return "0 B";
    if (bytes >= 1048576ull && (bytes % 1048576ull) == 0) {
        std::snprintf(buf, sizeof(buf), "%llu MB", static_cast<unsigned long long>(bytes / 1048576ull));
        return buf;
    }
    if (bytes >= 1048576ull) {
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / 1048576.0);
        return buf;
    }
    if (bytes >= 1024ull) {
        std::snprintf(buf, sizeof(buf), "%llu KB", static_cast<unsigned long long>(bytes / 1024ull));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

} // namespace integrations::modloader
