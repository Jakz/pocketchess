// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Pgn.h"

#include "core/chess/Fen.h"
#include "core/chess/San.h"
#include "core/util/Utf8.h"

#include <charconv>

namespace pchess::chess {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSymbolChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80) return false;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_' || c == '+' || c == '#' ||
           c == '=' || c == ':' || c == '-' || c == '/' || c == '!' || c == '?' || c == '.';
}
bool isResult(std::string_view s) { return s == "1-0" || s == "0-1" || s == "1/2-1/2" || s == "*"; }

std::string collapseWhitespace(std::string_view s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (isSpace(c)) {
            space = !out.empty();
        } else {
            if (space) out += ' ';
            out += c;
            space = false;
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// PgnGame

std::string PgnGame::header(std::string_view name) const {
    for (const auto& [k, v] : headers)
        if (k == name) return v;
    return {};
}

void PgnGame::setHeader(std::string_view name, std::string value) {
    for (auto& [k, v] : headers)
        if (k == name) {
            v = std::move(value);
            return;
        }
    headers.emplace_back(std::string(name), std::move(value));
}

int nagFromSuffix(std::string_view s) {
    if (s == "!") return 1;
    if (s == "?") return 2;
    if (s == "!!") return 3;
    if (s == "??") return 4;
    if (s == "!?") return 5;
    if (s == "?!") return 6;
    return 0;
}

const char* nagSymbol(int nag) {
    switch (nag) {
    case 1: return "!";
    case 2: return "?";
    case 3: return "!!";
    case 4: return "??";
    case 5: return "!?";
    case 6: return "?!";
    default: return nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// Lexer

PgnReader::Token PgnReader::peek() {
    if (!peeked_) peeked_ = lex();
    return *peeked_;
}

PgnReader::Token PgnReader::take() {
    Token t = peek();
    peeked_.reset();
    return t;
}

PgnReader::Token PgnReader::lex() {
    const size_t n = text_.size();
    for (;;) {
        while (pos_ < n && isSpace(text_[pos_])) {
            if (text_[pos_] == '\n') ++line_;
            ++pos_;
        }
        if (pos_ >= n) return {Tok::End, {}, line_};
        // %-escape: the whole line is ignored when '%' is the first character.
        if (text_[pos_] == '%' && (pos_ == 0 || text_[pos_ - 1] == '\n')) {
            while (pos_ < n && text_[pos_] != '\n') ++pos_;
            continue;
        }
        break;
    }

    const size_t start = pos_;
    const int line = line_;
    const char c = text_[pos_];
    switch (c) {
    case '[': ++pos_; return {Tok::HeaderOpen, text_.substr(start, 1), line};
    case '(': ++pos_; return {Tok::LParen, text_.substr(start, 1), line};
    case ')': ++pos_; return {Tok::RParen, text_.substr(start, 1), line};
    case '*': ++pos_; return {Tok::Result, text_.substr(start, 1), line};
    case '{': {
        ++pos_;
        const size_t b = pos_;
        while (pos_ < n && text_[pos_] != '}') {
            if (text_[pos_] == '\n') ++line_;
            ++pos_;
        }
        const std::string_view body = text_.substr(b, pos_ - b);
        if (pos_ < n) ++pos_;  // unterminated comment: take the rest of the input
        return {Tok::Comment, body, line};
    }
    case ';': {
        ++pos_;
        const size_t b = pos_;
        while (pos_ < n && text_[pos_] != '\n') ++pos_;
        return {Tok::Comment, text_.substr(b, pos_ - b), line};
    }
    case '$': {
        ++pos_;
        const size_t b = pos_;
        while (pos_ < n && isDigit(text_[pos_])) ++pos_;
        return {Tok::Nag, text_.substr(b, pos_ - b), line};
    }
    default: break;
    }

    if (isDigit(c)) {
        size_t p = pos_;
        while (p < n && isDigit(text_[p])) ++p;
        if (p < n && text_[p] == '.') {
            while (p < n && text_[p] == '.') ++p;
            pos_ = p;
            return {Tok::MoveNumber, text_.substr(start, p - start), line};
        }
    }
    if (c == '.') {
        while (pos_ < n && text_[pos_] == '.') ++pos_;
        return {Tok::MoveNumber, text_.substr(start, pos_ - start), line};
    }
    if (isSymbolChar(c)) {
        while (pos_ < n && isSymbolChar(text_[pos_])) ++pos_;
        const std::string_view sym = text_.substr(start, pos_ - start);
        if (isResult(sym)) return {Tok::Result, sym, line};
        if (sym.find_first_not_of("!?") == std::string_view::npos) return {Tok::Nag, sym, line};
        return {Tok::Symbol, sym, line};
    }
    // Unknown byte (e.g. stray UTF-8 outside comments): return it as a one-char symbol so the
    // parser reports a useful error.
    size_t p = pos_;
    util::decodeUtf8(text_, p);
    pos_ = p;
    return {Tok::Symbol, text_.substr(start, p - start), line};
}

bool PgnReader::parseHeader(PgnGame& game, std::string& err) {
    // '[' already consumed.
    const size_t n = text_.size();
    auto skipWs = [&] {
        while (pos_ < n && isSpace(text_[pos_]) && text_[pos_] != '\n') ++pos_;
    };
    skipWs();
    const size_t b = pos_;
    while (pos_ < n && !isSpace(text_[pos_]) && text_[pos_] != '"' && text_[pos_] != ']') ++pos_;
    std::string name(text_.substr(b, pos_ - b));
    skipWs();
    if (name.empty() || pos_ >= n || text_[pos_] != '"') {
        err = "malformed header";
        return false;
    }
    ++pos_;
    std::string value;
    while (pos_ < n && text_[pos_] != '"') {
        if (text_[pos_] == '\\' && pos_ + 1 < n) ++pos_;
        if (text_[pos_] == '\n') {
            err = "unterminated header value";
            return false;
        }
        value += text_[pos_++];
    }
    if (pos_ >= n) {
        err = "unterminated header value";
        return false;
    }
    ++pos_;  // closing quote
    skipWs();
    if (pos_ >= n || text_[pos_] != ']') {
        err = "missing ']' after header";
        return false;
    }
    ++pos_;
    game.headers.emplace_back(std::move(name), std::move(value));
    return true;
}

void PgnReader::skipToNextGame() {
    for (;;) {
        Token t = peek();
        if (t.type == Tok::End || t.type == Tok::HeaderOpen) return;
        take();
        if (t.type == Tok::Result) return;
    }
}

std::optional<Result<PgnGame>> PgnReader::next() {
    PgnGame game;
    bool any = false;
    std::string gameComment;

    auto fail = [&](int line, const std::string& what) -> std::optional<Result<PgnGame>> {
        std::string ctx = "line " + std::to_string(line) + ": " + what;
        const std::string white = game.header("White"), black = game.header("Black");
        if (!white.empty() || !black.empty()) ctx += " (" + white + " - " + black + ")";
        skipToNextGame();
        return Result<PgnGame>(makeError(ErrorCode::ParsePgn, ctx));
    };

    // Headers (and stray comments before them).
    for (;;) {
        Token t = peek();
        if (t.type == Tok::HeaderOpen) {
            take();
            std::string err;
            if (!parseHeader(game, err)) return fail(t.line, err);
            any = true;
        } else if (t.type == Tok::Comment && game.headers.empty()) {
            take();
        } else {
            break;
        }
    }

    // Start position.
    const std::string fen = game.header("FEN");
    if (!fen.empty()) {
        auto pos = parseFen(fen);
        if (!pos) return fail(line_, pos.error().context);
        game.tree = GameTree(pos.value());
    }

    struct Frame {
        GameNode* node;
        Position pos;
    };
    std::vector<Frame> stack;
    GameNode* cur = game.tree.root();
    Position pos = game.tree.startPosition();
    std::string pendingPre;
    bool atLineStart = true;  // no move yet in the current (sub)line

    for (;;) {
        Token t = peek();
        switch (t.type) {
        case Tok::End:
        case Tok::HeaderOpen:
            if (!any && t.type == Tok::End) return std::nullopt;
            if (!stack.empty()) return fail(t.line, "unterminated variation");
            if (game.result == "*") {
                const std::string hr = game.header("Result");
                if (isResult(hr)) game.result = hr;
            }
            game.tree.root()->comment = gameComment;
            return Result<PgnGame>(std::move(game));
        case Tok::Result:
            take();
            if (!stack.empty()) return fail(t.line, "result inside a variation");
            game.result = std::string(t.text);
            game.tree.root()->comment = gameComment;
            return Result<PgnGame>(std::move(game));
        case Tok::LParen:
            take();
            any = true;
            if (cur->isRoot()) return fail(t.line, "variation before any move");
            stack.push_back({cur, pos});
            cur = cur->parent;
            pos = game.tree.positionAt(cur);
            atLineStart = true;
            break;
        case Tok::RParen:
            take();
            if (stack.empty()) return fail(t.line, "unbalanced ')'");
            cur = stack.back().node;
            pos = stack.back().pos;
            stack.pop_back();
            atLineStart = false;
            break;
        case Tok::Comment: {
            take();
            any = true;
            std::string text = collapseWhitespace(t.text);
            if (text.empty()) break;
            std::string& target = atLineStart ? (cur->isRoot() && stack.empty() ? gameComment : pendingPre) : cur->comment;
            if (!target.empty()) target += ' ';
            target += text;
            break;
        }
        case Tok::Nag: {
            take();
            int nag = 0;
            if (!t.text.empty() && isDigit(t.text[0])) std::from_chars(t.text.data(), t.text.data() + t.text.size(), nag);
            else nag = nagFromSuffix(t.text);
            if (nag > 0 && nag < 256 && !cur->isRoot()) cur->nags.push_back(static_cast<uint8_t>(nag));
            break;
        }
        case Tok::MoveNumber:
            take();
            any = true;
            break;
        case Tok::Symbol: {
            take();
            any = true;
            std::string_view sym = t.text;
            // Split a trailing "!?"-style suffix into a NAG.
            size_t cut = sym.size();
            while (cut > 0 && (sym[cut - 1] == '!' || sym[cut - 1] == '?')) --cut;
            const int nag = nagFromSuffix(sym.substr(cut));
            const std::string_view san = sym.substr(0, cut);
            if (san == "--" || san == "Z0") return fail(t.line, "null moves are not supported");
            auto move = parseSan(pos, san);
            if (!move) return fail(t.line, move.error().context);
            GameNode* child = game.tree.addMove(cur, move.value());
            if (!pendingPre.empty()) {
                child->preComment = std::move(pendingPre);
                pendingPre.clear();
            }
            if (nag) child->nags.push_back(static_cast<uint8_t>(nag));
            cur = child;
            pos.makeMove(move.value());
            atLineStart = false;
            break;
        }
        }
    }
}

Result<PgnGame> parsePgnGame(std::string_view text) {
    PgnReader reader(text);
    auto g = reader.next();
    if (!g) return makeError(ErrorCode::ParsePgn, "no game found");
    return std::move(*g);
}

std::vector<Result<PgnGame>> parsePgn(std::string_view text) {
    std::vector<Result<PgnGame>> games;
    PgnReader reader(text);
    while (auto g = reader.next()) games.push_back(std::move(*g));
    return games;
}

// ---------------------------------------------------------------------------------------------
// Writer

namespace {

class TokenWriter {
public:
    void word(std::string_view w) { words_.emplace_back(w); }
    void comment(const std::string& text) {
        // Split so long comments wrap; braces stay attached to the first/last word.
        std::vector<std::string> parts;
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && text[i] == ' ') ++i;
            size_t j = i;
            while (j < text.size() && text[j] != ' ') ++j;
            if (j > i) parts.emplace_back(text.substr(i, j - i));
            i = j;
        }
        if (parts.empty()) return;
        parts.front() = "{" + parts.front();
        parts.back() += "}";
        for (auto& p : parts) words_.push_back(std::move(p));
    }
    std::string wrap(size_t width) const {
        std::string out, line;
        for (const auto& w : words_) {
            const bool glue = w == ")" || (!line.empty() && line.back() == '(');
            if (!line.empty() && !glue && line.size() + 1 + w.size() > width) {
                out += line;
                out += '\n';
                line.clear();
            }
            if (!line.empty() && !glue) line += ' ';
            line += w;
        }
        if (!line.empty()) out += line;
        return out;
    }

private:
    std::vector<std::string> words_;
};

void writeMove(TokenWriter& w, const GameNode* node, const Position& before, bool forceNumber, bool withComments) {
    if (withComments && !node->preComment.empty()) {
        w.comment(node->preComment);
        forceNumber = true;
    }
    const int number = before.fullmoveNumber();
    if (before.sideToMove() == White) w.word(std::to_string(number) + ".");
    else if (forceNumber) w.word(std::to_string(number) + "...");
    w.word(toSan(before, node->move));
    for (uint8_t nag : node->nags) w.word("$" + std::to_string(nag));
    if (withComments && !node->comment.empty()) w.comment(node->comment);
}

void writeLine(TokenWriter& w, const GameNode* parent, Position pos, bool forceNumber, bool withComments) {
    const GameNode* node = parent;
    while (!node->children.empty()) {
        const GameNode* main = node->children.front().get();
        writeMove(w, main, pos, forceNumber, withComments);
        forceNumber = withComments && !main->comment.empty();
        for (size_t i = 1; i < node->children.size(); ++i) {
            const GameNode* var = node->children[i].get();
            w.word("(");
            writeMove(w, var, pos, true, withComments);
            writeLine(w, var, pos.after(var->move), withComments && !var->comment.empty(), withComments);
            w.word(")");
            forceNumber = true;
        }
        pos.makeMove(main->move);
        node = main;
    }
}

std::string escapeHeader(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

} // namespace

std::string writeMovetext(const GameTree& tree, bool withComments) {
    TokenWriter w;
    if (withComments && !tree.root()->comment.empty()) w.comment(tree.root()->comment);
    writeLine(w, tree.root(), tree.startPosition(), true, withComments);
    return w.wrap(79);
}

std::string writePgn(const PgnGame& game) {
    static const char* roster[] = {"Event", "Site", "Date", "Round", "White", "Black", "Result"};
    std::string out;
    for (const char* tag : roster) {
        std::string v = tag == std::string_view("Result") ? game.result : game.header(tag);
        if (v.empty()) v = tag == std::string_view("Date") ? "????.??.??" : "?";
        out += "[" + std::string(tag) + " \"" + escapeHeader(v) + "\"]\n";
    }
    for (const auto& [k, v] : game.headers) {
        bool inRoster = false;
        for (const char* tag : roster) inRoster |= (k == tag);
        if (!inRoster) out += "[" + k + " \"" + escapeHeader(v) + "\"]\n";
    }
    out += '\n';
    TokenWriter w;
    if (!game.tree.root()->comment.empty()) w.comment(game.tree.root()->comment);
    writeLine(w, game.tree.root(), game.tree.startPosition(), true, true);
    w.word(game.result);
    out += w.wrap(79);
    out += "\n\n";
    return out;
}

} // namespace pchess::chess
