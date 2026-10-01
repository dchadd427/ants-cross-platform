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

}  // namespace

// ---- the engine's rules --------------------------------------------------------------------------------------------------------------------------------

// SimulationEngineImpl::can_enter for an ant that walks (no swimmer, fire ant or thief; no order that has a pile, a power-up, a bomb or a hill for its goal), rules R1, R3, R4
// and R5 (R2, the occupants, and R6, the claims of team-mates, move with the ants; R7, the bombs, is dynamic and none lie on the map at the start). The engine's search
// (step_cost) blocks exactly the same tiles. NOT Grid::is_passable and not the tile's is_obstacle_overlay flag: the engine does not look at them (an overlay that the LVL file
// does not mark solid is walked over: TREASURE has one).
bool MapInfo::walkable(const sim::Grid& grid, uint8_t team, sim::TileCoord tile) noexcept {
    if (!grid.in_bounds(tile)) return false;
    if (!sim::movement::terrain_walkable(grid.terrain_class_at(tile))) return false;          // R1: water, unless a bridge stands on it (a bridge piece is mud)
    for (const assets::AnthillSpawn& ah : grid.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (tile.x >= bx && tile.x <= bx + 3 && tile.y >= by && tile.y <= by + 3) return false;                 // R3: the mound of any hill, its ramp and its hole too
        if (tile.y == by - 1 && tile.x >= bx && tile.x <= bx + 2 && ah.team_id != team) return false;            // R5: another team's queue row
    }
    return !grid.is_solid_object(tile);                                                       // R4: obstacles, food, power-ups, lunchboxes, fire walls
}

uint32_t MapInfo::step_cost(const sim::Grid& grid, sim::TileCoord from, sim::TileCoord to) noexcept {
    const uint32_t ca = sim::movement::terrain_step_weight(grid.terrain_class_at(from), false);
    const uint32_t cb = sim::movement::terrain_step_weight(grid.terrain_class_at(to), false);
    uint32_t s = ca + cb;
    if (from.x != to.x && from.y != to.y) {
        // The same double multiply, truncated, as the engine (fild / fmul qword [0x10049e0] / __ftol at 0x10208e8)
        s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
    }
    return s >> 1;
}

std::vector<uint8_t> MapInfo::walkable_mask(const sim::Grid& grid, uint8_t team) {
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    std::vector<uint8_t> mask(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) mask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = walkable(grid, team, sim::TileCoord{x, y}) ? 1 : 0;
    }
    return mask;
}

std::vector<int32_t> MapInfo::cost_field(const sim::Grid& grid, uint8_t team, sim::TileCoord source) { return cost_field(grid, walkable_mask(grid, team), source); }

std::vector<int32_t> MapInfo::cost_field(const sim::Grid& grid, const std::vector<uint8_t>& mask, sim::TileCoord source) {
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    std::vector<int32_t> d(static_cast<size_t>(std::max(w, 0)) * static_cast<size_t>(std::max(h, 0)), -1);
    const auto ok = [&](sim::TileCoord t) { return t.x >= 0 && t.y >= 0 && t.x < w && t.y < h && mask[static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x)] != 0; };
    if (w <= 0 || h <= 0 || mask.size() != d.size() || !ok(source)) return d;
    using Item = std::pair<int32_t, int>;                                                       // (cost, index): the cost is final when popped, ties cannot matter
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    d[static_cast<size_t>(source.y) * static_cast<size_t>(w) + static_cast<size_t>(source.x)] = 0;
    open.push({0, source.y * w + source.x});
    while (!open.empty()) {
        const auto [cost, idx] = open.top();
        open.pop();
        if (cost != d[static_cast<size_t>(idx)]) continue;
        const sim::TileCoord cur{idx % w, idx / w};
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord n{cur.x + kDx[k], cur.y + kDy[k]};
            if (!ok(n)) continue;
            const int32_t nc = cost + static_cast<int32_t>(step_cost(grid, cur, n));
            int32_t& slot = d[static_cast<size_t>(n.y) * static_cast<size_t>(w) + static_cast<size_t>(n.x)];
            if (slot < 0 || nc < slot) {
                slot = nc;
                open.push({nc, n.y * w + n.x});
            }
        }
    }
    return d;
}

// ---- construction ----------------------------------------------------------------------------------------------------------------------------------------

MapInfo::MapInfo(const sim::SimulationEngine& sim) { build(sim.grid()); }

MapInfo::MapInfo(const sim::Grid& grid) { build(grid); }

Approach MapInfo::approach_in(const sim::Grid& grid, const std::vector<int32_t>& field, const std::vector<sim::TileCoord>& cells) const {
    Approach best;
    if (field.empty()) return best;
    for (const sim::TileCoord& c : cells) {
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
        const int32_t bx = static_cast<int32_t>(a.x);
        const int32_t by = static_cast<int32_t>(a.y);
        HillInfo& hi = hills_[a.team_id];
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
        if (hills_[t].present) cost_[t] = cost_field(grid, mask, hills_[t].queue);
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
    for (PileInfo& p : piles_) {
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) p.approach[t] = approach_in(grid, cost_[t], p.cells);
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
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) pu.approach[t] = approach_in(grid, cost_[t], std::vector<sim::TileCoord>{c});
            powerups_.push_back(pu);
        }
    }
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

int32_t MapInfo::hill_component(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS && hills_[team].present ? component(team, hills_[team].queue) : -1; }

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

uint32_t MapInfo::reachable_points(uint8_t team) const noexcept {
    if (team >= sim::MAX_PLAYERS) return 0;
    uint32_t sum = 0;
    for (const PileInfo& p : piles_) {
        if (p.approach[team].reachable()) sum += static_cast<uint32_t>(piles_[p.bite_index].units) * p.value;
    }
    return sum;
}

uint32_t MapInfo::reachable_points() const noexcept {
    uint32_t sum = 0;
    for (const PileInfo& p : piles_) {
        bool any = false;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) any = any || p.approach[t].reachable();
        if (any) sum += static_cast<uint32_t>(piles_[p.bite_index].units) * p.value;
    }
    return sum;
}

uint32_t MapInfo::total_points() const noexcept {
    uint32_t sum = 0;
    for (const PileInfo& p : piles_) sum += static_cast<uint32_t>(p.units) * p.value;
    return sum;
}

int32_t MapInfo::trip_ticks(uint8_t team, uint32_t pile) const noexcept {
    if (pile >= piles_.size() || team >= sim::MAX_PLAYERS) return -1;
    return trip_ticks_for_cost(piles_[pile].approach[team].cost);
}

Approach MapInfo::approach_now(const sim::Grid& grid, uint8_t team, uint32_t pile) const {
    if (pile >= piles_.size() || team >= sim::MAX_PLAYERS || !hills_[team].present || static_cast<int>(grid.width()) != w_ || static_cast<int>(grid.height()) != h_) return Approach{};
    const PileInfo& p = piles_[pile];
    if (pile >= grid.food_objects().size() || grid.food_objects()[pile].remaining == 0) return Approach{};      // nothing left to walk to (a leftover crumb of the picture is not a pile)
    // the cells of the object now: the cells within a window of the anchor that the engine still attributes to it (footprints are a few tiles wide)
    std::vector<sim::TileCoord> cells;
    const int r = 8;
    for (int y = std::max(0, p.anchor.y - r); y <= std::min(h_ - 1, p.anchor.y + r); ++y) {
        for (int x = std::max(0, p.anchor.x - r); x <= std::min(w_ - 1, p.anchor.x + r); ++x) {
            const sim::TileCoord c{x, y};
            if (grid.get_cell(c).is_food && grid.food_object_at_cell(c) == static_cast<int32_t>(pile)) cells.push_back(c);
        }
    }
    if (cells.empty()) return Approach{};
    return approach_in(grid, cost_field(grid, team, hills_[team].queue), cells);
}

}  // namespace ants::ai
