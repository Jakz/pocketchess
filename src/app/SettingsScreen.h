// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "ui/widgets/Widgets.h"

#include <functional>
#include <string>
#include <vector>

namespace pchess::app {

// Settings v1 (design notes §20, M3): Display, Input and Engine/Power options as a paged list of
// rows "label ........ value". Tapping a row toggles it (two choices) or opens a choice dialog.
// Every change is applied at once and saved to user/chess.db; the engine picks it up on its
// next job (no restart). Prev/Next move the focus, OK/Menu opens the focused row.
class SettingsScreen final : public Screen {
public:
    explicit SettingsScreen(Application& app);
    const char* name() const override { return "settings"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

    // Label of the focused row ("" if none) and the page shown: for logs and key-driven tests.
    std::string focusedLabel() const;
    int page() const { return page_; }
    int pageCount() const { return static_cast<int>(pages_.size()); }

private:
    struct Item {
        std::string section;
        std::string label;
        std::vector<std::string> choices;  // display texts
        std::function<int()> current;      // index into choices (-1: none matches)
        std::function<void(int)> set;
    };
    struct Row {
        int item = -1;  // -1: section header
        std::string header;
        ui::Rect rect;
    };

    void buildItems();
    void paginate();
    void activate(int item);
    void changed();
    void setPage(int page);
    void moveFocus(int delta);
    std::string valueText(const Item& it) const;
    ui::Rect focusRect() const;

    std::vector<Item> items_;
    std::vector<std::vector<Row>> pages_;
    std::vector<ui::Button> nav_;  // Prev page / Next page
    ui::Rect body_, footer_;
    int page_ = 0;
    int focus_ = -1;  // row index on the current page (item rows only), -1 = none
};

} // namespace pchess::app
