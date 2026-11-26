#pragma once

namespace phoneui {

void PreScriptsTick();
void PostScriptsTick();
void Render2D();
bool IsOpen();
void Close();
void ReturnToMain();
void Reset();

// Save/Load persisted cooldowns (e.g. Old Acquaintance 10-minute cooldown!)
void OnGameSave(int slot1to8);
void OnGameLoad(int slot1to8);
void OnNewGame();

} // namespace phoneui
