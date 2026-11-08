#include "ants_ai/standard_tasks.hpp"
#include "standard_tasks_shared.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;

// ---- HatchTask ------------------------------------------------------------------------------------------------------------------------------------------

void HatchTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const Memory& m = tactics_.memory;
    if (start_ants_ == 0) start_ants_ = v.mine().size();
    if (!plan.hatches || v.eggs() == 0 || now < next_after_ || v.ticks_left() < plan.hatch_min_left) return;
    // a click that left the egg count where it was and started nothing was refused (the own score was below 200: an ally's points show in the box): leave it for a while
    if (clicked_ && v.eggs() >= eggs_before_ && !v.hatching()) {
        refused_until_ = now + 900u;
        clicked_ = false;
    } else if (clicked_) {
        clicked_ = false;
    }
    if (now < refused_until_) return;
    const bool allied = v.ally() < sim::MAX_PLAYERS;
    // the box shows the own score plus the ally's, the click looks at the own: with an ally ask for a margin
    const int32_t need = 200 + static_cast<int32_t>(plan.hatch_reserve) + (allied ? 400 : 0);
    if (v.score() < need) return;
    // a fight: several own ants hit within a short while, or one lost (a thief that raids the hill is no fight); and the strike force that the bot sent out
    const bool fight = m.hits_within(plan.hatch_window) >= plan.hatch_hits || (m.last_loss() != 0 && now <= m.last_loss() + plan.hatch_window);
    const bool squad = plan.hatch_for_squad && tactics_.harass_active;
    if (!(fight || tactics_.strike_active || squad)) return;
    const size_t want = std::max<size_t>(plan.hatch_floor, start_ants_ + (tactics_.strike_active || squad ? plan.hatch_extra : 0u));
    if (v.mine().size() + (v.hatching() ? 1u : 0u) >= want || v.hatching()) return;
    c.orders.hatch();
    ++hatches_ordered_;
    clicked_ = true;
    eggs_before_ = v.eggs();
    next_after_ = now + 70u;
}

// ---- GateTask -------------------------------------------------------------------------------------------------------------------------------------------

int GateTask::cost_of(const Geometry& g, sim::TileCoord t) const noexcept {
    if (t.x < 0 || t.y < 0 || t.x >= g.width || g.cost == nullptr) return -1;
    const size_t i = static_cast<size_t>(t.y) * static_cast<size_t>(g.width) + static_cast<size_t>(t.x);
    return i < g.cost->size() ? (*g.cost)[i] : -1;
}

bool GateTask::blocked(sim::TileCoord t, uint64_t tick) const noexcept {
    const auto it = blocked_.find(static_cast<int64_t>(t.y) * 4096 + t.x);
    return it != blocked_.end() && it->second > tick;
}

void GateTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if ((fate != Bot::Fate::Sent && fate != Bot::Fate::Filtered) || command.type != sim::CommandType::GroupMove) return;
    for (const uint32_t id : command.ants) {
        const auto it = cmd_.find(id);
        if (it == cmd_.end()) continue;
        Cmd& cm = it->second;
        if (cm.tile.x != command.tile_x || cm.tile.y != command.tile_y) continue;      // (an order of another task that this one did not give)
        if (fate == Bot::Fate::Filtered) {                                              // refused: no click onto that tile for a while, and the ant is taken over again at the next look
            blocked_[static_cast<int64_t>(cm.tile.y) * 4096 + cm.tile.x] = tick + params_.blocked_ticks;
            if (id == user_) user_ = 0;
            cmd_.erase(it);
            continue;
        }
        cm.release = tick;
        if (id == user_) user_release_ = tick;
    }
}

// The depositors leave through the queue-row tile that is cheapest toward the pile (the engine plans around idle ants, but a moving ant that arrives later is a head-on conflict): that
// tile and the lane behind it are kept free, the other two tiles are the staging slots (Q1 first, it is the nearest to the ramp). Seen from the looks: an empty ant that stands on a
// queue-row tile after the previous look saw it inside the mound has just left through that tile.
void GateTask::track_exits(TaskContext& c, const Geometry& g) {
    const BotView& v = c.view;
    bool changed = false;
    for (const AntView& a : v.mine()) {
        const int zone = in_mound(g, a.tile) ? 1 : 0;
        const auto it = prev_zone_.find(a.id);
        if (it != prev_zone_.end() && it->second == 1 && zone == 0 && !a.holding && in_queue_row(g, a.tile)) {
            exits_.push_back(a.tile.x - g.hill.x);
            if (exits_.size() > 12) exits_.pop_front();
            changed = true;
        }
        prev_zone_[a.id] = zone;
    }
    if (!changed && slot_pile_ != 0xFFFFFFFFu) return;
    slot_pile_ = 0;
    if (exits_.size() < 3) {                                                          // not enough exits seen yet: by the walking cost from the queue row to the nearest pile
        slot_pile_ = 0xFFFFFFFFu;
        choose_slots(c, g);
        slot_pile_ = 0;
        return;
    }
    int cnt[3] = {0, 0, 0};
    for (const int e : exits_) ++cnt[e];
    int best = 1;
    for (const int i : {1, 0, 2}) {
        if (cnt[i] > cnt[best]) best = i;
    }
    int n = 0;
    if (best != 1) slot_order_[n++] = 1;
    for (const int i : {0, 2}) {
        if (i != best) slot_order_[n++] = i;
    }
    while (n < 3) slot_order_[n++] = -1;
}

void GateTask::choose_slots(TaskContext& c, const Geometry& g) {
    const BotView& v = c.view;
    const PileView* pile = nullptr;
    int32_t pile_cost = 0;
    for (const PileView& p : v.piles()) {
        const PileInfo* info = c.map.pile(p.index);
        if (info == nullptr || !info->approach[c.seat].reachable()) continue;
        if (pile == nullptr || info->approach[c.seat].cost < pile_cost) {
            pile = &p;
            pile_cost = info->approach[c.seat].cost;
        }
    }
    if (pile == nullptr) return;
    if (pile->index == slot_pile_) return;
    slot_pile_ = pile->index;
    const std::vector<uint8_t> mask = MapInfo::walkable_mask(v.grid(), c.seat, v.walk_context());
    const std::vector<int32_t> f = MapInfo::cost_field_onto(v.grid(), mask, c.seat, pile->anchor, v.walk_context());
    int best = 1;
    int32_t best_cost = 1 << 30;
    for (const int i : {1, 0, 2}) {
        const sim::TileCoord q = queue_tile(g, i);
        const size_t idx = static_cast<size_t>(q.y) * static_cast<size_t>(g.width) + static_cast<size_t>(q.x);
        const int32_t cost = idx < f.size() ? f[idx] : -1;
        if (cost >= 0 && cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    int n = 0;
    if (best != 1) slot_order_[n++] = 1;
    for (const int i : {0, 2}) {
        if (i != best) slot_order_[n++] = i;
    }
    while (n < 3) slot_order_[n++] = -1;
}

void GateTask::refresh_now(TaskContext& c) {
    const BotView& v = c.view;
    const HillInfo& hill = c.map.hill(c.seat);
    const sim::Grid& grid = v.grid();
    // which of the eleven tiles round and on the queue row can be walked on: when one of them changed (a wall of fire lit or burnt out, a bomb) the gate may have been shut or opened
    uint32_t signature = 0;
    uint32_t bit = 1;
    for (const sim::TileCoord& t : SabotageTask::ring_of(hill)) {
        signature |= grid.in_bounds(t) && MapInfo::walkable(grid, c.seat, t, v.walk_context()) ? bit : 0u;
        bit <<= 1;
    }
    for (int32_t i = 0; i < 3; ++i) {
        const sim::TileCoord t{hill.origin.x + i, hill.origin.y - 1};
        signature |= grid.in_bounds(t) && MapInfo::walkable(grid, c.seat, t, v.walk_context()) ? bit : 0u;
        bit <<= 1;
    }
    const uint64_t now = v.tick();
    if (now_made_ && signature == now_signature_ && now < now_at_ + params_.field_ticks) return;
    now_ = c.map.field_now(grid, c.seat, v.walk_context());
    now_at_ = now;
    now_signature_ = signature;
    now_made_ = true;
}

void GateTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    usable_ = false;
    const HillInfo& hill = c.map.hill(c.seat);
    if (!hill.present || !v.has_grid()) return;
    Geometry g;
    g.hill = hill.origin;
    g.entrance = hill.entrance;
    g.width = c.map.width();
    g.cost = &c.map.cost_field_of(c.seat);
    if (g.cost->empty()) return;
    // the staging tiles must be ones that an ant can stand on (a rock or water on the queue row or four rows north of the ramp: another tile, or no gate guiding at that hill)
    const sim::Grid& grid = v.grid();
    for (auto it = blocked_.begin(); it != blocked_.end();) it = it->second <= now ? blocked_.erase(it) : std::next(it);
    int slots_walkable = 0;
    for (int i = 0; i < 3; ++i) {
        g.slot_ok[static_cast<size_t>(i)] = MapInfo::walkable(grid, c.seat, queue_tile(g, i), v.walk_context()) && v.powerup_at(queue_tile(g, i)) == nullptr && !blocked(queue_tile(g, i), now);
        slots_walkable += g.slot_ok[static_cast<size_t>(i)] ? 1 : 0;
    }
    bool buffer_found = false;
    for (int32_t dy = 0; dy <= 3 && !buffer_found; ++dy) {
        for (const int32_t dx : {0, -1, 1}) {
            const sim::TileCoord t{g.hill.x + 1 + dx, g.hill.y - params_.buffer_rows - (dy % 2 == 0 ? dy / 2 : -(dy + 1) / 2)};
            if (!grid.in_bounds(t) || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || v.powerup_at(t) != nullptr || cost_of(g, t) < 0 || blocked(t, now)) continue;
            g.buffer = t;
            buffer_found = true;
            break;
        }
    }
    if (slots_walkable < 2 || !buffer_found || blocked(g.entrance, now)) return;       // no room at the doorstep, or no click onto the entrance is accepted: the engine's flow stays
    // the doorstep must be joined to the hill as the map is NOW: a ring of fire walls round the gate (a sabotage) shuts the carriers out, and every click into the hill would be refused
    if (params_.cantgo_aware) {
        refresh_now(c);
        if (!c.map.reaches_hill(now_, g.buffer)) return;
    }
    usable_ = true;
    track_exits(c, g);

    // what the ants do: a bite that runs, the entrance occupied, a clip that was first seen now
    bool bite = false;
    bool entrance_occupied = false;
    bool ramp_standing = false;
    bool leaver_on_ramp = false;
    const sim::TileCoord ramp{g.hill.x + 1, g.hill.y};
    std::vector<const AntView*> carriers;
    for (const AntView& a : v.mine()) {
        if (a.state == sim::UnitState::HarvestingFood) bite = true;
        if (a.tile == g.entrance) entrance_occupied = true;
        if (params_.cantgo_aware && a.tile == ramp && a.state != sim::UnitState::Walking) ramp_standing = true;                          // an own ant that stands on the ramp shuts the way to the entrance
        if (params_.cantgo_aware && a.tile == ramp && !a.holding) leaver_on_ramp = true;                                                                         // an ant that has been in the hill still leaves over the ramp (the view draws it walking while the ants ahead of it hold it up)
        if (a.state == sim::UnitState::EnteringBase) {
            if (clip_seen_.count(a.id) == 0) {
                clip_seen_[a.id] = now;
                pending_free_at_ = static_cast<int64_t>(now) + params_.clip_ticks + params_.exit_ticks;
            }
        }
        if (a.holding && a.state != sim::UnitState::EnteringBase && a.tile != g.entrance && c.ledger.owner(a.id) != kHealTaskId && c.ledger.rank(c.ledger.owner(a.id)) < 6 && (!params_.cantgo_aware || c.map.reaches_hill(now_, a.tile, a.type))) carriers.push_back(&a);          // (a thief with loot banks at the entrance too; an ant that no walk joins to the hill is not ours to place)
    }
    if (!ramp_standing) ramp_since_ = -1;
    else if (ramp_since_ < 0) ramp_since_ = static_cast<int64_t>(now);
    const bool ramp_held = ramp_standing && static_cast<int64_t>(now) < ramp_since_ + static_cast<int64_t>(params_.ramp_wait_ticks);        // (an ant that stays longer is not waited for: nothing moves it)
    if (!leaver_on_ramp) leaver_since_ = -1;
    else if (leaver_since_ < 0) leaver_since_ = static_cast<int64_t>(now);
    const bool leaver_hold = leaver_on_ramp && static_cast<int64_t>(now) < leaver_since_ + static_cast<int64_t>(params_.leaver_wait_ticks);        // (a click while it is there meets it head on in the one-wide way out; one that stays longer is not waited for)
    // the ramp unjam (LevelPlan::ramp_unjam): the ramp has been held by ants that stand still on it for plan.ramp_unjam_ticks while a carrier is on its way home: an own ant without food that stands idle
    // on it (a Combat Ant that came home from a fight, a worker that was held up) shuts the way to the entrance for the carriers and nothing moves it: a person would step it aside. Only an ant that nobody
    // above the economy holds and that has no walk ordered is sent, to a free tile at the side of the doorstep (three to five tiles off the ramp)
    if (params_.unjam_ticks > 0 && ramp_standing && ramp_since_ >= 0 && static_cast<int64_t>(now) >= ramp_since_ + static_cast<int64_t>(params_.unjam_ticks) && now >= unjam_next_ && !carriers.empty()) {
        const AntView* mover = nullptr;
        for (const AntView& a : v.mine()) {
            if (a.tile != ramp || a.holding || a.carried_points > 0 || !a.idle() || v.has_pending_path(a.id)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) > 1) continue;
            mover = &a;
            break;
        }
        sim::TileCoord spot{-1, -1};
        if (mover != nullptr) {
            int32_t best_d = 0;
            for (const int32_t dx : {-3, 5, -4, 6, -2, 4}) {                              // beside the doorstep, two rows out from the queue row, west and east of the lane
                const sim::TileCoord t{g.hill.x + dx, g.hill.y - 2};
                if (!grid.in_bounds(t) || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || v.powerup_at(t) != nullptr || grid.has_fire_at(t) || cost_of(g, t) < 0 || blocked(t, now) || occupied(v, t)) continue;
                const int32_t d = t.chebyshev_dist(mover->tile);
                if (spot.x < 0 || d < best_d) {
                    spot = t;
                    best_d = d;
                }
            }
        }
        if (mover != nullptr && spot.x >= 0) {
            c.orders.move({mover->id}, spot, Priority::Urgent);
            unjam_next_ = now + 80;
            ++ramp_unjams_;
        }
    }
    for (auto it = cmd_.begin(); it != cmd_.end();) {                                // only carriers are ours to place
        bool carrier = false;
        for (const AntView* a : carriers) carrier = carrier || a->id == it->first;
        if (!carrier) it = cmd_.erase(it);
        else ++it;
    }
    for (auto it = clip_seen_.begin(); it != clip_seen_.end();) {
        bool in_clip = false;
        for (const AntView& a : v.mine()) in_clip = in_clip || (a.id == it->first && a.state == sim::UnitState::EnteringBase);
        if (!in_clip) it = clip_seen_.erase(it);
        else ++it;
    }
    const auto find_mine = [&](uint32_t id) -> const AntView* {
        for (const AntView& a : v.mine()) {
            if (a.id == id) return &a;
        }
        return nullptr;
    };

    // 1. the gate user: the ant that was clicked onto the entrance
    bool user_active = false;
    if (user_ != 0) {
        const AntView* ua = find_mine(user_);
        if (ua == nullptr || !ua->holding) {
            user_ = 0;
        } else if (ua->tile == g.entrance || ua->state == sim::UnitState::EnteringBase) {
            user_active = true;
            user_streak_ = 0;                                                       // the click delivered
        } else {
            user_active = true;
            if (user_release_ != 0) {
                const uint64_t age = now - user_release_;
                const int cost = cost_of(g, ua->tile);
                bool fail = false;
                if (age >= params_.fail_age && !in_mound(g, ua->tile) && cost >= user_cost0_ && user_cost0_ >= 0) fail = true;
                if (age >= 12 && !in_mound(g, ua->tile) && (ua->state == sim::UnitState::Idle || ua->state == sim::UnitState::CantGo) && !v.has_pending_path(ua->id)) fail = true;      // it stands: the click was refused
                if (age > static_cast<uint64_t>(user_eta0_ + 40)) fail = true;
                if (fail) {
                    ++user_failures_;
                    cmd_.erase(user_);
                    user_ = 0;
                    user_active = false;
                    ++user_streak_;
                    if (params_.user_fail_limit != 0 && user_streak_ >= params_.user_fail_limit) {            // the clicks deliver nothing: stop for a while (the carriers are boxed in)
                        blocked_[static_cast<int64_t>(g.entrance.y) * 4096 + g.entrance.x] = now + params_.blocked_ticks;
                        user_streak_ = 0;
                        ++pauses_;
                        usable_ = false;
                        return;
                    }
                }
            }
        }
    }
    if (!entrance_occupied) pending_free_at_ = 0;
    const bool gate_free = !entrance_occupied && !ramp_held && !user_active;
    const bool predicted = params_.predictive && pending_free_at_ > 0 && !gate_free && static_cast<int64_t>(now) + params_.latency_ticks >= pending_free_at_;

    // 2. the queue row as the engine counts it: occupied tiles (any own ant) and own orders onto its tiles that are still on their way
    bool q_occ[3] = {false, false, false};
    bool q_tgt[3] = {false, false, false};
    for (const AntView& a : v.mine()) {
        for (int i = 0; i < 3; ++i) {
            if (a.tile == queue_tile(g, i)) q_occ[i] = true;
        }
    }
    for (const AntView* a : carriers) {
        const auto it = cmd_.find(a->id);
        if (it == cmd_.end()) continue;
        if (a->state != sim::UnitState::Walking && it->second.release != 0) continue;     // only an ant that is still on its way (or whose order has not left yet) holds a slot
        for (int i = 0; i < 3; ++i) {
            if (it->second.tile == queue_tile(g, i) && a->tile != queue_tile(g, i) && a->id != user_) q_tgt[i] = true;
        }
    }
    int q_count = 0;
    for (int i = 0; i < 3; ++i) {
        if (q_occ[i] || q_tgt[i]) ++q_count;
    }

    // 3. the entrance click: the carrier that can be on the entrance soonest
    const AntView* best = nullptr;
    int best_key = 1 << 30;
    for (const AntView* a : carriers) {
        if (a->id == user_ && user_active) continue;
        const int cost = cost_of(g, a->tile);
        if (cost < 0) continue;
        int key = cost * 2 + (a->idle() ? 0 : 1);
        if (a->tile == queue_tile(g, 1)) key -= 1;                                    // straight in front of the ramp
        if (key < best_key) {
            best_key = key;
            best = a;
        }
    }
    uint32_t click = 0;
    if ((gate_free || predicted) && !leaver_hold && best != nullptr) {
        const bool hold = bite && bite_waited_ < params_.bite_wait_max;
        if (hold) ++bite_waited_;
        else {
            click = best->id;
            bite_waited_ = 0;
        }
    } else {
        bite_waited_ = 0;
    }

    // 4. how many carriers are at the doorstep or on their way to it (the parked ones are not)
    uint32_t staged = 0;
    for (const AntView* a : carriers) {
        if (a->id == user_ && user_active) continue;
        const auto it = cmd_.find(a->id);
        if (it != cmd_.end() && it->second.parked) continue;
        if (in_doorstep(g, a->tile)) {
            ++staged;
            continue;
        }
        if (it != cmd_.end() && it->second.decided && in_doorstep(g, it->second.tile)) ++staged;
    }

    // 5. carriers nobody has ordered: take them over (the engine's own order for a fresh carrier is the entrance or the far waiting tile)
    std::vector<uint32_t> to_buffer;
    std::vector<uint32_t> to_park;
    std::vector<std::pair<uint32_t, int>> to_slot;
    bool released_one = false;
    for (const AntView* a : carriers) {
        if (a->id == click) continue;
        if (a->id == user_ && user_active) continue;
        auto it = cmd_.find(a->id);
        const bool at_doorstep = in_doorstep(g, a->tile);
        if (it != cmd_.end() && it->second.parked) {
            if (!a->idle()) {                                                         // it moves again (a hit, a push): start over
                cmd_.erase(it);
                continue;
            }
            if (released_one || staged + 1 >= params_.max_staged) continue;          // stays parked
            cmd_.erase(it);                                                           // room at the doorstep: this one is released
            released_one = true;
            ++staged;
            to_buffer.push_back(a->id);
            continue;
        }
        if (it != cmd_.end()) {
            Cmd& cm = it->second;
            if (!cm.decided || cm.release == 0) continue;                             // decided, not released yet
            if (a->state == sim::UnitState::Walking) continue;                        // on its way
            if (a->idle() && at_doorstep) {                                           // staged: remember where it really stands
                cm.tile = a->tile;
                continue;
            }
            if (now < cm.release + 24) continue;                                      // a young order: its path is not delivered yet
            cmd_.erase(it);                                                           // stopped somewhere else: take it over again
        }
        if (a->idle() && at_doorstep) {
            Cmd& cm = cmd_[a->id];
            cm.tile = a->tile;
            cm.release = now;
            cm.decided = true;
            continue;
        }
        if (staged >= params_.max_staged) {                                           // the doorstep is full: it stands where it is
            to_park.push_back(a->id);
            continue;
        }
        ++staged;
        ++takeovers_;
        int slot = -1;                                                                // a free queue-row slot if there is one, else the buffer
        if (q_count < static_cast<int>(params_.slots)) {
            for (int k = 0; k < 3; ++k) {
                const int pref = slot_order_[k];
                if (pref >= 0 && g.slot_ok[static_cast<size_t>(pref)] && !q_occ[pref] && !q_tgt[pref]) {
                    slot = pref;
                    break;
                }
            }
        }
        if (slot >= 0) {
            to_slot.emplace_back(a->id, slot);
            q_tgt[slot] = true;
            ++q_count;
        } else {
            to_buffer.push_back(a->id);
        }
    }

    // 6. what is sent: the takeovers first (they cancel the claims of fresh carriers), then the entrance click
    const sim::TileCoord buffer = g.buffer;
    if (!to_park.empty()) {
        click = 0;                                                                    // a Stop is a Normal-priority command and would leave after an Urgent click: the click waits for the next look
        c.orders.stop(to_park);
        parks_ += static_cast<uint32_t>(to_park.size());
        for (const uint32_t id : to_park) {
            Cmd& cm = cmd_[id];
            cm.tile = sim::TileCoord{-1, -1};
            cm.release = now;
            cm.decided = true;
            cm.parked = true;
        }
    }
    if (!to_buffer.empty()) {
        c.orders.move(to_buffer, buffer, Priority::Urgent);
        for (const uint32_t id : to_buffer) {
            Cmd& cm = cmd_[id];
            cm.tile = buffer;
            cm.release = 0;
            cm.decided = true;
        }
    }
    for (const auto& p : to_slot) {
        c.orders.move({p.first}, queue_tile(g, p.second), Priority::Urgent);
        Cmd& cm = cmd_[p.first];
        cm.tile = queue_tile(g, p.second);
        cm.release = 0;
        cm.decided = true;
    }
    if (click != 0 && best != nullptr) {
        c.orders.move({click}, g.entrance, Priority::Urgent);
        ++entrance_clicks_;
        if (predicted) pending_free_at_ = 0;
        user_ = click;
        user_release_ = 0;
        user_cost0_ = cost_of(g, best->tile);
        user_eta0_ = MapInfo::walking_ticks(user_cost0_) + 25;
        Cmd& cm = cmd_[click];
        cm.tile = g.entrance;
        cm.release = 0;
        cm.decided = true;
    }

    // 7. keep the queue row filled: an idle carrier at the doorstep that is not on the row goes to a free slot
    if (click == 0 && to_slot.empty() && q_count < static_cast<int>(params_.slots)) {
        int slot = -1;
        for (int k = 0; k < 3; ++k) {
            const int pref = slot_order_[k];
            if (pref >= 0 && g.slot_ok[static_cast<size_t>(pref)] && !q_occ[pref] && !q_tgt[pref]) {
                slot = pref;
                break;
            }
        }
        if (slot >= 0) {
            const AntView* pick = nullptr;
            int pick_cost = 1 << 30;
            for (const AntView* a : carriers) {
                if (a->id == user_ && user_active) continue;
                if (!a->idle() || !in_doorstep(g, a->tile) || in_queue_row(g, a->tile)) continue;
                const int cost = cost_of(g, a->tile);
                if (cost >= 0 && cost < pick_cost) {
                    pick_cost = cost;
                    pick = a;
                }
            }
            if (pick != nullptr) {
                c.orders.move({pick->id}, queue_tile(g, slot), Priority::Urgent);
                Cmd& cm = cmd_[pick->id];
                cm.tile = queue_tile(g, slot);
                cm.release = 0;
                cm.decided = true;
            }
        }
    }
}

// ---- GuardTask ------------------------------------------------------------------------------------------------------------------------------------------

sim::TileCoord GuardTask::post_of(const HillInfo& hill, size_t n) noexcept {
    switch (n) {
        case 0: return sim::TileCoord{hill.origin.x + 5, hill.origin.y + 2};          // east of the walls: its square holds the three tiles in front of the thief hole
        case 1: return sim::TileCoord{hill.origin.x + 1, hill.origin.y - 3};          // north of the gate: its square holds the queue row
        default: return sim::TileCoord{hill.origin.x + 5, hill.origin.y + 5};         // south east
    }
}

void GuardTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove) return;
    for (const uint32_t ant : command.ants) {
        const auto it = posts_.find(ant);
        if (it == posts_.end() || it->second.sent != kPending) continue;
        if (fate == Bot::Fate::Sent) it->second.sent = tick;
        else it->second.decided = 0;                                                    // it never left: the ant is ordered again
    }
}

void GuardTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    if (!hill.present || !v.has_grid()) return;
    const sim::Grid& grid = v.grid();
    for (auto it = posts_.begin(); it != posts_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || a->type != sim::AntType::Combat || c.ledger.owner(it->first) != id()) it = posts_.erase(it);
        else ++it;
    }
    size_t n = 0;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Combat) continue;
        if (c.ledger.owner(a.id) != id() && !c.ledger.take(a.id, id())) continue;
        const size_t index = n++;
        auto it = posts_.find(a.id);
        if (it == posts_.end()) {
            Post p;
            p.tile = post_of(hill, index);
            // a post must be a tile an ant can stand on (walkable() leaves out every solid object, a power-up too: a click on one takes it)
            for (int32_t k = 0; k < 4 && !MapInfo::walkable(grid, c.seat, p.tile, v.walk_context()); ++k) p.tile = sim::TileCoord{p.tile.x + (k % 2 == 0 ? 1 : -1) * (k / 2 + 1), p.tile.y};
            it = posts_.emplace(a.id, p).first;
        }
        Post& p = it->second;
        if (a.tile.chebyshev_dist(p.tile) <= 2) continue;                              // on its post: no order (an order restarts the two seconds of the reflex)
        if (!a.takes_orders() || a.state == sim::UnitState::Stunned) continue;
        if (p.sent != kPending && now < p.sent + 400u && !a.idle()) continue;           // on its way
        if (p.decided != 0 && p.sent == kPending && now < p.decided + 150u) continue;   // the order has not left yet
        if (!MapInfo::walkable(grid, c.seat, p.tile, v.walk_context())) continue;
        c.orders.move({a.id}, p.tile, Priority::Normal);
        p.decided = now;
        p.sent = kPending;
        ++posted_;
    }
}

// ---- CarrierAidTask ---------------------------------------------------------------------------------------------------------------------------------------

void CarrierAidTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (fate == Bot::Fate::Filtered && command.type == sim::CommandType::GroupMove && home_.x >= 0 && command.tile_x == home_.x && command.tile_y == home_.y) blocked_until_ = tick + 900u;
}

void CarrierAidTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    if (!tactics_.plan.carrier_aid || !hill.present) return;
    for (const Hit& h : tactics_.memory.hits()) {
        if (h.carried && h.hp_now > 0) aid_[h.ant] = Aid{now, 0, 0};
    }
    std::vector<uint32_t> home;
    MapInfo::NowField joined;                                                            // the walking field of the hill as it is now, made at the first carrier that is due an order
    for (auto it = aid_.begin(); it != aid_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool carrying = a != nullptr && (a->holding || a->carried_points > 0);
        if (a == nullptr || !carrying || it->second.tries >= 4 || now > it->second.hit + 800 || c.ledger.owner(it->first) == kHealTaskId) {
            it = aid_.erase(it);                                                         // gone, delivered (or dropped its food), helped as often as it makes sense, or held by the healing task (its click is the same)
            continue;
        }
        // the blow's stun lasts about 12 ticks; an ant that is idle after it has lost its walk
        if (now >= blocked_until_ && a->idle() && now >= it->second.hit + 8 && now >= it->second.last_order + 40 && !v.has_pending_path(a->id)) {
            it->second.last_order = now;
            if (tactics_.plan.cantgo_aware && v.has_grid()) {                            // a carrier that no walk joins to the hill (a ring of fire walls round its gate) is not sent: it would be refused, and no try is spent on it
                if (!joined.valid()) joined = c.map.field_now(v.grid(), c.seat, v.walk_context());
                if (!c.map.reaches_hill(joined, a->tile, a->type)) {
                    ++it;
                    continue;
                }
            }
            home.push_back(a->id);
            ++it->second.tries;
        }
        ++it;
    }
    if (!home.empty()) {
        c.orders.move(home, hill.entrance, Priority::Normal);
        home_ = hill.entrance;
        sent_home_ += static_cast<uint32_t>(home.size());
    }
}

}  // namespace ants::ai
