#pragma once

namespace clothesshop {

void InstallHooks();
void PreScriptsTick();
void PostScriptsTick();
void Render2D();
bool IsShopActive();
bool ShouldClaimShopPanel();
void Reset();

} // namespace clothesshop
