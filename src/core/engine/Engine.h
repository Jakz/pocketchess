// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Move.h"
#include "core/chess/Position.h"
#include "core/engine/EngineDescriptor.h"
#include "core/engine/EngineTypes.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace pchess::engine {

enum class EngineState : uint8_t { Off, Starting, Idle, Searching, Stopping, Failed };
const char* toString(EngineState s);

struct SearchResult {
    bool ok = false;       // false: the engine failed (see error) or the search was cancelled
    bool stopped = false;  // ended by stop() (or superseded); `best` may still be set
    std::string error;     // failures only
    chess::Move best;      // none for "bestmove (none)" (mate/stalemate) and failures
    chess::Move ponder;
    std::vector<SearchInfo> lines;  // last info per multipv (index 0 = multipv 1)
    double wallMs = 0;              // go -> bestmove
};

// One running engine instance (architecture doc §8.1). All calls and callbacks happen on the
// UI thread. Jobs talk to engines only through this interface; tests use fakes.
class IEngine {
public:
    using InfoCallback = std::function<void(const SearchInfo&)>;
    using DoneCallback = std::function<void(const SearchResult&)>;

    virtual ~IEngine() = default;
    virtual const EngineDescriptor& descriptor() const = 0;
    // Parsed from the engine's `option` lines; empty until the first start completes.
    virtual const Capabilities& capabilities() const = 0;
    // Options to apply (validated against capabilities) before the next search. Replaces the
    // previous set; unchanged values are not re-sent.
    virtual void configure(const OptionSet& options) = 0;
    // Lower CPU priority for the process (0 = normal; nice level on the device).
    virtual void setPriority(int niceLevel) = 0;
    // Starts the process if needed, then searches `start` + `moves`. A search while another one
    // runs stops it first (its done callback gets stopped = true). The done callback always fires
    // exactly once, unless the engine object is destroyed first.
    virtual void search(const chess::Position& start, std::span<const chess::Move> moves, const SearchLimits& limits,
                        InfoCallback onInfo, DoneCallback onDone) = 0;
    virtual void stop() = 0;      // ends the current search; the done callback reports the best move so far
    virtual void newGame() = 0;   // sends ucinewgame before the next search
    virtual void shutdown() = 0;  // quits the process (state Off); the next search restarts it
    virtual EngineState state() const = 0;
    virtual bool processRunning() const = 0;
    virtual EngineStats stats() const = 0;
    virtual std::string lastError() const = 0;
};

} // namespace pchess::engine
