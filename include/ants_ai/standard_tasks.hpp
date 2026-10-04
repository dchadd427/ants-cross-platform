#pragma once

// The tasks of the standard bot (standard_bot.hpp), built on the task model of tasks.hpp: each claims ants through the AntLedger (a task of a higher rank may take an ant from one of a
// lower rank), puts orders into Orders, reads the result from the next view and ends when the reason is gone. They share the Tactics (the level's plan and the bot's memory).
//
//   rank 5  FightTask     strike back at an enemy that hit an own ant, and attack an enemy Thief that is on its way to the own hill
//   rank 4  WallTask      the Fire Ant that keeps three fire walls in front of the own thief hole
//   rank 4  StrikeTask    a strike force hunts the carriers of the leading team when the bot is clearly behind and the fight looks winnable (the owner's playbook)
//   rank 3  PowerUpTask   the ants that are sent to take the power-ups that the bot wants; RaidTask: a Thief raids the hill of the leading team
//   rank 2  GuardTask     a Combat Ant is parked where its reflex covers the hill
//   rank 1  HarvestTask   the economy (tasks.hpp, unchanged)
//
// Every order a task gives is a click that a person could make: a plain move, an attack on the tile of an enemy ant, a special order of one ant (BotController filters the rest).

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

/// Whether an attack order can reach the ant now: it is not on a power-up (an ant that stands on one cannot be attacked, and one that is about to take it will not be there in a few
/// ticks), not on a hill tile (the order would be a plain move there), not on water, not in the middle of a hit, a flight, the enter clip or a burn
bool attackable(const BotView& view, const AntView& enemy);

/// Whether an own ant may be sent to fight: it takes orders, holds nothing, is healthy and is a worker (of the level's default type) or a Combat Ant, and does not stand on a power-up
bool can_fight(const BotView& view, const AntView& ant);

// ---- rank 5: fights ---------------------------------------------------------------------------------------------------------------------------------------

/// Fights, one per enemy ant:
///   a blow      an own ant lost hit points (Memory::hits) and an enemy ant that can be attacked stands within two tiles of it: that ant is the target. The nearest healthy ants that
///               hold nothing (Combat Ants first), as many as the level says, are taken from whatever they were doing and ordered to attack it, and ordered again each time they are
///               idle after their blow (one attack order is one blow), while the target stays within the leash of the place of the first blow.
///   a thief     (Medium and Hard) an enemy Thief ant that holds no loot and is within reach of the own raid tile: attacked the same way until it is gone or has raided.
/// A fight ends when the target dies, leaves sight or the leash, stands on a power-up, or no blow has been seen for the level's linger time; an attack that was acknowledged and then
/// shows "can't go" within a few ticks tells that the target is out of reach (it stands on something that no order reaches): that ant is left alone for 900 ticks. The ants go back
/// to the pool when the fight ends.
class FightTask final : public Task {
public:
    FightTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "fight"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t fights() const noexcept { return fights_.size(); }
    uint32_t fights_started() const noexcept { return fights_started_; }
    uint32_t attacks_ordered() const noexcept { return attacks_ordered_; }
    uint32_t shunned_count() const noexcept { return shunned_count_; }
    bool shunned(uint32_t enemy, uint64_t tick) const noexcept;
    /// The defenders of the fight against `enemy` (empty when there is none)
    std::vector<uint32_t> defenders_of(uint32_t enemy) const;

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Defender {
        uint64_t decided{0};
        uint64_t sent{kPending};             // the tick the last attack order LEFT (kPending: none, or it has not left yet); 0: never ordered
        bool ordered{false};
    };
    struct Fight {
        uint32_t target{0};
        bool thief{false};
        sim::TileCoord anchor{};             // the place of the first blow (the own hill for a thief): the leash is measured from here
        uint64_t started{0};
        uint64_t last_alarm{0};
        bool ally{false};                    // a blow on an ant of the ally: only ants within ally_help_radius of the target answer
        std::map<uint32_t, Defender> defenders;
    };

    void start_fights(TaskContext& context);
    void run_fight(TaskContext& context, Fight& fight, bool& end, std::vector<std::pair<sim::TileCoord, std::vector<uint32_t>>>& attacks);

    Tactics& tactics_;
    std::map<uint32_t, Fight> fights_;       // by the target's id
    std::map<uint32_t, uint64_t> shunned_;   // enemy ant -> the tick until which it is left alone (an attack on it showed "can't go")
    uint32_t fights_started_{0};
    uint32_t attacks_ordered_{0};
    uint32_t shunned_count_{0};
};

// ---- rank 3: power-ups --------------------------------------------------------------------------------------------------------------------------------------

/// Takes the power-ups that the bot wants (Tactics::wants: the Fire Ant of the walls, the own side's Fire, Bomber and Thief of the opening, the Thief of the raids, the Combat Ants of the guard), one ant for each, by a plain click of the
/// ant on the power-up's tile (Orders::pick_up): the ant takes it when its walk ends there, changes type for life, and plays 17 ticks of the pick-up clip in which it takes no order.
///   the ant       the closest idle (or walking to a pile) worker with empty hands and enough hit points, by the walking cost from ITS OWN tile (one search from the power-up serves
///                 every ant: MapInfo::cost_field_onto); it is taken from the economy (AntLedger::take) for the trip
///   contest       a trip is not started when an enemy ant stands closer than the own ant (its arrival, at the engine's walking speed, is no later than the own order's plus walk);
///                 a power-up that an enemy ant stands on is no goal at all (nobody else can take it)
///   success       the ant's type changed; it is released (the task of its new role claims it)
///   failure       the ant is idle or shows "can't go" after the time the walk takes and nothing changed: the (ant, tile) pair is left alone for 900 ticks and the tile for 300
///   the dropper   a flower dropper replaces what lies on its tile every period: when the kind changes under an ant that is still far, the ant is called back (a plain order elsewhere
///                 cancels the walk before it crosses), when it is near it takes what is there
class PowerUpTask final : public Task {
public:
    struct Params {
        uint32_t max_trip_ticks{420};        // a power-up whose walk takes longer than this is not worth an ant
        uint32_t max_active{5};              // ants on their way at a time (the opening: Fire, Bomber, Thief, Combat, Swimmer)
        uint32_t blacklist_ticks{900};       // an (ant, tile) pair that failed
        uint32_t tile_blacklist_ticks{300};  // a tile that was contested, blocked or gave a failure
        uint32_t field_ttl_ticks{100};       // a walking-cost field is used this long
        uint32_t call_back_tiles{3};         // an ant nearer than this to a power-up that changed kind takes it anyway
    };
    PowerUpTask(TaskId id, Tactics& tactics) : PowerUpTask(id, tactics, Params{}) {}
    PowerUpTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "powerup"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t active() const noexcept { return takes_.size(); }
    uint32_t started() const noexcept { return started_; }
    uint32_t taken() const noexcept { return taken_; }
    uint32_t failed() const noexcept { return failed_; }
    uint32_t contested() const noexcept { return contested_; }
    uint32_t called_back() const noexcept { return called_back_; }
    bool blacklisted(uint32_t ant, sim::TileCoord tile, uint64_t tick) const noexcept;
    bool tile_blacklisted(sim::TileCoord tile, uint64_t tick) const noexcept;
    /// The ants that are on their way and what for
    std::vector<std::pair<uint32_t, sim::AntType>> on_their_way() const;
    const Params& params() const noexcept { return params_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Take {
        sim::TileCoord tile{};
        sim::AntType kind{sim::AntType::Thief};      // what was wanted
        sim::AntType before{sim::AntType::Worker};   // what the ant was
        uint64_t decided{0};
        uint64_t sent{kPending};
        uint32_t walk_ticks{0};                      // the estimate of the walk
    };
    struct Cached {
        uint64_t tick{0};
        std::vector<int32_t> field;
    };
    static int64_t key_of(sim::TileCoord t) noexcept { return static_cast<int64_t>(t.y) * 4096 + t.x; }
    const std::vector<int32_t>& field_for(TaskContext& context, sim::TileCoord tile);
    bool try_start(TaskContext& context, sim::AntType kind);

    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Take> takes_;                                 // ant -> its trip
    std::map<std::pair<uint32_t, int64_t>, uint64_t> black_;         // (ant, tile) -> until
    std::map<int64_t, uint64_t> tile_black_;                         // tile -> until
    std::map<int64_t, Cached> fields_;
    std::vector<uint8_t> mask_;
    uint64_t mask_tick_{~uint64_t{0}};
    uint32_t started_{0};
    uint32_t taken_{0};
    uint32_t failed_{0};
    uint32_t contested_{0};
    uint32_t called_back_{0};
};

// ---- rank 4: the thief hole's fire walls ----------------------------------------------------------------------------------------------------------------------

/// The Fire Ant (an own ant of type Fire, which this task claims) keeps a fire wall on each of the three tiles in front of the own thief hole (east_tiles) while the level's trigger
/// says that a thief threatens (wall_demand), and lights them again before they burn out (3,600 ticks after lighting; the bot notes the tick it first saw a wall, Memory::wall_seen):
///   build      the tile that is Open (a thief could step there and a wall can be lit) and nearest to the ant, one at a time: a special order of the ant onto it, checked with the
///              engine's own prediction (a cursor that shows the target cursor); never a tile that holds a power-up (nothing can be lit there)
///   renew      a level with a renewal lead (Medium, Hard) puts a wall out and lights it again when it has stood that long minus the lead, at a calm moment (no enemy Thief within
///              reach of the hill) and one wall at a time; the other levels light the wall again after it burned out, while the threat lasts
///   failure    an order after which the ant is idle again and the tile is still open: the tile is left alone for 300 ticks
class WallTask final : public Task {
public:
    WallTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "walls"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t keeper() const noexcept { return keeper_; }
    uint32_t walls_ordered() const noexcept { return walls_ordered_; }
    uint32_t renewals() const noexcept { return renewals_; }
    uint32_t failures() const noexcept { return failures_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Job {
        sim::TileCoord tile{};
        uint64_t decided{0};
        uint64_t sent{kPending};
        bool extinguish{false};
    };
    Tactics& tactics_;
    uint32_t keeper_{0};
    std::map<int, Job> jobs_;                      // by east tile index
    std::map<int, uint64_t> failed_until_;
    Job counter_job_;                              // the enemy wall that is being put out (tile; decided 0: none)
    std::map<int64_t, uint64_t> counter_black_;    // enemy walls that could not be put out, until when they are left alone
    uint32_t walls_ordered_{0};
    uint32_t renewals_{0};
    uint32_t failures_{0};
    uint32_t extinguished_{0};
public:
    /// Enemy walls put out (the counter): for the tests and the reports
    uint32_t extinguished() const noexcept { return extinguished_; }
};

// ---- rank 4: the counters to the enemy's bombs ---------------------------------------------------------------------------------------------------------------

/// An enemy bomb (any bomb whose owner is neither the seat nor its ally; every bomb is drawn in its owner's colour, so a person sees it) that lies near the own hill or a pile that the
/// hill reaches is harmful: an ant that steps on it loses 2 hit points, is thrown four tiles and loses its walk (a carrier stands with its food), and the engine's path finder does not
/// go round an enemy bomb. One bomb at a time is cleared:
///   a Bomber    an own Bomber Ant defuses it (a special order onto the bomb's tile: it walks next to it and the 12-tick clip removes the bomb; nothing is hurt)
///   a worker    without a Bomber (and with plan.bomb_hit) a healthy idle worker is clicked onto the bomb's tile and sets it off on purpose (a person's click on a bomb): it loses 2
///               hit points, which it heals at the hill, instead of a carrier on its way losing them and its walk
/// A bomb that is still there when the ant is idle again is left alone for 300 ticks. The ant is claimed for the job (a Bomber harvests between jobs).
class BombTask final : public Task {
public:
    BombTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "bombs"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t defused() const noexcept { return defused_; }
    uint32_t set_off() const noexcept { return set_off_; }
    uint32_t failures() const noexcept { return failures_; }
    bool busy() const noexcept { return actor_ != 0; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    static int64_t key_of(sim::TileCoord t) noexcept { return static_cast<int64_t>(t.y) * 4096 + t.x; }
    Tactics& tactics_;
    uint32_t actor_{0};
    sim::TileCoord target_{};
    bool defuse_{false};
    uint64_t decided_{0};
    uint64_t sent_{kPending};
    std::map<int64_t, uint64_t> black_;
    uint32_t defused_{0};
    uint32_t set_off_{0};
    uint32_t failures_{0};
};

/// Whether a bomb or a fire wall on `tile` is in the way of the seat's economy: within `plan.counter_hill_radius` of its hill, or within `plan.counter_pile_radius` tiles of a pile
/// with units that its hill reaches
bool harms_economy(const BotView& view, const MapInfo& map, const LevelPlan& plan, sim::TileCoord tile) noexcept;

/// Whose economy a bomb or a fire wall on `tile` is in the way of: 1 the own (harms_economy), 2 the ally's (with plan.ally_help: within counter_hill_radius of the ally's hill, or
/// within counter_pile_radius of a pile that the ally's hill reaches), 0 nobody's
int harm_to(const BotView& view, const MapInfo& map, const LevelPlan& plan, sim::TileCoord tile) noexcept;

// ---- rank 3: the raids -------------------------------------------------------------------------------------------------------------------------------------

/// The Thief ants of the bot (an own ant of type Thief, which this task claims) raid, again and again: a special order on an enemy hill makes the thief walk to its raid tile, plays
/// 75 ticks of the raid clip (which nothing can interrupt) and takes min(the victim's score, 50) points, which the thief walks home with and banks (the engine does all of that by
/// itself after the order). The target is the hill of the LEADING team (by the score boxes) of those that
///   have points to take   their box shows at least LevelPlan::raid_min_loot points (an ally's hill never)
///   are not shut          the three tiles in front of the thief hole are not all fire walls, bombs or solid (east_state: a thief cannot raid such a hill)
///   can be reached        the thief can walk to one of those tiles from where it stands
///   leave time            the round trip (there, 80 ticks of raid, back, 30 to bank) ends before the clock does
///   are not guarded       (Hard) no enemy Combat Ant stands near the raid tile of the hill: its reflex would hit the thief before it gets there
/// A hill that the thief did not get to (it stands where it stood after the order left) is left alone for LevelPlan::raid_black_ticks, and one whose raid click the controller refused (Fate::Filtered:
/// a power-up lies on the entrance) for Params::filtered_ticks. The thief is ordered again as soon as it is idle and empty-handed.
class RaidTask final : public Task {
public:
    struct Params {
        uint32_t guard_radius{7};            // an enemy Combat Ant this close to a raid tile guards the hill
        uint32_t filtered_ticks{900};        // a hill whose raid click the controller refused (a power-up on the tile) is left alone this long
    };
    RaidTask(TaskId id, Tactics& tactics) : RaidTask(id, tactics, Params{}) {}
    RaidTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "raids"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t raids_ordered() const noexcept { return raids_ordered_; }
    uint32_t failures() const noexcept { return failures_; }
    uint32_t ambushes() const noexcept { return ambushes_; }
    size_t waiting() const noexcept { return waiting_.size(); }
    int last_target() const noexcept { return last_target_; }
    bool black(uint8_t team, uint64_t tick) const noexcept;
    const Params& params() const noexcept { return params_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Raid {
        uint8_t team{0};
        uint64_t decided{0};
        uint64_t sent{kPending};
        sim::TileCoord origin{};
    };
    bool launch(TaskContext& context, const AntView& thief);
    bool ambush(TaskContext& context, const AntView& thief);
    struct Waiting {
        uint8_t team{0};
        uint64_t since{0};
        sim::TileCoord spot{};
        uint64_t last_move{0};
    };
    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Waiting> waiting_;
    uint64_t ambush_pause_until_{0};
    uint32_t ambushes_{0};
    std::map<uint32_t, Raid> raids_;
    std::map<uint8_t, uint64_t> black_;
    uint32_t raids_ordered_{0};
    uint32_t failures_{0};
    int last_target_{-1};
};

// ---- rank 4: the strike -------------------------------------------------------------------------------------------------------------------------------------

/// The owner's playbook: "if you're losing, forcing a team fight is a good way to swing the game back in your favour if you can win". When the bot (with its ally, by the score boxes) is
/// clearly BEHIND the leader (Standing::behind) and a fight looks winnable, a strike force hunts the carriers of the leader near its hill: a blow on a carrier on its way home clears
/// its walk (it stands idle with its food until its owner sends it on) and costs the leader the time, and a won fight costs it ants (and, at the last ant, an egg). One attack order is
/// one blow, so a member is ordered again each time it is idle after a blow; between targets the force waits three tiles in front of the leader's hill on the side the carriers come from.
///   the force   Combat Ants first, then healthy workers with empty hands, at most strike_force of them and never so many that fewer than strike_reserve ants stay at home; ants that a
///               pick-up, a raid, a wall, a bomb or a fight holds are never taken
///   winnable    the own strength (a Combat Ant counts 8, any other ant 4) is at least strike_odds_percent of the enemy's near the hill (the leader's ants, and its ally's, within
///               strike_radius tiles of the queue row: a Combat Ant counts 8, an ant that was drawn in its attack clip lately 4, any other ant 1: the workers that queue at a gate are
///               not an army); once the force is out it is called off when the own strength falls below the enemy's
///   ahead       the bot that is not behind has no strike (the force goes back to the pool at once): it protects its lead
///   wipe        (wipe_focus, Hard) an enemy team that shows at most wipe_max_ants ants and is far weaker than the force (three times, and the force at least three ants) is hunted
///               down: every ant of it is a target, not only carriers; its last ant forces a hatch (the engine takes min(200, score) from it) and with no egg left it is out of the match
class StrikeTask final : public Task {
public:
    StrikeTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "strike"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t force() const noexcept { return force_.size(); }
    bool active() const noexcept { return active_; }
    bool wiping() const noexcept { return wipe_; }
    int target() const noexcept { return target_; }
    uint32_t attacks_ordered() const noexcept { return attacks_ordered_; }
    uint32_t strikes_started() const noexcept { return strikes_started_; }
    uint32_t calls_off() const noexcept { return calls_off_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Member {
        uint64_t decided{0};
        uint64_t sent{kPending};
        uint32_t target{0};
        bool ordered{false};
    };
    void disband(TaskContext& context);
    uint32_t enemy_weight(const AntView& enemy, uint64_t now) const;
    Tactics& tactics_;
    std::map<uint32_t, Member> force_;
    bool active_{false};
    bool wipe_{false};
    int target_{-1};
    uint64_t paused_until_{0};
    uint32_t attacks_ordered_{0};
    uint32_t strikes_started_{0};
    uint32_t calls_off_{0};
};

// ---- rank 4: sabotage ---------------------------------------------------------------------------------------------------------------------------------------

/// "if somebody stole your fire power-up, they could fire your whole basin and then you cannot eat" (the owner): the Fire Ant of the bot, when its own work (the walls in front of the own thief
/// hole, the enemy walls and bombs in the way of the own economy) is done, lights fire walls on the tiles around the gate of the BEST OPPONENT: the row two tiles above the queue row and the
/// tiles at its ends (bx - 1 .. bx + 3, by - 2; bx - 1, by - 1; bx + 3, by - 1; bx - 1, by), which seal the queue row from every side but the mound. Nobody can reach the gate until the victim puts one of
/// them out (its own Fire Ant, if it has one) or they burn out (3,600 ticks). The ant is claimed only while it has a tile to light that its cursor would accept, one order at a time (every tile
/// is ordered again after 150 ticks at the earliest), and is given back to the economy when the ring stands. It claims no ant of a higher rank and no ant that holds food.
class SabotageTask final : public Task {
public:
    SabotageTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "sabotage"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t walls_ordered() const noexcept { return walls_ordered_; }
    size_t working() const noexcept { return working_ ? 1u : 0u; }
    int target() const noexcept { return target_; }
    /// The tiles around the gate of `hill` that seal the queue row (see above)
    static std::array<sim::TileCoord, 8> ring_of(const HillInfo& hill) noexcept;

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    Tactics& tactics_;
    uint32_t ant_{0};
    bool working_{false};
    int target_{-1};
    sim::TileCoord job_tile_{-1, -1};
    uint64_t job_decided_{0};
    uint64_t job_sent_{kPending};
    std::map<int64_t, uint64_t> ordered_;            // tile -> the tick of the last order
    uint32_t walls_ordered_{0};
};

// ---- rank 4: harassment ---------------------------------------------------------------------------------------------------------------------------------------

/// The harassment squad ("Hard bots should be really aggressive", the owner): the Combat Ants of the bot (and, with harass_workers, workers that the economy can spare) hunt the carriers
/// of the other teams. A blow clears the walk of a carrier and it stands with its food until its owner sends it on, so a squad that stays with its target keeps a team's income down:
/// in the bench ONE Combat Ant of an aggressor costs a Medium bot half of its score and a Hard bot a quarter (docs/BOTS.md, "Aggression"). Targets are carriers that an attack order can
/// reach (attackable()), the best opponent's first (by the score boxes: the margin to the best other is what a match is won by), the ones far from their hill and from help, near the
/// squad; a target is not taken when the enemy's strength near it is above the squad's (odds). Orders: one attack order per blow per ant (a group of ants on one target is one command);
/// a member that shows "can't go" after an attack is not sent at that ant again for a while; a member that is hurt (harass_min_hp), holds food or is taken by a fight leaves the squad.
/// Without a target the Combat Ants wait in the middle between the enemy hills (their reflex punches what passes within three tiles), the workers go back to the economy.
class HarassTask final : public Task {
public:
    HarassTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "harass"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t squad() const noexcept { return squad_.size(); }
    bool hunting() const noexcept { return hunting_; }
    uint32_t attacks_ordered() const noexcept { return attacks_ordered_; }
    uint32_t refused_odds() const noexcept { return refused_odds_; }
    uint32_t shunned() const noexcept { return shunned_count_; }
    uint32_t recruited() const noexcept { return recruited_; }
    uint32_t pauses() const noexcept { return pauses_; }


private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Member {
        uint64_t decided{0};
        uint64_t sent{kPending};
        uint32_t target{0};
        bool ordered{false};
        bool station_sent{false};
        uint64_t out_since{0};               // since when no target has been within range of the member (0: one is)
        sim::TileCoord tile{};               // where it stood at the previous look
    };
    void disband(TaskContext& context);
    uint32_t enemy_weight(const AntView& enemy, uint64_t now) const;
    Tactics& tactics_;
    std::map<uint32_t, Member> squad_;
    std::map<uint32_t, uint64_t> shunned_;
    std::array<uint64_t, sim::MAX_PLAYERS> pause_until_{};     // a team whose ants hurt the squad (or that shrugs the blows off): left alone until then

    std::array<uint32_t, sim::MAX_PLAYERS> pause_count_{};     // how often a team has been put on hold: every time it lasts twice as long as the one before (a team that hurt the squad twice is no prey)
    uint32_t pauses_{0};
    bool hunting_{false};
    uint64_t last_target_{0};
    uint32_t attacks_ordered_{0};
    uint32_t refused_odds_{0};
    uint32_t shunned_count_{0};
    uint32_t recruited_{0};
};

// ---- the hatch -----------------------------------------------------------------------------------------------------------------------------------------------------

/// Medium and Hard hatch an egg (the hatch pedestal: 200 points, an egg, 160 ticks until the newborn exists, 171 until it takes orders) only for a fight, never for the economy (the
/// hill's gate caps the income: a 7th worker adds nothing, docs/BOTS.md). A fight is expected when an own ant was hit within the last 600 ticks, the strike force is out, or an enemy
/// Combat or Thief ant stands within 14 tiles of the own hill. Then the bot wants as many ants as it started with plus hatch_extra (Medium 1, Hard 2), and at least hatch_floor, and
/// hatches one egg at a time until it has them (an egg that incubates counts): that replaces what a fight cost and adds the strength of the fight. Needed: the points (200 plus
/// hatch_reserve; with an ally the box is not the own score: 400 more), an egg, nothing incubating, and hatch_min_left ticks left. One click per 70 ticks (the egg count falls only after
/// the click has been released and applied). The task claims no ant.
class HatchTask final : public Task {
public:
    HatchTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "hatch"; }
    void step(TaskContext& context) override;
    uint32_t hatches_ordered() const noexcept { return hatches_ordered_; }

private:
    Tactics& tactics_;
    uint64_t next_after_{0};
    uint32_t hatches_ordered_{0};
    size_t start_ants_{0};               // the ants of the bot at the first look
    bool clicked_{false};                // a click was proposed at the last eligible look
    uint32_t eggs_before_{0};
    uint64_t refused_until_{0};
};

// ---- the gate -----------------------------------------------------------------------------------------------------------------------------------------------

/// "Guiding for eating" (the owner's playbook: "bypassing the queue by manually controlling which ant deposits food"). The engine's own queue sends an ant that finds the entrance busy to a
/// waiting tile on the FAR side of the mound (bx - 1, by + 3), and the task ANTHILLQ dispatches it from there on a walk of 44 to 68 ticks around the mound: one deposit per 93 to 116
/// ticks, 10 to 13 a minute for a hill however many workers feed it. By hand a deposit takes 55 to 65 ticks (docs/BOTS.md, "The gate": measured with a Hard-limited client in the
/// engine, +23 percent on TREASURE for a lone seat, +55 to +86 percent for 8 to 12 workers on a pile 8 to 20 tiles away). The task owns every carrier of the seat (an ant that holds food):
///   stage      a fresh carrier (the engine's own order for it is the entrance or the far waiting tile) is taken over before it can claim the entrance: it goes to a free slot on the queue row
///              (bx .. bx + 2, by - 1; two of them are kept filled, never the tile the depositors leave by), else to a buffer tile four rows north of the ramp; more than max_staged are
///              stopped where they stand and released one by one
///   deposit    one ant at a time is clicked onto the entrance (bx + 1, by + 1): the one that can be there soonest, when the gate is free, or, predictively, latency_ticks before it will
///              be (the enter clip is 22 ticks and the depositor needs 9 more to leave the entrance, counted from the look that first showed the clip); never while a bite runs
///   exit       the tile that the empty ants leave the hill by is seen from the looks (the queue-row tile an ant steps on right after the mound): it and the lane behind it stay free
///   fails      a clicked ant that is neither on the mound nor nearer after 30 ticks, or that takes longer than its walk and 40 ticks, was refused: it is taken over again
/// A click that the controller refused (Fate::Filtered: a power-up on the tile) blocks that tile for Params::blocked_ticks; with the entrance blocked the gate does not guide (the engine's flow stays).
/// So does a run of Params::user_fail_limit clicks onto the entrance that delivered nothing (the carriers are boxed in: a causeway jammed head on): the gate stops for blocked_ticks, which turns the
/// economy's rescue and the aid of a hit carrier on, instead of ordering the same ants every 24 ticks for the rest of the match.
/// Without an entry in the plan (Easy and Medium, and every level until the tournaments say so) the engine's flow stays. It claims no ant in the ledger (carriers are nobody's task); the
/// economy's own rescue of idle carriers and the carrier aid are off while it runs.
class GateTask final : public Task {
public:
    struct Params {
        int32_t buffer_rows{4};              // the buffer tile is (bx + 1, by - buffer_rows)
        uint32_t slots{2};                   // queue-row tiles kept filled
        uint32_t max_staged{8};              // carriers brought to the doorstep at a time (the others are parked)
        uint32_t fail_age{30};
        uint32_t clip_ticks{22};
        uint32_t exit_ticks{9};
        uint32_t latency_ticks{9};           // an order decided now is applied this many ticks later (the profile's delay less its jitter, and the sink)
        uint32_t bite_wait_max{25};          // the entrance click waits at most this many looks for a bite that runs (a fresh carrier's own order claims the entrance until the takeover lands)
        uint32_t blocked_ticks{900};         // a tile that the controller refused a click onto (a power-up on it) is not chosen again this long
        uint32_t user_fail_limit{16};        // this many clicks onto the entrance in a row that delivered nothing (a healthy gate fails up to 10 in a row: measured on the shipped maps): it stops guiding for blocked_ticks (0: never)
        bool predictive{true};
        int32_t doorstep_dx0{-4};
        int32_t doorstep_dx1{6};
        int32_t doorstep_dy0{-8};
        int32_t doorstep_dy1{-1};
    };
    explicit GateTask(TaskId id) : GateTask(id, Params{}) {}
    GateTask(TaskId id, const Params& params) : Task(id), params_(params) {}
    const char* name() const noexcept override { return "gate"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t entrance_clicks() const noexcept { return entrance_clicks_; }
    uint32_t takeovers() const noexcept { return takeovers_; }
    uint32_t parks() const noexcept { return parks_; }
    uint32_t user_failures() const noexcept { return user_failures_; }
    /// Times the gate stopped guiding because its clicks delivered nothing (Params::user_fail_limit)
    uint32_t pauses() const noexcept { return pauses_; }
    uint32_t user() const noexcept { return user_; }
    /// (review experiment) the gate could guide at the last look: the hill has room at its doorstep
    bool usable() const noexcept { return usable_; }
    /// Whether the controller refused a click of this task onto the tile lately (Fate::Filtered): it is not chosen again before blocked_ticks are over
    bool blocked(sim::TileCoord t, uint64_t tick) const noexcept;
    const std::deque<int>& exits() const noexcept { return exits_; }
    /// The order in which the queue-row tiles (0, 1, 2) are used as slots, -1 for none
    std::array<int, 3> slot_order() const noexcept { return {slot_order_[0], slot_order_[1], slot_order_[2]}; }
    const Params& params() const noexcept { return params_; }
    void set_params(const Params& params) { params_ = params; }

private:
    struct Cmd {
        sim::TileCoord tile{-1, -1};
        uint64_t release{0};
        bool decided{false};
        bool parked{false};
    };
    struct Geometry {
        sim::TileCoord hill{};
        sim::TileCoord entrance{};
        sim::TileCoord buffer{};             // the doorstep tile for the carriers that wait: walkable, (bx + 1, by - buffer_rows) or the nearest that is
        std::array<bool, 3> slot_ok{{true, true, true}};   // the queue-row tiles that an ant can stand on
        int width{0};
        const std::vector<int32_t>* cost{nullptr};
    };
    sim::TileCoord queue_tile(const Geometry& g, int i) const noexcept { return sim::TileCoord{g.hill.x + i, g.hill.y - 1}; }
    bool in_queue_row(const Geometry& g, sim::TileCoord t) const noexcept { return t.y == g.hill.y - 1 && t.x >= g.hill.x && t.x <= g.hill.x + 2; }
    bool in_mound(const Geometry& g, sim::TileCoord t) const noexcept { return t.x >= g.hill.x && t.x <= g.hill.x + 3 && t.y >= g.hill.y && t.y <= g.hill.y + 3; }
    bool in_doorstep(const Geometry& g, sim::TileCoord t) const noexcept {
        return t.x >= g.hill.x + params_.doorstep_dx0 && t.x <= g.hill.x + params_.doorstep_dx1 && t.y >= g.hill.y + params_.doorstep_dy0 && t.y <= g.hill.y + params_.doorstep_dy1;
    }
    int cost_of(const Geometry& g, sim::TileCoord t) const noexcept;
    void track_exits(TaskContext& context, const Geometry& g);
    void choose_slots(TaskContext& context, const Geometry& g);

    Params params_;
    int slot_order_[3]{1, 0, 2};
    uint32_t slot_pile_{0xFFFFFFFFu};
    std::deque<int> exits_;                       // the queue-row tile (0..2) used by the last empty ants that left the hill
    std::map<uint32_t, int> prev_zone_;           // ant -> 0 outside / 1 in the mound (previous look)
    std::map<uint32_t, Cmd> cmd_;
    std::map<int64_t, uint64_t> blocked_;         // tile (y * 4096 + x) -> the tick until which no click is put onto it (a refused click)
    std::map<uint32_t, uint64_t> clip_seen_;
    int64_t pending_free_at_{0};
    uint32_t bite_waited_{0};
    uint32_t user_{0};
    uint64_t user_release_{0};
    int user_cost0_{0};
    int user_eta0_{0};
    uint32_t entrance_clicks_{0};
    uint32_t takeovers_{0};
    uint32_t parks_{0};
    uint32_t user_failures_{0};
    uint32_t user_streak_{0};                     // clicks onto the entrance in a row that delivered nothing
    uint32_t pauses_{0};
    bool usable_{false};
};

// ---- rank 2: the guard -------------------------------------------------------------------------------------------------------------------------------------

/// A Combat Ant (an own ant of type Combat that no task of a higher rank holds) is parked where its reflex covers the hill: an idle Combat Ant that has had no order for 2 seconds
/// punches the first enemy within three tiles of it (and goes back to where it stood), so a guard post is a tile whose 7 x 7 square holds the three tiles in front of the thief hole
/// (the east side) or the hill's queue row. Posts, in the order the ants take them: east of the walls, south east, north of the gate. The ant is ordered once and then left alone
/// (every order restarts the 2 seconds and cancels a reflex that is under way); it is ordered again only when it stands more than two tiles from its post.
class GuardTask final : public Task {
public:
    GuardTask(TaskId id, Tactics&) : Task(id) {}
    const char* name() const noexcept override { return "guard"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t guards() const noexcept { return posts_.size(); }
    uint32_t posted() const noexcept { return posted_; }
    /// The post of the n-th guard of the hill (0, 1, 2), by the hill's geometry only
    static sim::TileCoord post_of(const HillInfo& hill, size_t n) noexcept;

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Post {
        sim::TileCoord tile{};
        uint64_t decided{0};
        uint64_t sent{kPending};
    };
    std::map<uint32_t, Post> posts_;
    uint32_t posted_{0};
};

// ---- rank 2: the hit carrier ------------------------------------------------------------------------------------------------------------------------------

/// A carrier that was hit (Memory::hits: an own ant that held food or loot lost hit points) stands idle with its food: the blow cleared its walk, and nothing in the engine sends it
/// on (the economy's own rescue waits 900 ticks near the gate). It is sent to the hill entrance as soon as it is idle again (a person would click it). It claims no ant: it only gives
/// the order, once per attempt, and forgets the ant when it carries nothing or is walking again.
class CarrierAidTask final : public Task {
public:
    explicit CarrierAidTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "carrier-aid"; }
    void step(TaskContext& context) override;
    /// A click onto the entrance that the controller refused (Fate::Filtered: a power-up lies on it) stops the aid for 900 ticks
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    uint32_t sent_home() const noexcept { return sent_home_; }
    size_t watching() const noexcept { return aid_.size(); }

private:
    struct Aid {
        uint64_t hit{0};
        uint64_t last_order{0};
        uint32_t tries{0};
    };
    Tactics& tactics_;
    std::map<uint32_t, Aid> aid_;
    sim::TileCoord home_{-1, -1};         // where the last order sent a carrier (the hill entrance)
    uint64_t blocked_until_{0};           // a refused click onto it: no aid until this tick
    uint32_t sent_home_{0};
};

}  // namespace ants::ai
