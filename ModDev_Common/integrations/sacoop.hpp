#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <windows.h>
#include <shlobj.h>
#include "../mod_bus.hpp"

// ============================================================================
// Standalone Integration Module: GTA SA COOP (Karimov-Akbar/sa-coop)
//
// Centralizes ALL SA-COOP detection, overlay/focus guards, cached per-player
// profile resolution, F4 custom-skin guards, teammate-safe vehicle deletion
// checks, and F2 player-list triggering so every module stays 100% DRY and
// co-op safe without repeating logic.
// ============================================================================
namespace integrations::sacoop {

inline bool IsGameWindowFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// Cached once per boot (checks whether SA-COOP DLL is installed in the process)
inline bool IsLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        bool found =
            (GetModuleHandleA("coop.asi")           != nullptr) ||
            (GetModuleHandleA("sa-coop.asi")        != nullptr) ||
            (GetModuleHandleA("SA-COOP.asi")        != nullptr) ||
            (GetModuleHandleA("sa_coop.asi")        != nullptr) ||
            (GetModuleHandleA("CoopAndreas.asi")    != nullptr) ||
            (GetModuleHandleA("coopandreas.asi")    != nullptr) ||
            (GetModuleHandleA("sa_coop_client.asi") != nullptr) ||
            (GetModuleHandleA("sa-coop-client.asi") != nullptr) ||
            (GetModuleHandleA("coop_client.asi")    != nullptr);
        s_state = found ? 1 : 0;
    }
    return s_state == 1;
}

inline std::string GetGameExeDir() {
    static std::string s_dir;
    if (!s_dir.empty()) return s_dir;
    char exeBuf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    std::string exeFull(exeBuf);
    size_t slash = exeFull.find_last_of("\\/");
    s_dir = (slash != std::string::npos) ? exeFull.substr(0, slash) : ".";
    return s_dir;
}

struct SessionInfo {
    bool        inMultiplayer = false; // Actually connected to a SA-COOP server (NOT Singleplayer!)
    bool        isHost        = false; // Connected as Host (playerId == hostId)
    bool        isClient      = false; // Connected as joining Client (playerId != hostId)
    std::string campaignId;            // 8-hex SA-COOP playthrough ID ("The host plays game %08X" / "A new game: game %08X")
};

// Lightweight live session detector reading coop\coop.log (created fresh on every boot by SA-COOP).
// Distinguishes Singleplayer (even with coop.asi installed!), Multiplayer Host, and Multiplayer Client!
inline const SessionInfo& GetSessionInfo(bool forceRefresh = false) {
    static SessionInfo s_info{};
    static DWORD       s_lastCheckMs = 0;
    static DWORD       s_lastSize    = 0xFFFFFFFFu;

    if (!IsLoaded()) return s_info;

    const DWORD now = GetTickCount();
    if (!forceRefresh && s_lastSize != 0xFFFFFFFFu && (now - s_lastCheckMs) < 350u) {
        return s_info;
    }
    s_lastCheckMs = now;

    const std::string logPath = GetGameExeDir() + "\\coop\\coop.log";
    HANDLE hFile = CreateFileA(logPath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return s_info;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || (!forceRefresh && fileSize == s_lastSize)) {
        CloseHandle(hFile);
        return s_info;
    }
    s_lastSize = fileSize;

    // Read tail (up to 64 KB — covers all join/disconnect/game-id events)
    constexpr DWORD kMaxRead = 65536u;
    DWORD readOffset = (fileSize > kMaxRead) ? (fileSize - kMaxRead) : 0u;
    if (readOffset > 0) {
        SetFilePointer(hFile, static_cast<LONG>(readOffset), nullptr, FILE_BEGIN);
    }
    DWORD toRead = fileSize - readOffset;
    std::string buf(toRead, '\0');
    DWORD got = 0;
    ReadFile(hFile, buf.data(), toRead, &got, nullptr);
    CloseHandle(hFile);
    buf.resize(got);

    SessionInfo next{};
    size_t pos = 0;
    while (pos < buf.size()) {
        size_t eol = buf.find('\n', pos);
        if (eol == std::string::npos) eol = buf.size();
        const std::string line = buf.substr(pos, eol - pos);
        pos = (eol < buf.size()) ? (eol + 1) : buf.size();

        // 1. Joined / hosted multiplayer server:
        //    [HH:MM:SS] INF Вошли на сервер "Name" как игрок 0, хост: 0
        size_t pJoined = line.find("\xD0\xBA\xD0\xB0\xD0\xBA \xD0\xB8\xD0\xB3\xD1\x80\xD0\xBE\xD0\xBA "); // "как игрок "
        if (pJoined != std::string::npos) {
            size_t pHost = line.find("\xD1\x85\xD0\xBE\xD1\x81\xD1\x82: ", pJoined); // "хост: "
            if (pHost != std::string::npos) {
                int pid = 0, hid = 0;
                if (std::sscanf(line.c_str() + pJoined + 18, "%d", &pid) == 1 &&
                    std::sscanf(line.c_str() + pHost + 10, "%d", &hid) == 1) {
                    next.inMultiplayer = true;
                    next.isHost        = (pid == hid);
                    next.isClient      = (pid != hid);
                }
            }
            continue;
        }

        // 2. Disconnected / connection lost (Russian & English SA-COOP log lines):
        //    "Соединение с сервером потеряно" / "не установлено" / "Lost connection to the server"
        const bool ruDisc =
            (line.find("\xD0\xA1\xD0\xBE\xD0\xB5\xD0\xB4\xD0\xB8\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD1\x81 \xD1\x81\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBE\xD0\xBC ") != std::string::npos &&
             (line.find("\xD0\xBF\xD0\xBE\xD1\x82\xD0\xB5\xD1\x80\xD1\x8F\xD0\xBD\xD0\xBE") != std::string::npos ||
              line.find("\xD0\xBD\xD0\xB5 \xD1\x83\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBE") != std::string::npos));
        const bool enDisc = (line.find("Lost connection to the server") != std::string::npos);
        if (ruDisc || enDisc) {
            next.inMultiplayer = false;
            next.isHost        = false;
            next.isClient      = false;
            next.campaignId.clear();
            continue;
        }

        // 3. Campaign / playthrough ID ("The host plays game %08X" or "A new game: game %08X")
        size_t pGame = line.find("plays game ");
        if (pGame != std::string::npos) {
            pGame += 11;
        } else {
            pGame = line.find("new game: game ");
            if (pGame != std::string::npos) pGame += 15;
        }
        if (pGame != std::string::npos && pGame + 8 <= line.size()) {
            std::string hex = line.substr(pGame, 8);
            bool validHex = true;
            for (char c : hex) {
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
                    validHex = false;
                    break;
                }
            }
            if (validHex) next.campaignId = hex;
        }
    }

    s_info = std::move(next);
    return s_info;
}

inline bool IsInMultiplayer(bool forceRefresh = false) {
    return IsLoaded() && GetSessionInfo(forceRefresh).inMultiplayer;
}

inline bool IsCoopClient(bool forceRefresh = false) {
    return IsLoaded() && GetSessionInfo(forceRefresh).isClient;
}

inline bool ShouldPersistSlot(int slot1to8) {
    return (slot1to8 >= 1 && slot1to8 <= 8) || IsCoopClient();
}

// Detects unfocused window, Pause Menu, or external ImGui / SA-COOP overlays (F9, F2, F4, T chat)
inline bool IsOverlayActive() {
    if (!IsGameWindowFocused()) return true;
    if (*reinterpret_cast<const uint8_t*>(0x00BA6748 + 0x5C) != 0) return true; // Pause menu
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) != 0) {
        return true;
    }
    return false;
}

inline std::string SanitizeProfileTag(const char* raw) {
    std::string out;
    if (!raw) return out;
    for (const char* p = raw; *p && out.size() < 28; ++p) {
        unsigned char ch = static_cast<unsigned char>(*p);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
            out.push_back(static_cast<char>(ch));
        } else if (ch == ' ' && !out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

inline const char* GetProfileOverride() {
    if (auto* b = modbus::GetBus(); b && b->coopProfileName[0] != '\0') {
        return b->coopProfileName;
    }
    return nullptr;
}

// Cached profile name resolver: checks explicit override, then coop\coop.ini ONCE per session
inline std::string GetPlayerProfileName(const char* overrideName = nullptr) {
    if (!overrideName || !overrideName[0]) {
        overrideName = GetProfileOverride();
    }
    if (overrideName && overrideName[0]) {
        std::string s = SanitizeProfileTag(overrideName);
        if (!s.empty()) return s;
    }

    static bool        s_cached = false;
    static std::string s_cachedName;
    if (s_cached) return s_cachedName;

    char exeBuf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    std::string exeFull(exeBuf);
    size_t slash = exeFull.find_last_of("\\/");
    std::string gDir = (slash != std::string::npos) ? exeFull.substr(0, slash) : ".";

    const std::string iniPaths[3] = {
        gDir + "\\coop\\coop.ini",
        gDir + "\\sa-coop.ini",
        gDir + "\\coop.ini"
    };
    const char* sections[] = {"coop", "player", "network", "game", "general"};
    const char* keys[]     = {"nickname", "name", "player_name", "username", "playername"};

    for (const auto& path : iniPaths) {
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        for (const char* sec : sections) {
            for (const char* key : keys) {
                char val[64] = {};
                GetPrivateProfileStringA(sec, key, "", val, sizeof(val), path.c_str());
                std::string clean = SanitizeProfileTag(val);
                if (!clean.empty()) {
                    s_cachedName = clean;
                    s_cached     = true;
                    return s_cachedName;
                }
            }
        }
    }

    char userBuf[64] = {};
    DWORD userLen = sizeof(userBuf);
    if (GetUserNameA(userBuf, &userLen) && userBuf[0]) {
        std::string clean = SanitizeProfileTag(userBuf);
        if (!clean.empty()) {
            s_cachedName = clean;
            s_cached     = true;
            return s_cachedName;
        }
    }

    s_cachedName = "player";
    s_cached     = true;
    return s_cachedName;
}

inline std::string GetUserFilesSubDir(const char* subFolder, const std::string& fallbackDir) {
    char docs[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)) && docs[0] != '\0') {
        std::string userFiles = std::string(docs) + "\\GTA San Andreas User Files";
        CreateDirectoryA(userFiles.c_str(), nullptr);
        std::string dir = userFiles + "\\" + subFolder;
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    std::string dir = fallbackDir + "\\" + subFolder;
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// Unified save-file resolver:
//   - Singleplayer / SA-COOP Host loaded/saved slots (1..8) -> <prefix>_slot_<1..8>.ini
//   - SA-COOP joining Client (slot 0, IsCoopClient()==true) -> <prefix>_coop_<Nick>[_<CampaignId>].ini
//   - Unsaved Singleplayer / Host New Game (slot 0, !IsCoopClient()) -> "" (in-memory only until saved to a slot!)
inline std::string BuildSaveProfilePath(const char* saveFolder, const std::string& fallbackDir,
                                        const char* filePrefix, int slot1to8,
                                        const char* coopOverride = nullptr)
{
    if (slot1to8 >= 1 && slot1to8 <= 8) {
        std::string dir = GetUserFilesSubDir(saveFolder, fallbackDir);
        char name[96];
        std::snprintf(name, sizeof(name), "\\%s_slot_%d.ini", filePrefix, slot1to8);
        return dir + name;
    }
    if (IsCoopClient()) {
        std::string dir  = GetUserFilesSubDir(saveFolder, fallbackDir);
        std::string nick = GetPlayerProfileName(coopOverride);
        const auto& sess = GetSessionInfo();
        char name[128];
        if (!sess.campaignId.empty() && sess.campaignId != "00000000") {
            std::snprintf(name, sizeof(name), "\\%s_coop_%s_%s.ini",
                          filePrefix, nick.c_str(), sess.campaignId.c_str());
        } else {
            std::snprintf(name, sizeof(name), "\\%s_coop_%s.ini", filePrefix, nick.c_str());
        }
        return dir + name;
    }
    // Unsaved New Game (Singleplayer or Multiplayer Host): never read/write stale slot-0 or coop files!
    return "";
}

// Strict profile path resolver (zero legacy *_autosave.ini leakage into fresh New Games!)
inline std::string ResolveExistingLoadPath(const char* saveFolder, const std::string& fallbackDir,
                                           const char* filePrefix, int slot1to8,
                                           const char* coopOverride = nullptr)
{
    return BuildSaveProfilePath(saveFolder, fallbackDir, filePrefix, slot1to8, coopOverride);
}

// F4 custom skin guard: CClothes::RebuildPlayer only works on CJ (model ID 0)
inline bool IsCustomSkinModel(int16_t pedModelIndex) {
    return pedModelIndex != 0;
}

// Teammate-safe vehicle deletion guard: never DELETE_CAR while another player/ped is driving!
template <typename TVehicle, typename TPed>
inline bool IsSafeToDeleteVehicle(const TVehicle* veh, const TPed* localPed) {
    if (!veh) return true;
    auto* drv = veh->GetDriver();
    return (!drv || drv == localPed);
}

// Safely sends SA-COOP player list hotkey (F2 by default) with scan code & window focus guard
inline bool TriggerPlayerListKey(int vk) {
    if (!IsGameWindowFocused() || vk <= 0 || vk >= 256) return false;
    WORD scan = static_cast<WORD>(MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
    INPUT in[2] = {};
    in[0].type       = INPUT_KEYBOARD;
    in[0].ki.wVk     = static_cast<WORD>(vk);
    in[0].ki.wScan   = scan;
    in[1].type       = INPUT_KEYBOARD;
    in[1].ki.wVk     = static_cast<WORD>(vk);
    in[1].ki.wScan   = scan;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    return true;
}

} // namespace integrations::sacoop
