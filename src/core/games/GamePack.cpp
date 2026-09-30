// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/games/GamePack.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveCodec.h"

#include <charconv>
#include <cstdio>
#include <functional>

namespace pchess::games {

using chess::GameNode;
using chess::GameTree;
using chess::PgnGame;

namespace {

constexpr storage::Migration kSteps[] = {
    {1, R"sql(
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT) WITHOUT ROWID;
CREATE TABLE players(
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL UNIQUE,
    normalized TEXT NOT NULL
);
CREATE INDEX idx_players_normalized ON players(normalized);
CREATE TABLE games(
    id        INTEGER PRIMARY KEY,
    event     TEXT, site TEXT, date TEXT, round TEXT,
    year      INTEGER,               -- from Date, NULL when unknown
    white_id  INTEGER NOT NULL REFERENCES players(id),
    black_id  INTEGER NOT NULL REFERENCES players(id),
    white_elo INTEGER, black_elo INTEGER,
    result    TEXT NOT NULL,
    eco       TEXT, opening TEXT,
    ply_count INTEGER NOT NULL,
    fen       TEXT,                  -- start position when not the standard one
    moves     BLOB NOT NULL,         -- main line, 16-bit MoveCodec
    pgn       TEXT                   -- full movetext, only when it has comments, NAGs or variations
);
CREATE INDEX idx_games_white ON games(white_id, year);
CREATE INDEX idx_games_black ON games(black_id, year);
CREATE INDEX idx_games_year ON games(year, id);
CREATE INDEX idx_games_eco ON games(eco);
)sql"},
    // v2 (packs for millions of games): repeated text in lookup tables, dates and results as
    // numbers, moves one byte each (MoveCodec indexes), an index per filter in list order.
    {2, R"sql(
DROP TABLE games;
CREATE TABLE events(id INTEGER PRIMARY KEY, name TEXT NOT NULL);  -- unique by construction (the importer)
CREATE TABLE sites(id INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE openings(id INTEGER PRIMARY KEY, eco TEXT NOT NULL, name TEXT NOT NULL);
CREATE TABLE games(
    id         INTEGER PRIMARY KEY,
    event_id   INTEGER REFERENCES events(id),
    site_id    INTEGER REFERENCES sites(id),
    date       INTEGER NOT NULL,     -- yyyymmdd, 00 for unknown parts, 0 when unknown
    round      TEXT,
    year       INTEGER NOT NULL,     -- 0 when unknown (sorts last)
    white_id   INTEGER NOT NULL REFERENCES players(id),
    black_id   INTEGER NOT NULL REFERENCES players(id),
    white_elo  INTEGER, black_elo INTEGER,
    result     INTEGER NOT NULL,     -- 1 = 1-0, 2 = 0-1, 3 = 1/2-1/2, 0 = unknown
    opening_id INTEGER REFERENCES openings(id),
    ply_count  INTEGER NOT NULL,
    fen        TEXT,                 -- start position when not the standard one
    moves      BLOB NOT NULL,        -- main line, one byte per move (encodeMoveIndexes)
    pgn        TEXT                  -- full movetext, only for real annotations
);
CREATE INDEX idx_games_year ON games(year);
CREATE INDEX idx_games_white ON games(white_id, year);
CREATE INDEX idx_games_black ON games(black_id, year);
CREATE INDEX idx_games_opening ON games(opening_id, year);
CREATE INDEX idx_games_event ON games(event_id, year);
)sql"},
};

constexpr int kFormat = 2;

int resultCode(const std::string& r) { return r == "1-0" ? 1 : r == "0-1" ? 2 : r == "1/2-1/2" ? 3 : 0; }
std::string resultText(int c) { return c == 1 ? "1-0" : c == 2 ? "0-1" : c == 3 ? "1/2-1/2" : "*"; }

// "1851.06.21" -> 18510621, "1851.??.??" -> 18510000.
int dateCode(const std::string& d) {
    auto part = [&](size_t pos, size_t len) {
        if (d.size() < pos + len) return 0;
        int v = 0;
        for (size_t i = pos; i < pos + len; ++i) {
            if (d[i] < '0' || d[i] > '9') return 0;
            v = v * 10 + (d[i] - '0');
        }
        return v;
    };
    return part(0, 4) * 10000 + part(5, 2) * 100 + part(8, 2);
}
std::string dateText(int c) {
    if (c <= 0) return {};
    char buf[16];
    const int y = c / 10000, m = c / 100 % 100, d = c % 100;
    std::snprintf(buf, sizeof buf, "%04d.", y);
    std::string s = buf;
    s += m ? (std::snprintf(buf, sizeof buf, "%02d.", m), std::string(buf)) : std::string("??.");
    s += d ? (std::snprintf(buf, sizeof buf, "%02d", d), std::string(buf)) : std::string("??");
    return s;
}

int toInt(const std::string& s) {
    int v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// Machine annotations ("[%clk 1:00:50]", "[%eval 0.18]", "[%csl ...]") are dropped: broadcasts and
// Lichess exports carry them on every move, which would store the full PGN of every game.
std::string withoutCommands(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '[' && i + 1 < s.size() && s[i + 1] == '%') {
            const size_t end = s.find(']', i);
            if (end == std::string::npos) break;
            i = end;
            continue;
        }
        const bool space = s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r';
        if (space && (out.empty() || out.back() == ' ')) continue;
        out += space ? ' ' : s[i];
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Lichess computer analysis: "$6 {Inaccuracy. b6 was best.} (8... b6 ...)", "{Checkmate is now
// unavoidable. Qxf7+ was best.}". Generated, not written by anyone: comment, NAGs and the
// suggested line (a sibling variation) go.
bool computerComment(const std::string& c) {
    static const std::string tail = " was best.";
    return c.size() > tail.size() && c.compare(c.size() - tail.size(), tail.size(), tail) == 0;
}

void cleanComments(GameNode* n) {
    if (n->comment.find("[%") != std::string::npos) n->comment = withoutCommands(n->comment);
    if (n->preComment.find("[%") != std::string::npos) n->preComment = withoutCommands(n->preComment);
}

void stripCommands(GameNode* n) {
    // Handled from the parent, before walking its children: the suggestion is a sibling of the
    // commented move, so removing it from inside the child's visit would change the list being walked.
    if (!n->children.empty()) {
        GameNode* played = n->children.front().get();
        cleanComments(played);
        if (computerComment(played->comment)) {
            played->comment.clear();
            played->nags.clear();
            n->children.resize(1);  // the engine's suggestion
        }
    }
    cleanComments(n);
    for (const auto& c : n->children) stripCommands(c.get());
}

bool annotated(const GameNode* n) {
    if (!n->comment.empty() || !n->preComment.empty() || !n->nags.empty() || n->children.size() > 1) return true;
    for (const auto& c : n->children)
        if (annotated(c.get())) return true;
    return false;
}

// WHERE clause (always starts with "WHERE 1") and its bindings, for games aliased `g`.
struct Where {
    std::string sql = " WHERE 1";
    std::vector<std::string> text;  // bound in order as ?N
    std::vector<int64_t> ints;
    std::vector<char> kinds;        // 't' or 'i', in placeholder order
    void addText(const std::string& v) {
        text.push_back(v);
        kinds.push_back('t');
    }
    void addInt(int64_t v) {
        ints.push_back(v);
        kinds.push_back('i');
    }
    void bind(storage::Statement& s, int first = 1) const {
        size_t ti = 0, ii = 0;
        int idx = first;
        for (char k : kinds) {
            if (k == 't') s.bind(idx++, text[ti++]);
            else s.bind(idx++, ints[ii++]);
        }
    }
};

Where buildWhere(const GameFilter& f) {
    Where w;
    const std::string players = "(SELECT id FROM players WHERE normalized LIKE ?)";
    auto like = [](const std::string& s) { return "%" + normalizePlayer(s) + "%"; };
    if (!f.player.empty() && !f.opponent.empty()) {
        w.sql += " AND ((g.white_id IN " + players + " AND g.black_id IN " + players + ") OR (g.black_id IN " +
                 players + " AND g.white_id IN " + players + "))";
        w.addText(like(f.player));
        w.addText(like(f.opponent));
        w.addText(like(f.player));
        w.addText(like(f.opponent));
    } else if (!f.player.empty()) {
        w.sql += " AND (g.white_id IN " + players + " OR g.black_id IN " + players + ")";
        w.addText(like(f.player));
        w.addText(like(f.player));
    }
    if (f.yearFrom > 0) {
        w.sql += " AND g.year >= ?";
        w.addInt(f.yearFrom);
    }
    if (f.yearTo > 0) {
        w.sql += " AND g.year <= ?";
        w.addInt(f.yearTo);
    }
    // Text filters match the small lookup tables; the games index on the id does the rest.
    if (!f.event.empty()) {
        w.sql += " AND g.event_id IN (SELECT id FROM events WHERE name LIKE ?)";
        w.addText("%" + f.event + "%");
    }
    if (!f.eco.empty()) {
        w.sql += " AND g.opening_id IN (SELECT id FROM openings WHERE eco LIKE ?)";
        w.addText(f.eco + "%");
    }
    if (!f.opening.empty()) {
        w.sql += " AND g.opening_id IN (SELECT id FROM openings WHERE name LIKE ?)";
        w.addText("%" + f.opening + "%");
    }
    if (!f.result.empty()) {
        w.sql += " AND g.result = ?";
        w.addInt(resultCode(f.result));
    }
    if (f.ids) {
        w.sql += " AND g.id IN (";
        for (size_t i = 0; i < f.ids->size(); ++i) {
            w.sql += i ? ",?" : "?";
            w.addInt((*f.ids)[i]);
        }
        w.sql += f.ids->empty() ? "NULL)" : ")";
    }
    return w;
}

constexpr const char* kRowSelect =
    "SELECT g.id, pw.name, pb.name, e.name, st.name, g.date, g.round, g.result, o.eco, o.name, g.white_elo, "
    "g.black_elo, g.ply_count FROM games g JOIN players pw ON pw.id = g.white_id JOIN players pb ON pb.id = g.black_id "
    "LEFT JOIN events e ON e.id = g.event_id LEFT JOIN sites st ON st.id = g.site_id "
    "LEFT JOIN openings o ON o.id = g.opening_id";

GameRow readRow(const storage::Statement& s) {
    GameRow r;
    r.id = s.int64(0);
    r.white = s.text(1);
    r.black = s.text(2);
    r.event = s.text(3);
    r.site = s.text(4);
    r.date = dateText(s.integer(5));
    r.round = s.text(6);
    r.result = resultText(s.integer(7));
    r.eco = s.text(8);
    r.opening = s.text(9);
    r.whiteElo = s.integer(10);
    r.blackElo = s.integer(11);
    r.plies = s.integer(12);
    return r;
}

} // namespace

std::span<const storage::Migration> gamePackMigrations() { return kSteps; }

std::string normalizePlayer(std::string_view name) {
    std::string out;
    bool space = false;
    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) {
            if (space && !out.empty()) out += ' ';
            space = false;
            out += ch;
        } else if (c >= 'A' && c <= 'Z') {
            if (space && !out.empty()) out += ' ';
            space = false;
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            space = true;  // spaces, commas, dots, dashes ...
        }
    }
    return out;
}

// --- Import --------------------------------------------------------------------------------------

GameImporter::GameImporter(storage::Database& db, const OpeningBook* openings) : db_(db), openings_(openings) {}

Result<void> GameImporter::begin() {
    if (auto r = storage::migrate(db_, gamePackMigrations()); !r) return r;
    if (auto r = db_.exec("BEGIN"); !r) return r;
    auto g = db_.prepare("INSERT INTO games(event_id, site_id, date, round, year, white_id, black_id, white_elo, "
                         "black_elo, result, opening_id, ply_count, fen, moves, pgn) "
                         "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)");
    if (!g) return g.error();
    insertGame_ = std::move(*g);
    auto p = db_.prepare("INSERT INTO players(name, normalized) VALUES(?1, ?2)");
    if (!p) return p.error();
    insertPlayer_ = std::move(*p);
    for (const char* table : {"events", "sites"}) {
        auto s = db_.prepare(std::string("INSERT INTO ") + table + "(name) VALUES(?1)");
        if (!s) return s.error();
        (table[0] == 'e' ? insertEvent_ : insertSite_) = std::move(*s);
    }
    auto o = db_.prepare("INSERT INTO openings(eco, name) VALUES(?1, ?2)");
    if (!o) return o.error();
    insertOpening_ = std::move(*o);
    return {};
}

// Id of a text in a lookup table (events, sites, "eco\topening"), inserted on first use.
Result<int64_t> GameImporter::lookupId(std::unordered_map<std::string, int64_t>& ids, storage::Statement& insert,
                                       const std::string& key, const std::string& second) {
    if (auto it = ids.find(key); it != ids.end()) return it->second;
    if (second.empty()) insert.bind(1, key);
    else insert.bind(1, key.substr(0, key.find('\t'))).bind(2, second);
    if (auto r = insert.run(); !r) return r.error();
    const int64_t id = db_.lastInsertId();
    ids.emplace(key, id);
    return id;
}

Result<int64_t> GameImporter::playerId(const std::string& name) {
    const std::string n = name.empty() ? "?" : name;
    if (auto it = players_.find(n); it != players_.end()) return it->second;
    insertPlayer_->bind(1, n).bind(2, normalizePlayer(n));
    if (auto r = insertPlayer_->run(); !r) return r.error();
    const int64_t id = db_.lastInsertId();
    players_.emplace(n, id);
    ++stats_.players;
    return id;
}

Result<bool> GameImporter::add(PgnGame& game) {
    const std::vector<chess::Move> moves = game.tree.mainLine();
    if (moves.empty()) {
        ++stats_.skipped;
        return false;
    }
    const chess::Position& start = game.tree.startPosition();
    const bool standard = chess::toFen(start) == chess::toFen(chess::Position::startPosition());
    auto blob = chess::encodeMoveIndexes(start, moves);
    if (!blob) return blob.error();
    std::string date = game.header("Date");
    if (date.empty() || date.rfind("????", 0) == 0) date = game.header("UTCDate");  // Lichess exports
    const int dcode = dateCode(date);

    // The same game from two collections (a player file of each side, an event file) is stored once.
    std::string key = game.header("White") + '\n' + game.header("Black") + '\n' + std::to_string(dcode) + '\n' +
                      (standard ? std::string() : chess::toFen(start)) + '\n';
    key.append(blob->begin(), blob->end());
    if (!seen_.insert(std::hash<std::string>{}(key)).second) {
        ++stats_.duplicates;
        return false;
    }

    auto white = playerId(game.header("White"));
    if (!white) return white.error();
    auto black = playerId(game.header("Black"));
    if (!black) return black.error();
    std::string eco = game.header("ECO"), opening = game.header("Opening");
    if (openings_ && standard)
        if (auto o = openings_->classify(start, moves)) {
            eco = o->eco;
            opening = o->name;
        }
    stripCommands(game.tree.root());
    const bool keepPgn = annotated(game.tree.root());
    if (keepPgn) ++stats_.annotated;

    storage::Statement& s = *insertGame_;
    auto idOrNull = [&](int i, std::unordered_map<std::string, int64_t>& ids, storage::Statement& insert,
                        const std::string& v, const std::string& second = {}) -> Result<void> {
        if (v.empty() || v == "?" || v == "\t") {
            s.bindNull(i);
            return {};
        }
        auto id = lookupId(ids, insert, v, second);
        if (!id) return id.error();
        s.bind(i, *id);
        return {};
    };
    if (auto r = idOrNull(1, eventIds_, *insertEvent_, game.header("Event")); !r) return r.error();
    // A URL (Lichess, broadcasts) is a link to the source, not a place: one per game, not kept.
    const std::string site = game.header("Site");
    if (auto r = idOrNull(2, siteIds_, *insertSite_, site.rfind("http", 0) == 0 ? std::string() : site); !r)
        return r.error();
    s.bind(3, dcode);
    const std::string round = game.header("Round");
    if (round.empty() || round == "?" || round == "-") s.bindNull(4);
    else s.bind(4, round);
    s.bind(5, dcode / 10000);
    s.bind(6, *white).bind(7, *black);
    const int we = toInt(game.header("WhiteElo")), be = toInt(game.header("BlackElo"));
    if (we > 0) s.bind(8, we);
    else s.bindNull(8);
    if (be > 0) s.bind(9, be);
    else s.bindNull(9);
    s.bind(10, resultCode(game.result));
    if (eco.empty() && opening.empty()) s.bindNull(11);
    else if (auto r = idOrNull(11, openingIds_, *insertOpening_, eco + '\t' + opening, opening.empty() ? "?" : opening); !r)
        return r.error();
    s.bind(12, static_cast<int64_t>(moves.size()));
    if (standard) s.bindNull(13);
    else s.bind(13, chess::toFen(start));
    s.bindBlob(14, *blob);
    if (keepPgn) s.bind(15, chess::writeMovetext(game.tree));
    else s.bindNull(15);
    if (auto r = s.run(); !r) return r.error();
    ++stats_.games;
    return true;
}

Result<void> GameImporter::finish(const std::map<std::string, std::string>& meta) {
    auto m = db_.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2)");
    if (!m) return m.error();
    std::map<std::string, std::string> all = meta;
    all["count"] = std::to_string(stats_.games);
    all["players"] = std::to_string(stats_.players);
    all["duplicates"] = std::to_string(stats_.duplicates);
    all["format"] = std::to_string(kFormat);
    for (const auto& [k, v] : all) {
        m->bind(1, k).bind(2, v);
        if (auto r = m->run(); !r) return r;
    }
    insertGame_.reset();
    insertPlayer_.reset();
    insertEvent_.reset();
    insertSite_.reset();
    insertOpening_.reset();
    if (auto r = db_.exec("COMMIT"); !r) return r;
    return db_.exec("ANALYZE");
}

// --- Read side -----------------------------------------------------------------------------------

Result<GamePack> GamePack::open(const std::filesystem::path& file) {
    auto db = storage::Database::open(file, storage::Database::Mode::ReadOnly);
    if (!db) return db.error();
    return fromDatabase(std::move(*db), file.stem().string());
}

Result<GamePack> GamePack::fromDatabase(storage::Database db, std::string name) {
    if (const int v = db.userVersion(); v != kFormat)
        return makeError(ErrorCode::Unsupported, name + ": game pack format " + std::to_string(v) + ", this app reads " +
                                                     std::to_string(kFormat) + " (rebuild it with PgnImporter)");
    GamePack p;
    p.db_ = std::move(db);
    p.name_ = std::move(name);
    // The exact total, without counting millions of rows: meta, else a count.
    const auto m = p.meta();
    if (auto it = m.find("count"); it != m.end()) p.count_ = toInt(it->second);
    else {
        auto s = p.db_.prepare("SELECT count(*) FROM games");
        if (!s) return s.error();
        if (auto r = s->step(); !r) return r.error();
        p.count_ = s->integer(0);
    }
    return p;
}

Result<int> GamePack::count(const GameFilter& f) {
    const Where w = buildWhere(f);
    if (w.sql == " WHERE 1") return count_;
    // Capped: a filter matching most of a multi-million pack must not scan it all.
    auto s = db_.prepare("SELECT count(*) FROM (SELECT 1 FROM games g" + w.sql + " LIMIT " +
                         std::to_string(kCountCap + 1) + ")");
    if (!s) return s.error();
    w.bind(*s);
    auto row = s->step();
    if (!row) return row.error();
    return s->integer(0);
}

Result<std::vector<GameRow>> GamePack::page(const GameFilter& f, int offset, int limit) {
    const Where w = buildWhere(f);
    auto s = db_.prepare(std::string(kRowSelect) + w.sql +
                         " ORDER BY g.year DESC, g.id DESC LIMIT ? OFFSET ?");
    if (!s) return s.error();
    w.bind(*s);
    const int n = static_cast<int>(w.kinds.size());
    s->bind(n + 1, limit).bind(n + 2, offset);
    std::vector<GameRow> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(readRow(*s));
    }
    return out;
}

Result<std::optional<GameRow>> GamePack::row(int64_t id) {
    auto s = db_.prepare(std::string(kRowSelect) + " WHERE g.id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    auto row = s->step();
    if (!row) return row.error();
    if (!*row) return std::optional<GameRow>();
    return std::optional<GameRow>(readRow(*s));
}

Result<PgnGame> GamePack::load(int64_t id) {
    auto r = row(id);
    if (!r) return r.error();
    if (!r->has_value()) return makeError(ErrorCode::NotFound, name_ + ": game " + std::to_string(id));
    auto s = db_.prepare("SELECT fen, moves, pgn FROM games WHERE id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    if (auto row2 = s->step(); !row2) return row2.error();
    const GameRow& g = **r;
    std::string headers;
    auto h = [&](const char* k, const std::string& v) {
        if (v.empty()) return;
        std::string esc;
        for (char c : v) {
            if (c == '"' || c == '\\') esc += '\\';
            esc += c;
        }
        headers += "[" + std::string(k) + " \"" + esc + "\"]\n";
    };
    h("Event", g.event);
    h("Site", g.site);
    h("Date", g.date);
    h("Round", g.round);
    h("White", g.white);
    h("Black", g.black);
    h("Result", g.result);
    if (g.whiteElo) h("WhiteElo", std::to_string(g.whiteElo));
    if (g.blackElo) h("BlackElo", std::to_string(g.blackElo));
    h("ECO", g.eco);
    h("Opening", g.opening);
    const std::string fen = s->text(0);
    if (!fen.empty()) {
        h("SetUp", "1");
        h("FEN", fen);
    }
    if (!s->isNull(2)) {  // annotated: the stored movetext
        auto parsed = chess::parsePgnGame(headers + "\n" + s->text(2) + " " + g.result + "\n");
        if (!parsed) return makeError(ErrorCode::CorruptData, name_ + ": game " + std::to_string(id) + ": " +
                                                                  parsed.error().message());
        return parsed;
    }
    auto start = fen.empty() ? Result<chess::Position>(chess::Position::startPosition()) : chess::parseFen(fen);
    if (!start) return start.error();
    const auto blob = s->blob(1);
    auto moves = chess::decodeMoveIndexes(start.value(), blob);
    if (!moves) return makeError(ErrorCode::CorruptData, name_ + ": game " + std::to_string(id) + ": bad moves");
    auto parsed = chess::parsePgnGame(headers + "\n" + g.result + "\n");  // headers only
    if (!parsed) return parsed.error();
    PgnGame game = std::move(parsed.value());
    game.tree = GameTree(start.value());
    GameNode* n = game.tree.root();
    for (chess::Move m : *moves) n = game.tree.addMove(n, m);
    game.result = g.result;
    return game;
}

std::map<std::string, std::string> GamePack::meta() {
    std::map<std::string, std::string> out;
    auto s = db_.prepare("SELECT key, value FROM meta");
    if (!s) return out;
    for (auto row = s->step(); row && *row; row = s->step()) out[s->text(0)] = s->text(1);
    return out;
}

} // namespace pchess::games
