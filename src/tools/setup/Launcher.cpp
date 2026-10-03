// SPDX-License-Identifier: GPL-3.0-or-later
// The reader's launcher (firmware 6.8+): system/config/desktop/view.json lists the home-screen
// groups (arrays of app keys) and the apps; user apps are "U_<name>" entries with path, title,
// icon and focused_icon (ADR 0019, docs/installer.md). Edits go through a parsed copy.
#include "tools/setup/Json.h"
#include "tools/setup/Setup.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace pchess::setup {

namespace {

constexpr const char* kKey = "U_pocketchess";
constexpr const char* kBackup = "view.json.pocketchess-backup";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isKeyList(const Json& j) {
    if (j.type != Json::Type::Array) return false;
    for (const Json& i : j.items)
        if (i.type != Json::Type::String) return false;
    return true;
}

// Every array of strings (the groups), with its name, anywhere in the document.
void groups(Json& j, std::vector<std::pair<std::string, Json*>>& out, const std::string& name = "") {
    if (isKeyList(j) && !name.empty()) out.emplace_back(name, &j);
    if (j.type == Json::Type::Object)
        for (auto& [k, v] : j.members) groups(v, out, k);
    if (j.type == Json::Type::Array)
        for (Json& i : j.items) groups(i, out, name);
}

// The object holding the app entries: the one that already has a "U_..." member or members with a
// "path", else the document itself.
Json* appTable(Json& j) {
    if (j.type == Json::Type::Object) {
        for (auto& [k, v] : j.members)
            if (k.rfind("U_", 0) == 0 && v.type == Json::Type::Object) return &j;
        for (auto& [k, v] : j.members)
            if (Json* t = appTable(v)) return t;
    }
    if (j.type == Json::Type::Array)
        for (Json& i : j.items)
            if (Json* t = appTable(i)) return t;
    return nullptr;
}

std::optional<std::string> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFile(const fs::path& p, const std::string& text) {
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.write(text.data(), static_cast<std::streamsize>(text.size()))) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    return !ec;
}

} // namespace

std::optional<std::string> withPocketChess(const std::string& viewJson, const std::string& group) {
    auto doc = Json::parse(viewJson);
    if (!doc || doc->type != Json::Type::Object) return std::nullopt;
    Json* table = appTable(*doc);
    if (!table) table = &*doc;
    Json entry = table->find(kKey) ? *table->find(kKey) : Json::object();
    entry.set("path", Json::string("/mnt/ext1/applications/pocketchess.app"));
    if (!entry.find("title")) entry.set("title", Json::string("PocketChess"));
    entry.set("icon", Json::string("/mnt/ext1/applications/icons/pocketchess.bmp"));
    entry.set("focused_icon", Json::string("/mnt/ext1/applications/icons/pocketchess_f.bmp"));
    table->set(kKey, std::move(entry));
    if (!group.empty()) {
        std::vector<std::pair<std::string, Json*>> all;
        groups(*doc, all);
        Json* target = nullptr;
        for (auto& [name, list] : all) {
            const std::string n = lower(name);
            if (n == lower(group) || n == "@" + lower(group)) target = list;
        }
        if (!target) return std::nullopt;
        for (auto& [name, list] : all) {
            auto& items = list->items;
            for (size_t k = 0; k < items.size();)
                if (items[k].text == kKey) items.erase(items.begin() + static_cast<std::ptrdiff_t>(k));
                else ++k;
        }
        target->items.push_back(Json::string(kKey));
    }
    return doc->dump();
}

std::string withoutPocketChess(const std::string& viewJson) {
    auto doc = Json::parse(viewJson);
    if (!doc) return viewJson;
    if (Json* table = appTable(*doc)) table->erase(kKey);
    doc->erase(kKey);
    std::vector<std::pair<std::string, Json*>> all;
    groups(*doc, all);
    for (auto& [name, list] : all) {
        auto& items = list->items;
        for (size_t k = 0; k < items.size();)
            if (items[k].text == kKey) items.erase(items.begin() + static_cast<std::ptrdiff_t>(k));
            else ++k;
    }
    return doc->dump();
}

bool installLauncher(const Layout& l, const fs::path& reader, const std::string& group) {
    const fs::path desktop = reader / "system" / "config" / "desktop";
    const fs::path view = desktop / "view.json";
    std::error_code ec;
    // The icons first: they are harmless on firmware without view.json.
    const fs::path icons = reader / "applications" / "icons";
    fs::create_directories(icons, ec);
    for (const char* f : {"pocketchess.bmp", "pocketchess_f.bmp"}) {
        fs::copy_file(l.root / "reader" / "icons" / f, icons / f, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "cannot copy the icon %s: %s\n", f, ec.message().c_str());
            return false;
        }
    }
    auto text = readFile(view);
    if (!text) {
        std::printf("no %s (firmware before 6.8?): icons copied, launcher left as it is\n", view.string().c_str());
        return true;
    }
    auto patched = withPocketChess(*text, group);
    if (!patched) {
        std::fprintf(stderr, "cannot change %s%s; nothing was changed\n", view.string().c_str(),
                     group.empty() ? " (unexpected format)" : (" (no group named '" + group + "'?)").c_str());
        if (auto doc = Json::parse(*text)) {
            std::vector<std::pair<std::string, Json*>> all;
            groups(*doc, all);
            std::fprintf(stderr, "groups on this reader:");
            for (auto& [name, list] : all) std::fprintf(stderr, " %s", name.c_str());
            std::fprintf(stderr, "\n");
        }
        return false;
    }
    if (*patched == *text) {
        std::printf("launcher already set up\n");
        return true;
    }
    if (!fs::exists(desktop / kBackup)) {
        fs::copy_file(view, desktop / kBackup, ec);
        if (ec) {
            std::fprintf(stderr, "cannot back up view.json: %s; nothing was changed\n", ec.message().c_str());
            return false;
        }
        std::printf("backup: %s\n", (desktop / kBackup).string().c_str());
    }
    if (!writeFile(view, *patched)) {
        std::fprintf(stderr, "cannot write %s\n", view.string().c_str());
        return false;
    }
    std::printf("launcher: PocketChess icon%s set (shown after the reader restarts its home screen)\n",
                group.empty() ? "" : (" and group '" + group + "'").c_str());
    return true;
}

bool resetLauncher(const fs::path& reader) {
    const fs::path desktop = reader / "system" / "config" / "desktop";
    std::error_code ec;
    if (fs::exists(desktop / kBackup)) {
        fs::copy_file(desktop / kBackup, desktop / "view.json", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "cannot restore view.json: %s\n", ec.message().c_str());
            return false;
        }
        fs::remove(desktop / kBackup, ec);
        std::printf("view.json restored from the backup\n");
    } else if (auto text = readFile(desktop / "view.json")) {
        if (!writeFile(desktop / "view.json", withoutPocketChess(*text))) return false;
        std::printf("no backup: the PocketChess entry was removed from view.json\n");
    }
    for (const char* f : {"pocketchess.bmp", "pocketchess_f.bmp"}) fs::remove(reader / "applications" / "icons" / f, ec);
    std::printf("icons removed\n");
    return true;
}

} // namespace pchess::setup
