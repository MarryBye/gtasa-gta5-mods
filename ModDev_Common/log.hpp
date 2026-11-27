#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <windows.h>

namespace modlog {

struct LockState {
    CRITICAL_SECTION cs{};
    LockState()  { InitializeCriticalSection(&cs); }
    ~LockState() { DeleteCriticalSection(&cs); }
};

inline CRITICAL_SECTION& Mutex() {
    static LockState s_lock;
    return s_lock.cs;
}

inline std::string& LogPath() {
    static std::string path;
    return path;
}

inline bool& Enabled() {
    static bool enabled = true;
    return enabled;
}

inline bool& Initialized() {
    static bool inited = false;
    return inited;
}

inline void Init(HMODULE selfModule, const char* headerTitle = "GTA SA ASI Mod") {
    EnterCriticalSection(&Mutex());

    if (!Initialized()) {
        char buf[MAX_PATH] = {};
        GetModuleFileNameA(selfModule, buf, MAX_PATH);
        std::string full(buf);
        size_t dot = full.find_last_of('.');
        if (dot != std::string::npos) {
            full = full.substr(0, dot);
        }
        LogPath() = full + ".log";
        Initialized() = true;

        if (FILE* f = std::fopen(LogPath().c_str(), "w")) {
            std::fprintf(f, "=== %s ASI Log ===\n", headerTitle ? headerTitle : "Mod");
            std::fclose(f);
        }
    } else if (FILE* f = std::fopen(LogPath().c_str(), "a")) {
        // Merge-safe: if multiple modules call Init inside one unified ASI, append section headers!
        std::fprintf(f, "--- %s ---\n", headerTitle ? headerTitle : "Module");
        std::fclose(f);
    }

    LeaveCriticalSection(&Mutex());
}

inline void Info(const char* fmt, ...) {
    if (!Enabled()) return;
    EnterCriticalSection(&Mutex());
    if (!LogPath().empty()) {
        if (FILE* f = std::fopen(LogPath().c_str(), "a")) {
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
    }
    LeaveCriticalSection(&Mutex());
}

} // namespace modlog
