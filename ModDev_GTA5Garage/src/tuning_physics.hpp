#pragma once
#include <cstdint>
#include "sa_sdk.hpp"
#include "vehicle_data.hpp"

namespace tuning {

struct VehicleStats {
    int topSpeedKmh    = 0;   // Calibrated achievable top speed in km/h
    int topSpeedMph    = 0;   // Calibrated achievable top speed in MPH (matches SA Styled Speedometer!)
    int accelRating    = 0;   // 0..100+ acceleration score
    int brakeRating    = 0;   // 0..100+ braking score
    int handlingRating = 0;   // 0..100+ grip & stability score
    int armorPercent   = 100; // 100%..200% effective durability
};

// Apply (or update) all GTA 5 performance tuning + ETS 2 wear penalties to a live CVehicle*
// Isolated per-vehicle handling clone so traffic cars of the same model are NEVER affected!
void ApplyTuning(sa::CVehicle* veh, const mod::PerformanceTuning& cfg, const mod::VehicleWear* wearOverride = nullptr);

// Update only wear state on a live tracked vehicle and immediately rebuild its physics!
void SetLiveWear(sa::CVehicle* veh, const mod::VehicleWear& wear);
mod::VehicleWear GetLiveWear(const sa::CVehicle* veh);

// Compute realistic numeric vehicle stats for a live vehicle + hypothetical tuning + wear!
VehicleStats ComputeStats(sa::CVehicle* veh, const mod::PerformanceTuning& cfg, const mod::VehicleWear& wear = mod::VehicleWear{});

// Compute realistic numeric vehicle stats offline from modelId + saved tuning + wear (for F6/Phone/Dealership!)
VehicleStats ComputeStatsForModel(int32_t modelId, const mod::PerformanceTuning& cfg, const mod::VehicleWear& wear = mod::VehicleWear{});

// Query current tuning attached to a live CVehicle* (returns nullptr if stock/untracked)
const mod::PerformanceTuning* GetLiveTuning(const sa::CVehicle* veh);
mod::PerformanceTuning* GetLiveTuningMutable(sa::CVehicle* veh);

// Detach & restore original handling pointer safely
void DetachVehicle(sa::CVehicle* veh);
void ResetAll();

// Per-frame physics, wear accumulation, armor mitigation, bulletproof tires, turbo boost & audio, engine tone, custom horn
void TickAll();

} // namespace tuning
