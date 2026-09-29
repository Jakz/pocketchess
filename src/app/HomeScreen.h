// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "ui/widgets/Widgets.h"

#include <vector>

namespace pchess::app {

// Home (design notes §6.1): one big button per mode, Prev/Next move the focus, OK opens.
// Modes arrive milestone by milestone; later ones are shown disabled with their milestone.
class HomeScreen final : public Screen {
public:
    explicit HomeScreen(Application& app);
    const char* name() const override { return "home"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onEnter() override;
    // Play setup (side, then strength) -> PlayScreen. The strength becomes the new default.
    void startPlay();

private:
    void open(int id);
    void moveFocus(int delta);
    ui::Rect focusRect(int index) const;

    std::vector<ui::Button> buttons_;
    int focus_ = 0;
    ui::Rect footer_;
};

} // namespace pchess::app
