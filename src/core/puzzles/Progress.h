// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Tactics progress (architecture doc §9–10, design notes §10–11, §25): the Elo-like rating,
// its history, and the per-puzzle review schedule, in user/progress.db.

#include "core/puzzles/Puzzle.h"
#include "core/storage/Database.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace pchess::puzzles {

constexpr int kInitialRating = 1500;
constexpr int kRatingK = 24;

// Elo expectation of the user against the puzzle, and the new rating after a binary result.
double expectedScore(int userRating, int puzzleRating);
int updatedRating(int userRating, int puzzleRating, bool solved, int k = kRatingK);

// Spaced repetition (design notes §11): a failure schedules a review in 1 day; each later
// success moves along 1 -> 3 -> 7 -> 21 -> 60 days (60 stays). A puzzle solved at the first try
// never enters review.
struct ReviewState {
    int attempts = 0, successes = 0, failures = 0;
    int intervalDays = 0;                // 0 = not in review
    std::optional<int64_t> nextReview;   // unix seconds
    std::optional<int64_t> lastAttempt;
    std::optional<bool> lastSolved;
};
ReviewState scheduleAfter(const ReviewState& before, bool solved, int64_t now);

struct RecordResult {
    int ratingBefore = kInitialRating, ratingAfter = kInitialRating;
    ReviewState state;
};

std::span<const storage::Migration> progressMigrations();

class Progress {
public:
    static Result<Progress> open(const std::filesystem::path& file);  // creates and migrates
    static Result<Progress> fromDatabase(storage::Database db);       // tests, in-memory

    int rating();
    // One finished attempt. `rated`: the first attempt of a new puzzle (reviews do not move the
    // rating). All updates happen in one transaction.
    Result<RecordResult> record(const std::string& pack, const Puzzle& p, bool solved, bool rated, int64_t now);

    Result<std::optional<ReviewState>> state(const std::string& puzzleId);
    Result<std::unordered_set<std::string>> seenIds();
    Result<int> dueCount(int64_t now);
    Result<std::vector<std::string>> dueIds(int64_t now, int limit);  // most overdue first

    struct RatingPoint {
        int64_t when = 0;
        int rating = 0;
    };
    Result<std::vector<RatingPoint>> history(int limit);  // newest first

    storage::Database& db() { return db_; }

private:
    storage::Database db_;
};

} // namespace pchess::puzzles
