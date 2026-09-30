// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/San.h"
#include "support/Test.h"
#include "ui/board/BoardView.h"
#include "ui/canvas/Canvas.h"
#include "ui/canvas/Font.h"
#include "ui/canvas/PngWriter.h"
#include "ui/input/Input.h"
#include "ui/render/Refresh.h"
#include "ui/theme/SvgPieceSet.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/Widgets.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace pchess;
using namespace pchess::ui;
using namespace pchess::chess;

// --- Canvas -------------------------------------------------------------------------------------

TEST_CASE("canvas: fill respects clip, blend is exact at the ends") {
    Canvas c(40, 30);
    c.fill(c.bounds(), colors::White);
    c.pushClip({10, 10, 5, 5});
    c.fill({0, 0, 40, 30}, colors::Black);
    c.popClip();
    CHECK(c.pixel(10, 10) == colors::Black);
    CHECK(c.pixel(14, 14) == colors::Black);
    CHECK(c.pixel(15, 15) == colors::White);
    CHECK(c.pixel(9, 10) == colors::White);
    CHECK_EQ(int(blend8(0, 255, 255)), 255);
    CHECK_EQ(int(blend8(255, 0, 255)), 0);
    CHECK_EQ(int(blend8(200, 0, 0)), 200);
    CHECK_EQ(int(blend8(0, 255, 128)), 128);
}

TEST_CASE("canvas: hash changes with content and is stable") {
    Canvas a(16, 16), b(16, 16);
    CHECK_EQ(a.hash(), b.hash());
    b.fill({3, 3, 1, 1}, colors::Black);
    CHECK(a.hash() != b.hash());
    CHECK_EQ(a.hash({0, 0, 3, 16}), b.hash({0, 0, 3, 16}));
}

TEST_CASE("png: encoder output has a valid signature, chunks and CRC") {
    Canvas c(33, 7);
    for (int x = 0; x < 33; ++x) c.fill({x, 0, 1, 7}, Rgb::gray(static_cast<uint8_t>(x * 7)));
    const auto png = encodePng(c, c.bounds());
    REQUIRE(png.size() > 57);
    CHECK_EQ(int(png[0]), 0x89);
    CHECK_EQ(int(png[1]), 'P');
    // IHDR CRC covers type + data.
    const uint32_t crc = crc32(png.data() + 12, 17);
    const uint32_t stored = (uint32_t{png[29]} << 24) | (uint32_t{png[30]} << 16) | (uint32_t{png[31]} << 8) | png[32];
    CHECK_EQ(crc, stored);
    CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9), 0xCBF43926u);
}

// --- Fonts / rasterizer ---------------------------------------------------------------------------

TEST_CASE("font: loads the bundled face, measures and rasterizes") {
    auto face = TrueTypeFont::load(std::filesystem::path(test::fontDir()) / "DejaVuSans.ttf");
    REQUIRE(face.ok());
    CHECK(face.value()->glyphIndex(U'A') != 0);
    CHECK(face.value()->glyphIndex(0x2654) != 0);  // chess king
    CHECK_EQ(face.value()->glyphIndex(0x10FFFD), 0);
    Font f(face.value(), 30);
    CHECK(f.measure("WWW") > f.measure("iii"));
    CHECK_EQ(f.measure(""), 0);
    const Glyph& g = f.glyph(U'O');
    CHECK(g.mask.width > 10);
    int inked = 0;
    for (uint8_t a : g.mask.alpha) inked += a > 128;
    CHECK(inked > 30);
    auto lines = f.wrap("the quick brown fox jumps over the lazy dog", f.measure("the quick brown"));
    CHECK(lines.size() >= 3);
    for (const auto& l : lines) CHECK(f.measure(l) <= f.measure("the quick brown"));
    CHECK_FALSE(TrueTypeFont::load("does-not-exist.ttf").ok());
}

TEST_CASE("rasterizer: a square covers exactly its area") {
    Rasterizer r(10, 10);
    r.moveTo({2, 2});
    r.lineTo({7, 2});
    r.lineTo({7, 7});
    r.lineTo({2, 7});
    r.close();
    const AlphaMask m = r.finish();
    int full = 0, partial = 0;
    for (uint8_t a : m.alpha) {
        full += a == 255;
        partial += a > 0 && a < 255;
    }
    CHECK_EQ(full, 25);
    CHECK_EQ(partial, 0);
    Rasterizer h(10, 10);  // half-pixel offset -> 50 % edges
    h.moveTo({2.5f, 2});
    h.lineTo({7.5f, 2});
    h.lineTo({7.5f, 7});
    h.lineTo({2.5f, 7});
    h.close();
    const AlphaMask hm = h.finish();
    CHECK_EQ(int(hm.at(2, 3)), 128);
    CHECK_EQ(int(hm.at(5, 3)), 255);
    CHECK_EQ(int(hm.at(7, 3)), 128);
}

// --- Dirty regions / refresh policy ---------------------------------------------------------------

TEST_CASE("dirty: merges adjacent rects, keeps distant ones, collapses when too many") {
    DirtyRegions d({0, 0, 1000, 1000}, 4);
    d.add({0, 0, 10, 10});
    d.add({10, 0, 10, 10});  // adjacent -> merged
    CHECK_EQ(d.rects().size(), size_t(1));
    CHECK(d.rects()[0] == (Rect{0, 0, 20, 10}));
    d.add({500, 500, 10, 10});
    CHECK_EQ(d.rects().size(), size_t(2));
    d.add({5, 5, 2, 2});  // contained
    CHECK_EQ(d.rects().size(), size_t(2));
    d.add({900, 0, 5, 5});
    d.add({0, 900, 5, 5});
    d.add({300, 300, 5, 5});  // 5th -> collapse to bounding box
    CHECK_EQ(d.rects().size(), size_t(1));
    CHECK(d.rects()[0] == (Rect{0, 0, 905, 905}));
    d.add({-50, -50, 60, 60});  // clipped to bounds
    auto taken = d.take();
    CHECK(d.empty());
    CHECK_EQ(taken.size(), size_t(1));
}

TEST_CASE("refresh policy: selection fast, content partial, periodic and screen full") {
    RefreshPolicy p;
    p.setFullEvery(3);
    const int64_t screen = 1000 * 1000;
    CHECK(p.decide(RefreshHint::Selection, 100, screen) == RefreshMode::Fast);
    CHECK(p.decide(RefreshHint::Content, 100, screen) == RefreshMode::Partial);
    CHECK(p.decide(RefreshHint::Content, 100, screen) == RefreshMode::Partial);
    CHECK(p.decide(RefreshHint::Content, 100, screen) == RefreshMode::Full);  // 3rd content update
    CHECK(p.decide(RefreshHint::Content, 100, screen) == RefreshMode::Partial);
    CHECK(p.decide(RefreshHint::Screen, 100, screen) == RefreshMode::Full);
    CHECK(p.decide(RefreshHint::Content, screen * 6 / 10, screen) == RefreshMode::Full);  // > 50 %
}

// --- Board geometry and minimal invalidation (design notes §7.3) -------------------------------------

TEST_CASE("board: square geometry, hit testing and flip") {
    BoardView v;
    v.setRect({0, 64, 1072, 1072});
    CHECK_EQ(v.squareSize(), 134);
    CHECK(v.squareRect(A1) == (Rect{0, 64 + 7 * 134, 134, 134}));
    CHECK(v.squareRect(H8) == (Rect{7 * 134, 64, 134, 134}));
    CHECK_EQ(v.squareAt({10, 64 + 7 * 134 + 5}), A1);
    CHECK_EQ(v.squareAt({1071, 64}), H8);
    CHECK_EQ(v.squareAt({10, 10}), NoSquare);
    BoardVisual flipped;
    flipped.flipped = true;
    auto rects = v.update(flipped);
    CHECK_EQ(rects.size(), size_t(1));  // orientation change -> whole board
    CHECK(v.squareRect(A1) == (Rect{7 * 134, 64, 134, 134}));
    CHECK_EQ(v.squareAt({10, 64 + 5}), H1);
}

TEST_CASE("board: affected squares for quiet move, capture, castling, en passant, check") {
    auto squares = [](const char* fen, const char* sanMove) {
        Position p = parseFen(fen).value();
        BoardVisual before = BoardVisual::fromPosition(p);
        Move m = parseSan(p, sanMove).value();
        p.makeMove(m);
        BoardVisual after = BoardVisual::fromPosition(p, m);
        auto s = affectedSquares(before, after);
        std::sort(s.begin(), s.end());
        return s;
    };
    CHECK(squares(StartFen.data(), "e4") == (std::vector<Square>{E2, E4}));
    CHECK(squares("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "O-O") == (std::vector<Square>{E1, F1, G1, H1}));
    CHECK(squares("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "exd6") == (std::vector<Square>{D5, E5, D6}));
    // Check adds the king square.
    CHECK(squares("4k3/8/8/8/8/8/8/R3K3 w - - 0 1", "Ra8+") == (std::vector<Square>{A1, A8, E8}));

    // Previous last-move squares are cleared on the next move.
    Position p = Position::startPosition();
    Move e4 = parseSan(p, "e4").value();
    p.makeMove(e4);
    BoardVisual v1 = BoardVisual::fromPosition(p, e4);
    Move e5 = parseSan(p, "e5").value();
    p.makeMove(e5);
    BoardVisual v2 = BoardVisual::fromPosition(p, e5);
    auto s = affectedSquares(v1, v2);
    std::sort(s.begin(), s.end());
    CHECK(s == (std::vector<Square>{E2, E4, E7, E5}) || s == (std::vector<Square>{E2, E4, E5, E7}));
    CHECK_EQ(s.size(), size_t(4));

    // Selection touches only the selected square and its targets.
    BoardVisual sel = v2;
    sel.selected = G1;
    sel.targets = bb(F3) | bb(H3) | bb(E2);
    CHECK_EQ(affectedSquares(v2, sel).size(), size_t(4));
}

// --- Gestures -------------------------------------------------------------------------------------

TEST_CASE("gestures: tap, drag, long press") {
    using T = InputEvent::Type;
    using C = GestureRecognizer::Clock;
    GestureRecognizer g;
    g.setSlop(10);
    const C::time_point t0{};
    CHECK_FALSE(g.feed(InputEvent::pointer(T::PointerDown, {100, 100}), t0).has_value());
    auto tap = g.feed(InputEvent::pointer(T::PointerUp, {104, 98}), t0 + std::chrono::milliseconds(80));
    REQUIRE(tap.has_value());
    CHECK(tap->type == Gesture::Type::Tap);

    g.feed(InputEvent::pointer(T::PointerDown, {100, 100}), t0);
    auto drag = g.feed(InputEvent::pointer(T::PointerUp, {300, 100}), t0 + std::chrono::milliseconds(200));
    REQUIRE(drag.has_value());
    CHECK(drag->type == Gesture::Type::Drag);
    CHECK(drag->end == (Point{300, 100}));

    g.feed(InputEvent::pointer(T::PointerDown, {50, 50}), t0);
    auto lp = g.feed(InputEvent::pointer(T::LongPress, {50, 50}), t0 + std::chrono::milliseconds(700));
    REQUIRE(lp.has_value());
    CHECK(lp->type == Gesture::Type::LongPress);
    CHECK_FALSE(g.feed(InputEvent::pointer(T::PointerUp, {50, 50}), t0 + std::chrono::milliseconds(900)).has_value());

    g.feed(InputEvent::pointer(T::PointerDown, {50, 50}), t0);
    auto held = g.feed(InputEvent::pointer(T::PointerUp, {51, 50}), t0 + std::chrono::milliseconds(900));
    REQUIRE(held.has_value());
    CHECK(held->type == Gesture::Type::LongPress);
}

TEST_CASE("refresh policy: a page turn stays partial even when it covers most of the screen") {
    RefreshPolicy p;
    p.setFullEvery(3);
    const int64_t screen = 1000 * 1000;
    CHECK(p.decide(RefreshHint::Content, 800 * 1000, screen) == RefreshMode::Full);  // large content: full
    CHECK(p.decide(RefreshHint::Page, 800 * 1000, screen) == RefreshMode::Partial);
    CHECK(p.decide(RefreshHint::Page, 800 * 1000, screen) == RefreshMode::Partial);
    CHECK(p.decide(RefreshHint::Page, 800 * 1000, screen) == RefreshMode::Full);  // the periodic full still comes
}

TEST_CASE("fitText: unchanged when it fits, else shortened with an ellipsis on a character boundary") {
    FontLibrary fonts;
    REQUIRE(fonts.load(std::filesystem::path(test::fontDir()) / "DejaVuSans.ttf",
                       std::filesystem::path(test::fontDir()) / "DejaVuSans-Bold.ttf")
                .ok());
    Font& f = fonts.get(26);
    CHECK_EQ(fitText(f, "Kasparov", 1000), std::string("Kasparov"));
    const std::string long_ = "José Raúl Capablanca y Graupera – Alexander Alekhine";
    const std::string fit = fitText(f, long_, 200);
    CHECK(f.measure(fit) <= 200);
    CHECK(fit.size() < long_.size());
    CHECK(fit.rfind("…") == fit.size() - std::string("…").size());
    CHECK_EQ(fitText(f, "abc", 1), std::string("…"));
}

// --- Themes (M6) -----------------------------------------------------------------------------------

TEST_CASE("themes: YAML descriptors override the mode's defaults, mono gray keys included") {
    auto color = util::parseYaml("name: Walnut\nmode: color\nlight: \"#eadbc0\"\ndark: 120\n"
                                 "selected_pattern: hatch\ntint_markers: false\n");
    REQUIRE(color.ok());
    auto t = parseTheme(*color);
    REQUIRE(t.ok());
    CHECK_EQ(t->name, std::string("Walnut"));
    CHECK(!t->mono);
    CHECK((t->light == Rgb::hex(0xEADBC0)));
    CHECK((t->dark == Rgb::gray(120)));
    CHECK((t->check == BoardTheme::paper().check));  // not given: Paper's
    CHECK(t->selectedStyle == MarkerStyle::Hatch);
    CHECK(!t->tintMarkers);

    auto mono = parseTheme(*util::parseYaml("name: News\nmode: mono\nlight_gray: 250\ndark_gray: 190\n"));
    REQUIRE(mono.ok());
    CHECK(mono->mono);
    CHECK((mono->dark == Rgb::gray(190)));
    CHECK((mono->ink == BoardTheme::classicMono().ink));

    // A theme written out reads back the same.
    auto back = parseTheme(*util::parseYaml(themeToYaml(*t)));
    REQUIRE(back.ok());
    CHECK((back->light == t->light && back->frame == t->frame && back->coordOnDark == t->coordOnDark));
    CHECK(back->selectedStyle == t->selectedStyle);
    CHECK_EQ(back->tintMarkers, t->tintMarkers);
}

TEST_CASE("themes: malformed descriptors are rejected with the line") {
    auto err = [](const char* yaml) {
        auto doc = util::parseYaml(yaml);
        if (!doc) return doc.error().context;
        auto t = parseTheme(*doc);
        return t ? std::string() : t.error().context;
    };
    CHECK(err("mode: color\n").find("name") != std::string::npos);
    CHECK(err("name: X\nmode: sepia\n").find("mode") != std::string::npos);
    CHECK(err("name: X\nlight: #eadbc0\n").find("quoted") != std::string::npos);  // # starts a comment
    CHECK(err("name: X\nlight: \"#eadbcz\"\n").find("line 2") != std::string::npos);
    CHECK(err("name: X\ndark: 300\n").find("0..255") != std::string::npos);
    CHECK(err("name: X\nligth: \"#ffffff\"\n").find("unknown key 'ligth'") != std::string::npos);
    CHECK(err("name: X\ncheck_pattern: stripes\n").find("double_border") != std::string::npos);
    CHECK(err("name: X\ntint_markers: maybe\n").find("true or false") != std::string::npos);
}

TEST_CASE("themes: the manager adds a folder's themes and skips broken or duplicate files") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pchess-themes-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto write = [&](const char* name, const char* text) {
        std::ofstream(dir / name, std::ios::binary) << text;
    };
    write("a.yaml", "name: Walnut\nlight: \"#eadbc0\"\n");
    write("b.yaml", "name: Broken\nlight: nope\n");
    write("c.yaml", "name: Paper\n");  // a built-in's name
    write("d.yaml", "name: Newsprint\nmode: mono\n");
    write("notes.txt", "not a theme");

    ThemeManager m;
    const size_t builtIns = m.themes().size();
    CHECK_EQ(m.loadDirectory(dir), 2);
    CHECK_EQ(m.themes().size(), builtIns + 2);
    CHECK_EQ(m.problems().size(), size_t(2));
    CHECK(m.find("Walnut") != nullptr);
    const auto monoNames = m.names(true);
    CHECK(std::find(monoNames.begin(), monoNames.end(), "Newsprint") != monoNames.end());
    CHECK_EQ(m.resolve("Walnut", false).name, std::string("Walnut"));
    CHECK_EQ(m.resolve("Walnut", true).name, std::string("ClassicMono"));  // mode mismatch
    CHECK_EQ(m.resolve("Missing", false).name, std::string("Paper"));
    CHECK_EQ(m.loadDirectory(dir / "missing"), 0);
    std::filesystem::remove_all(dir);
}

TEST_CASE("themes: the bundled theme files load") {
    ThemeManager m;
    CHECK_EQ(m.loadDirectory(std::filesystem::path(test::fontDir()).parent_path() / "themes"), 7);
    CHECK(m.problems().empty());
    REQUIRE(m.find("Walnut") != nullptr);
    CHECK(m.find("Newsprint") != nullptr && m.find("Newsprint")->mono);
}

// --- Piece sets (M6, ADR 0012) -----------------------------------------------------------------------

namespace {

std::filesystem::path bundledPieces() { return std::filesystem::path(test::fontDir()).parent_path() / "pieces"; }

struct Ink {
    int covered = 0;  // alpha > 128
    int dark = 0;     // covered and luma < 80
    int light = 0;    // covered and luma > 200
};

Ink inkOf(const Bitmap& b) {
    Ink ink;
    for (int y = 0; y < b.height; ++y)
        for (int x = 0; x < b.width; ++x) {
            const uint8_t* p = b.px(x, y);
            if (p[3] <= 128) continue;
            ++ink.covered;
            const int luma = Rgb{p[0], p[1], p[2]}.luma();
            if (luma < 80) ++ink.dark;
            if (luma > 200) ++ink.light;
        }
    return ink;
}

} // namespace

TEST_CASE("pieces: the bundled SVG sets load and draw white and black pieces") {
    PieceSetLibrary lib;
    CHECK_EQ(lib.scan(bundledPieces()), 4);
    CHECK(lib.problems().empty());
    for (const char* name : {"Cburnett", "Merida", "Chessnut", "Bold"}) {
        const PieceSetInfo* info = lib.find(name);
        REQUIRE(info != nullptr);
        CHECK(!info->license.empty());
        auto set = SvgPieceSet::load(info->dir);
        REQUIRE(set.ok());
        for (int px : {60, 134}) {
            const Bitmap wk = (*set)->render(chess::WKing, px);
            const Bitmap bp = (*set)->render(chess::BPawn, px);
            REQUIRE(wk.width == px);
            const Ink w = inkOf(wk), b = inkOf(bp);
            MESSAGE(std::string(name) + " " + std::to_string(px) + "px: white king " + std::to_string(w.covered) +
                    " covered, black pawn " + std::to_string(b.covered));
            CHECK(w.covered > px * px / 8);
            CHECK(w.light > w.covered / 4);  // a white piece is mostly light, with an outline
            CHECK(b.covered > px * px / 20);
            CHECK(b.dark > b.covered / 2);
        }
    }
    // The bold variant is the same shapes with wider strokes: more dark ink on a white piece.
    auto thin = SvgPieceSet::load(lib.find("Cburnett")->dir);
    auto bold = SvgPieceSet::load(lib.find("Bold")->dir);
    REQUIRE((thin.ok() && bold.ok()));
    CHECK(inkOf((*bold)->render(chess::WQueen, 134)).dark > inkOf((*thin)->render(chess::WQueen, 134)).dark * 5 / 4);
}

TEST_CASE("pieces: bad manifests and missing SVGs are rejected with the reason") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "pchess-pieces-test";
    std::filesystem::remove_all(root);
    auto make = [&](const std::string& id, const std::string& manifest) {
        std::filesystem::create_directories(root / id);
        std::ofstream(root / id / "pieceset.yaml", std::ios::binary) << manifest;
        return root / id;
    };
    auto err = [](const std::filesystem::path& dir) {
        auto s = SvgPieceSet::load(dir);
        return s ? std::string() : s.error().context;
    };
    CHECK(err(make("noname", "license: MIT\n")).find("name missing") != std::string::npos);
    CHECK(err(make("nolicense", "name: X\n")).find("license missing") != std::string::npos);
    CHECK(err(make("scale", "name: X\nlicense: MIT\nscale: 3\n")).find("0.5 to 1.2") != std::string::npos);
    CHECK(err(make("typo", "name: X\nlicense: MIT\nsacle: 1\n")).find("unknown key 'sacle'") != std::string::npos);
    CHECK(err(make("escape", "name: X\nlicense: MIT\nbase: ../x\n")).find("sibling") != std::string::npos);
    CHECK(err(make("empty", "name: Empty\nlicense: MIT\n")).find("wP.svg") != std::string::npos);
    // A set built on a sibling's SVGs.
    const auto cb = bundledPieces() / "cburnett";
    std::filesystem::create_directories(root / "cburnett");
    for (const auto& e : std::filesystem::directory_iterator(cb))
        std::filesystem::copy_file(e.path(), root / "cburnett" / e.path().filename());
    CHECK(SvgPieceSet::load(make("derived", "name: Derived\nlicense: GPL-2.0-or-later\nbase: cburnett\nscale: 0.9\n")).ok());

    PieceSetLibrary lib;
    CHECK_EQ(lib.scan(root), 3);  // empty, cburnett, derived (manifests only; SVGs load on use)
    CHECK_EQ(lib.problems().size(), size_t(5));
    std::filesystem::remove_all(root);
}
