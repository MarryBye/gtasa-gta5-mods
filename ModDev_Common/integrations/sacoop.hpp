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

// ============================================================================
// Native SA-COOP 0.3.4 (coop.asi) Signature-Verified UI, Theme & Flow Bridge
// Every RVA is verified against exact machine-code prologues / immediate bytes
// before any call or patch — completely inert & safe on unknown versions!
// ============================================================================
namespace detail {

inline uintptr_t GetCoopAsiBase() {
    static uintptr_t s_base = 0;
    static bool      s_checked = false;
    if (!s_checked) {
        s_checked = true;
        s_base = reinterpret_cast<uintptr_t>(GetModuleHandleA("coop.asi"));
    }
    return s_base;
}

inline bool MatchBytes(uintptr_t addr, const uint8_t* pat, size_t len) {
    if (!addr) return false;
    const volatile uint8_t* p = reinterpret_cast<const volatile uint8_t*>(addr);
    for (size_t i = 0; i < len; ++i) {
        if (p[i] != pat[i]) return false;
    }
    return true;
}

inline bool MatchByte(uintptr_t addr, uint8_t expected) {
    if (addr < 0x10000u) return false;
    uint8_t val = 0;
    std::memcpy(&val, reinterpret_cast<const void*>(addr), 1);
    return val == expected;
}

inline bool PatchU32IfMatches(uintptr_t addr, uint32_t expected, uint32_t replacement) {
    if (!addr) return false;
    auto* p = reinterpret_cast<uint32_t*>(addr);
    if (*p != expected) return false;
    DWORD oldProt = 0;
    if (!VirtualProtect(p, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    *p = replacement;
    VirtualProtect(p, sizeof(uint32_t), oldProt, &oldProt);
    return true;
}

// Read-only Clang 32-bit libc++ std::__1::basic_string<T> long-mode view (12 bytes):
// Bit 0 of capFlags == 1 => long string; coop.asi only reads [size, data] for const ref args!
template <typename CharT>
struct LibCxxConstStrView {
    uint32_t     capFlags;
    uint32_t     size;
    const CharT* data;

    LibCxxConstStrView(const CharT* str, size_t len)
        : capFlags(static_cast<uint32_t>((len + 16u) | 1u)),
          size(static_cast<uint32_t>(len)),
          data(str) {}
};

// Read a Clang 32-bit libc++ std::__1::string (handles both SSO and long-mode safely)
inline std::string ReadLibCxxString(const void* ptr) {
    if (!ptr) return "";
    const auto* bytes = reinterpret_cast<const uint8_t*>(ptr);
    if ((bytes[0] & 1u) != 0) {
        uint32_t sz = *reinterpret_cast<const uint32_t*>(bytes + 4);
        const char* data = *reinterpret_cast<const char* const*>(bytes + 8);
        if (!data || sz == 0 || sz > 512) return "";
        return std::string(data, sz);
    } else {
        uint32_t sz = static_cast<uint32_t>(bytes[0] >> 1);
        if (sz == 0 || sz > 11) return "";
        return std::string(reinterpret_cast<const char*>(bytes + 1), sz);
    }
}

inline bool ReadBoolGetterAtRva(uintptr_t rva) {
    uintptr_t base = GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + rva;
    // Expected: 0F B6 05 xx xx xx xx C3  (movzx eax, BYTE PTR [addr]; ret)
    const auto* p = reinterpret_cast<const uint8_t*>(fn);
    if (p[0] != 0x0F || p[1] != 0xB6 || p[2] != 0x05 || p[7] != 0xC3) return false;
    using Fn = bool(__cdecl*)();
    return reinterpret_cast<Fn>(fn)();
}

} // namespace detail

inline bool IsConnectMenuVisible() { return detail::ReadBoolGetterAtRva(0x11DB90); }
inline bool IsPlayersMenuVisible() { return detail::ReadBoolGetterAtRva(0x123520); }
inline bool IsSkinsMenuVisible()   { return detail::ReadBoolGetterAtRva(0x127840); }
inline bool IsAnimsMenuVisible()   { return detail::ReadBoolGetterAtRva(0x11A460); }

inline bool IsAnyCoopMenuVisible() {
    if (!IsLoaded()) return false;
    return IsConnectMenuVisible() || IsPlayersMenuVisible() ||
           IsSkinsMenuVisible()   || IsAnimsMenuVisible();
}

inline bool SetConnectMenuVisible(bool visible) {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x11D910;
    static constexpr uint8_t kSig[] = {0x57, 0x56, 0x83, 0xEC, 0x30, 0x0F, 0xB6, 0x44, 0x24, 0x3C};
    if (!detail::MatchBytes(fn, kSig, sizeof(kSig))) return false;
    using Fn = void(__cdecl*)(bool);
    reinterpret_cast<Fn>(fn)(visible);
    return true;
}

// Suppresses SA-COOP's automatic F9 connect popup on boot (sets s_didAutoOpen = 1 in ProcessConnectMenu)
inline bool SuppressStartupConnectPopup() {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x11DBA0;
    const auto* p = reinterpret_cast<const uint8_t*>(fn);
    // 1011dba0: 80 3d [addr4] 00 75 25 83 3d c0 d4 c8 00 07
    if (p[0] != 0x80 || p[1] != 0x3D || p[6] != 0x00 ||
        p[7] != 0x75 || p[8] != 0x25 || p[9] != 0x83 || p[10] != 0x3D) {
        return false;
    }
    uintptr_t flagAddr = *reinterpret_cast<const uintptr_t*>(p + 2);
    if (flagAddr > base && flagAddr < base + 0x00400000u) {
        *reinterpret_cast<uint8_t*>(flagAddr) = 1;
    }
    if (IsConnectMenuVisible()) {
        SetConnectMenuVisible(false);
    }
    return true;
}

// Live instantaneous multiplayer world checks (verified coop::flow::* functions)
inline bool IsInSharedWorldLive() {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x0334F0;
    const auto* p = reinterpret_cast<const uint8_t*>(fn);
    // E8 xx xx xx xx 83 B8 70 01 00 00 02
    if (p[0] != 0xE8 || p[5] != 0x83 || p[6] != 0xB8 || p[7] != 0x70) return false;
    using Fn = bool(__cdecl*)();
    return reinterpret_cast<Fn>(fn)();
}

inline bool IsInHostWorldLive() {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x033490;
    const auto* p = reinterpret_cast<const uint8_t*>(fn);
    // E8 xx xx xx xx 89 C1 31 C0
    if (p[0] != 0xE8 || p[5] != 0x89 || p[6] != 0xC1 || p[7] != 0x31) return false;
    using Fn = bool(__cdecl*)();
    return reinterpret_cast<Fn>(fn)();
}

inline bool IsFollowerLive() {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x033450;
    const auto* p = reinterpret_cast<const uint8_t*>(fn);
    // 80 3D xx xx xx xx 01 75 30
    if (p[0] != 0x80 || p[1] != 0x3D || p[6] != 0x01 || p[7] != 0x75) return false;
    using Fn = bool(__cdecl*)();
    return reinterpret_cast<Fn>(fn)();
}

inline bool IsMultiplayerSessionActive() {
    if (!IsLoaded()) return false;
    return IsInSharedWorldLive() || IsInHostWorldLive() || IsFollowerLive() || IsInMultiplayer();
}

inline std::string GetLastFlowError() {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return "";
    uintptr_t fn = base + 0x02CA70;
    const auto* p = reinterpret_cast<const uint8_t*>(fn);
    // B8 xx xx xx xx C3
    if (p[0] != 0xB8 || p[5] != 0xC3) return "";
    using Fn = const void*(__cdecl*)();
    return detail::ReadLibCxxString(reinterpret_cast<Fn>(fn)());
}

// Restyles ALL SA-COOP UI widgets (w::Panel, w::MenuItem, w::Checkbox, w::TextBox, w::Selector)
// from blue-slate to our sleek modernized classic dark GTA SA UI Kit palette (idempotent & sig-checked!)
inline int ApplyDarkUiKitTheme() {
    static int s_patched = -1;
    if (s_patched >= 0) return s_patched;
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) { s_patched = 0; return 0; }

    int count = 0;
    // 1. coop::ui::w::Panel (fill & outline)
    if (detail::MatchBytes(base + 0x1405A4, (const uint8_t*)"\xC7\x44\x24\x10", 4))
        count += detail::PatchU32IfMatches(base + 0x1405A8, 0xFF000000u, 0xDC0A0A0Du) ? 1 : 0;
    if (detail::MatchBytes(base + 0x1405F8, (const uint8_t*)"\xC7\x44\x24\x10", 4))
        count += detail::PatchU32IfMatches(base + 0x1405FC, 0xFF26303Bu, 0x88B9B9C0u) ? 1 : 0;

    // 2. coop::ui::w::MenuItem (Buttons & Tabs: active/hover, normal, disabled)
    if (detail::MatchByte(base + 0x14088E, 0xB8))
        count += detail::PatchU32IfMatches(base + 0x14088F, 0xFFACCBF1u, 0xFFFFFFFFu) ? 1 : 0;
    if (detail::MatchByte(base + 0x140893, 0xB9))
        count += detail::PatchU32IfMatches(base + 0x140894, 0xFF4A5A6Bu, 0xFFCCCCD0u) ? 1 : 0;
    if (detail::MatchByte(base + 0x1408AB, 0xB8))
        count += detail::PatchU32IfMatches(base + 0x1408AC, 0xFF353F4Au, 0xFF707076u) ? 1 : 0;

    // 3. coop::ui::w::Checkbox (box fill & border)
    if (detail::MatchBytes(base + 0x141BDE, (const uint8_t*)"\xC7\x44\x24\x10", 4))
        count += detail::PatchU32IfMatches(base + 0x141BE2, 0xFF090C10u, 0xEB101014u) ? 1 : 0;
    if (detail::MatchByte(base + 0x141BF0, 0xB8))
        count += detail::PatchU32IfMatches(base + 0x141BF1, 0xFF4A5A6Bu, 0x90B9B9C0u) ? 1 : 0;

    // 4. coop::ui::w::TextBox (borders, input fill, placeholder)
    if (detail::MatchByte(base + 0x140AFC, 0xBB))
        count += detail::PatchU32IfMatches(base + 0x140AFD, 0xFF26303Bu, 0x88B9B9C0u) ? 1 : 0;
    if (detail::MatchByte(base + 0x140B36, 0xBB))
        count += detail::PatchU32IfMatches(base + 0x140B37, 0xFF4A5A6Bu, 0xFFEBEBF0u) ? 1 : 0;
    if (detail::MatchBytes(base + 0x140C2D, (const uint8_t*)"\xC7\x44\x24\x10", 4))
        count += detail::PatchU32IfMatches(base + 0x140C31, 0xFF090C10u, 0xEB101014u) ? 1 : 0;
    if (detail::MatchBytes(base + 0x14106F, (const uint8_t*)"\xC7\x44\x24\x14", 4))
        count += detail::PatchU32IfMatches(base + 0x141073, 0xFF4A5A6Bu, 0xFF96969Cu) ? 1 : 0;

    // 5. coop::ui::w::Selector (fill, border, arrows hover/normal)
    if (detail::MatchBytes(base + 0x1417A2, (const uint8_t*)"\xC7\x44\x24\x10", 4))
        count += detail::PatchU32IfMatches(base + 0x1417A6, 0xFF090C10u, 0xEB101014u) ? 1 : 0;
    if (detail::MatchBytes(base + 0x1417FF, (const uint8_t*)"\xC7\x44\x24\x10", 4))
        count += detail::PatchU32IfMatches(base + 0x141803, 0xFF26303Bu, 0x88B9B9C0u) ? 1 : 0;
    if (detail::MatchBytes(base + 0x1418C5, (const uint8_t*)"\xC7\x44\x24\x14", 4))
        count += detail::PatchU32IfMatches(base + 0x1418C9, 0xFFACCBF1u, 0xFFFFFFFFu) ? 1 : 0;
    if (detail::MatchBytes(base + 0x14190B, (const uint8_t*)"\xC7\x44\x24\x14", 4))
        count += detail::PatchU32IfMatches(base + 0x14190F, 0xFF4A5A6Bu, 0xFFCCCCD0u) ? 1 : 0;
    if (detail::MatchBytes(base + 0x1419F4, (const uint8_t*)"\xC7\x44\x24\x14", 4))
        count += detail::PatchU32IfMatches(base + 0x1419F8, 0xFFACCBF1u, 0xFFFFFFFFu) ? 1 : 0;
    if (detail::MatchBytes(base + 0x141A40, (const uint8_t*)"\xC7\x44\x24\x14", 4))
        count += detail::PatchU32IfMatches(base + 0x141A44, 0xFF4A5A6Bu, 0xFFCCCCD0u) ? 1 : 0;

    s_patched = count;
    return count;
}

// ============================================================================
// SA-COOP Configuration (coop\coop.ini) + Native Host / Join Launchers
// ============================================================================
struct CoopSettings {
    std::string playerName      = "Player";
    std::string mode            = "host";
    std::string host            = "127.0.0.1";
    int         port            = 7788;
    std::string password;
    int         loadSlot        = 0; // 0 = New Game, 1..8 = Save Slot
    int         maxPlayers      = 2;
    bool        friendlyFire    = false;
    bool        partnerTeleport = false;
    bool        sharedLife      = false;
    bool        partnerCheats   = false;
    bool        voiceEnabled    = true;
    bool        voiceProximity  = true;
    bool        gps             = true;
};

inline std::string GetCoopIniPath() {
    return GetGameExeDir() + "\\coop\\coop.ini";
}

inline CoopSettings LoadCoopSettings() {
    CoopSettings s{};
    std::string path = GetCoopIniPath();
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return s;

    auto readStr = [&](const char* sec, const char* key, const char* def) -> std::string {
        char buf[128] = {};
        GetPrivateProfileStringA(sec, key, def, buf, sizeof(buf), path.c_str());
        return std::string(buf);
    };
    s.playerName      = readStr("player", "name", "Player");
    s.mode            = readStr("connect", "mode", "host");
    s.host            = readStr("connect", "host", "127.0.0.1");
    s.port            = static_cast<int>(GetPrivateProfileIntA("connect", "port", 7788, path.c_str()));
    s.password        = readStr("connect", "password", "");
    s.loadSlot        = static_cast<int>(GetPrivateProfileIntA("connect", "load_slot", 0, path.c_str()));
    s.maxPlayers      = static_cast<int>(GetPrivateProfileIntA("connect", "max_players", 2, path.c_str()));
    s.friendlyFire    = GetPrivateProfileIntA("connect", "friendly_fire", 0, path.c_str()) != 0;
    s.partnerTeleport = GetPrivateProfileIntA("connect", "partner_teleport", 0, path.c_str()) != 0;
    s.sharedLife      = GetPrivateProfileIntA("connect", "shared_life", 0, path.c_str()) != 0;
    s.partnerCheats   = GetPrivateProfileIntA("connect", "partner_cheats", 0, path.c_str()) != 0;
    s.voiceEnabled    = GetPrivateProfileIntA("voice", "enabled", 1, path.c_str()) != 0;
    s.voiceProximity  = GetPrivateProfileIntA("voice", "proximity", 1, path.c_str()) != 0;
    s.gps             = GetPrivateProfileIntA("game", "gps", 1, path.c_str()) != 0;

    if (s.port < 1 || s.port > 65535) s.port = 7788;
    if (s.loadSlot < 0 || s.loadSlot > 8) s.loadSlot = 0;
    if (s.maxPlayers < 2 || s.maxPlayers > 8) s.maxPlayers = 2;
    return s;
}

inline bool ReloadCoopConfigInMemory() {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fnCfg  = base + 0x130000;
    uintptr_t fnLoad = base + 0x130420;
    // Verify Load prologue (push ebp/ebx/edi/esi)
    static constexpr uint8_t kSigLoad[] = {0x55, 0x53, 0x57, 0x56};
    if (!detail::MatchBytes(fnLoad, kSigLoad, sizeof(kSigLoad))) return false;

    using CfgFn  = void*(__cdecl*)();
    using LoadFn = void(__thiscall*)(void*, const void*);
    void* cfg = reinterpret_cast<CfgFn>(fnCfg)();
    if (!cfg) return false;

    std::string pathA = GetCoopIniPath();
    std::wstring pathW;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, pathA.c_str(), -1, nullptr, 0);
    if (wlen > 1) {
        pathW.resize(static_cast<size_t>(wlen - 1));
        MultiByteToWideChar(CP_UTF8, 0, pathA.c_str(), -1, pathW.data(), wlen);
    } else {
        return false;
    }
    detail::LibCxxConstStrView<wchar_t> view(pathW.c_str(), pathW.size());
    reinterpret_cast<LoadFn>(fnLoad)(cfg, &view);
    return true;
}

inline bool SaveCoopSettings(const CoopSettings& s) {
    std::string path = GetCoopIniPath();
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    char portBuf[16], slotBuf[16], maxPBuf[16];
    std::snprintf(portBuf, sizeof(portBuf), "%d", (s.port >= 1 && s.port <= 65535) ? s.port : 7788);
    std::snprintf(slotBuf, sizeof(slotBuf), "%d", (s.loadSlot >= 0 && s.loadSlot <= 8) ? s.loadSlot : 0);
    std::snprintf(maxPBuf, sizeof(maxPBuf), "%d", (s.maxPlayers >= 2 && s.maxPlayers <= 8) ? s.maxPlayers : 2);

    WritePrivateProfileStringA("player",  "name",             s.playerName.c_str(), path.c_str());
    WritePrivateProfileStringA("connect", "mode",             s.mode.c_str(), path.c_str());
    WritePrivateProfileStringA("connect", "host",             s.host.c_str(), path.c_str());
    WritePrivateProfileStringA("connect", "port",             portBuf, path.c_str());
    WritePrivateProfileStringA("connect", "password",         s.password.c_str(), path.c_str());
    WritePrivateProfileStringA("connect", "load_slot",        slotBuf, path.c_str());
    WritePrivateProfileStringA("connect", "max_players",      maxPBuf, path.c_str());
    WritePrivateProfileStringA("connect", "friendly_fire",    s.friendlyFire ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("connect", "partner_teleport", s.partnerTeleport ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("connect", "shared_life",      s.sharedLife ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("connect", "partner_cheats",   s.partnerCheats ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("voice",   "enabled",          s.voiceEnabled ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("voice",   "proximity",        s.voiceProximity ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("game",    "gps",              s.gps ? "1" : "0", path.c_str());

    ReloadCoopConfigInMemory();
    return true;
}

inline bool LaunchHost(int slot0to8, uint16_t port, const std::string& password) {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x02D7E0;
    static constexpr uint8_t kSig[] = {0x55, 0x89, 0xE5, 0x53, 0x57, 0x56, 0x83, 0xE4, 0xF0};
    if (!detail::MatchBytes(fn, kSig, sizeof(kSig))) return false;
    detail::LibCxxConstStrView<char> passView(password.c_str(), password.size());
    using Fn = void(__cdecl*)(int, uint32_t, const void*);
    reinterpret_cast<Fn>(fn)(slot0to8, static_cast<uint32_t>(port), &passView);
    return true;
}

inline bool LaunchJoin(const std::string& host, uint16_t port, const std::string& password) {
    uintptr_t base = detail::GetCoopAsiBase();
    if (!base) return false;
    uintptr_t fn = base + 0x0304D0;
    static constexpr uint8_t kSig[] = {0x55, 0x53, 0x57, 0x56, 0x83, 0xEC, 0x6C};
    if (!detail::MatchBytes(fn, kSig, sizeof(kSig))) return false;
    detail::LibCxxConstStrView<char> hostView(host.c_str(), host.size());
    detail::LibCxxConstStrView<char> passView(password.c_str(), password.size());
    using Fn = void(__cdecl*)(const void*, uint32_t, const void*);
    reinterpret_cast<Fn>(fn)(&hostView, static_cast<uint32_t>(port), &passView);
    return true;
}

} // namespace integrations::sacoop
