// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>

namespace pchess::util {

// The data root (build doc §5): `--root`, then %POCKETCHESS_ROOT%, then <exeDir>/pocketchess.
// Identical on both platforms, so there is no hard-coded device path.
class Paths {
public:
    static std::filesystem::path resolveRoot(const std::optional<std::filesystem::path>& cliRoot,
                                             const std::filesystem::path& exeDir);

    explicit Paths(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path engines() const { return root_ / "engines"; }
    std::filesystem::path assets() const { return root_ / "assets"; }
    std::filesystem::path fonts() const { return root_ / "assets" / "fonts"; }
    std::filesystem::path themes() const { return root_ / "assets" / "themes"; }
    std::filesystem::path pieces() const { return root_ / "assets" / "pieces"; }
    std::filesystem::path content() const { return root_ / "content"; }
    std::filesystem::path imports() const { return root_ / "imports"; }
    std::filesystem::path exports() const { return root_ / "exports"; }
    std::filesystem::path user() const { return root_ / "user"; }
    std::filesystem::path logs() const { return root_ / "logs"; }

    // Creates user/, logs/, imports/, exports/ if missing. Never touches existing files.
    void ensureWritableDirs() const;

private:
    std::filesystem::path root_;
};

} // namespace pchess::util
