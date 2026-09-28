// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/GameTree.h"
#include "core/util/Error.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pchess::chess {

struct PgnGame {
    std::vector<std::pair<std::string, std::string>> headers;
    GameTree tree;
    std::string result = "*";

    std::string header(std::string_view name) const;
    void setHeader(std::string_view name, std::string value);
};

// Streaming PGN reader: headers, movetext, {comments}, ;comments, NAGs ($n and !?-style),
// nested variations, results, %-escape lines. A malformed game yields an error with its line
// number; the reader then skips to the next game, so an import can continue (design notes §32).
class PgnReader {
public:
    explicit PgnReader(std::string_view text) : text_(text) {}

    // nullopt at end of input.
    std::optional<Result<PgnGame>> next();
    int line() const { return line_; }

private:
    enum class Tok { End, HeaderOpen, LParen, RParen, Comment, Nag, MoveNumber, Result, Symbol };
    struct Token {
        Tok type = Tok::End;
        std::string_view text;
        int line = 0;
    };

    Token peek();
    Token take();
    Token lex();
    bool parseHeader(PgnGame& game, std::string& err);
    void skipToNextGame();

    std::string_view text_;
    size_t pos_ = 0;
    int line_ = 1;
    std::optional<Token> peeked_;
};

Result<PgnGame> parsePgnGame(std::string_view text);
std::vector<Result<PgnGame>> parsePgn(std::string_view text);

// Export with the seven-tag roster first, 80-column wrapping, comments, NAGs and variations.
std::string writePgn(const PgnGame& game);
// Movetext only (no headers, no result), e.g. for the move list.
std::string writeMovetext(const GameTree& tree, bool withComments = true);

// Maps "!", "?", "!!", "??", "!?", "?!" to NAG 1..6 (0 if unknown).
int nagFromSuffix(std::string_view suffix);
const char* nagSymbol(int nag);  // "!" for 1 etc., nullptr if it has no symbol

} // namespace pchess::chess
