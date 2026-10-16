#include "ants_ai/field_plan.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <utility>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr int32_t kFar = std::numeric_limits<int32_t>::max() / 4;
constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};   // the engine's directions N, NE, E, SE, S, SW, W, NW (a facing is one of them; the even ones are the sides)
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// The costs of a way, in steps of about 8 ticks: a step on clear ground 2 (3 diagonally), a swim 3, a defuse 8 (24 ticks and the step), a fire cell put out 8, a bridge 12 (34 ticks
// and the step); a hop 1000 a bomb (2 hit points each, and a fifth of the bombs are duds): whatever a dig reaches is done before a hop
constexpr int32_t kStep = 2;
constexpr int32_t kSwim = 3;
constexpr int32_t kDefuse = 8;
constexpr int32_t kPutOut = 8;
constexpr int32_t kBridgeCost = 12;
constexpr int32_t kHopBomb = 1000;
constexpr int32_t kHopWork = 30;              // planting and the throw itself
constexpr int32_t kStrandRisk = 100;          // a chain over a map bomb whose dud leaves the ant where no dig reaches the work
constexpr int32_t kDudRisk = 10;
constexpr uint8_t kLeaveHp = 8;               // the hit points a breach ant leaves the hill's side with at least (a pocket and an island hop, and a dud)

enum Kind : uint8_t { kBlocked, kClear, kBomb, kFire, kWater, kToken, kPass };   // kToken: a power-up a walk does not pass (solid: one that an ant left); kPass: one it passes over

struct Tiles {
    int32_t w{0};
    int32_t h{0};
    std::vector<uint8_t> kind;
    std::vector<uint8_t> own;                // a kBomb of the seat itself (or its ally): its own ants do not step on it, so it never throws them; it is still defused
    bool in(sim::TileCoord t) const noexcept { return t.x >= 0 && t.y >= 0 && t.x < w && t.y < h; }
    size_t at(sim::TileCoord t) const noexcept { return static_cast<size_t>(t.y) * static_cast<size_t>(w) + static_cast<size_t>(t.x); }
    uint8_t of(sim::TileCoord t) const noexcept { return in(t) ? kind[at(t)] : static_cast<uint8_t>(kBlocked); }
    sim::TileCoord tile(size_t i) const noexcept { return sim::TileCoord{static_cast<int32_t>(i % static_cast<size_t>(w)), static_cast<int32_t>(i / static_cast<size_t>(w))}; }
};

// What every tile is to the breach. A bomb of the seat or its ally is a wall for its walkers; one that lay on the map at the start (`map_bombs`: the map's own bombs are the
// team 0's in many levels) is defused like any other, one planted since (the bomb of a hop, a mine) never. A bomb in water is a wall. A power-up is taken by the move that ENDS on it: a walk passes over one that is not solid (most of the map's own: the walls of POPcOrN's power-up rooms are rows of
// them), never over one an ant has left (it is solid), and the ant never steps onto one but to take it.
Tiles read_tiles(const BotView& view, uint8_t seat, const FieldHints& hints) {
    const sim::Grid& grid = view.grid();
    Tiles t;
    t.w = static_cast<int32_t>(grid.width());
    t.h = static_cast<int32_t>(grid.height());
    t.kind.assign(static_cast<size_t>(t.w) * static_cast<size_t>(t.h), kBlocked);
    t.own.assign(t.kind.size(), 0);
    const WalkContext ctx = view.walk_context();
    for (int32_t y = 0; y < t.h; ++y) {
        for (int32_t x = 0; x < t.w; ++x) {
            const sim::TileCoord c{x, y};
            const bool water = grid.terrain_class_at(c) == sim::movement::kTerrainWater;
            uint8_t k = kBlocked;
            if (grid.has_powerup_at(c)) {
                k = grid.is_solid_object(c) ? kToken : kPass;
            } else if (grid.has_fire_at(c)) {
                k = kFire;
            } else if (grid.has_bomb_at(c)) {
                const uint8_t owner = grid.get_cell(c).interactive_owner;
                const bool own = owner == seat || (view.ally() < sim::MAX_PLAYERS && owner == view.ally());
                const bool start = !hints.map_bombs_known || std::find(hints.map_bombs.begin(), hints.map_bombs.end(), c) != hints.map_bombs.end();
                k = (water || (own && !start)) ? kBlocked : kBomb;
                t.own[t.at(c)] = own ? 1 : 0;
            } else if (water) {
                k = grid.is_solid_object(c) ? kBlocked : kWater;
            } else if (MapInfo::walkable(grid, seat, c, ctx)) {
                k = kClear;
            }
            t.kind[t.at(c)] = k;
        }
    }
    return t;
}

// How an ant gets about: clear ground always; water, fire and bombs as its kind and the work allow (-1: closed). The ant's OWN way is strict: it enters a bomb only from a side and
// from land (it defuses it from there), and steps onto a power-up only as a last resort (it takes it: its kind changes). The carriers' route and the ways of the typed ants are
// not: the work on them is done from whichever side its worker reaches.
struct Mover {
    int32_t swim{-1};       // water passable (a Swimmer)
    int32_t fire{-1};       // fire passable (a Fire Ant)
    int32_t dig{-1};        // a bomb, defused first
    int32_t bridge{-1};     // water, bridged first
    int32_t put_out{-1};    // fire, put out first
    int32_t token{-1};      // a power-up, stepped on
    bool strict{false};
};

constexpr int32_t kPassOver = 10;             // a power-up that a walk passes over: allowed (the rooms of power-ups are entered so) but not sought (a step onto it takes it)
constexpr int32_t kTokenStep = 40;            // a power-up on the ant's own way: about five defuses' worth, so that a dig round it is preferred
constexpr int32_t kTokenWay = 20;             // a power-up on a planned way (the route, the fire way, the swim way): it moves when an ant takes it, so it does not change the work
constexpr int32_t kNewRegion = 100;           // a source whose way has work in a region of the field that none of the route's work is in: one hop more for the breach ant
constexpr int32_t kDropShut = 120;            // the fire way leaves a Fire power-up over land: the power-up the ant leaves when it takes it lands on one of the n free land tiles next
                                              // to it (never water or a bridge; the engine draws which) and shuts a way over that tile: kDropShut / n on each of them

Mover mover_of(sim::AntType type) {
    Mover m;
    m.strict = true;
    m.token = kTokenStep;
    if (type == sim::AntType::Bomber) m.dig = kDefuse;
    if (type == sim::AntType::Swimmer) m.swim = kSwim;
    if (type == sim::AntType::Fire) m.fire = kStep;
    return m;
}

int32_t enter_cost(const Tiles& t, const Mover& m, sim::TileCoord from, sim::TileCoord to, int dir, bool from_start = false) {
    const int32_t diagonal = (dir & 1) != 0 ? 1 : 0;
    const bool stands = from_start || (t.of(from) != kPass && t.of(from) != kToken);         // (an ant passes over a power-up, it never stops on one but to take it)
    switch (t.of(to)) {
        case kClear:
            return kStep + diagonal;
        case kWater:
            if (m.swim >= 0) return m.swim + diagonal;
            return (m.bridge >= 0 && !(m.strict && (diagonal != 0 || !stands))) ? m.bridge + diagonal : -1;
        case kFire:
            if (m.fire >= 0) return m.fire + diagonal;
            return (m.put_out >= 0 && !(m.strict && (diagonal != 0 || !stands))) ? m.put_out + diagonal : -1;
        case kBomb:
            if (m.dig < 0 || (m.strict && (diagonal != 0 || t.of(from) == kWater || !stands))) return -1;
            return m.dig + diagonal;
        case kToken:
            return m.token >= 0 ? m.token + diagonal : -1;
        case kPass:
            return kStep + kPassOver + diagonal;
        default:
            return -1;
    }
}

// The extra cost of entering some tiles (empty: none): the land beside a Fire power-up that the Fire Ant leaves from (see plan_field)
using Extra = std::vector<int32_t>;

struct Search {
    std::vector<int32_t> dist;
    std::vector<int32_t> parent;
    int32_t cost(const Tiles& t, sim::TileCoord c) const { return t.in(c) ? dist[t.at(c)] : kFar; }
    bool reaches(const Tiles& t, sim::TileCoord c) const { return cost(t, c) < kFar; }
};

using Node = std::pair<int32_t, size_t>;

// Dijkstra from `sources` (tile, cost at the start): integer costs, ties by the tile index, the neighbours in the engine's order
Search search(const Tiles& t, const Mover& m, const std::vector<std::pair<sim::TileCoord, int32_t>>& sources, const Extra& extra = Extra{}) {
    Search s;
    s.dist.assign(t.kind.size(), kFar);
    s.parent.assign(t.kind.size(), -1);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    for (const auto& src : sources) {
        if (!t.in(src.first) || src.second >= s.dist[t.at(src.first)]) continue;
        s.dist[t.at(src.first)] = src.second;
        queue.push({src.second, t.at(src.first)});
    }
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > s.dist[i]) continue;
        const sim::TileCoord c = t.tile(i);
        for (int dir = 0; dir < 8; ++dir) {
            const sim::TileCoord n{c.x + kDx[dir], c.y + kDy[dir]};
            if (!t.in(n)) continue;
            int32_t step = enter_cost(t, m, c, n, dir, d == 0);
            if (step < 0) continue;
            const size_t ni = t.at(n);
            if (!extra.empty()) step += extra[ni];
            if (d + step >= s.dist[ni]) continue;
            s.dist[ni] = d + step;
            s.parent[ni] = static_cast<int32_t>(i);
            queue.push({d + step, ni});
        }
    }
    return s;
}

// The same, backwards: the cost of the way FROM every tile to the nearest of `targets` (a target is reached by entering it)
Search search_back(const Tiles& t, const Mover& m, const std::vector<sim::TileCoord>& targets) {
    Search s;
    s.dist.assign(t.kind.size(), kFar);
    s.parent.assign(t.kind.size(), -1);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    for (const sim::TileCoord& c : targets) {
        if (!t.in(c) || s.dist[t.at(c)] == 0) continue;
        s.dist[t.at(c)] = 0;
        queue.push({0, t.at(c)});
    }
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > s.dist[i]) continue;
        const sim::TileCoord c = t.tile(i);
        for (int dir = 0; dir < 8; ++dir) {
            const sim::TileCoord v{c.x - kDx[dir], c.y - kDy[dir]};                  // the move v -> c goes in direction dir
            if (!t.in(v)) continue;
            const uint8_t vk = t.of(v);
            const bool stands = vk == kClear || vk == kToken || vk == kPass || (vk == kBomb && m.dig >= 0) || (vk == kWater && (m.swim >= 0 || m.bridge >= 0)) || (vk == kFire && (m.fire >= 0 || m.put_out >= 0));
            if (!stands) continue;
            const int32_t step = enter_cost(t, m, v, c, dir, vk == kPass || vk == kToken);   // (a power-up's tile is where a flight may put the ant: it works from it once)
            if (step < 0) continue;
            const size_t vi = t.at(v);
            if (d + step >= s.dist[vi]) continue;
            s.dist[vi] = d + step;
            s.parent[vi] = static_cast<int32_t>(i);
            queue.push({d + step, vi});
        }
    }
    return s;
}

// The way of a search to `end`, from its source: [source, ..., end]
std::vector<sim::TileCoord> way_to(const Tiles& t, const Search& s, sim::TileCoord end) {
    std::vector<sim::TileCoord> out;
    if (!s.reaches(t, end)) return out;
    for (int32_t i = static_cast<int32_t>(t.at(end)); i >= 0; i = s.parent[static_cast<size_t>(i)]) out.push_back(t.tile(static_cast<size_t>(i)));
    std::reverse(out.begin(), out.end());
    return out;
}

// The tiles other ants stand on: the breach ant's own way goes round them where it can (the engine's paths do; an ant that stands still blocks a step onto its tile)
constexpr int32_t kCrowd = 60;
Extra crowd(const Tiles& t, const BotView& view, uint32_t ant) {
    Extra e(t.kind.size(), 0);
    for (const AntView& a : view.mine()) {
        if (a.id != ant && t.in(a.tile)) e[t.at(a.tile)] = kCrowd;
    }
    for (const AntView& a : view.others()) {
        if (t.in(a.tile)) e[t.at(a.tile)] = kCrowd;
    }
    return e;
}

void add_once(std::vector<sim::TileCoord>& list, sim::TileCoord c) {
    if (std::find(list.begin(), list.end(), c) == list.end()) list.push_back(c);
}

bool has_bomb_of(const BotView& view, sim::TileCoord c, uint8_t owner) {
    for (const BombView& b : view.bombs()) {
        if (b.tile == c && b.owner == owner) return true;
    }
    return false;
}

// The sides of a tile (N, S, W, E: the engine's approach tiles of an ability)
std::vector<sim::TileCoord> sides_of(sim::TileCoord c) {
    return {sim::TileCoord{c.x, c.y - 1}, sim::TileCoord{c.x, c.y + 1}, sim::TileCoord{c.x - 1, c.y}, sim::TileCoord{c.x + 1, c.y}};
}

bool hill_walk(const Tiles& t, const MapInfo& map, uint8_t seat, sim::TileCoord from) {   // the ant walks to its hill (no dig, no swim): it can go in to heal
    const Search walk = search(t, Mover{}, {{from, 0}});
    for (const sim::TileCoord& s : map.hill(seat).starts) {
        if (walk.reaches(t, s)) return true;
    }
    return false;
}

FieldStep step_of(FieldAction action, FieldWork work, sim::TileCoord tile) {
    FieldStep s;
    s.action = action;
    s.work = work;
    s.tile = tile;
    return s;
}

// The next move on a way that holds no bomb or solid power-up before `goal`: a walk of several tiles over ground with no bomb on or next to it, else one tile. A walk never ends on a
// power-up (it would take it), and a power-up next to the ant is passed by a walk to the first tile after it.
FieldStep move_along(const Tiles& t, const std::vector<sim::TileCoord>& way, size_t goal, FieldWork work) {
    while (goal >= 1 && t.of(way[goal]) == kPass) --goal;
    if (goal == 0) {                                                                      // nothing but power-ups up to the goal: past them
        size_t past = 1;
        while (past < way.size() && t.of(way[past]) == kPass) ++past;
        if (past >= way.size() || t.of(way[past]) != kClear) return step_of(FieldAction::Stuck, work, way.size() > 1 ? way[1] : way[0]);
        return step_of(FieldAction::Walk, work, way[past]);
    }
    bool safe = goal >= 2;
    for (size_t i = 1; i <= goal && safe; ++i) {
        if (t.of(way[i]) != kClear && t.of(way[i]) != kPass) safe = false;
        for (int dir = 0; dir < 8 && safe; ++dir) safe = t.of(sim::TileCoord{way[i].x + kDx[dir], way[i].y + kDy[dir]}) != kBomb;
    }
    if (safe) return step_of(FieldAction::Walk, work, way[goal]);
    if (t.of(way[1]) != kPass) return step_of(FieldAction::Step, work, way[1]);
    size_t past = 1;                                                                      // (a power-up next to the ant in the field: the walk past it is the only way on)
    while (past <= goal && t.of(way[past]) == kPass) ++past;
    return step_of(FieldAction::Walk, work, way[std::min(past, goal)]);
}

// The next step on `way` (from the ant): the first bomb on it is defused from the tile before it, a power-up on it is taken (its kind changes: the next look plans again), the ground
// up to them is walked
FieldStep along(const Tiles& t, const std::vector<sim::TileCoord>& way, FieldWork work) {
    if (way.size() < 2) return step_of(FieldAction::Stuck, work, sim::TileCoord{-1, -1});
    size_t first = 1;
    while (first < way.size() && t.of(way[first]) != kBomb && t.of(way[first]) != kToken) ++first;
    if (first == 1) return step_of(t.of(way[1]) == kBomb ? FieldAction::Defuse : FieldAction::Take, work, way[1]);
    return move_along(t, way, first - 1, work);
}

// Toward `goal` (a tile the ant can stand on) by the way `reach` found, then `then` on `target` there
FieldStep toward(const Tiles& t, const Search& reach, sim::TileCoord goal, FieldAction then, sim::TileCoord target, FieldWork work) {
    const std::vector<sim::TileCoord> way = way_to(t, reach, goal);
    if (way.empty()) return step_of(FieldAction::Stuck, work, goal);
    if (way.size() == 1) return step_of(then, work, target);
    return along(t, way, work);
}

// ---- hops ----------------------------------------------------------------------------------------------------------------------------------------------------

struct Launch {
    BombHop hop;                         // bomb T, from P, facing, the chain
    sim::TileCoord plant_from{-1, -1};   // C: the side of T the Bomber plants from (P itself for a hop straight on)
    int32_t cost{kFar};
};

bool plant_ground(const sim::Grid& grid, const Tiles& t, sim::TileCoord c) {               // T: free ground for a bomb once a bomb on it is defused
    if (!t.in(c) || (t.of(c) != kClear && t.of(c) != kBomb)) return false;
    const uint8_t terrain = grid.terrain_class_at(c);
    if (!(terrain == sim::movement::kTerrainGrass || terrain == sim::movement::kTerrainSand || terrain == sim::movement::kTerrainDirt)) return false;
    return t.of(c) == kBomb || bomb_ground(grid, c);
}

// The launch (P, T, facing) and its value: the chain is the engine's rule on the grid as it is (GateInfo::simulate). A launch is kept when its landing lies where no dig of the ant reaches
// and a dig from it reaches the work; the cost is the bombs first, then the digs to the launch and from the landing, and the risk of the chain's map bombs.
Launch value_launch(const sim::Grid& grid, const Tiles& t, const Search& reach, const Search& back, sim::TileCoord from, int facing, uint8_t hp) {
    Launch l;
    const sim::TileCoord bomb{from.x + kDx[facing], from.y + kDy[facing]};
    if (!t.in(bomb) || !reach.reaches(t, from) || (t.of(from) != kClear && t.of(from) != kBomb) || !plant_ground(grid, t, bomb) || !reach.reaches(t, bomb)) return l;
    sim::TileCoord side = from;
    if ((facing & 1) != 0) {                                                              // a diagonal throw: the bomb is planted from a side of T next to P
        const sim::TileCoord a{from.x, bomb.y};
        const sim::TileCoord b{bomb.x, from.y};
        const bool a_ok = reach.reaches(t, a) && (t.of(a) == kClear || t.of(a) == kBomb);
        const bool b_ok = reach.reaches(t, b) && (t.of(b) == kClear || t.of(b) == kBomb);
        if (!a_ok && !b_ok) return l;
        side = (a_ok && (!b_ok || reach.cost(t, a) <= reach.cost(t, b))) ? a : b;
    }
    BombHop h = GateInfo::simulate(grid, bomb, static_cast<uint8_t>(facing));
    if (h.end != HopEnd::Ground) return l;
    const sim::TileCoord land = h.land();
    if (reach.reaches(t, land) || !back.reaches(t, land)) return l;                       // nowhere new, or nothing to do there
    if (static_cast<int32_t>(hp) <= h.hp) return l;                                       // it would not survive the chain
    int32_t risk = 0;
    for (size_t i = 0; i + 1 < h.lands.size(); ++i) risk += back.reaches(t, h.lands[i]) ? kDudRisk : kStrandRisk;
    h.from = from;
    l.hop = h;
    l.plant_from = side;
    l.cost = kHopBomb * h.bombs + std::max(reach.cost(t, from), reach.cost(t, side)) + kHopWork + back.cost(t, land) + risk;
    return l;
}

// The steps of a launch from where the ant is: to the side it plants from (digging what is in the way), the bombs on T, P and that side defused, the plant, the step to P, the throw
FieldStep carry_out(const Tiles& t, const Search& reach, const BotView& view, uint8_t seat, const AntView& ant, const Launch& l, FieldWork work) {
    const BombHop& h = l.hop;
    FieldStep s;
    if (has_bomb_of(view, h.bomb, seat)) {                                                // planted: to P, then onto the bomb
        if (ant.tile == h.from) s = step_of(FieldAction::Fly, work, h.bomb);
        else if (ant.tile.chebyshev_dist(h.from) == 1 && t.of(h.from) == kClear) s = step_of(FieldAction::Step, work, h.from);
        else s = toward(t, reach, h.from, FieldAction::Step, h.from, work);
    } else if (ant.tile == h.bomb) {                                                      // a dud left it on T: off it, to the side it plants from
        s = step_of(FieldAction::Step, work, l.plant_from);
    } else if (ant.tile != l.plant_from) {
        s = toward(t, reach, l.plant_from, FieldAction::Plant, h.bomb, work);
    } else if (t.of(h.bomb) == kBomb) {
        s = step_of(FieldAction::Defuse, work, h.bomb);
    } else if (t.of(h.from) == kBomb) {
        s = step_of(FieldAction::Defuse, work, h.from);
    } else {
        s = step_of(FieldAction::Plant, work, h.bomb);
    }
    s.hop = h;
    return s;
}

// A Bomber that no dig brings to `targets`: the best hop (see value_launch), the one under way first while it still holds; a full ant leaves the hill's side
FieldStep hop_step(const Tiles& t, const Search& reach, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant, const std::vector<sim::TileCoord>& targets,
                   const FieldHints& hints, FieldWork work) {
    const sim::Grid& grid = view.grid();
    const Search back = search_back(t, mover_of(sim::AntType::Bomber), targets);
    const bool at_home = map.hill(seat).present && hill_walk(t, map, seat, ant.tile);
    Launch best;
    if (hints.hop.bombs > 0) {                                                            // the hop under way
        const BombHop& h = hints.hop;
        if (has_bomb_of(view, h.bomb, seat) && reach.reaches(t, h.from)) {
            best.hop = h;
            best.plant_from = h.from;
            best.cost = 0;
        } else {
            for (int f = 0; f < 8; ++f) {
                if (h.from.x + kDx[f] != h.bomb.x || h.from.y + kDy[f] != h.bomb.y) continue;
                const Launch again = value_launch(grid, t, reach, back, h.from, f, ant.hp);
                if (again.cost < kFar && again.hop.land() == h.land()) best = again;
            }
        }
    }
    if (best.cost >= kFar) {
        for (size_t i = 0; i < t.kind.size(); ++i) {
            if (reach.dist[i] >= kFar) continue;
            const sim::TileCoord from = t.tile(i);
            for (int f = 0; f < 8; ++f) {
                const Launch l = value_launch(grid, t, reach, back, from, f, ant.hp);
                if (l.cost < best.cost) best = l;
            }
        }
    }
    if (best.cost >= kFar) return step_of(at_home && ant.hp < sim::AntUnit::MAX_HP ? FieldAction::Heal : FieldAction::Stuck, work, map.hill(seat).entrance);
    if (at_home && ant.hp < kLeaveHp && !has_bomb_of(view, best.hop.bomb, seat)) {     // a hop leaves the hill's side with the hit points the field will need (a dud there costs two)
        FieldStep s = step_of(FieldAction::Heal, work, map.hill(seat).entrance);
        s.hop = best.hop;
        return s;
    }
    return carry_out(t, reach, view, seat, ant, best, work);
}

// ---- the work ------------------------------------------------------------------------------------------------------------------------------------------------

// An ant that took a power-up before leaves its own kind on a free land tile beside the one it takes (the engine draws which; an ant of the level's kind leaves nothing, unless
// it took that kind: the view cannot tell). On a power-up with one free land tile beside it, and nothing else that its new kind leaves over (water for a Swimmer, fire for a Fire
// Ant, a bomb for a Bomber, a power-up that a walk passes over), the power-up it leaves shuts it in.
bool shuts_in(const Tiles& t, const BotView& view, const AntView& ant, sim::TileCoord token, sim::AntType kind) {
    if (ant.type == sim::AntType::Worker || ant.type == view.default_ant_type()) return false;
    int32_t free_land = 0;
    int32_t other = 0;
    for (int dir = 0; dir < 8; ++dir) {
        const sim::TileCoord n{token.x + kDx[dir], token.y + kDy[dir]};
        const uint8_t k = t.of(n);
        if (k == kClear) ++free_land;
        else if (k == kPass || (kind == sim::AntType::Swimmer && k == kWater) || (kind == sim::AntType::Fire && k == kFire) || (kind == sim::AntType::Bomber && k == kBomb)) ++other;
    }
    return free_land <= 1 && other == 0;
}

// The ant takes a power-up of `kind` (`prefer` first): it walks, swims or digs to a tile next to it and steps on; a Bomber that cannot get there hops toward it
FieldStep take_step(const Tiles& t, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant, sim::AntType kind, sim::TileCoord prefer, const FieldHints& hints,
                    FieldWork work) {
    std::vector<sim::TileCoord> tokens;
    for (const PowerUpView& p : view.powerups()) {
        if (p.kind == kind && (p.standing_team == 255 || p.standing_ant == ant.id) && !shuts_in(t, view, ant, p.tile, kind)) tokens.push_back(p.tile);
    }
    if (tokens.empty()) return step_of(FieldAction::Stuck, work, sim::TileCoord{-1, -1});
    if (std::find(tokens.begin(), tokens.end(), ant.tile) != tokens.end()) {             // a flight put it on the power-up, which a flight does not take: off and back on
        for (int dir = 0; dir < 8; ++dir) {
            const sim::TileCoord n{ant.tile.x + kDx[dir], ant.tile.y + kDy[dir]};
            if (t.of(n) == kClear) return step_of(FieldAction::Step, work, n);
        }
        if (ant.type == sim::AntType::Bomber) {
            for (const sim::TileCoord& n : sides_of(ant.tile)) {
                if (t.of(n) == kBomb) return step_of(FieldAction::Defuse, work, n);
            }
        }
        return step_of(FieldAction::Stuck, work, ant.tile);
    }
    const Search reach = search(t, mover_of(ant.type), {{ant.tile, 0}}, crowd(t, view, ant.id));
    int32_t best = kFar;
    sim::TileCoord best_token{-1, -1};
    sim::TileCoord best_stand{-1, -1};
    std::vector<sim::TileCoord> next_to;
    for (const sim::TileCoord& u : tokens) {
        for (int dir = 0; dir < 8; ++dir) {
            const sim::TileCoord n{u.x + kDx[dir], u.y + kDy[dir]};
            const uint8_t k = t.of(n);
            if (k == kClear || k == kBomb || (k == kWater && ant.type == sim::AntType::Swimmer)) next_to.push_back(n);
            if (!reach.reaches(t, n) || k == kBomb || !(k == kClear || (k == kWater && ant.type == sim::AntType::Swimmer))) continue;
            const int32_t cost = reach.cost(t, n) - (u == prefer ? 40 : 0);
            if (cost < best) {
                best = cost;
                best_token = u;
                best_stand = n;
            }
        }
    }
    if (best_token.x < 0) {
        if (ant.type == sim::AntType::Bomber) {                                           // digs reach a side of it once the bombs there are defused, or a hop does
            for (const sim::TileCoord& n : next_to) {
                if (t.of(n) == kBomb && reach.reaches(t, n)) return toward(t, reach, n, FieldAction::Defuse, n, work);
            }
            return hop_step(t, reach, view, map, seat, ant, next_to, hints, work);
        }
        return step_of(FieldAction::Stuck, work, sim::TileCoord{-1, -1});
    }
    if (ant.tile == best_stand || ant.tile.chebyshev_dist(best_token) == 1) return step_of(FieldAction::Take, work, best_token);
    return toward(t, reach, best_stand, FieldAction::Take, best_token, work);
}

FieldStep dig_step(const Tiles& t, const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant, const FieldHints& hints) {
    const Search reach = search(t, mover_of(sim::AntType::Bomber), {{ant.tile, 0}}, crowd(t, view, ant.id));
    int32_t best = kFar;
    sim::TileCoord target{-1, -1};
    for (const sim::TileCoord& b : plan.bombs) {
        if (t.of(b) == kBomb && reach.cost(t, b) < best) {
            best = reach.cost(t, b);
            target = b;
        }
    }
    if (target.x >= 0) return along(t, way_to(t, reach, target), FieldWork::Bombs);
    std::vector<sim::TileCoord> left;
    for (const sim::TileCoord& b : plan.bombs) {
        if (t.of(b) == kBomb) left.push_back(b);
    }
    return hop_step(t, reach, view, map, seat, ant, left, hints, FieldWork::Bombs);
}

// A Swimmer bridges the fire way's water from the land beside it; a Fire Ant puts the route's fire cells out from a side
FieldStep work_from_side(const Tiles& t, const BotView& view, const AntView& ant, const std::vector<sim::TileCoord>& targets, uint8_t target_kind, FieldAction action, FieldWork work) {
    const sim::Grid& grid = view.grid();
    const Search reach = search(t, mover_of(ant.type), {{ant.tile, 0}}, crowd(t, view, ant.id));
    int32_t best = kFar;
    sim::TileCoord goal{-1, -1};
    sim::TileCoord target{-1, -1};
    for (const sim::TileCoord& c : targets) {
        if (t.of(c) != target_kind || (target_kind == kWater && !bridge_water(grid, c))) continue;
        for (const sim::TileCoord& side : sides_of(c)) {
            if (t.of(side) != kClear || !reach.reaches(t, side)) continue;
            if (side == ant.tile) return step_of(action, work, c);
            if (reach.cost(t, side) < best) {
                best = reach.cost(t, side);
                goal = side;
                target = c;
            }
        }
    }
    if (goal.x < 0) return step_of(FieldAction::Stuck, work, sim::TileCoord{-1, -1});
    return toward(t, reach, goal, action, target, work);
}

// The engine's step cost of a carrier (MapInfo::step_cost for a walker; a Swimmer's weights for water): another team's bomb is no obstacle to the engine's walk (an ant that steps on one is
// thrown), the seat's own is a wall, and so are fire walls, except the route's own fire cells (they will be out). -1: not a step the engine takes.
int32_t carrier_step(const sim::Grid& grid, const Tiles& t, bool swimmer, const std::vector<sim::TileCoord>& open_fire, sim::TileCoord from, sim::TileCoord to) {
    const uint8_t k = t.of(to);
    const bool fire_open = k == kFire && std::find(open_fire.begin(), open_fire.end(), to) != open_fire.end();
    if (!((k == kBomb && t.own[t.at(to)] == 0) || k == kClear || k == kPass || fire_open || (swimmer && k == kWater))) return -1;
    uint32_t s = sim::movement::terrain_step_weight(grid.terrain_class_at(from), swimmer) + sim::movement::terrain_step_weight(grid.terrain_class_at(to), swimmer);
    if (from.x != to.x && from.y != to.y) s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
    s >>= 1;
    return s >= static_cast<uint32_t>(MapInfo::kPathCostLimit) ? -1 : static_cast<int32_t>(s);
}

// The carriers' walks between the hill and the pile: the engine gives a carrier the cheapest walk, and of walks of the same cost the one its search meets first, which no model here
// can tell. So every bomb on a walk that costs at most kCorridorSlack more than the cheapest (from each side of the pile the carriers come to: those within kSideSlack of the nearest)
// is the Bomber's work, and so is every bomb round the pile (a power-up that an ant leaves next to the pile turns the walk aside). The seat's own bombs never throw its carriers.
constexpr int32_t kCorridorSlack = 10;        // half a step: walks that tie with the cheapest, and the rounding of the model
constexpr int32_t kSideSlack = 40;            // two steps

std::vector<int32_t> carrier_costs(const sim::Grid& grid, const Tiles& t, bool swimmer, const std::vector<sim::TileCoord>& open_fire, const std::vector<sim::TileCoord>& from) {
    std::vector<int32_t> dist(t.kind.size(), kFar);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    for (const sim::TileCoord& f : from) {
        if (!t.in(f) || dist[t.at(f)] == 0) continue;
        dist[t.at(f)] = 0;
        queue.push({0, t.at(f)});
    }
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > dist[i]) continue;
        const sim::TileCoord c = t.tile(i);
        for (int dir = 0; dir < 8; ++dir) {
            const sim::TileCoord n{c.x + kDx[dir], c.y + kDy[dir]};
            if (!t.in(n)) continue;
            const int32_t step = carrier_step(grid, t, swimmer, open_fire, c, n);
            if (step < 0 || d + step >= dist[t.at(n)]) continue;
            dist[t.at(n)] = d + step;
            queue.push({d + step, t.at(n)});
        }
    }
    return dist;
}

std::vector<sim::TileCoord> carrier_bombs(const sim::Grid& grid, const Tiles& t, const MapInfo& map, uint8_t seat, bool swimmer, const std::vector<sim::TileCoord>& pile_cells,
                                          const std::vector<sim::TileCoord>& fires) {
    std::vector<sim::TileCoord> out;
    const std::vector<int32_t> from_hill = carrier_costs(grid, t, swimmer, fires, map.hill(seat).starts);
    std::vector<sim::TileCoord> sides;
    int32_t nearest = kFar;
    for (const sim::TileCoord& c : pile_cells) {
        for (int dir = 0; dir < 8; ++dir) {
            const sim::TileCoord n{c.x + kDx[dir], c.y + kDy[dir]};
            if (!t.in(n) || std::find(pile_cells.begin(), pile_cells.end(), n) != pile_cells.end()) continue;
            if (t.of(n) == kBomb && t.own[t.at(n)] == 0) add_once(out, n);
            if (from_hill[t.at(n)] >= kFar) continue;
            add_once(sides, n);
            nearest = std::min(nearest, from_hill[t.at(n)]);
        }
    }
    for (const sim::TileCoord& n : sides) {
        if (from_hill[t.at(n)] > nearest + kSideSlack) continue;
        const std::vector<int32_t> from_side = carrier_costs(grid, t, swimmer, fires, {n});
        for (size_t i = 0; i < t.kind.size(); ++i) {
            if (t.kind[i] == kBomb && from_side[i] < kFar && from_hill[i] < kFar && from_side[i] + from_hill[i] <= from_hill[t.at(n)] + kCorridorSlack) add_once(out, t.tile(i));
        }
    }
    return out;
}

}  // namespace

FieldWork FieldPlan::work() const noexcept {
    if (!field) return FieldWork::None;
    if (!bombs.empty()) return FieldWork::Bombs;
    if (!waters.empty()) return FieldWork::Bridges;
    if (!fires.empty()) return FieldWork::Fire;
    if (swim && (!has_swimmer || carrier_at == 0)) return FieldWork::Carrier;
    return FieldWork::None;
}

FieldPlan plan_field(const BotView& view, const MapInfo& map, uint8_t seat, const FieldHints& hints) {
    FieldPlan p;
    if (!view.has_grid() || seat >= sim::MAX_PLAYERS || !map.hill(seat).present) return p;
    const sim::Grid& grid = view.grid();
    if (static_cast<int>(grid.width()) != map.width() || static_cast<int>(grid.height()) != map.height()) return p;
    const GateInfo gates = GateInfo::analyse(map, grid, seat);
    if (gates.empty()) return p;
    const Tiles t = read_tiles(view, seat, hints);
    const AntView* breach = nullptr;
    for (const AntView& a : view.mine()) {
        p.has_swimmer = p.has_swimmer || a.type == sim::AntType::Swimmer;
        if (a.id == hints.ant) breach = &a;
    }
    bool kept_pile = false;                                                               // (a pile the hill walks to has a route with no work on it: the breach is done or not needed)
    for (const PileView& pv : view.piles()) {
        if (!pv.lunchbox && pv.index < gates.piles().size()) kept_pile = kept_pile || pv.index == hints.pile;
    }

    // the route: the one chosen before while its pile has food and each of its tiles is still one the carriers pass once the work is done; else the cheapest pile behind the field
    bool kept = kept_pile && hints.route.size() >= 2;
    for (const sim::TileCoord& c : hints.route) {
        const uint8_t k = t.of(c);
        kept = kept && (k == kClear || k == kBomb || k == kFire || k == kWater || k == kPass || k == kToken);
    }
    if (kept) {
        p.route = hints.route;
        p.pile = hints.pile;
    } else {
        Mover carrier;
        carrier.swim = kSwim;
        carrier.dig = kDefuse;
        carrier.put_out = kPutOut;
        carrier.token = kTokenWay;
        std::vector<std::pair<sim::TileCoord, int32_t>> hill;
        for (const sim::TileCoord& s : map.hill(seat).starts) hill.push_back({s, 0});
        const Search from_hill = search(t, carrier, hill);
        int32_t best = kFar;
        sim::TileCoord end{-1, -1};
        for (const PileView& pv : view.piles()) {
            if (pv.lunchbox || pv.index >= gates.piles().size()) continue;
            const GateTarget& g = gates.piles()[pv.index];
            const bool listed = std::find(hints.piles.begin(), hints.piles.end(), pv.index) != hints.piles.end();
            if (hints.piles.empty() ? (g.gate != GateKind::Sealed && g.gate != GateKind::Mixed && !g.free()) : !listed) continue;   // (a wall of one kind is the gate goals' own)
            const PileInfo* info = map.pile(pv.index);
            if (info == nullptr) continue;
            for (const sim::TileCoord& c : info->cells) {
                for (int dir = 0; dir < 8; ++dir) {
                    const sim::TileCoord n{c.x + kDx[dir], c.y + kDy[dir]};
                    if (std::find(info->cells.begin(), info->cells.end(), n) != info->cells.end() || !from_hill.reaches(t, n) || t.of(n) == kPass || t.of(n) == kToken) continue;
                    if (from_hill.cost(t, n) < best) {
                        best = from_hill.cost(t, n);
                        end = n;
                        p.pile = pv.index;
                    }
                }
            }
        }
        if (end.x < 0) return p;
        p.route = way_to(t, from_hill, end);
    }
    for (const sim::TileCoord& c : p.route) {                                            // (a bridge on the route is water to the carriers: it falls in after 3,600 ticks)
        if (t.of(c) == kFire) p.fires.push_back(c);
        else if (t.of(c) == kBomb) add_once(p.bombs, c);
        if (t.of(c) == kWater || (t.in(c) && grid.get_cell(c).has_any_bridge())) p.swim = true;
    }
    p.field = !p.fires.empty() || !p.bombs.empty() || (kept && p.swim);
    if (!p.field) return p;
    const PileInfo* pile_info = map.pile(p.pile);
    const std::vector<sim::TileCoord> pile_cells = pile_info != nullptr ? pile_info->cells : std::vector<sim::TileCoord>{};
    for (const sim::TileCoord& c : carrier_bombs(grid, t, map, seat, p.swim, pile_cells, p.fires)) add_once(p.bombs, c);
    if (p.swim && breach != nullptr && breach->type == sim::AntType::Swimmer) p.carrier_at = breach->tile.chebyshev_dist(p.route.back()) <= 1 ? 1 : 0;

    // the sources of the typed ants: the breach ant when it is of the kind; else the power-up chosen before while it lies there; else every power-up of the kind that nobody stands on
    const auto sources_of = [&](sim::AntType kind, sim::TileCoord keep) {
        std::vector<std::pair<sim::TileCoord, int32_t>> out;
        if (breach != nullptr && breach->type == kind) {
            out.push_back({breach->tile, 0});
            return out;
        }
        const auto free_for_us = [&](const PowerUpView& u) { return u.standing_team == 255 || (breach != nullptr && u.standing_ant == breach->id); };   // (a flight may have put it there)
        bool kept_here = false;
        for (const PowerUpView& u : view.powerups()) kept_here = kept_here || (u.kind == kind && free_for_us(u) && u.tile == keep);
        for (const PowerUpView& u : view.powerups()) {
            if (u.kind == kind && free_for_us(u) && (!kept_here || u.tile == keep)) out.push_back({u.tile, 0});
        }
        return out;
    };
    const auto sides = [&](const std::vector<sim::TileCoord>& cells, bool water_ok) {
        std::vector<sim::TileCoord> out;
        for (const sim::TileCoord& c : cells) {
            for (const sim::TileCoord& sd : sides_of(c)) {
                const uint8_t k = t.of(sd);
                if (k == kClear || k == kBomb || (water_ok && k == kWater)) add_once(out, sd);
            }
        }
        return out;
    };
    const std::vector<std::pair<sim::TileCoord, int32_t>> swimmers = sources_of(sim::AntType::Swimmer, hints.swim_source);
    const auto still_open = [&](const std::vector<sim::TileCoord>& way, bool water_ok) {     // a way kept from before: every tile still one its ant passes once the work is done
        if (way.size() < 2) return false;
        for (const sim::TileCoord& c : way) {
            const uint8_t k = t.of(c);
            if (!(k == kClear || k == kBomb || k == kFire || k == kToken || k == kPass || (water_ok && k == kWater))) return false;
        }
        return true;
    };
    const auto take_items = [&](const std::vector<sim::TileCoord>& way, bool waters) {
        for (const sim::TileCoord& c : way) {
            if (t.of(c) == kBomb) add_once(p.bombs, c);
            else if (waters && t.of(c) == kWater) add_once(p.waters, c);
        }
    };

    // The regions of the field: what a Bomber reaches by digging, without a hop (ground, bombs and power-ups, 8 neighbours; water and fire part them). A source of the Fire Ant or of the
    // Swimmer whose way has work in a region that holds none of the route's costs the one breach ant a hop more (2 hit points, a dud a fifth of the time, the time): kNewRegion
    std::vector<int32_t> region(t.kind.size(), -1);
    {
        int32_t next = 0;
        for (size_t i = 0; i < t.kind.size(); ++i) {
            const uint8_t k0 = t.kind[i];
            if (region[i] >= 0 || !(k0 == kClear || k0 == kBomb || k0 == kPass || k0 == kToken)) continue;
            std::vector<size_t> stack{i};
            region[i] = next;
            while (!stack.empty()) {
                const sim::TileCoord c = t.tile(stack.back());
                stack.pop_back();
                for (int dir = 0; dir < 8; ++dir) {
                    const sim::TileCoord n{c.x + kDx[dir], c.y + kDy[dir]};
                    if (!t.in(n) || region[t.at(n)] >= 0) continue;
                    const uint8_t k = t.of(n);
                    if (!(k == kClear || k == kBomb || k == kPass || k == kToken)) continue;
                    region[t.at(n)] = next;
                    stack.push_back(t.at(n));
                }
            }
            ++next;
        }
    }
    std::vector<int32_t> work_regions;
    for (const sim::TileCoord& b : p.bombs) {
        if (region[t.at(b)] >= 0 && std::find(work_regions.begin(), work_regions.end(), region[t.at(b)]) == work_regions.end()) work_regions.push_back(region[t.at(b)]);
    }
    const auto new_regions = [&](const std::vector<sim::TileCoord>& tiles, std::vector<int32_t>& seen) {
        int32_t added = 0;
        for (const sim::TileCoord& c : tiles) {
            const int32_t r = t.in(c) ? region[t.at(c)] : -1;
            if (r < 0 || std::find(seen.begin(), seen.end(), r) != seen.end()) continue;
            seen.push_back(r);
            ++added;
        }
        return added;
    };
    const auto work_of = [&](const std::vector<sim::TileCoord>& way, bool waters_too) {   // the tiles of a way the breach ant works or stands on: its bombs, the land beside its water, its source
        std::vector<sim::TileCoord> out;
        if (!way.empty()) out.push_back(way.front());
        for (const sim::TileCoord& c : way) {
            if (t.of(c) == kBomb) out.push_back(c);
            if (waters_too && t.of(c) == kWater) {
                for (const sim::TileCoord& sd : sides_of(c)) {
                    if (t.of(sd) == kClear || t.of(sd) == kBomb) out.push_back(sd);
                }
            }
        }
        return out;
    };
    struct Way {
        std::vector<sim::TileCoord> tiles;
        int32_t cost{kFar};
    };
    const auto way_from = [&](const Mover& m, sim::TileCoord source, const std::vector<sim::TileCoord>& targets, const Extra& extra) {
        Way w;
        const Search sr = search(t, m, {{source, 0}}, extra);
        sim::TileCoord at{-1, -1};
        for (const sim::TileCoord& c : targets) {
            if (sr.cost(t, c) < w.cost) {
                w.cost = sr.cost(t, c);
                at = c;
            }
        }
        if (at.x >= 0) w.tiles = way_to(t, sr, at);
        return w;
    };
    Mover swimmer;
    swimmer.swim = kSwim;
    swimmer.dig = kDefuse;
    swimmer.token = kTokenWay;
    const auto best_swim = [&](const std::vector<sim::TileCoord>& waters, std::vector<int32_t> seen, int32_t& cost) {   // the swim way to the land beside `waters`, cheapest with its regions
        Way best;
        cost = kFar;
        std::vector<sim::TileCoord> land;
        for (const sim::TileCoord& w : waters) {
            for (const sim::TileCoord& sd : sides_of(w)) {
                if (t.of(sd) == kClear || t.of(sd) == kBomb) add_once(land, sd);
            }
        }
        for (const auto& src : swimmers) {
            const Way w = way_from(swimmer, src.first, land, Extra{});
            if (w.cost >= kFar) continue;
            std::vector<int32_t> s2 = seen;
            const int32_t c = w.cost + kNewRegion * new_regions(work_of(w.tiles, false), s2);
            if (c < cost) {
                cost = c;
                best = w;
            }
        }
        return best;
    };

    // the fire way (the one kept from before while it is open: the work done on it would be lost; a power-up dropped on it does not close it); else the source whose ways cost least,
    // their new regions counted
    if (!p.fires.empty() && kept && still_open(hints.fire_way, true)) {
        p.fire_way = hints.fire_way;
        p.fire_source = hints.fire_source;
        take_items(p.fire_way, true);
    } else if (!p.fires.empty()) {
        Mover fire;
        fire.fire = kStep;
        fire.dig = kDefuse;
        fire.bridge = swimmers.empty() ? -1 : kBridgeCost;
        fire.token = kTokenWay;
        const std::vector<sim::TileCoord> targets = sides(p.fires, !swimmers.empty());
        int32_t best = kFar;
        for (const auto& src : sources_of(sim::AntType::Fire, hints.fire_source)) {
            Extra drop(t.kind.size(), 0);                                                 // from a power-up the way leaves over water (bridged) where it can
            if (t.of(src.first) == kToken || t.of(src.first) == kPass) {
                const auto dug = [&](sim::TileCoord c) { return t.of(c) == kBomb && std::find(p.bombs.begin(), p.bombs.end(), c) != p.bombs.end(); };
                int32_t free_land = 0;                                                    // (free when the ant takes it: clear now, or a bomb of the work)
                for (int dir = 0; dir < 8; ++dir) {
                    const sim::TileCoord n{src.first.x + kDx[dir], src.first.y + kDy[dir]};
                    free_land += (t.of(n) == kClear || dug(n)) ? 1 : 0;
                }
                for (int dir = 0; dir < 8; ++dir) {                                       // (a bomb that only the way itself digs is one more such tile)
                    const sim::TileCoord n{src.first.x + kDx[dir], src.first.y + kDy[dir]};
                    if (t.of(n) == kClear || t.of(n) == kBomb) drop[t.at(n)] = kDropShut / std::max<int32_t>(1, free_land + ((t.of(n) == kBomb && !dug(n)) ? 1 : 0));
                }
            }
            const Way w = way_from(fire, src.first, targets, drop);
            if (w.cost >= kFar) continue;
            std::vector<int32_t> seen = work_regions;
            int32_t cost = w.cost + kNewRegion * new_regions(work_of(w.tiles, true), seen);
            std::vector<sim::TileCoord> waters;
            for (const sim::TileCoord& c : w.tiles) {
                if (t.of(c) == kWater) waters.push_back(c);
            }
            if (!waters.empty()) {
                int32_t swim_cost = kFar;
                best_swim(waters, seen, swim_cost);
                if (swim_cost >= kFar) continue;
                cost += swim_cost;
            }
            if (cost < best) {
                best = cost;
                p.fire_way = w.tiles;
            }
        }
        if (!p.fire_way.empty()) {
            p.fire_source = p.fire_way.front();
            take_items(p.fire_way, true);
        }
    }

    // the swim way: to the land beside the fire way's water (kept as the fire way)
    if (!p.waters.empty() && kept && still_open(hints.swim_way, true)) {
        p.swim_way = hints.swim_way;
        p.swim_source = hints.swim_source;
        take_items(p.swim_way, false);
    } else if (!p.waters.empty()) {
        std::vector<int32_t> seen = work_regions;
        new_regions(work_of(p.fire_way, true), seen);
        int32_t cost = kFar;
        const Way w = best_swim(p.waters, seen, cost);
        if (!w.tiles.empty()) {
            p.swim_way = w.tiles;
            p.swim_source = p.swim_way.front();
            take_items(p.swim_way, false);
        }
    }
    return p;
}

FieldStep next_field_step(const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant, const FieldHints& hints) {
    const FieldWork work = plan.work();
    if (work == FieldWork::None || !view.has_grid()) return FieldStep{};
    const Tiles t = read_tiles(view, seat, hints);
    switch (work) {
        case FieldWork::Bombs:
            if (ant.type != sim::AntType::Bomber) return take_step(t, view, map, seat, ant, sim::AntType::Bomber, sim::TileCoord{-1, -1}, hints, work);
            return dig_step(t, plan, view, map, seat, ant, hints);
        case FieldWork::Bridges:
            if (ant.type != sim::AntType::Swimmer) return take_step(t, view, map, seat, ant, sim::AntType::Swimmer, plan.swim_source, hints, work);
            return work_from_side(t, view, ant, plan.waters, kWater, FieldAction::Bridge, work);
        case FieldWork::Fire:
            if (ant.type != sim::AntType::Fire) return take_step(t, view, map, seat, ant, sim::AntType::Fire, plan.fire_source, hints, work);
            return work_from_side(t, view, ant, plan.fires, kFire, FieldAction::Extinguish, work);
        case FieldWork::Carrier:
            if (ant.type == sim::AntType::Swimmer) {                                       // the carrier starts at the pile: the walk the engine gives it from far may cross bombs
                if (plan.route.empty() || ant.tile.chebyshev_dist(plan.route.back()) <= 1) return FieldStep{};
                const Search reach = search(t, mover_of(ant.type), {{ant.tile, 0}}, crowd(t, view, ant.id));
                return toward(t, reach, plan.route.back(), FieldAction::None, plan.route.back(), work);
            }
            return take_step(t, view, map, seat, ant, sim::AntType::Swimmer, plan.swim_source, hints, work);
        default:
            return FieldStep{};
    }
}

// The choice of the breach ant: the work is reached from the hill's side (an ant away from it may be cut off), and a hurt ant has fewer hops in it
constexpr int32_t kAwayFromHill = 60;
constexpr uint8_t kHurtHp = 5;
constexpr int32_t kHurt = 40;

int32_t field_ant_cost(const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat, const AntView& ant) {
    const FieldWork work = plan.work();
    if (work == FieldWork::None || !view.has_grid()) return -1;
    const sim::AntType need = work == FieldWork::Bombs ? sim::AntType::Bomber : (work == FieldWork::Fire ? sim::AntType::Fire : sim::AntType::Swimmer);
    const Tiles t = read_tiles(view, seat, FieldHints{});
    int32_t cost = 0;
    if (ant.type != need) {
        Mover m = mover_of(ant.type);
        m.token = -1;
        const Search reach = search(t, m, {{ant.tile, 0}});
        cost = kFar;
        for (const PowerUpView& p : view.powerups()) {
            if (p.kind != need || p.standing_team != 255 || shuts_in(t, view, ant, p.tile, need)) continue;
            if (ant.tile.chebyshev_dist(p.tile) <= 1) cost = std::min(cost, kStep);
            for (int dir = 0; dir < 8; ++dir) {
                const sim::TileCoord n{p.tile.x + kDx[dir], p.tile.y + kDy[dir]};
                const uint8_t k = t.of(n);
                if (k == kClear || (k == kWater && ant.type == sim::AntType::Swimmer)) cost = std::min(cost, reach.cost(t, n) + kStep);
            }
        }
        if (cost >= kFar) return -1;
    }
    if (!hill_walk(t, map, seat, ant.tile)) cost += kAwayFromHill;
    if (ant.hp < kHurtHp) cost += kHurt;
    return cost;
}

bool field_startable(const FieldPlan& plan, const BotView& view, const MapInfo& map, uint8_t seat) {
    if (!view.has_grid() || !map.hill(seat).present) return false;
    const Tiles t = read_tiles(view, seat, FieldHints{});
    for (const AntView& a : view.mine()) {
        if (hill_walk(t, map, seat, a.tile) && field_ant_cost(plan, view, map, seat, a) >= 0) return true;
    }
    return false;
}

}  // namespace ants::ai
