#pragma once

namespace wheel {

// Called before CTheScripts::Process each frame (handles TAB hold, mouse sector select, weapon cycling, slow-mo)
void PreScriptsTick();

// Called after CTheScripts::Process each frame (syncs player inventory cleanly)
void PostScriptsTick();

// Called inside CHud::DrawAfterFade to render GTA SA-styled GTA 5 Weapon Wheel + Bottom-Right Weapon Stats
void Render2D();

bool IsWheelOpen();

void Reset();

} // namespace wheel
