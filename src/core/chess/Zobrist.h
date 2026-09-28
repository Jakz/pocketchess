// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Types.h"

#include <cstdint>

namespace pchess::chess {

// Deterministic Zobrist keys (fixed seed), so keys are identical on every platform and run.
struct ZobristKeys {
    uint64_t piece[12][64];
    uint64_t castling[16];
    uint64_t epFile[8];
    uint64_t side;
};

const ZobristKeys& zobrist();

} // namespace pchess::chess
