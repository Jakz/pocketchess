// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/GameTree.h"
#include "ui/canvas/Canvas.h"

#include <string>

namespace pchess::app {

class Application;

// Move list panel shared by Analyze and Play: the path to `current` (current move highlighted),
// then the main continuation after it (greyed), wrapped into lines; the window scrolls so the
// current move stays visible. An empty game shows `emptyHint`.
void drawMoveList(Application& app, ui::Canvas& c, const ui::Rect& rect, const chess::GameTree& tree,
                  const chess::GameNode* current, const std::string& emptyHint);

} // namespace pchess::app
