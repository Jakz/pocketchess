// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Historical game packs (content/games/<pack>.db, design notes §13 and §26, architecture doc §9 and
// §11): the importer used by the desktop PgnImporter, and the read side used by the Games screens.

#include "core/chess/Pgn.h"
#include "core/games/Openings.h"
#include "core/storage/Database.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pchess::games {

std::span<const storage::Migration> gamePackMigrations();

// "Carlsen, Magnus" -> "carlsen magnus": ASCII letters/digits lowercased, other bytes kept,
// punctuation and repeated spaces folded. Used for player search.
std::string normalizePlayer(std::string_view name);

struct ImportStats {
    uint64_t games = 0;     // imported
    uint64_t skipped = 0;   // no moves
    uint64_t annotated = 0; // stored with their PGN text (comments, NAGs or variations)
    uint64_t players = 0;
    uint64_t duplicates = 0;  // the same game seen before in this import
    uint64_t bookMoves = 0;   // rows of opening statistics written
};

// Writes games into an empty pack database. Call add() for every parsed game, then finish().
// Everything happens inside one transaction (a crash leaves no half pack).
class GameImporter {
public:
    GameImporter(storage::Database& db, const OpeningBook* openings);
    Result<void> begin();  // creates the schema and opens the transaction
    // false: skipped. Machine annotations ([%clk], [%eval] ...) are removed from `game` first.
    Result<bool> add(chess::PgnGame& game);
    Result<void> finish(const std::map<std::string, std::string>& meta);  // commit + ANALYZE
    const ImportStats& stats() const { return stats_; }

private:
    Result<int64_t> playerId(const std::string& name);
    Result<int64_t> lookupId(std::unordered_map<std::string, int64_t>& ids, storage::Statement& insert,
                             const std::string& key, const std::string& second);
    storage::Database& db_;
    const OpeningBook* openings_;
    std::optional<storage::Statement> insertGame_, insertPlayer_, insertEvent_, insertSite_, insertOpening_;
    std::unordered_map<std::string, int64_t> eventIds_, siteIds_, openingIds_;
    std::unordered_set<size_t> seen_;  // hashes of (players, date, moves): duplicates are skipped
    // Opening statistics (book table): (position key, move) -> results, for the first kBookPlies.
    struct BookKey {
        uint64_t key;
        uint16_t move;
        bool operator==(const BookKey&) const = default;
    };
    struct BookKeyHash {
        size_t operator()(const BookKey& k) const {
            return static_cast<size_t>(k.key ^ (uint64_t(k.move) * 0x9E3779B97F4A7C15ull));
        }
    };
    struct BookCount {
        uint32_t games = 0, white = 0, draws = 0, black = 0;
    };
    std::unordered_map<BookKey, BookCount, BookKeyHash> book_;
    std::unordered_map<std::string, int64_t> players_;
    ImportStats stats_;
};

// Browser filters (design notes §13.3). Empty fields do not filter.
struct GameFilter {
    std::string player;    // either side, substring of the normalized name
    std::string opponent;  // with `player`: the other side
    int yearFrom = 0, yearTo = 0;
    std::string event;     // substring
    std::string eco;       // prefix: "B" or "B90"
    std::string opening;   // substring of the opening name
    std::string result;    // "1-0", "0-1", "1/2-1/2"
    std::optional<std::vector<int64_t>> ids;  // only these games (favorites)
};

// One move of the opening statistics at a position: how often it was played and how those games ended.
struct BookMove {
    chess::Move move;
    int games = 0, white = 0, draws = 0, black = 0;
};

struct GameRow {
    int64_t id = 0;
    std::string white, black, event, site, date, round, result, eco, opening;
    int whiteElo = 0, blackElo = 0, plies = 0;
};

class GamePack {
public:
    // Filtered counts stop here (shown as "10,000+"): a filter matching most of a multi-million pack
    // must not scan it all. The unfiltered total is exact (meta).
    static constexpr int kCountCap = 10000;
    static Result<GamePack> open(const std::filesystem::path& file);  // read-only
    static Result<GamePack> fromDatabase(storage::Database db, std::string name);

    const std::string& name() const { return name_; }
    int count() const { return count_; }
    Result<int> count(const GameFilter& f);
    // Newest first (year, then id), `limit` rows from `offset`.
    Result<std::vector<GameRow>> page(const GameFilter& f, int offset, int limit);
    Result<std::optional<GameRow>> row(int64_t id);
    // The full game: headers, moves, and comments/NAGs/variations when the pack kept them.
    Result<chess::PgnGame> load(int64_t id);
    std::map<std::string, std::string> meta();
    // The opening statistics of `pos`, most played first (empty when the pack has none).
    Result<std::vector<BookMove>> bookMoves(const chess::Position& pos);

private:
    storage::Database db_;
    std::string name_;
    int count_ = 0;
};

} // namespace pchess::games
