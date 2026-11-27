#pragma once

namespace foodshop {

void InstallHooks();
void PreScriptsTick();
void Render2D();
bool IsShopActive();
bool ShouldClaimFoodPanel();
void Reset();

} // namespace foodshop
