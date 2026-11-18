#include "ants_ai/island_link.hpp"

#include <algorithm>
#include <functional>
#include <limits>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

// The ant of any team that stands on the tile, or null: no special order names such a tile
bool ant_on(const BotView& v, sim::TileCoord t) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == t) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == t) return true;
    }
    return false;
}

int32_t index_of(const sim::Grid& grid, sim::TileCoord t) noexcept { return t.y * static_cast<int32_t>(grid.width()) + t.x; }

}  // namespace

void FlowerLinkTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupSpecial || command.ants.size() != 1 || command.ants[0] != digger_ || !ordered_ || sent_ != kPending) return;
    if (command.tile_x != ordered_tile_.x || command.tile_y != ordered_tile_.y) return;
    if (fate == Bot::Fate::Sent) sent_ = tick;                                                     // the clock of the dig starts when the order LEFT
    else ordered_ = false;                                                                         // it never left: ordered again at the next look
}

void FlowerLinkTask::finish(AntLedger& ledger) {
    digger_ = 0;
    ordered_ = false;
    Task::finish(ledger);
}

void FlowerLinkTask::release_digger(TaskContext& c) {
    if (digger_ != 0) c.ledger.release(digger_, id());
    digger_ = 0;
    digger_link_ = -1;
    digger_kind_ = -1;
    blocked_since_ = 0;
    ordered_ = false;
    sent_ = kPending;
}

const AntView* FlowerLinkTask::nearest_digger(TaskContext& c, sim::TileCoord tile) const {
    const AntView* best = nullptr;
    int32_t best_dist = 0;
    for (const AntView& a : c.view.mine()) {
        if (a.type != sim::AntType::Swimmer || a.holding || a.carried_points > 0 || a.hp < 5 || !a.takes_orders()) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        const int32_t d = a.tile.chebyshev_dist(tile);
        if (best == nullptr || d < best_dist) {
            best = &a;
            best_dist = d;
        }
    }
    return best;
}

// The cost of a link in ticks: the dig (34 ticks for the tile next to the shore, 54 for each next one), the swim of the nearest Swimmer to the first tile still to dig (a Swimmer's step costs 21 where a
// walker's costs 20 on grass), the walk of the waiting ant to the drop tile of the strip, and the ants of other teams that stand about the drop tile (they take the drop by a few ticks: a strip with a crowd
// is a gamble). A link with no Swimmer to dig it costs more than any other.
int64_t FlowerLinkTask::link_cost(TaskContext& c, const Tactics::FlowerLink& link, const AntView& waiting) const {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    int64_t missing = 0;
    sim::TileCoord first{-1, -1};
    for (const sim::TileCoord t : link.tiles) {
        if (grid.get_cell(t).has_completed_bridge()) continue;
        if (first.x < 0) first = t;
        ++missing;
    }
    int64_t cost = missing == 0 ? 0 : static_cast<int64_t>(params_.dig_first_ticks) + (missing - 1) * static_cast<int64_t>(params_.dig_next_ticks);
    if (missing > 0) {
        const AntView* d = nearest_digger(c, first);
        cost += d == nullptr ? 100000 : MapInfo::walking_ticks(21 * d->tile.chebyshev_dist(first));
    }
    cost += MapInfo::walking_ticks(20 * waiting.tile.chebyshev_dist(link.drop));
    int64_t crowd = 0;
    for (const AntView& o : v.others()) {
        if (o.tile.chebyshev_dist(link.drop) <= static_cast<int32_t>(params_.crowd_radius)) ++crowd;
    }
    return cost + std::min<int64_t>(crowd, params_.crowd_cap) * static_cast<int64_t>(params_.crowd_ticks);
}

// The links, from the analysis of the start: a channel of at most max_tiles between a component that holds a flower (and is no hill's island) and another component that holds none and is no hill's
// island; the shortest channel for every such ring half, the lower strip number on a tie. The tiles are put in the order of a dig from the ring half outward.
void FlowerLinkTask::start(TaskContext& c) {
    started_ = true;
    tactics_.flower_links.clear();
    if (islands_ == nullptr || !islands_->active()) return;
    const IslandInfo& info = islands_->info();
    for (const IslandComponent& ring : info.components()) {
        if (ring.hill || !ring.flowers.empty()) continue;
        uint32_t worth = 0;                                                                         // the ring halves hold the big piles (the crews of the hills land there): a satellite is no ring
        for (const uint32_t pi : ring.piles) {
            const PileInfo* pile = c.map.pile(pi);
            if (pile != nullptr) worth = std::max<uint32_t>(worth, static_cast<uint32_t>(pile->units) * pile->value);
        }
        if (worth < params_.min_pile_worth) continue;
        for (const Channel& ch : info.channels()) {                                                 // (every strip within reach of the ring half is a link: the spare ants of the crew wait at all of them)
            if (ch.a != ring.id && ch.b != ring.id) continue;
            const int32_t other = ch.a == ring.id ? ch.b : ch.a;
            const IslandComponent* strip = info.component(other);
            if (strip == nullptr || strip->hill || strip->flowers.empty() || ch.length() < 1 || ch.length() > static_cast<int32_t>(params_.max_tiles)) continue;
            Tactics::FlowerLink link;
            link.ring = ring.id;
            link.strip = other;
            link.drop = flower_drop_tile(strip->flowers.front());
            link.tiles = ch.tiles;
            if (ch.a != ring.id) std::reverse(link.tiles.begin(), link.tiles.end());
            tactics_.flower_links.push_back(std::move(link));
        }
    }
}

void FlowerLinkTask::step(TaskContext& c) {
    const BotView& v = c.view;
    if (!tactics_.plan.island_flower_link || !v.has_grid()) {
        if (digger_ != 0) release_digger(c);
        return;
    }
    const uint64_t now = v.tick();
    if (!started_) {
        if (islands_ == nullptr || !islands_->active() || islands_->info().empty()) return;      // (the island task has not looked yet)
        start(c);
    }
    if (tactics_.flower_links.empty()) return;
    const sim::Grid& grid = v.grid();
    // the Swimmer that digs: still there, still ours, still empty-handed
    if (digger_ != 0) {
        const AntView* d = find_ant(v.mine(), digger_);
        if (d == nullptr || c.ledger.owner(digger_) != id() || d->holding || d->carried_points > 0) {
            if (d != nullptr && c.ledger.owner(digger_) == id()) c.ledger.release(digger_, id());
            digger_ = 0;
            digger_link_ = -1;
            digger_kind_ = -1;
            blocked_since_ = 0;
            ordered_ = false;
        }
    }
    if (now < next_look_ && digger_ == 0) return;
    next_look_ = now + params_.refresh_ticks;
    // the tiles that stand: when each was first seen finished (and forgotten when it is gone)
    for (const Tactics::FlowerLink& l : tactics_.flower_links) {
        for (const sim::TileCoord t : l.tiles) {
            const int32_t idx = index_of(grid, t);
            if (grid.get_cell(t).has_completed_bridge()) {
                if (seen_.emplace(idx, now).second) ++tiles_finished_;
            } else {
                seen_.erase(idx);
            }
        }
    }
    const auto life_left = [&](const Tactics::FlowerLink& l) {
        int64_t least = 1 << 30;
        for (const sim::TileCoord t : l.tiles) {
            const auto it = seen_.find(index_of(grid, t));
            if (it == seen_.end()) return int64_t{0};
            least = std::min<int64_t>(least, static_cast<int64_t>(it->second + params_.life_ticks) - static_cast<int64_t>(now));
        }
        return std::max<int64_t>(least, 0);
    };
    // nothing is dug for ants that would take nothing over there: the bot has the Swimmers that it wants
    uint32_t swimmers = 0;
    for (const AntView& a : v.mine()) swimmers += a.type == sim::AntType::Swimmer ? 1u : 0u;
    const bool lacking = swimmers < tactics_.plan.island_swarm_swimmers;
    int wanted = -1;                                                                                // the link that a free plain ant waits to cross
    int64_t wanted_cost = 0;
    for (size_t i = 0; i < tactics_.flower_links.size() && lacking; ++i) {
        const Tactics::FlowerLink& l = tactics_.flower_links[i];
        if (life_left(l) >= params_.min_life) continue;
        const AntView* waiting = nullptr;
        for (const AntView& a : v.mine()) {                                                         // the ant that waits: plain, empty-handed, on the ring half, nobody's but the economy's
            if ((a.type != v.default_ant_type() && a.type != sim::AntType::Fire && a.type != sim::AntType::Bomber) || a.holding || a.carried_points > 0 || a.hp < tactics_.plan.island_swarm_hp || !a.takes_orders()) continue;
            if (c.map.ant_component(c.seat, a.tile) != l.ring) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            if (waiting == nullptr || a.tile.chebyshev_dist(l.drop) < waiting->tile.chebyshev_dist(l.drop)) waiting = &a;
            if (!tactics_.plan.island_link_cost) break;
        }
        if (waiting == nullptr) continue;
        if (!tactics_.plan.island_link_cost) {                                                      // the first link of the list that has a waiting ant, as it was
            wanted = static_cast<int>(i);
            break;
        }
        // the cheapest one; a link that its digger is working on stays the one while an ant waits (a dig is not left half done for a dearer link)
        const int64_t cost = digger_ != 0 && digger_link_ == static_cast<int64_t>(i) ? std::numeric_limits<int64_t>::min() : link_cost(c, l, *waiting);
        if (wanted < 0 || cost < wanted_cost) {
            wanted = static_cast<int>(i);
            wanted_cost = cost;
        }
    }
    // the bridge for an ant that stands away from the hill: only when no ant waits for a link
    const std::vector<sim::TileCoord>* want_tiles = wanted >= 0 ? &tactics_.flower_links[static_cast<size_t>(wanted)].tiles : nullptr;
    int64_t want_key = wanted;
    int32_t want_kind = -1;
    const BridgeJob* job = nullptr;
    const bool jobs_on = tactics_.plan.island_return || tactics_.plan.island_feeder || tactics_.plan.island_power_bridge;
    const std::function<bool(sim::TileCoord)> unusable = [&](sim::TileCoord t) {
        const auto bad = bad_.find(index_of(grid, t));
        return bad != bad_.end() && bad->second > now;
    };
    if (wanted < 0 && jobs_on) {
        job = planner_.look(c, tactics_, islands_->info(), swimmers, unusable);
        if (job != nullptr) {
            want_tiles = &job->tiles;
            want_key = kJobKey + job->key();
            want_kind = static_cast<int32_t>(job->kind);
        }
    }
    if (jobs_on || tactics_.plan.island_link_cost) planner_.upkeep(c, islands_->info());                   // (with every switch at 0 the bot is the one before them: it walks nobody off a bridge)
    planner_.walk_home(c);
    if (digger_ != 0 && want_key != digger_link_) {                                                 // nobody waits any more (or another link): the Swimmer goes back to the ferry
        release_digger(c);
    }
    if (want_tiles == nullptr || v.ticks_left() < params_.min_ticks_left) return;
    // the next tile to dig
    sim::TileCoord target{-1, -1};
    bool busy = false;                                                                              // a stage of a dig (the neighbour's or ours) is on a tile: wait
    for (const sim::TileCoord t : *want_tiles) {
        if (grid.get_cell(t).has_completed_bridge()) continue;
        if (grid.get_cell(t).has_partial_bridge()) {
            busy = true;
            break;
        }
        if (!bridge_water(grid, t)) {
            busy = true;
            break;
        }
        const auto bad = bad_.find(index_of(grid, t));
        if (bad != bad_.end() && bad->second > now) {
            busy = true;
            break;
        }
        target = t;
        break;
    }
    if (target.x < 0 && !busy) return;                                                              // every tile stands
    if (digger_ == 0) {
        if (busy) return;
        const AntView* best = nearest_digger(c, target);
        if (best != nullptr && want_kind >= 0 && best->tile.chebyshev_dist(target) > static_cast<int32_t>(params_.job_max_swim)) return;   // (the swim would cost more than a bridge for an ant is worth)
        if (best == nullptr || !c.ledger.take(best->id, id())) return;
        digger_ = best->id;
        digger_link_ = want_key;
        digger_kind_ = want_kind;
        ordered_ = false;
        sent_ = kPending;
    }
    const AntView* d = find_ant(v.mine(), digger_);
    if (d == nullptr) return;
    if (d->state == sim::UnitState::BuildingBridge) return;                                         // a clip is running
    if (ordered_ && sent_ != kPending && grid.get_cell(ordered_tile_).has_completed_bridge()) {     // the tile is a bridge: the next one is ordered
        ordered_ = false;
        sent_ = kPending;
    }
    if (busy) {
        if (ordered_ && sent_ != kPending && now >= sent_ + params_.dig_ticks) {                    // our own dig that never ended: given up
            ++failures_;
            bad_[index_of(grid, ordered_tile_)] = now + params_.blacklist_ticks;
            release_digger(c);
        }
        return;
    }
    if (ordered_) {
        if (sent_ == kPending) {
            if (now >= decided_ + 200u) ordered_ = false;                                           // lost
            return;
        }
        if (d->state == sim::UnitState::CantGo && now >= sent_ + 8u) {                              // refused
            ++failures_;
            bad_[index_of(grid, ordered_tile_)] = now + params_.blacklist_ticks;
            release_digger(c);
            return;
        }
        if (!d->idle() || v.has_pending_path(digger_) || now < sent_ + 16u) return;                 // on its way, or digging
        ++failures_;                                                                                // idle and the tile is not finished
        bad_[index_of(grid, ordered_tile_)] = now + params_.blacklist_ticks;
        release_digger(c);
        return;
    }
    if (ant_on(v, target)) {                                                                        // no click names a tile with an ant on it
        if (digger_kind_ < 0) return;
        if (blocked_since_ == 0) blocked_since_ = now;
        if (now >= blocked_since_ + params_.blocked_ticks) {                                        // (a bridge job does not wait for ever: the tile is left alone for a while and the Swimmer goes back to the ferry)
            bad_[index_of(grid, target)] = now + params_.blacklist_ticks;
            blocked_since_ = 0;
            release_digger(c);
        }
        return;
    }
    blocked_since_ = 0;
    c.orders.special(digger_, target);
    ordered_ = true;
    ordered_tile_ = target;
    decided_ = now;
    sent_ = kPending;
    ++tiles_ordered_;
    if (digger_kind_ >= 0) planner_.note_ordered(static_cast<BridgeJob::Kind>(digger_kind_));
}

}  // namespace ants::ai
