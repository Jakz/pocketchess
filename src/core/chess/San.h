// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/util/Error.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pchess::chess {

// Standard Algebraic Notation for a legal move, with minimal disambiguation and +/# suffix.
std::string toSan(const Position& pos, Move m);

// Tolerant SAN parser: accepts 0-0/O-O, e8Q/e8=Q/e8=q, 'x' or ':', trailing +#!? and "e.p.",
// and long algebraic / UCI forms (e2-e4, Ng1-f3, e2e4) as a fallback.
Result<Move> parseSan(const Position& pos, std::string_view san);

// SAN for a sequence starting at `pos` (used for PVs and move lists).
std::vector<std::string> toSanLine(Position pos, std::span<const Move> moves);

// UCI long algebraic ("e2e4", "e7e8q").
std::string toUci(Move m);
Result<Move> parseUci(const Position& pos, std::string_view uci);

} // namespace pchess::chess
