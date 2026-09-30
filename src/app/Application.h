// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "app/Settings.h"
#include "core/engine/EngineManager.h"
#include "core/engine/EngineRegistry.h"
#include "core/games/GamePack.h"
#include "core/puzzles/Progress.h"
#include "core/puzzles/PuzzlePack.h"
#include "core/storage/Database.h"
#include "core/util/Clock.h"
#include "core/util/Paths.h"
#include "core/util/Random.h"
#include "core/util/Scheduler.h"
#include "platform/Host.h"
#include "ui/canvas/Canvas.h"
#include "ui/canvas/Font.h"
#include "ui/input/Input.h"
#include "ui/render/Refresh.h"
#include "ui/theme/BoardTheme.h"
#include "ui/theme/PieceSet.h"
#include "ui/widgets/Widgets.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pchess::app {

struct AppConfig {
    std::filesystem::path root;       // data root (util::Paths::resolveRoot)
    std::string startScreen = "home"; // home | analyze | diagnostics | play | tactics | games
    std::string fen;                  // start position for analyze
    std::optional<bool> mono;         // override Settings::mono
    std::string theme;                // override board theme by name
    std::filesystem::path fontDir;    // default: <root>/assets/fonts
    bool logToFile = true;
    // Tests and scripted sessions: engines added to the scanned engines/ folder, and how engine
    // objects are made (default: UciEngine over a real child process).
    std::vector<engine::EngineDescriptor> extraEngines;
    engine::EngineManager::EngineFactory engineFactory;
    // Load and save Settings in <root>/user/chess.db. Off for tests and headless screenshots, so
    // their output never depends on a previous run.
    bool persistSettings = true;
    // Time for review scheduling (unix seconds); nullptr = the system clock. Tests use a FakeClock.
    util::Clock* clock = nullptr;
};

// util::Scheduler over the platform host (engine subsystem -> UI thread).
class HostScheduler final : public util::Scheduler {
public:
    explicit HostScheduler(platform::Host& host) : host_(host) {}
    void post(std::function<void()> fn) override { host_.post(std::move(fn)); }
    TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) override {
        return host_.startTimer(delay, std::move(fn));
    }
    void cancelTimer(TimerId id) override { host_.cancelTimer(id); }
    std::chrono::steady_clock::time_point now() const override { return host_.now(); }
    void setBackgroundActivity(bool active) override { host_.setBackgroundActivity(active); }

private:
    platform::Host& host_;
};

// Screen areas for a portrait device (architecture doc §3): status bar, square board, panel.
struct Layout {
    ui::Rect screen;
    ui::Rect status;
    ui::Rect board;
    ui::Rect panel;
    int square = 0;
};

class Application {
public:
    Application(platform::Host& host, platform::Presenter& presenter, AppConfig config);
    ~Application();

    void start();
    void handleInput(const ui::InputEvent& e);
    void handleLifecycle(platform::Lifecycle l);
    // Redraws dirty regions and presents them (once per event).
    void flush();

    // --- Services for screens -----------------------------------------------------------------
    void invalidate(const ui::Rect& r, ui::RefreshHint hint);
    void invalidateAll(ui::RefreshHint hint = ui::RefreshHint::Screen);
    void push(std::unique_ptr<Screen> s);
    void pop();
    void replaceTop(std::unique_ptr<Screen> s);
    void popToRoot();
    Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
    bool wantsText() const { return top() && top()->wantsText(); }
    size_t depth() const { return stack_.size(); }

    // Text entry: native keyboard when the host has one, otherwise an on-canvas dialog.
    void requestText(const std::string& title, const std::string& initial,
                     std::function<void(std::optional<std::string>)> done);
    void showMessage(const std::string& title, const std::string& text);

    platform::Host& host() { return host_; }
    platform::Presenter& presenter() { return presenter_; }
    const platform::DeviceProfile& profile() const { return profile_; }
    const util::Paths& paths() const { return paths_; }
    const AppConfig& config() const { return config_; }
    Settings& settings() { return settings_; }
    void applySettings();  // theme/mono/refresh interval/engine settings changed
    void saveSettings();   // writes Settings to chess.db (no-op when not persisted)
    bool settingsPersisted() const { return userDb_.isOpen(); }
    // SessionState: screens call this after every meaningful change (move, undo); push/pop and
    // background/exit call it too. Saves the topmost resumable screen, or clears the session.
    void sessionChanged();
    // user/chess.db, or nullptr when settings are not persisted (tests) or the file failed to open.
    storage::Database* userDb() { return userDb_.isOpen() ? &userDb_ : nullptr; }
    util::Random& random() { return random_; }  // seeded from the clock at start (tests may reseed)

    // Tactics (M4): progress (user/progress.db, in memory when not persisted) and the puzzle pack
    // from content/puzzles/ (Settings -> Tactics -> Pack; nullptr when none is installed).
    puzzles::Progress* progress() { return progress_.get(); }
    puzzles::PuzzlePack* puzzlePack();
    std::vector<std::string> puzzlePackNames() const;  // file stems, sorted
    // Historical games (M5): content/games/<pack>.db (Settings -> Games -> Pack, else the largest).
    games::GamePack* gamePack();
    std::vector<std::string> gamePackNames() const;
    int64_t now() const;  // unix seconds for reviews: the clock plus Settings clock offset (device tests)

    // Engines (M2): registry of engines/*/engine.yaml and the job manager.
    engine::EngineRegistry& engineRegistry() { return engineRegistry_; }
    engine::EngineManager& engines() { return *engines_; }
    util::Scheduler& scheduler() { return scheduler_; }
    void rescanEngines();

    ui::Canvas& canvas() { return canvas_; }
    ui::FontLibrary& fonts() { return fonts_; }
    ui::PieceCache& pieces() { return pieces_; }
    const ui::BoardTheme& theme() const { return theme_; }
    const ui::UiStyle& style() const { return style_; }
    const Layout& layout() const { return layout_; }
    ui::RefreshPolicy& refreshPolicy() { return policy_; }

    // Present counters (simulator title bar, Diagnostics).
    struct Counters {
        int fast = 0, partial = 0, full = 0;
    };
    const Counters& counters() const { return counters_; }
    // Observer for every present (Diagnostics log).
    std::function<void(const ui::PresentRequest&, double ms)> onPresent;

private:
    std::unique_ptr<Screen> makeScreen(const std::string& name);
    std::vector<std::string> packNames(const char* kind) const;
    std::string choosePack(const char* kind, const std::string& wanted) const;
    void offerResume(std::map<std::string, std::string> session);
    std::unique_ptr<Screen> restoreScreen(const std::map<std::string, std::string>& session);
    void computeLayout();
    void drawRegion(const ui::Rect& r);

    platform::Host& host_;
    platform::Presenter& presenter_;
    AppConfig config_;
    platform::DeviceProfile profile_;
    util::Paths paths_;
    Settings settings_;
    HostScheduler scheduler_;
    engine::EngineRegistry engineRegistry_;
    std::unique_ptr<engine::EngineManager> engines_;
    storage::Database userDb_;  // user/chess.db (closed when persistence is off or failed)
    std::unique_ptr<puzzles::Progress> progress_;
    std::unique_ptr<puzzles::PuzzlePack> pack_;
    std::string packName_;  // the name pack_ was opened for
    std::unique_ptr<games::GamePack> gamePack_;
    std::string gamePackName_;
    util::Random random_;
    bool sessionReady_ = false;  // false until the Resume question is answered (keeps the old session)
    bool sessionEmpty_ = true;   // what the table holds now (skips redundant clears)

    ui::Canvas canvas_;
    ui::FontLibrary fonts_;
    ui::PieceCache pieces_;
    ui::BoardTheme theme_;
    ui::UiStyle style_;
    Layout layout_;

    ui::DirtyRegions dirty_;
    ui::RefreshHint strongest_ = ui::RefreshHint::Selection;
    ui::RefreshPolicy policy_;
    ui::GestureRecognizer gestures_;
    Counters counters_;

    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<std::unique_ptr<Screen>> graveyard_;  // popped during an event, destroyed after it
    bool started_ = false;
};

} // namespace pchess::app
