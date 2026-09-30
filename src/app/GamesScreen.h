// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/games/GamePack.h"
#include "ui/widgets/Widgets.h"

#include <string>
#include <vector>

namespace pchess::app {

// Games browser (M5, design notes §13.3, architecture doc §11): filter buttons (player, year,
// result, opening, favorites) over a paged list; Prev/Next (also the hardware keys) turn pages
// with partial refreshes only; a tap opens the game in GameStudyScreen. Every query is timed in
// the log (M5 budget: < 150 ms on the device with ≥ 10k games).
class GamesScreen final : public Screen {
public:
    explicit GamesScreen(Application& app);
    const char* name() const override { return "games"; }
    std::string title() const override { return "Games"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onEnter() override;

    // For tests and scripted sessions.
    const games::GameFilter& filter() const { return filter_; }
    void setFilter(games::GameFilter f, bool favoritesOnly = false);
    int page() const { return page_; }
    int pageCount() const;
    bool capped() const;       // the filtered count reached GamePack::kCountCap
    bool hasNextPage() const;
    int total() const { return total_; }
    const std::vector<games::GameRow>& rows() const { return rows_; }
    void nextPage();
    void prevPage();
    void open(size_t row);

private:
    enum ButtonId { BtnPlayer, BtnYear, BtnResult, BtnOpening, BtnFavorites, BtnPrev, BtnNext, BtnMenu };

    void layout();
    void query();     // count + first page (filters changed)
    void loadPage();  // rows of page_
    void updateChips();
    void editPlayer();
    void chooseYear();
    void chooseResult();
    void editOpening();
    void openMenu();

    games::GameFilter filter_;
    bool favoritesOnly_ = false;
    int rowsPerPage_ = 8;
    int page_ = 0, total_ = 0;
    std::vector<games::GameRow> rows_;
    std::vector<ui::Button> chips_, nav_;
    ui::Rect listRect_;
    int rowH_ = 0;
    std::string message_;  // empty list / no pack
    std::string title_ = "Games";  // the pack's meta title
};

} // namespace pchess::app
