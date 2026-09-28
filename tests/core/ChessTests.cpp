// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/MoveCodec.h"
#include "core/chess/MoveGen.h"
#include "core/chess/Pgn.h"
#include "core/chess/Rules.h"
#include "core/chess/San.h"
#include "core/util/Utf8.h"
#include "support/Test.h"

using namespace pchess;
using namespace pchess::chess;

namespace {

Position fen(const char* f) {
    auto p = parseFen(f);
    if (!p) MESSAGE(p.error().message());
    REQUIRE(p.ok());
    return p.value();
}

Move san(const Position& p, const char* s) {
    auto m = parseSan(p, s);
    if (!m) MESSAGE(m.error().message());
    REQUIRE(m.ok());
    return m.value();
}

Position play(Position p, std::initializer_list<const char*> moves) {
    for (const char* s : moves) p.makeMove(san(p, s));
    return p;
}

} // namespace

// --- Types / squares ------------------------------------------------------------------------

TEST_CASE("squares: names and parsing") {
    CHECK_EQ(squareName(A1), std::string("a1"));
    CHECK_EQ(squareName(H8), std::string("h8"));
    CHECK_EQ(parseSquare("e4"), E4);
    CHECK_EQ(parseSquare("E4"), E4);
    CHECK_EQ(parseSquare("i1"), NoSquare);
    CHECK_EQ(parseSquare("a9"), NoSquare);
    CHECK_EQ(parseSquare("a"), NoSquare);
}

// --- FEN --------------------------------------------------------------------------------------

TEST_CASE("fen: round trip of normalized positions") {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "8/8/8/8/8/8/8/K6k b - - 99 150",
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
    };
    for (const char* f : fens) CHECK_EQ(toFen(fen(f)), std::string(f));
    CHECK_EQ(toFen(Position::startPosition()), std::string(StartFen));
}

TEST_CASE("fen: counters are optional, ep and castling are normalized") {
    CHECK_EQ(toFen(fen("4k3/8/8/8/8/8/8/4K3 w - -")), std::string("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
    // e3 is not capturable by any black pawn -> dropped.
    CHECK_EQ(toFen(fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1")),
             std::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    // Castling right without the rook -> dropped.
    CHECK_EQ(toFen(fen("4k3/8/8/8/8/8/8/4K3 w K - 0 1")), std::string("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
}

TEST_CASE("fen: strict errors") {
    const char* bad[] = {
        "",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",          // 7 ranks
        "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",  // bad digit
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",  // side
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkx - 0 1",  // castling
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq z9 0 1", // ep
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - a 1",  // counter
        "8/8/8/8/8/8/8/8 w - - 0 1",                                  // no kings
        "4k3/8/8/8/8/8/8/3QK3 b - - 0 1" ,                           // legal (control below)
        "4k3/4Q3/8/8/8/8/8/4K3 w - - 0 1",                            // side not to move in check
        "P3k3/8/8/8/8/8/8/4K3 w - - 0 1",                             // pawn on the 8th rank
    };
    int errors = 0;
    for (const char* f : bad) errors += parseFen(f).ok() ? 0 : 1;
    CHECK_EQ(errors, 10);
    auto r = parseFen("8/8/8/8/8/8/8/8 w - - 0 1");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == ErrorCode::ParseFen);
}

// --- SAN / UCI --------------------------------------------------------------------------------

TEST_CASE("san: generation with disambiguation, captures, checks and mates") {
    Position p = Position::startPosition();
    CHECK_EQ(toSan(p, san(p, "e4")), std::string("e4"));
    CHECK_EQ(toSan(p, san(p, "Nf3")), std::string("Nf3"));

    // Two knights can reach d2 -> file disambiguation; two rooks on one file -> rank.
    Position q = fen("4k3/8/8/8/8/8/8/1N2KN1R w - - 0 1");
    CHECK_EQ(toSan(q, san(q, "Nbd2")), std::string("Nbd2"));
    CHECK_EQ(toSan(q, san(q, "Nfd2")), std::string("Nfd2"));
    Position r = fen("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1");
    CHECK_EQ(toSan(r, san(r, "R1a3")), std::string("R1a3"));
    CHECK_EQ(toSan(r, san(r, "R5a3")), std::string("R5a3"));
    // Three queens: needs full square.
    Position s = fen("1k6/8/8/8/4Q2Q/8/8/K6Q w - - 0 1");
    CHECK_EQ(toSan(s, san(s, "Qh4e1")), std::string("Qh4e1"));

    Position mate = fen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    CHECK_EQ(toSan(mate, san(mate, "Ra8")), std::string("Ra8#"));
    Position check = fen("6k1/5pp1/8/8/8/8/8/R5K1 w - - 0 1");
    CHECK_EQ(toSan(check, san(check, "Ra8")), std::string("Ra8+"));
}

TEST_CASE("san: castling, en passant and promotion") {
    Position p = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    CHECK_EQ(toSan(p, san(p, "O-O")), std::string("O-O"));
    CHECK_EQ(toSan(p, san(p, "0-0-0")), std::string("O-O-O"));
    CHECK(san(p, "O-O").isCastle());
    CHECK_EQ(toUci(san(p, "O-O")), std::string("e1g1"));

    Position ep = fen("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    Move exf6 = san(ep, "exf6");
    CHECK(exf6.isEnPassant());
    CHECK_EQ(toSan(ep, exf6), std::string("exf6"));
    CHECK(san(ep, "exf6 e.p.") == exf6);

    Position promo = fen("3r3k/4P3/8/8/8/8/8/K7 w - - 0 1");
    CHECK_EQ(toSan(promo, san(promo, "e8=Q")), std::string("e8=Q+"));
    CHECK_EQ(toSan(promo, san(promo, "e8N")), std::string("e8=N"));
    CHECK_EQ(toSan(promo, san(promo, "exd8=R")), std::string("exd8=R+"));
    CHECK(san(promo, "e8q") == san(promo, "e8=Q"));
    CHECK_FALSE(parseSan(promo, "e8").ok());  // promotion piece required
}

TEST_CASE("san: tolerant input and errors") {
    Position p = Position::startPosition();
    CHECK(san(p, "e2e4") == san(p, "e4"));
    CHECK(san(p, "e2-e4") == san(p, "e4"));
    CHECK(san(p, "Ng1-f3") == san(p, "Nf3"));
    CHECK(san(p, "Nf3!?") == san(p, "Nf3"));
    CHECK(san(p, "Pe4") == san(p, "e4"));
    CHECK_FALSE(parseSan(p, "e5").ok());
    CHECK_FALSE(parseSan(p, "Nd2").ok());
    CHECK_FALSE(parseSan(p, "Zz9").ok());
    CHECK_FALSE(parseSan(p, "").ok());
    // Ambiguous without disambiguation.
    Position q = fen("4k3/8/8/8/8/8/8/1N2KN1R w - - 0 1");
    auto amb = parseSan(q, "Nd2");
    REQUIRE_FALSE(amb.ok());
    CHECK(amb.error().context.find("ambiguous") != std::string::npos);
    // bxc3 is a pawn capture; Bxc3 is the bishop.
    Position b = fen("4k3/8/8/8/8/2n5/1P1B4/4K3 w - - 0 1");
    CHECK_EQ(typeOf(b.movingPiece(san(b, "bxc3"))), Pawn);
    CHECK_EQ(typeOf(b.movingPiece(san(b, "Bxc3"))), Bishop);
}

TEST_CASE("uci: parse and format") {
    Position p = Position::startPosition();
    auto m = parseUci(p, "g1f3");
    REQUIRE(m.ok());
    CHECK_EQ(toUci(m.value()), std::string("g1f3"));
    CHECK_FALSE(parseUci(p, "e2e5").ok());
    CHECK_FALSE(parseUci(p, "e2").ok());
    Position promo = fen("7k/P7/8/8/8/8/8/K7 w - - 0 1");
    auto q = parseUci(promo, "a7a8n");
    REQUIRE(q.ok());
    CHECK_EQ(q.value().promotion(), Knight);
    CHECK_EQ(toUci(q.value()), std::string("a7a8n"));
}

TEST_CASE("san: line conversion") {
    Position p = Position::startPosition();
    std::vector<Move> moves;
    Position walk = p;
    for (const char* s : {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6"}) {
        Move m = san(walk, s);
        moves.push_back(m);
        walk.makeMove(m);
    }
    auto line = toSanLine(p, moves);
    REQUIRE_EQ(line.size(), size_t(6));
    CHECK_EQ(line[4], std::string("Bb5"));
}

// --- Make / unmake ------------------------------------------------------------------------------

TEST_CASE("position: make/unmake restores state for every move type") {
    const char* fens[] = {
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    };
    for (const char* f : fens) {
        Position p = fen(f);
        const Position before = p;
        for (Move m : legalMoves(p)) {
            Undo u = p.makeMove(m);
            p.unmakeMove(m, u);
            CHECK(p == before);
            CHECK_EQ(toFen(p), toFen(before));
        }
    }
}

TEST_CASE("position: castling rights are lost when king/rook move or rook is captured") {
    Position p = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    CHECK_EQ(toFen(play(p, {"Rxa8+"})), std::string("R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1"));
    CHECK_EQ(toFen(play(p, {"Kf1"})), std::string("r3k2r/8/8/8/8/8/8/R4K1R b kq - 1 1"));
    CHECK_EQ(toFen(play(p, {"O-O"})), std::string("r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1"));
    CHECK_EQ(toFen(play(p, {"O-O-O"})), std::string("r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1"));
}

// --- Rules --------------------------------------------------------------------------------------

TEST_CASE("rules: checkmate, stalemate and insufficient material") {
    Position fool = play(Position::startPosition(), {"f3", "e5", "g4", "Qh4#"});
    Outcome o = gameOutcome(fool, {});
    CHECK(o.termination == Termination::Checkmate);
    CHECK_EQ(o.result, std::string("0-1"));

    Outcome st = gameOutcome(fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"), {});
    CHECK(st.termination == Termination::Stalemate);
    CHECK_EQ(st.result, std::string("1/2-1/2"));

    CHECK(insufficientMaterial(fen("8/8/4k3/8/8/3K4/8/8 w - - 0 1")));
    CHECK(insufficientMaterial(fen("8/8/4k3/8/8/3KN3/8/8 w - - 0 1")));
    CHECK(insufficientMaterial(fen("8/8/3bk3/8/8/3KB3/8/8 w - - 0 1")));       // same-colour bishops
    CHECK_FALSE(insufficientMaterial(fen("8/8/2b1k3/8/8/3KB3/8/8 w - - 0 1"))); // opposite colours
    CHECK_FALSE(insufficientMaterial(fen("8/8/4k3/8/8/3KNN2/8/8 w - - 0 1")));
    CHECK_FALSE(insufficientMaterial(fen("8/8/4k3/8/8/3K4/3P4/8 w - - 0 1")));
}

TEST_CASE("rules: repetition and fifty-move claims") {
    Position p = Position::startPosition();
    std::vector<uint64_t> history;
    auto step = [&](const char* s) {
        history.push_back(p.key());
        p.makeMove(san(p, s));
    };
    for (int i = 0; i < 2; ++i)
        for (const char* s : {"Nf3", "Nf6", "Ng1", "Ng8"}) step(s);
    CHECK_EQ(repetitionCount(p, history), 3);
    CHECK(canClaimThreefold(p, history));
    CHECK_FALSE(gameOutcome(p, history).over());
    for (int i = 0; i < 2; ++i)
        for (const char* s : {"Nf3", "Nf6", "Ng1", "Ng8"}) step(s);
    CHECK(gameOutcome(p, history).termination == Termination::FivefoldRepetition);

    CHECK(canClaimFiftyMoves(fen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80")));
    CHECK_FALSE(canClaimFiftyMoves(fen("4k3/8/8/8/8/8/8/R3K3 w - - 99 80")));
}

// --- Move codec -----------------------------------------------------------------------------------

TEST_CASE("codec: 16-bit little-endian round trip with legality check") {
    Position p = Position::startPosition();
    std::vector<Move> moves;
    Position walk = p;
    for (const char* s : {"e4", "d5", "exd5", "c6", "dxc6", "Qb6", "cxb7", "Qxb2", "bxa8=N"}) {
        Move m = san(walk, s);
        moves.push_back(m);
        walk.makeMove(m);
    }
    auto blob = encodeMoves(moves);
    CHECK_EQ(blob.size(), moves.size() * 2);
    CHECK_EQ(blob[0], static_cast<uint8_t>(moves[0].raw() & 0xFF));
    auto back = decodeMoves(p, blob);
    REQUIRE(back.ok());
    CHECK(back.value() == moves);

    blob[3] ^= 0x01;  // corrupt the second move
    CHECK_FALSE(decodeMoves(p, blob).ok());
    blob.pop_back();
    CHECK_FALSE(decodeMoves(p, blob).ok());
}

// --- PGN ----------------------------------------------------------------------------------------

namespace {
const char* kSamplePgn = R"PGN(
[Event "Casual Game"]
[Site "Berlin GER"]
[Date "1852.??.??"]
[Round "?"]
[White "Adolf Anderssen"]
[Black "Jean Dufresne"]
[Result "1-0"]
[ECO "C52"]

{The "Evergreen" game.} 1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5 4.b4 Bxb4 5.c3 Ba5 6.d4 exd4 7.O-O
d3 8.Qb3 Qf6 9.e5 Qg6 10.Re1 Nge7 11.Ba3 b5 12.Qxb5 Rb8 13.Qa4 Bb6 14.Nbd2 Bb7
15.Ne4 Qf5 16.Bxd3 Qh5 17.Nf6+ gxf6 18.exf6 Rg8 19.Rad1! (19.Qe4?! {is weaker}) 19...Qxf3
20.Rxe7+ Nxe7 21.Qxd7+ $1 Kxd7 22.Bf5+ Ke8 (22...Kc6 23.Bd7#) 23.Bd7+ Kf8 24.Bxe7# 1-0

[Event "Broken"]
[Result "*"]

1. e4 e5 2. Nf3 Nf6 3. Ke3 *

% this line is an escape and is ignored
[Event "Variations"]
[Result "*"]

1. e4 (1. d4 d5 (1... Nf6 2. c4) 2. c4) (1. c4) 1... c5 ; Sicilian
2. Nf3 *
)PGN";
}

TEST_CASE("pgn: parses headers, comments, NAGs and variations; skips broken games") {
    auto games = parsePgn(kSamplePgn);
    REQUIRE_EQ(games.size(), size_t(3));

    REQUIRE(games[0].ok());
    const PgnGame& g = games[0].value();
    CHECK_EQ(g.header("White"), std::string("Adolf Anderssen"));
    CHECK_EQ(g.result, std::string("1-0"));
    CHECK_EQ(g.tree.mainLine().size(), size_t(47));
    CHECK_EQ(g.tree.root()->comment, std::string("The \"Evergreen\" game."));
    Position end = g.tree.positionAt(const_cast<GameTree&>(g.tree).mainLineEnd());
    CHECK(gameOutcome(end, {}).termination == Termination::Checkmate);

    // 19.Rad1! has a NAG and a sibling variation with a comment.
    const GameNode* n = g.tree.root();
    for (int i = 0; i < 36; ++i) n = n->mainChild();
    CHECK_EQ(n->ply, 36);
    const GameNode* rad1 = n->mainChild();
    REQUIRE(rad1 != nullptr);
    REQUIRE_EQ(rad1->nags.size(), size_t(1));
    CHECK_EQ(int(rad1->nags[0]), 1);
    REQUIRE_EQ(n->children.size(), size_t(2));
    CHECK_EQ(n->children[1]->comment, std::string("is weaker"));
    CHECK_EQ(int(n->children[1]->nags[0]), 6);

    REQUIRE_FALSE(games[1].ok());
    CHECK(games[1].error().context.find("line 19") != std::string::npos);

    REQUIRE(games[2].ok());
    const PgnGame& v = games[2].value();
    CHECK_EQ(v.tree.root()->children.size(), size_t(3));        // e4, d4, c4
    const GameNode* d4 = v.tree.root()->children[1].get();
    CHECK_EQ(d4->mainChild()->mainChild()->ply, 3);              // 1.d4 d5 2.c4
    CHECK_EQ(d4->children.size(), size_t(2));                    // 1...d5 and 1...Nf6
    CHECK_EQ(v.tree.root()->mainChild()->mainChild()->comment, std::string("Sicilian"));
}

TEST_CASE("pgn: writer round trip keeps moves, comments and variations") {
    auto games = parsePgn(kSamplePgn);
    for (size_t i : {size_t(0), size_t(2)}) {
        REQUIRE(games[i].ok());
        const std::string text = writePgn(games[i].value());
        auto again = parsePgnGame(text);
        if (!again) MESSAGE(text);
        REQUIRE(again.ok());
        CHECK(again.value().tree.mainLine() == games[i].value().tree.mainLine());
        CHECK_EQ(again.value().tree.nodeCount(), games[i].value().tree.nodeCount());
        CHECK_EQ(writePgn(again.value()), text);
        for (const auto& line : std::vector<std::string>{text})
            for (size_t b = 0, e; b < line.size(); b = e + 1) {
                e = line.find('\n', b);
                if (e == std::string::npos) e = line.size();
                CHECK(e - b <= 80);
            }
    }
    const std::string mt = writeMovetext(games[2].value().tree);
    CHECK_EQ(mt, std::string("1. e4 (1. d4 d5 (1... Nf6 2. c4) 2. c4) (1. c4) 1... c5 {Sicilian} 2. Nf3"));
}

TEST_CASE("pgn: FEN header, black to move first, and unterminated input") {
    const char* text = "[FEN \"4k3/8/8/8/8/8/4P3/4K3 b - - 0 1\"]\n[SetUp \"1\"]\n\n1... Kd7 2. e4";
    auto g = parsePgnGame(text);
    REQUIRE(g.ok());
    CHECK_EQ(g.value().tree.mainLine().size(), size_t(2));
    CHECK_EQ(g.value().result, std::string("*"));
    const std::string out = writePgn(g.value());
    CHECK(out.find("1... Kd7 2. e4 *") != std::string::npos);
    CHECK_FALSE(parsePgnGame("1. e4 (").ok());
    CHECK_FALSE(parsePgnGame("1. e4 )").ok());
    CHECK_FALSE(parsePgnGame("").ok());
}

// --- GameTree -------------------------------------------------------------------------------------

TEST_CASE("gametree: add, reuse, promote and delete variations") {
    GameTree t;
    Position p = t.startPosition();
    GameNode* e4 = t.addMove(t.root(), san(p, "e4"));
    GameNode* d4 = t.addMove(t.root(), san(p, "d4"));
    CHECK(t.addMove(t.root(), san(p, "e4")) == e4);
    CHECK(e4->isMainLine());
    CHECK_FALSE(d4->isMainLine());
    t.promoteVariation(d4);
    CHECK(d4->isMainLine());
    CHECK_EQ(t.root()->children.size(), size_t(2));
    t.deleteNode(e4);
    CHECK_EQ(t.root()->children.size(), size_t(1));
    CHECK_EQ(t.nodeCount(), size_t(2));
    CHECK_EQ(t.keyHistoryTo(d4).size(), size_t(1));
}

// --- UTF-8 ----------------------------------------------------------------------------------------

TEST_CASE("utf8: decode is safe for invalid input") {
    std::string s = "a\xC3\xA9\xE2\x99\x94\xF0\x9F\x98\x80";
    size_t pos = 0;
    CHECK_EQ(uint32_t(util::decodeUtf8(s, pos)), uint32_t('a'));
    CHECK_EQ(uint32_t(util::decodeUtf8(s, pos)), 0xE9u);
    CHECK_EQ(uint32_t(util::decodeUtf8(s, pos)), 0x2654u);
    CHECK_EQ(uint32_t(util::decodeUtf8(s, pos)), 0x1F600u);
    CHECK_EQ(pos, s.size());
    std::string bad = "\xC3\x28\xE2\x82";
    pos = 0;
    CHECK_EQ(uint32_t(util::decodeUtf8(bad, pos)), 0xFFFDu);
    CHECK_EQ(pos, size_t(1));
    std::string back;
    util::appendUtf8(back, 0x2654);
    CHECK_EQ(back, std::string("\xE2\x99\x94"));
}
