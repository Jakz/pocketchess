// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace pchess::chess {

enum Color : uint8_t { White = 0, Black = 1 };
constexpr Color operator~(Color c) { return static_cast<Color>(c ^ 1); }

enum PieceType : uint8_t { Pawn = 0, Knight, Bishop, Rook, Queen, King, NoPieceType };

// Piece = color * 6 + type; NoPiece = 12.
enum Piece : uint8_t {
    WPawn = 0, WKnight, WBishop, WRook, WQueen, WKing,
    BPawn, BKnight, BBishop, BRook, BQueen, BKing,
    NoPiece
};

constexpr Piece makePiece(Color c, PieceType t) { return static_cast<Piece>(c * 6 + t); }
constexpr PieceType typeOf(Piece p) { return p == NoPiece ? NoPieceType : static_cast<PieceType>(p % 6); }
constexpr Color colorOf(Piece p) { return static_cast<Color>(p / 6); }

// Squares are 0..63 with A1 = 0, H1 = 7, A8 = 56. `int` on purpose: no char/signedness traps.
using Square = int;
constexpr Square NoSquare = 64;

enum : Square {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
};

constexpr int fileOf(Square s) { return s & 7; }
constexpr int rankOf(Square s) { return s >> 3; }
constexpr Square makeSquare(int file, int rank) { return rank * 8 + file; }
constexpr bool isValidSquare(Square s) { return s >= 0 && s < 64; }
// Rank from the side's own point of view (0 = back rank).
constexpr int relativeRank(Color c, Square s) { return c == White ? rankOf(s) : 7 - rankOf(s); }

std::string squareName(Square s);
// Parses "e4" (lowercase or uppercase file). Returns NoSquare on error.
Square parseSquare(std::string_view text);

char pieceChar(Piece p);            // 'P','n',... (FEN letters), '.' for NoPiece
Piece pieceFromChar(char c);        // inverse of pieceChar, NoPiece when invalid
char pieceTypeLetter(PieceType t);  // 'N','B','R','Q','K'; 'P' for pawn

// Castling rights bitmask.
enum CastlingRight : uint8_t {
    NoCastling = 0,
    WhiteOO = 1,
    WhiteOOO = 2,
    BlackOO = 4,
    BlackOOO = 8,
    AllCastling = 15,
};

} // namespace pchess::chess
