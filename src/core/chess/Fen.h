// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/util/Error.h"

#include <string>
#include <string_view>

namespace pchess::chess {

inline constexpr std::string_view StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Strict parser: board, side, castling, ep; the two counters are optional (default "0 1").
// The result is normalized (impossible rights/ep dropped) and validated (kings, pawns, check).
Result<Position> parseFen(std::string_view fen);

std::string toFen(const Position& pos);

} // namespace pchess::chess
