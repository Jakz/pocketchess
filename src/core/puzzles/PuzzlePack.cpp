// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/PuzzlePack.h"

#include "core/chess/MoveCodec.h"

#include <algorithm>

namespace pchess::puzzles {

namespace {

constexpr const char* kColumns = "id, fen, moves, rating, rd, popularity, plays, themes, opening, rnd";

Puzzle readPuzzle(const storage::Statement& s) {
    Puzzle p;
    p.id = s.text(0);
    p.fen = s.text(1);
    const auto blob = s.blob(2);
    p.moves = chess::decodeMovesUnchecked(blob);  // validated at import; the session replays them
    p.rating = s.integer(3);
    p.ratingDeviation = s.integer(4);
    p.popularity = s.integer(5);
    p.plays = s.integer(6);
    p.themes = static_cast<uint64_t>(s.int64(7));
    p.opening = s.text(8);
    p.rnd = static_cast<uint32_t>(s.int64(9));
    return p;
}

} // namespace

Result<PuzzlePack> PuzzlePack::open(const std::filesystem::path& file) {
    auto db = storage::Database::open(file, storage::Database::Mode::ReadOnly);
    if (!db) return db.error();
    return fromDatabase(std::move(*db), file.stem().string());
}

Result<PuzzlePack> PuzzlePack::fromDatabase(storage::Database db, std::string name) {
    PuzzlePack p;
    p.db_ = std::move(db);
    p.name_ = std::move(name);
    if (auto r = p.load(); !r) return r.error();
    return p;
}

Result<void> PuzzlePack::load() {
    auto c = db_.prepare("SELECT count(*) FROM puzzles");
    if (!c) return c.error();
    if (auto row = c->step(); !row) return row.error();
    count_ = c->integer(0);
    themes_.assign(64, std::string());
    auto t = db_.prepare("SELECT bit, name FROM themes");
    if (!t) return t.error();
    for (;;) {
        auto row = t->step();
        if (!row) return row.error();
        if (!*row) break;
        const int bit = t->integer(0);
        if (bit >= 0 && bit < 64) themes_[static_cast<size_t>(bit)] = t->text(1);
    }
    return {};
}

uint64_t PuzzlePack::themeMask(const std::vector<std::string>& names) const {
    uint64_t mask = 0;
    for (const auto& n : names)
        for (size_t b = 0; b < themes_.size(); ++b)
            if (!n.empty() && themes_[b] == n) mask |= uint64_t(1) << b;
    return mask;
}

std::vector<std::string> PuzzlePack::themesOf(uint64_t mask) const {
    std::vector<std::string> out;
    for (size_t b = 0; b < themes_.size(); ++b)
        if ((mask >> b) & 1 && !themes_[b].empty()) out.push_back(themes_[b]);
    return out;
}

Result<std::optional<Puzzle>> PuzzlePack::byId(const std::string& id) {
    auto s = db_.prepare(std::string("SELECT ") + kColumns + " FROM puzzles WHERE id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    auto row = s->step();
    if (!row) return row.error();
    if (!*row) return std::optional<Puzzle>();
    return std::optional<Puzzle>(readPuzzle(*s));
}

Result<std::vector<Puzzle>> PuzzlePack::batch(int lo, int hi, uint64_t include, uint64_t exclude, int limit,
                                              std::optional<std::pair<int, uint32_t>> after) {
    auto s = db_.prepare(std::string("SELECT ") + kColumns +
                         " FROM puzzles WHERE rating BETWEEN ?1 AND ?2 AND (themes & ?3) = ?3 AND (themes & ?4) = 0"
                         " AND (rating > ?6 OR (rating = ?6 AND rnd > ?7))"
                         " ORDER BY rating, rnd LIMIT ?5");
    if (!s) return s.error();
    s->bind(1, lo).bind(2, hi).bind(3, static_cast<int64_t>(include)).bind(4, static_cast<int64_t>(exclude));
    s->bind(5, limit);
    s->bind(6, after ? after->first : lo - 1).bind(7, after ? static_cast<int64_t>(after->second) : int64_t(-1));
    std::vector<Puzzle> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(readPuzzle(*s));
    }
    return out;
}

Result<std::optional<Puzzle>> PuzzleSelector::next(const SelectRequest& req) {
    constexpr int kBatch = 32;
    constexpr int kPages = 8;  // per window part: bounded work even when most puzzles are seen
    for (int width : {150, 300, 10'000}) {
        const int lo = req.rating - width, hi = req.rating + width;
        const int start = lo + static_cast<int>(random_.below(static_cast<uint32_t>(hi - lo + 1)));
        // [start, hi] first, then the part of the window below `start`.
        for (auto [a, b] : {std::pair{start, hi}, std::pair{lo, start - 1}}) {
            if (a > b) continue;
            std::optional<std::pair<int, uint32_t>> after;
            for (int page = 0; page < kPages; ++page) {
                auto batch = pack_.batch(a, b, req.include, req.exclude, kBatch, after);
                if (!batch) return batch.error();
                std::vector<Puzzle>& list = *batch;
                if (list.empty()) break;
                after = std::pair{list.back().rating, list.back().rnd};
                const bool last = static_cast<int>(list.size()) < kBatch;
                if (req.skip)
                    list.erase(std::remove_if(list.begin(), list.end(), [&](const Puzzle& p) { return req.skip(p.id); }),
                               list.end());
                if (!list.empty()) {
                    const size_t pick = random_.below(static_cast<uint32_t>(list.size()));
                    return std::optional<Puzzle>(std::move(list[pick]));
                }
                if (last) break;
            }
        }
    }
    return std::optional<Puzzle>();
}

} // namespace pchess::puzzles
