// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/PowerProfile.h"

#include <array>
#include <cctype>

namespace pchess::engine {

namespace {

SearchLimits limits(std::optional<int> movetimeMs, std::optional<int> depth = std::nullopt,
                    std::optional<uint64_t> nodes = std::nullopt) {
    SearchLimits l;
    l.movetimeMs = movetimeMs;
    l.depth = depth;
    l.nodes = nodes;
    l.infinite = !movetimeMs && !depth && !nodes;
    return l;
}

std::array<PowerProfile, 3> makeTable() {
    using std::chrono::milliseconds;
    PowerProfile perf;
    perf.id = PowerProfileId::Performance;
    perf.name = "Performance";
    perf.threads = 2;
    perf.hashMb = 64;
    perf.quickAnalysis = limits(3000);
    perf.deepAnalysis = limits(std::nullopt);  // until stopped
    perf.hint = limits(2000);
    perf.play = limits(1000);
    perf.validate = limits(2000);
    perf.idleTimeout = milliseconds(5 * 60 * 1000);
    perf.nice = 0;

    PowerProfile bal;
    bal.id = PowerProfileId::Balanced;
    bal.name = "Balanced";
    bal.threads = 1;
    bal.hashMb = 16;
    bal.quickAnalysis = limits(1500);
    bal.deepAnalysis = limits(30000, 22);
    bal.hint = limits(1000);
    bal.play = limits(500);
    bal.validate = limits(1000);
    bal.idleTimeout = milliseconds(60 * 1000);
    bal.nice = 10;

    PowerProfile eco;
    eco.id = PowerProfileId::Eco;
    eco.name = "Eco";
    eco.threads = 1;
    eco.hashMb = 8;
    eco.quickAnalysis = limits(500, std::nullopt, 100000);
    eco.deepAnalysis = limits(10000, 16);
    eco.hint = limits(300, std::nullopt, 50000);
    eco.play = limits(300, std::nullopt, 50000);
    eco.validate = limits(500, std::nullopt, 100000);
    eco.idleTimeout = milliseconds(0);
    eco.nice = 19;
    eco.lightEngine = true;
    return {perf, bal, eco};
}

} // namespace

const char* toString(PowerProfileId p) {
    return powerProfile(p).name;
}

std::optional<PowerProfileId> powerProfileFromString(std::string_view s) {
    for (PowerProfileId p : {PowerProfileId::Performance, PowerProfileId::Balanced, PowerProfileId::Eco}) {
        const std::string_view n = toString(p);
        if (n.size() != s.size()) continue;
        bool eq = true;
        for (size_t i = 0; i < n.size(); ++i)
            eq = eq && std::tolower(static_cast<unsigned char>(n[i])) == std::tolower(static_cast<unsigned char>(s[i]));
        if (eq) return p;
    }
    return std::nullopt;
}

const PowerProfile& powerProfile(PowerProfileId id) {
    static const std::array<PowerProfile, 3> table = makeTable();
    return table[static_cast<size_t>(id)];
}

} // namespace pchess::engine
