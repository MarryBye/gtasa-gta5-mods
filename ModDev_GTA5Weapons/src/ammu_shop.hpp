#pragma once

namespace ammu {

// Hooks CMenuSystem::Process (0x0058FCDA) to seamlessly upgrade Ammu-Nation into a GTA 5 Weapon Workshop
void InstallHooks();

// Called before CTheScripts::Process each frame
void PreScriptsTick();

// Called after CTheScripts::Process each frame
void PostScriptsTick();

// Called inside CHud::DrawAfterFade to draw vanilla menus + bottom-right live Weapon Stats Panel
void Render2D();

bool IsShopActive();
bool ShouldClaimAmmuPanel();

void Reset();

} // namespace ammu
