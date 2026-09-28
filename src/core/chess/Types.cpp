// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Types.h"

namespace pchess::chess {

std::string squareName(Square s) {
    if (!isValidSquare(s)) return "-";
    std::string r(2, ' ');
    r[0] = static_cast<char>('a' + fileOf(s));
    r[1] = static_cast<char>('1' + rankOf(s));
    return r;
}

Square parseSquare(std::string_view text) {
    if (text.size() != 2) return NoSquare;
    char f = text[0];
    char r = text[1];
    if (f >= 'A' && f <= 'H') f = static_cast<char>(f - 'A' + 'a');
    if (f < 'a' || f > 'h' || r < '1' || r > '8') return NoSquare;
    return makeSquare(f - 'a', r - '1');
}

char pieceChar(Piece p) {
    static const char chars[] = "PNBRQKpnbrqk.";
    return p <= NoPiece ? chars[p] : '?';
}

Piece pieceFromChar(char c) {
    switch (c) {
    case 'P': return WPawn;
    case 'N': return WKnight;
    case 'B': return WBishop;
    case 'R': return WRook;
    case 'Q': return WQueen;
    case 'K': return WKing;
    case 'p': return BPawn;
    case 'n': return BKnight;
    case 'b': return BBishop;
    case 'r': return BRook;
    case 'q': return BQueen;
    case 'k': return BKing;
    default: return NoPiece;
    }
}

char pieceTypeLetter(PieceType t) {
    static const char letters[] = "PNBRQK?";
    return letters[t <= NoPieceType ? t : NoPieceType];
}

} // namespace pchess::chess
