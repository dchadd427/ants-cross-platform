#include "ants_ai/defence.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "ants_ai/group_fight.hpp"
#include "ants_ai/standard_bot.hpp"

namespace ants::ai {

namespace {

constexpr uint8_t kMinDefendHp = 4;            // an ant with fewer hit points than this is not taken into a fight (it heals at its hill: below 2 the engine sends it there itself)
constexpr int32_t kRaidNearTiles = 6;          // a Thief of the raid task this close to the raid tile of an enemy hill is about to raid
constexpr uint64_t kLingerFactor = 5;          // a fight that answers a blow is kept up while a blow fell within this many times the level's linger time

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

}  // namespace

// Whether `tile` is within kRaidNearTiles of the raid tile of a hill of a team that is neither the seat nor its ally (the hills that a Thief of the raid task can be about to raid)
bool near_raid_tile(const BotView& view, const MapInfo& map, sim::TileCoord tile) noexcept {
    for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
        const HillInfo& hill = map.hill(team);
        if (team != view.seat() && team != view.ally() && hill.present && tile.chebyshev_dist(hill.raid) <= kRaidNearTiles) return true;
    }
    return false;
}

uint32_t blow_damage(sim::AntType type) noexcept { return type == sim::AntType::Combat ? 2u : 1u; }

uint32_t blows_to_kill(uint32_t hp, uint32_t damage) noexcept { return damage == 0 ? hp : (hp + damage - 1) / damage; }

Strength enemy_strength(const BotView& view, sim::TileCoord centre, int32_t radius, uint32_t exclude, uint32_t cap) {
    std::vector<std::pair<uint32_t, uint32_t>> near;                              // (hit points, damage)
    for (const AntView& e : view.others()) {
        if (e.id == exclude || e.holding || e.tile.chebyshev_dist(centre) > radius) continue;
        if (view.ally() < sim::MAX_PLAYERS && e.team == view.ally()) continue;
        near.emplace_back(std::max<uint32_t>(e.hp, 1u), blow_damage(e.type));
    }
    std::stable_sort(near.begin(), near.end(), [](const auto& x, const auto& y) { return x.first * x.second > y.first * y.second; });
    Strength out;
    for (size_t i = 0; i < near.size() && i < cap; ++i) out.add(near[i].first, near[i].second);
    return out;
}

bool can_defend(const BotView& view, const AntView& ant, const Tactics& tactics, const AntLedger& ledger, const MapInfo& map, bool struck) {
    const bool in_hit_clip = struck && (ant.state == sim::UnitState::Flinch || ant.state == sim::UnitState::Knockback);
    if ((!ant.takes_orders() && !in_hit_clip) || ant.state == sim::UnitState::Stunned) return false;
    if (ant.holding || ant.carried_points > 0 || ant.hp < kMinDefendHp || view.powerup_at(ant.tile) != nullptr) return false;
    if (tactics.wall_demand && ant.id == tactics.wall_keeper) return false;
    const TaskId owner = ledger.owner(ant.id);
    if (ant.type == sim::AntType::Thief && (ant.state == sim::UnitState::Infiltrating || (owner == StandardBot::kRaids && near_raid_tile(view, map, ant.tile)))) return false;
    if (owner == StandardBot::kIslands || owner == StandardBot::kExpedition) return false;
    return owner == kNoTask || ledger.rank(owner) < ledger.rank(StandardBot::kFight);
}

size_t defence_wanted(const LevelPlan& plan, size_t enemies, bool guard_stance) noexcept {
    const size_t level = plan.defenders + (guard_stance ? 1u : 0u);
    if (!plan.defend_by_strength) return level;
    return std::min<size_t>(plan.defend_max, std::max<size_t>(level, enemies + plan.defend_extra));
}

bool keep_defence(const LevelPlan& plan, const Strength& own, const Strength& enemy, int32_t from_anchor, uint64_t since_blow) noexcept {
    return own.value() * 100u >= enemy.value() * plan.defend_keep_percent && from_anchor <= plan.leash_tiles && since_blow <= static_cast<uint64_t>(plan.fight_linger_ticks) * kLingerFactor;
}

DefenceFrame defence_frame(const BotView& view, const LevelPlan& plan, sim::TileCoord target, bool guard_stance) {
    DefenceFrame out;
    out.wanted = defence_wanted(plan, enemy_strength(view, target, plan.defend_radius, 0, std::numeric_limits<uint32_t>::max()).n, guard_stance);
    out.theirs = enemy_strength(view, target, plan.defend_radius, 0, static_cast<uint32_t>(out.wanted) + 2u);
    return out;
}

// The ants that join a fight that answers a blow (the plan's defence rules; FightTask::run_fight calls this for such a fight when one of them is on): the ants that were struck first (victim_answers),
// then the ants near the target, a Combat Ant first and then the nearest, the ants that a job of rank 3 or 4 holds only after all the others. With defend_by_strength nobody is taken when the force
// that would stand there (the defenders that are left and the new ones) is weaker than the enemy's near the target: the fight is called off (false). The ants are taken from their tasks only now
bool FightTask::take_defenders(TaskContext& c, Fight& f, const AntView& target, size_t wanted, const Strength& theirs) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const int32_t reach = plan.defend_by_strength ? recruit_reach(plan) : std::numeric_limits<int32_t>::max();
    // the ants that answer: any ant that can strike (defend_all_types), else the own default type and the Combat Ants, and no ant of a task of rank 3 or more
    const auto fit = [&](const AntView& a, bool struck) {
        if (plan.defend_all_types) return can_defend(v, a, tactics_, c.ledger, c.map, struck);
        AntView probe = a;
        if (struck && (a.state == sim::UnitState::Flinch || a.state == sim::UnitState::Knockback)) probe.state = sim::UnitState::Idle;
        const TaskId owner = c.ledger.owner(a.id);
        return can_fight(v, probe) && (owner == kNoTask || c.ledger.rank(owner) < 3);
    };
    const auto busy = [&](const AntView& a) {
        const TaskId owner = c.ledger.owner(a.id);
        return owner != kNoTask && c.ledger.rank(owner) >= 3;
    };
    std::vector<const AntView*> force;                                               // the defenders that are in the fight
    for (const auto& d : f.defenders) {
        const AntView* a = find_ant(v.mine(), d.first);
        if (a != nullptr) force.push_back(a);
    }
    std::vector<const AntView*> join;                                                // the ants that join now
    const auto room = [&]() { return force.size() + join.size() < wanted; };
    const auto listed = [&](const AntView& a) { return f.defenders.count(a.id) != 0 || std::find(join.begin(), join.end(), &a) != join.end(); };
    if (plan.victim_answers) {
        for (const uint32_t ant : f.victims) {
            const AntView* a = find_ant(v.mine(), ant);
            if (a != nullptr && room() && !listed(*a) && fit(*a, true)) join.push_back(a);
        }
    }
    f.victims.clear();
    if (room()) {
        std::vector<const AntView*> cands;
        for (const AntView& a : v.mine()) {
            if (listed(a) || !fit(a, false) || a.tile.chebyshev_dist(target.tile) > reach) continue;
            if ((plan.reach_fights && !walk_reaches(c.map, c.seat, a.tile, target.tile)) || (f.ally && a.tile.chebyshev_dist(target.tile) > plan.ally_help_radius)) continue;
            cands.push_back(&a);
        }
        std::stable_sort(cands.begin(), cands.end(), [&](const AntView* x, const AntView* y) {
            if (busy(*x) != busy(*y)) return busy(*y);
            const bool cx = x->type == sim::AntType::Combat;
            const bool cy = y->type == sim::AntType::Combat;
            if (cx != cy) return cx;
            return x->tile.chebyshev_dist(target.tile) < y->tile.chebyshev_dist(target.tile);
        });
        for (const AntView* a : cands) {
            if (room()) join.push_back(a);
        }
    }
    if (plan.defend_by_strength) {
        Strength own;
        for (const AntView* a : force) own.add(a->hp, blow_damage(a->type));
        for (const AntView* a : join) own.add(a->hp, blow_damage(a->type));
        if (!keep_defence(plan, own, theirs, target.tile.chebyshev_dist(f.anchor), v.tick() - std::max(f.last_alarm, f.last_hurt))) return false;
    }
    for (const AntView* a : join) {
        if (c.ledger.take(a->id, id())) f.defenders[a->id] = Defender{};
    }
    return true;
}

}  // namespace ants::ai
