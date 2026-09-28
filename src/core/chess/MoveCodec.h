// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/util/Error.h"

#include <cstdint>
#include <span>
#include <vector>

namespace pchess::chess {

// Database move blobs (puzzles, games): each move is its 16-bit in-memory value, little-endian.
// Decoding is a copy plus a legality check against the replayed position (architecture doc §7).
std::vector<uint8_t> encodeMoves(std::span<const Move> moves);
std::vector<Move> decodeMovesUnchecked(std::span<const uint8_t> blob);
Result<std::vector<Move>> decodeMoves(const Position& start, std::span<const uint8_t> blob);

} // namespace pchess::chess
