#include "ants_ai/map_info.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

const std::vector<int32_t>& no_field() {
    static const std::vector<int32_t> none;
    return none;
}

// The eight neighbours of a tile, the order of the engine's own search (N, NE, E, SE, S, SW, W, NW); the order cannot change a cost, only keeps the code the same shape
constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// Row 90 of a map taller than 90 rows: the engine's path finder never generates a tile of it (path_planner.cpp PathSearch::step skips every neighbour whose row is the invalid
// marker 0x5a = 90; the original does the same: Ants.exe 0x1019b23, docs/legacy/Ants.exe.c FUN_01019a66 `if (*local_14 != 0x5a)`)
bool in_sentinel_row(const sim::Grid& grid, sim::TileCoord t) noexcept {
    return t.y == MapInfo::kPathSentinelRow && grid.height() > static_cast<uint32_t>(MapInfo::kPathSentinelRow);
}

// The walkable tiles of the queue row of the hill whose origin is (bx, by), left to right, in the given mask
std::vector<sim::TileCoord> queue_starts(const std::vector<uint8_t>& mask, int w, int h, sim::TileCoord origin) {
    std::vector<sim::TileCoord> out;
    for (int dx = 0; dx <= 2; ++dx) {
        const sim::TileCoord t{origin.x + dx, origin.y - 1};
        if (t.x < 0 || t.y < 0 || t.x >= w || t.y >= h) continue;
        if (mask[static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x)] != 0) out.push_back(t);
    }
    return out;
}

}  // namespace

// ---- the engine's rules --------------------------------------------------------------------------------------------------------------------------------

// SimulationEngineImpl::can_enter for an ant that walks (no swimmer, fire ant or thief; no order that has a pile, a power-up, a bomb or a hill for its goal), rules R1, R3, R4, R5
// and R7 (R2, the occupants, and R6, the claims of team-mates, move with the ants), plus the path finder's own refusal of row 90. The engine's search (step_cost) blocks exactly the
// same tiles. NOT Grid::is_passable and not the tile's is_obstacle_overlay flag: the engine does not look at them (an overlay that the LVL file does not mark solid is walked over:
// TREASURE has one).
bool MapInfo::walkable(const sim::Grid& grid, uint8_t team, sim::TileCoord tile, const WalkContext& ctx) noexcept {
    if (!grid.in_bounds(tile)) return false;
    if (in_sentinel_row(grid, tile)) return false;
    if (!sim::movement::terrain_walkable(grid.terrain_class_at(tile))) return false;          // R1: water, unless a bridge stands on it (a bridge piece is mud)
    for (const assets::AnthillSpawn& ah : grid.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (tile.x >= bx && tile.x <= bx + 3 && tile.y >= by && tile.y <= by + 3) return false;                 // R3: the mound of any hill, its ramp and its hole too
        if (tile.y == by - 1 && tile.x >= bx && tile.x <= bx + 2 && ah.team_id != team && !ctx.dropped(ah.team_id)) return false;    // R5: another live team's queue row
    }
    if (grid.is_solid_object(tile)) return false;                                             // R4: obstacles, food, power-ups, lunchboxes, fire walls
    const sim::TileCell& cell = grid.get_cell(tile);
    return !(cell.has_bomb() && (cell.interactive_owner == team || cell.interactive_owner == ctx.ally));   // R7: a bomb of the team itself or of its ally
}

bool MapInfo::can_step_onto(const sim::Grid& grid, uint8_t team, sim::TileCoord cell, const WalkContext& ctx) noexcept {
    if (!grid.in_bounds(cell) || in_sentinel_row(grid, cell)) return false;
    if (!sim::movement::terrain_walkable(grid.terrain_class_at(cell))) return false;          // water (step_cost: a weight of 8000 on either tile is a blocked step)
    for (const assets::AnthillSpawn& ah : grid.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (cell.x >= bx && cell.x <= bx + 3 && cell.y >= by && cell.y <= by + 3) return false;                // R3: an own mound is a walk home, another one is closed
        if (cell.y == by - 1 && cell.x >= bx && cell.x <= bx + 2 && ah.team_id != team && !ctx.dropped(ah.team_id)) return false;   // R5, tested before the food exemption
    }
    return true;
}

uint32_t MapInfo::step_cost(const sim::Grid& grid, sim::TileCoord from, sim::TileCoord to) noexcept {
    const uint32_t ca = sim::movement::terrain_step_weight(grid.terrain_class_at(from), false);
    const uint32_t cb = sim::movement::terrain_step_weight(grid.terrain_class_at(to), false);
    constexpr uint32_t kBlocked = static_cast<uint32_t>(kPathCostLimit);                      // the engine's kBlockedCost: the weight of water for a walker
    if (ca == kBlocked || cb == kBlocked) return kBlocked;
    uint32_t s = ca + cb;
    if (from.x != to.x && from.y != to.y) {
        // The same double multiply, truncated, as the engine (fild / fmul qword [0x10049e0] / __ftol at 0x10208e8)
        s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
    }
    return s >> 1;
}

std::vector<uint8_t> MapInfo::walkable_mask(const sim::Grid& grid, uint8_t team, const WalkContext& ctx) {
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    std::vector<uint8_t> mask(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) mask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = walkable(grid, team, sim::TileCoord{x, y}, ctx) ? 1 : 0;
    }
    return mask;
}

std::vector<int32_t> MapInfo::cost_field(const sim::Grid& grid, uint8_t team, sim::TileCoord source, const WalkContext& ctx) {
    return cost_field(grid, walkable_mask(grid, team, ctx), source);
}

std::vector<int32_t> MapInfo::cost_field(const sim::Grid& grid, const std::vector<uint8_t>& mask, sim::TileCoord source) {
    return cost_field(grid, mask, std::vector<sim::TileCoord>{source});
}

namespace {

// The search behind the cost fields: `seeds` are (walkable tile, starting cost); a tile that is not walkable in `mask` is ignored
std::vector<int32_t> flood(const sim::Grid& grid, const std::vector<uint8_t>& mask, const std::vector<std::pair<sim::TileCoord, int32_t>>& seeds) {
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    std::vector<int32_t> d(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)), -1);
    const auto ok = [&](sim::TileCoord t) { return t.x >= 0 && t.y >= 0 && t.x < w && t.y < h && mask[static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x)] != 0; };
    if (w <= 0 || h <= 0 || mask.size() != d.size()) return d;
    using Item = std::pair<int32_t, int>;                                                       // (cost, index): the cost is final when popped, ties cannot matter
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (const auto& seed : seeds) {
        const sim::TileCoord source = seed.first;
        if (!ok(source)) continue;                                                              // an unwalkable source is ignored
        int32_t& slot = d[static_cast<size_t>(source.y) * static_cast<size_t>(w) + static_cast<size_t>(source.x)];
        if (slot >= 0 && slot <= seed.second) continue;
        slot = seed.second;
        open.push({seed.second, source.y * w + source.x});
    }
    while (!open.empty()) {
        const auto [cost, idx] = open.top();
        open.pop();
        if (cost != d[static_cast<size_t>(idx)]) continue;
        const sim::TileCoord cur{idx % w, idx / w};
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord n{cur.x + kDx[k], cur.y + kDy[k]};
            if (!ok(n)) continue;
            const int32_t nc = cost + static_cast<int32_t>(MapInfo::step_cost(grid, cur, n));
            int32_t& slot = d[static_cast<size_t>(n.y) * static_cast<size_t>(w) + static_cast<size_t>(n.x)];
            if (slot < 0 || nc < slot) {
                slot = nc;
                open.push({nc, n.y * w + n.x});
            }
        }
    }
    return d;
}

}  // namespace

std::vector<int32_t> MapInfo::cost_field(const sim::Grid& grid, const std::vector<uint8_t>& mask, const std::vector<sim::TileCoord>& sources) {
    std::vector<std::pair<sim::TileCoord, int32_t>> seeds;
    seeds.reserve(sources.size());
    for (const sim::TileCoord& s : sources) seeds.emplace_back(s, 0);
    return flood(grid, mask, seeds);
}

std::vector<int32_t> MapInfo::cost_field_onto(const sim::Grid& grid, const std::vector<uint8_t>& mask, uint8_t team, sim::TileCoord cell, const WalkContext& ctx) {
    std::vector<std::pair<sim::TileCoord, int32_t>> seeds;
    if (can_step_onto(grid, team, cell, ctx)) {
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord n{cell.x + kDx[k], cell.y + kDy[k]};
            if (!grid.in_bounds(n)) continue;
            const int32_t onto = static_cast<int32_t>(step_cost(grid, n, cell));
            if (onto >= kPathCostLimit) continue;                                               // the engine's search would give up before it
            seeds.emplace_back(n, onto);
        }
    }
    return flood(grid, mask, seeds);
}

// ---- construction ----------------------------------------------------------------------------------------------------------------------------------------

MapInfo::MapInfo(const sim::SimulationEngine& sim) {
    build(sim.grid());
    add_flowers(sim.grid(), sim.get_world_state().flower_droppers);
}

MapInfo::MapInfo(const sim::Grid& grid) { build(grid); }

Approach MapInfo::approach_in(const sim::Grid& grid, const std::vector<int32_t>& field, const std::vector<sim::TileCoord>& cells, uint8_t team, const WalkContext& ctx) const {
    Approach best;
    if (field.empty()) return best;
    for (const sim::TileCoord& c : cells) {
        if (!can_step_onto(grid, team, c, ctx)) continue;                                       // the engine refuses the last step: water, a mound, another team's queue row
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord n{c.x + kDx[k], c.y + kDy[k]};
            if (!inside(n)) continue;
            const int32_t base = field[at(n)];
            if (base < 0) continue;
            const int32_t total = base + static_cast<int32_t>(step_cost(grid, n, c));
            if (total >= kPathCostLimit) continue;                                              // the engine's search would give up before it
            if (best.cost < 0 || total < best.cost) {                                          // the first cell (reading order of `cells`) wins a tie
                best.cost = total;
                best.click = c;
            }
        }
    }
    return best;
}

void MapInfo::build(const sim::Grid& grid) {
    w_ = static_cast<int>(grid.width());
    h_ = static_cast<int>(grid.height());
    comp_count_.fill(0);
    if (w_ <= 0 || h_ <= 0) return;
    for (const assets::AnthillSpawn& a : grid.anthills()) {
        if (a.team_id >= sim::MAX_PLAYERS) continue;
        HillInfo& hi = hills_[a.team_id];
        if (hi.present) continue;                      // the FIRST hill of a team, as Grid::find_anthill picks it (a map without hill art can name a team twice)
        const int32_t bx = static_cast<int32_t>(a.x);
        const int32_t by = static_cast<int32_t>(a.y);
        hi.present = true;
        hi.origin = {bx, by};
        hi.mouth = {bx + 1, by};
        hi.entrance = {bx + 1, by + 1};
        hi.queue = {bx + 1, by - 1};
        hi.raid = {bx + 3, by + 2};
        hi.tile42 = {bx + 4, by + 4};
    }

    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        // walker components: a flood fill over the tiles the team's ants can stand on, eight neighbours
        const std::vector<uint8_t> mask = walkable_mask(grid, t);                  // the engine's rules asked once per tile, for the components and the cost field
        const auto walks = [&](sim::TileCoord n) { return inside(n) && mask[at(n)] != 0; };
        std::vector<int32_t>& comp = comp_[t];
        comp.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_), -1);
        int32_t labels = 0;
        std::vector<int> stack;
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                if (comp[at({x, y})] >= 0 || !walks({x, y})) continue;
                comp[at({x, y})] = labels;
                stack.assign(1, y * w_ + x);
                while (!stack.empty()) {
                    const int idx = stack.back();
                    stack.pop_back();
                    for (int k = 0; k < 8; ++k) {
                        const sim::TileCoord n{idx % w_ + kDx[k], idx / w_ + kDy[k]};
                        if (!walks(n) || comp[at(n)] >= 0) continue;
                        comp[at(n)] = labels;
                        stack.push_back(n.y * w_ + n.x);
                    }
                }
                ++labels;
            }
        }
        comp_count_[t] = labels;
        if (hills_[t].present) {
            // an ant on the ramp steps onto any walkable tile of the queue row (the outer two diagonally): every one of them is a start, the engine does not need the middle one
            hills_[t].starts = queue_starts(mask, w_, h_, hills_[t].origin);
            cost_[t] = cost_field(grid, mask, hills_[t].starts);
        }
    }

    // piles: the footprint of every object as the engine attributes the cells, and how far each is for each team
    const std::vector<sim::FoodObject>& objects = grid.food_objects();
    piles_.resize(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        PileInfo& p = piles_[i];
        p.index = static_cast<uint32_t>(i);
        p.anchor = {objects[i].col, objects[i].row};
        p.units = objects[i].units;
        p.value = objects[i].value;
        const int32_t first = grid.food_object_first_at(p.anchor);
        p.bite_index = first >= 0 ? static_cast<uint32_t>(first) : p.index;
    }
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const sim::TileCoord c{x, y};
            if (!grid.get_cell(c).is_food) continue;
            const int32_t o = grid.food_object_at_cell(c);
            if (o >= 0 && static_cast<size_t>(o) < piles_.size()) piles_[static_cast<size_t>(o)].cells.push_back(c);
        }
    }
    const WalkContext start_ctx;
    for (PileInfo& p : piles_) {
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) p.approach[t] = approach_in(grid, cost_[t], p.cells, t, start_ctx);
    }

    // power-ups
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const sim::TileCoord c{x, y};
            const sim::TileCell& cell = grid.get_cell(c);
            if (!cell.has_powerup() || cell.powerup_type < 1 || cell.powerup_type > 5) continue;
            PowerUpInfo pu;
            pu.tile = c;
            pu.type = static_cast<sim::AntType>(cell.powerup_type);                              // 1 Bomber, 2 Fire, 3 Thief, 4 Combat, 5 Swimmer: the AntType numbers
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) pu.approach[t] = approach_in(grid, cost_[t], std::vector<sim::TileCoord>{c}, t, start_ctx);
            powerups_.push_back(pu);
        }
    }
}

void MapInfo::add_flowers(const sim::Grid& grid, const std::vector<sim::FlowerDropperSnapshot>& droppers) {
    if (w_ <= 0 || h_ <= 0) return;
    const WalkContext start_ctx;
    for (const sim::FlowerDropperSnapshot& d : droppers) {
        FlowerInfo f;
        f.plant = sim::TileCoord{d.x, d.y};
        f.drop = sim::TileCoord{d.drop_x, d.drop_y};
        if (inside(f.drop)) {
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) f.approach[t] = approach_in(grid, cost_[t], std::vector<sim::TileCoord>{f.drop}, t, start_ctx);
        }
        flowers_.push_back(f);
    }
}

const FlowerInfo* MapInfo::flower_at(sim::TileCoord tile) const noexcept {
    for (const FlowerInfo& f : flowers_) {
        if (f.drop == tile) return &f;
    }
    return nullptr;
}

// ---- queries ---------------------------------------------------------------------------------------------------------------------------------------------

const HillInfo& MapInfo::hill(uint8_t team) const noexcept {
    static const HillInfo none;
    return team < sim::MAX_PLAYERS ? hills_[team] : none;
}

const std::vector<int32_t>& MapInfo::cost_field_of(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS ? cost_[team] : no_field(); }

int32_t MapInfo::walking_cost(uint8_t team, sim::TileCoord tile) const noexcept {
    if (team >= sim::MAX_PLAYERS || cost_[team].empty() || !inside(tile)) return -1;
    return cost_[team][at(tile)];
}

int32_t MapInfo::component(uint8_t team, sim::TileCoord tile) const noexcept {
    if (team >= sim::MAX_PLAYERS || comp_[team].empty() || !inside(tile)) return -1;
    return comp_[team][at(tile)];
}

int32_t MapInfo::ant_component(uint8_t team, sim::TileCoord tile) const noexcept {
    const int32_t own = component(team, tile);
    if (own >= 0) return own;
    int32_t best = -1;
    for (int k = 0; k < 8; ++k) {
        const int32_t c = component(team, {tile.x + kDx[k], tile.y + kDy[k]});
        if (c >= 0 && (best < 0 || c < best)) best = c;
    }
    return best;
}

int32_t MapInfo::hill_component(uint8_t team) const noexcept {
    if (team >= sim::MAX_PLAYERS || !hills_[team].present || hills_[team].starts.empty()) return -1;
    return component(team, hills_[team].starts.front());
}

int32_t MapInfo::component_count(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS ? comp_count_[team] : 0; }

bool MapInfo::can_reach_pile(uint8_t team, sim::TileCoord from, uint32_t pile) const noexcept {
    if (pile >= piles_.size() || team >= sim::MAX_PLAYERS) return false;
    const int32_t mine = ant_component(team, from);
    if (mine < 0) return false;
    for (const sim::TileCoord& c : piles_[pile].cells) {
        for (int k = 0; k < 8; ++k) {
            if (component(team, {c.x + kDx[k], c.y + kDy[k]}) == mine) return true;
        }
    }
    return false;
}

uint64_t MapInfo::reachable_points(uint8_t team) const noexcept {
    if (team >= sim::MAX_PLAYERS) return 0;
    uint64_t sum = 0;
    for (const PileInfo& p : piles_) {
        if (p.approach[team].reachable()) sum += static_cast<uint64_t>(piles_[p.bite_index].units) * p.value;
    }
    return sum;
}

uint64_t MapInfo::reachable_points() const noexcept {
    uint64_t sum = 0;
    for (const PileInfo& p : piles_) {
        bool any = false;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) any = any || p.approach[t].reachable();
        if (any) sum += static_cast<uint64_t>(piles_[p.bite_index].units) * p.value;
    }
    return sum;
}

uint64_t MapInfo::total_points() const noexcept {
    uint64_t sum = 0;
    for (const PileInfo& p : piles_) sum += static_cast<uint64_t>(p.units) * p.value;
    return sum;
}

int32_t MapInfo::trip_ticks(uint8_t team, uint32_t pile) const noexcept {
    if (pile >= piles_.size() || team >= sim::MAX_PLAYERS) return -1;
    return trip_ticks_for_cost(piles_[pile].approach[team].cost);
}

MapInfo::NowField MapInfo::field_now(const sim::Grid& grid, uint8_t team, const WalkContext& ctx) const {
    NowField out;
    out.team = team;
    out.ctx = ctx;
    if (team >= sim::MAX_PLAYERS || !hills_[team].present || static_cast<int>(grid.width()) != w_ || static_cast<int>(grid.height()) != h_) return out;
    const std::vector<uint8_t> mask = walkable_mask(grid, team, ctx);
    out.cost = cost_field(grid, mask, queue_starts(mask, w_, h_, hills_[team].origin));        // the queue row as it is now: a bomb or a fire wall may lie on a tile of it
    return out;
}

bool MapInfo::reaches_hill(const NowField& field, sim::TileCoord tile, sim::AntType type) const noexcept {
    if (!field.valid() || type == sim::AntType::Swimmer) return true;
    const sim::TileCoord o = hills_[field.team].origin;
    if (tile.x >= o.x && tile.x <= o.x + 3 && tile.y >= o.y && tile.y <= o.y + 3) return true;       // on the hill itself (the mound has no walkable tile)
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const sim::TileCoord t{tile.x + dx, tile.y + dy};
            if (inside(t) && at(t) < field.cost.size() && field.cost[at(t)] >= 0) return true;
        }
    }
    return false;
}

Approach MapInfo::approach_now(const sim::Grid& grid, uint32_t pile, const NowField& field) const {
    if (!field.valid() || static_cast<int>(grid.width()) != w_ || static_cast<int>(grid.height()) != h_) return Approach{};
    const std::vector<sim::FoodObject>& table = grid.food_objects();
    if (pile >= table.size() || table[pile].remaining == 0) return Approach{};                // nothing left to walk to (a leftover crumb of the picture is not a pile)
    // the cells of the object now: the cells within a window of the anchor that the engine still attributes to it (footprints are a few tiles wide). The anchor comes from the
    // engine's table, not from the analysis: the table grows during a match (a lunchbox is an object of its own at the end of it)
    const sim::TileCoord anchor{table[pile].col, table[pile].row};
    std::vector<sim::TileCoord> cells;
    const int r = 8;
    for (int y = std::max(0, anchor.y - r); y <= std::min(h_ - 1, anchor.y + r); ++y) {
        for (int x = std::max(0, anchor.x - r); x <= std::min(w_ - 1, anchor.x + r); ++x) {
            const sim::TileCoord c{x, y};
            if (grid.get_cell(c).is_food && grid.food_object_at_cell(c) == static_cast<int32_t>(pile)) cells.push_back(c);
        }
    }
    if (cells.empty()) return Approach{};
    return approach_in(grid, field.cost, cells, field.team, field.ctx);
}

Approach MapInfo::approach_now(const sim::Grid& grid, uint8_t team, uint32_t pile, const WalkContext& ctx) const {
    return approach_now(grid, pile, field_now(grid, team, ctx));
}

}  // namespace ants::ai
