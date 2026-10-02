#pragma once

// The client half of lock-step: a queue of sealed turns and the driver that executes them on the simulation. Turns arrive from the host at
// unsteady moments (network jitter); the runner buffers a few of them and executes one every 100 ms of real time, two ticks each, so the
// simulation runs at a steady 20 Hz on every machine and at exactly the same commands per tick. If the next turn has not arrived when it is due
// the runner stalls (the game shows "waiting"); when turns pile up it runs faster to catch up.
//
// The buffer is kept where it belongs. A turn is executed one turn (100 ms) after it arrived: the jitter buffer. A stall that lasts rebuild_after_ms (the buffer was
// exhausted and the next turn is still missing) makes the runner collect buffer_turns turns again before it goes on, as at the start: otherwise the time that it waited
// would stay owed (the accumulator), every later turn would run the moment it arrives with both of its ticks at once (10 steps a second instead of 20) and every bit of
// jitter would be a visible stall. And a queue that stays longer than buffer_turns (the turns of a stall that came in a bunch, a window that was not drawn for a second) is
// run down at double speed instead of being carried to the end of the match as extra delay (standing_window).

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

class LockstepRunner {
public:
    struct Config {
        uint32_t buffer_turns = 2;        // turns to collect before the first one is executed (the jitter buffer)
        uint32_t catch_up_turns = 3;      // more than this many queued turns beyond the buffer: run at double speed
        uint32_t max_ticks_per_update = 8;
        uint32_t rebuild_after_ms = 300;  // no tick for this long at a turn boundary with nothing queued: collect buffer_turns turns again before going on (0: never)
        uint32_t standing_window = 20;    // more than buffer_turns turns queued at each of the last 20 turns that were started (two seconds): a standing queue, run it down at
                                          // double speed (0: never; a bunch of turns that arrived together is gone after a few of them, a standing queue is not)
    };

    /// What one executed turn produced for the caller: the turn number and, on every kHashEveryTurns-th turn, the state hash after it.
    struct Executed {
        uint32_t turn{0};
        bool has_hash{false};
        sim::StateHash hash;
    };

    explicit LockstepRunner(sim::SimulationEngine& sim) : LockstepRunner(sim, Config{}) {}
    LockstepRunner(sim::SimulationEngine& sim, Config config) : sim_(sim), cfg_(config) {}

    /// A sealed turn from the host. Turns must arrive in order without gaps (the transport is reliable and ordered); anything else is refused.
    bool on_turn(TurnMsg turn);

    /// Advances real time by `dt_ms` and executes the ticks that are due (one every 50 ms; a turn's commands are applied before its first tick and
    /// its second tick follows 50 ms later). Returns the turns that were completed, in order.
    std::vector<Executed> update(uint32_t dt_ms);

    /// Called after every simulation tick (twice per turn), so that the application can present each tick (HUD, events, audio) as the local game does
    void set_on_tick(std::function<void()> fn) { on_tick_ = std::move(fn); }
    /// Called for every command a turn applies, with the engine's verdict (the application uses it to correct its predicted feedback)
    void set_on_command(std::function<void(const sim::Command&, const sim::CommandResult&)> fn) { on_command_ = std::move(fn); }
    /// The session's own observer, called for every command a turn applies just before on_command: the session measures the delay of the player's commands with it
    /// (latency.hpp). A separate hook, so that the application's on_command stays free; whoever takes the runner over (host migration) installs its own.
    void set_on_applied(std::function<void(const sim::Command&)> fn) { on_applied_ = std::move(fn); }

    /// The turns received last (up to kTurnLogTurns), kept so that a machine that becomes the host after the old one left can hand the turns that others
    /// are missing to them (docs/NETWORK_PORT.md, host migration). Null when the turn is older than the log or not received yet.
    const TurnMsg* logged_turn(uint32_t turn) const noexcept;
    static constexpr size_t kTurnLogTurns = 300;               // 30 s of play

    uint32_t next_turn_to_execute() const noexcept { return next_execute_; }
    uint32_t next_turn_expected() const noexcept { return next_receive_; }
    size_t queued() const noexcept { return queue_.size(); }
    /// The runner stands at a turn boundary with no turn to run, and has not run a tick for want of one: the next turn is due but has not arrived
    bool stalled() const noexcept { return stalled_ms() > 0; }
    /// How long the runner has stood still for want of a turn: the milliseconds since it last ran a tick, while it is at a turn boundary with nothing queued; 0 while it
    /// runs (every tick starts the count again, so a turn that arrives ends the stall at once), and before it has started. The "Waiting for the other players..." message
    /// shows at one second of it. It is measured from the last tick, never from the accumulator of unspent time: that holds up to 400 ms after a stall, so that "due" would
    /// stay true for ever while turns arrive on time (docs/NETWORK_PORT.md).
    uint32_t stalled_ms() const noexcept { return started_ && phase_ == 0 && queue_.empty() ? idle_ms_ : 0u; }
    /// Milliseconds into the current tick (0 .. 49), for smooth drawing between ticks
    uint32_t sub_tick_ms() const noexcept { return started_ ? std::min<uint32_t>(acc_ms_, kTickMs - 1) : 0u; }
    static constexpr uint32_t kTickMs = 50;

private:
    bool behind() const noexcept;

    sim::SimulationEngine& sim_;
    Config cfg_;
    std::deque<TurnMsg> queue_;
    std::deque<TurnMsg> log_;      // the last kTurnLogTurns turns received, consecutive
    TurnMsg current_;              // the turn whose second tick is still to come (phase 1)
    uint32_t next_receive_{0};     // the turn number the queue expects next
    uint32_t next_execute_{0};
    bool started_{false};
    bool rebuilding_{false};       // a stall exhausted the buffer: collecting buffer_turns turns again (idle_ms_ goes on counting meanwhile)
    uint32_t phase_{0};            // 0: at a turn boundary, 1: between the two ticks of `current_`
    uint32_t acc_ms_{0};
    uint32_t idle_ms_{0};          // milliseconds of update() calls since the last tick that was run (see stalled_ms)
    std::deque<uint32_t> started_with_;   // how many turns were queued each time a turn was started (the last standing_window of them: the standing queue)
    std::function<void()> on_tick_;
    std::function<void(const sim::Command&, const sim::CommandResult&)> on_command_;
    std::function<void(const sim::Command&)> on_applied_;
};

}  // namespace ants::net
