// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Value types shared by descriptors, engines, jobs and the manager (architecture doc §8.1).

#include "core/chess/Move.h"
#include "core/engine/UciParser.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pchess::engine {

enum class Role : uint8_t { Play, Analysis, Hint, Validate };
constexpr int RoleCount = 4;
const char* toString(Role r);
std::optional<Role> roleFromString(std::string_view s);

enum class EnergyClass : uint8_t { Low, Medium, High };
const char* toString(EnergyClass e);
std::optional<EnergyClass> energyClassFromString(std::string_view s);

// UCI options to send, in order: name -> value text ("true"/"false" for check options).
using OptionSet = std::vector<std::pair<std::string, std::string>>;
void setOption(OptionSet& set, const std::string& name, const std::string& value);  // replaces
const std::string* findOption(const OptionSet& set, std::string_view name);         // case-insensitive

struct SearchLimits {
    std::optional<int> movetimeMs, depth;
    std::optional<uint64_t> nodes;
    bool infinite = false;  // until stop(); callers must bound it (auto-stop timer)

    bool bounded() const { return movetimeMs || depth || nodes; }
    // "go ..." command text.
    std::string goCommand() const;
    // Fields of `other` override ours when set.
    SearchLimits overriddenBy(const SearchLimits& other) const;
};

// What the engine reported about itself after `uci` (id + options).
struct Capabilities {
    std::string name;
    std::string author;
    std::vector<UciOption> options;

    const UciOption* find(std::string_view optionName) const;  // case-insensitive (UCI option names)
    bool has(std::string_view optionName) const { return find(optionName) != nullptr; }
};

// Drops options the engine does not have, clamps spin values and checks combo values.
// One warning per change.
OptionSet validateOptions(const OptionSet& wanted, const Capabilities& caps, std::vector<std::string>* warnings);

// One parsed "info" update (per multipv line).
struct SearchInfo {
    int depth = 0;
    int seldepth = 0;
    int multipv = 1;
    std::optional<Score> score;   // from the side to move's point of view (UCI)
    std::vector<chess::Move> pv;  // legal moves only (converted and checked in the searched position)
    uint64_t nodes = 0;
    uint64_t nps = 0;
    int timeMs = 0;
    int hashfull = 0;
};

// Per-process resource use, for energy/RAM accounting (architecture doc §8.3).
struct EngineStats {
    std::chrono::milliseconds startup{0};  // spawn -> readyok of the most recent start
    uint64_t rssKb = 0;
    uint64_t peakRssKb = 0;
    double cpuSeconds = 0;  // of the current (or last) process
    int starts = 0;         // processes started by this engine object
};

} // namespace pchess::engine
