#include "ants_ai/idle_jobs_task.hpp"

#include <algorithm>

#include "ants_ai/opportunity_task.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;

void IdleJobsTask::release_all(TaskContext& c) {
    for (const auto& h : healing_) c.ledger.release(h.first, id());
    for (const auto& h : holders_) c.ledger.release(h.first, id());
    healing_.clear();
    holders_.clear();
}

void IdleJobsTask::finish(AntLedger& ledger) {
    healing_.clear();
    holders_.clear();
    joined_made_ = false;
    Task::finish(ledger);
}

void IdleJobsTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    const HillInfo& hill = c.map.hill(c.seat);
    if (!plan.idle_jobs || !v.has_grid() || !hill.present || v.game_mode() != sim::GameMode::HighestScore || !v.hill_enterable()) {
        release_all(c);
        idle_.clear();
        return;
    }
    const sim::Grid& grid = v.grid();
    // who stands idle with empty hands and no task, and since when
    std::map<uint32_t, Idle> next;
    for (const AntView& a : v.mine()) {
        const TaskId owner = c.ledger.owner(a.id);
        if (!a.idle() || a.holding || a.carried_points > 0 || (owner != kNoTask && owner != id())) continue;
        const auto was = idle_.find(a.id);
        next[a.id] = was != idle_.end() && was->second.tile == a.tile ? was->second : Idle{a.tile, now};
    }
    idle_ = std::move(next);
    // the jobs in hand: a walk home ends when the ant is idle again (at the hill, or not going anywhere), a post is kept while it is wanted
    for (auto it = healing_.begin(); it != healing_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool owned = a != nullptr && c.ledger.owner(it->first) == id();
        if (!owned || a->hp >= sim::AntUnit::MAX_HP || (a->idle() && now >= it->second + 60u) || now >= it->second + 1200u) {
            if (owned) c.ledger.release(it->first, id());
            it = healing_.erase(it);
        } else {
            ++it;
        }
    }
    uint32_t swimmers = 0;
    for (const AntView& a : v.mine()) swimmers += a.type == sim::AntType::Swimmer ? 1u : 0u;
    const auto joined_now = [&]() -> const MapInfo::NowField& {
        if (!joined_made_ || now >= joined_tick_ + kJoinedTicks) {
            joined_ = c.map.field_now(grid, c.seat, v.walk_context(), v.walkable_mask(c.seat));
            joined_tick_ = now;
            joined_made_ = true;
        }
        return joined_;
    };
    // the walls of a Fire Ant that can walk home are the better answer (they last 3,600 ticks and cost no ant), and the keeper lights none on a tile that an ant holds: with one, nobody holds
    // (the food is gone, or an enemy Thief was seen within the latch of the walls and no pile can be reached: while there is food to carry the ants are the economy's, which cannot take a holder back)
    const uint64_t seen = tactics_.memory.thief_last_seen();
    const bool want_hold = tactics_.gone || (seen != 0 && now <= seen + plan.wall_latch_ticks && tactics_.no_work);
    bool keeper = false;
    for (const AntView& a : v.mine()) {
        if (!want_hold) break;                                                                      // (nobody holds: the keeper is of no matter, and the flood is not made)
        if (a.type != sim::AntType::Fire || a.hp < 4) continue;
        keeper = keeper || c.map.reaches_hill(joined_now(), a.tile, a.type);
    }
    const bool hold_on = want_hold && !keeper;
    std::vector<sim::TileCoord> open;                                                               // the tiles in front of the thief hole that nothing holds: wall, bomb, rock, power-up or an ant of another team
    for (const sim::TileCoord& e : east_tiles(hill)) {
        const EastTile k = classify_tile(grid, e);
        if (k != EastTile::Open && k != EastTile::Bare) continue;
        if (v.powerup_at(e) != nullptr || grid.has_fire_at(e) || grid.has_bomb_at(e)) continue;
        bool foreign = false;
        for (const AntView& o : v.others()) foreign = foreign || o.tile == e;
        if (!foreign) open.push_back(e);
    }
    for (auto it = holders_.begin(); it != holders_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        const bool owned = a != nullptr && c.ledger.owner(it->first) == id();
        const bool post_ok = std::find(open.begin(), open.end(), it->second.tile) != open.end() || (a != nullptr && a->tile == it->second.tile);
        if (!owned || !hold_on || !post_ok || a->hp < 5 || a->holding || a->carried_points > 0 || v.ticks_left() < 200) {
            if (owned) c.ledger.release(it->first, id());
            it = holders_.erase(it);
            continue;
        }
        if (a->tile != it->second.tile && a->takes_orders() && (a->idle() || it->second.ordered == 0) && now >= it->second.ordered + 80u && !occupied(v, it->second.tile)) {
            c.orders.move({a->id}, it->second.tile, Priority::Normal);
            it->second.ordered = now;
        }
        ++it;
    }
    for (const auto& e : idle_) {
        const AntView* a = find_ant(v.mine(), e.first);
        if (a == nullptr || now < e.second.since + plan.idle_after || !a->takes_orders() || v.powerup_at(a->tile) != nullptr) continue;
        if (a->type == sim::AntType::Fire || a->type == sim::AntType::Thief || c.ledger.owner(a->id) == id()) continue;
        // a hurt ant walks home when a walk leads there
        if (a->hp <= plan.idle_heal_hp && a->tile.chebyshev_dist(hill.entrance) > 3) {
            if (c.map.reaches_hill(joined_now(), a->tile, a->type) && c.ledger.claim(a->id, id())) {
                c.orders.move({a->id}, hill.entrance, Priority::Normal);
                healing_[a->id] = now;
                ++heals_;
                continue;
            }
        }
    }
    if (!hold_on || v.ticks_left() < 400) return;
    // the posts in front of the hole: one ant for each open tile that has none
    std::vector<sim::TileCoord> free_posts;
    for (const sim::TileCoord& t : open) {
        bool held = false;
        for (const auto& h : holders_) held = held || h.second.tile == t;
        if (!held) free_posts.push_back(t);
    }
    if (free_posts.empty() || holders_.size() >= plan.idle_posts) return;
    struct Cand {
        const AntView* ant;
        int32_t d;
    };
    std::vector<Cand> cands;
    for (const auto& e : idle_) {
        const AntView* a = find_ant(v.mine(), e.first);
        if (a == nullptr || holders_.count(a->id) != 0 || now < e.second.since + std::min<uint32_t>(plan.idle_after, 200u) || !a->takes_orders() || a->hp < 6) continue;
        if (a->type == sim::AntType::Fire || a->type == sim::AntType::Thief || (a->type == sim::AntType::Swimmer && swimmers <= plan.endraid_keep_swimmers)) continue;
        if (v.powerup_at(a->tile) != nullptr || c.ledger.owner(a->id) != kNoTask) continue;
        const int32_t d = a->tile.chebyshev_dist(hill.raid);
        if (d > 14 || !MapInfo::walkable(grid, c.seat, free_posts.front(), v.walk_context()) || !walk_reaches(c.map, c.seat, a->tile, free_posts.front())) continue;
        cands.push_back(Cand{a, d});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d != y.d ? x.d < y.d : x.ant->id < y.ant->id; });
    size_t k = 0;
    for (const sim::TileCoord& post : free_posts) {
        if (k >= cands.size() || holders_.size() >= plan.idle_posts) break;
        const AntView* a = cands[k++].ant;
        if (!c.ledger.claim(a->id, id())) continue;
        Post p;
        p.tile = post;
        if (!occupied(v, post)) {
            c.orders.move({a->id}, post, Priority::Normal);
            p.ordered = now;
        }
        holders_[a->id] = p;
        ++holds_;
    }
}

}  // namespace ants::ai
