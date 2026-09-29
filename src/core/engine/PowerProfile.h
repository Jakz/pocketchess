// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/EngineTypes.h"

#include <chrono>
#include <optional>
#include <string_view>

namespace pchess::engine {

enum class PowerProfileId : uint8_t { Performance, Balanced, Eco };
const char* toString(PowerProfileId p);
std::optional<PowerProfileId> powerProfileFromString(std::string_view s);

// One row of the power-profile table (architecture doc §8.3, design notes §20 "Power"). The
// numbers are placeholders until the M2 device measurements; a user-set limit always wins.
struct PowerProfile {
    PowerProfileId id = PowerProfileId::Balanced;
    const char* name = "Balanced";
    int threads = 1;
    int hashMb = 16;
    SearchLimits quickAnalysis;                    // analysis panel "Quick"
    SearchLimits deepAnalysis;                     // analysis panel "Deep" auto-stop (unbounded = manual stop)
    SearchLimits hint;                             // hint budget
    SearchLimits play;                             // play move budget when the strength level sets no limit
    SearchLimits validate;                         // "check with engine" for alternative puzzle solutions
    std::chrono::milliseconds idleTimeout{60000};  // 0 = quit right after the job
    int nice = 10;                                 // engine process priority on the device
    bool lightEngineForPlay = false;               // prefer the lowest energy class for play/hint/validate
};

const PowerProfile& powerProfile(PowerProfileId id);

} // namespace pchess::engine
