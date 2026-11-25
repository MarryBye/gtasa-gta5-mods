#include "tuning_physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <windows.h>
#include "audio.hpp"
#include "config.hpp"
#include "log.hpp"

namespace tuning {

namespace {

constexpr int MAX_TRACKED_VEHICLES = 64;

struct TrackedVehicle {
    bool                   inUse        = false;
    sa::CVehicle*          veh          = nullptr;
    int32_t                handle       = -1;
    int16_t                modelId      = 0;
    sa::tHandlingData*     origHandling = nullptr;
    sa::tHandlingData      baseHandling{};   // Clean snapshot of stock handling
    sa::tHandlingData      customHandling{}; // Isolated per-vehicle handling instance!
    mod::PerformanceTuning cfg{};

    // Live runtime state
    float    lastHealth   = 1000.0f;
    float    turboBoost   = 0.0f;
    float    smoothRpm    = 0.0f;
    float    prevGas      = 0.0f;
    uint8_t  prevGear     = 0;
    uint32_t lastBovMs    = 0;
    uint32_t lastPopMs    = 0;
};

TrackedVehicle g_tracked[MAX_TRACKED_VEHICLES];

struct CachedModelHandling {
    bool              valid       = false;
    bool              isFourWheel = true;
    sa::tHandlingData handling{};
};
CachedModelHandling g_modelBaseCache[212]; // Models 400..611

inline bool IsHandlingSane(const sa::tHandlingData* h) {
    if (!h) return false;
    float mv = h->m_transmissionData.m_fMaxGearVelocity;
    float g5 = h->m_transmissionData.m_aGears[5].m_fMaxVelocity;
    float g4 = h->m_transmissionData.m_aGears[4].m_fMaxVelocity;
    float best = std::max({mv, g5, g4});
    return std::isfinite(best) && best >= 0.08f && best <= 12.0f &&
           std::isfinite(h->m_fMass) && h->m_fMass >= 50.0f && h->m_fMass <= 200000.0f;
}

void CacheModelHandling(int32_t modelId, bool isFourWheel, const sa::tHandlingData* h) {
    if (modelId < 400 || modelId > 611 || !IsHandlingSane(h)) return;
    auto& slot = g_modelBaseCache[modelId - 400];
    slot.valid       = true;
    slot.isFourWheel = isFourWheel;
    std::memcpy(&slot.handling, h, sizeof(sa::tHandlingData));
}

bool LoadHandlingFromCfgFallback(int32_t modelId, sa::tHandlingData& out, bool& outIsFourWheel) {
    if (modelId < 400 || modelId > 611) return false;
    const std::string& gDir = mod::Config().gameDir;
    if (gDir.empty()) return false;

    // 1. Resolve modelId -> handling name & vehicle type from data\vehicles.ide
    std::string handlingName;
    outIsFourWheel = true;
    {
        std::ifstream f(gDir + "\\data\\vehicles.ide");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            // Replace commas with spaces for clean tokenization
            for (char& ch : line) if (ch == ',') ch = ' ';
            std::istringstream iss(line);
            int id = 0;
            std::string dff, txd, vtype, hname;
            if ((iss >> id >> dff >> txd >> vtype >> hname) && id == modelId) {
                handlingName = hname;
                outIsFourWheel = (_stricmp(vtype.c_str(), "car") == 0 ||
                                  _stricmp(vtype.c_str(), "mtruck") == 0 ||
                                  _stricmp(vtype.c_str(), "quad") == 0);
                break;
            }
        }
    }
    if (handlingName.empty()) return false;

    // 2. Parse handling row from data\handling.cfg
    std::ifstream hf(gDir + "\\data\\handling.cfg");
    std::string line;
    while (std::getline(hf, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '%' || line[0] == '!' || line[0] == '$' || line[0] == '^') continue;
        std::istringstream iss(line);
        std::string name;
        float mass, turnMass, drag, cx, cy, cz;
        int sub;
        float tracMult, tracLoss, tracBias;
        int numGears;
        float maxVelKmh, accel, inertia;
        std::string driveType, engType;
        float brakeDecel;
        if (!(iss >> name >> mass >> turnMass >> drag >> cx >> cy >> cz >> sub
                  >> tracMult >> tracLoss >> tracBias >> numGears
                  >> maxVelKmh >> accel >> inertia >> driveType >> engType >> brakeDecel)) {
            continue;
        }
        if (_stricmp(name.c_str(), handlingName.c_str()) == 0) {
            out = sa::tHandlingData{};
            out.m_fMass = mass;
            out.m_fInvMass = (mass > 1.0f) ? (1.0f / mass) : 0.001f;
            out.m_fTurnMass = turnMass;
            out.m_fDragMult = drag;
            out.m_fTractionMultiplier = tracMult;
            out.m_fTractionLoss = tracLoss;
            out.m_fTractionBias = tracBias;
            out.m_transmissionData.m_nNumberOfGears = static_cast<uint8_t>(std::clamp(numGears, 1, 5));
            out.m_transmissionData.m_fMaxGearVelocity = maxVelKmh / 180.0f;
            out.m_transmissionData.m_fEngineAcceleration = accel * 0.4f * 0.0004f;
            out.m_transmissionData.m_fEngineInertia = inertia;
            out.m_fBrakeDeceleration = brakeDecel * 0.0004f;
            return true;
        }
    }
    return false;
}

TrackedVehicle* FindSlot(const sa::CVehicle* veh) {
    if (!veh) return nullptr;
    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        if (g_tracked[i].inUse && g_tracked[i].veh == veh) {
            return &g_tracked[i];
        }
    }
    return nullptr;
}

TrackedVehicle* AllocSlot(sa::CVehicle* veh) {
    if (auto* existing = FindSlot(veh)) return existing;
    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        if (g_tracked[i].inUse) {
            if (!sa::IsVehicleValid(g_tracked[i].veh) ||
                sa::GetVehicleHandle(g_tracked[i].veh) != g_tracked[i].handle) {
                g_tracked[i].inUse = false;
                g_tracked[i].veh = nullptr;
            }
        }
    }
    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        if (!g_tracked[i].inUse) {
            g_tracked[i] = TrackedVehicle{};
            g_tracked[i].inUse        = true;
            g_tracked[i].veh          = veh;
            g_tracked[i].handle       = sa::GetVehicleHandle(veh);
            g_tracked[i].modelId      = veh->GetModelIndex();
            g_tracked[i].origHandling = veh->HandlingData();
            if (g_tracked[i].origHandling) {
                std::memcpy(&g_tracked[i].baseHandling, g_tracked[i].origHandling, sizeof(sa::tHandlingData));
                std::memcpy(&g_tracked[i].customHandling, g_tracked[i].origHandling, sizeof(sa::tHandlingData));
                CacheModelHandling(g_tracked[i].modelId, veh->IsFourWheelLike(), g_tracked[i].origHandling);
            }
            g_tracked[i].lastHealth = veh->Health();
            g_tracked[i].prevGear   = veh->GetCurrentGear();
            return &g_tracked[i];
        }
    }
    return nullptr;
}

// Compute tuned handling from a clean base handling snapshot
void BuildTunedHandling(const sa::tHandlingData& base, bool isFourWheel, const mod::PerformanceTuning& c, sa::tHandlingData& out) {
    std::memcpy(&out, &base, sizeof(sa::tHandlingData));

    // 1. Engine (0..4): balanced, noticeably stronger acceleration + higher top speed + lower aero drag!
    // Reducing m_fDragMult ensures heavier sedans (like Tahoma) actually reach their new top speed!
    static constexpr float kEngAccel[5] = {1.00f, 1.18f, 1.38f, 1.60f, 1.85f};
    static constexpr float kEngTop[5]   = {1.00f, 1.11f, 1.23f, 1.36f, 1.50f};
    static constexpr float kEngDrag[5]  = {1.00f, 0.92f, 0.84f, 0.75f, 0.66f};
    int eIdx = std::clamp<int>(c.engineLevel, 0, 4);

    // 2. Transmission (0..3): snappier revs/shifts + extra pull
    static constexpr float kTransAccel[4]   = {1.00f, 1.08f, 1.17f, 1.28f};
    static constexpr float kTransInertia[4] = {1.00f, 0.80f, 0.62f, 0.45f};
    int tIdx = std::clamp<int>(c.transmissionLevel, 0, 3);

    // Turbo: strong power boost + subtle top speed bump
    float turboAccel = c.turbo ? 1.16f : 1.00f;
    float turboTop   = c.turbo ? 1.08f : 1.00f;
    float turboDrag  = c.turbo ? 0.92f : 1.00f;

    float accelMult = kEngAccel[eIdx] * kTransAccel[tIdx] * turboAccel;
    float topMult   = kEngTop[eIdx] * turboTop;

    out.m_fDragMult *= (kEngDrag[eIdx] * turboDrag);
    out.m_transmissionData.m_fEngineAcceleration *= accelMult;
    out.m_transmissionData.m_fEngineInertia      *= kTransInertia[tIdx];
    out.m_transmissionData.m_fMaxGearVelocity    *= topMult;
    for (int g = 1; g < 6; ++g) {
        out.m_transmissionData.m_aGears[g].m_fMaxVelocity        *= topMult;
        out.m_transmissionData.m_aGears[g].m_fChangeUpVelocity   *= topMult;
        out.m_transmissionData.m_aGears[g].m_fChangeDownVelocity *= topMult;
    }

    // 3. Suspension (0..4) — 4-wheel vehicles
    if (isFourWheel && c.suspensionLevel > 0) {
        static constexpr float kSuspDrop[5]  = {0.00f, 0.032f, 0.062f, 0.092f, 0.122f};
        static constexpr float kSuspForce[5] = {1.00f, 1.08f,  1.16f,  1.26f,  1.36f};
        static constexpr float kSuspDamp[5]  = {1.00f, 1.08f,  1.17f,  1.27f,  1.38f};
        static constexpr float kAntiDive[5]  = {1.00f, 1.14f,  1.28f,  1.42f,  1.56f};
        int sIdx = std::clamp<int>(c.suspensionLevel, 0, 4);

        float newLower = out.m_fSuspensionLowerLimit + kSuspDrop[sIdx];
        float maxSafeLower = std::min(-0.03f, out.m_fSuspensionUpperLimit - 0.12f);
        if (newLower > maxSafeLower) newLower = maxSafeLower;
        out.m_fSuspensionLowerLimit = newLower;
        out.m_fSuspensionForceLevel *= kSuspForce[sIdx];
        out.m_fSuspensionDampingLevel *= kSuspDamp[sIdx];
        out.m_fSuspensionAntiDiveMultiplier *= kAntiDive[sIdx];
    }

    // 4. Brakes (0..3)
    static constexpr float kBrakes[4] = {1.00f, 1.26f, 1.54f, 1.85f};
    int bIdx = std::clamp<int>(c.brakesLevel, 0, 3);
    out.m_fBrakeDeceleration *= kBrakes[bIdx];

    // 5. Tires Grip (0..3)
    static constexpr float kTraction[4] = {1.00f, 1.10f, 1.20f, 1.32f};
    static constexpr float kTracLoss[4] = {1.00f, 1.08f, 1.16f, 1.24f};
    int gIdx = std::clamp<int>(c.tiresGripLevel, 0, 3);
    out.m_fTractionMultiplier *= kTraction[gIdx];
    out.m_fTractionLoss       *= kTracLoss[gIdx];

    // 6. Armor / Body (0..5 = 0..100%)
    // Compatible with Proper Vehicle Damage v0.3.7:
    // - Strong m_fCollisionDamageMultiplier reduction protects panels, doors, bumpers & glass
    // - Progressive chassis mass/rigidity reinforcement reduces impact delta-V (less body crumpling in collisions!)
    static constexpr float kCollDmg[6]  = {1.00f, 0.78f, 0.58f, 0.40f, 0.25f, 0.14f};
    static constexpr float kArmorMass[6] = {1.00f, 1.12f, 1.25f, 1.40f, 1.56f, 1.74f};
    int aIdx = std::clamp<int>(c.armorLevel, 0, 5);
    out.m_fCollisionDamageMultiplier *= kCollDmg[aIdx];
    if (aIdx > 0 && out.m_fMass > 10.0f) {
        float mm = kArmorMass[aIdx];
        out.m_fMass     *= mm;
        out.m_fInvMass   = 1.0f / out.m_fMass;
        out.m_fTurnMass *= (1.0f + (mm - 1.0f) * 0.50f);
    }
}

void RebuildHandling(TrackedVehicle& tv) {
    if (!tv.origHandling || !sa::IsVehicleValid(tv.veh)) return;

    BuildTunedHandling(tv.baseHandling, tv.veh->IsFourWheelLike(), tv.cfg, tv.customHandling);
    tv.veh->HandlingData() = &tv.customHandling;

    // Bulletproof Tires flag
    tv.veh->SetTyresDontBurst(tv.cfg.bulletproofTires);
    if (tv.handle > 0) {
        sa::ScriptCmd(0x053F).Int(tv.handle).Int(tv.cfg.bulletproofTires ? 0 : 1).Run();
    }
}

VehicleStats ComputeStatsFromHandling(const sa::tHandlingData& base, bool isFourWheel, const mod::PerformanceTuning& cfg) {
    VehicleStats st{};
    sa::tHandlingData sim{};
    BuildTunedHandling(base, isFourWheel, cfg, sim);

    // Calibrated achievable top speed matching GTA SA's real physics & SA Styled Speedometer!
    // GTA SA stores m_fMaxGearVelocity in units/step (1 unit/step = 50 m/s = 180 km/h = 111.847 mph).
    float rawMax = sim.m_transmissionData.m_fMaxGearVelocity;
    if (rawMax < 0.08f) {
        float gearMax = std::max(sim.m_transmissionData.m_aGears[5].m_fMaxVelocity,
                                 sim.m_transmissionData.m_aGears[4].m_fMaxVelocity);
        float maxVelField = 0.0f;
        std::memcpy(&maxVelField, &sim.m_transmissionData._unk5C, sizeof(float));
        if (std::isfinite(maxVelField) && maxVelField > gearMax && maxVelField < 10.0f) gearMax = maxVelField;
        rawMax = gearMax;
    }
    float safeDrag = std::clamp(std::isfinite(sim.m_fDragMult) ? sim.m_fDragMult : 2.0f, 0.5f, 8.0f);
    float dragFactor = 1.0f / (1.0f + std::max(0.0f, safeDrag - 0.85f) * 0.085f);
    float kmh = rawMax * 180.0f * dragFactor;
    if (cfg.turbo) kmh += 9.0f; // Active turbo boost thrust
    st.topSpeedKmh = std::clamp(static_cast<int>(std::round(kmh)), 40, 380);
    st.topSpeedMph = std::clamp(static_cast<int>(std::round(kmh * 0.621371f)), 25, 240);

    // Acceleration rating (scaled cleanly around GTA SA's m_fEngineAcceleration ~ 0.0004..0.0025 + inertia)
    float rawAccel = sim.m_transmissionData.m_fEngineAcceleration * 1000.0f;
    float inertiaBonus = (base.m_transmissionData.m_fEngineInertia > 0.1f)
        ? (base.m_transmissionData.m_fEngineInertia / std::max(1.0f, sim.m_transmissionData.m_fEngineInertia))
        : 1.0f;
    float accelScore = rawAccel * 185.0f * (0.75f + 0.25f * inertiaBonus);
    if (cfg.turbo) accelScore += 9.0f;
    st.accelRating = std::clamp(static_cast<int>(std::round(accelScore)), 10, 150);

    // Braking rating (GTA SA stores m_fBrakeDeceleration scaled by 1/2500 -> ~0.002..0.008)
    float brakeScore = sim.m_fBrakeDeceleration * 11500.0f;
    st.brakeRating = std::clamp(static_cast<int>(std::round(brakeScore)), 10, 150);

    // Handling / Grip rating (traction multiplier + traction loss + suspension stability)
    float suspBonus = 1.0f + static_cast<float>(cfg.suspensionLevel) * 0.055f;
    float handScore = (sim.m_fTractionMultiplier * 52.0f + sim.m_fTractionLoss * 28.0f) * suspBonus;
    st.handlingRating = std::clamp(static_cast<int>(std::round(handScore)), 15, 150);

    // Effective body durability (100% stock -> 200% at Armor 100%)
    st.armorPercent = 100 + std::clamp<int>(cfg.armorLevel, 0, 5) * 20;

    return st;
}

} // namespace

VehicleStats ComputeStats(sa::CVehicle* veh, const mod::PerformanceTuning& cfg) {
    if (!sa::IsVehicleValid(veh)) return VehicleStats{};

    const sa::tHandlingData* base = nullptr;
    if (auto* tv = FindSlot(veh)) {
        base = &tv->baseHandling;
    } else {
        base = veh->HandlingData();
        CacheModelHandling(veh->GetModelIndex(), veh->IsFourWheelLike(), base);
    }
    if (!base) return VehicleStats{};
    return ComputeStatsFromHandling(*base, veh->IsFourWheelLike(), cfg);
}

VehicleStats ComputeStatsForModel(int32_t modelId, const mod::PerformanceTuning& cfg) {
    if (modelId < 400 || modelId > 611) return VehicleStats{};

    // Tier 1: Live tracked vehicle of this model (100% exact clean base handling snapshot!)
    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        if (g_tracked[i].inUse && g_tracked[i].modelId == modelId && IsHandlingSane(&g_tracked[i].baseHandling)) {
            bool is4w = sa::IsVehicleValid(g_tracked[i].veh) ? g_tracked[i].veh->IsFourWheelLike() : true;
            CacheModelHandling(modelId, is4w, &g_tracked[i].baseHandling);
            return ComputeStatsFromHandling(g_tracked[i].baseHandling, is4w, cfg);
        }
    }

    // Tier 2: Any live vehicle of this model currently spawned in GTA SA's vehicle pool
    if (auto* pool = sa::GetVehiclePool(); pool && pool->objects && pool->byteMap && pool->size > 0 && pool->size < 2000) {
        for (int i = 0; i < pool->size; ++i) {
            if ((pool->byteMap[i] & 0x80) != 0) continue;
            auto* v = reinterpret_cast<sa::CVehicle*>(pool->objects + static_cast<size_t>(i) * sa::VEHICLE_POOL_STRIDE);
            if (v->GetModelIndex() == modelId) {
                const sa::tHandlingData* h = v->HandlingData();
                if (auto* tv = FindSlot(v)) h = &tv->baseHandling;
                if (IsHandlingSane(h)) {
                    CacheModelHandling(modelId, v->IsFourWheelLike(), h);
                    return ComputeStatsFromHandling(*h, v->IsFourWheelLike(), cfg);
                }
            }
        }
    }

    // Tier 3: Previously cached live handling snapshot for this model
    if (g_modelBaseCache[modelId - 400].valid) {
        const auto& c = g_modelBaseCache[modelId - 400];
        return ComputeStatsFromHandling(c.handling, c.isFourWheel, cfg);
    }

    // Tier 4: Runtime-patched gHandlingDataMgr lookup (follows Open Limit Adjuster if relocated!)
    uintptr_t miTableAddr = *reinterpret_cast<const uintptr_t*>(0x006B0B22 + 3);
    uintptr_t hTableAddr  = *reinterpret_cast<const uintptr_t*>(0x006B0BA2 + 2);
    auto** modelInfoPtrs  = reinterpret_cast<uint8_t**>(miTableAddr ? miTableAddr : 0x00A9B0C8u);
    uintptr_t handlingTbl = hTableAddr ? hTableAddr : 0x00C2B9DCu;

    uint8_t* mi = modelInfoPtrs ? modelInfoPtrs[modelId] : nullptr;
    if (mi) {
        // Verified in gta_sa.exe @ 0x004C89BF, 0x006F608B, 0x006F0D4D, 0x006B0B97:
        //   CVehicleModelInfo + 0x4A = uint8_t m_nHandlingId (0..209)
        //   CVehicleModelInfo + 0x3C = int32_t m_nVehicleType (0=car, 1=mtruck, 2=quad)
        uint8_t hid = *reinterpret_cast<const uint8_t*>(mi + 0x4A);
        int32_t vtype = *reinterpret_cast<const int32_t*>(mi + 0x3C);
        bool isFourWheel = (vtype == 0 || vtype == 1 || vtype == 2);

        if (hid < 210) {
            const auto* base = reinterpret_cast<const sa::tHandlingData*>(
                handlingTbl + static_cast<uintptr_t>(hid) * 0xE0u);
            if (IsHandlingSane(base)) {
                CacheModelHandling(modelId, isFourWheel, base);
                return ComputeStatsFromHandling(*base, isFourWheel, cfg);
            }
        }

        // Secondary memory lookup via cHandlingDataMgr::GetHandlingId(gxtKey) @ 0x006F4FD0
        char gxt[12] = {};
        std::memcpy(gxt, mi + 0x32, 8);
        gxt[8] = '\0';
        if (gxt[0] != '\0') {
            using FnGetHid = int32_t(__thiscall*)(void*, const char*);
            int32_t hid2 = reinterpret_cast<FnGetHid>(0x006F4FD0)(reinterpret_cast<void*>(0x00C2B9C8), gxt);
            if (hid2 >= 0 && hid2 < 210) {
                const auto* base2 = reinterpret_cast<const sa::tHandlingData*>(
                    handlingTbl + static_cast<uintptr_t>(hid2) * 0xE0u);
                if (IsHandlingSane(base2)) {
                    CacheModelHandling(modelId, isFourWheel, base2);
                    return ComputeStatsFromHandling(*base2, isFourWheel, cfg);
                }
            }
        }
    }

    // Tier 5: Direct data\vehicles.ide + data\handling.cfg file fallback (100% immune to any memory relocation!)
    sa::tHandlingData cfgFallback{};
    bool fbFourWheel = true;
    if (LoadHandlingFromCfgFallback(modelId, cfgFallback, fbFourWheel)) {
        CacheModelHandling(modelId, fbFourWheel, &cfgFallback);
        modlog::Info("[Tuning] Used handling.cfg fallback for model %d (maxVel=%.2f).",
                     modelId, cfgFallback.m_transmissionData.m_fMaxGearVelocity * 180.0f);
        return ComputeStatsFromHandling(cfgFallback, fbFourWheel, cfg);
    }

    return VehicleStats{};
}

void ApplyTuning(sa::CVehicle* veh, const mod::PerformanceTuning& cfg) {
    if (!sa::IsVehicleValid(veh)) return;
    TrackedVehicle* tv = AllocSlot(veh);
    if (!tv) {
        modlog::Info("[Tuning] Pool full, could not track vehicle %p", veh);
        return;
    }
    tv->cfg = cfg;
    RebuildHandling(*tv);
}

const mod::PerformanceTuning* GetLiveTuning(const sa::CVehicle* veh) {
    TrackedVehicle* tv = FindSlot(veh);
    return tv ? &tv->cfg : nullptr;
}

mod::PerformanceTuning* GetLiveTuningMutable(sa::CVehicle* veh) {
    TrackedVehicle* tv = FindSlot(veh);
    return tv ? &tv->cfg : nullptr;
}

void DetachVehicle(sa::CVehicle* veh) {
    TrackedVehicle* tv = FindSlot(veh);
    if (!tv) return;
    if (sa::IsVehicleValid(tv->veh) && tv->origHandling) {
        tv->veh->HandlingData() = tv->origHandling;
    }
    tv->inUse = false;
    tv->veh = nullptr;
}

void ResetAll() {
    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        if (g_tracked[i].inUse) {
            if (sa::IsVehicleValid(g_tracked[i].veh) && g_tracked[i].origHandling) {
                g_tracked[i].veh->HandlingData() = g_tracked[i].origHandling;
            }
            g_tracked[i].inUse = false;
            g_tracked[i].veh = nullptr;
        }
    }
    audio::StopAll();
}

void TickAll() {
    if (sa::IsPauseMenuActive() || !sa::IsGameWindowFocused() || sa::GetGameState() != 9) {
        audio::StopAll();
        return;
    }

    sa::CVehicle* playerVeh = sa::FindPlayerVehicle(0, false);
    sa::CPed* playerPed = sa::FindPlayerPed(0);
    bool playerDriving = (playerVeh != nullptr && playerPed != nullptr && playerVeh->GetDriver() == playerPed);

    const float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
    const uint32_t nowMs = sa::GetTimeMs();

    bool engineAudioHandled = false;
    bool turboAudioHandled  = false;
    bool hornAudioHandled   = false;

    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        auto& tv = g_tracked[i];
        if (!tv.inUse) continue;

        if (!sa::IsVehicleValid(tv.veh) ||
            sa::GetVehicleHandle(tv.veh) != tv.handle ||
            tv.veh->GetModelIndex() != tv.modelId) {
            tv.inUse = false;
            tv.veh = nullptr;
            continue;
        }

        sa::CVehicle* v = tv.veh;
        const auto& c = tv.cfg;

        if (v->HandlingData() != &tv.customHandling && tv.origHandling != nullptr) {
            v->HandlingData() = &tv.customHandling;
        }

        // 1. Bulletproof Tires continuous guard
        if (c.bulletproofTires) {
            v->SetTyresDontBurst(true);
            if (v->IsFourWheelLike()) {
                uint8_t* ws = v->AutoWheelStatus();
                for (int w = 0; w < 4; ++w) {
                    if (ws[w] == 1) ws[w] = 0;
                }
            } else if (v->IsBikeLike()) {
                uint8_t* ws = v->BikeWheelStatus();
                for (int w = 0; w < 2; ++w) {
                    if (ws[w] == 1) ws[w] = 0;
                }
            }
        }

        // 2. Armor / Body damage mitigation (0..5 -> 0%, 20%, 35%, 50%, 62%, 74% reduction)
        float curHp = v->Health();
        if (c.armorLevel > 0 && curHp > 0.0f && curHp < tv.lastHealth && tv.lastHealth <= 1050.0f) {
            float dmg = tv.lastHealth - curHp;
            if (dmg > 0.1f && dmg < 650.0f) {
                static constexpr float kReduce[6] = {0.0f, 0.20f, 0.35f, 0.50f, 0.62f, 0.74f};
                int aIdx = std::clamp<int>(c.armorLevel, 0, 5);
                float refunded = dmg * kReduce[aIdx];
                curHp = std::min(1000.0f, curHp + refunded);
                v->Health() = curHp;
            }
        }
        tv.lastHealth = curHp;

        // 3. Active Driving Physics + Expressive Engine Tone + Subtle Turbo + Custom Horn
        if (playerDriving && v == playerVeh && v->Health() > 0.0f && !playerPed->IsInWater()) {
            float gas    = std::fabs(v->GasPedal());
            float speed  = v->Speed();
            uint8_t gear = v->GetCurrentGear();

            // Detect airborne vehicle via native IS_CAR_IN_AIR_PROPER (0x01F3) so audio/boost NEVER sticks in mid-air!
            bool inAir = (tv.handle > 0) && sa::ScriptCmd(0x01F3).Int(tv.handle).Run();

            // Estimate smooth in-gear RPM (0..1) so engine tone sweeps naturally with every shift!
            float rawRpm = 0.08f;
            float effectiveLoad = gas;
            if (inAir) {
                // Wheels unloaded in mid-air: free-rev lightly only while holding gas, drop immediately to quiet idle off-gas!
                rawRpm        = std::clamp(0.10f + gas * 0.44f, 0.08f, 0.54f);
                effectiveLoad = gas * 0.28f;
            } else {
                float gearMax = 0.20f;
                if (gear >= 1 && gear < 6) {
                    gearMax = std::max(0.15f, tv.customHandling.m_transmissionData.m_aGears[gear].m_fMaxVelocity);
                }
                rawRpm = std::clamp((speed / gearMax) * 0.85f + gas * 0.28f, 0.08f, 1.0f);
            }
            tv.smoothRpm += (rawRpm - tv.smoothRpm) * std::min(1.0f, dt * 8.5f);

            // Expressive Engine Exhaust Tone (Engine Lv 1..4) — layers warmly onto GTA SA's native engine!
            // Keep quiet when parked idle off-gas or flying off-gas so sound never drones annoyingly!
            bool engineAudible = v->IsEngineOn() && !(inAir && gas < 0.08f) && (speed > 0.015f || gas > 0.06f);
            if (c.engineLevel >= 1 && engineAudible) {
                audio::UpdateEngineTone(c.engineLevel, true, tv.smoothRpm, effectiveLoad);
                engineAudioHandled = true;

                // Subtle exhaust burble/pop on throttle lift at high RPM (Lv 3-4)
                if (!inAir && c.engineLevel >= 3 && tv.prevGas > 0.45f && gas < 0.12f &&
                    tv.smoothRpm > 0.58f && speed > 0.20f && (nowMs - tv.lastPopMs > 900)) {
                    audio::PlayExhaustPop(c.engineLevel);
                    tv.lastPopMs = nowMs;
                }
            }

            // Transmission crisp upshift punch (grounded only!)
            if (!inAir && c.transmissionLevel > 0 && gear > tv.prevGear && gas > 0.3f && speed > 0.10f) {
                sa::CVector fwd = v->GetForward();
                float kick = 0.013f * static_cast<float>(c.transmissionLevel);
                v->MoveSpeed().x += fwd.x * kick;
                v->MoveSpeed().y += fwd.y * kick;
            }

            // Turbocharger boost thrust + soft subtle spool & BOV (grounded load only — decays cleanly in mid-air!)
            if (c.turbo) {
                bool onGas = (!inAir && v->GasPedal() > 0.22f && v->IsEngineOn());
                if (onGas && speed > 0.03f) {
                    float targetBoost = std::clamp(gas * (0.40f + speed * 1.15f), 0.0f, 1.0f);
                    tv.turboBoost += (targetBoost - tv.turboBoost) * std::min(1.0f, dt * 3.2f);

                    if (tv.turboBoost > 0.30f) {
                        sa::CVector fwd = v->GetForward();
                        float maxCap = tv.customHandling.m_transmissionData.m_fMaxGearVelocity * 1.06f;
                        if (speed < maxCap) {
                            float thrust = (tv.turboBoost - 0.30f) * 0.19f * dt;
                            v->MoveSpeed().x += fwd.x * thrust;
                            v->MoveSpeed().y += fwd.y * thrust;
                        }
                    }
                } else {
                    if (!inAir && (tv.prevGas > 0.38f && gas <= 0.15f && tv.turboBoost > 0.45f) &&
                        (nowMs - tv.lastBovMs > 650)) {
                        audio::PlayTurboBov(tv.turboBoost);
                        tv.lastBovMs = nowMs;
                        tv.turboBoost *= 0.25f;
                    }
                    tv.turboBoost = std::max(0.0f, tv.turboBoost - dt * (inAir ? 4.5f : 2.8f));
                }

                if (!inAir && gear > tv.prevGear && tv.turboBoost > 0.52f && (nowMs - tv.lastBovMs > 650)) {
                    audio::PlayTurboBov(tv.turboBoost);
                    tv.lastBovMs = nowMs;
                    tv.turboBoost *= 0.55f;
                }

                if (!inAir && tv.turboBoost >= 0.08f) {
                    audio::UpdateTurboSpool(true, tv.turboBoost);
                    turboAudioHandled = true;
                }
            }

            // Custom Horn
            if (c.hornType >= 1 && c.hornType <= 4) {
                bool hornPressed = (v->GetHornState() > 0) ||
                                   ((GetAsyncKeyState('H') & 0x8000) != 0);
                audio::UpdateCustomHorn(c.hornType, hornPressed);
                hornAudioHandled = true;
            }

            tv.prevGas  = gas;
            tv.prevGear = gear;
        }
    }

    if (!engineAudioHandled) audio::UpdateEngineTone(0, false, 0.0f, 0.0f);
    if (!turboAudioHandled)  audio::UpdateTurboSpool(false, 0.0f);
    if (!hornAudioHandled)   audio::UpdateCustomHorn(0, false);
}

} // namespace tuning
