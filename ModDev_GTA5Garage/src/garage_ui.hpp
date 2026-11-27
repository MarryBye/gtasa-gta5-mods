#pragma once
#include <string>
#include "common_utils.hpp"
#include "garage_config.hpp"
#include "mod_bus.hpp"
#include "tuning_physics.hpp"
#include "ui_kit.hpp"

// Domain-specific Garage UI presenters built strictly on Tier 2 UI Kit components!
namespace garage_ui {

inline bool UseMphSetting() {
    if (auto* b = modbus::GetBus(); b && b->uiKitInstalled) return b->useMph;
    return mod::Config().useMph;
}

inline sa::CRGBA GetWearColor(float wearPct) {
    if (wearPct < 15.0f) return ui::ColorSuccess(); // Healthy green
    if (wearPct < 55.0f) return ui::ColorWarning(); // Warning yellow
    return ui::ColorDanger();                       // Heavy / critical red
}

inline void RenderVehicleStatsCard(const std::string& vehicleNameGame,
                                   const tuning::VehicleStats& cur,
                                   const tuning::VehicleStats& preview,
                                   bool anchorBottomRight = false,
                                   const std::string& noteGame = "",
                                   const tuning::VehicleStats* pristine = nullptr)
{
    const bool mph      = UseMphSetting();
    std::string spdUnit = mph ? ui::Loc(" миль/ч", " mph") : ui::Loc(" км/ч", " km/h");
    int curSpd  = mph ? cur.topSpeedMph     : cur.topSpeedKmh;
    int prevSpd = mph ? preview.topSpeedMph : preview.topSpeedKmh;
    int prisSpd = pristine ? (mph ? pristine->topSpeedMph : pristine->topSpeedKmh) : -1;
    int maxSpd  = mph ? 220                 : 350;

    ui::StatsCard card;
    card.SetTitle(ui::Loc("ХАРАКТЕРИСТИКИ", "VEHICLE STATS"), vehicleNameGame)
        .SetNote(noteGame)
        .SetAnchor(anchorBottomRight, 162.0f, 16.0f, anchorBottomRight ? 18.0f : 106.0f)
        .AddBar(ui::Loc("Макс. скорость", "Top Speed"),    curSpd,             prevSpd,                maxSpd, spdUnit, prisSpd)
        .AddBar(ui::Loc("Ускорение",      "Acceleration"), cur.accelRating,    preview.accelRating,    120,    "",      pristine ? pristine->accelRating    : -1, preview.accelSkillBonus,    preview.accelSkillPct)
        .AddBar(ui::Loc("Передачи",       "Gears"),        cur.numGears,       preview.numGears,       5,      "",      -1)
        .AddBar(ui::Loc("Торможение",     "Braking"),      cur.brakeRating,    preview.brakeRating,    120,    "",      pristine ? pristine->brakeRating    : -1, preview.brakeSkillBonus,    preview.brakeSkillPct)
        .AddBar(ui::Loc("Управление",     "Handling"),     cur.handlingRating, preview.handlingRating, 120,    "",      pristine ? pristine->handlingRating : -1, preview.handlingSkillBonus, preview.handlingSkillPct)
        .AddBar(ui::Loc("Броня",          "Armor"),        cur.armorPercent,   preview.armorPercent,   200,    "%",     pristine ? pristine->armorPercent   : -1);
    card.Render();
}

inline void RenderPlateInputModal(const std::string& plateText, int price) {
    ui::TextInputModal modal;
    modal.SetTitle(ui::Loc("НОМЕРНОЙ ЗНАК", "LICENSE PLATE"),
                   common::FormatPrice(price, ui::UseRussianFlag()))
         .SetText(plateText)
         .SetPlaceholder("GROVE ST")
         .SetMaxLength(8)
         .SetHint(ui::Loc("Латиница A-Z, цифры 0-9, пробел", "A-Z, 0-9, Space, Backspace"))
         .SetFooter(ui::Loc("Enter - Применить   ПКМ - Отмена", "Enter - Apply   RMB - Cancel"));
    modal.Render();
}

} // namespace garage_ui
