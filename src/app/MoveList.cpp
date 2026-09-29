// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/MoveList.h"

#include "app/Application.h"
#include "core/chess/San.h"

#include <algorithm>
#include <vector>

namespace pchess::app {

using namespace ui;
using namespace chess;

void drawMoveList(Application& app, Canvas& c, const Rect& rect, const GameTree& tree, const GameNode* current,
                  const std::string& emptyHint) {
    const BoardTheme& t = app.theme();
    const UiStyle& s = app.style();
    Font& f = app.fonts().get(s.fontPx);
    Font& fb = app.fonts().get(s.fontPx, FontStyle::Bold);

    struct Token {
        std::string text;
        bool current = false;
        bool future = false;
    };
    std::vector<Token> tokens;
    // Path to the current node, then the main continuation after it (greyed).
    std::vector<const GameNode*> path;
    for (const GameNode* n = current; n && !n->isRoot(); n = n->parent) path.push_back(n);
    std::reverse(path.begin(), path.end());
    Position p = tree.startPosition();
    auto addMove = [&](const GameNode* n, bool future, bool forceNumber) {
        if (p.sideToMove() == White) tokens.push_back({std::to_string(p.fullmoveNumber()) + ".", false, future});
        else if (forceNumber) tokens.push_back({std::to_string(p.fullmoveNumber()) + "…", false, future});
        tokens.push_back({toSan(p, n->move), n == current, future});
        p.makeMove(n->move);
    };
    bool first = true;
    for (const GameNode* n : path) {
        addMove(n, false, first);
        first = false;
    }
    for (const GameNode* n = current ? current->mainChild() : nullptr; n; n = n->mainChild()) {
        addMove(n, true, first);
        first = false;
    }
    if (tokens.empty()) {
        f.draw(c, rect.x, rect.y + f.ascent(), emptyHint, blend(t.background, t.ink, 150));
        return;
    }

    // Wrap tokens into lines and show the window that contains the current move.
    const int space = f.measure(" ");
    std::vector<std::vector<size_t>> lines(1);
    int x = 0, currentLine = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const int w = (tokens[i].current ? fb : f).measure(tokens[i].text) + space;
        if (x + w > rect.w && !lines.back().empty()) {
            lines.emplace_back();
            x = 0;
        }
        lines.back().push_back(i);
        x += w;
        if (tokens[i].current) currentLine = static_cast<int>(lines.size()) - 1;
    }
    const int lh = f.lineHeight();
    const int visible = std::max(1, rect.h / lh);
    int firstLine = std::max(0, currentLine - visible + 1);
    if (!current || current->isRoot()) firstLine = 0;
    c.pushClip(rect);
    int y = rect.y;
    for (int li = firstLine; li < static_cast<int>(lines.size()) && li < firstLine + visible; ++li) {
        int px = rect.x;
        for (size_t idx : lines[static_cast<size_t>(li)]) {
            const Token& tk = tokens[idx];
            Font& ff = tk.current ? fb : f;
            const int w = ff.measure(tk.text);
            if (tk.current) {
                c.fill({px - space / 2, y, w + space, lh}, t.ink);
                ff.draw(c, px, y + ff.ascent(), tk.text, t.background);
            } else {
                ff.draw(c, px, y + ff.ascent(), tk.text, tk.future ? blend(t.background, t.ink, 130) : t.ink);
            }
            px += w + space;
        }
        y += lh;
    }
    c.popClip();
}

} // namespace pchess::app
