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
// FOG OF WAR. The explored ground is the player's own picture, and a guess of another player's order (an ally's walk that turns) would explore ground that the real engine never explores: so the
// predicted engine takes the confirmed engine's explored ground after every tick (SimulationEngine::adopt_explored_ground_of), and the screen, the HUD's picks and the minimap, which ask the shown
// engine what is explored, never show an ant of another team where the confirmed engine has not explored yet (the veil lifts as the confirmed engine's ants arrive, a few ticks after the shown ones).
//
// Other players' commands are not predicted (their ants go on with what the engine knows). Predicted: group moves, group special orders, group attacks and Stop. NOT predicted, because they
// wait for the confirmation: Hatch (a new ant would need an id that only the confirmed stream gives), the alliance commands, Quit (and Drop, which only the sequencer makes).
//
// HINTS (docs/P2P_HINTS.md). Another player's order is learned from its sealed turn, a round trip's worth late. A HINT is the same order sent ahead of its turn, by the other player's machine (a
// direct channel between the two browsers) or, for a computer player, by the server (the moment the bot decides it): hint() puts it into the predicted engine at the tick it names, so that the ants it
// concerns are moved by less when the turn arrives. The hint is a guess and the sealed turn is the truth: the confirmed engine, the turns, the hashes and the record never see a hint, and a hint that
// the sealed turn does not confirm is dropped. The matching rule (the one place it is written): a sealed command of another seat takes the OLDEST hint of that seat that is equal to it; the hints of the
// same seat before it were not sealed (the server refused them): dropped. A hint is applied at its tick (never behind a turn in hand); when that tick's turn comes without it, it moves ONCE to the next
// tick (a late order is most often one tick late, as for one's own orders), and when that turn comes without it either, it is dropped; one that no turn has carried for pending_timeout_ticks is
// dropped. A hint that comes after its sealed turn (the direct path is slower than the server's) is ignored. Only the kinds that predicts() names are hinted, and only by a seat that is not this one.
//
// THE TICK OF AN ORDER (protocol 30). An order is sent with the tick that this screen applies it at (plan_order(): the display tick, never behind a turn in hand), and the host seals it into exactly that turn when the turn is still
// open, else into the first open one (the order came late: the correction below). The host answers every such order with the first turn that was open when it arrived (OrderAck, on_order_ack()), and the lead learns from those
// answers how many ticks an order needs: the longest of the last ones. So an order is sealed where the screen put it unless the link did something that the lead has not seen yet; nothing is corrected then.
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
// ready" dialog), a pause, a catch-up, a rejoin, a host change, a desync, a hidden page's background step; and the user can turn it off. It also switches itself off for a cool-down when its
// work costs more than its budget too often (Config::budget_ns). While it is off the engine that is shown is the confirmed one, as it always was, and nothing is predicted. It never touches
// the confirmed engine, the network or the hashes.
//
// The class has no sockets and no SDL, and its only clocks are the thread's CPU time and the wall clock, for the budget: the owner calls on_turn() for every live turn that the runner queues
// (LockstepRunner::set_on_turn), on_tick() after every tick that the runner executes (set_on_tick), and submit() for the player's commands.

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "ants_net/lockstep.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

/// The CPU time that the calling thread has used, in ns: what the budget counts (a sleep, a preempted thread or a stalled process is not the prediction's cost). POSIX clock_gettime(
/// CLOCK_THREAD_CPUTIME_ID); Windows GetThreadTimes (kernel + user: a counter of clock ticks); the web build has one thread and gives the wall clock. Never goes back within a thread.
uint64_t thread_cpu_ns();

/// What a block of work costs against the budget: the thread's CPU time, never more than the wall time (a clock that counts in ticks may charge a short block a whole tick)
inline uint64_t work_cost_ns(uint64_t wall_ns, uint64_t cpu_ns) noexcept { return cpu_ns < wall_ns ? cpu_ns : wall_ns; }

class Prediction {
public:
    struct Config {
        uint8_t seat{sim::PLAYER_NEUTRAL};        // the player whose orders are predicted
        uint32_t min_lead_ticks{1};               // the display tick is at least this many ticks ahead of the confirmed one
        uint32_t max_lead_ticks{12};              // ... and at most this many (600 ms: a link slower than that is not helped by guessing further)
        uint32_t lead_rise_after_ticks{40};       // the lead rises by one tick when the delay has asked for one more for this long (2 s); it rises at once by two and more
        uint32_t lead_fall_every_ticks{4};        // the lead falls one tick at a time, the first at once and each further one this many ticks later: the picture stands still for one tick each time, never longer
        uint32_t pending_timeout_ticks{100};      // an own order that no turn has carried after this many ticks (5 s) is lost
        size_t max_events{4096};                  // the cues and news of the predicted ticks that are kept for the application
        // The budget. The prediction is work that a frame pays for: a rebuild (a copy and a replay of `lead` ticks), or the ticks that it runs for one confirmed tick, that takes more than
        // `budget_ns` of the thread's CPU time is a STRIKE, and `budget_strikes` of them within `budget_window_ticks` confirmed ticks start a COOL-DOWN of `cooldown_ticks` confirmed ticks:
        // the prediction is off (cooling_down(): the confirmed engine is shown from then on, as without the prediction), and after it begins again from the confirmed engine like after any
        // suspension. Each further cool-down of the match is twice as long, up to `cooldown_max_ticks`. A machine that cannot afford it (a slow browser, a very large map) loses it for a
        // while, never the frame rate.
        uint64_t budget_ns{12ull * 1000ull * 1000ull};     // 12 ms: three quarters of a frame at 60 Hz (a rebuild is a few hundred microseconds on the shipped maps, R1)
        uint32_t budget_strikes{4};                        // (not one: a machine that is busy with something else stalls a frame now and then, and that is not the prediction's cost)
        uint32_t budget_window_ticks{200};                 // 10 s
        uint32_t cooldown_ticks{200};                      // 10 s
        uint32_t cooldown_max_ticks{3200};                 // 160 s
        std::function<void()> work_hook;                   // the tests: called inside every timed block (a sleep there is not a cost, a spin is)
        // The tests: the clocks that a timed block is measured with, in ns (empty: the real ones, steady_clock and thread_cpu_ns()). Moved by hand from the work hook they cost a block exactly
        // what the test says on every platform (tests/test_net/manual_clock.hpp).
        std::function<uint64_t()> wall_clock;
        std::function<uint64_t()> cpu_clock;
        // The lead is the LONGEST lag of the last `lag_samples` own orders, none older than `lag_fresh_ticks`, plus `lead_bias_ticks`. The lag of an order is the ticks from the confirmed tick at which it was given to the first
        // turn that was open when it reached the host (the host's OrderAck). It varies by a tick with the moment of the click (the phase of the host's seals) and with the link's jitter, and the host seals an order into the
        // tick that it is stamped with when that tick is still open: with the longest lag of the fresh orders the stamp is open for nearly every order, so it is sealed where this screen put it and nothing is corrected. The
        // price is that an order runs in the confirmed engine a little later than it could (the lag's spread, a tick on average at most); the picture is not later, it shows the order at once. The same margin is all that an
        // order that is sent with no tick gains over one that waits for its stamp (NETWORK_PORT.md, "Protocol 30"): it is never more than the lead less the order's own lag, and the lead is held down twice:
        //  - no lag counts for more than `lag_spike_cap_ticks` above the MEDIAN of the fresh ones (one order that a stall held up must not hold the lead high for as long as it is fresh; a link whose lags are spread over
        //    more than that is learned by its middle plus the cap, and the orders beyond it are late);
        //  - a stall ends with the first answer that comes back at the link's own pace: the older lags that stand more than `lag_forget_ticks` above a NEWER answer are dropped (the stall's orders all report the stall, several
        //    of them in a second, and would otherwise be most of the twelve and set the middle too), so the lead is back to the link's lag as fast as it falls (one tick at a time: `lead_fall_every_ticks`).
        // Off: the owner's estimate alone (the tests that hold a wrong lead to account).
        bool learn_lead{true};
        uint32_t lag_samples{12};
        uint32_t lag_spike_cap_ticks{2};
        uint32_t lag_forget_ticks{4};                      // 200 ms: more than the spread that the links measured have (jitter up to 60 ms: three ticks), less than any stall that is worth a longer lead
        uint32_t lag_fresh_ticks{200};                     // 10 s: a stall that no order has come back from since is forgotten after this long (LK18: the same orders late as with 20 s)
        // The number of the first order (1, 2, 3 ...). A game that makes a new prediction in a match that goes on (the player switched it off and on) gives it the number where the last one stopped: an answer that is
        // still on its way is about an order of the old one, and its number is never an order of the new one's.
        uint32_t first_seq{1};
        // Ticks that the display stands beyond the lag. 0: an order is put where the host runs it, so nothing is corrected while the lag holds. 1: a tick more room for a jitter that the lag has not seen yet.
        uint32_t lead_bias_ticks{0};
        // Hints (see above). A hint whose tick is more than this many ticks behind the next turn that is expected is older than any order that is not sealed yet: it is late. At most `max_hints` wait at a
        // time (a peer that floods hints cannot make the prediction rebuild without end: the budget and the cool-down count its work too).
        uint32_t hint_max_age_ticks{12};
        size_t max_hints{64};
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
        uint64_t sealed_exact{0};                 // own orders that the host sealed at the tick at which the prediction first applied them (what the click was aimed at is what the order met)
        uint64_t sealed_early{0};                 // ... at an earlier tick
        uint64_t sealed_late{0};                  // ... at a later one
        uint64_t lead_changes{0};                 // times the lead changed while the prediction ran (each moves the whole picture by a tick of walking)
        uint64_t starts{0};                       // times the prediction began (the first tick, and after every suspension)
        uint64_t rebuild_ns_total{0};             // time of the rebuilds, and the longest
        uint64_t rebuild_ns_max{0};
        uint64_t advance_ns_total{0};             // time of the predicted ticks run one by one, and the longest
        uint64_t advance_ns_max{0};
        uint64_t over_budget{0};                  // strikes: rebuilds or runs of predicted ticks that cost more than Config::budget_ns
        uint64_t cooldowns{0};                    // cool-downs started (the strikes reached Config::budget_strikes)
        // Hints (hint()): what became of the orders that other seats sent ahead of their turns
        uint64_t hints_received{0};               // every call
        uint64_t hints_accepted{0};               // put into the predicted engine
        uint64_t hints_refused{0};                // the prediction was off, an own seat, a kind that is not predicted, or too many waiting
        uint64_t hints_duplicate{0};              // a copy that was seen already (a channel sends each hint more than once)
        uint64_t hints_late{0};                   // came after the sealed turn that carries the order
        uint64_t hints_confirmed{0};              // matched by a sealed command at the tick where the hint stood: nothing to correct
        uint64_t hints_confirmed_moved{0};        // matched by a sealed command at another tick than the one where the hint stood (one correction)
        uint64_t hints_dropped{0};                // not confirmed: the turn came without it twice, the server did not seal it before an older one of the seat, or it timed out
        uint64_t rebuilds_hint{0};                // rebuilds because of a hint (it arrived behind the display tick, or a turn did not confirm it)
        // The hints of this seat's own orders (last_order_tick()): they name the tick that the order is stamped with, which the host seals it into when it is still open
        uint64_t order_acks{0};                   // the host's answers to the orders (OrderAck) that matched an order of this prediction
        uint64_t order_acks_late{0};              // ... that said that the stamp was no longer open: the order is sealed in the first open turn, behind the tick that the screen put it at (one correction)
        // Corrections (measure_corrections only): what a rebuild changed in the picture, compared at the display tick before and after
        uint64_t corrections{0};                  // rebuilds that were measured
        uint64_t corrections_visible{0};          // ... in which at least one ant stood elsewhere afterwards
        uint64_t ants_moved{0};                   // ants that stood elsewhere, over all corrections
        uint32_t max_move_px{0};                  // the largest distance that an ant was moved, in pixels (the larger of the two axes)
        uint64_t move_px_total{0};                // the sum of the distances that the moved ants were moved, in pixels (the larger of the two axes of each): over ants_moved, the usual size of a correction
    };

    /// An ant that a rebuild moved: the picture showed it at the engine's position plus (dx, dy) at the tick that was shown, before the rebuild (pixels)
    struct Shift {
        uint32_t ant{0};
        int32_t dx{0};
        int32_t dy{0};
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
    /// The lag of the player's orders as the owner estimates it (the round trip and the jitter buffer), in ms: the lead is that many ticks, rounded, plus Config::lead_bias_ticks, until the orders themselves
    /// have taught it their lag (Config::learn_lead). It falls one tick at a time (the first at once, the next ones Config::lead_fall_every_ticks apart), rises at once when an order has needed more (a learned
    /// lag) or by two ticks and more (an estimate), and by one tick on an estimate only after Config::lead_rise_after_ticks, so that the picture never stands still for more than a tick.
    void set_expected_delay_ms(uint32_t ms);
    uint32_t lead_ticks() const noexcept { return lead_; }
    /// What the lead has learned from the player's own orders: the longest lag of the fresh ones (see Config::learn_lead), 0 when there is none. The lead is this plus the bias, else the estimate
    /// plus the bias
    uint32_t learned_lag_ticks() const noexcept;
    /// Compare the picture before and after every rebuild (Stats::corrections...): costs a copy of the ants' positions, off by default
    void set_measure_corrections(bool on) noexcept { measure_corrections_ = on; }
    /// The runner moved to another session (host migration): the same runner object, but a caller that holds another one says so
    void rebind(const LockstepRunner& runner) noexcept { runner_ = &runner; }

    // ---- the three inputs ---------------------------------------------------------------------------------------------------------------------------
    /// The player's command, as the HUD gave it (the issuer is the seat). A predicted kind is applied to the predicted engine at once, at the tick it stands at, and `result` is what the
    /// engine said (Applied, the ant that acknowledges, the ants that needed the order): true. False (nothing happened, `result` untouched) when the prediction is off or the command is of a
    /// kind that waits for the confirmation: the caller answers as it did before the prediction existed.
    /// The order is numbered (last_order_seq(): the number that plan_order() took for it, else the next one, 1, 2, 3 ...) and waits for the host's answer (on_order_ack).
    bool submit(const sim::Command& command, sim::CommandResult& result);
    /// Where an order given NOW is applied (protocol 30): `tick` is the turn that the host is asked to seal it into (CommandMsg::turn), `seq` the number that the host answers with (CommandMsg::seq).
    /// Nothing is applied: the engine is only brought to the state that submit() would apply it to (a stale one is rebuilt, one that stands behind the turns in hand runs up to them), so the plan is the tick
    /// that submit() then uses, in the same call of the caller. The number is TAKEN here, once and for all: submit() of the same command gives the order this number, and no other order ever has it (a plan whose order
    /// was not sent leaves a number unused, never one that two orders share). Planning the same command again before it was submitted is the same plan. Empty when submit() would refuse the order (the prediction is off,
    /// the order is another seat's or of a kind that waits): the order is sent without a tick.
    struct OrderPlan {
        uint64_t tick{0};
        uint32_t seq{0};
    };
    std::optional<OrderPlan> plan_order(const sim::Command& command);
    /// The host's answer to an order (OrderAck): `open_turn` was the first turn that was open when the order reached it. The lag of the order is that less the confirmed tick that it was given at; the lead
    /// learns from it (Config::learn_lead). An answer that matches no order (the prediction began again since, or it was answered before) is ignored. An order whose stamp is behind `open_turn` is late: the host seals it
    /// in `open_turn`, and the turn that carries it will correct the prediction.
    void on_order_ack(uint32_t seq, uint64_t open_turn);
    /// What became of a hint
    enum class HintVerdict : uint8_t { Accepted, Refused, Duplicate, Late };
    /// Another seat's order, ahead of its turn: `command` (its issuer is the sender), the sender's own number for it (`seq`, 1, 2, 3 ... : a copy of a hint that was seen is a Duplicate) and the turn that the
    /// sender asked the server to seal it in (`tick`: last_order_tick() of the sender, the stamp of its order). Applied to the predicted engine at once when the display tick is not past it, else the engine is rebuilt with it; see HINTS at the top for the matching rule.
    HintVerdict hint(const sim::Command& command, uint32_t seq, uint64_t tick);
    /// The hints that no turn has confirmed or dropped yet
    size_t pending_hints() const noexcept { return hints_.size(); }
    /// A live turn has been queued by the runner. Compared with what the prediction assumed for that tick; the player's own orders in it leave the waiting list.
    void on_turn(const TurnMsg& turn);
    /// The confirmed engine has executed one more tick: the predicted engine runs one more (to the display tick). The first call after the prediction was off begins it.
    void on_tick();

    // ---- the output ---------------------------------------------------------------------------------------------------------------------------------
    /// True while the predicted engine is the one that the screen shows (it exists, is not suspended and is not cooling down). When it is false the confirmed engine is shown.
    bool active() const noexcept { return running_; }
    /// True during a cool-down: the prediction switched itself off because its work cost more than the budget (Config::budget_ns) too often. The confirmed engine is shown, orders go as they did
    /// before the prediction, and it begins again by itself when the cool-down is over
    bool cooling_down() const noexcept { return cooling_down_; }
    /// The confirmed ticks that the cool-down still lasts (0 when there is none)
    uint64_t cooldown_ticks_left() const noexcept;
    /// The predicted engine, up to date (a stale one is rebuilt first); only while active(). The player's orders go through submit(), never to this engine directly.
    sim::SimulationEngine& engine();
    /// The tick that the predicted engine stands at (0 when not active)
    uint64_t display_tick() const noexcept { return running_ ? display_ : 0u; }
    /// True when a turn has shown that the predicted engine is wrong and it has not been rebuilt yet (it is, the next time it is asked for)
    bool stale() const noexcept { return running_ && stale_; }
    /// The own orders that no turn has carried yet
    size_t pending_orders() const noexcept { return pending_.size(); }
    /// The tick at which the last order that submit() took was applied on this screen (0 before there is one)
    uint64_t last_order_tick() const noexcept { return last_order_tick_; }
    /// The number that the last order that submit() took carries (0 before there is one)
    uint32_t last_order_seq() const noexcept { return last_order_seq_; }
    /// The number that the next order gets: what a new prediction in the same match starts from (Config::first_seq)
    uint32_t next_seq() const noexcept { return next_seq_; }
    const Stats& stats() const noexcept { return stats_; }
    /// The cues and news that the predicted ticks made since the last call (each tick that the engine ran, once per run: see PredictedAudio::generation). Nothing is kept while off.
    std::vector<PredictedAudio> take_audio();
    std::vector<PredictedNews> take_news();
    /// The ants that the picture has to move since the last call, each with how far it was shown from where the engine that is shown NOW has it: the ants that a rebuild moved (what several
    /// rebuilds moved an ant by is added up), and every ant when the prediction begins (the screen goes from the confirmed engine to the predicted one, `lead` ticks ahead) or stops (back to the
    /// confirmed one). The application draws them sliding (ants_app/view_glide.hpp). What a stop left is forgotten when nobody takes it within a tick
    std::vector<Shift> take_shifts();

    /// Which commands are predicted (the kinds that move or stop one's own ants)
    static bool predicts(sim::CommandType type) noexcept;

    // ---- diagnostics (the tests; nothing here changes the prediction) -------------------------------------------------------------------------------
    /// The state hash of the engine that a rebuild would make NOW (a scratch copy of the confirmed engine, run to the display tick with the turns in hand and the waiting orders). The
    /// predicted engine advanced tick by tick must equal it whenever it is not stale: that is what "it is derived state" means, and the property that the tests hold it to.
    sim::StateHash derived_hash() const;
    /// The commands that the predicted engine has applied at the state it stands at (before that tick runs): the turn's commands when the turn was in hand, else the own orders given here
    const std::vector<sim::Command>& applied_at_display() const noexcept;

private:
    bool ready_for(const sim::Command& command);
    struct Pending {
        sim::Command command;
        uint64_t tick{0};                         // the tick at which the prediction applied it (or will apply it, after a correction)
        uint64_t born{0};                         // the confirmed tick when the player gave it: a lost order is told by the time that has passed since THEN (a correction moves `tick`)
        uint64_t first_tick{0};                   // the tick at which it was applied when it was given: what the player aimed at (the statistics)
        uint32_t misses{0};                       // the turns of its tick that arrived without it
    };
    // An order that was sent with a number and has not been answered yet (OrderAck)
    struct Sent {
        uint32_t seq{0};
        uint64_t born{0};                         // the confirmed tick when it was given
        uint64_t stamp{0};                        // the tick that it was stamped with
    };

    struct Foreign {
        sim::Command command;
        uint32_t seq{0};
        uint64_t tick{0};                         // where the predicted engine applies it (the hint's own tick, never behind a turn in hand; one tick on after a turn came without it)
        uint64_t born{0};                         // the confirmed tick when it was accepted (a hint that no turn carries is told by the time since THEN)
        uint32_t misses{0};
    };
    // The numbers of the hints of one seat that were seen (a window of 64: the copies of a hint are the same number)
    struct SeqWindow {
        bool any{false};
        uint32_t highest{0};
        uint64_t bits{0};
        bool take(uint32_t seq) noexcept;
    };
    // A command of another seat that a live turn carried and no hint had announced: a hint for it that comes later is late
    struct SealedSeen {
        sim::Command command;
        uint64_t turn{0};
        bool matched{false};
    };
    /// The commands that the predicted engine applies at `tick` when no turn is in hand for it: the own orders waiting for it and the hints at it, in the order of a turn (by seat, each seat's in the order sent)
    std::vector<sim::Command> commands_without_turn(uint64_t tick, uint64_t next_receive, uint64_t target) const;
    bool take_hint_for(const sim::Command& cmd, uint64_t turn, bool& wrong);
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
    uint32_t wanted_lead() const noexcept;
    /// A rebuild or a run of predicted ticks cost `ns` (work_cost_ns): over the budget it is a strike, and enough of them within the window start a cool-down
    void note_cost(uint64_t ns);
    void start_cooldown(uint64_t tick);
    void drop_lost_orders(uint64_t confirmed_tick);
    void add_shift(uint32_t ant, int32_t dx, int32_t dy);
    /// Every ant that `shown` has elsewhere than `now`: it slides from there (the screen changes from one engine to the other)
    void shift_all(const std::vector<sim::AntSnapshot>& shown, const std::vector<sim::AntSnapshot>& now);
    uint64_t confirmed_tick() const noexcept { return confirmed_->current_tick(); }

    const sim::SimulationEngine* confirmed_;
    const LockstepRunner* runner_;
    Config cfg_;
    sim::SimulationEngine pred_;
    bool running_{false};
    bool suspended_{false};
    bool cooling_down_{false};
    uint64_t cooldown_until_{0};                  // the confirmed tick at which the cool-down is over
    uint64_t cooldown_next_{0};                   // the length of the next cool-down, in ticks
    std::deque<uint64_t> strikes_;                // the confirmed ticks at which the budget was exceeded, within the window
    bool stale_{false};
    bool stale_foreign_{false};                   // why it went stale (the statistics)
    bool stale_own_timing_{false};
    uint64_t display_{0};                         // the tick the predicted engine stands at: the ticks before it have run (the engine's own counter stops at the end of the match)
    uint64_t base_tick_{0};                       // the tick that assumed_.front() is about: the confirmed engine's tick
    std::deque<std::vector<sim::Command>> assumed_;      // [tick - base_tick_]: the commands applied at that tick, for every tick from the confirmed one to the display tick
    std::deque<Pending> pending_;
    std::vector<Foreign> hints_;                   // sorted by (seat, seq)
    std::vector<SealedSeen> sealed_seen_;
    std::array<SeqWindow, sim::MAX_PLAYERS> seen_;
    bool stale_hint_{false};                       // a hint is why it went stale (the statistics)
    uint64_t last_order_tick_{0};
    uint32_t last_order_seq_{0};
    uint32_t next_seq_{1};                        // the number of the next order (never reset, not by a suspension and not by a new prediction in the same match: an answer to an order of a prediction that was off since matches no order of the next one)
    struct Planned {
        sim::Command command;
        OrderPlan plan;
    };
    std::optional<Planned> planned_;              // the number that plan_order() took and submit() has not used yet
    uint32_t take_seq() noexcept;
    std::deque<Sent> sent_;
    uint32_t lead_{3};
    uint32_t target_lead_{3};
    uint32_t estimate_lag_{2};                    // the owner's estimate of the lag of an order (set_expected_delay_ms), in ticks
    std::deque<std::pair<uint32_t, uint64_t>> lags_;      // (lag in ticks, the confirmed tick at which it was seen) of the last orders that came back in a turn
    uint32_t lead_high_ticks_{0};
    uint32_t lead_fall_wait_{0};                  // ticks until the lead may fall by another tick
    uint64_t stopped_at_{0};                      // the confirmed tick at which the prediction stopped: its shifts are about the picture of that moment
    uint32_t generation_{0};
    bool measure_corrections_{false};
    std::vector<PredictedAudio> audio_;
    std::vector<PredictedNews> news_;
    std::vector<Shift> shifts_;
    Stats stats_;
};

}  // namespace ants::net
