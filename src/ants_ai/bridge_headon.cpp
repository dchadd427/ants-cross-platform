#include "ants_ai/bridge_headon.hpp"

#include <algorithm>
#include <set>

#include "ants_sim/movement_tables.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::tile_index;

namespace {

// Whether the ant is in a state that goes nowhere while it holds a tile: a walk that is blocked shows Walking at one tile, a refusal shows the can't-go clip, a bump shows the idle clip
bool holds_tile(const AntView& a) noexcept {
    return a.state == sim::UnitState::Idle || a.state == sim::UnitState::GuardIdle || a.state == sim::UnitState::CantGo || a.state == sim::UnitState::Walking;
}

}  // namespace

void BridgeHeadOnTask::release_all(TaskContext& c) {
    for (const auto& h : held_) c.ledger.release(h.first, id());
    held_.clear();
}

void BridgeHeadOnTask::finish(AntLedger& ledger) {
    held_.clear();
    Task::finish(ledger);
}

void BridgeHeadOnTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    if (!plan.bridge_headon || !v.has_grid()) {
        release_all(c);
        still_.clear();
        return;
    }
    const sim::Grid& grid = v.grid();
    // the ants that gave way: held on land until the other has passed (or the hold has run out), then given back
    for (auto it = held_.begin(); it != held_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool owned = a != nullptr && c.ledger.owner(it->first) == id();
        if (!owned || now >= it->second.until || a->holding || a->carried_points > 0) {
            if (owned) c.ledger.release(it->first, id());
            it = held_.erase(it);
            continue;
        }
        // a click that has not been carried out (the ant stands where it stood on a bridge tile): given again, no oftener than every 40 ticks
        if (a->tile != it->second.shore && now >= it->second.ordered + 40u && grid.in_bounds(a->tile) && grid.get_cell(a->tile).has_completed_bridge()) {
            if (a->takes_orders()) c.orders.move({a->id}, it->second.shore, Priority::Urgent);
            else if (a->state == sim::UnitState::CantGo) c.orders.volley(a->id, it->second.shore, {0, 3, 7, 10, 14, 17}, Priority::Urgent);
            it->second.ordered = now;
        }
        ++it;
    }
    for (auto it = cooldown_.begin(); it != cooldown_.end();) it = it->second <= now ? cooldown_.erase(it) : std::next(it);
    // the ants on a finished bridge tile that have not changed tile
    std::map<uint32_t, Still> next;
    const auto note = [&](const AntView& a) {
        if (a.type == sim::AntType::Swimmer || !grid.in_bounds(a.tile) || !grid.get_cell(a.tile).has_completed_bridge() || !holds_tile(a)) return;
        const auto was = still_.find(a.id);
        next[a.id] = was != still_.end() && was->second.tile == a.tile ? was->second : Still{a.tile, now};
    };
    for (const AntView& a : v.mine()) note(a);
    for (const AntView& a : v.others()) note(a);
    still_ = std::move(next);
    if (still_.size() < 2 || held_.size() >= params_.max_hold_ants) return;
    // the pairs: neighbouring bridge tiles, both still for stand_ticks, at least one of them the seat's own
    std::set<uint32_t> asked;
    struct Pick {
        const AntView* ant;
        const AntView* other;
        sim::TileCoord shore;
        int32_t cost;
        uint8_t rank;
    };
    std::vector<Pick> picks;
    std::vector<uint8_t> mask;
    for (const AntView& a : v.mine()) {
        const auto sa = still_.find(a.id);
        if (sa == still_.end() || now < sa->second.since + params_.stand_ticks || held_.count(a.id) != 0 || cooldown_.count(a.id) != 0 || a.hp == 0) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;     // (the ants of a fight, a heal or a goal keep their way)
        const AntView* other = nullptr;
        for (const AntView& b : v.mine()) {
            if (b.id == a.id || held_.count(b.id) != 0) continue;                                // (an ant that has given way already is on its way: the pair is the owner's of the other to go on with)
            const auto sb = still_.find(b.id);
            if (sb != still_.end() && now >= sb->second.since + params_.stand_ticks && b.tile.chebyshev_dist(a.tile) == 1) other = &b;
        }
        if (other == nullptr) {
            for (const AntView& b : v.others()) {
                const auto sb = still_.find(b.id);
                if (sb != still_.end() && now >= sb->second.since + params_.stand_ticks && b.tile.chebyshev_dist(a.tile) == 1) other = &b;
            }
        }
        if (other == nullptr) continue;
        // the way back to land that does not lead through the other ant: the nearest tile that is no bridge piece
        if (mask.empty()) mask = MapInfo::walkable_mask(grid, c.seat, v.walk_context());
        std::vector<uint8_t> free_walk = mask;
        free_walk[tile_index(grid, other->tile)] = 0;
        const std::vector<int32_t> cost = MapInfo::cost_field(grid, free_walk, a.tile);
        if (cost.empty()) continue;
        sim::TileCoord shore{-1, -1};
        int32_t best = 1 << 30;
        for (int32_t dy = -params_.retreat_tiles; dy <= params_.retreat_tiles; ++dy) {
            for (int32_t dx = -params_.retreat_tiles; dx <= params_.retreat_tiles; ++dx) {
                const sim::TileCoord t{a.tile.x + dx, a.tile.y + dy};
                if (!grid.in_bounds(t) || grid.get_cell(t).has_any_bridge() || grid.terrain_class_at(t) == sim::movement::kTerrainWater) continue;
                if (v.powerup_at(t) != nullptr || grid.has_fire_at(t) || grid.has_bomb_at(t) || standard_detail::occupied(v, t)) continue;
                const int32_t k = cost[tile_index(grid, t)];
                if (k >= 0 && k < best) {
                    best = k;
                    shore = t;
                }
            }
        }
        if (shore.x < 0) continue;
        picks.push_back(Pick{&a, other, shore, best, owner == kNoTask ? uint8_t{0} : c.ledger.rank(owner)});
    }
    if (picks.empty()) return;
    ++jams_;
    // the ant that is nearest to land gives way (the one that has a task of a lower rank next, then the lower number); one ant of a pair, never both
    std::stable_sort(picks.begin(), picks.end(), [](const Pick& x, const Pick& y) {
        if (x.cost != y.cost) return x.cost < y.cost;
        if (x.rank != y.rank) return x.rank < y.rank;
        return x.ant->id < y.ant->id;
    });
    for (const Pick& p : picks) {
        if (held_.size() >= params_.max_hold_ants) break;
        if (asked.count(p.ant->id) != 0 || asked.count(p.other->id) != 0) continue;           // (the other of this pair gave way already)
        if (!c.ledger.take(p.ant->id, id())) continue;
        if (p.ant->takes_orders()) c.orders.move({p.ant->id}, p.shore, Priority::Urgent);
        else if (p.ant->state == sim::UnitState::CantGo) c.orders.volley(p.ant->id, p.shore, {0, 3, 7, 10, 14, 17}, Priority::Urgent);
        Held h;
        h.shore = p.shore;
        h.until = now + params_.hold_ticks;
        h.ordered = now;
        held_[p.ant->id] = h;
        cooldown_[p.ant->id] = now + params_.cooldown_ticks;
        asked.insert(p.ant->id);
        asked.insert(p.other->id);
        ++backoffs_;
    }
}

}  // namespace ants::ai
