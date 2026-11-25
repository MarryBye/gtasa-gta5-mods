#pragma once
#include <cstdint>
#include "weapon_data.hpp"

namespace wvis {

// Installs RenderEffects hook (0x0053E1E0 -> CWeaponEffects::Render) for 3D attachments + laser beam
void InstallHooks();

// Live preview override when browsing attachments inside Ammu-Nation
void SetPreviewOverride(bool active, uint32_t weaponId, const wdata::WeaponLoadout& previewLoadout);

} // namespace wvis
