// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/chess/GameTree.h"
#include "core/chess/Move.h"
#include "core/chess/Position.h"
#include "core/engine/EngineManager.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <memory>
#include <string>
#include <vector>

namespace pchess::app {

struct PlaySetup {
    chess::Color human = chess::White;
    std::string strength = "casual";  // strength label (engine descriptor `strength:`)
    std::string fen;                  // start position; empty = the standard one
};

// Play vs the engine (M3, design notes §6.2): the human moves by tap/drag, the engine replies
// through BestMove jobs at the chosen strength. Undo takes back the last move pair, Draw claims
// threefold/50 moves, Resign asks first. At the end a result dialog offers Analyze, Export PGN
// and New game; the game is saved in user/chess.db `played_games`.
class PlayScreen final : public Screen {
public:
    PlayScreen(Application& app, PlaySetup setup);
    ~PlayScreen() override;
    const char* name() const override { return "play"; }
    std::string title() const override { return "Play"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onEnter() override;
    void onLeave() override;
    void onLifecycle(platform::Lifecycle l) override;
    bool saveSession(std::map<std::string, std::string>& out) const override;
    // From a saved session (ADR 0011); nullptr if it is invalid.
    static std::unique_ptr<PlayScreen> restore(Application& app, const std::map<std::string, std::string>& session);

    // For tests and scripted sessions.
    const chess::Position& position() const { return pos_; }
    const chess::GameTree& tree() const { return tree_; }
    const PlaySetup& setup() const { return setup_; }
    bool playSan(const std::string& san);  // the human's move (ignored when it is not their turn)
    bool engineThinking() const { return job_ != 0; }
    bool finished() const { return finished_; }
    const std::string& result() const { return result_; }
    const std::string& reason() const { return reason_; }
    int64_t savedId() const { return savedId_; }
    std::string pgn() const;
    void undo();
    void resign();  // immediately (the button asks first)
    bool claimDraw();
    const std::vector<ui::Button>& buttons() const { return buttons_; }

private:
    enum ButtonId { BtnUndo, BtnFlip, BtnDraw, BtnResign, BtnMenu };

    void layoutPanel();
    void setNode(chess::GameNode* node);
    void refreshBoard(ui::RefreshHint hint);
    void refreshPanel();
    void updateButtons();
    std::string statusText() const;
    bool humanToMove() const;
    bool canClaimDraw() const;

    void onSquare(chess::Square s);
    void select(chess::Square s);
    void tryMove(chess::Square from, chess::Square to);
    void commitMove(chess::Move m);
    void afterMove();

    void requestEngineMove();
    void cancelEngine();
    void onEngineDone(const engine::JobResult& r);
    void showEngineProblem(const std::string& title, const std::string& text);

    void finish(const std::string& result, const std::string& reason);
    void saveGame();
    void showResult();
    void exportPgn();
    void analyzeGame();
    void newGame();
    void openMenu();
    void leave();

    PlaySetup setup_;
    std::string engineName_;
    int64_t startedAt_ = 0;  // unix seconds

    chess::GameTree tree_;
    chess::GameNode* node_ = nullptr;  // always the end of the main line
    chess::Position pos_;
    chess::MoveList legal_;
    chess::Square selected_ = chess::NoSquare;
    bool flipped_ = false;

    engine::JobId job_ = 0;
    bool finished_ = false;
    bool engineBlocked_ = false;  // engine missing/failed: wait for Retry instead of asking again
    std::string result_ = "*", reason_;
    int64_t savedId_ = 0;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, movesRect_, buttonsRect_, backRect_;
    std::shared_ptr<char> alive_ = std::make_shared<char>('\0');
};

} // namespace pchess::app
