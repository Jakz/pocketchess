// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Importer.h"

#include "core/chess/Fen.h"
#include "core/chess/Pgn.h"
#include "core/engine/EngineDescriptor.h"
#include "core/games/GamePack.h"
#include "core/puzzles/Progress.h"
#include "core/puzzles/PuzzlePack.h"
#include "core/storage/Backup.h"
#include "core/storage/ChessDb.h"
#include "core/storage/Database.h"
#include "core/util/Files.h"
#include "core/util/Log.h"
#include "ui/theme/SvgPieceSet.h"
#include "ui/theme/ThemeManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace pchess::app {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string plural(size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

ImportItem invalid(ImportItem item, std::string why) {
    item.valid = false;
    item.detail = std::move(why);
    return item;
}

// rename() when possible; across file systems a copy followed by removing the source.
Result<void> moveEntry(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::rename(from, to, ec);
    if (!ec) return {};
    ec.clear();
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove_all(to, ignored);
        return makeError(ErrorCode::Io, "cannot copy " + from.filename().string() + ": " + ec.message());
    }
    std::filesystem::remove_all(from, ec);  // the copy is complete; a leftover source is harmless
    return {};
}

} // namespace

const char* toString(ImportKind kind) {
    switch (kind) {
    case ImportKind::Pgn: return "PGN";
    case ImportKind::Fen: return "FEN";
    case ImportKind::PuzzlePack: return "Puzzle pack";
    case ImportKind::GamePack: return "Game pack";
    case ImportKind::PieceSet: return "Piece set";
    case ImportKind::Theme: return "Board theme";
    case ImportKind::Engine: return "Engine";
    case ImportKind::Backup: return "Progress backup";
    case ImportKind::Unknown: return "Unknown";
    }
    return "?";
}

std::string executableFormat(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char h[20] = {};
    if (!in.read(reinterpret_cast<char*>(h), sizeof h)) return "";
    if (h[0] == 'M' && h[1] == 'Z') return "pe";
    if (h[0] != 0x7F || h[1] != 'E' || h[2] != 'L' || h[3] != 'F') return "";
    const bool little = h[5] == 1;
    const int machine = little ? h[18] | (h[19] << 8) : (h[18] << 8) | h[19];
    switch (machine) {
    case 40: return "elf-arm";
    case 62: return "elf-x86_64";
    case 183: return "elf-aarch64";
    default: return "elf-other";
    }
}

bool executableRunsOn(const std::string& format, const std::string& platform) {
    if (platform == "arm") return format == "elf-arm";
    if (platform == "arm64") return format == "elf-aarch64";
    if (platform == "linux-x64") return format == "elf-x86_64";
    if (platform.rfind("windows", 0) == 0) return format == "pe";
    return !format.empty();
}

Importer::Importer(const util::Paths& paths, std::string platform) : paths_(paths), platform_(std::move(platform)) {}

std::vector<ImportItem> Importer::scan(const InstalledNames& installed) const {
    std::vector<std::filesystem::path> entries;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(paths_.imports(), ec)) {
        const std::string name = de.path().filename().string();
        if (name.empty() || name[0] == '.') continue;  // hidden files, macOS/Windows metadata
        entries.push_back(de.path());
    }
    std::sort(entries.begin(), entries.end());
    std::vector<ImportItem> items;
    for (const auto& p : entries) items.push_back(classify(p, installed));
    util::Log::info("import", std::to_string(items.size()) + " item(s) in " + paths_.imports().string());
    return items;
}

ImportItem Importer::classify(const std::filesystem::path& path, const InstalledNames& installed) const {
    ImportItem item;
    item.path = path;
    item.name = path.stem().string();
    std::error_code ec;

    if (std::filesystem::is_directory(path, ec)) {
        if (std::filesystem::exists(path / "pieceset.yaml", ec)) {
            item.kind = ImportKind::PieceSet;
            auto info = ui::readPieceSetInfo(path);
            if (!info) return invalid(item, info.error().context);
            item.name = info->name;
            if (!info->base.empty()) return invalid(item, "'base' is only for bundled sets: include the 12 SVGs");
            if ((installed.pieceSet && (installed.pieceSet(info->name) || installed.pieceSet(info->id))) ||
                std::filesystem::exists(paths_.installed("pieces") / path.filename(), ec))
                return invalid(item, "a piece set named '" + info->name + "' is already installed");
            auto set = ui::SvgPieceSet::load(path);
            if (!set) return invalid(item, set.error().context);
            item.detail = info->license + (info->author.empty() ? "" : " · " + info->author);
            item.valid = true;
            return item;
        }
        if (std::filesystem::exists(path / "engine.yaml", ec)) {
            item.kind = ImportKind::Engine;
            auto d = engine::loadDescriptor(path / "engine.yaml");
            if (!d) return invalid(item, d.error().context);
            item.name = d->id;
            if ((installed.engine && installed.engine(d->id)) ||
                std::filesystem::exists(paths_.installed("engines") / path.filename(), ec))
                return invalid(item, "an engine with id '" + d->id + "' is already installed");
            const std::filesystem::path bin = d->binaryPath(platform_);
            if (bin.empty()) return invalid(item, "engine.yaml has no binary for platform " + platform_);
            if (!std::filesystem::is_regular_file(bin, ec)) return invalid(item, "binary not found: " + bin.filename().string());
            const std::string format = executableFormat(bin);
            if (!executableRunsOn(format, platform_))
                return invalid(item, bin.filename().string() + " is " + (format.empty() ? "not a program" : format) +
                                         ", this reader needs " + (platform_ == "arm" ? "elf-arm" : platform_));
            for (const auto& f : d->files)
                if (!std::filesystem::is_regular_file(path / f, ec)) return invalid(item, "file not found: " + f);
            item.detail = d->name + (d->license.empty() ? "" : " · " + d->license);
            item.valid = true;
            return item;
        }
        if (std::filesystem::exists(path / "backup.txt", ec)) {
            // A progress backup (Stats → Menu → Back up): restored, never installed.
            item.kind = ImportKind::Backup;
            auto chess = storage::checkBackupFile(path / "chess.db", static_cast<int>(storage::chessDbMigrations().size()));
            if (!chess) return invalid(item, chess.error().context);
            if (std::filesystem::exists(path / "progress.db", ec))
                if (auto p = storage::checkBackupFile(path / "progress.db",
                                                      static_cast<int>(puzzles::progressMigrations().size()));
                    !p)
                    return invalid(item, p.error().context);
            std::string created;
            if (auto note = util::readTextFile(path / "backup.txt", 4096); note) {
                const size_t at = note->find("created ");
                if (at != std::string::npos) created = note->substr(at + 8, note->find('\n', at) - at - 8);
            }
            item.detail = "progress, games and settings" + (created.empty() ? "" : " · " + created);
            item.valid = true;
            return item;
        }
        return invalid(item, "a folder without pieceset.yaml, engine.yaml or backup.txt");
    }

    const std::string ext = lower(path.extension().string());
    if (ext == ".pgn") {
        item.kind = ImportKind::Pgn;
        auto text = util::readTextFile(path);
        if (!text) return invalid(item, text.error().context);
        // Counting "[Event" lines is enough for the list; parsing happens when the file is used.
        size_t games = 0;
        for (size_t pos = 0; pos < text->size();) {
            if (text->compare(pos, 7, "[Event ") == 0) ++games;
            const size_t nl = text->find('\n', pos);
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        if (games == 0) {
            auto first = chess::parsePgnGame(*text);
            if (!first) return invalid(item, first.error().context);
            games = 1;
        }
        item.detail = plural(games, "game", "games");
        item.valid = true;
        return item;
    }
    if (ext == ".fen") {
        item.kind = ImportKind::Fen;
        auto text = util::readTextFile(path, 1u << 20);
        if (!text) return invalid(item, text.error().context);
        size_t ok = 0, bad = 0;
        size_t start = 0;
        while (start <= text->size()) {
            size_t end = text->find('\n', start);
            if (end == std::string::npos) end = text->size();
            std::string line = text->substr(start, end - start);
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            if (!line.empty()) (chess::parseFen(line) ? ok : bad)++;
            start = end + 1;
        }
        if (ok == 0) return invalid(item, "no valid FEN line");
        item.detail = plural(ok, "position", "positions") + (bad ? " (" + std::to_string(bad) + " invalid)" : "");
        item.valid = true;
        return item;
    }
    if (ext == ".db") {
        // A game pack or a puzzle pack; either is validated by opening it read-only.
        if (auto g = games::GamePack::open(path); g) {
            item.kind = ImportKind::GamePack;
            item.detail = plural(static_cast<size_t>(g->count()), "game", "games");
        } else if (auto p = puzzles::PuzzlePack::open(path); p) {
            item.kind = ImportKind::PuzzlePack;
            item.detail = plural(static_cast<size_t>(p->count()), "puzzle", "puzzles");
        } else {
            return invalid(item, "not a PocketChess game or puzzle pack (" + g.error().context + ")");
        }
        const std::string kind = item.kind == ImportKind::GamePack ? "games" : "puzzles";
        if ((installed.pack && installed.pack(kind, item.name)) ||
            std::filesystem::exists(paths_.installed("content") / kind / path.filename(), ec))
            return invalid(item, "a pack named '" + item.name + "' is already installed");
        item.valid = true;
        return item;
    }
    if (ext == ".yaml" || ext == ".yml") {
        item.kind = ImportKind::Theme;
        auto t = ui::loadThemeFile(path);
        if (!t) return invalid(item, t.error().context);
        item.name = t->name;
        if ((installed.theme && installed.theme(t->name)) ||
            std::filesystem::exists(paths_.installed("themes") / path.filename(), ec))
            return invalid(item, "a theme named '" + t->name + "' is already installed");
        item.detail = t->mono ? "monochrome" : "color";
        item.valid = true;
        return item;
    }
    return invalid(item, "not a PGN, FEN, pack, board theme, piece set or engine");
}

Result<std::filesystem::path> Importer::install(const ImportItem& item) const {
    if (!item.valid) return makeError(ErrorCode::InvalidArgument, item.path.filename().string() + ": " + item.detail);
    std::filesystem::path dest;
    switch (item.kind) {
    case ImportKind::GamePack: dest = paths_.installed("content") / "games" / item.path.filename(); break;
    case ImportKind::PuzzlePack: dest = paths_.installed("content") / "puzzles" / item.path.filename(); break;
    case ImportKind::PieceSet: dest = paths_.installed("pieces") / item.path.filename(); break;
    case ImportKind::Theme: dest = paths_.installed("themes") / (item.path.stem().string() + ".yaml"); break;
    case ImportKind::Engine: dest = paths_.installed("engines") / item.path.filename(); break;
    default:
        return makeError(ErrorCode::Unsupported, std::string(toString(item.kind)) + " files are opened, not installed");
    }
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) return makeError(ErrorCode::InvalidArgument, dest.string() + " already exists");
    if (auto r = moveEntry(item.path, dest); !r) return r.error();
    util::Log::info("import", std::string(toString(item.kind)) + " '" + item.name + "' installed to " + dest.string());
    return dest;
}

Result<std::filesystem::path> Importer::makeGamePack(const ImportItem& item, const games::OpeningBook* book,
                                                     const std::function<void(int)>& progress) const {
    if (item.kind != ImportKind::Pgn || !item.valid)
        return makeError(ErrorCode::InvalidArgument, "a game pack is made from a valid PGN file");
    const std::filesystem::path dest = paths_.installed("content") / "games" / (item.path.stem().string() + ".db");
    const std::filesystem::path tmp = dest.string() + ".part";
    std::error_code ec;
    if (std::filesystem::exists(dest, ec))
        return makeError(ErrorCode::InvalidArgument, "a game pack named '" + dest.stem().string() + "' already exists");
    auto text = util::readTextFile(item.path);
    if (!text) return text.error();
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::filesystem::remove(tmp, ec);
    int imported = 0, rejected = 0;
    {
        auto db = storage::Database::open(tmp, storage::Database::Mode::ReadWrite);
        if (!db) return db.error();
        (void)db->exec("PRAGMA journal_mode=DELETE");  // one self-contained file, like desktop packs
        games::GameImporter importer(*db, book);
        if (auto r = importer.begin(); !r) return r.error();
        chess::PgnReader reader(*text);
        while (auto g = reader.next()) {
            if (!*g) {
                ++rejected;
                continue;
            }
            auto added = importer.add(**g);
            if (!added) return added.error();
            if (*added && ++imported % 500 == 0 && progress) progress(imported);
        }
        if (auto r = importer.finish({{"name", item.path.stem().string()},
                                      {"source", item.path.filename().string()},
                                      {"rejected", std::to_string(rejected)},
                                      {"made_by", "PocketChess Import"}});
            !r)
            return r.error();
    }
    if (imported == 0) {
        std::filesystem::remove(tmp, ec);
        return makeError(ErrorCode::ParsePgn, "no game could be read from " + item.path.filename().string());
    }
    std::filesystem::rename(tmp, dest, ec);
    if (ec) return makeError(ErrorCode::Io, "cannot create " + dest.string() + ": " + ec.message());
    util::Log::info("import", item.path.filename().string() + " -> game pack " + dest.stem().string() + ": " +
                                  std::to_string(imported) + " games, " + std::to_string(rejected) + " rejected");
    return dest;
}

} // namespace pchess::app
