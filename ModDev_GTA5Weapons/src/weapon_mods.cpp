#include "weapon_mods.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "integrations/widescreen_fix.hpp"
#include "log.hpp"
#include "weapon_config.hpp"
#include "weapon_storage.hpp"

namespace wmods {

namespace {

bool                 g_stockCaptured = false;
sa::CWeaponInfo      g_stockTable[sa::WEAPON_INFO_COUNT] = {};

uint32_t             g_appliedWeaponId  = 0;
wdata::WeaponLoadout g_appliedLoadout{};
float                g_appliedSkill01   = 0.0f;
int8_t               g_appliedSkillTier = -1;
bool                 g_hasAppliedMod    = false;

bool                                      g_zoomActive    = false;
float                                     g_zoomTargetFov = 70.0f;
integrations::widescreen::ScopeFovGuard   g_scopeFovGuard{};

struct LoadoutMultipliers {
    float dmg      = 1.0f;
    float acc      = 1.0f;
    float rng      = 1.0f;
    float rof      = 1.0f; // > 1.0 = faster fire rate
    float clip     = 1.0f;
    bool  moveFire = false;
};

// Verified against CWeaponInfo::FindWeaponStat (0x00743CD0) in GTA SA 1.0 US
inline uint16_t GetWeaponSkillStatId(uint32_t weaponId) {
    switch (weaponId) {
        case 22: return sa::stats::STAT_WEAPON_PISTOL;
        case 23: return sa::stats::STAT_WEAPON_SILENCED;
        case 24: return sa::stats::STAT_WEAPON_DESERT_EAGLE;
        case 25: return sa::stats::STAT_WEAPON_SHOTGUN;
        case 26: return sa::stats::STAT_WEAPON_SAWNOFF;
        case 27: return sa::stats::STAT_WEAPON_SPAS12;
        case 28:
        case 32: return sa::stats::STAT_WEAPON_MICRO_UZI;
        case 29: return sa::stats::STAT_WEAPON_MP5;
        case 30: return sa::stats::STAT_WEAPON_AK47;
        case 31: return sa::stats::STAT_WEAPON_M4;
        case 33:
        case 34: return sa::stats::STAT_WEAPON_SNIPER;
        default: return 0;
    }
}

inline float GetPlayerWeaponSkill01(uint32_t weaponId) {
    uint16_t statId = GetWeaponSkillStatId(weaponId);
    if (statId == 0) return 0.0f;
    return sa::stats::GetNormalized01(statId);
}

// Verified against CWeaponInfo::GetWeaponInfo (0x00743C60) & weapon.dat in GTA SA 1.0 US:
// ONLY weapons 22..32 have separate Poor(0)/Gangster(1)/Hitman(2)/Cop(3) entries in aWeaponInfo!
// Calling GetWeaponInfo(33+, 0/2/3) aliases into OTHER weapons or out-of-bounds memory!
inline bool WeaponHasSkillTiers(uint32_t weaponId) {
    return weaponId >= 22 && weaponId <= 32;
}

inline int8_t GetPlayerWeaponSkillTier(const sa::CPed* ped, uint32_t weaponId) {
    if (!WeaponHasSkillTiers(weaponId)) return 1;
    if (sa::IsPedValid(ped)) {
        return std::clamp<int8_t>(ped->GetWeaponSkill(weaponId), 0, 2);
    }
    uint16_t statId = GetWeaponSkillStatId(weaponId);
    if (statId == 0) return 1;
    float statVal = sa::stats::Get(statId);
    if (const auto* w2 = sa::GetWeaponInfo(weaponId, 2); w2 && statVal >= static_cast<float>(w2->m_nReqStatLevel)) return 2;
    if (const auto* w1 = sa::GetWeaponInfo(weaponId, 1); w1 && statVal >= static_cast<float>(w1->m_nReqStatLevel)) return 1;
    return 0;
}

LoadoutMultipliers ComputeLoadoutMultipliers(const wdata::WeaponLoadout& L, float skill01 = 0.0f) {
    LoadoutMultipliers m{};
    if (L.muzzle == 1) {        // Suppressor
        m.dmg *= 0.92f;
        m.acc *= 1.12f;
        m.rng *= 0.90f;
    } else if (L.muzzle == 2) { // Compensator
        m.acc *= 1.22f;
        m.rof *= 1.06f;
    }

    if (L.mag == 1)      m.clip = 1.60f; // Extended Mag
    else if (L.mag == 2) m.clip = 2.00f; // Drum Mag

    if (L.scope == 1) {         // Reflex Sight
        m.acc *= 1.16f;
        m.rng *= 1.08f;
    } else if (L.scope == 2) {  // Tactical Scope
        m.acc *= 1.28f;
        m.rng *= 1.25f;
    }

    if (L.grip == 1) {          // Vertical Foregrip
        m.acc      *= 1.20f;
        m.moveFire  = true;
    }

    if (L.barrel == 1) {        // Heavy Barrel
        m.dmg *= 1.22f;
        m.rng *= 1.20f;
        m.rof *= 1.15f;
    }

    if (L.laser != 0) {         // Laser Sight
        m.acc *= 1.10f;
    }

    // Modest continuous CJ weapon mastery layered cleanly over vanilla Poor->Gangster->Hitman tiers
    if (skill01 > 0.001f) {
        const float sk = std::clamp(skill01, 0.0f, 1.0f);
        m.acc *= (1.0f + 0.05f * sk);
        m.rof *= (1.0f + 0.03f * sk);
        m.rng *= (1.0f + 0.04f * sk);
    }
    return m;
}

int ComputeModifiedClip(int baseClip, uint8_t magTier, float clipMult) {
    if (clipMult <= 1.01f || baseClip < 1) return baseClip;
    int newClip = static_cast<int>(std::round(static_cast<float>(baseClip) * clipMult));
    int minGain = (magTier == 2) ? 4 : 2;
    if (newClip < baseClip + minGain) newClip = baseClip + minGain;
    return std::clamp(newClip, 1, 250);
}

int InfoIndex(const sa::CWeaponInfo* ptr) {
    if (!ptr) return -1;
    const auto* base = sa::GetWeaponInfoTable();
    ptrdiff_t idx = ptr - base;
    if (idx < 0 || idx >= sa::WEAPON_INFO_COUNT) return -1;
    return static_cast<int>(idx);
}

void RestoreWeaponStock(uint32_t weaponId) {
    if (!g_stockCaptured || weaponId == 0) return;
    const int8_t skMin = WeaponHasSkillTiers(weaponId) ? 0 : 1;
    const int8_t skMax = WeaponHasSkillTiers(weaponId) ? 3 : 1;
    for (int8_t sk = skMin; sk <= skMax; ++sk) {
        sa::CWeaponInfo* wi = sa::GetWeaponInfo(weaponId, sk);
        int idx = InfoIndex(wi);
        if (idx >= 0) {
            *wi = g_stockTable[idx];
        }
    }
}

void ApplyLoadoutToWeapon(uint32_t weaponId, const wdata::WeaponLoadout& rawL, float skill01) {
    if (!g_stockCaptured || weaponId == 0) return;

    const wdata::WeaponLoadout L = wdata::SanitizeLoadout(weaponId, rawL);
    const LoadoutMultipliers   m = ComputeLoadoutMultipliers(L, skill01);
    const int8_t skMin = WeaponHasSkillTiers(weaponId) ? 0 : 1;
    const int8_t skMax = WeaponHasSkillTiers(weaponId) ? 2 : 1;

    for (int8_t sk = skMin; sk <= skMax; ++sk) {
        sa::CWeaponInfo* wi = sa::GetWeaponInfo(weaponId, sk);
        int idx = InfoIndex(wi);
        if (idx < 0) continue;

        // Start cleanly from stock tier (0=Poor, 1=Gangster, 2=Hitman for 22..32; ONLY 1=Std for others!)
        // Preserves ALL vanilla skill jumps, WEAPON_FLAG_TWIN dual-wield, 2x clips & MOVEFIRE flags!
        const sa::CWeaponInfo& base = g_stockTable[idx];
        *wi = base;

        int newDmg = static_cast<int>(std::round(static_cast<float>(base.m_nDamage) * m.dmg));
        wi->m_nDamage      = static_cast<uint16_t>(std::clamp(newDmg, 1, 600));
        wi->m_fAccuracy    = std::clamp(base.m_fAccuracy * m.acc, 0.2f, 4.5f);
        wi->m_fWeaponRange = std::clamp(base.m_fWeaponRange * m.rng, 5.0f, 180.0f);
        wi->m_fTargetRange = std::clamp(base.m_fTargetRange * m.rng, 5.0f, 120.0f);
        wi->m_nAmmoClip    = static_cast<uint16_t>(ComputeModifiedClip(static_cast<int>(base.m_nAmmoClip), L.mag, m.clip));

        if (m.rof > 1.005f && base.m_fAnimLoopEnd > base.m_fAnimLoopStart + 0.05f) {
            float dur = std::max((base.m_fAnimLoopEnd - base.m_fAnimLoopStart) / m.rof, 0.055f);
            wi->m_fAnimLoopEnd = base.m_fAnimLoopStart + dur;
            if (wi->m_fAnimLoopFire > wi->m_fAnimLoopEnd - 0.01f) {
                wi->m_fAnimLoopFire = wi->m_fAnimLoopEnd - 0.01f;
            }
            if (base.m_fAnim2LoopEnd > base.m_fAnim2LoopStart + 0.05f) {
                float dur2 = std::max((base.m_fAnim2LoopEnd - base.m_fAnim2LoopStart) / m.rof, 0.055f);
                wi->m_fAnim2LoopEnd = base.m_fAnim2LoopStart + dur2;
                if (wi->m_fAnim2LoopFire > wi->m_fAnim2LoopEnd - 0.01f) {
                    wi->m_fAnim2LoopFire = wi->m_fAnim2LoopEnd - 0.01f;
                }
            }
        }

        if (m.moveFire) {
            wi->m_nFlags |= (sa::WEAPON_FLAG_MOVEAIM | sa::WEAPON_FLAG_MOVEFIRE);
            wi->m_fMoveSpeed = std::max(base.m_fMoveSpeed, 1.15f);
        }
    }
}

bool SameLoadout(const wdata::WeaponLoadout& a, const wdata::WeaponLoadout& b) {
    return a.muzzle == b.muzzle && a.mag == b.mag && a.scope == b.scope &&
           a.grip == b.grip && a.barrel == b.barrel && a.laser == b.laser;
}

void TickScopeZoom(const sa::CPed* ped, uint32_t activeWeaponId, const wdata::WeaponLoadout* L) {
    bool wantZoom = false;

    if (wcfg::Get().enableScopeZoom && sa::IsPedValid(ped) && !ped->IsInVehicle() && L && L->scope != 0) {
        if (sa::camera::IsAimingWeaponCam() && !sa::camera::IsSniperOrRocketCam() && activeWeaponId != 34) {
            wantZoom = true;
        }
    }

    if (wantZoom) {
        const float cur = std::clamp(sa::camera::GetActiveCamFov(), 25.0f, 120.0f);
        if (!g_zoomActive) {
            g_scopeFovGuard.CaptureBeforeZoom(cur);
        }
        const float baseFov   = g_scopeFovGuard.GetRestoreFov();
        const float targetFov = (L->scope == 2) ? std::min(38.0f, baseFov * 0.55f)
                                                : std::min(53.0f, baseFov * 0.76f);
        if (!g_zoomActive || std::fabs(g_zoomTargetFov - targetFov) > 0.5f) {
            sa::camera::LerpFOV(cur, targetFov, 145.0f, true);
            g_zoomActive    = true;
            g_zoomTargetFov = targetFov;
        }
    } else if (g_zoomActive) {
        const float cur        = std::clamp(sa::camera::GetActiveCamFov(), 25.0f, 120.0f);
        const float restoreFov = g_scopeFovGuard.GetRestoreFov();
        sa::camera::LerpFOV(cur, restoreFov, 145.0f, false);
        g_zoomActive    = false;
        g_zoomTargetFov = restoreFov;
        g_scopeFovGuard.Clear();
    }
}

struct RawWeaponScores {
    float damage   = 0.0f;
    float fireRate = 0.0f;
    float accuracy = 0.0f;
    float range    = 0.0f;
    int   clipSize = 0;
};

RawWeaponScores EvaluateWeaponScores(uint32_t weaponId, const sa::CWeaponInfo& base,
                                     const wdata::WeaponLoadout& rawL, float skill01) {
    const wdata::WeaponLoadout L = wdata::SanitizeLoadout(weaponId, rawL);
    RawWeaponScores r{};
    float effDmg = static_cast<float>(base.m_nDamage);
    if (weaponId == 25 || weaponId == 26) effDmg *= 6.0f;
    else if (weaponId == 27) effDmg *= 5.0f;
    else if (weaponId == 35 || weaponId == 16 || weaponId == 39) effDmg = 140.0f;

    const LoadoutMultipliers m = ComputeLoadoutMultipliers(L, skill01);
    r.damage = (effDmg * m.dmg) * 0.65f;

    r.clipSize = ComputeModifiedClip(static_cast<int>(base.m_nAmmoClip), L.mag, m.clip);

    float loopDur = std::max(0.06f, base.m_fAnimLoopEnd - base.m_fAnimLoopStart);
    float rps = (1.0f / loopDur) * m.rof;
    // Dual-wield (Hitman WEAPON_FLAG_TWIN) alternates two weapons per cycle
    if ((base.m_nFlags & sa::WEAPON_FLAG_TWIN) != 0) rps *= 1.45f;
    if (weaponId == 38) rps = 18.0f;
    r.fireRate = rps * 7.5f;

    float baseAcc = std::max(0.35f, base.m_fAccuracy);
    r.accuracy = baseAcc * m.acc * 36.0f;
    r.range    = base.m_fWeaponRange * m.rng;
    return r;
}

inline void ComputeWeaponSkillBonus(int baseInt, float baseRaw, float skilledRaw, int& outBonus, int& outPct) {
    if (baseInt <= 0 || baseRaw <= 0.001f || skilledRaw <= baseRaw + 0.01f) {
        outBonus = 0;
        outPct   = 0;
        return;
    }
    const float ratio   = skilledRaw / baseRaw;
    const int   skilled = static_cast<int>(std::round(static_cast<float>(baseInt) * ratio));
    outBonus = std::max(0, skilled - baseInt);
    outPct   = (outBonus > 0) ? std::max(1, static_cast<int>(std::round((ratio - 1.0f) * 100.0f))) : 0;
}

} // namespace

void EnsureStockCaptured() {
    if (g_stockCaptured) return;
    const auto* tbl = sa::GetWeaponInfoTable();
    std::memcpy(g_stockTable, tbl, sizeof(g_stockTable));
    g_stockCaptured = true;
    modlog::Info("[WMods] Captured stock CWeaponInfo table (%d entries).", sa::WEAPON_INFO_COUNT);
}

wdata::WeaponStats ComputeStats(uint32_t weaponId, const wdata::WeaponLoadout& L) {
    EnsureStockCaptured();
    wdata::WeaponStats s{};

    const uint16_t statId   = GetWeaponSkillStatId(weaponId);
    const int8_t   baseTier = WeaponHasSkillTiers(weaponId) ? 0 : 1;
    const int      baseIdx  = InfoIndex(sa::GetWeaponInfo(weaponId, baseTier));
    if (baseIdx < 0) return s;

    const RawWeaponScores baseR = EvaluateWeaponScores(weaponId, g_stockTable[baseIdx], L, 0.0f);
    s.damage   = std::clamp(static_cast<int>(std::round(baseR.damage)),   5, 120);
    s.fireRate = std::clamp(static_cast<int>(std::round(baseR.fireRate)), 5, 120);
    s.accuracy = std::clamp(static_cast<int>(std::round(baseR.accuracy)), 10, 120);
    s.range    = std::clamp(static_cast<int>(std::round(baseR.range)),    5, 180);
    s.clipSize = baseR.clipSize;

    if (statId != 0) {
        const sa::CPed* ped      = sa::FindPlayerPed(0);
        const int8_t  liveTier   = GetPlayerWeaponSkillTier(ped, weaponId);
        const float   liveSkill  = GetPlayerWeaponSkill01(weaponId);
        const int     liveIdx    = InfoIndex(sa::GetWeaponInfo(weaponId, liveTier));
        if (liveIdx >= 0 && (liveTier > baseTier || liveSkill > 0.001f)) {
            const RawWeaponScores skillR = EvaluateWeaponScores(weaponId, g_stockTable[liveIdx], L, liveSkill);
            ComputeWeaponSkillBonus(s.damage,   baseR.damage,   skillR.damage,   s.damageSkillBonus,   s.damageSkillPct);
            ComputeWeaponSkillBonus(s.fireRate, baseR.fireRate, skillR.fireRate, s.fireRateSkillBonus, s.fireRateSkillPct);
            ComputeWeaponSkillBonus(s.accuracy, baseR.accuracy, skillR.accuracy, s.accuracySkillBonus, s.accuracySkillPct);
            ComputeWeaponSkillBonus(s.range,    baseR.range,    skillR.range,    s.rangeSkillBonus,    s.rangeSkillPct);
            if (skillR.clipSize > baseR.clipSize && baseR.clipSize > 0) {
                s.clipSkillBonus = skillR.clipSize - baseR.clipSize;
                s.clipSkillPct   = std::max(1, static_cast<int>(std::round(
                    100.0f * static_cast<float>(s.clipSkillBonus) / static_cast<float>(baseR.clipSize))));
            }
        }
    }
    return s;
}

void ForceRefresh(bool topUpClipIfLarger) {
    EnsureStockCaptured();
    if (g_hasAppliedMod && g_appliedWeaponId != 0) {
        RestoreWeaponStock(g_appliedWeaponId);
        g_hasAppliedMod    = false;
        g_appliedWeaponId  = 0;
        g_appliedSkill01   = 0.0f;
        g_appliedSkillTier = -1;
    }

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;
    sa::CWeapon* active = ped->GetActiveWeapon();
    if (!active || active->m_nType == 0) return;

    const uint32_t wid        = active->m_nType;
    const auto*    L          = wstore::Store().Find(wid);
    const float    liveSkill  = GetPlayerWeaponSkill01(wid);
    const int8_t   liveTier   = GetPlayerWeaponSkillTier(ped, wid);
    const wdata::WeaponLoadout effL = L ? wdata::SanitizeLoadout(wid, *L) : wdata::WeaponLoadout{};

    if (effL.HasAnyMod() || liveSkill > 0.005f) {
        ApplyLoadoutToWeapon(wid, effL, liveSkill);
        g_appliedWeaponId  = wid;
        g_appliedLoadout   = effL;
        g_appliedSkill01   = liveSkill;
        g_appliedSkillTier = liveTier;
        g_hasAppliedMod    = true;
    }

    if (topUpClipIfLarger) {
        sa::CWeaponInfo* wi = sa::GetWeaponInfo(wid, liveTier);
        if (wi && wi->m_nAmmoClip > active->m_nAmmoInClip) {
            active->m_nAmmoInClip = std::min<uint32_t>(wi->m_nAmmoClip, active->m_nTotalAmmo);
        }
    }
}

void Tick() {
    EnsureStockCaptured();
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) {
        if (g_zoomActive) {
            sa::camera::StopLerpFOV();
            g_zoomActive = false;
            g_scopeFovGuard.Clear();
        }
        return;
    }

    sa::CWeapon* active = ped->GetActiveWeapon();
    const uint32_t wid  = active ? active->m_nType : 0;
    const auto*    L    = (wid != 0) ? wstore::Store().Find(wid) : nullptr;
    const wdata::WeaponLoadout effL = L ? wdata::SanitizeLoadout(wid, *L) : wdata::WeaponLoadout{};
    const float  liveSkill = (wid != 0) ? GetPlayerWeaponSkill01(wid) : 0.0f;
    const int8_t liveTier  = (wid != 0) ? GetPlayerWeaponSkillTier(ped, wid) : -1;
    const bool   wantApply = (wid != 0) && (effL.HasAnyMod() || liveSkill > 0.005f);

    const bool needsUpdate =
        (wid != g_appliedWeaponId && (wantApply || g_hasAppliedMod)) ||
        (wantApply != g_hasAppliedMod) ||
        (wantApply && (!SameLoadout(effL, g_appliedLoadout) ||
                       std::fabs(liveSkill - g_appliedSkill01) >= 0.01f ||
                       liveTier != g_appliedSkillTier));

    if (needsUpdate) {
        if (g_hasAppliedMod && g_appliedWeaponId != 0) {
            RestoreWeaponStock(g_appliedWeaponId);
            g_hasAppliedMod    = false;
            g_appliedWeaponId  = 0;
            g_appliedSkill01   = 0.0f;
            g_appliedSkillTier = -1;
        }
        if (wantApply) {
            ApplyLoadoutToWeapon(wid, effL, liveSkill);
            g_appliedWeaponId  = wid;
            g_appliedLoadout   = effL;
            g_appliedSkill01   = liveSkill;
            g_appliedSkillTier = liveTier;
            g_hasAppliedMod    = true;
        }
    }

    TickScopeZoom(ped, wid, L);
}

void RestoreAllStock() {
    if (g_stockCaptured) {
        auto* tbl = sa::GetWeaponInfoTable();
        std::memcpy(tbl, g_stockTable, sizeof(g_stockTable));
    }
    g_hasAppliedMod    = false;
    g_appliedWeaponId  = 0;
    g_appliedSkill01   = 0.0f;
    g_appliedSkillTier = -1;
    if (g_zoomActive) {
        sa::camera::StopLerpFOV();
        g_zoomActive = false;
    }
    g_scopeFovGuard.Clear();
}

bool IsScopeZoomActive() {
    return g_zoomActive;
}

} // namespace wmods
