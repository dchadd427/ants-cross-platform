#include "ants_ai/island_tasks.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <queue>

#include "ants_ai/map_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr int32_t kNoLife = 1 << 30;                       // no bridge tile (on the way, under the ant)
constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr uint32_t kNoPile = ~uint32_t{0};                // (an ant of the guard that works no pile of the economy)
constexpr int32_t kNearHill = 300;                         // a walking cost (15 steps over grass): an ant this close to the hill's queue row is home

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

// The ant of any team that stands on the tile, the seat's own included, or null: no special order names such a tile (BotController::allowed: a click on an ant selects it or attacks it)
const AntView* ant_on(const BotView& v, sim::TileCoord t) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == t) return &a;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == t) return &a;
    }
    return nullptr;
}

int32_t index_of(const sim::Grid& grid, sim::TileCoord t) noexcept { return t.y * static_cast<int32_t>(grid.width()) + t.x; }
sim::TileCoord tile_at(const sim::Grid& grid, int32_t idx) noexcept {
    const int32_t w = static_cast<int32_t>(grid.width());
    return sim::TileCoord{idx % w, idx / w};
}

// The remaining life of a finished bridge tile as far as the bot knows it: `life_ticks` from the look at which it first saw the tile finished (a bot that watches continuously knows no
// more and no less: the screen draws every bridge alike; the caller takes one look interval off `life_ticks` for the time between the finish and the look); kNoLife for a tile that is no
// finished bridge
int32_t life_of(const sim::Grid& grid, const std::map<int32_t, uint64_t>& seen, int32_t idx, uint64_t now, uint32_t life_ticks) {
    if (!grid.cells()[static_cast<size_t>(idx)].has_completed_bridge()) return kNoLife;
    const auto it = seen.find(idx);
    const int64_t first = static_cast<int64_t>(it != seen.end() ? it->second : now);
    const int64_t left = first + static_cast<int64_t>(life_ticks) - static_cast<int64_t>(now);
    return left < 0 ? 0 : static_cast<int32_t>(left);
}

// The walkable tiles of the seat as the engine's rules have them now, without the bridge pieces that are not finished (the stages of a dig: a failed dig takes them away again)
std::vector<uint8_t> walk_mask_now(const BotView& view, uint8_t seat) {
    const sim::Grid& grid = view.grid();
    std::vector<uint8_t> mask = MapInfo::walkable_mask(grid, seat, view.walk_context());
    const std::vector<sim::TileCell>& cells = grid.cells();
    for (size_t i = 0; i < cells.size() && i < mask.size(); ++i) {
        if (cells[i].has_partial_bridge()) mask[i] = 0;
    }
    return mask;
}

// The step of the engine's path finder with the tiles that are to be dug counted as mud (a bridge piece is mud): the weights added, times 1.4 truncated on a diagonal, halved
uint32_t step_of(const sim::Grid& grid, const std::vector<uint8_t>& walk, int32_t from, int32_t to, bool diagonal) {
    const auto weight = [&](int32_t idx) {
        const uint8_t cls = walk[static_cast<size_t>(idx)] != 0 ? grid.terrain_class_at(tile_at(grid, idx)) : static_cast<uint8_t>(sim::movement::kTerrainMud);
        return sim::movement::terrain_step_weight(cls, false);
    };
    uint32_t s = weight(from) + weight(to);
    if (diagonal) s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
    return s >> 1;
}

struct Search {
    std::vector<int32_t> cost;           // the walking cost from the hill, -1: not reached
    std::vector<int32_t> parent;         // the tile before (an index), -1 at a source
    std::vector<int32_t> z;              // the least (remaining life + ticks of walking from the hill) of a bridge tile on the way (kNoLife: none; a tile to be dug counts the full life)
    std::vector<int32_t> m;              // the least remaining life of a bridge tile on the way (kNoLife: none)
    std::vector<uint8_t> fresh;          // tiles to be dug on the way
};

// A walking-cost search from the hill's tiles over the walkable tiles `walk` and, when `dig` is given, over the water tiles that a swimmer may dig (`dig` = 1): each counts as mud and adds
// `tile_cost`, and a way has at most `max_new` of them. `z` is what the guard needs: an ant that stands at P and walks home passes the bridge tile k at (its ticks from the hill) minus
// (k's), so it gets home before k goes when its ticks from the hill, the latency of the bot's order and a margin are less than the least (life + ticks from the hill) on the way
Search search(const sim::Grid& grid, const std::vector<uint8_t>& walk, const std::vector<uint8_t>* dig, const std::vector<sim::TileCoord>& sources, const std::map<int32_t, uint64_t>& seen, uint64_t now,
              uint32_t tile_cost, uint32_t max_new, uint32_t life_ticks) {
    const int32_t w = static_cast<int32_t>(grid.width());
    const int32_t h = static_cast<int32_t>(grid.height());
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    Search s;
    s.cost.assign(n, -1);
    s.parent.assign(n, -1);
    s.z.assign(n, kNoLife);
    s.m.assign(n, kNoLife);
    s.fresh.assign(n, 0);
    using Item = std::pair<int32_t, int32_t>;                                                   // (cost, index): the cost is final when popped
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (const sim::TileCoord& src : sources) {
        const int32_t idx = index_of(grid, src);
        if (!grid.in_bounds(src) || walk[static_cast<size_t>(idx)] == 0 || s.cost[static_cast<size_t>(idx)] == 0) continue;
        s.cost[static_cast<size_t>(idx)] = 0;
        open.push({0, idx});
    }
    while (!open.empty()) {
        const auto [cost, idx] = open.top();
        open.pop();
        if (cost != s.cost[static_cast<size_t>(idx)]) continue;
        const sim::TileCoord cur = tile_at(grid, idx);
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
            if (!grid.in_bounds(t)) continue;
            const int32_t ti = index_of(grid, t);
            const bool diagonal = kDx[k] != 0 && kDy[k] != 0;
            bool new_tile = false;
            if (walk[static_cast<size_t>(ti)] == 0) {
                if (dig == nullptr || (*dig)[static_cast<size_t>(ti)] == 0 || s.fresh[static_cast<size_t>(idx)] >= max_new) continue;
                new_tile = true;
            }
            const int32_t nc = cost + static_cast<int32_t>(step_of(grid, walk, idx, ti, diagonal)) + (new_tile ? static_cast<int32_t>(tile_cost) : 0);
            const size_t tu = static_cast<size_t>(ti);
            if (s.cost[tu] >= 0 && nc >= s.cost[tu]) continue;
            s.cost[tu] = nc;
            s.parent[tu] = idx;
            s.fresh[tu] = static_cast<uint8_t>(s.fresh[static_cast<size_t>(idx)] + (new_tile ? 1 : 0));
            const int32_t here = new_tile ? static_cast<int32_t>(life_ticks) : life_of(grid, seen, ti, now, life_ticks);
            s.z[tu] = here == kNoLife ? s.z[static_cast<size_t>(idx)] : std::min(s.z[static_cast<size_t>(idx)], here + MapInfo::walking_ticks(nc));
            s.m[tu] = std::min(s.m[static_cast<size_t>(idx)], here);
            open.push({nc, ti});
        }
    }
    return s;
}

// The tile next to `click` (a cell of a pile) from which the search reaches it as cheaply as `approach_cost`, -1 when there is none
int32_t approach_tile(const sim::Grid& grid, const std::vector<int32_t>& cost, sim::TileCoord click, int32_t approach_cost) {
    for (int k = 0; k < 8; ++k) {
        const sim::TileCoord t{click.x + kDx[k], click.y + kDy[k]};
        if (!grid.in_bounds(t)) continue;
        const int32_t ti = index_of(grid, t);
        if (cost[static_cast<size_t>(ti)] < 0) continue;
        if (cost[static_cast<size_t>(ti)] + static_cast<int32_t>(MapInfo::step_cost(grid, t, click)) == approach_cost) return ti;
    }
    return -1;
}

// A way off a bridge: the first land tile, when the ant gets there (ticks from now), and how much time it has to spare at the tightest bridge tile on the way
struct Way {
    bool found{false};
    sim::TileCoord exit{-1, -1};
    int32_t arrive{0};
    int32_t slack{0};
};

// The ways off the bridge for an ant that stands on the finished bridge tile `from`: a search over the finished bridge tiles (the engine's step times, the ant's walk starts after `lead`
// ticks) that ends at the first tile that is no bridge. With `respect`, a bridge tile may only be left (the ant is on the next tile) `margin` ticks before it goes; without it, only the
// walking time counts (the ant is lost if it is slower than the bridge, and the nearest way is the best bet). `homeward` says which of the ways leads towards the hill (the tile's
// walking cost from the hill is less than that of `from`): the best of each kind is returned.
void ways_off(const sim::Grid& grid, const std::function<bool(sim::TileCoord)>& walkable, const std::function<int32_t(int32_t)>& life, const std::function<bool(sim::TileCoord)>& homeward,
              sim::TileCoord from, int32_t lead, int32_t margin, bool respect, Way& home, Way& far) {
    using Item = std::pair<int32_t, int32_t>;                                                   // (arrival, index)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    std::map<int32_t, int32_t> arrive;
    std::map<int32_t, int32_t> slack;
    const int32_t start = index_of(grid, from);
    arrive[start] = lead;
    slack[start] = kNoLife;
    open.push({lead, start});
    while (!open.empty()) {
        const auto [at, idx] = open.top();
        open.pop();
        if (at != arrive[idx]) continue;
        const sim::TileCoord cur = tile_at(grid, idx);
        const int32_t here = life(idx);
        if (idx != start && here == kNoLife) {                                                  // land: the way ends here
            Way& slot = homeward(cur) ? home : far;
            if (!slot.found || at < slot.arrive) {
                slot.found = true;
                slot.exit = cur;
                slot.arrive = at;
                slot.slack = slack[idx];
            }
            continue;
        }
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
            if (!grid.in_bounds(t) || !walkable(t)) continue;
            const int32_t ti = index_of(grid, t);
            const int32_t step = MapInfo::walking_ticks(static_cast<int32_t>(MapInfo::step_cost(grid, cur, t)));
            const int32_t next = at + std::max(step, 1);
            if (respect && next + margin > here) continue;                                       // the tile under the ant would go before it stood on the next one
            const auto old = arrive.find(ti);
            if (old != arrive.end() && old->second <= next) continue;
            arrive[ti] = next;
            slack[ti] = std::min(slack[idx], here - next);
            open.push({next, ti});
        }
    }
}

}  // namespace

// ---- the first look -------------------------------------------------------------------------------------------------------------------------------------

bool IslandTask::beyond_food(const BotView& view, const MapInfo& map, uint8_t seat) const {
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map.pile(p.index);
        if (info != nullptr && !info->approach[seat].reachable()) return true;
    }
    return false;
}

bool IslandTask::start(TaskContext& c) {
    if (started_) return active_;
    const BotView& v = c.view;
    if (!c.map.hill(c.seat).present || c.map.width() <= 0) return false;
    started_ = true;
    width_ = c.map.width();
    interval_ = c.profile.decision_interval;
    latency_ = c.profile.decision_interval + c.profile.reaction_delay * (100u + c.profile.jitter_percent) / 100u + 10u;
    if (!tactics_.plan.islands || c.map.component_count(c.seat) <= 1) return false;                // one component: nothing is cut off (TINY, MEDIUM, GAUNTLET)
    info_ = IslandInfo::analyse(c.map, v.grid(), c.seat);
    hill_comp_ = c.map.hill_component(c.seat);
    if (info_.empty() || hill_comp_ < 0) return false;
    // active when a pile lies on an island that a chain of water tiles joins to the hill's island, directly or over other islands (TREASURE has islands, walled in by rocks: no channel)
    std::vector<uint8_t> seen(info_.components().size(), 0);
    std::vector<int32_t> queue{hill_comp_};
    seen[static_cast<size_t>(hill_comp_)] = 1;
    for (size_t head = 0; head < queue.size(); ++head) {
        for (const Channel& ch : info_.channels()) {
            const int32_t from = queue[head];
            const int32_t to = ch.a == from ? ch.b : ch.b == from ? ch.a : -1;
            if (to < 0 || seen[static_cast<size_t>(to)] != 0) continue;
            seen[static_cast<size_t>(to)] = 1;
            queue.push_back(to);
        }
    }
    for (const IslandComponent& comp : info_.components()) {
        if (comp.id != hill_comp_ && seen[static_cast<size_t>(comp.id)] != 0 && !comp.piles.empty()) active_ = true;
    }
    return active_;
}

// ---- the bridges as they are now ------------------------------------------------------------------------------------------------------------------------

bool IslandTask::scan_bridges(const BotView& view, uint64_t now) {
    const sim::Grid& grid = view.grid();
    const std::vector<sim::TileCell>& cells = grid.cells();
    bool fresh = false;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (!cells[i].has_completed_bridge()) continue;
        fresh = seen_.emplace(static_cast<int32_t>(i), now).second || fresh;                       // (the first look that sees it finished)
    }
    for (auto it = seen_.begin(); it != seen_.end();) {
        if (!cells[static_cast<size_t>(it->first)].has_completed_bridge()) {                        // collapsed, or demolished: the paths that the ants walk may lead over it
            it = seen_.erase(it);
            collapsed_ = true;
        } else {
            ++it;
        }
    }
    return fresh;
}

int64_t IslandTask::tile_life(sim::TileCoord tile, uint64_t now) const noexcept {
    if (width_ <= 0) return -1;
    const auto it = seen_.find(tile.y * width_ + tile.x);
    if (it == seen_.end()) return -1;
    const int64_t left = static_cast<int64_t>(it->second) + static_cast<int64_t>(params_.life_ticks) - static_cast<int64_t>(interval_) - static_cast<int64_t>(now);
    return left < 0 ? 0 : left;
}

std::vector<sim::TileCoord> IslandTask::chain() const { return builders_.empty() ? std::vector<sim::TileCoord>{} : builders_.begin()->second.chain; }

// ---- the builders -----------------------------------------------------------------------------------------------------------------------------------------

void IslandTask::adopt_builders(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    for (auto it = builders_.begin(); it != builders_.end();) {                                    // a swimmer that died (or that a task of a higher rank took) is no builder
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || c.ledger.owner(it->first) != id()) {
            c.ledger.release(it->first, id());
            it = builders_.erase(it);
        } else {
            ++it;
        }
    }
    // a Swimmer digs while somebody needs a bridge (a worker stands idle with nowhere to go, or a bridge is being dug); with nobody to walk it the Swimmer is the ferry's (FerryTask)
    bool work = !plan_.tiles.empty();
    for (const auto& e : builders_) work = work || !e.second.chain.empty();
    for (auto it = builders_.begin(); it != builders_.end();) {
        Builder& b = it->second;
        if (!b.chain.empty() || !plan_.tiles.empty() || tactics_.surplus > 0) {
            b.idle_since = 0;
        } else if (b.idle_since == 0) {
            b.idle_since = now;
        } else if (now >= b.idle_since + params_.builder_idle) {
            c.ledger.release(it->first, id());
            it = builders_.erase(it);
            continue;
        }
        ++it;
    }
    if (!work && tactics_.surplus == 0) return;
    // a bridge is for the workers that walk over it: with none at the hill's island (the expedition took them) the Swimmer carries food instead
    uint32_t workers = 0;
    for (const AntView& a : v.mine()) {
        if (a.type != v.default_ant_type() || c.map.ant_component(c.seat, a.tile) != hill_comp_) continue;
        const TaskId owner = c.ledger.owner(a.id);
        workers += owner == kNoTask || c.ledger.rank(owner) < c.ledger.rank(id()) ? 1u : 0u;
    }
    if (workers < params_.min_workers) return;
    for (const AntView& a : v.mine()) {
        if (builders_.size() >= params_.builders) break;
        if (a.type != sim::AntType::Swimmer || builders_.count(a.id) != 0 || a.holding || a.carried_points > 0) continue;
        if (!c.ledger.take(a.id, id())) continue;                                                  // (from the ferry, which has the lower rank)
        builders_[a.id] = Builder{};
    }
}

void IslandTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupSpecial || command.ants.size() != 1) return;
    const auto it = builders_.find(command.ants[0]);
    if (it == builders_.end()) return;
    Builder& b = it->second;
    if (!b.ordered || b.sent != kPending || b.next >= b.chain.size() || command.tile_x != b.chain[b.next].x || command.tile_y != b.chain[b.next].y) return;
    if (fate == Bot::Fate::Sent) b.sent = tick;                                                    // the clock of the dig starts when the order LEFT
    else b.ordered = false;                                                                        // it never left: ordered again at the next look
}

void IslandTask::step_builder(TaskContext& c, uint32_t ant, Builder& b) {
    const BotView& v = c.view;
    const AntView* a = find_ant(v.mine(), ant);
    if (a == nullptr) return;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    if (a->state == sim::UnitState::BuildingBridge) return;                                        // a clip is running: wait for it
    // the tiles that are finished already
    while (b.next < b.chain.size() && grid.get_cell(b.chain[b.next]).has_completed_bridge()) {
        ++b.next;
        ++tiles_finished_;
        b.ordered = false;
    }
    // the plan, when there is no chain
    if (b.chain.empty()) {
        if (plan_.tiles.empty()) return;
        b.chain = plan_.tiles;
        b.next = 0;
        b.ordered = false;
        b.parked = false;
        if (plan_.renewal) ++renewals_;
        plan_ = Plan{};
    }
    if (b.next >= b.chain.size()) {                                                                // the chain is done: the swimmer steps off the bridge, into the water next to the last tile
        ++chains_finished_;
        reask_ = true;
        const sim::TileCoord last = b.chain.back();
        b.chain.clear();
        b.next = 0;
        b.ordered = false;
        b.park = sim::TileCoord{-1, -1};
        for (int r = 1; r <= 3 && b.park.x < 0; ++r) {
            for (int dy = -r; dy <= r && b.park.x < 0; ++dy) {
                for (int dx = -r; dx <= r && b.park.x < 0; ++dx) {
                    const sim::TileCoord t{last.x + dx, last.y + dy};
                    if (!grid.in_bounds(t) || grid.terrain_class_at(t) != sim::movement::kTerrainWater || !grid.get_cell(t).is_empty_overlay()) continue;
                    b.park = t;
                }
            }
        }
        if (b.park.x >= 0 && a->takes_orders()) c.orders.move({ant}, b.park);
        return;
    }
    const sim::TileCoord t = b.chain[b.next];
    if (!bridge_water(grid, t)) {                                                                  // something else is on the tile (a stage of a dig that is not finished, an ant's bomb, ...)
        if (grid.get_cell(t).has_partial_bridge() && b.ordered && now < b.sent + 40u) return;      // (our own dig, between two clips)
        bad_[index_of(grid, t)] = now + params_.blacklist_ticks;
        b.chain.clear();
        b.next = 0;
        b.ordered = false;
        return;
    }
    if (!a->takes_orders()) return;
    if (b.ordered) {
        if (b.sent == kPending) {                                                                  // the order has not left yet (the reaction time, the budget): a look that comes much later finds it lost
            if (now >= b.decided + 200u) b.ordered = false;
            return;
        }
        if (a->state == sim::UnitState::CantGo && now >= b.sent + 8u) {                           // refused
            ++failures_;
            bad_[index_of(grid, t)] = now + params_.blacklist_ticks;
            b.chain.clear();
            b.next = 0;
            b.ordered = false;
            return;
        }
        if (!a->idle() || v.has_pending_path(ant) || now < b.sent + 16u) return;                   // on its way, or digging
        // idle again, and the tile is not finished: the order did not do what it meant
        ++failures_;
        bad_[index_of(grid, t)] = now + params_.blacklist_ticks;
        b.chain.clear();
        b.next = 0;
        b.ordered = false;
        return;
    }
    if (const AntView* on = ant_on(v, t)) {                                                        // an ant stands on the tile: no click names it, the swimmer's own rest there included
        if (b.blocked_since == 0) b.blocked_since = now;
        if (on->id == ant && a->idle() && !v.has_pending_path(ant) && now >= b.aside_at + 40u) {   // (it steps aside, to a free water tile that is not one of the tiles to dig, and digs from there)
            for (int r = 1; r <= 3 && b.aside_at != now; ++r) {
                for (int dy = -r; dy <= r && b.aside_at != now; ++dy) {
                    for (int dx = -r; dx <= r && b.aside_at != now; ++dx) {
                        const sim::TileCoord s{t.x + dx, t.y + dy};
                        if (!grid.in_bounds(s) || grid.terrain_class_at(s) != sim::movement::kTerrainWater || !grid.get_cell(s).is_empty_overlay() || ant_on(v, s) != nullptr) continue;
                        if (std::find(b.chain.begin() + static_cast<std::ptrdiff_t>(b.next), b.chain.end(), s) != b.chain.end()) continue;
                        c.orders.move({ant}, s);
                        b.aside_at = now;
                        ++asides_;
                    }
                }
            }
        }
        if (now >= b.blocked_since + params_.blocked_ticks) {                                      // it stays: the tile is left alone for a while and another chain is looked for
            ++blocked_tiles_;
            bad_[index_of(grid, t)] = now + params_.blacklist_ticks;
            b.chain.clear();
            b.next = 0;
            b.ordered = false;
            b.blocked_since = 0;
        }
        return;
    }
    b.blocked_since = 0;
    c.orders.special(ant, t);
    b.ordered = true;
    b.decided = now;
    b.sent = kPending;
    ++tiles_ordered_;
}

// ---- the map as it is now: the closing and the plan -----------------------------------------------------------------------------------------------------------

void IslandTask::refresh(TaskContext& c) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    const uint32_t life_eff = params_.life_ticks > interval_ ? params_.life_ticks - interval_ : 0u;
    bool builder_free = false;
    for (const auto& e : builders_) builder_free = builder_free || e.second.chain.empty();
    const bool might_plan = builder_free && plan_.tiles.empty() && v.ticks_left() > params_.min_ticks_left && beyond_food(v, c.map, c.seat);
    const bool have_bridges = !seen_.empty();
    if (!have_bridges) {
        closed_.clear();
        limits_.clear();
        hot_entries_.clear();
        tactics_.shut_tiles.clear();
        field_.valid = false;
    }
    if (!might_plan && !have_bridges) return;
    const std::vector<uint8_t> walk = walk_mask_now(v, c.seat);
    std::vector<sim::TileCoord> starts;                                                            // where the hill's ants leave it: the walkable tiles of its queue row
    for (int dx = 0; dx <= 2; ++dx) {
        const sim::TileCoord t{hill.origin.x + dx, hill.origin.y - 1};
        if (grid.in_bounds(t) && walk[static_cast<size_t>(index_of(grid, t))] != 0) starts.push_back(t);
    }
    if (starts.empty()) return;
    const size_t n = walk.size();
    const std::vector<sim::TileCell>& cells = grid.cells();
    const int32_t retire_below = static_cast<int32_t>(params_.retire_life + latency_);
    const int32_t renew_below = retire_below + static_cast<int32_t>(params_.renew_life);
    const auto life_at = [&](int32_t idx) { return life_of(grid, seen_, idx, now, life_eff); };

    // ---- the bridges: finished tiles that touch each other are one bridge, and its life is that of its oldest tile ---
    std::vector<int32_t> bridge_comp(n, -1);
    std::vector<std::vector<int32_t>> bridge_tiles;
    std::vector<int32_t> bridge_life;
    for (const auto& e : seen_) {
        if (bridge_comp[static_cast<size_t>(e.first)] >= 0) continue;
        const int32_t id = static_cast<int32_t>(bridge_tiles.size());
        bridge_tiles.emplace_back();
        bridge_life.push_back(kNoLife);
        std::vector<int32_t> stack{e.first};
        bridge_comp[static_cast<size_t>(e.first)] = id;
        while (!stack.empty()) {
            const int32_t idx = stack.back();
            stack.pop_back();
            bridge_tiles[static_cast<size_t>(id)].push_back(idx);
            bridge_life[static_cast<size_t>(id)] = std::min(bridge_life[static_cast<size_t>(id)], life_at(idx));
            const sim::TileCoord cur = tile_at(grid, idx);
            for (int k = 0; k < 8; ++k) {
                const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
                if (!grid.in_bounds(t)) continue;
                const int32_t ti = index_of(grid, t);
                if (bridge_comp[static_cast<size_t>(ti)] >= 0 || !cells[static_cast<size_t>(ti)].has_completed_bridge()) continue;
                bridge_comp[static_cast<size_t>(ti)] = id;
                stack.push_back(ti);
            }
        }
    }
    const auto retired = [&](int32_t idx) {
        const int32_t id = bridge_comp[static_cast<size_t>(idx)];
        return id >= 0 && bridge_life[static_cast<size_t>(id)] < retire_below;
    };
    tactics_.shut_tiles.assign(n, 0);                                                              // no other task plans a walk over a retired bridge
    for (const auto& e : seen_) tactics_.shut_tiles[static_cast<size_t>(e.first)] = retired(e.first) ? 1 : 0;

    // ---- the walk from the hill over what stands now: the field that the guard reads ---
    Search now_search = search(grid, walk, nullptr, starts, seen_, now, params_.tile_cost, params_.max_new_tiles, life_eff);
    field_.cost = now_search.cost;
    field_.z = now_search.z;
    field_.walk = walk;
    field_.tick = now;
    field_.valid = true;
    MapInfo::NowField field;
    field.team = c.seat;
    field.ctx = v.walk_context();
    field.cost = now_search.cost;
    // The engine takes the cheapest way, and of two ways that cost about the same the bot cannot know which (the start of the walk is the ant's place, not the hill's gate, and a standing
    // ant on a bridge sends the ants behind it another way). So the ways over the RETIRED bridges are searched apart, and a pile is closed when such a way is about as cheap as the best one.
    std::vector<uint8_t> walk_old = walk;
    for (const auto& e : seen_) {
        if (!retired(e.first)) walk_old[static_cast<size_t>(e.first)] = 0;
    }
    const Search old_search = search(grid, walk_old, nullptr, starts, seen_, now, params_.tile_cost, params_.max_new_tiles, life_eff);
    field_.old_cost = old_search.cost;
    field_.old_z = old_search.z;
    MapInfo::NowField old_field;
    old_field.team = c.seat;
    old_field.ctx = v.walk_context();
    old_field.cost = old_search.cost;

    // ---- the closing and the traffic: which piles over a bridge are worked now, and by how many ants ---
    std::map<uint32_t, uint32_t> working;                                                          // pile -> ants of the economy that work it
    if (economy_ != nullptr) {
        for (const AntView& a : v.mine()) {
            uint32_t pile = 0;
            if (economy_->assigned_pile(a.id, pile)) ++working[pile];
        }
    }
    struct Open {
        uint32_t pile{0};
        std::vector<int32_t> bridges;
        bool held_before{false};                                                                   // it had the bridge at the last look: it keeps it while it is open
        bool incumbent{false};                                                                     // an ant of the economy works it
        int64_t score{0};
        bool due{false};                                                                           // its bridge will be retired within renew_life
    };
    std::vector<Open> open;
    std::map<uint32_t, int32_t> route_cost;                                                        // the cost of the way that the engine takes to a pile now
    closed_.clear();
    limits_.clear();
    for (const PileView& p : v.piles()) {
        if (c.map.pile(p.index) == nullptr) continue;
        const Approach ap = c.map.approach_now(grid, p.index, field);
        if (!ap.reachable()) continue;
        const int32_t at = approach_tile(grid, field.cost, ap.click, ap.cost);
        if (at < 0 || now_search.z[static_cast<size_t>(at)] == kNoLife) continue;                  // no bridge on the way to it
        route_cost[p.index] = ap.cost;
        const int32_t trip = MapInfo::trip_ticks_for_cost(ap.cost);
        const int32_t z = now_search.z[static_cast<size_t>(at)];
        const int32_t m = now_search.m[static_cast<size_t>(at)];
        const int32_t need = trip + static_cast<int32_t>(latency_ + params_.close_margin);
        bool going = z < need || m < retire_below;
        if (!going) {                                                                              // another way that costs about the same, over a retired bridge
            const Approach ap2 = c.map.approach_now(grid, p.index, old_field);
            going = ap2.reachable() && ap2.cost <= ap.cost + static_cast<int32_t>(params_.tie_cost) && approach_tile(grid, old_field.cost, ap2.click, ap2.cost) >= 0;
        }
        if (going) {
            closed_.insert(p.index);
            continue;
        }
        Open o;
        o.pile = p.index;
        for (int32_t cur = at; cur >= 0; cur = now_search.parent[static_cast<size_t>(cur)]) {
            const int32_t b = bridge_comp[static_cast<size_t>(cur)];
            if (b >= 0 && std::find(o.bridges.begin(), o.bridges.end(), b) == o.bridges.end()) o.bridges.push_back(b);
        }
        o.held_before = holders_.count(p.index) != 0;
        o.incumbent = working.count(p.index) != 0;
        o.score = static_cast<int64_t>(p.value) * std::min<int64_t>(p.remaining, 12) * 1000 / (static_cast<int64_t>(trip) + 200);
        o.due = m < renew_below || z < need + static_cast<int32_t>(params_.renew_life);
        open.push_back(std::move(o));
    }
    std::stable_sort(open.begin(), open.end(), [](const Open& x, const Open& y) {
        if (x.held_before != y.held_before) return x.held_before;
        return x.incumbent != y.incumbent ? x.incumbent : x.score > y.score;
    });
    std::set<int32_t> held;                                                                        // the bridges that an open pile uses
    std::set<uint32_t> token_closed;
    std::set<uint32_t> due;
    for (const Open& o : open) {
        bool busy = false;
        for (const int32_t b : o.bridges) busy = busy || held.count(b) != 0;
        if (busy) {
            closed_.insert(o.pile);
            token_closed.insert(o.pile);
            continue;
        }
        for (const int32_t b : o.bridges) held.insert(b);
        limits_[o.pile] = params_.bridge_ants;
        if (o.due) due.insert(o.pile);
    }
    holders_.clear();
    for (const auto& e : limits_) holders_.insert(e.first);
    for (const PileView& p : v.piles()) {                                                          // a pile that no walker reached at the start gets ants only over a bridge that has a place for them
        const PileInfo* info = c.map.pile(p.index);
        if (info != nullptr && !info->approach[c.seat].reachable() && limits_.count(p.index) == 0) closed_.insert(p.index);
    }

    // ---- the hot bridges: the ants near their ends are watched (see the top) ---
    hot_entries_.clear();
    const int32_t hot_below = static_cast<int32_t>(params_.hot_life + latency_);
    for (size_t id = 0; id < bridge_tiles.size(); ++id) {
        if (bridge_life[id] >= hot_below) continue;
        for (const int32_t idx : bridge_tiles[id]) {
            const sim::TileCoord cur = tile_at(grid, idx);
            bool shore = false;
            for (int k = 0; k < 8 && !shore; ++k) {
                const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
                if (grid.in_bounds(t)) shore = walk[static_cast<size_t>(index_of(grid, t))] != 0 && !cells[static_cast<size_t>(index_of(grid, t))].has_completed_bridge();
            }
            if (shore) hot_entries_.push_back(idx);
        }
    }

    // ---- the plan: the next bridge, over the tiles that will last, beside the bridges that carry ---
    if (!might_plan) return;
    std::vector<uint8_t> walk_plan = walk;
    std::vector<uint8_t> no_dig(n, 0);                                                             // a new bridge does not touch one that carries or goes soon: it would be one bridge, and one ant
    for (const auto& e : seen_) {
        const int32_t id = bridge_comp[static_cast<size_t>(e.first)];
        const bool carries = held.count(id) != 0;
        if (!carries && bridge_life[static_cast<size_t>(id)] >= renew_below) continue;
        walk_plan[static_cast<size_t>(e.first)] = 0;                                              // (a wall for the planner: it is no water, so it is not dug again)
        const sim::TileCoord cur = tile_at(grid, e.first);
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
            if (grid.in_bounds(t)) no_dig[static_cast<size_t>(index_of(grid, t))] = 1;
        }
    }
    std::vector<uint8_t> dig(n, 0);
    std::vector<uint8_t> stands(n, 0);                                                             // a tile with an ant that stands still cannot be dug (the engine's valid_water)
    for (const AntView& a : v.mine()) {
        if (grid.in_bounds(a.tile) && a.state != sim::UnitState::Walking) stands[static_cast<size_t>(index_of(grid, a.tile))] = 1;
    }
    for (const AntView& a : v.others()) {
        if (grid.in_bounds(a.tile) && a.state != sim::UnitState::Walking) stands[static_cast<size_t>(index_of(grid, a.tile))] = 1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (walk_plan[i] != 0 || stands[i] != 0 || no_dig[i] != 0) continue;
        const auto bad = bad_.find(static_cast<int32_t>(i));
        if (bad != bad_.end() && bad->second > now) continue;
        dig[i] = bridge_water(grid, tile_at(grid, static_cast<int32_t>(i))) ? 1 : 0;
    }
    for (auto it = bad_.begin(); it != bad_.end();) {
        if (it->second <= now) it = bad_.erase(it);
        else ++it;
    }
    Search s = search(grid, walk_plan, &dig, starts, seen_, now, params_.tile_cost, params_.max_new_tiles, life_eff);
    MapInfo::NowField plan_field;
    plan_field.team = c.seat;
    plan_field.ctx = v.walk_context();
    plan_field.cost = s.cost;
    Plan best;
    best.score = 0;
    for (const PileView& p : v.piles()) {
        if (c.map.pile(p.index) == nullptr || p.remaining < params_.min_units) continue;
        const bool served = limits_.count(p.index) != 0;
        if (served ? due.count(p.index) == 0 : (tactics_.surplus == 0 || limits_.size() >= params_.max_bridges)) continue;   // a served pile whose bridge will be retired soon, or a pile when somebody is idle and a bridge is wanted
        const Approach ap = c.map.approach_now(grid, p.index, plan_field);
        if (!ap.reachable()) continue;
        const int32_t at = approach_tile(grid, plan_field.cost, ap.click, ap.cost);
        if (at < 0 || s.fresh[static_cast<size_t>(at)] == 0) continue;                             // no tile to dig on the way to it: it has a way that will last
        const int32_t own = ap.cost - static_cast<int32_t>(s.fresh[static_cast<size_t>(at)] * params_.tile_cost);
        const auto now_cost = route_cost.find(p.index);
        if (token_closed.count(p.index) != 0 && now_cost != route_cost.end() && own >= now_cost->second) continue;   // the engine would walk the old way: the new bridge would not be used
        const int32_t trip = MapInfo::trip_ticks_for_cost(ap.cost);
        const int64_t units = std::min<int64_t>(p.remaining, 12);
        const int64_t dig_ticks = static_cast<int64_t>(s.fresh[static_cast<size_t>(at)]) * static_cast<int64_t>(params_.tile_cost) * MapInfo::kTicksPerCostNum / MapInfo::kTicksPerCostDen;
        const int64_t score = static_cast<int64_t>(p.value) * units * 1000 / (static_cast<int64_t>(trip) + dig_ticks + 200);
        if (score <= best.score) continue;
        best.score = score;
        best.pile = p.index;
        best.renewal = served;
        best.tiles.clear();
        for (int32_t cur = at; cur >= 0; cur = s.parent[static_cast<size_t>(cur)]) {
            if (walk_plan[static_cast<size_t>(cur)] == 0) best.tiles.push_back(tile_at(grid, cur));
        }
        std::reverse(best.tiles.begin(), best.tiles.end());
    }
    plan_ = best;
}

// ---- the guard ----------------------------------------------------------------------------------------------------------------------------------------------

// Whether an ant that stands on the tile of the field gets home before the bridge tiles on its way go (the order takes `latency_` to work, the walk takes the ticks from the hill, and
// each tile is left `escape_margin` ticks before it goes)
bool IslandTask::home_ok(int32_t index) const {
    if (!field_.valid || index < 0 || static_cast<size_t>(index) >= field_.cost.size() || field_.cost[static_cast<size_t>(index)] < 0) return false;
    const int32_t need = static_cast<int32_t>(latency_ + params_.escape_margin);
    const int32_t z = field_.z[static_cast<size_t>(index)];
    if (z != kNoLife && MapInfo::walking_ticks(field_.cost[static_cast<size_t>(index)]) + need > z) return false;
    // the engine may walk another way that costs about the same, over the bridges that are going (see refresh): the ant gets home only if that way lasts as well
    const int32_t old_cost = field_.old_cost.empty() ? -1 : field_.old_cost[static_cast<size_t>(index)];
    if (old_cost >= 0 && old_cost <= field_.cost[static_cast<size_t>(index)] + static_cast<int32_t>(params_.tie_cost)) {
        const int32_t zo = field_.old_z[static_cast<size_t>(index)];
        if (zo != kNoLife && MapInfo::walking_ticks(old_cost) + need > zo) return false;
    }
    return true;
}

// Where a recalled ant goes: a carrier to the entrance (it delivers), an empty ant to the tile where empty ants wait (not into the hill: it would only hatch again)
sim::TileCoord IslandTask::home_tile(const TaskContext& c, bool carrying) const {
    const HillInfo& hill = c.map.hill(c.seat);
    if (carrying) return hill.entrance;
    const sim::TileCoord w = hill.tile42;
    if (field_.valid && w.x >= 0 && c.view.grid().in_bounds(w) && field_.walk[static_cast<size_t>(index_of(c.view.grid(), w))] != 0) return w;
    return hill.entrance;
}

void IslandTask::guard(TaskContext& c) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    const std::vector<sim::TileCell>& cells = grid.cells();
    const uint32_t life_eff = params_.life_ticks > interval_ ? params_.life_ticks - interval_ : 0u;

    // the ants that are gone, or that a task of a higher rank took
    for (auto it = guarded_.begin(); it != guarded_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || c.ledger.owner(it->first) != id()) {
            c.ledger.release(it->first, id());
            it = guarded_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = idle_since_.begin(); it != idle_since_.end();) {
        if (find_ant(v.mine(), it->first) == nullptr) it = idle_since_.erase(it);
        else ++it;
    }
    if (seen_.empty()) {                                                                           // no bridge stands: nothing can drown, nobody is held
        for (const auto& e : guarded_) c.ledger.release(e.first, id());
        guarded_.clear();
        idle_since_.clear();
        return;
    }

    // the pile that each ant of the economy works: the engine's harvest loop goes on to the pile of the last order when the economy has forgotten the ant (an ant that stood idle with its food
    // for a while is dropped by the economy, delivers when it is moved, and walks back to its pile by itself)
    for (auto it = last_pile_.begin(); it != last_pile_.end();) {
        if (find_ant(v.mine(), it->first) == nullptr) it = last_pile_.erase(it);
        else ++it;
    }
    for (const AntView& a : v.mine()) {
        uint32_t pile = 0;
        if (economy_ != nullptr && economy_->assigned_pile(a.id, pile)) last_pile_[a.id] = pile;
        else if (a.idle() && !a.holding && a.carried_points <= 0 && c.ledger.owner(a.id) != id()) last_pile_.erase(a.id);   // (idle and empty: the loop is over)
    }
    const bool repath = collapsed_;                                                                // a bridge tile went since the last look: the paths that were found before may lead over it
    collapsed_ = false;
    std::vector<uint32_t> repaths;
    std::map<std::pair<int32_t, int32_t>, std::vector<uint32_t>> moves;                             // target -> ants
    std::vector<uint32_t> stops;
    const auto order_move = [&](uint32_t ant, sim::TileCoord target) { moves[{target.x, target.y}].push_back(ant); };
    std::set<int32_t> standing;                                                                    // tiles with an ant that stands still: the engine's path finder walks round it, or not at all
    for (const AntView& a : v.mine()) {
        if (grid.in_bounds(a.tile) && a.state != sim::UnitState::Walking) standing.insert(index_of(grid, a.tile));
    }
    for (const AntView& a : v.others()) {
        if (grid.in_bounds(a.tile) && a.state != sim::UnitState::Walking) standing.insert(index_of(grid, a.tile));
    }
    const std::function<bool(sim::TileCoord)> walkable_now = [&](sim::TileCoord t) {
        return MapInfo::walkable(grid, c.seat, t, v.walk_context()) && !grid.get_cell(t).has_partial_bridge() && standing.count(index_of(grid, t)) == 0;
    };
    const std::function<int32_t(int32_t)> life_fn = [&](int32_t idx) { return life_of(grid, seen_, idx, now, life_eff); };
    const auto homeward_of = [&](int32_t from_idx) {
        return std::function<bool(sim::TileCoord)>([&, from_idx](sim::TileCoord t) {
            if (!field_.valid) return c.map.component(c.seat, t) == hill_comp_;
            const int32_t here = field_.cost[static_cast<size_t>(index_of(grid, t))];
            const int32_t base = field_.cost[static_cast<size_t>(from_idx)];
            if (here < 0) return false;
            return base < 0 || here < base;
        });
    };
    const int32_t margin = static_cast<int32_t>(params_.escape_margin);
    const int32_t trigger = static_cast<int32_t>(interval_ + params_.trigger_extra);
    // the ends of the hot bridges: an ant that walks towards one within `radius` tiles (it covers a tile in about 8 ticks, and the order takes `latency_` to work, and the ant may have
    // walked for a look interval since the last look) is stopped
    const int32_t radius = std::min<int32_t>(14, 2 + static_cast<int32_t>(latency_ / 8u + interval_ / 16u));
    // (the steps of a walk over land from the nearest end of a hot bridge: the water between two banks is no way, an ant on the other bank is not near it)
    std::vector<int16_t> hot_steps;
    if (!hot_entries_.empty()) {
        hot_steps.assign(cells.size(), -1);
        std::vector<int32_t> queue;
        for (const int32_t e : hot_entries_) {
            if (hot_steps[static_cast<size_t>(e)] >= 0) continue;
            hot_steps[static_cast<size_t>(e)] = 0;
            queue.push_back(e);
        }
        for (size_t head = 0; head < queue.size(); ++head) {
            const int32_t at = queue[head];
            if (hot_steps[static_cast<size_t>(at)] > radius) continue;
            const sim::TileCoord cur = tile_at(grid, at);
            for (int k = 0; k < 8; ++k) {
                const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
                if (!grid.in_bounds(t)) continue;
                const int32_t ti = index_of(grid, t);
                if (hot_steps[static_cast<size_t>(ti)] >= 0 || cells[static_cast<size_t>(ti)].has_completed_bridge() || !MapInfo::walkable(grid, c.seat, t, v.walk_context())) continue;
                hot_steps[static_cast<size_t>(ti)] = static_cast<int16_t>(hot_steps[static_cast<size_t>(at)] + 1);
                queue.push_back(ti);
            }
        }
    }
    const auto hot_dist = [&](sim::TileCoord t) -> int32_t {
        if (hot_steps.empty() || !grid.in_bounds(t)) return 1 << 20;
        const int16_t steps = hot_steps[static_cast<size_t>(index_of(grid, t))];
        return steps < 0 ? (1 << 20) : steps;
    };
    // where a held ant waits: the free land tile that is farthest from the ends of the hot bridges among those it can reach (the first one beyond `radius`, when there is one): an ant that
    // stands at the end of a bridge blocks the way off it for the ants on it. (-1: it stands as far from them as it can)
    const auto park_of = [&](sim::TileCoord from) {
        std::vector<int32_t> queue{index_of(grid, from)};
        std::set<int32_t> reached{queue[0]};
        sim::TileCoord best{-1, -1};
        int32_t best_dist = hot_dist(from) + 1;                                                    // (it must be better than where the ant stands)
        for (size_t head = 0; head < queue.size() && head < 900u; ++head) {
            const sim::TileCoord cur = tile_at(grid, queue[head]);
            const int32_t d = hot_dist(cur);
            if (head > 0 && standing.count(queue[head]) == 0 && d >= best_dist) {
                best = cur;
                best_dist = d;
                if (d > radius + 1) return best;
            }
            for (int k = 0; k < 8; ++k) {
                const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
                if (!grid.in_bounds(t)) continue;
                const int32_t ti = index_of(grid, t);
                if (cells[static_cast<size_t>(ti)].has_completed_bridge() || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || !reached.insert(ti).second) continue;
                queue.push_back(ti);
            }
        }
        return best;
    };
    // a pile that the economy may not send ants to now: closed, or a pile over water that holds no bridge place (also a pile that is eaten up: the engine's loop still walks back to it)
    const auto shut = [&](uint32_t pile) {
        if (closed_.count(pile) != 0) return true;
        const PileInfo* info = c.map.pile(pile);
        return info != nullptr && !info->approach[c.seat].reachable() && limits_.count(pile) == 0;
    };
    // the ants beyond what a pile over a bridge may have (the engine's loop goes on for the ants that were sent before the limit): the empty ones with the highest ids go home
    std::set<uint32_t> excess;
    if (economy_ != nullptr && !limits_.empty()) {
        std::map<uint32_t, std::vector<uint32_t>> crew;
        for (const AntView& a : v.mine()) {
            uint32_t pile = 0;
            if (a.type != sim::AntType::Swimmer && economy_->assigned_pile(a.id, pile) && limits_.count(pile) != 0) crew[pile].push_back(a.id);
        }
        for (const auto& e : crew) {
            const size_t limit = limits_.at(e.first);
            if (e.second.size() <= limit) continue;
            size_t drop = e.second.size() - limit;
            for (auto it = e.second.rbegin(); it != e.second.rend() && drop > 0; ++it) {
                const AntView* a = find_ant(v.mine(), *it);
                if (a == nullptr || a->holding || a->carried_points > 0) continue;
                excess.insert(*it);
                --drop;
            }
        }
    }

    for (const AntView& a : v.mine()) {
        if (a.type == sim::AntType::Swimmer || !grid.in_bounds(a.tile)) continue;                  // a swimmer swims
        auto g = guarded_.find(a.id);
        const bool ours = g != guarded_.end();
        if (!ours) {                                                                               // an ant of a task that outranks the guard is that task's
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        }
        const int32_t idx = index_of(grid, a.tile);
        const bool on_bridge = cells[static_cast<size_t>(idx)].has_completed_bridge();
        const bool carrying = a.holding || a.carried_points > 0;
        const bool still = a.idle() || a.state == sim::UnitState::CantGo;                          // it does not move
        uint32_t pile = kNoPile;
        const auto lp = last_pile_.find(a.id);
        const bool works = lp != last_pile_.end();
        if (works) pile = lp->second;

        if (on_bridge) {
            // 1. an ant on a bridge tile: sent off by the way with the most time to spare when that is little, or when it stands there (a standing ant blocks the one-wide bridge)
            if (!still) idle_since_.erase(a.id);
            if (!a.takes_orders() && a.state != sim::UnitState::CantGo) continue;                  // (a clip runs: the next look)
            if (ours && !still && now < g->second.ordered + latency_ + 20u) continue;               // on its way: the order has not even worked yet
            Way home, far;
            const int32_t lead = static_cast<int32_t>(latency_) * (still ? 1 : 2);                 // (an ant that walks may go on the wrong way for a while before the order works)
            const std::function<bool(sim::TileCoord)> homeward = homeward_of(idx);
            ways_off(grid, walkable_now, life_fn, homeward, a.tile, lead, margin, true, home, far);
            if (ours && g->second.mode == Guarded::Mode::Home && home.found && !still) continue;    // on its way home, and it makes it
            const bool recalled = !carrying && works && (shut(pile) || excess.count(a.id) != 0);   // an empty ant on its way to a pile that is closed to it
            const Way* best = nullptr;
            if (home.found && far.found) best = (carrying || recalled || home.slack >= trigger || home.slack + 30 >= far.slack) ? &home : &far;   // (the way home, unless it is tight and the far shore is clearly safer)
            else if (home.found) best = &home;
            else if (far.found) best = &far;
            bool act = ours || recalled || best == nullptr || best->slack < trigger;
            if (!act && still) {                                                                   // standing on the bridge for a whole look
                const auto idle = idle_since_.find(a.id);
                if (idle == idle_since_.end()) idle_since_[a.id] = now;
                else if (now >= idle->second + interval_ + 20u) act = true;
            }
            if (!act) continue;
            Way dhome, dfar;
            if (best == nullptr) {                                                                 // no way off in time: the nearest is the best bet
                ways_off(grid, walkable_now, life_fn, homeward, a.tile, lead, margin, false, dhome, dfar);
                if (dhome.found && dfar.found) best = dhome.arrive <= dfar.arrive ? &dhome : &dfar;
                else if (dhome.found) best = &dhome;
                else if (dfar.found) best = &dfar;
            }
            if (best == nullptr) continue;
            if (!ours) {
                if (!c.ledger.take(a.id, id())) continue;
                Guarded gd;
                gd.since = now;
                gd.pile = works ? pile : kNoPile;
                g = guarded_.emplace(a.id, gd).first;
                ++escapes_;
            }
            Guarded& gd = g->second;
            const bool toward_home = (best == &home || best == &dhome) && home_ok(idx);             // (the way home may lead over another bridge that will not last: then it ends at the shore)
            gd.mode = toward_home ? Guarded::Mode::Home : Guarded::Mode::Escape;
            gd.target = toward_home ? home_tile(c, carrying) : best->exit;
            idle_since_.erase(a.id);
            if (a.takes_orders() && (gd.ordered == 0 || now >= gd.ordered + latency_ + (still ? 20u : 40u))) {   // (told again only when the order should have worked by now)
                order_move(a.id, gd.target);
                gd.ordered = now;
            }
            continue;
        }
        idle_since_.erase(a.id);

        // 2. an ant on land
        bool toward_hot = false;                                                                   // walking towards the end of a bridge that is about to go, whatever it was told
        if (!hot_entries_.empty() && a.state == sim::UnitState::Walking && a.takes_orders()) {
            const int32_t d = hot_dist(a.tile);
            const auto prev = prev_tile_.find(a.id);
            toward_hot = d <= radius && (prev == prev_tile_.end() || d <= hot_dist(prev->second));        // (not moving away from it)
        }
        if (toward_hot && ours) {                                                                  // (an ant of the guard that walks home over a hot bridge: its way goes over it, whatever the map says)
            Guarded& gd = g->second;
            gd.mode = Guarded::Mode::Hold;
            if (gd.ordered != 0 && now < gd.ordered + latency_ + 20u) continue;                    // (told already: the order has not worked yet, and a second one only spends the budget of the level)
            gd.ordered = now;
            const sim::TileCoord park = park_of(a.tile);
            if (park.x >= 0) {
                gd.target = park;
                order_move(a.id, park);
            } else {
                stops.push_back(a.id);
            }
            continue;
        }
        if (repath && carrying && a.state == sim::UnitState::Walking && !ours) repaths.push_back(a.id);   // (a carrier walks home: told again, it finds its way now)
        if (ours) {
            Guarded& gd = g->second;
            if (repath) gd.ordered = 0;
            const bool near_hill = field_.valid && field_.cost[static_cast<size_t>(idx)] >= 0 && field_.cost[static_cast<size_t>(idx)] <= kNearHill;
            const bool by_hot = hot_dist(a.tile) <= radius;                                        // (a hot bridge end is near: it stays where it is)
            if (gd.mode == Guarded::Mode::Escape) gd.mode = home_ok(idx) && !by_hot ? Guarded::Mode::Home : Guarded::Mode::Hold;   // off the bridge: home, or where it stands
            if (gd.mode == Guarded::Mode::Home && !home_ok(idx)) gd.mode = Guarded::Mode::Hold;
            if (gd.mode == Guarded::Mode::Home) {
                if (still && near_hill) {
                    c.ledger.release(a.id, id());                                                  // home: the economy has it again
                    guarded_.erase(g);
                } else if (a.takes_orders() && (gd.ordered == 0 || (still && now >= gd.ordered + latency_ + 40u))) {
                    gd.target = home_tile(c, carrying);
                    order_move(a.id, gd.target);
                    gd.ordered = now;
                }
                continue;
            }
            // Hold
            if (home_ok(idx) && hot_dist(a.tile) > radius) {
                gd.mode = Guarded::Mode::Home;                                                     // a bridge stands that gets it home now
                if (a.takes_orders()) {
                    gd.target = home_tile(c, carrying);
                    order_move(a.id, gd.target);
                    gd.ordered = now;
                }
            } else if (!carrying && still && (gd.pile == kNoPile || !shut(gd.pile)) && hot_dist(a.tile) > radius) {
                c.ledger.release(a.id, id());                                                      // the pile it worked at is gone (or open again), and no bridge that goes is near: the economy has it again
                guarded_.erase(g);
            } else if (hot_dist(a.tile) <= radius + 1 && a.takes_orders() && now >= gd.ordered + latency_ + 40u) {
                const sim::TileCoord park = park_of(a.tile);                                       // too near the end of a bridge that goes: it blocks the way off it
                if (park.x >= 0) {
                    gd.target = park;
                    order_move(a.id, park);
                    gd.ordered = now;
                }
            } else if (a.state == sim::UnitState::Walking && now >= gd.ordered + latency_ && (hot_dist(a.tile) <= radius || !home_ok(idx))) {
                stops.push_back(a.id);                                                             // something set it walking: it would walk onto a bridge that goes
                gd.ordered = now;
            }
            continue;
        }
        if (!a.takes_orders() || !field_.valid || field_.cost[static_cast<size_t>(idx)] < 0) continue;   // (cut off: no bridge to walk onto)
        const bool closed = works && (shut(pile) || excess.count(a.id) != 0);
        // an empty ant that stands idle by the end of a bridge that goes: the economy may send it to a pile over another bridge, and the engine takes the cheapest way from where the ant
        // stands, which may lead over this one (the way from the hill is another). It waits farther off
        const bool by_hot = !carrying && still && hot_dist(a.tile) <= radius;
        // an ant that carries food walks home by itself: it is stopped only when it would not get home before a bridge tile goes. An empty ant of the economy that works a closed pile: home
        // while the way lasts, else where it stands
        if (!toward_hot && !by_hot && (carrying ? (home_ok(idx) || a.state != sim::UnitState::Walking) : !closed)) continue;
        if (!c.ledger.take(a.id, id())) continue;
        Guarded gd;
        gd.since = now;
        gd.ordered = now;
        gd.pile = pile;
        if (home_ok(idx) && !toward_hot && !by_hot) {                                              // (an ant that walks to a hot end is kept where it is: its way goes over it, whatever the map says)
            gd.mode = Guarded::Mode::Home;
            gd.target = home_tile(c, carrying);
            order_move(a.id, gd.target);
            ++recalls_;
        } else {
            gd.mode = Guarded::Mode::Hold;
            const sim::TileCoord park = hot_dist(a.tile) <= radius + 1 ? park_of(a.tile) : sim::TileCoord{-1, -1};
            if (park.x >= 0) {
                gd.target = park;
                order_move(a.id, park);
            } else {
                stops.push_back(a.id);
            }
            ++holds_;
        }
        guarded_[a.id] = gd;
    }
    (void)life_eff;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Swimmer) prev_tile_[a.id] = a.tile;
    }
    for (auto it = prev_tile_.begin(); it != prev_tile_.end();) {
        if (find_ant(v.mine(), it->first) == nullptr) it = prev_tile_.erase(it);
        else ++it;
    }
    if (!repaths.empty()) {
        c.orders.move(repaths, c.map.hill(c.seat).entrance, Priority::Urgent);
        repaths_ += static_cast<uint32_t>(repaths.size());
    }
    for (const auto& e : moves) c.orders.move(e.second, sim::TileCoord{e.first.first, e.first.second}, Priority::Urgent);
    if (!stops.empty()) c.orders.stop(stops, Priority::Urgent);
}

void IslandTask::step(TaskContext& c) {
    const BotView& v = c.view;
    if (!v.has_grid() || !start(c)) return;
    const uint64_t now = v.tick();
    if (scan_bridges(v, now)) next_refresh_ = 0;                                                   // a new bridge tile: the piles it opens get their limits before the economy looks
    // the Swimmers that the level wants: the power-up task takes them from the map (one ant each, the closest)
    if (v.ticks_left() > params_.min_ticks_left && beyond_food(v, c.map, c.seat)) tactics_.wants[static_cast<size_t>(sim::AntType::Swimmer)] = static_cast<uint8_t>(std::max<uint32_t>(params_.swimmers, tactics_.wants[static_cast<size_t>(sim::AntType::Swimmer)]));
    adopt_builders(c);
    if (now >= next_refresh_) {
        refresh(c);
        next_refresh_ = now + params_.refresh_ticks;
    }
    if (params_.guard) guard(c);
    for (auto& e : builders_) step_builder(c, e.first, e.second);
}

}  // namespace ants::ai
