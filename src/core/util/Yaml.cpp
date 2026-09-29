// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Yaml.h"

#include "core/util/Files.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <locale>
#include <sstream>

namespace pchess::util {

// --- YamlNode ----------------------------------------------------------------------------------

namespace {
const YamlNode& nullNode() {
    static const YamlNode n;
    return n;
}

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
} // namespace

YamlNode YamlNode::scalar(std::string text, bool quoted, int line) {
    YamlNode n;
    n.kind_ = Kind::Scalar;
    n.text_ = std::move(text);
    n.quoted_ = quoted;
    n.line_ = line;
    return n;
}

YamlNode YamlNode::sequence(int line) {
    YamlNode n;
    n.kind_ = Kind::Sequence;
    n.line_ = line;
    return n;
}

YamlNode YamlNode::map(int line) {
    YamlNode n;
    n.kind_ = Kind::Map;
    n.line_ = line;
    return n;
}

size_t YamlNode::size() const {
    if (kind_ == Kind::Sequence) return items_.size();
    if (kind_ == Kind::Map) return entries_.size();
    return 0;
}

const YamlNode& YamlNode::operator[](size_t index) const {
    return kind_ == Kind::Sequence && index < items_.size() ? items_[index] : nullNode();
}

const YamlNode& YamlNode::operator[](std::string_view key) const {
    const YamlNode* n = find(key);
    return n ? *n : nullNode();
}

const YamlNode* YamlNode::find(std::string_view key) const {
    if (kind_ != Kind::Map) return nullptr;
    for (const auto& [k, v] : entries_)
        if (k == key) return &v;
    return nullptr;
}

bool YamlNode::insert(std::string key, YamlNode value) {
    if (find(key)) return false;
    entries_.emplace_back(std::move(key), std::move(value));
    return true;
}

std::optional<std::string> YamlNode::asString() const {
    if (kind_ != Kind::Scalar) return std::nullopt;
    return text_;
}

std::optional<int64_t> YamlNode::asInt() const {
    if (kind_ != Kind::Scalar || text_.empty()) return std::nullopt;
    const char* b = text_.data();
    const char* e = b + text_.size();
    if (*b == '+') ++b;
    int64_t v = 0;
    const auto r = std::from_chars(b, e, v);
    if (r.ec != std::errc() || r.ptr != e) return std::nullopt;
    return v;
}

std::optional<double> YamlNode::asDouble() const {
    if (kind_ != Kind::Scalar || text_.empty()) return std::nullopt;
    std::istringstream is(text_);
    is.imbue(std::locale::classic());
    double v = 0;
    is >> v;
    if (is.fail()) return std::nullopt;
    is >> std::ws;
    if (!is.eof()) return std::nullopt;
    return v;
}

std::optional<bool> YamlNode::asBool() const {
    if (kind_ != Kind::Scalar) return std::nullopt;
    const std::string t = lower(text_);
    if (t == "true" || t == "yes" || t == "on") return true;
    if (t == "false" || t == "no" || t == "off") return false;
    return std::nullopt;
}

std::vector<std::string> YamlNode::stringList() const {
    std::vector<std::string> out;
    if (kind_ == Kind::Scalar)
        out.push_back(text_);
    else if (kind_ == Kind::Sequence)
        for (const auto& i : items_)
            if (i.isScalar()) out.push_back(i.text());
    return out;
}

// --- Parser ------------------------------------------------------------------------------------

namespace {

struct Line {
    int number = 0;
    int indent = 0;
    std::string text;  // without indentation, comment and trailing whitespace
};

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && (isSpace(s.back()) || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool quoteMayStart(std::string_view s, size_t i) {
    if (i == 0) return true;
    const char p = s[i - 1];
    return isSpace(p) || p == ':' || p == ',' || p == '[' || p == '{' || p == '-';
}

// Removes a trailing comment: '#' at the start or after whitespace, outside quotes.
std::string stripComment(std::string_view s) {
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote == '\'') {
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'')
                    ++i;
                else
                    quote = 0;
            }
        } else if (quote == '"') {
            if (c == '\\')
                ++i;
            else if (c == '"')
                quote = 0;
        } else if ((c == '\'' || c == '"') && quoteMayStart(s, i)) {
            quote = c;
        } else if (c == '#' && (i == 0 || isSpace(s[i - 1]))) {
            return std::string(s.substr(0, i));
        }
    }
    return std::string(s);
}

bool isSeqItem(std::string_view t) {
    return t == "-" || (t.size() >= 2 && t[0] == '-' && isSpace(t[1]));
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    explicit Parser(std::vector<Line> lines) : lines_(std::move(lines)) {}

    Result<YamlNode> run() {
        if (lines_.empty()) return YamlNode{};
        YamlNode root = parseNode(lines_[0].indent);
        if (!error_.empty()) return makeError(ErrorCode::CorruptData, error_);
        if (pos_ < lines_.size())
            return makeError(ErrorCode::CorruptData,
                             "line " + std::to_string(lines_[pos_].number) + ": bad indentation");
        return root;
    }

private:
    bool failed() const { return !error_.empty(); }
    YamlNode fail(int line, const std::string& msg) {
        if (error_.empty()) error_ = "line " + std::to_string(line) + ": " + msg;
        return {};
    }

    // Position of the ':' ending a block mapping key, or npos.
    static size_t mapColon(std::string_view t) {
        if (t.empty() || t[0] == '{' || t[0] == '[' || isSeqItem(t)) return std::string_view::npos;
        size_t i = 0;
        if (t[0] == '"' || t[0] == '\'') {
            const char q = t[0];
            for (i = 1; i < t.size(); ++i) {
                if (q == '"' && t[i] == '\\') {
                    ++i;
                    continue;
                }
                if (t[i] == q) {
                    if (q == '\'' && i + 1 < t.size() && t[i + 1] == '\'') {
                        ++i;
                        continue;
                    }
                    break;
                }
            }
            if (i >= t.size()) return std::string_view::npos;
            ++i;
            while (i < t.size() && isSpace(t[i])) ++i;
            return i < t.size() && t[i] == ':' && (i + 1 == t.size() || isSpace(t[i + 1])) ? i : std::string_view::npos;
        }
        for (; i < t.size(); ++i)
            if (t[i] == ':' && (i + 1 == t.size() || isSpace(t[i + 1]))) return i;
        return std::string_view::npos;
    }

    YamlNode parseNode(int indent) {
        const Line& l = lines_[pos_];
        if (isSeqItem(l.text)) return parseSequence(indent);
        if (mapColon(l.text) != std::string_view::npos) return parseMap(indent);
        ++pos_;
        YamlNode v = parseInline(l.text, l.number);
        if (!failed() && pos_ < lines_.size() && lines_[pos_].indent > indent)
            return fail(lines_[pos_].number, "unexpected indentation (multi-line scalars are not supported)");
        return v;
    }

    YamlNode parseMap(int indent) {
        YamlNode m = YamlNode::map(lines_[pos_].number);
        while (!failed() && pos_ < lines_.size() && lines_[pos_].indent == indent) {
            const Line l = lines_[pos_];
            const size_t colon = mapColon(l.text);
            if (colon == std::string_view::npos) return fail(l.number, "expected 'key: value'");
            std::string key = parseKey(trim(std::string_view(l.text).substr(0, colon)), l.number);
            if (failed()) return {};
            const std::string_view rest = trim(std::string_view(l.text).substr(colon + 1));
            ++pos_;
            YamlNode value;
            if (rest.empty()) {
                if (pos_ < lines_.size() && lines_[pos_].indent > indent)
                    value = parseNode(lines_[pos_].indent);
                else if (pos_ < lines_.size() && lines_[pos_].indent == indent && isSeqItem(lines_[pos_].text))
                    value = parseSequence(indent);
                else
                    value = YamlNode{};
            } else {
                value = parseInline(rest, l.number);
                if (!failed() && pos_ < lines_.size() && lines_[pos_].indent > indent)
                    return fail(lines_[pos_].number, "unexpected indentation");
            }
            if (failed()) return {};
            if (!m.insert(std::move(key), std::move(value))) return fail(l.number, "duplicate key");
        }
        return m;
    }

    YamlNode parseSequence(int indent) {
        YamlNode s = YamlNode::sequence(lines_[pos_].number);
        while (!failed() && pos_ < lines_.size() && lines_[pos_].indent == indent && isSeqItem(lines_[pos_].text)) {
            Line& l = lines_[pos_];
            size_t off = 1;
            while (off < l.text.size() && isSpace(l.text[off])) ++off;
            const std::string rest = l.text.substr(off);
            if (rest.empty()) {
                ++pos_;
                if (pos_ < lines_.size() && lines_[pos_].indent > indent)
                    s.push(parseNode(lines_[pos_].indent));
                else
                    s.push(YamlNode{});
            } else if (isSeqItem(rest) || mapColon(rest) != std::string_view::npos) {
                // "- key: value" / "- - x": the item is a nested block starting at the item column.
                l.indent = indent + static_cast<int>(off);
                l.text = rest;
                s.push(parseNode(l.indent));
            } else {
                const int number = l.number;
                ++pos_;
                s.push(parseInline(rest, number));
                if (!failed() && pos_ < lines_.size() && lines_[pos_].indent > indent)
                    return fail(lines_[pos_].number, "unexpected indentation");
            }
        }
        return s;
    }

    std::string parseKey(std::string_view k, int line) {
        if (k.empty()) {
            fail(line, "empty key");
            return {};
        }
        if (k[0] == '"' || k[0] == '\'') {
            size_t i = 0;
            std::string out;
            if (!parseQuoted(k, i, out, line)) return {};
            return out;
        }
        return std::string(k);
    }

    YamlNode parseInline(std::string_view text, int line) {
        text = trim(text);
        if (text.empty()) return YamlNode{};
        const char c = text[0];
        if (c == '{' || c == '[') {
            std::string s(text);
            while (!balanced(s) && pos_ < lines_.size()) {
                s += ' ';
                s += lines_[pos_].text;
                ++pos_;
            }
            size_t i = 0;
            YamlNode v = parseFlow(s, i, line);
            if (failed()) return {};
            while (i < s.size() && isSpace(s[i])) ++i;
            if (i != s.size()) return fail(line, "unexpected text after a flow collection");
            return v;
        }
        if (c == '"' || c == '\'') {
            size_t i = 0;
            std::string out;
            if (!parseQuoted(text, i, out, line)) return {};
            if (!trim(text.substr(i)).empty()) return fail(line, "unexpected text after a quoted string");
            return YamlNode::scalar(std::move(out), true, line);
        }
        if (c == '&' || c == '*' || c == '!' || c == '|' || c == '>' || c == '%' || c == '@' || c == '`')
            return fail(line, std::string("unsupported YAML feature '") + c + "'");
        if (text == "~" || text == "null" || text == "Null" || text == "NULL") return YamlNode{};
        return YamlNode::scalar(std::string(text), false, line);
    }

    static bool balanced(std::string_view s) {
        int depth = 0;
        char quote = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (quote) {
                if (quote == '"' && c == '\\')
                    ++i;
                else if (c == quote)
                    quote = 0;
            } else if ((c == '"' || c == '\'') && quoteMayStart(s, i)) {
                quote = c;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
            }
        }
        return depth <= 0 && quote == 0;
    }

    bool parseQuoted(std::string_view s, size_t& i, std::string& out, int line) {
        const char q = s[i++];
        while (i < s.size()) {
            const char c = s[i++];
            if (q == '\'') {
                if (c == '\'') {
                    if (i < s.size() && s[i] == '\'') {
                        out += '\'';
                        ++i;
                        continue;
                    }
                    return true;
                }
                out += c;
                continue;
            }
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '0': out += '\0'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case '/': out += '/'; break;
                case ' ': out += ' '; break;
                case 'x':
                case 'u':
                case 'U': {
                    const int digits = e == 'x' ? 2 : (e == 'u' ? 4 : 8);
                    uint32_t cp = 0;
                    for (int d = 0; d < digits; ++d) {
                        const int h = i < s.size() ? hexValue(s[i]) : -1;
                        if (h < 0) {
                            fail(line, "bad escape in a quoted string");
                            return false;
                        }
                        cp = cp * 16 + static_cast<uint32_t>(h);
                        ++i;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail(line, std::string("unknown escape '\\") + e + "'"); return false;
            }
        }
        fail(line, "unterminated quoted string");
        return false;
    }

    static void skipSpace(std::string_view s, size_t& i) {
        while (i < s.size() && isSpace(s[i])) ++i;
    }

    // Plain scalar inside a flow collection; stops at , ] } (and at ':' + space for keys).
    static std::string flowPlain(std::string_view s, size_t& i, bool key) {
        const size_t b = i;
        while (i < s.size()) {
            const char c = s[i];
            if (c == ',' || c == ']' || c == '}') break;
            if (key && c == ':' && (i + 1 == s.size() || isSpace(s[i + 1]) || s[i + 1] == ',' || s[i + 1] == '}'))
                break;
            ++i;
        }
        return std::string(trim(s.substr(b, i - b)));
    }

    YamlNode parseFlow(std::string_view s, size_t& i, int line) {
        skipSpace(s, i);
        if (i >= s.size()) return YamlNode{};
        const char c = s[i];
        if (c == '[') {
            ++i;
            YamlNode seq = YamlNode::sequence(line);
            skipSpace(s, i);
            if (i < s.size() && s[i] == ']') {
                ++i;
                return seq;
            }
            while (!failed()) {
                seq.push(parseFlow(s, i, line));
                skipSpace(s, i);
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    skipSpace(s, i);
                    if (i < s.size() && s[i] == ']') {
                        ++i;
                        return seq;
                    }
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return seq;
                }
                return fail(line, "expected ',' or ']' in a flow sequence");
            }
            return {};
        }
        if (c == '{') {
            ++i;
            YamlNode map = YamlNode::map(line);
            skipSpace(s, i);
            if (i < s.size() && s[i] == '}') {
                ++i;
                return map;
            }
            while (!failed()) {
                skipSpace(s, i);
                std::string key;
                if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                    if (!parseQuoted(s, i, key, line)) return {};
                } else {
                    key = flowPlain(s, i, true);
                }
                if (key.empty()) return fail(line, "empty key in a flow mapping");
                skipSpace(s, i);
                YamlNode value;
                if (i < s.size() && s[i] == ':') {
                    ++i;
                    skipSpace(s, i);
                    if (i < s.size() && s[i] != ',' && s[i] != '}') value = parseFlow(s, i, line);
                }
                if (failed()) return {};
                if (!map.insert(std::move(key), std::move(value))) return fail(line, "duplicate key");
                skipSpace(s, i);
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    skipSpace(s, i);
                    if (i < s.size() && s[i] == '}') {
                        ++i;
                        return map;
                    }
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return map;
                }
                return fail(line, "expected ',' or '}' in a flow mapping");
            }
            return {};
        }
        if (c == '"' || c == '\'') {
            std::string out;
            if (!parseQuoted(s, i, out, line)) return {};
            return YamlNode::scalar(std::move(out), true, line);
        }
        if (c == '&' || c == '*' || c == '!' || c == '|' || c == '>')
            return fail(line, std::string("unsupported YAML feature '") + c + "'");
        std::string text = flowPlain(s, i, false);
        if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") return YamlNode{};
        return YamlNode::scalar(std::move(text), false, line);
    }

    std::vector<Line> lines_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

Result<YamlNode> parseYaml(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    std::vector<Line> lines;
    int number = 0;
    bool first = true;
    while (!text.empty() || number == 0) {
        const size_t nl = text.find('\n');
        std::string_view raw = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        ++number;
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        int indent = 0;
        while (static_cast<size_t>(indent) < raw.size() && raw[static_cast<size_t>(indent)] == ' ') ++indent;
        const std::string body = stripComment(raw.substr(static_cast<size_t>(indent)));
        const std::string_view t = trim(body);
        if (t.empty()) {
            if (nl == std::string_view::npos) break;
            continue;
        }
        if (static_cast<size_t>(indent) < raw.size() && raw[static_cast<size_t>(indent)] == '\t')
            return makeError(ErrorCode::CorruptData, "line " + std::to_string(number) + ": tab in indentation");
        if (first && t == "---") {
            first = false;
            continue;
        }
        if (t == "---" || t == "...")
            return makeError(ErrorCode::CorruptData,
                             "line " + std::to_string(number) + ": multiple documents are not supported");
        first = false;
        lines.push_back(Line{number, indent, std::string(t)});
        if (nl == std::string_view::npos) break;
    }
    return Parser(std::move(lines)).run();
}

Result<YamlNode> loadYamlFile(const std::filesystem::path& file) {
    auto text = readTextFile(file, 1u << 20);  // descriptors are small
    if (!text) return text.error();
    auto r = parseYaml(*text);
    if (!r) return makeError(r.error().code, file.filename().string() + ": " + r.error().context);
    return r;
}

} // namespace pchess::util
