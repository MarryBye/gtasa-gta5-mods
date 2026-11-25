#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <windows.h>

namespace modlog {

inline std::string& LogPath() {
    static std::string path;
    return path;
}

inline bool& Enabled() {
    static bool enabled = true;
    return enabled;
}

inline void Init(HMODULE selfModule, const char* headerTitle = "GTA SA ASI Mod") {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(selfModule, buf, MAX_PATH);
    std::string full(buf);
    size_t dot = full.find_last_of('.');
    if (dot != std::string::npos) {
        full = full.substr(0, dot);
    }
    LogPath() = full + ".log";

    if (FILE* f = std::fopen(LogPath().c_str(), "w")) {
        std::fprintf(f, "=== %s ASI Log ===\n", headerTitle ? headerTitle : "Mod");
        std::fclose(f);
    }
}

inline void Info(const char* fmt, ...) {
    if (!Enabled() || LogPath().empty()) return;
    FILE* f = std::fopen(LogPath().c_str(), "a");
    if (!f) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);

    std::fprintf(f, "\n");
    std::fclose(f);
}

} // namespace modlog
