#pragma once

// What the tasks of the standard bot (standard_tasks.hpp) share: the switches and numbers of a level (LevelPlan), the geometry of a hill's thief hole (the three tiles in front of
// it), and the bot's soft MEMORY of the last looks (who was hit, which enemy ants moved, which thieves were seen).
//
// Everything in here is something a person of the seat could know: the memory is built from the BotView alone (the hit points of the seat's OWN ants, the tile, type and drawn state of
// every ant, the grid), and the geometry is the hill's. Nothing here reads another team's hit points, carried points, orders, eggs or timers, and nothing is a fact that the next look
// could not give back: a bot that is started in the middle of a match has an empty memory and plays on (a hit that it did not see is a hit that it does not answer).

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/grid.hpp"

namespace ants::ai {

// ---- what a level does ---------------------------------------------------------------------------------------------------------------------------------

/// When a level builds the three fire walls in front of its thief hole (and takes a Fire power-up for it)
enum class WallTrigger : uint8_t {
    Never,           // no walls (only for the tournaments' ablations)
    ThiefSeen,       // Easy: an enemy Thief ant has been seen (and for a while after)
    ThiefPossible,   // Medium: an enemy Thief ant has been seen, or an enemy that plays can reach a Thief power-up that lies on the map
    Early            // Hard: an enemy Thief ant has been seen, or any enemy can reach a Thief power-up that lies on the map (pre-emptively: without waiting for the enemy to show
                     // that it plays), and the walls are renewed before they burn out
};

/// The switches and numbers of a level's tactics. The profile (look interval, reaction delay, command budget) is a fairness limit and is NOT here: it stays as profile_for() says.
/// The numbers were decided by tournaments (docs/BOTS.md, "The standard bot").
struct LevelPlan {
    Level level{Level::Medium};
    // fighting back (every level)
    uint32_t defenders{2};               // the nearest healthy ants that answer a blow: Easy 1, Medium 2, Hard 3
    int32_t leash_tiles{10};             // a fight is never carried further than this from the place of the blow (or from the hill, for a thief)
    uint32_t fight_linger_ticks{100};    // a fight ends this long after the last blow that was seen
    bool carrier_aid{true};              // a carrier that was hit stands idle with its food (the blow cleared its walk): it is sent home at once, as a person would click it
    // the economy
    bool contest_aware{false};           // the pile order of the owner's playbook: the piles that several enemies reach first, then those that one reaches, the safe ones, those of the ally, the hopeless (HarvestTask::Params::contest_aware)
    uint32_t contest_low{70};            // an enemy whose cost to a pile is below this percentage of the own cost is there first
    uint32_t contest_high{130};          // ... and above this percentage it is no competitor
    bool rank_by_remaining{false};       // within a class: the points that a pile still holds per tick of the trip (Medium, Hard)
    bool contest_one_first{true};        // the piles that one enemy competes for come before the safe ones
    bool fire_aware{true};               // a pile with a fire wall near it is asked again with the map as it is now: no ant is sent into fire (HarvestTask::Params::fire_aware)
    bool typed_harvest{true};            // Fire and Bomber ants harvest between their jobs (HarvestTask::Params::extra_types)
    bool combat_harvests{false};         // Combat Ants harvest too (and punch what comes within two tiles of their way) instead of standing on a guard post
    // protecting the thief hole (every level)
    WallTrigger wall_trigger{WallTrigger::ThiefPossible};
    uint32_t wall_latch_ticks{3600};     // a thief threat lasts this long after the last time it was seen (a walled hill needs its walls renewed while it lasts)
    uint32_t renew_lead_ticks{0};        // a wall is put out and lit again this long before it would burn out (0: it is lit again after it burned out, while the threat lasts)
    // the side's power-ups and the counters of the enemy's fire walls and bombs
    bool secure_side{false};             // (Medium, Hard) the power-ups of secure_kinds on the own side of the map are taken early, so that nobody steals them (docs/BOTS.md)
    uint8_t secure_kinds{0};             // bit t = AntType t, in the order of value: Fire, Bomber, then Thief, Combat, Swimmer (the owner's playbook: the first moves go to power-ups, not food)
    bool counters{true};                 // enemy fire walls near the hill or a pile are put out by an own Fire Ant, enemy bombs defused by an own Bomber or, without one, set off by a healthy idle worker
    bool bomb_hit{true};                 // ... the worker's way (without a Bomber): an ant that steps on a bomb loses 2 hit points and is thrown, and the bomb is gone
    int32_t counter_hill_radius{10};     // a hazard within this many tiles of the own hill (or within counter_pile_radius of a pile that the hill reaches) is harmful
    int32_t counter_pile_radius{3};
    // power-ups, raids, guards (Medium and Hard)
    bool combat_when_attacked{true};     // Combat Ants are taken only once the enemy fights (a blow, an enemy Combat Ant or Thief in sight), not at the start of the match ...
    bool combat_when_idle{true};         // ... or when the economy has workers that stand idle with nothing to harvest (they cost nothing)
    bool takes_combat{false};
    bool takes_thief{false};
    bool intercepts{false};              // an enemy Thief that stands near the own hill is attacked (measured: it does not pay, see docs/BOTS.md; off at every level)
    bool guards{false};                  // a Combat Ant is parked where its reflex covers the hill
    bool raids{false};                   // a Thief raids the hill of the leading team
    uint32_t max_combat{0};              // Combat Ants at a time (including those that are on their way to a power-up)
    uint32_t max_thief{0};
    // Hard only
    bool squads{false};                  // a fight is joined by Combat Ants first and by a larger group
    bool harasses{false};                // enemy carriers near contested piles are attacked when it pays
    uint32_t harassers{0};               // ants of the harassing squad (Combat Ants first); 0: none
    uint32_t harass_min_workers{4};      // the squad is taken only from a workforce of at least this many (the economy keeps its harvesters)
    int32_t harass_radius{14};           // carriers within this many tiles of an enemy hill are hunted
    bool harass_idle_only{true};         // a worker joins the squad only when the economy has no pile for it (it costs nothing); false: taken from the harvest
    bool avoids_guarded_hills{false};    // a raid does not go to a hill that has an enemy Combat Ant near it
};

LevelPlan plan_for(Level level) noexcept;

// ---- the thief hole -------------------------------------------------------------------------------------------------------------------------------------

/// The three tiles in front of a hill's thief hole: (bx + 4, by + 1 .. by + 3), east of the raid tile (bx + 3, by + 2). A thief reaches the raid tile only by stepping from one of them
/// (the other neighbours of the raid tile are tiles of the mound), so a thief cannot raid a hill whose three tiles are all shut. Verified against the engine's own raid order
/// (docs/audit/B4_1_notes.md): with a fire wall on each of them the order ends in "Can't go there." and the victim keeps its points; with two walls, or three one tile further east, the
/// raid goes through.
std::array<sim::TileCoord, 3> east_tiles(const HillInfo& hill) noexcept;

/// What stands on one of the three tiles
enum class EastTile : uint8_t {
    Blocked,    // a thief cannot step there anyway: water, a rock, another solid object (a pile, a power-up, a lunchbox), off the map
    Wall,       // a fire wall (solid for every ant but a fire ant)
    Bomb,       // a bomb (a thief that steps on it is thrown away and its order is gone: it counts as shut for a raid, but one blast opens the tile again)
    Open,       // a thief can step there and a fire wall can be lit on it (grass, sand or dirt with nothing on it)
    Bare        // a thief can step there and no wall can stand on it (mud): the hole cannot be shut
};

struct EastState {
    std::array<EastTile, 3> tile{EastTile::Open, EastTile::Open, EastTile::Open};
    /// Every tile is shut (Blocked, Wall or Bomb): a thief cannot raid this hill now
    bool shut() const noexcept {
        for (const EastTile t : tile) {
            if (t == EastTile::Open || t == EastTile::Bare) return false;
        }
        return true;
    }
    /// A wall can still shut the hole: no tile is Bare
    bool shuttable() const noexcept {
        for (const EastTile t : tile) {
            if (t == EastTile::Bare) return false;
        }
        return true;
    }
    size_t walls() const noexcept {
        size_t n = 0;
        for (const EastTile t : tile) n += t == EastTile::Wall ? 1u : 0u;
        return n;
    }
};

/// The state of the three tiles of `hill` on the map as `grid` shows it now
EastState east_state(const sim::Grid& grid, const HillInfo& hill) noexcept;

// ---- the sides of the map -----------------------------------------------------------------------------------------------------------------------------

/// The team whose side of the map a power-up lies on: the team whose hill reaches it at the least walking cost (the start analysis: PowerUpInfo::approach), strictly less than every
/// other team of the match; -1 for a power-up that no hill reaches or that two hills reach equally well. On TREASURE every base has one power-up of each kind on its side.
/// `present` is the bitmask of the teams in the match. A power-up that a flower dropper makes is not in the analysis of the start: it has no side.
int power_up_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept;

// ---- the memory ---------------------------------------------------------------------------------------------------------------------------------------

/// An own ant that lost hit points (or died) since the previous look
struct Hit {
    uint32_t ant{0};
    sim::TileCoord tile{};               // where it stands now (where it stood at the last look when it died)
    uint8_t hp_before{0};
    uint8_t hp_now{0};                   // 0: it is gone
    bool carried{false};                 // it held food (or loot)
};

/// An enemy Thief ant that is in sight
struct ThiefSighting {
    uint32_t ant{0};
    uint8_t team{0};
    sim::TileCoord tile{};
    sim::TileCoord before{-1, -1};       // where it stood at the previous look, (-1, -1) when it was not in sight then
    bool carrying{false};
};

class Memory {
public:
    /// Called once at every look, before the tasks: compares the view with the previous one
    void update(const BotView& view, const MapInfo& map);

    uint64_t now() const noexcept { return now_; }
    /// The own ants that lost hit points since the previous look, or died (a hit is only seen when the ant was in the previous view)
    const std::vector<Hit>& hits() const noexcept { return hits_; }
    /// The enemy Thief ants in sight now
    const std::vector<ThiefSighting>& thieves() const noexcept { return thieves_; }
    /// The tick at which an enemy Thief ant was last in sight (0: never)
    uint64_t thief_last_seen() const noexcept { return thief_last_seen_; }
    /// The tick at which an enemy Combat Ant was last in sight (0: never)
    uint64_t combat_last_seen() const noexcept { return combat_last_seen_; }
    /// The tick of the last look at which an own ant had lost hit points (0: never)
    uint64_t last_hit() const noexcept { return last_hit_; }
    /// Whether any ant of the team has been seen doing something (not idle) at some look: an idle bot's ants never move, so nothing of that team is a threat
    bool plays(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS && seen_moving_[team] != 0; }
    /// The tick of the first look that saw a wall on the tile (0: no wall there now); the wall burns out 3,600 ticks after it was lit, which is at or before this tick plus 3,600
    uint64_t wall_seen(sim::TileCoord tile) const noexcept;
    /// The own ant's hit points at the previous look (0: it was not in that view)
    uint8_t hp_before(uint32_t ant) const noexcept;

private:
    struct Last {
        uint8_t hp{0};
        sim::TileCoord tile{};
        bool carried{false};
    };
    uint64_t now_{0};
    std::map<uint32_t, Last> own_;                          // the seat's own ants at the previous look
    std::map<uint32_t, sim::TileCoord> enemy_tile_;         // the Thief ants of other teams at the previous look
    std::vector<Hit> hits_;
    std::vector<ThiefSighting> thieves_;
    uint64_t thief_last_seen_{0};
    uint64_t combat_last_seen_{0};
    uint64_t last_hit_{0};
    std::array<uint64_t, sim::MAX_PLAYERS> seen_moving_{};
    std::map<std::pair<int32_t, int32_t>, uint64_t> wall_seen_;   // (x, y) of a wall tile of the thief hole -> the tick it was first seen
};

/// What every task of the standard bot is handed besides the TaskContext: the level's plan, the memory and the wishes that the bot formed at this look
struct Tactics {
    LevelPlan plan;
    Memory memory;
    /// How many ants of each kind the bot wants at this look (index = sim::AntType): the Fire Ant that lights the walls, the Thief that raids, the Combat Ants that guard. Counted
    /// with the ants that already are of the kind and the pick-ups that are on their way.
    std::array<uint8_t, 6> wants{};
    /// A thief threatens the own hill by the level's rule (WallTrigger): the fire walls are wanted, and a Fire Ant for them
    bool wall_demand{false};
    /// Workers that stood idle with empty hands at the last look and that the economy could send nowhere (HarvestTask::unplaced: no pile left that they can reach in time): they
    /// cost nothing to use otherwise
    size_t surplus{0};
};

/// Whether the fire walls in front of the own thief hole are wanted at this look, by the level's trigger: an enemy Thief ant has been seen lately (Easy, Medium, Hard), an enemy that
/// plays can reach a Thief power-up (Medium, Hard) or some enemy plays at all (Hard); never for a hole that no wall can shut (mud), never in the last 400 ticks of the match
bool wall_demand(const Tactics& tactics, const BotView& view, const MapInfo& map);

/// Whether the walkers of `team` can reach a tile next to `tile` from their own hill (the analysis of the start: components, so a power-up that is walled in for an enemy is not a
/// thief it can take)
bool reachable_by(const MapInfo& map, uint8_t team, sim::TileCoord tile) noexcept;


}  // namespace ants::ai
