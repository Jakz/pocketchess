// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/GameTree.h"

#include <algorithm>

namespace pchess::chess {

bool GameNode::isMainLine() const {
    for (const GameNode* n = this; n->parent; n = n->parent)
        if (n->parent->children.front().get() != n) return false;
    return true;
}

int GameNode::indexInParent() const {
    if (!parent) return -1;
    for (size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i].get() == this) return static_cast<int>(i);
    return -1;
}

GameTree::GameTree(Position start) : start_(std::move(start)), root_(std::make_unique<GameNode>()) {}

GameNode* GameTree::addMove(GameNode* parent, Move m) {
    for (auto& c : parent->children)
        if (c->move == m) return c.get();
    auto node = std::make_unique<GameNode>();
    node->move = m;
    node->parent = parent;
    node->ply = parent->ply + 1;
    parent->children.push_back(std::move(node));
    return parent->children.back().get();
}

std::vector<Move> GameTree::movesTo(const GameNode* node) const {
    std::vector<Move> moves;
    for (const GameNode* n = node; n && n->parent; n = n->parent) moves.push_back(n->move);
    std::reverse(moves.begin(), moves.end());
    return moves;
}

Position GameTree::positionAt(const GameNode* node) const {
    Position p = start_;
    for (Move m : movesTo(node)) p.makeMove(m);
    return p;
}

std::vector<uint64_t> GameTree::keyHistoryTo(const GameNode* node) const {
    std::vector<uint64_t> keys;
    Position p = start_;
    for (Move m : movesTo(node)) {
        keys.push_back(p.key());
        p.makeMove(m);
    }
    return keys;
}

std::vector<Move> GameTree::mainLine() const {
    std::vector<Move> moves;
    for (const GameNode* n = root_->mainChild(); n; n = n->mainChild()) moves.push_back(n->move);
    return moves;
}

GameNode* GameTree::mainLineEnd() {
    GameNode* n = root_.get();
    while (n->mainChild()) n = n->mainChild();
    return n;
}

void GameTree::promoteVariation(GameNode* node) {
    // Walk up until the fork where node's branch is not the first child, and move it to front.
    for (GameNode* n = node; n && n->parent; n = n->parent) {
        auto& siblings = n->parent->children;
        auto it = std::find_if(siblings.begin(), siblings.end(), [&](const auto& c) { return c.get() == n; });
        if (it != siblings.begin() && it != siblings.end()) {
            std::rotate(siblings.begin(), it, it + 1);
            return;
        }
    }
}

void GameTree::deleteNode(GameNode* node) {
    if (!node || !node->parent) return;
    auto& siblings = node->parent->children;
    siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [&](const auto& c) { return c.get() == node; }),
                   siblings.end());
}

size_t GameTree::nodeCount() const {
    size_t count = 0;
    std::vector<const GameNode*> stack{root_.get()};
    while (!stack.empty()) {
        const GameNode* n = stack.back();
        stack.pop_back();
        ++count;
        for (const auto& c : n->children) stack.push_back(c.get());
    }
    return count;
}

} // namespace pchess::chess
