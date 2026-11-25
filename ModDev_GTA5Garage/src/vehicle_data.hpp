#pragma once
#include <array>
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
};

inline bool HasAnyCustomTuning(const PerformanceTuning& t) {
    return t.engineLevel > 0 || t.transmissionLevel > 0 || t.suspensionLevel > 0 ||
           t.brakesLevel > 0 || t.tiresGripLevel > 0 || t.armorLevel > 0 ||
           t.bulletproofTires || t.turbo || t.xenonLights ||
           t.neonColor != NeonColor::None || t.hornType > 0 || t.plateText[0] != '\0';
}

} // namespace mod
