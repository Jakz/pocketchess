// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Golden.h"

#include "support/Test.h"
#include "ui/canvas/PngWriter.h"

#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace pchess::test {
namespace {

struct Entry {
    uint64_t hash = 0;
    int width = 0, height = 0;
};

std::filesystem::path indexPath() { return std::filesystem::path(goldenDir()) / "golden.txt"; }

std::map<std::string, Entry> load() {
    std::map<std::string, Entry> m;
    std::ifstream in(indexPath());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string name, hex;
        Entry e;
        if (is >> name >> hex >> e.width >> e.height) {
            e.hash = std::strtoull(hex.c_str(), nullptr, 16);
            m[name] = e;
        }
    }
    return m;
}

void save(const std::map<std::string, Entry>& m) {
    std::ofstream out(indexPath(), std::ios::binary);
    out << "# Golden canvas hashes (FNV-1a 64 over RGB). Regenerate: PocketChessTests --update-golden\n";
    for (const auto& [name, e] : m) {
        char hex[20];
        std::snprintf(hex, sizeof hex, "%016" PRIx64, e.hash);
        out << name << ' ' << hex << ' ' << e.width << ' ' << e.height << '\n';
    }
}

} // namespace

void checkGolden(const std::string& name, const ui::Canvas& canvas) {
    const uint64_t h = canvas.hash();
    auto index = load();
    if (updateGolden()) {
        index[name] = Entry{h, canvas.width(), canvas.height()};
        save(index);
        (void)ui::writePng(std::filesystem::path(goldenDir()) / (name + ".png"), canvas);
        MESSAGE("golden updated: " + name);
        return;
    }
    auto it = index.find(name);
    if (it == index.end()) {
        reportFailure(__FILE__, __LINE__, "no golden entry for '" + name + "' (run with --update-golden)");
        return;
    }
    if (it->second.hash != h || it->second.width != canvas.width() || it->second.height != canvas.height()) {
        const auto actual = std::filesystem::path(goldenDir()) / "actual" / (name + ".png");
        (void)ui::writePng(actual, canvas);
        reportFailure(__FILE__, __LINE__, "golden mismatch for '" + name + "', actual image: " + actual.string());
    }
    countAssertion();
}

} // namespace pchess::test
