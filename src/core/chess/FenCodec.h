// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"

#include <string>
#include <string_view>

namespace pchess::chess {

// Compact FEN for content packs (puzzles v2): 8 bytes of occupancy (a8 = bit 0, rank by rank as
// FEN lists them), one nibble per occupied square (PNBRQK = 0..5, black + 6), then side and
// castling rights (1 byte), en passant file + 1 (1 byte, 0 = none), halfmove clock (1 byte) and
// fullmove number (2 bytes, little-endian). A middlegame position takes ~23 bytes instead of ~55.
// Standard castling only (KQkq); the FEN must already be valid (packFen does not validate).
Result<std::string> packFen(std::string_view fen);
Result<std::string> unpackFen(std::string_view packed);

} // namespace pchess::chess
