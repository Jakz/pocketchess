// SPDX-License-Identifier: GPL-3.0-or-later
#include "tools/setup/Json.h"

#include <cctype>

namespace pchess::setup {

Json* Json::find(const std::string& key) {
    for (auto& [k, v] : members)
        if (k == key) return &v;
    return nullptr;
}

void Json::set(const std::string& key, Json value) {
    if (Json* j = find(key)) *j = std::move(value);
    else members.emplace_back(key, std::move(value));
}

bool Json::erase(const std::string& key) {
    for (auto it = members.begin(); it != members.end(); ++it)
        if (it->first == key) {
            members.erase(it);
            return true;
        }
    return false;
}

namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;

    void ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    std::optional<std::string> str() {
        if (!eat('"')) return std::nullopt;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char c = s[++i];
                switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': out += "\\u"; break;  // kept escaped: written back unchanged
                default: out += c; break;
                }
                ++i;
            } else {
                out += s[i++];
            }
        }
        if (i >= s.size()) return std::nullopt;
        ++i;
        return out;
    }
    std::optional<Json> value(int depth) {
        if (depth > 64) return std::nullopt;
        ws();
        if (i >= s.size()) return std::nullopt;
        Json j;
        const char c = s[i];
        if (c == '{') {
            ++i;
            j.type = Json::Type::Object;
            if (eat('}')) return j;
            do {
                auto k = str();
                if (!k || !eat(':')) return std::nullopt;
                auto v = value(depth + 1);
                if (!v) return std::nullopt;
                j.members.emplace_back(std::move(*k), std::move(*v));
            } while (eat(','));
            if (!eat('}')) return std::nullopt;
        } else if (c == '[') {
            ++i;
            j.type = Json::Type::Array;
            if (eat(']')) return j;
            do {
                auto v = value(depth + 1);
                if (!v) return std::nullopt;
                j.items.push_back(std::move(*v));
            } while (eat(','));
            if (!eat(']')) return std::nullopt;
        } else if (c == '"') {
            auto t = str();
            if (!t) return std::nullopt;
            j = Json::string(std::move(*t));
        } else if (s.compare(i, 4, "true") == 0 || s.compare(i, 5, "false") == 0) {
            j.type = Json::Type::Bool;
            j.boolean = s[i] == 't';
            i += j.boolean ? 4 : 5;
        } else if (s.compare(i, 4, "null") == 0) {
            i += 4;
        } else {
            const size_t start = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
                                    s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
                ++i;
            if (i == start) return std::nullopt;
            j.type = Json::Type::Number;
            j.text = s.substr(start, i - start);
        }
        return j;
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (size_t k = 0; k < s.size(); ++k) {
        const char c = s[k];
        if (c == '\\' && k + 1 < s.size() && s[k + 1] == 'u') out += '\\';  // a kept \uXXXX
        else if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    out += '"';
}

void write(std::string& out, const Json& j, int indent, int level) {
    const std::string pad(static_cast<size_t>(indent * (level + 1)), ' ');
    const std::string end(static_cast<size_t>(indent * level), ' ');
    switch (j.type) {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += j.boolean ? "true" : "false"; break;
    case Json::Type::Number: out += j.text; break;
    case Json::Type::String: escape(out, j.text); break;
    case Json::Type::Array:
        if (j.items.empty()) {
            out += "[]";
            break;
        }
        out += "[\n";
        for (size_t k = 0; k < j.items.size(); ++k) {
            out += pad;
            write(out, j.items[k], indent, level + 1);
            out += k + 1 < j.items.size() ? ",\n" : "\n";
        }
        out += end + "]";
        break;
    case Json::Type::Object:
        if (j.members.empty()) {
            out += "{}";
            break;
        }
        out += "{\n";
        for (size_t k = 0; k < j.members.size(); ++k) {
            out += pad;
            escape(out, j.members[k].first);
            out += ": ";
            write(out, j.members[k].second, indent, level + 1);
            out += k + 1 < j.members.size() ? ",\n" : "\n";
        }
        out += end + "}";
        break;
    }
}

} // namespace

std::optional<Json> Json::parse(const std::string& text) {
    Parser p{text};
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) p.i = 3;  // UTF-8 BOM
    auto v = p.value(0);
    if (!v) return std::nullopt;
    p.ws();
    if (p.i != text.size()) return std::nullopt;
    return v;
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, *this, indent, 0);
    return out + "\n";
}

} // namespace pchess::setup
