#pragma once
#include <cstdint>
#include "sa_sdk.hpp"
#include "weapon_data.hpp"

namespace wmods {

// Snapshots stock CWeaponInfo table once after weapon.dat is loaded
void EnsureStockCaptured();

// Computes GTA 5-style numerical + bar stats for UI preview & live stats panel
wdata::WeaponStats ComputeStats(uint32_t weaponId, const wdata::WeaponLoadout& loadout);

// Applies attachments to CJ's currently held weapon (restoring previous weapon to stock)
void Tick();

// Force-reapply immediately after installing/changing a mod in Ammu-Nation
void ForceRefresh(bool topUpClipIfLarger = false);

// Restores all CWeaponInfo entries to clean stock (on exit / reset)
void RestoreAllStock();

// Returns true if player is currently in ADS scope zoom
bool IsScopeZoomActive();

} // namespace wmods
