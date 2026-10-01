// SPDX-License-Identifier: GPL-3.0-or-later
// PgnImporter (architecture doc §12): PGN files (.pgn or .pgn.zst, streamed game by game) -> a game
// pack for content/games/. Games are classified with data/openings (ECO names, by position);
// a malformed game is reported and skipped, the import goes on (design notes §32).
//
//   PgnImporter data/samples/pgn/classics.pgn data/samples/games/classics.db
//   PgnImporter data/downloads/lichess_db_standard_rated_2013-01.pgn.zst data/generated/games/lichess.db --max 20000
#include "core/chess/Pgn.h"
#include "core/games/GamePack.h"
#include "core/util/Files.h"
#include "tools/common/LineReader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace pchess;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: PgnImporter <input.pgn[.zst] | folder>... <out.db> [options]\n"
                 "  folder           every .pgn and .pgn.zst inside it (recursively, sorted)\n"
                 "  --openings dir   ECO names (default: data/openings, searched upwards)\n"
                 "  --max N          stop after N games\n"
                 "  --name text      pack title stored in meta (default: the output file name)\n"
                 "  --from-year Y    skip games dated before Y (undated games are kept)\n"
                 "  --to-year Y      skip games dated after Y\n"
                 "  --min-elo E      skip games where a rated player is below E (unrated players are kept)\n"
                 "  --skip-online    skip games played on servers (Lichess, Chess.com, ICC, playchess ...)\n");
    return 2;
}

// Which games a pack keeps (historical collections: over-the-board, by period, by strength).
struct Selection {
    int fromYear = 0, toYear = 0, minElo = 0;
    bool skipOnline = false;
    uint64_t skipped = 0;

    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    bool keep(const chess::PgnGame& g) {
        std::string date = g.header("Date");
        if (date.empty() || date[0] == '?') date = g.header("UTCDate");
        const int year = date.size() >= 4 && std::isdigit(static_cast<unsigned char>(date[0])) ? std::atoi(date.c_str()) : 0;
        bool ok = true;
        if (year > 0 && fromYear > 0 && year < fromYear) ok = false;
        if (year > 0 && toYear > 0 && year > toYear) ok = false;
        for (const char* side : {"WhiteElo", "BlackElo"}) {
            const int elo = std::atoi(g.header(side).c_str());
            if (minElo > 0 && elo > 0 && elo < minElo) ok = false;
        }
        if (ok && skipOnline) {
            const std::string where = lower(g.header("Site") + " " + g.header("Event"));
            for (const char* server : {"lichess", "chess.com", "chesscom", "icc", "internet chess club", "playchess",
                                       "chess24", "fics", "online", "titled arena", "titled tuesday", "bullet"})
                if (where.find(server) != std::string::npos) ok = false;
            if (lower(g.header("Site")) == "int") ok = false;  // the ChessBase convention for Internet games
        }
        if (!ok) ++skipped;
        return ok;
    }
};

// Inputs: files as given; folders expanded to their .pgn / .pgn.zst files (sorted, recursive).
std::vector<std::filesystem::path> expandInputs(const std::vector<std::filesystem::path>& args) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& a : args) {
        if (!std::filesystem::is_directory(a, ec)) {
            out.push_back(a);
            continue;
        }
        std::vector<std::filesystem::path> found;
        for (const auto& e : std::filesystem::recursive_directory_iterator(a, ec)) {
            const std::string n = Selection::lower(e.path().filename().string());
            if (e.is_regular_file(ec) && (n.ends_with(".pgn") || n.ends_with(".pgn.zst"))) found.push_back(e.path());
        }
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

std::filesystem::path findUp(const std::filesystem::path& rel) {
    std::error_code ec;
    for (auto dir = std::filesystem::current_path(ec); !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / rel, ec)) return dir / rel;
        if (dir == dir.parent_path()) break;
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> paths;
    std::filesystem::path openingsDir;
    long long maxGames = -1;
    std::string title;
    Selection select;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--openings") openingsDir = value();
        else if (a == "--max") maxGames = std::atoll(value());
        else if (a == "--name") title = value();
        else if (a == "--from-year") select.fromYear = std::atoi(value());
        else if (a == "--to-year") select.toYear = std::atoi(value());
        else if (a == "--min-elo") select.minElo = std::atoi(value());
        else if (a == "--skip-online") select.skipOnline = true;
        else if (a.rfind("--", 0) == 0) return usage();
        else paths.emplace_back(a);
    }
    if (paths.size() < 2) return usage();
    const std::filesystem::path output = paths.back();
    paths.pop_back();
    paths = expandInputs(paths);
    if (paths.empty()) return usage();
    std::printf("%zu input file(s)\n", paths.size());

    games::OpeningBook book;
    if (openingsDir.empty()) openingsDir = findUp("data/openings");
    for (const char* f : {"a.tsv", "b.tsv", "c.tsv", "d.tsv", "e.tsv"}) {
        auto text = util::readTextFile(openingsDir / f);
        if (!text) {
            std::fprintf(stderr, "openings: %s (use --openings)\n", text.error().message().c_str());
            return 1;
        }
        (void)book.addTsv(*text);
    }
    std::printf("openings: %zu positions\n", book.size());

    std::error_code ec;
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path(), ec);
    std::filesystem::remove(output, ec);
    auto db = storage::Database::open(output, storage::Database::Mode::ReadWrite);
    if (!db) {
        std::fprintf(stderr, "%s\n", db.error().message().c_str());
        return 1;
    }
    (void)db->exec("PRAGMA journal_mode=DELETE");  // a content pack: one file, read-only on the device
    games::GameImporter importer(*db, &book);
    if (auto r = importer.begin(); !r) {
        std::fprintf(stderr, "%s\n", r.error().message().c_str());
        return 1;
    }

    const auto t0 = std::chrono::steady_clock::now();
    uint64_t rejected = 0;
    std::vector<std::string> firstRejects;
    bool stop = false;
    for (const auto& input : paths) {
        std::string game;
        bool inMoves = false;
        uint64_t gameLine = 0, line = 0;
        auto flush = [&]() -> bool {
            if (game.find_first_not_of(" \r\n\t") == std::string::npos) return true;
            auto parsed = chess::parsePgnGame(game);
            game.clear();
            if (!parsed) {
                ++rejected;
                if (firstRejects.size() < 10)
                    firstRejects.push_back(input.filename().string() + ":" + std::to_string(gameLine) + ": " +
                                           parsed.error().message());
                return true;
            }
            if (!select.keep(*parsed)) return true;
            if (auto r = importer.add(*parsed); !r) {
                std::fprintf(stderr, "%s\n", r.error().message().c_str());
                return false;
            }
            if (importer.stats().games % 10'000 == 0 && importer.stats().games)
                std::printf("  %llu games...\n", static_cast<unsigned long long>(importer.stats().games));
            return !(maxGames > 0 && static_cast<long long>(importer.stats().games) >= maxGames);
        };
        auto read = tools::forEachLine(input, [&](std::string_view l) {
            ++line;
            const bool header = !l.empty() && l.front() == '[';
            if (header && inMoves) {  // the next game starts
                inMoves = false;
                if (!flush()) {
                    stop = true;
                    return false;
                }
            }
            if (game.empty()) gameLine = line;
            if (!header && !l.empty()) inMoves = true;
            game.append(l).push_back('\n');
            return true;
        });
        if (!read) {
            std::fprintf(stderr, "%s\n", read.error().message().c_str());
            return 1;
        }
        if (!stop && !flush()) stop = true;
        if (stop) break;
    }

    std::map<std::string, std::string> meta = {{"title", title.empty() ? output.stem().string() : title}};
    std::string inputs;
    for (const auto& p : paths) inputs += (inputs.empty() ? "" : ", ") + p.filename().string();
    if (paths.size() > 20) inputs = std::to_string(paths.size()) + " files from " + paths.front().parent_path().string();
    if (paths.size() > 20)
        inputs = std::to_string(paths.size()) + " files from " + paths.front().parent_path().string();
    meta["rejected"] = std::to_string(rejected);
    meta["selection"] = "years " + std::to_string(select.fromYear) + "-" + std::to_string(select.toYear) + ", min Elo " +
                        std::to_string(select.minElo) + (select.skipOnline ? ", over the board only" : "") + ", skipped " +
                        std::to_string(select.skipped);
    if (auto info = util::readTextFile(paths.front().string() + ".info", 1 << 16); info) meta["download_info"] = *info;
    if (auto r = importer.finish(meta); !r) {
        std::fprintf(stderr, "%s\n", r.error().message().c_str());
        return 1;
    }
    if (auto v = db->exec("VACUUM"); !v) std::fprintf(stderr, "VACUUM: %s\n", v.error().message().c_str());
    db->close();
    const auto& s = importer.stats();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: %llu games (%llu annotated, %llu players, %llu duplicates, %llu skipped, %llu rejected) in "
                "%.1f s, %.1f MB\n",
                output.string().c_str(), static_cast<unsigned long long>(s.games),
                static_cast<unsigned long long>(s.annotated), static_cast<unsigned long long>(s.players),
                static_cast<unsigned long long>(s.duplicates), static_cast<unsigned long long>(s.skipped),
                static_cast<unsigned long long>(rejected), secs,
                static_cast<double>(std::filesystem::file_size(output, ec)) / (1024.0 * 1024.0));
    if (select.skipped)
        std::printf("  %llu games outside the selection (years, Elo, online)\n",
                    static_cast<unsigned long long>(select.skipped));
    for (const auto& r : firstRejects) std::printf("  rejected %s\n", r.c_str());
    return 0;
}
