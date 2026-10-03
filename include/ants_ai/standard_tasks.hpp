#pragma once

// The tasks of the standard bot (standard_bot.hpp), built on the task model of tasks.hpp: each claims ants through the AntLedger (a task of a higher rank may take an ant from one of a
// lower rank), puts orders into Orders, reads the result from the next view and ends when the reason is gone. They share the Tactics (the level's plan and the bot's memory).
//
//   rank 5  FightTask     strike back at an enemy that hit an own ant, and attack an enemy Thief that is on its way to the own hill
//   rank 4  WallTask      the Fire Ant that keeps three fire walls in front of the own thief hole
//   rank 3  PowerUpTask   one ant at a time is sent to take a power-up that the bot wants; RaidTask: a Thief raids the hill of the leading team
//   rank 2  GuardTask     a Combat Ant is parked where its reflex covers the hill
//   rank 1  HarvestTask   the economy (tasks.hpp, unchanged)
//
// Every order a task gives is a click that a person could make: a plain move, an attack on the tile of an enemy ant, a special order of one ant (BotController filters the rest).

#include <cstdint>
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

/// Takes the power-ups that the bot wants (Tactics::wants: the Fire Ant of the walls, the Thief of the raids, the Combat Ants of the guard), one ant for each, by a plain click of the
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
        uint32_t max_active{2};              // ants on their way at a time
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

// ---- rank 3: the raids -------------------------------------------------------------------------------------------------------------------------------------

/// The Thief ants of the bot (an own ant of type Thief, which this task claims) raid, again and again: a special order on an enemy hill makes the thief walk to its raid tile, plays
/// 75 ticks of the raid clip (which nothing can interrupt) and takes min(the victim's score, 50) points, which the thief walks home with and banks (the engine does all of that by
/// itself after the order). The target is the hill of the LEADING team (by the score boxes) of those that
///   have points to take   their box shows at least min_loot points (an ally's hill never)
///   are not shut          the three tiles in front of the thief hole are not all fire walls, bombs or solid (east_state: a thief cannot raid such a hill)
///   can be reached        the thief can walk to one of those tiles from where it stands
///   leave time            the round trip (there, 80 ticks of raid, back, 30 to bank) ends before the clock does
///   are not guarded       (Hard) no enemy Combat Ant stands near the raid tile of the hill: its reflex would hit the thief before it gets there
/// A hill that the thief did not get to (it stands where it stood after the order left) is left alone for black_ticks. The thief is ordered again as soon as it is idle and empty-handed.
class RaidTask final : public Task {
public:
    struct Params {
        uint32_t min_loot{30};               // a hill whose score box shows less is not worth the trip
        uint32_t black_ticks{600};           // a hill that could not be reached is left alone this long
        uint32_t guard_radius{7};            // an enemy Combat Ant this close to a raid tile guards the hill
    };
    RaidTask(TaskId id, Tactics& tactics) : RaidTask(id, tactics, Params{}) {}
    RaidTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "raids"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t raids_ordered() const noexcept { return raids_ordered_; }
    uint32_t failures() const noexcept { return failures_; }
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
    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Raid> raids_;
    std::map<uint8_t, uint64_t> black_;
    uint32_t raids_ordered_{0};
    uint32_t failures_{0};
    int last_target_{-1};
};

// ---- rank 4: the harassment --------------------------------------------------------------------------------------------------------------------------------

/// A small squad (Combat Ants first, then workers) hunts the enemy carriers near the enemy hills: a blow on a carrier on its way home (or one that waits next to the gate) clears its
/// walk (it stands idle with its food until its owner sends it on) and costs its team the time. One attack order is one blow, so a squad member is ordered again each time it is
/// idle after a blow. Where it waits between targets: three tiles in front of the nearest enemy hill, on the side the carriers come from (towards the middle of the map).
///   when it pays   the squad is called off for a while when one of its members was hit (the enemy fights back) and, once an enemy Combat Ant is in sight, for the match; it is never
///                  taken from a workforce smaller than harass_min_workers, and not in the last 900 ticks
///   never          an ant that stands on a power-up, a carrier within three tiles of an enemy Combat Ant (its reflex would hit the squad first)
class HarassTask final : public Task {
public:
    HarassTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "harass"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    size_t squad() const noexcept { return squad_.size(); }
    uint32_t attacks_ordered() const noexcept { return attacks_ordered_; }
    uint32_t calls_off() const noexcept { return calls_off_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Member {
        uint64_t decided{0};
        uint64_t sent{kPending};
        uint32_t target{0};
        bool ordered{false};
    };
    Tactics& tactics_;
    std::map<uint32_t, Member> squad_;
    uint64_t paused_until_{0};
    uint32_t attacks_ordered_{0};
    uint32_t calls_off_{0};
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
    uint32_t sent_home_{0};
};

}  // namespace ants::ai
