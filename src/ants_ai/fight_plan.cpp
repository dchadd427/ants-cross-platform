#include "ants_ai/fight_plan.hpp"

#include <algorithm>
#include <cstddef>

#include "ants_ai/defence.hpp"
#include "ants_ai/standard_bot.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;

namespace {

// Whether the tile belongs to the 4 x 4 mound of a hill (an attack click on an ant that stands there is a plain move for the HUD, and the controller refuses it)
bool on_mound(const sim::Grid& grid, sim::TileCoord t) {
    for (const assets::AnthillSpawn& hill : grid.anthills()) {
        if (t.x >= static_cast<int32_t>(hill.x) && t.x <= static_cast<int32_t>(hill.x) + 3 && t.y >= static_cast<int32_t>(hill.y) && t.y <= static_cast<int32_t>(hill.y) + 3) return true;
    }
    return false;
}

struct Cluster {
    std::vector<const AntView*> seeds;           // the own ants that an enemy ant is near
    sim::TileCoord center{};
};

}  // namespace

void FightPlanTask::finish(AntLedger& ledger) {
    held_.clear();
    Task::finish(ledger);
}

void FightPlanTask::step(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!plan.fight_plan || !v.has_grid() || v.game_mode() != sim::GameMode::HighestScore) {
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
    if (now < last_look_ + params_.look_every || v.ticks_left() < 120) return;
    last_look_ = now;
    const sim::Grid& grid = v.grid();

    // the enemy ants that a blow can reach: not an ally's, not on a mound, not one that stands on a power-up (immune)
    std::vector<const AntView*> foes;
    for (const AntView& e : v.others()) {
        if (e.hp == 0 || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) || !grid.in_bounds(e.tile) || on_mound(grid, e.tile)) continue;
        if (v.powerup_at(e.tile) != nullptr && v.standing(e)) continue;
        foes.push_back(&e);
    }
    if (foes.empty()) return;

    // the contacts: own ants (a carrier too, with plan.fp_guard) that an enemy ant is within `contact` tiles of
    std::vector<const AntView*> seeds;
    for (const AntView& m : v.mine()) {
        if (m.type == sim::AntType::Swimmer || m.hp == 0) continue;
        if ((m.holding || m.carried_points > 0) && !plan.fp_guard) continue;
        for (const AntView* e : foes) {
            if (m.tile.chebyshev_dist(e->tile) <= params_.contact) {
                seeds.push_back(&m);
                break;
            }
        }
    }
    if (seeds.empty()) return;

    // the engagements: contacts within `cluster` tiles of each other (a chain of them is one)
    std::vector<Cluster> clusters;
    for (const AntView* s : seeds) {
        std::vector<size_t> joined;                                                             // the engagements that the ant is within `cluster` tiles of
        for (size_t k = 0; k < clusters.size(); ++k) {
            for (const AntView* o : clusters[k].seeds) {
                if (o->tile.chebyshev_dist(s->tile) <= params_.cluster) {
                    joined.push_back(k);
                    break;
                }
            }
        }
        if (joined.empty()) {
            clusters.emplace_back();
            clusters.back().seeds.push_back(s);
            continue;
        }
        Cluster& first = clusters[joined.front()];
        first.seeds.push_back(s);
        for (size_t n = joined.size(); n-- > 1;) {                                              // the ant joins several engagements: they are one (from the back, so that the indexes hold)
            first.seeds.insert(first.seeds.end(), clusters[joined[n]].seeds.begin(), clusters[joined[n]].seeds.end());
            clusters.erase(clusters.begin() + static_cast<std::ptrdiff_t>(joined[n]));
        }
    }

    const HillInfo& hill = c.map.hill(c.seat);
    for (Cluster& cl : clusters) {
        int64_t sx = 0, sy = 0;
        for (const AntView* s : cl.seeds) {
            sx += s->tile.x;
            sy += s->tile.y;
        }
        const int64_t n = static_cast<int64_t>(cl.seeds.size());
        cl.center = sim::TileCoord{static_cast<int32_t>(sx / n), static_cast<int32_t>(sy / n)};

        // the enemy side: the enemy ants near the center that fight (an ant that carries food does not)
        Strength theirs;
        std::vector<const AntView*> targets;
        for (const AntView* e : foes) {
            if (e->tile.chebyshev_dist(cl.center) > params_.engage + 2) continue;
            targets.push_back(e);
            if (!e->holding && e->tile.chebyshev_dist(cl.center) <= params_.engage) theirs.add(std::max<uint32_t>(e->hp, 1u), blow_damage(e->type));
        }
        if (theirs.n == 0 || targets.empty()) continue;
        ++engagements_;

        // the own side: the ants that can strike within `fp_reach` tiles of the center, the nearest first, at most `fp_max_force`
        std::vector<const AntView*> fit;
        for (const AntView& m : v.mine()) {
            if (m.type == sim::AntType::Swimmer || m.hp < params_.fit_hp || m.tile.chebyshev_dist(cl.center) > plan.fp_reach) continue;
            const bool mine_already = held_.count(m.id) != 0;
            if (!mine_already && !can_defend(v, m, tactics_, c.ledger, c.map)) continue;
            if (!mine_already) {
                const TaskId owner = c.ledger.owner(m.id);
                if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
                if (plan.fp_spare_only && owner != kNoTask) continue;                                // (an ant that a task holds, the economy's included, is not sent: the fight costs no food)
            }
            fit.push_back(&m);
        }
        std::stable_sort(fit.begin(), fit.end(), [&](const AntView* x, const AntView* y) {
            const int32_t dx = x->tile.chebyshev_dist(cl.center);
            const int32_t dy = y->tile.chebyshev_dist(cl.center);
            return dx != dy ? dx < dy : x->id < y->id;
        });
        if (fit.size() > plan.fp_max_force) fit.resize(plan.fp_max_force);
        Strength ours;
        for (const AntView* m : fit) ours.add(m->hp, blow_damage(m->type));

        const bool edge = ours.n > 0 && ours.value() * 100u >= theirs.value() * plan.fp_edge_percent;
        if (edge) {
            // the target: the enemy ant with the fewest hit points (it dies soonest and stops striking), the nearest to the center among equals; with fp_focus off the nearest
            const AntView* target = nullptr;
            for (const AntView* e : targets) {
                if (target == nullptr) {
                    target = e;
                    continue;
                }
                const bool better = plan.fp_focus ? (e->hp != target->hp ? e->hp < target->hp : e->tile.chebyshev_dist(cl.center) < target->tile.chebyshev_dist(cl.center))
                                                  : e->tile.chebyshev_dist(cl.center) < target->tile.chebyshev_dist(cl.center);
                if (better) target = e;
            }
            if (plan.fp_bridge_hold && grid.get_cell(target->tile).has_completed_bridge()) {         // a one-tile way: the ants hold the shore and let it come off
                ++skipped_;
                continue;
            }
            std::vector<uint32_t> strikers;
            for (const AntView* m : fit) {
                const bool ours_already = held_.count(m->id) != 0;
                if (ours_already && !m->idle()) continue;                                         // (on its way, or in its blow: told again when it is idle)
                if (!m->takes_orders() && m->state != sim::UnitState::Flinch) continue;
                if (!ours_already && !c.ledger.take(m->id, id())) continue;
                held_[m->id] = Hold{Mode::Strike, now + params_.linger_ticks};
                strikers.push_back(m->id);
            }
            for (const AntView* m : fit) {                                                         // (the ants that are on their way keep their hold while the fight lasts)
                auto it = held_.find(m->id);
                if (it != held_.end()) it->second.until = std::max<uint64_t>(it->second.until, now + params_.linger_ticks);
            }
            if (!strikers.empty()) {
                c.orders.attack(strikers, target->tile, Priority::Urgent, true);
                attacks_ += 1;
            }
            ++fights_;
        } else {
            ++skipped_;
            if (!plan.fp_pull || !v.hill_enterable() || !hill.present) continue;
            std::vector<uint32_t> pull;
            for (const AntView* s : cl.seeds) {                                                    // the hurt ants of the place walk home: the hill's door heals them
                if (s->hp > plan.fp_pull_hp || held_.count(s->id) != 0 || !s->takes_orders()) continue;
                const TaskId owner = c.ledger.owner(s->id);
                if (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id())) continue;
                if (owner == StandardBot::kIslands || owner == StandardBot::kExpedition || !c.ledger.take(s->id, id())) continue;
                held_[s->id] = Hold{Mode::Pull, now + params_.pull_ticks};
                pull.push_back(s->id);
                ++pulls_;
            }
            if (!pull.empty()) c.orders.move(pull, hill.entrance, Priority::Urgent);
        }
    }
}

}  // namespace ants::ai
