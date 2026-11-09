#include "ants_ai/standard_tasks.hpp"
#include "standard_tasks_shared.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;

// ---- SabotageTask ---------------------------------------------------------------------------------------------------------------------------------------

std::array<sim::TileCoord, 8> SabotageTask::ring_of(const HillInfo& hill) noexcept {
    const int32_t x = hill.origin.x;
    const int32_t y = hill.origin.y;
    return {sim::TileCoord{x + 1, y - 2}, sim::TileCoord{x, y - 2}, sim::TileCoord{x + 2, y - 2}, sim::TileCoord{x - 1, y - 2}, sim::TileCoord{x + 3, y - 2},
            sim::TileCoord{x - 1, y - 1}, sim::TileCoord{x + 3, y - 1}, sim::TileCoord{x - 1, y}};
}

void SabotageTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupSpecial || command.ants.size() != 1 || command.ants[0] != ant_ || job_tile_.x != command.tile_x || job_tile_.y != command.tile_y) return;
    if (job_sent_ != kPending) return;
    if (fate == Bot::Fate::Sent) job_sent_ = tick;
    else job_tile_ = sim::TileCoord{-1, -1};
}

void SabotageTask::release_escorts(AntLedger& ledger) {
    for (const uint32_t id : escorts_) {
        if (ledger.owner(id) == this->id()) ledger.release(id, this->id());
    }
    escorts_.clear();
    escort_since_ = 0;
}

void SabotageTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const auto give_back = [&]() {
        if (ant_ != 0 && c.ledger.owner(ant_) == id()) c.ledger.release(ant_, id());
        working_ = false;
        job_tile_ = sim::TileCoord{-1, -1};
    };
    const uint64_t light_from = plan.behind_war && tactics_.standing.war >= 1 ? std::min<uint64_t>(plan.sabotage_after, plan.behind_sabotage_after) : plan.sabotage_after;       // (a bot that is behind lights sooner)
    if (!plan.sabotage || !v.has_grid() || now < light_from || v.ticks_left() < 900 || tactics_.guard_stance) {
        give_back();
        release_escorts(c.ledger);
        return;
    }
    const sim::Grid& grid = v.grid();
    // the victim: the best opponent by the score boxes (not an ally) that has points to lose (and, with plan.sabotage_safe, whose walls were not put out too often lately)
    int victim = -1;
    int32_t best = -1;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || !c.map.hill(t).present) continue;
        if ((plan.sabotage_safe && giveup_until_[t] > now) || (plan.chore_worth && !fire_in_worth_it(v, c.map, t))) continue;       // (chore_worth: a ring round a gate that has nothing to bank costs its owner nothing)
        if (row.score >= static_cast<int32_t>(plan.sabotage_min_score) && row.score > best) {
            best = row.score;
            victim = t;
        }
    }
    target_ = victim;
    if (victim < 0) {
        give_back();
        release_escorts(c.ledger);
        return;
    }
    // the tiles of the ring that can still be lit
    const HillInfo& foe_hill = c.map.hill(static_cast<uint8_t>(victim));
    const std::array<sim::TileCoord, 8> ring = ring_of(foe_hill);
    std::vector<sim::TileCoord> open;
    for (const sim::TileCoord& t : ring) {
        if (!grid.in_bounds(t) || v.powerup_at(t) != nullptr) continue;
        if (classify_tile(grid, t) != EastTile::Open) continue;                           // lit already, a rock, a pile, mud (no wall can stand on it)
        open.push_back(t);
    }
    if (plan.sabotage_safe) {
        // A wall that was burning and is gone long before it could burn out was put out: twice, and the hill is left alone for a while (a Fire Ant is not wasted again and again)
        for (auto it = lit_order_.begin(); it != lit_order_.end();) {
            if (now > it->second + 3700u) {
                burning_.erase(it->first);
                it = lit_order_.erase(it);
            } else {
                ++it;
            }
        }
        for (const sim::TileCoord& t : ring) {
            const int64_t key = static_cast<int64_t>(t.y) * 4096 + t.x;
            const auto lo = lit_order_.find(key);
            if (lo == lit_order_.end() || !grid.in_bounds(t)) continue;
            if (grid.has_fire_at(t)) {
                burning_[key] = true;
            } else if (burning_.count(key) != 0) {
                if (now < lo->second + 3500u) {
                    ++put_out_;
                    if (++putouts_[static_cast<size_t>(victim)] >= plan.sabotage_putout_limit) {
                        giveup_until_[static_cast<size_t>(victim)] = now + plan.sabotage_giveup_ticks;
                        putouts_[static_cast<size_t>(victim)] = 0;
                    }
                }
                burning_.erase(key);
                lit_order_.erase(lo);
            }
        }
        if (giveup_until_[static_cast<size_t>(victim)] > now) {
            give_back();
            release_escorts(c.ledger);
            return;
        }
    }
    // A lone Fire Ant is no fire-in against an enemy that can simply put the fire out: an enemy Fire Ant of the hill's team or its ally that is in sight, or a Fire power-up that the enemy can still
    // take. Then the Fire Ant goes only with Combat Ants that stay at the entrance and kill what comes to put the fire out (plan.sabotage_escort of them).
    bool can_put_out = false;
    if (plan.sabotage_safe) {
        const uint8_t foe_ally = v.rows()[static_cast<size_t>(victim)].ally;
        for (const AntView& e : v.others()) {
            if (e.type == sim::AntType::Fire && (e.team == victim || (foe_ally < sim::MAX_PLAYERS && e.team == foe_ally))) can_put_out = true;
        }
        for (const PowerUpView& p : v.powerups()) {
            if (p.kind != sim::AntType::Fire || (p.standing_ant != 0 && p.standing_team == c.seat)) continue;          // (one that an ant of the bot stands on is the bot's)
            const PowerUpInfo* info = nullptr;
            for (const PowerUpInfo& i : c.map.powerups()) {
                if (i.tile == p.tile) info = &i;
            }
            if (info == nullptr || info->approach[static_cast<size_t>(victim)].reachable() || (foe_ally < sim::MAX_PLAYERS && info->approach[foe_ally].reachable())) can_put_out = true;       // (a drop of a flower dropper is in no analysis: anybody may reach it)
        }
    }
    const sim::TileCoord station = foe_hill.origin.y >= 5 ? sim::TileCoord{foe_hill.origin.x + 1, foe_hill.origin.y - 4} : sim::TileCoord{foe_hill.origin.x + 1, foe_hill.origin.y + 6};
    // the entrance must be a tile that the escorts can stand on and walk to (on a rock, on an island or on a power-up they would wait for ever, or take the power-up)
    const bool station_ok = grid.in_bounds(station) && MapInfo::walkable(grid, c.seat, station, v.walk_context()) && v.powerup_at(station) == nullptr && reachable_by(c.map, c.seat, station);
    // The ring stands when no tile of it can be lit any more, whoever lit it: ring_done_ is the tick it was first seen so, and is cleared the moment a wall has to be lit again
    if (open.empty()) {
        if (ring_done_ == 0) ring_done_ = now;
    } else {
        ring_done_ = 0;
    }
    // Nobody is called to an enemy gate without a Fire Ant to light it (the one that this task has, or a free one: the choice below, the hands apart) and without a wall to light: a ring that stands
    // is no work. The escorts that were called while it was lit are held for plan.sabotage_escort_ticks after it stood and then let go (and not called again until a wall has to be renewed)
    bool fire_ant_ready = false;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Fire || a.hp < 5 || (a.id == tactics_.wall_keeper && plan.sabotage_spare_keeper)) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        fire_ant_ready = true;
    }
    const bool lighting = !open.empty() && fire_ant_ready;
    const bool holding = open.empty() && !escorts_.empty() && now < ring_done_ + plan.sabotage_escort_ticks;
    const bool escorts_wanted = plan.sabotage_safe && can_put_out && (lighting || holding);
    bool escorted = true;
    if (escorts_wanted) {
        for (auto it = escorts_.begin(); it != escorts_.end();) {                         // the escorts that are still ours and fit
            const AntView* e = find_ant(v.mine(), *it);
            if (e == nullptr || c.ledger.owner(*it) != id() || e->hp < 5) {
                if (e != nullptr && c.ledger.owner(*it) == id()) c.ledger.release(*it, id());
                it = escorts_.erase(it);
            } else {
                ++it;
            }
        }
        if (lighting && station_ok && escorts_.size() < plan.sabotage_escort) {           // more: the Combat Ants that no task of a higher rank holds, the nearest to the hill first
            std::vector<const AntView*> cands;
            for (const AntView& a : v.mine()) {
                if (a.type != sim::AntType::Combat || escorts_.count(a.id) != 0 || !can_fight(v, a) || a.hp < 5) continue;
                const TaskId owner = c.ledger.owner(a.id);
                if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
                cands.push_back(&a);
            }
            std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) { return x->tile.chebyshev_dist(station) < y->tile.chebyshev_dist(station); });
            for (const AntView* a : cands) {
                if (escorts_.size() >= plan.sabotage_escort) break;
                if (c.ledger.take(a->id, id())) {
                    escorts_.insert(a->id);
                    ++escorts_called_;
                }
            }
        }
        if (lighting && (!station_ok || escorts_.size() < plan.sabotage_escort)) {        // no entrance to hold, or no force to hold it: no fire-in
            ++refused_safe_;
            give_back();
            release_escorts(c.ledger);
            return;
        }
        for (const uint32_t id_e : escorts_) {                                            // the escorts go to the entrance and wait there (their reflex punches what comes within three tiles)
            const AntView* e = find_ant(v.mine(), id_e);
            if (e == nullptr) continue;
            if (e->tile.chebyshev_dist(station) > 2) {
                escorted = false;
                if (e->takes_orders() && (e->idle() || e->state == sim::UnitState::Walking) && !v.has_pending_path(id_e)) {
                    const auto last = escort_order_.find(id_e);
                    if (last == escort_order_.end() || now >= last->second + 160u) {
                        c.orders.move({id_e}, station, Priority::Normal);
                        escort_order_[id_e] = now;
                    }
                }
            }
        }
        // the first wall waits for the escorts, but not for ever: escorts that no walk gets to the entrance within plan.sabotage_escort_wait end the attempt at that team for a while
        if (lighting && !escorted) {
            if (escort_since_ == 0) escort_since_ = now;
            if (now >= escort_since_ + plan.sabotage_escort_wait) {
                ++escort_timeouts_;
                giveup_until_[static_cast<size_t>(victim)] = now + plan.sabotage_giveup_ticks;
                give_back();
                release_escorts(c.ledger);
                return;
            }
        } else {
            escort_since_ = 0;
        }
    } else if (!escorts_.empty()) {
        release_escorts(c.ledger);
    } else {
        escort_since_ = 0;
    }
    // the ant: an own Fire Ant that is free (or has a task of lower rank), healthy, with empty hands
    const AntView* ant = ant_ != 0 ? find_ant(v.mine(), ant_) : nullptr;
    if (ant != nullptr && ant->id == tactics_.wall_keeper && plan.sabotage_spare_keeper) {
        give_back();                                                                        // the walls need this ant (the other one died): it is theirs
        ant_ = 0;
        ant = nullptr;
    }
    if (ant != nullptr && (ant->type != sim::AntType::Fire || (c.ledger.owner(ant_) != id() && c.ledger.owner(ant_) != kNoTask && c.ledger.rank(c.ledger.owner(ant_)) >= c.ledger.rank(id())))) ant = nullptr;
    if (ant == nullptr) {
        ant_ = 0;
        working_ = false;
        for (const AntView& a : v.mine()) {
            if (a.type != sim::AntType::Fire || a.hp < 5 || a.holding || a.carried_points > 0 || !a.takes_orders()) continue;
            if (a.id == tactics_.wall_keeper && plan.sabotage_spare_keeper) continue;                            // the Fire Ant of the own walls is not for the sabotage (a second one, stolen, is)
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;      // the own walls, a pick-up, a fight have it
            if (open.empty()) continue;
            ant = &a;
            break;
        }
        if (ant == nullptr) return;
        ant_ = ant->id;
    }
    if (open.empty()) {                                                                    // the ring stands: the ant goes back to the economy, the escorts stay a while (holding)
        give_back();
        return;
    }
    if (c.ledger.owner(ant_) != id()) {
        if (ant->holding || ant->carried_points > 0 || !ant->takes_orders() || !c.ledger.take(ant_, id())) return;
        working_ = true;
    }
    // what became of the last order
    if (job_tile_.x >= 0) {
        if (classify_tile(grid, job_tile_) != EastTile::Open) {
            job_tile_ = sim::TileCoord{-1, -1};
        } else if (job_sent_ != kPending && now >= job_sent_ + 140u && ant->idle()) {
            ordered_[static_cast<int64_t>(job_tile_.y) * 4096 + job_tile_.x] = now + 300u;       // the order did not light it: leave that tile alone for a while
            job_tile_ = sim::TileCoord{-1, -1};
        } else if (job_sent_ == kPending && now > job_decided_ + 200u) {
            job_tile_ = sim::TileCoord{-1, -1};
        } else {
            return;
        }
    }
    if (!ant->takes_orders() || ant->state == sim::UnitState::Stunned || !ant->idle()) return;
    if (!escorted) return;                                                                 // the escorts are on their way: the first wall waits for them
    // the nearest tile of the ring that has not been ordered lately and that the cursor accepts
    std::stable_sort(open.begin(), open.end(), [&](const sim::TileCoord& x, const sim::TileCoord& y) { return ant->tile.chebyshev_dist(x) < ant->tile.chebyshev_dist(y); });
    for (const sim::TileCoord& t : open) {
        const int64_t key = static_cast<int64_t>(t.y) * 4096 + t.x;
        const auto o = ordered_.find(key);
        if (o != ordered_.end() && now < o->second) continue;
        if (ant->tile == t) continue;
        if (occupied(v, t)) {                                                              // an ant stands there: nothing can be lit under it, and no click names it
            ordered_[key] = now + 20u;
            continue;
        }
        sim::Command probe;
        probe.type = sim::CommandType::GroupSpecial;
        probe.tile_x = static_cast<int16_t>(t.x);
        probe.tile_y = static_cast<int16_t>(t.y);
        probe.ants = {ant_};
        if (v.predict_ack(probe) == 0) {
            ordered_[key] = now + 60u;
            continue;
        }
        c.orders.special(ant_, t, Priority::Normal);
        job_tile_ = t;
        job_decided_ = now;
        job_sent_ = kPending;
        ordered_[key] = now + 150u;
        if (plan.sabotage_safe) {
            lit_order_[key] = now;                                                         // (the wall is seen burning after this tick; if it is gone long before 3,600 ticks are over it was put out)
            burning_.erase(key);
        }
        ++walls_ordered_;
        working_ = true;
        return;
    }
}

// ---- HarassTask -----------------------------------------------------------------------------------------------------------------------------------------

void HarassTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupAttack && command.type != sim::CommandType::GroupMove) return;
    for (const uint32_t ant : command.ants) {
        const auto it = squad_.find(ant);
        if (it == squad_.end() || it->second.sent != kPending) continue;
        if (fate == Bot::Fate::Sent) it->second.sent = tick;
        else it->second.ordered = false;
    }
}

void HarassTask::disband(TaskContext& c) {
    for (const auto& m : squad_) c.ledger.release(m.first, id());
    squad_.clear();
    hunting_ = false;
    tactics_.harass_active = false;
}

uint32_t HarassTask::enemy_weight(const AntView& e, uint64_t now) const {
    (void)now;
    if (e.type == sim::AntType::Combat) return 8u;
    return tactics_.memory.fought_lately(e.id, 600) || e.state == sim::UnitState::Attacking ? 4u : tactics_.plan.harass_idle_weight;
}

void HarassTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    tactics_.harass_active = false;
    hunting_ = false;
    if (!plan.harass || !v.has_grid() || v.ticks_left() < plan.harass_stop_ticks || tactics_.guard_stance) {
        if (!squad_.empty()) disband(c);
        return;
    }
    const sim::Grid& grid = v.grid();
    for (auto it = shunned_.begin(); it != shunned_.end();) it = it->second <= now ? shunned_.erase(it) : std::next(it);

    // 1. the members that are still ours and fit; one that was hurt badly (or lost) in a fight makes the squad leave the team of the ants next to it alone for a while
    for (auto it = squad_.begin(); it != squad_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (plan.harass_retreat_hp > 0 && (a == nullptr || a->hp < plan.harass_retreat_hp) && (a == nullptr || c.ledger.owner(it->first) == id())) {
            const sim::TileCoord where = a != nullptr ? a->tile : it->second.tile;
            int team = -1;
            int32_t best_d = 4;
            for (const AntView& e : v.others()) {
                const int32_t d = e.tile.chebyshev_dist(where);
                if (d < best_d && e.type != sim::AntType::Thief && !(v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) {
                    best_d = d;
                    team = e.team;
                }
            }
            if (team >= 0 && pause_until_[static_cast<size_t>(team)] <= now) {
                pause_until_[static_cast<size_t>(team)] = now + (static_cast<uint64_t>(plan.harass_pause_ticks) << std::min<uint32_t>(pause_count_[static_cast<size_t>(team)]++, 4u));
                ++pauses_;
            }
        }
        if (a != nullptr) it->second.tile = a->tile;
        const bool gone = a == nullptr || c.ledger.owner(it->first) != id() || a->hp < std::max(plan.harass_min_hp, plan.harass_retreat_hp) || a->holding || a->carried_points > 0;
        if (gone) {
            if (a != nullptr && c.ledger.owner(it->first) == id()) c.ledger.release(it->first, id());
            it = squad_.erase(it);
        } else {
            ++it;
        }
    }

    // 1b. a team that answers the squad with several ants at once is left alone: the ants of a team that were drawn in their attack clip lately within eight tiles of a member
    if (plan.harass_strong_defence > 0) {
        std::array<std::set<uint32_t>, sim::MAX_PLAYERS> answering;
        for (const auto& m : squad_) {
            const AntView* a = find_ant(v.mine(), m.first);
            if (a == nullptr) continue;
            for (const AntView& e : v.others()) {
                if (e.type == sim::AntType::Thief || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) || e.tile.chebyshev_dist(a->tile) > 8) continue;
                if (tactics_.memory.fought_lately(e.id, 150) || e.state == sim::UnitState::Attacking) answering[e.team].insert(e.id);
            }
        }
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            if (answering[t].size() >= plan.harass_strong_defence && pause_until_[t] <= now) {
                pause_until_[t] = now + (static_cast<uint64_t>(plan.harass_pause_ticks) << std::min<uint32_t>(pause_count_[t]++, 4u));
                ++pauses_;
            }
        }
    }

    // 2. the targets: carriers of the other teams that an attack order can reach, with what makes one worth the trip
    int leader = -1;
    int32_t leader_score = -1;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally())) continue;
        if (row.score > leader_score) {
            leader = t;
            leader_score = row.score;
        }
    }
    struct Target {
        const AntView* e{nullptr};
        int32_t value{0};
        uint32_t enemy{0};          // the enemy strength near it
    };
    std::vector<Target> targets;
    for (const AntView& e : v.others()) {
        if (!e.holding || !attackable(v, e) || pause_until_[e.team] > now) continue;
        const auto sh = shunned_.find(e.id);
        if (sh != shunned_.end() && sh->second > now) continue;
        Target t;
        t.e = &e;
        for (const AntView& o : v.others()) {
            if (o.id == e.id || (v.ally() < sim::MAX_PLAYERS && o.team == v.ally())) continue;
            if (o.tile.chebyshev_dist(e.tile) <= plan.harass_near && !o.holding) t.enemy += enemy_weight(o, now);
        }
        int32_t value = 100;
        if (e.team == leader) value += static_cast<int32_t>(plan.harass_leader_bonus);
        const HillInfo& hill = c.map.hill(e.team);
        if (hill.present) value += static_cast<int32_t>(plan.harass_far_bonus) * std::min<int32_t>(25, e.tile.chebyshev_dist(hill.queue));
        if (e.state == sim::UnitState::Idle || e.state == sim::UnitState::GuardIdle) value += static_cast<int32_t>(plan.harass_idle_bonus);
        t.value = value;
        targets.push_back(t);
    }
    if (!targets.empty()) last_target_ = now;

    // 3. the squad: every Combat Ant that can fight (they have no better use), and workers that the economy can spare while there is something to hunt
    const size_t total = v.mine().size();
    const auto target_in_range = [&](const AntView& a) {
        if (plan.harass_range <= 0) return !targets.empty();
        for (const Target& t : targets) {
            if (t.e->tile.chebyshev_dist(a.tile) <= plan.harass_range) return true;
        }
        return false;
    };
    for (const AntView& a : v.mine()) {
        if (squad_.count(a.id) != 0 || a.type != sim::AntType::Combat) continue;
        if (!can_fight(v, a) || a.hp < plan.harass_min_hp) continue;
        if (plan.harass_range > 0 && !target_in_range(a)) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        if (c.ledger.take(a.id, id())) {
            squad_[a.id] = Member{};
            ++recruited_;
        }
    }
    if (plan.harass_workers > 0 && !targets.empty()) {
        size_t workers = 0;
        for (const auto& m : squad_) {
            const AntView* a = find_ant(v.mine(), m.first);
            workers += a != nullptr && a->type != sim::AntType::Combat ? 1u : 0u;
        }
        // the workers nearest to the most valuable target, while the economy keeps its reserve
        const Target* best = &targets[0];
        for (const Target& t : targets) {
            if (t.value > best->value) best = &t;
        }
        std::vector<const AntView*> cands;
        for (const AntView& a : v.mine()) {
            if (squad_.count(a.id) != 0 || a.type == sim::AntType::Combat || !can_fight(v, a) || a.hp < 7) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            cands.push_back(&a);
        }
        std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) { return x->tile.chebyshev_dist(best->e->tile) < y->tile.chebyshev_dist(best->e->tile); });
        for (const AntView* a : cands) {
            if (workers >= plan.harass_workers || total <= squad_.size() + plan.harass_reserve) break;
            if (!c.ledger.take(a->id, id())) continue;
            squad_[a->id] = Member{};
            ++workers;
            ++recruited_;
        }
    }
    if (squad_.empty()) return;

    // 4. the members that found nothing to hunt for a while go back to the economy (with harass_range: the ones that have no target within range)
    if (plan.harass_range > 0) {
        for (auto it = squad_.begin(); it != squad_.end();) {
            const AntView* a = find_ant(v.mine(), it->first);
            if (a != nullptr && !target_in_range(*a)) {
                if (it->second.out_since == 0) it->second.out_since = now;
                if (now >= it->second.out_since + plan.harass_idle_release) {
                    c.ledger.release(it->first, id());
                    it = squad_.erase(it);
                    continue;
                }
            } else {
                it->second.out_since = 0;
            }
            ++it;
        }
    }
    if (plan.harass_range <= 0 && targets.empty() && now >= last_target_ + plan.harass_idle_release) {
        for (auto it = squad_.begin(); it != squad_.end();) {
            const AntView* a = find_ant(v.mine(), it->first);
            if (a != nullptr && (a->type != sim::AntType::Combat || !plan.harass_station)) {      // (with no station the Combat Ants go back to their post at home as well)
                c.ledger.release(it->first, id());
                it = squad_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 5. the strength of the squad, and the orders
    uint32_t own = 0;
    for (const auto& m : squad_) {
        const AntView* a = find_ant(v.mine(), m.first);
        if (a != nullptr) own += a->type == sim::AntType::Combat ? 8u : 4u;
    }
    std::map<uint32_t, std::vector<uint32_t>> groups;                              // target -> attackers
    std::vector<uint32_t> waiting;
    for (auto& f : squad_) {
        const AntView* a = find_ant(v.mine(), f.first);
        if (a == nullptr) continue;
        Member& mem = f.second;
        // an attack that was acknowledged and shows "can't go" shortly after: that ant cannot be reached (it stands on something that no order reaches)
        if (mem.ordered && mem.sent != kPending && mem.target != 0 && a->state == sim::UnitState::CantGo && now >= mem.sent + 8u && now <= mem.sent + 60u) {
            shunned_[mem.target] = now + 900u;
            ++shunned_count_;
            mem.ordered = false;
        }
        bool need = false;
        if (!mem.ordered) need = a->takes_orders();
        else if (mem.sent != kPending) need = a->idle() && now >= mem.sent + 6u && !v.has_pending_path(a->id);
        else need = now > mem.decided + 200u;
        if (!need) continue;
        // the target of this ant: the one it has while it is still a target, else the best by value less the distance
        const Target* pick = nullptr;
        int32_t pick_score = 0;
        for (const Target& t : targets) {
            if (static_cast<uint64_t>(own) * 100u < static_cast<uint64_t>(t.enemy) * plan.harass_odds_percent) {
                ++refused_odds_;
                continue;
            }
            int32_t score = t.value - static_cast<int32_t>(plan.harass_dist_cost) * a->tile.chebyshev_dist(t.e->tile);
            if (t.e->id == mem.target) score += static_cast<int32_t>(plan.harass_stick);       // stay with a target
            if (pick == nullptr || score > pick_score) {
                pick = &t;
                pick_score = score;
            }
        }
        mem.ordered = true;
        mem.decided = now;
        mem.sent = kPending;
        if (pick != nullptr) {
            mem.target = pick->e->id;
            mem.station_sent = false;
            groups[pick->e->id].push_back(a->id);
        } else {
            mem.target = 0;
            waiting.push_back(a->id);
        }
    }
    for (const auto& g : groups) {
        const AntView* e = find_ant(v.others(), g.first);
        if (e == nullptr) continue;
        c.orders.attack(g.second, e->tile, Priority::Urgent);
        attacks_ordered_ += static_cast<uint32_t>(g.second.size());
        hunting_ = true;
        tactics_.harass_active = true;
    }
    // no target: the Combat Ants wait in the middle between the enemy hills, where the carriers pass
    if (!waiting.empty() && plan.harass_station) {
        int32_t sx = 0;
        int32_t sy = 0;
        int32_t n = 0;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = v.rows()[t];
            if (t == c.seat || !row.present || row.dropped || !c.map.hill(t).present) continue;
            sx += c.map.hill(t).queue.x;
            sy += c.map.hill(t).queue.y;
            ++n;
        }
        if (n > 0) {
            sim::TileCoord station{sx / n, sy / n};
            for (int32_t k = 1; k <= 6 && (!MapInfo::walkable(grid, c.seat, station, v.walk_context()) || v.powerup_at(station) != nullptr); ++k) {
                station = sim::TileCoord{sx / n + (k % 2 == 0 ? k : -k) / 2, sy / n + (k % 3 == 0 ? 1 : -1)};
            }
            if (MapInfo::walkable(grid, c.seat, station, v.walk_context()) && v.powerup_at(station) == nullptr) {
                std::vector<uint32_t> go;
                for (const uint32_t waiting_id : waiting) {
                    const AntView* a = find_ant(v.mine(), waiting_id);
                    Member& mem = squad_[waiting_id];
                    if (a == nullptr || a->type != sim::AntType::Combat || mem.station_sent || a->tile.chebyshev_dist(station) <= 2) continue;
                    mem.station_sent = true;
                    go.push_back(waiting_id);
                }
                if (!go.empty()) c.orders.move(go, station, Priority::Normal);
            }
        }
    }
}

}  // namespace ants::ai
