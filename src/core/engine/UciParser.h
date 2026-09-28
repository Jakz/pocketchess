// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pchess::engine {

struct Score {
    enum class Kind : uint8_t { Cp, Mate };
    Kind kind = Kind::Cp;
    int value = 0;       // centipawns, or moves to mate (negative = getting mated)
    bool lowerbound = false;
    bool upperbound = false;
};

struct UciOption {
    std::string name;
    std::string type;  // check, spin, combo, button, string
    std::string defaultValue;
    std::optional<int> min, max;
    std::vector<std::string> vars;
};

struct UciInfo {
    std::optional<int> depth, seldepth, multipv, hashfull;
    std::optional<Score> score;
    std::optional<uint64_t> nodes, nps, timeMs, tbhits;
    std::vector<std::string> pv;  // UCI move text; convert with chess::parseUci
    std::string string;           // "info string ..."
};

struct UciMessage {
    enum class Type : uint8_t { Unknown, IdName, IdAuthor, UciOk, ReadyOk, Option, Info, BestMove };
    Type type = Type::Unknown;
    std::string text;    // id name/author value, bestmove move, raw line for Unknown
    std::string ponder;  // bestmove ... ponder <move>
    UciOption option;
    UciInfo info;
};

// Tolerant parser: extra whitespace, unknown tokens and unknown commands are ignored.
UciMessage parseUciLine(std::string_view line);

std::string formatScore(const Score& s, bool whiteToMove, bool fromWhitePov = true);

} // namespace pchess::engine
