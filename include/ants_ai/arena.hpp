#pragma once

// The match runner of the headless bot arena (tools/bot_arena.cpp, the tests of tests/test_ai, later the tournaments and the server): one match, played start to end with the
// real engine and the BotController, no window, no sound, no network. It is pure: no threads, no clock, no files, no global state (so any number of matches may run at once
// in different threads, and the same arguments always give the same match, bit for bit); what the tool adds is the command line, the threads, the wall clock and the report.
//
// A match is a map, a seed and bots on seats. The engine is initialised for the roster of the seats that have a bot (a seat without one has no hill and no ants), the
// controller plays each bot, and every tick the news and audio queues are emptied (nobody reads them here and they would grow without bound; ArenaResult counts what they held).
// A map that LevelData::validate(roster) says the seated teams cannot play (a start marker outside the grid) is refused, as the server and the application refuse it.
//
// How a command reaches the engine is the SINK LATENCY: 0 applies a command the moment the controller releases it (a local game); more than 0 mimics a lock-step room
// (docs/NETWORK_PORT.md): a command released at tick t is applied at the first turn boundary (every 2nd tick, a turn is 100 ms) that is at least `latency_ticks` later,
// in canonical order (by issuer, each issuer's commands in the order they were released), before the bots look again. Contested results move by up to 15 percent per seat
// between latency 0 and 3, which is why the default is 3: a tournament should play like a room.
//
// The bots open like the bots of a real match (ArenaSpec::start_hold, by default the product's kStartHoldTicks: a first look on tick 1 + the seat, a bucket of one token). The game's "Get ready to play!"
// dialog costs the arena nothing: the match clock waits for it in the game (the simulation does not run while it is up), so a match here starts where the game's tick 0 is.
//
// Replay: with `record` on, every command is kept with the tick it was applied at and the engine's state hash after every 20th tick. replay_commands() feeds those commands
// into a FRESH engine without any bot and requires the same hash at every 20th tick and at the end: the proof that a bot match is nothing but its commands.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"

namespace ants::ai {

/// A command as the engine applied it
struct RecordedCommand {
    uint64_t step{0};                    // the number of SimulationEngine::tick() calls made when it was applied: it came after that call (the call that ends a match does not advance
                                         // current_tick(), so the tick count alone could not tell the last two apart)
    uint64_t tick{0};                    // the engine's tick count at that time (for people)
    sim::Command command;
};

/// How often the state hash is sampled (the lock-step check of a room uses the same period)
inline constexpr uint64_t kArenaHashPeriod = 20;

/// What the arena makes of the seats' scores (ArenaSeatResult::banked and ::raided): between two ticks a rise of a seat's score is points BANKED at its hill (the deposits of food, and of
/// loot a thief brought home) and a fall is points taken by a raid. The price of an egg is neither: the seat's hatch counter rose since the last sample, so the engine charged it
/// min(score, 200) (HATCH_COST_POINTS: a hatch command, applied right after the sample, or the egg the engine starts by itself for a team that lost its last ant) and that price is taken
/// off before the change is judged (a deposit in the same tick is still a deposit). The engine's own food counters are never fed, so this is the one place these numbers come from.
class ScoreLedger {
public:
    void start(const sim::SimulationEngine& sim);
    /// After every tick (and after the commands of the turn were applied, which is when a hatch is charged: the next tick's sample sees its price)
    void sample(const sim::SimulationEngine& sim);
    uint32_t banked(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS ? banked_[team] : 0u; }
    uint32_t raided(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS ? raided_[team] : 0u; }

private:
    std::array<int32_t, sim::MAX_PLAYERS> last_{};
    std::array<uint32_t, sim::MAX_PLAYERS> hatched_{};
    std::array<uint32_t, sim::MAX_PLAYERS> banked_{};
    std::array<uint32_t, sim::MAX_PLAYERS> raided_{};
};

struct ArenaSpec {
    const assets::LevelData* level{nullptr};   // the map (must outlive the call)
    uint32_t seed{1};                          // the engine's seed AND the controller's match seed
    std::vector<BotSpec> bots;                 // the seats that play: one bot each, seat 0 to 3, every seat once
    uint64_t max_ticks{0};                     // 0 = until the match is over (the map's own length); else at most this many ticks
    uint32_t latency_ticks{3};                 // the sink latency, see the top of this file
    /// The start hold of the bots (BotController::set_start_hold): the opening of a real match, a first look on tick 1 + the seat and a bucket of one token. The default is the product's
    /// (kStartHoldTicks), because an arena match must be the real one; 0 is the opening of v0.1.0 (a full bucket), for the tests that measure something else from tick 0 (each says so).
    uint32_t start_hold{kStartHoldTicks};
    bool record{false};                        // keep the applied commands (for replay_commands)
    /// A bot that is not in the registry (tests, later the tournaments): called instead of make_bot when set; a null result refuses the match. The `kind` of the BotSpec must
    /// still be a name that make_bot knows (check_setup), and the Profile comes from the spec's level.
    std::function<std::unique_ptr<Bot>(const BotSpec&)> factory;
    /// Kinds besides the registry's that `factory` supplies (the bench bots of tools/bot_arena.cpp, never a bot of the game): the setup check accepts them
    std::vector<std::string> extra_kinds;
    /// Called once with the engine as the match ended (a test hook: the tests compare the result with the engine's own getters). Read-only; the engine is gone after play_match.
    std::function<void(const sim::SimulationEngine&)> inspect;
};

struct ArenaSeatResult {
    BotSpec spec;                              // what was asked for
    std::string runs;                          // the kind of bot that actually played ("worker" for "worker" and for "standard", which is an alias of it until B4)
    int32_t score{0};                          // the individual score at the end (what the results screen ranks by, before ties)
    int32_t shown_score{0};                    // the number of the score box (own plus ally's)
    uint32_t ants{0};                          // living ants at the end
    uint32_t eggs{0};
    uint32_t hatched{0};
    /// Points credited to the seat at its hill over the whole match: the deposits of food, and of loot that a thief of the seat carried home. Counted by the arena (ScoreLedger) as the
    /// rises of the seat's score between two ticks, with the price of an egg (min(score, 200)) taken off first. (The engine's own PlayerMatchStats::food_deposited, food_stolen and
    /// food_lost are never fed by the simulation: they are 0 in every match, so they cannot be reported.)
    uint32_t banked{0};
    /// Points taken from the seat by raids on its hill (the falls of its score between two ticks)
    uint32_t raided{0};
    uint32_t kills{0};
    uint32_t losses{0};
    BotController::SeatStats stats;
    /// commands released per second of game time, in thousandths
    uint32_t milli_commands_per_second(uint64_t ticks) const noexcept {
        return ticks == 0 ? 0u : static_cast<uint32_t>(static_cast<uint64_t>(stats.released) * 20000u / ticks);
    }
};

struct ArenaResult {
    std::string error;                         // not empty: the match was refused or could not be set up (nothing else is valid)
    uint64_t ticks{0};                         // ticks played (the engine's tick count at the end)
    uint64_t steps{0};                         // calls of SimulationEngine::tick(): ticks, plus one for the call that ended the match
    bool match_over{false};                    // the engine declared the match over (else max_ticks ended it)
    uint64_t initial_ticks{0};                 // the length of the match on this map, in ticks
    uint64_t hash{0};                          // the engine's state hash after the last tick
    /// Units still lying on the piles that the hill of some playing seat can walk to (MapInfo's analysis of the start), the object whose units a bite takes counted once: 0 when
    /// the whole reachable pot was taken
    uint32_t reachable_units_left{0};
    std::vector<ArenaSeatResult> seats;        // in seat order
    std::vector<uint64_t> checkpoints;         // the state hash after tick 20, 40, 60, ... (while the match lasted)
    std::vector<RecordedCommand> log;          // with ArenaSpec::record
    uint64_t news_events{0};                   // the news items the engine posted during the match (the arena empties the queue after every tick and counts what it held)
    uint64_t audio_events{0};                  // the same for the audio queue
    uint32_t peak_queue{0};                    // the most items either queue held when the arena emptied it: one tick's worth, since it is emptied every tick (a queue that was
                                               // not emptied would hold the whole match)
};

/// Plays one match. Never throws for a bad spec: `error` says why nothing was played.
ArenaResult play_match(const ArenaSpec& spec);

/// Reads the result fields of one seat from the engine as it stands: score, shown_score, ants (living), eggs, hatched, kills (enemy_killed) and losses (friendly_lost). What play_match
/// does at the end of a match, exposed so that a test can hold every field of the mapping against values it set on a hand-made engine. (spec, runs, banked, raided and stats are
/// not the engine's and stay as they were.)
void read_seat_result(const sim::SimulationEngine& sim, uint8_t seat, ArenaSeatResult& out);

struct ReplayResult {
    bool ok{false};
    uint64_t first_bad_tick{0};                // 0 when ok; else the first checkpoint (or the last tick) at which the hash differed
    uint64_t hash{0};                          // the hash after the last replayed tick
    std::string error;
};

/// Re-plays `played.log` into a fresh engine with no bot at all and compares the state hash at every 20th tick and after the last tick. `spec` and `played` must be the
/// arguments and the result of a play_match with `record` on.
ReplayResult replay_commands(const ArenaSpec& spec, const ArenaResult& played);

}  // namespace ants::ai
