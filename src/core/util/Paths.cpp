// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Paths.h"

#include <cstdlib>

namespace pchess::util {

std::filesystem::path Paths::resolveRoot(const std::optional<std::filesystem::path>& cliRoot,
                                         const std::filesystem::path& exeDir) {
    if (cliRoot && !cliRoot->empty()) return *cliRoot;
#if defined(_MSC_VER)
    char* env = nullptr;
    size_t len = 0;
    if (_dupenv_s(&env, &len, "POCKETCHESS_ROOT") == 0 && env) {
        std::filesystem::path p(env);
        std::free(env);
        if (!p.empty()) return p;
    }
#else
    if (const char* env = std::getenv("POCKETCHESS_ROOT"); env && *env) return std::filesystem::path(env);
#endif
    return exeDir / "pocketchess";
}

void Paths::ensureWritableDirs() const {
    std::error_code ec;
    for (const auto& dir : {user(), logs(), imports(), exports()}) std::filesystem::create_directories(dir, ec);
}

} // namespace pchess::util
