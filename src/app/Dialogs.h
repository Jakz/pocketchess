// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/chess/Types.h"
#include "ui/widgets/Widgets.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pchess::app {

// Base for modal dialogs drawn over the current screen (not opaque).
class Dialog : public Screen {
public:
    using Screen::Screen;
    bool opaque() const override { return false; }
    ui::Rect bounds() const override { return rect_; }

protected:
    void drawFrame(ui::Canvas& c, const std::string& title);
    ui::Rect contentRect() const;
    ui::Rect rect_;
    int titleH_ = 0;
};

// Title, wrapped text and a row of buttons. `done(index)` gets the button index, or -1 on Back.
class MessageDialog final : public Dialog {
public:
    MessageDialog(Application& app, std::string title, std::string text, std::vector<std::string> buttons,
                  std::function<void(int)> done, bool vertical = false);
    const char* name() const override { return "message"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

private:
    void finish(int index);
    std::string title_, text_;
    std::vector<ui::Button> buttons_;
    std::vector<std::string> lines_;
    std::function<void(int)> done_;
    int focus_ = 0;
};

// Picks one item from a list of any length: pages of `pageSize` rows plus "More…" and "Close".
// `done(index)` gets the item's index, or -1 when closed.
void chooseFromList(Application& app, const std::string& title, std::vector<std::string> items,
                    std::function<void(int)> done, int pageSize = 6);

// Promotion choice over the board: Q R B N in a row.
class PromotionDialog final : public Dialog {
public:
    PromotionDialog(Application& app, chess::Color color, std::function<void(chess::PieceType)> done);
    const char* name() const override { return "promotion"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

private:
    void finish(chess::PieceType t);
    chess::Color color_;
    std::function<void(chess::PieceType)> done_;
    std::vector<ui::Rect> cells_;
    int focus_ = 0;
};

// On-canvas text entry for hosts without a native keyboard (desktop): type with the PC
// keyboard; Enter/OK accepts, Esc cancels.
class TextEntryDialog final : public Dialog {
public:
    TextEntryDialog(Application& app, std::string title, std::string initial,
                    std::function<void(std::optional<std::string>)> done);
    const char* name() const override { return "text-entry"; }
    bool wantsText() const override { return true; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onText(const std::string& text) override;

private:
    void finish(bool accept);
    std::string title_, text_;
    std::function<void(std::optional<std::string>)> done_;
    std::vector<ui::Button> buttons_;
    ui::Rect field_;
};

} // namespace pchess::app
