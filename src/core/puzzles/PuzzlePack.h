// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/puzzles/Puzzle.h"
#include "core/storage/Database.h"
#include "core/util/Random.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pchess::puzzles {

// A puzzle pack (content/puzzles/<pack>.db), opened read-only and immutable (architecture doc §9).
class PuzzlePack {
public:
    static Result<PuzzlePack> open(const std::filesystem::path& file);
    // Tests: a pack already built in `db` (e.g. an in-memory import).
    static Result<PuzzlePack> fromDatabase(storage::Database db, std::string name);

    const std::string& name() const { return name_; }  // file stem, e.g. "large"
    int count() const { return count_; }
    // Theme name per bit ("" for unused bits).
    const std::vector<std::string>& themeNames() const { return themes_; }
    uint64_t themeMask(const std::vector<std::string>& names) const;  // unknown names are ignored
    std::vector<std::string> themesOf(uint64_t mask) const;

    Result<std::optional<Puzzle>> byId(const std::string& id);
    // Up to `limit` puzzles with rating in [lo, hi], in (rating, rnd) order, whose themes contain
    // all of `include` and none of `exclude`. Index-bounded (idx_puzzles_rating_rnd).
    // `after` continues a previous batch: only puzzles after that (rating, rnd).
    Result<std::vector<Puzzle>> batch(int lo, int hi, uint64_t include, uint64_t exclude, int limit,
                                      std::optional<std::pair<int, uint32_t>> after = std::nullopt);

private:
    Result<void> load();
    storage::Database db_;
    std::string name_;
    int count_ = 0;
    std::vector<std::string> themes_;
};

struct SelectRequest {
    int rating = 1500;                                // the user's rating
    uint64_t include = 0, exclude = 0;                // theme masks (Settings -> Tactics)
    std::function<bool(const std::string&)> skip;    // e.g. already seen (progress.db)
};

// Adaptive choice (design notes §9.3): rating ±150, widened to ±300 and then to any rating when
// nothing unskipped is left. Each window starts at a random rating so the whole window is used.
class PuzzleSelector {
public:
    PuzzleSelector(PuzzlePack& pack, util::Random& random) : pack_(pack), random_(random) {}
    Result<std::optional<Puzzle>> next(const SelectRequest& req);

private:
    PuzzlePack& pack_;
    util::Random& random_;
};

} // namespace pchess::puzzles
