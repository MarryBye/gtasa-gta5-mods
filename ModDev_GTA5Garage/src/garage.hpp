#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "sa_sdk.hpp"
#include "vehicle_data.hpp"

namespace garage {

// Active save slot (1..8 for GTASAsf1..8, 0 for autosave/NewGame/SA-COOP client)
int  GetActiveSaveSlot();
void OnGameSave(int slot1to8);
void OnGameLoad(int slot1to8);
void OnNewGame();
void SaveNow();

// Access all 10 garage slots
const std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& Slots();
std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& SlotsMutable();

int FindFreeSlot();
int FindSlotForLiveVehicle(const sa::CVehicle* veh);
int CountOccupied();

// Register current vehicle into a free slot (returns slot index 0..9, or -1 if full)
int  RegisterVehicle(sa::CVehicle* veh);
bool UnregisterSlot(int slotIdx);

// Capture current vehicle's visual mods, colors, paintjob, and GTA 5 tuning into slot
void SnapshotVehicleToSlot(sa::CVehicle* veh, int slotIdx);

// Apply all saved visuals (colors, paintjob, 15 vanilla mods) + GTA 5 tuning to a spawned vehicle
void ApplySavedVehicleConfig(sa::CVehicle* veh, const mod::OwnedVehicle& data);

// Live personal vehicle tracking (active spawned vehicle + radar blip management)
sa::CVehicle* GetActivePersonalVehicle();
int           GetActivePersonalSlot();
int32_t       GetDeliveryBlip();

void SetActivePersonalVehicle(sa::CVehicle* veh, int slotIdx, int32_t blipHandle);
void ClearDeliveryBlip();
void DismissCurrentPersonalVehicle(bool deleteEntity);

// Per-frame tick: monitors destruction, player entry (clears blip), and live sync
void Tick();

} // namespace garage
