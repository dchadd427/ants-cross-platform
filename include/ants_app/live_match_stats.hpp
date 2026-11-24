#pragma once

// The stats pass of the replay pages (ants_replay/match_stats.hpp), run LIVE: while a match is played, on the CONFIRMED engine of the application, for every tick that it executes, so that the results screen has its numbers (match_report.hpp)
// the moment the match is over, with no file to read back. It only WATCHES: it reads the engine after a tick and changes nothing in it (the state hash, the replay's bytes, the cues and the news of the engine are the same with it as without it).
//
// Where the ticks come from (Application): a game on one machine ticks in update_simulation (on_tick); a network match ticks inside the lock-step runner, which tells the tap NetGame::set_on_executed about every turn it runs, the live
// ones, the catch-up's (LockstepRunner::fast_forward: many at once, with no hook between them) and a rejoin's (the match from its first turn) alike (on_turn). Both are the places that the replay recorder is fed from.
//
// A report is never made of a count that missed anything. The pass fails, and the report is not ok, when
//   * the engine was not at its first tick when the match began (a match that was not played from its first turn),
//   * a turn is not the one after the last (the tap missed one), or the engine's own tick count moved by more than the one tick that was seen (a tick ran that this pass did not see),
//   * the engine has run a tick since the last one that was seen when the match ends,
//   * the pass itself failed (match_stats.hpp: a world that could not be read) or could not tell the owner of a bomb or the place of a fire for sure.
// The orders of the seats (what the match pages count by minute and kind) are NOT counted live: the pass needs the engine as it stood before each command, which a lock-step runner's tap cannot give (it comes after the tick),
// and nothing on the results screen needs them. Everything else is counted as the recording's pass counts it: the same numbers, field by field (tests/test_app/test_match_report.cpp).

#include <cstdint>
#include <memory>

#include "ants_app/match_report.hpp"
#include "ants_net/protocol.hpp"
#include "ants_replay/match_stats.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

class LiveMatchStats {
public:
    LiveMatchStats() = default;
    LiveMatchStats(const LiveMatchStats&) = delete;
    LiveMatchStats& operator=(const LiveMatchStats&) = delete;

    /// A match begins: its engine is set up (init, teams made) and has run no tick. The report of the match before it is gone. An engine that has run ticks already makes this match one that was not played from its first turn.
    void start(const sim::SimulationEngine& engine);
    /// A tick of a game on one machine has run: the next turn
    void on_tick(sim::SimulationEngine& engine);
    /// A turn of the lock-step runner has run (its commands, then its tick); it must be the turn after the last one that was seen, from turn 0
    void on_turn(const net::TurnMsg& turn, sim::SimulationEngine& engine);
    /// The match is left or dropped before its end: what was counted is thrown away. The report of a match that ended stays (it is valid until the next match starts).
    void abandon();
    /// Nothing is counted and no report is kept (a viewer of a recording counts nothing, and has none of an earlier match to show)
    void reset();
    /// The match is over: its report (`local_seat` is the seat of this machine's player, whose results screen it is). Not ok when the pass missed anything. When no match is being counted (no start() since the last finish or abandon) it does nothing and gives a report that is not ok, never the report of an earlier match (report() still holds that one).
    const MatchReport& finish(sim::SimulationEngine& engine, uint8_t local_seat);

    /// True from start() until finish() or abandon()
    bool counting() const noexcept { return state_ == State::Counting; }
    /// The report of the match that ended last: ok = false until a match has ended and been counted whole; valid until the next match starts
    const MatchReport& report() const noexcept { return report_; }
    /// What the pass counted in that match (the report is made from it): for the tests
    const replay::MatchStats& stats() const noexcept { return stats_; }
    /// The ticks that the pass has seen in the match that is counted
    uint32_t turns_seen() const noexcept { return turns_; }

private:
    enum class State : uint8_t { Idle, Counting };
    void see(sim::SimulationEngine& engine);
    void fail(const char* why);

    State state_{State::Idle};
    std::unique_ptr<replay::MatchStatsCollector> collector_;
    uint32_t turns_{0};                  // the ticks seen
    uint64_t last_tick_{0};              // the engine's own tick count after the last tick that was seen
    MatchReport report_;
    const MatchReport none_{};           // what finish() gives when nothing was counted
    replay::MatchStats stats_;
};

}  // namespace ants::app
