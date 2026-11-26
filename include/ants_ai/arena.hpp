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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/start_teams.hpp"

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

/// What the arena counts of the "Can't go there." reactions (docs/BOT_PLAY.md, "The can't-go loop"): per seat and match, from what a person sees of it; counted after every tick, once the tick's
/// commands were applied. The refused orders are an indicator that the tests bound, not a proof of cause (an ant that walks on from an earlier order and meets a wall is counted too).
class CantGoTally {
public:
    static constexpr uint64_t kRefusedWindow = 10;
    struct Seat {
        uint32_t reactions{0};                 // the news items "Can't go there." and "Can't do that..." posted to the seat: everything its owner hears
        uint32_t began{0};                     // of them, those of an ant that was not in the can't-go state a tick before (the rest repeat: the original's loop)
        uint32_t orders{0};                    // the group orders (move, special, attack) for the seat, as the engine was given them
        uint32_t attacks{0};                   // of them, the attack orders
        uint32_t bridge_orders{0};             // of them, the special orders onto a water tile with no bridge on it (a Swimmer's dig: one order is one tile)
        uint32_t refused{0};                   // of them, those followed within kRefusedWindow ticks by the first reaction of an ant they name (the engine answers a walk within 4)
        uint32_t again{0};                     // of the refused ones, those that give the same kind of order to the same first ant onto the same tile as an order that was refused before (docs/BOT_UNSTUCK.md)
    };
    /// A command that the engine was given when its tick count was `tick`: the ants it names are remembered
    void command(const sim::Command& c, uint64_t tick);
    /// A special order of the seat onto a water tile that holds no bridge (the arena asks the grid: the tally has none)
    void bridge_order(uint8_t seat) noexcept { ++seats_[seat < sim::MAX_PLAYERS ? seat : 0].bridge_orders; }
    /// After the tick's commands were applied: counts `news` (the tick's news items) and the ants that entered the can't-go state (looked at only when one can have)
    void scan(const sim::SimulationEngine& sim, const std::vector<sim::NewsEvent>& news);
    const Seat& seat(uint8_t s) const noexcept { return seats_[s < sim::MAX_PLAYERS ? s : 0]; }
    /// The ticks at which the ants were looked at (the tests: a match without a can't-go reaction looks at none)
    uint64_t scans() const noexcept { return scans_; }

private:
    struct Last {
        uint32_t order{0};                     // the number of the last order that named the ant (0: none)
        uint64_t tick{0};
    };
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
    std::vector<uint8_t> in_cantgo_;           // by ant id: the ant showed can't-go at the last scan
    uint32_t flagged_{0};                      // how many ants showed it at the last scan
    uint64_t scans_{0};
    std::vector<Last> last_;                   // by ant id
    std::vector<uint8_t> refused_;             // by order number: counted as refused already
    std::vector<uint8_t> order_seat_;          // by order number: the seat it was for
    std::vector<uint64_t> order_key_;          // by order number: the first ant, the kind of order and the tile
    std::map<uint64_t, uint32_t> refused_keys_;   // the keys of the orders that were refused, and how often
};

/// What the arena counts of the flowers (docs/BOT_TACTICS.md, "The flowers"): per match `landings` (the end of every droplet's fall; a landing replaces an untaken power-up) and `landed` (them by the
/// kind of ant they make); per seat `took` (the ants that changed type, by the kind they became) and `took_at_flowers` (those that stood within one tile of a drop tile). The arena scans every 4th tick and once when the match ends.
class FlowerTally {
public:
    struct Seat {
        std::array<uint32_t, 6> took{};        // by sim::AntType of the new type: 1 Bomber, 2 Fire, 3 Thief, 4 Combat, 5 Swimmer (index 0 is never used)
        uint32_t at_flowers{0};
    };
    void scan(const sim::SimulationEngine& sim);
    uint32_t landings() const noexcept { return landings_; }
    uint32_t landed(sim::AntType kind) const noexcept { return static_cast<size_t>(kind) < landed_.size() ? landed_[static_cast<size_t>(kind)] : 0u; }
    const Seat& seat(uint8_t s) const noexcept { return seats_[s < sim::MAX_PLAYERS ? s : 0]; }

private:
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
    uint32_t landings_{0};
    std::array<uint32_t, 6> landed_{};
    std::vector<uint8_t> dropping_;            // by dropper: it dropped at the last scan
    std::vector<uint8_t> raw_type_;            // by ant id: the ant's own type at the last scan, 255 before it was seen
};

/// The power-ups that dead typed ants leave (the arena's measure of whether a team takes its own drop back): from the looks the flower tally makes (every 4 ticks), a typed ant that is gone
/// and a power-up that stands on a free neighbour of its last tile are one drop; the drop ends when its tile is empty again, and the ant that changed type beside it is the taker.
class DropTally {
public:
    struct Drop {
        uint8_t owner{0};                      // the team of the dead ant
        uint8_t kind{0};                       // sim::AntType number of the power-up
        int32_t x{0}, y{0};
        uint64_t tick{0};                      // the look at which the drop was seen
        uint64_t taken{0};                     // the look at which its tile was empty again (0: still there at the end)
        uint8_t taker{255};                    // the team of the ant that became typed beside it (255: none seen)
        uint8_t others{0};                     // the owner's other living ants of the kind when the drop was seen
        int32_t hill_dist{0};                  // tiles (Chebyshev) from the owner's start marker
        uint8_t nearest{255};                  // the team whose start marker is nearest to the drop (255: a tie)
    };
    void scan(const sim::SimulationEngine& sim);
    const std::vector<Drop>& drops() const noexcept { return drops_; }

private:
    struct Typed {
        uint8_t team{0};
        uint8_t type{0};
        int32_t x{0}, y{0};
    };
    std::vector<Drop> drops_;
    std::vector<size_t> open_;                 // indexes into drops_ that lie on the map
    std::vector<Typed> typed_;                 // by ant id: the ant's own type at the last look (type 0: a worker or not seen)
    std::vector<uint8_t> raw_type_;            // by ant id, 255 before it was seen
};

struct ArenaSpec {
    const assets::LevelData* level{nullptr};   // the map (must outlive the call)
    uint32_t seed{1};                          // the engine's seed AND the controller's match seed
    std::vector<BotSpec> bots;                 // the seats that play: one bot each, seat 0 to 3, every seat once
    sim::GameMode game_mode{sim::GameMode::HighestScore};   // the rules of the match (set before init, like a room's Start does): Kills187 plays the game type 187
    sim::StartTeams teams{};                   // the teams the match starts with: apply_start_teams right after init, as every start does (a room, a local game); free for all by default
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
    /// Called after every tick, once the bots have looked and the news and audio queues are empty (a test hook, like `inspect`; read-only)
    std::function<void(const sim::SimulationEngine&)> on_tick;
};

/// What the arena counts of the ants that stand still, and of the endgame (docs/BOT_TACTICS.md, "When the food is gone"): per seat, from the engine's own state, looked at every 4th tick (each look
/// counts 4). The yardstick is `bot_watch`'s: an ant is idle when it stands in an idle clip with no order, no waypoint and nothing in its hand, and has done so for kIdleTicks (5 s). The
/// last food is the last look at which the units on the map's piles fell (a bite, from any pile, a lunchbox too): `after_*` count the ants and the idle ones from that look to the end
/// (the whole match when no unit was ever taken). A raid is counted when a Thief's target becomes a team.
class IdleTally {
public:
    static constexpr uint64_t kIdleTicks = 100;
    static constexpr uint64_t kIdleLongTicks = 200;           // the second yardstick: an ant that has stood idle for 10 seconds (docs/BOT_IDLE.md)
    static constexpr uint64_t kPeriod = 4;
    struct Seat {
        uint64_t ant_ticks{0};                 // the living ants of the seat, in ticks
        uint64_t idle_ticks{0};                // ... of them, the idle ones
        uint64_t after_ant_ticks{0};           // the same two, from the last food taken to the end
        uint64_t after_idle_ticks{0};
        uint64_t idle_long_ticks{0};           // the same two numbers for the ants that stood idle for 10 seconds (kIdleLongTicks)
        uint64_t after_idle_long_ticks{0};
        uint32_t raids{0};                     // raids begun by the seat's Thieves
        uint32_t raid_points{0};               // ... and what they took (the raid_amount of the Thief when it began)
        uint32_t steals{0};                    // ... the Thieves that carried loot to the hill's entrance (a steal completed), and the points they banked
        uint32_t steal_points{0};
    };
    void scan(sim::SimulationEngine& sim);
    const Seat& seat(uint8_t s) const noexcept { return seats_[s < sim::MAX_PLAYERS ? s : 0]; }
    /// The last look at which the food on the map fell (0: it never did)
    uint64_t last_food() const noexcept { return last_food_; }

private:
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
    std::vector<uint64_t> idle_since_;         // by ant id: the tick at which the ant began to stand idle (0: it does not)
    std::vector<uint8_t> target_team_;         // by ant id: the Thief's target team at the last look (255: none)
    std::vector<uint16_t> loot_;               // by ant id: the loot that a Thief held at the last look (0: none)
    uint64_t units_{0};                        // the units on all piles at the last look
    uint64_t last_food_{0};
    bool started_{false};
};

/// What the arena counts of the ants that are stuck (docs/BOT_UNSTUCK.md): per seat, from the engine's own state, looked at every 4th tick. An ant is STUCK when, for kStuckTicks (10 seconds) in a
/// row, it has stood on one tile and was in a state that is going nowhere: it shows the can't-go clip, or it has an order (a walk, a pick-up, a harvest) and does not move, or it holds food or
/// loot and has no order at all and stands more than 8 tiles from its own mound (the carriers that wait for the gate stand about it: a wait of 700 ticks is legitimate). An ant in a clip of its own work (a raid, a plant, a fire wall, a pick-up, a bite, the hill's entrance) or queueing for its hill (home_state) is not. The count is
/// one for each ant and span, the ticks are the ant-ticks of the span past the 10 seconds; the first kMaxEpisodes spans are kept with what the ant was doing, to look at (a test hook, like the rest).
class StuckTally {
public:
    static constexpr uint64_t kStuckTicks = 200;
    static constexpr uint64_t kPeriod = 4;
    static constexpr size_t kMaxEpisodes = 64;
    struct Seat {
        uint32_t ants{0};                      // the spans that reached kStuckTicks (one for each ant and span)
        uint64_t ticks{0};                     // the ant-ticks of those spans past the first kStuckTicks
        uint32_t looping{0};                   // of the spans, the ones of an ant that showed the can't-go clip when the span reached kStuckTicks
        uint32_t blocked{0};                   // ... that had an order and did not move
        uint32_t carriers{0};                  // ... that held food or loot and had no order: a carrier that the engine's walk home left standing (the rescue of docs/BOTS.md sends it after 900 ticks)
    };
    struct Episode {
        uint64_t tick{0};                      // the look at which the span reached kStuckTicks
        uint8_t seat{0};
        uint32_t ant{0};
        sim::AntType type{sim::AntType::Worker};
        int32_t x{0}, y{0};
        sim::UnitState state{sim::UnitState::Idle};
        uint8_t order{0};                      // AntUnit::kOrder*, 0 none
        int32_t order_x{0}, order_y{0};
        bool holding{false};
        int32_t carried{0};
    };
    void scan(sim::SimulationEngine& sim);
    const Seat& seat(uint8_t s) const noexcept { return seats_[s < sim::MAX_PLAYERS ? s : 0]; }
    const std::vector<Episode>& episodes() const noexcept { return episodes_; }

private:
    struct Track {
        int32_t x{-1}, y{-1};
        uint64_t since{0};                     // the look at which the ant came to this tile and was in a going-nowhere state, 0: not now
        bool counted{false};
    };
    std::array<Seat, sim::MAX_PLAYERS> seats_{};
    std::vector<Track> tracks_;                // by ant id
    std::vector<Episode> episodes_;
};

class ExpeditionTask;
class MinerFlightTask;

/// What a standard bot's expedition over water did (ExpeditionTask, docs/BOT_ISLANDS.md "Islands"); the ticks are 0 for what never happened, and everything is 0 for a seat that has no standard bot
struct ExpeditionResult {
    uint32_t planned{0};                       // expeditions planned (one per attempt)
    uint32_t given_up{0};                      // ... of them, given up for want of progress or of a crew
    uint32_t planted{0};                       // bombs planted
    uint32_t hops{0};                          // hops ordered (an ant clicked onto a bomb)
    uint32_t landings{0};                      // landings seen (a crew ant on a later island of the route)
    uint32_t duds{0};                          // bombs seen to be duds (an ant burning on B)
    uint32_t taken{0};                         // tokens of a row taken by the crew
    uint32_t swimmers_taken{0};                // ... of them, Swimmers
    uint64_t first_plant{0};
    uint64_t first_landing{0};
    uint64_t first_swimmer{0};
};

inline bool operator==(const ExpeditionResult& a, const ExpeditionResult& b) noexcept {
    return a.planned == b.planned && a.given_up == b.given_up && a.planted == b.planted && a.hops == b.hops && a.landings == b.landings && a.duds == b.duds && a.taken == b.taken &&
           a.swimmers_taken == b.swimmers_taken && a.first_plant == b.first_plant && a.first_landing == b.first_landing && a.first_swimmer == b.first_swimmer;
}

/// What the tasks of the idle batch did (docs/BOT_IDLE.md); everything is 0 for a seat that has no standard bot or no work for the task
struct IdleBatchResult {
    uint32_t headon_jams{0};                   // pairs of ants face to face on a bridge for 3 seconds, with an own ant (BridgeHeadOnTask::jams), and the ants that stepped back
    uint32_t headon_backoffs{0};
    uint32_t contact_jobs{0};                  // single ants that struck at an enemy beside them (ContactTask::started), and their blows
    uint32_t contact_blows{0};
    uint32_t meets{0};                         // enemy Thieves that the idle ants met at the own hole (ThiefGuardTask::meets_started), and the blows they struck
    uint32_t meet_strikes{0};
    uint32_t holds{0};                         // posts taken in front of the own thief hole (IdleJobsTask::holds) and hurt ants sent home to heal
    uint32_t heals{0};
    uint32_t swarm_thieves{0};                 // ants that became a Thief at a strip while they waited there (SwarmTask::thieves)
    uint32_t homed{0};                         // Thieves sent home before their first raid (RaidTask::homed)
    uint32_t raid_tiles{0};                    // water tiles that Swimmers were told to dig for a Thief's chain, and for a Fire Ant's way home (BridgePlanner::tiles_ordered)
    uint32_t home_tiles{0};
};

inline bool operator==(const IdleBatchResult& a, const IdleBatchResult& b) noexcept {
    return a.headon_jams == b.headon_jams && a.headon_backoffs == b.headon_backoffs && a.contact_jobs == b.contact_jobs && a.contact_blows == b.contact_blows && a.meets == b.meets &&
           a.meet_strikes == b.meet_strikes && a.holds == b.holds && a.heals == b.heals && a.swarm_thieves == b.swarm_thieves && a.homed == b.homed && a.raid_tiles == b.raid_tiles &&
           a.home_tiles == b.home_tiles;
}

/// What the rules of the fight batch did (part 2 of G9, docs/BOT_CONTEST.md; format 10); everything is 0 for a seat that has no standard bot or no work for the rule
struct FightBatchResult {
    uint32_t foot_plugs{0};                    // own ants that stood at the foot of a bridge in the way of another and stepped off it (BridgeHeadOnTask::plugs), with a load or in the can't-go clip or not
    uint32_t loads_released{0};                // ants that held a load and were given back by the island guard after a long hold (IslandTask::loads_released)
    uint32_t return_tiles{0};                  // water tiles that Swimmers were told to dig for an ant that stands away from the hill (BridgePlanner::tiles_ordered(Return))
};

inline bool operator==(const FightBatchResult& a, const FightBatchResult& b) noexcept {
    return a.foot_plugs == b.foot_plugs && a.loads_released == b.loads_released && a.return_tiles == b.return_tiles;
}

/// What a standard bot's Bomber flight to the island of the ring pile did (MinerFlightTask); the ticks are 0 for what never happened
struct MinerFlightResult {
    uint32_t started{0};
    uint32_t planted{0};
    uint32_t hops{0};
    uint32_t landed{0};
    uint32_t gave_up{0};
    uint64_t first_start{0};
    uint64_t first_landing{0};
};

inline bool operator==(const MinerFlightResult& a, const MinerFlightResult& b) noexcept {
    return a.started == b.started && a.planted == b.planted && a.hops == b.hops && a.landed == b.landed && a.gave_up == b.gave_up && a.first_start == b.first_start && a.first_landing == b.first_landing;
}

MinerFlightResult read_miner_flight(const MinerFlightTask& task) noexcept;

/// The counters of an expedition as the arena reports them
ExpeditionResult read_expedition(const ExpeditionTask& expedition) noexcept;

struct ArenaSeatResult {
    BotSpec spec;                              // what was asked for
    std::string style;                         // the style that the standard bot played (the pinned one or the one it drew: "aggressive", ...); "" for the other kinds
    std::string runs;                          // the kind of bot that actually played: the bot's own kind() ("standard", "worker", "idle", or a bench bot of the tournaments)
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
    uint32_t stalls{0};                        // times a standard bot's stall detector sent it to the plain economy (StandardBot::stalls); 0 for the other kinds
    uint32_t cantgo{0};                        // the "Can't go there." / "Can't do that..." reactions of the seat's ants (CantGoTally::Seat::reactions)
    uint32_t cantgo_began{0};                  // of them, the ones that began from another state: the rest are the repeats of a can't-go loop
    uint32_t orders{0};                        // the orders (group moves, specials, attacks) that the engine was given for the seat
    uint32_t attack_orders{0};                 // of them, the attack orders (CantGoTally::Seat::attacks)
    uint32_t bombs_planted{0};                 // the bombs the seat's Bombers planted and the fire walls its Fire Ants lit (the engine's own match statistics)
    uint32_t fires_lit{0};
    uint32_t bridge_orders{0};                 // the special orders onto a water tile with no bridge (CantGoTally::Seat::bridge_orders): the bridge tiles the seat's Swimmers were told to dig
    std::array<uint32_t, 4> war_ticks{};       // the ticks a standard bot spent at every war tier (StandardBot::war_ticks: not behind enough, 1 .. 3)
    uint32_t fire_hunts{0};                    // the stand batch: hunts of an enemy Fire Ant that fired the gate in (FightTask::fire_hunts)
    uint32_t mines_planted{0};                 // ... the mines the Bombers laid (MineTask::planted)
    uint32_t ramp_unjams{0};                   // ... the ants sent aside off the ramp (GateTask::ramp_unjams)
    uint32_t rushes{0};                        // ... the rushes begun (RushTask::rushes_started),
    uint32_t rush_ticks{0};                    //     the ticks they lasted
    uint32_t rush_attacks{0};                  //     and the ants they ordered at an enemy ant (one count a member and order)
    uint32_t refused_orders{0};                // of them, the ones that were followed by a first reaction of an ant they named within CantGoTally::kRefusedWindow ticks
    uint32_t refused_again{0};                 // of the refused ones, those that repeat a refused order (CantGoTally::Seat::again)
    std::array<uint32_t, 6> took{};            // the power-ups that the seat's ants took, by the kind of ant they became (FlowerTally::Seat::took)
    uint32_t took_at_flowers{0};               // of them, the ones taken at a flower's drop tile
    uint64_t ant_ticks{0};                     // the idle tally (IdleTally): the seat's living ants in ticks, the ones that stood idle, the same two from the last food taken to the end,
    uint64_t idle_ticks{0};                    //     the raids its Thieves began and the points they took
    uint64_t after_ant_ticks{0};
    uint64_t after_idle_ticks{0};
    uint64_t idle_long_ticks{0};               //     the ants that stood idle for 10 seconds, over the match and from the last food on (IdleTally::kIdleLongTicks)
    uint64_t after_idle_long_ticks{0};
    uint32_t raids{0};
    uint32_t raid_points{0};
    uint32_t steals{0};                        // the steals completed (IdleTally::Seat::steals) and the points banked by them
    uint32_t steal_points{0};
    uint32_t stuck_ants{0};                    // the stuck tally (StuckTally): the spans of an ant that stood nowhere for 10 seconds, the ant-ticks past them, and the spans by what the ant was doing
    uint64_t stuck_ticks{0};
    uint32_t stuck_looping{0};
    uint32_t stuck_blocked{0};
    uint32_t stuck_carriers{0};
    ExpeditionResult expedition;
    MinerFlightResult miner_flight;
    IdleBatchResult idle_batch;
    FightBatchResult fight_batch;
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
    uint64_t last_food{0};                     // the last look at which the food on the map fell (IdleTally::last_food); 0: no unit was ever taken
    std::vector<DropTally::Drop> drops;        // the power-ups that dead typed ants left (DropTally)
    std::vector<StuckTally::Episode> stuck;    // the first StuckTally::kMaxEpisodes stuck spans of the match
    uint32_t landings{0};                      // the droplets of the flower droppers that landed (FlowerTally)
    std::array<uint32_t, 6> landed{};          // ... by the kind of ant they make (index = sim::AntType)
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
