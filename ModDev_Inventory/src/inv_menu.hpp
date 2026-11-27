#pragma once

namespace invmenu {

void Open(bool fromPhone, int tab = 0);
void PreScriptsTick();
void Render2D();
bool IsOpen();
void Reset();
void Shutdown();

} // namespace invmenu
