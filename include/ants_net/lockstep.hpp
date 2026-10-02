#pragma once

// The client half of lock-step: a queue of sealed turns and the driver that executes them on the simulation. Turns arrive from the host at unsteady moments (network
// jitter); the runner keeps a few of them in hand and runs one every 50 ms of real time, one tick each, so the simulation runs at a steady 20 Hz on every machine and at
// exactly the same commands per tick.
//
// The jitter buffer (jitter.hpp) says how many turns are kept in hand: one on a steady link, up to four on a rough one. A turn runs that many turns after it was read, so the
// buffer is also the delay that the network adds to every command, and the runner keeps it as small as the link allows.
//   - start     the runner begins when target + 1 turns are queued (the first one runs at once, `target` stay behind it)
//   - stall     the next turn is due and has not arrived. The runner waits, owes nothing for the wait (the accumulator holds one tick at most: a late turn runs the moment
//               it comes, never in a burst) and collects target + 1 turns again before it goes on, so that the buffer that the stall used up is back and the next late turn
//               is on time. When the late turn comes the jitter buffer is told how long the wait was: a short one grows the buffer at once, a long one (a lost packet that
//               the link resends: no buffer would bridge it) does not. A turn that comes late behind a bunch of others (a blocked link that frees at last) finds the queue
//               full and does not wait.
//   - too many  more turns are queued than the buffer asks for (a stall that ended in a bunch, a window that was not drawn for a second, a lagging player's backlog, the
//               buffer that shrank): the runner runs faster, in proportion to the excess, up to four times, instead of carrying the extra turns as delay to the end of the
//               match. The excess is measured in time: the newest queued turn is due `slack_ms()` from now, and the buffer wants `target * 50 ms` and 10 ms of rounding
//               for the frames; every 50 ms beyond that adds a quarter of the normal speed (1.25x, 1.5x, ...), twelve turns or more run at 4x.
//   - a frame that stood for more than 100 ms (a window that is hardly drawn) is not read as the link's lateness and does not count as a stall: the turns that came meanwhile
//               are all there at its start, and its own time covers the ticks that its length holds.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "ants_net/jitter.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

class LockstepRunner {
public:
    struct Config {
        JitterBuffer::Config jitter{};    // the buffer in turns: min_turns == max_turns makes it fixed
        uint32_t max_ticks_per_update = 8;   // ticks that one update runs at most (a frame that stood for more than 100 ms may run the ticks of its time beyond that, too)
        uint32_t max_speed_x4 = 16;       // the fastest run-down of a queue that is longer than the buffer, in quarters of normal speed (16: four times)
    };

    /// What one executed turn produced for the caller: the turn number and, on every kHashEveryTurns-th turn, the state hash after it.
    struct Executed {
        uint32_t turn{0};
        bool has_hash{false};
        sim::StateHash hash;
    };

    explicit LockstepRunner(sim::SimulationEngine& sim) : LockstepRunner(sim, Config{}) {}
    LockstepRunner(sim::SimulationEngine& sim, Config config) : sim_(sim), cfg_(config), jitter_(config.jitter) {}

    /// A sealed turn from the host. Turns must arrive in order without gaps (the transport is reliable and ordered); anything else is refused.
    bool on_turn(TurnMsg turn);

    /// Advances real time by `dt_ms` and executes the ticks that are due (one every 50 ms of game time, faster while the queue is longer than the buffer: a turn's commands are
    /// applied, then its tick runs). Returns the turns that were completed, in order.
    std::vector<Executed> update(uint32_t dt_ms);

    /// Called after every simulation tick (once per turn), so that the application can present each tick (HUD, events, audio) as the local game does
    void set_on_tick(std::function<void()> fn) { on_tick_ = std::move(fn); }
    /// Called for every command a turn applies, with the engine's verdict (the application uses it to correct its predicted feedback)
    void set_on_command(std::function<void(const sim::Command&, const sim::CommandResult&)> fn) { on_command_ = std::move(fn); }
    /// The session's own observer, called for every command a turn applies just before on_command: the session measures the delay of the player's commands with it
    /// (latency.hpp). A separate hook, so that the application's on_command stays free; whoever takes the runner over (host migration) installs its own.
    void set_on_applied(std::function<void(const sim::Command&)> fn) { on_applied_ = std::move(fn); }

    /// The turns received last (up to kTurnLogTurns), kept so that a machine that becomes the host after the old one left can hand the turns that others
    /// are missing to them (docs/NETWORK_PORT.md, host migration). Null when the turn is older than the log or not received yet.
    const TurnMsg* logged_turn(uint32_t turn) const noexcept;
    static constexpr size_t kTurnLogTurns = 30 * kTurnsPerSecond;       // 30 s of play

    uint32_t next_turn_to_execute() const noexcept { return next_execute_; }
    uint32_t next_turn_expected() const noexcept { return next_receive_; }
    size_t queued() const noexcept { return queue_.size(); }
    /// How far behind the turns that are here this machine is: the turns that were received and not yet run, in ms of play. A player that was away for a while (a frozen
    /// window, a machine that stopped) finds a long backlog and runs it down at up to four times normal speed ("Catching up...").
    uint32_t backlog_ms() const noexcept { return static_cast<uint32_t>(queue_.size()) * kTurnMs; }
    /// The runner is waiting for a turn that is due (or collecting the buffer again after such a wait): see stalled_ms
    bool stalled() const noexcept { return stalled_ms() > 0; }
    /// How long the runner has stood still for want of a turn: the time, in ms, since a tick was due and had no turn to run, while that goes on; 0 while it runs (every tick
    /// starts the count again, so a turn that arrives ends the stall at once), and before it has started. The "Waiting for the other players..." message shows at one second
    /// of it. It is not measured from the accumulator of unspent time, which would stay "due" for ever while turns arrive on time (docs/NETWORK_PORT.md).
    uint32_t stalled_ms() const noexcept { return started_ ? stall_ms_ : 0u; }
    /// Milliseconds into the current tick (0 .. 49), for smooth drawing between ticks
    uint32_t sub_tick_ms() const noexcept { return started_ ? std::min<uint32_t>(acc_q_ / 4u, kTickMs - 1) : 0u; }
    static constexpr uint32_t kTickMs = kTurnMs / kTicksPerTurn;
    /// The application hands the network at most a second of a frame (Application::run_frame_with_delta): a frame of that length may have been cut, so this runner's clock
    /// jumped by less than the time that passed, and the lateness measured before and after it do not refer to the same clock: the rule forgets what it has read
    static constexpr uint32_t kClockCutMs = 1000;

    /// The jitter buffer in turns now (jitter.hpp), the speed the runner runs at in quarters of normal (4: normal), and the rule itself for diagnostics
    uint32_t buffer_turns() const noexcept { return jitter_.target(); }
    /// The speed of the next update (given the time that it will stand for, 0 for "now") in quarters of normal: 4 is normal, 16 the fastest
    uint32_t speed_x4(uint32_t dt_ms = 0) const noexcept;
    /// How long from now until the newest queued turn is due, in ms: the time that the buffer holds in hand (0 when nothing is queued)
    uint32_t slack_ms(uint32_t dt_ms = 0) const noexcept;
    static constexpr uint32_t kSlackDeadMs = 10;       // the slack beyond the buffer that is left alone (a frame of 60 Hz is 17 ms: the turns are read at frames)
    const JitterBuffer& jitter() const noexcept { return jitter_; }
    /// True while the runner collects the buffer again after a stall (it runs nothing meanwhile)
    bool rebuilding() const noexcept { return started_ && rebuilding_; }

private:
    static constexpr uint32_t kTickQuarters = kTickMs * 4u;           // the accumulator counts quarters of a millisecond of game time

    sim::SimulationEngine& sim_;
    Config cfg_;
    JitterBuffer jitter_;
    std::deque<TurnMsg> queue_;
    std::deque<TurnMsg> log_;      // the last kTurnLogTurns turns received, consecutive
    uint32_t next_receive_{0};     // the turn number the queue expects next
    uint32_t next_execute_{0};
    bool started_{false};
    bool rebuilding_{false};       // a stall used the buffer up: collecting target + 1 turns again (stall_ms_ goes on counting meanwhile)
    bool in_stall_{false};         // a tick was due and had no turn, and no tick has run since
    bool stall_told_{false};       // ... and the jitter buffer has been told how long the wait was (when the late turn came)
    uint32_t acc_q_{0};            // game time owed to the next tick, in quarters of a millisecond
    uint32_t stall_ms_{0};         // milliseconds of update() calls since a tick was due and had no turn (see stalled_ms)
    uint64_t now_ms_{0};           // the sum of the dt of every update: the clock that the arrivals are stamped with
    uint64_t updates_{0};          // how many updates there have been
    uint32_t fresh_first_{0};      // the turns received since the last update (they are stamped with the clock of that update): the first of them and how many
    uint32_t fresh_count_{0};
    std::function<void()> on_tick_;
    std::function<void(const sim::Command&, const sim::CommandResult&)> on_command_;
    std::function<void(const sim::Command&)> on_applied_;
};

}  // namespace ants::net
