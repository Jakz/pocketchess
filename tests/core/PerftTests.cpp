// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "support/Test.h"

#include <cstdint>

using namespace pchess::chess;

namespace {

struct PerftCase {
    const char* name;
    const char* fen;
    uint64_t nodes[7];  // index = depth; 0 = not checked
};

// Reference values from the Chess Programming Wiki "Perft Results" page.
const PerftCase kCases[] = {
    {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     {1, 20, 400, 8902, 197281, 4865609, 119060324}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     {1, 48, 2039, 97862, 4085603, 193690690, 0}},
    {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {1, 14, 191, 2812, 43238, 674624, 11030083}},
    {"position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
     {1, 6, 264, 9467, 422333, 15833292, 0}},
    {"position4-mirrored", "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
     {1, 6, 264, 9467, 422333, 15833292, 0}},
    {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {1, 44, 1486, 62379, 2103487, 89941194, 0}},
    {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
     {1, 46, 2079, 89890, 3894594, 164075551, 0}},
};

// Tricky en-passant / promotion / castling positions (well-known "perftsuite" subset).
struct SmallCase {
    const char* fen;
    int depth;
    uint64_t nodes;
};
const SmallCase kTricky[] = {
    {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888},        // illegal ep move #1
    {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133},       // illegal ep move #2
    {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467},      // ep capture checks opponent
    {"5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072},            // short castling gives check
    {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711},            // long castling gives check
    {"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206}, // castle rights
    {"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476},  // castling prevented
    {"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001},         // promote out of check
    {"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658},       // discovered check
    {"4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342},             // promote to give check
    {"8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683},               // under-promote to give check
    {"K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217},                // self stalemate
    {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584},             // stalemate & checkmate
    {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527},           // stalemate & checkmate
};

void runCase(const PerftCase& c, int maxDepth) {
    auto pos = parseFen(c.fen);
    REQUIRE(pos.ok());
    Position p = pos.value();
    for (int d = 1; d <= maxDepth && d < 7; ++d) {
        if (!c.nodes[d]) continue;
        const uint64_t n = perft(p, d);
        if (n != c.nodes[d]) MESSAGE(std::string(c.name) + " depth " + std::to_string(d));
        CHECK_EQ(n, c.nodes[d]);
    }
    CHECK(p == pos.value());  // make/unmake restored everything, including the key
}

} // namespace

TEST_CASE("perft: standard positions to depth 4") {
    for (const auto& c : kCases) runCase(c, 4);
}

TEST_CASE("perft: tricky en-passant/castling/promotion suite") {
    for (const auto& c : kTricky) {
        auto pos = parseFen(c.fen);
        REQUIRE(pos.ok());
        Position p = pos.value();
        const uint64_t n = perft(p, c.depth);
        if (n != c.nodes) MESSAGE(c.fen);
        CHECK_EQ(n, c.nodes);
    }
}

TEST_CASE("perft: standard positions to depth 5 [slow]") {
    for (const auto& c : kCases) runCase(c, 5);
}

TEST_CASE("perft: start position depth 6 [slow]") {
    runCase(kCases[0], 6);
    runCase(kCases[2], 6);
}

TEST_CASE("zobrist: incremental key equals recomputed key") {
    // Walk a few thousand random-ish plies and compare against a FEN round trip.
    auto pos = parseFen(kCases[1].fen);
    REQUIRE(pos.ok());
    Position p = pos.value();
    uint32_t seed = 12345;
    for (int ply = 0; ply < 400; ++ply) {
        MoveList list;
        generateLegalMoves(p, list);
        if (list.empty()) break;
        seed = seed * 1103515245u + 12345u;
        p.makeMove(list[(seed >> 16) % list.size()]);
        auto again = parseFen(toFen(p));
        REQUIRE(again.ok());
        CHECK_EQ(again.value().key(), p.key());
    }
}
