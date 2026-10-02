// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Importer.h"
#include "app/Screen.h"
#include "ui/widgets/Widgets.h"

#include <string>
#include <vector>

namespace pchess::app {

// Import (M6, design notes §34, ADR 0013): lists what the user copied into imports/, scanned when
// the screen opens and on Rescan only. A tap on an item offers what fits it: open a PGN or FEN in
// Analyze, make a game pack from a PGN, or install a pack, piece set, board theme or engine.
class ImportScreen final : public Screen {
public:
    explicit ImportScreen(Application& app);
    const char* name() const override { return "import"; }
    std::string title() const override { return "Import"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

    // For tests and scripted sessions.
    const std::vector<ImportItem>& items() const { return items_; }
    void rescan();
    void choose(size_t index);  // the action dialog of one item
    void install(size_t index);
    void restore(size_t index);
    void makeGamePack(size_t index);

private:
    enum ButtonId { BtnPrev, BtnNext, BtnRescan, BtnMenu };

    void layout();
    void showPage();
    int pageCount() const;
    void openInAnalyze(const ImportItem& item);
    void turnPage(int delta);
    void openMenu();

    std::vector<ImportItem> items_;
    std::vector<ui::Button> nav_;
    ui::Rect listRect_, backRect_;
    int rowH_ = 0, rowsPerPage_ = 8, page_ = 0;
};

} // namespace pchess::app
