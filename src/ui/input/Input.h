// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Geometry.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pchess::ui {

// Semantic keys (architecture doc §5). Backends map physical buttons to these.
enum class KeyCode : uint8_t { None, Prev, Next, Menu, Home, Back, Up, Down, Left, Right, Ok };

const char* toString(KeyCode k);

struct InputEvent {
    enum class Type : uint8_t { PointerDown, PointerUp, PointerMove, LongPress, Key, KeyRelease, Text };
    Type type = Type::PointerDown;
    Point pt;
    KeyCode key = KeyCode::None;
    bool repeat = false;
    int rawCode = 0;  // platform key code (Diagnostics shows it)
    std::string text; // Type::Text: UTF-8 typed text; "\b" = backspace (desktop text entry)

    static InputEvent pointer(Type t, Point p) {
        InputEvent e;
        e.type = t;
        e.pt = p;
        return e;
    }
    static InputEvent keyPress(KeyCode k, bool repeat = false, int raw = 0) {
        InputEvent e;
        e.type = Type::Key;
        e.key = k;
        e.repeat = repeat;
        e.rawCode = raw;
        return e;
    }
    static InputEvent typed(std::string t) {
        InputEvent e;
        e.type = Type::Text;
        e.text = std::move(t);
        return e;
    }
};

struct Gesture {
    enum class Type : uint8_t { Tap, LongPress, Drag };
    Type type = Type::Tap;
    Point start;  // pointer down
    Point end;    // pointer up (== start for Tap/LongPress)
};

// Turns pointer streams into tap / long press / drag (touch down = source, touch up =
// destination; no redraw while dragging, design notes §21.1).
class GestureRecognizer {
public:
    using Clock = std::chrono::steady_clock;

    void setSlop(int px) { slop_ = px; }
    void setLongPressDelay(std::chrono::milliseconds d) { longDelay_ = d; }

    // Returns a completed gesture, if any. `now` is injectable for tests.
    std::optional<Gesture> feed(const InputEvent& e, Clock::time_point now = Clock::now());
    bool pressed() const { return down_; }

private:
    bool down_ = false;
    bool longFired_ = false;
    bool moved_ = false;
    Point start_;
    Clock::time_point downAt_{};
    int slop_ = 24;
    std::chrono::milliseconds longDelay_{600};
};

} // namespace pchess::ui
