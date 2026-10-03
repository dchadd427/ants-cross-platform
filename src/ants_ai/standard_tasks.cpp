#include "ants_ai/standard_tasks.hpp"

#include <algorithm>
#include <iterator>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

namespace {

constexpr uint8_t kMinFightHp = 4;            // an ant with fewer hit points than this is not sent to fight (it heals at its hill: below 2 the engine sends it there itself)
constexpr uint8_t kKeepFightHp = 3;           // ... and one that has fewer than this leaves a fight it is in
constexpr int32_t kThiefRadius = 16;          // an enemy Thief this close to the own raid tile is on its way (or standing in front of the hole)
constexpr uint64_t kShunTicks = 900;          // an enemy ant that no order reached is left alone this long (45 s)
constexpr uint64_t kBlowTicks = 6;            // an attack order is one blow: an ant that is idle this long after the order LEFT has done it (or cannot)
constexpr uint64_t kCantGoWindow = 24;        // an attack that shows "can't go" within this many ticks of leaving was refused by the path finder

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

bool on_hill_tile(const sim::Grid& grid, sim::TileCoord t) noexcept {
    for (const assets::AnthillSpawn& hill : grid.anthills()) {
        if (t.x >= static_cast<int32_t>(hill.x) && t.x <= static_cast<int32_t>(hill.x) + 3 && t.y >= static_cast<int32_t>(hill.y) && t.y <= static_cast<int32_t>(hill.y) + 3) return true;
    }
    return false;
}

}  // namespace

bool attackable(const BotView& view, const AntView& enemy) {
    if (enemy.team == view.seat() || (view.ally() < sim::MAX_PLAYERS && enemy.team == view.ally())) return false;
    if (view.powerup_at(enemy.tile) != nullptr) return false;
    switch (enemy.state) {
        case sim::UnitState::EnteringBase:
        case sim::UnitState::Flinch:
        case sim::UnitState::Knockback:
        case sim::UnitState::Burn:
        case sim::UnitState::Infiltrating:
        case sim::UnitState::Drowning:
        case sim::UnitState::Dead:
            return false;
        default:
            break;
    }
    if (!view.has_grid()) return true;
    const sim::Grid& grid = view.grid();
    if (on_hill_tile(grid, enemy.tile)) return false;
    if (grid.terrain_class_at(enemy.tile) == sim::movement::kTerrainWater) return false;
    return true;
}

bool can_fight(const BotView& view, const AntView& ant) {
    if (!ant.takes_orders() || ant.state == sim::UnitState::Stunned) return false;
    if (ant.holding || ant.carried_points > 0) return false;
    if (ant.hp < kMinFightHp) return false;
    if (ant.type != view.default_ant_type() && ant.type != sim::AntType::Combat) return false;
    if (view.powerup_at(ant.tile) != nullptr) return false;
    return true;
}

// ---- FightTask ------------------------------------------------------------------------------------------------------------------------------------------

bool FightTask::shunned(uint32_t enemy, uint64_t tick) const noexcept {
    const auto it = shunned_.find(enemy);
    return it != shunned_.end() && it->second > tick;
}

std::vector<uint32_t> FightTask::defenders_of(uint32_t enemy) const {
    std::vector<uint32_t> out;
    const auto it = fights_.find(enemy);
    if (it != fights_.end()) {
        for (const auto& d : it->second.defenders) out.push_back(d.first);
    }
    return out;
}

void FightTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupAttack) return;
    for (auto& f : fights_) {
        for (const uint32_t ant : command.ants) {
            const auto it = f.second.defenders.find(ant);
            if (it == f.second.defenders.end() || it->second.sent != kPending) continue;
            if (fate == Bot::Fate::Sent) it->second.sent = tick;                 // the clock of this blow starts when the order LEFT
            else it->second.ordered = false;                                     // it never left: the ant is ordered again at the next look
        }
    }
}

void FightTask::start_fights(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    if (v.mine().size() <= plan.fight_reserve + (v.eggs() == 0 ? 1u : 0u)) return;                 // the last ants stay out of every fight (a wipe-out forces a hatch of 200 points, or ends the team)
    // a blow: the attacker is the enemy ant that can be attacked and stands nearest to the place of the blow (within two tiles)
    for (const Hit& h : tactics_.memory.hits()) {
        const AntView* best = nullptr;
        int32_t best_d = 0;
        for (const AntView& e : v.others()) {
            const int32_t d = e.tile.chebyshev_dist(h.tile);
            if (d > 2 || !attackable(v, e) || shunned(e.id, now)) continue;
            if (best == nullptr || d < best_d) {                                 // (v.others() is by id, so the lower id wins a tie)
                best = &e;
                best_d = d;
            }
        }
        if (best == nullptr) continue;
        const auto it = fights_.find(best->id);
        if (it != fights_.end()) {
            it->second.last_alarm = now;
            continue;
        }
        Fight f;
        f.target = best->id;
        f.anchor = h.tile;
        f.started = now;
        f.last_alarm = now;
        fights_.emplace(best->id, std::move(f));
        ++fights_started_;
    }
    // a blow on an ant of the ally: answered by the ants that are near it (nobody walks across the map)
    if (plan.ally_help) {
        for (const sim::TileCoord& tile : tactics_.memory.ally_hits()) {
            const AntView* best = nullptr;
            int32_t best_d = 0;
            for (const AntView& e : v.others()) {
                const int32_t d = e.tile.chebyshev_dist(tile);
                if (d > 2 || !attackable(v, e) || shunned(e.id, now)) continue;
                if (best == nullptr || d < best_d) {
                    best = &e;
                    best_d = d;
                }
            }
            if (best == nullptr) continue;
            const auto it = fights_.find(best->id);
            if (it != fights_.end()) {
                it->second.last_alarm = now;
                continue;
            }
            bool near = false;
            for (const AntView& a : v.mine()) {
                if (can_fight(v, a) && a.tile.chebyshev_dist(best->tile) <= plan.ally_help_radius) near = true;
            }
            if (!near) continue;
            Fight f;
            f.target = best->id;
            f.anchor = tile;
            f.started = now;
            f.last_alarm = now;
            f.ally = true;
            fights_.emplace(best->id, std::move(f));
            ++fights_started_;
        }
    }
    // a thief on its way to the own hill (Medium and Hard)
    const HillInfo& hill = c.map.hill(c.seat);
    if (plan.intercepts && hill.present && v.has_grid()) {
        for (const ThiefSighting& t : tactics_.memory.thieves()) {
            if (t.carrying || t.tile.chebyshev_dist(hill.raid) > kThiefRadius) continue;
            const AntView* e = find_ant(v.others(), t.ant);
            // A thief that WALKS to the hill is not chased (measured: the defenders run after a tile that it has left and cross its path without a contact, the raid starts all the
            // same, and the ants are lost to the economy); it is stopped by the walls and hit by the guard. One that STANDS near the hill is a target that cannot run: it waits in
            // front of the walls ("can't go"), or for its next order, and may be attacked
            if (e == nullptr || e->state == sim::UnitState::Walking || !attackable(v, *e) || shunned(e->id, now)) continue;
            const auto it = fights_.find(e->id);
            if (it != fights_.end()) {
                it->second.last_alarm = now;
                it->second.thief = true;
                continue;
            }
            Fight f;
            f.target = e->id;
            f.thief = true;
            f.anchor = hill.origin;
            f.started = now;
            f.last_alarm = now;
            fights_.emplace(e->id, std::move(f));
            ++fights_started_;
        }
    }
}

void FightTask::run_fight(TaskContext& c, Fight& f, bool& end, std::vector<std::pair<sim::TileCoord, std::vector<uint32_t>>>& attacks) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const AntView* target = find_ant(v.others(), f.target);
    if (v.mine().size() <= plan.fight_reserve + (v.eggs() == 0 ? 1u : 0u)) {
        end = true;                                                              // down to the last ants: they leave the fight
        return;
    }
    if (target == nullptr || !attackable(v, *target) || shunned(f.target, now)) {
        end = true;                                                              // it died, left sight, stands on a power-up or went into a hill
        return;
    }
    if (f.thief) {
        const HillInfo& hill = c.map.hill(c.seat);
        if (target->holding || !hill.present || target->tile.chebyshev_dist(hill.raid) > kThiefRadius + 4 || now > f.last_alarm + plan.fight_linger_ticks) {
            end = true;                                                          // it has raided (it holds loot now), or it turned away
            return;
        }
    } else if (target->tile.chebyshev_dist(f.anchor) > plan.leash_tiles || now > f.last_alarm + plan.fight_linger_ticks) {
        end = true;                                                              // it left the leash, or the fight is over
        return;
    }
    // the defenders that are still ours and fit
    for (auto it = f.defenders.begin(); it != f.defenders.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || c.ledger.owner(it->first) != id() || a->hp < kKeepFightHp || a->holding || a->carried_points > 0) {
            if (a != nullptr) c.ledger.release(it->first, id());
            it = f.defenders.erase(it);
        } else {
            ++it;
        }
    }
    // an acknowledged attack that shows "can't go" soon after it left: no order reaches the target (it stands on something): leave it alone
    for (const auto& d : f.defenders) {
        if (d.second.sent == kPending || now < d.second.sent + 2 || now > d.second.sent + kCantGoWindow) continue;
        const AntView* a = find_ant(v.mine(), d.first);
        if (a != nullptr && a->state == sim::UnitState::CantGo) {
            shunned_[f.target] = now + kShunTicks;
            ++shunned_count_;
            end = true;
            return;
        }
    }
    // more defenders: Combat Ants first, then the nearest
    if (f.defenders.size() < plan.defenders) {
        std::vector<const AntView*> cands;
        for (const AntView& a : v.mine()) {
            if (f.defenders.count(a.id) != 0 || !can_fight(v, a)) continue;
            if (f.ally && a.tile.chebyshev_dist(target->tile) > plan.ally_help_radius) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= 3) continue;         // the ants of a pick-up, a raid or a fight are not taken
            cands.push_back(&a);
        }
        std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) {
            const bool cx = x->type == sim::AntType::Combat;
            const bool cy = y->type == sim::AntType::Combat;
            if (cx != cy) return cx;
            return x->tile.chebyshev_dist(target->tile) < y->tile.chebyshev_dist(target->tile);
        });
        for (const AntView* a : cands) {
            if (f.defenders.size() >= plan.defenders) break;
            if (!c.ledger.take(a->id, id())) continue;
            f.defenders[a->id] = Defender{};
        }
    }
    // the orders: an ant that was never ordered, or is idle again after its blow, gets a new attack order (one order is one blow)
    std::vector<uint32_t> list;
    for (auto& d : f.defenders) {
        const AntView* a = find_ant(v.mine(), d.first);
        if (a == nullptr) continue;
        bool need = false;
        if (!d.second.ordered) need = a->takes_orders();
        else if (d.second.sent != kPending) need = a->idle() && now >= d.second.sent + kBlowTicks && !v.has_pending_path(d.first);
        else need = now > d.second.decided + 200;                                // an order that no fate was ever reported for
        if (!need) continue;
        d.second.ordered = true;
        d.second.decided = now;
        d.second.sent = kPending;
        list.push_back(d.first);
    }
    if (!list.empty()) attacks.emplace_back(target->tile, std::move(list));
}

void FightTask::step(TaskContext& c) {
    const uint64_t now = c.view.tick();
    for (auto it = shunned_.begin(); it != shunned_.end();) {
        if (it->second <= now) it = shunned_.erase(it);
        else ++it;
    }
    start_fights(c);
    std::vector<std::pair<sim::TileCoord, std::vector<uint32_t>>> attacks;
    for (auto it = fights_.begin(); it != fights_.end();) {
        bool end = false;
        run_fight(c, it->second, end, attacks);
        if (end) {
            for (const auto& d : it->second.defenders) c.ledger.release(d.first, id());
            it = fights_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& a : attacks) {
        c.orders.attack(a.second, a.first, Priority::Urgent);
        ++attacks_ordered_;
    }
}

// ---- PowerUpTask ----------------------------------------------------------------------------------------------------------------------------------------

namespace {

// A worker that may be sent for a power-up: empty hands, healthy, takes orders, is not on a power-up tile
bool usable_for_powerup(const BotView& view, const AntView& a) {
    if (a.type != view.default_ant_type() || a.holding || a.carried_points > 0 || a.hp < 5) return false;
    if (!a.takes_orders() || a.state == sim::UnitState::Stunned) return false;
    return view.powerup_at(a.tile) == nullptr;
}

size_t tile_index(const sim::Grid& grid, sim::TileCoord t) noexcept { return static_cast<size_t>(t.y) * static_cast<size_t>(grid.width()) + static_cast<size_t>(t.x); }

}  // namespace

bool PowerUpTask::blacklisted(uint32_t ant, sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = black_.find({ant, key_of(tile)});
    return it != black_.end() && it->second > tick;
}

bool PowerUpTask::tile_blacklisted(sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = tile_black_.find(key_of(tile));
    return it != tile_black_.end() && it->second > tick;
}

std::vector<std::pair<uint32_t, sim::AntType>> PowerUpTask::on_their_way() const {
    std::vector<std::pair<uint32_t, sim::AntType>> out;
    for (const auto& t : takes_) out.emplace_back(t.first, t.second.kind);
    return out;
}

void PowerUpTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove || command.ants.size() != 1) return;
    const auto it = takes_.find(command.ants[0]);
    if (it == takes_.end() || it->second.sent != kPending || command.tile_x != it->second.tile.x || command.tile_y != it->second.tile.y) return;
    if (fate == Bot::Fate::Sent) it->second.sent = tick;              // the clock of the walk starts when the order LEFT
    else takes_.erase(it);                                            // it never left: the ant was not told anything (its claim is given back at the next look)
}

const std::vector<int32_t>& PowerUpTask::field_for(TaskContext& c, sim::TileCoord tile) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (mask_tick_ != now) {
        mask_ = MapInfo::walkable_mask(v.grid(), c.seat, v.walk_context());
        mask_tick_ = now;
    }
    Cached& e = fields_[key_of(tile)];
    if (e.field.empty() || now >= e.tick + params_.field_ttl_ticks) {
        e.field = MapInfo::cost_field_onto(v.grid(), mask_, c.seat, tile, v.walk_context());
        e.tick = now;
    }
    return e.field;
}

bool PowerUpTask::try_start(TaskContext& c, sim::AntType kind) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    std::vector<const PowerUpView*> cands;
    for (const PowerUpView& p : v.powerups()) {
        if (p.kind != kind || p.standing_ant != 0 || tile_blacklisted(p.tile, now)) continue;
        bool taken = false;
        for (const auto& t : takes_) taken = taken || t.second.tile == p.tile;
        if (!taken) cands.push_back(&p);
    }
    if (cands.empty()) return false;
    std::vector<const AntView*> ants;
    for (const AntView& a : v.mine()) {
        if (!usable_for_powerup(v, a) || takes_.count(a.id) != 0) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;     // the ants of a fight, the walls and the raids are not taken
        ants.push_back(&a);
    }
    if (ants.empty()) return false;
    const auto nearest = [&](const PowerUpView* p) {
        int32_t best = 1 << 20;
        for (const AntView* a : ants) best = std::min(best, a->tile.chebyshev_dist(p->tile));
        return best;
    };
    std::stable_sort(cands.begin(), cands.end(), [&](const PowerUpView* x, const PowerUpView* y) { return nearest(x) < nearest(y); });
    const sim::Grid& grid = v.grid();
    for (size_t i = 0; i < cands.size() && i < 3; ++i) {
        const PowerUpView& p = *cands[i];
        const std::vector<int32_t>& f = field_for(c, p.tile);
        if (f.empty()) continue;
        const AntView* best = nullptr;
        int32_t best_cost = 0;
        for (const AntView* a : ants) {                                                 // the closest ant by the walking cost from its own tile (a lower id wins a tie)
            if (blacklisted(a->id, p.tile, now) || !grid.in_bounds(a->tile)) continue;
            const int32_t cost = f[tile_index(grid, a->tile)];
            if (cost < 0 || (best != nullptr && cost >= best_cost)) continue;
            best = a;
            best_cost = cost;
        }
        if (best == nullptr) continue;
        const uint32_t walk = static_cast<uint32_t>(MapInfo::walking_ticks(best_cost)) + 5u;           // the pick-up comes 8 d + 5 ticks after the order on grass (POWERUPS D3)
        if (walk > params_.max_trip_ticks) continue;
        if (static_cast<uint64_t>(v.ticks_left()) < walk + 17u + 2u * c.profile.reaction_delay + 100u) continue;
        // a contest: an enemy ant whose walk to the power-up is no longer than ours (the engine's walking speed, a tenth quicker for the diagonal runs of the estimate)
        bool lost = false;
        for (const AntView& e : v.others()) {
            if (e.holding || !grid.in_bounds(e.tile) || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) continue;
            if (e.state == sim::UnitState::EnteringBase || e.state == sim::UnitState::PoweringUp) continue;
            const int32_t cost = f[tile_index(grid, e.tile)];
            if (cost < 0) continue;
            const uint32_t theirs = static_cast<uint32_t>(MapInfo::walking_ticks(cost)) * 9u / 10u + 5u;
            if (theirs <= walk + c.profile.reaction_delay) {
                lost = true;
                break;
            }
        }
        if (lost) {
            tile_black_[key_of(p.tile)] = now + 200;
            ++contested_;
            continue;
        }
        if (!c.ledger.take(best->id, id())) continue;
        c.orders.pick_up(best->id, p.tile, Priority::Normal);
        Take t;
        t.tile = p.tile;
        t.kind = kind;
        t.before = best->type;
        t.decided = now;
        t.walk_ticks = walk;
        takes_[best->id] = t;
        ++started_;
        return true;
    }
    return false;
}

void PowerUpTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const Profile& profile = c.profile;
    for (const uint32_t ant : c.ledger.ants_of(id())) {                                 // an ant whose trip is over (or never left) is free again
        if (takes_.count(ant) == 0) c.ledger.release(ant, id());
    }
    for (auto it = black_.begin(); it != black_.end();) it = it->second <= now ? black_.erase(it) : std::next(it);
    for (auto it = tile_black_.begin(); it != tile_black_.end();) it = it->second <= now ? tile_black_.erase(it) : std::next(it);
    for (auto it = fields_.begin(); it != fields_.end();) it = now >= it->second.tick + 5u * params_.field_ttl_ticks ? fields_.erase(it) : std::next(it);

    // 1. the trips that are under way
    const uint64_t stale_after = profile.reaction_delay + profile.reaction_delay * profile.jitter_percent / 100u + profile.intent_ttl + profile.decision_interval + 8u;
    std::vector<uint32_t> call_back;
    for (auto it = takes_.begin(); it != takes_.end();) {
        const uint32_t ant = it->first;
        Take& t = it->second;
        const AntView* a = find_ant(v.mine(), ant);
        bool over = false;
        if (a == nullptr) {
            over = true;                                                                // it died
        } else if (a->type != t.before) {
            ++taken_;                                                                   // it is a typed ant now: the task of its role claims it
            over = true;
        } else if (c.ledger.owner(ant) != id()) {
            over = true;                                                                // a task of a higher rank (a fight) took it
        } else if (t.sent == kPending && now >= t.decided + stale_after) {
            over = true;                                                                // the order never left, and no fate was reported (a bot driven without the controller)
        } else {
            const PowerUpView* p = v.powerup_at(t.tile);
            if (p == nullptr) {
                over = true;                                                            // somebody took it, or the dropper replaced it with nothing
                tile_black_[key_of(t.tile)] = now + 60;
            } else if (p->standing_ant != 0 && p->standing_ant != ant) {
                over = true;                                                            // somebody stands on it: nobody else can take it
                tile_black_[key_of(t.tile)] = now + 400;
                ++contested_;
            } else if (p->kind != t.kind && a->tile.chebyshev_dist(t.tile) >= static_cast<int32_t>(params_.call_back_tiles)) {
                over = true;                                                            // a dropper changed what lies there: a plain order elsewhere cancels the walk before it crosses
                call_back.push_back(ant);
                tile_black_[key_of(t.tile)] = now + 100;
                ++called_back_;
            } else if (t.sent != kPending) {
                const uint64_t due = t.sent + t.walk_ticks + 17u + 40u;
                const bool refused = a->state == sim::UnitState::CantGo && now >= t.sent + 8u;
                if (refused || (now > due && a->idle())) {
                    black_[{ant, key_of(t.tile)}] = now + params_.blacklist_ticks;      // this ant could not take it: the pair is left alone
                    tile_black_[key_of(t.tile)] = now + params_.tile_blacklist_ticks;
                    ++failed_;
                    over = true;
                } else if (now > due + 300u) {
                    call_back.push_back(ant);                                           // a walk that does not end: stop it
                    black_[{ant, key_of(t.tile)}] = now + params_.blacklist_ticks;
                    ++failed_;
                    over = true;
                }
            }
        }
        if (over) {
            c.ledger.release(ant, id());
            it = takes_.erase(it);
        } else {
            ++it;
        }
    }
    if (!call_back.empty()) c.orders.stop(call_back);

    // 2. new trips for what is wanted and missing, in the order of value (Fire, Bomber, Thief, Combat, Swimmer), as many as max_active allows: the opening sends them all at the first look
    for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer}) {
        const size_t want = tactics_.wants[static_cast<size_t>(kind)];
        if (want == 0) continue;
        size_t have = 0;
        for (const AntView& a : v.mine()) have += a.type == kind ? 1u : 0u;
        for (const auto& t : takes_) have += t.second.kind == kind ? 1u : 0u;
        while (have < want && takes_.size() < params_.max_active) {
            if (!try_start(c, kind)) break;
            ++have;
        }
    }
}

// ---- WallTask -------------------------------------------------------------------------------------------------------------------------------------------

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
            const int harm = harm_to(v, c.map, plan, w.tile);                              // the own economy first, then the ally's
            if (harm == 0) continue;
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
        if (v.predict_ack(probe) == 0) {                                                  // the cursor would not show the target cursor there (an ant stands on it): later
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
        if (v.predict_ack(probe) == 0) return;
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

int harm_to(const BotView& view, const MapInfo& map, const LevelPlan& plan, sim::TileCoord tile) noexcept {
    if (harms_economy(view, map, plan, tile)) return 1;
    const uint8_t ally = view.ally();
    if (!plan.ally_help || ally >= sim::MAX_PLAYERS) return 0;
    const HillInfo& hill = map.hill(ally);
    if (hill.present && tile.chebyshev_dist(hill.origin) <= plan.counter_hill_radius) return 2;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map.pile(p.index);
        if (info == nullptr || !info->approach[ally].reachable()) continue;
        if (tile.chebyshev_dist(p.anchor) <= plan.counter_pile_radius + 1) return 2;
    }
    return 0;
}

bool harms_economy(const BotView& view, const MapInfo& map, const LevelPlan& plan, sim::TileCoord tile) noexcept {
    const HillInfo& hill = map.hill(view.seat());
    if (hill.present && tile.chebyshev_dist(hill.origin) <= plan.counter_hill_radius) return true;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map.pile(p.index);
        if (info == nullptr || !info->approach[view.seat()].reachable()) continue;
        if (tile.chebyshev_dist(p.anchor) <= plan.counter_pile_radius + 1) return true;
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
    for (const BombView& b : v.bombs()) {
        if (b.owner == c.seat || (v.ally() < sim::MAX_PLAYERS && b.owner == v.ally())) continue;       // an own or an allied bomb is no enemy's
        const auto bl = black_.find(key_of(b.tile));
        if (bl != black_.end() && bl->second > now) continue;
        const int harm = harm_to(v, c.map, plan, b.tile);                                              // the own economy first, then the ally's (only a Bomber goes there)
        if (harm == 0) continue;
        const int32_t d = hill.present ? b.tile.chebyshev_dist(hill.origin) : 0;
        if (pick == nullptr || harm < best_harm || (harm == best_harm && d < best)) {
            pick = &b;
            best = d;
            best_harm = harm;
        }
    }
    if (pick == nullptr) return;
    // a Bomber defuses it; without one a healthy idle worker sets it off
    const AntView* actor = nullptr;
    bool defuse = false;
    int32_t actor_d = 0;
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Bomber || a.hp < 3 || a.holding || a.carried_points > 0 || !a.takes_orders() || a.state == sim::UnitState::Stunned) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
        const int32_t d = a.tile.chebyshev_dist(pick->tile);
        if (actor == nullptr || d < actor_d) {
            actor = &a;
            actor_d = d;
            defuse = true;
        }
    }
    if (actor == nullptr && plan.bomb_hit && best_harm == 1) {                                         // (a worker does not walk to the ally's base to set a bomb off)
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
        if (v.predict_ack(probe) == 0) {
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

// ---- RaidTask -------------------------------------------------------------------------------------------------------------------------------------------

bool RaidTask::black(uint8_t team, uint64_t tick) const noexcept {
    const auto it = black_.find(team);
    return it != black_.end() && it->second > tick;
}

void RaidTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupSpecial || command.ants.size() != 1) return;
    const auto it = raids_.find(command.ants[0]);
    if (it == raids_.end() || it->second.sent != kPending) return;
    if (fate == Bot::Fate::Sent) it->second.sent = tick;
    else raids_.erase(it);                                              // it never left: the thief is ordered again at the next look
}

bool RaidTask::launch(TaskContext& c, const AntView& thief) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    const LevelPlan& plan = tactics_.plan;
    // the teams that may be raided, the leading one first (the score boxes: a team that is not in the match, has dropped out or is an ally shows nothing or is no victim)
    std::vector<uint8_t> teams;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally())) continue;
        if (row.score < static_cast<int32_t>(params_.min_loot) || black(t, now)) continue;
        teams.push_back(t);
    }
    if (teams.empty()) return false;
    std::stable_sort(teams.begin(), teams.end(), [&](uint8_t x, uint8_t y) { return v.rows()[x].score > v.rows()[y].score; });
    const std::vector<uint8_t> mask = MapInfo::walkable_mask(grid, c.seat, v.walk_context());
    const std::vector<int32_t> field = MapInfo::cost_field(grid, mask, thief.tile);
    if (field.empty()) return false;
    for (const uint8_t t : teams) {
        const HillInfo& hill = c.map.hill(t);
        if (!hill.present || east_state(grid, hill).shut()) continue;                  // walls or bombs in front of the hole: the order would end in "Can't go there."
        int32_t best = -1;
        for (const sim::TileCoord& e : east_tiles(hill)) {
            if (!grid.in_bounds(e) || e.x < 0) continue;
            const int32_t cost = field[tile_index(grid, e)];
            if (cost >= 0 && (best < 0 || cost < best)) best = cost;
        }
        if (best < 0) continue;                                                        // not on foot from where the thief stands
        const int32_t home = c.map.walking_cost(c.seat, east_tiles(hill)[1]);          // back to the own gate (the analysis of the start; the way is the same both ways)
        const uint64_t go = static_cast<uint64_t>(MapInfo::walking_ticks(best)) + 10u;
        const uint64_t back = home < 0 ? go : static_cast<uint64_t>(MapInfo::walking_ticks(home)) + 30u;
        const uint64_t round = go + 80u + back + 30u;
        if (static_cast<uint64_t>(v.ticks_left()) < round + 2u * c.profile.reaction_delay + 60u) continue;
        if (plan.avoids_guarded_hills) {
            bool guarded = false;
            for (const AntView& e : v.others()) {
                if (e.team == t && e.type == sim::AntType::Combat && e.tile.chebyshev_dist(hill.raid) <= static_cast<int32_t>(params_.guard_radius)) guarded = true;
            }
            if (guarded) continue;
        }
        c.orders.special(thief.id, hill.entrance, Priority::Normal);
        Raid r;
        r.team = t;
        r.decided = now;
        r.origin = thief.tile;
        raids_[thief.id] = r;
        ++raids_ordered_;
        last_target_ = t;
        return true;
    }
    return false;
}

void RaidTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!v.has_grid()) return;
    for (auto it = black_.begin(); it != black_.end();) it = it->second <= now ? black_.erase(it) : std::next(it);
    // the thieves of the bot: one is claimed only for a raid that is under way or about to be ordered; between raids (and while no hill is worth a trip) it harvests like a worker. A
    // task of a higher rank may take one for a fight: then it is not ours
    for (auto it = raids_.begin(); it != raids_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || a->type != sim::AntType::Thief || c.ledger.owner(it->first) != id()) it = raids_.erase(it);
        else ++it;
    }
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Thief) continue;
        const bool held = c.ledger.owner(a.id) == id();
        const auto it = raids_.find(a.id);
        if (it != raids_.end()) {
            Raid& r = it->second;
            if (a.state == sim::UnitState::Infiltrating) continue;                      // the raid clip: nothing to do (and nothing can be done)
            if (a.holding || a.carried_points > 0) {                                    // it has the loot: the engine walks it home
                raids_.erase(it);
                continue;
            }
            if (r.sent != kPending && a.idle() && now >= r.sent + 40u) {
                if (a.tile.chebyshev_dist(r.origin) <= 1) {                             // it never got going: the way is shut (a wall that came up, nobody can reach)
                    black_[r.team] = now + params_.black_ticks;
                    ++failures_;
                }
                raids_.erase(it);                                                       // (or the raid is over and it stands empty-handed: the victim had nothing)
            } else if (r.sent == kPending && now > r.decided + 200u) {
                raids_.erase(it);
            }
            continue;
        }
        const bool free_to_go = a.idle() && !a.holding && a.carried_points == 0 && a.hp >= 4 && a.takes_orders() && v.powerup_at(a.tile) == nullptr;
        if (!free_to_go) {
            if (held) c.ledger.release(a.id, id());                                     // walking, carrying, hurt: the economy's again
            continue;
        }
        if (!held && !c.ledger.take(a.id, id())) continue;                              // a task of a higher rank has it
        if (!launch(c, a)) c.ledger.release(a.id, id());                                // nothing to raid: back to the economy
    }
}

// ---- StrikeTask -----------------------------------------------------------------------------------------------------------------------------------------

void StrikeTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupAttack && command.type != sim::CommandType::GroupMove) return;
    for (const uint32_t ant : command.ants) {
        const auto it = force_.find(ant);
        if (it == force_.end() || it->second.sent != kPending) continue;
        if (fate == Bot::Fate::Sent) it->second.sent = tick;
        else it->second.ordered = false;
    }
}

void StrikeTask::disband(TaskContext& c) {
    for (const auto& m : force_) c.ledger.release(m.first, id());
    force_.clear();
    active_ = false;
    wipe_ = false;
    target_ = -1;
    tactics_.strike_active = false;
}

namespace {
// The strength of an own ant in a strike: a Combat Ant hits twice as hard as any other
uint32_t own_weight(sim::AntType type) noexcept { return type == sim::AntType::Combat ? 8u : 4u; }
}  // namespace

// The strength of an enemy ant as far as a person can tell: a Combat Ant counts as much as an own one, an ant that was drawn in its attack clip lately fights, any other ant is taken
// as one that would not fight back unless ordered (a quarter): the workers that queue at a gate are not an army
uint32_t StrikeTask::enemy_weight(const AntView& e, uint64_t now) const {
    if (e.type == sim::AntType::Combat) return 8u;
    return tactics_.memory.fought_lately(e.id, 600) || e.state == sim::UnitState::Attacking ? 4u : (now != 0 ? 1u : 1u);
}

void StrikeTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const Standing& st = tactics_.standing;
    tactics_.strike_active = false;
    if (!v.has_grid() || !(plan.strikes || plan.wipe_focus) || plan.strike_force == 0) {
        if (!force_.empty()) disband(c);
        return;
    }
    const sim::Grid& grid = v.grid();

    // 1. the members that are still ours and fit
    for (auto it = force_.begin(); it != force_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool gone = a == nullptr || c.ledger.owner(it->first) != id() || a->hp < 4 || a->holding || a->carried_points > 0;
        if (gone) {
            if (a != nullptr && c.ledger.owner(it->first) == id()) c.ledger.release(it->first, id());
            it = force_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. the ants that could be in the force, the strongest first (members, Combat Ants), and how many may go
    std::vector<const AntView*> cands;
    for (const AntView& a : v.mine()) {
        if (force_.count(a.id) == 0) {
            if (!can_fight(v, a) || a.hp < 7) continue;
            if (!plan.strike_workers && a.type != sim::AntType::Combat) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= 3) continue;           // the ants of a pick-up, a raid, a wall, a bomb or a fight are not taken
        }
        cands.push_back(&a);
    }
    const size_t total = v.mine().size();
    const size_t limit = std::min<size_t>(plan.strike_force, total > plan.strike_reserve ? total - plan.strike_reserve : 0);
    std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) {
        const bool mx = force_.count(x->id) != 0;
        const bool my = force_.count(y->id) != 0;
        if (mx != my) return mx;
        return (x->type == sim::AntType::Combat) > (y->type == sim::AntType::Combat);
    });
    if (cands.size() > limit) cands.resize(limit);
    uint32_t own = 0;
    for (const AntView* a : cands) own += own_weight(a->type);

    // 3. what to hunt: the leader's carriers when the bot is clearly behind, a team that shows very few ants when the force is far stronger
    int target = -1;
    bool wipe = false;
    const bool time_ok = v.ticks_left() >= plan.strike_stop_ticks && now >= paused_until_;
    if (time_ok && plan.wipe_focus && cands.size() >= 3) {
        std::array<uint32_t, sim::MAX_PLAYERS> ants{};
        std::array<uint32_t, sim::MAX_PLAYERS> strength{};
        for (const AntView& e : v.others()) {
            ++ants[e.team];
            strength[e.team] += enemy_weight(e, now);
        }
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const TeamRow& row = v.rows()[t];
            if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || ants[t] == 0 || ants[t] > plan.wipe_max_ants) continue;
            if (static_cast<uint64_t>(ants[t]) * 2u > tactics_.memory.ants_peak(t)) continue;      // "very few ants LEFT": the team has lost at least half of what it showed
            if (!c.map.hill(t).present || own < strength[t] * 3u) continue;
            if (target < 0 || ants[t] < ants[static_cast<size_t>(target)]) target = t;
        }
        wipe = target >= 0;
    }
    uint32_t enemy_near = 0;
    if (!wipe && time_ok && plan.strikes && st.behind && st.leader >= 0 && c.map.hill(static_cast<uint8_t>(st.leader)).present) {
        target = st.leader;
        const HillInfo& hill = c.map.hill(static_cast<uint8_t>(target));
        const uint8_t friend_of_target = v.rows()[static_cast<size_t>(target)].ally;
        for (const AntView& e : v.others()) {
            if (e.team != target && e.team != friend_of_target) continue;
            if (e.tile.chebyshev_dist(hill.queue) <= plan.strike_radius) enemy_near += enemy_weight(e, now);
        }
        const uint32_t need = active_ ? 100u : plan.strike_odds_percent;
        if (cands.size() < plan.strike_min_force || static_cast<uint64_t>(own) * 100u < static_cast<uint64_t>(enemy_near) * need) {
            if (active_) {
                ++calls_off_;
                paused_until_ = now + 600u;
            }
            target = -1;                                                           // not winnable: no strike
        }
    }
    if (target < 0) {
        if (active_) {
            if (!wipe && !(st.behind)) ++calls_off_;
            disband(c);
        } else if (!force_.empty()) {
            disband(c);
        }
        return;
    }
    if (!active_ || target != target_ || wipe != wipe_) {
        if (!active_) ++strikes_started_;
        active_ = true;
        target_ = target;
        wipe_ = wipe;
    }
    tactics_.strike_active = true;

    // 4. the force: the members that fit, then the recruits, nearest to the target hill first among equals
    const HillInfo& hill = c.map.hill(static_cast<uint8_t>(target));
    for (auto it = force_.begin(); it != force_.end();) {
        bool keep = false;
        for (const AntView* a : cands) keep = keep || a->id == it->first;
        if (!keep) {
            c.ledger.release(it->first, id());
            it = force_.erase(it);
        } else {
            ++it;
        }
    }
    for (const AntView* a : cands) {
        if (force_.count(a->id) != 0) continue;
        if (!c.ledger.take(a->id, id())) continue;
        force_[a->id] = Member{};
    }
    if (force_.empty()) return;

    // 5. the targets: carriers of the target team (and its ally) near its hill; in a wipe every attackable ant of the team
    std::vector<const AntView*> targets;
    const uint8_t friend_team = v.rows()[static_cast<size_t>(target)].ally;
    for (const AntView& e : v.others()) {
        if (!attackable(v, e)) continue;
        if (wipe) {
            if (e.team == target) targets.push_back(&e);
        } else if ((e.team == target || e.team == friend_team) && e.holding && e.tile.chebyshev_dist(hill.queue) <= plan.strike_radius) {
            targets.push_back(&e);
        }
    }
    std::map<uint32_t, std::vector<uint32_t>> groups;                              // target -> attackers
    std::vector<uint32_t> waiting;
    for (auto& f : force_) {
        const AntView* a = find_ant(v.mine(), f.first);
        if (a == nullptr) continue;
        Member& mem = f.second;
        bool need = false;
        if (!mem.ordered) need = a->takes_orders();
        else if (mem.sent != kPending) need = a->idle() && now >= mem.sent + 6u && !v.has_pending_path(a->id);
        else need = now > mem.decided + 200u;
        if (!need) continue;
        const AntView* best = nullptr;
        int32_t best_d = 0;
        for (const AntView* e : targets) {
            const int32_t d = a->tile.chebyshev_dist(e->tile);
            if (best == nullptr || d < best_d) {
                best = e;
                best_d = d;
            }
        }
        mem.ordered = true;
        mem.decided = now;
        mem.sent = kPending;
        if (best != nullptr) {
            mem.target = best->id;
            groups[best->id].push_back(a->id);
        } else {
            mem.target = 0;
            waiting.push_back(a->id);
        }
    }
    for (const auto& g : groups) {
        const AntView* e = find_ant(v.others(), g.first);
        if (e == nullptr) continue;
        c.orders.attack(g.second, e->tile, Priority::Urgent);
        ++attacks_ordered_;
    }
    // no target in sight: wait three tiles in front of the hill, on the side the carriers come from (towards the middle of the map)
    if (!waiting.empty()) {
        const int32_t sx = hill.queue.x * 2 < static_cast<int32_t>(grid.width()) ? 1 : -1;
        const int32_t sy = hill.queue.y * 2 < static_cast<int32_t>(grid.height()) ? 1 : -1;
        sim::TileCoord station{hill.queue.x + sx * 3, hill.queue.y + sy * 3};
        for (int32_t k = 0; k < 6 && (!MapInfo::walkable(grid, c.seat, station, v.walk_context()) || v.powerup_at(station) != nullptr); ++k) station = sim::TileCoord{station.x + sx, station.y + sy};
        for (const uint32_t id_ : waiting) {
            const AntView* a = find_ant(v.mine(), id_);
            if (a == nullptr || a->tile.chebyshev_dist(station) <= 2) continue;
            c.orders.move({id_}, station, Priority::Normal);
        }
    }
}

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
    if (!(fight || tactics_.strike_active)) return;
    const size_t want = std::max<size_t>(plan.hatch_floor, start_ants_ + (tactics_.strike_active ? plan.hatch_extra : 0u));
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

void GateTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (fate != Bot::Fate::Sent || command.type != sim::CommandType::GroupMove) return;
    for (const uint32_t id : command.ants) {
        const auto it = cmd_.find(id);
        if (it == cmd_.end()) continue;
        Cmd& cm = it->second;
        if (cm.tile.x != command.tile_x || cm.tile.y != command.tile_y) continue;      // (an order of another task that this one did not give)
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

void GateTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
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
    int slots_walkable = 0;
    for (int i = 0; i < 3; ++i) {
        g.slot_ok[static_cast<size_t>(i)] = MapInfo::walkable(grid, c.seat, queue_tile(g, i), v.walk_context()) && v.powerup_at(queue_tile(g, i)) == nullptr;
        slots_walkable += g.slot_ok[static_cast<size_t>(i)] ? 1 : 0;
    }
    bool buffer_found = false;
    for (int32_t dy = 0; dy <= 3 && !buffer_found; ++dy) {
        for (const int32_t dx : {0, -1, 1}) {
            const sim::TileCoord t{g.hill.x + 1 + dx, g.hill.y - params_.buffer_rows - (dy % 2 == 0 ? dy / 2 : -(dy + 1) / 2)};
            if (!grid.in_bounds(t) || !MapInfo::walkable(grid, c.seat, t, v.walk_context()) || v.powerup_at(t) != nullptr || cost_of(g, t) < 0) continue;
            g.buffer = t;
            buffer_found = true;
            break;
        }
    }
    if (slots_walkable < 2 || !buffer_found) return;                                   // no room at the doorstep: the engine's flow stays
    track_exits(c, g);

    // what the ants do: a bite that runs, the entrance occupied, a clip that was first seen now
    bool bite = false;
    bool entrance_occupied = false;
    std::vector<const AntView*> carriers;
    for (const AntView& a : v.mine()) {
        if (a.state == sim::UnitState::HarvestingFood) bite = true;
        if (a.tile == g.entrance) entrance_occupied = true;
        if (a.state == sim::UnitState::EnteringBase) {
            if (clip_seen_.count(a.id) == 0) {
                clip_seen_[a.id] = now;
                pending_free_at_ = static_cast<int64_t>(now) + params_.clip_ticks + params_.exit_ticks;
            }
        }
        if (a.holding && a.state != sim::UnitState::EnteringBase && a.tile != g.entrance) carriers.push_back(&a);          // (a thief with loot banks at the entrance too)
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
                }
            }
        }
    }
    if (!entrance_occupied) pending_free_at_ = 0;
    const bool gate_free = !entrance_occupied && !user_active;
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
    if ((gate_free || predicted) && best != nullptr) {
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

void CarrierAidTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    if (!tactics_.plan.carrier_aid || !hill.present) return;
    for (const Hit& h : tactics_.memory.hits()) {
        if (h.carried && h.hp_now > 0) aid_[h.ant] = Aid{now, 0, 0};
    }
    std::vector<uint32_t> home;
    for (auto it = aid_.begin(); it != aid_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool carrying = a != nullptr && (a->holding || a->carried_points > 0);
        if (a == nullptr || !carrying || it->second.tries >= 4 || now > it->second.hit + 800) {
            it = aid_.erase(it);                                                         // gone, delivered (or dropped its food), or helped as often as it makes sense
            continue;
        }
        // the blow's stun lasts about 12 ticks; an ant that is idle after it has lost its walk
        if (a->idle() && now >= it->second.hit + 8 && now >= it->second.last_order + 40 && !v.has_pending_path(a->id)) {
            home.push_back(a->id);
            it->second.last_order = now;
            ++it->second.tries;
        }
        ++it;
    }
    if (!home.empty()) {
        c.orders.move(home, hill.entrance, Priority::Normal);
        sent_home_ += static_cast<uint32_t>(home.size());
    }
}

}  // namespace ants::ai
