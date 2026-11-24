#include "ants_ai/island_bridge.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <numeric>

#include "ants_ai/standard_bot.hpp"
#include "ants_sim/movement_tables.hpp"

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

// What every look does first: the age of the finished tiles of the channels, and the ants that stand on one that has little life left: they are walked to the nearest shore, whoever holds them (a finished
// tile lasts 3,600 ticks and drowns what is on it; a bridge piece that stood at the first look is the map file's and has no end)
void BridgePlanner::upkeep(TaskContext& c, const IslandInfo& info) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    for (const Channel& ch : info.channels()) {
        for (const sim::TileCoord t : ch.tiles) {
            const int32_t idx = t.y * static_cast<int32_t>(grid.width()) + t.x;
            if (grid.get_cell(t).has_completed_bridge()) since_.emplace(idx, primed_ ? now : kFromTheFile);
            else since_.erase(idx);
        }
    }
    primed_ = true;
    if (since_.empty()) return;
    std::vector<uint8_t> walk;
    for (const AntView& a : v.mine()) {
        if (a.type == sim::AntType::Swimmer || !grid.in_bounds(a.tile)) continue;
        const auto since = since_.find(a.tile.y * static_cast<int32_t>(grid.width()) + a.tile.x);
        if (since == since_.end() || since->second == kFromTheFile) continue;
        const int64_t left = static_cast<int64_t>(since->second) + params_.life_ticks - static_cast<int64_t>(now);
        if (left > static_cast<int64_t>(params_.evacuate_life) || !a.takes_orders() || c.ledger.owner(a.id) == StandardBot::kIslands) continue;       // (an ant of the island task has that task's guard)
        const auto last = evacuated_at_.find(a.id);
        if (last != evacuated_at_.end() && now < last->second + params_.evacuate_gap) continue;
        if (walk.empty()) walk = v.walkable_mask(c.seat);
        // the nearest land tile that the ant can walk to now, round the ants that stand in the way (an ant on the only shore tile shuts that end: the other end is the way off): not water, not a piece of a bridge
        std::vector<uint8_t> free_walk = walk;
        for (const AntView& o : v.others()) {
            if (grid.in_bounds(o.tile)) free_walk[static_cast<size_t>(o.tile.y * static_cast<int32_t>(grid.width()) + o.tile.x)] = 0;
        }
        for (const AntView& o : v.mine()) {
            if (o.id != a.id && grid.in_bounds(o.tile)) free_walk[static_cast<size_t>(o.tile.y * static_cast<int32_t>(grid.width()) + o.tile.x)] = 0;
        }
        const std::vector<int32_t> cost = MapInfo::cost_field(grid, free_walk, a.tile);
        sim::TileCoord shore{-1, -1};
        int32_t best = 1 << 30;
        for (int32_t dy = -8; dy <= 8; ++dy) {
            for (int32_t dx = -8; dx <= 8; ++dx) {
                const sim::TileCoord t{a.tile.x + dx, a.tile.y + dy};
                if (!grid.in_bounds(t) || grid.get_cell(t).has_any_bridge() || grid.terrain_class_at(t) == sim::movement::kTerrainWater) continue;
                const int32_t k = cost.empty() ? -1 : cost[static_cast<size_t>(t.y * static_cast<int32_t>(grid.width()) + t.x)];
                if (k >= 0 && k < best) {
                    best = k;
                    shore = t;
                }
            }
        }
        if (shore.x < 0) continue;
        c.orders.move({a.id}, shore, Priority::Urgent);
        evacuated_at_[a.id] = now;
        ++evacuated_;
    }
}

const BridgeJob* BridgePlanner::look(TaskContext& c, const Tactics& tactics, const IslandInfo& info, uint32_t swimmers, const std::function<bool(sim::TileCoord)>& blocked) {
    const LevelPlan& plan = tactics.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    const bool return_on = plan.island_return;
    const bool feeder_on = plan.island_feeder;
    const bool power_on = plan.island_power_bridge;
    const bool raid_on = plan.island_endraid && tactics.endraid;                                  // an own Thief that no walk joins to a hill that it could raid (docs/BOT_IDLE.md)
    const bool home_on = plan.fire_hats;                                                          // a Fire Ant that took a hat where no walk leads home
    walk_ttl_ = (plan.island_endraid || plan.fire_hats || plan.bridge_more) ? kWalkTicks : 0u;
    if (!(return_on || feeder_on || power_on || raid_on || home_on) || info.empty()) {
        still_.clear();
        return nullptr;
    }
    // the ants that stand free and still: since when
    std::map<uint32_t, Still> next;
    for (const AntView& a : v.mine()) {
        // (a Thief with loot that no walk takes home stands idle or in its can't-go clip: it waits for a chain home like the others, plan.island_endraid)
        const bool loaded_thief = raid_on && a.type == sim::AntType::Thief && (a.holding || a.carried_points > 0);
        if (a.type == sim::AntType::Swimmer || !(a.idle() || (loaded_thief && a.state == sim::UnitState::CantGo)) || ((a.holding || a.carried_points > 0) && !loaded_thief) || c.ledger.owner(a.id) != kNoTask) continue;
        const auto was = still_.find(a.id);
        next[a.id] = was != still_.end() && was->second.tile == a.tile ? was->second : Still{a.tile, now};
    }
    still_ = std::move(next);
    // with the food gone (plan.bridge_more) the Swimmers have nothing to ferry: the chains of the older kinds may be longer, an ant waits for its bridge a shorter time, one Swimmer is enough to dig
    const bool more = plan.bridge_more && tactics.late;
    more_ = more;
    const uint32_t chain_tiles = more ? std::max(plan.island_bridge_tiles, plan.bridge_more_tiles) : plan.island_bridge_tiles;
    const uint32_t idle_wait = more ? std::min(plan.island_bridge_idle, plan.bridge_more_idle) : plan.island_bridge_idle;
    if (swimmers < (more ? 1u : params_.min_swimmers) || v.ticks_left() < (more ? std::min<uint32_t>(params_.min_ticks_left, 1800u) : params_.min_ticks_left)) return nullptr;
    const int32_t hill_comp = c.map.hill_component(c.seat);
    if (hill_comp < 0) return nullptr;
    // the ants that wait for a bridge
    std::vector<const AntView*> waiting;
    for (const AntView& a : v.mine()) {
        const auto s = still_.find(a.id);
        const uint32_t wait = a.type == sim::AntType::Thief ? plan.endraid_wait : a.type == sim::AntType::Fire && home_on ? plan.fire_hat_wait : idle_wait;
        if (s == still_.end() || now < s->second.since + wait || !a.takes_orders() || a.hp == 0) continue;
        if (c.map.ant_component(c.seat, a.tile) < 0) continue;                                    // (on a bridge, or on no island at all)
        waiting.push_back(&a);
    }
    if (waiting.empty()) return nullptr;
    if (more) {                                                                                   // an idle ant that a walk joins to the hill now comes home too (plan.bridge_more): the bridge that was dug for another ant of its island is its way as well
        const MapInfo::NowField& walk = walk_now(c);
        for (const AntView* a : waiting) {
            if (!(raid_on && a->type == sim::AntType::Thief) && a->tile.chebyshev_dist(c.map.hill(c.seat).entrance) > 3 && c.map.reaches_hill(walk, a->tile, a->type)) returning_.insert(a->id);
        }
    }

    // the islands that stand joined now, and the channels that are still to be dug
    const std::vector<IslandComponent>& comps = info.components();
    Sets sets(comps.size());
    std::vector<bool> foreign(comps.size(), false);                                              // the island of another team's hill: no bridge leads there
    std::array<int32_t, sim::MAX_PLAYERS> team_island;                                            // ... and whose it is (-1: no hill, or the seat's own)
    team_island.fill(-1);
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
        if (island >= 0 && static_cast<size_t>(island) < foreign.size()) {
            foreign[static_cast<size_t>(island)] = true;
            team_island[t] = island;
        }
    }
    std::vector<const Channel*> open;
    for (const Channel& ch : info.channels()) {
        if (ch.a < 0 || ch.b < 0 || static_cast<size_t>(ch.a) >= comps.size() || static_cast<size_t>(ch.b) >= comps.size()) continue;
        bool stands = true;
        for (const sim::TileCoord t : ch.tiles) stands = stands && grid.get_cell(t).has_completed_bridge();
        if (stands) sets.unite(ch.a, ch.b);
        else open.push_back(&ch);
    }
    std::vector<bool> foreign_set(comps.size(), false);                                          // ... and so is every island that stands joined to one: a bridge to it would open a walk to that hill
    for (size_t k = 0; k < comps.size(); ++k) {
        if (foreign[k]) foreign_set[static_cast<size_t>(sets.find(static_cast<int32_t>(k)))] = true;
    }
    std::vector<Edge> edges;                                                                      // the channels that no foreign hill's island touches: the chains of the older kinds
    std::vector<Edge> all_edges;                                                                  // ... and every channel that can be dug, the foreign hills' islands too: the Raid chain
    const int32_t hop_limit = static_cast<int32_t>(std::max<uint32_t>(chain_tiles, raid_on || home_on ? 4u : 0u));
    for (const Channel* ch : open) {
        Edge e;
        e.channel = ch;
        e.a = sets.find(ch->a);
        e.b = sets.find(ch->b);
        if (e.a == e.b && !raid_on) continue;                                                     // (a channel inside a set: the Raid hop may be one, a short way beside a long walk)
        bool usable = true;
        for (const sim::TileCoord t : ch->tiles) {
            const sim::TileCell& cell = grid.get_cell(t);
            if (cell.has_completed_bridge()) continue;
            ++e.missing;
            usable = usable && (cell.has_partial_bridge() || (bridge_water(grid, t) && !blocked(t)));
        }
        if (!usable || e.missing <= 0 || e.missing > hop_limit) continue;
        if (raid_on) all_edges.push_back(e);
        if (e.a != e.b && !foreign_set[static_cast<size_t>(e.a)] && !foreign_set[static_cast<size_t>(e.b)]) edges.push_back(e);
    }
    if (edges.empty() && all_edges.empty()) return nullptr;
    const MapInfo::NowField& now_field = walk_now(c);
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
        for (const IslandComponent& comp : comps) {
            for (const uint32_t pi : comp.powerups) {
                const PowerUpView* p = v.powerup_at(c.map.powerups()[pi].tile);
                if (p != nullptr && p->standing_ant == 0) spots.push_back(Spot{p, comp.id});         // (one that an ant stands on cannot be taken)
            }
        }
    }

    const BridgeJob* best = nullptr;
    BridgeJob candidate;
    const BridgeJob* raid_best = nullptr;                                                         // (the chain of the Thief that is being judged: the best target so far)
    BridgeJob raid_job;
    std::vector<Edge> raid_pool;
    std::vector<size_t> raid_path;
    bool raid_hop_only = false;                                                                   // (the chain is the one hop from the hill's island: its tiles are dug from that island outward)
    int32_t raid_key = 0;
    const auto consider = [&](BridgeJob job, const std::vector<size_t>& path, int32_t from_set, const std::vector<Edge>& pool, int32_t start_comp = -1) {
        const Edge& first = pool[path.front()];
        job.tiles = first.channel->tiles;
        const bool reverse = start_comp >= 0 ? first.channel->b == start_comp : sets.find(first.channel->a) != from_set;
        if (reverse) std::reverse(job.tiles.begin(), job.tiles.end());                              // (from the ant's side outward)
        job.missing = 0;
        for (const size_t i : path) job.missing += pool[i].missing;
        if (best == nullptr || job.missing < candidate.missing || (job.missing == candidate.missing && (static_cast<int>(job.kind) < static_cast<int>(candidate.kind) ||
                                                                                                     (job.kind == candidate.kind && job.ant < candidate.ant)))) {
            candidate = std::move(job);
            best = &candidate;
        }
    };
    // The Raid chain of a Thief that stands on the island `from`: the cheapest way to join it to the own hill's island (the loot is banked there, and the Thief waits there) and ONE channel from the own hill's
    // island to the island of an enemy hill whose hole is open, never through the island of another enemy hill; at most plan.endraid_tiles tiles in all. The richest hill among the cheap ones is the target.
    // The edges of the chain, the Thief's side first
    const auto consider_raid = [&](const AntView& thief, int32_t from) {
        raid_best = nullptr;
        raid_key = 0;
        bool raid_ready = false;
        if (hill_set < 0 || all_edges.empty()) return;
        const uint32_t min_loot = std::min(plan.raid_min_loot, tactics.gone ? plan.gone_raid_min : plan.raid_min_loot);
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = v.rows()[t];
            const HillInfo& other = c.map.hill(t);
            if (t == c.seat || !other.present || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || team_island[t] < 0) continue;
            if (row.score < static_cast<int32_t>(min_loot)) continue;
            size_t open_tiles = 0;                                                                // (a hole that the walls have shut is no target: three fire walls stop every raid)
            for (const sim::TileCoord& e : east_tiles(other)) {
                const EastTile k = classify_tile(grid, e);
                open_tiles += (k == EastTile::Open || k == EastTile::Bare) ? 1u : 0u;
            }
            if (open_tiles < plan.raid_min_free) continue;
            const int32_t target = sets.find(team_island[t]);
            // the islands of the other enemy hills are left out of the chain (a way to them would be a way for them)
            std::vector<Edge> pool;
            for (const Edge& e : all_edges) {
                bool shut = false;
                for (uint8_t u = 0; u < sim::MAX_PLAYERS; ++u) {
                    if (u == t || team_island[u] < 0) continue;
                    const int32_t island = sets.find(team_island[u]);
                    if (island == hill_set || island == target || island == from) continue;      // (an enemy hill joined to the own hill's island already, or to the Thief's, is no new way for anybody)
                    shut = shut || e.a == island || e.b == island;
                }
                if (!shut) pool.push_back(e);
            }
            const int32_t limit = static_cast<int32_t>(plan.endraid_tiles);
            // the chain: the Thief's island to the hill's (the loot is banked there, and the Thief waits there between raids: the raid is a short walk), then ONE channel from the hill's island to the
            // target's (the neighbor hills are 3 or 4 tiles apart: a bridge for the Thief to cross, and a short way home)
            std::vector<size_t> best_path;
            int32_t best_total = 1 << 20;
            // a hill whose raid tiles the own hill's ants reach now over a short walk needs no chain: the raid task raids it (and nothing is dug for a Thief that can go)
            int32_t walk = -1;
            for (const sim::TileCoord& e : east_tiles(other)) {
                const int32_t k = grid.in_bounds(e) && now_field.valid() ? now_field.cost[static_cast<size_t>(e.y) * grid.width() + static_cast<size_t>(e.x)] : -1;
                if (k >= 0 && (walk < 0 || k < walk)) walk = k;
            }
            const bool walks = walk >= 0 && static_cast<uint32_t>(MapInfo::walking_ticks(walk)) <= plan.endraid_walk;
            std::vector<size_t> first = chain(pool, comps.size(), from, hill_set, limit);
            bool hop_only = false;
            if (from == hill_set || !first.empty()) {
                int32_t total = 0;
                for (const size_t i : first) total += pool[i].missing;
                if (walks) {
                    if (first.empty()) {
                        raid_ready = true;
                        continue;
                    }
                    best_total = total;                                                           // (the Thief only has to get home: the walk to the hill is there already)
                    best_path = first;
                } else {
                    // the one channel between the hill's island and the target's, the fewest tiles to dig (a channel whose two islands stand joined by a long way round counts: the short way is the one that is dug)
                    size_t hop = pool.size();
                    for (size_t k = 0; k < pool.size(); ++k) {
                        const Channel& ch = *pool[k].channel;
                        const bool between = (ch.a == hill_comp && ch.b == team_island[t]) || (ch.b == hill_comp && ch.a == team_island[t]);
                        if (between && (hop == pool.size() || pool[k].missing < pool[hop].missing)) hop = k;
                    }
                    if (hop != pool.size() && total + pool[hop].missing <= limit) {
                        best_total = total + pool[hop].missing;
                        best_path = first;
                        best_path.push_back(hop);
                        hop_only = first.empty();
                    }
                }
            }
            if (best_path.empty() || best_total <= 0) continue;
            // the richest hill among the cheap ones: a tile is worth about 60 points of the victim's box
            BridgeJob job;
            job.ant = thief.id;
            job.kind = BridgeJob::Kind::Raid;
            const int32_t key = best_total * 60 - row.score;
            if (raid_best == nullptr || key < raid_key) {
                raid_key = key;
                raid_pool = pool;
                raid_path = best_path;
                raid_job = job;
                raid_hop_only = hop_only;
                raid_best = &raid_job;
            }
        }
        if (raid_ready) return;                                                                    // (a hill is raidable already: no chain)
        if (raid_best != nullptr) consider(raid_job, raid_path, from, raid_pool, raid_hop_only ? hill_comp : -1);
    };
    for (const AntView* a : waiting) {
        const int32_t from = sets.find(c.map.ant_component(c.seat, a->tile));
        if (raid_on && a->type == sim::AntType::Thief) {                                         // (a Thief waits for its raid chain; the older kinds are for the other ants)
            if (a->holding || a->carried_points > 0) {                                           // ... one that has the loot waits for the way home (the engine walks it, when a walk leads there)
                if (from != hill_set && !c.map.reaches_hill(now_field, a->tile, a->type) && hill_set >= 0) {
                    std::vector<Edge> pool;                                                       // (the islands of enemy hills are left out, but the one that it stands on and the own)
                    for (const Edge& e : all_edges) {
                        bool shut = false;
                        for (uint8_t u = 0; u < sim::MAX_PLAYERS; ++u) {
                            if (team_island[u] < 0) continue;
                            const int32_t island = sets.find(team_island[u]);
                            if (island == hill_set || island == from) continue;
                            shut = shut || e.a == island || e.b == island;
                        }
                        if (!shut) pool.push_back(e);
                    }
                    const std::vector<size_t> path = chain(pool, comps.size(), from, hill_set, static_cast<int32_t>(plan.endraid_tiles));
                    if (!path.empty()) {
                        BridgeJob job;
                        job.ant = a->id;
                        job.kind = BridgeJob::Kind::Return;
                        consider(job, path, from, pool);
                    }
                }
                continue;
            }
            if (a->hp >= 4) {
                consider_raid(*a, from);
                continue;
            }
        }
        if (foreign_set[static_cast<size_t>(from)]) continue;                                    // (it stands on an island that is joined to another team's hill: no bridge for it)
        const bool home = c.map.reaches_hill(now_field, a->tile, a->type);
        // a Fire Ant that took a hat where no walk leads home: the chain to the hill's island (plan.fire_hats), longer than the other kinds' because the hats lie on the ring
        if (home_on && a->type == sim::AntType::Fire && !home && from != hill_set) {
            const std::vector<size_t> path = chain(edges, comps.size(), from, hill_set, static_cast<int32_t>(plan.fire_hat_tiles));
            if (!path.empty()) {
                BridgeJob job;
                job.ant = a->id;
                job.kind = BridgeJob::Kind::Home;
                consider(job, path, from, edges);
                if (best != nullptr && best->kind == BridgeJob::Kind::Home && best->ant == a->id) home_ants_.insert(a->id);
            }
            continue;
        }
        // the way to the hill's island: for a hurt ant (Return) or one whose island has food that the hill cannot walk to (Feeder)
        if (!home && from != hill_set && (return_on || feeder_on)) {
            const std::vector<size_t> path = chain(edges, comps.size(), from, hill_set, static_cast<int32_t>(chain_tiles));
            if (!path.empty()) {
                BridgeJob job;
                job.ant = a->id;
                bool wanted = false;
                if (return_on && (a->hp <= params_.return_hp || more)) {
                    job.kind = BridgeJob::Kind::Return;
                    wanted = true;
                } else if (feeder_on) {
                    std::set<int32_t> reached{from};                                             // the islands of the walk: the ant's own and the ones between (the hill's is the end of it)
                    for (size_t k = 0; k + 1 < path.size(); ++k) {
                        const Edge& e = edges[path[k]];
                        reached.insert(e.a);
                        reached.insert(e.b);
                    }
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
                if (wanted) consider(job, path, from, edges);
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
                    if (to == from || foreign_set[static_cast<size_t>(to)]) continue;
                    const std::vector<size_t> path = chain(edges, comps.size(), from, to, static_cast<int32_t>(chain_tiles));
                    if (path.empty()) continue;
                    BridgeJob job;
                    job.kind = BridgeJob::Kind::Power;
                    job.ant = a->id;
                    consider(job, path, from, edges);
                }
            }
        }
    }
    if (best != nullptr && (best->kind == BridgeJob::Kind::Return || best->kind == BridgeJob::Kind::Home)) returning_.insert(best->ant);
    if (best == nullptr) return nullptr;
    job_ = candidate;
    return &job_;
}

const MapInfo::NowField& BridgePlanner::walk_now(TaskContext& c) {
    const uint64_t now = c.view.tick();
    if (!walk_made_ || (walk_ttl_ == 0 ? now > walk_tick_ : now >= walk_tick_ + walk_ttl_)) {
        walk_ = c.map.field_now(c.view.grid(), c.seat, c.view.walk_context(), c.view.walkable_mask(c.seat));
        walk_tick_ = now;
        walk_made_ = true;
    }
    return walk_;
}

void BridgePlanner::walk_home(TaskContext& c) {
    if (returning_.empty()) return;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    std::vector<uint32_t> go;
    std::vector<uint32_t> go_whole;
    for (auto it = returning_.begin(); it != returning_.end();) {
        const uint32_t id = *it;
        const AntView* a = find_ant(v.mine(), id);
        const TaskId owner = c.ledger.owner(id);
        if (a == nullptr || (owner != kNoTask && owner != owner_)) {                               // gone, or another task has it
            home_.erase(id);
            it = returning_.erase(it);
            continue;
        }
        const bool fire_home = more_ || home_ants_.count(id) != 0;                              // (a Fire Ant that took a hat far from the hill, or any ant that the food-gone bridges brought, is the planner's until it is home: its hit points are no reason to let it go)
        if ((!fire_home && a->hp > params_.return_hp) || !hill.present || (fire_home && a->tile.chebyshev_dist(hill.entrance) <= 3)) {                  // healed (or no hill to go to, or home): the economy has it again
            if (owner == owner_) c.ledger.release(id, owner_);
            home_.erase(id);
            home_ants_.erase(id);
            it = returning_.erase(it);
            continue;
        }
        Home& h = home_[id];
        ++it;
        if (!a->takes_orders() || a->holding || a->carried_points > 0 || a->tile.chebyshev_dist(hill.entrance) <= 1) continue;
        const MapInfo::NowField& field = walk_now(c);
        if (!c.map.reaches_hill(field, a->tile, a->type)) continue;                                // (the way does not stand yet)
        // a way that stands now is no way home if a tile of a bridge of the planner's goes before the ant has walked it: the ant would be on the bridge when it falls. The tile with the least life left
        // counts, wherever it is (the planner does not know which tiles the walk takes), and the walk gets the margin of the evacuation's gap
        const int32_t at = a->tile.y * static_cast<int32_t>(v.grid().width()) + a->tile.x;
        if (!field.cost.empty() && at >= 0 && static_cast<size_t>(at) < field.cost.size() && field.cost[static_cast<size_t>(at)] >= 0) {
            int64_t least = std::numeric_limits<int64_t>::max();
            for (const auto& e : since_) {
                if (e.second != kFromTheFile) least = std::min<int64_t>(least, static_cast<int64_t>(e.second) + params_.life_ticks - static_cast<int64_t>(now));
            }
            if (least != std::numeric_limits<int64_t>::max() && least < static_cast<int64_t>(MapInfo::walking_ticks(field.cost[static_cast<size_t>(at)])) + params_.evacuate_gap) continue;
        }
        if (h.orders >= params_.home_orders || (h.orders > 0 && (now < h.last + params_.home_gap || !a->idle()))) continue;
        if (!c.ledger.claim(id, owner_)) continue;
        if (h.orders == 0) ++homeward_;
        h.last = now;
        ++h.orders;
        (a->hp <= params_.return_hp ? go : go_whole).push_back(id);
    }
    if (!go.empty()) {
        c.orders.move(go, hill.entrance, Priority::Normal);                                        // (an order onto the own hill is a walk home: the door heals)
    }
    if (!go_whole.empty()) {
        c.orders.move(go_whole, hill.tile42, Priority::Normal);                                    // (a whole ant has nothing to heal: it goes to the tile where the ants of the hill wait)
    }
}

}  // namespace ants::ai
