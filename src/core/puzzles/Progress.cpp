// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/Progress.h"

#include <algorithm>
#include <cmath>

namespace pchess::puzzles {

namespace {

constexpr storage::Migration kSteps[] = {
    {1, R"sql(
CREATE TABLE puzzle_progress(
    puzzle_id     TEXT PRIMARY KEY,
    pack          TEXT,
    attempts      INTEGER NOT NULL DEFAULT 0,
    successes     INTEGER NOT NULL DEFAULT 0,
    failures      INTEGER NOT NULL DEFAULT 0,
    last_result   INTEGER,              -- 1 solved, 0 failed
    last_attempt  INTEGER,              -- unix seconds
    next_review   INTEGER,              -- unix seconds, NULL = not in review
    interval_days INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
CREATE INDEX idx_progress_review ON puzzle_progress(next_review) WHERE next_review IS NOT NULL;
CREATE TABLE rating(
    id      INTEGER PRIMARY KEY CHECK (id = 1),
    current INTEGER NOT NULL,
    updated INTEGER NOT NULL
);
CREATE TABLE rating_history(
    ts            INTEGER NOT NULL,
    rating        INTEGER NOT NULL,
    puzzle_id     TEXT NOT NULL,
    puzzle_rating INTEGER NOT NULL,
    result        INTEGER NOT NULL
);
)sql"},
};

constexpr int kIntervals[] = {1, 3, 7, 21, 60};
constexpr int64_t kDay = 86400;

} // namespace

double expectedScore(int userRating, int puzzleRating) {
    return 1.0 / (1.0 + std::pow(10.0, (puzzleRating - userRating) / 400.0));
}

int updatedRating(int userRating, int puzzleRating, bool solved, int k) {
    const double delta = k * ((solved ? 1.0 : 0.0) - expectedScore(userRating, puzzleRating));
    return userRating + static_cast<int>(std::lround(delta));
}

ReviewState scheduleAfter(const ReviewState& before, bool solved, int64_t now) {
    ReviewState s = before;
    ++s.attempts;
    s.lastAttempt = now;
    s.lastSolved = solved;
    if (!solved) {
        ++s.failures;
        s.intervalDays = kIntervals[0];
    } else {
        ++s.successes;
        if (s.intervalDays == 0) {  // never failed: nothing to review
            s.nextReview.reset();
            return s;
        }
        const int* it = std::upper_bound(std::begin(kIntervals), std::end(kIntervals), s.intervalDays);
        s.intervalDays = it == std::end(kIntervals) ? kIntervals[4] : *it;
    }
    s.nextReview = now + s.intervalDays * kDay;
    return s;
}

std::span<const storage::Migration> progressMigrations() { return kSteps; }

Result<Progress> Progress::open(const std::filesystem::path& file) {
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    if (!db) return db.error();
    return fromDatabase(std::move(*db));
}

Result<Progress> Progress::fromDatabase(storage::Database db) {
    if (auto r = storage::migrate(db, progressMigrations()); !r) return r.error();
    Progress p;
    p.db_ = std::move(db);
    return p;
}

int Progress::rating() {
    auto s = db_.prepare("SELECT current FROM rating WHERE id = 1");
    if (!s) return kInitialRating;
    auto row = s->step();
    return row && *row ? s->integer(0) : kInitialRating;
}

Result<std::optional<ReviewState>> Progress::state(const std::string& id) {
    auto s = db_.prepare("SELECT attempts, successes, failures, interval_days, next_review, last_attempt, last_result "
                         "FROM puzzle_progress WHERE puzzle_id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    auto row = s->step();
    if (!row) return row.error();
    if (!*row) return std::optional<ReviewState>();
    ReviewState st;
    st.attempts = s->integer(0);
    st.successes = s->integer(1);
    st.failures = s->integer(2);
    st.intervalDays = s->integer(3);
    if (!s->isNull(4)) st.nextReview = s->int64(4);
    if (!s->isNull(5)) st.lastAttempt = s->int64(5);
    if (!s->isNull(6)) st.lastSolved = s->integer(6) != 0;
    return std::optional<ReviewState>(st);
}

Result<RecordResult> Progress::record(const std::string& pack, const Puzzle& p, bool solved, bool rated, int64_t now) {
    RecordResult out;
    auto r = db_.transaction([&]() -> Result<void> {
        out.ratingBefore = out.ratingAfter = rating();
        auto before = state(p.id);
        if (!before) return before.error();
        out.state = scheduleAfter(before->value_or(ReviewState{}), solved, now);
        auto up = db_.prepare(
            "INSERT INTO puzzle_progress(puzzle_id, pack, attempts, successes, failures, last_result, last_attempt, "
            "next_review, interval_days) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
            "ON CONFLICT(puzzle_id) DO UPDATE SET pack = excluded.pack, attempts = excluded.attempts, "
            "successes = excluded.successes, failures = excluded.failures, last_result = excluded.last_result, "
            "last_attempt = excluded.last_attempt, next_review = excluded.next_review, "
            "interval_days = excluded.interval_days");
        if (!up) return up.error();
        const ReviewState& st = out.state;
        up->bind(1, p.id).bind(2, pack).bind(3, st.attempts).bind(4, st.successes).bind(5, st.failures);
        up->bind(6, solved ? 1 : 0).bind(7, now);
        if (st.nextReview) up->bind(8, *st.nextReview);
        else up->bindNull(8);
        up->bind(9, st.intervalDays);
        if (auto e = up->run(); !e) return e;
        if (!rated) return {};
        out.ratingAfter = updatedRating(out.ratingBefore, p.rating, solved);
        auto rt = db_.prepare("INSERT INTO rating(id, current, updated) VALUES(1, ?1, ?2) "
                              "ON CONFLICT(id) DO UPDATE SET current = excluded.current, updated = excluded.updated");
        if (!rt) return rt.error();
        rt->bind(1, out.ratingAfter).bind(2, now);
        if (auto e = rt->run(); !e) return e;
        auto h = db_.prepare("INSERT INTO rating_history(ts, rating, puzzle_id, puzzle_rating, result) "
                             "VALUES(?1, ?2, ?3, ?4, ?5)");
        if (!h) return h.error();
        h->bind(1, now).bind(2, out.ratingAfter).bind(3, p.id).bind(4, p.rating).bind(5, solved ? 1 : 0);
        return h->run();
    });
    if (!r) return r.error();
    return out;
}

Result<std::unordered_set<std::string>> Progress::seenIds() {
    auto s = db_.prepare("SELECT puzzle_id FROM puzzle_progress");
    if (!s) return s.error();
    std::unordered_set<std::string> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.insert(s->text(0));
    }
    return out;
}

Result<int> Progress::dueCount(int64_t now) {
    auto s = db_.prepare("SELECT count(*) FROM puzzle_progress WHERE next_review IS NOT NULL AND next_review <= ?1");
    if (!s) return s.error();
    s->bind(1, now);
    auto row = s->step();
    if (!row) return row.error();
    return s->integer(0);
}

Result<std::vector<std::string>> Progress::dueIds(int64_t now, int limit) {
    auto s = db_.prepare("SELECT puzzle_id FROM puzzle_progress WHERE next_review IS NOT NULL AND next_review <= ?1 "
                         "ORDER BY next_review, puzzle_id LIMIT ?2");
    if (!s) return s.error();
    s->bind(1, now).bind(2, limit);
    std::vector<std::string> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(s->text(0));
    }
    return out;
}

Result<std::vector<Progress::RatingPoint>> Progress::history(int limit) {
    auto s = db_.prepare("SELECT ts, rating FROM rating_history ORDER BY ts DESC, rowid DESC LIMIT ?1");
    if (!s) return s.error();
    s->bind(1, limit);
    std::vector<RatingPoint> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back({s->int64(0), s->integer(1)});
    }
    return out;
}

Result<Progress::Totals> Progress::totals(const std::string& pack, bool only) {
    auto s = db_.prepare(std::string("SELECT count(*), coalesce(sum(attempts), 0), coalesce(sum(successes), 0), "
                                     "coalesce(sum(failures), 0), coalesce(sum(failures = 0 AND successes > 0), 0), "
                                     "coalesce(sum(next_review IS NOT NULL), 0) FROM puzzle_progress WHERE ") +
                         (only ? "pack = ?1" : "coalesce(pack, '') <> ?1"));
    if (!s) return s.error();
    s->bind(1, pack);
    auto row = s->step();
    if (!row) return row.error();
    Totals t;
    t.puzzles = s->integer(0);
    t.attempts = s->integer(1);
    t.successes = s->integer(2);
    t.failures = s->integer(3);
    t.solvedFirst = s->integer(4);
    t.inReview = s->integer(5);
    return t;
}

} // namespace pchess::puzzles
