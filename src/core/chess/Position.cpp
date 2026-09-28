// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Position.h"

#include "core/chess/MoveGen.h"
#include "core/chess/Zobrist.h"

namespace pchess::chess {
namespace {

// Rights that survive a move touching this square.
constexpr std::array<uint8_t, 64> makeCastleMask() {
    std::array<uint8_t, 64> m{};
    for (auto& v : m) v = AllCastling;
    m[E1] = static_cast<uint8_t>(AllCastling & ~(WhiteOO | WhiteOOO));
    m[H1] = static_cast<uint8_t>(AllCastling & ~WhiteOO);
    m[A1] = static_cast<uint8_t>(AllCastling & ~WhiteOOO);
    m[E8] = static_cast<uint8_t>(AllCastling & ~(BlackOO | BlackOOO));
    m[H8] = static_cast<uint8_t>(AllCastling & ~BlackOO);
    m[A8] = static_cast<uint8_t>(AllCastling & ~BlackOOO);
    return m;
}
constexpr std::array<uint8_t, 64> CastleMask = makeCastleMask();

bool epCapturable(const Position& p, Square ep, Color capturer) {
    return (pawnAttacks(~capturer, ep) & p.pieces(capturer, Pawn)) != 0;
}

} // namespace

Position::Position() { clear(); }

void Position::clear() {
    pieces_.fill(0);
    byColor_.fill(0);
    board_.fill(NoPiece);
    stm_ = White;
    castling_ = NoCastling;
    ep_ = NoSquare;
    halfmove_ = 0;
    fullmove_ = 1;
    key_ = computeKey();
}

Position Position::startPosition() {
    Position p;
    static const Piece back[8] = {WRook, WKnight, WBishop, WQueen, WKing, WBishop, WKnight, WRook};
    for (int f = 0; f < 8; ++f) {
        p.addPiece(back[f], makeSquare(f, 0));
        p.addPiece(WPawn, makeSquare(f, 1));
        p.addPiece(BPawn, makeSquare(f, 6));
        p.addPiece(static_cast<Piece>(back[f] + 6), makeSquare(f, 7));
    }
    p.castling_ = AllCastling;
    p.key_ = p.computeKey();
    return p;
}

void Position::addPiece(Piece p, Square s) {
    const Bitboard b = bb(s);
    pieces_[p] |= b;
    byColor_[colorOf(p)] |= b;
    board_[s] = p;
    key_ ^= zobrist().piece[p][s];
}

void Position::removePiece(Square s) {
    const Piece p = board_[s];
    const Bitboard b = bb(s);
    pieces_[p] &= ~b;
    byColor_[colorOf(p)] &= ~b;
    board_[s] = NoPiece;
    key_ ^= zobrist().piece[p][s];
}

void Position::movePiece(Square from, Square to) {
    const Piece p = board_[from];
    const Bitboard fromTo = bb(from) | bb(to);
    pieces_[p] ^= fromTo;
    byColor_[colorOf(p)] ^= fromTo;
    board_[from] = NoPiece;
    board_[to] = p;
    key_ ^= zobrist().piece[p][from] ^ zobrist().piece[p][to];
}

void Position::put(Piece p, Square s) {
    if (board_[s] != NoPiece) removePiece(s);
    if (p != NoPiece) addPiece(p, s);
}

void Position::removeAt(Square s) {
    if (board_[s] != NoPiece) removePiece(s);
}

uint64_t Position::computeKey() const {
    const ZobristKeys& z = zobrist();
    uint64_t k = 0;
    for (Square s = 0; s < 64; ++s)
        if (board_[s] != NoPiece) k ^= z.piece[board_[s]][s];
    k ^= z.castling[castling_];
    if (ep_ != NoSquare) k ^= z.epFile[fileOf(ep_)];
    if (stm_ == Black) k ^= z.side;
    return k;
}

void Position::normalize() {
    if (board_[E1] != WKing || board_[H1] != WRook) castling_ = static_cast<uint8_t>(castling_ & ~WhiteOO);
    if (board_[E1] != WKing || board_[A1] != WRook) castling_ = static_cast<uint8_t>(castling_ & ~WhiteOOO);
    if (board_[E8] != BKing || board_[H8] != BRook) castling_ = static_cast<uint8_t>(castling_ & ~BlackOO);
    if (board_[E8] != BKing || board_[A8] != BRook) castling_ = static_cast<uint8_t>(castling_ & ~BlackOOO);
    if (ep_ != NoSquare) {
        // Valid only on rank 6 (white to move) / rank 3 (black to move), behind an enemy pawn
        // that just double-pushed, with an empty square and a pawn able to capture.
        const Color them = ~stm_;
        const int expectedRank = stm_ == White ? 5 : 2;
        const Square pawnSq = stm_ == White ? ep_ - 8 : ep_ + 8;
        const Square origin = stm_ == White ? ep_ + 8 : ep_ - 8;
        if (rankOf(ep_) != expectedRank || board_[pawnSq] != makePiece(them, Pawn) || board_[ep_] != NoPiece ||
            board_[origin] != NoPiece || !epCapturable(*this, ep_, stm_))
            ep_ = NoSquare;
    }
    if (halfmove_ < 0) halfmove_ = 0;
    if (fullmove_ < 1) fullmove_ = 1;
    key_ = computeKey();
}

std::string Position::validate() const {
    if (popcount(pieces(White, King)) != 1) return "White must have exactly one king";
    if (popcount(pieces(Black, King)) != 1) return "Black must have exactly one king";
    if ((pieces(WPawn) | pieces(BPawn)) & (Rank1 | Rank8)) return "Pawns cannot stand on the first or last rank";
    if (popcount(pieces(White)) > 16 || popcount(pieces(Black)) > 16) return "Too many pieces";
    if (isAttacked(kingSquare(~stm_), stm_)) return "The side not to move is in check";
    return {};
}

Square Position::kingSquare(Color c) const {
    Bitboard k = pieces(c, King);
    return k ? lsb(k) : NoSquare;
}

Bitboard Position::attackersTo(Square s, Bitboard occ) const {
    return (pawnAttacks(Black, s) & pieces_[WPawn]) | (pawnAttacks(White, s) & pieces_[BPawn]) |
           (knightAttacks(s) & (pieces_[WKnight] | pieces_[BKnight])) |
           (kingAttacks(s) & (pieces_[WKing] | pieces_[BKing])) |
           (bishopAttacks(s, occ) & (pieces_[WBishop] | pieces_[BBishop] | pieces_[WQueen] | pieces_[BQueen])) |
           (rookAttacks(s, occ) & (pieces_[WRook] | pieces_[BRook] | pieces_[WQueen] | pieces_[BQueen]));
}

bool Position::isAttacked(Square s, Color by) const {
    if (!isValidSquare(s)) return false;
    return (attackersTo(s, occupied()) & byColor_[by]) != 0;
}

Bitboard Position::checkers() const {
    const Square k = kingSquare(stm_);
    if (k == NoSquare) return 0;
    return attackersTo(k, occupied()) & byColor_[~stm_];
}

Piece Position::capturedPiece(Move m) const {
    if (m.isEnPassant()) return makePiece(~stm_, Pawn);
    if (m.isCastle()) return NoPiece;
    return board_[m.to()];
}

Undo Position::makeMove(Move m) {
    const ZobristKeys& z = zobrist();
    Undo u;
    u.castling = castling_;
    u.ep = ep_;
    u.halfmove = halfmove_;
    u.key = key_;

    const Color us = stm_;
    const Color them = ~us;
    const Square from = m.from();
    const Square to = m.to();
    const Piece pc = board_[from];

    if (ep_ != NoSquare) {
        key_ ^= z.epFile[fileOf(ep_)];
        ep_ = NoSquare;
    }
    ++halfmove_;

    if (m.isCastle()) {
        const int rank = rankOf(from);
        const bool kingSide = m.flags() == Move::CastleKing;
        const Square rookFrom = makeSquare(kingSide ? 7 : 0, rank);
        const Square rookTo = makeSquare(kingSide ? 5 : 3, rank);
        movePiece(from, to);
        movePiece(rookFrom, rookTo);
    } else {
        if (m.isEnPassant()) {
            const Square capSq = us == White ? to - 8 : to + 8;
            u.captured = board_[capSq];
            removePiece(capSq);
            halfmove_ = 0;
        } else if (board_[to] != NoPiece) {
            u.captured = board_[to];
            removePiece(to);
            halfmove_ = 0;
        }
        movePiece(from, to);
        if (typeOf(pc) == Pawn) {
            halfmove_ = 0;
            if (m.flags() == Move::DoublePush) {
                const Square epCand = (from + to) / 2;
                if (pawnAttacks(us, epCand) & pieces(them, Pawn)) {
                    ep_ = epCand;
                    key_ ^= z.epFile[fileOf(ep_)];
                }
            } else if (m.isPromotion()) {
                removePiece(to);
                addPiece(makePiece(us, m.promotion()), to);
            }
        }
    }

    key_ ^= z.castling[castling_];
    castling_ = static_cast<uint8_t>(castling_ & CastleMask[from] & CastleMask[to]);
    key_ ^= z.castling[castling_];

    if (us == Black) ++fullmove_;
    stm_ = them;
    key_ ^= z.side;
    return u;
}

void Position::unmakeMove(Move m, const Undo& u) {
    const Color them = stm_;
    const Color us = ~them;
    const Square from = m.from();
    const Square to = m.to();

    if (m.isCastle()) {
        const int rank = rankOf(from);
        const bool kingSide = m.flags() == Move::CastleKing;
        movePiece(to, from);
        movePiece(makeSquare(kingSide ? 5 : 3, rank), makeSquare(kingSide ? 7 : 0, rank));
    } else {
        if (m.isPromotion()) {
            removePiece(to);
            addPiece(makePiece(us, Pawn), to);
        }
        movePiece(to, from);
        if (u.captured != NoPiece) {
            const Square capSq = m.isEnPassant() ? (us == White ? to - 8 : to + 8) : to;
            addPiece(u.captured, capSq);
        }
    }
    if (us == Black) --fullmove_;
    stm_ = us;
    castling_ = u.castling;
    ep_ = u.ep;
    halfmove_ = u.halfmove;
    key_ = u.key;
}

bool Position::isLegal(Move m) const {
    MoveList list;
    generateLegalMoves(*this, list);
    return list.contains(m);
}

bool Position::sameBoard(const Position& o) const {
    return board_ == o.board_ && stm_ == o.stm_ && castling_ == o.castling_ && ep_ == o.ep_;
}

} // namespace pchess::chess
