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
#include "garage_config.hpp"
#include "integrations/limit_adjusters.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "mod_bus.hpp"

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
    mod::VehicleWear       wear{};
    mod::VehicleWear       lastAppliedWear{};
    float                  appliedSkill01 = 0.0f;

    // Live runtime state
    float    lastHealth   = 1000.0f;
    float    turboBoost   = 0.0f;
    float    prevGas      = 0.0f;
    uint8_t  prevGear     = 0;
    uint32_t lastBovMs    = 0;
    uint32_t lastSputterMs= 0;
};

TrackedVehicle g_tracked[MAX_TRACKED_VEHICLES];

struct CachedModelHandling {
    bool              valid    = false;
    uint32_t          subClass = sa::VEHICLE_AUTOMOBILE;
    sa::tHandlingData handling{};
};
CachedModelHandling g_modelBaseCache[212]; // Models 400..611

inline bool IsFourWheelSubClass(uint32_t subClass) {
    return subClass == sa::VEHICLE_AUTOMOBILE ||
           subClass == sa::VEHICLE_MTRUCK ||
           subClass == sa::VEHICLE_QUAD;
}

inline float GetPlayerVehicleSkill01(uint32_t subClass) {
    switch (subClass) {
        case sa::VEHICLE_AUTOMOBILE:
        case sa::VEHICLE_MTRUCK:
        case sa::VEHICLE_QUAD:
            return sa::stats::GetNormalized01(sa::stats::STAT_DRIVING_SKILL);
        case sa::VEHICLE_BIKE:
            return sa::stats::GetNormalized01(sa::stats::STAT_BIKE_SKILL);
        case sa::VEHICLE_BMX:
            return sa::stats::GetNormalized01(sa::stats::STAT_CYCLING_SKILL);
        case sa::VEHICLE_HELI:
        case sa::VEHICLE_PLANE:
        case sa::VEHICLE_FHELI:
        case sa::VEHICLE_FPLANE:
            return sa::stats::GetNormalized01(sa::stats::STAT_FLYING_SKILL);
        default:
            return 0.0f;
    }
}

inline float GetTargetDriverSkill01(const sa::CVehicle* veh, const sa::CPed* playerPed) {
    if (!sa::IsVehicleValid(veh)) return 0.0f;
    const sa::CPed* drv = veh->GetDriver();
    // SA-COOP safety: never apply local CJ's driving skill while another player or NPC is driving!
    if (drv != nullptr && drv != playerPed) return 0.0f;
    return GetPlayerVehicleSkill01(veh->GetSubClass());
}

inline bool IsHandlingSane(const sa::tHandlingData* h) {
    if (!h) return false;
    float mv = h->m_transmissionData.m_fMaxGearVelocity;
    float g5 = h->m_transmissionData.m_aGears[5].m_fMaxVelocity;
    float g4 = h->m_transmissionData.m_aGears[4].m_fMaxVelocity;
    float best = std::max({mv, g5, g4});
    return std::isfinite(best) && best >= 0.08f && best <= 12.0f &&
           std::isfinite(h->m_fMass) && h->m_fMass >= 50.0f && h->m_fMass <= 200000.0f;
}

void CacheModelHandling(int32_t modelId, uint32_t subClass, const sa::tHandlingData* h) {
    if (modelId < 400 || modelId > 611 || !IsHandlingSane(h)) return;
    auto& slot = g_modelBaseCache[modelId - 400];
    slot.valid    = true;
    slot.subClass = subClass;
    std::memcpy(&slot.handling, h, sizeof(sa::tHandlingData));
}

bool LoadHandlingFromCfgFallback(int32_t modelId, sa::tHandlingData& out, uint32_t& outSubClass) {
    if (modelId < 400 || modelId > 611) return false;
    const std::string& gDir = mod::Config().gameDir;
    if (gDir.empty()) return false;

    // 1. Resolve modelId -> handling name & vehicle type from data\vehicles.ide
    std::string handlingName;
    outSubClass = sa::VEHICLE_AUTOMOBILE;
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
                if (_stricmp(vtype.c_str(), "mtruck") == 0)       outSubClass = sa::VEHICLE_MTRUCK;
                else if (_stricmp(vtype.c_str(), "quad") == 0)    outSubClass = sa::VEHICLE_QUAD;
                else if (_stricmp(vtype.c_str(), "bike") == 0)    outSubClass = sa::VEHICLE_BIKE;
                else if (_stricmp(vtype.c_str(), "bmx") == 0)     outSubClass = sa::VEHICLE_BMX;
                else if (_stricmp(vtype.c_str(), "heli") == 0)    outSubClass = sa::VEHICLE_HELI;
                else if (_stricmp(vtype.c_str(), "plane") == 0)   outSubClass = sa::VEHICLE_PLANE;
                else if (_stricmp(vtype.c_str(), "boat") == 0)    outSubClass = sa::VEHICLE_BOAT;
                else if (_stricmp(vtype.c_str(), "train") == 0)   outSubClass = sa::VEHICLE_TRAIN;
                else if (_stricmp(vtype.c_str(), "trailer") == 0) outSubClass = sa::VEHICLE_TRAILER;
                else                                              outSubClass = sa::VEHICLE_AUTOMOBILE;
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
            out.m_transmissionData.m_fMinGearVelocity = -0.25f;
            out.m_transmissionData.m_fEngineAcceleration = accel * 0.4f * 0.0004f;
            out.m_transmissionData.m_fEngineInertia = inertia;
            out.m_fBrakeDeceleration = brakeDecel * 0.0004f;
            sa::InitTransmissionGearRatios(&out.m_transmissionData);
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
                CacheModelHandling(g_tracked[i].modelId, veh->GetSubClass(), g_tracked[i].origHandling);
            }
            g_tracked[i].lastHealth = veh->Health();
            g_tracked[i].prevGear   = veh->GetCurrentGear();
            return &g_tracked[i];
        }
    }
    return nullptr;
}

// Compute tuned handling from a clean base handling snapshot + ETS 2 wear penalties + CJ driver skill
void BuildTunedHandling(const sa::tHandlingData& base, bool isFourWheel,
                        const mod::PerformanceTuning& c, const mod::VehicleWear& wear,
                        float driverSkill01, sa::tHandlingData& out) {
    std::memcpy(&out, &base, sizeof(sa::tHandlingData));

    // 1. Engine (0..4): realistic percentage-based gains that preserve class hierarchy!
    // Slow sedans become brisk & pleasant (+22% accel / +12% top), while real supercars become monsters.
    static constexpr float kEngAccel[5] = {1.00f, 1.05f, 1.10f, 1.16f, 1.22f};
    static constexpr float kEngTop[5]   = {1.00f, 1.02f, 1.05f, 1.08f, 1.12f};
    static constexpr float kEngDrag[5]  = {1.00f, 0.98f, 0.95f, 0.92f, 0.89f};
    int eIdx = std::clamp<int>(c.engineLevel, 0, 4);

    // 2. Transmission (0..3): adds extra forward gears (up to GTA SA's 5-speed max, e.g. Hermes 4 -> 5!)
    // + modest percentage acceleration/top speed bump + snappier shift response!
    static constexpr float kTransAccel[4]   = {1.00f, 1.03f, 1.07f, 1.11f};
    static constexpr float kTransTop[4]     = {1.00f, 1.02f, 1.04f, 1.06f};
    static constexpr float kTransInertia[4] = {1.00f, 0.88f, 0.76f, 0.64f};
    int tIdx = std::clamp<int>(c.transmissionLevel, 0, 3);

    int baseGears = std::clamp<int>(base.m_transmissionData.m_nNumberOfGears, 1, 5);
    int newGears  = baseGears;
    if (tIdx == 1 && baseGears < 5)      newGears = std::min(5, baseGears + 1);
    else if (tIdx == 2 && baseGears < 5) newGears = std::min(5, baseGears + 2);
    else if (tIdx >= 3 && baseGears >= 2) newGears = 5;

    // Turbo: clean percentage power boost (+9% accel, +4% top speed, -4% drag; maxed combo = ~1.48x accel / ~1.24x top!)
    float turboAccel = c.turbo ? 1.09f : 1.00f;
    float turboTop   = c.turbo ? 1.04f : 1.00f;
    float turboDrag  = c.turbo ? 0.96f : 1.00f;

    float accelMult = kEngAccel[eIdx] * kTransAccel[tIdx] * turboAccel;
    float topMult   = kEngTop[eIdx] * kTransTop[tIdx] * turboTop;

    out.m_fDragMult *= (kEngDrag[eIdx] * turboDrag);
    out.m_transmissionData.m_nNumberOfGears      = static_cast<uint8_t>(newGears);
    out.m_transmissionData.m_fEngineAcceleration *= accelMult;
    out.m_transmissionData.m_fEngineInertia      *= kTransInertia[tIdx];
    out.m_transmissionData.m_fMaxGearVelocity    *= topMult;
    if (out.m_transmissionData.m_fMaxGearVelocity <= 0.05f) {
        for (int g = 1; g < 6; ++g) {
            out.m_transmissionData.m_aGears[g].m_fMaxVelocity        *= topMult;
            out.m_transmissionData.m_aGears[g].m_fChangeUpVelocity   *= topMult;
            out.m_transmissionData.m_aGears[g].m_fChangeDownVelocity *= topMult;
        }
    }

    // 3. Suspension (0..4) — 4-wheel vehicles
    // Realistic flush sports lowering that stays compatible with Izzy's Lively Vehicle Physics
    // (never slams chassis into the floor or forces broken wheel camber!).
    if (isFourWheel && c.suspensionLevel > 0) {
        static constexpr float kSuspDrop[5]  = {0.00f, 0.015f, 0.032f, 0.050f, 0.068f};
        static constexpr float kSuspForce[5] = {1.00f, 1.08f,  1.16f,  1.26f,  1.36f};
        static constexpr float kSuspDamp[5]  = {1.00f, 1.08f,  1.17f,  1.27f,  1.38f};
        static constexpr float kAntiDive[5]  = {1.00f, 1.14f,  1.28f,  1.42f,  1.56f};
        int sIdx = std::clamp<int>(c.suspensionLevel, 0, 4);

        float stockDroop = std::max(0.0f, -out.m_fSuspensionLowerLimit);
        float drop       = std::min(kSuspDrop[sIdx], stockDroop * 0.42f);
        float newLower   = out.m_fSuspensionLowerLimit + drop;
        float maxSafeLower = std::min(-0.08f, out.m_fSuspensionUpperLimit - 0.16f);
        if (newLower > maxSafeLower) newLower = maxSafeLower;
        if (newLower > out.m_fSuspensionLowerLimit) {
            out.m_fSuspensionLowerLimit = newLower;
        }
        out.m_fSuspensionForceLevel *= kSuspForce[sIdx];
        out.m_fSuspensionDampingLevel *= kSuspDamp[sIdx];
        out.m_fSuspensionAntiDiveMultiplier *= kAntiDive[sIdx];
    }

    // 4. Brakes (0..3): proportional stopping power upgrade (+12% / +25% / +40%)
    static constexpr float kBrakes[4] = {1.00f, 1.12f, 1.25f, 1.40f};
    int bIdx = std::clamp<int>(c.brakesLevel, 0, 3);
    out.m_fBrakeDeceleration *= kBrakes[bIdx];

    // 5. Tires Grip (0..3): realistic grip scaling (+6% / +12% / +18%)
    static constexpr float kTraction[4] = {1.00f, 1.06f, 1.12f, 1.18f};
    static constexpr float kTracLoss[4] = {1.00f, 1.05f, 1.10f, 1.15f};
    int gIdx = std::clamp<int>(c.tiresGripLevel, 0, 3);
    out.m_fTractionMultiplier *= kTraction[gIdx];
    out.m_fTractionLoss       *= kTracLoss[gIdx];

    // 6. Armor / Body (0..5 = 0..100%)
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

    // 7. ETS 2-Style Mechanical Wear Degradation (Strongly degrades real vehicle performance!)
    if (mod::Config().enableWearSystem && wear.HasAnyWear()) {
        const float wE  = std::clamp(wear.engine,       0.0f, 100.0f) * 0.01f;
        const float wTr = std::clamp(wear.transmission, 0.0f, 100.0f) * 0.01f;
        const float wB  = std::clamp(wear.brakes,       0.0f, 100.0f) * 0.01f;
        const float wTi = std::clamp(wear.tires,        0.0f, 100.0f) * 0.01f;
        const float wS  = std::clamp(wear.suspension,   0.0f, 100.0f) * 0.01f;
        const float wC  = std::clamp(wear.chassis,      0.0f, 100.0f) * 0.01f;

        // Engine + Transmission wear kill Top Speed & Acceleration
        float wearTopMult   = std::max(0.58f, 1.0f - 0.36f * wE - 0.06f * wTr);
        float wearAccelMult = std::max(0.38f, (1.0f - 0.44f * wE) * (1.0f - 0.28f * wTr));
        out.m_fDragMult *= (1.0f + 0.30f * wE + 0.12f * wTi);
        out.m_transmissionData.m_fEngineAcceleration *= wearAccelMult;
        out.m_transmissionData.m_fEngineInertia      *= (1.0f + 0.75f * wTr);
        out.m_transmissionData.m_fMaxGearVelocity *= wearTopMult;
        if (out.m_transmissionData.m_fMaxGearVelocity <= 0.05f) {
            for (int g = 1; g < 6; ++g) {
                out.m_transmissionData.m_aGears[g].m_fMaxVelocity        *= wearTopMult;
                out.m_transmissionData.m_aGears[g].m_fChangeUpVelocity   *= wearTopMult;
                out.m_transmissionData.m_aGears[g].m_fChangeDownVelocity *= wearTopMult;
            }
        }

        // Brakes wear weakens deceleration strongly
        out.m_fBrakeDeceleration *= std::max(0.38f, 1.0f - 0.58f * wB);

        // Tires + Suspension wear ruin traction & cornering stability
        out.m_fTractionMultiplier *= std::max(0.45f, (1.0f - 0.40f * wTi) * (1.0f - 0.15f * wS));
        out.m_fTractionLoss       *= std::max(0.50f, 1.0f - 0.36f * wTi);
        if (isFourWheel && wS > 0.005f) {
            out.m_fSuspensionForceLevel         *= std::max(0.60f, 1.0f - 0.35f * wS);
            out.m_fSuspensionDampingLevel       *= std::max(0.48f, 1.0f - 0.48f * wS);
            out.m_fSuspensionAntiDiveMultiplier *= std::max(0.50f, 1.0f - 0.45f * wS);
        }

        // Chassis wear makes body take heavier impact damage
        out.m_fCollisionDamageMultiplier *= (1.0f + 0.85f * wC);
    }

    // 8. CJ Vehicle Skill Mastery (Driving / Bike / Cycling / Flying — gentle realistic bonus over stock + tuning + wear!)
    // Preserves vanilla CStats::GetFatAndMuscleModifier mid-air & bike balance while improving grip, braking & throttle response!
    if (driverSkill01 > 0.001f) {
        const float sk = std::clamp(driverSkill01, 0.0f, 1.0f);
        out.m_transmissionData.m_fEngineAcceleration *= (1.0f + 0.05f * sk);
        out.m_transmissionData.m_fEngineInertia      *= (1.0f - 0.06f * sk);
        out.m_fBrakeDeceleration                     *= (1.0f + 0.08f * sk);
        out.m_fTractionMultiplier                    *= (1.0f + 0.10f * sk);
        out.m_fTractionLoss                          *= (1.0f + 0.08f * sk);
    }

    // Recompute ALL forward gear curves (1..m_nNumberOfGears) + reverse via GTA SA's native InitGearRatios (0x006D0460)!
    // Ensures upgraded 4->5 gear cars (e.g. Hermes) shift smoothly all the way up to top speed without choking!
    if (out.m_transmissionData.m_fMaxGearVelocity > 0.05f) {
        sa::InitTransmissionGearRatios(&out.m_transmissionData);
    }
}

void RebuildHandling(TrackedVehicle& tv, float driverSkill01) {
    if (!tv.origHandling || !sa::IsVehicleValid(tv.veh)) return;

    tv.wear.ClampAll();
    tv.appliedSkill01 = std::clamp(driverSkill01, 0.0f, 1.0f);
    BuildTunedHandling(tv.baseHandling, tv.veh->IsFourWheelLike(), tv.cfg, tv.wear, tv.appliedSkill01, tv.customHandling);
    tv.veh->HandlingData() = &tv.customHandling;
    tv.lastAppliedWear = tv.wear;

    // Bulletproof Tires flag
    tv.veh->SetTyresDontBurst(tv.cfg.bulletproofTires);
    if (tv.handle > 0) {
        sa::ScriptCmd(0x053F).Int(tv.handle).Int(tv.cfg.bulletproofTires ? 0 : 1).Run();
    }
}

struct RawVehicleScores {
    int   topSpeedKmh = 0;
    int   topSpeedMph = 0;
    float accelRaw    = 0.0f;
    float brakeRaw    = 0.0f;
    float handRaw     = 0.0f;
    int   armorPct    = 100;
    int   numGears    = 5;
};

RawVehicleScores EvaluateVehicleScores(const sa::tHandlingData& base, bool isFourWheel,
                                       const mod::PerformanceTuning& cfg, const mod::VehicleWear& wear,
                                       float driverSkill01) {
    RawVehicleScores r{};
    sa::tHandlingData sim{};
    BuildTunedHandling(base, isFourWheel, cfg, wear, driverSkill01, sim);

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
    float wE = std::clamp(wear.engine, 0.0f, 100.0f) * 0.01f;
    float kmh = rawMax * 180.0f * dragFactor;
    if (cfg.turbo) kmh *= (1.0f + 0.025f * (1.0f - 0.40f * wE));
    r.topSpeedKmh = std::clamp(static_cast<int>(std::round(kmh)), 25, 380);
    r.topSpeedMph = std::clamp(static_cast<int>(std::round(kmh * 0.621371f)), 16, 240);

    float rawAccel = sim.m_transmissionData.m_fEngineAcceleration * 1000.0f;
    float inertiaBonus = (base.m_transmissionData.m_fEngineInertia > 0.1f)
        ? (base.m_transmissionData.m_fEngineInertia / std::max(1.0f, sim.m_transmissionData.m_fEngineInertia))
        : 1.0f;
    r.accelRaw = rawAccel * 185.0f * (0.75f + 0.25f * inertiaBonus);
    if (cfg.turbo) r.accelRaw *= (1.0f + 0.045f * (1.0f - 0.45f * wE));

    r.brakeRaw = sim.m_fBrakeDeceleration * 11500.0f;

    float wS = std::clamp(wear.suspension, 0.0f, 100.0f) * 0.01f;
    float suspBonus = (1.0f + static_cast<float>(cfg.suspensionLevel) * 0.055f) * (1.0f - 0.22f * wS);
    r.handRaw = (sim.m_fTractionMultiplier * 52.0f + sim.m_fTractionLoss * 28.0f) * suspBonus;

    float wC = std::clamp(wear.chassis, 0.0f, 100.0f) * 0.01f;
    int baseArmor = 100 + std::clamp<int>(cfg.armorLevel, 0, 5) * 20;
    r.armorPct = std::clamp(static_cast<int>(std::round(static_cast<float>(baseArmor) * (1.0f - 0.48f * wC))), 20, 200);
    r.numGears = std::clamp<int>(sim.m_transmissionData.m_nNumberOfGears, 1, 5);
    return r;
}

inline void ComputeSkillBonus(int baseInt, float baseRaw, float skilledRaw, int& outBonus, int& outPct) {
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

VehicleStats ComputeStatsFromHandling(const sa::tHandlingData& base, uint32_t subClass,
                                      const mod::PerformanceTuning& cfg, const mod::VehicleWear& wear) {
    const bool isFourWheel = IsFourWheelSubClass(subClass);
    const RawVehicleScores baseR = EvaluateVehicleScores(base, isFourWheel, cfg, wear, 0.0f);

    VehicleStats st{};
    st.topSpeedKmh    = baseR.topSpeedKmh;
    st.topSpeedMph    = baseR.topSpeedMph;
    st.accelRating    = std::clamp(static_cast<int>(std::round(baseR.accelRaw)), 6, 150);
    st.brakeRating    = std::clamp(static_cast<int>(std::round(baseR.brakeRaw)), 6, 150);
    st.handlingRating = std::clamp(static_cast<int>(std::round(baseR.handRaw)),  8, 150);
    st.armorPercent   = baseR.armorPct;
    st.numGears       = baseR.numGears;

    const float liveSkill = GetPlayerVehicleSkill01(subClass);
    if (liveSkill > 0.001f) {
        const RawVehicleScores skillR = EvaluateVehicleScores(base, isFourWheel, cfg, wear, liveSkill);
        ComputeSkillBonus(st.accelRating,    baseR.accelRaw, skillR.accelRaw, st.accelSkillBonus,    st.accelSkillPct);
        ComputeSkillBonus(st.brakeRating,    baseR.brakeRaw, skillR.brakeRaw, st.brakeSkillBonus,    st.brakeSkillPct);
        ComputeSkillBonus(st.handlingRating, baseR.handRaw,  skillR.handRaw,  st.handlingSkillBonus, st.handlingSkillPct);
    }

    return st;
}

} // namespace

VehicleStats ComputeStats(sa::CVehicle* veh, const mod::PerformanceTuning& cfg, const mod::VehicleWear& wear) {
    if (!sa::IsVehicleValid(veh)) return VehicleStats{};

    const uint32_t sc = veh->GetSubClass();
    const sa::tHandlingData* base = nullptr;
    if (auto* tv = FindSlot(veh)) {
        base = &tv->baseHandling;
    } else {
        base = veh->HandlingData();
        CacheModelHandling(veh->GetModelIndex(), sc, base);
    }
    if (!base) return VehicleStats{};
    return ComputeStatsFromHandling(*base, sc, cfg, wear);
}

VehicleStats ComputeStatsForModel(int32_t modelId, const mod::PerformanceTuning& cfg, const mod::VehicleWear& wear) {
    if (modelId < 400 || modelId > 611) return VehicleStats{};

    // Tier 1: Live tracked vehicle of this model (100% exact clean base handling snapshot!)
    for (int i = 0; i < MAX_TRACKED_VEHICLES; ++i) {
        if (g_tracked[i].inUse && g_tracked[i].modelId == modelId && IsHandlingSane(&g_tracked[i].baseHandling)) {
            uint32_t sc = sa::IsVehicleValid(g_tracked[i].veh) ? g_tracked[i].veh->GetSubClass() : sa::VEHICLE_AUTOMOBILE;
            CacheModelHandling(modelId, sc, &g_tracked[i].baseHandling);
            return ComputeStatsFromHandling(g_tracked[i].baseHandling, sc, cfg, wear);
        }
    }

    // Tier 2: Any live vehicle of this model currently spawned in GTA SA's vehicle pool
    if (auto* pool = sa::GetVehiclePool(); pool && pool->objects && pool->byteMap && pool->size > 0 && pool->size < 65536) {
        for (int i = 0; i < pool->size; ++i) {
            if ((pool->byteMap[i] & 0x80) != 0) continue;
            auto* v = reinterpret_cast<sa::CVehicle*>(pool->objects + static_cast<size_t>(i) * sa::VEHICLE_POOL_STRIDE);
            if (v->GetModelIndex() == modelId) {
                const sa::tHandlingData* h = v->HandlingData();
                if (auto* tv = FindSlot(v)) h = &tv->baseHandling;
                if (IsHandlingSane(h)) {
                    uint32_t sc = v->GetSubClass();
                    CacheModelHandling(modelId, sc, h);
                    return ComputeStatsFromHandling(*h, sc, cfg, wear);
                }
            }
        }
    }

    // Tier 3: Previously cached live handling snapshot for this model
    if (g_modelBaseCache[modelId - 400].valid) {
        const auto& c = g_modelBaseCache[modelId - 400];
        return ComputeStatsFromHandling(c.handling, c.subClass, cfg, wear);
    }

    // Tier 4: Runtime-patched gHandlingDataMgr lookup via shared Limit Adjusters integration!
    uint8_t* handlingBase = integrations::limits::GetHandlingDataMgrBase();
    const bool handlingRelocated = (reinterpret_cast<uintptr_t>(handlingBase) != 0x00C2B9DCu);
    const int  maxHandlingId     = handlingRelocated ? 1024 : 210;

    uint8_t* mi = reinterpret_cast<uint8_t*>(integrations::limits::GetModelInfo(modelId));
    if (mi && handlingBase) {
        uint8_t hid = *reinterpret_cast<const uint8_t*>(mi + 0x4A);
        int32_t vtype = *reinterpret_cast<const int32_t*>(mi + 0x3C);
        uint32_t sc = (vtype >= 0 && vtype <= 11) ? static_cast<uint32_t>(vtype) : sa::VEHICLE_AUTOMOBILE;

        if (static_cast<int>(hid) < maxHandlingId) {
            const auto* base = reinterpret_cast<const sa::tHandlingData*>(
                handlingBase + static_cast<size_t>(hid) * 0xE0u);
            if (IsHandlingSane(base)) {
                CacheModelHandling(modelId, sc, base);
                return ComputeStatsFromHandling(*base, sc, cfg, wear);
            }
        }

        char gxt[12] = {};
        std::memcpy(gxt, mi + 0x32, 8);
        gxt[8] = '\0';
        if (gxt[0] != '\0') {
            using FnGetHid = int32_t(__thiscall*)(void*, const char*);
            int32_t hid2 = reinterpret_cast<FnGetHid>(0x006F4FD0)(reinterpret_cast<void*>(0x00C2B9C8), gxt);
            if (hid2 >= 0 && hid2 < maxHandlingId) {
                const auto* base2 = reinterpret_cast<const sa::tHandlingData*>(
                    handlingBase + static_cast<size_t>(hid2) * 0xE0u);
                if (IsHandlingSane(base2)) {
                    CacheModelHandling(modelId, sc, base2);
                    return ComputeStatsFromHandling(*base2, sc, cfg, wear);
                }
            }
        }
    }

    // Tier 5: Direct data\vehicles.ide + data\handling.cfg file fallback
    sa::tHandlingData cfgFallback{};
    uint32_t fbSubClass = sa::VEHICLE_AUTOMOBILE;
    if (LoadHandlingFromCfgFallback(modelId, cfgFallback, fbSubClass)) {
        CacheModelHandling(modelId, fbSubClass, &cfgFallback);
        modlog::Info("[Tuning] Used handling.cfg fallback for model %d (maxVel=%.2f).",
                     modelId, cfgFallback.m_transmissionData.m_fMaxGearVelocity * 180.0f);
        return ComputeStatsFromHandling(cfgFallback, fbSubClass, cfg, wear);
    }

    return VehicleStats{};
}

void ApplyTuning(sa::CVehicle* veh, const mod::PerformanceTuning& cfg, const mod::VehicleWear* wearOverride) {
    if (!sa::IsVehicleValid(veh)) return;
    TrackedVehicle* tv = AllocSlot(veh);
    if (!tv) {
        modlog::Info("[Tuning] Pool full, could not track vehicle %p", veh);
        return;
    }
    tv->cfg = cfg;
    if (wearOverride) {
        tv->wear = *wearOverride;
        tv->wear.ClampAll();
    }
    RebuildHandling(*tv, GetTargetDriverSkill01(veh, sa::FindPlayerPed(0)));
}

void SetLiveWear(sa::CVehicle* veh, const mod::VehicleWear& wear) {
    if (!sa::IsVehicleValid(veh)) return;
    TrackedVehicle* tv = AllocSlot(veh);
    if (!tv) return;
    tv->wear = wear;
    tv->wear.ClampAll();
    RebuildHandling(*tv, GetTargetDriverSkill01(veh, sa::FindPlayerPed(0)));
}

mod::VehicleWear GetLiveWear(const sa::CVehicle* veh) {
    if (auto* tv = FindSlot(veh)) return tv->wear;
    return mod::VehicleWear{};
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

void EnsureStreetVehicleWear(sa::CVehicle* veh) {
    if (!mod::Config().enableWearSystem || !sa::IsVehicleValid(veh) || !veh->IsLandVehicle()) return;
    // Never overwrite an already-tracked vehicle (Dealership 0%-wear, registered personal car, or already-rolled street car!)
    if (FindSlot(veh) != nullptr) return;
    // Never assign random street wear to a vehicle sitting inside a vanilla safehouse garage (let safehouse restore run!)
    if (sa::FindHideoutGarageIndexAt(veh->GetPosition()) >= 0) return;
    // Don't artificially slow down scripted mission-spawned vehicles during active story missions
    if (sa::IsOnMission() && veh->CreatedBy() == 2) return;

    // Deterministic per-vehicle wear roll strictly averaging 35%..85% across all 6 subsystems
    uint32_t seed = static_cast<uint32_t>(sa::GetVehicleHandle(veh)) * 2654435761u ^
                    static_cast<uint32_t>(veh->GetModelIndex()) * 2246822519u ^
                    (sa::GetTimeMs() / 4000u) * 3266489917u;
    auto nextF = [&]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };

    float targetAvg = 35.0f + nextF() * 50.0f; // 35%..85% average mechanical wear!
    float raw[6];
    float sum = 0.0f;
    for (int i = 0; i < 6; ++i) {
        raw[i] = std::clamp(targetAvg + (nextF() - 0.5f) * 28.0f, 18.0f, 96.0f);
        sum += raw[i];
    }
    float shift = targetAvg - (sum / 6.0f);

    mod::VehicleWear w{};
    w.engine       = std::clamp(raw[0] + shift, 15.0f, 97.0f);
    w.transmission = std::clamp(raw[1] + shift, 15.0f, 97.0f);
    w.brakes       = std::clamp(raw[2] + shift, 15.0f, 97.0f);
    w.tires        = std::clamp(raw[3] + shift, 15.0f, 97.0f);
    w.suspension   = std::clamp(raw[4] + shift, 15.0f, 97.0f);
    w.chassis      = std::clamp(raw[5] + shift, 15.0f, 97.0f);
    w.ClampAll();

    ApplyTuning(veh, mod::PerformanceTuning{}, &w);
    modlog::Info("[Wear] Assigned realistic street wear to model %d (avg=%.1f%%: eng=%.0f, tr=%.0f, brk=%.0f, tir=%.0f, sus=%.0f, chs=%.0f).",
                 veh->GetModelIndex(), w.Average(), w.engine, w.transmission, w.brakes, w.tires, w.suspension, w.chassis);
}

void TickAll() {
    if (sa::IsPauseMenuActive() || !integrations::sacoop::IsGameWindowFocused() || sa::GetGameState() != 9) {
        audio::StopAll();
        return;
    }

    sa::CVehicle* playerVeh = sa::FindPlayerVehicle(0, false);
    sa::CPed* playerPed = sa::FindPlayerPed(0);
    bool playerDriving = (playerVeh != nullptr && playerPed != nullptr && playerVeh->GetDriver() == playerPed);

    // Street-picked vehicles start with 35%-85% mechanical wear so Dealership 0%-wear purchases stay valuable!
    if (playerDriving && playerVeh->IsLandVehicle()) {
        EnsureStreetVehicleWear(playerVeh);
        // Also ensure stock / untuned vehicles receive CJ's live vehicle skill bonus!
        const float liveSkill = GetPlayerVehicleSkill01(playerVeh->GetSubClass());
        if (liveSkill > 0.005f && !FindSlot(playerVeh) &&
            sa::FindHideoutGarageIndexAt(playerVeh->GetPosition()) < 0) {
            if (auto* tv = AllocSlot(playerVeh)) {
                RebuildHandling(*tv, liveSkill);
            }
        }
    }

    const float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
    const uint32_t nowMs = sa::GetTimeMs();

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

        // Keep CJ's live vehicle skill synced in real time (and stripped cleanly if another player/NPC drives in SA-COOP!)
        const float targetSkill = GetTargetDriverSkill01(v, playerPed);
        if (std::fabs(targetSkill - tv.appliedSkill01) >= 0.01f) {
            RebuildHandling(tv, targetSkill);
        }

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

        // 2. Armor / Body damage mitigation + Impact Mechanical Wear (Proper Vehicle Damage compatible!)
        float curHp = v->Health();
        if (curHp > 0.0f && curHp < tv.lastHealth && tv.lastHealth <= 1050.0f) {
            float dmg = tv.lastHealth - curHp;
            if (dmg > 0.1f && dmg < 2500.0f) {
                // Accumulate crash wear onto mechanical subsystems (ETS 2 style!)
                if (mod::Config().enableWearSystem && dmg > 4.0f) {
                    const float rate = mod::Config().wearRateMult;
                    const float armorShield = 1.0f - static_cast<float>(std::clamp<int>(c.armorLevel, 0, 5)) * 0.08f;
                    const float s = (dmg * 0.01f) * rate * armorShield;
                    tv.wear.chassis      += s * 4.8f;
                    tv.wear.engine       += s * 3.8f;
                    tv.wear.suspension   += s * 3.4f;
                    tv.wear.transmission += s * 2.2f;
                    tv.wear.tires        += s * 2.0f;
                    tv.wear.brakes       += s * 1.4f;
                    tv.wear.ClampAll();
                }

                if (c.armorLevel > 0) {
                    static constexpr float kReduce[6] = {0.0f, 0.20f, 0.35f, 0.50f, 0.62f, 0.74f};
                    int aIdx = std::clamp<int>(c.armorLevel, 0, 5);
                    float chassisIntegrity = mod::Config().enableWearSystem
                        ? (1.0f - 0.60f * (tv.wear.chassis * 0.01f))
                        : 1.0f;
                    float refunded = dmg * kReduce[aIdx] * std::clamp(chassisIntegrity, 0.30f, 1.0f);
                    curHp = std::min(1000.0f, curHp + refunded);
                    v->Health() = curHp;
                }
            }
        }
        tv.lastHealth = curHp;

        // 3. Active Driving Physics + Passive Mechanical Wear + Expressive Engine Tone + Turbo + Horn
        if (playerDriving && v == playerVeh && v->Health() > 0.0f && !playerPed->IsInWater()) {
            float gas    = std::fabs(v->GasPedal());
            float brake  = std::fabs(v->BrakePedal());
            float speed  = v->Speed();
            uint8_t gear = v->GetCurrentGear();

            // Detect airborne vehicle via native IS_CAR_IN_AIR_PROPER (0x01F3) so audio/boost NEVER sticks in mid-air!
            bool inAir = (tv.handle > 0) && sa::ScriptCmd(0x01F3).Int(tv.handle).Run();

            // Passive driving wear (distance & load based)
            if (mod::Config().enableWearSystem && speed > 0.025f) {
                const float km   = (speed * dt * 50.0f) * 0.001f;
                const float rate = mod::Config().wearRateMult;
                tv.wear.tires        += km * (0.90f + 0.50f * gas + (c.tiresGripLevel >= 3 ? 0.25f : 0.0f)) * (c.bulletproofTires ? 0.72f : 1.0f) * rate;
                tv.wear.engine       += km * (0.60f + 0.70f * gas * (c.turbo ? 1.25f : 1.0f)) * rate;
                tv.wear.transmission += km * (0.45f + 0.32f * gas) * rate;
                tv.wear.suspension   += km * (0.50f + (inAir ? 2.0f : 0.0f)) * rate;
                tv.wear.brakes       += ((brake > 0.12f) ? (km * 2.8f + dt * 0.14f) : (km * 0.10f)) * rate;
                tv.wear.chassis      += km * 0.18f * rate;
                tv.wear.ClampAll();
            }

            // Rebuild live handling whenever any part wears by >= 1% so degradation is felt immediately!
            if (mod::Config().enableWearSystem) {
                float maxDelta = std::max({
                    std::fabs(tv.wear.engine       - tv.lastAppliedWear.engine),
                    std::fabs(tv.wear.transmission - tv.lastAppliedWear.transmission),
                    std::fabs(tv.wear.brakes       - tv.lastAppliedWear.brakes),
                    std::fabs(tv.wear.tires        - tv.lastAppliedWear.tires),
                    std::fabs(tv.wear.suspension   - tv.lastAppliedWear.suspension),
                    std::fabs(tv.wear.chassis      - tv.lastAppliedWear.chassis)
                });
                if (maxDelta >= 1.0f) {
                    RebuildHandling(tv, targetSkill);
                }

                // Severe wear symptoms (>85% Engine sputter, >92% non-BP Tire blowout risk)
                if (!inAir && tv.wear.engine >= 85.0f && gas > 0.55f && speed > 0.18f && (nowMs - tv.lastSputterMs > 4200)) {
                    v->MoveSpeed().x *= 0.94f;
                    v->MoveSpeed().y *= 0.94f;
                    tv.lastSputterMs = nowMs;
                }
                if (!inAir && !c.bulletproofTires && tv.wear.tires >= 92.0f && speed > 0.38f && (nowMs - tv.lastSputterMs > 9500)) {
                    if (v->IsFourWheelLike()) {
                        v->AutoWheelStatus()[(nowMs / 100) % 4] = 1;
                    } else if (v->IsBikeLike()) {
                        v->BikeWheelStatus()[(nowMs / 100) % 2] = 1;
                    }
                    tv.lastSputterMs = nowMs;
                }
            }

            // Scale live transmission & turbo feel proportionally to the vehicle's own base acceleration
            // so slow city cars stay realistic while high-power sports/supercars accelerate fiercely!
            const float baseAccNorm = std::clamp(tv.baseHandling.m_transmissionData.m_fEngineAcceleration * 500.0f, 0.35f, 1.80f);
            const float engWearFactor = mod::Config().enableWearSystem
                ? std::max(0.45f, 1.0f - 0.50f * (tv.wear.engine * 0.01f))
                : 1.0f;

            // Transmission crisp upshift punch (grounded only, proportional to car power!)
            if (!inAir && c.transmissionLevel > 0 && gear > tv.prevGear && gas > 0.3f && speed > 0.10f) {
                sa::CVector fwd = v->GetForward();
                float kick = 0.0045f * static_cast<float>(c.transmissionLevel) * baseAccNorm * engWearFactor;
                v->MoveSpeed().x += fwd.x * kick;
                v->MoveSpeed().y += fwd.y * kick;
            }

            // Turbocharger boost thrust (proportional to car power!) + spool & BOV on gear shifts / throttle lift
            if (c.turbo) {
                bool onGas = (!inAir && v->GasPedal() > 0.22f && v->IsEngineOn());
                if (onGas && speed > 0.03f) {
                    float targetBoost = std::clamp(gas * (0.40f + speed * 1.15f), 0.0f, 1.0f);
                    tv.turboBoost += (targetBoost - tv.turboBoost) * std::min(1.0f, dt * 3.2f);

                    if (tv.turboBoost > 0.30f) {
                        sa::CVector fwd = v->GetForward();
                        float maxCap = tv.customHandling.m_transmissionData.m_fMaxGearVelocity * 1.02f;
                        if (speed < maxCap) {
                            float thrust = (tv.turboBoost - 0.30f) * 0.075f * baseAccNorm * engWearFactor * dt;
                            v->MoveSpeed().x += fwd.x * thrust;
                            v->MoveSpeed().y += fwd.y * thrust;
                        }
                    }
                } else {
                    if (!inAir && (tv.prevGas > 0.35f && gas <= 0.15f && tv.turboBoost > 0.40f) &&
                        (nowMs - tv.lastBovMs > 600)) {
                        audio::PlayTurboBov(tv.turboBoost);
                        tv.lastBovMs = nowMs;
                        tv.turboBoost *= 0.25f;
                    }
                    tv.turboBoost = std::max(0.0f, tv.turboBoost - dt * (inAir ? 4.5f : 2.8f));
                }

                // Crisp light BOV pshh on brisk gear upshifts under boost!
                if (!inAir && gear > tv.prevGear && tv.turboBoost > 0.30f && (nowMs - tv.lastBovMs > 550)) {
                    audio::PlayTurboBov(tv.turboBoost);
                    tv.lastBovMs = nowMs;
                    tv.turboBoost *= 0.52f;
                }

                if (!inAir && tv.turboBoost >= 0.08f) {
                    audio::UpdateTurboSpool(true, tv.turboBoost);
                    turboAudioHandled = true;
                }
            }

            // Custom Horn (focus, overlay, and modal-menu guarded so SA-COOP chat 'H' never honks!)
            if (c.hornType >= 1 && c.hornType <= 4) {
                bool canKeyHorn = integrations::sacoop::IsGameWindowFocused() &&
                                  !integrations::sacoop::IsOverlayActive() &&
                                  !modbus::IsAnyModalMenuOpen() &&
                                  (sa::GetNumMenusInUse() == 0);
                bool hornPressed = (v->GetHornState() > 0) ||
                                   (canKeyHorn && ((GetAsyncKeyState('H') & 0x8000) != 0));
                audio::UpdateCustomHorn(c.hornType, hornPressed);
                hornAudioHandled = true;
            }

            tv.prevGas  = gas;
            tv.prevGear = gear;
        }
    }

    if (!turboAudioHandled)  audio::UpdateTurboSpool(false, 0.0f);
    if (!hornAudioHandled)   audio::UpdateCustomHorn(0, false);
}

} // namespace tuning
