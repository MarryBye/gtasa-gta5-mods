#include "weapon_mods.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "config.hpp"
#include "log.hpp"
#include "weapon_storage.hpp"

namespace wmods {

namespace {

bool            g_stockCaptured = false;
sa::CWeaponInfo g_stockTable[sa::WEAPON_INFO_COUNT] = {};

uint32_t        g_appliedWeaponId = 0;
wdata::WeaponLoadout g_appliedLoadout{};
bool            g_hasAppliedMod = false;

bool            g_zoomActive    = false;
float           g_zoomTargetFov = 70.0f;

int InfoIndex(const sa::CWeaponInfo* ptr) {
    if (!ptr) return -1;
    const auto* base = sa::GetWeaponInfoTable();
    ptrdiff_t idx = ptr - base;
    if (idx < 0 || idx >= sa::WEAPON_INFO_COUNT) return -1;
    return static_cast<int>(idx);
}

void RestoreWeaponStock(uint32_t weaponId) {
    if (!g_stockCaptured || weaponId == 0) return;
    for (int8_t sk = 0; sk <= 3; ++sk) {
        sa::CWeaponInfo* wi = sa::GetWeaponInfo(weaponId, sk);
        int idx = InfoIndex(wi);
        if (idx >= 0) {
            *wi = g_stockTable[idx];
        }
    }
}

void ApplyLoadoutToWeapon(uint32_t weaponId, const wdata::WeaponLoadout& L) {
    if (!g_stockCaptured || weaponId == 0) return;

    float dmgMult   = 1.0f;
    float accMult   = 1.0f;
    float rngMult   = 1.0f;
    float rofMult   = 1.0f; // > 1.0 = faster fire rate (shorter anim loop)
    float clipMult  = 1.0f;
    bool  moveFire  = false;

    if (L.muzzle == 1) {       // Suppressor
        dmgMult *= 0.92f;
        accMult *= 1.12f;
        rngMult *= 0.90f;
    } else if (L.muzzle == 2) {// Compensator
        accMult *= 1.22f;
        rofMult *= 1.06f;
    }

    if (L.mag == 1)      clipMult = 1.60f; // Extended Mag
    else if (L.mag == 2) clipMult = 2.00f; // Drum Mag

    if (L.scope == 1) {        // Reflex Sight
        accMult *= 1.16f;
        rngMult *= 1.08f;
    } else if (L.scope == 2) { // Tactical Scope
        accMult *= 1.28f;
        rngMult *= 1.25f;
    }

    if (L.grip == 1) {         // Vertical Tactical Foregrip
        accMult *= 1.20f;
        moveFire = true;
    }

    if (L.barrel == 1) {       // Heavy Barrel & Match Trigger
        dmgMult *= 1.22f;
        rngMult *= 1.20f;
        rofMult *= 1.15f;
    }

    if (L.laser != 0) {        // Tactical Laser Sight
        accMult *= 1.10f;
    }

    for (int8_t sk = 0; sk <= 2; ++sk) {
        sa::CWeaponInfo* wi = sa::GetWeaponInfo(weaponId, sk);
        int idx = InfoIndex(wi);
        if (idx < 0) continue;

        const sa::CWeaponInfo& base = g_stockTable[idx];
        *wi = base;

        // Damage
        int newDmg = static_cast<int>(std::round(static_cast<float>(base.m_nDamage) * dmgMult));
        wi->m_nDamage = static_cast<uint16_t>(std::clamp(newDmg, 1, 600));

        // Accuracy (higher = tighter spread in GTA SA!)
        wi->m_fAccuracy = std::clamp(base.m_fAccuracy * accMult, 0.2f, 4.5f);

        // Range
        wi->m_fWeaponRange = std::clamp(base.m_fWeaponRange * rngMult, 5.0f, 180.0f);
        wi->m_fTargetRange = std::clamp(base.m_fTargetRange * rngMult, 5.0f, 120.0f);

        // Clip size (Extended / Drum Mag)
        if (clipMult > 1.01f && base.m_nAmmoClip >= 1) {
            int newClip = static_cast<int>(std::round(static_cast<float>(base.m_nAmmoClip) * clipMult));
            int minGain = (L.mag == 2) ? 4 : 2;
            if (newClip < static_cast<int>(base.m_nAmmoClip) + minGain) {
                newClip = static_cast<int>(base.m_nAmmoClip) + minGain;
            }
            wi->m_nAmmoClip = static_cast<uint16_t>(std::clamp(newClip, 1, 250));
        }

        // Fire rate speedup (compress anim loop end toward start while keeping loopFire inside!)
        if (rofMult > 1.01f && base.m_fAnimLoopEnd > base.m_fAnimLoopStart + 0.05f) {
            float dur = (base.m_fAnimLoopEnd - base.m_fAnimLoopStart) / rofMult;
            dur = std::max(dur, 0.055f);
            wi->m_fAnimLoopEnd = base.m_fAnimLoopStart + dur;
            if (wi->m_fAnimLoopFire > wi->m_fAnimLoopEnd - 0.01f) {
                wi->m_fAnimLoopFire = wi->m_fAnimLoopEnd - 0.01f;
            }
            if (base.m_fAnim2LoopEnd > base.m_fAnim2LoopStart + 0.05f) {
                float dur2 = std::max((base.m_fAnim2LoopEnd - base.m_fAnim2LoopStart) / rofMult, 0.055f);
                wi->m_fAnim2LoopEnd = base.m_fAnim2LoopStart + dur2;
                if (wi->m_fAnim2LoopFire > wi->m_fAnim2LoopEnd - 0.01f) {
                    wi->m_fAnim2LoopFire = wi->m_fAnim2LoopEnd - 0.01f;
                }
            }
        }

        // Vertical grip grants smoother strafing & shooting while moving
        if (moveFire) {
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
    float targetFov = 70.0f;

    if (wcfg::Get().enableScopeZoom && sa::IsPedValid(ped) && !ped->IsInVehicle() && L && L->scope != 0) {
        if (sa::camera::IsAimingWeaponCam() && !sa::camera::IsSniperOrRocketCam() &&
            activeWeaponId != 34) // Sniper already has built-in scope
        {
            wantZoom  = true;
            targetFov = (L->scope == 2) ? 38.0f : 53.0f; // Tactical Scope = crisp 1.8x zoom, Reflex = 1.3x zoom
        }
    }

    if (wantZoom) {
        if (!g_zoomActive || std::fabs(g_zoomTargetFov - targetFov) > 0.5f) {
            float cur = std::clamp(sa::camera::GetActiveCamFov(), 25.0f, 95.0f);
            sa::camera::LerpFOV(cur, targetFov, 145.0f, true);
            g_zoomActive    = true;
            g_zoomTargetFov = targetFov;
        }
    } else if (g_zoomActive) {
        float cur = std::clamp(sa::camera::GetActiveCamFov(), 25.0f, 95.0f);
        sa::camera::LerpFOV(cur, 70.0f, 145.0f, false);
        g_zoomActive    = false;
        g_zoomTargetFov = 70.0f;
    }
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

    const sa::CWeaponInfo* basePtr = sa::GetWeaponInfo(weaponId, 1);
    int idx = InfoIndex(basePtr);
    if (idx < 0) return s;
    const sa::CWeaponInfo& base = g_stockTable[idx];

    // Effective base damage (shotguns fire multiple pellets per trigger pull)
    float effDmg = static_cast<float>(base.m_nDamage);
    if (weaponId == 25) effDmg *= 6.0f;       // Pump shotgun pellets
    else if (weaponId == 26) effDmg *= 6.0f;  // Sawnoff pellets
    else if (weaponId == 27) effDmg *= 5.0f;  // Combat SPAS-12 pellets
    else if (weaponId == 35 || weaponId == 16 || weaponId == 39) effDmg = 140.0f; // Explosives

    float dmgMult  = 1.0f;
    float accMult  = 1.0f;
    float rngMult  = 1.0f;
    float rofMult  = 1.0f;
    float clipMult = 1.0f;

    if (L.muzzle == 1)      { dmgMult *= 0.92f; accMult *= 1.12f; rngMult *= 0.90f; }
    else if (L.muzzle == 2) { accMult *= 1.22f; rofMult *= 1.06f; }
    if (L.mag == 1)         clipMult = 1.60f;
    else if (L.mag == 2)    clipMult = 2.00f;
    if (L.scope == 1)       { accMult *= 1.16f; rngMult *= 1.08f; }
    else if (L.scope == 2)  { accMult *= 1.28f; rngMult *= 1.25f; }
    if (L.grip == 1)        accMult *= 1.20f;
    if (L.barrel == 1)      { dmgMult *= 1.22f; rngMult *= 1.20f; rofMult *= 1.15f; }
    if (L.laser != 0)       accMult *= 1.10f;

    // Damage rating (0..120 scale, intuitive GTA 5 numbers)
    s.damage = std::clamp(static_cast<int>(std::round((effDmg * dmgMult) * 0.65f)), 5, 120);

    // Fire Rate rating (0..120 scale)
    float loopDur = std::max(0.06f, base.m_fAnimLoopEnd - base.m_fAnimLoopStart);
    float rps = (1.0f / loopDur) * rofMult;
    if (weaponId == 38) rps = 18.0f; // Minigun
    s.fireRate = std::clamp(static_cast<int>(std::round(rps * 7.5f)), 5, 120);

    // Accuracy rating (0..120 scale)
    float baseAcc = std::max(0.35f, base.m_fAccuracy);
    s.accuracy = std::clamp(static_cast<int>(std::round(baseAcc * accMult * 36.0f)), 10, 120);

    // Effective Range in meters
    s.range = std::clamp(static_cast<int>(std::round(base.m_fWeaponRange * rngMult)), 5, 180);

    // Real clip capacity
    int clip = static_cast<int>(base.m_nAmmoClip);
    if (clipMult > 1.01f && clip >= 1) {
        int nc = static_cast<int>(std::round(static_cast<float>(clip) * clipMult));
        int minGain = (L.mag == 2) ? 4 : 2;
        if (nc < clip + minGain) nc = clip + minGain;
        clip = std::clamp(nc, 1, 250);
    }
    s.clipSize = clip;
    return s;
}

void ForceRefresh(bool topUpClipIfLarger) {
    EnsureStockCaptured();
    if (g_hasAppliedMod && g_appliedWeaponId != 0) {
        RestoreWeaponStock(g_appliedWeaponId);
        g_hasAppliedMod   = false;
        g_appliedWeaponId = 0;
    }

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;
    sa::CWeapon* active = ped->GetActiveWeapon();
    if (!active || active->m_nType == 0) return;

    uint32_t wid = active->m_nType;
    const auto* L = wstore::Store().Find(wid);
    if (L && L->HasAnyMod()) {
        ApplyLoadoutToWeapon(wid, *L);
        g_appliedWeaponId = wid;
        g_appliedLoadout  = *L;
        g_hasAppliedMod   = true;

        if (topUpClipIfLarger) {
            sa::CWeaponInfo* wi = sa::GetWeaponInfo(wid, 1);
            if (wi && wi->m_nAmmoClip > active->m_nAmmoInClip) {
                active->m_nAmmoInClip = std::min<uint32_t>(wi->m_nAmmoClip, active->m_nTotalAmmo);
            }
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
        }
        return;
    }

    sa::CWeapon* active = ped->GetActiveWeapon();
    uint32_t wid = active ? active->m_nType : 0;
    const auto* L = (wid != 0) ? wstore::Store().Find(wid) : nullptr;

    if (wid != g_appliedWeaponId ||
        (L && (!g_hasAppliedMod || !SameLoadout(*L, g_appliedLoadout))) ||
        (!L && g_hasAppliedMod))
    {
        if (g_hasAppliedMod && g_appliedWeaponId != 0) {
            RestoreWeaponStock(g_appliedWeaponId);
            g_hasAppliedMod   = false;
            g_appliedWeaponId = 0;
        }
        if (wid != 0 && L && L->HasAnyMod()) {
            ApplyLoadoutToWeapon(wid, *L);
            g_appliedWeaponId = wid;
            g_appliedLoadout  = *L;
            g_hasAppliedMod   = true;
        }
    }

    TickScopeZoom(ped, wid, L);
}

void RestoreAllStock() {
    if (g_stockCaptured) {
        auto* tbl = sa::GetWeaponInfoTable();
        std::memcpy(tbl, g_stockTable, sizeof(g_stockTable));
    }
    g_hasAppliedMod   = false;
    g_appliedWeaponId = 0;
    if (g_zoomActive) {
        sa::camera::StopLerpFOV();
        g_zoomActive = false;
    }
}

bool IsScopeZoomActive() {
    return g_zoomActive;
}

} // namespace wmods
