// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Move.h"
#include "core/chess/Position.h"
#include "core/util/Error.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pchess::games {

struct Opening {
    std::string eco;   // "C42"
    std::string name;  // "Petrov's Defense: Classical Attack"
    int plies = 0;     // length of the named line
};

// ECO names from data/openings/*.tsv (lichess chess-openings, CC0), keyed by position so that
// transpositions are recognised. Games are classified by the deepest named position they reach.
class OpeningBook {
public:
    // One TSV file ("eco<TAB>name<TAB>pgn" with a header line). Returns the lines added; bad lines
    // are counted in `rejected`.
    Result<int> addTsv(std::string_view text);
    size_t size() const { return byKey_.size(); }
    int rejected() const { return rejected_; }

    std::optional<Opening> classify(const chess::Position& start, std::span<const chess::Move> moves,
                                    int maxPlies = 40) const;

private:
    std::unordered_map<uint64_t, Opening> byKey_;
    int rejected_ = 0;
};

} // namespace pchess::games
