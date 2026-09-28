// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Types.h"

#include <bit>
#include <cstdint>

namespace pchess::chess {

// Always uint64_t (never `long`), so the code behaves the same on 32-bit ARM.
using Bitboard = uint64_t;

constexpr Bitboard bb(Square s) { return Bitboard{1} << s; }

constexpr Bitboard FileA = 0x0101010101010101ULL;
constexpr Bitboard FileH = FileA << 7;
constexpr Bitboard Rank1 = 0xFFULL;
constexpr Bitboard Rank2 = Rank1 << 8;
constexpr Bitboard Rank4 = Rank1 << 24;
constexpr Bitboard Rank5 = Rank1 << 32;
constexpr Bitboard Rank7 = Rank1 << 48;
constexpr Bitboard Rank8 = Rank1 << 56;
constexpr Bitboard LightSquares = 0x55AA55AA55AA55AAULL;
constexpr Bitboard DarkSquares = ~LightSquares;

constexpr Bitboard fileBB(int file) { return FileA << file; }
constexpr Bitboard rankBB(int rank) { return Rank1 << (8 * rank); }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) { return static_cast<Square>(std::countr_zero(b)); }
inline Square msb(Bitboard b) { return static_cast<Square>(63 - std::countl_zero(b)); }
inline Square popLsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
constexpr bool moreThanOne(Bitboard b) { return (b & (b - 1)) != 0; }

// Attack tables, built once on first use (thread-safe static init).
struct AttackTables {
    Bitboard knight[64];
    Bitboard king[64];
    Bitboard pawn[2][64];
    Bitboard ray[8][64];      // directions: N, NE, E, SE, S, SW, W, NW
    Bitboard between[64][64]; // squares strictly between two aligned squares, else 0
    Bitboard line[64][64];    // full line through two aligned squares, else 0
};

const AttackTables& attackTables();

Bitboard bishopAttacks(Square s, Bitboard occupied);
Bitboard rookAttacks(Square s, Bitboard occupied);
inline Bitboard queenAttacks(Square s, Bitboard occupied) { return bishopAttacks(s, occupied) | rookAttacks(s, occupied); }
inline Bitboard knightAttacks(Square s) { return attackTables().knight[s]; }
inline Bitboard kingAttacks(Square s) { return attackTables().king[s]; }
inline Bitboard pawnAttacks(Color c, Square s) { return attackTables().pawn[c][s]; }
inline Bitboard between(Square a, Square b) { return attackTables().between[a][b]; }
inline Bitboard lineThrough(Square a, Square b) { return attackTables().line[a][b]; }

} // namespace pchess::chess
