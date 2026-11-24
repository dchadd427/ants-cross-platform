#include "ants_ai/contact_task.hpp"

#include <algorithm>
#include <set>

#include "ants_ai/defence.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;

namespace {

bool is_ally(const BotView& v, uint8_t team) noexcept { return v.ally() < sim::MAX_PLAYERS && team == v.ally(); }

// An ant of the seat that may be sent to strike: it can strike at all, is neither the keeper of the walls (a Fire Ant), a Thief (it holds the loot or raids) nor a Swimmer that is no worker (the ferry's ant:
// from the water it hits nothing, and the engine refuses the blow again and again), and is free or holds a chore of rank 2 or less (the economy, the guard, the ferry)
bool striker_ok(const BotView& v, const AntLedger& ledger, TaskId mine, const AntView& a, uint8_t min_hp) noexcept {
    if (!can_strike(v, a) || a.hp < min_hp || !a.idle() || a.type == sim::AntType::Fire || a.type == sim::AntType::Thief) return false;
    if (a.type == sim::AntType::Swimmer && a.type != v.default_ant_type()) return false;
    const TaskId owner = ledger.owner(a.id);
    return owner == kNoTask || owner == mine || ledger.rank(owner) <= 2;
}

}  // namespace

void ContactTask::release_all(TaskContext& c) {
    for (const auto& j : jobs_) c.ledger.release(j.first, id());
    jobs_.clear();
}

void ContactTask::finish(AntLedger& ledger) {
    jobs_.clear();
    Task::finish(ledger);
}

void ContactTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    if (!plan.contact_strike || !v.has_grid() || tactics_.guard_stance) {
        release_all(c);
        still_.clear();
        return;
    }
    // where the ants of other teams stand and since when
    std::map<uint32_t, Still> next;
    for (const AntView& e : v.others()) {
        const auto was = still_.find(e.id);
        next[e.id] = was != still_.end() && was->second.tile == e.tile && e.state != sim::UnitState::Walking ? was->second : Still{e.tile, now};
    }
    still_ = std::move(next);
    // the jobs in hand: one blow at a time, while the enemy stands beside the striker
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const AntView* e = find_ant(v.others(), it->second.target);
        const bool owned = a != nullptr && c.ledger.owner(it->first) == id();
        const bool over = !owned || e == nullptr || e->hp == 0 || !attackable(v, *e) || a->hp < params_.stop_hp || a->holding || a->carried_points > 0 ||
                          a->tile.chebyshev_dist(e->tile) > 2 || now >= it->second.started + params_.job_ticks || it->second.blows >= params_.max_blows;
        if (over) {
            if (owned) c.ledger.release(it->first, id());
            it = jobs_.erase(it);
            continue;
        }
        Job& j = it->second;
        if (a->idle() && !v.has_pending_path(a->id) && now >= j.last_blow + params_.blow_gap) {
            c.orders.attack({a->id}, e->tile, Priority::Urgent, j.blows > 0);
            j.last_blow = now;
            ++j.blows;
            ++blows_;
        }
        ++it;
    }
    if (jobs_.size() >= params_.max_jobs) return;
    // a new job: a free ant that stands idle beside an enemy that does not move
    std::set<uint32_t> taken;
    for (const auto& j : jobs_) taken.insert(j.second.target);
    for (const AntView& a : v.mine()) {
        if (jobs_.size() >= params_.max_jobs) break;
        if (jobs_.count(a.id) != 0 || !striker_ok(v, c.ledger, id(), a, params_.min_hp)) continue;
        const AntView* best = nullptr;
        for (const AntView& e : v.others()) {
            if (taken.count(e.id) != 0 || is_ally(v, e.team) || e.tile.chebyshev_dist(a.tile) > 1 || !attackable(v, e)) continue;
            if (e.type == sim::AntType::Combat || e.hp > a.hp) continue;                               // (a Combat Ant fights back with its reflex; the blow is for the one that is not the weaker)
            const auto s = still_.find(e.id);
            const bool thief = e.type == sim::AntType::Thief;
            if (!thief && (s == still_.end() || now < s->second.since + params_.stand_ticks)) continue;
            const HillInfo& home = c.map.hill(e.team);
            if (!thief && home.present && e.tile.chebyshev_dist(home.origin) <= params_.home_tiles) continue;
            if (best == nullptr || (thief && best->type != sim::AntType::Thief) || (thief == (best->type == sim::AntType::Thief) && e.hp < best->hp)) best = &e;
        }
        if (best == nullptr) continue;
        // a person looks first: no enemy Combat Ant near, and not two that were drawn fighting lately
        uint32_t fighters = 0;
        bool combat = false;
        for (const AntView& o : v.others()) {
            if (is_ally(v, o.team) || o.tile.chebyshev_dist(a.tile) > params_.crowd_tiles) continue;
            if (o.type == sim::AntType::Combat) combat = true;
            else if (o.id != best->id && (tactics_.memory.fought_lately(o.id, 600) || o.state == sim::UnitState::Attacking)) ++fighters;
        }
        if (combat || fighters >= 2) continue;
        if (!c.ledger.take(a.id, id())) continue;
        Job j;
        j.target = best->id;
        j.started = now;
        jobs_[a.id] = j;
        taken.insert(best->id);
        ++started_;
        c.orders.attack({a.id}, best->tile, Priority::Urgent);
        jobs_[a.id].last_blow = now;
        jobs_[a.id].blows = 1;
        ++blows_;
    }
}

}  // namespace ants::ai
