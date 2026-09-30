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
    std::string pieceSet = "Cburnett"; // assets/pieces/<set>; "DejaVu" = the built-in font glyphs
    bool mono = false;                 // Color / Monochrome
    bool coordinates = true;
    int fullRefreshEvery = 12;         // full refresh interval (content updates)
    // Input
    bool tapToMove = true;
    bool dragToMove = true;
    bool showLegalMoves = true;
    bool swapPageKeys = false;         // the reader's page buttons: Prev <-> Next (left hand, flipped cover)
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
    // Tactics (M4, design notes §20)
    std::string tacticsPack;           // pack name (content/puzzles/<name>.db); empty = the largest installed
    std::string gamesPack;             // content/games/<name>.db; empty = the largest installed
    std::string tacticsTheme;          // puzzles must have this theme; empty = any
    std::string tacticsSkipTheme;      // puzzles with this theme are skipped; empty = none
    int tacticsDifficulty = 0;         // offset added to the rating when choosing puzzles: -200, 0, +200
    bool tacticsAutoNext = false;      // next puzzle 1.5 s after a solve
    bool tacticsAutoOpponent = true;   // the opponent's move plays by itself (else: tap Next)
    // Advanced
    int clockOffsetDays = 0;           // shifts "now" for reviews (device test of the schedule)
    bool debugLogging = false;         // Debug-level lines in logs/pocketchess.log

    // key -> value text. Unknown keys and invalid values are ignored by fromMap (defaults stay),
    // so older and newer databases both load.
    std::map<std::string, std::string> toMap() const;
    void fromMap(const std::map<std::string, std::string>& values);
};

} // namespace pchess::app
