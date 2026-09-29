// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/EngineTypes.h"
#include "core/util/Error.h"
#include "core/util/Yaml.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pchess::engine {

// One strength label (beginner ... maximum) mapped to options + limits for one engine (P10).
struct StrengthLevel {
    std::string label;
    OptionSet options;
    SearchLimits limits;
};

// engines/<id>/engine.yaml (architecture doc §8.2). Engines are data: adding, swapping or
// removing one never touches screens or controllers (D8).
struct EngineDescriptor {
    std::string id;
    std::string name;
    std::string protocol = "uci";
    std::string license;
    std::map<std::string, std::string> binaries;  // platform key (arm, windows-x64, ...) -> file name
    std::vector<std::string> args;                // extra command-line arguments (usually none)
    std::vector<std::string> files;               // extra files that must exist (e.g. a .nnue)
    std::vector<Role> roles;
    struct Resources {
        std::optional<int> diskMb, rssMb, startupMs;
        EnergyClass energy = EnergyClass::High;
    } resources;
    OptionSet defaults;                   // e.g. Threads, Hash
    std::vector<StrengthLevel> strength;  // in file order
    std::filesystem::path dir;            // folder of engine.yaml

    bool supports(Role r) const;
    const StrengthLevel* strengthFor(std::string_view label) const;
    // dir / binaries[platform]; empty when the platform has no binary. An absolute file name
    // is used as is (tests, user-configured paths).
    std::filesystem::path binaryPath(const std::string& platform) const;
};

// Platform key of this build: "windows-x64", "arm" (PocketBook), "linux-x64", "arm64" ...
const char* currentPlatformKey();

// Strength labels in UI order (design notes §7.2).
const std::vector<std::string>& strengthLabels();

Result<EngineDescriptor> parseDescriptor(const util::YamlNode& doc, const std::filesystem::path& dir);
Result<EngineDescriptor> loadDescriptor(const std::filesystem::path& yamlFile);

} // namespace pchess::engine
