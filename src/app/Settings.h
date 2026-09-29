// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/PowerProfile.h"

#include <string>

namespace pchess::app {

// Settings v0 (in memory; persisted in chess.db from M3, design notes §20).
struct Settings {
    std::string boardTheme = "Paper";  // used in color mode
    std::string monoTheme = "ClassicMono";
    bool mono = false;                 // Display -> Color / Monochrome
    bool coordinates = true;
    int fullRefreshEvery = 12;         // Display -> Full refresh interval (content updates)
    bool tapToMove = true;
    bool dragToMove = true;
    bool showLegalMoves = true;
    // Engine / Power (M2; Settings screen and persistence in M3).
    engine::PowerProfileId powerProfile = engine::PowerProfileId::Balanced;
    std::string analysisEngine;        // engine id; empty = the profile's default
    std::string playEngine;
    int analysisMultiPv = 1;           // 1..3
    bool evalFromWhite = true;         // show evaluations from White's point of view
};

} // namespace pchess::app
