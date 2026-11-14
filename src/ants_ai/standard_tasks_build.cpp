#include "ants_ai/standard_tasks.hpp"
#include "standard_tasks_shared.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;

namespace {

// A free tile next to `tile` for the ant that stands on it to step to (the ant is the one that is to order a special order onto the tile: a click on a tile with an ant on it, the ordering
// ant included, is no special order; a person moves the ant aside first): walkable, no ant, no power-up, no fire wall; {-1, -1} when there is none
sim::TileCoord step_aside(const BotView& v, uint8_t seat, sim::TileCoord tile) {
    static const int32_t kOffsets[8][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& o : kOffsets) {
        const sim::TileCoord t{tile.x + o[0], tile.y + o[1]};
        if (!v.grid().in_bounds(t) || v.grid().has_fire_at(t) || v.powerup_at(t) != nullptr || occupied(v, t)) continue;
        if (MapInfo::walkable(v.grid(), seat, t, v.walk_context())) return t;
    }
    return sim::TileCoord{-1, -1};
}

}  // namespace

// ---- WallTask -------------------------------------------------------------------------------------------------------------------------------------------

// Whether a fire wall of the ring round the own gate is not to be put out now (a report after play: the bots just extinguish all of the fire and leave the Fire Ant that lights it free to light it
// again): an enemy Fire Ant is near the tile (the fighters go after it, FightTask::start_fire_defence) for plan.fire_defence_hold ticks at the most (`hold_over`: one that no blow reaches, on a wall
// or a power-up, would hold the walls for the 3,600 ticks that they burn), or the enemy's force near it is stronger than the own (the Fire Ant would walk into it)
bool fired_in_wall(const BotView& v, const HillInfo& hill, const LevelPlan& plan, sim::TileCoord tile, bool hold_over) {
    bool on_ring = false;
    for (const sim::TileCoord& r : SabotageTask::ring_of(hill)) on_ring = on_ring || r == tile;
    if (!on_ring) return false;
    for (const AntView& e : v.others()) {
        if (!hold_over && e.type == sim::AntType::Fire && e.tile.chebyshev_dist(tile) <= plan.fire_defence_wait && !(v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) return true;
    }
    const Strength theirs = enemy_strength(v, tile, 6, 0, 4);
    if (theirs.n == 0) return false;
    Strength own;
    for (const AntView& a : v.mine()) {
        if (a.tile.chebyshev_dist(tile) <= 8 && !a.holding) own.add(a.hp, blow_damage(a.type));
    }
    return own.value() < theirs.value();
}

void WallTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupSpecial || command.ants.size() != 1 || command.ants[0] != keeper_) return;
    if (counter_job_.decided != 0 && counter_job_.sent == kPending && counter_job_.tile.x == command.tile_x && counter_job_.tile.y == command.tile_y) {
        if (fate == Bot::Fate::Sent) counter_job_.sent = tick;
        else counter_job_ = Job{};
        return;
    }
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        Job& j = it->second;
        if (j.sent != kPending || j.tile.x != command.tile_x || j.tile.y != command.tile_y) continue;
        if (fate == Bot::Fate::Sent) j.sent = tick;
        else jobs_.erase(it);                                              // it never left: order it again
        return;
    }
}

void WallTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const HillInfo& hill = c.map.hill(c.seat);
    if (!hill.present || !v.has_grid()) return;
    const sim::Grid& grid = v.grid();
    // the walls of the ring round the own gate wait for the fighters to deal with the Fire Ant that lit them (fired_in_wall), but not for ever
    bool ring_lit = false;
    for (const sim::TileCoord& r : SabotageTask::ring_of(hill)) ring_lit = ring_lit || (grid.in_bounds(r) && grid.has_fire_at(r));
    if (!ring_lit) ring_lit_since_ = 0;
    else if (ring_lit_since_ == 0) ring_lit_since_ = now;
    const bool hold_over = ring_lit_since_ != 0 && now >= ring_lit_since_ + plan.fire_defence_hold;
    // 1. the keeper: an own Fire Ant. It is claimed (taken from the economy, in which a Fire Ant harvests) only while it has work, and given back when it has none
    if (keeper_ != 0) {
        const AntView* k = find_ant(v.mine(), keeper_);
        if (k == nullptr || k->type != sim::AntType::Fire) {
            keeper_ = 0;
            jobs_.clear();
        } else if (c.ledger.owner(keeper_) != id() && c.ledger.owner(keeper_) != kNoTask && c.ledger.rank(c.ledger.owner(keeper_)) >= c.ledger.rank(id())) {
            jobs_.clear();                                                                // a task of a higher rank has it
        }
    }
    if (keeper_ == 0) {
        for (const AntView& a : v.mine()) {
            if (a.type == sim::AntType::Fire && a.hp >= 3) {
                keeper_ = a.id;
                break;
            }
        }
    }
    tactics_.wall_keeper = keeper_;
    if (keeper_ == 0) return;
    const AntView* keeper = find_ant(v.mine(), keeper_);
    if (keeper == nullptr) return;
    const EastState st = east_state(grid, hill);
    const std::array<sim::TileCoord, 3> tiles = east_tiles(hill);

    // 2. what became of the orders
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const int i = it->first;
        Job& j = it->second;
        const bool done = j.extinguish ? st.tile[static_cast<size_t>(i)] != EastTile::Wall : st.tile[static_cast<size_t>(i)] != EastTile::Open;
        if (done) {
            it = jobs_.erase(it);
        } else if (j.sent != kPending && now >= j.sent + 140u && keeper->idle()) {
            failed_until_[i] = now + 300u;                                                // the ant is idle again and the tile is as it was: something stands there, or the order failed
            ++failures_;
            it = jobs_.erase(it);
        } else if (j.sent == kPending && now > j.decided + 200u) {
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }

    // 2b. the enemy wall that is being put out
    if (counter_job_.decided != 0) {
        bool still = false;
        for (const FireWallView& w : v.fire_walls()) still = still || w.tile == counter_job_.tile;
        if (!still) {
            ++extinguished_;
            counter_job_ = Job{};
        } else if (counter_job_.sent != kPending && now >= counter_job_.sent + 140u && keeper->idle()) {
            counter_black_[static_cast<int64_t>(counter_job_.tile.y) * 4096 + counter_job_.tile.x] = now + 300u;
            ++failures_;
            counter_job_ = Job{};
        } else if (counter_job_.sent == kPending && now > counter_job_.decided + 200u) {
            counter_job_ = Job{};
        }
    }
    for (auto it = counter_black_.begin(); it != counter_black_.end();) it = it->second <= now ? counter_black_.erase(it) : std::next(it);

    // 3. is there work? an open tile to build on (the nearest to the ant first), or a wall to renew at a calm moment
    int pick = -1;
    int32_t best_d = 0;
    if (tactics_.wall_demand) {
        for (int i = 0; i < 3; ++i) {
            if (st.tile[static_cast<size_t>(i)] != EastTile::Open) continue;
            const auto f = failed_until_.find(i);
            if (f != failed_until_.end() && f->second > now) continue;
            const int32_t d = keeper->tile.chebyshev_dist(tiles[static_cast<size_t>(i)]);
            if (pick < 0 || d < best_d) {
                pick = i;
                best_d = d;
            }
        }
    }
    int renew = -1;
    if (tactics_.wall_demand && pick < 0 && plan.renew_lead_ticks > 0) {
        // calm: no thief within twelve tiles of the raid tile (it would be there before a new wall: the tile is open for about 60 ticks, a thief needs 8 per tile)
        bool calm = true;
        for (const ThiefSighting& t : tactics_.memory.thieves()) calm = calm && t.tile.chebyshev_dist(hill.raid) > 12;
        for (int i = 0; i < 3 && calm && renew < 0; ++i) {
            if (st.tile[static_cast<size_t>(i)] != EastTile::Wall) continue;
            const uint64_t seen = tactics_.memory.wall_seen(tiles[static_cast<size_t>(i)]);
            const uint64_t travel = 8u * static_cast<uint64_t>(keeper->tile.chebyshev_dist(tiles[static_cast<size_t>(i)])) + 30u;       // the ant may be away at a pile
            if (seen == 0 || now + plan.renew_lead_ticks + travel < seen + sim::LIFETIME_180S_TICKS) continue;
            if (seen + sim::LIFETIME_180S_TICKS >= now + v.ticks_left()) continue;        // it outlives the match (the engine arms the burn-out timer only while more than 180 s remain)
            renew = i;
        }
    }
    // an enemy wall in the way of the economy that this ant could put out (the walls in front of the own thief hole and of the ally's are not enemy walls)
    bool have_counter = counter_job_.decided != 0;
    sim::TileCoord counter_tile = counter_job_.tile;
    if (plan.counters && !have_counter && pick < 0 && renew < 0) {
        const HillInfo* ally_hill = v.ally() < sim::MAX_PLAYERS && c.map.hill(v.ally()).present ? &c.map.hill(v.ally()) : nullptr;
        int32_t best = 0;
        int best_harm = 0;
        for (const FireWallView& w : v.fire_walls()) {
            bool ours = false;
            for (const sim::TileCoord& e : tiles) ours = ours || e == w.tile;
            if (ally_hill != nullptr) {
                for (const sim::TileCoord& e : east_tiles(*ally_hill)) ours = ours || e == w.tile;
            }
            const auto b = counter_black_.find(static_cast<int64_t>(w.tile.y) * 4096 + w.tile.x);
            if (ours || (b != counter_black_.end() && b->second > now)) continue;
            if (plan.fire_defence && fired_in_wall(v, hill, plan, w.tile, hold_over)) continue;          // a wall of the ring round the own gate: not before the Fire Ant that lights it is dealt with
            const int harm = harm_to(v, c.map, plan, w.tile);                              // the own economy first, then the ally's
            if (harm == 0) continue;
            if (plan.own_ground_counters && harm >= 1) {
                if (tactics_.wall_demand && w.tile.chebyshev_dist(hill.origin) > plan.counter_keeper_leash) continue;        // the keeper stays at its hole while the walls are wanted
                if (v.mine().size() <= tactics_.reserve_of(v) && (harm == 2 || hazard_ground(v, c.map, plan, w.tile) != HazardGround::Own)) continue;   // the last ants do not leave their own ground (the ally's yard is not theirs)
            }
            const int32_t d = keeper->tile.chebyshev_dist(w.tile);
            if (!have_counter || harm < best_harm || (harm == best_harm && d < best)) {
                have_counter = true;
                best = d;
                best_harm = harm;
                counter_tile = w.tile;
            }
        }
    }
    const bool work = !jobs_.empty() || pick >= 0 || renew >= 0 || have_counter;
    if (!work) {
        if (c.ledger.owner(keeper_) == id()) c.ledger.release(keeper_, id());          // nothing to do: back to the economy
        return;
    }
    const TaskId owner = c.ledger.owner(keeper_);
    if (owner != id()) {
        if (keeper->holding || keeper->carried_points > 0 || !keeper->takes_orders()) return;     // it is busy with a harvest: the work waits for the deposit
        if (!c.ledger.take(keeper_, id())) return;
    }
    // (an ant that walks to a pile can be ordered as well: its harvest loop is the engine's, the claim alone does not stop it)
    if (!jobs_.empty() || !keeper->takes_orders() || keeper->state == sim::UnitState::Stunned || keeper->holding || keeper->carried_points > 0) return;

    // 4. build
    if (pick >= 0) {
        const sim::TileCoord target = tiles[static_cast<size_t>(pick)];
        if (keeper->tile == target) {                                                     // the ant stands on the tile: nothing can be lit under it; it steps out of the way first
            const sim::TileCoord away{target.x + 2, target.y};
            if (v.powerup_at(away) == nullptr && grid.in_bounds(away)) c.orders.move({keeper_}, away, Priority::Normal);
            failed_until_[pick] = now + 30u;
            return;
        }
        sim::Command probe;
        probe.type = sim::CommandType::GroupSpecial;
        probe.tile_x = static_cast<int16_t>(target.x);
        probe.tile_y = static_cast<int16_t>(target.y);
        probe.ants = {keeper_};
        if (occupied(v, target) || v.predict_ack(probe) == 0) {                           // the cursor would not show the target cursor there (an ant stands on it): later
            failed_until_[pick] = now + 40u;
            return;
        }
        c.orders.special(keeper_, target, Priority::Normal);
        Job j;
        j.tile = target;
        j.decided = now;
        jobs_[pick] = j;
        ++walls_ordered_;
        return;
    }

    // 5b. an enemy wall that is in the way: put it out
    if (have_counter && renew < 0 && counter_job_.decided == 0) {
        sim::Command probe;
        probe.type = sim::CommandType::GroupSpecial;
        probe.tile_x = static_cast<int16_t>(counter_tile.x);
        probe.tile_y = static_cast<int16_t>(counter_tile.y);
        probe.ants = {keeper_};
        if (keeper->tile == counter_tile) {                                               // the keeper itself stands on the wall: it steps aside first
            const sim::TileCoord aside = step_aside(v, c.seat, counter_tile);
            if (aside.x >= 0) c.orders.move({keeper_}, aside, Priority::Normal);
            counter_black_[static_cast<int64_t>(counter_tile.y) * 4096 + counter_tile.x] = now + 20u;
            return;
        }
        if (occupied(v, counter_tile)) {                                                  // another ant stands on the wall (a fire ant stands on fire): no click puts it out, the ant is the thing to deal with
            counter_black_[static_cast<int64_t>(counter_tile.y) * 4096 + counter_tile.x] = now + 20u;
            return;
        }
        if (v.predict_ack(probe) == 0) {
            counter_black_[static_cast<int64_t>(counter_tile.y) * 4096 + counter_tile.x] = now + 100u;
            return;
        }
        c.orders.special(keeper_, counter_tile, Priority::Normal);
        counter_job_.tile = counter_tile;
        counter_job_.decided = now;
        counter_job_.sent = kPending;
        counter_job_.extinguish = true;
        return;
    }

    // 5. renew: the wall is put out (and lit again at the next look: the tile is open then)
    if (renew >= 0) {
        sim::Command probe;
        probe.type = sim::CommandType::GroupSpecial;
        probe.tile_x = static_cast<int16_t>(tiles[static_cast<size_t>(renew)].x);
        probe.tile_y = static_cast<int16_t>(tiles[static_cast<size_t>(renew)].y);
        probe.ants = {keeper_};
        if (keeper->tile == tiles[static_cast<size_t>(renew)]) {                          // the keeper stands on the wall that is to be put out: it steps aside first
            const sim::TileCoord aside = step_aside(v, c.seat, keeper->tile);
            if (aside.x >= 0) c.orders.move({keeper_}, aside, Priority::Normal);
            return;
        }
        if (occupied(v, tiles[static_cast<size_t>(renew)]) || v.predict_ack(probe) == 0) return;
        c.orders.special(keeper_, tiles[static_cast<size_t>(renew)], Priority::Normal);
        Job j;
        j.tile = tiles[static_cast<size_t>(renew)];
        j.decided = now;
        j.extinguish = true;
        jobs_[renew] = j;
        ++renewals_;
    }
}

// ---- BombTask -------------------------------------------------------------------------------------------------------------------------------------------

int harm_to(const BotView& view, const MapInfo& map, const LevelPlan& plan, sim::TileCoord tile, bool map_owned) noexcept {
    if (harms_economy(view, map, plan, tile, map_owned)) return 1;
    const uint8_t ally = view.ally();
    if (!plan.ally_help || ally >= sim::MAX_PLAYERS) return 0;
    if (plan.own_ground_counters && !map_owned && ally_hazard_ground(view, map, plan, tile) == HazardGround::Foreign) return 0;      // nor is a third team's base the ally's business
    const HillInfo& hill = map.hill(ally);
    if (hill.present && tile.chebyshev_dist(hill.origin) <= plan.counter_hill_radius) return 2;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map.pile(p.index);
        if (info == nullptr || !info->approach[ally].reachable()) continue;
        if (tile.chebyshev_dist(p.anchor) <= plan.counter_pile_radius + 1) return 2;
    }
    return 0;
}

bool harms_economy(const BotView& view, const MapInfo& map, const LevelPlan& plan, sim::TileCoord tile, bool map_owned) noexcept {
    if (plan.own_ground_counters && !map_owned && hazard_ground(view, map, plan, tile) == HazardGround::Foreign) return false;      // another team's base is none of the bot's business
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map.pile(p.index);
        if (info == nullptr || !info->approach[view.seat()].reachable()) continue;
        if (tile.chebyshev_dist(p.anchor) <= plan.counter_pile_radius + 1) return true;
    }
    const HillInfo& hill = map.hill(view.seat());
    if (hill.present && tile.chebyshev_dist(hill.origin) <= plan.counter_hill_radius) {
        if (plan.smart_counters && view.has_grid() && ((view.grid().has_fire_at(tile) && harmless_lone_fire(view, hill, tile)) || (view.grid().has_bomb_at(tile) && harmless_stray_bomb(view, map, hill, tile)))) return false;
        return true;
    }
    return false;
}

void BombTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (actor_ == 0 || sent_ != kPending || command.ants.size() != 1 || command.ants[0] != actor_) return;
    if (command.tile_x != target_.x || command.tile_y != target_.y) return;
    if (command.type != (defuse_ ? sim::CommandType::GroupSpecial : sim::CommandType::GroupMove)) return;
    if (fate == Bot::Fate::Sent) sent_ = tick;
    else actor_ = 0;                                                    // it never left (its claim is given back at the next look)
}

void BombTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    for (auto it = black_.begin(); it != black_.end();) it = it->second <= now ? black_.erase(it) : std::next(it);
    for (const uint32_t ant : c.ledger.ants_of(id())) {
        if (ant != actor_) c.ledger.release(ant, id());
    }
    if (!plan.counters || !v.has_grid()) return;
    // the job that is under way
    if (actor_ != 0) {
        const AntView* a = find_ant(v.mine(), actor_);
        bool still = false;
        for (const BombView& b : v.bombs()) still = still || b.tile == target_;
        bool over = false;
        if (a == nullptr || c.ledger.owner(actor_) != id()) {
            over = true;
        } else if (!still) {
            if (defuse_) ++defused_;
            else ++set_off_;
            over = true;
        } else if (sent_ != kPending && now >= sent_ + 160u && a->idle()) {
            black_[key_of(target_)] = now + 300u;                           // it did not get there (or did nothing): the bomb is left alone for a while
            ++failures_;
            over = true;
        } else if (sent_ == kPending && now > decided_ + 200u) {
            over = true;
        }
        if (over) {
            c.ledger.release(actor_, id());
            actor_ = 0;
        } else {
            return;
        }
    }
    // the harmful bombs, the nearest to the hill first
    const HillInfo& hill = c.map.hill(c.seat);
    const BombView* pick = nullptr;
    int32_t best = 0;
    int best_harm = 0;
    HazardGround pick_ground = HazardGround::Own;
    const bool few_ants = v.mine().size() <= tactics_.reserve_of(v);                                   // (the last ants stay out of every chore that leaves the own ground)
    for (const BombView& b : v.bombs()) {
        if (b.owner == c.seat || (v.ally() < sim::MAX_PLAYERS && b.owner == v.ally()) || (plan.gate_spare_map_bombs && tactics_.gate_goals_live && b.owner >= sim::MAX_PLAYERS)) continue;   // an own or an allied bomb is no enemy's; a gate map's own bombs (no team, 255) are its gate, which the live gate goals defuse on their way (goals exhausted or in the fallback: countered as on any map)
        const auto bl = black_.find(key_of(b.tile));
        if (bl != black_.end() && bl->second > now) continue;
        const int harm = harm_to(v, c.map, plan, b.tile, b.owner >= sim::MAX_PLAYERS);                 // the own economy first, then the ally's (only a Bomber goes there)
        if (harm == 0) continue;
        HazardGround ground = HazardGround::Own;
        if (plan.own_ground_counters && b.owner < sim::MAX_PLAYERS) {
            ground = harm == 2 ? HazardGround::Foreign : hazard_ground(v, c.map, plan, b.tile);          // (the ally's yard is not the bot's own ground)
            if (ground != HazardGround::Own && few_ants) continue;                                       // the last ants do not leave their own ground
        }
        const int32_t d = hill.present ? b.tile.chebyshev_dist(hill.origin) : 0;
        if (pick == nullptr || harm < best_harm || (harm == best_harm && d < best)) {
            pick = &b;
            best = d;
            best_harm = harm;
            pick_ground = ground;
        }
    }
    if (pick == nullptr) return;
    // a Bomber defuses it; without one a healthy idle worker sets it off
    const AntView* actor = nullptr;
    bool defuse = false;
    int32_t actor_d = 0;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Bomber || a.hp < (plan.own_ground_counters ? plan.counter_bomber_hp : 3u) || a.holding || a.carried_points > 0 || !a.takes_orders() || a.state == sim::UnitState::Stunned) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        const int32_t d = a.tile.chebyshev_dist(pick->tile);
        if (actor == nullptr || d < actor_d) {
            actor = &a;
            actor_d = d;
            defuse = true;
        }
    }
    // (a worker does not walk to the ally's base to set a bomb off, and with own-ground counters only steps on a bomb on the own ground, and not as one of the last ants)
    if (actor == nullptr && plan.bomb_hit && best_harm == 1 && (!plan.own_ground_counters || (pick_ground == HazardGround::Own && !few_ants))) {
        for (const AntView& a : v.mine()) {
            if (a.type != v.default_ant_type() || a.hp < 8 || a.holding || a.carried_points > 0 || !a.idle() || v.powerup_at(a.tile) != nullptr) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            const int32_t d = a.tile.chebyshev_dist(pick->tile);
            if (actor == nullptr || d < actor_d) {
                actor = &a;
                actor_d = d;
            }
        }
    }
    if (actor == nullptr) return;
    if (defuse) {
        sim::Command probe;
        probe.type = sim::CommandType::GroupSpecial;
        probe.tile_x = static_cast<int16_t>(pick->tile.x);
        probe.tile_y = static_cast<int16_t>(pick->tile.y);
        probe.ants = {actor->id};
        if (occupied(v, pick->tile) || v.predict_ack(probe) == 0) {
            black_[key_of(pick->tile)] = now + 100u;
            return;
        }
    }
    if (!c.ledger.take(actor->id, id())) return;
    if (defuse) c.orders.special(actor->id, pick->tile, Priority::Normal);
    else c.orders.move({actor->id}, pick->tile, Priority::Normal);
    actor_ = actor->id;
    target_ = pick->tile;
    defuse_ = defuse;
    decided_ = now;
    sent_ = kPending;
}

// ---- MineTask -------------------------------------------------------------------------------------------------------------------------------------------

void MineTask::finish(AntLedger& ledger) {
    for (const auto& j : jobs_) ledger.release(j.first, id());
    jobs_.clear();
    Task::finish(ledger);
}

void MineTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupSpecial || command.ants.size() != 1) return;
    const auto it = jobs_.find(command.ants[0]);
    if (it == jobs_.end() || it->second.sent != kPending) return;
    if (it->second.tile.x != command.tile_x || it->second.tile.y != command.tile_y) return;
    if (fate == Bot::Fate::Sent) {
        it->second.sent = tick;
        return;
    }
    cool_[key_of(it->second.tile)] = tick + (fate == Bot::Fate::Filtered ? 300u : 60u);          // it never left (the claim is given back at the next look)
    jobs_.erase(it);
}

// The tiles round the piles that an enemy works, where the enemy is no further than the bot (see the class). Every candidate is scored by how much nearer the enemy is (the walking costs of the two
// hills' fields) less 25 for every tile of distance from the pile; the best come first.
void MineTask::collect_pile_targets(TaskContext& c, std::vector<Target>& out) const {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    if (plan.mine_per_pile == 0) return;
    const std::vector<int32_t>& own_field = c.map.cost_field_of(c.seat);
    if (own_field.empty()) return;
    const HillInfo& own_hill = c.map.hill(c.seat);
    const int32_t width = static_cast<int32_t>(grid.width());
    for (const PileView& p : v.piles()) {
        if (p.lunchbox || p.remaining < plan.mine_min_units) continue;
        const PileInfo* info = c.map.pile(p.index);
        if (info == nullptr || !info->approach[c.seat].reachable() || info->cells.empty()) continue;
        const int32_t own_cost = info->approach[c.seat].cost;
        std::vector<const std::vector<int32_t>*> foes;                                   // the cost fields of the hills of the enemies that work the pile
        bool leader_works = false;                                                       // (a bot that is behind) the leader is one of them: his food is what the mines are for
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = v.rows()[t];
            if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || !c.map.hill(t).present) continue;
            const Approach& ap = info->approach[t];
            bool works = ap.reachable() && static_cast<int64_t>(ap.cost) * 100 <= static_cast<int64_t>(own_cost) * plan.mine_percent;
            for (const AntView& e : v.others()) works = works || (e.team == t && e.tile.chebyshev_dist(p.anchor) <= 6);
            const std::vector<int32_t>& field = c.map.cost_field_of(t);
            if (works && !field.empty()) {
                foes.push_back(&field);
                leader_works = leader_works || (plan.behind_war && tactics_.standing.war >= 1 && static_cast<int>(t) == tactics_.standing.leader);
            }
        }
        if (foes.empty()) continue;
        const auto by_pile = [&](sim::TileCoord t) {                                     // within two tiles of a cell of the pile: where the mines of this pile are laid
            for (const sim::TileCoord& q : info->cells) {
                if (q.chebyshev_dist(t) <= 2) return true;
            }
            return false;
        };
        uint32_t standing = 0;                                                           // the mines that stand, or are being laid, at this pile
        for (const BombView& b : v.bombs()) standing += b.owner == c.seat && by_pile(b.tile) ? 1u : 0u;
        for (const auto& j : jobs_) standing += by_pile(j.second.tile) ? 1u : 0u;
        if (standing >= plan.mine_per_pile) continue;
        std::vector<Target> here;
        std::set<int64_t> seen;
        for (const sim::TileCoord& cell : info->cells) {
            for (int32_t dy = -2; dy <= 2; ++dy) {
                for (int32_t dx = -2; dx <= 2; ++dx) {
                    const sim::TileCoord t{cell.x + dx, cell.y + dy};
                    if (!grid.in_bounds(t) || !seen.insert(key_of(t)).second) continue;
                    bool in_pile = false;
                    int32_t ring = 9;
                    for (const sim::TileCoord& q : info->cells) {
                        in_pile = in_pile || q == t;
                        ring = std::min(ring, q.chebyshev_dist(t));
                    }
                    if (in_pile || ring > 2) continue;
                    if (!grid.get_cell(t).can_place_bomb() || v.powerup_at(t) != nullptr || occupied(v, t)) continue;
                    const auto cl = cool_.find(key_of(t));
                    if (cl != cool_.end() && cl->second > now) continue;
                    if (own_hill.present && t.chebyshev_dist(own_hill.origin) <= 6) continue;           // never at home
                    bool crowded = false;
                    for (const BombView& b : v.bombs()) crowded = crowded || b.tile.chebyshev_dist(t) < static_cast<int32_t>(plan.mine_apart);
                    for (const auto& j : jobs_) crowded = crowded || j.second.tile.chebyshev_dist(t) < static_cast<int32_t>(plan.mine_apart);
                    if (crowded) continue;
                    const size_t idx = static_cast<size_t>(t.y) * static_cast<size_t>(width) + static_cast<size_t>(t.x);
                    const int32_t mine_cost = own_field[idx];
                    int32_t foe_cost = -1;
                    for (const std::vector<int32_t>* f : foes) {
                        if ((*f)[idx] >= 0 && (foe_cost < 0 || (*f)[idx] < foe_cost)) foe_cost = (*f)[idx];
                    }
                    if (mine_cost < 0 || foe_cost < 0 || foe_cost > mine_cost) continue;               // only where the enemy is no further than the bot (a mine in front of the own workers blocks their way: R7)
                    here.push_back(Target{t, (mine_cost - foe_cost) - 25 * ring + (leader_works ? 40 : 0)});
                }
            }
        }
        std::stable_sort(here.begin(), here.end(), [](const Target& x, const Target& y) { return x.score > y.score; });
        // the best few that keep apart from each other (two mines side by side are one mine with a neighbour)
        uint32_t need = plan.mine_per_pile - standing;
        std::vector<Target> chosen;
        for (const Target& t : here) {
            if (chosen.size() >= need) break;
            bool apart = true;
            for (const Target& o : chosen) apart = apart && o.tile.chebyshev_dist(t.tile) >= static_cast<int32_t>(plan.mine_apart);
            if (apart) chosen.push_back(t);
        }
        for (const Target& t : chosen) out.push_back(t);
    }
}

// With no pile to mine: the ring round the gate of the best opponent (the tiles that SabotageTask lights), plan.mine_gate mines at a time.
void MineTask::collect_gate_targets(TaskContext& c, std::vector<Target>& out) const {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    const uint32_t limit = behind_mining() ? std::max(plan.mine_gate, plan.behind_mine_gate) : plan.mine_gate;       // (a loser from plan.behind_mine_tier on lays more of the ring)
    if (limit == 0) return;
    int victim = -1;
    int32_t best = -1;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || !c.map.hill(t).present) continue;
        if (row.score > best) {
            best = row.score;
            victim = t;
        }
    }
    if (victim < 0) return;
    const std::array<sim::TileCoord, 8> ring = SabotageTask::ring_of(c.map.hill(static_cast<uint8_t>(victim)));
    uint32_t standing = 0;
    for (const sim::TileCoord& t : ring) {
        for (const BombView& b : v.bombs()) standing += b.owner == c.seat && b.tile == t ? 1u : 0u;
        for (const auto& j : jobs_) standing += j.second.tile == t ? 1u : 0u;
    }
    if (standing >= limit) return;
    for (const sim::TileCoord& t : ring) {
        if (!grid.in_bounds(t) || !grid.get_cell(t).can_place_bomb() || v.powerup_at(t) != nullptr || occupied(v, t)) continue;
        const auto cl = cool_.find(key_of(t));
        if (cl != cool_.end() && cl->second > now) continue;
        bool taken = false;
        for (const BombView& b : v.bombs()) taken = taken || b.tile == t;
        for (const auto& j : jobs_) taken = taken || j.second.tile == t;
        if (!taken) out.push_back(Target{t, 0});
    }
}

// Round the own hill: the open tiles five to nine tiles from it that lie on the enemy's way to it (the enemy's walking cost to the tile and the bot's own from its queue row add up to little more than the
// enemy's walk to the hill), plan.mine_home standing at a time, plan.mine_home_apart tiles apart from every other mine. An enemy walk does not go round a bomb of the bot (the engine's path finder skips only the
// walker's own and the ally's), so a rush of many ants walks into them, and the bot's own carriers walk round them. Never within two tiles of a pile (a pile near home is worked), and only on open
// ground (seven of the eight neighbours are walkable) so that no corridor of the own economy is closed by one.
void MineTask::collect_home_targets(TaskContext& c, std::vector<Target>& out) const {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const sim::Grid& grid = v.grid();
    const uint64_t now = v.tick();
    if (plan.mine_home == 0 || now < plan.mine_home_after) return;
    const HillInfo& own_hill = c.map.hill(c.seat);
    const std::vector<int32_t>& own_field = c.map.cost_field_of(c.seat);
    if (!own_hill.present || own_field.empty()) return;
    const int32_t width = static_cast<int32_t>(grid.width());
    std::vector<const std::vector<int32_t>*> foes;                                       // the cost fields of the enemies' hills, with what an enemy pays to reach the own queue row
    std::vector<int32_t> foe_home;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || !c.map.hill(t).present || !tactics_.memory.plays(t)) continue;      // (an enemy that has not moved yet comes nowhere)
        const std::vector<int32_t>& field = c.map.cost_field_of(t);
        if (field.empty()) continue;
        int32_t to_home = -1;                                                            // the enemy's walk to the ring round the gate (the queue row itself is the owner's) plus the own walk from the queue row to it
        for (const sim::TileCoord& r : SabotageTask::ring_of(own_hill)) {
            if (!grid.in_bounds(r)) continue;
            const size_t ri = static_cast<size_t>(r.y) * static_cast<size_t>(width) + static_cast<size_t>(r.x);
            if (own_field[ri] < 0 || field[ri] < 0) continue;
            if (to_home < 0 || own_field[ri] + field[ri] < to_home) to_home = own_field[ri] + field[ri];
        }
        if (to_home < 0) continue;
        foes.push_back(&field);
        foe_home.push_back(to_home);
    }
    if (foes.empty()) return;
    uint32_t standing = 0;                                                               // the mines that stand, or are being laid, round the hill
    for (const BombView& b : v.bombs()) standing += b.owner == c.seat && b.tile.chebyshev_dist(own_hill.origin) <= kHomeFar ? 1u : 0u;
    for (const auto& j : jobs_) standing += j.second.tile.chebyshev_dist(own_hill.origin) <= kHomeFar ? 1u : 0u;
    if (standing >= plan.mine_home) return;
    const auto walkable = [&](sim::TileCoord t) {
        return grid.in_bounds(t) && own_field[static_cast<size_t>(t.y) * static_cast<size_t>(width) + static_cast<size_t>(t.x)] >= 0;
    };
    std::vector<Target> here;
    for (int32_t dy = -kHomeFar; dy <= kHomeFar; ++dy) {
        for (int32_t dx = -kHomeFar; dx <= kHomeFar; ++dx) {
            const sim::TileCoord t{own_hill.origin.x + dx, own_hill.origin.y + dy};
            const int32_t ring = t.chebyshev_dist(own_hill.origin);
            if (ring < kHomeNear || ring > kHomeFar || !grid.in_bounds(t)) continue;
            if (!grid.get_cell(t).can_place_bomb() || v.powerup_at(t) != nullptr || occupied(v, t)) continue;
            const auto cl = cool_.find(key_of(t));
            if (cl != cool_.end() && cl->second > now) continue;
            const size_t idx = static_cast<size_t>(t.y) * static_cast<size_t>(width) + static_cast<size_t>(t.x);
            if (own_field[idx] < 0) continue;
            int32_t lane = -1;                                                           // how far off the enemy's shortest way to the hill the tile lies (cost units); -1: no enemy comes this way
            for (size_t i = 0; i < foes.size(); ++i) {
                const int32_t f = (*foes[i])[idx];
                if (f < 0) continue;
                const int32_t off = f + own_field[idx] - foe_home[i];
                if (off <= kHomeLaneSlack && (lane < 0 || off < lane)) lane = std::max(off, 0);
            }
            if (lane < 0) continue;
            int32_t open = 0;
            for (int32_t ny = -1; ny <= 1; ++ny) {
                for (int32_t nx = -1; nx <= 1; ++nx) open += (nx != 0 || ny != 0) && walkable(sim::TileCoord{t.x + nx, t.y + ny}) ? 1 : 0;
            }
            if (open < 7) continue;
            bool by_pile = false;
            bool on_route = false;
            for (const PileView& p : v.piles()) {
                const PileInfo* info = c.map.pile(p.index);
                if (info == nullptr) continue;
                for (const sim::TileCoord& q : info->cells) by_pile = by_pile || q.chebyshev_dist(t) <= 2;
                // (plan.mine_home_off_route) nor on the way of the bot's own carriers to a pile: own bombs block their walks, and on a map whose food lies between the hills that way is also the
                // enemy's lane to the gate, where every bot of a room laid its field and all of them lost 17 percent of their food on TREASURE. The way is estimated: the walk to the tile plus
                // twenty cost units a tile of the straight rest of it costs no more than the walk to the pile and a little
                if (plan.mine_home_off_route && !by_pile && !on_route && p.remaining >= plan.mine_min_units && info->approach[c.seat].reachable() && !info->cells.empty()) {
                    int32_t near = 1 << 20;
                    for (const sim::TileCoord& q : info->cells) near = std::min(near, q.chebyshev_dist(t));
                    on_route = own_field[idx] + 20 * near <= info->approach[c.seat].cost + kRouteSlack;
                }
                if (by_pile || (on_route && !plan.home_on_route)) break;                          // (the tile is turned down whatever the other piles say)
            }
            if (by_pile || (on_route && !(plan.home_on_route && lane <= plan.home_route_lane))) continue;      // (plan.home_on_route: the way of the carriers is open to a mine where the enemy's shortest way runs)
            bool crowded = false;
            for (const BombView& b : v.bombs()) crowded = crowded || b.tile.chebyshev_dist(t) < static_cast<int32_t>(plan.mine_home_apart);
            for (const auto& j : jobs_) crowded = crowded || j.second.tile.chebyshev_dist(t) < static_cast<int32_t>(plan.mine_home_apart);
            if (crowded) continue;
            here.push_back(Target{t, 100 - lane - 12 * std::abs(ring - 7), true});          // on the lane and about seven tiles out first
        }
    }
    std::stable_sort(here.begin(), here.end(), [](const Target& x, const Target& y) { return x.score > y.score; });
    const uint32_t need = plan.mine_home - standing;
    std::vector<Target> chosen;
    for (const Target& t : here) {
        if (chosen.size() >= need) break;
        bool apart = true;
        for (const Target& o : chosen) apart = apart && o.tile.chebyshev_dist(t.tile) >= static_cast<int32_t>(plan.mine_home_apart);
        if (apart) chosen.push_back(t);
    }
    for (const Target& t : chosen) out.push_back(t);
}

bool home_mine_turn(const LevelPlan& plan, uint32_t home_laid, uint32_t other_laid) noexcept {
    return plan.home_first && plan.mine_home > 0 && home_laid < std::min<uint32_t>(plan.mine_home, other_laid / 2u + 2u);
}

void MineTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    for (auto it = cool_.begin(); it != cool_.end();) it = it->second <= now ? cool_.erase(it) : std::next(it);
    std::set<int64_t> mines;                                                             // the bot's own mines now; one that was there at the last look and is not any more went off or was defused
    for (const BombView& b : v.bombs()) {
        if (b.owner == c.seat) mines.insert(key_of(b.tile));
    }
    for (const int64_t k : known_) {
        if (mines.count(k) == 0) {
            ++gone_;
            cool_[k] = now + plan.mine_replant_ticks;
        }
    }
    known_ = mines;
    for (const uint32_t ant : c.ledger.ants_of(id())) {
        if (jobs_.count(ant) == 0) c.ledger.release(ant, id());
    }
    const bool on = (plan.mine_per_pile > 0 || plan.mine_gate > 0 || plan.behind_mine_gate > 0 || plan.mine_home > 0) && v.has_grid() && !tactics_.guard_stance && v.ticks_left() > 300;
    for (auto it = jobs_.begin(); it != jobs_.end();) {                                  // the jobs under way
        const Job& j = it->second;
        const AntView* a = find_ant(v.mine(), it->first);
        bool over = false;
        if (a == nullptr || c.ledger.owner(it->first) != id() || !on) {
            over = true;
        } else if (mines.count(key_of(j.tile)) != 0) {
            ++planted_;
            home_planted_ += j.home ? 1u : 0u;
            over = true;
        } else if (j.sent != kPending && ((now >= j.sent + 200u && a->idle()) || now >= j.sent + 700u)) {
            cool_[key_of(j.tile)] = now + 300u;                                          // it did not get there (or did nothing): the tile is left alone for a while
            ++failures_;
            over = true;
        } else if (j.sent == kPending && now > j.decided + 200u) {
            over = true;
        }
        if (over) {
            if (c.ledger.owner(it->first) == id()) c.ledger.release(it->first, id());
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
    const bool behind_free = plan.behind_war && tactics_.standing.war >= 1 && tactics_.standing.war >= plan.behind_free_tier;      // (a bot far behind lays mines with ants that have work: it will not out-eat the leader)
    const bool free_to_mine = !plan.war_free_only || tactics_.surplus > 0 || behind_free || behind_mining();
    if (!on || (!free_to_mine && !(plan.mine_home > 0 && now >= plan.mine_home_after))) return;
    std::vector<Target> targets;
    const bool home_turn = home_mine_turn(plan, home_planted_, planted_ - home_planted_);   // (plan.home_first: a home mine before a pile mine while fewer have been laid than the pile mines earn)
    if (home_turn) collect_home_targets(c, targets);
    if (targets.empty() && free_to_mine) collect_pile_targets(c, targets);
    if (targets.empty() && !home_turn) collect_home_targets(c, targets);                // (a few mines round the own hill: the Bomber has nothing else to do for them)
    if (targets.empty() && free_to_mine) collect_gate_targets(c, targets);
    if (targets.empty()) return;
    std::stable_sort(targets.begin(), targets.end(), [](const Target& x, const Target& y) { return x.score > y.score; });
    for (size_t i = 0; i < targets.size() && i < 3; ++i) {                               // one new job at a look
        const sim::TileCoord tile = targets[i].tile;
        const AntView* actor = nullptr;
        int32_t actor_d = 0;
        for (const AntView& a : v.mine()) {
            if (a.type != sim::AntType::Bomber || a.hp < 5 || a.holding || a.carried_points > 0 || !a.takes_orders() || a.state == sim::UnitState::Stunned || jobs_.count(a.id) != 0) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) > 1) continue;                  // (only an ant of the economy: the Bombers of the expedition, the power-up trips and the walls are theirs)
            const int32_t d = a.tile.chebyshev_dist(tile);
            if (actor == nullptr || d < actor_d) {
                actor = &a;
                actor_d = d;
            }
        }
        if (actor == nullptr) return;                                                    // no Bomber is free
        sim::Command probe;
        probe.type = sim::CommandType::GroupSpecial;
        probe.tile_x = static_cast<int16_t>(tile.x);
        probe.tile_y = static_cast<int16_t>(tile.y);
        probe.ants = {actor->id};
        if (v.predict_ack(probe) == 0) {
            cool_[key_of(tile)] = now + 100u;
            continue;
        }
        if (!c.ledger.take(actor->id, id())) continue;
        c.orders.special(actor->id, tile, Priority::Normal);
        Job j;
        j.tile = tile;
        j.decided = now;
        j.home = targets[i].home;
        jobs_[actor->id] = j;
        return;
    }
}

}  // namespace ants::ai
