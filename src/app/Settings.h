// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/PowerProfile.h"

#include <map>
#include <string>

namespace pchess::app {

// Settings v1 (design notes §20), persisted in user/chess.db `settings(key, value)` (M3).
struct Settings {
    // Display
    std::string boardTheme = "Paper";  // used in color mode
    std::string monoTheme = "ClassicMono";
    bool mono = false;                 // Color / Monochrome
    bool coordinates = true;
    int fullRefreshEvery = 12;         // full refresh interval (content updates)
    // Input
    bool tapToMove = true;
    bool dragToMove = true;
    bool showLegalMoves = true;
    // Engine / Power
    engine::PowerProfileId powerProfile = engine::PowerProfileId::Balanced;
    std::string analysisEngine;        // engine id; empty = the profile's default
    std::string playEngine;
    std::string playStrength = "casual";  // default strength label for new games
    int threads = 0;                   // 0 = the profile's value
    int hashMb = 0;                    // 0 = the profile's value
    int deepLimitSeconds = 0;          // Deep analysis auto-stop: 0 = profile, -1 = until stopped
    int analysisMultiPv = 1;           // 1..3
    bool evalFromWhite = true;         // show evaluations from White's point of view

    // key -> value text. Unknown keys and invalid values are ignored by fromMap (defaults stay),
    // so older and newer databases both load.
    std::map<std::string, std::string> toMap() const;
    void fromMap(const std::map<std::string, std::string>& values);
};

} // namespace pchess::app
