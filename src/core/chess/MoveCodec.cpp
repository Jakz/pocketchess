// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/MoveCodec.h"

#include "core/chess/MoveGen.h"

namespace pchess::chess {

std::vector<uint8_t> encodeMoves(std::span<const Move> moves) {
    std::vector<uint8_t> out;
    out.reserve(moves.size() * 2);
    for (Move m : moves) {
        out.push_back(static_cast<uint8_t>(m.raw() & 0xFF));
        out.push_back(static_cast<uint8_t>(m.raw() >> 8));
    }
    return out;
}

std::vector<Move> decodeMovesUnchecked(std::span<const uint8_t> blob) {
    std::vector<Move> out;
    out.reserve(blob.size() / 2);
    for (size_t i = 0; i + 1 < blob.size(); i += 2)
        out.push_back(Move::fromRaw(static_cast<uint16_t>(blob[i] | (blob[i + 1] << 8))));
    return out;
}

Result<std::vector<Move>> decodeMoves(const Position& start, std::span<const uint8_t> blob) {
    if (blob.size() % 2 != 0) return makeError(ErrorCode::CorruptData, "move blob has odd length");
    std::vector<Move> moves = decodeMovesUnchecked(blob);
    Position pos = start;
    MoveList legal;
    for (size_t i = 0; i < moves.size(); ++i) {
        generateLegalMoves(pos, legal);
        if (!legal.contains(moves[i]))
            return makeError(ErrorCode::CorruptData, "illegal move #" + std::to_string(i + 1) + " in move blob");
        pos.makeMove(moves[i]);
    }
    return moves;
}

} // namespace pchess::chess
