#pragma once

// GateInfo: the gate analysis of one team's map (docs/reverse_engineering/bomb_gates.md, "Gate analysis"). Where MapInfo says what an ant can walk to, and IslandInfo what lies beyond WATER,
// this says what lies beyond FIRE WALLS and BOMBS: a fire wall is solid for every ant but a Fire ant, a bomb of a map file (or of another team) is walkable but throws the ant that steps on
// it four tiles back for 2 hit points. POPcOrN walls each home half in with a fire wall across the whole map and a field of bombs behind it; Bombz Away covers its floor with bombs. The
// analysis names, for every pile and power-up of the map, what stands between the team's hill and it, and which means get past it:
//
//   a Fire hat   a Fire ant walks through fire walls (and over no bomb): the fewest fire cells on a walk, bombs closed
//   a tunnel     a Bomber defuses the bombs in its way (24 ticks each, no damage): the fewest bombs on a walk, fire closed
//   a hop        a Bomber plants a bomb on a free tile T next to the home side, an ant steps onto it and is thrown four tiles the way it came: the rule of the engine
//                (tests/test_sim/test_bomb_facts.cpp): the start direction is the opposite of the ant's facing (the direction of its last step), a landing tile that the engine refuses (a rock,
//                the map edge, a hill, its queue row) makes the next direction clockwise the one tried, a landing on a bomb throws again (the facing the flight leaves is kept unless the rotation
//                turned it), every bomb costs 2 hit points and is a dud 20 times in 100 (the ant then burns on the spot). A landing in water drowns, one on a fire wall costs a hit point and
//                scatters the ant: neither is a way over.
//
// What is here is the map AT THE START (like MapInfo and IslandInfo): bombs and fire walls that appear later are read from the grid at the moment of use by whoever plans with it. The special tiles
// of every hill count as live (the safe side, as in IslandInfo). No threads, no clock, no randomness: the same map gives the same analysis, bit for bit. The analysis itself changes no behaviour; plan_gate_goal (gate_goal.hpp) and GateGoalTask (gate_task.hpp) plan with it, and StandardBot::decide_gate_map asks walled() once at the first look whether the bot plays the gate goals.

#include <cstdint>
#include <vector>

#include "ants_sim/ant_unit.hpp"
#include "ants_sim/grid.hpp"

namespace ants::ai {

class MapInfo;

/// What stands between the hill and a target (a pile or a power-up)
enum class GateKind : uint8_t {
    None,      // an ant reaches it on foot with fire walls and bombs both closed
    Fire,      // only fire walls are in the way: a Fire ant walks through, and no bomb gets in the way of that walk
    Bombs,     // only bombs are in the way: a Bomber defuses its way through
    Either,    // both a walk through fire (bombs closed) and a walk over defused bombs (fire closed) exist
    Mixed,     // a walk exists only through fire AND bombs together
    Sealed,    // no walk at all (water, rocks): only a hop or a bridge could reach it
};

/// The ways past a gate (a bit set in GateTarget::passes)
inline constexpr uint8_t kPassFireAnt = 1;      // a Fire ant walks through the fire walls
inline constexpr uint8_t kPassDefuse = 2;       // a Bomber defuses the bombs on a walk
inline constexpr uint8_t kPassHop = 4;          // a Bomber hop chain lands in reach of the target

/// How a hop chain ends
enum class HopEnd : uint8_t {
    Ground,    // the last landing is a tile that a walker can stand on (a safe component: no fire, no bomb)
    Water,     // the last landing is water: a walker drowns
    Fire,      // the last landing is a fire wall cell: the ant loses a hit point and is scattered by chance
    Stuck,     // no landing tile is free in any of the 8 directions: the ant does not fly, it burns on the spot
};

/// One throw of a Bomber's bomb and what follows it
struct BombHop {
    sim::TileCoord bomb{-1, -1};         // T: the tile the bomb is planted on (free ground: the Bomber works from a cardinal neighbour of it in the same component)
    sim::TileCoord from{-1, -1};         // P: the tile the ant steps onto T from (a neighbour of T in the component), the facing is the direction P -> T
    uint8_t facing{0};                   // 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW
    std::vector<sim::TileCoord> lands;   // every landing of the chain, the first after the planted bomb, the next after each bomb of the map that a landing lies on
    HopEnd end{HopEnd::Ground};
    int32_t from_comp{-1};               // component of P (-1 when the hop was asked of a tile outside the analysis)
    int32_t to_comp{-1};                 // component of the last landing for HopEnd::Ground, else -1
    int32_t bombs{0};                    // bombs that go off: the planted one and each bomb the chain lands on (a Stuck chain counts the one that burned the ant)
    int32_t hp{0};                       // hit points lost: 2 per bomb, and 1 more for HopEnd::Fire
    int32_t fly_percent{0};              // the chance that none of the bombs is a dud (80 percent each, truncated per bomb)
    sim::TileCoord land() const noexcept { return lands.empty() ? bomb : lands.back(); }
};

/// One walker component of the team with fire walls and bombs both closed
struct GateComponent {
    int32_t id{-1};
    int32_t tiles{0};
    bool hill{false};                    // the team's hill leaves into it
};

/// A connected group (8 neighbours) of fire cells or of bomb cells
struct GateBarrier {
    GateKind kind{GateKind::Fire};       // Fire or Bombs
    int32_t cells{0};
    sim::TileCoord min{0, 0};            // the bounding box
    sim::TileCoord max{0, 0};
};

/// A pile or a power-up of the map and what stands between the hill and it
struct GateTarget {
    bool powerup{false};                 // false: a pile (MapInfo::piles() index), true: a power-up (MapInfo::powerups() index)
    uint32_t index{0};
    std::vector<int32_t> comps;          // the components with a tile next to the target (or, for a power-up that is walkable, on it)
    GateKind gate{GateKind::Sealed};
    bool free() const noexcept { return gate == GateKind::None; }
    int32_t fire_cells{-1};              // the fewest fire cells a Fire ant crosses on a walk to it from the hill, bombs closed; -1 none
    int32_t defuse_bombs{-1};            // the fewest bombs a Bomber defuses on a walk, fire closed; -1 none
    sim::TileCoord fire_cell{-1, -1};    // the first fire cell (nearest the hill) of a walk with the fewest fire cells: the one a Fire ant puts out to open the way (-1 when fire_cells <= 0)
    sim::TileCoord bomb_cell{-1, -1};    // the first bomb cell (nearest the hill) of a walk with the fewest bombs: the one a Bomber defuses to open the way (-1 when defuse_bombs <= 0)
    int32_t shore_fire_cells{-1};        // for a Sealed water-moated pile: fire cells on the shortest walk+swim from the hill (-1 when none)
    int32_t shore_bombs{-1};             // for a Sealed water-moated pile: bomb cells on the shortest walk+swim from the hill (-1 when none)
    sim::TileCoord shore_fire_cell{-1, -1};
    sim::TileCoord shore_bomb_cell{-1, -1};
    int32_t hop{-1};                     // the best hop of GateInfo::hops() that lands in reach of it (fewest bombs, then the best chance), -1 none
    uint8_t passes{0};                   // kPassFireAnt | kPassDefuse | kPassHop
};

class GateInfo {
public:
    GateInfo() = default;
    /// Analyses the map for the ants of `team` (the hill's ants leave through the queue row). A team without a hill, or a grid that is not `map`'s, gets an empty analysis.
    static GateInfo analyse(const MapInfo& map, const sim::Grid& grid, uint8_t team);

    /// The throw rule on its own: a bomb planted on `bomb` and an ant that steps onto it with `facing`, on the map `grid` shows. Components are not known to it (from_comp and to_comp are -1,
    /// `end` is Ground for any landing that is neither water nor fire). The engine's own rule (see the top); the conformance test holds it against the simulation on every tile.
    static BombHop simulate(const sim::Grid& grid, sim::TileCoord bomb, uint8_t facing);
    /// The best planted-bomb hop from component `from_comp` that lands on safe ground in component `to_comp` (bombs == 0 when none exists).
    static BombHop best_hop_between(const GateInfo& gates, const sim::Grid& grid, int32_t from_comp, int32_t to_comp);
    /// The best map-bomb chain triggered by stepping 1 tile from `from` onto an adjacent map bomb that lands on safe ground in `to_comp` (bombs == 0 when none exists).
    static BombHop best_map_bomb_bounce(const GateInfo& gates, const sim::Grid& grid, sim::TileCoord from, int32_t to_comp);

    /// A bomb costs this many hit points (the ant of a chain pays it for every bomb), is a dud this often (percent), and a flight is this many tiles
    static constexpr int32_t kBombHp = 2;
    static constexpr int32_t kDudPercent = 20;
    static constexpr int32_t kFlightRange = 4;
    /// A chain of more bombs than this is cut (the engine has no limit: a chain of map bombs may run on; none of the shipped maps has more than 8)
    static constexpr int32_t kMaxChain = 12;

    bool empty() const noexcept { return !valid_; }
    const std::vector<GateComponent>& components() const noexcept { return comps_; }
    /// The component of the team's hill (-1 when every tile in front of its ramp is closed: a bomb lies on each of them on Bombz Away)
    int32_t home() const noexcept { return home_; }
    /// The safe component of a tile (-1 for fire, bombs, rocks, water, and a tile outside)
    int32_t component(sim::TileCoord tile) const noexcept;
    const std::vector<GateBarrier>& barriers() const noexcept { return barriers_; }
    int32_t fire_cell_count() const noexcept { return fire_cells_; }
    int32_t bomb_cell_count() const noexcept { return bomb_cells_; }
    const std::vector<GateTarget>& piles() const noexcept { return piles_; }
    const std::vector<GateTarget>& powerups() const noexcept { return powerups_; }
    /// The hops that leave the HOME component and end on ground in another component, in the order of the scan (T row by row, the facing 0 .. 7)
    const std::vector<BombHop>& hops() const noexcept { return hops_; }
    /// The hops (of hops()) that land in component `to` / whose last landing is exactly `tile`
    std::vector<const BombHop*> hops_to(int32_t to) const;
    std::vector<const BombHop*> hops_onto(sim::TileCoord tile) const;
    /// Whether the map is WALLED for this team: the analysis is valid, the hill's ants leave into a safe component (home() >= 0), the map has piles and none of them is free of every
    /// gate (HELLRAISER and its family, POPcOrN, and ISLANDS, which the island play keeps; not Bombz Away, where home() is -1). Read at the start: it decides whether the bot plays the gate goals (StandardBot).
    bool walled() const noexcept;
    /// How many piles / power-ups are free of every gate, and how many can be reached by some means at all
    int32_t free_piles() const noexcept;
    int32_t passable_piles() const noexcept;

private:
    bool valid_{false};
    int w_{0};
    int h_{0};
    int32_t home_{-1};
    int32_t fire_cells_{0};
    int32_t bomb_cells_{0};
    std::vector<int32_t> comp_of_;          // index y * w + x
    std::vector<GateComponent> comps_;
    std::vector<GateBarrier> barriers_;
    std::vector<GateTarget> piles_;
    std::vector<GateTarget> powerups_;
    std::vector<BombHop> hops_;
};

}  // namespace ants::ai
