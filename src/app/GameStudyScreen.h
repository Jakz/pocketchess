// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/MoveList.h"
#include "app/Screen.h"
#include "core/chess/Pgn.h"
#include "core/games/GamePack.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <memory>
#include <string>
#include <vector>

namespace pchess::app {

// Study one historical game (M5, design notes §13.4–13.5, architecture doc §11): the board, the
// current move's comment under it, and the move list (tap a move to jump). Prev/Next (also the
// hardware keys) step through the current line, "Main" returns from a variation, variations are
// entered from the menu, ★ keeps the game in the favorites, "Analyze from here" opens Analyze.
class GameStudyScreen final : public Screen {
public:
    // `path`: UCI moves from the start to show (session resume); empty = the start position.
    GameStudyScreen(Application& app, int64_t gameId, const std::vector<std::string>& path = {});
    const char* name() const override { return "study"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    bool saveSession(std::map<std::string, std::string>& out) const override;
    static std::unique_ptr<GameStudyScreen> restore(Application& app, const std::map<std::string, std::string>& session);

    // For tests and scripted sessions.
    bool loaded() const { return loaded_; }
    const chess::GameTree& tree() const { return game_.tree; }
    const chess::GameNode* node() const { return node_; }
    const chess::Position& position() const { return pos_; }
    void next();
    void prev();
    void mainLine();
    void jumpTo(const chess::GameNode* n);
    void enterVariation(int index);  // index into the current node's children (1 = first variation)
    void toggleFavorite();
    bool favorite() const { return favorite_; }
    const std::vector<MoveHit>& moveHits() const { return hits_; }

private:
    enum ButtonId { BtnPrev, BtnNext, BtnMain, BtnStar, BtnMenu };

    void layoutPanel();
    void setNode(const chess::GameNode* n);
    void refreshBoard();
    void updateButtons();
    void openMenu();
    void chooseVariation();
    void showInfo();
    void analyzeHere();
    std::string headline() const;

    int64_t gameId_ = 0;
    std::string pack_;  // pack name, for favorites and sessions
    chess::PgnGame game_;
    games::GameRow row_;
    bool loaded_ = false;
    const chess::GameNode* node_ = nullptr;
    chess::Position pos_;
    bool flipped_ = false;
    bool favorite_ = false;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect headRect_, commentRect_, movesRect_, buttonsRect_;
    std::vector<MoveHit> hits_;
    std::string error_;
};

} // namespace pchess::app
