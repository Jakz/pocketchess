// SPDX-License-Identifier: GPL-3.0-or-later
// pocketchess-setup (ADR 0019): fetch the content that cannot be shipped, install on a reader.
//
//   pocketchess-setup                          what is here, which readers are connected
//   pocketchess-setup fetch --historical --puzzles 500k
//   pocketchess-setup install [--group Games] [--reader E:\]
//   pocketchess-setup reset-launcher           the reader's home screen as before
#include "tools/setup/Setup.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace pchess::setup;

namespace {

int usage() {
    std::printf(
        "PocketChess setup: fetches what cannot be shipped and installs PocketChess on a PocketBook\n"
        "connected over USB (choose \"PC link\" / \"Connect\" on the reader).\n\n"
        "  pocketchess-setup                 show the packs here and the connected readers\n"
        "  pocketchess-setup fetch [--historical] [--opening-files] [--puzzles 50k,100k,500k,1m,all]\n"
        "        --historical     PGN Mentor players and events -> packs/historical.db (your own use only)\n"
        "        --opening-files  also PGN Mentor's opening collections\n"
        "        --puzzles SIZES  Lichess puzzles (CC0) -> packs/puzzles-<size>.db\n"
        "  pocketchess-setup install [--reader PATH] [--packs all|none|NAME,...] [--group NAME] [--no-icon] [--selftest]\n"
        "        copies the app (your progress in user/ is never touched), the packs (default: all in packs/)\n"
        "        and the launcher icon; --group also moves PocketChess to that home-screen group\n"
        "  pocketchess-setup reset-launcher [--reader PATH]\n"
        "        restores the reader's launcher file from the backup made by the first install\n\n"
        "Needs curl and tar (built into Windows 10+, macOS and Linux; on Linux unzip for ZIP files).\n");
    return 2;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

std::vector<std::string> localPacks(const Layout& l) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(l.packs(), ec))
        if (e.path().extension() == ".db") out.push_back(e.path().stem().string());
    return out;
}

int status(const Layout& l) {
    std::printf("PocketChess setup in %s\n", l.root.string().c_str());
    std::printf("  reader bundle: %s\n", fs::exists(l.bundle() / "pocketchess.app") ? "present" : "MISSING");
    const auto packs = localPacks(l);
    std::printf("  packs: %s\n", packs.empty() ? "none yet (pocketchess-setup fetch ...)" : "");
    for (const auto& p : packs)
        std::printf("    %s (%.0f MB)\n", p.c_str(), fs::file_size(l.packs() / (p + ".db")) / 1048576.0);
    const auto readers = findReaders();
    std::printf("  connected readers: %s\n", readers.empty() ? "none" : "");
    for (const auto& r : readers) std::printf("    %s\n", r.string().c_str());
    std::printf("\n");
    usage();
    return 0;
}

std::optional<fs::path> pickReader(const std::string& wanted) {
    if (!wanted.empty()) return fs::path(wanted);
    const auto readers = findReaders();
    if (readers.empty()) {
        std::fprintf(stderr, "no reader found: connect it over USB and choose \"PC link\" on its screen, "
                             "or pass --reader <path>\n");
        return std::nullopt;
    }
    if (readers.size() > 1) {
        std::fprintf(stderr, "several readers found; pass --reader <path>:\n");
        for (const auto& r : readers) std::fprintf(stderr, "  %s\n", r.string().c_str());
        return std::nullopt;
    }
    std::printf("reader: %s\n", readers.front().string().c_str());
    return readers.front();
}

} // namespace

int main(int argc, char** argv) {
    Layout l;
    l.root = fs::absolute(fs::path(argv[0])).parent_path();
    if (argc < 2) return status(l);
    const std::string cmd = argv[1];
    std::string reader, group, packsArg = "all", puzzles;
    bool historical = false, openingFiles = false, icon = true, selfTest = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--reader") reader = value();
        else if (a == "--group") group = value();
        else if (a == "--packs") packsArg = value();
        else if (a == "--puzzles") puzzles = value();
        else if (a == "--historical") historical = true;
        else if (a == "--opening-files") openingFiles = true;
        else if (a == "--no-icon") icon = false;
        else if (a == "--selftest") selfTest = true;
        else if (a == "--root") l.root = fs::absolute(value());  // development: another package folder
        else return usage();
    }

    if (cmd == "fetch") {
        if (!historical && puzzles.empty()) return usage();
        bool ok = true;
        if (!puzzles.empty()) ok = fetchPuzzles(l, split(puzzles)) && ok;
        if (historical) ok = fetchHistorical(l, openingFiles) && ok;
        std::printf(ok ? "\nfetch done: packs in %s\n" : "\nfetch finished with errors (see above)\n",
                    l.packs().string().c_str());
        return ok ? 0 : 1;
    }
    if (cmd == "install") {
        auto r = pickReader(reader);
        if (!r) return 1;
        std::vector<std::string> packs = packsArg == "all" ? localPacks(l) : packsArg == "none" ? std::vector<std::string>{}
                                                                                                : split(packsArg);
        if (!installApp(l, *r, selfTest)) return 1;
        if (!installPacks(l, *r, packs)) return 1;
        if (icon && !installLauncher(l, *r, group)) return 1;
        std::printf("\nDone. Eject the reader safely before unplugging it.\n");
        return 0;
    }
    if (cmd == "reset-launcher") {
        auto r = pickReader(reader);
        if (!r) return 1;
        return resetLauncher(*r) ? 0 : 1;
    }
    return usage();
}
