#pragma once

namespace phoneui {

void InstallHooks();
void PreScriptsTick();
void PostScriptsTick();
void Render2D();
bool IsOpen();
void Close();
void ReturnToMain();
void Reset();
void Shutdown();

void OnGameSave(int slot1to8);
void OnGameLoad(int slot1to8);
void OnNewGame();

} // namespace phoneui
