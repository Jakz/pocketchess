// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/DiagnosticsScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "core/util/Clock.h"
#include "core/util/Log.h"
#include "ui/canvas/PngWriter.h"

#include <cstdio>

namespace pchess::app {

using namespace ui;
using util::Log;

namespace {
std::string fmt(double v, int decimals = 1) {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}
} // namespace

DiagnosticsScreen::DiagnosticsScreen(Application& app) : Screen(app) {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 2;
    const Rect body{pad, l.status.bottom() + pad, l.screen.w - 2 * pad, l.screen.h - l.status.bottom() - 2 * pad};

    const int small = app_.fonts().get(s.smallPx).lineHeight();
    infoRect_ = {body.x, body.y, body.w, small * 15};
    testRect_ = {body.x, infoRect_.bottom() + pad, body.w, l.square > 0 ? l.square : 100};
    const int btnH = kMinTouchPx;
    const Rect grid{body.x, testRect_.bottom() + pad, body.w, 2 * btnH + pad};
    logRect_ = {body.x, grid.bottom() + pad, body.w, body.bottom() - grid.bottom() - pad};

    const char* labels[] = {"1 square", "12 squares", "Full", "Touch", "Keys", "Engine", "Timing log", "Bench"};
    const auto rows = splitColumn(grid, 2, pad);
    for (int r = 0; r < 2; ++r) {
        const auto cells = splitRow(rows[static_cast<size_t>(r)], 4, pad);
        for (int c = 0; c < 4; ++c) {
            Button b;
            b.id = r * 4 + c;
            b.label = labels[b.id];
            b.rect = cells[static_cast<size_t>(c)];
            buttons_.push_back(b);
        }
    }
    cells_.assign(12, false);

    // Static info (host + presenter + app).
    const auto& p = app_.profile();
    info_.push_back("Platform: " + app_.host().platformName() + "  Profile: " + p.name + " " + std::to_string(p.width) +
                    "x" + std::to_string(p.height) + (p.color ? " color" : " mono"));
    for (const auto& [k, v] : app_.host().systemInfo()) info_.push_back(k + ": " + v);
    for (const auto& [k, v] : app_.presenter().info()) info_.push_back(k + ": " + v);
    info_.push_back("Root: " + app_.paths().root().string());
    info_.push_back("Piece cache: " + std::to_string(app_.pieces().bytes() / 1024) + " KiB, board square " +
                    std::to_string(l.square) + " px");
    for (const auto& e : app_.engineRegistry().entries())
        info_.push_back("Engine " + e.descriptor.id + ": " +
                        (e.available ? "available (" + e.descriptor.name + ")" : "disabled - " + e.problem));
    if (app_.engineRegistry().entries().empty())
        info_.push_back("Engines: none in " + app_.paths().engines().string());
    info_.push_back(std::string("Power profile: ") + app_.engines().profile().name + " (Settings), engine platform " +
                    app_.engineRegistry().platform());
    for (const auto& line : info_) Log::info("diag", line);
}

DiagnosticsScreen::~DiagnosticsScreen() {
    if (probeTimer_) app_.host().cancelTimer(probeTimer_);
    if (benchActivity_) app_.host().setBackgroundActivity(false);
    bench_.reset();
    app_.onPresent = nullptr;
}

void DiagnosticsScreen::onEnter() {
    app_.onPresent = [this](const PresentRequest& req, double ms) {
        Rect bb;
        for (const Rect& r : req.rects) bb = bb.united(r);
        const double panelMs = app_.presenter().lastPresentMs();
        Log::info("diag", std::string("present ") + toString(req.mode) + " " + std::to_string(bb.w) + "x" +
                              std::to_string(bb.h) + " app " + fmt(ms) + " ms, panel call " + fmt(panelMs) + " ms");
    };
}

void DiagnosticsScreen::onLeave() {
    app_.onPresent = nullptr;
    if (probe_ && probe_->running()) probe_->abort("left Diagnostics");
    stopBenchmark("left Diagnostics");
}

void DiagnosticsScreen::onLifecycle(platform::Lifecycle l) {
    log(std::string("lifecycle: ") + platform::toString(l));
    if (l == platform::Lifecycle::Background || l == platform::Lifecycle::Sleep || l == platform::Lifecycle::Exit) {
        if (probe_ && probe_->running()) probe_->abort(std::string("lifecycle ") + platform::toString(l));
        stopBenchmark(std::string("lifecycle ") + platform::toString(l));
    }
}

void DiagnosticsScreen::log(const std::string& line) {
    Log::info("diag", line);
    log_.push_back(line);
    const int lh = app_.fonts().get(app_.style().smallPx).lineHeight();
    const size_t maxLines = static_cast<size_t>(std::max(1, logRect_.h / lh));
    while (log_.size() > maxLines) log_.pop_front();
    app_.invalidate(logRect_, RefreshHint::Content);
}

Rect DiagnosticsScreen::testCell(int i) const {
    const int w = testRect_.w / 12;
    return {testRect_.x + i * w, testRect_.y, w, testRect_.h};
}

void DiagnosticsScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    drawStatusBar(app_, c, *this, keyCapture_ ? "Diagnostics · keys" : "Diagnostics", batteryText(app_));
    Font& small = app_.fonts().get(s.smallPx);
    if (clip.intersects(infoRect_)) {
        std::vector<std::string> lines = info_;
        const auto& k = app_.counters();
        lines.push_back("Presents: fast " + std::to_string(k.fast) + " / partial " + std::to_string(k.partial) +
                        " / full " + std::to_string(k.full) + "   full every " +
                        std::to_string(app_.refreshPolicy().fullEvery()));
        const engine::EngineTotals& et = app_.engines().totals();
        lines.push_back("Engine jobs: " + std::to_string(et.jobs) + " (" + std::to_string(et.failures) + " failed), " +
                        std::to_string(et.starts) + " starts, CPU " + fmt(et.cpuSeconds, 1) + " s, wall " +
                        fmt(et.wallMs / 1000.0, 1) + " s, " + std::to_string(et.nodes / 1000) + "k nodes, peak " +
                        std::to_string(et.peakRssKb / 1024) + " MiB" +
                        (app_.engines().processRunning() ? ", running" : ""));
        drawLines(c, infoRect_, lines, small, t.ink);
    }
    for (int i = 0; i < 12; ++i) {
        const Rect r = testCell(i);
        if (!r.intersects(clip)) continue;
        c.fill(r, cells_[static_cast<size_t>(i)] ? t.ink : ((i & 1) ? t.dark : t.light));
    }
    for (const Button& b : buttons_) {
        Button bb = b;
        if (b.id == BtnKeys && keyCapture_) bb.primary = true;
        if (b.id == BtnTiming && Log::timingEnabled()) bb.primary = true;
        if (b.id == BtnBench && bench_ && bench_->running()) bb.primary = true;
        if (b.rect.intersects(clip)) drawButton(c, bb, t, app_.fonts(), s);
    }
    if (clip.intersects(logRect_)) {
        c.frame(logRect_, 1, t.frame);
        drawLines(c, logRect_.inset(s.stroke * 2), std::vector<std::string>(log_.begin(), log_.end()), small, t.ink);
    }
    if (touchMode_) drawTouchTargets(c);
}

std::vector<Point> DiagnosticsScreen::touchTargets() const {
    const Rect b = app_.layout().screen;
    const int m = app_.profile().mm(6);
    return {{b.x + m, b.y + m}, {b.right() - m, b.y + m}, {b.center()}, {b.x + m, b.bottom() - m}, {b.right() - m, b.bottom() - m}};
}

void DiagnosticsScreen::drawTouchTargets(Canvas& c) {
    const BoardTheme& t = app_.theme();
    const int arm = app_.profile().mm(4);
    c.fill(c.clip(), t.background);
    Font& f = app_.fonts().get(app_.style().fontPx, FontStyle::Bold);
    f.drawCentered(c, {0, app_.layout().screen.h / 3, app_.layout().screen.w, f.lineHeight()},
                   "Tap the 5 crosses (" + std::to_string(touchCount_) + "/5)", t.ink);
    for (const Point& p : touchTargets()) {
        c.fill({p.x - arm, p.y - 1, 2 * arm, 3}, t.ink);
        c.fill({p.x - 1, p.y - arm, 3, 2 * arm}, t.ink);
        c.ring(p.x, p.y, arm / 2, arm / 2 - 3, t.ink);
    }
}

void DiagnosticsScreen::runOneSquare() {
    cells_[0] = !cells_[0];
    app_.invalidate(testCell(0), RefreshHint::Content);
    app_.flush();
    log("1 square: panel call " + fmt(app_.presenter().lastPresentMs()) + " ms");
}

void DiagnosticsScreen::runTwelve() {
    double total = 0, worst = 0;
    for (int i = 0; i < 12; ++i) {
        cells_[static_cast<size_t>(i)] = !cells_[static_cast<size_t>(i)];
        app_.invalidate(testCell(i), RefreshHint::Content);
        app_.flush();
        const double ms = app_.presenter().lastPresentMs();
        total += ms;
        worst = std::max(worst, ms);
    }
    log("12 squares: total " + fmt(total) + " ms, avg " + fmt(total / 12) + ", worst " + fmt(worst));
}

void DiagnosticsScreen::runFull() {
    app_.invalidateAll(RefreshHint::Screen);
    app_.flush();
    log("full update: panel call " + fmt(app_.presenter().lastPresentMs()) + " ms");
}

void DiagnosticsScreen::runEngine() {
    if (probe_ && probe_->running()) {
        log("engine probe already running");
        return;
    }
    if (bench_ && bench_->running()) {
        log("engine benchmark running; tap Bench to cancel it first");
        return;
    }
    // The analysis engine from the registry (engines/<id>/engine.yaml), else the stockfish default path.
    std::filesystem::path exe;
    std::vector<std::string> args;
    const engine::EngineDescriptor* d = app_.engines().engineFor(engine::Role::Analysis);
    if (!d)
        if (const auto* e = app_.engineRegistry().find("stockfish")) d = &e->descriptor;
    if (d) {
        exe = d->binaryPath(app_.engineRegistry().platform());
        args = d->args;
    }
    if (exe.empty()) {
        const std::string exeName = app_.host().platformName() == "windows" ? "stockfish.exe" : "stockfish";
        exe = app_.paths().engines() / "stockfish" / exeName;
    }
    app_.engines().shutdownEngine();  // one engine process at a time
    log("engine probe: " + exe.string());
    auto& host = app_.host();
    probe_ = std::make_unique<engine::UciProbe>(
        [&host](std::function<void()> fn) { host.post(std::move(fn)); },
        [this](const engine::UciProbe::Result& r) {
            app_.host().setBackgroundActivity(false);
            if (probeTimer_) {
                app_.host().cancelTimer(probeTimer_);
                probeTimer_ = 0;
            }
            if (!r.ok) {
                log("engine: FAILED - " + r.error);
            } else {
                log("engine: " + r.engineName + ", " + std::to_string(r.optionCount) + " options");
                log("  uciok " + fmt(r.spawnToUciOkMs, 0) + " ms, readyok " + fmt(r.readyOkMs, 0) + " ms, RSS " +
                    std::to_string(r.rssAfterLoadKb / 1024) + " MiB");
                log("  go 1000 ms: depth " + std::to_string(r.depth) + ", " + std::to_string(r.nodes) + " nodes, " +
                    std::to_string(r.nps / 1000) + " knps, best " + r.bestMove);
                log("  wall " + fmt(r.searchWallMs, 0) + " ms, CPU " + fmt(r.cpuSeconds, 2) + " s, peak RSS " +
                    std::to_string(r.peakRssKb / 1024) + " MiB");
            }
            app_.flush();
        },
        [this](const std::string& line) {
            if (line.rfind("option", 0) != 0) log("  < " + line.substr(0, 80));
            app_.flush();
        });
    probeTimer_ = host.startTimer(std::chrono::seconds(20), [this] {
        probeTimer_ = 0;
        if (probe_) probe_->abort("timeout (20 s)");
        app_.flush();
    });
    host.setBackgroundActivity(true);
    probe_->start(exe, 1000, 1, 16, args);
}

void DiagnosticsScreen::runBenchmark() {
    if (bench_ && bench_->running()) {
        stopBenchmark("cancelled");
        return;
    }
    if (probe_ && probe_->running()) {
        log("engine probe running; try again when it is done");
        return;
    }
    std::vector<engine::EngineDescriptor> engines;
    for (const auto& e : app_.engineRegistry().entries())
        if (e.available) engines.push_back(e.descriptor);
    if (engines.empty()) {
        log("bench: no engine installed in " + app_.paths().engines().string());
        return;
    }
    app_.engines().shutdownEngine();  // one engine process at a time
    const engine::PowerProfileId profile = app_.engines().settings().profile;
    bench_ = std::make_unique<engine::EngineBenchmark>(app_.scheduler(), std::move(engines), profile);
    benchRow_.clear();
    log("bench: " + std::to_string(bench_->totalSearches()) + " searches, profile " +
        engine::powerProfile(profile).name + " (tap Bench again to cancel)");
    app_.host().setBackgroundActivity(true);
    benchActivity_ = true;
    bench_->start(
        util::SystemClock::instance().unixSeconds(), app_.engineRegistry().platform(),
        [this](const std::string& what, int done, int total) {
            // "id level position": one log line per engine x level.
            const std::string row = what.substr(0, what.rfind(' '));
            if (row == benchRow_) return;
            benchRow_ = row;
            log("bench " + std::to_string(done) + "/" + std::to_string(total) + ": " + row);
            app_.flush();
        },
        [this](const engine::BenchReport& r) {
            app_.host().setBackgroundActivity(false);
            benchActivity_ = false;
            for (const engine::BenchEngine& e : r.engines) {
                log(e.name + ": ready " + std::to_string(e.startupMs) + " ms, peak " +
                    std::to_string(e.peakRssKb / 1024) + " MiB" + (e.error.empty() ? "" : ", " + e.error));
                for (const engine::BenchRow& row : e.rows)
                    if (row.searches > 0)
                        log("  " + row.strength + ": CPU " + fmt(row.cpuSeconds / std::max(1, row.searches), 2) +
                            " s/move, " + fmt(row.knps(), 0) + " knps, depth " + fmt(row.avgDepth(), 1));
            }
            const std::filesystem::path file = app_.paths().logs() / "engine-bench.yaml";
            const std::string text = engine::toYaml(r);
            if (auto w = ui::writeFile(file, std::vector<uint8_t>(text.begin(), text.end())); !w.ok())
                log("bench: cannot write " + file.string() + ": " + w.error().message());
            else
                log(std::string("bench ") + (r.cancelled ? "cancelled, partial report in " : "done: ") + file.string());
            app_.invalidate(buttons_[BtnBench].rect, RefreshHint::Content);
            app_.flush();
        });
    app_.invalidate(buttons_[BtnBench].rect, RefreshHint::Content);
}

void DiagnosticsScreen::stopBenchmark(const std::string& why) {
    if (!bench_ || !bench_->running()) return;
    log("bench: stopping (" + why + ")");
    bench_->cancel();  // the report still arrives (cancelled) and is written
}

void DiagnosticsScreen::onGesture(const Gesture& g) {
    if (touchMode_) {
        const auto targets = touchTargets();
        int best = 0;
        long bestD = -1;
        for (size_t i = 0; i < targets.size(); ++i) {
            const long dx = g.start.x - targets[i].x, dy = g.start.y - targets[i].y;
            const long d = dx * dx + dy * dy;
            if (bestD < 0 || d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        const Point t = targets[static_cast<size_t>(best)];
        log("tap " + std::to_string(g.start.x) + "," + std::to_string(g.start.y) + " target " + std::to_string(best + 1) +
            " error " + std::to_string(g.start.x - t.x) + "," + std::to_string(g.start.y - t.y));
        if (++touchCount_ >= 5) touchMode_ = false;
        app_.invalidateAll(touchMode_ ? RefreshHint::Content : RefreshHint::Screen);
        return;
    }
    if (g.type == Gesture::Type::LongPress) log("long press " + std::to_string(g.start.x) + "," + std::to_string(g.start.y));
    if (g.type == Gesture::Type::Drag)
        log("drag " + std::to_string(g.start.x) + "," + std::to_string(g.start.y) + " -> " + std::to_string(g.end.x) +
            "," + std::to_string(g.end.y));
    if (g.type != Gesture::Type::Tap) return;
    if (statusBackRect(app_).contains(g.start)) {
        app_.pop();
        return;
    }
    switch (hitTest(buttons_, g.start)) {
    case BtnOne: runOneSquare(); break;
    case BtnTwelve: runTwelve(); break;
    case BtnFull: runFull(); break;
    case BtnTouch:
        touchMode_ = true;
        touchCount_ = 0;
        log("touch test: tap the 5 crosses");
        app_.invalidateAll(RefreshHint::Screen);
        break;
    case BtnKeys:
        keyCapture_ = !keyCapture_;
        log(keyCapture_ ? "key capture ON: every button is only logged; tap Keys again to stop"
                        : "key capture OFF");
        app_.invalidateAll(RefreshHint::Content);
        break;
    case BtnEngine: runEngine(); break;
    case BtnTiming:
        Log::setTimingEnabled(!Log::timingEnabled());
        log(std::string("timing log ") + (Log::timingEnabled() ? "ON" : "OFF"));
        app_.invalidate(buttons_[BtnTiming].rect, RefreshHint::Content);
        break;
    case BtnBench: runBenchmark(); break;
    default:
        log("tap " + std::to_string(g.start.x) + "," + std::to_string(g.start.y));
        break;
    }
}

void DiagnosticsScreen::onKey(const InputEvent& e) {
    log(std::string("key ") + toString(e.key) + " raw " + std::to_string(e.rawCode) + (e.repeat ? " (repeat)" : ""));
    if (keyCapture_ || touchMode_) return;
    // Menu leaves too: the reader never delivers Back/Home (key tests use the key capture mode).
    if (e.key == KeyCode::Back || e.key == KeyCode::Home || e.key == KeyCode::Menu) app_.pop();
}

} // namespace pchess::app
