// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/games/OpeningDrill.h"

#include <algorithm>

namespace pchess::games {

using namespace chess;

OpeningDrill::OpeningDrill(Position start, Color user, BookFn book, util::Random& random)
    : pos_(std::move(start)), user_(user), book_(std::move(book)), random_(random) {
    lookUp();
}

void OpeningDrill::lookUp() {
    candidates_.clear();
    games_ = 0;
    std::vector<BookMove> all = book_ ? book_(pos_) : std::vector<BookMove>{};
    for (const BookMove& b : all) games_ += b.games;
    if (games_ < kMinGames) return;
    for (const BookMove& b : all)
        if (b.games * 100 >= kMinSharePct * games_ && b.games >= 2) candidates_.push_back(b);
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [](const BookMove& a, const BookMove& b) { return a.games > b.games; });
}

void OpeningDrill::play(Move m) {
    pos_.makeMove(m);
    played_.push_back(m);
    misses_ = 0;
    hinted_ = false;
    lookUp();
}

bool OpeningDrill::opponentMove() {
    if (finished() || userToMove()) return false;
    int total = 0;
    for (const BookMove& b : candidates_) total += b.games;
    int pick = static_cast<int>(random_.below(static_cast<uint32_t>(total)));
    for (const BookMove& b : candidates_) {
        if (pick < b.games) {
            play(b.move);
            return true;
        }
        pick -= b.games;
    }
    play(candidates_.front().move);
    return true;
}

OpeningDrill::Verdict OpeningDrill::guess(Move m) {
    if (!userToMove()) return Verdict::NotYourTurn;
    for (size_t i = 0; i < candidates_.size(); ++i) {
        if (candidates_[i].move != m) continue;
        lastShare_ = (candidates_[i].games * 100 + games_ / 2) / games_;
        lastOutcome_ = i == 0 ? Outcome::Main : Outcome::Book;
        ++(i == 0 ? score_.main : score_.book);
        play(m);
        return i == 0 ? Verdict::Main : Verdict::Book;
    }
    if (++misses_ < kTries) return Verdict::Wrong;
    reveal();
    return Verdict::Shown;
}

Square OpeningDrill::hint() {
    if (!userToMove()) return NoSquare;
    hinted_ = true;
    return candidates_.front().move.from();
}

void OpeningDrill::reveal() {
    if (!userToMove()) return;
    lastShare_ = (candidates_.front().games * 100 + games_ / 2) / games_;
    lastOutcome_ = Outcome::Missed;
    ++score_.missed;
    play(candidates_.front().move);
}

} // namespace pchess::games
