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

// A land tile that touches a finished bridge tile (the landing of a bridge: an ant that stands there is in the way of every ant that comes off the bridge or goes on it)
bool at_bridge_foot(const sim::Grid& grid, sim::TileCoord t) {
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const sim::TileCoord n{t.x + dx, t.y + dy};
            if ((dx != 0 || dy != 0) && grid.in_bounds(n) && grid.get_cell(n).has_completed_bridge()) return true;
        }
    }
    return false;
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
        jam_.clear();
        idle_.clear();
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
    clear_feet(c);
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
        int32_t best = 1 << 30;
        const sim::TileCoord shore = shore_for(c, a, mask, other->tile, false, best);
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

sim::TileCoord BridgeHeadOnTask::shore_for(TaskContext& c, const AntView& a, const std::vector<uint8_t>& mask, sim::TileCoord other, bool off_the_feet, int32_t& best_cost) const {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    // (only the tiles within the retreat range are walkable for the search: the flood is no wider than the box that the shore is looked for in)
    std::vector<uint8_t> free_walk(mask.size(), 0);
    const int32_t width = static_cast<int32_t>(grid.width());
    for (int32_t y = std::max(0, a.tile.y - params_.retreat_tiles); y <= std::min(static_cast<int32_t>(grid.height()) - 1, a.tile.y + params_.retreat_tiles); ++y) {
        const int32_t x0 = std::max(0, a.tile.x - params_.retreat_tiles);
        const int32_t x1 = std::min(width - 1, a.tile.x + params_.retreat_tiles);
        for (int32_t x = x0; x <= x1; ++x) free_walk[static_cast<size_t>(y * width + x)] = mask[static_cast<size_t>(y * width + x)];
    }
    if (grid.in_bounds(other)) free_walk[tile_index(grid, other)] = 0;
    const std::vector<int32_t> cost = MapInfo::cost_field(grid, free_walk, a.tile);
    sim::TileCoord shore{-1, -1};
    best_cost = 1 << 30;
    if (cost.empty()) return shore;
    for (int32_t dy = -params_.retreat_tiles; dy <= params_.retreat_tiles; ++dy) {
        for (int32_t dx = -params_.retreat_tiles; dx <= params_.retreat_tiles; ++dx) {
            const sim::TileCoord t{a.tile.x + dx, a.tile.y + dy};
            if (!grid.in_bounds(t) || grid.get_cell(t).has_any_bridge() || grid.terrain_class_at(t) == sim::movement::kTerrainWater) continue;
            if (v.powerup_at(t) != nullptr || grid.has_fire_at(t) || grid.has_bomb_at(t) || standard_detail::occupied(v, t)) continue;
            if (off_the_feet && at_bridge_foot(grid, t)) continue;
            const int32_t k = cost[tile_index(grid, t)];
            if (k >= 0 && k < best_cost) {
                best_cost = k;
                shore = t;
            }
        }
    }
    return shore;
}

// An own ant that has stood idle on the land at the foot of a bridge for plug_ticks, with an ant of any team that cannot go within jam_tiles (in the can't-go clip at three looks, or standing on a bridge
// tile beside it): the ant stands where the walk of the other has to go (the landing of a bridge is one tile, and an ant that stands on it is no way at all for the engine). It steps off the feet.
void BridgeHeadOnTask::clear_feet(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    // who shows the can't-go clip, and who stands idle
    const auto note_jam = [&](const AntView& a) {
        if (a.state != sim::UnitState::CantGo) return;
        Jam& j = jam_[a.id];
        if (j.looks == 0 || j.tile != a.tile) j = Jam{a.tile, now, now, 0};
        j.last = now;
        ++j.looks;
    };
    for (const AntView& a : v.mine()) note_jam(a);
    for (const AntView& a : v.others()) note_jam(a);
    for (auto it = jam_.begin(); it != jam_.end();) it = now > it->second.last + 60u ? jam_.erase(it) : std::next(it);
    std::map<uint32_t, Still> next;
    for (const AntView& a : v.mine()) {
        if (!a.idle() || a.type == sim::AntType::Swimmer) continue;
        const auto was = idle_.find(a.id);
        next[a.id] = was != idle_.end() && was->second.tile == a.tile ? was->second : Still{a.tile, now};
    }
    idle_ = std::move(next);
    if (jam_.empty() && still_.empty()) return;
    std::vector<uint8_t> mask;
    for (const auto& e : idle_) {
        if (held_.size() >= params_.max_hold_ants) return;
        if (now < e.second.since + params_.plug_ticks || held_.count(e.first) != 0 || cooldown_.count(e.first) != 0) continue;
        const AntView* a = find_ant(v.mine(), e.first);
        if (a == nullptr || a->hp == 0 || !a->takes_orders() || a->holding || a->carried_points > 0 || v.powerup_at(a->tile) != nullptr) continue;
        const TaskId owner = c.ledger.owner(a->id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        if (!grid.in_bounds(a->tile) || grid.get_cell(a->tile).has_any_bridge() || !at_bridge_foot(grid, a->tile)) continue;
        bool jammed = false;                                                                      // an ant that cannot go is near, or one stands on a bridge tile beside the ant
        for (const auto& j : jam_) {
            if (j.first != a->id && j.second.looks >= 3 && now >= j.second.since + 40u && j.second.tile.chebyshev_dist(a->tile) <= params_.jam_tiles) jammed = true;
        }
        for (const auto& s : still_) {
            if (s.first != a->id && now >= s.second.since + params_.stand_ticks && s.second.tile.chebyshev_dist(a->tile) <= 1) jammed = true;
        }
        if (!jammed) continue;
        if (mask.empty()) mask = MapInfo::walkable_mask(grid, c.seat, v.walk_context());
        int32_t cost = 0;
        const sim::TileCoord shore = shore_for(c, *a, mask, sim::TileCoord{-1, -1}, true, cost);
        if (shore.x < 0 || !c.ledger.take(a->id, id())) continue;
        c.orders.move({a->id}, shore, Priority::Urgent);
        Held h;
        h.shore = shore;
        h.until = now + params_.hold_ticks;
        h.ordered = now;
        held_[a->id] = h;
        cooldown_[a->id] = now + params_.cooldown_ticks;
        ++plugs_;
    }
}

}  // namespace ants::ai
