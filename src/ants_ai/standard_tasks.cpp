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
constexpr uint64_t kAsideTicks = 80;          // an ant that was sent off the tiles in front of a hole is not sent to a raid, nor off again, for this long (4 s)

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

// Whether an ant of any team, the seat's own included, stands on the tile (one that dies on its clip too): no special order may name such a tile (BotController::allowed: a click on an ant
// selects it or attacks it, the target cursor shows only where no ant is under the pointer)
bool occupied(const BotView& v, sim::TileCoord tile) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == tile) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == tile) return true;
    }
    return v.dying_at(tile);
}

// Whether an ant of any team stands still on the tile (it holds the tile: an ant that walks moves on). The walk of a thief into a thief hole ends in "Can't go there." when the tiles in front of
// the hole or the raid tile itself are held by ants
bool held_by_standing_ant(const BotView& v, sim::TileCoord tile) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == tile && a.state != sim::UnitState::Walking) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == tile && a.state != sim::UnitState::Walking) return true;
    }
    return false;
}

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

// The real numbers of a blow (docs/GAME_REVERSE_ENGINEERING.md 5.36): every ant has 10 hit points, a Combat Ant's punch takes 2 of them and every other blow 1
uint32_t blow_damage(sim::AntType type) noexcept { return type == sim::AntType::Combat ? 2u : 1u; }

// Blows that an ant of `damage` per blow needs to take `hp` hit points (0 for an ant that has none)
uint32_t blows_to_kill(uint32_t hp, uint32_t damage) noexcept { return damage == 0 ? hp : (hp + damage - 1) / damage; }

// The strength of a group in a fight, the number of the square law of attrition: the sum of the hit points times the sum of the damage of one round of blows
struct Strength {
    uint32_t hp{0};
    uint32_t dmg{0};
    uint32_t n{0};
    void add(uint32_t h, uint32_t d) noexcept {
        hp += h;
        dmg += d;
        ++n;
    }
    uint64_t value() const noexcept { return static_cast<uint64_t>(hp) * dmg; }
};

// What answers a blow near `centre`: the enemy ants within `radius` tiles that do not carry food (a carrier is no fighter) and are not `exclude`, the `cap` strongest of them (the standard
// bot of Hard sends three). Hit points are on the view for every ant (the owner's decision).
Strength enemy_strength(const BotView& v, sim::TileCoord centre, int32_t radius, uint32_t exclude, uint32_t cap) {
    std::vector<std::pair<uint32_t, uint32_t>> near;                              // (hit points, damage)
    for (const AntView& e : v.others()) {
        if (e.id == exclude || e.holding || e.tile.chebyshev_dist(centre) > radius) continue;
        if (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) continue;
        near.emplace_back(std::max<uint32_t>(e.hp, 1u), blow_damage(e.type));
    }
    std::stable_sort(near.begin(), near.end(), [](const auto& x, const auto& y) { return x.first * x.second > y.first * y.second; });
    Strength out;
    for (size_t i = 0; i < near.size() && i < cap; ++i) out.add(near[i].first, near[i].second);
    return out;
}

bool on_hill_tile(const sim::Grid& grid, sim::TileCoord t) noexcept {
    for (const assets::AnthillSpawn& hill : grid.anthills()) {
        if (t.x >= static_cast<int32_t>(hill.x) && t.x <= static_cast<int32_t>(hill.x) + 3 && t.y >= static_cast<int32_t>(hill.y) && t.y <= static_cast<int32_t>(hill.y) + 3) return true;
    }
    return false;
}

}  // namespace

KillPlan kill_plan(uint32_t hp, size_t combats, size_t others) noexcept {
    KillPlan k;
    if (hp == 0) return k;
    if (hp == 1) {
        k.possible = combats + others > 0;
        k.blows = 1;
        return k;
    }
    if (combats > 0) {
        if (hp % 2 == 0) {
            k.possible = true;
            k.blows = hp / 2;
        } else if (others > 0) {
            k.possible = true;
            k.opener = true;
            k.blows = 1 + (hp - 1) / 2;
        }
        return k;
    }
    if (hp == 2 && others >= 3) {
        k.possible = true;
        k.blows = 2;
    }
    return k;
}

bool attackable(const BotView& view, const AntView& enemy, bool from_fire) {
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
    if (grid.has_fire_at(enemy.tile) && !from_fire) return false;                    // only a Fire Ant stands on a fire wall, and no other ant can step onto it to strike (its path ends on the tile: "Can't go there."); a Fire Ant can
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

bool can_strike(const BotView& view, const AntView& ant) {
    if (!ant.takes_orders() || ant.state == sim::UnitState::Stunned) return false;
    if (ant.holding || ant.carried_points > 0) return false;
    if (ant.hp < kMinFightHp) return false;
    return view.powerup_at(ant.tile) == nullptr;
}

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

// An ant of the stand batch's hunt of a Fire Ant that fires the own gate in (plan.fire_draft): any type that takes orders and is healthy; with food only a carrier that stands still while the ring is
// shut (it cannot bank anyway). `shut` is that state
bool draft_ant(const BotView& v, const AntView& a, bool shut) {
    if (!a.takes_orders() || a.state == sim::UnitState::Stunned || a.hp < kMinFightHp || v.powerup_at(a.tile) != nullptr) return false;
    if (a.holding || a.carried_points > 0) return shut && (a.idle() || a.state == sim::UnitState::CantGo);
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
    if (v.mine().size() <= plan.fight_reserve + (v.eggs() == 0 ? 1u : 0u)) return;                 // the last ants stay out of every fight (a wipe-out forces a hatch of 200 points, or ends the team)
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

// The defence against being fired in (plan.fire_defence, the owner's report: "the other team just extinguishes all of the fire, it does not try to kill the fire ant that is firing them in"): while
// fire walls stand on the ring round the own gate the enemy Fire Ant that lights them, an enemy Fire Ant within plan.fire_defence_radius tiles of the own hill, is hunted by the fighters (the
// level's defenders and plan.fire_defence_extra more, a Combat Ant first), a wounded one first; the walls are put out afterwards (WallTask)
void FightTask::start_fire_defence(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    if ((!plan.fire_defence && !plan.raider_hunt) || !v.has_grid()) return;
    const HillInfo& hill = c.map.hill(c.seat);
    if (!hill.present) return;
    if (v.mine().size() <= plan.fight_reserve + (v.eggs() == 0 ? 1u : 0u)) return;
    const uint32_t duel_spare = duel_spare_of(tactics_);
    size_t walls = 0;
    for (const sim::TileCoord& t : SabotageTask::ring_of(hill)) walls += v.grid().in_bounds(t) && v.grid().has_fire_at(t) ? 1u : 0u;
    const bool ring = plan.fire_defence && walls != 0;                                              // walls stand on the ring: the Fire Ant that lights them is the target
    // (plan.fire_duel, the stand batch) an own Fire Ant that can strike: the enemy Fire Ant stands on a fire wall, where no other ant's order reaches it, and the Fire Ant's blow throws it off the wall
    bool striker = false;
    if (plan.fire_duel && ring) {
        for (const AntView& a : v.mine()) {
            if (a.type != sim::AntType::Fire || a.id == duel_spare || !draft_ant(v, a, plan.fire_draft && walls >= kShutRingWalls)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner == kNoTask || owner == id() || c.ledger.rank(owner) < c.ledger.rank(id())) striker = true;
        }
    }
    if (!ring && !plan.raider_hunt) return;
    for (const auto& f : fights_) {
        if (f.second.fire) return;                                                                   // one hunt at a time
    }
    // (plan.raider_hunt, the war batch; the owner: "try to kill their fire ant if they try to come for them") an enemy Fire or Bomber Ant near the own hill, or at a pile the own ants work, whether
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
        if ((!fire_ant && !(plan.raider_hunt && e.type == sim::AntType::Bomber)) || fights_.count(e.id) != 0 || !attackable(v, e, striker && fire_ant) || shunned(e.id, now)) continue;
        if (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) continue;
        const int32_t d = e.tile.chebyshev_dist(hill.origin);
        const bool ring_target = ring && fire_ant && d <= plan.fire_defence_radius;
        if (!ring_target && !(plan.raider_hunt && near_work(e))) continue;
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
    if (v.mine().size() <= plan.fight_reserve + (v.eggs() == 0 ? 1u : 0u)) return;                  // the last ants stay out of every fight
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
    const auto in_scope = [&](const AntView& e) {
        for (const AntView* a : pool) {
            if (a->tile.chebyshev_dist(e.tile) <= plan.hunt_next) return true;                       // next to the own ants: Easy's whole scope
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
            if (a->tile.chebyshev_dist(e.tile) > reach || (assault && !assault_ant(*a, pull))) continue;
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
    bool best_assault = false;
    for (const AntView& e : v.others()) {
        if (fights_.count(e.id) != 0 || !attackable(v, e) || shunned(e.id, now)) continue;
        // a kill that is available: of a WOUNDED ant (a fresh one is the business of the fights that a blow begins and of the skirmish), by the plan of the real blows (kill_plan); a survivor with one
        // hit point that already walks home is out of reach
        if (plan.hunt && e.hp >= 1 && e.hp < sim::AntUnit::MAX_HP && !(e.hp == 1 && e.state == sim::UnitState::Walking) && in_scope(e)) {
            const std::vector<const AntView*> near = form(e, plan.hunt_reach, plan.hunt_force);
            Strength own;
            size_t combats = 0;
            for (const AntView* a : near) {
                own.add(a->hp, blow_damage(a->type));
                combats += a->type == sim::AntType::Combat ? 1u : 0u;
            }
            const KillPlan kill = kill_plan(e.hp, combats, near.size() - combats);
            if (kill.possible && kill.blows <= plan.hunt_blows) {
                const Strength theirs = enemy_strength(v, e.tile, plan.skirmish_near, e.id, plan.skirmish_enemy_cap);
                if (own.value() * 100u >= theirs.value() * plan.hunt_odds_percent * odds_scale / 100u) {
                    available_.insert(e.id);
                    const int32_t score = 300 - 12 * static_cast<int32_t>(kill.blows) - 4 * near.front()->tile.chebyshev_dist(e.tile) + (10 - static_cast<int32_t>(std::min<uint32_t>(e.hp, 10u))) * 6;
                    if (!running && (best == nullptr || score > best_score)) {
                        best = &e;
                        best_score = score;
                        best_hunt = true;
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
    if (v.mine().size() <= plan.fight_reserve + (v.eggs() == 0 ? 1u : 0u)) {
        end = true;                                                              // down to the last ants: they leave the fight
        return;
    }
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
    const auto kill_now = [&]() {
        if (f.hunt) return true;
        size_t combats = 0;
        size_t others = 0;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            if (a == nullptr) continue;
            if (a->type == sim::AntType::Combat) ++combats;
            else ++others;
        }
        const KillPlan kill = kill_plan(target->hp, combats, others);
        return target->hp < sim::AntUnit::MAX_HP && kill.possible && kill.blows <= plan.hunt_blows;
    };
    const bool waits_for_clip = plan.health_aware && target != nullptr && (f.offence || f.fire || f.thief || f.defenders.empty() || kill_now());
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
    if (plan.health_aware && !f.thief && !f.defenders.empty() && target->hp == 1 && target->state == sim::UnitState::Walking) {
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
    // a wounded target is finished: the fight lingers on and the leash is longer (the owner: "if they're attacking an ant and it is low on health, they should continue and try to kill it")
    const bool wounded = waits_for_clip && target->hp > 0 && target->hp <= 4;
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
        Strength own;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            if (a != nullptr) own.add(a->hp, blow_damage(a->type));
        }
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
    } else if (target->tile.chebyshev_dist(f.anchor) > plan.leash_tiles + (wounded ? 4 : 0) || now > f.last_alarm + plan.fight_linger_ticks * (wounded ? 3u : 1u)) {
        end = true;                                                              // it left the leash, or the fight is over
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
            if (target->hp > 0 && target->hp <= blow_damage(a->type)) keep = 1;                         // one blow from death: finish it
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
    const size_t wanted = f.hunt ? plan.hunt_force : f.assault ? f.force : f.offence ? plan.skirmish_force : f.fire ? plan.defenders + (f.raider ? plan.raider_extra : plan.fire_defence_extra) : plan.defenders + (tactics_.guard_stance ? 1u : 0u);
    if (f.defenders.size() < wanted) {
        std::vector<const AntView*> cands;
        for (const AntView& a : v.mine()) {
            // (the stand batch) a Fire Ant of the own side strikes the Fire Ant that fires the gate in; with the draft every ant that can strike does
            const bool fire_striker = f.fire && plan.fire_duel && a.type == sim::AntType::Fire && a.id != duel_spare && draft_ant(v, a, shut);
            if (f.defenders.count(a.id) != 0 || !(can_fight(v, a) || fire_striker || (draft && draft_ant(v, a, shut)))) continue;
            if (f.ally && a.tile.chebyshev_dist(target->tile) > plan.ally_help_radius) continue;
            if (f.offence && a.tile.chebyshev_dist(target->tile) > (f.hunt ? plan.hunt_reach : f.assault ? plan.assault_reach : plan.skirmish_reach)) continue;
            if (f.fire && a.tile.chebyshev_dist(target->tile) > (draft && a.type == sim::AntType::Combat ? plan.fire_draft_far : 20)) continue;
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
    // an odd number (3 or more) first by ONE other ant, the nearest, so that it is never left standing with one hit point; without a Combat Ant everybody strikes
    uint32_t opener = 0;
    bool combat_only = false;
    if (plan.health_aware && !on_fire && target->hp >= 2 && kill_now()) {
        bool any_combat = false;
        for (const auto& d : f.defenders) {
            const AntView* a = find_ant(v.mine(), d.first);
            any_combat = any_combat || (a != nullptr && a->type == sim::AntType::Combat);
        }
        if (any_combat) {
            if (target->hp % 2 == 0) {
                combat_only = true;
            } else {
                int32_t best_d = 0;
                for (const auto& d : f.defenders) {
                    const AntView* a = find_ant(v.mine(), d.first);
                    if (a == nullptr || a->type == sim::AntType::Combat) continue;
                    const int32_t dist = a->tile.chebyshev_dist(target->tile);
                    if (opener == 0 || dist < best_d) {
                        opener = a->id;
                        best_d = dist;
                    }
                }
                combat_only = opener == 0;
            }
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
        for (size_t i = 0; i < mask_.size() && i < tactics_.shut_tiles.size(); ++i) {
            if (tactics_.shut_tiles[i] != 0) mask_[i] = 0;                                         // (the bridges that will not last the trip: the island task)
        }
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
    struct Cand {
        const PowerUpView* p;
        int side;                                                                                   // whose side of the map it is (power_up_side: asked once per power-up, the sort compares often)
    };
    std::vector<Cand> cands;
    uint8_t present = 0;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) present = static_cast<uint8_t>(present | (v.rows()[t].present ? 1u << t : 0u));
    for (const PowerUpView& p : v.powerups()) {
        if (p.kind != kind || p.standing_ant != 0 || tile_blacklisted(p.tile, now)) continue;
        const int side = power_up_side(c.map, p.tile, present);
        if (side >= 0 && side != c.seat && (!tactics_.plan.steals || (v.ally() < sim::MAX_PLAYERS && side == v.ally()))) continue;       // another side's power-up is a theft: Hard's alone, and never the ally's
        bool taken = false;
        for (const auto& t : takes_) taken = taken || t.second.tile == p.tile;
        if (!taken) cands.push_back(Cand{&p, side});
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
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& x, const Cand& y) {
        const bool ox = x.side == c.seat || x.side < 0;                                             // the own side's first, then by the distance
        const bool oy = y.side == c.seat || y.side < 0;
        if (ox != oy) return ox;
        return nearest(x.p) < nearest(y.p);
    });
    const sim::Grid& grid = v.grid();
    for (size_t i = 0; i < cands.size() && i < 3; ++i) {
        const PowerUpView& p = *cands[i].p;
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
        const uint32_t walk = static_cast<uint32_t>(MapInfo::walking_ticks(best_cost)) + 5u;           // the pick-up comes 8 d + 5 ticks after the order on grass (measured in the engine, docs/audit/B4_1_notes.md)
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

// The watcher (Hard): an ant waits beside the drop tile of the own side's flower, arrives about plan.flower_watch_early ticks before the landing that Memory::flower expects, takes a kind that
// the plan lacks like any trip does, and goes home when the plan lacks nothing the flower is likely to drop or the wait is longer than a trip there and back (docs/BOTS.md, "The flowers").
void PowerUpTask::watch(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const auto give_up = [&]() {
        if (watch_.ant == 0 || takes_.count(watch_.ant) != 0) return;                   // (a trip that is under way ends by itself, and a typed ant then steps off)
        c.ledger.release(watch_.ant, id());
        watch_ = Watch{};
    };
    if (watch_.ant != 0) {                                                              // is the watcher still the watcher
        const AntView* w = find_ant(v.mine(), watch_.ant);
        const TaskId owner = w != nullptr ? c.ledger.owner(watch_.ant) : kNoTask;
        const bool hit = w != nullptr && w->hp < watch_.hp;
        if (hit) watch_off_until_ = now + kWatchHoldAfterHit;                           // (a hit ends the wait, and nobody waits there for a while: an enemy is near, and a fight never takes an ant of this rank)
        if (w == nullptr || w->type != v.default_ant_type() || (owner != id() && owner != kNoTask) || w->holding || w->carried_points > 0) {
            if (w != nullptr && owner == id() && takes_.count(watch_.ant) == 0) c.ledger.release(watch_.ant, id());
            watch_ = Watch{};
        } else if (owner == kNoTask && !c.ledger.take(watch_.ant, id())) {
            watch_ = Watch{};
        }
    }
    if (!plan.flower_watch || !plan.flower_sides || !plan.secure_side || v.ticks_left() < 900u || now < watch_off_until_) {
        give_up();
        return;
    }
    uint8_t present = 0;
    for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) present = static_cast<uint8_t>(present | (v.rows()[team].present ? 1u << team : 0u));
    // what the plan lacks of the kinds that it takes for its own side (no ant of the kind, none on its way)
    uint32_t lacking = 0;
    for (const sim::AntType kind : {sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Thief}) {
        if (((plan.secure_kinds >> static_cast<unsigned>(kind)) & 1u) == 0) continue;
        bool have = false;
        for (const AntView& a : v.mine()) have = have || a.type == kind;
        for (const auto& take : takes_) have = have || take.second.kind == kind;
        if (!have) lacking |= 1u << static_cast<unsigned>(kind);
    }
    // the flower: the own side's (no other hill is nearer to its drop tile), the nearest of those to the hill
    const FlowerInfo* flower = nullptr;
    for (const FlowerInfo& f : c.map.flowers()) {
        if (drop_side(c.map, f.drop, present) != static_cast<int>(c.seat)) continue;
        if (flower == nullptr || f.approach[c.seat].cost < flower->approach[c.seat].cost) flower = &f;
    }
    const FlowerLog* log = flower != nullptr ? tactics_.memory.flower(flower->drop) : nullptr;
    if (lacking == 0 || log == nullptr || log->cycle() < 60u || log->chance(lacking) < plan.flower_watch_chance) {
        give_up();                                                                      // nothing to wait for, or the flower is not known well enough yet (a cycle takes two droplets seen)
        return;
    }
    if (watch_.ant != 0 && watch_.drop != flower->drop) give_up();
    const std::vector<int32_t>& to_drop = field_for(c, flower->drop);
    if (to_drop.empty()) {
        give_up();
        return;
    }
    const sim::Grid& grid = v.grid();
    const HillInfo& hill = c.map.hill(c.seat);
    sim::TileCoord spot = watch_.spot;
    if (watch_.ant == 0) {                                                              // the tile beside the drop tile that the ant stands on: a free one, the nearest to the hill
        bool found = false;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const sim::TileCoord n{flower->drop.x + dx, flower->drop.y + dy};
                if ((dx == 0 && dy == 0) || !grid.in_bounds(n) || to_drop[tile_index(grid, n)] < 0 || v.powerup_at(n) != nullptr) continue;
                bool occupied = false;
                for (const AntView& a : v.others()) occupied = occupied || a.tile == n;
                for (const AntView& a : v.mine()) occupied = occupied || a.tile == n;
                if (occupied) continue;
                if (!found || n.chebyshev_dist(hill.queue) < spot.chebyshev_dist(hill.queue)) spot = n;
                found = true;
            }
        }
        if (!found) return;
    }
    const std::vector<int32_t>& to_spot = field_for(c, spot);
    if (to_spot.empty() || !grid.in_bounds(hill.queue) || to_spot[tile_index(grid, hill.queue)] < 0) {
        give_up();                                                                      // (a place that no walk joins to the hill: the watcher would stand there for good)
        return;
    }
    // the next landing the bot expects (a landing that did not come, the tile was held, moves it one cycle on) and the walk of the trip there
    uint64_t next = log->next();
    while (next + 60u < now) next += log->cycle();
    const uint64_t round_trip = 2u * (static_cast<uint64_t>(MapInfo::walking_ticks(to_spot[tile_index(grid, hill.queue)])) + 5u);
    if (watch_.ant == 0) {
        const AntView* best = nullptr;
        int32_t best_cost = 0;
        for (const AntView& a : v.mine()) {
            if (!usable_for_powerup(v, a) || takes_.count(a.id) != 0 || !grid.in_bounds(a.tile)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            const int32_t cost = to_spot[tile_index(grid, a.tile)];
            if (cost < 0 || (best != nullptr && cost >= best_cost)) continue;
            best = &a;
            best_cost = cost;
        }
        if (best == nullptr) return;
        const uint64_t walk = static_cast<uint64_t>(MapInfo::walking_ticks(best_cost)) + 5u;
        if (walk > params_.max_trip_ticks || now + walk + plan.flower_watch_early < next) return;          // not yet: it leaves so as to arrive flower_watch_early ticks before the landing
        if (!c.ledger.take(best->id, id())) return;
        watch_ = Watch{best->id, flower->drop, spot, 0, best->hp};
        ++watched_;
    }
    const AntView* w = find_ant(v.mine(), watch_.ant);
    if (w == nullptr) return;
    if (w->tile == watch_.spot) {
        const bool landing_pending = now <= log->last + 60u;                              // a droplet falls or has just landed: the watcher is there for that one
        if (!landing_pending && next > now + round_trip + plan.flower_watch_early + 30u) give_up();   // the wait for the next one is longer than a trip there and back: it goes home
        return;
    }
    if (takes_.count(watch_.ant) == 0 && w->takes_orders() && (watch_.ordered == 0 || (w->idle() && now >= watch_.ordered + 80u))) {      // (the first order goes to an ant that walks too: the engine's harvest loop leads it to a pile)
        c.orders.move({watch_.ant}, watch_.spot, Priority::Normal);
        watch_.ordered = now;
    }
}

// A typed ant that took a drop from the watcher's place and still stands on the drop tile steps off it once (a standing ant locks the flower: no drop is made on a tile that an ant stands on)
void PowerUpTask::step_off(TaskContext& c) {
    const uint64_t now = c.view.tick();
    for (auto it = step_off_.begin(); it != step_off_.end();) {
        const AntView* a = find_ant(c.view.mine(), it->ant);
        if (a == nullptr || now >= it->until || a->tile != it->drop) {
            it = step_off_.erase(it);
        } else if (a->takes_orders()) {
            c.orders.move({it->ant}, it->spot, Priority::Urgent);
            it = step_off_.erase(it);
        } else {
            ++it;
        }
    }
}

void PowerUpTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const Profile& profile = c.profile;
    for (const uint32_t ant : c.ledger.ants_of(id())) {                                 // an ant whose trip is over (or never left) is free again (the watcher waits for a landing)
        if (takes_.count(ant) == 0 && ant != watch_.ant) c.ledger.release(ant, id());
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
            if (ant == watch_.ant) {
                step_off_.push_back(StepOff{ant, watch_.drop, watch_.spot, now + 200u});
                watch_ = Watch{};
            }
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
            } else if (p->kind != t.kind && a->tile.chebyshev_dist(t.tile) >= (tactics_.plan.flower_recall ? 1 : static_cast<int32_t>(params_.call_back_tiles))) {
                over = true;                                                            // a dropper changed what lies there: a plain order elsewhere cancels the walk before it crosses (the recall: from any distance, else the ant would take a kind that the plan does not want)
                call_back.push_back(ant);
                tile_black_[key_of(t.tile)] = now + 100;
                ++called_back_;
            } else if (tactics_.plan.flower_recover && (a->holding || a->carried_points > 0) && c.map.flower_at(t.tile) != nullptr) {
                over = true;                                                            // the ant took food before the order reached it (a bite does not hear an order): the gate has it now, and the want is tried again with another ant
                ++failed_;
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
    watch(c);
    step_off(c);

    // 2. new trips for what is wanted and missing, in the order of value (Fire, Bomber, Thief, Combat, Swimmer), as many as max_active allows: the opening sends them all at the first look
    for (const sim::AntType kind : tactics_.plan.opening_order) {
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

// Whether a fire wall of the ring round the own gate is not to be put out now (the owner's report: the bots "just extinguish all of the fire" and leave the Fire Ant that lights it free to light it
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
// enemy's walk to the hill), plan.mine_home standing at a time, three tiles apart from every other mine. An enemy walk does not go round a bomb of the bot (the engine's path finder skips only the
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
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally()) || !c.map.hill(t).present) continue;
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
            for (const PileView& p : v.piles()) {
                const PileInfo* info = c.map.pile(p.index);
                if (info == nullptr) continue;
                for (const sim::TileCoord& q : info->cells) by_pile = by_pile || q.chebyshev_dist(t) <= 2;
                // (plan.mine_home_off_route) nor on the way of the bot's own carriers to a pile: own bombs block their walks, and on a map whose food lies between the hills that way is also the
                // enemy's lane to the gate, where every bot of a room laid its field and all of them lost 17 percent of their food on TREASURE. The way is estimated: the walk to the tile plus
                // twenty cost units a tile of the straight rest of it costs no more than the walk to the pile and a little
                if (plan.mine_home_off_route && !by_pile && p.remaining >= plan.mine_min_units && info->approach[c.seat].reachable() && !info->cells.empty()) {
                    int32_t near = 1 << 20;
                    for (const sim::TileCoord& q : info->cells) near = std::min(near, q.chebyshev_dist(t));
                    by_pile = own_field[idx] + 20 * near <= info->approach[c.seat].cost + kRouteSlack;
                }
            }
            if (by_pile) continue;
            bool crowded = false;
            for (const BombView& b : v.bombs()) crowded = crowded || b.tile.chebyshev_dist(t) < static_cast<int32_t>(plan.mine_home_apart);
            for (const auto& j : jobs_) crowded = crowded || j.second.tile.chebyshev_dist(t) < static_cast<int32_t>(plan.mine_home_apart);
            if (crowded) continue;
            here.push_back(Target{t, 100 - lane - 12 * std::abs(ring - 7)});                // on the lane and about seven tiles out first
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
    if (free_to_mine) collect_pile_targets(c, targets);
    if (targets.empty()) collect_home_targets(c, targets);                              // (a few mines round the own hill: the Bomber has nothing else to do for them)
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
        jobs_[actor->id] = j;
        return;
    }
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
    if (fate == Bot::Fate::Sent) {
        it->second.sent = tick;
        return;
    }
    if (fate == Bot::Fate::Filtered) black_[it->second.team] = tick + params_.filtered_ticks;     // the click is refused: that hill is left alone for a while, or it is ordered at every look
    raids_.erase(it);                                                   // it never left: the thief is ordered again at the next look
}

bool RaidTask::launch(TaskContext& c, const AntView& thief) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    const LevelPlan& plan = tactics_.plan;
    // behind the leader a smaller loot is worth the trip (tier 1: two thirds, tier 2: a third, tier 3: 10 points) and a hill with an enemy Combat Ant near it is raided (tier 2 and up)
    const uint8_t tier = plan.catchup ? tactics_.standing.tier : 0;
    const uint32_t min_loot = tier >= 3 ? std::min<uint32_t>(plan.raid_min_loot, 10u) : tier == 2 ? plan.raid_min_loot / 3u : tier == 1 ? plan.raid_min_loot * 2u / 3u : plan.raid_min_loot;
    // the teams that may be raided, the leading one first (the score boxes: a team that is not in the match, has dropped out or is an ally shows nothing or is no victim)
    std::vector<uint8_t> teams;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally())) continue;
        if (row.score < static_cast<int32_t>(min_loot) || black(t, now)) continue;
        teams.push_back(t);
    }
    // the cheap checks first: a hill that is not there, or whose hole is shut, is no target, and a thief that looks at the world every few ticks must not search the map for nothing (a full
    // search per look and thief was the most expensive thing that the bot did)
    teams.erase(std::remove_if(teams.begin(), teams.end(),
                               [&](uint8_t t) {
                                   const HillInfo& h = c.map.hill(t);
                                   if (!h.present) return true;
                                   // raided with raid_min_free free tiles in front (nothing lit, bombed or solid; with the can't-go fixes no ant on them) and no ant on the raid tile: such an ant shuts the hole
                                   size_t free_tiles = 0;
                                   for (const sim::TileCoord& e : east_tiles(h)) {
                                       const EastTile k = classify_tile(grid, e);
                                       free_tiles += (k == EastTile::Open || k == EastTile::Bare) && !(plan.cantgo_aware && held_by_standing_ant(v, e)) ? 1u : 0u;
                                   }
                                   if (free_tiles < plan.raid_min_free || (plan.cantgo_aware && held_by_standing_ant(v, h.raid))) return true;
                                   // an own thief on the raid tile (it is raiding, or has raided and is on its way out) has the hole: a second one sent now waits on a tile in front of it, and the first one cannot leave
                                   // when the walls have shut the other tiles (unjam)
                                   if (plan.raid_unjam && plan.cantgo_aware) {
                                       for (const AntView& m : v.mine()) {
                                           if (m.id != thief.id && m.type == sim::AntType::Thief && m.tile == h.raid) return true;
                                       }
                                   }
                                   return false;
                               }),
                teams.end());
    if (teams.empty()) return false;
    std::stable_sort(teams.begin(), teams.end(), [&](uint8_t x, uint8_t y) { return v.rows()[x].score > v.rows()[y].score; });
    const std::vector<uint8_t> mask = MapInfo::walkable_mask(grid, c.seat, v.walk_context());
    const std::vector<int32_t> field = MapInfo::cost_field(grid, mask, thief.tile);
    if (field.empty()) return false;
    for (const uint8_t t : teams) {
        const HillInfo& hill = c.map.hill(t);
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
        if (plan.avoids_guarded_hills && tier < 2) {
            bool guarded = false;
            for (const AntView& e : v.others()) {
                if (e.team == t && e.type == sim::AntType::Combat && e.tile.chebyshev_dist(hill.raid) <= static_cast<int32_t>(params_.guard_radius)) guarded = true;
            }
            if (guarded) continue;
        }
        // any tile of the enemy mound is a raid click (the engine's classification); the entrance is the usual one, and when an ant stands on it (the enemy's carriers enter there) another tile of the
        // mound that no ant stands on: the cursor shows the target cursor only where no ant is under the pointer
        sim::TileCoord click = hill.entrance;
        if (occupied(v, click)) {
            click = sim::TileCoord{-1, -1};
            for (const sim::TileCoord& alt : {sim::TileCoord{hill.origin.x + 2, hill.origin.y + 1}, sim::TileCoord{hill.origin.x + 1, hill.origin.y + 2}, sim::TileCoord{hill.origin.x + 2, hill.origin.y + 2},
                                              sim::TileCoord{hill.origin.x, hill.origin.y}, sim::TileCoord{hill.origin.x + 3, hill.origin.y + 3}}) {
                if (!occupied(v, alt)) {
                    click = alt;
                    break;
                }
            }
            if (click.x < 0) continue;                                                      // (the whole mound is covered by ants: the next look)
        }
        c.orders.special(thief.id, click, Priority::Normal);
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

namespace {
bool plan_idle_or_walking(const AntView& a) noexcept { return a.idle() || a.state == sim::UnitState::Walking; }
}  // namespace

// A thief that finds every hill shut or poor goes and waits where the thief hole of the leading team can be reached in a few ticks, and raids when the hole opens
bool RaidTask::ambush(TaskContext& c, const AntView& thief) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const sim::Grid& grid = v.grid();
    if (!plan.ambush || now < ambush_pause_until_) return false;
    const auto held = waiting_.find(thief.id);
    if (held == waiting_.end() && waiting_.size() >= plan.ambush_thieves) return false;
    // the target: the leading team (by the score boxes) with loot, a hill that is there, not shut by chance of the moment (launch() would have gone) but shuttable at all
    int target = -1;
    int32_t best = -1;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = v.rows()[t];
        if (t == c.seat || !row.present || row.dropped || (v.ally() < sim::MAX_PLAYERS && t == v.ally())) continue;
        if (row.score < static_cast<int32_t>(plan.raid_min_loot) || !c.map.hill(t).present || row.score <= best) continue;
        target = t;
        best = row.score;
    }
    if (target < 0 || v.ticks_left() < 900) return false;
    const HillInfo& hill = c.map.hill(static_cast<uint8_t>(target));
    // the waiting place: east of the three tiles in front of the hole
    sim::TileCoord spot{-1, -1};
    for (int32_t d = plan.ambush_distance; d >= 3 && spot.x < 0; --d) {
        for (const int32_t dy : {2, 1, 3, 0, 4}) {
            const sim::TileCoord t{hill.origin.x + 4 + d, hill.origin.y + dy};
            if (grid.in_bounds(t) && MapInfo::walkable(grid, c.seat, t, v.walk_context()) && v.powerup_at(t) == nullptr && reachable_by(c.map, c.seat, t)) {
                spot = t;
                break;
            }
        }
    }
    if (spot.x < 0) return false;
    Waiting w;
    if (held != waiting_.end()) w = held->second;
    else {
        w.since = now;
        ++ambushes_;
    }
    w.team = static_cast<uint8_t>(target);
    w.spot = spot;
    if (now >= w.since + plan.ambush_ticks) {                                           // nothing came of it: the economy needs it
        waiting_.erase(thief.id);
        ambush_pause_until_ = now + plan.ambush_pause;
        return false;
    }
    if (thief.tile.chebyshev_dist(spot) > 2 && now >= w.last_move + 120u) {
        c.orders.move({thief.id}, spot, Priority::Normal);
        w.last_move = now;
    }
    waiting_[thief.id] = w;
    return true;
}

namespace {

// A tile for an ant that waits in front of a hole to step to: east of the three tiles in front of it and off their rows, which a thief that leaves walks; {-1, -1} when there is none
sim::TileCoord aside_of_hole(TaskContext& c, const HillInfo& hill, sim::TileCoord from) {
    const BotView& v = c.view;
    sim::TileCoord best{-1, -1};
    int32_t best_d = 0;
    for (int32_t dx = 6; dx <= 8; ++dx) {
        for (int32_t dy = -2; dy <= 6; ++dy) {
            if (dy >= 1 && dy <= 3) continue;
            const sim::TileCoord t{hill.origin.x + dx, hill.origin.y + dy};
            if (!v.grid().in_bounds(t) || v.grid().has_fire_at(t) || v.powerup_at(t) != nullptr || occupied(v, t)) continue;
            if (!MapInfo::walkable(v.grid(), c.seat, t, v.walk_context()) || !reachable_by(c.map, c.seat, t)) continue;
            const int32_t d = t.chebyshev_dist(from);
            if (best.x < 0 || d < best_d) {
                best = t;
                best_d = d;
            }
        }
    }
    return best;
}

}  // namespace

// Two thieves on one hole (the real game lets the second wait on a tile in front of it until the first is out). The victim's Fire Ant walls the hole in while the first raids, and when the first
// has the loot and no tile in front of the hole is free but the one on which the second waits, it shows "Can't go there." for as long as the walls burn (3,600 ticks) while the second waits for
// the raid tile: neither can move. A person moves the second aside. So does the bot: an own ant that stands on a tile in front of a hole whose raid tile an own thief holds (not in its raid clip)
// while no tile in front of the hole is free steps off, to a tile east of the hole and off the lane; the raid is ordered again when the thief is out (launch: no hole with an own thief on its raid
// tile). Only an own thief or an ant that stands still is moved; an enemy ant on the tile is not the bot's to move
void RaidTask::unjam(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    for (auto it = aside_.begin(); it != aside_.end();) it = it->second + kAsideTicks <= now ? aside_.erase(it) : std::next(it);
    if (!tactics_.plan.raid_unjam || !tactics_.plan.cantgo_aware || !v.has_grid()) return;
    const sim::Grid& grid = v.grid();
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const HillInfo& hill = c.map.hill(t);
        if (t == c.seat || !hill.present) continue;
        bool shut_in = false;                                                           // an own thief on the raid tile that is no longer raiding: it stands (idle, or in its can't-go clip) and has to leave
        for (const AntView& a : v.mine()) {
            if (a.type == sim::AntType::Thief && a.tile == hill.raid && (a.idle() || a.state == sim::UnitState::CantGo)) shut_in = true;
        }
        if (!shut_in) continue;
        std::vector<const AntView*> movers;                                              // the own ants that stand on the tiles in front of the hole and may be sent off, in the order of the tiles
        bool free_tile = false;
        for (const sim::TileCoord& e : east_tiles(hill)) {
            const EastTile k = classify_tile(grid, e);
            if (k != EastTile::Open && k != EastTile::Bare) continue;                    // a wall, a bomb, rock or water: nobody stands there
            bool held = false;
            for (const AntView& a : v.others()) held = held || a.tile == e;
            for (const AntView& a : v.mine()) {
                if (a.tile != e) continue;
                held = true;
                if (aside_.count(a.id) == 0 && a.takes_orders() && !a.holding && a.carried_points == 0 && (a.type == sim::AntType::Thief || a.idle())) movers.push_back(&a);
            }
            free_tile = free_tile || !held;                                             // a tile is free: the thief leaves by it
        }
        if (free_tile) continue;
        for (const AntView* mover : movers) {                                           // (the first that is nobody else's: a fight may have taken one)
            const sim::TileCoord spot = aside_of_hole(c, hill, mover->tile);
            if (spot.x < 0 || !c.ledger.take(mover->id, id())) continue;
            c.orders.move({mover->id}, spot, Priority::Normal);
            raids_.erase(mover->id);                                                    // (a waiting thief's raid is over: it must not count as one that never got going)
            waiting_.erase(mover->id);
            aside_[mover->id] = now;
            c.ledger.release(mover->id, id());
            ++unjams_;
            break;
        }
    }
}

void RaidTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!v.has_grid()) return;
    unjam(c);
    for (auto it = waiting_.begin(); it != waiting_.end();) {                           // a waiting thief that is gone, hurt or busy with something else is not waiting
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || a->type != sim::AntType::Thief || c.ledger.owner(it->first) != id() || a->hp < 4 || a->holding || a->carried_points > 0) it = waiting_.erase(it);
        else ++it;
    }
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
                    black_[r.team] = now + tactics_.plan.raid_black_ticks;
                    ++failures_;
                }
                raids_.erase(it);                                                       // (or the raid is over and it stands empty-handed: the victim had nothing)
            } else if (r.sent == kPending && now > r.decided + 200u) {
                raids_.erase(it);
            }
            continue;
        }
        if (aside_.count(a.id) != 0) {                                                  // it was sent off the tiles in front of a hole a moment ago: the economy's for now
            if (held) c.ledger.release(a.id, id());
            continue;
        }
        if (!launching_) {                                                              // no raid is wanted now (the pressure fell): the thief is the economy's again
            if (held) c.ledger.release(a.id, id());
            waiting_.erase(a.id);
            continue;
        }
        // free to go: empty-handed and healthy, standing or on its way to a pile (a thief that harvests is never idle: its walk to the pile, the bite and the walk home are the engine's loop, so a
        // raid that waited for an idle thief would hardly ever start); one that carries food delivers it first
        const bool free_to_go = (plan_idle_or_walking(a)) && !a.holding && a.carried_points == 0 && a.hp >= 4 && a.takes_orders() && v.powerup_at(a.tile) == nullptr;
        if (!free_to_go) {
            if (held && waiting_.count(a.id) == 0) c.ledger.release(a.id, id());        // walking, carrying, hurt: the economy's again (a thief on its way to its waiting place stays ours)
            continue;
        }
        if (!held && !c.ledger.take(a.id, id())) continue;                              // a task of a higher rank has it
        if (launch(c, a)) {
            waiting_.erase(a.id);
            continue;
        }
        if (!ambush(c, a)) c.ledger.release(a.id, id());                                // nothing to raid, and nothing to wait for: back to the economy
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
    if (!v.has_grid() || !(plan.strikes || plan.wipe_focus || plan.catchup) || plan.strike_force == 0 || tactics_.guard_stance) {
        if (!force_.empty()) disband(c);
        return;
    }
    // behind the leader: the old trigger (plan.strikes, a margin in points) or the tiers of the pressure (plan.catchup); the tiers lower the odds (120 percent at tier 2, 100 at tier 3), take away
    // the stop of the last minutes (tier 3: all-in) and may widen the force (workers from plan.catchup_workers_tier: never as shipped, they kill nothing above two hit points)
    const bool striking = (plan.strikes && st.behind) || (plan.catchup && st.tier >= 1);
    const bool workers_join = plan.strike_workers || (plan.catchup && st.tier >= plan.catchup_workers_tier);
    const uint32_t reserve = plan.catchup && st.tier >= 3 ? std::min<uint32_t>(plan.strike_reserve, 2u) : plan.strike_reserve;
    const uint32_t odds = plan.catchup && st.tier >= 3 ? std::min<uint32_t>(plan.strike_odds_percent, 100u) : plan.catchup && st.tier == 2 ? std::min<uint32_t>(plan.strike_odds_percent, 120u) : plan.strike_odds_percent;
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
            if (!workers_join && a.type != sim::AntType::Combat) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner != kNoTask && c.ledger.rank(owner) >= 3) continue;           // the ants of a pick-up, a raid, a wall, a bomb or a fight are not taken
        }
        cands.push_back(&a);
    }
    const size_t total = v.mine().size();
    const size_t limit = std::min<size_t>(plan.strike_force, total > reserve ? total - reserve : 0);
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
    const bool time_ok = (v.ticks_left() >= plan.strike_stop_ticks || (plan.catchup && st.tier >= 3 && v.ticks_left() >= 240)) && now >= paused_until_;
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
    if (!wipe && time_ok && striking && st.leader >= 0 && c.map.hill(static_cast<uint8_t>(st.leader)).present) {
        target = st.leader;
        const HillInfo& hill = c.map.hill(static_cast<uint8_t>(target));
        const uint8_t friend_of_target = v.rows()[static_cast<size_t>(target)].ally;
        for (const AntView& e : v.others()) {
            if (e.team != target && e.team != friend_of_target) continue;
            if (e.tile.chebyshev_dist(hill.queue) <= plan.strike_radius) enemy_near += enemy_weight(e, now);
        }
        const uint32_t need = active_ ? 100u : odds;
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
        for (const uint32_t waiting_id : waiting) {
            const AntView* a = find_ant(v.mine(), waiting_id);
            if (a == nullptr || a->tile.chebyshev_dist(station) <= 2) continue;
            c.orders.move({waiting_id}, station, Priority::Normal);
        }
    }
}

// ---- RushTask -------------------------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr size_t kRushMinForce = 4;                 // a rush is not begun with fewer ants than this
constexpr int32_t kRallyCost = 400;                 // the rally tile lies where the leader's walk from his hill costs this much (about twenty tiles of grass)
constexpr int32_t kRallyRadius = 6;                 // an ant within this many tiles of the rally tile has gathered
constexpr uint64_t kGatherTicks = 1500;             // the force does not wait longer than this for the rest
constexpr int32_t kRushRadius = 18;                 // the ants of the leader within this many tiles of his queue row are the targets
constexpr uint32_t kRushPauseTicks = 1800;          // the next rush begins after this long at the earliest
constexpr uint64_t kRushLastTicks = 100;            // no rush in the last ticks of the match

// A tile on the way from the own hill to the leader's where his walk from his hill costs kRallyCost: the cost field of the leader is walked downhill from the own hill's waiting tile
sim::TileCoord rally_tile(TaskContext& c, uint8_t target) {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const std::vector<int32_t>& foe = c.map.cost_field_of(target);
    const HillInfo& own = c.map.hill(c.seat);
    if (foe.empty() || !own.present) return sim::TileCoord{-1, -1};
    const int32_t width = c.map.width();
    const auto cost = [&](sim::TileCoord t) { return grid.in_bounds(t) ? foe[static_cast<size_t>(t.y) * static_cast<size_t>(width) + static_cast<size_t>(t.x)] : -1; };
    sim::TileCoord cur{-1, -1};
    for (const sim::TileCoord& t : {own.tile42, own.raid, own.queue, own.entrance}) {
        if (cost(t) >= 0) {
            cur = t;
            break;
        }
    }
    if (cur.x < 0) return cur;
    for (int step = 0; step < 800 && cost(cur) > kRallyCost; ++step) {
        sim::TileCoord next = cur;
        int32_t best = cost(cur);
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const sim::TileCoord n{cur.x + dx, cur.y + dy};
                const int32_t cn = cost(n);
                if (cn >= 0 && cn < best) {
                    best = cn;
                    next = n;
                }
            }
        }
        if (next == cur) break;
        cur = next;
    }
    // a tile that is no goal for a walk (a power-up lies on it): the nearest free one
    if (v.powerup_at(cur) != nullptr || !MapInfo::walkable(grid, c.seat, cur, v.walk_context())) {
        for (int32_t dy = -2; dy <= 2; ++dy) {
            for (int32_t dx = -2; dx <= 2; ++dx) {
                const sim::TileCoord n{cur.x + dx, cur.y + dy};
                if (grid.in_bounds(n) && cost(n) >= 0 && v.powerup_at(n) == nullptr && MapInfo::walkable(grid, c.seat, n, v.walk_context())) return n;
            }
        }
    }
    return cur;
}

}  // namespace

void RushTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupAttack && command.type != sim::CommandType::GroupMove) return;
    for (const uint32_t ant : command.ants) {
        const auto it = force_.find(ant);
        if (it == force_.end() || it->second.sent != kPending) continue;
        if (fate == Bot::Fate::Sent) it->second.sent = tick;
        else it->second.ordered = false;
    }
}

void RushTask::disband(TaskContext& c, uint64_t now) {
    for (const auto& m : force_) c.ledger.release(m.first, id());
    force_.clear();
    if (active_) paused_until_ = now + kRushPauseTicks;
    active_ = false;
    assault_ = false;
    target_ = -1;
    rally_ = sim::TileCoord{-1, -1};
}

void RushTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const Standing& st = tactics_.standing;
    const uint64_t since = last_look_ != 0 && now > last_look_ ? now - last_look_ : 0;
    last_look_ = now;
    if (active_) rush_ticks_ += since;
    if (!plan.rush || !v.has_grid() || tactics_.guard_stance) {
        disband(c, now);
        return;
    }
    const HillInfo* foe_hill = st.leader >= 0 ? &c.map.hill(static_cast<uint8_t>(st.leader)) : nullptr;
    const bool leader_ok = foe_hill != nullptr && foe_hill->present && !v.rows()[static_cast<size_t>(st.leader)].dropped;
    const bool deficit_ok = st.war >= plan.rush_tier && st.deficit >= static_cast<int32_t>(plan.rush_deficit);
    if (active_) {                                                                    // is it over?
        const bool caught_up = st.war < plan.rush_tier && st.deficit < static_cast<int32_t>(plan.rush_deficit / 2);
        if (!leader_ok || st.leader != target_ || caught_up || now > started_ + plan.rush_ticks || v.ticks_left() <= kRushLastTicks) {
            disband(c, now);
            return;
        }
    } else if (!leader_ok || !deficit_ok || now < plan.rush_after || now < paused_until_ || v.ticks_left() < plan.rush_min_left) {
        if (!force_.empty()) disband(c, now);
        return;
    }

    // 1. the members that are still ours and fit (an ant that picked up food or got hurt leaves; it is called again when it is well and empty-handed)
    for (auto it = force_.begin(); it != force_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool gone = a == nullptr || c.ledger.owner(it->first) != id() || a->hp < kMinFightHp || a->holding || a->carried_points > 0;
        if (gone) {
            if (a != nullptr && c.ledger.owner(it->first) == id()) c.ledger.release(it->first, id());
            it = force_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. the ants to call: every one that holds no food and that no task of the pick-ups' rank holds
    std::vector<const AntView*> cands;
    for (const AntView& a : v.mine()) {
        if (force_.count(a.id) != 0) continue;
        if (!can_strike(v, a) || (a.type == sim::AntType::Worker && !tactics_.plan.rush_workers)) continue;
        const TaskId owner = c.ledger.owner(a.id);
        if (owner != kNoTask && (c.ledger.rank(owner) == 3 || c.ledger.rank(owner) >= c.ledger.rank(id()))) continue;       // a pick-up, a raid, the sabotage, the islands; a fight (the ledger gives nothing to a task of the same rank)
        cands.push_back(&a);
    }
    if (!active_) {
        if (force_.size() + cands.size() < kRushMinForce) return;
        const sim::TileCoord rally = rally_tile(c, static_cast<uint8_t>(st.leader));
        if (rally.x < 0) {                                                            // no walk joins the two hills (an island): the ants would stand at the water
            paused_until_ = now + kRushPauseTicks;
            return;
        }
        active_ = true;
        assault_ = false;
        target_ = st.leader;
        started_ = now;
        rally_ = rally;
        ++rushes_started_;
    }
    for (const AntView* a : cands) {
        if (!c.ledger.take(a->id, id())) continue;
        force_[a->id] = Member{};
    }
    if (force_.size() < 2) {
        disband(c, now);
        return;
    }
    const HillInfo& hill = c.map.hill(static_cast<uint8_t>(target_));
    const uint8_t friend_team = v.rows()[static_cast<size_t>(target_)].ally;

    // 3. gathered?
    if (!assault_) {
        size_t there = 0;
        for (const auto& m : force_) {
            const AntView* a = find_ant(v.mine(), m.first);
            if (a != nullptr && (rally_.x < 0 || a->tile.chebyshev_dist(rally_) <= kRallyRadius)) ++there;
        }
        if (rally_.x < 0 || there * 10u >= force_.size() * 7u || now > started_ + kGatherTicks) assault_ = true;
    }

    // 4. the orders
    std::vector<const AntView*> targets;
    if (assault_) {
        for (const AntView& e : v.others()) {
            if (e.team != target_ && e.team != friend_team) continue;
            if (!attackable(v, e) || e.tile.chebyshev_dist(hill.queue) > kRushRadius) continue;
            targets.push_back(&e);
        }
    }
    std::map<uint32_t, std::vector<uint32_t>> groups;                                  // target -> attackers
    std::vector<uint32_t> gathering;
    std::vector<uint32_t> posting;
    for (auto& f : force_) {
        const AntView* a = find_ant(v.mine(), f.first);
        if (a == nullptr) continue;
        Member& mem = f.second;
        bool need = false;
        if (!mem.ordered) need = a->takes_orders();
        else if (mem.sent != kPending) need = a->idle() && now >= mem.sent + 6u && !v.has_pending_path(a->id);
        else need = now > mem.decided + 200u;
        if (!need) continue;
        if (!assault_) {
            if (rally_.x >= 0 && a->tile.chebyshev_dist(rally_) > 2) {
                mem.ordered = true;
                mem.decided = now;
                mem.sent = kPending;
                gathering.push_back(a->id);
            }
            continue;
        }
        const AntView* best = nullptr;
        int32_t best_key = 0;
        for (const AntView* e : targets) {
            const int32_t key = a->tile.chebyshev_dist(e->tile) * 4 - (e->holding ? 24 : 0) - (e->type == sim::AntType::Combat ? 8 : 0);
            if (best == nullptr || key < best_key) {
                best = e;
                best_key = key;
            }
        }
        mem.ordered = true;
        mem.decided = now;
        mem.sent = kPending;
        if (best != nullptr) groups[best->id].push_back(a->id);
        else posting.push_back(a->id);
    }
    for (const auto& g : groups) {
        const AntView* e = find_ant(v.others(), g.first);
        if (e == nullptr) continue;
        c.orders.attack(g.second, e->tile, Priority::Urgent);
        attacks_ordered_ += static_cast<uint32_t>(g.second.size());
    }
    if (!gathering.empty()) c.orders.move(gathering, rally_, Priority::Normal);
    if (!posting.empty()) {                                                            // nobody in sight: the free tiles round his gate, nearest first, the rest at the rally tile
        const sim::Grid& grid = v.grid();
        std::vector<sim::TileCoord> spots;
        for (const sim::TileCoord& t : SabotageTask::ring_of(hill)) {
            if (grid.in_bounds(t) && !grid.has_fire_at(t) && grid.get_cell(t).can_place_bomb() && MapInfo::walkable(grid, c.seat, t, v.walk_context()) && v.powerup_at(t) == nullptr && !occupied(v, t)) spots.push_back(t);
        }
        const std::array<sim::TileCoord, 8> ring = SabotageTask::ring_of(hill);
        for (const uint32_t id_posted : posting) {
            const AntView* a = find_ant(v.mine(), id_posted);
            if (a == nullptr || std::find(ring.begin(), ring.end(), a->tile) != ring.end()) continue;              // (an ant that stands on the ring stays)
            sim::TileCoord goal = rally_.x >= 0 ? rally_ : hill.queue;
            if (!spots.empty()) {
                size_t pick = 0;
                for (size_t i = 1; i < spots.size(); ++i) {
                    if (a->tile.chebyshev_dist(spots[i]) < a->tile.chebyshev_dist(spots[pick])) pick = i;
                }
                goal = spots[pick];
                spots.erase(spots.begin() + static_cast<std::ptrdiff_t>(pick));
            }
            if (a->tile.chebyshev_dist(goal) > 0) c.orders.move({id_posted}, goal, Priority::Normal);
        }
    }
}

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
        if (plan.sabotage_safe && giveup_until_[t] > now) continue;
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
        if (a.holding && a.state != sim::UnitState::EnteringBase && a.tile != g.entrance && (!params_.cantgo_aware || c.map.reaches_hill(now_, a.tile, a.type))) carriers.push_back(&a);          // (a thief with loot banks at the entrance too; an ant that no walk joins to the hill is not ours to place)
    }
    if (!ramp_standing) ramp_since_ = -1;
    else if (ramp_since_ < 0) ramp_since_ = static_cast<int64_t>(now);
    const bool ramp_held = ramp_standing && static_cast<int64_t>(now) < ramp_since_ + static_cast<int64_t>(params_.ramp_wait_ticks);        // (an ant that stays longer is not waited for: nothing moves it)
    if (!leaver_on_ramp) leaver_since_ = -1;
    else if (leaver_since_ < 0) leaver_since_ = static_cast<int64_t>(now);
    const bool leaver_hold = leaver_on_ramp && static_cast<int64_t>(now) < leaver_since_ + static_cast<int64_t>(params_.leaver_wait_ticks);        // (a click while it is there meets it head on in the one-wide way out; one that stays longer is not waited for)
    // the ramp unjam (LevelPlan::ramp_unjam): an own ant without food that stands on the ramp, idle or in its can't-go clip, shuts the way to the entrance for the carriers (a Combat Ant that came home
    // from a fight, a worker that was held up) and nothing moves it: a person would step it aside. Only an ant that nobody above the economy holds is sent, to a free tile at the side of the doorstep
    if (params_.unjam_ticks > 0 && ramp_standing && ramp_since_ >= 0 && static_cast<int64_t>(now) >= ramp_since_ + static_cast<int64_t>(params_.unjam_ticks) && now >= unjam_next_ && !carriers.empty()) {
        const AntView* mover = nullptr;
        for (const AntView& a : v.mine()) {
            if (a.tile != ramp || a.holding || a.carried_points > 0 || !a.takes_orders() || !(a.idle() || a.state == sim::UnitState::CantGo)) continue;
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
        if (a == nullptr || !carrying || it->second.tries >= 4 || now > it->second.hit + 800) {
            it = aid_.erase(it);                                                         // gone, delivered (or dropped its food), or helped as often as it makes sense
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
