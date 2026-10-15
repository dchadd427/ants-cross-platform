#include "ants_ai/gate_info.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <queue>

#include "ants_ai/island_info.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr int32_t kFar = std::numeric_limits<int32_t>::max();

sim::TileCoord step_in(sim::TileCoord t, int dir, int range) noexcept {
    const sim::movement::TileDelta d = sim::movement::dir_delta(static_cast<uint8_t>(dir));
    return sim::TileCoord{t.x + d.dcol * range, t.y + d.drow * range};
}

// The tiles around `cells` (8 neighbours of each, the cells themselves too), each once, in reading order
std::vector<sim::TileCoord> around(const std::vector<sim::TileCoord>& cells, int w, int h) {
    std::vector<uint8_t> seen(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    std::vector<sim::TileCoord> out;
    for (const sim::TileCoord& c : cells) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const sim::TileCoord t{c.x + dx, c.y + dy};
                if (t.x < 0 || t.y < 0 || t.x >= w || t.y >= h) continue;
                uint8_t& s = seen[static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x)];
                if (s) continue;
                s = 1;
                out.push_back(t);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const sim::TileCoord& a, const sim::TileCoord& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    return out;
}

}  // namespace

BombHop GateInfo::simulate(const sim::Grid& grid, sim::TileCoord bomb, uint8_t facing) {
    BombHop h;
    h.bomb = bomb;
    h.facing = facing;
    sim::TileCoord at = bomb;
    int face = facing % 8;
    bool decided = false;
    for (int n = 0; n < kMaxChain; ++n) {
        ++h.bombs;
        const int start = (face + 4) % 8;                                       // pick_landing: the direction opposite the facing, then the next ones clockwise
        sim::TileCoord land{-1, -1};
        int dir = -1;
        for (int i = 0; i < 8 && dir < 0; ++i) {
            const int d = (start + i) % 8;
            const sim::TileCoord k = step_in(at, d, kFlightRange);
            if (flight_landing_ok(grid, k)) {
                land = k;
                dir = d;
            }
        }
        if (dir < 0) {                                                          // no free tile: no flight, the ant burns where it stands
            h.lands.push_back(at);
            h.end = HopEnd::Stuck;
            decided = true;
            break;
        }
        h.lands.push_back(land);
        face = (dir + 4) % 8;                                                   // bomb_victim: the clip faces the way the ant came from
        if (grid.has_bomb_at(land)) {
            at = land;
            continue;
        }
        break;
    }
    if (!decided) {
        const sim::TileCoord last = h.lands.back();
        if (grid.has_fire_at(last)) h.end = HopEnd::Fire;
        else if (grid.terrain_class_at(last) == sim::movement::kTerrainWater) h.end = HopEnd::Water;
        else h.end = HopEnd::Ground;
    }
    h.hp = kBombHp * h.bombs + (h.end == HopEnd::Fire ? 1 : 0);
    int32_t p = 100;
    for (int32_t i = 0; i < h.bombs; ++i) p = p * (100 - kDudPercent) / 100;
    h.fly_percent = p;
    return h;
}

int32_t GateInfo::component(sim::TileCoord tile) const noexcept {
    if (tile.x < 0 || tile.y < 0 || tile.x >= w_ || tile.y >= h_) return -1;
    return comp_of_[static_cast<size_t>(tile.y) * static_cast<size_t>(w_) + static_cast<size_t>(tile.x)];
}

GateInfo GateInfo::analyse(const MapInfo& map, const sim::Grid& grid, uint8_t team) {
    GateInfo out;
    const int w = map.width();
    const int h = map.height();
    if (team >= sim::MAX_PLAYERS || !map.hill(team).present || w <= 0 || h <= 0 || static_cast<int>(grid.width()) != w || static_cast<int>(grid.height()) != h) return out;
    const auto at = [&](sim::TileCoord t) { return static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x); };
    const size_t cells = static_cast<size_t>(w) * static_cast<size_t>(h);
    const std::vector<uint8_t> walk = MapInfo::walkable_mask(grid, team);     // fire walls are solid, bombs are not: a bomb tile is walkable here
    std::vector<uint8_t> is_fire(cells, 0), is_bomb(cells, 0), safe(cells, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const sim::TileCoord t{x, y};
            const size_t i = at(t);
            is_fire[i] = grid.has_fire_at(t) ? 1 : 0;
            is_bomb[i] = grid.has_bomb_at(t) ? 1 : 0;
            safe[i] = (walk[i] && !is_bomb[i] && !is_fire[i]) ? 1 : 0;
            out.fire_cells_ += is_fire[i];
            out.bomb_cells_ += is_bomb[i];
        }
    }
    out.w_ = w;
    out.h_ = h;
    out.valid_ = true;

    // ---- the safe components (8 neighbours, no corner rule, as the engine's walkers) -------------------------------------------------------------------------
    out.comp_of_.assign(cells, -1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const sim::TileCoord s{x, y};
            if (!safe[at(s)] || out.comp_of_[at(s)] >= 0) continue;
            const int32_t id = static_cast<int32_t>(out.comps_.size());
            GateComponent gc;
            gc.id = id;
            std::deque<sim::TileCoord> queue{s};
            out.comp_of_[at(s)] = id;
            while (!queue.empty()) {
                const sim::TileCoord t = queue.front();
                queue.pop_front();
                ++gc.tiles;
                for (int d = 0; d < 8; ++d) {
                    const sim::TileCoord n = step_in(t, d, 1);
                    if (n.x < 0 || n.y < 0 || n.x >= w || n.y >= h || !safe[at(n)] || out.comp_of_[at(n)] >= 0) continue;
                    out.comp_of_[at(n)] = id;
                    queue.push_back(n);
                }
            }
            out.comps_.push_back(gc);
        }
    }
    const std::vector<sim::TileCoord>& starts = map.hill(team).starts;
    for (const sim::TileCoord& s : starts) {
        if (out.home_ < 0) out.home_ = out.component(s);
    }
    if (out.home_ >= 0) out.comps_[static_cast<size_t>(out.home_)].hill = true;

    // ---- the barriers: connected groups of fire cells and of bomb cells ------------------------------------------------------------------------------------
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<uint8_t>& mark = pass == 0 ? is_fire : is_bomb;
        std::vector<uint8_t> done(cells, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const sim::TileCoord s{x, y};
                if (!mark[at(s)] || done[at(s)]) continue;
                GateBarrier b;
                b.kind = pass == 0 ? GateKind::Fire : GateKind::Bombs;
                b.min = b.max = s;
                std::deque<sim::TileCoord> queue{s};
                done[at(s)] = 1;
                while (!queue.empty()) {
                    const sim::TileCoord t = queue.front();
                    queue.pop_front();
                    ++b.cells;
                    b.min = {std::min(b.min.x, t.x), std::min(b.min.y, t.y)};
                    b.max = {std::max(b.max.x, t.x), std::max(b.max.y, t.y)};
                    for (int d = 0; d < 8; ++d) {
                        const sim::TileCoord n = step_in(t, d, 1);
                        if (n.x < 0 || n.y < 0 || n.x >= w || n.y >= h || !mark[at(n)] || done[at(n)]) continue;
                        done[at(n)] = 1;
                        queue.push_back(n);
                    }
                }
                out.barriers_.push_back(b);
            }
        }
    }

    // ---- the walks: the fewest fire cells / bombs on a walk from the hill (0-1 search over the tiles; a closed kind is not entered) ------------------------------
    std::vector<int32_t> fire_parent(cells, -1);                                 // the walk of the fire search: the tile it came from (a start has none)
    std::vector<int32_t> bomb_parent(cells, -1);                                 // the walk of the bomb search: the tile it came from (a start has none)
    const auto search = [&](bool fire_open, bool bombs_open, std::vector<int32_t>* parent = nullptr) {
        std::vector<int32_t> dist(cells, kFar);
        std::deque<sim::TileCoord> queue;
        for (const sim::TileCoord& s : starts) {
            if (s.x < 0 || s.y < 0 || s.x >= w || s.y >= h || !walk[at(s)]) continue;
            if (is_bomb[at(s)] && !bombs_open) continue;                           // (Bombz Away lays a bomb on the tiles in front of the ramp: the first step costs a bomb)
            dist[at(s)] = is_bomb[at(s)] ? 1 : 0;
            queue.push_back(s);
        }
        while (!queue.empty()) {
            const sim::TileCoord t = queue.front();
            queue.pop_front();
            for (int d = 0; d < 8; ++d) {
                const sim::TileCoord n = step_in(t, d, 1);
                if (n.x < 0 || n.y < 0 || n.x >= w || n.y >= h) continue;
                const size_t i = at(n);
                int32_t cost = 0;
                if (is_fire[i]) {
                    if (!fire_open) continue;
                    cost = 1;
                } else if (!walk[i]) {
                    continue;
                } else if (is_bomb[i]) {
                    if (!bombs_open) continue;
                    cost = 1;
                }
                const int32_t nd = dist[at(t)] + cost;
                if (nd >= dist[i]) continue;
                dist[i] = nd;
                if (parent != nullptr) (*parent)[i] = static_cast<int32_t>(at(t));
                if (cost == 0) queue.push_front(n);
                else queue.push_back(n);
            }
        }
        return dist;
    };
    const std::vector<int32_t> d_free = search(false, false);
    const std::vector<int32_t> d_fire = search(true, false, &fire_parent);
    const std::vector<int32_t> d_bomb = search(false, true, &bomb_parent);
    const std::vector<int32_t> d_both = search(true, true);
    std::vector<int32_t> swim_parent(cells, -1);
    std::vector<int32_t> d_swim(cells, kFar);
    if (out.home_ >= 0) {
        using Node = std::pair<int32_t, int32_t>;
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const sim::TileCoord s{x, y};
                if (out.comp_of_[at(s)] != out.home_) continue;
                d_swim[at(s)] = 0;
                pq.push({0, static_cast<int32_t>(at(s))});
            }
        }
        while (!pq.empty()) {
            const auto [cur_d, idx] = pq.top();
            pq.pop();
            if (cur_d > d_swim[static_cast<size_t>(idx)]) continue;
            const sim::TileCoord t{idx % w, idx / w};
            for (int d = 0; d < 8; d += 2) {
                const sim::TileCoord n = step_in(t, d, 1);
                if (n.x < 0 || n.y < 0 || n.x >= w || n.y >= h) continue;
                const size_t i = at(n);
                const bool water = grid.terrain_class_at(n) == sim::movement::kTerrainWater && grid.get_cell(n).is_empty_overlay() && !grid.is_solid_object(n);
                int32_t cost = 1;
                if (is_fire[i] || is_bomb[i]) cost = 1001;
                else if (!walk[i] && !water && !grid.has_powerup_at(n)) continue;
                const int32_t nd = cur_d + cost;
                if (nd >= d_swim[i]) continue;
                d_swim[i] = nd;
                swim_parent[i] = idx;
                pq.push({nd, static_cast<int32_t>(i)});
            }
        }
    }
    const auto least = [&](const std::vector<int32_t>& dist, const std::vector<sim::TileCoord>& tiles, sim::TileCoord* where = nullptr) {
        int32_t best = -1;
        for (const sim::TileCoord& t : tiles) {
            const int32_t v = dist[at(t)];
            if (v != kFar && (best < 0 || v < best)) {
                best = v;
                if (where != nullptr) *where = t;
            }
        }
        return best;
    };

    // ---- the targets ---------------------------------------------------------------------------------------------------------------------------------------------
    const auto classify = [&](GateTarget& g, const std::vector<sim::TileCoord>& cells_of, bool walkable_cells) {
        std::vector<sim::TileCoord> tiles = around(cells_of, w, h);
        if (!walkable_cells) {                                                   // a solid pile: an ant stands next to it, never on it
            tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const sim::TileCoord& t) { return std::find(cells_of.begin(), cells_of.end(), t) != cells_of.end(); }), tiles.end());
        }
        for (const sim::TileCoord& t : tiles) {
            const int32_t c = out.component(t);
            if (c >= 0 && std::find(g.comps.begin(), g.comps.end(), c) == g.comps.end()) g.comps.push_back(c);
        }
        std::sort(g.comps.begin(), g.comps.end());
        const int32_t free_cost = least(d_free, tiles);
        sim::TileCoord fire_end{-1, -1};
        g.fire_cells = least(d_fire, tiles, &fire_end);
        if (g.fire_cells > 0) {                                                  // the fire cell nearest the hill on that walk: walk back from the end of it
            for (int32_t i = static_cast<int32_t>(at(fire_end)); i >= 0; i = fire_parent[static_cast<size_t>(i)]) {
                if (is_fire[static_cast<size_t>(i)]) g.fire_cell = sim::TileCoord{i % w, i / w};
            }
        }
        sim::TileCoord bomb_end{-1, -1};
        g.defuse_bombs = least(d_bomb, tiles, &bomb_end);
        if (g.defuse_bombs > 0) {                                                // the bomb cell nearest the hill on that walk: walk back from the end of it
            for (int32_t i = static_cast<int32_t>(at(bomb_end)); i >= 0; i = bomb_parent[static_cast<size_t>(i)]) {
                if (is_bomb[static_cast<size_t>(i)]) g.bomb_cell = sim::TileCoord{i % w, i / w};
            }
        }
        const int32_t mixed = least(d_both, tiles);
        if (free_cost >= 0) g.gate = GateKind::None;
        else if (g.fire_cells >= 0 && g.defuse_bombs >= 0) g.gate = GateKind::Either;
        else if (g.fire_cells >= 0) g.gate = GateKind::Fire;
        else if (g.defuse_bombs >= 0) g.gate = GateKind::Bombs;
        else if (mixed >= 0) g.gate = GateKind::Mixed;
        else g.gate = GateKind::Sealed;
        if (!walkable_cells && g.gate == GateKind::Sealed) {
            sim::TileCoord swim_end{-1, -1};
            if (least(d_swim, tiles, &swim_end) >= 0) {
                int32_t s_fires = 0;
                int32_t s_bombs = 0;
                sim::TileCoord s_fire_cell{-1, -1};
                sim::TileCoord s_bomb_cell{-1, -1};
                bool crosses_water = false;
                for (int32_t i = static_cast<int32_t>(at(swim_end)); i >= 0; i = swim_parent[static_cast<size_t>(i)]) {
                    const sim::TileCoord cur{i % w, i / w};
                    if (grid.terrain_class_at(cur) == sim::movement::kTerrainWater) crosses_water = true;
                    if (is_fire[static_cast<size_t>(i)]) {
                        ++s_fires;
                        s_fire_cell = cur;
                    } else if (is_bomb[static_cast<size_t>(i)]) {
                        ++s_bombs;
                        s_bomb_cell = cur;
                    }
                }
                if (crosses_water) {
                    g.shore_fire_cells = s_fires;
                    g.shore_bombs = s_bombs;
                    g.shore_fire_cell = s_fire_cell;
                    g.shore_bomb_cell = s_bomb_cell;
                }
            }
        }
    };
    for (const PileInfo& p : map.piles()) {
        GateTarget g;
        g.index = p.index;
        if (!p.cells.empty()) classify(g, p.cells, false);
        out.piles_.push_back(std::move(g));
    }
    for (size_t i = 0; i < map.powerups().size(); ++i) {
        GateTarget g;
        g.powerup = true;
        g.index = static_cast<uint32_t>(i);
        classify(g, std::vector<sim::TileCoord>{map.powerups()[i].tile}, true);
        out.powerups_.push_back(std::move(g));
    }

    // ---- the hops from the home component ------------------------------------------------------------------------------------------------------------------
    if (out.home_ >= 0) {                                                        // (no safe tile in front of the ramp, home_ == -1: no ant of the hill can stand where a Bomber works, and component() is -1 for every unsafe tile and must not match it)
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const sim::TileCoord t{x, y};
                if (!bomb_ground(grid, t)) continue;
                bool planter = false;                                                // the Bomber works from a cardinal neighbour of the tile that it can stand on
                for (int d = 0; d < 8; d += 2) planter = planter || out.component(step_in(t, d, 1)) == out.home_;
                if (!planter) continue;
                for (int f = 0; f < 8; ++f) {
                    if (out.component(step_in(t, f, -1)) != out.home_) continue;     // the ant steps onto T from P = T - f
                    if (grid.has_powerup_at(step_in(t, f, -1))) continue;             // P must not hold a power-up: a click on it takes the power-up (and the controller refuses the click)
                    BombHop hop = simulate(grid, t, static_cast<uint8_t>(f));
                    hop.from = step_in(t, f, -1);
                    hop.from_comp = out.home_;
                    if (hop.end != HopEnd::Ground) continue;
                    hop.to_comp = out.component(hop.land());
                    if (hop.to_comp < 0 || hop.to_comp == out.home_) continue;
                    out.hops_.push_back(std::move(hop));
                }
            }
        }
    }
    const auto better = [&](int32_t a, int32_t b) {                              // the hop a is better than the hop b: fewer bombs (the chance follows from the number)
        if (b < 0) return true;
        const BombHop& x = out.hops_[static_cast<size_t>(a)];
        const BombHop& y = out.hops_[static_cast<size_t>(b)];
        if (x.bombs != y.bombs) return x.bombs < y.bombs;
        return false;                                                            // (the same number of bombs is the same chance: the first in the scan wins)
    };
    for (std::vector<GateTarget>* list : {&out.piles_, &out.powerups_}) {
        for (GateTarget& g : *list) {
            if (g.gate == GateKind::None) continue;
            for (size_t i = 0; i < out.hops_.size(); ++i) {
                if (std::find(g.comps.begin(), g.comps.end(), out.hops_[i].to_comp) == g.comps.end()) continue;
                if (better(static_cast<int32_t>(i), g.hop)) g.hop = static_cast<int32_t>(i);
            }
            if (g.fire_cells >= 0) g.passes = static_cast<uint8_t>(g.passes | kPassFireAnt);
            if (g.defuse_bombs >= 0) g.passes = static_cast<uint8_t>(g.passes | kPassDefuse);
            if (g.hop >= 0) g.passes = static_cast<uint8_t>(g.passes | kPassHop);
        }
    }
    return out;
}

BombHop GateInfo::best_hop_between(const GateInfo& gates, const sim::Grid& grid, int32_t from_comp, int32_t to_comp) {
    BombHop best;
    if (gates.empty() || from_comp < 0 || to_comp < 0 || from_comp == to_comp) return best;
    const int w = gates.w_;
    const int h = gates.h_;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const sim::TileCoord t{x, y};
            if (!bomb_ground(grid, t)) continue;
            bool planter = false;
            for (int d = 0; d < 8; d += 2) planter = planter || gates.component(step_in(t, d, 1)) == from_comp;
            if (!planter) continue;
            for (int f = 0; f < 8; ++f) {
                const sim::TileCoord from = step_in(t, f, -1);
                if (gates.component(from) != from_comp) continue;
                if (grid.has_powerup_at(from)) continue;                          // (see analyse: P holds no power-up)
                BombHop hop = simulate(grid, t, static_cast<uint8_t>(f));
                if (hop.end != HopEnd::Ground) continue;
                hop.from = from;
                hop.from_comp = from_comp;
                hop.to_comp = gates.component(hop.land());
                if (hop.to_comp != to_comp) continue;
                if (best.bombs == 0 || hop.bombs < best.bombs) best = std::move(hop);
            }
        }
    }
    return best;
}

BombHop GateInfo::best_map_bomb_bounce(const GateInfo& gates, const sim::Grid& grid, sim::TileCoord from, int32_t to_comp) {
    BombHop best;
    if (gates.empty() || to_comp < 0 || !grid.in_bounds(from)) return best;
    for (int f = 0; f < 8; ++f) {
        const sim::TileCoord b = step_in(from, f, 1);
        if (!grid.in_bounds(b) || !grid.has_bomb_at(b)) continue;
        BombHop hop = simulate(grid, b, static_cast<uint8_t>(f));
        if (hop.end != HopEnd::Ground) continue;
        hop.from = from;
        hop.from_comp = gates.component(from);
        hop.to_comp = gates.component(hop.land());
        if (hop.to_comp != to_comp) continue;
        if (best.bombs == 0 || hop.bombs < best.bombs) best = std::move(hop);
    }
    return best;
}

std::vector<const BombHop*> GateInfo::hops_to(int32_t to) const {
    std::vector<const BombHop*> out;
    for (const BombHop& h : hops_) {
        if (h.to_comp == to) out.push_back(&h);
    }
    return out;
}

std::vector<const BombHop*> GateInfo::hops_onto(sim::TileCoord tile) const {
    std::vector<const BombHop*> out;
    for (const BombHop& h : hops_) {
        if (h.land() == tile) out.push_back(&h);
    }
    return out;
}

bool GateInfo::walled() const noexcept {
    return valid_ && home_ >= 0 && !piles_.empty() && free_piles() == 0;
}

int32_t GateInfo::free_piles() const noexcept {
    int32_t n = 0;
    for (const GateTarget& g : piles_) n += g.free() ? 1 : 0;
    return n;
}

int32_t GateInfo::passable_piles() const noexcept {
    int32_t n = 0;
    for (const GateTarget& g : piles_) n += (g.free() || g.passes != 0) ? 1 : 0;
    return n;
}

}  // namespace ants::ai
