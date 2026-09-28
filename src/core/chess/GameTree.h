// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pchess::chess {

// A node is "the position after `move`". The root has no move. children[0] is the main line;
// further children are variations (RAVs). Used by study, annotations and analysis
// (design notes §8.3, §13.5).
struct GameNode {
    Move move;
    std::string preComment;  // comment printed before the move (start of a variation)
    std::string comment;     // comment after the move
    std::vector<uint8_t> nags;
    GameNode* parent = nullptr;
    std::vector<std::unique_ptr<GameNode>> children;
    int ply = 0;             // plies from the tree's start position

    bool isRoot() const { return parent == nullptr; }
    GameNode* mainChild() const { return children.empty() ? nullptr : children.front().get(); }
    bool isMainLine() const;  // on the path root -> children[0] -> children[0] ...
    int indexInParent() const;
};

class GameTree {
public:
    explicit GameTree(Position start = Position::startPosition());
    GameTree(GameTree&&) noexcept = default;
    GameTree& operator=(GameTree&&) noexcept = default;

    const Position& startPosition() const { return start_; }
    GameNode* root() { return root_.get(); }
    const GameNode* root() const { return root_.get(); }

    // Appends `m` after `parent`, reusing an existing child with the same move.
    // `m` must be legal in positionAt(parent).
    GameNode* addMove(GameNode* parent, Move m);

    Position positionAt(const GameNode* node) const;
    std::vector<Move> movesTo(const GameNode* node) const;
    // Zobrist keys of the positions before `node`'s position (for repetition rules).
    std::vector<uint64_t> keyHistoryTo(const GameNode* node) const;

    std::vector<Move> mainLine() const;
    GameNode* mainLineEnd();

    void promoteVariation(GameNode* node);  // makes node's branch the main line at its fork
    void deleteNode(GameNode* node);        // removes node and its subtree (not the root)
    size_t nodeCount() const;

private:
    Position start_;
    std::unique_ptr<GameNode> root_;
};

} // namespace pchess::chess
