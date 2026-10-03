// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// pocketchess-setup (ADR 0019): fetches what cannot be shipped (historical games, large puzzle packs)
// and installs PocketChess on a reader connected over USB. Desktop tool for Windows, macOS and Linux:
// downloads through the system's curl, ZIP files through the system's tar (or unzip).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pchess::setup {

namespace fs = std::filesystem;

// The folder layout next to the tool (the installer package):
//   pocketchess-setup[.exe]   tools/PgnImporter[.exe] tools/PuzzleImporter[.exe]
//   reader/applications/...   (the reader bundle)   downloads/  packs/  (created)
struct Layout {
    fs::path root;  // where the tool is
    fs::path tools() const { return root / "tools"; }
    fs::path bundle() const { return root / "reader" / "applications"; }
    fs::path downloads() const { return root / "downloads"; }
    fs::path packs() const { return root / "packs"; }
    fs::path tool(const std::string& name) const;  // tools/<name>[.exe]
};

// --- System commands -----------------------------------------------------------------------------
std::string quote(const fs::path& p);
int run(const std::string& command);  // through the shell; prints the command first
bool download(const std::string& url, const fs::path& file);  // curl, resumable, atomic (.part)
bool extractZip(const fs::path& zip, const fs::path& dir);    // tar -xf, else unzip
std::optional<std::string> fetchText(const std::string& url);

// --- Content -------------------------------------------------------------------------------------
// Lichess puzzles (CC0) -> packs/puzzles-<size>.db for every size ("50k", "500k", "1m", "all").
bool fetchPuzzles(const Layout& l, const std::vector<std::string>& sizes);
// PGN Mentor players + events (free downloads, not redistributable) -> packs/historical.db.
bool fetchHistorical(const Layout& l, bool openingFiles);

// --- Reader --------------------------------------------------------------------------------------
// Mounted readers: a volume with applications/ and system/ (Windows drive letters, /Volumes on
// macOS, /media and /run/media on Linux).
std::vector<fs::path> findReaders();
// Copies the bundle (the app, its assets, engines and bundled content) without ever touching
// user/, logs/, imports/ or exports/; files the new bundle no longer has are removed.
bool installApp(const Layout& l, const fs::path& reader, bool selfTest);
// packs/*.db -> applications/pocketchess/user/content/{games,puzzles}/ (kept by later installs).
bool installPacks(const Layout& l, const fs::path& reader, const std::vector<std::string>& names);

// Launcher (firmware 6.8+, system/config/desktop/view.json): the PocketChess icon, and optionally
// the group it is listed in. The first change keeps view.json.pocketchess-backup.
bool installLauncher(const Layout& l, const fs::path& reader, const std::string& group);
bool resetLauncher(const fs::path& reader);

// view.json text edits (pure, unit-tested): add/replace the "U_pocketchess" entry and move the app
// into `group` when given; remove both again.
std::optional<std::string> withPocketChess(const std::string& viewJson, const std::string& group);
std::string withoutPocketChess(const std::string& viewJson);

} // namespace pchess::setup
