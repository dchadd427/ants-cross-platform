#include "ants_ai/island_link.hpp"

#include <algorithm>

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
    ordered_ = false;
    sent_ = kPending;
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
        const Channel* best = nullptr;
        int32_t strip = -1;
        for (const Channel& ch : info.channels()) {
            if (ch.a != ring.id && ch.b != ring.id) continue;
            const int32_t other = ch.a == ring.id ? ch.b : ch.a;
            const IslandComponent* s = info.component(other);
            if (s == nullptr || s->hill || s->flowers.empty() || ch.length() < 1 || ch.length() > static_cast<int32_t>(params_.max_tiles)) continue;
            if (best == nullptr || ch.length() < best->length() || (ch.length() == best->length() && other < strip)) {
                best = &ch;
                strip = other;
            }
        }
        if (best == nullptr) continue;
        Tactics::FlowerLink link;
        link.ring = ring.id;
        link.strip = strip;
        link.drop = flower_drop_tile(info.component(strip)->flowers.front());
        link.tiles = best->tiles;
        if (best->a != ring.id) std::reverse(link.tiles.begin(), link.tiles.end());
        tactics_.flower_links.push_back(std::move(link));
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
    // nothing is dug for ants that would take nothing over there: the bot has the Swimmers that it wants (and no Thief to take, when the plan takes one)
    uint32_t swimmers = 0;
    bool thief = false;
    for (const AntView& a : v.mine()) {
        swimmers += a.type == sim::AntType::Swimmer ? 1u : 0u;
        thief = thief || a.type == sim::AntType::Thief;
    }
    const bool lacking = swimmers < tactics_.plan.island_swimmers + tactics_.plan.island_flower_swimmers || (tactics_.plan.island_flower_thief && !thief);
    int wanted = -1;                                                                                // the link that a free plain ant waits to cross
    for (size_t i = 0; i < tactics_.flower_links.size() && lacking; ++i) {
        const Tactics::FlowerLink& l = tactics_.flower_links[i];
        if (life_left(l) >= params_.min_life) continue;
        for (const AntView& a : v.mine()) {                                                         // the ant that waits: plain, empty-handed, on the ring half, nobody's but the economy's
            if ((a.type != v.default_ant_type() && a.type != sim::AntType::Fire) || a.holding || a.carried_points > 0 || a.hp < 5 || !a.takes_orders()) continue;
            if (c.map.ant_component(c.seat, a.tile) != l.ring) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            wanted = static_cast<int>(i);
            break;
        }
        if (wanted >= 0) break;
    }
    if (digger_ != 0 && wanted != digger_link_) {                                                   // nobody waits any more (or another link): the Swimmer goes back to the ferry
        release_digger(c);
    }
    if (wanted < 0 || v.ticks_left() < params_.min_ticks_left) return;
    const Tactics::FlowerLink& link = tactics_.flower_links[static_cast<size_t>(wanted)];
    // the next tile to dig
    sim::TileCoord target{-1, -1};
    bool busy = false;                                                                              // a stage of a dig (the neighbour's or ours) is on a tile: wait
    for (const sim::TileCoord t : link.tiles) {
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
        const AntView* best = nullptr;
        int32_t best_dist = 0;
        for (const AntView& a : v.mine()) {
            if (a.type != sim::AntType::Swimmer || a.holding || a.carried_points > 0 || a.hp < 5 || !a.takes_orders()) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            const int32_t d = a.tile.chebyshev_dist(target);
            if (best == nullptr || d < best_dist) {
                best = &a;
                best_dist = d;
            }
        }
        if (best == nullptr || !c.ledger.take(best->id, id())) return;
        digger_ = best->id;
        digger_link_ = wanted;
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
    if (ant_on(v, target)) return;                                                                  // no click names a tile with an ant on it
    c.orders.special(digger_, target);
    ordered_ = true;
    ordered_tile_ = target;
    decided_ = now;
    sent_ = kPending;
    ++tiles_ordered_;
}

}  // namespace ants::ai
