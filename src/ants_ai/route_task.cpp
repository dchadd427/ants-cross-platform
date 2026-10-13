#include "ants_ai/route_task.hpp"

#include <algorithm>
#include <deque>
#include <utility>

#include "ants_ai/bot_view.hpp"
#include "ants_ai/gate_info.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/path_planner.hpp"

namespace ants::ai {

namespace {

constexpr int kDrow[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kDcol[8] = {0, 1, 1, 1, 0, -1, -1, -1};

bool foreign_bomb_at(const BotView& view, sim::TileCoord t, uint8_t seat) {
    for (const BombView& b : view.bombs()) {
        if (b.tile == t && b.owner != seat) return true;
    }
    return false;
}

}  // namespace

void RouteTask::cancel_workers(AntLedger& ledger) {
    ledger.release_all(id());
    workers_.clear();
}

void RouteTask::cancel(AntLedger& ledger) {
    ledger.release_all(id());
    workers_.clear();
    walks_.clear();
    closed_.clear();
    route_.clear();
    shared_.clear();
    route_bombs_ = 0;
    next_look_ = 0;
}

// The walks of the economy's ants: from the hill to the pile's click tile and back, from the workers' tiles, and from each Bomber's own tile
void RouteTask::predict(TaskContext& context) {
    const BotView& view = context.view;
    const sim::Grid& grid = view.grid();
    const WalkContext wc = view.walk_context();
    const HillInfo& hill = context.map.hill(context.seat);
    route_.clear();
    shared_.clear();
    have_pile_ = false;
    int32_t best = -1;
    sim::TileCoord click{-1, -1};
    for (const PileView& p : view.piles()) {
        if (p.lunchbox) continue;
        const PileInfo* info = p.index < context.map.piles().size() ? &context.map.piles()[p.index] : nullptr;
        if (info == nullptr || !info->approach[context.seat].reachable()) continue;
        if (best < 0 || info->approach[context.seat].cost < best) {
            best = info->approach[context.seat].cost;
            click = info->approach[context.seat].click;
            pile_ = p.index;
            have_pile_ = true;
        }
    }
    if (!have_pile_) return;
    const uint8_t seat = context.seat;
    const sim::PathSearch::StepCostFn cost = [&](sim::TileCoord from, sim::TileCoord to) -> uint32_t {
        if (to != click && !MapInfo::walkable(grid, seat, to, wc)) return 8000u;                  // (the click is a cell of the pile: the walk ends onto it)
        return MapInfo::step_cost(grid, from, to);
    };
    sim::PathGridPool pool;
    auto add = [&](sim::TileCoord a, sim::TileCoord b, std::vector<sim::TileCoord>& into) {
        sim::PathSearch search(0, a, b, static_cast<uint16_t>(grid.height()), static_cast<uint16_t>(grid.width()), pool);
        while (!search.finished()) {
            if (search.step(sim::PathSearch::kDefaultBudget, cost) == 0) break;
        }
        for (const sim::TileCoord& t : search.path()) {
            if (std::find(into.begin(), into.end(), t) == into.end()) into.push_back(t);
        }
    };
    walks_.clear();
    for (const AntView& a : view.mine()) {                                                  // the walk of every Bomber (held or not: a lone Bomber is the economy)
        if (a.type == sim::AntType::Bomber && a.takes_orders()) add(a.tile, click, walks_[a.id]);
    }
    uint32_t others = 0;
    for (const AntView& a : view.mine()) {                                                  // the walks of the workers that will go to the food (the first sixteen: a walk costs a search)
        if (a.type == sim::AntType::Worker && others++ < 16u) add(a.tile, click, shared_);
    }
    for (const sim::TileCoord& s : hill.starts) {
        add(s, click, shared_);
        add(click, s, shared_);
    }
    for (const sim::TileCoord& t : shared_) route_.push_back(t);
    pile_cells_.clear();
    if (pile_ < context.map.piles().size()) pile_cells_ = context.map.piles()[pile_].cells;
    pile_cells_.push_back(click);
    const size_t pile_n = pile_cells_.size();
    for (size_t i = 0; i < pile_n; ++i) {                                                   // the workers stand on the tiles round the pile when they harvest (the engine stops a walk next to a pile)
        for (int d = 0; d < 8; ++d) {
            const sim::TileCoord n{pile_cells_[i].x + kDcol[d], pile_cells_[i].y + kDrow[d]};
            if (n.x >= 0 && n.y >= 0 && n.x < static_cast<int>(grid.width()) && n.y < static_cast<int>(grid.height())) pile_cells_.push_back(n);
        }
    }
    for (const sim::TileCoord& t : pile_cells_) {                                          // (a walk ends ON its goal and the path does not list it: the pile's cells and the tiles round them, where the workers spread)
        if (std::find(route_.begin(), route_.end(), t) == route_.end()) route_.push_back(t);
    }
    for (const auto& w : walks_) {
        for (const sim::TileCoord& t : w.second) {
            if (std::find(route_.begin(), route_.end(), t) == route_.end()) route_.push_back(t);
        }
    }
}

void RouteTask::step(TaskContext& context) {
    const BotView& view = context.view;
    if (!view.has_grid()) return;
    const uint64_t now = view.tick();
    if (!fire_known_) {                                                                    // a map with fire walls (a maze of fire and bombs, POPcOrN's field) is the gate goals' business: a Bomber cannot follow the engine's walk through fire
        fire_known_ = true;
        gave_up_ = GateInfo::analyse(context.map, view.grid(), context.seat).fire_cell_count() > 0;
    }
    if (gave_up_) {
        if (!workers_.empty() || !closed_.empty()) cancel(context.ledger);
        return;
    }
    for (auto it = workers_.begin(); it != workers_.end();) {                               // a Bomber that died or was taken by a higher task
        bool alive = false;
        for (const AntView& a : view.mine()) alive = alive || a.id == it->first;
        if (!alive || context.ledger.owner(it->first) != id()) {
            context.ledger.release(it->first, id());
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
    size_t foreign = 0;
    for (const BombView& b : view.bombs()) foreign += b.owner != context.seat ? 1u : 0u;
    if (foreign == 0 || view.ticks_left() < 600) {                                         // no bomb on the map (the usual map): nothing is held, nothing is closed
        if (!workers_.empty() || !closed_.empty()) cancel(context.ledger);
        return;
    }
    if (now >= next_look_) {
        next_look_ = now + params_.look_every;
        predict(context);
        size_t n = 0;
        for (const sim::TileCoord& t : route_) n += foreign_bomb_at(view, t, context.seat) ? 1u : 0u;
        route_bombs_ = n;
        cleared_ = n == 0 && have_pile_;
        closed_.clear();
        if (n == 0) {                                                                      // the way is clear: the Bombers are given back to the economy
            if (!workers_.empty()) cancel_workers(context.ledger);
            return;
        }
        for (const AntView& a : view.mine()) {                                             // the Bombers of the economy, the healthiest first
            if (workers_.size() >= params_.max_bombers) break;
            if (a.type != sim::AntType::Bomber || a.hp < params_.min_hp || !a.takes_orders() || workers_.count(a.id) != 0) continue;
            if (context.ledger.take(a.id, id())) {
                workers_[a.id] = Bomber{};
                progress_ = now;
            }
        }
        if (have_pile_ && !workers_.empty()) closed_.insert(pile_);                        // the economy keeps off the pile while a Bomber is at the bombs of its way (with no Bomber to clear them it plays as it does without the routing)
    }
    if (workers_.empty() || route_bombs_ == 0) return;
    uint64_t sig = foreign * 1000003u;
    for (const AntView& a : view.mine()) {
        if (workers_.count(a.id) != 0) sig = sig * 31u + static_cast<uint64_t>(a.tile.y) * 4096u + static_cast<uint64_t>(a.tile.x);
    }
    if (sig != seen_sig_) {
        seen_sig_ = sig;
        progress_ = now;
    }
    if (now >= progress_ + params_.stall_ticks) {
        gave_up_ = true;
        cancel(context.ledger);
        return;
    }
    for (const AntView& a : view.mine()) {
        const auto it = workers_.find(a.id);
        if (it != workers_.end()) work(context, a, it->second);
    }
}

// One order for a Bomber: defuse a bomb of its way from a tile next to it, or one step towards such a tile over tiles without bombs
void RouteTask::work(TaskContext& context, const AntView& ant, Bomber& me) {
    const BotView& view = context.view;
    const uint64_t now = view.tick();
    const uint32_t gap = me.target.x >= 0 && me.last_order != 0 && now < me.last_order + params_.defuse_gap ? params_.defuse_gap : params_.order_gap;
    if (!ant.idle() || (me.last_order != 0 && now < me.last_order + gap)) return;
    const sim::Grid& grid = view.grid();
    const WalkContext wc = view.walk_context();
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    const uint8_t seat = context.seat;
    auto idx = [&](sim::TileCoord t) { return t.y * w + t.x; };
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };
    std::vector<uint8_t> bomb(static_cast<size_t>(w * h), 0), fire(static_cast<size_t>(w * h), 0), onroute(static_cast<size_t>(w * h), 0);
    for (const BombView& b : view.bombs()) {
        if (b.owner != seat && inside(b.tile.x, b.tile.y)) bomb[static_cast<size_t>(idx(b.tile))] = 1;      // (an own bomb is a launch pad of the gate goals' hop: MapInfo::walkable keeps every walk off it, and it is never defused here)
    }
    for (const FireWallView& f : view.fire_walls()) {
        if (inside(f.tile.x, f.tile.y)) fire[static_cast<size_t>(idx(f.tile))] = 1;
    }
    for (const sim::TileCoord& t : route_) onroute[static_cast<size_t>(idx(t))] = 1;
    for (const auto& other : workers_) {                                                    // a bomb that another Bomber is at work on is left to it
        const sim::TileCoord t = other.second.target;
        if (&other.second != &me && t.x >= 0 && now < other.second.last_order + params_.defuse_gap) bomb[static_cast<size_t>(idx(t))] = 2;
    }
    auto standable = [&](sim::TileCoord t) {
        return inside(t.x, t.y) && !fire[static_cast<size_t>(idx(t))] && MapInfo::walkable(grid, seat, t, wc);
    };
    // A Bomber on a bomb tile (a start ant of Bombz Away) sets that bomb off when it works without walking: the defuse from its own tile ends a path of no steps on the bomb (engine
    // walk_step, "a bomb on the tile"). It first walks off onto a tile without a bomb, if there is one. If every Bomber is boxed in by bombs, the healthiest takes the one hit (the throw),
    // the rest wait: it lands on ground without a bomb and clears the way.
    auto free_neighbour = [&](sim::TileCoord from_tile, sim::TileCoord& out) {
        for (int d = 0; d < 8; ++d) {
            const sim::TileCoord n{from_tile.x + kDcol[d], from_tile.y + kDrow[d]};
            if (inside(n.x, n.y) && !bomb[static_cast<size_t>(idx(n))] && standable(n)) {
                out = n;
                return true;
            }
        }
        return false;
    };
    if (bomb[static_cast<size_t>(idx(ant.tile))] != 0) {
        sim::TileCoord off{-1, -1};
        if (free_neighbour(ant.tile, off)) {
            context.orders.move({ant.id}, off);
            me.target = sim::TileCoord{-1, -1};
            me.last_order = now;
            ++steps_;
            return;
        }
        const AntView* starter = nullptr;
        bool all_boxed = true;
        for (const AntView& o : view.mine()) {
            if (workers_.count(o.id) == 0) continue;
            sim::TileCoord ignore{-1, -1};
            if (bomb[static_cast<size_t>(idx(o.tile))] == 0 || free_neighbour(o.tile, ignore)) all_boxed = false;
            else if (starter == nullptr || o.hp > starter->hp) starter = &o;
        }
        if (!all_boxed || starter == nullptr || starter->id != ant.id) return;
    }
    // the tiles that the Bomber can reach over tiles without a bomb (its own tile counts: a start ant stands on one), one step at a time
    std::vector<int> dist(static_cast<size_t>(w * h), -1), from(static_cast<size_t>(w * h), -1);
    std::deque<sim::TileCoord> queue;
    dist[static_cast<size_t>(idx(ant.tile))] = 0;
    queue.push_back(ant.tile);
    std::vector<sim::TileCoord> reach;
    while (!queue.empty()) {
        const sim::TileCoord c = queue.front();
        queue.pop_front();
        reach.push_back(c);
        for (int d = 0; d < 8; ++d) {
            const sim::TileCoord n{c.x + kDcol[d], c.y + kDrow[d]};
            if (!inside(n.x, n.y) || dist[static_cast<size_t>(idx(n))] >= 0 || bomb[static_cast<size_t>(idx(n))] || !standable(n)) continue;
            dist[static_cast<size_t>(idx(n))] = dist[static_cast<size_t>(idx(c))] + 1;
            from[static_cast<size_t>(idx(n))] = idx(c);
            queue.push_back(n);
        }
    }
    // the bomb to defuse: a bomb of the way next to a reachable tile on N, S, W or E; else a bomb that opens the way to one (a cardinal neighbour of a bomb that only a diagonal step touches)
    sim::TileCoord target{-1, -1}, stand{-1, -1};
    for (int pass = 0; pass < 2 && target.x < 0; ++pass) {
        int best = -1;
        size_t best_order = 0;
        for (const sim::TileCoord& c : reach) {
            for (int d = 0; d < 8; d += 2) {
                const sim::TileCoord t{c.x + kDcol[d], c.y + kDrow[d]};
                if (!inside(t.x, t.y) || bomb[static_cast<size_t>(idx(t))] != 1 || (!standable(t) && std::find(pile_cells_.begin(), pile_cells_.end(), t) == pile_cells_.end())) continue;
                size_t order = 0;
                if (pass == 0) {
                    if (!onroute[static_cast<size_t>(idx(t))]) continue;
                    order = static_cast<size_t>(std::find(route_.begin(), route_.end(), t) - route_.begin());
                } else {                                                                    // a helper: a bomb with a bomb of the way next to it
                    bool opens = false;
                    for (int e = 0; e < 8 && !opens; ++e) {
                        const int x = t.x + kDcol[e], y = t.y + kDrow[e];
                        opens = inside(x, y) && onroute[static_cast<size_t>(y * w + x)] && bomb[static_cast<size_t>(y * w + x)] != 0;
                    }
                    if (!opens) continue;
                }
                const int dc = dist[static_cast<size_t>(idx(c))];
                if (best < 0 || dc < best || (dc == best && order < best_order)) {
                    best = dc;
                    best_order = order;
                    target = t;
                    stand = c;
                }
            }
        }
    }
    if (target.x < 0) {                                                                     // no bomb of the way is next to the Bomber's ground: the bombs that lie between are part of its way too
        // the cheapest walk (a bomb tile costs a defuse) from the Bomber to any tile of the way that it cannot reach; the first bomb on it, next to the ground the Bomber has, is the one to defuse
        const int inf = 1 << 28;
        std::vector<int> cost(static_cast<size_t>(w * h), inf), prev(static_cast<size_t>(w * h), -1);
        std::vector<std::pair<int, int>> heap;                                              // (cost, tile), a plain binary heap on std::push_heap with the smallest first
        auto push = [&](int c, int t) { heap.emplace_back(-c, t); std::push_heap(heap.begin(), heap.end()); };
        cost[static_cast<size_t>(idx(ant.tile))] = 0;
        push(0, idx(ant.tile));
        int goal = -1;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end());
            const int c = -heap.back().first;
            const int t = heap.back().second;
            heap.pop_back();
            if (c > cost[static_cast<size_t>(t)]) continue;
            if (onroute[static_cast<size_t>(t)] && dist[static_cast<size_t>(t)] < 0) {
                goal = t;
                break;
            }
            for (int d = 0; d < 8; ++d) {
                const sim::TileCoord n{t % w + kDcol[d], t / w + kDrow[d]};
                if (!inside(n.x, n.y) || !standable(n) || bomb[static_cast<size_t>(idx(n))] == 2) continue;
                const int step = c + (bomb[static_cast<size_t>(idx(n))] != 0 ? 8 : 1);
                if (step < cost[static_cast<size_t>(idx(n))]) {
                    cost[static_cast<size_t>(idx(n))] = step;
                    prev[static_cast<size_t>(idx(n))] = t;
                    push(step, idx(n));
                }
            }
        }
        if (goal >= 0) {
            std::vector<int> path;                                                          // from the goal back to the Bomber
            for (int t = goal; t >= 0; t = prev[static_cast<size_t>(t)]) path.push_back(t);
            for (size_t k = path.size(); k-- > 1;) {                                        // forward from the Bomber's tile: the first bomb
                const int t = path[k - 1];
                if (!bomb[static_cast<size_t>(t)]) continue;
                const sim::TileCoord b{t % w, t / w};
                const sim::TileCoord from_tile{path[k] % w, path[k] / w};                    // (a tile of the ground the Bomber has: every tile before the first bomb is free)
                if (from_tile.x == b.x || from_tile.y == b.y) {
                    target = b;
                    stand = from_tile;
                } else {                                                                    // a diagonal step: a tile next to both, or the bomb on it
                    const sim::TileCoord a{from_tile.x, b.y}, c2{b.x, from_tile.y};
                    for (const sim::TileCoord& q : {a, c2}) {
                        if (!inside(q.x, q.y) || !standable(q)) continue;
                        if (!bomb[static_cast<size_t>(idx(q))]) {
                            target = b;
                            stand = q;
                        } else if (target.x < 0 && bomb[static_cast<size_t>(idx(q))] == 1) {
                            target = q;
                            stand = from_tile;
                        }
                        if (stand.x >= 0 && target == b) break;
                    }
                }
                break;
            }
        }
    }
    if (target.x < 0) return;                                                               // nothing within reach: the stall timer decides
    if (stand == ant.tile) {
        context.orders.special(ant.id, target);
        me.target = target;
        ++defuses_;
    } else {                                                                                // one step along the way to the tile next to it
        sim::TileCoord c = stand;
        while (from[static_cast<size_t>(idx(c))] != idx(ant.tile) && from[static_cast<size_t>(idx(c))] >= 0) {
            const int p = from[static_cast<size_t>(idx(c))];
            c = sim::TileCoord{p % w, p / w};
        }
        context.orders.move({ant.id}, c);
        me.target = sim::TileCoord{-1, -1};
        ++steps_;
    }
    me.last_order = now;
}

}  // namespace ants::ai
