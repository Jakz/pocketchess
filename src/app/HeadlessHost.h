// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "platform/Host.h"

#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace pchess::app {

// Host + Presenter without a window: `--screenshot`, golden-image tests, scripted sessions and
// the device self-test. Timers only fire when advance() is called (deterministic).
class HeadlessHost final : public platform::Host, public platform::Power {
public:
    void post(std::function<void()> fn) override;
    platform::TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) override;
    void cancelTimer(platform::TimerId id) override;
    std::chrono::steady_clock::time_point now() const override { return std::chrono::steady_clock::time_point(now_); }
    void quit() override { quit_ = true; }
    platform::TextInput* textInput() override { return nullptr; }
    platform::Power& power() override { return *this; }
    std::string platformName() const override { return "headless"; }

    int batteryPercent() const override { return battery_; }
    bool charging() const override { return false; }
    void setBattery(int percent) { battery_ = percent; }

    // Runs posted functions and timers that are due after advancing the fake time.
    void advance(std::chrono::milliseconds ms = std::chrono::milliseconds(0));
    void runPosted();
    bool quitRequested() const { return quit_; }
    int backgroundActivity() const { return background_; }
    void setBackgroundActivity(bool active) override { background_ += active ? 1 : -1; }

private:
    std::mutex mutex_;
    std::vector<std::function<void()>> posted_;
    struct Timer {
        std::chrono::milliseconds due;
        std::function<void()> fn;
    };
    std::map<platform::TimerId, Timer> timers_;
    platform::TimerId nextId_ = 1;
    std::chrono::milliseconds now_{0};
    bool quit_ = false;
    int battery_ = 76;
    int background_ = 0;
};

class HeadlessPresenter final : public platform::Presenter {
public:
    explicit HeadlessPresenter(platform::DeviceProfile p = platform::DeviceProfile::verseProColor()) : profile_(std::move(p)) {}
    platform::DeviceProfile profile() const override { return profile_; }
    void present(const ui::Canvas&, const ui::PresentRequest& req) override { requests.push_back(req); }
    platform::InfoList info() const override { return {{"Presenter", "headless"}}; }

    std::vector<ui::PresentRequest> requests;

private:
    platform::DeviceProfile profile_;
};

} // namespace pchess::app
