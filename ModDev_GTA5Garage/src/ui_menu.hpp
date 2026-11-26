#pragma once
#include <cstdio>
#include <string>
#include "config.hpp"
#include "mod_bus.hpp"
#include "tuning_physics.hpp"
#include "ui_kit.hpp"

// Thin vehicle-specific adapter over the shared ModDev_Common UI Kit!
namespace ui {

inline bool UseMphSetting() {
    if (auto* b = modbus::GetBus(); b && b->uiKitInstalled) return b->useMph;
    return mod::Config().useMph;
}

inline void DrawVehicleStatsPanel(const std::string& vehicleNameGame,
                                  const tuning::VehicleStats& cur,
                                  const tuning::VehicleStats& preview,
                                  bool anchorBottomRight = false,
                                  const std::string& noteGame = "",
                                  const tuning::VehicleStats* pristine = nullptr)
{
    const bool mph = UseMphSetting();
    std::string spdUnit = mph ? " mph" : Loc(" км/ч", " km/h");
    int curSpd  = mph ? cur.topSpeedMph     : cur.topSpeedKmh;
    int prevSpd = mph ? preview.topSpeedMph : preview.topSpeedKmh;
    int prisSpd = pristine ? (mph ? pristine->topSpeedMph : pristine->topSpeedKmh) : -1;
    int maxSpd  = mph ? 220                 : 350;

    StatsPanelSpec spec;
    spec.titleGame         = Loc("ХАРАКТЕРИСТИКИ", "VEHICLE STATS");
    spec.subtitleGame      = vehicleNameGame;
    spec.footerNoteGame    = noteGame;
    spec.anchorBottomRight = anchorBottomRight;
    spec.w640              = 152.0f;
    spec.rightMargin640    = 16.0f;
    spec.yTop448           = 106.0f;
    spec.bottomMargin448   = 18.0f;
    spec.rows = {
        {Loc("Макс. скорость", "Top Speed"),    curSpd,             prevSpd,                maxSpd, spdUnit, prisSpd},
        {Loc("Ускорение",      "Acceleration"), cur.accelRating,    preview.accelRating,    120,    "",      pristine ? pristine->accelRating    : -1},
        {Loc("Передачи",       "Gears"),        cur.numGears,       preview.numGears,       5,      "",      -1},
        {Loc("Торможение",     "Braking"),      cur.brakeRating,    preview.brakeRating,    120,    "",      pristine ? pristine->brakeRating    : -1},
        {Loc("Управление",     "Handling"),     cur.handlingRating, preview.handlingRating, 120,    "",      pristine ? pristine->handlingRating : -1},
        {Loc("Кузов (Броня)",  "Armor"),        cur.armorPercent,   preview.armorPercent,   200,    "%",     pristine ? pristine->armorPercent   : -1},
    };
    DrawStatsPanel(spec);
}

inline sa::CRGBA GetWearColor(float wearPct) {
    if (wearPct < 15.0f) return {115, 230, 125, 255}; // Healthy green
    if (wearPct < 45.0f) return {235, 215, 95,  255}; // Warning yellow
    if (wearPct < 75.0f) return {245, 145, 70,  255}; // Heavy orange
    return {235, 75, 75, 255};                        // Critical red
}

inline void DrawPlateInputBox(const std::string& plateText, int price) {
    TextInputBoxSpec spec;
    spec.titleGame       = Loc("НОМЕРНОЙ ЗНАК", "LICENSE PLATE");
    spec.badgeGame       = FormatPrice(price);
    spec.text            = plateText;
    spec.placeholderGame = "GROVE ST";
    spec.maxLen          = 8;
    spec.line1HintGame   = Loc("Enter: Применить  -  Esc: Отмена", "Enter: Apply  -  Esc: Cancel");
    spec.line2HintGame   = Loc("Латиница A-Z, цифры 0-9, пробел", "A-Z, 0-9, Space, Backspace");
    spec.plateStyle      = false;
    DrawTextInputBox(spec);
}

} // namespace ui
