// SPDX-License-Identifier: GPL-3.0-or-later
// Downloads and pack building for pocketchess-setup (ADR 0019). The network is only used here and
// only on request (`fetch`); the packs are built by the bundled PuzzleImporter and PgnImporter.
#include "tools/setup/Setup.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace pchess::setup {

namespace {

constexpr const char* kLichessPuzzles = "https://database.lichess.org/lichess_db_puzzle.csv.zst";
constexpr const char* kPgnMentor = "https://www.pgnmentor.com/";

#ifdef _WIN32
constexpr const char* kExe = ".exe";
#else
constexpr const char* kExe = "";
#endif

bool hasCommand(const std::string& name) {
#ifdef _WIN32
    return std::system(("where " + name + " >NUL 2>NUL").c_str()) == 0;
#else
    return std::system(("command -v " + name + " >/dev/null 2>&1").c_str()) == 0;
#endif
}

// curl and tar: on Windows the ones in System32 (bsdtar reads ZIP files; a GNU tar from Git Bash or
// MSYS earlier on PATH does not, and takes "C:" for a remote host). Elsewhere the ones on PATH.
std::string systemTool(const char* name) {
#ifdef _WIN32
    if (const char* root = std::getenv("SystemRoot")) {
        const fs::path p = fs::path(root) / "System32" / (std::string(name) + ".exe");
        std::error_code ec;
        if (fs::exists(p, ec)) return quote(p);
    }
#endif
    return name;
}

std::string today() {
    const std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", std::gmtime(&t));
    return buf;
}

} // namespace

fs::path Layout::tool(const std::string& name) const {
    const fs::path p = tools() / (name + kExe);
#ifndef _WIN32
    // Zips made on Windows carry no Unix permissions: make the bundled tools executable.
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    return p;
}

std::string quote(const fs::path& p) {
    std::string s = p.string();
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    return out + "\"";
}

int run(const std::string& command) {
    std::printf("> %s\n", command.c_str());
    std::fflush(stdout);
#ifdef _WIN32
    // cmd.exe strips the first and last quote of a line that starts with one: wrap it once more.
    const int rc = std::system(("\"" + command + "\"").c_str());
#else
    const int rc = std::system(command.c_str());
#endif
    return rc;
}

bool download(const std::string& url, const fs::path& file) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path part = file.string() + ".part";
    // -f: fail on HTTP errors, -L: follow redirects, -C -: resume a partial download.
    if (run(systemTool("curl") + " -fL --retry 3 --retry-delay 2 -C - -o " + quote(part) + " " + quote(url)) != 0) {
        std::fprintf(stderr, "download failed: %s\n", url.c_str());
        return false;
    }
    fs::rename(part, file, ec);
    if (ec) {
        std::fprintf(stderr, "cannot rename %s: %s\n", part.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

std::optional<std::string> fetchText(const std::string& url) {
    const fs::path tmp = fs::temp_directory_path() / "pocketchess-setup-page.html";
    std::error_code ec;
    fs::remove(tmp, ec);
    if (run(systemTool("curl") + " -fsSL -o " + quote(tmp) + " " + quote(url)) != 0) return std::nullopt;
    std::ifstream in(tmp, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    fs::remove(tmp, ec);
    return ss.str();
}

bool extractZip(const fs::path& zip, const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    // bsdtar (Windows 10+, macOS) reads ZIP files; GNU tar (most Linux) does not: unzip there.
    if (run(systemTool("tar") + " -xf " + quote(zip) + " -C " + quote(dir)) == 0) return true;
    if (hasCommand("unzip") && run("unzip -o -q " + quote(zip) + " -d " + quote(dir)) == 0) return true;
    std::fprintf(stderr, "cannot extract %s (needs tar with ZIP support, or unzip)\n", zip.string().c_str());
    return false;
}

bool fetchPuzzles(const Layout& l, const std::vector<std::string>& sizes) {
    if (sizes.empty()) return true;
    const fs::path file = l.downloads() / "lichess_db_puzzle.csv.zst";
    if (!fs::exists(file)) {
        std::printf("Lichess puzzle database (CC0, about 300 MB)\n");
        if (!download(kLichessPuzzles, file)) return false;
        std::ofstream info(file.string() + ".info");
        info << "url = " << kLichessPuzzles << "\nsize = " << fs::file_size(file) << "\ndownloaded = " << today() << "\n";
    } else {
        std::printf("Lichess puzzle database already downloaded (delete %s to fetch a newer one)\n",
                    file.string().c_str());
    }
    std::string list;
    for (const auto& s : sizes) list += (list.empty() ? "" : ",") + s;
    std::error_code ec;
    fs::create_directories(l.packs(), ec);
    return run(quote(l.tool("PuzzleImporter")) + " " + quote(file) + " " + quote(l.packs()) + " --sizes " + list) == 0;
}

bool fetchHistorical(const Layout& l, bool openingFiles) {
    std::printf("PGN Mentor game collections (free downloads; for your own use only)\n");
    auto page = fetchText(std::string(kPgnMentor) + "files.html");
    if (!page) {
        std::fprintf(stderr, "cannot read the PGN Mentor file list\n");
        return false;
    }
    std::set<std::string> links;
    const std::regex href(R"re(href="((players|events|openings)/[^"/]+\.(zip|pgn))")re");
    for (auto it = std::sregex_iterator(page->begin(), page->end(), href); it != std::sregex_iterator(); ++it) {
        const std::string section = (*it)[2];
        if (section == "openings" && !openingFiles) continue;
        links.insert((*it)[1]);
    }
    std::printf("%zu files\n", links.size());
    const fs::path root = l.downloads() / "pgnmentor";
    const fs::path pgn = root / "pgn";
    int failed = 0;
    for (const std::string& rel : links) {
        const fs::path file = root / rel;
        const std::string section = rel.substr(0, rel.find('/'));
        if (!fs::exists(file)) {
            if (!download(kPgnMentor + rel, file)) {
                ++failed;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));  // gentle with a free site
        }
        const fs::path target = pgn / section;
        if (file.extension() == ".zip") {
            const fs::path marker = target / (file.filename().string() + ".extracted");
            if (!fs::exists(marker)) {
                if (!extractZip(file, target / file.stem())) {
                    ++failed;
                    continue;
                }
                std::ofstream(marker).put('\n');
            }
        } else {
            std::error_code ec;
            fs::create_directories(target, ec);
            fs::copy_file(file, target / file.filename(), fs::copy_options::overwrite_existing, ec);
        }
    }
    if (failed) std::fprintf(stderr, "%d file(s) failed; run fetch again to retry them\n", failed);
    std::error_code ec;
    fs::create_directories(l.packs(), ec);
    const fs::path out = l.packs() / "historical.db";
    fs::remove(out, ec);
    std::string cmd = quote(l.tool("PgnImporter"));
    for (const char* section : {"players", "events", "openings"})
        if (fs::exists(pgn / section)) cmd += " " + quote(pgn / section);
    cmd += " " + quote(out) + " --name \"Historical games\" --skip-online --openings " +
           quote(l.bundle() / "pocketchess" / "assets" / "openings");
    return run(cmd) == 0;
}

} // namespace pchess::setup
