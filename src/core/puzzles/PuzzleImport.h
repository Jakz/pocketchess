// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Lichess puzzle CSV -> puzzle pack (architecture doc §9 and §12). Lives in core so the device
// self-test can build its fixture pack; the desktop PuzzleImporter adds zstd streaming and the CLI.

#include "core/puzzles/Puzzle.h"
#include "core/storage/Database.h"
#include "core/util/Random.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pchess::puzzles {

// Schema of a puzzle pack (content/puzzles/<pack>.db, read-only on the device).
std::span<const storage::Migration> puzzlePackMigrations();

struct ImportFilter {
    int minRating = 0, maxRating = 4000;
    int minPopularity = -100;  // Lichess popularity is -100..100
    int minPlays = 0;
    std::vector<std::string> includeThemes;  // all must be present (empty = any)
    std::vector<std::string> excludeThemes;  // none may be present
    size_t count = 50'000;                   // design notes §9.2: Starter 5k, Standard 25k, Large 50k, Huge 100k
    uint64_t seed = 1;                       // same input + filter + seed = same pack
};

struct ImportStats {
    uint64_t rows = 0;       // data rows read (header excluded)
    uint64_t rejected = 0;   // malformed: bad CSV, FEN or move
    uint64_t filtered = 0;   // valid but excluded by the filter
    uint64_t candidates = 0; // passed the filter (the reservoir samples `count` of them)
    std::vector<std::string> firstRejects;  // up to 10 "line N: reason" messages
};

// Feed CSV lines in order (the header line is detected and skipped); then write the pack.
class PuzzleImporter {
public:
    explicit PuzzleImporter(ImportFilter filter);

    void addLine(std::string_view line);
    const ImportStats& stats() const { return stats_; }

    // Creates the schema in an empty database and inserts the sampled puzzles sorted by rating,
    // plus meta (source, date, filter, counts, `extraMeta`). Runs ANALYZE; VACUUM is left to the
    // caller (the database must not be inside a transaction).
    Result<size_t> write(storage::Database& db, const std::map<std::string, std::string>& extraMeta = {});

private:
    struct Row {
        Puzzle puzzle;
        std::vector<std::string> themes;
    };
    bool parse(std::string_view line, Row& out, std::string& why) const;
    bool accepted(const Row& r) const;
    void reject(const std::string& why);

    ImportFilter filter_;
    ImportStats stats_;
    util::Random random_;
    std::vector<Row> reservoir_;
    uint64_t line_ = 0;
    bool headerSeen_ = false;
};

} // namespace pchess::puzzles
