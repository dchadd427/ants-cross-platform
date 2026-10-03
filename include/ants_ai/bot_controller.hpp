#pragma once

// The controller owns the bots of one game and is called after EVERY simulation tick (a local game: after sim.tick(); a lock-step room: from the runner's
// tick hook). It runs each bot when it is due, and stands between a bot and the door a person uses:
//
//   the opening     the first look of a seat is on the first tick of the simulation (kStartHoldTicks = 1) + its stagger, and the bucket starts with ONE token. The "Get ready to play!" dialog needs no
//                   hold of the bots: the simulation does not run while it is up (the match clock waits for it), so no bot looks or acts while it is up (set_start_hold). A person on the far side
//                   of a link starts the link's delay plus the jitter buffer later than the machine that runs the bots (a LAN host, the server's referee): measured 20 - 290 ms, not seconds
//   schedule        a bot thinks every Profile::decision_interval ticks, the first time on the first tick of the hold + seat (the seats do not all think on the same tick)
//   reaction delay  every command of a decision is released Profile::reaction_delay ticks after the decision, plus or minus 25 percent drawn ONCE per decision
//                   from the seat's own generator (so the commands of one look leave in the order they were proposed, and a later look never leaves before an earlier one)
//   newest wins     among the orders that are due, an ant that a newer order names leaves every older one (like the later click of a person); an older order that
//                   is left with no ant never leaves (Fate::Superseded). An order that is not due yet takes nothing from the ones before it.
//   budget          a token bucket in thousandths of a command (one token per released command, whatever the number of ants): Profile::rate_milli_cps
//                   refills it, Profile::burst is its depth, a seat starts with ONE token (with the start hold, which every product path has: no burst of banked orders at the
//                   start); a command that cannot be paid waits and is dropped after Profile::intent_ttl ticks
//   priorities      Urgent before Normal before Background, first come first served inside a class
//   ants            ants that died since the decision are dropped from a command (a command whose ants all died never leaves); at most
//                   min(Profile::max_ants_per_command, kHudAntCap) ants go into one command, a larger one is split and every part is paid for
//   HUD parity      nothing a person could not click: no Quit, no Drop, no None, a special order names exactly one ant, no group order outside the map, no
//                   attack on the tile of an ally, alliance commands name another seat that is in the match and has not dropped out, a break needs an ally,
//                   an answer needs the invitation it answers, a withdrawal needs the offer it takes back; an ATTACK needs an ant of another team on its tile
//                   (the attack cursor shows over an enemy ant, and an ant on a hill tile gets the plain move cursor); a click on a power-up tile takes the power-up
//                   for the ant that arrives, so a move onto one passes only as a planned pick-up (Orders::pick_up: one ant), and no special order or attack names one
//   issuer          every released command carries the seat of the bot, whatever the bot wrote
//   anti-thrash     no second order to the same ant within Profile::reissue_cooldown ticks, unless it is Urgent (every order snaps the ant to its tile
//                   centre and replaces its queued path request)
//
// The controller changes nothing in the simulation by itself: a bot that only reads leaves every state hash as it was. It has no thread, no clock and no
// random source but the seat's BotRng, so a match with bots is reproducible from the match seed.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/rng.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

/// The bots' start hold in ticks of the simulation, the product's value (BotController::set_start_hold's default, the arena's and every path's): 1, the first tick. Since v0.2.0 the match clock
/// WAITS for the "Get ready to play!" dialog (the simulation does not tick while it is up, sim::kMatchStartDialogMs), so a person can select and order from the first tick and the bots need no
/// wait behind the dialog any more (v0.1.1 held them for 100 ticks, the dialog's length, while the simulation ran): a bot looks from tick 1 + its stagger, its orders leave after its reaction time,
/// and what the hold still gives is the human-like opening, a bucket of ONE token with no burst of banked orders. The dialog's own 5 s are sim::kMatchStartDialogMs (the HUD and the hosts' start
/// delay), a different number for a different thing.
inline constexpr uint32_t kStartHoldTicks = 1;

class BotController {
public:
    /// What happened to the commands of one seat (every counter counts commands after splitting, except `decisions`)
    struct SeatStats {
        uint32_t decisions{0};           // times the bot thought
        uint32_t intents{0};             // commands that passed the filter and were queued (an intent of more ants than one command holds counts every part)
        uint32_t released{0};            // commands handed to the sink
        uint32_t expired{0};             // dropped because they could not be paid within the time to live
        uint32_t pruned{0};              // commands that lost ants that died (a command with no ant left is not released at all)
        uint32_t superseded{0};          // commands that were due and lost every ant to a newer due order for the same ants: never released
        uint32_t filtered{0};            // refused when proposed: Quit, Drop, None, an empty or oversized ant list, a special order of other than one ant, ...
        uint32_t rejected{0};            // released, and the sink answered RejectedIssuer / RejectedMalformed / RejectedNotAllowed
    };

    BotController(const sim::SimulationEngine& sim, uint64_t match_seed);
    ~BotController();
    BotController(const BotController&) = delete;
    BotController& operator=(const BotController&) = delete;

    /// Seats a bot. `sink` is where the bot's commands go (it must outlive the controller): the simulation of a local game, the sequencer of a room. False
    /// with the reason in `error` when the seat is not in the match, has a bot already, the kind does not exist, or Fog of War is on (a bot would see through it).
    bool add(const BotSpec& spec, sim::CommandSink& sink, std::string& error);
    /// The same for a bot that is not in the registry (the tests' scripted bots; later the arena): `spec` supplies the seat and the level (the profile)
    bool add(const BotSpec& spec, std::unique_ptr<Bot> bot, sim::CommandSink& sink, std::string& error);

    /// After every simulation tick. Does nothing once the match is over, and nothing for a seat whose team has dropped out.
    void on_tick(const sim::SimulationEngine& sim);

    /// THE START HOLD (docs/BOTS.md "How fast a bot acts"). `ticks` is an ABSOLUTE tick of the match (SimulationEngine::current_tick()): the first tick on which a seat may look, release a command
    /// and refill its bucket. (1) A seat's first look is on tick max(now + 1, ticks) + its stagger: it never acts on a world that it saw earlier. (2) No command is released before that tick (every
    /// release time is at least it: defence in depth, no path may leave one earlier). (3) The bucket does not fill before it, and a seat starts with ONE token, so the first orders come one by one at
    /// the level's rate where a full bucket was a burst of up to ten in one tick. The default is kStartHoldTicks (1) in every product path (a local game, a LAN host, a room of the server, the arena):
    /// the match clock waits for the "Get ready to play!" dialog, so a person can order from the first tick and the bots look on it too (v0.1.1 held them for the dialog's 100 ticks, while the
    /// simulation ran behind it). A seat that is seated after the hold's first tick has no hold (its first look is on the next tick + its stagger) and starts with one token as well. Call it BEFORE
    /// seating the bots: it shapes the first look and the first bucket of the seats that come after it (the clamp of (2) follows the value at any time). 0 switches it off, and with it the rest of
    /// the above: the opening of v0.1.0 (a first look on tick 1 + the stagger, a FULL bucket, a refill from the first tick). It is for the tests that measure something else from tick 0 (the reaction
    /// delay, the budget, the filter, failure learning, the endgame), each of which says so; the opening has its own tests, AI2.17 .. AI2.20 (the controller), AI6.9 / AI6.10 (the application) and
    /// S3.76 (a server's room).
    void set_start_hold(uint32_t ticks) noexcept { hold_end_ = ticks; }
    uint32_t start_hold() const noexcept { return hold_end_; }

    bool has_seat(uint8_t seat) const noexcept;
    /// The seats that have a bot, bit s = seat s
    uint8_t seat_mask() const noexcept;
    /// All zero for a seat without a bot
    const SeatStats& stats(uint8_t seat) const noexcept;
    /// Commands of the seat that wait for their time or for a token
    size_t pending(uint8_t seat) const noexcept;
    /// The analysis of the match's map, made when the controller was created (the start of the match); the bots get it through BotContext and BotView::map()
    const MapInfo& map() const noexcept { return map_; }
    /// The bot of a seat (null when none): for the tests
    Bot* bot(uint8_t seat) noexcept;
    const Profile* profile(uint8_t seat) const noexcept;

private:
    struct Pending {
        sim::Command command;
        Priority priority{Priority::Normal};
        uint64_t release{0};             // the tick from which it may leave
        uint64_t expires{0};             // dropped when it is still here after this tick
        uint64_t seq{0};                 // the order in which the seat proposed it
        bool trimmed{false};
    };
    struct Seat {
        uint8_t seat{0};
        BotSpec spec;
        std::unique_ptr<Bot> bot;
        Profile profile;
        sim::CommandSink* sink{nullptr};
        BotRng rng;
        int64_t tokens_milli{0};
        uint32_t refill_carry{0};
        uint64_t next_decision{1};
        uint64_t next_seq{0};
        uint64_t last_release{0};                       // the release tick of the seat's latest decision: a later look never leaves before an earlier one
        std::vector<Pending> queue;
        std::map<uint32_t, uint64_t> last_order;        // ant -> tick of the last order released for it
        SeatStats stats;
    };

    Seat* find(uint8_t seat) noexcept;
    const Seat* find(uint8_t seat) const noexcept;
    void decide(const sim::SimulationEngine& sim, Seat& s, uint64_t tick);
    bool allowed(const sim::SimulationEngine& sim, const Seat& s, const Intent& in) const;
    void refill(Seat& s, uint64_t tick) const;
    void release(const sim::SimulationEngine& sim, Seat& s, uint64_t tick);

    const sim::SimulationEngine* sim_;
    uint64_t match_seed_;
    uint32_t hold_end_{kStartHoldTicks};                // the start hold: the first tick on which a seat acts (an absolute tick of the match), see set_start_hold
    MapInfo map_;
    std::vector<std::unique_ptr<Seat>> seats_;          // by seat number, so the order of the --bot options never matters
};

}  // namespace ants::ai
