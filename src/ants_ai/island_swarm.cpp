#include "ants_ai/island_swarm.hpp"

#include <algorithm>

#include "ants_ai/map_info.hpp"

namespace ants::ai {

namespace {

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

int32_t index_of(const sim::Grid& grid, sim::TileCoord t) noexcept { return t.y * static_cast<int32_t>(grid.width()) + t.x; }

// The order in which spare ants are used: a plain ant first, then a Fire Ant, a Bomber last (a typed ant leaves its hat where it takes the Swimmer, and the Bomber is the miner's)
int kind_rank(const AntView& a, const BotView& v) noexcept {
    if (a.type == v.default_ant_type()) return 0;
    return a.type == sim::AntType::Fire ? 1 : 2;
}

}  // namespace

void SwarmTask::release_all(AntLedger& ledger) {
    for (const auto& s : staged_) ledger.release(s.first, id());
    staged_.clear();
}

void SwarmTask::finish(AntLedger& ledger) {
    release_all(ledger);
    Task::finish(ledger);
}

// The walking cost onto the drop tile of every link from every tile, over the tiles that can be walked now (the bridges that stand, none that go): searched every refresh_ticks
const std::vector<int32_t>& SwarmTask::field_for(TaskContext& c, size_t link) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!fields_valid_ || now >= fields_tick_ + params_.refresh_ticks || fields_.size() != tactics_.flower_links.size()) {
        std::vector<uint8_t> mask = v.walkable_mask(c.seat);
        for (size_t i = 0; i < mask.size() && i < tactics_.shut_tiles.size(); ++i) {
            if (tactics_.shut_tiles[i] != 0) mask[i] = 0;
        }
        const WalkContext ctx = v.walk_context();
        const bool same = fields_valid_ && fields_.size() == tactics_.flower_links.size() && fields_ctx_.ally == ctx.ally && fields_ctx_.dropped_mask == ctx.dropped_mask && fields_mask_ == mask;
        if (!same) {                                                                           // (the walks are a function of the mask: the same mask keeps them, no flood)
            fields_.clear();
            for (const Tactics::FlowerLink& l : tactics_.flower_links) fields_.push_back(MapInfo::cost_field_onto(v.grid(), mask, c.seat, l.drop, ctx));
            fields_mask_ = std::move(mask);
            fields_ctx_ = ctx;
        }
        fields_tick_ = now;
        fields_valid_ = true;
    }
    return fields_[link];
}

// The free tile beside the drop tile that the ant waits on (the nearest to where it stands): reached by the field, no power-up, no ant and nobody else's place on it
sim::TileCoord SwarmTask::free_spot(TaskContext& c, size_t link, const std::vector<int32_t>& field, sim::TileCoord from, uint32_t self) const {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const sim::TileCoord drop = tactics_.flower_links[link].drop;
    sim::TileCoord spot{-1, -1};
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const sim::TileCoord n{drop.x + dx, drop.y + dy};
            if ((dx == 0 && dy == 0) || !grid.in_bounds(n) || field.empty() || field[static_cast<size_t>(index_of(grid, n))] < 0 || v.powerup_at(n) != nullptr) continue;
            if (tactics_.plan.island_endraid && grid.get_cell(n).has_any_bridge()) continue;                  // (an ant that waits on a piece of a bridge shuts it: the Thieves of the strip go home over it, docs/BOT_IDLE.md)
            bool taken = false;
            for (const AntView& a : v.others()) taken = taken || a.tile == n;
            for (const AntView& a : v.mine()) taken = taken || (a.tile == n && a.id != self);
            for (const auto& s : staged_) taken = taken || (s.first != self && s.second.spot == n);
            if (taken) continue;
            if (spot.x < 0 || n.chebyshev_dist(from) < spot.chebyshev_dist(from)) spot = n;
        }
    }
    return spot;
}

void SwarmTask::step(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    if (!plan.island_flower_link || !v.has_grid() || tactics_.flower_links.empty() || islands_ == nullptr || !islands_->active()) {
        release_all(c.ledger);
        return;
    }
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    uint32_t swimmers = 0;
    for (const AntView& a : v.mine()) swimmers += a.type == sim::AntType::Swimmer ? 1u : 0u;
    // a tile that the island task has retired (a bridge that will not last another trip: Tactics::shut_tiles) within `shut_radius` tiles of `tile`
    const auto near_shut = [&](const sim::Grid& g, sim::TileCoord tile) {
        for (int32_t dy = -params_.shut_radius; dy <= params_.shut_radius; ++dy) {
            for (int32_t dx = -params_.shut_radius; dx <= params_.shut_radius; ++dx) {
                const sim::TileCoord t{tile.x + dx, tile.y + dy};
                if (!g.in_bounds(t)) continue;
                const size_t idx = static_cast<size_t>(index_of(g, t));
                if (idx < tactics_.shut_tiles.size() && tactics_.shut_tiles[idx] != 0) return true;
            }
        }
        return false;
    };
    // the ants that wait: still there, still ours; one that is a Swimmer now is made (the ferry takes it)
    for (auto it = staged_.begin(); it != staged_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        bool keep = a != nullptr && c.ledger.owner(it->first) == id();
        const bool bridges = plan.island_link_cost || plan.island_return || plan.island_feeder || plan.island_power_bridge;
        if (keep && bridges && grid.in_bounds(a->tile) && a->tile != it->second.spot && near_shut(grid, a->tile)) {      // no walk over the bridges that will last reaches the ant's place any more, and it is by a bridge that falls (docs/BOT_ISLANDS.md, "Bridges for ants"): ...
            const std::vector<int32_t>& f = field_for(c, it->second.link);
            if (f.empty() || f[static_cast<size_t>(index_of(grid, a->tile))] < 0) {                         // ... it is let go and stopped (an order that is under way would take it onto the bridge), not sent on
                c.ledger.release(it->first, id());
                if (a->takes_orders()) c.orders.stop({a->id}, Priority::Urgent);
                keep = false;
            }
        }
        if (keep && a->type == sim::AntType::Swimmer) {
            ++made_;
            if (first_made_ == 0) first_made_ = now;
            c.ledger.release(it->first, id());
            keep = false;
        } else if (keep && plan.island_endraid && a->type == sim::AntType::Thief) {                                              // (the Thief of the endgame: the bridge planner and the raid task have it now)
            ++thieves_;
            c.ledger.release(it->first, id());
            keep = false;
        } else if (keep && (a->holding || a->carried_points > 0)) {
            c.ledger.release(it->first, id());
            keep = false;
        }
        if (keep && it->second.picking != 0 && now >= it->second.picking + params_.take_wait) it->second.picking = 0;      // (the pick-up did not make a Swimmer: it may be ordered again)
        it = keep ? std::next(it) : staged_.erase(it);
    }
    uint32_t coming = 0;                                                                              // the ants that are on their way to a Swimmer on a drop tile
    for (const auto& s : staged_) coming += s.second.picking != 0 ? 1u : 0u;
    const uint32_t want = plan.island_swarm_swimmers;
    uint32_t thieves = 0;
    for (const AntView& a : v.mine()) thieves += a.type == sim::AntType::Thief ? 1u : 0u;
    const bool thief_wanted = plan.island_endraid && tactics_.endraid && thieves < plan.endraid_thieves && v.ticks_left() >= params_.min_ticks_left;      // (the Thief of the endgame: one ant waits at a strip for the one that falls)
    if ((swimmers >= want && !thief_wanted) || v.ticks_left() < params_.min_ticks_left) {
        release_all(c.ledger);
        return;
    }
    if (swimmers + coming >= want && !thief_wanted) return;                                           // (the ants that are on their way make the rest)
    const size_t links = tactics_.flower_links.size();
    // (the links that end at one drop tile share it: the ants are counted for the strip, so that each strip has its own before one has two)
    const auto same_drop = [&](size_t i, size_t j) { return tactics_.flower_links[i].drop == tactics_.flower_links[j].drop; };
    std::vector<uint32_t> at_link(links, 0);
    for (const auto& s : staged_) {
        for (size_t i = 0; i < links; ++i) at_link[i] += same_drop(i, s.second.link) ? 1u : 0u;
    }
    uint32_t lacking = swimmers + coming >= want ? 0u : want - swimmers - coming;
    if (thief_wanted) lacking = std::max<uint32_t>(lacking, 1u);

    // the ants for the strips: the free ants (and the economy's) that a walk reaches a strip from, the nearest first (a plain ant before a typed one)
    if (staged_.size() < lacking) {
        struct Pick {
            const AntView* ant;
            size_t link;
            int32_t cost;
            int kind;
        };
        std::vector<Pick> picks;
        for (const AntView& a : v.mine()) {
            if (staged_.count(a.id) != 0 || (a.type != v.default_ant_type() && a.type != sim::AntType::Fire && a.type != sim::AntType::Bomber)) continue;
            if (a.holding || a.carried_points > 0 || a.hp < plan.island_swarm_hp || !a.takes_orders() || !grid.in_bounds(a.tile)) continue;
            if (a.state == sim::UnitState::Stunned || v.powerup_at(a.tile) != nullptr) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            int32_t best_cost = -1;
            size_t best_link = 0;
            for (size_t i = 0; i < links; ++i) {
                if (at_link[i] >= plan.island_swarm_stage) continue;
                if (plan.strip_fight && tactics_.strip_closed(tactics_.flower_links[i].drop, now)) continue;                   // (the strip that the bot left because it was losing the fight there: plan.strip_fight)
                const std::vector<int32_t>& f = field_for(c, i);
                const int32_t cost = f.empty() ? -1 : f[static_cast<size_t>(index_of(grid, a.tile))];
                if (cost < 0 || static_cast<uint32_t>(MapInfo::walking_ticks(cost)) > params_.max_walk_ticks) continue;
                if (best_cost < 0 || cost < best_cost) {
                    best_cost = cost;
                    best_link = i;
                }
            }
            if (best_cost >= 0) picks.push_back(Pick{&a, best_link, best_cost, kind_rank(a, v)});
        }
        std::stable_sort(picks.begin(), picks.end(), [](const Pick& x, const Pick& y) { return x.kind != y.kind ? x.kind < y.kind : x.cost < y.cost; });
        for (const Pick& p : picks) {
            if (staged_.size() >= lacking) break;
            if (at_link[p.link] >= plan.island_swarm_stage) continue;
            const sim::TileCoord spot = free_spot(c, p.link, field_for(c, p.link), p.ant->tile, p.ant->id);
            if (spot.x < 0 || !c.ledger.take(p.ant->id, id())) continue;
            Place place;
            place.link = p.link;
            place.spot = spot;
            place.before = p.ant->type;
            staged_[p.ant->id] = place;
            for (size_t i = 0; i < links; ++i) at_link[i] += same_drop(i, p.link) ? 1u : 0u;
            ++sent_;
        }
    }

    // the walk to the place, and the pick-up of a Swimmer that lies on a drop tile (the nearest ant of the strip that is no Swimmer; one at a time for a strip)
    for (auto& s : staged_) {
        const AntView* a = find_ant(v.mine(), s.first);
        Place& pl = s.second;
        if (a == nullptr || pl.picking != 0 || a->tile == pl.spot || !a->takes_orders()) continue;
        if (pl.ordered != 0 && !(a->idle() && now >= pl.ordered + params_.order_gap)) continue;
        sim::TileCoord spot = pl.spot;
        bool blocked = v.powerup_at(spot) != nullptr;
        for (const AntView& o : v.others()) blocked = blocked || o.tile == spot;
        if (blocked) {
            spot = free_spot(c, pl.link, field_for(c, pl.link), a->tile, a->id);
            if (spot.x < 0) continue;
            pl.spot = spot;
        }
        c.orders.move({a->id}, spot, Priority::Normal);
        pl.ordered = now;
    }
    for (size_t i = 0; i < links; ++i) {
        const sim::TileCoord drop = tactics_.flower_links[i].drop;
        const PowerUpView* p = v.powerup_at(drop);
        if (p == nullptr || p->standing_ant != 0) continue;
        if (p->kind == sim::AntType::Swimmer ? swimmers + coming >= want : !(p->kind == sim::AntType::Thief && thief_wanted)) continue;    // (a Swimmer when the bot lacks one, the Thief when it wants it)
        bool busy = false;
        const AntView* best = nullptr;
        int32_t best_d = 0;
        for (const auto& s : staged_) {
            if (!same_drop(s.second.link, i)) continue;
            busy = busy || s.second.picking != 0;
            const AntView* a = find_ant(v.mine(), s.first);
            if (a == nullptr || !a->takes_orders() || a->type == sim::AntType::Swimmer) continue;
            const int32_t d = a->tile.chebyshev_dist(drop);
            if (best == nullptr || d < best_d) {
                best = a;
                best_d = d;
            }
        }
        if (busy || best == nullptr) continue;
        c.orders.pick_up(best->id, drop, Priority::Urgent);
        staged_[best->id].picking = now;
        ++pickups_;
    }
}

}  // namespace ants::ai
