// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/engine/UciProbe.h"
#include "ui/widgets/Widgets.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace pchess::app {

// Device spike and permanent diagnostics (M0.5/M0.6): system and canvas info, refresh latency
// tests (1 square, 12-square sequence, full), touch mapping, key codes, lifecycle events, and
// the engine probe (time to readyok, RSS, nodes/s, CPU-seconds). Everything is also logged to
// logs/pocketchess.log so results can be copied into docs/project-status.md.
class DiagnosticsScreen final : public Screen {
public:
    explicit DiagnosticsScreen(Application& app);
    ~DiagnosticsScreen() override;
    const char* name() const override { return "diagnostics"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onEnter() override;
    void onLeave() override;
    void onLifecycle(platform::Lifecycle l) override;

private:
    enum ButtonId { BtnOne, BtnTwelve, BtnFull, BtnTouch, BtnKeys, BtnEngine, BtnTiming, BtnBack };

    void log(const std::string& line);
    void runOneSquare();
    void runTwelve();
    void runFull();
    void runEngine();
    void drawTouchTargets(ui::Canvas& c);
    std::vector<ui::Point> touchTargets() const;
    ui::Rect testCell(int i) const;

    std::vector<ui::Button> buttons_;
    ui::Rect infoRect_, testRect_, logRect_;
    std::vector<std::string> info_;
    std::deque<std::string> log_;
    std::vector<bool> cells_;
    bool touchMode_ = false;
    int touchCount_ = 0;
    bool keyCapture_ = false;
    std::unique_ptr<engine::UciProbe> probe_;
    platform::TimerId probeTimer_ = 0;
};

} // namespace pchess::app
