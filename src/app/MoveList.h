// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/GameTree.h"
#include "ui/canvas/Canvas.h"

#include <string>
#include <vector>

namespace pchess::app {

class Application;

// A drawn move and the node it stands for (tap a move to jump to it, design notes §13.4).
struct MoveHit {
    ui::Rect rect;
    const chess::GameNode* node = nullptr;
};

// Move list panel shared by Analyze, Play and game study: the path to `current` (current move
// highlighted), then the main continuation after it (greyed), wrapped into lines; the window
// scrolls so the current move stays visible. An empty game shows `emptyHint`. `hits` (optional)
// receives the rectangle of every visible move.
void drawMoveList(Application& app, ui::Canvas& c, const ui::Rect& rect, const chess::GameTree& tree,
                  const chess::GameNode* current, const std::string& emptyHint, std::vector<MoveHit>* hits = nullptr);

} // namespace pchess::app
