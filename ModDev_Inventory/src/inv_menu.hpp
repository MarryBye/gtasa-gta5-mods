#pragma once

namespace invmenu {

void PreScriptsTick();
void Render2D();
bool IsOpen();
void Open(bool fromPhone, int tab = 0); // tab: 0=Main, 1=Snacks, 2=Armor
void Reset();

} // namespace invmenu
