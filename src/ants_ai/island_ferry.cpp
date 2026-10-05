#include "ants_ai/island_ferry.hpp"

#include <algorithm>
#include <functional>
#include <queue>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

// The engine's cost of one step with the Swimmer's weights (MapInfo::step_cost has the walker's): the two tiles' weights added, times 1.4 (truncated) for a diagonal step, halved
uint32_t swim_step(const sim::Grid& grid, sim::TileCoord from, sim::TileCoord to) noexcept {
    uint32_t s = sim::movement::terrain_step_weight(grid.terrain_class_at(from), true) + sim::movement::terrain_step_weight(grid.terrain_class_at(to), true);
    if (from.x != to.x && from.y != to.y) s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
    return s >> 1;
}

}  // namespace

void FerryTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove || command.ants.size() != 1) return;
    const auto it = recs_.find(command.ants[0]);
    if (it == recs_.end()) return;
    const auto click = clicks_.find(it->second.pile);
    if (click == clicks_.end() || command.tile_x != click->second.x || command.tile_y != click->second.y) return;
    if (fate == Bot::Fate::Sent) it->second.sent = tick;                                            // the clock of the order starts when it LEFT
    else it->second.dropped = true;
}

// The Swimmer's cost from the hill over land and water, for the map as it is now (a bridge is land, a bomb or a wall blocks)
void FerryTask::search(TaskContext& c) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const int w = static_cast<int>(grid.width());
    const int h = static_cast<int>(grid.height());
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    cost_.assign(n, -1);
    const WalkContext ctx = v.walk_context();
    const auto passable = [&](sim::TileCoord t) {
        if (!grid.in_bounds(t)) return false;
        if (MapInfo::walkable(grid, c.seat, t, ctx)) return true;
        return grid.terrain_class_at(t) == sim::movement::kTerrainWater && grid.get_cell(t).is_empty_overlay();   // open water: a Swimmer swims over it (a dig in progress, a bomb, an object do not)
    };
    using Item = std::pair<int32_t, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const HillInfo& hill = c.map.hill(c.seat);
    for (const sim::TileCoord t : hill.starts) {
        if (!passable(t)) continue;
        const int32_t idx = t.y * w + t.x;
        cost_[static_cast<size_t>(idx)] = 0;
        open.push({0, idx});
    }
    while (!open.empty()) {
        const Item top = open.top();
        open.pop();
        if (top.first != cost_[static_cast<size_t>(top.second)]) continue;
        const sim::TileCoord cur{top.second % w, top.second / w};
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord t{cur.x + kDx[k], cur.y + kDy[k]};
            if (!passable(t)) continue;
            const int32_t next = top.first + static_cast<int32_t>(swim_step(grid, cur, t));
            int32_t& slot = cost_[static_cast<size_t>(t.y * w + t.x)];
            if (slot >= 0 && slot <= next) continue;
            slot = next;
            open.push({next, t.y * w + t.x});
        }
    }
    searched_ = v.tick();
    // the piles: the cell of each that is food now, and the cost of the way to it (a step onto the cell from the cheapest free neighbour)
    trips_.clear();
    clicks_.clear();
    for (const PileView& p : v.piles()) {
        const PileInfo* info = c.map.pile(p.index);
        if (info == nullptr) continue;                                                              // a lunchbox: no analysis
        int32_t best = -1;
        sim::TileCoord click{-1, -1};
        for (const sim::TileCoord cell : info->cells) {
            const int32_t o = grid.food_object_at_cell(cell);
            if (o < 0 || (static_cast<uint32_t>(o) != p.index && static_cast<uint32_t>(o) != info->bite_index)) continue;
            for (int k = 0; k < 8; ++k) {
                const sim::TileCoord t{cell.x + kDx[k], cell.y + kDy[k]};
                if (!grid.in_bounds(t)) continue;
                const int32_t d = cost_[static_cast<size_t>(t.y * w + t.x)];
                if (d < 0) continue;
                const int32_t total = d + static_cast<int32_t>(swim_step(grid, t, cell));
                if (best < 0 || total < best) {
                    best = total;
                    click = cell;
                }
            }
        }
        if (best < 0) continue;
        trips_[p.index] = MapInfo::trip_ticks_for_cost(best);
        clicks_[p.index] = click;
    }
}

void FerryTask::step(TaskContext& c) {
    const BotView& v = c.view;
    if (!v.has_grid()) return;
    const uint64_t now = v.tick();
    // the Swimmers: the records of the ones that died or that another task took are forgotten; a pile that is gone leaves its Swimmers free
    for (auto it = recs_.begin(); it != recs_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        bool pile = false;
        for (const PileView& p : v.piles()) pile = pile || p.index == it->second.pile;
        if (a == nullptr || !pile || c.ledger.owner(it->first) != id()) {
            c.ledger.release(it->first, id());
            it = recs_.erase(it);
        } else {
            ++it;
        }
    }
    std::vector<const AntView*> pool;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Swimmer) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id()) continue;
        if (recs_.count(a.id) != 0) {
            // an order that did nothing: the Swimmer stands where it stood, empty, long after the order left (or the order never left: it is ordered again)
            const Rec& r = recs_[a.id];
            const bool late = r.sent != 0 ? now >= r.sent + params_.fail_after : now >= r.decided + 3u * params_.fail_after;
            if (r.dropped) {
                c.ledger.release(a.id, id());
                recs_.erase(a.id);
            } else if (a.idle() && !a.holding && late && a.tile.chebyshev_dist(r.origin) <= 1) {
                ++failures_;
                excluded_[std::make_pair(a.id, r.pile)] = now + params_.blacklist_ticks;
                c.ledger.release(a.id, id());
                recs_.erase(a.id);
            }
            continue;
        }
        if (!a.takes_orders() || a.holding || a.carried_points > 0) {
            if (a.holding && a.idle()) {                                                             // a carrier that stands idle with its food: the engine's loop ended
                const auto it = carrying_.emplace(a.id, now).first;
                if (now >= it->second + params_.rescue_after && c.map.hill(c.seat).present) {
                    c.orders.move({a.id}, c.map.hill(c.seat).entrance);
                    it->second = now;
                    ++rescues_;
                }
            }
            continue;
        }
        carrying_.erase(a.id);
        if (a.idle()) pool.push_back(&a);
    }
    if (pool.empty()) return;
    if (cost_.empty() || now >= searched_ + params_.reask_ticks) search(c);

    // the piles by what they pay per tick of the trip, and the Swimmers that work them now
    struct Candidate {
        uint32_t pile{0};
        int64_t rank{0};
        uint32_t load{0};
        int32_t trip{0};
        sim::TileCoord click{};
    };
    std::vector<Candidate> cands;
    for (const PileView& p : v.piles()) {
        const auto trip = trips_.find(p.index);
        if (trip == trips_.end() || trip->second < 0) continue;
        if (static_cast<uint64_t>(v.ticks_left()) < static_cast<uint64_t>(trip->second) + params_.endgame_margin) continue;
        Candidate k;
        k.pile = p.index;
        k.trip = trip->second;
        k.click = clicks_[p.index];
        k.rank = static_cast<int64_t>(p.value) * std::min<int64_t>(p.remaining, 12) * 100000 / trip->second;
        if (!c.map.pile(p.index)->approach[c.seat].reachable()) k.rank *= 2;                       // the piles that no walker reaches come first: the workers have the others
        for (const auto& e : recs_) k.load += e.second.pile == p.index ? 1u : 0u;
        cands.push_back(k);
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.rank > b.rank; });
    for (const AntView* a : pool) {
        for (Candidate& k : cands) {
            if (k.load >= params_.per_pile) continue;
            const auto ex = excluded_.find(std::make_pair(a->id, k.pile));
            if (ex != excluded_.end() && ex->second > now) continue;
            if (!c.ledger.claim(a->id, id())) break;
            Rec r;
            r.pile = k.pile;
            r.decided = now;
            r.origin = a->tile;
            recs_[a->id] = r;
            c.orders.move({a->id}, k.click);
            ++k.load;
            ++sent_;
            break;
        }
    }
}

}  // namespace ants::ai
