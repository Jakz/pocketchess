// SPDX-License-Identifier: GPL-3.0-or-later
// PuzzleImporter (architecture doc §12): Lichess puzzle CSV (.csv or .csv.zst, streamed) -> puzzle
// pack for content/puzzles/. Deterministic for the same input, filter and --seed.
//
//   PuzzleImporter data/downloads/lichess_db_puzzle.csv.zst data/generated/puzzles/standard.db --preset standard
//   PuzzleImporter in.csv.zst out.db --count 5000 --min-rating 800 --max-rating 2000 --include fork
//   PuzzleImporter in.csv.zst --sample 40 tests/fixtures/puzzles-sample.csv   (first 40 rows, raw)
#include "core/puzzles/PuzzleImport.h"
#include "core/util/Files.h"
#include "tools/common/LineReader.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace pchess;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: PuzzleImporter <input.csv[.zst]> <out.db> [options]\n"
                 "       PuzzleImporter <input.csv[.zst]> --sample <N> <out.csv>\n"
                 "  --preset starter|standard|large|huge|all   5k / 25k / 50k / 100k / every puzzle (default large)\n"
                 "  --count N          number of puzzles (overrides --preset)\n"
                 "  --min-rating R  --max-rating R  --min-popularity P  --min-plays N\n"
                 "  --include a,b      themes that must all be present\n"
                 "  --exclude a,b      themes that must not be present\n"
                 "  --seed S           sampling seed (default 1)\n");
    return 2;
}

std::vector<std::string> list(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = s; *p; ++p) {
        if (*p == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// data/downloads/<file>.info (fetch-lichess.ps1): provenance copied into the pack's meta table.
std::map<std::string, std::string> provenance(const std::filesystem::path& input) {
    std::map<std::string, std::string> meta;
    auto text = util::readTextFile(input.string() + ".info", 1 << 16);
    if (!text) return meta;
    size_t i = 0;
    while (i < text->size()) {
        size_t j = text->find('\n', i);
        if (j == std::string::npos) j = text->size();
        std::string line = text->substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (const size_t eq = line.find(" = "); eq != std::string::npos)
            meta["download_" + line.substr(0, eq)] = line.substr(eq + 3);
        i = j + 1;
    }
    return meta;
}

int sample(const std::filesystem::path& input, long count, const std::filesystem::path& out) {
    std::ofstream os(out, std::ios::binary);
    if (!os) {
        std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
        return 1;
    }
    long written = -1;  // the header line does not count
    auto r = tools::forEachLine(input, [&](std::string_view line) {
        os << line << '\n';
        return ++written < count;
    });
    if (!r) {
        std::fprintf(stderr, "%s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("wrote %ld rows to %s\n", written, out.string().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::filesystem::path input = argv[1];
    if (std::strcmp(argv[2], "--sample") == 0) {
        if (argc < 5) return usage();
        return sample(input, std::atol(argv[3]), argv[4]);
    }
    const std::filesystem::path output = argv[2];
    puzzles::ImportFilter f;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--preset") {
            const std::string p = value();
            f.count = p == "starter"    ? 5'000
                      : p == "standard" ? 25'000
                      : p == "huge"     ? 100'000
                      : p == "all"      ? SIZE_MAX  // every puzzle that passes the filter
                                        : 50'000;
        } else if (a == "--count") f.count = static_cast<size_t>(std::atoll(value()));
        else if (a == "--min-rating") f.minRating = std::atoi(value());
        else if (a == "--max-rating") f.maxRating = std::atoi(value());
        else if (a == "--min-popularity") f.minPopularity = std::atoi(value());
        else if (a == "--min-plays") f.minPlays = std::atoi(value());
        else if (a == "--include") f.includeThemes = list(value());
        else if (a == "--exclude") f.excludeThemes = list(value());
        else if (a == "--seed") f.seed = static_cast<uint64_t>(std::atoll(value()));
        else return usage();
    }

    const auto t0 = std::chrono::steady_clock::now();
    puzzles::PuzzleImporter importer(f);
    auto read = tools::forEachLine(input, [&](std::string_view line) {
        importer.addLine(line);
        if (importer.stats().rows % 500'000 == 0 && importer.stats().rows)
            std::printf("  %llu rows...\n", static_cast<unsigned long long>(importer.stats().rows));
        return true;
    });
    if (!read) {
        std::fprintf(stderr, "%s\n", read.error().message().c_str());
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    std::filesystem::remove(output, ec);
    auto db = storage::Database::open(output, storage::Database::Mode::ReadWrite);
    if (!db) {
        std::fprintf(stderr, "%s\n", db.error().message().c_str());
        return 1;
    }
    // A content pack is read-only on the device: a single file, no WAL.
    (void)db->exec("PRAGMA journal_mode=DELETE");
    auto meta = provenance(input);
    meta["input"] = input.filename().string();
    auto written = importer.write(*db, meta);
    if (!written) {
        std::fprintf(stderr, "%s\n", written.error().message().c_str());
        return 1;
    }
    if (auto v = db->exec("VACUUM"); !v) std::fprintf(stderr, "VACUUM: %s\n", v.error().message().c_str());
    db->close();

    const auto& s = importer.stats();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: %zu puzzles (rows %llu, rejected %llu, filtered %llu, candidates %llu) in %.1f s, %.1f MB\n",
                output.string().c_str(), *written, static_cast<unsigned long long>(s.rows),
                static_cast<unsigned long long>(s.rejected), static_cast<unsigned long long>(s.filtered),
                static_cast<unsigned long long>(s.candidates), secs,
                static_cast<double>(std::filesystem::file_size(output, ec)) / (1024.0 * 1024.0));
    for (const auto& r : s.firstRejects) std::printf("  rejected %s\n", r.c_str());
    return 0;
}
