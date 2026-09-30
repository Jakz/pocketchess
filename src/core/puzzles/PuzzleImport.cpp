// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/PuzzleImport.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveCodec.h"
#include "core/chess/San.h"

#include <algorithm>
#include <charconv>

namespace pchess::puzzles {

namespace {

constexpr storage::Migration kPackSteps[] = {
    {1, R"sql(
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT) WITHOUT ROWID;   -- source, date, filter, counts
CREATE TABLE themes(bit INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL);
CREATE TABLE puzzles(
    id TEXT PRIMARY KEY,                -- Lichess PuzzleId
    fen TEXT NOT NULL,                  -- before the opponent's first move (P12)
    moves BLOB NOT NULL,                -- 16-bit MoveCodec, first move = opponent
    rating INTEGER NOT NULL,
    rd INTEGER, popularity INTEGER, plays INTEGER,
    themes INTEGER NOT NULL,            -- bitmask into themes(bit)
    opening TEXT,
    rnd INTEGER NOT NULL                -- random 31-bit, fixed at import
) WITHOUT ROWID;
CREATE INDEX idx_puzzles_rating_rnd ON puzzles(rating, rnd);
)sql"},
};

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    size_t i = 0;
    for (;;) {
        const size_t j = s.find(sep, i);
        out.push_back(s.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i));
        if (j == std::string_view::npos) break;
        i = j + 1;
    }
    return out;
}

bool toInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

} // namespace

std::span<const storage::Migration> puzzlePackMigrations() { return kPackSteps; }

PuzzleImporter::PuzzleImporter(ImportFilter filter) : filter_(std::move(filter)), random_(filter_.seed) {
    reservoir_.reserve(std::min<size_t>(filter_.count, 200'000));
}

void PuzzleImporter::reject(const std::string& why) {
    ++stats_.rejected;
    if (stats_.firstRejects.size() < 10) stats_.firstRejects.push_back("line " + std::to_string(line_) + ": " + why);
}

bool PuzzleImporter::parse(std::string_view line, Row& out, std::string& why) const {
    // PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags
    const auto f = split(line, ',');
    if (f.size() < 8) {
        why = "expected at least 8 fields, got " + std::to_string(f.size());
        return false;
    }
    Puzzle& p = out.puzzle;
    p.id = std::string(f[0]);
    p.fen = std::string(f[1]);
    if (p.id.empty()) {
        why = "empty PuzzleId";
        return false;
    }
    if (!toInt(f[3], p.rating) || !toInt(f[4], p.ratingDeviation) || !toInt(f[5], p.popularity) ||
        !toInt(f[6], p.plays)) {
        why = p.id + ": bad number";
        return false;
    }
    auto pos = chess::parseFen(p.fen);
    if (!pos) {
        why = p.id + ": " + pos.error().message();
        return false;
    }
    chess::Position board = pos.value();
    p.moves.clear();
    for (std::string_view uci : split(f[2], ' ')) {
        if (uci.empty()) continue;
        auto m = chess::parseUci(board, uci);
        if (!m) {
            why = p.id + ": " + m.error().message();
            return false;
        }
        board.makeMove(m.value());
        p.moves.push_back(m.value());
    }
    if (p.moves.size() < 2) {
        why = p.id + ": fewer than 2 moves";
        return false;
    }
    out.themes.clear();
    for (std::string_view t : split(f[7], ' '))
        if (!t.empty()) out.themes.emplace_back(t);
    p.opening = f.size() > 9 ? std::string(f[9]) : std::string();
    return true;
}

bool PuzzleImporter::accepted(const Row& r) const {
    const Puzzle& p = r.puzzle;
    if (p.rating < filter_.minRating || p.rating > filter_.maxRating) return false;
    if (p.popularity < filter_.minPopularity || p.plays < filter_.minPlays) return false;
    auto has = [&](const std::string& t) { return std::find(r.themes.begin(), r.themes.end(), t) != r.themes.end(); };
    for (const auto& t : filter_.includeThemes)
        if (!has(t)) return false;
    for (const auto& t : filter_.excludeThemes)
        if (has(t)) return false;
    return true;
}

void PuzzleImporter::addLine(std::string_view line) {
    ++line_;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    if (line.empty()) return;
    if (!headerSeen_) {
        headerSeen_ = true;
        if (line.rfind("PuzzleId,", 0) == 0) return;
    }
    ++stats_.rows;
    Row row;
    std::string why;
    if (!parse(line, row, why)) return reject(why);
    if (!accepted(row)) {
        ++stats_.filtered;
        return;
    }
    // Reservoir sampling (algorithm R): a uniform sample of `count` among all candidates.
    const uint64_t n = ++stats_.candidates;
    if (reservoir_.size() < filter_.count) {
        reservoir_.push_back(std::move(row));
    } else {
        const uint64_t j = random_.next() % n;
        if (j < filter_.count) reservoir_[static_cast<size_t>(j)] = std::move(row);
    }
}

Result<size_t> PuzzleImporter::write(storage::Database& db, const std::map<std::string, std::string>& extraMeta) {
    if (auto r = storage::migrate(db, puzzlePackMigrations()); !r) return r.error();

    // Theme bits by frequency in the pack (most frequent first); at most 64.
    std::map<std::string, uint64_t> freq;
    for (const Row& r : reservoir_)
        for (const auto& t : r.themes) ++freq[t];
    std::vector<std::pair<std::string, uint64_t>> byFreq(freq.begin(), freq.end());
    std::stable_sort(byFreq.begin(), byFreq.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::map<std::string, int> bit;
    for (size_t i = 0; i < byFreq.size() && i < 64; ++i) bit[byFreq[i].first] = static_cast<int>(i);

    std::sort(reservoir_.begin(), reservoir_.end(), [](const Row& a, const Row& b) {
        return a.puzzle.rating != b.puzzle.rating ? a.puzzle.rating < b.puzzle.rating : a.puzzle.id < b.puzzle.id;
    });

    auto r = db.transaction([&]() -> Result<void> {
        auto th = db.prepare("INSERT INTO themes(bit, name) VALUES(?1, ?2)");
        if (!th) return th.error();
        for (const auto& [name, b] : bit) {
            th->bind(1, b).bind(2, name);
            if (auto e = th->run(); !e) return e;
        }
        auto ins = db.prepare("INSERT INTO puzzles(id, fen, moves, rating, rd, popularity, plays, themes, opening, rnd) "
                              "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");
        if (!ins) return ins.error();
        for (Row& row : reservoir_) {
            Puzzle& p = row.puzzle;
            p.themes = 0;
            for (const auto& t : row.themes)
                if (auto it = bit.find(t); it != bit.end()) p.themes |= uint64_t(1) << it->second;
            p.rnd = static_cast<uint32_t>(random_.next() & 0x7fffffff);
            const auto blob = chess::encodeMoves(p.moves);
            ins->bind(1, p.id).bind(2, p.fen).bindBlob(3, blob).bind(4, p.rating).bind(5, p.ratingDeviation);
            ins->bind(6, p.popularity).bind(7, p.plays).bind(8, static_cast<int64_t>(p.themes));
            if (p.opening.empty()) ins->bindNull(9);
            else ins->bind(9, p.opening);
            ins->bind(10, static_cast<int64_t>(p.rnd));
            if (auto e = ins->run(); !e) return e;
        }
        auto meta = db.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2)");
        if (!meta) return meta.error();
        std::map<std::string, std::string> m = {
            {"source", "lichess puzzles (CC0)"},
            {"count", std::to_string(reservoir_.size())},
            {"rows_read", std::to_string(stats_.rows)},
            {"rows_rejected", std::to_string(stats_.rejected)},
            {"candidates", std::to_string(stats_.candidates)},
            {"filter", "rating " + std::to_string(filter_.minRating) + "-" + std::to_string(filter_.maxRating) +
                           ", popularity >= " + std::to_string(filter_.minPopularity) +
                           ", plays >= " + std::to_string(filter_.minPlays)},
            {"seed", std::to_string(filter_.seed)},
            {"themes_dropped", std::to_string(byFreq.size() > 64 ? byFreq.size() - 64 : 0)},
        };
        for (const auto& [k, v] : extraMeta) m[k] = v;
        for (const auto& [k, v] : m) {
            meta->bind(1, k).bind(2, v);
            if (auto e = meta->run(); !e) return e;
        }
        return {};
    });
    if (!r) return r.error();
    if (auto a = db.exec("ANALYZE"); !a) return a.error();
    return reservoir_.size();
}

} // namespace pchess::puzzles
