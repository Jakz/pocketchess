// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/games/Openings.h"

#include "core/chess/San.h"

namespace pchess::games {

Result<int> OpeningBook::addTsv(std::string_view text) {
    int added = 0;
    size_t i = 0;
    bool header = true;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string_view::npos) j = text.size();
        std::string_view line = text.substr(i, j - i);
        i = j + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (header) {
            header = false;
            if (line.rfind("eco\t", 0) == 0) continue;
        }
        const size_t t1 = line.find('\t'), t2 = t1 == std::string_view::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string_view::npos) {
            ++rejected_;
            continue;
        }
        Opening o;
        o.eco = std::string(line.substr(0, t1));
        o.name = std::string(line.substr(t1 + 1, t2 - t1 - 1));
        OpeningLine entry{o.eco, o.name, {}};
        std::string_view pgn = line.substr(t2 + 1);
        chess::Position pos = chess::Position::startPosition();
        bool ok = true;
        size_t k = 0;
        while (k < pgn.size()) {
            size_t e = pgn.find(' ', k);
            if (e == std::string_view::npos) e = pgn.size();
            std::string_view tok = pgn.substr(k, e - k);
            k = e + 1;
            if (tok.empty() || (tok.back() == '.')) continue;  // "1." move numbers
            auto m = chess::parseSan(pos, tok);
            if (!m) {
                ok = false;
                break;
            }
            pos.makeMove(m.value());
            entry.moves.push_back(m.value());
            ++o.plies;
        }
        if (!ok || o.plies == 0) {
            ++rejected_;
            continue;
        }
        // The same position can be named by several lines: keep the most specific (longest) name.
        auto [it, inserted] = byKey_.try_emplace(pos.key(), o);
        if (!inserted && o.name.size() > it->second.name.size()) it->second = o;
        lines_.push_back(std::move(entry));
        ++added;
    }
    return added;
}

std::vector<std::string> OpeningBook::families() const {
    std::vector<std::string> out;
    std::unordered_map<std::string, bool> seen;
    for (const OpeningLine& l : lines_)
        if (seen.emplace(l.family(), true).second) out.push_back(l.family());
    return out;
}

std::optional<Opening> OpeningBook::classify(const chess::Position& start, std::span<const chess::Move> moves,
                                             int maxPlies) const {
    std::optional<Opening> best;
    chess::Position pos = start;
    int ply = 0;
    for (chess::Move m : moves) {
        if (++ply > maxPlies) break;
        pos.makeMove(m);
        if (auto it = byKey_.find(pos.key()); it != byKey_.end()) best = it->second;
    }
    return best;
}

} // namespace pchess::games
