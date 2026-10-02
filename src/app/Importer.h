// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"
#include "core/util/Paths.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pchess::games {
class OpeningBook;
}

namespace pchess::app {

// Import workflow (design notes §34, ADR 0013): the user copies files into imports/ over USB and
// opens Import, which scans that one folder on request (never the whole filesystem).
enum class ImportKind { Pgn, Fen, PuzzlePack, GamePack, PieceSet, Theme, Engine, Backup, Unknown };

const char* toString(ImportKind kind);

struct ImportItem {
    std::filesystem::path path;  // a file or folder directly inside imports/
    ImportKind kind = ImportKind::Unknown;
    std::string name;            // what it is called once installed (pack stem, set/theme/engine name)
    std::string detail;          // "12 games", "piece set · GPL-2.0-or-later", or why it cannot be used
    bool valid = false;
};

// What already exists, so an import never replaces a bundled or installed item of the same name.
struct InstalledNames {
    std::function<bool(const std::string&)> theme, pieceSet, engine;
    std::function<bool(const std::string& kind, const std::string& name)> pack;  // kind: puzzles | games
};

class Importer {
public:
    // `platform` is the engine platform key binaries must match ("arm" on the reader).
    Importer(const util::Paths& paths, std::string platform);

    std::vector<ImportItem> scan(const InstalledNames& installed) const;

    // Packs, piece sets, themes and engines: moved (or copied, for a theme file) under user/
    // (Paths::installed). Returns where it went. PGN and FEN files are opened, not installed.
    Result<std::filesystem::path> install(const ImportItem& item) const;

    // A PGN file -> user/content/games/<stem>.db with openings classified when `book` is given.
    // `progress(games)` is called every 500 games.
    Result<std::filesystem::path> makeGamePack(const ImportItem& item, const games::OpeningBook* book,
                                               const std::function<void(int)>& progress = {}) const;

private:
    ImportItem classify(const std::filesystem::path& path, const InstalledNames& installed) const;
    const util::Paths& paths_;
    std::string platform_;
};

// "elf-arm", "elf-x86_64", "elf-other", "pe" (Windows) or "" from the first bytes of a file, and
// whether that runs on `platform` (engine platform key).
std::string executableFormat(const std::filesystem::path& file);
bool executableRunsOn(const std::string& format, const std::string& platform);

} // namespace pchess::app
