// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ImportScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/Fen.h"
#include "core/engine/EngineDescriptor.h"
#include "core/util/Files.h"
#include "core/util/Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace pchess::app {

using namespace ui;

ImportScreen::ImportScreen(Application& app) : Screen(app) {
    layout();
    rescan();
}

void ImportScreen::layout() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect navRect{pad, l.screen.h - pad - kMinTouchPx, l.screen.w - 2 * pad, kMinTouchPx};
    const auto cells = splitRow(navRect, 4, pad);
    const char* labels[] = {"◀", "▶", "Rescan", "Menu"};  // ◀ ▶ = the Prev/Next keys
    for (int i = 0; i < 4; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        if (i < 2) b.hint = "page";
        b.rect = cells[static_cast<size_t>(i)];
        nav_.push_back(b);
    }
    listRect_ = {pad, l.status.bottom() + pad, l.screen.w - 2 * pad, navRect.y - pad - l.status.bottom() - pad};
    rowH_ = std::max(kMinTouchPx, app_.fonts().get(s.fontPx).lineHeight() + app_.fonts().get(s.smallPx).lineHeight() +
                                      s.fontPx / 2);
    rowsPerPage_ = std::max(1, listRect_.h / rowH_);
}

int ImportScreen::pageCount() const {
    return std::max(1, (static_cast<int>(items_.size()) + rowsPerPage_ - 1) / rowsPerPage_);
}

void ImportScreen::rescan() {
    const auto t0 = std::chrono::steady_clock::now();
    InstalledNames installed;
    installed.theme = [this](const std::string& n) { return app_.themes().find(n) != nullptr; };
    installed.pieceSet = [this](const std::string& n) { return app_.pieceSets().find(n) != nullptr; };
    installed.engine = [this](const std::string& id) { return app_.engineRegistry().find(id) != nullptr; };
    installed.pack = [this](const std::string& kind, const std::string& n) {
        const auto names = kind == "games" ? app_.gamePackNames() : app_.puzzlePackNames();
        return std::find(names.begin(), names.end(), n) != names.end();
    };
    items_ = Importer(app_.paths(), engine::currentPlatformKey()).scan(installed);
    util::Log::info("import", "scan " +
                                  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     std::chrono::steady_clock::now() - t0)
                                                     .count()) +
                                  " ms");
    page_ = std::min(page_, pageCount() - 1);
    showPage();
}

void ImportScreen::showPage() {
    for (Button& b : nav_) {
        if (b.id == BtnPrev) b.enabled = page_ > 0;
        if (b.id == BtnNext) b.enabled = page_ + 1 < pageCount();
    }
    app_.invalidate(listRect_, RefreshHint::Page);
    app_.invalidate(app_.layout().status, RefreshHint::Content);
    for (const Button& b : nav_) app_.invalidate(b.rect, RefreshHint::Content);
}

// --- Drawing -------------------------------------------------------------------------------------

void ImportScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    const std::string right = items_.empty() ? "" : std::to_string(page_ + 1) + "/" + std::to_string(pageCount());
    drawStatusBar(app_, c, *this, "Import", right);
    for (const Button& b : nav_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
    if (!clip.intersects(listRect_)) return;
    Font& body = app_.fonts().get(s.fontPx);
    Font& small = app_.fonts().get(s.smallPx);
    const Rgb dim = blend(t.background, t.ink, 160);
    if (items_.empty()) {
        // "<root folder>/imports" as seen over USB (the full path has no break points to wrap at).
        const std::string folder = app_.paths().imports().parent_path().filename().string() + "/imports";
        const std::string text = "Nothing to import. Copy files into the " + folder +
                                 " folder over USB, then tap Rescan:\n"
                                 "• .pgn games (open in Analyze, or make a game pack)\n"
                                 "• .fen positions\n"
                                 "• .db game or puzzle packs\n"
                                 "• .yaml board themes\n"
                                 "• piece set folders (pieceset.yaml + 12 SVGs)\n"
                                 "• engine folders (engine.yaml + ARM binary)";
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            for (auto& w : small.wrap(text.substr(start, end - start), listRect_.w - s.fontPx)) lines.push_back(w);
            start = end + 1;
        }
        drawLines(c, listRect_.inset(s.fontPx / 2), lines, small, dim);
        return;
    }
    c.pushClip(listRect_);
    const int pad = s.fontPx / 2;
    const size_t first = static_cast<size_t>(page_ * rowsPerPage_);
    for (size_t i = first; i < items_.size() && i < first + static_cast<size_t>(rowsPerPage_); ++i) {
        const ImportItem& it = items_[i];
        const Rect row{listRect_.x, listRect_.y + static_cast<int>(i - first) * rowH_, listRect_.w, rowH_};
        const std::string kind = toString(it.kind);
        const int kindW = small.measure(kind);
        const int y1 = row.y + pad / 2 + body.ascent();
        body.draw(c, row.x + pad, y1, fitText(body, it.path.filename().string(), row.w - kindW - 3 * pad),
                  it.valid ? t.ink : dim);
        small.draw(c, row.right() - pad - kindW, y1, kind, dim);
        const std::string line2 = (it.valid ? "" : "✗ ") + it.detail;
        small.draw(c, row.x + pad, y1 + body.descent() + small.ascent() + pad / 3, fitText(small, line2, row.w - 2 * pad),
                   dim);
        c.fill({row.x, row.bottom() - 1, row.w, 1}, blend(t.background, t.frame, 90));
    }
    c.popClip();
}

// --- Actions -------------------------------------------------------------------------------------

void ImportScreen::openInAnalyze(const ImportItem& item) {
    if (item.kind == ImportKind::Pgn) {
        auto screen = std::make_unique<AnalyzeScreen>(app_, chess::GameTree());
        AnalyzeScreen* analyze = screen.get();
        app_.push(std::move(screen));
        analyze->loadPgnFile(item.path);
        return;
    }
    // FEN: one position per line.
    auto text = util::readTextFile(item.path, 1u << 20);
    if (!text) return app_.showMessage("Cannot read the file", text.error().message());
    auto fens = std::make_shared<std::vector<std::string>>();
    size_t start = 0;
    while (start <= text->size()) {
        size_t end = text->find('\n', start);
        if (end == std::string::npos) end = text->size();
        std::string line = text->substr(start, end - start);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (!line.empty() && chess::parseFen(line)) fens->push_back(line);
        start = end + 1;
    }
    if (fens->empty()) return app_.showMessage("No positions", "The file has no valid FEN line.");
    if (fens->size() == 1) return app_.push(std::make_unique<AnalyzeScreen>(app_, fens->front()));
    chooseFromList(app_, item.path.filename().string(), *fens, [this, fens](int i) {
        if (i >= 0) app_.push(std::make_unique<AnalyzeScreen>(app_, (*fens)[static_cast<size_t>(i)]));
    });
}

void ImportScreen::install(size_t index) {
    if (index >= items_.size()) return;
    const ImportItem item = items_[index];
    auto r = Importer(app_.paths(), engine::currentPlatformKey()).install(item);
    if (!r) return app_.showMessage("Not installed", r.error().message());
    app_.reloadInstalled();
    rescan();
    std::string where;
    switch (item.kind) {
    case ImportKind::PieceSet: where = "Choose it in Settings → Display → Piece set."; break;
    case ImportKind::Theme: where = "Choose it in Settings → Display."; break;
    case ImportKind::Engine: where = "Choose it in Settings → Engine."; break;
    case ImportKind::GamePack: where = "Games → Menu → Pack… shows it."; break;
    case ImportKind::PuzzlePack: where = "Settings → Tactics → Pack shows it."; break;
    default: break;
    }
    app_.showMessage("Installed", std::string(toString(item.kind)) + " “" + item.name + "” installed. " + where);
}

void ImportScreen::makeGamePack(size_t index) {
    if (index >= items_.size()) return;
    const ImportItem item = items_[index];
    const auto t0 = std::chrono::steady_clock::now();
    auto r = Importer(app_.paths(), engine::currentPlatformKey()).makeGamePack(item, app_.openings());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    util::Log::info("import", "game pack from " + item.path.filename().string() + " in " + std::to_string(ms) + " ms");
    if (!r) return app_.showMessage("No game pack", r.error().message());
    rescan();
    app_.showMessage("Game pack ready", "“" + r->stem().string() + "” is in Games → Menu → Pack….");
}

void ImportScreen::choose(size_t index) {
    if (index >= items_.size()) return;
    const ImportItem& it = items_[index];
    const std::string title = it.path.filename().string();
    if (!it.valid) return app_.showMessage(title, it.detail);
    std::vector<std::string> labels;
    std::vector<int> actions;  // 0 open, 1 game pack, 2 install
    if (it.kind == ImportKind::Pgn || it.kind == ImportKind::Fen) {
        labels.push_back("Open in Analyze");
        actions.push_back(0);
    }
    if (it.kind == ImportKind::Pgn) {
        labels.push_back("Make a game pack");
        actions.push_back(1);
    }
    if (it.kind != ImportKind::Pgn && it.kind != ImportKind::Fen) {
        labels.push_back("Install");
        actions.push_back(2);
    }
    labels.push_back("Close");
    const std::string text = std::string(toString(it.kind)) + (it.name.empty() ? "" : " “" + it.name + "”") + ": " + it.detail;
    app_.push(std::make_unique<MessageDialog>(
        app_, title, text, labels,
        [this, index, actions](int i) {
            if (i < 0 || i >= static_cast<int>(actions.size()) || index >= items_.size()) return;
            switch (actions[static_cast<size_t>(i)]) {
            case 0: openInAnalyze(items_[index]); break;
            case 1: makeGamePack(index); break;
            case 2: install(index); break;
            }
        },
        true));
}

// --- Input ---------------------------------------------------------------------------------------

void ImportScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    if (statusBackRect(app_).contains(g.start)) return app_.pop();
    if (listRect_.contains(g.start) && rowH_ > 0) {
        const size_t row = static_cast<size_t>((g.start.y - listRect_.y) / rowH_);
        if (row < static_cast<size_t>(rowsPerPage_)) choose(static_cast<size_t>(page_ * rowsPerPage_) + row);
        return;
    }
    switch (hitTest(nav_, g.start)) {
    case BtnPrev: turnPage(-1); break;
    case BtnNext: turnPage(1); break;
    case BtnRescan: rescan(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void ImportScreen::turnPage(int delta) {
    const int p = std::clamp(page_ + delta, 0, pageCount() - 1);
    if (p == page_) return;
    page_ = p;
    showPage();
}

void ImportScreen::openMenu() {
    std::vector<std::string> labels;
    enum Action { Choose, Rescan, Back };
    std::vector<Action> actions;
    if (!items_.empty()) {  // key-only use: the reader's buttons are Prev, Next and Menu
        labels.push_back("Choose an item…");
        actions.push_back(Choose);
    }
    labels.push_back("Rescan imports/");
    actions.push_back(Rescan);
    labels.push_back(backMenuLabel(app_, *this));
    actions.push_back(Back);
    labels.push_back("Close");
    app_.push(std::make_unique<MessageDialog>(
        app_, "Import", "", labels,
        [this, actions](int i) {
            if (i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Choose: {
                std::vector<std::string> names;
                for (const ImportItem& it : items_) names.push_back(it.path.filename().string());
                chooseFromList(app_, "Import", names, [this](int k) {
                    if (k >= 0) choose(static_cast<size_t>(k));
                }, rowsPerPage_);
                break;
            }
            case Rescan: rescan(); break;
            case Back: app_.pop(); break;
            }
        },
        true));
}

void ImportScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: case KeyCode::Up: turnPage(-1); break;
    case KeyCode::Next: case KeyCode::Right: case KeyCode::Down: turnPage(1); break;
    case KeyCode::Ok: case KeyCode::Menu: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

} // namespace pchess::app
