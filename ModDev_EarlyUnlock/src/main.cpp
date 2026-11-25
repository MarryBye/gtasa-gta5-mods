#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include "log.hpp"
#include "memory.hpp"
#include "sa_sdk.hpp"

namespace {

struct EarlyUnlockConfig {
    bool enableLog             = true;
    bool unlockModShops        = true;
    bool unlockAmmuNation      = true;
    bool unlockClothes         = true;
    bool unlockBarbersTattoos  = true;
    bool unlockGyms            = true;
    bool unlockFoodAndMisc     = true;
    bool addMapIcons           = true;
};

EarlyUnlockConfig g_cfg;

using FnVoid   = void(__cdecl*)();
using FnLoadOp = bool(__cdecl*)(char*);

FnVoid   g_origScriptsProcess = nullptr;
FnLoadOp g_origGenericLoad    = nullptr;

int      g_lastGameState      = -1;
bool     g_wasOnMission       = false;
bool     g_appliedIcons       = false;
uint32_t g_lastUnlockMs       = 0;

// ----------------------------------------------------------------------------
// Verified Mod Shop Data (from main.scm & IPLs)
// ----------------------------------------------------------------------------
struct ModShopUnlock {
    const char* name;
    int32_t     garageType;
    uint32_t    blipGlobalOff;
    float       x, y, z;
};

constexpr ModShopUnlock kModShops[5] = {
    {"BODLAWN", 38, 0x0660,  1043.4f, -1025.3f, 34.4f}, // TransFender LS
    {"MODLAST", 36, 0x0678,  2644.2f, -2028.4f, 12.6f}, // Loco Low Co. LS
    {"MDSSFSE", 38, 0x2874, -1941.0f,   251.4f, 33.4f}, // TransFender SF
    {"MDS1SFS", 37, 0x2870, -2728.5f,   212.2f, 13.6f}, // Wheel Arch Angels SF
    {"VECMOD",  38, 0x2878,  2382.2f,  1044.0f,  9.8f}, // TransFender LV
};

// Verified Exterior Shop Coordinates from data\maps\**\*.ipl for non-duplicate map blips
struct ShopBlipDef {
    uint8_t cat; // 1=Ammu, 2=Clothes, 3=Barber, 4=Tattoo, 5=Gym
    int32_t sprite;
    float   x, y, z;
};

constexpr ShopBlipDef kShopBlips[] = {
    // Ammu-Nation (sprite 6) — 11 locations
    {1, 6,  1368.4f, -1279.1f, 12.6f},
    {1, 6,  2400.5f, -1981.5f, 12.6f},
    {1, 6,   242.7f,  -178.5f,  0.6f},
    {1, 6,  2333.4f,    61.5f, 25.7f},
    {1, 6, -2625.9f,   208.3f,  4.0f},
    {1, 6, -2093.5f, -2464.8f, 29.6f},
    {1, 6,  -315.7f,   829.9f, 13.4f},
    {1, 6, -1508.9f,  2610.8f, 54.9f},
    {1, 6,   777.2f,  1871.5f,  4.0f},
    {1, 6,  2159.5f,   943.3f,  9.8f},
    {1, 6,  2539.0f,  2083.6f,  9.8f},

    // Clothing Shops (sprite 45) — Binco, ProLaps, Sub Urban, ZIP, Victim, Didier Sachs
    {2, 45,  2244.5f, -1665.4f, 14.5f}, // Binco Ganton LS
    {2, 45,  2112.7f, -1211.7f, 23.0f}, // Sub Urban LS
    {2, 45,   499.6f, -1360.4f, 15.4f}, // ProLaps LS
    {2, 45,  1456.8f, -1138.0f, 23.3f}, // ZIP LS
    {2, 45,   461.2f, -1500.0f, 30.2f}, // Victim LS
    {2, 45,   453.9f, -1478.1f, 30.0f}, // Didier Sachs LS
    {2, 45, -2375.3f,   910.3f, 44.5f}, // Binco SF
    {2, 45, -2492.0f,   -29.1f, 24.8f}, // Sub Urban SF
    {2, 45, -1883.2f,   865.5f, 34.3f}, // ZIP SF
    {2, 45, -1694.8f,   951.6f, 24.3f}, // Victim SF
    {2, 45,  1657.0f,  1733.3f, 10.0f}, // Binco LV
    {2, 45,  2102.7f,  2257.5f, 10.1f}, // Binco LV North
    {2, 45,  2779.1f,  2453.5f, 10.1f}, // Sub Urban LV
    {2, 45,  2825.7f,  2407.4f, 10.1f}, // ProLaps LV
    {2, 45,  2090.6f,  2224.2f, 10.1f}, // ZIP LV
    {2, 45,  2572.1f,  1904.8f, 10.0f}, // ZIP LV East
    {2, 45,  2802.3f,  2430.6f, 10.1f}, // Victim LV

    // Barbers (sprite 7)
    {3, 7,  2070.9f, -1793.8f, 12.7f},
    {3, 7,  2723.8f, -2026.7f, 12.6f},
    {3, 7,   823.6f, -1588.9f, 12.6f},
    {3, 7, -2571.2f,   246.7f,  9.6f},
    {3, 7,  2080.4f,  2122.1f,  9.8f},

    // Tattoo Parlors (sprite 39)
    {4, 39,  2068.7f, -1779.8f, 12.5f},
    {4, 39,  1975.8f, -2036.7f, 12.6f},
    {4, 39, -2492.0f,   -39.0f, 24.8f},
    {4, 39,  2094.7f,  2122.1f,  9.8f},

    // Gyms (sprite 54)
    {5, 54,  2229.6f, -1721.6f, 12.7f},
    {5, 54, -2270.5f,  -156.0f, 34.4f},
    {5, 54,  1968.7f,  2295.3f, 15.5f},
};

// Verified CRadar::ms_RadarTrace (0x00BA86F0, 175 entries, stride 0x28):
//   +0x08 CVector m_vecPos
//   +0x24 uint8_t m_nRadarSprite
//   +0x25 uint8_t flags (bit 1 = 0x02 = m_bInUse)
int CountActiveRadarBlips() {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(0x00BA86F0);
    int used = 0;
    for (int i = 0; i < 175; ++i) {
        if ((base[i * 0x28 + 0x25] & 0x02) != 0) ++used;
    }
    return used;
}

bool HasExistingRadarBlip(float x, float y, int32_t sprite, float radius = 45.0f) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(0x00BA86F0);
    const float r2 = radius * radius;
    for (int i = 0; i < 175; ++i) {
        const uint8_t* e = base + i * 0x28;
        if ((e[0x25] & 0x02) == 0) continue;
        if (e[0x24] != static_cast<uint8_t>(sprite)) continue;
        float bx = *reinterpret_cast<const float*>(e + 0x08);
        float by = *reinterpret_cast<const float*>(e + 0x0C);
        float dx = bx - x, dy = by - y;
        if (dx * dx + dy * dy <= r2) return true;
    }
    return false;
}

void UnlockEnexList(const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        sa::ScriptCmd(0x07FB).Str8(names[i]).Int(1).Run();
    }
}

void ApplyShopUnlocks(bool tryAddBlips) {
    // Never fight story mission interior locks while a mission is active!
    if (sa::IsOnMission()) return;

    if (g_cfg.unlockModShops) {
        uint8_t* scriptSpace = reinterpret_cast<uint8_t*>(0x00A49960);
        for (const auto& u : kModShops) {
            sa::ScriptCmd(0x02FA).Str8(u.name).Int(u.garageType).Run();
            sa::ScriptCmd(0x0299).Str8(u.name).Run();
            if (tryAddBlips && g_cfg.addMapIcons) {
                int32_t& blipVar = *reinterpret_cast<int32_t*>(scriptSpace + u.blipGlobalOff);
                if (blipVar == 0 && !HasExistingRadarBlip(u.x, u.y, 27, 45.0f)) {
                    int32_t blip = 0;
                    sa::ScriptCmd(0x04CE).Float(u.x).Float(u.y).Float(u.z).Int(27).Out(blip).Run();
                    if (blip > 0) blipVar = blip;
                }
            }
        }
    }

    if (g_cfg.unlockAmmuNation) {
        static constexpr const char* kAmmu[] = {"AMMUN1", "AMMUN2", "AMMUN3", "AMMUN4", "AMMUN5"};
        UnlockEnexList(kAmmu, sizeof(kAmmu) / sizeof(kAmmu[0]));
    }

    if (g_cfg.unlockClothes) {
        static constexpr const char* kClothes[] = {"CSCHP", "CSSPRT", "LACS1", "CLOTHGP", "CSDESGN", "CSEXL"};
        UnlockEnexList(kClothes, sizeof(kClothes) / sizeof(kClothes[0]));
    }

    if (g_cfg.unlockBarbersTattoos) {
        static constexpr const char* kStyle[] = {"BARBERS", "BARBER2", "BARBER3", "TATTOO", "TATTO2", "TATTO3"};
        UnlockEnexList(kStyle, sizeof(kStyle) / sizeof(kStyle[0]));
    }

    if (g_cfg.unlockGyms) {
        static constexpr const char* kGyms[] = {"GYM1", "GYM2", "GYM3"};
        UnlockEnexList(kGyms, sizeof(kGyms) / sizeof(kGyms[0]));
    }

    if (g_cfg.unlockFoodAndMisc) {
        static constexpr const char* kFood[] = {
            "FDBURG", "FDCHICK", "FDPIZA", "FDREST1", "FDDONUT",
            "DINER1", "DINER2", "REST2", "TSDINER", "GENOTB", "SEXSHOP"
        };
        UnlockEnexList(kFood, sizeof(kFood) / sizeof(kFood[0]));
    }

    if (tryAddBlips && g_cfg.addMapIcons && !g_appliedIcons) {
        g_appliedIcons = true;
        int added = 0;
        int used  = CountActiveRadarBlips();
        for (const auto& b : kShopBlips) {
            if (used >= 152) break; // Always preserve safe headroom for mission/story radar blips!
            bool enabled = false;
            if (b.cat == 1) enabled = g_cfg.unlockAmmuNation;
            else if (b.cat == 2) enabled = g_cfg.unlockClothes;
            else if (b.cat == 3 || b.cat == 4) enabled = g_cfg.unlockBarbersTattoos;
            else if (b.cat == 5) enabled = g_cfg.unlockGyms;
            if (!enabled) continue;

            if (!HasExistingRadarBlip(b.x, b.y, b.sprite, 45.0f)) {
                int32_t blip = 0;
                sa::ScriptCmd(0x04CE).Float(b.x).Float(b.y).Float(b.z).Int(b.sprite).Out(blip).Run();
                if (blip > 0) {
                    ++added;
                    ++used;
                }
            }
        }
        modlog::Info("[EarlyUnlock] Shops unlocked cleanly (added %d missing radar icons, total blips=%d/175).", added, used);
    }
}

bool __cdecl HookedGenericLoad(char* outFlag) {
    bool ok = g_origGenericLoad ? g_origGenericLoad(outFlag) : false;
    if (ok) {
        g_appliedIcons = false;
        g_lastUnlockMs = 0;
    }
    return ok;
}

void __cdecl HookedScriptsProcess() {
    if (g_origScriptsProcess) {
        g_origScriptsProcess();
    }

    const int gs = sa::GetGameState();
    if (gs == 9) {
        if (g_lastGameState != 9) {
            g_appliedIcons = false;
            g_lastUnlockMs = 0;
        }
        bool onMission = sa::IsOnMission();
        uint32_t now = sa::GetTimeMs();

        // Apply immediately on enter world (after 1.5s so main.scm init finishes),
        // right when exiting a mission, and periodically every 4s outside interiors
        if (!onMission && sa::GetActiveInterior() == 0) {
            bool missionEnded = g_wasOnMission && !onMission;
            bool needIcons = !g_appliedIcons && (now > 1500);
            if (missionEnded || needIcons || (g_lastUnlockMs == 0) || (now - g_lastUnlockMs >= 4000)) {
                g_lastUnlockMs = now;
                ApplyShopUnlocks(needIcons || missionEnded);
            }
        }
        g_wasOnMission = onMission;
    } else if (g_lastGameState == 9) {
        g_appliedIcons = false;
        g_lastUnlockMs = 0;
        g_wasOnMission = false;
    }
    g_lastGameState = gs;
}

void LoadConfig(HMODULE selfModule) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(selfModule, buf, MAX_PATH);
    std::string full(buf);
    size_t slash = full.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";
    std::string ini = dir + "\\GTA5EarlyUnlock.ini";
    const char* p = ini.c_str();

    g_cfg.enableLog            = GetPrivateProfileIntA("General", "EnableLog", 1, p) != 0;
    g_cfg.unlockModShops       = GetPrivateProfileIntA("Unlock", "UnlockModShops", 1, p) != 0;
    g_cfg.unlockAmmuNation     = GetPrivateProfileIntA("Unlock", "UnlockAmmuNation", 1, p) != 0;
    g_cfg.unlockClothes        = GetPrivateProfileIntA("Unlock", "UnlockClothes", 1, p) != 0;
    g_cfg.unlockBarbersTattoos = GetPrivateProfileIntA("Unlock", "UnlockBarbersAndTattoos", 1, p) != 0;
    g_cfg.unlockGyms           = GetPrivateProfileIntA("Unlock", "UnlockGyms", 1, p) != 0;
    g_cfg.unlockFoodAndMisc    = GetPrivateProfileIntA("Unlock", "UnlockFoodAndMisc", 1, p) != 0;
    g_cfg.addMapIcons          = GetPrivateProfileIntA("Unlock", "AddMapIcons", 1, p) != 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadConfig(hModule);
        modlog::Enabled() = g_cfg.enableLog;
        modlog::Init(hModule, "GTA5EarlyUnlock");
        if (!sa::IsSA10US()) {
            modlog::Info("[Init] ERROR: Unsupported GTA SA exe! Requires 1.0 US (Hoodlum).");
            return TRUE;
        }
        g_origScriptsProcess = reinterpret_cast<FnVoid>(
            mem::HookCall(0x0053BFC7, reinterpret_cast<uintptr_t>(&HookedScriptsProcess)));
        g_origGenericLoad = reinterpret_cast<FnLoadOp>(
            mem::HookCall(0x0053C70B, reinterpret_cast<uintptr_t>(&HookedGenericLoad)));
        modlog::Info("GTA5EarlyUnlock v1.0 attached (ModShops=%d, Ammu=%d, Clothes=%d, Style=%d, Gyms=%d, Food=%d, Blips=%d).",
                     g_cfg.unlockModShops, g_cfg.unlockAmmuNation, g_cfg.unlockClothes,
                     g_cfg.unlockBarbersTattoos, g_cfg.unlockGyms, g_cfg.unlockFoodAndMisc, g_cfg.addMapIcons);
    }
    return TRUE;
}
