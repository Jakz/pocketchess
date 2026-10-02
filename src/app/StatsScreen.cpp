// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/StatsScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "app/TacticsScreen.h"
#include "core/storage/ChessDb.h"

#include <algorithm>

namespace pchess::app {

using namespace ui;

namespace {

int pct(int part, int whole) { return whole > 0 ? (part * 100 + whole / 2) / whole : 0; }
std::string pctText(int part, int whole) { return std::to_string(pct(part, whole)) + "%"; }
std::string capitalized(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

StatsScreen::StatsScreen(Application& app) : Screen(app) {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 2;
    const Rect bar{pad, l.screen.h - kMinTouchPx - pad, l.screen.w - 2 * pad, kMinTouchPx};
    bodyRect_ = {pad * 2, l.status.bottom() + pad, l.screen.w - 4 * pad, bar.y - pad - l.status.bottom() - pad};
    const auto cells = splitRow(bar, 4, pad);  // [←] ◀ ▶ Menu
    backRect_ = cells[0];
    const char* labels[] = {"◀", "▶", "Menu"};
    for (int i = 0; i < 3; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        if (i < 2) b.hint = "page";
        b.rect = cells[static_cast<size_t>(i) + 1];
        nav_.push_back(b);
    }
    build();
}

void StatsScreen::build() {
    pages_.clear();
    storage::Database* db = app_.userDb();
    puzzles::Progress* pr = app_.progress();

    // Puzzles: the rating, its course, attempts (the mistakes have their own page).
    Page puzzles{"Puzzles", {}, {}, "No puzzle tried yet: Home → Puzzles → Train."};
    if (pr) {
        auto t = pr->totals(TacticsScreen::kMistakesPack, false);
        auto hist = pr->history(120);
        if (t && t->puzzles > 0) {
            int peak = pr->rating();
            if (hist)
                for (const auto& p : *hist) peak = std::max(peak, p.rating);
            puzzles.rows = {{"Rating", std::to_string(pr->rating())},
                            {"Highest", std::to_string(peak)},
                            {"Puzzles tried", std::to_string(t->puzzles)},
                            {"Solved, never failed", std::to_string(t->solvedFirst) + " (" + pctText(t->solvedFirst, t->puzzles) + ")"},
                            {"Attempts", std::to_string(t->attempts) + " · " + pctText(t->successes, t->attempts) + " solved"},
                            {"In review", std::to_string(t->inReview)},
                            {"Due now", std::to_string(app_.dueInPack(100000).size()) + " in this pack"}};
            if (hist && hist->size() > 1)
                for (auto it = hist->rbegin(); it != hist->rend(); ++it) puzzles.chart.push_back(it->rating);
        }
    }
    pages_.push_back(std::move(puzzles));

    // Play: totals, then one line per strength.
    Page play{"Play", {}, {}, "No finished game yet: Home → Play."};
    if (db) {
        auto st = storage::playStats(*db);
        auto by = storage::playStatsByStrength(*db);
        if (st && st->games > 0) {
            play.rows = {{"Games", std::to_string(st->games)},
                         {"Won · drawn · lost", std::to_string(st->won) + " · " + std::to_string(st->drawn) + " · " +
                                                    std::to_string(st->lost)},
                         {"Score", pctText(2 * st->won + st->drawn, 2 * st->games)},
                         {"By strength", ""}};
            if (by)
                for (const auto& s : *by)
                    play.rows.push_back({capitalized(s.strength.empty() ? "unknown" : s.strength),
                                         "+" + std::to_string(s.won) + " =" + std::to_string(s.drawn) + " −" +
                                             std::to_string(s.lost) + " · " + pctText(2 * s.won + s.drawn, 2 * s.games)});
        }
    }
    pages_.push_back(std::move(play));

    // Guess the move: totals and the last runs.
    Page guess{"Guess the move", {}, {}, "No game guessed yet: Games → a game → Menu → Guess the moves."};
    if (db) {
        auto st = storage::guessStats(*db);
        auto recent = storage::recentGuessRuns(*db, 6);
        if (st && st->guessed() > 0) {
            guess.rows = {{"Games", std::to_string(st->runs) + " · " + std::to_string(st->finished) + " to the end"},
                          {"Moves guessed", std::to_string(st->guessed())},
                          {"First try", std::to_string(st->first) + " (" + pctText(st->first, st->guessed()) + ")"},
                          {"Second try", std::to_string(st->second) + " (" + pctText(st->second, st->guessed()) + ")"},
                          {"Shown", std::to_string(st->missed) + " (" + pctText(st->missed, st->guessed()) + ")"},
                          {"Score", std::to_string(st->percent()) + "%"},
                          {"Last games", ""}};
            if (recent)
                for (const auto& r : *recent) {
                    const int n = r.a + r.b + r.c;
                    guess.rows.push_back({r.title, std::to_string((200 * r.a + 100 * r.b) / (2 * n)) + "%" +
                                                       (r.finished ? "" : " …")});
                }
        }
    }
    pages_.push_back(std::move(guess));

    // Opening practice: totals and the last openings.
    Page drill{"Opening practice", {}, {}, "No opening practised yet: Openings → a line → Practice."};
    if (db) {
        auto st = storage::drillStats(*db);
        auto recent = storage::recentDrillRuns(*db, 6);
        const int moves = st ? st->main + st->book + st->missed : 0;
        if (moves > 0) {
            drill.rows = {{"Drills", std::to_string(st->runs)},
                          {"Your moves", std::to_string(moves)},
                          {"Main move", std::to_string(st->main) + " (" + pctText(st->main, moves) + ")"},
                          {"Other book move", std::to_string(st->book) + " (" + pctText(st->book, moves) + ")"},
                          {"Missed", std::to_string(st->missed) + " (" + pctText(st->missed, moves) + ")"},
                          {"Last openings", ""}};
            if (recent)
                for (const auto& r : *recent)
                    drill.rows.push_back({r.title, pctText(r.a + r.b, r.a + r.b + r.c) + " in book · " +
                                                       (r.side == "black" ? "Black" : "White")});
        }
    }
    pages_.push_back(std::move(drill));

    // My mistakes: games checked, mistakes found, how the training goes.
    Page mine{"My mistakes", {}, {}, "No game checked yet: finish a game in Play, then Puzzles → My mistakes."};
    if (db) {
        auto st = storage::mistakeStats(*db);
        if (st && st->gamesChecked > 0) {
            mine.rows = {{"Games checked", std::to_string(st->gamesChecked)},
                         {"Mistakes found", std::to_string(st->mistakes)},
                         {"Per game", st->gamesChecked ? std::to_string(st->mistakes * 10 / st->gamesChecked / 10) + "." +
                                                             std::to_string(st->mistakes * 10 / st->gamesChecked % 10)
                                                       : "0"}};
            if (pr)
                if (auto t = pr->totals(TacticsScreen::kMistakesPack, true); t) {
                    mine.rows.push_back({"Tried", std::to_string(t->puzzles) + " of " + std::to_string(st->mistakes)});
                    mine.rows.push_back({"Solved, never failed", std::to_string(t->solvedFirst)});
                    mine.rows.push_back({"In review", std::to_string(t->inReview)});
                }
            mine.rows.push_back({"To solve now", std::to_string(TacticsScreen::mistakeQueue(app_).size())});
        }
    }
    pages_.push_back(std::move(mine));
    page_ = std::clamp(page_, 0, static_cast<int>(pages_.size()) - 1);
}

// --- Pages ---------------------------------------------------------------------------------------

void StatsScreen::showPage(int index) {
    index = std::clamp(index, 0, static_cast<int>(pages_.size()) - 1);
    if (index == page_) return;
    page_ = index;
    app_.invalidate(bodyRect_, RefreshHint::Page);
    app_.invalidate(app_.layout().status, RefreshHint::Content);
    for (const Button& b : nav_) app_.invalidate(b.rect, RefreshHint::Content);
}

void StatsScreen::turnPage(int delta) { showPage(page_ + delta); }

void StatsScreen::openMenu() {
    std::vector<std::string> labels;
    for (const Page& p : pages_) labels.push_back(p.title);
    labels.push_back("Back up my progress");
    labels.push_back(backMenuLabel(app_, *this));
    labels.push_back("Close");
    const int n = static_cast<int>(pages_.size());
    app_.push(std::make_unique<MessageDialog>(
        app_, "Stats", "", labels,
        [this, n](int i) {
            if (i >= 0 && i < n) showPage(i);
            else if (i == n) backUp();
            else if (i == n + 1) app_.pop();
        },
        true));
}

// Everything the statistics come from, plus settings and played games (ADR 0017).
void StatsScreen::backUp() {
    auto r = app_.backupUserData();
    if (!r) return app_.showMessage("No backup", r.error().message());
    app_.showMessage("Backed up", "Saved to exports/" + r->filename().string() +
                                      ". To restore it, copy that folder into imports/ and open Home → Import.");
}

// --- Drawing -------------------------------------------------------------------------------------

void StatsScreen::drawChart(Canvas& c, const Rect& r, const std::vector<int>& values) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    Font& small = app_.fonts().get(s.smallPx);
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    const int low = (*lo / 50) * 50, high = std::max(low + 50, (*hi + 49) / 50 * 50);
    const int labelW = small.measure("0000") + s.fontPx / 2;
    const Rect plot{r.x + labelW, r.y + small.ascent() / 2, r.w - labelW, r.h - small.lineHeight()};
    c.frame(plot, s.stroke, blend(t.background, t.ink, 120));
    small.draw(c, r.x, plot.y + small.ascent() / 2, std::to_string(high), t.ink);
    small.draw(c, r.x, plot.bottom(), std::to_string(low), t.ink);
    // One column per attempt (columns, not lines: crisp on e-ink).
    const int n = static_cast<int>(values.size());
    for (int i = 0; i < n; ++i) {
        const int x0 = plot.x + s.stroke + i * (plot.w - 2 * s.stroke) / n;
        const int x1 = plot.x + s.stroke + (i + 1) * (plot.w - 2 * s.stroke) / n;
        const int h = (values[static_cast<size_t>(i)] - low) * (plot.h - 2 * s.stroke) / (high - low);
        c.fill({x0, plot.bottom() - s.stroke - h, std::max(1, x1 - x0 - 1), h}, t.accent);
    }
    small.draw(c, plot.x, r.bottom(), "last " + std::to_string(n) + " rated puzzles", blend(t.background, t.ink, 170));
}

void StatsScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    const Page& p = pages_[static_cast<size_t>(page_)];
    drawStatusBar(app_, c, *this,
                  "Stats · " + p.title + " (" + std::to_string(page_ + 1) + "/" + std::to_string(pages_.size()) + ")",
                  batteryText(app_));
    if (clip.intersects(bodyRect_)) {
        Font& f = app_.fonts().get(s.fontPx);
        Font& bold = app_.fonts().get(s.fontPx, FontStyle::Bold);
        c.pushClip(bodyRect_);
        int y = bodyRect_.y;
        const int lineH = f.lineHeight() + s.fontPx / 3;
        if (p.rows.empty()) {
            drawLines(c, {bodyRect_.x, y, bodyRect_.w, bodyRect_.h}, f.wrap(p.empty, bodyRect_.w), f, t.ink);
        }
        for (const auto& [label, value] : p.rows) {
            if (y + lineH > bodyRect_.bottom()) break;
            if (value.empty()) {  // a heading
                y += s.fontPx / 3;
                bold.draw(c, bodyRect_.x, y + bold.ascent(), fitText(bold, label, bodyRect_.w), t.ink);
            } else {
                const int vw = std::min(bold.measure(value), bodyRect_.w / 2);
                f.draw(c, bodyRect_.x, y + f.ascent(), fitText(f, label, bodyRect_.w - vw - s.fontPx), t.ink);
                bold.draw(c, bodyRect_.right() - vw, y + bold.ascent(), fitText(bold, value, vw), t.ink);
                c.hline(bodyRect_.x, bodyRect_.right(), y + lineH - s.fontPx / 6, 1, blend(t.background, t.ink, 60));
            }
            y += lineH;
        }
        if (p.chart.size() > 1 && bodyRect_.bottom() - y > s.fontPx * 6)
            drawChart(c, {bodyRect_.x, y + s.fontPx, bodyRect_.w, bodyRect_.bottom() - y - s.fontPx * 2}, p.chart);
        c.popClip();
    }
    drawBackButton(app_, c, *this, backRect_);
    for (Button b : nav_) {
        if (b.id == BtnPrev) b.enabled = page_ > 0;
        if (b.id == BtnNext) b.enabled = page_ + 1 < static_cast<int>(pages_.size());
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
    }
}

// --- Input ---------------------------------------------------------------------------------------

void StatsScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    if (backRect_.contains(g.start)) return app_.pop();
    switch (hitTest(nav_, g.start)) {
    case BtnPrev: turnPage(-1); break;
    case BtnNext: turnPage(1); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void StatsScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: turnPage(-1); break;
    case KeyCode::Next: case KeyCode::Right: turnPage(1); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

} // namespace pchess::app
