#pragma once

// MapInfo: the static analysis of one match, built once from the map (docs/BOTS.md, "Architecture"). What a person works out by looking at the map before the first
// click: where the hills are, how far every food pile is from each hill, which piles an ant can reach on foot at all, where the power-ups lie, and about how long a
// trip to a pile takes.
//
// ants_ai has to DUPLICATE a few rules of the engine to do this (which tile an ant may step on, what a step costs). They are not guessed: they are the engine's own
// (SimulationEngineImpl::step_cost and can_enter, Grid::is_solid_object, movement::terrain_step_weight) and every one of them is pinned by a conformance test in
// tests/test_ai (AI1.x: a flood fill and a Dijkstra search against the engine's own path manager on hundreds of pairs per shipped map). Should a rule of the
// engine ever change, a test fails here before a bot misjudges a map.
//
// What the analysis knows is the map AT THE MOMENT OF BUILDING (the start of the match): bombs, fire walls, bridges and consumed power-ups change the real map later.
// It is a HINT for planning, never a promise: on the six shipped maps a flood fill is exact, on community maps it is right about nine times in ten, so a bot
// still checks in the next view that an order did what it meant (docs/BOTS.md).
//
// No threads, no clock, no randomness: the same map always gives the same analysis, bit for bit.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_sim/ant_unit.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::ai {

/// The tiles of a hill that matter to a bot (bx, by = the hill's own tile; the mound is the 4 x 4 block from there)
struct HillInfo {
    bool present{false};                 // the team has a hill in this match (a seat that does not play has none)
    sim::TileCoord origin{-1, -1};       // (bx, by)
    sim::TileCoord mouth{-1, -1};        // (bx + 1, by): the ramp
    sim::TileCoord entrance{-1, -1};     // (bx + 1, by + 1): the hole an ant enters through
    sim::TileCoord queue{-1, -1};        // (bx + 1, by - 1): the tile in front of the ramp, on the owner-only row; the walking costs are measured from here
    sim::TileCoord raid{-1, -1};         // (bx + 3, by + 2): the tile on which a thief raids the hill
    sim::TileCoord tile42{-1, -1};       // (bx + 4, by + 4): where empty ants and newborns wait
};

/// How far a target is for the ants of one team
struct Approach {
    int32_t cost{-1};                    // in the engine's step-cost units (a step over grass is 20 = 8 ticks; see MapInfo::kTicksPerCostNum): -1 = not on foot
    sim::TileCoord click{-1, -1};        // the tile to click: a cell of the target itself
    bool reachable() const noexcept { return cost >= 0; }
};

/// One food pile of the map's table (every object, also one that starts empty), with how far it is for each team
struct PileInfo {
    uint32_t index{0};                   // its place in Grid::food_objects(); the table only grows
    sim::TileCoord anchor{};             // (col, row) of the object
    uint16_t units{0};                   // units at the start
    uint16_t value{0};                   // points per unit
    /// The cells of its footprint on the map, as the engine attributes them to objects (Grid::food_object_at_cell). EMPTY for an object that a later object with the
    /// same anchor has taken the cells of: it cannot be clicked.
    std::vector<sim::TileCoord> cells;
    /// The object whose units a bite takes when an ant harvests at this anchor: the FIRST object of the table with the anchor (the engine's own rule, action_system.cpp
    /// start_harvest), which differs from `index` only for duplicates of an anchor (two on TREASURE).
    uint32_t bite_index{0};
    std::array<Approach, sim::MAX_PLAYERS> approach{};
};

/// One power-up lying on the map at the start
struct PowerUpInfo {
    sim::TileCoord tile{};
    sim::AntType type{sim::AntType::Bomber};   // the kind of ant it makes: Bomber, Fire, Thief, Combat or Swimmer
    std::array<Approach, sim::MAX_PLAYERS> approach{};
};

class MapInfo {
public:
    /// The engine's A* gives up when the f value (g + h) of the node it pops reaches this (path_planner.hpp kFailF): a walk costing more is never found
    static constexpr int32_t kPathCostLimit = 8000;
    /// Walking time in ticks = cost * 2 / 5 on every terrain (grass: weight 20 = 8 ticks, sand 16 = 6.4, dirt 24 = 9.6, mud 48 = 19.2: the weights ARE the walking times of the
    /// original's animations, 0.4 tick per unit)
    static constexpr int32_t kTicksPerCostNum = 2;
    static constexpr int32_t kTicksPerCostDen = 5;
    /// What a round trip costs besides the walking: the bite (grab clip), the queue at the hill, the enter clip (22 ticks) and the walk from the queue tile; measured on the
    /// six shipped maps (docs/audit/B2_notes.md)
    static constexpr int32_t kTripOverheadTicks = 70;

    /// Analyses the map as it is now: the grid of `sim` (at the start of a match). A grid that is empty gives an empty analysis.
    explicit MapInfo(const sim::SimulationEngine& sim);
    explicit MapInfo(const sim::Grid& grid);

    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }

    // ---- the rules of the engine that ants_ai duplicates (public so that the conformance tests can hold them against the engine) -------------------------------

    /// Whether an ant of `team` that is neither a swimmer, a fire ant nor a thief may stand on `tile` in the map as `grid` shows it now (the engine's can_enter rules R1, R3, R4 and
    /// R5): inside the map, walkable terrain (water only under a bridge), not on the 4 x 4 mound of any hill (its ramp and hole included), not a solid object (Grid::is_solid_object:
    /// obstacles, food, power-ups, lunchboxes, fire walls) and not on another team's queue row (the three tiles above its ramp). Occupants (ants) are not part of it: they move.
    /// NOT TileCell::is_passable and not the tile's obstacle-overlay flag, which the engine does not look at.
    static bool walkable(const sim::Grid& grid, uint8_t team, sim::TileCoord tile) noexcept;
    /// The engine's cost of one step from `from` to the tile `to` next to it (SimulationEngineImpl::step_cost, its terrain part): the two tiles' weights added, times 1.4
    /// (truncated) for a diagonal step, halved
    static uint32_t step_cost(const sim::Grid& grid, sim::TileCoord from, sim::TileCoord to) noexcept;
    /// The least walking cost from `source` to every tile, for the ants of `team`, on the engine's weights (Dijkstra over walkable tiles, eight neighbours, no cutting-corner
    /// rule: the engine has none); -1 for a tile that cannot be reached. A source that is not walkable gives an all -1 field. Index = y * width + x.
    static std::vector<int32_t> cost_field(const sim::Grid& grid, uint8_t team, sim::TileCoord source);
    /// walkable() for every tile of the map (1 or 0, index = y * width + x): what the cost fields and the components are made from
    static std::vector<uint8_t> walkable_mask(const sim::Grid& grid, uint8_t team);
    /// cost_field over a mask that walkable_mask gave (a caller that needs several fields asks the engine's rules once per tile)
    static std::vector<int32_t> cost_field(const sim::Grid& grid, const std::vector<uint8_t>& mask, sim::TileCoord source);
    /// Ticks of walking for a cost
    static constexpr int32_t walking_ticks(int32_t cost) noexcept { return cost * kTicksPerCostNum / kTicksPerCostDen; }
    /// The trip model: the ticks of one round trip to a target whose approach costs `cost` (-1 for a cost that is none): there and back plus kTripOverheadTicks
    static constexpr int32_t trip_ticks_for_cost(int32_t cost) noexcept {
        return cost < 0 ? -1 : 2 * cost * kTicksPerCostNum / kTicksPerCostDen + kTripOverheadTicks;
    }

    // ---- hills --------------------------------------------------------------------------------------------------------------------------------------------------

    const HillInfo& hill(uint8_t team) const noexcept;

    // ---- walking costs and components -----------------------------------------------------------------------------------------------------------------------------

    /// The walking cost field of the team's hill (from its queue tile): -1 where its ants cannot walk from the hill. Empty for a team without a hill.
    const std::vector<int32_t>& cost_field_of(uint8_t team) const noexcept;
    int32_t walking_cost(uint8_t team, sim::TileCoord tile) const noexcept;
    /// The walker component of a tile for the ants of `team` (tiles are in one component when an ant of the team can walk from one to the other); -1 for a tile an ant of
    /// the team cannot stand on. Labels are numbered from 0 in reading order of the first tile of each component.
    int32_t component(uint8_t team, sim::TileCoord tile) const noexcept;
    /// The component an ant that stands on `tile` belongs to: its own tile's, or (on a tile that is not walkable, such as the hole of its hill) the smallest label among the
    /// walkable tiles next to it; -1 when there is none
    int32_t ant_component(uint8_t team, sim::TileCoord tile) const noexcept;
    /// The component of the team's hill (that of its queue tile), -1 without a hill
    int32_t hill_component(uint8_t team) const noexcept;
    /// How many components the team's ants have on this map
    int32_t component_count(uint8_t team) const noexcept;

    // ---- piles ---------------------------------------------------------------------------------------------------------------------------------------------------

    const std::vector<PileInfo>& piles() const noexcept { return piles_; }
    const PileInfo* pile(uint32_t index) const noexcept { return index < piles_.size() ? &piles_[index] : nullptr; }
    /// Whether an ant of `team` standing on `from` can walk to the pile at all (its component is a component of a tile next to a cell of the pile). Not about the hill: an ant
    /// stranded on another island is told "no" for every pile of the hill's island.
    bool can_reach_pile(uint8_t team, sim::TileCoord from, uint32_t pile) const noexcept;
    /// The points of every pile (units times value) that some team's hill can walk to / that the team's hill can walk to, as the engine attributes the clickable cells:
    /// the "reachable pot" of the map (an object that cannot be clicked, or that shares its anchor with an earlier object and so is bitten from the first, counts once)
    uint32_t reachable_points() const noexcept;
    uint32_t reachable_points(uint8_t team) const noexcept;
    /// The points of all objects of the table, reachable or not
    uint32_t total_points() const noexcept;

    /// The trip model for the team and the pile: ticks of one round trip, -1 when the pile cannot be reached from the team's hill
    int32_t trip_ticks(uint8_t team, uint32_t pile) const noexcept;
    /// The same question asked of the map as `grid` shows it NOW (the cost field is rebuilt, 7 to 75 microseconds): a pile's footprint shrinks and changes shape as it is
    /// eaten (its tile changes with every stage, the anchor cell always stays), so the click tile of the start may no longer be food. Use it when a bot re-targets a pile that
    /// has been worked on, or after a wall, a bridge or a bomb changed the map. Cost -1 when the pile has no units left (a leftover crumb of the picture is not a pile), has no cell
    /// on the map or cannot be reached now.
    Approach approach_now(const sim::Grid& grid, uint8_t team, uint32_t pile) const;

    // ---- power-ups -----------------------------------------------------------------------------------------------------------------------------------------------

    const std::vector<PowerUpInfo>& powerups() const noexcept { return powerups_; }

private:
    void build(const sim::Grid& grid);
    size_t at(sim::TileCoord t) const noexcept { return static_cast<size_t>(t.y) * static_cast<size_t>(w_) + static_cast<size_t>(t.x); }
    bool inside(sim::TileCoord t) const noexcept { return t.x >= 0 && t.y >= 0 && t.x < w_ && t.y < h_; }
    /// The cheapest way onto a cell of a target for the team whose cost field is `field`: the best walkable tile next to any of `cells`, plus the step onto the cell (the walk may
    /// end on any cell of the object: the engine lets a harvesting ant step on its own pile and a power-up seeker on its power-up)
    Approach approach_in(const sim::Grid& grid, const std::vector<int32_t>& field, const std::vector<sim::TileCoord>& cells) const;

    int w_{0};
    int h_{0};
    std::array<HillInfo, sim::MAX_PLAYERS> hills_{};
    std::array<std::vector<int32_t>, sim::MAX_PLAYERS> cost_{};
    std::array<std::vector<int32_t>, sim::MAX_PLAYERS> comp_{};
    std::array<int32_t, sim::MAX_PLAYERS> comp_count_{};
    std::vector<PileInfo> piles_;
    std::vector<PowerUpInfo> powerups_;
};

}  // namespace ants::ai
