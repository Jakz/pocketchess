// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/EngineDescriptor.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pchess::engine {

// The installed engines: every engines/<id>/engine.yaml (architecture doc §8.2). A malformed
// descriptor or a missing binary disables that engine with a reason; it never throws.
class EngineRegistry {
public:
    struct Entry {
        EngineDescriptor descriptor;  // for broken entries only id/dir are set
        bool available = false;
        std::string problem;  // why it is not available (shown in Settings/Diagnostics)
    };

    // Rescans `enginesDir` (missing folder = no engines). Entries are sorted by id.
    void scan(const std::filesystem::path& enginesDir, const std::string& platform = currentPlatformKey());
    // Several folders (bundled engines, then user/engines); a later duplicate id is ignored.
    void scan(const std::vector<std::filesystem::path>& dirs, const std::string& platform = currentPlatformKey());
    // Adds or replaces one engine directly (tests, user-configured engines). Checks the binary.
    void add(EngineDescriptor d, const std::string& platform = currentPlatformKey());

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view id) const;
    const EngineDescriptor* available(std::string_view id) const;  // nullptr if missing or broken
    std::vector<const EngineDescriptor*> availableFor(Role role) const;
    // Lowest energy class for the role (ties: registry order); nullptr if none.
    const EngineDescriptor* lightestFor(Role role) const;
    const std::string& platform() const { return platform_; }

private:
    Entry check(EngineDescriptor d) const;
    void scanOne(const std::filesystem::path& enginesDir);
    std::vector<Entry> entries_;
    std::string platform_ = currentPlatformKey();
};

} // namespace pchess::engine
