// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/TestTools.h"

#include "support/FakeEngine.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

namespace pchess::test {

int fakeEngine() {
    std::ios::sync_with_stdio(false);
    FakeUci uci;
    std::string line;
    while (std::getline(std::cin, line)) {
        int code = -1;
        const auto out = uci.handle(line, code);
        for (const auto& l : out) std::cout << l << '\n';
        std::cout.flush();
        if (code >= 0) return code;
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
