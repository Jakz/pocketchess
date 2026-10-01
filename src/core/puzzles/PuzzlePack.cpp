// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/PuzzlePack.h"

#include "core/chess/Fen.h"
#include "core/chess/FenCodec.h"
#include "core/chess/MoveCodec.h"

#include <algorithm>

namespace pchess::puzzles {

namespace {

constexpr int kFormat = 2;

constexpr const char* kColumns =
    "p.id, p.pos, p.moves, p.rating, p.popularity, p.themes, o.tags, p.rnd FROM puzzles p "
    "LEFT JOIN openings o ON o.id = p.opening_id";

Result<Puzzle> readPuzzle(const storage::Statement& s) {
    Puzzle p;
    p.id = s.text(0);
    const auto pos = s.blob(1);
    auto fen = chess::unpackFen(std::string_view(reinterpret_cast<const char*>(pos.data()), pos.size()));
    if (!fen) return makeError(ErrorCode::CorruptData, "puzzle " + p.id + ": " + fen.error().context);
    p.fen = std::move(*fen);
    auto start = chess::parseFen(p.fen);
    if (!start) return makeError(ErrorCode::CorruptData, "puzzle " + p.id + ": " + start.error().context);
    auto moves = chess::decodeMoveIndexes(start.value(), s.blob(2));
    if (!moves) return makeError(ErrorCode::CorruptData, "puzzle " + p.id + ": " + moves.error().context);
    p.moves = std::move(*moves);
    p.rating = s.integer(3);
    p.popularity = s.integer(4);
    p.themes = static_cast<uint64_t>(s.int64(5));
    p.opening = s.text(6);
    p.rnd = static_cast<uint32_t>(s.int64(7));
    return p;
}

} // namespace

Result<PuzzlePack> PuzzlePack::open(const std::filesystem::path& file) {
    auto db = storage::Database::open(file, storage::Database::Mode::ReadOnly);
    if (!db) return db.error();
    return fromDatabase(std::move(*db), file.stem().string());
}

Result<PuzzlePack> PuzzlePack::fromDatabase(storage::Database db, std::string name) {
    if (const int v = db.userVersion(); v != kFormat)
        return makeError(ErrorCode::Unsupported, name + ": puzzle pack format " + std::to_string(v) + ", this app reads " +
                                                     std::to_string(kFormat) + " (rebuild it with PuzzleImporter)");
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
    auto s = db_.prepare(std::string("SELECT ") + kColumns + " WHERE p.id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    auto row = s->step();
    if (!row) return row.error();
    if (!*row) return std::optional<Puzzle>();
    auto p = readPuzzle(*s);
    if (!p) return p.error();
    return std::optional<Puzzle>(std::move(*p));
}

Result<std::vector<Puzzle>> PuzzlePack::batch(int lo, int hi, uint64_t include, uint64_t exclude, int limit,
                                              std::optional<std::pair<int, uint32_t>> after) {
    auto s = db_.prepare(std::string("SELECT ") + kColumns +
                         " WHERE p.rating BETWEEN ?1 AND ?2 AND (p.themes & ?3) = ?3 AND (p.themes & ?4) = 0"
                         " AND (p.rating > ?6 OR (p.rating = ?6 AND p.rnd > ?7))"
                         " ORDER BY p.rating, p.rnd LIMIT ?5");
    if (!s) return s.error();
    s->bind(1, lo).bind(2, hi).bind(3, static_cast<int64_t>(include)).bind(4, static_cast<int64_t>(exclude));
    s->bind(5, limit);
    s->bind(6, after ? after->first : lo - 1).bind(7, after ? static_cast<int64_t>(after->second) : int64_t(-1));
    std::vector<Puzzle> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        auto p = readPuzzle(*s);
        if (!p) return p.error();
        out.push_back(std::move(*p));
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
