#include "ants_ai/island_bridge.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace ants::ai {

namespace {

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

// The islands that stand joined now: a channel whose tiles are all finished bridges joins its two components. The root of a set is its lowest component number.
class Sets {
public:
    explicit Sets(size_t n) : parent_(n) { std::iota(parent_.begin(), parent_.end(), 0); }
    int32_t find(int32_t x) {
        while (parent_[static_cast<size_t>(x)] != x) {
            parent_[static_cast<size_t>(x)] = parent_[static_cast<size_t>(parent_[static_cast<size_t>(x)])];
            x = parent_[static_cast<size_t>(x)];
        }
        return x;
    }
    void unite(int32_t a, int32_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent_[static_cast<size_t>(std::max(a, b))] = std::min(a, b);
    }

private:
    std::vector<int32_t> parent_;
};

// A channel that does not stand yet: the water tiles still to dig, and whether every one of them can be dug (or is being dug)
struct Edge {
    const Channel* channel{nullptr};
    int32_t a{-1};                           // the roots of the two sets that it joins
    int32_t b{-1};
    int32_t missing{0};
};

constexpr int32_t kFar = 1 << 20;

// The cheapest chain of edges (fewest tiles) from the set `from` to the set `to`, at most `limit` tiles; the edges in the order of the walk. Empty when there is none (or the sets are the same)
std::vector<size_t> chain(const std::vector<Edge>& edges, size_t sets, int32_t from, int32_t to, int32_t limit) {
    if (from == to) return {};
    std::vector<int32_t> dist(sets, kFar);
    std::vector<int64_t> via(sets, -1);                                                         // the edge that gave the set its distance
    dist[static_cast<size_t>(from)] = 0;
    for (size_t round = 0; round < sets; ++round) {
        bool changed = false;
        for (size_t i = 0; i < edges.size(); ++i) {
            const Edge& e = edges[i];
            const int32_t da = dist[static_cast<size_t>(e.a)];
            const int32_t db = dist[static_cast<size_t>(e.b)];
            if (da + e.missing < db) {
                dist[static_cast<size_t>(e.b)] = da + e.missing;
                via[static_cast<size_t>(e.b)] = static_cast<int64_t>(i);
                changed = true;
            }
            if (db + e.missing < da) {
                dist[static_cast<size_t>(e.a)] = db + e.missing;
                via[static_cast<size_t>(e.a)] = static_cast<int64_t>(i);
                changed = true;
            }
        }
        if (!changed) break;
    }
    if (dist[static_cast<size_t>(to)] > limit) return {};
    std::vector<size_t> path;
    int32_t at = to;
    while (at != from) {
        const int64_t i = via[static_cast<size_t>(at)];
        if (i < 0 || path.size() > sets) return {};
        const Edge& e = edges[static_cast<size_t>(i)];
        path.push_back(static_cast<size_t>(i));
        at = e.a == at ? e.b : e.a;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace

void BridgePlanner::release_all(AntLedger& ledger) {
    for (const auto& h : home_) ledger.release(h.first, owner_);
    home_.clear();
    returning_.clear();
}

const BridgeJob* BridgePlanner::look(TaskContext& c, const Tactics& tactics, const IslandInfo& info, uint32_t swimmers, const std::function<bool(sim::TileCoord)>& blocked) {
    const LevelPlan& plan = tactics.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    stranded_ = 0;
    const bool return_on = plan.island_return && v.hill_enterable();
    const bool feeder_on = plan.island_feeder;
    const bool power_on = plan.island_power_bridge;
    if (!(return_on || feeder_on || power_on) || info.empty()) {
        still_.clear();
        return nullptr;
    }
    // the ants that stand free and still: since when
    std::map<uint32_t, Still> next;
    for (const AntView& a : v.mine()) {
        if (a.type == sim::AntType::Swimmer || !a.idle() || a.holding || a.carried_points > 0 || c.ledger.owner(a.id) != kNoTask) continue;
        const auto was = still_.find(a.id);
        next[a.id] = was != still_.end() && was->second.tile == a.tile ? was->second : Still{a.tile, now};
    }
    still_ = std::move(next);
    if (swimmers < params_.min_swimmers || v.ticks_left() < params_.min_ticks_left) return nullptr;
    const int32_t hill_comp = c.map.hill_component(c.seat);
    if (hill_comp < 0) return nullptr;
    // the ants that wait for a bridge
    std::vector<const AntView*> waiting;
    for (const AntView& a : v.mine()) {
        const auto s = still_.find(a.id);
        if (s == still_.end() || now < s->second.since + plan.island_bridge_idle || !a.takes_orders() || a.hp == 0) continue;
        if (c.map.ant_component(c.seat, a.tile) < 0) continue;                                    // (on a bridge, or on no island at all)
        waiting.push_back(&a);
    }
    if (waiting.empty()) return nullptr;

    // the islands that stand joined now, and the channels that are still to be dug
    const std::vector<IslandComponent>& comps = info.components();
    Sets sets(comps.size());
    std::vector<bool> foreign(comps.size(), false);                                              // the island of another team's hill: no bridge leads there
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const HillInfo& other = c.map.hill(t);
        if (t == c.seat || !other.present) continue;
        std::map<int32_t, int32_t> votes;                                                        // (the components are the seat's own: the island is the one most of the tiles round the other mound are on)
        for (int32_t dy = -6; dy <= 10; ++dy) {
            for (int32_t dx = -6; dx <= 10; ++dx) {
                const int32_t k = c.map.ant_component(c.seat, sim::TileCoord{other.origin.x + dx, other.origin.y + dy});
                if (k >= 0) ++votes[k];
            }
        }
        int32_t best_votes = 0;
        int32_t island = -1;
        for (const auto& e : votes) {
            if (e.second > best_votes) {
                best_votes = e.second;
                island = e.first;
            }
        }
        if (island >= 0 && static_cast<size_t>(island) < foreign.size()) foreign[static_cast<size_t>(island)] = true;
    }
    std::vector<const Channel*> open;
    for (const Channel& ch : info.channels()) {
        if (ch.a < 0 || ch.b < 0 || static_cast<size_t>(ch.a) >= comps.size() || static_cast<size_t>(ch.b) >= comps.size()) continue;
        bool stands = true;
        for (const sim::TileCoord t : ch.tiles) stands = stands && grid.get_cell(t).has_completed_bridge();
        if (stands) sets.unite(ch.a, ch.b);
        else open.push_back(&ch);
    }
    std::vector<Edge> edges;
    for (const Channel* ch : open) {
        if (foreign[static_cast<size_t>(ch->a)] || foreign[static_cast<size_t>(ch->b)]) continue;
        Edge e;
        e.channel = ch;
        e.a = sets.find(ch->a);
        e.b = sets.find(ch->b);
        if (e.a == e.b) continue;
        bool usable = true;
        for (const sim::TileCoord t : ch->tiles) {
            const sim::TileCell& cell = grid.get_cell(t);
            if (cell.has_completed_bridge()) continue;
            ++e.missing;
            usable = usable && (cell.has_partial_bridge() || (bridge_water(grid, t) && !blocked(t)));
        }
        if (usable && e.missing > 0 && e.missing <= static_cast<int32_t>(plan.island_bridge_tiles)) edges.push_back(e);
    }
    if (edges.empty()) return nullptr;
    const MapInfo::NowField now_field = c.map.field_now(grid, c.seat, v.walk_context());
    const int32_t hill_set = sets.find(hill_comp);

    // the food on the islands, by the pile table's number (what each island holds is in the analysis of the start; what is left of it is on the view)
    std::map<uint32_t, int64_t> food;
    if (feeder_on) {
        for (const PileView& p : v.piles()) {
            if (!p.lunchbox) food[p.index] = static_cast<int64_t>(p.remaining) * p.value;
        }
    }
    // the power-ups of the start that lie on an island, and the island
    struct Spot {
        const PowerUpView* p;
        int32_t comp;
    };
    std::vector<Spot> spots;
    if (power_on) {
        uint8_t present = 0;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) present = static_cast<uint8_t>(present | (v.rows()[t].present ? 1u << t : 0u));
        for (const IslandComponent& comp : comps) {
            for (const uint32_t pi : comp.powerups) {
                const sim::TileCoord tile = c.map.powerups()[pi].tile;
                const PowerUpView* p = v.powerup_at(tile);
                if (p == nullptr || p->standing_ant != 0) continue;
                const int side = power_up_side(c.map, tile, present);
                if (side >= 0 && side != c.seat) continue;                                       // (another side's power-up is a theft: not a bridge's business)
                spots.push_back(Spot{p, comp.id});
            }
        }
    }

    const BridgeJob* best = nullptr;
    BridgeJob candidate;
    const auto consider = [&](BridgeJob job, const std::vector<size_t>& path, int32_t from_set) {
        const Edge& first = edges[path.front()];
        job.tiles = first.channel->tiles;
        if (sets.find(first.channel->a) != from_set) std::reverse(job.tiles.begin(), job.tiles.end());   // (from the ant's side outward)
        job.missing = 0;
        for (const size_t i : path) job.missing += edges[i].missing;
        if (best == nullptr || job.missing < candidate.missing || (job.missing == candidate.missing && (static_cast<int>(job.kind) < static_cast<int>(candidate.kind) ||
                                                                                                     (job.kind == candidate.kind && job.ant < candidate.ant)))) {
            candidate = std::move(job);
            best = &candidate;
        }
    };
    for (const AntView* a : waiting) {
        const int32_t from = sets.find(c.map.ant_component(c.seat, a->tile));
        const bool home = c.map.reaches_hill(now_field, a->tile, a->type);
        if (!home) ++stranded_;
        // the way to the hill's island: for a hurt ant (Return) or one whose island has food that the hill cannot walk to (Feeder)
        if (!home && from != hill_set && (return_on || feeder_on)) {
            const std::vector<size_t> path = chain(edges, comps.size(), from, hill_set, static_cast<int32_t>(plan.island_bridge_tiles));
            if (!path.empty()) {
                BridgeJob job;
                job.ant = a->id;
                bool wanted = false;
                if (return_on && a->hp <= params_.return_hp) {
                    job.kind = BridgeJob::Kind::Return;
                    wanted = true;
                } else if (feeder_on) {
                    std::set<int32_t> reached{from};                                             // the islands of the walk: the ant's own and the ones between (not the hill's)
                    for (size_t k = 0; k + 1 < path.size(); ++k) {
                        const Edge& e = edges[path[k]];
                        reached.insert(e.a);
                        reached.insert(e.b);
                    }
                    reached.erase(hill_set);
                    std::set<uint32_t> piles;
                    int64_t points = 0;
                    for (const IslandComponent& comp : comps) {
                        if (reached.count(sets.find(comp.id)) == 0) continue;
                        for (const uint32_t pi : comp.piles) {
                            const auto f = food.find(pi);
                            if (f != food.end() && piles.insert(pi).second) points += f->second;
                        }
                    }
                    job.kind = BridgeJob::Kind::Feeder;
                    wanted = points >= static_cast<int64_t>(params_.food_min_points);
                }
                if (wanted) consider(job, path, from);
            }
        }
        // a power-up that the plan wants and that lies on another island, for a whole plain ant with empty hands
        if (power_on && a->type == v.default_ant_type() && a->hp >= 5) {
            for (const sim::AntType kind : plan.opening_order) {
                const size_t want = tactics.wants[static_cast<size_t>(kind)];
                if (want == 0 || ((plan.island_power_kinds >> static_cast<unsigned>(kind)) & 1u) == 0) continue;
                size_t have = 0;
                for (const AntView& m : v.mine()) have += m.type == kind ? 1u : 0u;
                if (have >= want || (kind == sim::AntType::Fire && plan.fire_home_only && !home)) continue;
                bool reachable = false;                                                          // one that the economy takes on foot already: no bridge for it
                for (const Spot& s : spots) reachable = reachable || (s.p->kind == kind && (sets.find(s.comp) == from || sets.find(s.comp) == hill_set));
                if (reachable) continue;
                for (const Spot& s : spots) {
                    if (s.p->kind != kind) continue;
                    const int32_t to = sets.find(s.comp);
                    if (to == from || foreign[static_cast<size_t>(s.comp)]) continue;
                    const std::vector<size_t> path = chain(edges, comps.size(), from, to, static_cast<int32_t>(plan.island_bridge_tiles));
                    if (path.empty()) continue;
                    BridgeJob job;
                    job.kind = BridgeJob::Kind::Power;
                    job.ant = a->id;
                    consider(job, path, from);
                }
            }
        }
    }
    if (best != nullptr && best->kind == BridgeJob::Kind::Return) returning_.insert(best->ant);
    if (best == nullptr) return nullptr;
    job_ = candidate;
    return &job_;
}

void BridgePlanner::walk_home(TaskContext& c) {
    if (returning_.empty()) return;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    MapInfo::NowField field;
    bool made = false;
    std::vector<uint32_t> go;
    for (auto it = returning_.begin(); it != returning_.end();) {
        const uint32_t id = *it;
        const AntView* a = find_ant(v.mine(), id);
        const TaskId owner = c.ledger.owner(id);
        if (a == nullptr || (owner != kNoTask && owner != owner_)) {                               // gone, or another task has it
            home_.erase(id);
            it = returning_.erase(it);
            continue;
        }
        if (a->hp > params_.return_hp || !hill.present) {                                                          // healed (or no hill to go to): the economy has it again
            if (owner == owner_) c.ledger.release(id, owner_);
            home_.erase(id);
            it = returning_.erase(it);
            continue;
        }
        Home& h = home_[id];
        if (h.orders > 0 && now >= h.first + params_.home_wait) {                                  // told long ago: let go
            if (owner == owner_) c.ledger.release(id, owner_);
            home_.erase(id);
            it = returning_.erase(it);
            continue;
        }
        ++it;
        if (!a->takes_orders() || a->holding || a->carried_points > 0 || a->tile.chebyshev_dist(hill.entrance) <= 1) continue;
        if (!made) {
            field = c.map.field_now(v.grid(), c.seat, v.walk_context());
            made = true;
        }
        if (!c.map.reaches_hill(field, a->tile, a->type)) continue;                                // (the way does not stand yet)
        if (h.orders >= params_.home_orders || (h.orders > 0 && (now < h.last + params_.home_gap || !a->idle()))) continue;
        if (!c.ledger.claim(id, owner_)) continue;
        if (h.orders == 0) {
            h.first = now;
            ++homeward_;
        }
        h.last = now;
        ++h.orders;
        go.push_back(id);
    }
    if (!go.empty()) {
        c.orders.move(go, hill.entrance, Priority::Normal);                                        // (an order onto the own hill is a walk home: the door heals)
        home_orders_given_ += static_cast<uint32_t>(go.size());
    }
}

}  // namespace ants::ai
