// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/MoveCodec.h"

#include "core/chess/MoveGen.h"

#include <algorithm>
#include <array>

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


namespace {

// Legal moves of `pos` in raw-value order: the numbering both directions agree on.
void sortedLegal(const Position& pos, MoveList& list, std::array<Move, MoveList::Capacity>& sorted) {
    generateLegalMoves(pos, list);
    std::copy(list.begin(), list.end(), sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(list.size()),
              [](Move a, Move b) { return a.raw() < b.raw(); });
}

} // namespace

Result<std::vector<uint8_t>> encodeMoveIndexes(const Position& start, std::span<const Move> moves) {
    std::vector<uint8_t> out;
    out.reserve(moves.size());
    Position pos = start;
    MoveList list;
    std::array<Move, MoveList::Capacity> sorted{};
    for (size_t i = 0; i < moves.size(); ++i) {
        sortedLegal(pos, list, sorted);
        const auto end = sorted.begin() + static_cast<std::ptrdiff_t>(list.size());
        const auto it = std::lower_bound(sorted.begin(), end, moves[i], [](Move a, Move b) { return a.raw() < b.raw(); });
        if (it == end || *it != moves[i])
            return makeError(ErrorCode::IllegalMove, "move #" + std::to_string(i + 1) + " is not legal");
        out.push_back(static_cast<uint8_t>(it - sorted.begin()));
        pos.makeMove(moves[i]);
    }
    return out;
}

Result<std::vector<Move>> decodeMoveIndexes(const Position& start, std::span<const uint8_t> blob) {
    std::vector<Move> out;
    out.reserve(blob.size());
    Position pos = start;
    MoveList list;
    std::array<Move, MoveList::Capacity> sorted{};
    for (size_t i = 0; i < blob.size(); ++i) {
        sortedLegal(pos, list, sorted);
        if (blob[i] >= list.size())
            return makeError(ErrorCode::CorruptData, "move index #" + std::to_string(i + 1) + " out of range");
        out.push_back(sorted[blob[i]]);
        pos.makeMove(sorted[blob[i]]);
    }
    return out;
}
} // namespace pchess::chess
