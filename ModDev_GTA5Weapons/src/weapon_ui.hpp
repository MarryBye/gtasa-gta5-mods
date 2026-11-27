#pragma once
#include <algorithm>
#include <string>
#include "ui_kit.hpp"
#include "weapon_data.hpp"

// Domain-specific Weapons UI presenter built strictly on Tier 2 UI Kit components!
namespace wui {

inline void RenderWeaponStatsCard(const wdata::WeaponDef* def,
                                  const wdata::WeaponStats& cur,
                                  const wdata::WeaponStats& preview,
                                  const std::string& noteGame = "",
                                  float width640 = 162.0f,
                                  float rightMargin640 = 18.0f,
                                  float bottomMargin448 = 18.0f)
{
    if (!def || def->category == wdata::WeaponCategory::Melee) return;

    const int curClipTotal  = cur.clipSize     + cur.clipSkillBonus;
    const int prevClipTotal = preview.clipSize + preview.clipSkillBonus;
    const int clipMax       = std::max({30, curClipTotal, prevClipTotal});
    const std::string mUnit = ui::Loc("м", "m");

    ui::StatsCard card;
    card.SetTitle(ui::Loc("ХАРАКТЕРИСТИКИ", "STATS"), ui::Loc(def->nameRus, def->nameEng))
        .SetAnchor(true, width640, rightMargin640, bottomMargin448)
        .SetNote(noteGame)
        .AddBar(ui::Loc("Урон",      "Damage"),   cur.damage,   preview.damage,   120,     "",    -1, preview.damageSkillBonus,   preview.damageSkillPct)
        .AddBar(ui::Loc("Темп",      "FireRate"), cur.fireRate, preview.fireRate, 120,     "",    -1, preview.fireRateSkillBonus, preview.fireRateSkillPct)
        .AddBar(ui::Loc("Точность",  "Accuracy"), cur.accuracy, preview.accuracy, 120,     "",    -1, preview.accuracySkillBonus, preview.accuracySkillPct)
        .AddBar(ui::Loc("Дальность", "Range"),    cur.range,    preview.range,    140,     mUnit, -1, preview.rangeSkillBonus,    preview.rangeSkillPct)
        .AddBar(ui::Loc("Обойма",    "Clip"),     cur.clipSize, preview.clipSize, clipMax, "",    -1, preview.clipSkillBonus,     preview.clipSkillPct);
    card.Render();
}

} // namespace wui
