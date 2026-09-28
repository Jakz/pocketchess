// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Bitboard.h"
#include "core/chess/Move.h"
#include "core/chess/Types.h"

#include <array>
#include <cstdint>
#include <string>

namespace pchess::chess {

// State needed to take a move back.
struct Undo {
    Piece captured = NoPiece;
    uint8_t castling = 0;
    Square ep = NoSquare;
    int halfmove = 0;
    uint64_t key = 0;
};

// Board state: 12 piece bitboards + mailbox, side to move, castling rights, en-passant square,
// move counters and Zobrist key (architecture doc §7).
//
// The en-passant square is only kept when an enemy pawn could capture there (as Stockfish and
// Lichess do), so equal positions always have equal keys and FENs.
class Position {
public:
    Position();

    static Position startPosition();

    // --- Queries -------------------------------------------------------------------------
    Piece pieceAt(Square s) const { return board_[s]; }
    Bitboard pieces(Piece p) const { return pieces_[p]; }
    Bitboard pieces(Color c, PieceType t) const { return pieces_[makePiece(c, t)]; }
    Bitboard pieces(Color c) const { return byColor_[c]; }
    Bitboard occupied() const { return byColor_[White] | byColor_[Black]; }

    Color sideToMove() const { return stm_; }
    uint8_t castlingRights() const { return castling_; }
    Square epSquare() const { return ep_; }
    int halfmoveClock() const { return halfmove_; }
    int fullmoveNumber() const { return fullmove_; }
    uint64_t key() const { return key_; }

    Square kingSquare(Color c) const;
    Bitboard attackersTo(Square s, Bitboard occupied) const;
    bool isAttacked(Square s, Color by) const;
    Bitboard checkers() const;
    bool inCheck() const { return checkers() != 0; }

    // Piece that moves / is captured by `m` in this position.
    Piece movingPiece(Move m) const { return board_[m.from()]; }
    Piece capturedPiece(Move m) const;

    // --- Make / unmake -------------------------------------------------------------------
    // `m` must be pseudo-legal for this position (use the legal move list or isLegal()).
    Undo makeMove(Move m);
    void unmakeMove(Move m, const Undo& undo);
    Position after(Move m) const {
        Position p = *this;
        p.makeMove(m);
        return p;
    }
    bool isLegal(Move m) const;

    // --- Setup (FEN parser, editors, tests) ---------------------------------------------
    void clear();
    void put(Piece p, Square s);
    void removeAt(Square s);
    void setSideToMove(Color c) { stm_ = c; }
    void setCastlingRights(uint8_t rights) { castling_ = rights & AllCastling; }
    void setEpSquare(Square s) { ep_ = s; }
    void setHalfmoveClock(int n) { halfmove_ = n; }
    void setFullmoveNumber(int n) { fullmove_ = n; }
    // Drops rights/ep squares that are impossible and recomputes the key. Call after setup.
    void normalize();
    // Empty string when the position is playable; otherwise a human-readable reason.
    std::string validate() const;

    bool sameBoard(const Position& o) const;
    bool operator==(const Position& o) const { return sameBoard(o) && key_ == o.key_; }

private:
    void addPiece(Piece p, Square s);
    void removePiece(Square s);
    void movePiece(Square from, Square to);
    uint64_t computeKey() const;

    std::array<Bitboard, 12> pieces_{};
    std::array<Bitboard, 2> byColor_{};
    std::array<Piece, 64> board_{};
    Color stm_ = White;
    uint8_t castling_ = NoCastling;
    Square ep_ = NoSquare;
    int halfmove_ = 0;
    int fullmove_ = 1;
    uint64_t key_ = 0;
};

} // namespace pchess::chess
