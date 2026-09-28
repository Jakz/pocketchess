// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Bitboard.h"

#include <memory>

namespace pchess::chess {
namespace {

constexpr int DirFile[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int DirRank[8] = {1, 1, 0, -1, -1, -1, 0, 1};
// Directions whose squares increase (N, NE, E, NW): the nearest blocker is the lowest bit.
constexpr bool DirPositive[8] = {true, true, true, false, false, false, false, true};

Bitboard stepAttacks(Square s, const int (*steps)[2], int count) {
    Bitboard b = 0;
    for (int i = 0; i < count; ++i) {
        int f = fileOf(s) + steps[i][0];
        int r = rankOf(s) + steps[i][1];
        if (f >= 0 && f < 8 && r >= 0 && r < 8) b |= bb(makeSquare(f, r));
    }
    return b;
}

std::unique_ptr<AttackTables> build() {
    auto t = std::make_unique<AttackTables>();
    static const int knightSteps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    static const int kingSteps[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
    static const int wPawn[2][2] = {{-1, 1}, {1, 1}};
    static const int bPawn[2][2] = {{-1, -1}, {1, -1}};

    for (Square s = 0; s < 64; ++s) {
        t->knight[s] = stepAttacks(s, knightSteps, 8);
        t->king[s] = stepAttacks(s, kingSteps, 8);
        t->pawn[White][s] = stepAttacks(s, wPawn, 2);
        t->pawn[Black][s] = stepAttacks(s, bPawn, 2);
        for (int d = 0; d < 8; ++d) {
            Bitboard ray = 0;
            int f = fileOf(s) + DirFile[d];
            int r = rankOf(s) + DirRank[d];
            while (f >= 0 && f < 8 && r >= 0 && r < 8) {
                ray |= bb(makeSquare(f, r));
                f += DirFile[d];
                r += DirRank[d];
            }
            t->ray[d][s] = ray;
        }
    }
    for (Square a = 0; a < 64; ++a) {
        for (Square b = 0; b < 64; ++b) {
            t->between[a][b] = 0;
            t->line[a][b] = 0;
        }
        for (int d = 0; d < 8; ++d) {
            Bitboard ray = t->ray[d][a];
            Bitboard walk = ray;
            while (walk) {
                Square b = popLsb(walk);
                // between = ray from a minus ray from b (same direction) minus b itself
                t->between[a][b] = ray & ~t->ray[d][b] & ~bb(b);
                t->line[a][b] = t->ray[d][a] | t->ray[(d + 4) % 8][a] | bb(a);
            }
        }
    }
    return t;
}

Bitboard slide(Square s, Bitboard occupied, int d) {
    const AttackTables& t = attackTables();
    Bitboard attacks = t.ray[d][s];
    Bitboard blockers = attacks & occupied;
    if (blockers) {
        Square b = DirPositive[d] ? lsb(blockers) : msb(blockers);
        attacks &= ~t.ray[d][b];
    }
    return attacks;
}

} // namespace

const AttackTables& attackTables() {
    static const std::unique_ptr<AttackTables> tables = build();
    return *tables;
}

Bitboard bishopAttacks(Square s, Bitboard occupied) {
    return slide(s, occupied, 1) | slide(s, occupied, 3) | slide(s, occupied, 5) | slide(s, occupied, 7);
}

Bitboard rookAttacks(Square s, Bitboard occupied) {
    return slide(s, occupied, 0) | slide(s, occupied, 2) | slide(s, occupied, 4) | slide(s, occupied, 6);
}

} // namespace pchess::chess
