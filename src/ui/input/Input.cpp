// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/input/Input.h"

#include <cstdlib>

namespace pchess::ui {

const char* toString(KeyCode k) {
    switch (k) {
    case KeyCode::None: return "none";
    case KeyCode::Prev: return "prev";
    case KeyCode::Next: return "next";
    case KeyCode::Menu: return "menu";
    case KeyCode::Home: return "home";
    case KeyCode::Back: return "back";
    case KeyCode::Up: return "up";
    case KeyCode::Down: return "down";
    case KeyCode::Left: return "left";
    case KeyCode::Right: return "right";
    case KeyCode::Ok: return "ok";
    }
    return "?";
}

std::optional<Gesture> GestureRecognizer::feed(const InputEvent& e, Clock::time_point now) {
    using T = InputEvent::Type;
    switch (e.type) {
    case T::PointerDown:
        down_ = true;
        longFired_ = false;
        moved_ = false;
        start_ = e.pt;
        downAt_ = now;
        return std::nullopt;
    case T::PointerMove:
        if (down_ && (std::abs(e.pt.x - start_.x) > slop_ || std::abs(e.pt.y - start_.y) > slop_)) moved_ = true;
        return std::nullopt;
    case T::LongPress:
        // Device-reported long press (EVT_POINTERLONG); fire once, suppress the tap on release.
        if (down_ && !moved_ && !longFired_) {
            longFired_ = true;
            return Gesture{Gesture::Type::LongPress, start_, start_};
        }
        return std::nullopt;
    case T::PointerUp: {
        if (!down_) return std::nullopt;
        down_ = false;
        const bool far = std::abs(e.pt.x - start_.x) > slop_ || std::abs(e.pt.y - start_.y) > slop_;
        if (far || moved_) return Gesture{Gesture::Type::Drag, start_, e.pt};
        if (longFired_) return std::nullopt;
        if (now - downAt_ >= longDelay_) return Gesture{Gesture::Type::LongPress, start_, start_};
        return Gesture{Gesture::Type::Tap, start_, start_};
    }
    default: return std::nullopt;
    }
}

} // namespace pchess::ui
