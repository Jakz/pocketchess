// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Zobrist.h"

#include "core/util/Random.h"

namespace pchess::chess {
namespace {

ZobristKeys build() {
    ZobristKeys k{};
    uint64_t state = 0x5043484553535A42ULL; // "PCHESSZB"
    for (auto& p : k.piece)
        for (auto& s : p) s = util::Random::splitmix64(state);
    k.castling[0] = 0;
    for (int i = 1; i < 16; ++i) k.castling[i] = util::Random::splitmix64(state);
    for (auto& e : k.epFile) e = util::Random::splitmix64(state);
    k.side = util::Random::splitmix64(state);
    return k;
}

} // namespace

const ZobristKeys& zobrist() {
    static const ZobristKeys keys = build();
    return keys;
}

} // namespace pchess::chess
