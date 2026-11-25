#pragma once

namespace delivery {

// Per-frame tick: F6 hotkey, menu navigation, mechanic NPC driving AI & arrival
void Tick();

// Render F6 vanilla personal garage menu (2 pages x 5 slots = 10 vehicles)
void Render2D();

bool IsMenuOpen();
bool IsDeliveryInProgress();
void CancelDelivery(bool deleteVehicle);
void Reset();

} // namespace delivery
