#pragma once

namespace progression {

void InstallHooks();
void SyncBusState();
void OnWorldEntered(bool loadedSaveThisBoot);
void OnSaveLoaded(int slot1to8);
void OnSaveWritten(int slot1to8);
void OnLeftWorld();

void PreScriptsTick();
void Render2D();

void OpenUnlocksMenu(bool fromPhone);
bool IsUnlocksMenuOpen();
void PulseHudBar();

void Shutdown();

} // namespace progression
