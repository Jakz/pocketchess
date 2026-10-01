// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/PuzzleImport.h"

#include "core/chess/Fen.h"
#include "core/chess/FenCodec.h"
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
    // v2 (packs of millions): the position as a packed FEN, moves one byte each, opening tags once,
    // no RD/plays (used only as import filters).
    {2, R"sql(
DROP TABLE puzzles;
CREATE TABLE openings(id INTEGER PRIMARY KEY, tags TEXT NOT NULL);  -- Lichess OpeningTags
CREATE TABLE puzzles(
    id         TEXT PRIMARY KEY,        -- Lichess PuzzleId
    pos        BLOB NOT NULL,           -- packFen, before the opponent's first move (P12)
    moves      BLOB NOT NULL,           -- encodeMoveIndexes, first move = opponent
    rating     INTEGER NOT NULL,
    popularity INTEGER,
    themes     INTEGER NOT NULL,        -- bitmask into themes(bit)
    opening_id INTEGER REFERENCES openings(id),
    rnd        INTEGER NOT NULL         -- random 31-bit, fixed at import
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

bool PuzzleImporter::parse(std::string_view line, Parsed& out, std::string& why) const {
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

bool PuzzleImporter::accepted(const Parsed& r) const {
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

bool PuzzleImporter::pack(Parsed& in, Row& out, std::string& why) {
    Puzzle& p = in.puzzle;
    auto pos = chess::packFen(p.fen);
    if (!pos) {
        why = p.id + ": " + pos.error().message();
        return false;
    }
    auto start = chess::parseFen(p.fen);
    if (!start) {
        why = p.id + ": " + start.error().message();
        return false;
    }
    auto moves = chess::encodeMoveIndexes(start.value(), p.moves);
    if (!moves) {
        why = p.id + ": " + moves.error().message();
        return false;
    }
    out.id = std::move(p.id);
    out.pos = std::move(*pos);
    out.moves.assign(moves->begin(), moves->end());
    out.rating = p.rating;
    out.popularity = p.popularity;
    for (const std::string& t : in.themes) {
        auto [it, added] = themeIndex_.emplace(t, static_cast<int>(themeNames_.size()));
        if (added) themeNames_.push_back(t);
        if (it->second < 128) out.themes[it->second / 64] |= uint64_t(1) << (it->second % 64);
    }
    if (!p.opening.empty()) {
        auto [it, added] = openingIndex_.emplace(p.opening, static_cast<int>(openingTags_.size()));
        if (added) openingTags_.push_back(p.opening);
        out.opening = it->second;
    }
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
    Parsed parsed;
    std::string why;
    if (!parse(line, parsed, why)) return reject(why);
    if (!accepted(parsed)) {
        ++stats_.filtered;
        return;
    }
    // Reservoir sampling (algorithm R): a uniform sample of `count` among all candidates.
    const uint64_t n = ++stats_.candidates;
    size_t slot = reservoir_.size();
    if (reservoir_.size() >= filter_.count) {
        const uint64_t j = random_.next() % n;
        if (j >= filter_.count) return;
        slot = static_cast<size_t>(j);
    }
    Row row;
    if (!pack(parsed, row, why)) {
        --stats_.candidates;
        return reject(why);
    }
    if (slot == reservoir_.size()) reservoir_.push_back(std::move(row));
    else reservoir_[slot] = std::move(row);
}

// Quality ranking for nested packs: popularity tier first (Lichess -100..100), then a seeded hash of the
// id, so the order is random within a tier and the same on every run and platform (FNV-1a).
void PuzzleImporter::rank() {
    if (ranked_.size() == reservoir_.size()) return;
    auto tier = [](int popularity) { return popularity >= 90 ? 0 : popularity >= 75 ? 1 : popularity >= 50 ? 2 : 3; };
    auto hash = [this](const std::string& id) {
        uint64_t h = 14695981039346656037ull ^ filter_.seed;
        for (unsigned char c : id) h = (h ^ c) * 1099511628211ull;
        return h;
    };
    std::vector<std::pair<uint64_t, size_t>> keys;
    keys.reserve(reservoir_.size());
    for (size_t i = 0; i < reservoir_.size(); ++i)
        keys.push_back({(uint64_t(tier(reservoir_[i].popularity)) << 62) | (hash(reservoir_[i].id) >> 2), i});
    std::sort(keys.begin(), keys.end());
    ranked_.clear();
    for (const auto& k : keys) ranked_.push_back(k.second);
}

Result<size_t> PuzzleImporter::write(storage::Database& db, const std::map<std::string, std::string>& extraMeta,
                                     size_t limit) {
    if (auto r = storage::migrate(db, puzzlePackMigrations()); !r) return r.error();

    // The first `limit` puzzles of the ranking, stored by rating.
    rank();
    std::vector<const Row*> rows;
    for (size_t i = 0; i < ranked_.size() && i < limit; ++i) rows.push_back(&reservoir_[ranked_[i]]);
    std::sort(rows.begin(), rows.end(), [](const Row* a, const Row* b) {
        return a->rating != b->rating ? a->rating < b->rating : a->id < b->id;
    });

    // Theme bits by frequency in the pack (most frequent first); at most 64.
    std::vector<uint64_t> freq(themeNames_.size(), 0);
    for (const Row* rp : rows)
        for (size_t t = 0; t < themeNames_.size() && t < 128; ++t)
            if ((rp->themes[t / 64] >> (t % 64)) & 1) ++freq[t];
    std::vector<size_t> order(themeNames_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return freq[a] > freq[b]; });
    std::vector<int> bit(themeNames_.size(), -1);
    size_t used = 0;
    for (size_t i = 0; i < order.size() && used < 64; ++i)
        if (freq[order[i]] > 0) bit[order[i]] = static_cast<int>(used++);
    size_t dropped = 0;
    for (size_t t = 0; t < freq.size(); ++t) dropped += freq[t] > 0 && bit[t] < 0;

    auto r = db.transaction([&]() -> Result<void> {
        auto th = db.prepare("INSERT INTO themes(bit, name) VALUES(?1, ?2)");
        if (!th) return th.error();
        for (size_t t = 0; t < themeNames_.size(); ++t) {
            if (bit[t] < 0) continue;
            th->bind(1, bit[t]).bind(2, themeNames_[t]);
            if (auto e = th->run(); !e) return e;
        }
        std::vector<char> openingUsed(openingTags_.size(), 0);
        for (const Row* row : rows)
            if (row->opening >= 0) openingUsed[static_cast<size_t>(row->opening)] = 1;
        auto op = db.prepare("INSERT INTO openings(id, tags) VALUES(?1, ?2)");
        if (!op) return op.error();
        for (size_t o = 0; o < openingTags_.size(); ++o) {
            if (!openingUsed[o]) continue;
            op->bind(1, static_cast<int64_t>(o + 1)).bind(2, openingTags_[o]);
            if (auto e = op->run(); !e) return e;
        }
        auto ins = db.prepare("INSERT INTO puzzles(id, pos, moves, rating, popularity, themes, opening_id, rnd) "
                              "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
        if (!ins) return ins.error();
        for (const Row* rp : rows) {
            const Row& row = *rp;
            uint64_t mask = 0;
            for (size_t t = 0; t < themeNames_.size() && t < 128; ++t)
                if ((row.themes[t / 64] >> (t % 64)) & 1 && bit[t] >= 0) mask |= uint64_t(1) << bit[t];
            const auto rnd = static_cast<int64_t>(random_.next() & 0x7fffffff);
            ins->bind(1, row.id)
                .bindBlob(2, std::vector<uint8_t>(row.pos.begin(), row.pos.end()))
                .bindBlob(3, std::vector<uint8_t>(row.moves.begin(), row.moves.end()));
            ins->bind(4, row.rating).bind(5, row.popularity).bind(6, static_cast<int64_t>(mask));
            if (row.opening >= 0) ins->bind(7, static_cast<int64_t>(row.opening + 1));
            else ins->bindNull(7);
            ins->bind(8, rnd);
            if (auto e = ins->run(); !e) return e;
        }
        auto meta = db.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2)");
        if (!meta) return meta.error();
        std::map<std::string, std::string> m = {
            {"source", "lichess puzzles (CC0)"},
            {"count", std::to_string(rows.size())},
            {"ranking", "popularity tier, then FNV-1a(id, seed): packs from one import with growing sizes are nested"},
            {"rows_read", std::to_string(stats_.rows)},
            {"rows_rejected", std::to_string(stats_.rejected)},
            {"candidates", std::to_string(stats_.candidates)},
            {"filter", "rating " + std::to_string(filter_.minRating) + "-" + std::to_string(filter_.maxRating) +
                           ", popularity >= " + std::to_string(filter_.minPopularity) +
                           ", plays >= " + std::to_string(filter_.minPlays)},
            {"seed", std::to_string(filter_.seed)},
            {"themes_dropped", std::to_string(dropped)},
            {"format", "2"},
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
    return rows.size();
}

} // namespace pchess::puzzles
