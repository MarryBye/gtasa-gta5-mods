#pragma once
#include <cstdint>

namespace modern_menu {

using FnDrawBackground = void(__thiscall*)(void*);
using FnUserInput      = void(__thiscall*)(void*);

// Returns true if this page/state should bypass the modern overhaul and use vanilla GTA SA
// (e.g., Redefine Controls page 38, ModLoader Mod Configuration page 43, or loading screen).
bool ShouldPassThrough(uint8_t* mm);

// Called from HookedDrawBackground (0x0057C324).
void RenderModernMenu(uint8_t* mm, FnDrawBackground origDrawBg);

// Called from HookedUserInput (0x0057B457).
void HandleModernMenuInput(uint8_t* mm, FnUserInput origUserInput);

// Reset internal transient UI state (hover, scroll, slider drag).
void ResetMenuUiState();

} // namespace modern_menu
