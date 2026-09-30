// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "platform/Host.h"
#include "ui/canvas/Canvas.h"
#include "ui/input/Input.h"

#include <map>
#include <string>

namespace pchess::app {

class Application;

// A screen is a controller plus its view (architecture doc §6). Screens never present: they
// invalidate rectangles and the Application redraws and presents once per event.
class Screen {
public:
    explicit Screen(Application& app) : app_(app) {}
    virtual ~Screen() = default;
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    virtual const char* name() const = 0;
    // What the user calls this screen: the top bar of the screen above shows "‹ <title>".
    virtual std::string title() const { return name(); }
    // Opaque screens cover everything below them; dialogs are not opaque.
    virtual bool opaque() const { return true; }
    virtual ui::Rect bounds() const;

    virtual void draw(ui::Canvas& canvas, const ui::Rect& clip) = 0;
    virtual void onGesture(const ui::Gesture&) {}
    virtual void onKey(const ui::InputEvent&) {}
    virtual void onText(const std::string&) {}
    virtual void onEnter() {}  // became the top screen (pushed, or uncovered)
    virtual void onLeave() {}  // stopped being the top screen (covered, or popped)
    virtual void onLifecycle(platform::Lifecycle) {}
    // True while the screen edits text: desktop hosts then send Backspace as text ("\b").
    virtual bool wantsText() const { return false; }
    // SessionState (design notes §30, ADR 0011): a resumable screen fills `out` (its own keys plus
    // an optional "summary" for the Resume dialog) and returns true. The app saves the topmost
    // screen that does; `name()` becomes the "screen" key.
    virtual bool saveSession(std::map<std::string, std::string>& out) const {
        (void)out;
        return false;
    }

protected:
    Application& app_;
};

} // namespace pchess::app
