// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Color.h"

#include <string>
#include <vector>

namespace pchess::ui {

// How a marker is drawn. Color is never the only carrier of information (design notes §39.9):
// every marker has a shape/pattern, and color themes add a tint on top.
enum class MarkerStyle { Tint, Outline, Hatch, Dots, DoubleBorder };

struct BoardTheme {
    std::string name;
    bool mono = false;

    Rgb light;
    Rgb dark;
    Rgb selected;      // tint (color) / stroke color (mono)
    Rgb lastMove;
    Rgb check;
    Rgb marker;        // legal-move dots and rings
    Rgb coordOnLight;
    Rgb coordOnDark;
    Rgb background;    // UI background around the board
    Rgb ink;           // UI text
    Rgb frame;         // UI lines/buttons

    MarkerStyle selectedStyle = MarkerStyle::Outline;
    MarkerStyle lastMoveStyle = MarkerStyle::Dots;
    MarkerStyle checkStyle = MarkerStyle::DoubleBorder;
    bool tintMarkers = true;  // color themes: also tint selected/last-move/check squares
    bool coordinates = true;

    // Built-ins (design notes §19; M6 adds YAML-loaded themes).
    static BoardTheme paper();
    static BoardTheme slate();
    static BoardTheme classicMono();
    static BoardTheme highContrastMono();
    static std::vector<BoardTheme> builtIns();
    static BoardTheme byName(const std::string& name);  // falls back to paper()
};

} // namespace pchess::ui
