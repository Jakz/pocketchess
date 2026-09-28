// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/MoveGen.h"

#include "core/chess/Position.h"

namespace pchess::chess {
namespace {

void addPromotions(MoveList& out, Square from, Square to, bool capture) {
    out.push(Move(from, to, promoFlag(Queen, capture)));
    out.push(Move(from, to, promoFlag(Rook, capture)));
    out.push(Move(from, to, promoFlag(Bishop, capture)));
    out.push(Move(from, to, promoFlag(Knight, capture)));
}

// Slider attackers of `s` by `by` given occupancy `occ`, ignoring pieces in `removed`.
bool sliderAttacked(const Position& pos, Square s, Color by, Bitboard occ, Bitboard removed) {
    const Bitboard rq = (pos.pieces(by, Rook) | pos.pieces(by, Queen)) & ~removed;
    const Bitboard bq = (pos.pieces(by, Bishop) | pos.pieces(by, Queen)) & ~removed;
    return (rookAttacks(s, occ) & rq) || (bishopAttacks(s, occ) & bq);
}

} // namespace

void generateLegalMoves(const Position& pos, MoveList& out) {
    out.clear();
    const Color us = pos.sideToMove();
    const Color them = ~us;
    const Bitboard own = pos.pieces(us);
    const Bitboard enemy = pos.pieces(them);
    const Bitboard occ = pos.occupied();
    const Square ksq = pos.kingSquare(us);
    if (ksq == NoSquare) return;

    const Bitboard checkers = pos.attackersTo(ksq, occ) & enemy;

    // King moves: the destination must not be attacked with the king removed from the board
    // (so it cannot step back along a checking ray).
    {
        const Bitboard occNoKing = occ & ~bb(ksq);
        Bitboard targets = kingAttacks(ksq) & ~own;
        while (targets) {
            const Square to = popLsb(targets);
            if (pos.attackersTo(to, occNoKing) & enemy) continue;
            out.push(Move(ksq, to, (enemy & bb(to)) ? Move::Capture : Move::Quiet));
        }
    }
    if (moreThanOne(checkers)) return; // double check: only king moves

    // Squares a non-king move may land on: anywhere, or block/capture the single checker.
    Bitboard checkMask = ~Bitboard{0};
    if (checkers) {
        const Square c = lsb(checkers);
        checkMask = between(ksq, c) | checkers;
    }

    // Pinned pieces.
    Bitboard pinned = 0;
    {
        Bitboard snipers = (rookAttacks(ksq, 0) & (pos.pieces(them, Rook) | pos.pieces(them, Queen))) |
                           (bishopAttacks(ksq, 0) & (pos.pieces(them, Bishop) | pos.pieces(them, Queen)));
        while (snipers) {
            const Square s = popLsb(snipers);
            const Bitboard blockers = between(ksq, s) & occ;
            if (blockers && !moreThanOne(blockers) && (blockers & own)) pinned |= blockers;
        }
    }
    auto pinFilter = [&](Square from, Bitboard targets) {
        return (pinned & bb(from)) ? (targets & lineThrough(ksq, from)) : targets;
    };

    // Pawns.
    const int up = us == White ? 8 : -8;
    const Bitboard promoRank = us == White ? Rank8 : Rank1;
    const Bitboard doubleRank = us == White ? Rank4 : Rank5; // destination rank of a double push
    Bitboard pawns = pos.pieces(us, Pawn);
    while (pawns) {
        const Square from = popLsb(pawns);
        // Pushes.
        const Square one = from + up;
        if (isValidSquare(one) && !(occ & bb(one))) {
            Bitboard t = pinFilter(from, bb(one)) & checkMask;
            if (t) {
                if (bb(one) & promoRank) addPromotions(out, from, one, false);
                else out.push(Move(from, one, Move::Quiet));
            }
            const Square two = one + up;
            if (isValidSquare(two) && (bb(two) & doubleRank) && !(occ & bb(two))) {
                if (pinFilter(from, bb(two)) & checkMask) out.push(Move(from, two, Move::DoublePush));
            }
        }
        // Captures.
        Bitboard caps = pinFilter(from, pawnAttacks(us, from) & enemy) & checkMask;
        while (caps) {
            const Square to = popLsb(caps);
            if (bb(to) & promoRank) addPromotions(out, from, to, true);
            else out.push(Move(from, to, Move::Capture));
        }
        // En passant: rare, so verify with a full occupancy test (handles the horizontal pin
        // and a double-pushed checker).
        const Square ep = pos.epSquare();
        if (ep != NoSquare && (pawnAttacks(us, from) & bb(ep))) {
            const Square capSq = ep - up;
            const Bitboard occAfter = (occ & ~bb(from) & ~bb(capSq)) | bb(ep);
            const Bitboard removed = bb(capSq);
            const bool leavesCheck =
                sliderAttacked(pos, ksq, them, occAfter, removed) ||
                (knightAttacks(ksq) & pos.pieces(them, Knight)) ||
                (pawnAttacks(us, ksq) & pos.pieces(them, Pawn) & ~removed);
            if (!leavesCheck) out.push(Move(from, ep, Move::EnPassant));
        }
    }

    // Knights, bishops, rooks, queens.
    for (PieceType t : {Knight, Bishop, Rook, Queen}) {
        Bitboard b = pos.pieces(us, t);
        if (t == Knight) b &= ~pinned; // a pinned knight can never move
        while (b) {
            const Square from = popLsb(b);
            Bitboard targets;
            switch (t) {
            case Knight: targets = knightAttacks(from); break;
            case Bishop: targets = bishopAttacks(from, occ); break;
            case Rook: targets = rookAttacks(from, occ); break;
            default: targets = queenAttacks(from, occ); break;
            }
            targets = pinFilter(from, targets & ~own) & checkMask;
            while (targets) {
                const Square to = popLsb(targets);
                out.push(Move(from, to, (enemy & bb(to)) ? Move::Capture : Move::Quiet));
            }
        }
    }

    // Castling (standard chess only).
    if (!checkers) {
        const uint8_t rights = pos.castlingRights();
        const int rank = us == White ? 0 : 7;
        const uint8_t oo = us == White ? WhiteOO : BlackOO;
        const uint8_t ooo = us == White ? WhiteOOO : BlackOOO;
        const Square e = makeSquare(4, rank);
        if (ksq == e) {
            if ((rights & oo) && !(occ & (bb(makeSquare(5, rank)) | bb(makeSquare(6, rank)))) &&
                !pos.isAttacked(makeSquare(5, rank), them) && !pos.isAttacked(makeSquare(6, rank), them))
                out.push(Move(e, makeSquare(6, rank), Move::CastleKing));
            if ((rights & ooo) &&
                !(occ & (bb(makeSquare(1, rank)) | bb(makeSquare(2, rank)) | bb(makeSquare(3, rank)))) &&
                !pos.isAttacked(makeSquare(3, rank), them) && !pos.isAttacked(makeSquare(2, rank), them))
                out.push(Move(e, makeSquare(2, rank), Move::CastleQueen));
        }
    }
}

uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) return 1;
    MoveList list;
    generateLegalMoves(pos, list);
    if (depth == 1) return list.size();
    uint64_t nodes = 0;
    for (Move m : list) {
        const Undo u = pos.makeMove(m);
        nodes += perft(pos, depth - 1);
        pos.unmakeMove(m, u);
    }
    return nodes;
}

} // namespace pchess::chess
