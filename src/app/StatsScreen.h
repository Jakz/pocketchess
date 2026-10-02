// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "ui/widgets/Widgets.h"

#include <string>
#include <utility>
#include <vector>

namespace pchess::app {

// Statistics (M7, ADR 0015): one page per mode, read from user/chess.db and user/progress.db on
// entry. Puzzles (rating chart over the last attempts), Play (per strength), Guess the move,
// Opening practice and My mistakes. ◀ ▶ (Prev/Next) turn pages; Menu lists them.
class StatsScreen final : public Screen {
public:
    struct Page {
        std::string title;
        std::vector<std::pair<std::string, std::string>> rows;  // label, value ("" value: a heading)
        std::vector<int> chart;                                  // ratings, oldest first (Puzzles)
        std::string empty;                                       // shown when there is nothing yet
    };

    explicit StatsScreen(Application& app);
    const char* name() const override { return "stats"; }
    std::string title() const override { return "Stats"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

    // For tests.
    const std::vector<Page>& pages() const { return pages_; }
    int page() const { return page_; }
    void turnPage(int delta);
    void showPage(int index);

private:
    enum ButtonId { BtnPrev, BtnNext, BtnMenu };

    void build();
    void openMenu();
    void drawChart(ui::Canvas& c, const ui::Rect& r, const std::vector<int>& values);

    std::vector<Page> pages_;
    int page_ = 0;
    std::vector<ui::Button> nav_;
    ui::Rect backRect_, bodyRect_;
};

} // namespace pchess::app
