// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Platform interfaces (architecture doc §2). Pure C++: no SDL, InkView, Windows or POSIX headers.
// Backends: platform/sdl (desktop), platform/pocketbook (device).

#include "ui/canvas/Canvas.h"
#include "ui/render/Refresh.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pchess::platform {

struct DeviceProfile {
    int width = 1072;
    int height = 1448;
    int ppiMono = 300;
    int ppiColor = 150;
    bool color = true;
    std::string name = "verse-pro-color";

    static DeviceProfile verseProColor() { return {}; }
    static std::optional<DeviceProfile> byName(const std::string& name) {
        if (name == "verse-pro-color") return verseProColor();
        if (name == "mono-758x1024") return DeviceProfile{758, 1024, 212, 0, false, name};
        if (name == "mono-1072x1448") return DeviceProfile{1072, 1448, 300, 0, false, name};
        if (name == "era-1264x1680") return DeviceProfile{1264, 1680, 300, 150, true, name};
        return std::nullopt;
    }
    // Millimetres -> pixels at the monochrome resolution (touch targets, design notes §39.3).
    int mm(double millimetres) const { return static_cast<int>(millimetres * ppiMono / 25.4 + 0.5); }
};

using InfoList = std::vector<std::pair<std::string, std::string>>;

// Receives finished pixels.
class Presenter {
public:
    virtual ~Presenter() = default;
    virtual DeviceProfile profile() const = 0;
    virtual void present(const ui::Canvas& canvas, const ui::PresentRequest& request) = 0;
    // Canvas format, update calls used, last update latency... (Diagnostics).
    virtual InfoList info() const { return {}; }
    // Wall-clock duration of the most recent present() in milliseconds.
    virtual double lastPresentMs() const { return 0; }
};

enum class Lifecycle { Show, Foreground, Background, Sleep, Wake, Exit };
inline const char* toString(Lifecycle l) {
    switch (l) {
    case Lifecycle::Show: return "show";
    case Lifecycle::Foreground: return "foreground";
    case Lifecycle::Background: return "background";
    case Lifecycle::Sleep: return "sleep";
    case Lifecycle::Wake: return "wake";
    case Lifecycle::Exit: return "exit";
    }
    return "?";
}

using TimerId = uint64_t;

class TextInput {
public:
    virtual ~TextInput() = default;
    // Calls `done` on the UI thread with the text, or nullopt when cancelled.
    virtual void request(const std::string& title, const std::string& initial,
                         std::function<void(std::optional<std::string>)> done) = 0;
};

class Power {
public:
    virtual ~Power() = default;
    virtual int batteryPercent() const = 0;  // -1 = unknown
    virtual bool charging() const = 0;
};

// Event-loop services.
class Host {
public:
    virtual ~Host() = default;
    virtual void post(std::function<void()> fn) = 0;  // thread-safe, wakes the UI thread
    virtual TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) = 0;  // one-shot
    virtual void cancelTimer(TimerId id) = 0;
    virtual void quit() = 0;
    // Native text input, or nullptr when the app should use its own on-canvas dialog.
    virtual TextInput* textInput() = 0;
    virtual Power& power() = 0;
    virtual InfoList systemInfo() const { return {}; }
    // A background thread (engine reader) may post() while this is on. Hosts without a
    // thread-safe wake-up poll their post queue only while active (R4, InkView).
    virtual void setBackgroundActivity(bool /*active*/) {}
    virtual std::string platformName() const = 0;
};

} // namespace pchess::platform
