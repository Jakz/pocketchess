// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Dialogs.h"

#include "app/Application.h"
#include "ui/theme/Palette.h"

#include <algorithm>
#include <memory>

namespace pchess::app {

using namespace ui;

// --- Dialog -------------------------------------------------------------------------------------

void Dialog::drawFrame(Canvas& c, const std::string& title) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(rect_, t.background);
    c.frame(rect_, s.stroke * 2, t.frame);
    if (!title.empty()) {
        Font& f = app_.fonts().get(s.titlePx, FontStyle::Bold);
        titleH_ = f.lineHeight() + s.fontPx;
        f.drawCentered(c, {rect_.x, rect_.y + s.stroke * 2, rect_.w, titleH_}, title, t.ink);
        c.fill({rect_.x + s.fontPx, rect_.y + titleH_, rect_.w - 2 * s.fontPx, s.stroke}, t.frame);
    }
}

Rect Dialog::contentRect() const {
    const int pad = app_.style().fontPx;
    return {rect_.x + pad, rect_.y + titleH_ + pad / 2, rect_.w - 2 * pad, rect_.h - titleH_ - pad};
}

// --- MessageDialog ------------------------------------------------------------------------------

MessageDialog::MessageDialog(Application& app, std::string title, std::string text, std::vector<std::string> buttons,
                             std::function<void(int)> done, bool vertical, std::vector<std::string> hints)
    : Dialog(app), title_(std::move(title)), text_(std::move(text)), done_(std::move(done)) {
    const UiStyle& s = app_.style();
    const Rect screen = app_.layout().screen;
    const int w = screen.w * 5 / 6;
    Font& body = app_.fonts().get(s.fontPx);
    Font& titleFont = app_.fonts().get(s.titlePx, FontStyle::Bold);
    const int pad = s.fontPx;
    lines_ = text_.empty() ? std::vector<std::string>{} : body.wrap(text_, w - 2 * pad);
    titleH_ = title_.empty() ? 0 : titleFont.lineHeight() + s.fontPx;
    const int btnH = kMinTouchPx;
    const int rows = vertical ? static_cast<int>(buttons.size()) : 1;
    const int textH = static_cast<int>(lines_.size()) * body.lineHeight();
    const int h = titleH_ + pad + textH + (textH ? pad : 0) + rows * (btnH + pad / 2) + pad;
    rect_ = {screen.x + (screen.w - w) / 2, screen.y + (screen.h - h) / 2, w, h};

    const Rect area{rect_.x + pad, rect_.bottom() - pad - rows * (btnH + pad / 2) + pad / 2, w - 2 * pad,
                    rows * (btnH + pad / 2) - pad / 2};
    const auto cells = vertical ? splitColumn(area, rows, pad / 2) : splitRow(area, static_cast<int>(buttons.size()), pad / 2);
    for (size_t i = 0; i < buttons.size(); ++i) {
        Button b;
        b.rect = cells[i];
        b.label = buttons[i];
        if (i < hints.size()) b.hint = hints[i];
        b.id = static_cast<int>(i);
        b.primary = !vertical && i + 1 == buttons.size();
        buttons_.push_back(b);
    }
    focus_ = static_cast<int>(buttons_.size()) - 1;
    if (vertical) focus_ = 0;
}

void MessageDialog::draw(Canvas& c, const Rect&) {
    drawFrame(c, title_);
    const UiStyle& s = app_.style();
    Font& body = app_.fonts().get(s.fontPx);
    const int pad = s.fontPx;
    drawLines(c, {rect_.x + pad, rect_.y + titleH_ + pad / 2, rect_.w - 2 * pad, static_cast<int>(lines_.size()) * body.lineHeight() + 4},
              lines_, body, app_.theme().ink);
    for (size_t i = 0; i < buttons_.size(); ++i) {
        drawButton(c, buttons_[i], app_.theme(), app_.fonts(), s);
        if (static_cast<int>(i) == focus_ && buttons_.size() > 1)
            c.frame(buttons_[i].rect.inset(-s.stroke * 2), s.stroke, app_.theme().frame);
    }
}

void MessageDialog::finish(int index) {
    auto done = std::move(done_);
    app_.pop();  // `this` stays alive until the end of the event (graveyard)
    if (done) done(index);
}

void MessageDialog::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    const int id = hitTest(buttons_, g.start);
    if (id >= 0) finish(id);
}

void MessageDialog::onKey(const InputEvent& e) {
    using K = KeyCode;
    const int n = static_cast<int>(buttons_.size());
    const Rect old = buttons_.empty() ? Rect{} : buttons_[static_cast<size_t>(focus_)].rect.inset(-app_.style().stroke * 3);
    switch (e.key) {
    case K::Prev: case K::Left: case K::Up: focus_ = (focus_ + n - 1) % n; break;
    case K::Next: case K::Right: case K::Down: focus_ = (focus_ + 1) % n; break;
    case K::Ok: case K::Menu: finish(focus_); return;
    case K::Back: case K::Home: finish(-1); return;
    default: return;
    }
    app_.invalidate(old, RefreshHint::Selection);
    app_.invalidate(buttons_[static_cast<size_t>(focus_)].rect.inset(-app_.style().stroke * 3), RefreshHint::Selection);
}

// --- SwatchDialog -------------------------------------------------------------------------------

SwatchDialog::SwatchDialog(Application& app, std::string title, std::vector<Swatch> swatches, int current,
                           std::function<void(int)> done)
    : Dialog(app), title_(std::move(title)), swatches_(std::move(swatches)), current_(current), done_(std::move(done)) {
    const UiStyle& s = app_.style();
    const Rect screen = app_.layout().screen;
    const int pad = s.fontPx;
    const int w = screen.w * 11 / 12;
    const int cols = 4;
    const int rows = (static_cast<int>(swatches_.size()) + cols - 1) / cols;
    const int labelH = app_.fonts().get(s.smallPx).lineHeight();
    const int cellW = (w - 2 * pad - (cols - 1) * pad / 2) / cols;
    const int swatchH = std::max(kMinTouchPx - labelH, cellW / 2);
    const int cellH = swatchH + labelH;
    titleH_ = app_.fonts().get(s.titlePx, FontStyle::Bold).lineHeight() + s.fontPx;
    const int h = titleH_ + pad / 2 + rows * (cellH + pad / 2) + kMinTouchPx + pad * 3 / 2;
    rect_ = {screen.x + (screen.w - w) / 2, screen.y + std::max(0, (screen.h - h) / 2), w, h};
    for (int i = 0; i < static_cast<int>(swatches_.size()); ++i) {
        const int r = i / cols, col = i % cols;
        cells_.push_back({rect_.x + pad + col * (cellW + pad / 2), rect_.y + titleH_ + pad / 2 + r * (cellH + pad / 2),
                          cellW, cellH});
    }
    close_.label = "Close";
    close_.rect = {rect_.x + pad, rect_.bottom() - pad - kMinTouchPx, w - 2 * pad, kMinTouchPx};
    focus_ = current_ >= 0 && current_ < static_cast<int>(swatches_.size()) ? current_ : 0;
}

Rect SwatchDialog::focusRect(int i) const {
    const int st = app_.style().stroke;
    return (i < static_cast<int>(cells_.size()) ? cells_[static_cast<size_t>(i)] : close_.rect).inset(-st * 3);
}

void SwatchDialog::draw(Canvas& c, const Rect&) {
    drawFrame(c, title_);
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    Font& small = app_.fonts().get(s.smallPx);
    for (size_t i = 0; i < cells_.size(); ++i) {
        const Swatch& sw = swatches_[i];
        const Rect cell = cells_[i];
        const Rect box{cell.x, cell.y, cell.w, cell.h - small.lineHeight()};
        if (sw.b) {
            c.fill({box.x, box.y, box.w / 2, box.h}, sw.a);
            c.fill({box.x + box.w / 2, box.y, box.w - box.w / 2, box.h}, *sw.b);
        } else {
            c.fill(box, sw.a);
        }
        c.frame(box, static_cast<int>(i) == current_ ? s.stroke * 3 : s.stroke, t.frame);
        if (static_cast<int>(i) == current_) small.drawCentered(c, box, "✓", textOn(t, sw.a));
        small.drawCentered(c, {cell.x, box.bottom(), cell.w, small.lineHeight()}, fitText(small, sw.label, cell.w), t.ink);
    }
    drawButton(c, close_, t, app_.fonts(), s);
    c.frame(focusRect(focus_), s.stroke, t.frame);
}

void SwatchDialog::finish(int index) {
    auto done = std::move(done_);
    app_.pop();
    if (done) done(index);
}

void SwatchDialog::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    for (size_t i = 0; i < cells_.size(); ++i)
        if (cells_[i].contains(g.start)) return finish(static_cast<int>(i));
    if (close_.rect.contains(g.start) || !rect_.contains(g.start)) finish(-1);
}

void SwatchDialog::onKey(const InputEvent& e) {
    const int n = static_cast<int>(cells_.size()) + 1;  // + Close
    const Rect old = focusRect(focus_);
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: case KeyCode::Up: focus_ = (focus_ + n - 1) % n; break;
    case KeyCode::Next: case KeyCode::Right: case KeyCode::Down: focus_ = (focus_ + 1) % n; break;
    case KeyCode::Ok: case KeyCode::Menu: return finish(focus_ < n - 1 ? focus_ : -1);
    case KeyCode::Back: case KeyCode::Home: return finish(-1);
    default: return;
    }
    app_.invalidate(old, RefreshHint::Selection);
    app_.invalidate(focusRect(focus_), RefreshHint::Selection);
}

// --- chooseFromList -----------------------------------------------------------------------------

namespace {

void showListPage(Application& app, std::shared_ptr<const std::vector<std::string>> items, std::string title,
                  std::shared_ptr<std::function<void(int)>> done, int page, int pageSize,
                  std::shared_ptr<const std::vector<std::string>> hints) {
    const int n = static_cast<int>(items->size());
    const int first = page * pageSize;
    const int last = std::min(n, first + pageSize);
    std::vector<std::string> buttons(items->begin() + first, items->begin() + last);
    std::vector<std::string> pageHints;
    for (int k = first; k < last; ++k)
        pageHints.push_back(k < static_cast<int>(hints->size()) ? (*hints)[static_cast<size_t>(k)] : std::string());
    const bool more = last < n;
    if (more) buttons.push_back("More… (" + std::to_string(last + 1) + "–" + std::to_string(std::min(n, last + pageSize)) +
                                " of " + std::to_string(n) + ")");
    buttons.push_back("Close");
    app.push(std::make_unique<MessageDialog>(
        app, title, "", buttons,
        [&app, items, title, done, page, pageSize, first, last, more, hints](int i) {
            if (i >= 0 && i < last - first) return (*done)(first + i);
            if (more && i == last - first) return showListPage(app, items, title, done, page + 1, pageSize, hints);
            (*done)(-1);
        },
        true, pageHints));
}

} // namespace

void chooseFromList(Application& app, const std::string& title, std::vector<std::string> items,
                    std::function<void(int)> done, int pageSize, std::vector<std::string> hints) {
    auto shared = std::make_shared<const std::vector<std::string>>(std::move(items));
    auto cb = std::make_shared<std::function<void(int)>>(std::move(done));
    if (shared->empty()) return (*cb)(-1);
    showListPage(app, shared, title, cb, 0, std::max(1, pageSize),
                 std::make_shared<const std::vector<std::string>>(std::move(hints)));
}

// --- PromotionDialog ----------------------------------------------------------------------------

PromotionDialog::PromotionDialog(Application& app, chess::Color color, std::function<void(chess::PieceType)> done)
    : Dialog(app), color_(color), done_(std::move(done)) {
    const Layout& l = app_.layout();
    const int sq = l.square;
    const int cell = sq + sq / 3;
    const int pad = sq / 4;
    const int w = 4 * cell + 5 * pad;
    const int h = cell + 2 * pad;
    rect_ = {l.board.x + (l.board.w - w) / 2, l.board.y + (l.board.h - h) / 2, w, h};
    for (int i = 0; i < 4; ++i) cells_.push_back({rect_.x + pad + i * (cell + pad), rect_.y + pad, cell, cell});
}

void PromotionDialog::draw(Canvas& c, const Rect&) {
    drawFrame(c, "");
    const BoardTheme& t = app_.theme();
    static const chess::PieceType types[4] = {chess::Queen, chess::Rook, chess::Bishop, chess::Knight};
    for (int i = 0; i < 4; ++i) {
        const Rect r = cells_[static_cast<size_t>(i)];
        c.fill(r, i % 2 ? t.dark : t.light);
        c.frame(r, app_.style().stroke, t.frame);
        if (i == focus_) c.frame(r.inset(-app_.style().stroke * 2), app_.style().stroke, t.frame);
        const Bitmap& b = app_.pieces().get(chess::makePiece(color_, types[i]), r.w);
        c.blit(b, r.x, r.y);
    }
}

void PromotionDialog::finish(chess::PieceType t) {
    auto done = std::move(done_);
    app_.pop();
    if (done) done(t);
}

void PromotionDialog::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    static const chess::PieceType types[4] = {chess::Queen, chess::Rook, chess::Bishop, chess::Knight};
    for (int i = 0; i < 4; ++i)
        if (cells_[static_cast<size_t>(i)].contains(g.start)) return finish(types[i]);
    if (!rect_.contains(g.start)) finish(chess::NoPieceType);
}

void PromotionDialog::onKey(const InputEvent& e) {
    static const chess::PieceType types[4] = {chess::Queen, chess::Rook, chess::Bishop, chess::Knight};
    const int old = focus_;
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: focus_ = (focus_ + 3) % 4; break;
    case KeyCode::Next: case KeyCode::Right: focus_ = (focus_ + 1) % 4; break;
    case KeyCode::Ok: case KeyCode::Menu: return finish(types[focus_]);
    case KeyCode::Back: return finish(chess::NoPieceType);
    default: return;
    }
    const int s = app_.style().stroke * 3;
    app_.invalidate(cells_[static_cast<size_t>(old)].inset(-s), RefreshHint::Selection);
    app_.invalidate(cells_[static_cast<size_t>(focus_)].inset(-s), RefreshHint::Selection);
}

// --- TextEntryDialog ----------------------------------------------------------------------------

TextEntryDialog::TextEntryDialog(Application& app, std::string title, std::string initial,
                                 std::function<void(std::optional<std::string>)> done)
    : Dialog(app), title_(std::move(title)), text_(std::move(initial)), done_(std::move(done)) {
    const UiStyle& s = app_.style();
    const Rect screen = app_.layout().screen;
    const int w = screen.w - 2 * s.fontPx;
    const int pad = s.fontPx;
    Font& titleFont = app_.fonts().get(s.titlePx, FontStyle::Bold);
    titleH_ = titleFont.lineHeight() + s.fontPx;
    const int fieldH = app_.fonts().get(s.fontPx).lineHeight() * 3 + pad;
    const int h = titleH_ + pad + fieldH + pad + kMinTouchPx + pad;
    rect_ = {screen.x + (screen.w - w) / 2, screen.y + screen.h / 5, w, h};
    field_ = {rect_.x + pad, rect_.y + titleH_ + pad / 2, w - 2 * pad, fieldH};
    const auto cells = splitRow({rect_.x + pad, field_.bottom() + pad, w - 2 * pad, kMinTouchPx}, 3, pad / 2);
    const char* labels[3] = {"Close", "Clear", "OK"};
    for (int i = 0; i < 3; ++i) {
        Button b;
        b.rect = cells[static_cast<size_t>(i)];
        b.label = labels[i];
        b.id = i;
        b.primary = i == 2;
        buttons_.push_back(b);
    }
}

void TextEntryDialog::draw(Canvas& c, const Rect&) {
    drawFrame(c, title_);
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.frame(field_, s.stroke, t.frame);
    Font& f = app_.fonts().get(s.fontPx);
    auto lines = f.wrap(text_ + "│", field_.w - s.fontPx);
    drawLines(c, field_.inset(s.fontPx / 2, s.fontPx / 4), lines, f, t.ink);
    for (const Button& b : buttons_) drawButton(c, b, t, app_.fonts(), s);
}

void TextEntryDialog::finish(bool accept) {
    auto done = std::move(done_);
    std::optional<std::string> result;
    if (accept) result = text_;
    app_.pop();
    if (done) done(result);
}

void TextEntryDialog::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    switch (hitTest(buttons_, g.start)) {
    case 0: finish(false); break;
    case 1:
        text_.clear();
        app_.invalidate(field_, RefreshHint::Content);
        break;
    case 2: finish(true); break;
    default: break;
    }
}

void TextEntryDialog::onKey(const InputEvent& e) {
    if (e.key == KeyCode::Ok) finish(true);
    else if (e.key == KeyCode::Back || e.key == KeyCode::Home) finish(false);
}

void TextEntryDialog::onText(const std::string& t) {
    if (t == "\b") {
        // Remove one UTF-8 code point.
        while (!text_.empty()) {
            const unsigned char ch = static_cast<unsigned char>(text_.back());
            text_.pop_back();
            if ((ch & 0xC0) != 0x80) break;
        }
    } else {
        text_ += t;
    }
    app_.invalidate(field_, RefreshHint::Selection);
}

} // namespace pchess::app
