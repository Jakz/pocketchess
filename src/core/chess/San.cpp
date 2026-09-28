// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/San.h"

#include "core/chess/MoveGen.h"

#include <tuple>

namespace pchess::chess {
namespace {

PieceType pieceFromSanLetter(char c) {
    switch (c) {
    case 'N': return Knight;
    case 'B': return Bishop;
    case 'R': return Rook;
    case 'Q': return Queen;
    case 'K': return King;
    default: return NoPieceType;
    }
}

PieceType promoFromLetter(char c) {
    switch (c) {
    case 'N': case 'n': return Knight;
    case 'B': case 'b': return Bishop;
    case 'R': case 'r': return Rook;
    case 'Q': case 'q': return Queen;
    default: return NoPieceType;
    }
}

bool isFile(char c) { return c >= 'a' && c <= 'h'; }
bool isRank(char c) { return c >= '1' && c <= '8'; }

std::string checkSuffix(const Position& pos, Move m) {
    Position after = pos.after(m);
    if (!after.inCheck()) return {};
    MoveList replies;
    generateLegalMoves(after, replies);
    return replies.empty() ? "#" : "+";
}

Error sanError(std::string_view san, std::string what) {
    return makeError(ErrorCode::ParseSan, "\"" + std::string(san) + "\": " + what);
}

} // namespace

std::string toSan(const Position& pos, Move m) {
    if (m.isCastle()) return (m.flags() == Move::CastleKing ? "O-O" : "O-O-O") + checkSuffix(pos, m);

    const Piece pc = pos.movingPiece(m);
    const PieceType type = typeOf(pc);
    const bool capture = pos.capturedPiece(m) != NoPiece;
    std::string s;

    if (type == Pawn) {
        if (capture) {
            s += static_cast<char>('a' + fileOf(m.from()));
            s += 'x';
        }
    } else {
        s += pieceTypeLetter(type);
        MoveList list;
        generateLegalMoves(pos, list);
        bool ambiguous = false, sameFile = false, sameRank = false;
        for (Move o : list) {
            if (o == m || o.to() != m.to() || o.from() == m.from() || pos.movingPiece(o) != pc) continue;
            ambiguous = true;
            if (fileOf(o.from()) == fileOf(m.from())) sameFile = true;
            if (rankOf(o.from()) == rankOf(m.from())) sameRank = true;
        }
        if (ambiguous) {
            if (!sameFile) s += static_cast<char>('a' + fileOf(m.from()));
            else if (!sameRank) s += static_cast<char>('1' + rankOf(m.from()));
            else s += squareName(m.from());
        }
        if (capture) s += 'x';
    }
    s += squareName(m.to());
    if (m.isPromotion()) {
        s += '=';
        s += pieceTypeLetter(m.promotion());
    }
    return s + checkSuffix(pos, m);
}

Result<Move> parseSan(const Position& pos, std::string_view input) {
    // Strip decorations.
    std::string s;
    for (char c : input) {
        if (c == ' ' || c == '\t') continue;
        s += c;
    }
    for (std::string_view suffix : {"e.p.", "ep"}) {
        if (s.size() > suffix.size() + 2 && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
            s.erase(s.size() - suffix.size());
    }
    while (!s.empty() && (s.back() == '+' || s.back() == '#' || s.back() == '!' || s.back() == '?')) s.pop_back();
    if (s.empty()) return sanError(input, "empty move");

    MoveList list;
    generateLegalMoves(pos, list);

    // Castling.
    std::string upper = s;
    for (char& c : upper)
        if (c == 'o' || c == '0') c = 'O';
    if (upper == "O-O" || upper == "O-O-O") {
        const uint16_t flag = upper == "O-O" ? Move::CastleKing : Move::CastleQueen;
        for (Move m : list)
            if (m.flags() == flag) return m;
        return sanError(input, "castling is not legal here");
    }

    // Promotion suffix: "=Q", "Q" or "q" after the destination.
    PieceType promo = NoPieceType;
    if (s.size() >= 2 && s[s.size() - 2] == '=') {
        promo = promoFromLetter(s.back());
        if (promo == NoPieceType) return sanError(input, "bad promotion piece");
        s.erase(s.size() - 2);
    } else if (s.size() >= 3 && promoFromLetter(s.back()) != NoPieceType && isRank(s[s.size() - 2])) {
        promo = promoFromLetter(s.back());
        s.pop_back();
    }

    // Destination square: last two characters.
    if (s.size() < 2 || !isFile(s[s.size() - 2]) || !isRank(s.back())) return sanError(input, "missing destination square");
    const Square to = makeSquare(s[s.size() - 2] - 'a', s.back() - '1');
    std::string head = s.substr(0, s.size() - 2);

    // Remove capture markers and a long-algebraic dash.
    std::string clean;
    for (char c : head)
        if (c != 'x' && c != 'X' && c != ':' && c != '-') clean += c;

    PieceType type = Pawn;
    size_t i = 0;
    if (!clean.empty() && pieceFromSanLetter(clean[0]) != NoPieceType) {
        type = pieceFromSanLetter(clean[0]);
        i = 1;
    } else if (!clean.empty() && clean[0] == 'P') {
        i = 1;
    }
    int fromFile = -1, fromRank = -1;
    for (; i < clean.size(); ++i) {
        if (isFile(clean[i]) && fromFile < 0) fromFile = clean[i] - 'a';
        else if (isRank(clean[i]) && fromRank < 0) fromRank = clean[i] - '1';
        else return sanError(input, "unexpected characters");
    }

    auto match = [&](PieceType t) -> std::pair<Move, int> {
        Move found;
        int count = 0;
        for (Move m : list) {
            if (m.to() != to || typeOf(pos.movingPiece(m)) != t) continue;
            if (fromFile >= 0 && fileOf(m.from()) != fromFile) continue;
            if (fromRank >= 0 && rankOf(m.from()) != fromRank) continue;
            if (m.promotion() != promo) {
                // "e8" without a piece letter: allowed only when no promotion is involved.
                continue;
            }
            found = m;
            ++count;
        }
        return {found, count};
    };

    auto [m, n] = match(type);
    // "bxc3"-style ambiguity: a lowercase 'b' was read as a file; also try a bishop.
    if (n == 0 && type == Pawn && fromFile == 1 && !head.empty() && head[0] == 'b') {
        const int savedFile = fromFile;
        fromFile = -1;
        std::tie(m, n) = match(Bishop);
        if (n == 0) fromFile = savedFile;
    }
    if (n == 1) return m;
    if (n > 1) return sanError(input, "ambiguous move");
    return sanError(input, "no legal move matches");
}

std::vector<std::string> toSanLine(Position pos, std::span<const Move> moves) {
    std::vector<std::string> out;
    out.reserve(moves.size());
    for (Move m : moves) {
        if (!pos.isLegal(m)) break;
        out.push_back(toSan(pos, m));
        pos.makeMove(m);
    }
    return out;
}

std::string toUci(Move m) {
    if (m.isNone()) return "0000";
    std::string s = squareName(m.from()) + squareName(m.to());
    if (m.isPromotion()) s += static_cast<char>(pieceTypeLetter(m.promotion()) - 'A' + 'a');
    return s;
}

Result<Move> parseUci(const Position& pos, std::string_view uci) {
    if (uci.size() != 4 && uci.size() != 5) return makeError(ErrorCode::ParseUci, std::string(uci));
    const Square from = parseSquare(uci.substr(0, 2));
    const Square to = parseSquare(uci.substr(2, 2));
    if (from == NoSquare || to == NoSquare) return makeError(ErrorCode::ParseUci, std::string(uci));
    PieceType promo = NoPieceType;
    if (uci.size() == 5) {
        promo = promoFromLetter(uci[4]);
        if (promo == NoPieceType) return makeError(ErrorCode::ParseUci, std::string(uci));
    }
    MoveList list;
    generateLegalMoves(pos, list);
    for (Move m : list)
        if (m.from() == from && m.to() == to && m.promotion() == promo) return m;
    return makeError(ErrorCode::IllegalMove, std::string(uci));
}

} // namespace pchess::chess
