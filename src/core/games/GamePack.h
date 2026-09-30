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
};

// Writes games into an empty pack database. Call add() for every parsed game, then finish().
// Everything happens inside one transaction (a crash leaves no half pack).
class GameImporter {
public:
    GameImporter(storage::Database& db, const OpeningBook* openings);
    Result<void> begin();  // creates the schema and opens the transaction
    Result<bool> add(const chess::PgnGame& game);  // false: skipped
    Result<void> finish(const std::map<std::string, std::string>& meta);  // commit + ANALYZE
    const ImportStats& stats() const { return stats_; }

private:
    Result<int64_t> playerId(const std::string& name);
    storage::Database& db_;
    const OpeningBook* openings_;
    std::optional<storage::Statement> insertGame_, insertPlayer_;
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

struct GameRow {
    int64_t id = 0;
    std::string white, black, event, site, date, round, result, eco, opening;
    int whiteElo = 0, blackElo = 0, plies = 0;
};

class GamePack {
public:
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

private:
    storage::Database db_;
    std::string name_;
    int count_ = 0;
};

} // namespace pchess::games
