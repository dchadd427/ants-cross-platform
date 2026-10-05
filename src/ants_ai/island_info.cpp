#include "ants_ai/island_info.hpp"

#include <algorithm>
#include <deque>
#include <set>

#include "ants_ai/map_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

// The eight neighbours, the order of MapInfo's own searches (N, NE, E, SE, S, SW, W, NW): a flight's direction is the index of this table
constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// FUN_0101d762 / is_special_base_tile: the raid tile, the entrance and the three tiles of the queue row of every hill on the map (the hills of a roster: a team that is not in the match has none).
// The engine skips the hills of teams that have dropped out; the analysis is made once, at the first look, and counts them all: after a drop-out a few tiles are refused that the engine would accept
// (the safe side: no bridge, no bomb and no flight is planned there)
bool special_base_tile(const sim::Grid& grid, sim::TileCoord t) noexcept {
    for (const assets::AnthillSpawn& ah : grid.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if ((t.x == bx + 3 && t.y == by + 2) || (t.x == bx + 1 && t.y == by + 1)) return true;
        if (t.y == by - 1 && t.x >= bx && t.x <= bx + 2) return true;
    }
    return false;
}

bool inside_mound(const sim::Grid& grid, sim::TileCoord t) noexcept {
    for (const assets::AnthillSpawn& ah : grid.anthills()) {
        if (t.x >= static_cast<int32_t>(ah.x) && t.x < static_cast<int32_t>(ah.x) + 4 && t.y >= static_cast<int32_t>(ah.y) && t.y < static_cast<int32_t>(ah.y) + 4) return true;
    }
    return false;
}

}  // namespace

bool bridge_water(const sim::Grid& grid, sim::TileCoord tile) noexcept {
    if (!grid.in_bounds(tile)) return false;
    if (grid.terrain_class_at(tile) != sim::movement::kTerrainWater) return false;
    if (!grid.get_cell(tile).is_empty_overlay()) return false;
    if (grid.is_solid_object(tile)) return false;
    return !special_base_tile(grid, tile);
}

bool bomb_ground(const sim::Grid& grid, sim::TileCoord tile) noexcept {
    if (!grid.in_bounds(tile)) return false;
    const uint8_t terrain = grid.terrain_class_at(tile);
    if (!(terrain == sim::movement::kTerrainGrass || terrain == sim::movement::kTerrainSand || terrain == sim::movement::kTerrainDirt)) return false;
    if (!grid.get_cell(tile).is_empty_overlay()) return false;
    if (grid.is_solid_object(tile)) return false;
    if (special_base_tile(grid, tile)) return false;
    return !inside_mound(grid, tile);
}

bool flight_landing_ok(const sim::Grid& grid, sim::TileCoord tile) noexcept {
    if (!grid.in_bounds(tile)) return false;
    if (!grid.has_fire_at(tile) && grid.is_solid_object(tile)) return false;
    if (special_base_tile(grid, tile)) return false;
    return !inside_mound(grid, tile);
}

IslandInfo IslandInfo::analyse(const MapInfo& map, const sim::Grid& grid, uint8_t team) {
    IslandInfo out;
    const int w = map.width();
    const int h = map.height();
    if (team >= sim::MAX_PLAYERS || !map.hill(team).present || w <= 0 || h <= 0 || static_cast<int>(grid.width()) != w || static_cast<int>(grid.height()) != h) return out;
    const int32_t n = map.component_count(team);
    if (n <= 0) return out;
    const auto comp_at = [&](int x, int y) -> int32_t { return x < 0 || y < 0 || x >= w || y >= h ? -1 : map.component(team, sim::TileCoord{x, y}); };

    // ---- the components ----------------------------------------------------------------------------------------------------------------------------------
    out.comps_.resize(static_cast<size_t>(n));
    for (int32_t c = 0; c < n; ++c) {
        IslandComponent& ic = out.comps_[static_cast<size_t>(c)];
        ic.id = c;
        ic.min = {w, h};
        ic.max = {-1, -1};
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int32_t c = comp_at(x, y);
            if (c < 0) continue;
            IslandComponent& ic = out.comps_[static_cast<size_t>(c)];
            ++ic.tiles;
            ic.min.x = std::min(ic.min.x, x);
            ic.min.y = std::min(ic.min.y, y);
            ic.max.x = std::max(ic.max.x, x);
            ic.max.y = std::max(ic.max.y, y);
        }
    }
    const int32_t hill_comp = map.hill_component(team);
    if (hill_comp >= 0) out.comps_[static_cast<size_t>(hill_comp)].hill = true;

    const auto touching = [&](sim::TileCoord t) {                                  // the components next to a tile that is no walkable tile itself (a pile cell, a power-up)
        std::set<int32_t> s;
        for (int k = 0; k < 8; ++k) {
            const int32_t c = comp_at(t.x + kDx[k], t.y + kDy[k]);
            if (c >= 0) s.insert(c);
        }
        return s;
    };
    for (const PileInfo& p : map.piles()) {
        std::set<int32_t> s;
        for (const sim::TileCoord& cell : p.cells) {
            const std::set<int32_t> t = touching(cell);
            s.insert(t.begin(), t.end());
        }
        for (const int32_t c : s) out.comps_[static_cast<size_t>(c)].piles.push_back(p.index);
    }
    const std::vector<PowerUpInfo>& powerups = map.powerups();
    for (size_t i = 0; i < powerups.size(); ++i) {
        for (const int32_t c : touching(powerups[i].tile)) out.comps_[static_cast<size_t>(c)].powerups.push_back(static_cast<uint32_t>(i));
    }
    for (const sim::MapPlant& plant : grid.plants()) {
        if (plant.tile_id != kDaisyTile) continue;
        const sim::TileCoord plant_tile{static_cast<int32_t>(plant.x), static_cast<int32_t>(plant.y)};
        const sim::TileCoord drop = flower_drop_tile(plant_tile);
        const int32_t c = comp_at(drop.x, drop.y);
        if (c >= 0) out.comps_[static_cast<size_t>(c)].flowers.push_back(plant_tile);
    }

    // ---- the flights -------------------------------------------------------------------------------------------------------------------------------------
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const sim::TileCoord b{x, y};
            const int32_t bc = comp_at(x, y);
            if (bc < 0 || !bomb_ground(grid, b)) continue;                         // the bomb stands on ground that a walker could stand on and that takes a bomb
            for (int d = 0; d < 8; ++d) {
                const sim::TileCoord s{x - kDx[d], y - kDy[d]};                     // the ant steps from S onto B in direction d, so it is thrown the other way
                if (comp_at(s.x, s.y) != bc) continue;                              // S is a tile of the same component (walkable, next to B)
                const sim::TileCoord l{x - 4 * kDx[d], y - 4 * kDy[d]};
                if (!flight_landing_ok(grid, l)) continue;
                const int32_t lc = comp_at(l.x, l.y);                               // a landing in water or on a tile no walker stands on is a drowning, not a flight
                if (lc < 0 || lc == bc) continue;
                Flight f;
                f.from = s;
                f.bomb = b;
                f.land = l;
                f.from_comp = bc;
                f.to_comp = lc;
                out.flights_.push_back(f);
            }
        }
    }

    // ---- the channels: for every component the shortest chain of bridgeable water tiles to each component with a higher number -------------------------------------
    std::vector<int32_t> dist(static_cast<size_t>(w) * static_cast<size_t>(h));
    std::vector<int32_t> prev(static_cast<size_t>(w) * static_cast<size_t>(h));
    const auto at = [&](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); };
    std::vector<std::vector<int>> shore_of(static_cast<size_t>(n));                    // the bridgeable water tiles that touch a component, in reading order: the seeds of its search (one pass over the map, not one per component)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!bridge_water(grid, sim::TileCoord{x, y})) continue;
            int32_t seen[8];
            int seen_n = 0;
            for (int k = 0; k < 8; ++k) {
                const int32_t c = comp_at(x + kDx[k], y + kDy[k]);
                if (c < 0 || std::find(seen, seen + seen_n, c) != seen + seen_n) continue;
                seen[seen_n++] = c;
                shore_of[static_cast<size_t>(c)].push_back(y * w + x);
            }
        }
    }
    for (int32_t a = 0; a < n; ++a) {
        std::fill(dist.begin(), dist.end(), -1);
        std::fill(prev.begin(), prev.end(), -1);
        std::deque<int> queue;
        for (const int idx : shore_of[static_cast<size_t>(a)]) {
            dist[static_cast<size_t>(idx)] = 1;
            queue.push_back(idx);
        }
        std::vector<int32_t> best_len(static_cast<size_t>(n), -1);
        std::vector<int> best_end(static_cast<size_t>(n), -1);
        while (!queue.empty()) {
            const int idx = queue.front();
            queue.pop_front();
            const int x = idx % w;
            const int y = idx / w;
            const int32_t du = dist[at(x, y)];
            for (int k = 0; k < 8; ++k) {
                const int32_t c = comp_at(x + kDx[k], y + kDy[k]);
                if (c > a && best_len[static_cast<size_t>(c)] < 0) {               // the first chain found to a component is the shortest (the search goes by distance, in reading order)
                    best_len[static_cast<size_t>(c)] = du;
                    best_end[static_cast<size_t>(c)] = idx;
                }
            }
            if (du >= kMaxChannel) continue;
            for (int k = 0; k < 8; ++k) {
                const int nx = x + kDx[k];
                const int ny = y + kDy[k];
                if (!bridge_water(grid, sim::TileCoord{nx, ny}) || dist[at(nx, ny)] >= 0) continue;
                dist[at(nx, ny)] = du + 1;
                prev[at(nx, ny)] = idx;
                queue.push_back(ny * w + nx);
            }
        }
        for (int32_t c = a + 1; c < n; ++c) {
            if (best_len[static_cast<size_t>(c)] < 0) continue;
            Channel ch;
            ch.a = a;
            ch.b = c;
            for (int cur = best_end[static_cast<size_t>(c)]; cur >= 0; cur = prev[at(cur % w, cur / w)]) ch.tiles.push_back(sim::TileCoord{cur % w, cur / w});
            std::reverse(ch.tiles.begin(), ch.tiles.end());
            out.channels_.push_back(std::move(ch));
        }
    }

    // ---- the token groups --------------------------------------------------------------------------------------------------------------------------------
    std::vector<int> group_of(powerups.size(), -1);
    for (size_t i = 0; i < powerups.size(); ++i) {
        if (group_of[i] >= 0) continue;
        std::vector<uint32_t> found{static_cast<uint32_t>(i)};                       // the group by 8-adjacency (indices of MapInfo::powerups())
        group_of[i] = static_cast<int>(out.groups_.size());
        for (size_t head = 0; head < found.size(); ++head) {
            const sim::TileCoord t = powerups[found[head]].tile;
            for (size_t j = 0; j < powerups.size(); ++j) {
                if (group_of[j] >= 0 || t.chebyshev_dist(powerups[j].tile) != 1) continue;
                group_of[j] = group_of[i];
                found.push_back(static_cast<uint32_t>(j));
            }
        }
        struct Raw {
            int32_t comp;
            sim::TileCoord tile;
            uint32_t power;                                                          // MapInfo::powerups() index of the member it leads to
        };
        std::vector<Raw> raw;                                                        // the walkable tiles next to a member, one per (component, member): the lowest tile in reading order
        for (const uint32_t m : found) {
            std::set<int32_t> seen;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const sim::TileCoord t{powerups[m].tile.x + dx, powerups[m].tile.y + dy};
                    const int32_t c = comp_at(t.x, t.y);
                    if (c < 0 || !seen.insert(c).second) continue;
                    raw.push_back(Raw{c, t, m});
                }
            }
        }
        std::sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) {            // (two members can touch the same tile: the member is the last key, so that no library's order of ties is in the result)
            if (a.comp != b.comp) return a.comp < b.comp;
            if (a.tile.y != b.tile.y) return a.tile.y < b.tile.y;
            return a.tile.x != b.tile.x ? a.tile.x < b.tile.x : a.power < b.power;
        });
        TokenGroup g;
        if (!raw.empty()) {                                                          // the members in the order of a walk from the first entrance (a row of tokens is walked from its end)
            g.members.push_back(raw.front().power);
            for (size_t head = 0; head < g.members.size(); ++head) {
                for (const uint32_t m : found) {
                    if (std::find(g.members.begin(), g.members.end(), m) != g.members.end()) continue;
                    if (powerups[g.members[head]].tile.chebyshev_dist(powerups[m].tile) == 1) g.members.push_back(m);
                }
            }
        }
        for (const uint32_t m : found) {                                             // (a group that no walker touches at all keeps the order of the search)
            if (std::find(g.members.begin(), g.members.end(), m) == g.members.end()) g.members.push_back(m);
        }
        for (const Raw& r : raw) {
            const auto pos = static_cast<uint32_t>(std::find(g.members.begin(), g.members.end(), r.power) - g.members.begin());
            g.entrances.push_back(TokenGroup::Entrance{r.comp, r.tile, pos});
        }
        out.groups_.push_back(std::move(g));
    }
    return out;
}

const Channel* IslandInfo::channel(int32_t a, int32_t b) const noexcept {
    if (a > b) std::swap(a, b);
    for (const Channel& c : channels_) {
        if (c.a == a && c.b == b) return &c;
    }
    return nullptr;
}

std::vector<const Flight*> IslandInfo::flights_from(int32_t from) const {
    std::vector<const Flight*> out;
    for (const Flight& f : flights_) {
        if (f.from_comp == from) out.push_back(&f);
    }
    return out;
}

std::vector<const Flight*> IslandInfo::flights_between(int32_t from, int32_t to) const {
    std::vector<const Flight*> out;
    for (const Flight& f : flights_) {
        if (f.from_comp == from && f.to_comp == to) out.push_back(&f);
    }
    return out;
}

std::vector<int32_t> IslandInfo::flight_distances(int32_t from) const {
    std::vector<int32_t> d(comps_.size(), -1);
    if (from < 0 || static_cast<size_t>(from) >= comps_.size()) return d;
    d[static_cast<size_t>(from)] = 0;
    std::deque<int32_t> queue{from};
    while (!queue.empty()) {
        const int32_t c = queue.front();
        queue.pop_front();
        for (const Flight& f : flights_) {
            if (f.from_comp != c || d[static_cast<size_t>(f.to_comp)] >= 0) continue;
            d[static_cast<size_t>(f.to_comp)] = d[static_cast<size_t>(c)] + 1;
            queue.push_back(f.to_comp);
        }
    }
    return d;
}

std::vector<int32_t> IslandInfo::flight_path(int32_t from, int32_t to) const {
    std::vector<int32_t> out;
    if (from == to || from < 0 || to < 0 || static_cast<size_t>(from) >= comps_.size() || static_cast<size_t>(to) >= comps_.size()) return out;
    std::vector<int32_t> parent(comps_.size(), -1);
    std::vector<uint8_t> seen(comps_.size(), 0);
    seen[static_cast<size_t>(from)] = 1;
    std::deque<int32_t> queue{from};
    while (!queue.empty() && seen[static_cast<size_t>(to)] == 0) {
        const int32_t c = queue.front();
        queue.pop_front();
        std::set<int32_t> next;                                                      // the lower component number first, whatever the order of the flights
        for (const Flight& f : flights_) {
            if (f.from_comp == c) next.insert(f.to_comp);
        }
        for (const int32_t t : next) {
            if (seen[static_cast<size_t>(t)] != 0) continue;
            seen[static_cast<size_t>(t)] = 1;
            parent[static_cast<size_t>(t)] = c;
            queue.push_back(t);
        }
    }
    if (seen[static_cast<size_t>(to)] == 0) return out;
    for (int32_t c = to; c != from; c = parent[static_cast<size_t>(c)]) out.push_back(c);
    std::reverse(out.begin(), out.end());
    return out;
}

}  // namespace ants::ai
