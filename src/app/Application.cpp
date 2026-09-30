// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"

#include "app/AnalyzeScreen.h"
#include "app/DiagnosticsScreen.h"
#include "app/Dialogs.h"
#include "app/HomeScreen.h"
#include "app/PlayScreen.h"
#include "app/SettingsScreen.h"
#include "app/TacticsScreen.h"
#include "core/storage/ChessDb.h"
#include "core/util/Clock.h"
#include "core/util/Log.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace pchess::app {

using namespace pchess::ui;
using util::Log;

ui::Rect Screen::bounds() const { return app_.canvas().bounds(); }

Application::Application(platform::Host& host, platform::Presenter& presenter, AppConfig config)
    : host_(host), presenter_(presenter), config_(std::move(config)), profile_(presenter.profile()),
      paths_(config_.root), scheduler_(host), canvas_(profile_.width, profile_.height) {
    engines_ = std::make_unique<engine::EngineManager>(scheduler_, engineRegistry_, config_.engineFactory);
    dirty_.setBounds(canvas_.bounds());
    // Scale the UI with the panel resolution (tuned for 300 ppi).
    const double k = profile_.ppiMono / 300.0;
    style_.fontPx = static_cast<int>(34 * k + 0.5);
    style_.smallPx = static_cast<int>(26 * k + 0.5);
    style_.titlePx = static_cast<int>(44 * k + 0.5);
    style_.stroke = std::max(2, static_cast<int>(3 * k + 0.5));
    gestures_.setSlop(profile_.mm(2.0));
    computeLayout();
}

Application::~Application() {
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) (*it)->onLeave();
    stack_.clear();
    graveyard_.clear();
    engines_.reset();  // after the screens: they cancel their jobs on leave
}

void Application::rescanEngines() {
    engines_->shutdownEngine();
    engineRegistry_.scan(paths_.engines());
    for (const auto& d : config_.extraEngines) engineRegistry_.add(d);
}

void Application::computeLayout() {
    Layout& l = layout_;
    l.screen = canvas_.bounds();
    const int statusH = profile_.mm(5.4);
    const int minPanel = profile_.mm(26);
    int board = std::min(l.screen.w, l.screen.h - statusH - minPanel);
    l.square = board / 8;
    board = l.square * 8;
    l.status = {0, 0, l.screen.w, statusH};
    l.board = {(l.screen.w - board) / 2, statusH, board, board};
    l.panel = {0, statusH + board, l.screen.w, l.screen.h - statusH - board};
}

void Application::start() {
    paths_.ensureWritableDirs();
    if (config_.logToFile) Log::openFile(paths_.logs() / "pocketchess.log");
    Log::info("app", "PocketChess starting on " + host_.platformName() + ", profile " + profile_.name + " " +
                         std::to_string(profile_.width) + "x" + std::to_string(profile_.height) + ", root " +
                         paths_.root().string());

    const std::filesystem::path fontDir = config_.fontDir.empty() ? paths_.fonts() : config_.fontDir;
    if (auto r = fonts_.load(fontDir / "DejaVuSans.ttf", fontDir / "DejaVuSans-Bold.ttf"); !r)
        Log::error("app", "fonts: " + r.error().message() + " - text will not be drawn");
    if (fonts_.loaded()) {
        auto set = std::make_shared<GlyphPieceSet>(fonts_.face(), "DejaVu");
        if (!set->complete()) Log::warn("app", "piece glyphs incomplete in the bundled font");
        pieces_.setPieceSet(set);
    }

    if (config_.persistSettings) {
        // Content errors never stop the app (design notes §32): settings then stay in memory.
        if (auto db = storage::openChessDb(paths_.user() / "chess.db"); !db) {
            Log::error("app", "chess.db: " + db.error().message() + " - settings will not be saved");
        } else {
            userDb_ = std::move(*db);
            if (auto s = storage::loadSettings(userDb_); s)
                settings_.fromMap(*s);
            else
                Log::error("app", "settings: " + s.error().message());
            Log::info("app", "chess.db open (journal " + userDb_.journalMode() + ")");
        }
        if (auto p = puzzles::Progress::open(paths_.user() / "progress.db"); p)
            progress_ = std::make_unique<puzzles::Progress>(std::move(*p));
        else
            Log::error("app", "progress.db: " + p.error().message() + " - tactics progress will not be saved");
    }
    if (!progress_) {  // tests, or progress.db failed: keep tactics working for this session
        if (auto mem = storage::Database::open({}, storage::Database::Mode::Memory); mem)
            if (auto p = puzzles::Progress::fromDatabase(std::move(*mem)); p)
                progress_ = std::make_unique<puzzles::Progress>(std::move(*p));
    }
    // Command-line overrides apply to this session only.
    if (config_.mono) settings_.mono = *config_.mono;
    if (!config_.theme.empty()) {
        const BoardTheme t = BoardTheme::byName(config_.theme);
        if (t.mono) {
            settings_.mono = true;
            settings_.monoTheme = t.name;
        } else {
            settings_.boardTheme = t.name;
        }
    }
    // Seeded from the (injectable) clock: real runs vary, tests with a FakeClock are deterministic.
    random_.reseed(static_cast<uint64_t>(config_.clock ? config_.clock->unixSeconds()
                                                       : util::SystemClock::instance().unixSeconds()));
    rescanEngines();
    applySettings();

    push(makeScreen("home"));
    if (config_.startScreen != "home") {
        if (auto s = makeScreen(config_.startScreen)) push(std::move(s));
    }
    // SessionState (design notes §30): offer to resume; a start screen from the command line wins.
    std::map<std::string, std::string> session;
    if (userDb_.isOpen()) {
        if (auto s = storage::loadSession(userDb_); s) session = std::move(*s);
        else Log::error("app", "session: " + s.error().message());
    }
    sessionEmpty_ = session.empty();
    if (!session.empty() && config_.startScreen == "home") {
        offerResume(std::move(session));
    } else {
        sessionReady_ = true;
        sessionChanged();
    }
    started_ = true;
    invalidateAll(RefreshHint::Screen);
    flush();
}

void Application::offerResume(std::map<std::string, std::string> session) {
    auto it = session.find("summary");
    const std::string text = it != session.end() && !it->second.empty() ? it->second : "Continue where you left off?";
    Log::info("app", "session found: " + (session.count("screen") ? session["screen"] : std::string("?")));
    push(std::make_unique<MessageDialog>(*this, "Resume?", text, std::vector<std::string>{"Home", "Resume"},
                                         [this, session](int i) {
                                             if (i == 1) {
                                                 if (auto s = restoreScreen(session)) push(std::move(s));
                                                 else Log::warn("app", "session could not be restored; discarded");
                                             }
                                             sessionReady_ = true;
                                             sessionChanged();  // Home: clears the old session
                                         }));
}

std::unique_ptr<Screen> Application::restoreScreen(const std::map<std::string, std::string>& session) {
    auto it = session.find("screen");
    if (it == session.end()) return nullptr;
    if (it->second == "play") return PlayScreen::restore(*this, session);
    if (it->second == "analyze") return AnalyzeScreen::restore(*this, session);
    if (it->second == "tactics") return TacticsScreen::restore(*this, session);
    return nullptr;
}

void Application::sessionChanged() {
    if (!sessionReady_ || !userDb_.isOpen()) return;
    std::map<std::string, std::string> out;
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        std::map<std::string, std::string> m;
        if ((*it)->saveSession(m)) {
            m["screen"] = (*it)->name();
            out = std::move(m);
            break;
        }
    }
    if (out.empty()) {
        if (sessionEmpty_) return;
        if (auto r = storage::clearSession(userDb_); !r) Log::error("app", "session: " + r.error().message());
        else sessionEmpty_ = true;
        return;
    }
    if (auto r = storage::saveSession(userDb_, out); !r) Log::error("app", "session: " + r.error().message());
    else sessionEmpty_ = false;
}

std::unique_ptr<Screen> Application::makeScreen(const std::string& name) {
    if (name == "home") return std::make_unique<HomeScreen>(*this);
    if (name == "analyze") return std::make_unique<AnalyzeScreen>(*this, config_.fen);
    if (name == "diagnostics") return std::make_unique<DiagnosticsScreen>(*this);
    if (name == "settings") return std::make_unique<SettingsScreen>(*this);
    if (name == "tactics") return std::make_unique<TacticsScreen>(*this, TacticsScreen::Mode::Train);
    if (name == "review") return std::make_unique<TacticsScreen>(*this, TacticsScreen::Mode::Review);
    if (name == "play")
        return std::make_unique<PlayScreen>(*this, PlaySetup{chess::White, settings_.playStrength, config_.fen});
    Log::warn("app", "unknown or not yet available screen '" + name + "', staying on Home");
    return nullptr;
}

void Application::applySettings() {
    theme_ = settings_.mono ? BoardTheme::byName(settings_.monoTheme) : BoardTheme::byName(settings_.boardTheme);
    if (settings_.mono && !theme_.mono) theme_ = BoardTheme::classicMono();
    theme_.coordinates = settings_.coordinates;
    policy_.setFullEvery(settings_.fullRefreshEvery);

    engine::EngineSettings es = engines_->settings();
    es.profile = settings_.powerProfile;
    es.engineForRole[static_cast<size_t>(engine::Role::Analysis)] = settings_.analysisEngine;
    es.engineForRole[static_cast<size_t>(engine::Role::Play)] = settings_.playEngine;
    es.threads = settings_.threads > 0 ? std::optional<int>(settings_.threads) : std::nullopt;
    es.hashMb = settings_.hashMb > 0 ? std::optional<int>(settings_.hashMb) : std::nullopt;
    es.analysisLimit.reset();
    if (settings_.deepLimitSeconds > 0) {
        engine::SearchLimits l;
        l.movetimeMs = settings_.deepLimitSeconds * 1000;
        es.analysisLimit = l;
    } else if (settings_.deepLimitSeconds < 0) {
        engine::SearchLimits l;
        l.infinite = true;  // until the user stops it
        es.analysisLimit = l;
    }
    engines_->setSettings(es);
    if (started_) invalidateAll(RefreshHint::Screen);
}

std::vector<std::string> Application::packNames(const char* kind) const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(paths_.content() / kind, ec))
        if (de.is_regular_file(ec) && de.path().extension() == ".db") names.push_back(de.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}

// The wanted pack when installed, else the largest one (by file size: the most content).
std::string Application::choosePack(const char* kind, const std::string& wanted) const {
    const auto names = packNames(kind);
    if (std::find(names.begin(), names.end(), wanted) != names.end()) return wanted;
    std::string name;
    std::uintmax_t best = 0;
    std::error_code ec;
    for (const auto& n : names) {
        const auto size = std::filesystem::file_size(paths_.content() / kind / (n + ".db"), ec);
        if (!ec && size >= best) {
            best = size;
            name = n;
        }
    }
    return name;
}

std::vector<std::string> Application::puzzlePackNames() const { return packNames("puzzles"); }
std::vector<std::string> Application::gamePackNames() const { return packNames("games"); }

puzzles::PuzzlePack* Application::puzzlePack() {
    const std::string name = choosePack("puzzles", settings_.tacticsPack);
    if (name.empty()) return nullptr;
    if (pack_ && packName_ == name) return pack_.get();
    auto pack = puzzles::PuzzlePack::open(paths_.content() / "puzzles" / (name + ".db"));
    if (!pack) {
        Log::error("app", "puzzle pack " + name + ": " + pack.error().message());
        return nullptr;
    }
    pack_ = std::make_unique<puzzles::PuzzlePack>(std::move(*pack));
    packName_ = name;
    Log::info("app", "puzzle pack " + name + ": " + std::to_string(pack_->count()) + " puzzles");
    return pack_.get();
}

games::GamePack* Application::gamePack() {
    const std::string name = choosePack("games", settings_.gamesPack);
    if (name.empty()) return nullptr;
    if (gamePack_ && gamePackName_ == name) return gamePack_.get();
    auto pack = games::GamePack::open(paths_.content() / "games" / (name + ".db"));
    if (!pack) {
        Log::error("app", "game pack " + name + ": " + pack.error().message());
        return nullptr;
    }
    gamePack_ = std::make_unique<games::GamePack>(std::move(*pack));
    gamePackName_ = name;
    Log::info("app", "game pack " + name + ": " + std::to_string(gamePack_->count()) + " games");
    return gamePack_.get();
}

int64_t Application::now() const {
    const util::Clock& clock = config_.clock ? *config_.clock : util::SystemClock::instance();
    return clock.unixSeconds() + int64_t(settings_.clockOffsetDays) * 86400;
}

void Application::saveSettings() {
    if (!userDb_.isOpen()) return;
    if (auto r = storage::saveSettings(userDb_, settings_.toMap()); !r)
        Log::error("app", "saving settings: " + r.error().message());
}

void Application::invalidate(const Rect& r, RefreshHint hint) {
    if (r.empty()) return;
    dirty_.add(r);
    if (static_cast<int>(hint) > static_cast<int>(strongest_)) strongest_ = hint;
}

void Application::invalidateAll(RefreshHint hint) { invalidate(canvas_.bounds(), hint); }

void Application::push(std::unique_ptr<Screen> s) {
    if (!s) return;
    if (Screen* t = top()) t->onLeave();
    Screen* raw = s.get();
    stack_.push_back(std::move(s));
    Log::info("app", std::string("screen -> ") + raw->name());
    const Rect b = raw->bounds();
    invalidate(b, raw->opaque() || b.area() * 2 > canvas_.bounds().area() ? RefreshHint::Screen : RefreshHint::Content);
    raw->onEnter();
    sessionChanged();
}

void Application::pop() {
    if (stack_.size() <= 1) return;
    std::unique_ptr<Screen> s = std::move(stack_.back());
    stack_.pop_back();
    s->onLeave();
    const Rect b = s->bounds();
    invalidate(b, s->opaque() || b.area() * 2 > canvas_.bounds().area() ? RefreshHint::Screen : RefreshHint::Content);
    graveyard_.push_back(std::move(s));
    if (Screen* t = top()) t->onEnter();
    sessionChanged();
}

void Application::replaceTop(std::unique_ptr<Screen> s) {
    if (!stack_.empty()) {
        stack_.back()->onLeave();
        graveyard_.push_back(std::move(stack_.back()));
        stack_.pop_back();
    }
    push(std::move(s));
    invalidateAll(RefreshHint::Screen);
}

void Application::popToRoot() {
    while (stack_.size() > 1) pop();
    invalidateAll(RefreshHint::Screen);
}

void Application::requestText(const std::string& title, const std::string& initial,
                              std::function<void(std::optional<std::string>)> done) {
    if (platform::TextInput* ti = host_.textInput()) {
        ti->request(title, initial, std::move(done));
        return;
    }
    push(std::make_unique<TextEntryDialog>(*this, title, initial, std::move(done)));
}

void Application::showMessage(const std::string& title, const std::string& text) {
    push(std::make_unique<MessageDialog>(*this, title, text, std::vector<std::string>{"OK"}, nullptr));
}

void Application::handleInput(const InputEvent& e) {
    Screen* s = top();
    if (!s) return;
    switch (e.type) {
    case InputEvent::Type::Key:
        s->onKey(e);
        break;
    case InputEvent::Type::KeyRelease:
        break;
    case InputEvent::Type::Text:
        s->onText(e.text);
        break;
    default:
        if (auto g = gestures_.feed(e)) s->onGesture(*g);
        break;
    }
    flush();
}

void Application::handleLifecycle(platform::Lifecycle l) {
    Log::info("app", std::string("lifecycle ") + platform::toString(l));
    if (l == platform::Lifecycle::Background || l == platform::Lifecycle::Sleep || l == platform::Lifecycle::Exit)
        sessionChanged();  // before anything is torn down
    for (auto& s : stack_) s->onLifecycle(l);
    // Battery rules (P11): the engine stops at once on background, sleep and exit.
    using EL = engine::EngineManager::Lifecycle;
    switch (l) {
    case platform::Lifecycle::Background: engines_->onLifecycle(EL::Background); break;
    case platform::Lifecycle::Sleep: engines_->onLifecycle(EL::Sleep); break;
    case platform::Lifecycle::Exit: engines_->onLifecycle(EL::Exit); break;
    case platform::Lifecycle::Wake: engines_->onLifecycle(EL::Wake); break;
    case platform::Lifecycle::Foreground: engines_->onLifecycle(EL::Foreground); break;
    case platform::Lifecycle::Show: break;
    }
    switch (l) {
    case platform::Lifecycle::Show:
    case platform::Lifecycle::Foreground:
    case platform::Lifecycle::Wake:
        // The panel may have been redrawn by the system (sleep screen, library): redraw fully once.
        invalidateAll(RefreshHint::Screen);
        flush();
        break;
    default: break;
    }
}

void Application::drawRegion(const Rect& r) {
    // Draw from the top-most opaque screen upwards.
    size_t first = 0;
    for (size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i]->opaque()) {
            first = i;
            break;
        }
    }
    canvas_.pushClip(r);
    if (stack_.empty()) canvas_.fill(r, theme_.background);
    for (size_t i = first; i < stack_.size(); ++i) {
        const Rect b = stack_[i]->bounds();
        if (!b.intersects(r)) continue;
        canvas_.pushClip(b);
        stack_[i]->draw(canvas_, canvas_.clip());
        canvas_.popClip();
    }
    canvas_.popClip();
}

void Application::flush() {
    graveyard_.clear();
    if (dirty_.empty()) return;
    const RefreshHint hint = strongest_;
    strongest_ = RefreshHint::Selection;
    std::vector<Rect> rects = dirty_.take();
    int64_t area = 0;
    for (const Rect& r : rects) area += r.area();

    PresentRequest req;
    req.mode = policy_.decide(hint, area, canvas_.bounds().area());
    if (req.mode == RefreshMode::Full) {
        rects.assign(1, canvas_.bounds());
        req.fullScreen = true;
    }
    for (const Rect& r : rects) drawRegion(r);
    req.rects = std::move(rects);
    req.colorContent = !theme_.mono;

    const auto t0 = std::chrono::steady_clock::now();
    presenter_.present(canvas_, req);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    switch (req.mode) {
    case RefreshMode::Fast: ++counters_.fast; break;
    case RefreshMode::Partial: ++counters_.partial; break;
    case RefreshMode::Full: ++counters_.full; break;
    }
    if (Log::timingEnabled()) {
        Rect bb;
        for (const Rect& r : req.rects) bb = bb.united(r);
        Log::info("present", std::string(toString(req.mode)) + " rects=" + std::to_string(req.rects.size()) + " bbox=" +
                                 std::to_string(bb.x) + "," + std::to_string(bb.y) + " " + std::to_string(bb.w) + "x" +
                                 std::to_string(bb.h) + " " + std::to_string(ms) + " ms");
    }
    if (onPresent) onPresent(req, ms);
}

} // namespace pchess::app
