// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/UciParser.h"

#include <charconv>
#include <cstdio>

namespace pchess::engine {
namespace {

std::vector<std::string_view> tokenize(std::string_view s) {
    std::vector<std::string_view> t;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        size_t j = i;
        while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
        if (j > i) t.push_back(s.substr(i, j - i));
        i = j;
    }
    return t;
}

template <class T>
std::optional<T> num(std::string_view s) {
    T v{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return v;
}

std::string join(const std::vector<std::string_view>& t, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to && i < t.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += t[i];
    }
    return out;
}

void parseOption(const std::vector<std::string_view>& t, UciOption& o) {
    // option name <words...> type <t> default <words...> min <n> max <n> var <words...> ...
    auto isKey = [](std::string_view w) { return w == "type" || w == "default" || w == "min" || w == "max" || w == "var"; };
    size_t i = 1;
    while (i < t.size()) {
        const std::string_view key = t[i];
        size_t j = i + 1;
        if (key == "name") {
            while (j < t.size() && t[j] != "type") ++j;
            o.name = join(t, i + 1, j);
        } else if (isKey(key)) {
            while (j < t.size() && !isKey(t[j])) ++j;
            const std::string value = join(t, i + 1, j);
            if (key == "type") o.type = value;
            else if (key == "default") o.defaultValue = value;
            else if (key == "min") o.min = num<int>(value);
            else if (key == "max") o.max = num<int>(value);
            else o.vars.push_back(value);
        }
        i = j;
    }
}

void parseInfo(const std::vector<std::string_view>& t, UciInfo& info) {
    for (size_t i = 1; i < t.size(); ++i) {
        const std::string_view k = t[i];
        auto next = [&]() -> std::string_view { return i + 1 < t.size() ? t[++i] : std::string_view{}; };
        if (k == "depth") info.depth = num<int>(next());
        else if (k == "seldepth") info.seldepth = num<int>(next());
        else if (k == "multipv") info.multipv = num<int>(next());
        else if (k == "hashfull") info.hashfull = num<int>(next());
        else if (k == "nodes") info.nodes = num<uint64_t>(next());
        else if (k == "nps") info.nps = num<uint64_t>(next());
        else if (k == "time") info.timeMs = num<uint64_t>(next());
        else if (k == "tbhits") info.tbhits = num<uint64_t>(next());
        else if (k == "score") {
            Score s;
            const std::string_view kind = next();
            const auto v = num<int>(next());
            if ((kind == "cp" || kind == "mate") && v) {
                s.kind = kind == "cp" ? Score::Kind::Cp : Score::Kind::Mate;
                s.value = *v;
                while (i + 1 < t.size() && (t[i + 1] == "lowerbound" || t[i + 1] == "upperbound")) {
                    if (t[++i] == "lowerbound") s.lowerbound = true;
                    else s.upperbound = true;
                }
                info.score = s;
            }
        } else if (k == "pv") {
            // The PV runs until the next known keyword (engines put it last, but be tolerant).
            while (i + 1 < t.size()) {
                const std::string_view w = t[i + 1];
                if (w == "depth" || w == "seldepth" || w == "multipv" || w == "score" || w == "nodes" ||
                    w == "nps" || w == "time" || w == "hashfull" || w == "tbhits" || w == "string" ||
                    w == "currmove" || w == "currmovenumber" || w == "wdl")
                    break;
                info.pv.emplace_back(w);
                ++i;
            }
        } else if (k == "string") {
            info.string = join(t, i + 1, t.size());
            break;
        } else if (k == "wdl") {
            i += 3;
        } else if (k == "currmove" || k == "currmovenumber" || k == "cpuload" || k == "sbhits" || k == "refutation" || k == "currline") {
            next();
        }
    }
}

} // namespace

UciMessage parseUciLine(std::string_view line) {
    UciMessage m;
    const auto t = tokenize(line);
    if (t.empty()) return m;
    const std::string_view cmd = t[0];
    if (cmd == "uciok") m.type = UciMessage::Type::UciOk;
    else if (cmd == "readyok") m.type = UciMessage::Type::ReadyOk;
    else if (cmd == "id" && t.size() >= 2) {
        if (t[1] == "name") m.type = UciMessage::Type::IdName;
        else if (t[1] == "author") m.type = UciMessage::Type::IdAuthor;
        m.text = join(t, 2, t.size());
    } else if (cmd == "option") {
        m.type = UciMessage::Type::Option;
        parseOption(t, m.option);
    } else if (cmd == "info") {
        m.type = UciMessage::Type::Info;
        parseInfo(t, m.info);
    } else if (cmd == "bestmove" && t.size() >= 2) {
        m.type = UciMessage::Type::BestMove;
        m.text = std::string(t[1]);
        if (t.size() >= 4 && t[2] == "ponder") m.ponder = std::string(t[3]);
    } else {
        m.text = std::string(line);
    }
    return m;
}

std::string formatScore(const Score& s, bool whiteToMove, bool fromWhitePov) {
    int v = s.value;
    if (fromWhitePov && !whiteToMove) v = -v;
    char buf[32];
    if (s.kind == Score::Kind::Mate) {
        std::snprintf(buf, sizeof buf, "%s#%d", v < 0 ? "-" : "", v < 0 ? -v : v);
    } else {
        const int a = v < 0 ? -v : v;
        std::snprintf(buf, sizeof buf, "%s%d.%02d", v < 0 ? "-" : "+", a / 100, a % 100);
    }
    return buf;
}

} // namespace pchess::engine
