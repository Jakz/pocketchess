// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/ImportScreen.h"
#include "app/Importer.h"
#include "app/Script.h"
#include "core/chess/Pgn.h"
#include "core/engine/EngineDescriptor.h"
#include "core/games/GamePack.h"
#include "core/puzzles/PuzzleImport.h"
#include "core/storage/Database.h"
#include "core/util/Files.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace pchess;
using namespace pchess::app;

namespace {

namespace fs = std::filesystem;

std::string fixture(const char* name) {
    fs::path file = fs::path(test::fixtureDir()) / name;
    if (!fs::exists(file)) file = fs::path(test::fontDir()).parent_path() / "samples" / "pgn" / name;
    auto t = util::readTextFile(file);
    REQUIRE(t.ok());
    return *t;
}

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// The first bytes of an executable for `platform` (the importer only checks the header).
std::string fakeBinary(const std::string& platform) {
    std::string elf(20, '\0');
    elf[0] = 0x7F, elf[1] = 'E', elf[2] = 'L', elf[3] = 'F', elf[4] = 1, elf[5] = 1;
    if (platform.rfind("windows", 0) == 0) return "MZ" + std::string(64, '\0');
    elf[18] = static_cast<char>(platform == "arm" ? 40 : platform == "arm64" ? 183 : 62);
    return elf;
}

void gamePackFile(const fs::path& file) {
    fs::create_directories(file.parent_path());
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    REQUIRE(db->exec("PRAGMA journal_mode=DELETE").ok());
    games::GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    for (auto& g : chess::parsePgn(fixture("games-sample.pgn"))) REQUIRE(imp.add(*g).ok());
    REQUIRE(imp.finish({{"title", "Imported games"}}).ok());
}

void puzzlePackFile(const fs::path& file) {
    fs::create_directories(file.parent_path());
    const std::string csv = fixture("puzzles-sample.csv");
    puzzles::ImportFilter f;
    f.count = 1000;
    puzzles::PuzzleImporter imp(f);
    size_t i = 0;
    while (i < csv.size()) {
        size_t j = csv.find('\n', i);
        if (j == std::string::npos) j = csv.size();
        imp.addLine(std::string_view(csv).substr(i, j - i));
        i = j + 1;
    }
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    REQUIRE(db->exec("PRAGMA journal_mode=DELETE").ok());
    REQUIRE(imp.write(*db).ok());
}

// A root whose imports/ holds one of everything, plus a few broken items.
fs::path importRoot(const char* name) {
    const fs::path root = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path in = root / "imports";
    fs::create_directories(in);
    write(in / "classics.pgn", fixture("classics.pgn"));
    write(in / "positions.fen", "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1\nnot a fen\n"
                                "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\n");
    write(in / "study.fen", "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1\n");
    write(in / "cherry.yaml", "name: Cherry\nmode: color\nlight: \"#f0d9b5\"\ndark: \"#b58863\"\n");
    write(in / "broken.yaml", "name: Broken\nligth: \"#ffffff\"\n");
    write(in / "paper.yaml", "name: Paper\n");  // the built-in's name
    gamePackFile(in / "lichess.db");
    puzzlePackFile(in / "mypuzzles.db");
    write(in / "junk.db", "not sqlite");
    // A piece set: the bundled Cburnett SVGs under a new name.
    const fs::path set = in / "mine";
    fs::create_directories(set);
    for (const auto& e : fs::directory_iterator(fs::path(test::fontDir()).parent_path() / "pieces" / "cburnett"))
        if (e.path().extension() == ".svg") fs::copy_file(e.path(), set / e.path().filename());
    write(set / "pieceset.yaml", "name: Mine\nlicense: GPL-2.0-or-later\nscale: 0.9\n");
    // Engines: one for this platform, one built for another CPU.
    const std::string platform = engine::currentPlatformKey();
    const std::string other = platform == "arm" ? "windows-x64" : "arm";
    for (const auto& [dir, bytes] : {std::pair{"fish", fakeBinary(platform)}, std::pair{"wrongfish", fakeBinary(other)}}) {
        write(in / dir / "engine.yaml", std::string("id: ") + dir + "\nname: \"Fish\"\nprotocol: uci\nbinary: { " + platform +
                                            ": fish.bin }\nlicense: MIT\nroles: [analysis]\n");
        write(in / dir / "fish.bin", bytes);
    }
    write(in / "notes.txt", "hello");
    write(in / ".DS_Store", "metadata");
    return root;
}

const ImportItem* findItem(const std::vector<ImportItem>& items, const char* file) {
    for (const ImportItem& it : items)
        if (it.path.filename() == file) return &it;
    return nullptr;
}

struct ImportHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    std::unique_ptr<Application> app;

    explicit ImportHarness(const fs::path& root) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = false;
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    ImportScreen* screen() const { return dynamic_cast<ImportScreen*>(app->top()); }
};

} // namespace

TEST_CASE("import: the scan classifies and validates everything in imports/") {
    const fs::path root = importRoot("pchess-import-scan");
    util::Paths paths(root);
    InstalledNames installed;
    installed.theme = [](const std::string& n) { return n == "Paper"; };
    const auto items = Importer(paths, engine::currentPlatformKey()).scan(installed);
    for (const ImportItem& it : items)
        MESSAGE(it.path.filename().string() + ": " + toString(it.kind) + (it.valid ? " ok " : " no ") + it.detail);
    CHECK_EQ(items.size(), size_t(13));  // .DS_Store is hidden
    auto check = [&](const char* file, ImportKind kind, bool valid, const char* detailPart) {
        const ImportItem* it = findItem(items, file);
        REQUIRE(it != nullptr);
        CHECK(it->kind == kind);
        CHECK_EQ(it->valid, valid);
        if (it->detail.find(detailPart) == std::string::npos) MESSAGE(std::string(file) + ": '" + it->detail + "'");
        CHECK(it->detail.find(detailPart) != std::string::npos);
    };
    check("classics.pgn", ImportKind::Pgn, true, "3 games");
    check("positions.fen", ImportKind::Fen, true, "2 positions (1 invalid)");
    check("cherry.yaml", ImportKind::Theme, true, "color");
    check("broken.yaml", ImportKind::Theme, false, "unknown key 'ligth'");
    check("paper.yaml", ImportKind::Theme, false, "already installed");
    check("lichess.db", ImportKind::GamePack, true, "39 games");
    check("mypuzzles.db", ImportKind::PuzzlePack, true, "60 puzzles");
    check("junk.db", ImportKind::Unknown, false, "not a PocketChess");
    check("mine", ImportKind::PieceSet, true, "GPL-2.0-or-later");
    check("fish", ImportKind::Engine, true, "Fish");
    check("wrongfish", ImportKind::Engine, false, "this reader needs");
    check("notes.txt", ImportKind::Unknown, false, "not a PGN");
    CHECK_EQ(findItem(items, "mine")->name, std::string("Mine"));

    CHECK_EQ(executableFormat(root / "imports" / "fish" / "fish.bin").empty(), false);
    CHECK(executableRunsOn("elf-arm", "arm"));
    CHECK(!executableRunsOn("pe", "arm"));
    CHECK(executableRunsOn("pe", "windows-x64"));
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("import screen: installs go under user/, then show up in the app") {
    const fs::path root = importRoot("pchess-import-install");
    ImportHarness h(root);
    h.app->push(std::make_unique<ImportScreen>(*h.app));
    REQUIRE(h.screen() != nullptr);
    h.app->flush();
    test::checkGolden("import", h.app->canvas());

    auto indexOf = [&](const char* file) {
        const auto& items = h.screen()->items();
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].path.filename() == file) return i;
        return items.size();
    };
    auto installOk = [&](const char* file) {
        const size_t i = indexOf(file);
        REQUIRE(i < h.screen()->items().size());
        h.screen()->install(i);
        CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));  // "Installed ..."
        h.app->pop();
        CHECK_EQ(indexOf(file), h.screen()->items().size());  // moved out of imports/
    };

    installOk("cherry.yaml");
    CHECK(fs::exists(root / "user" / "themes" / "cherry.yaml"));
    CHECK(h.app->themes().find("Cherry") != nullptr);

    installOk("mine");
    CHECK(fs::exists(root / "user" / "pieces" / "mine" / "wK.svg"));
    REQUIRE(h.app->pieceSets().find("Mine") != nullptr);
    h.app->settings().pieceSet = "Mine";
    h.app->applySettings();
    REQUIRE(h.app->pieces().pieceSet() != nullptr);
    CHECK_EQ(h.app->pieces().pieceSet()->name(), std::string("Mine"));

    installOk("lichess.db");
    installOk("mypuzzles.db");
    const auto games = h.app->gamePackNames();
    CHECK(std::find(games.begin(), games.end(), "lichess") != games.end());
    const auto puzzles = h.app->puzzlePackNames();
    CHECK(std::find(puzzles.begin(), puzzles.end(), "mypuzzles") != puzzles.end());
    REQUIRE(h.app->gamePack() != nullptr);  // opened from user/content/games
    CHECK_EQ(h.app->gamePack()->count(), 39);

    installOk("fish");
    CHECK(h.app->engineRegistry().find("fish") != nullptr);

    // An invalid item only explains itself.
    h.screen()->install(indexOf("broken.yaml"));
    CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));
    h.app->pop();
    CHECK(fs::exists(root / "imports" / "broken.yaml"));

    // A PGN becomes a game pack (the PGN stays for Analyze).
    h.screen()->makeGamePack(indexOf("classics.pgn"));
    h.app->pop();
    CHECK(fs::exists(root / "user" / "content" / "games" / "classics.db"));
    CHECK(fs::exists(root / "imports" / "classics.pgn"));
    auto pack = games::GamePack::open(root / "user" / "content" / "games" / "classics.db");
    REQUIRE(pack.ok());
    CHECK_EQ(pack->count(), 3);

    // A one-position FEN file opens straight in Analyze.
    h.screen()->choose(indexOf("study.fen"));
    CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));
    REQUIRE(runScript(*h.app, "key:ok").ok());  // "Open in Analyze"
    CHECK_EQ(std::string(h.app->top()->name()), std::string("analyze"));

    // Nothing under user/ was replaced by a later scan (ADR 0013: deploys and imports keep user/).
    CHECK(fs::exists(root / "user" / "themes" / "cherry.yaml"));
    std::error_code ec;
    h.app.reset();
    fs::remove_all(root, ec);
}
