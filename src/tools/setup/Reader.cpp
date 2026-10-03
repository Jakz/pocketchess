// SPDX-License-Identifier: GPL-3.0-or-later
// Reader side of pocketchess-setup (ADR 0019): finding the mounted reader and copying the app.
#include "tools/setup/Setup.h"

#include <cstdio>
#include <cstdlib>
#include <set>

namespace pchess::setup {

namespace {

// Never written by an install: the user's progress, logs and file exchange folders (AGENTS.md).
const std::set<std::string> kKeep = {"user", "logs", "imports", "exports"};

bool isReader(const fs::path& root) {
    std::error_code ec;
    return fs::is_directory(root / "applications", ec) && fs::is_directory(root / "system", ec);
}

void addChildren(std::vector<fs::path>& out, const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (isReader(e.path())) out.push_back(e.path());
}

bool sameFile(const fs::path& a, const fs::path& b) {
    std::error_code ec1, ec2;
    return fs::exists(b, ec2) && fs::file_size(a, ec1) == fs::file_size(b, ec2) &&
           fs::last_write_time(a, ec1) <= fs::last_write_time(b, ec2);
}

// Copies `src` into `dst` (new or changed files), then removes what `src` no longer has. At the top
// level, folders in `keep` are left alone on both sides.
bool mirror(const fs::path& src, const fs::path& dst, const std::set<std::string>& keep, int& copied, int& removed) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (it.depth() == 0 && it->is_directory() && keep.count(rel.string())) {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path to = dst / rel;
        if (it->is_directory()) {
            fs::create_directories(to, ec);
        } else if (!sameFile(it->path(), to)) {
            fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::fprintf(stderr, "cannot copy %s: %s\n", to.string().c_str(), ec.message().c_str());
                return false;
            }
            ++copied;
        }
    }
    std::vector<fs::path> stale;
    for (auto it = fs::recursive_directory_iterator(dst, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::path rel = fs::relative(it->path(), dst, ec);
        if (it.depth() == 0 && it->is_directory() && keep.count(rel.string())) {
            it.disable_recursion_pending();
            continue;
        }
        if (!fs::exists(src / rel)) {
            stale.push_back(it->path());
            if (it->is_directory()) it.disable_recursion_pending();
        }
    }
    for (const fs::path& p : stale) {
        removed += static_cast<int>(fs::remove_all(p, ec));
        std::printf("removed %s\n", p.string().c_str());
    }
    return true;
}

} // namespace

std::vector<fs::path> findReaders() {
    std::vector<fs::path> out;
#ifdef _WIN32
    for (char d = 'D'; d <= 'Z'; ++d) {
        const fs::path root = std::string(1, d) + ":\\";
        if (isReader(root)) out.push_back(root);
    }
#elif defined(__APPLE__)
    addChildren(out, "/Volumes");
#else
    if (const char* user = std::getenv("USER")) {
        addChildren(out, fs::path("/media") / user);
        addChildren(out, fs::path("/run/media") / user);
    }
    addChildren(out, "/media");
    addChildren(out, "/mnt");
#endif
    return out;
}

bool installApp(const Layout& l, const fs::path& reader, bool selfTest) {
    const fs::path apps = reader / "applications";
    if (!fs::exists(l.bundle() / "pocketchess.app")) {
        std::fprintf(stderr, "no reader bundle in %s\n", l.bundle().string().c_str());
        return false;
    }
    std::error_code ec;
    for (const char* app : {"pocketchess.app", "pocketchess-selftest.app"}) {
        if (std::string(app) == "pocketchess-selftest.app" && !selfTest) continue;
        fs::copy_file(l.bundle() / app, apps / app, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "cannot copy %s: %s\n", app, ec.message().c_str());
            return false;
        }
    }
    // Packs the new bundle does not have (e.g. historical.db from an older deploy) move to user/content
    // instead of being removed: the user keeps them, and later installs leave them alone.
    for (const char* kind : {"games", "puzzles"}) {
        const fs::path dir = apps / "pocketchess" / "content" / kind;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".db" || fs::exists(l.bundle() / "pocketchess" / "content" / kind / e.path().filename()))
                continue;
            const fs::path to = apps / "pocketchess" / "user" / "content" / kind / e.path().filename();
            fs::create_directories(to.parent_path(), ec);
            if (!fs::exists(to)) {
                fs::rename(e.path(), to, ec);
                if (!ec) std::printf("kept %s: moved to user/content/%s\n", e.path().filename().string().c_str(), kind);
            }
        }
    }
    int copied = 0, removed = 0;
    if (!mirror(l.bundle() / "pocketchess", apps / "pocketchess", kKeep, copied, removed)) return false;
    for (const std::string& d : kKeep) fs::create_directories(apps / "pocketchess" / d, ec);
    std::printf("app installed: %d file(s) copied, %d removed; user/ untouched\n", copied, removed);
    return true;
}

bool installPacks(const Layout& l, const fs::path& reader, const std::vector<std::string>& names) {
    const fs::path user = reader / "applications" / "pocketchess" / "user" / "content";
    for (const std::string& name : names) {
        const fs::path pack = l.packs() / (name + ".db");
        if (!fs::exists(pack)) {
            std::fprintf(stderr, "no pack %s: run `pocketchess-setup fetch` first\n", pack.string().c_str());
            return false;
        }
        const fs::path dir = user / (name.rfind("puzzles", 0) == 0 ? "puzzles" : "games");
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (sameFile(pack, dir / pack.filename())) {
            std::printf("%s already on the reader\n", pack.filename().string().c_str());
            continue;
        }
        std::printf("copying %s (%.0f MB)\n", pack.filename().string().c_str(), fs::file_size(pack) / 1048576.0);
        fs::copy_file(pack, dir / pack.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "cannot copy %s: %s\n", pack.string().c_str(), ec.message().c_str());
            return false;
        }
    }
    return true;
}

} // namespace pchess::setup
