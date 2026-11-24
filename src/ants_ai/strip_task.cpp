#include "ants_ai/strip_task.hpp"

#include <algorithm>

#include "ants_ai/defence.hpp"
#include "ants_ai/standard_bot.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;

namespace {

bool on_mound(const sim::Grid& grid, sim::TileCoord t) {
    for (const assets::AnthillSpawn& hill : grid.anthills()) {
        if (t.x >= static_cast<int32_t>(hill.x) && t.x <= static_cast<int32_t>(hill.x) + 3 && t.y >= static_cast<int32_t>(hill.y) && t.y <= static_cast<int32_t>(hill.y) + 3) return true;
    }
    return false;
}

}  // namespace

void StripTask::finish(AntLedger& ledger) {
    held_.clear();
    Task::finish(ledger);
}

void StripTask::step(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!plan.strip_fight || !v.has_grid() || v.game_mode() != sim::GameMode::HighestScore) {
        for (const auto& h : held_) c.ledger.release(h.first, id());
        held_.clear();
        return;
    }
    for (auto it = held_.begin(); it != held_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool owned = a != nullptr && c.ledger.owner(it->first) == id();
        if (!owned || now >= it->second.until) {
            if (owned) c.ledger.release(it->first, id());
            it = held_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = tactics_.strip_shut.begin(); it != tactics_.strip_shut.end();) it = it->second <= now ? tactics_.strip_shut.erase(it) : std::next(it);
    if (now < last_look_ + params_.look_every || v.ticks_left() < 300 || v.flowers().empty()) return;
    last_look_ = now;
    const sim::Grid& grid = v.grid();
    const HillInfo& hill = c.map.hill(c.seat);

    for (const FlowerView& flower : v.flowers()) {
        const sim::TileCoord drop = flower.drop;
        // who waits at the strip: the own ants that stand idle within strip_radius of the drop tile (no Swimmer: it swims past), the enemy ants of the same kind
        std::vector<const AntView*> posts;
        for (const AntView& m : v.mine()) {
            if (m.type == sim::AntType::Swimmer || m.hp == 0 || !(m.idle() || held_.count(m.id) != 0) || m.holding || m.tile.chebyshev_dist(drop) > plan.strip_radius) continue;      // (an ant of the fight is still a post while it strikes)
            posts.push_back(&m);
        }
        std::vector<const AntView*> rivals;
        for (const AntView& e : v.others()) {
            if (e.hp == 0 || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) || !grid.in_bounds(e.tile) || on_mound(grid, e.tile)) continue;
            const bool waits = e.state == sim::UnitState::Idle || e.state == sim::UnitState::GuardIdle || e.state == sim::UnitState::Flinch || e.state == sim::UnitState::Knockback || e.state == sim::UnitState::Stunned;
            if (e.tile.chebyshev_dist(drop) > plan.strip_radius || e.holding || !waits) continue;      // (an ant that waits, or one that has just been struck; one that walks is passing or leaving)
            if (v.powerup_at(e.tile) != nullptr && v.standing(e)) continue;                      // (an ant that stands on a power-up is immune)
            rivals.push_back(&e);
        }
        if (posts.empty() || rivals.empty()) continue;
        ++contests_;

        // the enemy side: every enemy ant that fights within enemy_reach of the drop tile (the ones that are walking up count)
        Strength theirs;
        for (const AntView& e : v.others()) {
            if (e.hp == 0 || e.holding || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) || e.tile.chebyshev_dist(drop) > params_.enemy_reach) continue;
            theirs.add(std::max<uint32_t>(e.hp, 1u), blow_damage(e.type));
        }
        // the own side: the ants that wait there, and the ants that can strike within strip_help_reach of the drop tile (the posts at the other strips, an ant of the economy that is near)
        std::vector<const AntView*> force;
        std::vector<const AntView*> helpers;
        for (const AntView& m : v.mine()) {
            if (m.type == sim::AntType::Swimmer || m.hp < params_.fit_hp || m.tile.chebyshev_dist(drop) > plan.strip_help_reach) continue;
            const bool mine_already = held_.count(m.id) != 0;
            if (!mine_already) {
                if (!can_defend(v, m, tactics_, c.ledger, c.map)) continue;
                const TaskId owner = c.ledger.owner(m.id);
                if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
            }
            force.push_back(&m);
            if (m.tile.chebyshev_dist(drop) > plan.strip_radius) helpers.push_back(&m);
        }
        std::stable_sort(force.begin(), force.end(), [&](const AntView* x, const AntView* y) {
            const int32_t dx = x->tile.chebyshev_dist(drop);
            const int32_t dy = y->tile.chebyshev_dist(drop);
            return dx != dy ? dx < dy : x->id < y->id;
        });
        if (force.size() > plan.strip_max_force) force.resize(plan.strip_max_force);
        Strength ours;
        for (const AntView* m : force) ours.add(m->hp, blow_damage(m->type));

        const bool edge = ours.n > 0 && ours.value() * 100u >= theirs.value() * plan.strip_edge_percent;
        const bool losing = !edge && ours.value() * 100u < theirs.value() * plan.strip_leave_percent;
        if (edge) {
            const AntView* target = nullptr;                                                      // the rival with the fewest hit points (the nearest to the drop tile among equals)
            for (const AntView* e : rivals) {
                if (target == nullptr || e->hp < target->hp || (e->hp == target->hp && e->tile.chebyshev_dist(drop) < target->tile.chebyshev_dist(drop))) target = e;
            }
            std::vector<uint32_t> strikers;
            for (const AntView* m : force) {
                const bool ours_already = held_.count(m->id) != 0;
                if (ours_already && !m->idle()) continue;
                if (!m->takes_orders() && m->state != sim::UnitState::Flinch) continue;
                if (!ours_already && !c.ledger.take(m->id, id())) continue;
                if (!ours_already && std::find(helpers.begin(), helpers.end(), m) != helpers.end()) ++helped_;
                held_[m->id] = Hold{Mode::Strike, now + params_.linger_ticks};
                strikers.push_back(m->id);
            }
            for (const AntView* m : force) {
                auto it = held_.find(m->id);
                if (it != held_.end() && it->second.mode == Mode::Strike) it->second.until = std::max<uint64_t>(it->second.until, now + params_.linger_ticks);
            }
            if (!strikers.empty()) {
                c.orders.attack(strikers, target->tile, Priority::Urgent, true);
                ++attacks_;
            }
            ++strikes_;
        } else if (losing && hill.present) {
            std::vector<uint32_t> go;
            for (const AntView* m : posts) {                                                       // the posts leave: they walk home, and the swarm leaves the strip alone for a while
                if (held_.count(m->id) != 0 || !m->takes_orders()) continue;
                const TaskId owner = c.ledger.owner(m->id);
                if ((owner != kNoTask && c.ledger.rank(owner) >= c.ledger.rank(id())) || !c.ledger.take(m->id, id())) continue;
                held_[m->id] = Hold{Mode::Leave, now + params_.leave_ticks};
                go.push_back(m->id);
                ++leaves_;
            }
            if (!go.empty()) {
                c.orders.move(go, hill.tile42.x >= 0 ? hill.tile42 : hill.entrance, Priority::Urgent);
                tactics_.strip_shut[drop.y * 4096 + drop.x] = now + plan.strip_close_ticks;
            }
        }
    }
}

}  // namespace ants::ai
