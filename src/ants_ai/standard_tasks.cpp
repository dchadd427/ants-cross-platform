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

    // 2. a new trip for what is wanted and missing, one at a look
    if (takes_.size() >= params_.max_active) return;
    for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Thief, sim::AntType::Combat}) {
        const size_t want = tactics_.wants[static_cast<size_t>(kind)];
        if (want == 0) continue;
        size_t have = 0;
        for (const AntView& a : v.mine()) have += a.type == kind ? 1u : 0u;
        for (const auto& t : takes_) have += t.second.kind == kind ? 1u : 0u;
        if (have >= want) continue;
        if (try_start(c, kind)) return;
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
        for (const FireWallView& w : v.fire_walls()) {
            bool ours = false;
            for (const sim::TileCoord& e : tiles) ours = ours || e == w.tile;
            if (ally_hill != nullptr) {
                for (const sim::TileCoord& e : east_tiles(*ally_hill)) ours = ours || e == w.tile;
            }
            const auto b = counter_black_.find(static_cast<int64_t>(w.tile.y) * 4096 + w.tile.x);
            if (ours || (b != counter_black_.end() && b->second > now) || !harms_economy(v, c.map, plan, w.tile)) continue;
            const int32_t d = keeper->tile.chebyshev_dist(w.tile);
            if (!have_counter || d < best) {
                have_counter = true;
                best = d;
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
    for (const BombView& b : v.bombs()) {
        if (b.owner == c.seat || (v.ally() < sim::MAX_PLAYERS && b.owner == v.ally())) continue;       // an own or an allied bomb is no enemy's
        const auto bl = black_.find(key_of(b.tile));
        if ((bl != black_.end() && bl->second > now) || !harms_economy(v, c.map, plan, b.tile)) continue;
        const int32_t d = hill.present ? b.tile.chebyshev_dist(hill.origin) : 0;
        if (pick == nullptr || d < best) {
            pick = &b;
            best = d;
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
    if (actor == nullptr && plan.bomb_hit) {
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
    // the thieves of the bot: claimed by this task (a task of a higher rank may take one for a fight: then it is not ours)
    for (auto it = raids_.begin(); it != raids_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || a->type != sim::AntType::Thief || c.ledger.owner(it->first) != id()) it = raids_.erase(it);
        else ++it;
    }
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Thief) continue;
        if (c.ledger.owner(a.id) != id() && !c.ledger.take(a.id, id())) continue;
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
        if (!a.idle() || a.holding || a.carried_points > 0 || a.hp < 4 || !a.takes_orders()) continue;
        if (v.powerup_at(a.tile) != nullptr) continue;
        launch(c, a);
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

void HarassTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const Memory& m = tactics_.memory;
    if (!v.has_grid() || plan.harassers == 0) return;
    const sim::Grid& grid = v.grid();
    // the squad's members that are still ours and fit; a member that was hit calls the squad off for a while (the enemy fights back: the hunt does not pay)
    for (auto it = squad_.begin(); it != squad_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool gone = a == nullptr || c.ledger.owner(it->first) != id() || a->hp < 5 || a->holding || a->carried_points > 0;
        if (gone) {
            if (a != nullptr) c.ledger.release(it->first, id());
            it = squad_.erase(it);
        } else {
            ++it;
        }
    }
    for (const Hit& h : m.hits()) {
        if (squad_.count(h.ant) != 0) {
            paused_until_ = now + 1200u;
            ++calls_off_;
        }
    }
    const bool off = now < paused_until_ || m.combat_last_seen() != 0 || v.ticks_left() < 900;
    if (off) {
        for (const auto& s : squad_) c.ledger.release(s.first, id());
        squad_.clear();
        return;
    }
    // recruits: Combat Ants first, then the idle workers that the economy has no pile for (they cost nothing), from a workforce that can afford them
    if (squad_.size() < plan.harassers && (tactics_.surplus > 0 || plan.harass_min_workers == 0 || squad_.empty() == false || true)) {
        size_t workers = 0;
        for (const AntView& a : v.mine()) workers += a.type == v.default_ant_type() ? 1u : 0u;
        std::vector<const AntView*> cands;
        for (const AntView& a : v.mine()) {
            if (squad_.count(a.id) != 0 || !can_fight(v, a) || a.hp < 6) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            if (a.type != sim::AntType::Combat && workers < plan.harass_min_workers) continue;
            if (a.type != sim::AntType::Combat && plan.harass_idle_only && !(a.idle() && tactics_.surplus > 0)) continue;       // a worker joins only when the economy has nothing for it
            cands.push_back(&a);
        }
        std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) { return (x->type == sim::AntType::Combat) > (y->type == sim::AntType::Combat); });
        for (const AntView* a : cands) {
            if (squad_.size() >= plan.harassers) break;
            if (!c.ledger.take(a->id, id())) continue;
            squad_[a->id] = Member{};
        }
    }
    if (squad_.empty()) return;
    // the carriers that can be hunted: on their way home near an enemy hill, attackable, not under the eyes of an enemy Combat Ant
    std::vector<const AntView*> carriers;
    for (const AntView& e : v.others()) {
        if (!e.holding || !attackable(v, e)) continue;
        bool near_hill = false;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            const HillInfo& hill = c.map.hill(t);
            if (t == c.seat || !hill.present || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || e.team != t) continue;
            near_hill = near_hill || e.tile.chebyshev_dist(hill.queue) <= plan.harass_radius;
        }
        if (near_hill) carriers.push_back(&e);
    }
    std::map<uint32_t, std::vector<uint32_t>> groups;                    // target -> attackers
    std::vector<uint32_t> waiting;
    for (auto& s : squad_) {
        const AntView* a = find_ant(v.mine(), s.first);
        if (a == nullptr) continue;
        Member& mem = s.second;
        bool need = false;
        if (!mem.ordered) need = a->takes_orders();
        else if (mem.sent != kPending) need = a->idle() && now >= mem.sent + 6u && !v.has_pending_path(a->id);
        else need = now > mem.decided + 200u;
        if (!need) continue;
        const AntView* best = nullptr;
        int32_t best_d = 0;
        for (const AntView* e : carriers) {
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
    // no carrier in sight: wait three tiles in front of the nearest enemy hill, on the side the carriers come from (towards the middle of the map)
    if (!waiting.empty()) {
        for (const uint32_t id_ : waiting) {
            const AntView* a = find_ant(v.mine(), id_);
            if (a == nullptr) continue;
            const HillInfo* best = nullptr;
            int32_t best_d = 0;
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
                const HillInfo& hill = c.map.hill(t);
                if (t == c.seat || !hill.present || !v.rows()[t].present || v.rows()[t].dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally())) continue;
                const int32_t d = a->tile.chebyshev_dist(hill.queue);
                if (best == nullptr || d < best_d) {
                    best = &hill;
                    best_d = d;
                }
            }
            if (best == nullptr) continue;
            const int32_t sx = best->queue.x * 2 < static_cast<int32_t>(grid.width()) ? 1 : -1;
            const int32_t sy = best->queue.y * 2 < static_cast<int32_t>(grid.height()) ? 1 : -1;
            sim::TileCoord station{best->queue.x + sx * 3, best->queue.y + sy * 3};
            for (int32_t k = 0; k < 6 && (!MapInfo::walkable(grid, c.seat, station, v.walk_context()) || v.powerup_at(station) != nullptr); ++k) station = sim::TileCoord{station.x + sx, station.y + sy};
            if (a->tile.chebyshev_dist(station) <= 2) continue;
            c.orders.move({id_}, station, Priority::Normal);
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
