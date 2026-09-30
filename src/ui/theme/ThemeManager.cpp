// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme/ThemeManager.h"

#include "core/util/Log.h"

#include <algorithm>
#include <cstdio>

namespace pchess::ui {

namespace {

struct ColorKey {
    const char* key;
    Rgb BoardTheme::*field;
};

constexpr ColorKey kColors[] = {
    {"light", &BoardTheme::light},           {"dark", &BoardTheme::dark},
    {"selected", &BoardTheme::selected},     {"last_move", &BoardTheme::lastMove},
    {"check", &BoardTheme::check},           {"marker", &BoardTheme::marker},
    {"coord_light", &BoardTheme::coordOnLight}, {"coord_dark", &BoardTheme::coordOnDark},
    {"background", &BoardTheme::background}, {"ink", &BoardTheme::ink},
    {"frame", &BoardTheme::frame},
};

struct PatternKey {
    const char* key;
    MarkerStyle BoardTheme::*field;
};

constexpr PatternKey kPatterns[] = {
    {"selected_pattern", &BoardTheme::selectedStyle},
    {"last_move_pattern", &BoardTheme::lastMoveStyle},
    {"check_pattern", &BoardTheme::checkStyle},
};

constexpr const char* kStyleNames[] = {"tint", "outline", "hatch", "dots", "double_border"};

Error bad(const util::YamlNode& node, const std::string& what) {
    return makeError(ErrorCode::CorruptData, "line " + std::to_string(node.line()) + ": " + what);
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Result<Rgb> parseColor(const util::YamlNode& node, const std::string& key) {
    if (!node.isScalar() || node.text().empty())
        return bad(node, key + ": expected \"#rrggbb\" (quoted: # starts a comment) or a gray 0..255");
    const std::string& s = node.text();
    if (s[0] == '#') {
        if (s.size() != 7) return bad(node, key + ": '" + s + "' is not #rrggbb");
        uint32_t v = 0;
        for (size_t i = 1; i < 7; ++i) {
            const int d = hexDigit(s[i]);
            if (d < 0) return bad(node, key + ": '" + s + "' is not #rrggbb");
            v = v * 16 + static_cast<uint32_t>(d);
        }
        return Rgb::hex(v);
    }
    const auto gray = node.asInt();
    if (!gray || *gray < 0 || *gray > 255) return bad(node, key + ": '" + s + "' is neither #rrggbb nor 0..255");
    return Rgb::gray(static_cast<uint8_t>(*gray));
}

std::string hex(Rgb c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

} // namespace

Result<BoardTheme> parseTheme(const util::YamlNode& doc) {
    if (!doc.isMap()) return bad(doc, "a theme is a mapping of keys (name, mode, light, dark, ...)");
    const auto name = doc["name"].asString();
    if (!name || name->empty()) return bad(doc, "name: missing");
    const std::string mode = doc["mode"].stringOr("color");
    if (mode != "color" && mode != "mono") return bad(doc["mode"], "mode: '" + mode + "' (color or mono)");

    BoardTheme t = mode == "mono" ? BoardTheme::classicMono() : BoardTheme::paper();
    t.name = *name;
    for (const auto& [key, value] : doc.entries()) {
        if (key == "name" || key == "mode") continue;
        std::string colorKey = key;
        if (colorKey.size() > 5 && colorKey.ends_with("_gray")) colorKey.resize(colorKey.size() - 5);
        if (auto it = std::find_if(std::begin(kColors), std::end(kColors),
                                   [&](const ColorKey& c) { return colorKey == c.key; });
            it != std::end(kColors)) {
            auto c = parseColor(value, key);
            if (!c) return c.error();
            t.*(it->field) = *c;
            continue;
        }
        if (auto it = std::find_if(std::begin(kPatterns), std::end(kPatterns),
                                   [&](const PatternKey& p) { return key == p.key; });
            it != std::end(kPatterns)) {
            const std::string v = value.stringOr("");
            auto s = std::find(std::begin(kStyleNames), std::end(kStyleNames), v);
            if (s == std::end(kStyleNames))
                return bad(value, key + ": '" + v + "' (tint, outline, hatch, dots or double_border)");
            t.*(it->field) = static_cast<MarkerStyle>(s - std::begin(kStyleNames));
            continue;
        }
        if (key == "tint_markers" || key == "coordinates") {
            const auto b = value.asBool();
            if (!b) return bad(value, key + ": expected true or false");
            (key == "tint_markers" ? t.tintMarkers : t.coordinates) = *b;
            continue;
        }
        return bad(value, "unknown key '" + key + "'");
    }
    return t;
}

Result<BoardTheme> loadThemeFile(const std::filesystem::path& file) {
    auto doc = util::loadYamlFile(file);
    if (!doc) return doc.error();
    return parseTheme(*doc);
}

std::string themeToYaml(const BoardTheme& t) {
    std::string out = "name: " + t.name + "\nmode: " + (t.mono ? "mono" : "color") + "\n";
    for (const ColorKey& c : kColors) out += std::string(c.key) + ": \"" + hex(t.*(c.field)) + "\"\n";
    for (const PatternKey& p : kPatterns)
        out += std::string(p.key) + ": " + kStyleNames[static_cast<int>(t.*(p.field))] + "\n";
    out += std::string("tint_markers: ") + (t.tintMarkers ? "true" : "false") + "\n";
    return out;
}

ThemeManager::ThemeManager() : themes_(BoardTheme::builtIns()) {}

bool ThemeManager::add(BoardTheme theme, const std::filesystem::path& file) {
    if (find(theme.name)) {
        problems_.push_back({file, "a theme named '" + theme.name + "' already exists"});
        return false;
    }
    themes_.push_back(std::move(theme));
    return true;
}

int ThemeManager::loadDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".yaml") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    int added = 0;
    for (const auto& f : files) {
        auto t = loadThemeFile(f);
        if (!t) {
            problems_.push_back({f, t.error().message()});
            util::Log::warn("themes", f.filename().string() + ": " + t.error().message() + " - skipped");
            continue;
        }
        if (add(std::move(*t), f)) ++added;
        else util::Log::warn("themes", f.filename().string() + ": " + problems_.back().message + " - skipped");
    }
    util::Log::info("themes", std::to_string(added) + " theme(s) from " + dir.string());
    return added;
}

std::vector<std::string> ThemeManager::names(bool mono) const {
    std::vector<std::string> out;
    for (const BoardTheme& t : themes_)
        if (t.mono == mono) out.push_back(t.name);
    return out;
}

const BoardTheme* ThemeManager::find(const std::string& name) const {
    for (const BoardTheme& t : themes_)
        if (t.name == name) return &t;
    return nullptr;
}

BoardTheme ThemeManager::resolve(const std::string& name, bool mono) const {
    if (const BoardTheme* t = find(name); t && t->mono == mono) return *t;
    return mono ? BoardTheme::classicMono() : BoardTheme::paper();
}

} // namespace pchess::ui
