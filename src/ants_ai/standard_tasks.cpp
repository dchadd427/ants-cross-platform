#include "ants_ai/standard_tasks.hpp"
#include "standard_tasks_shared.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::kMinFightHp;

namespace {

constexpr uint8_t kKeepFightHp = 3;           // ... and one that has fewer than this leaves a fight it is in
constexpr int32_t kThiefRadius = 16;          // an enemy Thief this close to the own raid tile is on its way (or standing in front of the hole)
constexpr uint64_t kShunTicks = 900;          // an enemy ant that no order reached is left alone this long (45 s)
constexpr uint64_t kBlowTicks = 6;            // an attack order is one blow: an ant that is idle this long after the order LEFT has done it (or cannot)
constexpr uint64_t kCantGoWindow = 24;        // an attack that shows "can't go" within this many ticks of leaving was refused by the path finder

}  // namespace

// ---- FightTask ------------------------------------------------------------------------------------------------------------------------------------------

namespace {

// How many tiles of the ring round the own gate burn (SabotageTask::ring_of: eight tiles; the queue row inside is shut off when they all do)
size_t ring_walls_lit(const BotView& v, const HillInfo& hill) {
    size_t n = 0;
    for (const sim::TileCoord& t : SabotageTask::ring_of(hill)) n += v.grid().in_bounds(t) && v.grid().has_fire_at(t) ? 1u : 0u;
    return n;
}

// The ring is lit so far that the carriers cannot bank (or the ants inside cannot leave): an ant that stands still holding food earns nothing while it waits, so it may strike (it keeps its food)
constexpr size_t kShutRingWalls = 5;

// A fire hunt recruits the ants that stand within this many tiles of its target
constexpr int32_t kFireHuntReach = 20;

// An ant of the stand batch's hunt of a Fire Ant that fires the own gate in (plan.fire_draft): any type that takes orders and is healthy; with food only a carrier that stands still while the ring is
// shut (it cannot bank anyway). `shut` is that state
bool draft_ant(const BotView& v, const AntView& a, bool shut) {
    if (!a.takes_orders() || a.state == sim::UnitState::Stunned || a.hp < kMinFightHp || v.powerup_at(a.tile) != nullptr) return false;
    if (a.holding || a.carried_points > 0) return shut && a.idle();
    return true;
}

// The Fire Ant that the duel leaves alone: the one that keeps the walls of the thief hole while a thief threatens (0: none)
uint32_t duel_spare_of(const Tactics& t) noexcept {
    return t.plan.fire_duel_walls || !t.wall_demand ? 0u : t.wall_keeper;
}

}  // namespace

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
    if (v.mine().size() <= tactics_.reserve_of(v)) return;                 // the last ants stay out of every fight (a wipe-out forces a hatch of 200 points, or ends the team)
    // a blow: the attacker is the enemy ant that can be attacked and stands nearest to the place of the blow (within two tiles)
    for (const Hit& h : tactics_.memory.hits()) {
        const AntView* best = nullptr;
        int32_t best_d = 0;
        for (const AntView& e : v.others()) {
            const int32_t d = e.tile.chebyshev_dist(h.tile);
            if (d > 2 || !attackable(v, e) || shunned(e.id, now)) continue;
            // the nearest; with the hit points on the view the one that dies soonest (the fewest blows), the nearest of those
            const int32_t key = plan.health_aware ? static_cast<int32_t>(blows_to_kill(e.hp, 1)) * 4 + d : d;
            if (best == nullptr || key < best_d) {                               // (v.others() is by id, so the lower id wins a tie)
                best = &e;
                best_d = key;
            }
        }
        if (best == nullptr) continue;
        const auto it = fights_.find(best->id);
        if (it != fights_.end()) {
            it->second.last_alarm = now;
            if (plan.victim_answers && !it->second.offence && !it->second.fire && !it->second.thief) it->second.victims.push_back(h.ant);   // (only a fight that answers a blow takes defenders)
            continue;
        }
        Fight f;
        f.target = best->id;
        f.anchor = h.tile;
        f.started = now;
        f.last_alarm = now;
        if (plan.victim_answers) f.victims.push_back(h.ant);
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

// The defence against being fired in (plan.fire_defence, after a report that the other team just extinguishes all of the fire and does not try to kill the fire ant that is firing them in): while
// fire walls stand on the ring round the own gate the enemy Fire Ant that lights them, an enemy Fire Ant within plan.fire_defence_radius tiles of the own hill, is hunted by the fighters (the
// level's defenders and plan.fire_defence_extra more, a Combat Ant first), a wounded one first; the walls are put out afterwards (WallTask)
void FightTask::start_fire_defence(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    if ((!plan.fire_defence && !plan.raider_hunt) || !v.has_grid()) return;
    const HillInfo& hill = c.map.hill(c.seat);
    if (!hill.present) return;
    if (v.mine().size() <= tactics_.reserve_of(v)) return;
    const uint32_t duel_spare = duel_spare_of(tactics_);
    size_t walls = 0;
    for (const sim::TileCoord& t : SabotageTask::ring_of(hill)) walls += v.grid().in_bounds(t) && v.grid().has_fire_at(t) ? 1u : 0u;
    const bool ring = plan.fire_defence && walls != 0;                                              // walls stand on the ring: the Fire Ant that lights them is the target
    // (plan.fire_duel, the stand batch) an own Fire Ant that can strike: the enemy Fire Ant stands on a fire wall, where no other ant's order reaches it, and the Fire Ant's blow throws it off the wall
    std::vector<sim::TileCoord> strikers;                                                            // (where they stand: the hunt recruits within kFireHuntReach tiles of its target only)
    if (plan.fire_duel && ring) {
        for (const AntView& a : v.mine()) {
            if (a.type != sim::AntType::Fire || a.id == duel_spare || !draft_ant(v, a, plan.fire_draft && walls >= kShutRingWalls)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner == kNoTask || owner == id() || c.ledger.rank(owner) < c.ledger.rank(id())) strikers.push_back(a.tile);
        }
    }
    const auto striker_near = [&](const AntView& e) {
        for (const sim::TileCoord& t : strikers) {
            if (t.chebyshev_dist(e.tile) <= kFireHuntReach) return true;
        }
        return false;
    };
    if (!ring && !plan.raider_hunt) return;
    size_t active_fire = 0;
    bool fire_ant_hunted = false;
    for (const auto& f : fights_) {
        if (!f.second.fire) continue;
        ++active_fire;
        const AntView* t = find_ant(v.others(), f.first);
        fire_ant_hunted = fire_ant_hunted || !f.second.raider || (t != nullptr && t->type == sim::AntType::Fire);
    }
    const bool spare_for_two = v.mine().size() >= plan.fight_reserve + plan.defenders + plan.fire_defence_extra + 2u;
    if (active_fire >= (plan.smart_counters && ring && walls == 1 && !fire_ant_hunted && spare_for_two ? 2u : 1u)) return;
    // (plan.raider_hunt, the war batch; requested: try to kill their fire ant if they try to come for them) an enemy Fire or Bomber Ant near the own hill, or at a pile the own ants work, whether
    // or not walls stand: the blows stop what it is doing (a hit cancels an ability) and send it home when it is down to one hit point
    const auto near_work = [&](const AntView& e) {
        if (e.tile.chebyshev_dist(hill.origin) <= plan.raider_radius) return true;
        if (!plan.raider_piles) return false;
        for (const PileView& p : v.piles()) {
            if (p.lunchbox || p.anchor.chebyshev_dist(e.tile) > 4) continue;
            for (const AntView& a : v.mine()) {
                if (a.tile.chebyshev_dist(p.anchor) <= 8) return true;
            }
        }
        return false;
    };
    const AntView* best = nullptr;
    int32_t best_key = 0;
    bool best_ring = false;
    for (const AntView& e : v.others()) {
        const bool fire_ant = e.type == sim::AntType::Fire;
        if ((!fire_ant && !(plan.raider_hunt && e.type == sim::AntType::Bomber)) || fights_.count(e.id) != 0 || !attackable(v, e, fire_ant && striker_near(e)) || shunned(e.id, now)) continue;
        if (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) continue;
        const int32_t d = e.tile.chebyshev_dist(hill.origin);
        const bool ring_target = ring && fire_ant && d <= plan.fire_defence_radius;
        if ((active_fire >= 1 && !ring_target) || (!ring_target && !(plan.raider_hunt && near_work(e)))) continue;
        const int32_t key = plan.health_aware ? static_cast<int32_t>(blows_to_kill(e.hp, 1)) * 4 + d : d;
        if (best == nullptr || key < best_key) {
            best = &e;
            best_key = key;
            best_ring = ring_target;
        }
    }
    if (best == nullptr) return;
    Fight f;
    f.target = best->id;
    f.anchor = best_ring ? hill.origin : best->tile;
    f.started = now;
    f.last_alarm = now;
    f.fire = true;
    f.raider = !best_ring;
    fights_.emplace(best->id, std::move(f));
    ++fights_started_;
    if (best_ring) ++fire_hunts_;
    else ++raider_hunts_;
}

namespace {

// An ant for the assault: one that stands idle with empty hands (nothing to harvest or no way to it; a worker of that kind too, at every tier, and from the pull tier without the surplus that the other
// tiers ask for), and, when the bot is far enough behind (pull), a Combat Ant wherever it is (it is a worker that fights: it harvests between the blows). A worker on its way to a pile or carrying food
// is never taken: it kills nothing above two hit points and the food is what the assault is not for
bool assault_ant(const AntView& a, bool pull) noexcept {
    return (a.idle() && !a.holding && a.carried_points == 0) || (pull && a.type == sim::AntType::Combat);
}

bool flanked_by(sim::TileCoord target, const std::vector<const AntView*>& near) noexcept {
    int32_t min_dx = 0, max_dx = 0, min_dy = 0, max_dy = 0;
    for (const AntView* a : near) {
        if (a == nullptr || a->tile.chebyshev_dist(target) > 2) return false;
        min_dx = std::min(min_dx, a->tile.x - target.x);
        max_dx = std::max(max_dx, a->tile.x - target.x);
        min_dy = std::min(min_dy, a->tile.y - target.y);
        max_dy = std::max(max_dy, a->tile.y - target.y);
    }
    return (min_dx < 0 && max_dx > 0) || (min_dy < 0 && max_dy > 0);
}

}  // namespace

// The bot's own fights, begun without a blow on an ant of its own (decisions 2 and 14 of the contest plan). The free ants (can_fight: they carry nothing, are healthy, take orders; the ants of a
// pick-up, a raid, a wall or a bomb are never taken, nor the last ants) attack an enemy ant when their force is clearly stronger than what answers near it:
//   hunt      (plan.hunt, every level) a KILL that is available of a WOUNDED ant: by kill_plan (the real blows: one lands per hit clip, only a Combat Ant's punch finishes an ant, so the plan is built on
//             Combat Ants and an opener for an odd number of hit points) within plan.hunt_blows blows, and the force has plan.hunt_odds_percent of the strength near the target. The hit points of
//             every ant are on the view. The scope is Easy: what stands within plan.hunt_next tiles of a free ant; Medium and Hard also what is within the leash of the own hill and at a pile that the
//             own ants work, and Hard the carriers of the leading team wherever they are. The hunt is kept until the target is dead or out of reach, nobody is left to hunt it, or its clock runs out
//             (the odds that it began with are not asked again: a wounded target is finished; the fight is bound to its target, a fresh enemy does not take the ants away), and the ants go back
//             to the economy afterwards
//   skirmish  (plan.skirmish, Hard) pressure: a carrier on its way (a blow clears its walk and it stands with its food until its owner sends it on) or a worker at a pile, with plan.skirmish_odds_percent
// Strength is the square law's number: the sum of the hit points times the sum of the damage of a round. What answers near the target are the plan.*_enemy_cap strongest enemy ants within
// plan.*_near tiles that carry nothing (the standard bot of Hard sends three).
void FightTask::start_offence(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const Standing& st = tactics_.standing;
    const bool pressure_fights = plan.catchup && st.tier >= plan.catchup_workers_tier;               // behind the leader enough: the skirmish opens for every level, and even trades are taken
    // (the war batch, plan.assault) free ants - nothing to harvest, the economy has nowhere to send them - go for the enemy ants: with the food gone the score is fixed and an ant at home earns nothing
    // (plan.behind_war) a bot that is behind does not wait for the food to be gone: it begins sooner, asks for less of the odds, sends one more ant when it is far behind, takes ants that have work from the
    // war tier plan.behind_free_tier on, and goes for the ants of the leader first
    const uint8_t war = plan.behind_war ? st.war : 0;
    const bool pull = war >= 1 && war >= plan.behind_free_tier;                                      // (from the war tier plan.behind_free_tier on the Combat Ants are taken off the piles for the assault, and the surplus of free ants is no longer asked for)
    const uint64_t assault_from = war >= 1 ? std::min<uint64_t>(plan.assault_after, plan.behind_assault_after) : plan.assault_after;
    const bool assault_on = plan.assault && now >= assault_from && (tactics_.surplus >= plan.assault_min || pull) && v.ticks_left() > 200;
    const uint32_t assault_odds = plan.assault_odds_percent * (100u - std::min<uint32_t>(80u, plan.behind_odds_ease * war)) / 100u;
    const size_t assault_force = plan.assault_force + (war >= 2 ? 1u : 0u);
    if (!(plan.skirmish || plan.hunt || pressure_fights || assault_on) || !v.has_grid() || now < offence_pause_until_ || tactics_.guard_stance) return;     // (the leader of the last minute guards: no offence)
    const uint32_t odds_scale = plan.catchup && st.tier >= 3 ? 50u : pressure_fights ? 70u : 100u;
    if (v.mine().size() <= tactics_.reserve_of(v)) return;      // the last ants stay out of every fight (idle_hunt: but with nothing to bank and nothing to lose only the last one)
    std::set<uint32_t> busy;                                                                         // the ants of the fights that run
    for (const auto& f : fights_) {
        for (const auto& d : f.second.defenders) busy.insert(d.first);
    }
    std::vector<const AntView*> pool;
    for (const AntView& a : v.mine()) {
        if (!can_fight(v, a) || busy.count(a.id) != 0) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= 3) continue;               // the ants of a pick-up, a raid, a wall or a bomb are not taken
        pool.push_back(&a);
    }
    if (pool.empty()) return;
    const HillInfo& hill = c.map.hill(c.seat);
    const int leader = tactics_.standing.leader;
    // the scope of a hunt
    const auto in_scope = [&](const AntView& e, uint32_t w_blows) {
        for (const AntView* a : pool) {
            if (a->tile.chebyshev_dist(e.tile) <= plan.hunt_next) return true;                       // next to the own ants: Easy's whole scope
        }
        if (plan.water_hunt && w_blows == 1 && e.state != sim::UnitState::Walking) {
            for (const AntView* a : pool) {
                if (a->tile.chebyshev_dist(e.tile) <= plan.hunt_reach) return true;
            }
        }
        if (!plan.hunt_wide) return false;
        if (hill.present && e.tile.chebyshev_dist(hill.origin) <= plan.leash_tiles) return true;       // within the leash of the own hill
        for (const PileView& p : v.piles()) {                                                        // at a pile that the own ants work
            if (p.anchor.chebyshev_dist(e.tile) > 4) continue;
            for (const AntView& a : v.mine()) {
                if (a.tile.chebyshev_dist(p.anchor) <= 8) return true;
            }
        }
        return plan.hunt_leader_carriers && e.holding && leader >= 0 && e.team == leader;
    };
    // the force that can be called to a target: the pool within `reach`, a Combat Ant first, then the nearest, at most `force`
    const auto form = [&](const AntView& e, int32_t reach, size_t force, bool assault = false) {
        std::vector<const AntView*> near;
        for (const AntView* a : pool) {
            if (a->tile.chebyshev_dist(e.tile) > reach || (assault && !assault_ant(*a, pull)) || (plan.reach_fights && !walk_reaches(c.map, c.seat, a->tile, e.tile))) continue;       // (reach_fights: no walk, no blow)
            near.push_back(a);
        }
        std::stable_sort(near.begin(), near.end(), [&](const AntView* x, const AntView* y) {
            const bool cx = x->type == sim::AntType::Combat;
            const bool cy = y->type == sim::AntType::Combat;
            if (cx != cy) return cx;
            return x->tile.chebyshev_dist(e.tile) < y->tile.chebyshev_dist(e.tile);
        });
        if (near.size() > force) near.resize(force);
        return near;
    };
    const bool running = offence_running() >= 1;                                                     // one at a time (the next target is chosen when it is over)
    const AntView* best = nullptr;
    int32_t best_score = 0;
    bool best_hunt = false;
    bool best_vicinity = false;
    bool best_assault = false;
    for (const AntView& e : v.others()) {
        if (fights_.count(e.id) != 0 || !attackable(v, e) || shunned(e.id, now)) continue;
        // a kill that is available: of a WOUNDED ant (or a non-Swimmer near unbridged water), by the plan of the real blows (kill_plan or water_push_blows); a survivor with one
        // hit point that already walks home is out of reach unless it is on the water's edge
        const std::vector<const AntView*> near_hunt = plan.hunt ? form(e, plan.hunt_reach, plan.hunt_force) : std::vector<const AntView*>{};
        const uint32_t raw_w_blows = (plan.hunt && plan.water_hunt) ? water_push_blows(v, e, near_hunt) : 0u;
        const bool water_ready = raw_w_blows == 1
            ? (e.state != sim::UnitState::Walking || e.hp < sim::AntUnit::MAX_HP || (!near_hunt.empty() && near_hunt.front()->idle() && near_hunt.front()->tile.chebyshev_dist(e.tile) <= 2))
            : (raw_w_blows > 1 && e.hp < sim::AntUnit::MAX_HP);
        const uint32_t w_blows = water_ready ? raw_w_blows : 0u;
        if (plan.hunt && e.hp >= 1 && (e.hp < sim::AntUnit::MAX_HP || w_blows > 0) && !(e.hp == 1 && e.state == sim::UnitState::Walking && w_blows == 0) && in_scope(e, w_blows)) {
            Strength own;
            size_t combats = 0;
            for (const AntView* a : near_hunt) {
                own.add(a->hp, blow_damage(a->type));
                combats += a->type == sim::AntType::Combat ? 1u : 0u;
            }
            const size_t others = near_hunt.size() - combats;
            KillPlan kill = kill_plan(e.hp, combats, others);
            const bool v_hunt = !kill.possible && plan.vicinity_hunt && e.hp == 3 && combats == 0 && others >= 3 && e.state != sim::UnitState::Walking && flanked_by(e.tile, near_hunt);
            if (v_hunt) {
                kill.possible = true;
                kill.blows = 3;
            }
            if (w_blows > 0 && (!kill.possible || w_blows < kill.blows)) {
                kill.possible = true;
                kill.blows = w_blows;
            }
            const uint32_t max_blows = w_blows > 0 ? std::max<uint32_t>(plan.hunt_blows, 4u) : (v_hunt ? 3u : plan.hunt_blows);
            if (kill.possible && kill.blows <= max_blows && !near_hunt.empty()) {
                const Strength theirs = enemy_strength(v, e.tile, plan.skirmish_near, e.id, plan.skirmish_enemy_cap);
                const uint32_t req_odds = w_blows == 1 ? 50u : plan.hunt_odds_percent;
                if (own.value() * 100u >= theirs.value() * req_odds * odds_scale / 100u) {
                    available_.insert(e.id);
                    const int32_t water_bonus = w_blows == 1 ? 120 : (w_blows > 0 ? 60 : 0);
                    const int32_t score = 300 + water_bonus - 12 * static_cast<int32_t>(kill.blows) - 4 * near_hunt.front()->tile.chebyshev_dist(e.tile) + (10 - static_cast<int32_t>(std::min<uint32_t>(e.hp, 10u))) * 6;
                    if (!running && (best == nullptr || score > best_score)) {
                        best = &e;
                        best_score = score;
                        best_hunt = true;
                        best_vicinity = v_hunt && w_blows == 0;
                        best_assault = false;
                    }
                    continue;
                }
            }
        }
        // the assault: the nearest enemy ant that the force of free ants outweighs (a raider, a wounded ant and a carrier first)
        if (assault_on && !running) {
            const std::vector<const AntView*> near = form(e, plan.assault_reach, assault_force, true);
            if (!near.empty() && near.size() >= plan.assault_min) {
                Strength own;
                for (const AntView* a : near) own.add(a->hp, blow_damage(a->type));
                const Strength theirs = enemy_strength(v, e.tile, plan.skirmish_near, e.id, plan.skirmish_enemy_cap);
                if (own.value() * 100u >= theirs.value() * assault_odds) {
                    int32_t score = 100 - 3 * near.front()->tile.chebyshev_dist(e.tile) + (10 - static_cast<int32_t>(std::min<uint32_t>(e.hp, 10u))) * 8;
                    if (e.type == sim::AntType::Fire || e.type == sim::AntType::Bomber) score += 60;
                    if (e.holding) score += 40;
                    if (war >= 1 && leader >= 0 && e.team == leader) score += 50;                     // the leader's ants: what he has not banked yet is what the bot can still take from him
                    if (best == nullptr || (!best_hunt && score > best_score)) {
                        best = &e;
                        best_score = score;
                        best_hunt = false;
                        best_vicinity = false;
                        best_assault = true;
                    }
                }
            }
        }
        // pressure: a carrier on its way, a worker at a pile
        if (!(plan.skirmish || pressure_fights) || running || e.type == sim::AntType::Thief) continue;
        int32_t value = 0;
        if (e.holding) {
            value = 100;                                                                             // a carrier: a blow costs its owner the walk and the time to send it on
        } else {
            for (const PileView& p : v.piles()) {
                if (p.anchor.chebyshev_dist(e.tile) <= 3) value = 50;                                // a worker at a pile
            }
        }
        if (value == 0) continue;
        const std::vector<const AntView*> near = form(e, plan.skirmish_reach, plan.skirmish_force);
        if (near.size() < plan.skirmish_min_force) continue;
        Strength own;
        for (const AntView* a : near) own.add(a->hp, blow_damage(a->type));
        const Strength theirs = enemy_strength(v, e.tile, plan.skirmish_near, e.id, plan.skirmish_enemy_cap);
        if (own.value() * 100u < theirs.value() * plan.skirmish_odds_percent * odds_scale / 100u) continue;      // not the stronger force
        int32_t score = value - 4 * near.front()->tile.chebyshev_dist(e.tile);
        if (plan.health_aware) {
            score += static_cast<int32_t>(10u - std::min<uint32_t>(e.hp, 10u)) * 12;                 // a wounded ant first
            if (blows_to_kill(e.hp, own.dmg) <= 1) score += 80;                                      // one round of blows kills it
        }
        if (best == nullptr || (!best_hunt && score > best_score)) {                                 // (a kill that is available comes before a pressure target)
            best = &e;
            best_score = score;
            best_hunt = false;
            best_vicinity = false;
            best_assault = false;
        }
    }
    if (best == nullptr) return;
    Fight f;
    f.target = best->id;
    f.anchor = best->tile;
    f.started = now;
    f.last_alarm = now;
    f.offence = true;
    f.hunt = best_hunt;
    f.vicinity = best_vicinity;
    f.assault = best_assault;
    f.force = static_cast<uint32_t>(assault_force);
    f.pull = pull;
    f.abort_percent = std::min<uint32_t>(plan.skirmish_abort_percent, assault_odds * 8u / 10u);
    fights_.emplace(best->id, std::move(f));
    ++fights_started_;
    ++offence_started_;
    if (best_hunt) ++hunts_started_;
    if (best_assault) ++assaults_started_;
}

void FightTask::run_fight(TaskContext& c, Fight& f, bool& end, std::vector<std::pair<sim::TileCoord, std::vector<uint32_t>>>& attacks) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const uint32_t duel_spare = duel_spare_of(tactics_);
    const AntView* target = find_ant(v.others(), f.target);
    if (v.mine().size() <= tactics_.reserve_of(v)) {
        end = true;                                                              // down to the last ants: they leave the fight
        return;
    }
    // (the defence batch: defence.hpp) a fight that answers a blow is sized by the enemy ants near its target and kept up while the force is at least plan.defend_keep_percent of theirs (plan.defend_by_strength)
    const bool defence = !f.offence && !f.fire && !f.thief;
    const bool by_strength = defence && plan.defend_by_strength && target != nullptr;
    const DefenceFrame frame = by_strength ? defence_frame(v, plan, target->tile, tactics_.guard_stance) : DefenceFrame{};
    if (target != nullptr) {
        if (target->hp < f.seen_hp) f.last_hurt = now;                           // (a blow of the own defenders, or of anything else, landed)
        f.seen_hp = target->hp;
    }
    const auto own_strength = [&]() {
        Strength s;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            if (a != nullptr) s.add(a->hp, blow_damage(a->type));
        }
        return s;
    };
    // (the stand batch) the hunt of the Fire Ant that fires the gate in: while it stands on a fire wall only an own Fire Ant's blow reaches it (plan.fire_duel); the others wait until it is thrown off
    const bool reach_fire = f.fire && plan.fire_duel;
    const bool on_fire = reach_fire && target != nullptr && v.has_grid() && v.grid().has_fire_at(target->tile);
    const bool draft = f.fire && plan.fire_draft;
    size_t ring_lit = 0;
    if (f.fire && v.has_grid()) {
        const HillInfo& own = c.map.hill(c.seat);
        if (own.present) ring_lit = ring_walls_lit(v, own);
    }
    const bool shut = draft && ring_lit >= kShutRingWalls;
    // Is a kill in reach? (health-aware fights.) Only then the fight is kept up after a blow: the engine lets one blow land per hit clip and only a punch finishes an ant (kill_plan), so a duel
    // of workers that cannot end in a kill ends with the first blow, as it did (and costs the economy no more). The hunts are built on a kill plan; the skirmishes, the fire hunts and the thief
    // hunts have their own ends and wait out the clips; a fight that has just begun takes its defenders below
    std::vector<const AntView*> def_ptrs;
    for (const auto& d : f.defenders) {
        const AntView* a = find_ant(v.mine(), d.first);
        if (a != nullptr) def_ptrs.push_back(a);
    }
    const uint32_t w_blows = (plan.water_hunt && target != nullptr) ? water_push_blows(v, *target, def_ptrs) : 0u;
    const auto kill_now = [&]() {
        if (f.hunt || w_blows > 0) return true;
        size_t combats = 0;
        size_t others = 0;
        for (const AntView* a : def_ptrs) {
            if (a->type == sim::AntType::Combat) ++combats;
            else ++others;
        }
        const KillPlan kill = kill_plan(target->hp, combats, others);
        return target->hp < sim::AntUnit::MAX_HP && kill.possible && kill.blows <= plan.hunt_blows;
    };
    const bool waits_for_clip = plan.health_aware && target != nullptr && (f.offence || f.fire || f.thief || f.defenders.empty() || kill_now() ||
                                                                           (by_strength && keep_defence(plan, own_strength(), frame.theirs, target->tile.chebyshev_dist(f.anchor), now - std::max(f.last_alarm, f.last_hurt))));
    // An enemy that was struck plays the hit clip for a second or so and cannot be attacked meanwhile: the fight waits for it while a kill is in reach (with the hit points on the view a wounded target
    // is finished, not given up at the first blow); every other reason that attackable() gives ends the fight
    bool in_hit_clip = false;
    if (target != nullptr && waits_for_clip && (target->state == sim::UnitState::Flinch || target->state == sim::UnitState::Knockback)) {
        AntView probe = *target;
        probe.state = sim::UnitState::Idle;
        in_hit_clip = attackable(v, probe, reach_fire);
        if (in_hit_clip) ++clip_waits_;
    }
    if (target == nullptr || (!in_hit_clip && !attackable(v, *target, reach_fire)) || shunned(f.target, now)) {
        if (target == nullptr && f.hunt) ++hunts_killed_;                        // (an ant that is not on the view any more is dead or drowned)
        end = true;                                                              // it died, left sight, stands on a power-up or went into a hill
        return;
    }
    // A survivor with one hit point walks home (the engine sends it) at the speed of its hunters: with a lead of two tiles it is not caught any more (measured: docs/BOTS.md, "Hunting the kill"), so the
    // fight is over and the ant is left alone for a while
    if (plan.health_aware && !f.thief && !f.defenders.empty() && target->hp == 1 && target->state == sim::UnitState::Walking && w_blows == 0) {
        int32_t nearest = 99;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            if (a != nullptr) nearest = std::min(nearest, a->tile.chebyshev_dist(target->tile));
        }
        if (nearest >= 3) {
            shunned_[f.target] = now + 300u;
            ++escapes_;
            end = true;
            return;
        }
    }
    // a wounded target is finished: the fight lingers on and the leash is longer (requested: if they are attacking an ant and it is low on health, they continue and try to kill it)
    const bool wounded = waits_for_clip && ((target->hp > 0 && target->hp <= 4) || w_blows > 0);
    if (f.thief) {
        const HillInfo& hill = c.map.hill(c.seat);
        if (target->holding || !hill.present || target->tile.chebyshev_dist(hill.raid) > kThiefRadius + 4 || now > f.last_alarm + plan.fight_linger_ticks) {
            end = true;                                                          // it has raided (it holds loot now), or it turned away
            return;
        }
    } else if (f.offence) {
        if (tactics_.guard_stance) {                                             // the last minute of a leader: no offence, the ants are free to guard and to bank
            end = true;
            return;
        }
        // a skirmish of the bot's own ends by its clock, by how far the target was followed and by the strength of the force: the odds that it was started with are not kept (a force that has
        // lost a third of its strength still fights) but one that has fallen below the enemy's is called off
        const Strength own = own_strength();
        const Strength theirs = enemy_strength(v, target->tile, plan.skirmish_near, target->id, plan.skirmish_enemy_cap);
        const uint32_t abort_percent = f.assault ? f.abort_percent : plan.skirmish_abort_percent;
        const bool weak = own.n > 0 && own.value() * 100u < theirs.value() * abort_percent;
        if (now > f.started + (f.hunt ? plan.hunt_ticks : f.assault ? plan.assault_ticks : plan.skirmish_ticks) || target->tile.chebyshev_dist(f.anchor) > (f.assault ? plan.assault_chase : plan.skirmish_chase) + (wounded ? 4 : 0) || (weak && !wounded)) {
            if (weak) {
                ++offence_aborted_;
                offence_pause_until_ = now + plan.skirmish_pause_ticks;
            }
            end = true;
            return;
        }
        f.last_alarm = now;
    } else if (f.fire) {
        // the hunt of the Fire Ant that fires the own gate in: it ends when the ant is far from the hill, after 1,200 ticks (a bound), or when it is not a Fire Ant any more; while it lasts there is no linger
        const bool kind_ok = target->type == sim::AntType::Fire || (f.raider && target->type == sim::AntType::Bomber);
        if (!kind_ok || target->tile.chebyshev_dist(f.anchor) > (f.raider ? plan.raider_radius : plan.fire_defence_radius) + 4 || now > f.started + 1200u) {
            end = true;
            return;
        }
        f.last_alarm = now;
    } else if (target->tile.chebyshev_dist(f.anchor) > plan.leash_tiles + (wounded ? 4 : 0) || (!by_strength && now > f.last_alarm + plan.fight_linger_ticks * (wounded ? 3u : 1u))) {
        end = true;                                                              // it left the leash, or the fight is over (by_strength: keep_defence says when)
        return;
    }
    // the defenders that are still ours and fit
    // (with the hit points on the view an ant leaves when it would die before its target: one blow from the enemy's death it stays, against a Combat Ant it leaves sooner; another ant takes
    // over below)
    bool combat_near = false;
    for (const AntView& e : v.others()) {
        if (e.type == sim::AntType::Combat && e.tile.chebyshev_dist(target->tile) <= 3 && !(v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) combat_near = true;
    }
    for (auto it = f.defenders.begin(); it != f.defenders.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        uint8_t keep = kKeepFightHp;
        if (plan.health_aware && a != nullptr) {
            if ((target->hp > 0 && target->hp <= blow_damage(a->type)) || w_blows == 1) keep = 1;       // one blow from death or water: finish it
            else if (combat_near) keep = 4;                                                             // a punch takes 2: an ant with 3 would be dead in two
        }
        const bool carrying = a != nullptr && (a->holding || a->carried_points > 0) && !(shut && f.fire);              // (a carrier that cannot bank keeps striking)
        if (a == nullptr || c.ledger.owner(it->first) != id() || a->hp < keep || carrying) {
            if (a != nullptr) c.ledger.release(it->first, id());
            it = f.defenders.erase(it);
        } else {
            ++it;
        }
    }
    // an acknowledged attack that shows "can't go" soon after it left: no order reaches the target (it stands on something): leave it alone
    for (auto& d : f.defenders) {
        if (d.second.sent == kPending || now < d.second.sent + 2 || now > d.second.sent + kCantGoWindow) continue;
        const AntView* a = find_ant(v.mine(), d.first);
        if (a != nullptr && a->state == sim::UnitState::CantGo) {
            if (f.hunt && (w_blows > 0 || f.vicinity) && target->hp < d.second.target_hp) {
                d.second.ordered = false;
                d.second.sent = kPending;
                continue;
            }
            shunned_[f.target] = now + kShunTicks;
            ++shunned_count_;
            end = true;
            return;
        }
    }
    // more defenders: Combat Ants first, then the nearest
    const size_t wanted = f.hunt ? plan.hunt_force : f.assault ? f.force : f.offence ? plan.skirmish_force : f.fire ? plan.defenders + (f.raider ? plan.raider_extra : plan.fire_defence_extra) : by_strength ? frame.wanted : plan.defenders + (tactics_.guard_stance ? 1u : 0u);
    if (defence && (plan.defend_all_types || plan.victim_answers || plan.defend_by_strength)) {
        if (!take_defenders(c, f, *target, wanted, frame.theirs)) {              // (defence.cpp: the struck ants first, then the ants near the target; false: the force is weaker than the enemy's)
            end = true;
            return;
        }
    } else if (f.defenders.size() < wanted) {
        std::vector<const AntView*> cands;
        for (const AntView& a : v.mine()) {
            // (the stand batch) a Fire Ant of the own side strikes the Fire Ant that fires the gate in; with the draft every ant that can strike does
            const bool fire_striker = f.fire && plan.fire_duel && a.type == sim::AntType::Fire && a.id != duel_spare && draft_ant(v, a, shut);
            if (f.defenders.count(a.id) != 0 || !(can_fight(v, a) || fire_striker || (draft && draft_ant(v, a, shut))) || (plan.reach_fights && !fire_striker && !walk_reaches(c.map, c.seat, a.tile, target->tile))) continue;
            if (f.ally && a.tile.chebyshev_dist(target->tile) > plan.ally_help_radius) continue;
            if (f.offence && a.tile.chebyshev_dist(target->tile) > (f.hunt ? plan.hunt_reach : f.assault ? plan.assault_reach : plan.skirmish_reach)) continue;
            if (f.fire && a.tile.chebyshev_dist(target->tile) > (draft && a.type == sim::AntType::Combat ? plan.fire_draft_far : kFireHuntReach)) continue;
            if (f.assault && !assault_ant(a, f.pull)) continue;                  // (an assault is made of the free ants, and of the Combat Ants of a bot that is behind)
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= 3 && !(fire_striker && c.ledger.rank(owner) < c.ledger.rank(id()))) continue;         // the ants of a pick-up, a raid or a fight are not taken (the Fire Ant of the walls is, for the duel)
            cands.push_back(&a);
        }
        std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) {
            const bool fx = on_fire && x->type == sim::AntType::Fire;             // (a target on a wall: only the Fire Ant's blow reaches it)
            const bool fy = on_fire && y->type == sim::AntType::Fire;
            if (fx != fy) return fx;
            const bool cx = x->type == sim::AntType::Combat;
            const bool cy = y->type == sim::AntType::Combat;
            if (cx != cy) return cx;
            return x->tile.chebyshev_dist(target->tile) < y->tile.chebyshev_dist(target->tile);
        });
        for (const AntView* a : cands) {
            if (f.defenders.size() >= wanted) break;
            if (!c.ledger.take(a->id, id())) continue;
            f.defenders[a->id] = Defender{};
        }
    }
    if (on_fire) {                                                               // (a target on a wall is struck by a Fire Ant alone: with none left in the hunt it is out of reach, as it was)
        bool fire_defender = false;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            fire_defender = fire_defender || (a != nullptr && a->type == sim::AntType::Fire);
        }
        if (!fire_defender) {
            end = true;
            return;
        }
    }
    if (f.offence && f.defenders.empty() && now > f.started) {                  // nobody is left to hunt it (they were hurt, taken by a task of a higher rank or killed) and no free ant near it takes over: it is over, and the next one may begin
        end = true;
        return;
    }
    // who strikes next (health-aware fights): with a Combat Ant among the defenders only punches finish an ant (kill_plan), so an ant of an even number of hit points is struck by Combat Ants alone, one of
    // an odd number (3 or more) first by ONE other ant, the nearest, so that it is never left standing with one hit point; without a Combat Ant (or when pushing into water) everybody strikes
    uint32_t opener = 0;
    bool combat_only = false;
    if (plan.health_aware && !on_fire && target->hp >= 2 && w_blows == 0 && kill_now()) {
        bool any_combat = false;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            any_combat = any_combat || (a != nullptr && a->type == sim::AntType::Combat);
        }
        if (any_combat && target->hp % 2 == 0) {
            combat_only = true;
        } else if (any_combat || f.vicinity) {
            int32_t best_d = 0;
            for (const auto& d : f.defenders) {
                const AntView* a = find_ant(v.mine(), d.first);
                if (a == nullptr || (any_combat && a->type == sim::AntType::Combat)) continue;
                const int32_t dist = a->tile.chebyshev_dist(target->tile);
                if (opener == 0 || dist < best_d) {
                    opener = a->id;
                    best_d = dist;
                }
            }
            combat_only = any_combat && opener == 0;
        }
    }
    // the orders: an ant that was never ordered, or is idle again after its blow, gets a new attack order (one order is one blow)
    std::vector<uint32_t> list;
    for (auto& d : f.defenders) {
        const AntView* a = find_ant(v.mine(), d.first);
        if (a == nullptr) continue;
        if ((combat_only && a->type != sim::AntType::Combat) || (opener != 0 && a->id != opener)) continue;
        if (on_fire && a->type != sim::AntType::Fire) continue;                  // (the others wait where they stand until the blow has thrown the target off the wall)
        bool need = false;
        if (!d.second.ordered) need = a->takes_orders();
        else if (d.second.sent != kPending) need = a->idle() && now >= d.second.sent + kBlowTicks && !v.has_pending_path(d.first);
        else need = now > d.second.decided + 200;                                // an order that no fate was ever reported for
        if (!need || in_hit_clip) continue;                                      // (a target in its hit clip is not ordered at: the order would be refused and the ant would stand in the can't-go clip)
        d.second.ordered = true;
        d.second.decided = now;
        d.second.sent = kPending;
        d.second.target_hp = target->hp;
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
    start_fire_defence(c);
    start_offence(c);
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
