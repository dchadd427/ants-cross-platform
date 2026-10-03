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
    ThiefSeen,       // Easy: an enemy Thief ant has been seen (and for a while after)
    ThiefPossible,   // Medium: an enemy Thief ant has been seen, or an enemy that plays can reach a Thief power-up
    Always           // Hard: as soon as an enemy plays (pre-emptively), and the walls are renewed before they burn out
};

/// The switches and numbers of a level's tactics. The profile (look interval, reaction delay, command budget) is a fairness limit and is NOT here: it stays as profile_for() says.
/// The numbers were decided by tournaments (docs/BOTS.md, "The standard bot").
struct LevelPlan {
    Level level{Level::Medium};
    // fighting back (every level)
    uint32_t defenders{2};               // the nearest healthy ants that answer a blow: Easy 1, Medium 2, Hard 3
    int32_t leash_tiles{10};             // a fight is never carried further than this from the place of the blow (or from the hill, for a thief)
    uint32_t fight_linger_ticks{100};    // a fight ends this long after the last blow that was seen
    // protecting the thief hole (every level)
    WallTrigger wall_trigger{WallTrigger::ThiefPossible};
    uint32_t wall_latch_ticks{3600};     // a thief threat lasts this long after the last time it was seen (a walled hill needs its walls renewed while it lasts)
    uint32_t renew_lead_ticks{0};        // a wall is put out and lit again this long before it would burn out (0: it is lit again after it burned out, while the threat lasts)
    // power-ups, raids, guards (Medium and Hard)
    bool takes_combat{false};
    bool takes_thief{false};
    bool intercepts{false};              // an enemy Thief on its way to the own hill is attacked
    bool guards{false};                  // a Combat Ant is parked where its reflex covers the hill
    bool raids{false};                   // a Thief raids the hill of the leading team
    uint32_t max_combat{0};              // Combat Ants at a time (including those that are on their way to a power-up)
    uint32_t max_thief{0};
    // Hard only
    bool squads{false};                  // a fight is joined by Combat Ants first and by a larger group
    bool harasses{false};                // enemy carriers near contested piles are attacked when it pays
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
};

}  // namespace ants::ai
