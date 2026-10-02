// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/HomeScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/DiagnosticsScreen.h"
#include "app/EndgameScreen.h"
#include "app/ImportScreen.h"
#include "app/MistakeScan.h"
#include "app/OpeningsScreen.h"
#include "app/Dialogs.h"
#include "app/GamesScreen.h"
#include "app/PlayScreen.h"
#include "app/SettingsScreen.h"
#include "app/StatsScreen.h"
#include "app/TacticsScreen.h"
#include "core/engine/EngineDescriptor.h"
#include "core/storage/ChessDb.h"
#include "core/util/Clock.h"

#include <algorithm>
#include <cctype>

namespace pchess::app {

using namespace ui;

namespace {
enum Id { Play, Tactics, Games, Openings, Analyze, Review, SettingsItem, Import, Diagnostics, Exit, StatsItem };
}

HomeScreen::HomeScreen(Application& app) : Screen(app) {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int margin = l.screen.w / 10;
    const int btnH = std::max(kMinTouchPx, l.screen.h / 12);
    const int gap = s.fontPx / 2;
    // The launcher sits at the bottom (one-hand use): 5 rows + the tools row end one gap above the
    // screen edge; title, status line and summary fill the top.
    const int top = l.screen.h - gap - 6 * (btnH + gap) + gap;
    struct Item {
        int id;
        const char* label;
        const char* hint;
        bool enabled;
    };
    const Item items[] = {
        {Play, "Play", "vs engine", true},
        {Tactics, "Puzzles", "train · review", true},
        {Games, "Games", "historical and your own", true},
        {Openings, "Openings", "named lines and variations", true},
        {Analyze, "Analyze", "", true},
    };
    int y = top;
    for (const Item& it : items) {
        Button b;
        b.id = it.id;
        b.label = it.label;
        b.hint = it.hint;
        b.enabled = it.enabled;
        b.rect = {margin, y, l.screen.w - 2 * margin, btnH};
        buttons_.push_back(b);
        y += btnH + gap;
    }
    // Less frequent destinations share one row.
    const Item tools[] = {
        {StatsItem, "Stats", "", true}, {SettingsItem, "Settings", "", true}, {Import, "Import", "", true},
        {Diagnostics, "Diagnostics", "", true}};
    const auto cells = splitRow({margin, y, l.screen.w - 2 * margin, btnH}, 4, gap);
    for (size_t i = 0; i < 4; ++i) {
        Button b;
        b.id = tools[i].id;
        b.label = tools[i].label;
        b.rect = cells[i];
        buttons_.push_back(b);
    }
    footer_ = {0, l.status.bottom() + s.titlePx * 4, l.screen.w, s.fontPx * 2};  // battery · version · engine
    const int summaryTop = footer_.bottom() + gap;
    summaryRect_ = {margin, summaryTop, l.screen.w - 2 * margin, top - gap - summaryTop};
    refreshSummary();
    for (size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i].id == Analyze) focus_ = static_cast<int>(i);
}

void HomeScreen::onEnter() {
    // Battery, engine and summary only update on screen entry (no animated status area, §6.1).
    refreshSummary();
    app_.invalidate(footer_, RefreshHint::Content);
    app_.invalidate(summaryRect_, RefreshHint::Content);
}

void HomeScreen::refreshSummary() {
    const engine::EngineDescriptor* e = app_.engines().engineFor(engine::Role::Analysis);
    engineName_ = e ? e->name : "none";
    summary_.clear();
    // Tactics (design notes §6.1): rating and due reviews, from cheap indexed queries.
    if (puzzles::Progress* pr = app_.progress()) {
        // Only reviews of the active pack: the others wait until their pack is chosen again.
        const int due = static_cast<int>(app_.dueInPack(100000).size());
        for (Button& b : buttons_)
            if (b.id == Tactics) b.hint = due > 0 ? "train · " + std::to_string(due) + " to review" : "train · review";
        auto hist = pr->history(1);
        if (hist && !hist->empty())
            summary_.push_back("Puzzle rating " + std::to_string(pr->rating()) + " · review due: " + std::to_string(due));
    }
    storage::Database* db = app_.userDb();
    if (!db) return;
    auto recent = storage::recentPlayedGames(*db, 1);
    auto stats = storage::playStats(*db);
    if (!recent || recent->empty()) return;
    const storage::PlayedGame& g = recent->front();
    std::string outcome = "played";
    if (g.result == "1/2-1/2") outcome = "drew";
    else if (!g.human.empty() && g.result != "*")
        outcome = (g.human == "white") == (g.result == "1-0") ? "won" : "lost";
    std::string line = "Last game: " + outcome;
    if (!g.human.empty()) line += std::string(" as ") + (g.human == "white" ? "White" : "Black");
    if (!g.strength.empty()) {
        std::string label = g.strength;
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        line += " · " + label;
    }
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const util::CivilTime d = util::civilFromUnix(g.date);
    line += " · " + std::to_string(d.day) + " " + months[(d.month - 1) % 12] + " " + std::to_string(d.year);
    summary_.push_back(line);
    if (stats && stats->games > 0)
        summary_.push_back("Games: " + std::to_string(stats->games) + " · won " + std::to_string(stats->won) +
                           " · drawn " + std::to_string(stats->drawn) + " · lost " + std::to_string(stats->lost));
}

Rect HomeScreen::focusRect(int index) const {
    const int s = app_.style().stroke;
    return buttons_[static_cast<size_t>(index)].rect.inset(-s * 4);
}

void HomeScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    drawStatusBar(app_, c, *this, "PocketChess", batteryText(app_));

    Font& big = app_.fonts().get(s.titlePx * 2, FontStyle::Bold);
    const int titleY = l.status.bottom() + s.titlePx * 2 + big.ascent() / 2;
    const std::string title = "♞ PocketChess";
    big.draw(c, (l.screen.w - big.measure(title)) / 2, titleY, title, t.ink);

    for (size_t i = 0; i < buttons_.size(); ++i) {
        drawButton(c, buttons_[i], t, app_.fonts(), s);
        if (static_cast<int>(i) == focus_) c.frame(focusRect(focus_), s.stroke * 2, t.frame);
    }

    Font& small = app_.fonts().get(s.smallPx);
    if (clip.intersects(summaryRect_)) {
        int y = summaryRect_.y;
        for (const std::string& line : summary_) {
            small.draw(c, (l.screen.w - small.measure(line)) / 2, y + small.ascent(), line, t.ink);
            y += small.lineHeight();
        }
    }
    const std::string battery = batteryText(app_);
    const std::string left = battery.empty() ? "" : "Battery " + battery;
    const std::string right = "Engine: " + engineName_;
    const int baseline = footer_.y + footer_.h / 2 + small.ascent() / 2;
    const int pad = s.fontPx;
    small.draw(c, pad, baseline, left, t.ink);
    small.draw(c, l.screen.w - pad - small.measure(right), baseline, right, t.ink);
    const std::string version = "0.0.1 · M7 preview";
    small.draw(c, (l.screen.w - small.measure(version)) / 2, baseline, version, blend(t.background, t.ink, 140));
}

void HomeScreen::open(int id) {
    switch (id) {
    case Play: startPlay(); break;
    case Tactics: openPuzzles(); break;
    case Games: app_.push(std::make_unique<GamesScreen>(app_)); break;
    case Openings: app_.push(std::make_unique<OpeningsScreen>(app_)); break;
    case Analyze: app_.push(std::make_unique<AnalyzeScreen>(app_, "")); break;
    case StatsItem: app_.push(std::make_unique<StatsScreen>(app_)); break;
    case SettingsItem: app_.push(std::make_unique<SettingsScreen>(app_)); break;
    case Diagnostics: app_.push(std::make_unique<DiagnosticsScreen>(app_)); break;
    case Import: app_.push(std::make_unique<ImportScreen>(app_)); break;
    default: break;
    }
}

HomeScreen::~HomeScreen() = default;

// Puzzles: training, the review of failed puzzles (spaced repetition) with how many are due, and the
// mistakes found in the user's own games.
void HomeScreen::openPuzzles() {
    const int due = static_cast<int>(app_.dueInPack(100000).size());
    const int mine = static_cast<int>(TacticsScreen::mistakeQueue(app_).size());
    int unchecked = 0;
    if (storage::Database* db = app_.userDb())
        if (auto g = storage::uncheckedPlayedGames(*db); g) unchecked = static_cast<int>(g->size());
    std::string mineHint = mine > 0 ? std::to_string(mine) + " to solve" : "from your Play games";
    if (unchecked > 0) mineHint += " · " + std::to_string(unchecked) + (unchecked == 1 ? " new game" : " new games");
    chooseFromList(
        app_, "Puzzles", {"Train", "Review", "My mistakes", "Endgames"},
        [this](int i) {
            if (i == 0) app_.push(std::make_unique<TacticsScreen>(app_, TacticsScreen::Mode::Train));
            if (i == 1) app_.push(std::make_unique<TacticsScreen>(app_, TacticsScreen::Mode::Review));
            if (i == 2) openMistakes();
            if (i == 3) EndgameScreen::choose(app_);
        },
        6,
        {"new puzzles at your rating", due > 0 ? std::to_string(due) + " failed puzzles due" : "nothing due", mineHint,
         "mates, pawn and rook endings vs the engine"});
}

// Games not checked yet go through the engine first (a progress dialog with Stop), then the
// mistakes open as puzzles. Without an engine, the mistakes already found still open.
void HomeScreen::openMistakes() {
    int unchecked = 0;
    if (storage::Database* db = app_.userDb())
        if (auto g = storage::uncheckedPlayedGames(*db); g) unchecked = static_cast<int>(g->size());
    auto openScreen = [this] { app_.push(std::make_unique<TacticsScreen>(app_, TacticsScreen::Mode::Mistakes)); };
    if (unchecked == 0 || !app_.engines().available(engine::Role::Hint)) return openScreen();
    scan_ = std::make_unique<MistakeScan>(app_);
    auto dialog = std::make_unique<MessageDialog>(app_, "Checking your games", "Starting the engine…\n ",
                                                  std::vector<std::string>{"Stop"}, [this](int) {
                                                      if (scan_) scan_->cancel();
                                                  });
    MessageDialog* d = dialog.get();
    app_.push(std::move(dialog));
    scan_->start(
        [this, d](const MistakeScan::Status& s) {
            if (app_.top() != d) return;
            d->setText("Game " + std::to_string(s.game) + " of " + std::to_string(s.games) + " · position " +
                       std::to_string(s.position) + " of " + std::to_string(s.positions) + "\n" +
                       std::to_string(s.found) + (s.found == 1 ? " mistake" : " mistakes") + " found so far");
        },
        [this, d, openScreen](const MistakeScan::Status&, bool) {
            if (app_.top() == d) app_.pop();  // finished: the dialog goes; stopped: Stop already closed it
            openScreen();
        });
}

void HomeScreen::startPlay() {
    // Games in progress first (most recently played), each with its start and last-played dates.
    std::vector<storage::ActiveGame> games;
    if (storage::Database* db = app_.userDb())
        if (auto g = storage::activeGames(*db); g) games = std::move(*g);
    if (games.empty()) return newGame();
    std::vector<std::string> labels, hints;
    for (const auto& g : games) {
        const int plies = g.moves.empty() ? 0 : static_cast<int>(std::count(g.moves.begin(), g.moves.end(), ' ')) + 1;
        std::string strength = g.strength;
        if (!strength.empty()) strength[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(strength[0])));
        labels.push_back((g.human == "white" ? "White" : "Black") + std::string(" vs ") + strength + " · move " +
                         std::to_string(plies / 2 + 1));
        hints.push_back("started " + app_.dateText(g.started) + " · last played " + app_.dateText(g.lastPlayed));
    }
    labels.push_back("New game…");
    hints.push_back("");
    chooseFromList(
        app_, "Play", labels,
        [this, games](int i) {
            if (i < 0) return;
            if (static_cast<size_t>(i) == games.size()) return newGame();
            if (auto p = PlayScreen::resume(app_, games[static_cast<size_t>(i)])) app_.push(std::move(p));
            else app_.showMessage("Cannot resume", "The saved game could not be read.");
        },
        5, hints);
}

void HomeScreen::newGame() {
    // Choice lists end with Close (chooseFromList): nothing forces a game to start.
    chooseFromList(
        app_, "Play as", {"White", "Black", "Random"},
        [this](int side) {
            if (side < 0) return;
            chess::Color human = side == 0 ? chess::White : chess::Black;
            if (side == 2) human = app_.random().below(2) == 0 ? chess::White : chess::Black;
            const std::vector<std::string>& labels = engine::strengthLabels();
            std::vector<std::string> texts;
            for (const std::string& l : labels) {
                std::string t = l;
                t[0] = static_cast<char>(t[0] - 'a' + 'A');
                texts.push_back((l == app_.settings().playStrength ? "✓ " : "") + t);
            }
            chooseFromList(
                app_, "Strength", texts,
                [this, human](int i) {
                    if (i < 0) return;
                    PlaySetup setup;
                    setup.human = human;
                    setup.strength = engine::strengthLabels()[static_cast<size_t>(i)];
                    if (app_.settings().playStrength != setup.strength) {
                        app_.settings().playStrength = setup.strength;
                        app_.saveSettings();
                    }
                    app_.push(std::make_unique<PlayScreen>(app_, setup));
                },
                8);
        });
}

void HomeScreen::moveFocus(int delta) {
    const int n = static_cast<int>(buttons_.size());
    int f = focus_;
    for (int k = 0; k < n; ++k) {
        f = (f + delta + n) % n;
        if (buttons_[static_cast<size_t>(f)].enabled) break;
    }
    if (f == focus_) return;
    app_.invalidate(focusRect(focus_), RefreshHint::Selection);
    focus_ = f;
    app_.invalidate(focusRect(focus_), RefreshHint::Selection);
}

void HomeScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    const int id = hitTest(buttons_, g.start);
    if (id >= 0) open(id);
}

void HomeScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Up: case KeyCode::Left: moveFocus(-1); break;
    case KeyCode::Next: case KeyCode::Down: case KeyCode::Right: moveFocus(1); break;
    case KeyCode::Ok: case KeyCode::Menu: open(buttons_[static_cast<size_t>(focus_)].id); break;
    case KeyCode::Back: app_.host().quit(); break;
    default: break;
    }
}

} // namespace pchess::app
