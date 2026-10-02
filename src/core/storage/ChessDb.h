// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// user/chess.db (architecture doc §9): settings, played games, favorites. Never shipped or
// overwritten by deploy/staging (AGENTS.md "Data").

#include "core/storage/Database.h"

#include <map>
#include <span>
#include <optional>
#include <string>
#include <vector>

namespace pchess::storage {

std::span<const Migration> chessDbMigrations();

// Opens (creating if needed) and migrates to the latest schema.
Result<Database> openChessDb(const std::filesystem::path& file);

// settings(key, value): the app serializes its Settings struct into these strings.
Result<std::map<std::string, std::string>> loadSettings(Database& db);
// Upserts every pair in one transaction.
Result<void> saveSettings(Database& db, const std::map<std::string, std::string>& values);

// session: SessionState of the resumable screen (design notes §30, ADR 0011). saveSession
// replaces the whole table in one transaction; an empty table means "nothing to resume".
Result<std::map<std::string, std::string>> loadSession(Database& db);
Result<void> saveSession(Database& db, const std::map<std::string, std::string>& values);
Result<void> clearSession(Database& db);

// played_games: finished games (Play mode), newest first when listed.
struct PlayedGame {
    int64_t id = 0;
    int64_t date = 0;  // unix seconds
    std::string pgn;
    std::string result;  // 1-0, 0-1, 1/2-1/2, *
    std::string mode = "play";
    std::string strength;
    std::string engine;
    std::string human;  // "white" / "black" (the user's side); empty for games from before v3
};
Result<int64_t> addPlayedGame(Database& db, const PlayedGame& game);  // returns the new id
Result<std::vector<PlayedGame>> recentPlayedGames(Database& db, int limit);

// favorites(pack, game_id): games starred in the Games screens (design notes §13.3).
Result<void> setFavorite(Database& db, const std::string& pack, int64_t gameId, bool on, int64_t now);
Result<bool> isFavorite(Database& db, const std::string& pack, int64_t gameId);
Result<std::vector<int64_t>> favoriteIds(Database& db, const std::string& pack);

// active_games: Play games in progress, most recently played first.
struct ActiveGame {
    int64_t id = 0;  // 0 = not saved yet (saveActiveGame inserts and sets it)
    int64_t started = 0, lastPlayed = 0;
    std::string human;  // white / black
    std::string strength, engine, fen, moves;
    bool flipped = false;
};
Result<int64_t> saveActiveGame(Database& db, const ActiveGame& game);  // insert or update; returns the id
Result<void> deleteActiveGame(Database& db, int64_t id);
Result<std::vector<ActiveGame>> activeGames(Database& db);
Result<std::optional<ActiveGame>> activeGame(Database& db, int64_t id);

// Totals from the user's side (games with a known side and a result), for Home.
struct PlayStats {
    int games = 0, won = 0, drawn = 0, lost = 0;
};
Result<PlayStats> playStats(Database& db);
Result<int> playedGameCount(Database& db);
// Every played game, oldest first (the "My games" pack is built in this order: stable ids).
Result<std::vector<PlayedGame>> allPlayedGames(Database& db);

// guess_runs / guess_moves: Guess the move (M7). A run is saved after every guessed move, so an
// interrupted run keeps its score and can be resumed (session) or counted (stats).
struct GuessRun {
    int64_t id = 0;  // 0 = not saved yet
    std::string pack;
    int64_t gameId = 0;
    std::string side;  // white / black / both
    std::string title;
    int64_t started = 0, updated = 0;
    int fromPly = 0, ply = 0, plies = 0;
    int first = 0, second = 0, missed = 0;
    bool finished = false;
};
Result<int64_t> saveGuessRun(Database& db, const GuessRun& run);  // insert or update; returns the id
// outcome: 0 first try, 1 second try / hint, 2 missed. `wrong`: the first wrong try (UCI) or "".
Result<void> addGuessMove(Database& db, int64_t runId, int ply, int outcome, const std::string& wrong, bool hinted);
Result<std::optional<GuessRun>> guessRun(Database& db, int64_t id);

// Totals over every run (finished or not).
struct GuessStats {
    int runs = 0, finished = 0, first = 0, second = 0, missed = 0;
    int guessed() const { return first + second + missed; }
    int percent() const { return guessed() ? (200 * first + 100 * second) / (2 * guessed()) : 0; }
};
Result<GuessStats> guessStats(Database& db);

// mistakes / mistake_scans: My mistakes (M7). Finished games with a known side that no scan has
// checked yet, oldest first; a scan stores its mistakes and marks the game in one transaction.
struct StoredMistake {
    int64_t id = 0, gameId = 0;
    int ply = 0;
    std::string fen, moves, played;  // moves: UCI, space separated (puzzle format)
    int before = 0, after = 0;
    int64_t created = 0;
};
Result<std::vector<PlayedGame>> uncheckedPlayedGames(Database& db);
Result<void> saveMistakeScan(Database& db, int64_t gameId, const std::vector<StoredMistake>& found,
                             const std::string& engine, int64_t now);
Result<std::vector<StoredMistake>> mistakes(Database& db);  // oldest first

// drill_runs / drill_moves: opening practice (M7). A run is saved after every move of the user.
struct DrillRun {
    int64_t id = 0;  // 0 = not saved yet
    std::string pack, side, start, opening;
    int64_t started = 0, updated = 0;
    int moves = 0, main = 0, book = 0, missed = 0;
    bool finished = false;
};
struct DrillMove {
    int ply = 0;
    std::string fen, played;
    int outcome = 0;  // 0 main, 1 book, 2 missed
    int share = 0;
    std::string wrong;
    bool hinted = false;
};
Result<int64_t> saveDrillRun(Database& db, const DrillRun& run);  // insert or update; returns the id
Result<void> addDrillMove(Database& db, int64_t runId, const DrillMove& move);
struct DrillStats {
    int runs = 0, main = 0, book = 0, missed = 0;
};
Result<DrillStats> drillStats(Database& db);

// Statistics screen.
struct StrengthStats {
    std::string strength;
    int games = 0, won = 0, drawn = 0, lost = 0;
};
Result<std::vector<StrengthStats>> playStatsByStrength(Database& db);  // by games played, most first
struct RunSummary {
    std::string title;  // guess: "White – Black, 1858"; drill: the opening (or the start moves)
    std::string side;
    int64_t updated = 0;
    int a = 0, b = 0, c = 0;  // guess: first, second, missed; drill: main, book, missed
    bool finished = false;
};
Result<std::vector<RunSummary>> recentGuessRuns(Database& db, int limit);
Result<std::vector<RunSummary>> recentDrillRuns(Database& db, int limit);
struct MistakeStats {
    int gamesChecked = 0, mistakes = 0;
};
Result<MistakeStats> mistakeStats(Database& db);

} // namespace pchess::storage
