// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/EngineBenchmark.h"

#include "core/chess/Fen.h"
#include "core/engine/UciEngine.h"
#include "core/util/Clock.h"
#include "core/util/Log.h"

#include <algorithm>
#include <cstdio>

namespace pchess::engine {

const std::vector<BenchPosition>& benchmarkPositions() {
    // Opening, middlegames (quiet, tactical, both castling rights), endgames: from Stockfish's
    // own bench set, so node counts are familiar when comparing with `stockfish bench`.
    static const std::vector<BenchPosition> positions = {
        {"opening", "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10"},
        {"middlegame", "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19"},
        {"tactics", "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15"},
        {"knight-endgame", "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1"},
        {"pawn-endgame", "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1"},
    };
    return positions;
}

namespace {

std::string fmt(double v, int decimals) {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// UTC "YYYY-MM-DDTHH:MM:SSZ".
std::string isoUtc(int64_t t) {
    const util::CivilTime c = util::civilFromUnix(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", c.year, c.month, c.day, c.hour, c.minute, c.second);
    return b;
}

} // namespace

std::string toYaml(const BenchReport& r) {
    std::string y = "# SPDX-License-Identifier: GPL-3.0-or-later\n"
                    "# PocketChess engine benchmark (M2): Diagnostics -> Bench. One run per file.\n";
    y += "date: " + isoUtc(r.unixSeconds) + "\n";
    y += "platform: " + quoted(r.platform) + "\n";
    y += "profile: " + quoted(r.profile) + "\n";
    y += "positions: " + std::to_string(r.positions) + "\n";
    y += std::string("cancelled: ") + (r.cancelled ? "true" : "false") + "\n";
    y += "engines:\n";
    for (const BenchEngine& e : r.engines) {
        y += "  - id: " + quoted(e.id) + "\n";
        y += "    name: " + quoted(e.name) + "\n";
        if (!e.error.empty()) y += "    error: " + quoted(e.error) + "\n";
        y += "    startup_ms: " + std::to_string(e.startupMs) + "\n";
        y += "    peak_rss_mb: " + std::to_string(e.peakRssKb / 1024) + "\n";
        y += "    rows:\n";
        for (const BenchRow& row : e.rows) {
            const int ok = row.searches - row.failures;
            y += "      - { strength: " + quoted(row.strength) + ", searches: " + std::to_string(row.searches) +
                 ", failures: " + std::to_string(row.failures) + ", wall_ms: " + fmt(row.wallMs, 0) +
                 ", cpu_s: " + fmt(row.cpuSeconds, 2) +
                 ", cpu_s_per_move: " + fmt(ok > 0 ? row.cpuSeconds / ok : 0.0, 3) +
                 ", nodes: " + std::to_string(row.nodes) + ", knps: " + fmt(row.knps(), 0) +
                 ", avg_depth: " + fmt(row.avgDepth(), 1) + " }\n";
        }
    }
    return y;
}

EngineBenchmark::EngineBenchmark(util::Scheduler& scheduler, std::vector<EngineDescriptor> engines,
                                 PowerProfileId profile, EngineFactory factory)
    : scheduler_(scheduler),
      descriptors_(std::move(engines)),
      profile_(powerProfile(profile)),
      factory_(std::move(factory)),
      token_(std::make_shared<char>('\0')) {
    if (!factory_)
        factory_ = [](const EngineDescriptor& d, util::Scheduler& s) -> std::unique_ptr<IEngine> {
            return std::make_unique<UciEngine>(d, s);
        };
}

EngineBenchmark::~EngineBenchmark() {
    token_.reset();
    if (engine_) engine_->shutdown();
    engine_.reset();
    retired_.clear();
}

int EngineBenchmark::totalSearches() const {
    int rows = 0;
    for (const EngineDescriptor& d : descriptors_) rows += static_cast<int>(d.strength.size()) + 1;
    return rows * static_cast<int>(benchmarkPositions().size());
}

void EngineBenchmark::start(int64_t unixSeconds, const std::string& platform, ProgressCallback onProgress,
                            DoneCallback onDone) {
    if (running_) return;
    report_ = {};
    report_.unixSeconds = unixSeconds;
    report_.platform = platform;
    report_.profile = profile_.name;
    report_.positions = static_cast<int>(benchmarkPositions().size());
    onProgress_ = std::move(onProgress);
    onDone_ = std::move(onDone);
    engineIndex_ = 0;
    done_ = 0;
    cancelled_ = false;
    running_ = true;
    util::Log::info("bench", "engine benchmark: " + std::to_string(descriptors_.size()) + " engine(s), " +
                                 std::to_string(totalSearches()) + " searches, profile " + profile_.name);
    std::weak_ptr<char> alive = token_;
    scheduler_.post([this, alive] {
        if (!alive.expired()) startEngine();
    });
}

void EngineBenchmark::cancel() {
    if (!running_ || cancelled_) return;
    cancelled_ = true;
    report_.cancelled = true;
    if (engine_ && engine_->state() == EngineState::Searching)
        engine_->stop();  // the result arrives and ends the run
    else
        finish();
}

void EngineBenchmark::startEngine() {
    if (cancelled_) return finish();
    if (engineIndex_ >= descriptors_.size()) return finish();
    const EngineDescriptor& d = descriptors_[engineIndex_];
    BenchEngine be;
    be.id = d.id;
    be.name = d.name;
    report_.engines.push_back(be);

    // Rows: every strength level (level limits, else the play budget), then Quick analysis.
    OptionSet base = d.defaults;
    setOption(base, "Threads", std::to_string(profile_.threads));
    setOption(base, "Hash", std::to_string(profile_.hashMb));
    rowOptions_.clear();
    rowLimits_.clear();
    for (const StrengthLevel& level : d.strength) {
        OptionSet o = base;
        for (const auto& [k, v] : level.options) setOption(o, k, v);
        rowOptions_.emplace_back(level.label, o);
        rowLimits_.push_back(level.limits.bounded() ? level.limits : profile_.play);
    }
    OptionSet analysis = base;
    setOption(analysis, "MultiPV", "1");
    rowOptions_.emplace_back("analysis", analysis);
    rowLimits_.push_back(profile_.quickAnalysis);
    for (const auto& [label, o] : rowOptions_) report_.engines.back().rows.push_back(BenchRow{label});

    engine_ = factory_(d, scheduler_);
    engine_->setPriority(profile_.nice);
    step_ = {0, 0};
    next();
}

void EngineBenchmark::next() {
    if (cancelled_) return finish();
    if (step_.row >= rowOptions_.size()) return finishEngine();
    const BenchPosition& bp = benchmarkPositions()[step_.position];
    auto pos = chess::parseFen(bp.fen);
    if (!pos) {  // cannot happen with the fixed set (tested); counts as a failed search
        SearchResult bad;
        bad.error = std::string("bad benchmark FEN ") + bp.name;
        onResult(bad, engine_->stats());
        return;
    }
    const auto& [label, options] = rowOptions_[step_.row];
    if (onProgress_)
        onProgress_(report_.engines.back().id + " " + label + " " + bp.name, done_, totalSearches());
    engine_->configure(options);
    engine_->newGame();
    const EngineStats before = engine_->stats();
    std::weak_ptr<char> alive = token_;
    engine_->search(*pos, {}, rowLimits_[step_.row], nullptr, [this, alive, before](const SearchResult& r) {
        if (alive.expired()) return;
        scheduler_.post([this, alive, r, before] {
            if (!alive.expired()) onResult(r, before);
        });
    });
}

void EngineBenchmark::onResult(const SearchResult& r, const EngineStats& before) {
    if (!running_ || !engine_) return;  // finished (cancelled) while the search was ending
    BenchEngine& be = report_.engines.back();
    BenchRow& row = be.rows[step_.row];
    const EngineStats after = engine_->stats();
    if (cancelled_) return finish();  // a cut-short search does not count
    ++row.searches;
    ++done_;
    if (!r.ok) {
        ++row.failures;
        if (be.error.empty()) be.error = r.error.empty() ? engine_->lastError() : r.error;
        util::Log::warn("bench", be.id + " " + row.strength + ": " + be.error);
        if (!engine_->processRunning() && engine_->state() == EngineState::Failed) {
            // The engine does not start: skip its remaining searches.
            done_ += static_cast<int>((rowOptions_.size() - step_.row) * benchmarkPositions().size()) -
                     static_cast<int>(step_.position) - 1;
            return finishEngine();
        }
    } else {
        row.wallMs += r.wallMs;
        row.cpuSeconds += std::max(0.0, after.cpuSeconds - before.cpuSeconds);
        uint64_t nodes = 0;
        for (const SearchInfo& l : r.lines) nodes = std::max(nodes, l.nodes);
        row.nodes += nodes;
        if (!r.lines.empty()) row.depthSum += r.lines[0].depth;
    }
    if (after.starts > 0 && be.startupMs == 0) be.startupMs = static_cast<int>(after.startup.count());
    be.peakRssKb = std::max(be.peakRssKb, after.peakRssKb);
    if (!engine_->capabilities().name.empty()) be.name = engine_->capabilities().name;

    if (++step_.position >= benchmarkPositions().size()) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s %s: %d searches, %.0f ms wall, %.2f CPU-s, %.0f knps, depth %.1f",
                      be.id.c_str(), row.strength.c_str(), row.searches, row.wallMs, row.cpuSeconds, row.knps(),
                      row.avgDepth());
        util::Log::info("bench", buf);
        step_.position = 0;
        ++step_.row;
    }
    next();
}

void EngineBenchmark::finishEngine() {
    if (engine_) {
        engine_->shutdown();
        retired_.push_back(std::move(engine_));
    }
    ++engineIndex_;
    std::weak_ptr<char> alive = token_;
    scheduler_.post([this, alive] {
        if (alive.expired()) return;
        retired_.clear();
        startEngine();
    });
}

void EngineBenchmark::finish() {
    if (!running_) return;
    running_ = false;
    if (engine_) {
        engine_->shutdown();
        retired_.push_back(std::move(engine_));
    }
    util::Log::info("bench", std::string("engine benchmark ") + (report_.cancelled ? "cancelled" : "done"));
    std::weak_ptr<char> alive = token_;
    scheduler_.post([this, alive, report = report_, onDone = std::move(onDone_)] {
        if (alive.expired()) return;
        retired_.clear();
        if (onDone) onDone(report);
    });
}

} // namespace pchess::engine
