// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"

#include <charconv>
#include <vector>

namespace pchess::chess {
namespace {

std::vector<std::string_view> splitFields(std::string_view s) {
    std::vector<std::string_view> fields;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        size_t j = i;
        while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
        if (j > i) fields.push_back(s.substr(i, j - i));
        i = j;
    }
    return fields;
}

bool parseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

Error fenError(std::string_view fen, std::string what) {
    return makeError(ErrorCode::ParseFen, what + " in \"" + std::string(fen) + "\"");
}

} // namespace

Result<Position> parseFen(std::string_view fen) {
    const auto f = splitFields(fen);
    if (f.size() < 4 || f.size() > 6) return fenError(fen, "expected 4 to 6 fields");

    Position pos;
    pos.clear();
    int rank = 7, file = 0;
    for (char c : f[0]) {
        if (c == '/') {
            if (file != 8 || rank == 0) return fenError(fen, "bad rank layout");
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fenError(fen, "rank too long");
        } else {
            const Piece p = pieceFromChar(c);
            if (p == NoPiece) return fenError(fen, std::string("unknown piece '") + c + "'");
            if (file > 7) return fenError(fen, "rank too long");
            pos.put(p, makeSquare(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8) return fenError(fen, "board must have 8 full ranks");

    if (f[1] == "w") pos.setSideToMove(White);
    else if (f[1] == "b") pos.setSideToMove(Black);
    else return fenError(fen, "side to move must be 'w' or 'b'");

    unsigned rights = 0;
    if (f[2] != "-") {
        for (char c : f[2]) {
            switch (c) {
            case 'K': rights |= WhiteOO; break;
            case 'Q': rights |= WhiteOOO; break;
            case 'k': rights |= BlackOO; break;
            case 'q': rights |= BlackOOO; break;
            default: return fenError(fen, "bad castling field");
            }
        }
    }
    pos.setCastlingRights(static_cast<uint8_t>(rights));

    if (f[3] != "-") {
        const Square ep = parseSquare(f[3]);
        if (ep == NoSquare) return fenError(fen, "bad en-passant square");
        pos.setEpSquare(ep);
    }

    int half = 0, full = 1;
    if (f.size() >= 5 && !parseInt(f[4], half)) return fenError(fen, "bad halfmove clock");
    if (f.size() >= 6 && !parseInt(f[5], full)) return fenError(fen, "bad fullmove number");
    pos.setHalfmoveClock(half);
    pos.setFullmoveNumber(full);

    pos.normalize();
    if (std::string why = pos.validate(); !why.empty()) return fenError(fen, why);
    return pos;
}

std::string toFen(const Position& pos) {
    std::string s;
    s.reserve(90);
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece p = pos.pieceAt(makeSquare(file, rank));
            if (p == NoPiece) {
                ++empty;
                continue;
            }
            if (empty) s += static_cast<char>('0' + empty);
            empty = 0;
            s += pieceChar(p);
        }
        if (empty) s += static_cast<char>('0' + empty);
        if (rank) s += '/';
    }
    s += pos.sideToMove() == White ? " w " : " b ";
    const uint8_t r = pos.castlingRights();
    if (!r) s += '-';
    if (r & WhiteOO) s += 'K';
    if (r & WhiteOOO) s += 'Q';
    if (r & BlackOO) s += 'k';
    if (r & BlackOOO) s += 'q';
    s += ' ';
    s += squareName(pos.epSquare());
    s += ' ';
    s += std::to_string(pos.halfmoveClock());
    s += ' ';
    s += std::to_string(pos.fullmoveNumber());
    return s;
}

} // namespace pchess::chess
