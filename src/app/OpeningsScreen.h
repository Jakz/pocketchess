// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "ui/widgets/Widgets.h"

#include <string>
#include <vector>

namespace pchess::games {
struct OpeningLine;
}

namespace pchess::app {

// Openings (design notes §15): the named lines of the bundled book (Lichess chess-openings, CC0)
// as families -> variations, or the lines matching a search. A line opens in Analyze at its last
// move, where the engine, the move list and "Games" in the menu take over.
class OpeningsScreen final : public Screen {
public:
    explicit OpeningsScreen(Application& app);
    const char* name() const override { return "openings"; }
    std::string title() const override { return "Openings"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

    // For tests and scripted sessions.
    struct Item {
        std::string title, detail;
        std::string family;                       // a family row: its name
        const games::OpeningLine* line = nullptr;  // a line row
        bool counted = false;                      // games added to `detail`
    };
    const std::vector<Item>& items() const { return items_; }
    void showFamilies();
    void showFamily(const std::string& family);
    void search(const std::string& text);
    void open(size_t index);

private:
    enum ButtonId { BtnPrev, BtnNext, BtnFind, BtnMenu };

    void layout();
    void showPage();
    int pageCount() const;
    void turnPage(int delta);
    void openMenu();
    void find();

    std::vector<Item> items_;
    std::string heading_ = "Openings";
    std::string family_, query_;
    std::vector<ui::Button> nav_;
    ui::Rect listRect_, backRect_;
    int rowH_ = 0, rowsPerPage_ = 8, page_ = 0;
};

} // namespace pchess::app
