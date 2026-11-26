#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace mod {

constexpr int MAX_OWNED_VEHICLES = 10;
constexpr int VEHICLES_PER_PAGE  = 5;

enum class NeonColor : uint8_t {
    None = 0,
    White,
    Blue,
    Red,
    Green,
    Purple,
    Yellow,
    Orange,
    Pink,
    Count
};

struct PerformanceTuning {
    uint8_t   engineLevel       = 0; // 0..4 (Stock, EMS 1..4)
    uint8_t   transmissionLevel = 0; // 0..3 (Stock, Street, Sport, Race)
    uint8_t   suspensionLevel   = 0; // 0..4 (Stock, Lowered, Street, Sport, Competition)
    uint8_t   brakesLevel       = 0; // 0..3 (Stock, Street, Sport, Race)
    uint8_t   tiresGripLevel    = 0; // 0..3 (Stock, Street, Sport, Slick/Race)
    uint8_t   armorLevel        = 0; // 0..5 (None, 20%, 40%, 60%, 80%, 100%)
    bool      bulletproofTires  = false;
    bool      turbo             = false;
    bool      xenonLights       = false;
    NeonColor neonColor         = NeonColor::None;
    uint8_t   hornType          = 0; // 0 = Stock, 1 = Airhorn, 2 = Sport, 3 = Classic, 4 = Chime
    char      plateText[9]      = {}; // Up to 8 ASCII chars + null
};

// ETS 2-style per-subsystem mechanical wear (0.0 = factory fresh, 100.0 = completely worn out)
struct VehicleWear {
    float engine       = 0.0f; // Degrades Top Speed & Acceleration; sputters at >85%
    float transmission = 0.0f; // Degrades Acceleration & shift crispness
    float brakes       = 0.0f; // Degrades Braking deceleration strongly
    float tires        = 0.0f; // Degrades Traction/Handling; blowout risk >88% if non-BP
    float suspension   = 0.0f; // Degrades Handling stability & damping
    float chassis      = 0.0f; // Degrades Armor durability & impact protection

    void ClampAll() {
        engine       = std::clamp(engine,       0.0f, 100.0f);
        transmission = std::clamp(transmission, 0.0f, 100.0f);
        brakes       = std::clamp(brakes,       0.0f, 100.0f);
        tires        = std::clamp(tires,        0.0f, 100.0f);
        suspension   = std::clamp(suspension,   0.0f, 100.0f);
        chassis      = std::clamp(chassis,      0.0f, 100.0f);
    }

    float Average() const {
        return (engine + transmission + brakes + tires + suspension + chassis) / 6.0f;
    }

    float MaxPart() const {
        return std::max({engine, transmission, brakes, tires, suspension, chassis});
    }

    bool HasAnyWear() const {
        return MaxPart() >= 0.5f;
    }

    void ApplyExplosionPenalty() {
        engine       += 32.0f;
        transmission += 24.0f;
        brakes       += 20.0f;
        tires        += 30.0f;
        suspension   += 26.0f;
        chassis      += 38.0f;
        ClampAll();
    }
};

struct VisualAppearance {
    uint8_t primaryColor   = 0;
    uint8_t secondaryColor = 0;
    int8_t  paintjob       = -1; // -1 = none, 0..2
    std::array<int16_t, 15> upgrades = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
};

struct OwnedVehicle {
    bool              occupied   = false;
    bool              destroyed  = false;
    int32_t           modelId    = 0;
    // Stored as raw game-encoded name (directly renderable in CFont!) or UTF-8 converted on load
    std::string       displayNameGame;
    PerformanceTuning tuning{};
    VisualAppearance  visuals{};
    VehicleWear       wear{};
};

// Persists tuning, custom plate, universal mods & mechanical wear across vanilla GTA SA safehouse garage storage!
constexpr int MAX_SAFEHOUSE_CARS = 32;
struct SafehouseCarRecord {
    bool              occupied        = false;
    int               hideoutGarageId = -1; // Index into CGarages::aGarages
    int               personalSlotIdx = -1; // 0..9 if registered personal vehicle, -1 if unregistered tuned car
    int32_t           modelId         = 0;
    VisualAppearance  visuals{};
    PerformanceTuning tuning{};
    VehicleWear       wear{};
};

inline bool HasAnyCustomTuning(const PerformanceTuning& t) {
    return t.engineLevel > 0 || t.transmissionLevel > 0 || t.suspensionLevel > 0 ||
           t.brakesLevel > 0 || t.tiresGripLevel > 0 || t.armorLevel > 0 ||
           t.bulletproofTires || t.turbo || t.xenonLights ||
           t.neonColor != NeonColor::None || t.hornType > 0 || t.plateText[0] != '\0';
}

// Compute expensive maintenance repair cost for a specific wear percentage & vehicle tier!
inline int ComputePartServiceCost(float wearPct, int baseFullCost, const PerformanceTuning& tuning, float mult = 1.0f) {
    if (wearPct < 0.5f) return 0;
    // Upgraded performance parts cost significantly more to maintain!
    float tierMult = 1.0f + 0.14f * static_cast<float>(tuning.engineLevel + tuning.transmissionLevel +
                                                       tuning.brakesLevel + tuning.suspensionLevel +
                                                       tuning.tiresGripLevel + tuning.armorLevel) * 0.25f
                          + (tuning.turbo ? 0.20f : 0.0f);
    float raw = static_cast<float>(baseFullCost) * (wearPct / 100.0f) * tierMult * mult;
    int cost = static_cast<int>(std::round(raw / 50.0f)) * 50;
    return std::max(150, cost);
}

} // namespace mod
