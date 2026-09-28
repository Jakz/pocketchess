// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Device backend (PocketBook, InkView 6.10). No C++ types cross the InkView boundary: InkView is
// a C API and Zig links its own libc++ statically (build doc §4.1).

#include "platform/Host.h"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace pchess::app {
class Application;
}

namespace pchess::platform::pocketbook {

// How post() wakes the UI thread (the M0.5 spike decides; ADR 0003).
enum class PostMode {
    Poll,       // 100 ms UI timer, only while background activity is on (safe default, R4)
    SendEvent,  // SendEvent(handler, custom event) from the posting thread (candidate)
};

class InkViewHost final : public Host, public Power, public TextInput {
public:
    InkViewHost();

    // Event entry point (called from the C handler).
    int handle(int type, int par1, int par2);
    void attach(app::Application* app) { app_ = app; }

    void post(std::function<void()> fn) override;
    TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) override;
    void cancelTimer(TimerId id) override;
    void quit() override;
    TextInput* textInput() override { return this; }
    Power& power() override { return *this; }
    InfoList systemInfo() const override;
    std::string platformName() const override { return "pocketbook"; }
    void setBackgroundActivity(bool active) override;

    int batteryPercent() const override;
    bool charging() const override;

    void request(const std::string& title, const std::string& initial,
                 std::function<void(std::optional<std::string>)> done) override;

    void setPostMode(PostMode m) { postMode_ = m; }
    static void timerTrampoline();
    static void pollTrampoline();
    static void keyboardTrampoline(char* text);

private:
    void runPosted();
    void armTimer();
    void fireTimers();

    app::Application* app_ = nullptr;
    PostMode postMode_ = PostMode::Poll;
    std::mutex postMutex_;
    std::deque<std::function<void()>> posted_;
    int backgroundRefs_ = 0;

    struct Timer {
        std::chrono::steady_clock::time_point due;
        std::function<void()> fn;
    };
    std::map<TimerId, Timer> timers_;
    TimerId nextTimer_ = 1;
    std::function<void(std::optional<std::string>)> keyboardDone_;
};

// Blits dirty rects into the InkView screen buffer and issues the refresh call per mode.
class InkViewPresenter final : public Presenter {
public:
    InkViewPresenter();
    DeviceProfile profile() const override { return profile_; }
    void present(const ui::Canvas& canvas, const ui::PresentRequest& req) override;
    InfoList info() const override;
    double lastPresentMs() const override { return lastMs_; }

private:
    DeviceProfile profile_;
    double lastMs_ = 0;
};

// Installs the C handler and runs InkViewMain with the given app factory.
int runInkViewApp(int argc, char** argv);

} // namespace pchess::platform::pocketbook
