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
// approach_now() asks again of the map as it is now, with the rules that depend on the moment: own and allied bombs block (R7) and the queue row of a team that dropped
// out is ordinary ground (R5): the ally and the dropped teams are not on the grid, so a caller passes them (WalkContext; BotView::walk_context() has both).
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

/// What the engine's walking rules read besides the grid: R7 (an own or allied bomb blocks the team's ants) needs the team's ally, and R5 (another team's queue row is closed)
/// has an exception for a team that dropped out (the engine treats its special tiles as ordinary ground, FUN_0101d858). Neither is on the grid, so the caller passes them;
/// BotView::walk_context() gives both for a seat. The default is "no ally, nobody dropped".
struct WalkContext {
    uint8_t ally{sim::ALLIANCE_NONE};    // the team's ally, ALLIANCE_NONE when it plays alone
    uint8_t dropped_mask{0};             // bit t: team t dropped out of the match
    bool dropped(uint8_t team) const noexcept { return team < 8 && ((dropped_mask >> team) & 1u) != 0; }
};

/// The tiles of a hill that matter to a bot (bx, by = the hill's own tile; the mound is the 4 x 4 block from there). The FIRST hill of a team on the map (Grid::find_anthill: a map
/// with no hill art falls back to its start markers, which may name a team twice)
struct HillInfo {
    bool present{false};                 // the team has a hill in this match (a seat that does not play has none)
    sim::TileCoord origin{-1, -1};       // (bx, by)
    sim::TileCoord mouth{-1, -1};        // (bx + 1, by): the ramp
    sim::TileCoord entrance{-1, -1};     // (bx + 1, by + 1): the hole an ant enters through
    sim::TileCoord queue{-1, -1};        // (bx + 1, by - 1): the middle tile in front of the ramp, on the owner-only row (it may be blocked: then see `starts`)
    sim::TileCoord raid{-1, -1};         // (bx + 3, by + 2): the tile on which a thief raids the hill
    sim::TileCoord tile42{-1, -1};       // (bx + 4, by + 4): where empty ants and newborns wait
    /// The walkable tiles of the queue row (bx .. bx + 2, by - 1), left to right: the tiles an ant on the ramp steps onto (the two outer ones diagonally) and the tiles the walking
    /// costs of the hill are measured from (cost 0 on each). Empty when all three are blocked: nothing can leave that hill.
    std::vector<sim::TileCoord> starts;
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

/// One flower dropper of the map (a plant whose record drops, as the engine's table lists them at the start), with how far its drop tile is for each team. The drop tile holds nothing at
/// the start, so the approach is the one onto a tile that a power-up will lie on (like PowerUpInfo's): the walk onto the cell, from the walkable tiles next to it
struct FlowerInfo {
    sim::TileCoord plant{};
    sim::TileCoord drop{};
    std::array<Approach, sim::MAX_PLAYERS> approach{};
};

class MapInfo {
public:
    /// The engine's A* gives up when the f value (g + h) of the node it pops reaches this (path_planner.hpp kFailF): a walk costing more is never found. It is also the
    /// engine's cost of a blocked step (kBlockedCost), which MapInfo::step_cost returns for water
    static constexpr int32_t kPathCostLimit = 8000;
    /// The engine's path finder never generates a tile of this row (the neighbour routine writes it as the "invalid" marker and the search skips every neighbour in it:
    /// src/ants_sim/path_planner.cpp, FUN_01019c31 and the test `!= 0x5a` at 0x1019b23 in Ants.exe), so on a map taller than 90 rows nothing can be walked INTO row 90 and the
    /// map is cut in two there. `walkable` models it. (The shipped maps have at most 60 rows; the loader takes up to 256.)
    static constexpr int kPathSentinelRow = 90;
    /// Walking time in ticks = cost * 2 / 5 for an ORTHOGONAL run on every terrain (grass: weight 20 = 8 ticks, sand 16 = 6.4, dirt 24 = 9.6, mud 48 = 19.2: the weights ARE the
    /// walking times of the original's animations, 0.4 tick per unit; mud walks about 7 percent faster than its weight says). A DIAGONAL step is charged 1.4 times by the path finder
    /// but its clip takes about 1.25 times an orthogonal one, so a run of diagonal steps takes about 10 percent LESS than this says (pinned by test AI1.35: 40 tiles of grass,
    /// 448 predicted, about 401 measured). A real route mixes both kinds of step, so walking_ticks and trip_ticks_for_cost OVERESTIMATE a route by a few percent and a diagonal-heavy
    /// route by up to about 10 percent; the model is not corrected for it (a correction needs the shape of the path, which a cost field does not hold).
    static constexpr int32_t kTicksPerCostNum = 2;
    static constexpr int32_t kTicksPerCostDen = 5;
    /// What a round trip costs besides the walking: the bite (grab clip), the queue at the hill, the enter clip (22 ticks) and the walk from the queue tile; a design value that
    /// the nearest pile of every hill of the five maps with food on foot fits to within 25 percent (docs/audit/B2_notes.md has the measured numbers)
    static constexpr int32_t kTripOverheadTicks = 70;

    /// Analyses the map as it is now: the grid of `sim` (at the start of a match). A grid that is empty gives an empty analysis.
    explicit MapInfo(const sim::SimulationEngine& sim);
    explicit MapInfo(const sim::Grid& grid);

    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }

    // ---- the rules of the engine that ants_ai duplicates (public so that the conformance tests can hold them against the engine) -------------------------------

    /// Whether an ant of `team` that is neither a swimmer, a fire ant nor a thief may stand on `tile` in the map as `grid` shows it now (the engine's can_enter rules R1, R3, R4, R5 and
    /// R7 for a walking ant): inside the map, walkable terrain (water only under a bridge), not on the 4 x 4 mound of any hill (its ramp and hole included), not a solid object
    /// (Grid::is_solid_object: obstacles, food, power-ups, lunchboxes, fire walls), not on another LIVE team's queue row (the three tiles above its ramp), not under a bomb of the
    /// team itself or of its ally (`ctx`), and not in row 90 of a map taller than that (see kPathSentinelRow). Occupants (ants) are not part of it: they move.
    /// NOT TileCell::is_passable and not the tile's obstacle-overlay flag, which the engine does not look at.
    static bool walkable(const sim::Grid& grid, uint8_t team, sim::TileCoord tile, const WalkContext& ctx = WalkContext{}) noexcept;
    /// The engine's cost of one step from `from` to the tile `to` next to it (SimulationEngineImpl::step_cost, its terrain part): the two tiles' weights added, times 1.4
    /// (truncated) for a diagonal step, halved; kPathCostLimit (the engine's kBlockedCost) when either tile is water (weight 8000), which a walking ant cannot enter
    static uint32_t step_cost(const sim::Grid& grid, sim::TileCoord from, sim::TileCoord to) noexcept;
    /// The least walking cost from `source` to every tile, for the ants of `team`, on the engine's weights (Dijkstra over walkable tiles, eight neighbours, no cutting-corner
    /// rule: the engine has none); -1 for a tile that cannot be reached. A source that is not walkable gives an all -1 field. Index = y * width + x.
    static std::vector<int32_t> cost_field(const sim::Grid& grid, uint8_t team, sim::TileCoord source, const WalkContext& ctx = WalkContext{});
    /// walkable() for every tile of the map (1 or 0, index = y * width + x): what the cost fields and the components are made from
    static std::vector<uint8_t> walkable_mask(const sim::Grid& grid, uint8_t team, const WalkContext& ctx = WalkContext{});
    /// cost_field over a mask that walkable_mask gave (a caller that needs several fields asks the engine's rules once per tile)
    static std::vector<int32_t> cost_field(const sim::Grid& grid, const std::vector<uint8_t>& mask, sim::TileCoord source);
    /// The same from several sources at once (every walkable source costs 0; an unwalkable one is ignored): the field of a hill, whose ants leave through any tile of the queue row
    static std::vector<int32_t> cost_field(const sim::Grid& grid, const std::vector<uint8_t>& mask, const std::vector<sim::TileCoord>& sources);
    /// The walking cost of every tile to the step ONTO `cell` (a power-up: solid, so no tile of the map has a field that starts on it): a search that starts at the walkable tiles
    /// next to the cell with the cost of the last step onto it. The engine's steps cost the same in both directions, so field[t] is what an ant standing on t pays to walk onto the
    /// cell; -1 for a tile that cannot reach it. All -1 when the engine would not let `team` step onto the cell (can_step_onto: water, a mound, another team's queue row).
    /// One search serves every ant at once (the closest one, an enemy's arrival).
    static std::vector<int32_t> cost_field_onto(const sim::Grid& grid, const std::vector<uint8_t>& mask, uint8_t team, sim::TileCoord cell, const WalkContext& ctx = WalkContext{});
    /// Ticks of walking for a cost (the orthogonal rate: see kTicksPerCostNum for the bias on diagonal runs)
    static constexpr int32_t walking_ticks(int32_t cost) noexcept { return cost * kTicksPerCostNum / kTicksPerCostDen; }
    /// The trip model: the ticks of one round trip to a target whose approach costs `cost` (-1 for a cost that is none): there and back plus kTripOverheadTicks
    static constexpr int32_t trip_ticks_for_cost(int32_t cost) noexcept {
        return cost < 0 ? -1 : 2 * cost * kTicksPerCostNum / kTicksPerCostDen + kTripOverheadTicks;
    }
    /// Whether the engine's path finder can step ONTO `cell` as the end of a walk of `team` (the target of a pile or a power-up; the cell itself is solid food or a power-up,
    /// which step_cost lets a harvester or a power-up seeker onto): not water, not inside any mound (R3: an order onto an own mound is a walk home, onto another is refused), not
    /// on another live team's queue row (R5: checked before the food exemption), and not in row 90 of a map taller than that
    static bool can_step_onto(const sim::Grid& grid, uint8_t team, sim::TileCoord cell, const WalkContext& ctx = WalkContext{}) noexcept;

    // ---- hills --------------------------------------------------------------------------------------------------------------------------------------------------

    const HillInfo& hill(uint8_t team) const noexcept;

    // ---- walking costs and components -----------------------------------------------------------------------------------------------------------------------------

    /// The walking cost field of the team's hill (from every walkable tile of its queue row, HillInfo::starts, each at cost 0): -1 where its ants cannot walk from the hill. Empty for
    /// a team without a hill.
    const std::vector<int32_t>& cost_field_of(uint8_t team) const noexcept;
    int32_t walking_cost(uint8_t team, sim::TileCoord tile) const noexcept;
    /// The walker component of a tile for the ants of `team` (tiles are in one component when an ant of the team can walk from one to the other); -1 for a tile an ant of
    /// the team cannot stand on. Labels are numbered from 0 in reading order of the first tile of each component.
    int32_t component(uint8_t team, sim::TileCoord tile) const noexcept;
    /// The component an ant that stands on `tile` belongs to: its own tile's, or (on a tile that is not walkable, such as the hole of its hill) the smallest label among the
    /// walkable tiles next to it; -1 when there is none
    int32_t ant_component(uint8_t team, sim::TileCoord tile) const noexcept;
    /// The component of the team's hill (that of the first tile of HillInfo::starts), -1 without a hill or when its whole queue row is blocked
    int32_t hill_component(uint8_t team) const noexcept;
    /// How many components the team's ants have on this map
    int32_t component_count(uint8_t team) const noexcept;

    // ---- piles ---------------------------------------------------------------------------------------------------------------------------------------------------

    /// The piles of the map's table AT THE START: the table grows during a match (a dying ant's lunchbox is a new object at the end of it), and those later objects are not here:
    /// pile(index) is null for them (BotView::piles() lists them with PileView::lunchbox); approach_now() works for every index of the engine's table
    const std::vector<PileInfo>& piles() const noexcept { return piles_; }
    const PileInfo* pile(uint32_t index) const noexcept { return index < piles_.size() ? &piles_[index] : nullptr; }
    /// Whether an ant of `team` standing on `from` can walk to the pile at all (its component is a component of a tile next to a cell of the pile). Not about the hill: an ant
    /// stranded on another island is told "no" for every pile of the hill's island. Piles of the table at the start only (false for a lunchbox).
    bool can_reach_pile(uint8_t team, sim::TileCoord from, uint32_t pile) const noexcept;
    /// The points of every pile (units times value) that some team's hill can walk to / that the team's hill can walk to, as the engine attributes the clickable cells:
    /// the "reachable pot" of the map (an object that cannot be clicked, or that shares its anchor with an earlier object and so is bitten from the first, counts once)
    /// (64 bits: a file stores units and points as 16 bits each and the loader does not clamp them, so the sum over the piles may not fit 32)
    uint64_t reachable_points() const noexcept;
    uint64_t reachable_points(uint8_t team) const noexcept;
    /// The points of all objects of the table, reachable or not
    uint64_t total_points() const noexcept;

    /// The trip model for the team and the pile: ticks of one round trip, -1 when the pile cannot be reached from the team's hill
    int32_t trip_ticks(uint8_t team, uint32_t pile) const noexcept;
    /// The walking costs of one team for the map as it is NOW (see approach_now)
    struct NowField {
        uint8_t team{0};
        WalkContext ctx;
        std::vector<int32_t> cost;       // like cost_field_of, for the map as it is now; empty when the team has no hill or the grid is not this map's
        bool valid() const noexcept { return !cost.empty(); }
    };
    /// One walkable mask and one Dijkstra over the whole map (about 70 to 300 microseconds on the shipped maps in a release build, about 5 ms on a 256 x 256 map)
    NowField field_now(const sim::Grid& grid, uint8_t team, const WalkContext& ctx = WalkContext{}) const;
    /// Whether an ant on `tile` can walk to the queue row of the hill that `field` was made for, as the map was then (a ring of fire walls shuts it: every walk in is refused). True for a tile of the
    /// mound or next to one that the field reaches, for an empty field, and for every Swimmer (the field is the walkers': water and fire are not checked for a swimmer).
    bool reaches_hill(const NowField& field, sim::TileCoord tile, sim::AntType type = sim::AntType::Worker) const noexcept;
    /// The same question as the Approach of a PileInfo, asked of the map as `grid` shows it NOW: a pile's footprint shrinks and changes shape as it is eaten (its tile changes with
    /// every stage, the anchor cell always stays), so the click tile of the start may no longer be food, and a wall, a bridge or a bomb may have changed the way. The walking costs
    /// are rebuilt with the engine's dynamic rules: `ctx` carries the team's ally (own and allied bombs block) and the dropped teams (their queue rows are open). Works for every
    /// object of the engine's table, also a lunchbox that a dying ant dropped after the start. Cost -1 when the pile has no units left (a leftover crumb of the picture is not a
    /// pile), has no cell on the map or cannot be reached now.
    /// The overload with a NowField is for a bot that re-targets several piles in one look: ONE field (field_now), then one cheap question per pile. The overload with a team
    /// makes a field for each call (see field_now for the cost).
    Approach approach_now(const sim::Grid& grid, uint32_t pile, const NowField& field) const;
    Approach approach_now(const sim::Grid& grid, uint8_t team, uint32_t pile, const WalkContext& ctx = WalkContext{}) const;

    // ---- power-ups -----------------------------------------------------------------------------------------------------------------------------------------------

    const std::vector<PowerUpInfo>& powerups() const noexcept { return powerups_; }

    // ---- flower droppers -----------------------------------------------------------------------------------------------------------------------------------------

    /// The flowers that drop, as the engine's table lists them when the analysis is made from an engine (empty for an analysis made from a grid alone, and on a map without one: TINY, TREASURE;
    /// the plants whose record is switched off are not listed). Static for the whole match
    const std::vector<FlowerInfo>& flowers() const noexcept { return flowers_; }
    /// The flower whose drop tile is `tile`, null when none is
    const FlowerInfo* flower_at(sim::TileCoord tile) const noexcept;

private:
    void build(const sim::Grid& grid);
    void add_flowers(const sim::Grid& grid, const std::vector<sim::FlowerDropperSnapshot>& droppers);
    size_t at(sim::TileCoord t) const noexcept { return static_cast<size_t>(t.y) * static_cast<size_t>(w_) + static_cast<size_t>(t.x); }
    bool inside(sim::TileCoord t) const noexcept { return t.x >= 0 && t.y >= 0 && t.x < w_ && t.y < h_; }
    /// The cheapest way onto a cell of a target for the team whose cost field is `field`: the best walkable tile next to any cell of `cells` that the engine lets the team's ants
    /// step onto (can_step_onto), plus the step onto the cell (the walk may end on any cell of the object: the engine lets a harvesting ant step on its own pile and a power-up
    /// seeker on its power-up)
    Approach approach_in(const sim::Grid& grid, const std::vector<int32_t>& field, const std::vector<sim::TileCoord>& cells, uint8_t team, const WalkContext& ctx) const;

    int w_{0};
    int h_{0};
    std::array<HillInfo, sim::MAX_PLAYERS> hills_{};
    std::array<std::vector<int32_t>, sim::MAX_PLAYERS> cost_{};
    std::array<std::vector<int32_t>, sim::MAX_PLAYERS> comp_{};
    std::array<int32_t, sim::MAX_PLAYERS> comp_count_{};
    std::vector<PileInfo> piles_;
    std::vector<PowerUpInfo> powerups_;
    std::vector<FlowerInfo> flowers_;
};

}  // namespace ants::ai
