#pragma once
#include <cstdint>
#include "sa_sdk.hpp"

namespace workshop {

// Install hook on CMenuSystem::Process call inside CHud::Draw (0x0058FCDB)
void InstallHooks();

// Called right BEFORE CTheScripts::Process() every frame (manages input suppression for Mode A & garage triggers for Mode B)
void PreScriptsTick();

// Called after scripts each frame (camera orbit, live preview, Mode B entry/exit)
void PostScriptsTick();

// Called inside CHud::DrawAfterFade hook to draw our vanilla workshop UI cleanly on top
void Render2D();

bool          IsWorkshopActive();
bool          IsCustomWorkshopMenuDrawing();
bool          ShouldClaimWorkshopPanel();
sa::CVehicle* GetWorkshopVehicle();

void Reset();

} // namespace workshop
