#pragma once
#include <cstdio>
#include <string>
#include "config.hpp"
#include "tuning_physics.hpp"
#include "ui_kit.hpp"

// Thin vehicle-specific adapter over the shared ModDev_Common UI Kit!
namespace ui {

inline void DrawVehicleStatsPanel(const std::string& vehicleNameGame,
                                  const tuning::VehicleStats& cur,
                                  const tuning::VehicleStats& preview,
                                  bool anchorBottomRight = false,
                                  const std::string& noteGame = "")
{
    const bool mph = mod::Config().useMph;
    std::string spdUnit = mph ? " mph" : Loc(" км/ч", " km/h");
    int curSpd  = mph ? cur.topSpeedMph     : cur.topSpeedKmh;
    int prevSpd = mph ? preview.topSpeedMph : preview.topSpeedKmh;
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
        {Loc("Макс. скорость", "Top Speed"),    curSpd,             prevSpd,                maxSpd, spdUnit},
        {Loc("Ускорение",      "Acceleration"), cur.accelRating,    preview.accelRating,    120,    ""},
        {Loc("Торможение",     "Braking"),      cur.brakeRating,    preview.brakeRating,    120,    ""},
        {Loc("Управление",     "Handling"),     cur.handlingRating, preview.handlingRating, 120,    ""},
        {Loc("Кузов (Броня)",  "Armor"),        cur.armorPercent,   preview.armorPercent,   200,    "%"},
    };
    DrawStatsPanel(spec);
}

inline void DrawPlateInputBox(const std::string& plateText, int price) {
    char hintBuf[128];
    std::snprintf(hintBuf, sizeof(hintBuf), "%s ($%d) | Esc - %s",
                  Loc("Enter - Установить", "Enter - Apply").c_str(),
                  price,
                  Loc("Отмена", "Cancel").c_str());

    TextInputBoxSpec spec;
    spec.titleGame     = Loc("ВВЕДИТЕ НОМЕР (ДО 8 СИМВ.)", "ENTER PLATE (MAX 8 CHARS)");
    spec.text          = plateText;
    spec.maxLen        = 8;
    spec.line1HintGame = hintBuf;
    spec.line2HintGame = Loc("A-Z, 0-9, Пробел, Backspace", "A-Z, 0-9, Space, Backspace");
    spec.plateStyle    = true;
    DrawTextInputBox(spec);
}

} // namespace ui
