#include "ants_ai/standard_tasks.hpp"

#include <algorithm>

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
    if (plan.intercepts && hill.present) {
        for (const ThiefSighting& t : tactics_.memory.thieves()) {
            if (t.carrying || t.tile.chebyshev_dist(hill.raid) > kThiefRadius) continue;
            const AntView* e = find_ant(v.others(), t.ant);
            if (e == nullptr || !attackable(v, *e) || shunned(e->id, now)) continue;
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

}  // namespace ants::ai
