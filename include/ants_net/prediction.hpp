#pragma once

// Client-side prediction of one's own orders (docs/NETWORK_PORT.md, "Prediction of one's own orders").
//
// A match runs on every machine from the same sealed turns. An order that this player gives takes a round trip to the server, waits for its seal and for the jitter buffer before the
// engine runs it: about 140 ms at a round trip of 60 ms, and everything the player does feels late. The original had no such delay for one's own ants (it was not lock-step: every machine
// simulated its own team). The prediction brings the feeling back WITHOUT touching the lock-step: the confirmed engine, the turns, the hashes, the server and the rules stay exactly as they
// are, and a SECOND engine, the predicted one, shows what the screen shows:
//
//     predicted engine  =  a copy of the confirmed engine (SimulationEngine's value semantics)
//                          + the turns that are already in hand (the jitter buffer) run as KNOWN commands
//                          + the player's own orders that are not sealed yet, applied at the tick at which the prediction stands
//                          advanced to the DISPLAY TICK = the confirmed tick + the lead (the delay of one's own orders, in ticks).
//
// Other players' commands are not predicted (their ants go on with what the engine knows). Predicted: group moves, group special orders, group attacks and Stop. NOT predicted, because they
// wait for the confirmation: Hatch (a new ant would need an id that only the confirmed stream gives), the alliance commands, Quit (and Drop, which only the sequencer makes).
//
// HOW IT STAYS CORRECT. The predicted engine is derived state: it can always be rebuilt from the confirmed engine, the turns in hand and the orders that are still waiting. It is advanced ONE
// tick for every tick that the confirmed engine executes, and it is rebuilt (copy + replay) only when something that it ran as an assumption turns out wrong: the prediction keeps, for every tick
// from the confirmed one to the display tick, the list of commands that it applied there, and every live turn that arrives for a tick it has already started is compared with that list.
//   - the turn carries exactly what was assumed (no command, or the player's own orders at the tick where they were applied): nothing to do, the prediction goes on;
//   - it carries something else (a command of another player, an order of one's own that was sealed at another tick than it was applied, a command of a kind that is not predicted): the
//     prediction is marked stale, and the next time anybody asks for it (a frame, an order, the next tick) it is rebuilt from the confirmed engine with the turns as they are now. That is the
//     CORRECTION: it shows only in the ants that the wrong assumption moved, by what they walked in the ticks in question (a tick is 50 ms: a few pixels).
// An own order that no turn carries (the server refused it: flood control, a host change) is dropped after pending_timeout_ticks, and the prediction is rebuilt without it.
//
// WHEN IT IS OFF. The owner (NetGame) suspends it (set_suspended) in every state in which the confirmed engine is not simply following the live stream: before the first turn has run (the "Get
// ready" dialog), a pause, a catch-up, a rejoin, a host change, a desync, a hidden page's background step; and the user can turn it off. While it is off the engine that is shown is the confirmed
// one, as it always was, and nothing is predicted. It never touches the confirmed engine, the network or the hashes.
//
// The class has no clock, no sockets and no SDL: the owner calls on_turn() for every live turn that the runner queues (LockstepRunner::set_on_turn), on_tick() after every tick that the runner
// executes (set_on_tick), and submit() for the player's commands.

#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

#include "ants_net/lockstep.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

class Prediction {
public:
    struct Config {
        uint8_t seat{sim::PLAYER_NEUTRAL};        // the player whose orders are predicted
        uint32_t min_lead_ticks{1};               // the display tick is at least this many ticks ahead of the confirmed one
        uint32_t max_lead_ticks{12};              // ... and at most this many (600 ms: a link slower than that is not helped by guessing further)
        uint32_t lead_fall_after_ticks{40};       // the lead falls by one tick when the delay has asked for less for this long (2 s): it rises at once
        uint32_t pending_timeout_ticks{100};      // an own order that no turn has carried after this many ticks (5 s) is lost
        size_t max_events{4096};                  // the cues and news of the predicted ticks that are kept for the application
        // The budget. The prediction is work that a frame pays for: a rebuild (a copy and a replay of `lead` ticks), or the ticks that it runs for one confirmed tick, that takes longer than
        // `budget_ns` is a STRIKE, and `budget_strikes` of them within `budget_window_ticks` confirmed ticks switch it off for the rest of the match (gave_up(): the confirmed engine is shown
        // from then on, as without the prediction). A machine that cannot afford it (a slow browser, a very large map) loses the prediction, never the frame rate.
        uint64_t budget_ns{12ull * 1000ull * 1000ull};     // 12 ms: three quarters of a frame at 60 Hz (a rebuild is a few hundred microseconds on the shipped maps, R1)
        uint32_t budget_strikes{4};                        // (not one: a machine that is busy with something else stalls a frame now and then, and that is not the prediction's cost)
        uint32_t budget_window_ticks{200};                 // 10 s
    };

    /// What the prediction did (for the tests and the measurements; nothing here is state)
    struct Stats {
        uint64_t ticks_advanced{0};               // predicted ticks run one by one, with the confirmed engine
        uint64_t rebuilds{0};                     // copies of the confirmed engine with a replay
        uint64_t rebuilds_foreign{0};             // ... because a turn carried a command of another player that the prediction did not have
        uint64_t rebuilds_own_timing{0};          // ... because an own order was sealed at another tick than the one at which it was applied
        uint64_t rebuilds_other{0};               // ... a command of a kind that is not predicted, a lost order, a tick that no hook told of
        uint64_t replay_ticks{0};                 // ticks run by the rebuilds
        uint64_t turns_checked{0};                // live turns compared with the assumptions
        uint64_t commands_predicted{0};           // own orders applied to the predicted engine
        uint64_t commands_lost{0};                // own orders that no turn ever carried
        uint64_t starts{0};                       // times the prediction began (the first tick, and after every suspension)
        uint64_t rebuild_ns_total{0};             // time of the rebuilds, and the longest
        uint64_t rebuild_ns_max{0};
        uint64_t advance_ns_total{0};             // time of the predicted ticks run one by one, and the longest
        uint64_t advance_ns_max{0};
        uint64_t over_budget{0};                  // strikes: rebuilds or runs of predicted ticks that took longer than Config::budget_ns
        // Corrections (measure_corrections only): what a rebuild changed in the picture, compared at the display tick before and after
        uint64_t corrections{0};                  // rebuilds that were measured
        uint64_t corrections_visible{0};          // ... in which at least one ant stood elsewhere afterwards
        uint64_t ants_moved{0};                   // ants that stood elsewhere, over all corrections
        uint32_t max_move_px{0};                  // the largest distance that an ant was moved, in pixels (the larger of the two axes)
    };

    /// A cue or a line of news that the predicted engine made at `tick` (the tick that produced it); `generation` counts the rebuilds. `replay` is true for what a rebuild's REPLAY made: a tick
    /// that the predicted engine had run before (or the ticks that a rebuild catches up in one go), as against the tick that it runs on from its display tick, which no run has made before.
    /// The application decides what is heard (CueRouter) and plays each occurrence once.
    struct PredictedAudio {
        sim::AudioEvent event;
        uint64_t tick{0};
        uint32_t generation{0};
        bool replay{false};
    };
    struct PredictedNews {
        sim::NewsEvent event;
        uint64_t tick{0};
        uint32_t generation{0};
        bool replay{false};
    };

    /// `confirmed` and `runner` must outlive the prediction (a runner that moves to another session as the host changes stays where it is: it is a heap object). Nothing is predicted until
    /// the first on_tick().
    Prediction(const sim::SimulationEngine& confirmed, const LockstepRunner& runner, Config config);
    Prediction(const Prediction&) = delete;
    Prediction& operator=(const Prediction&) = delete;

    // ---- the owner's switches ---------------------------------------------------------------------------------------------------------------------
    /// While suspended the prediction is off: the predicted engine is dropped (the memory stays for the next time), submit() refuses, and nothing is checked. Cleared, it begins again with
    /// the next on_tick().
    void set_suspended(bool suspended);
    bool suspended() const noexcept { return suspended_; }
    /// The delay of the player's orders as the owner knows it (the measured one, or an estimate from the round trip and the jitter buffer), in ms: the lead is that many ticks, rounded. It rises
    /// at once and falls slowly (Config::lead_fall_after_ticks), so that the picture does not jump back and forth with the phase of the server's seals.
    void set_expected_delay_ms(uint32_t ms);
    uint32_t lead_ticks() const noexcept { return lead_; }
    /// Compare the picture before and after every rebuild (Stats::corrections...): costs a copy of the ants' positions, off by default
    void set_measure_corrections(bool on) noexcept { measure_corrections_ = on; }
    /// The runner moved to another session (host migration): the same runner object, but a caller that holds another one says so
    void rebind(const LockstepRunner& runner) noexcept { runner_ = &runner; }

    // ---- the three inputs ---------------------------------------------------------------------------------------------------------------------------
    /// The player's command, as the HUD gave it (the issuer is the seat). A predicted kind is applied to the predicted engine at once, at the tick it stands at, and `result` is what the
    /// engine said (Applied, the ant that acknowledges, the ants that needed the order): true. False (nothing happened, `result` untouched) when the prediction is off or the command is of a
    /// kind that waits for the confirmation: the caller answers as it did before the prediction existed.
    bool submit(const sim::Command& command, sim::CommandResult& result);
    /// A live turn has been queued by the runner. Compared with what the prediction assumed for that tick; the player's own orders in it leave the waiting list.
    void on_turn(const TurnMsg& turn);
    /// The confirmed engine has executed one more tick: the predicted engine runs one more (to the display tick). The first call after the prediction was off begins it.
    void on_tick();

    // ---- the output ---------------------------------------------------------------------------------------------------------------------------------
    /// True while the predicted engine is the one that the screen shows (it exists, is not suspended and has not given up). When it is false the confirmed engine is shown.
    bool active() const noexcept { return running_; }
    /// True when the prediction switched itself off for the rest of the match because its work took longer than the budget (Config::budget_ns) too often: it never begins again
    bool gave_up() const noexcept { return gave_up_; }
    /// The predicted engine, up to date (a stale one is rebuilt first); only while active(). The player's orders go through submit(), never to this engine directly.
    sim::SimulationEngine& engine();
    /// The tick that the predicted engine stands at, and how far it is ahead of the confirmed engine (0 when not active)
    uint64_t display_tick() const noexcept { return running_ ? display_ : 0u; }
    uint64_t ticks_ahead() const noexcept;
    /// True when a turn has shown that the predicted engine is wrong and it has not been rebuilt yet (it is, the next time it is asked for)
    bool stale() const noexcept { return running_ && stale_; }
    /// The own orders that no turn has carried yet
    size_t pending_orders() const noexcept { return pending_.size(); }
    const Stats& stats() const noexcept { return stats_; }
    /// The cues and news that the predicted ticks made since the last call (each tick that the engine ran, once per run: see PredictedAudio::generation). Nothing is kept while off.
    std::vector<PredictedAudio> take_audio();
    std::vector<PredictedNews> take_news();

    /// Which commands are predicted (the kinds that move or stop one's own ants)
    static bool predicts(sim::CommandType type) noexcept;

    // ---- diagnostics (the tests; nothing here changes the prediction) -------------------------------------------------------------------------------
    /// The state hash of the engine that a rebuild would make NOW (a scratch copy of the confirmed engine, run to the display tick with the turns in hand and the waiting orders). The
    /// predicted engine advanced tick by tick must equal it whenever it is not stale: that is what "it is derived state" means, and the property that the tests hold it to.
    sim::StateHash derived_hash() const;
    /// The commands that the predicted engine has applied at the state it stands at (before that tick runs): the turn's commands when the turn was in hand, else the own orders given here
    const std::vector<sim::Command>& applied_at_display() const noexcept;

private:
    struct Pending {
        sim::Command command;
        uint64_t tick{0};                         // the tick at which the prediction applied it (or will apply it, after a correction)
        uint64_t born{0};                         // the confirmed tick when the player gave it: a lost order is told by the time that has passed since THEN (a correction moves `tick`)
    };
    using Clock = std::chrono::steady_clock;

    void begin(uint64_t confirmed_tick);
    void stop();
    /// A copy of the confirmed engine run to `target_display` with the turns in hand and the waiting orders (`is_start`: the prediction begins, nothing is corrected)
    void rebuild(uint64_t target_display, bool is_start);
    void advance_to(uint64_t target_display);
    void advance_one();
    /// Applies the commands of the state that stands at `tick` (before its tick runs) and records them: the turn when it is in hand (certain), else the own orders waiting for this tick
    void apply_commands_for(uint64_t tick);
    void capture_events(uint64_t tick, bool replay);
    void update_lead();
    /// A rebuild or a run of predicted ticks took `ns`: over the budget it is a strike, and enough of them within the window end the prediction for this match
    void note_cost(uint64_t ns);
    void drop_lost_orders(uint64_t confirmed_tick);
    uint64_t confirmed_tick() const noexcept { return confirmed_->current_tick(); }

    const sim::SimulationEngine* confirmed_;
    const LockstepRunner* runner_;
    Config cfg_;
    sim::SimulationEngine pred_;
    bool running_{false};
    bool suspended_{false};
    bool gave_up_{false};
    std::deque<uint64_t> strikes_;                // the confirmed ticks at which the budget was exceeded, within the window
    bool stale_{false};
    bool stale_foreign_{false};                   // why it went stale (the statistics)
    bool stale_own_timing_{false};
    uint64_t display_{0};                         // the tick the predicted engine stands at: the ticks before it have run (the engine's own counter stops at the end of the match)
    uint64_t base_tick_{0};                       // the tick that assumed_.front() is about: the confirmed engine's tick
    std::deque<std::vector<sim::Command>> assumed_;      // [tick - base_tick_]: the commands applied at that tick, for every tick from the confirmed one to the display tick
    std::deque<Pending> pending_;
    uint32_t lead_{3};
    uint32_t target_lead_{3};
    uint32_t lead_low_ticks_{0};
    uint32_t generation_{0};
    bool measure_corrections_{false};
    std::vector<PredictedAudio> audio_;
    std::vector<PredictedNews> news_;
    Stats stats_;
};

}  // namespace ants::net
