#pragma once

namespace delivery {

void PreScriptsTick();
void Tick();
void Render2D();
bool IsMenuOpen();
bool IsDeliveryInProgress();
void CancelDelivery(bool deleteVehicle = false);
void Reset();

// Phone / External Addon entry points
void OpenMechanicMenu(bool fromPhone);
void OpenInsuranceMenu(bool fromPhone);
bool StartDeliveryForSlot(int slotIdx);

} // namespace delivery
