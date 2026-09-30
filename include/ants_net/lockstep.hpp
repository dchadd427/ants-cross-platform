#pragma once

// The client half of lock-step: a queue of sealed turns and the driver that executes them on the simulation. Turns arrive from the host at
// unsteady moments (network jitter); the runner buffers a few of them and executes one every 100 ms of real time, two ticks each, so the
// simulation runs at a steady 20 Hz on every machine and at exactly the same commands per tick. If the next turn has not arrived when it is due
// the runner stalls (the game shows "waiting"); when turns pile up it runs faster to catch up.

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

    /// The turns received last (up to kTurnLogTurns), kept so that a machine that becomes the host after the old one left can hand the turns that others
    /// are missing to them (docs/NETWORK_PORT.md, host migration). Null when the turn is older than the log or not received yet.
    const TurnMsg* logged_turn(uint32_t turn) const noexcept;
    static constexpr size_t kTurnLogTurns = 300;               // 30 s of play

    uint32_t next_turn_to_execute() const noexcept { return next_execute_; }
    uint32_t next_turn_expected() const noexcept { return next_receive_; }
    size_t queued() const noexcept { return queue_.size(); }
    /// The next turn is due but has not arrived
    bool stalled() const noexcept { return started_ && phase_ == 0 && queue_.empty() && acc_ms_ >= kTickMs; }
    /// Milliseconds into the current tick (0 .. 49), for smooth drawing between ticks
    uint32_t sub_tick_ms() const noexcept { return started_ ? std::min<uint32_t>(acc_ms_, kTickMs - 1) : 0u; }
    bool started() const noexcept { return started_; }
    static constexpr uint32_t kTickMs = 50;

private:
    sim::SimulationEngine& sim_;
    Config cfg_;
    std::deque<TurnMsg> queue_;
    std::deque<TurnMsg> log_;      // the last kTurnLogTurns turns received, consecutive
    TurnMsg current_;              // the turn whose second tick is still to come (phase 1)
    uint32_t next_receive_{0};     // the turn number the queue expects next
    uint32_t next_execute_{0};
    bool started_{false};
    uint32_t phase_{0};            // 0: at a turn boundary, 1: between the two ticks of `current_`
    uint32_t acc_ms_{0};
    std::function<void()> on_tick_;
    std::function<void(const sim::Command&, const sim::CommandResult&)> on_command_;
};

}  // namespace ants::net
