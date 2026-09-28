// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Desktop backend (Windows first; any SDL3 platform works): window, event loop, e-ink
// simulator. The only place that includes SDL headers besides Main.cpp.

#include "platform/Host.h"
#include "platform/sdl/EinkSimulator.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace pchess::app {
class Application;
}

namespace pchess::platform::sdl {

class SdlPresenter;

class SdlHost final : public Host, public Power {
public:
    SdlHost();
    ~SdlHost() override;

    // Creates the window for `profile` at `scale` (0 = fit the primary display).
    bool init(const DeviceProfile& profile, double scale, std::string& error);
    int run(app::Application& app);

    void post(std::function<void()> fn) override;
    TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) override;
    void cancelTimer(TimerId id) override;
    void quit() override;
    TextInput* textInput() override { return nullptr; }  // on-canvas dialog
    Power& power() override { return *this; }
    InfoList systemInfo() const override;
    std::string platformName() const override;

    int batteryPercent() const override { return battery_; }
    bool charging() const override { return false; }

    SdlPresenter& presenter() { return *presenter_; }
    SDL_Window* window() const { return window_; }

private:
    void handleEvent(app::Application& app, const SDL_Event& e);
    void handleKey(app::Application& app, const SDL_KeyboardEvent& k);
    ui::Point toCanvas(float x, float y) const;
    void setScale(double scale);
    static Uint32 SDLCALL timerCallback(void* userdata, SDL_TimerID id, Uint32 interval);

    DeviceProfile profile_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<SdlPresenter> presenter_;
    double scale_ = 1.0;
    Uint32 postEvent_ = 0;
    Uint32 timerEvent_ = 0;
    std::mutex postMutex_;
    std::deque<std::function<void()>> posted_;
    struct Timer {
        SDL_TimerID sdl = 0;
        std::function<void()> fn;
    };
    std::map<TimerId, Timer> timers_;
    TimerId nextTimer_ = 1;
    bool running_ = false;
    bool mouseDown_ = false;
    int battery_ = 76;
};

// Canvas -> e-ink simulator -> streaming texture; refresh visualization, counters in the title
// bar, optional full-refresh flash and latency emulation.
class SdlPresenter final : public Presenter {
public:
    SdlPresenter(SDL_Window* window, SDL_Renderer* renderer, DeviceProfile profile, SdlHost& host);
    ~SdlPresenter() override;

    DeviceProfile profile() const override { return profile_; }
    void present(const ui::Canvas& canvas, const ui::PresentRequest& req) override;
    InfoList info() const override;
    double lastPresentMs() const override { return lastMs_; }

    void repaint();  // redraw the window from the simulated panel (+ overlays)
    void cycleDisplayMode(const ui::Canvas& canvas);
    void toggleOverlay() { overlay_ = !overlay_; }
    void toggleFlash() { flash_ = !flash_; }
    void toggleLatency() { latency_ = !latency_; }
    EinkSimulator& simulator() { return sim_; }
    void updateTitle();

private:
    struct Overlay {
        ui::Rect rect;
        ui::RefreshMode mode;
        Uint64 until;
    };

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    DeviceProfile profile_;
    SdlHost& host_;
    EinkSimulator sim_;
    std::vector<Overlay> overlays_;
    bool overlay_ = true;
    bool flash_ = false;
    bool latency_ = false;
    int counts_[3] = {0, 0, 0};
    double lastMs_ = 0;
    TimerId repaintTimer_ = 0;
};

} // namespace pchess::platform::sdl
