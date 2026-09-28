// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/TestTools.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

namespace pchess::test {

int fakeEngine() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "uci") {
            std::cout << "id name FakeFish 1.0\n"
                      << "id author PocketChess tests\n"
                      << "option name Hash type spin default 16 min 1 max 1024\n"
                      << "option name Skill Level type spin default 20 min 0 max 20\n"
                      << "uciok" << std::endl;
        } else if (line == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (line.rfind("go", 0) == 0) {
            std::cout << "info depth 1 score cp 10 nodes 20 pv e2e4\n"
                      << "info depth 3 seldepth 4 score cp 20 nodes 4242 nps 84000 time 50 pv e2e4 e7e5\n"
                      << "bestmove e2e4 ponder e7e5" << std::endl;
        } else if (line == "quit") {
            return 0;
        }
    }
    return 0;
}

int benchMovegen() {
    using namespace pchess::chess;
    const char* fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"};
    uint64_t nodes = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const char* f : fens) {
        Position p = parseFen(f).value();
        nodes += perft(p, 4);
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("bench-movegen: %llu nodes in %.3f s = %.2f Mnps (bulk-counted perft 4)\n",
                static_cast<unsigned long long>(nodes), s, static_cast<double>(nodes) / s / 1e6);
    return 0;
}

} // namespace pchess::test
