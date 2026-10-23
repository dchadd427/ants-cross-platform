#include "ants_ai/opportunity_task.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <vector>

#include "ants_ai/island_info.hpp"

namespace ants::ai {

namespace {

constexpr uint64_t kBlowTicks = 8;                // an ant that is idle this long after the attack order LEFT has struck (or cannot)
constexpr uint64_t kApproachTicks = 60;           // an ant that stands idle this long after its move order LEFT, not on its tile, did not get there
constexpr uint64_t kShunTicks = 300;              // an enemy that was struck at (or that moved away from the job) is left alone this long
constexpr uint64_t kBadStandTicks = 300;
constexpr int32_t kCrowd = 3;                     // no job where this many other enemy ants stand within two tiles of the tile to strike from

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

int64_t key_of(sim::TileCoord t) noexcept { return static_cast<int64_t>(t.y) * 4096 + t.x; }

// Whether an ant other than `except` stands on the tile, or one dies there (no click may name a tile that holds an ant)
bool held_by_other(const BotView& v, sim::TileCoord tile, uint32_t except) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == tile && a.id != except) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == tile) return true;
    }
    return v.dying_at(tile);
}

bool is_ally(const BotView& v, uint8_t team) noexcept { return v.ally() < sim::MAX_PLAYERS && team == v.ally(); }

// What the kill is worth: the kind first (a drowned ant is dead for good whatever its hit points), then who dies
int32_t worth(KillKind kind, const AntView& e) noexcept {
    int32_t w = kind == KillKind::Water ? 1000 : (kind == KillKind::Bomb || kind == KillKind::Fire) ? 600 : 300;
    if (e.type == sim::AntType::Combat) w += 200;
    else if (e.type != sim::AntType::Worker) w += 120;
    if (e.holding) w += 150;
    return w + 15 * static_cast<int32_t>(e.hp);
}

// An ant that may be sent: it can strike at all (can_strike), is of the level's own kind or a Combat Ant, and is free or holds a chore of a lower rank than the job's: the economy, the guard, and
// (a plain ant only, a Fire Ant and a Bomber have their work) the walls, the mines and the counters of bombs. A pick-up, a raid, the expedition, a squad and a fight keep their ants.
bool may_strike(const BotView& v, const AntLedger& ledger, TaskId mine, const AntView& a) noexcept {
    if (!can_strike(v, a)) return false;
    if (a.type != v.default_ant_type() && a.type != sim::AntType::Combat) return false;
    const TaskId owner = ledger.owner(a.id);
    if (owner == kNoTask || owner == mine) return true;
    const uint8_t rank = ledger.rank(owner);
    if (rank <= 2) return true;
    return rank == 4 && a.type == v.default_ant_type();
}

}  // namespace

bool walk_reaches(const MapInfo& map, uint8_t seat, sim::TileCoord from, sim::TileCoord to) noexcept {
    const int32_t mine = map.ant_component(seat, from);
    if (mine < 0) return false;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (map.component(seat, sim::TileCoord{to.x + dx, to.y + dy}) == mine) return true;
        }
    }
    return false;
}

bool OpportunityTask::shunned(uint32_t enemy, uint64_t tick) const noexcept {
    const auto it = shunned_.find(enemy);
    return it != shunned_.end() && it->second > tick;
}

void OpportunityTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.ants.size() != 1 || (command.type != sim::CommandType::GroupMove && command.type != sim::CommandType::GroupAttack)) return;
    for (Job& j : jobs_) {
        if (j.striker != command.ants[0]) continue;
        if (command.type == sim::CommandType::GroupMove && j.move_ordered && j.move_sent == kPending && command.tile_x == j.stand.x && command.tile_y == j.stand.y) {
            if (fate == Bot::Fate::Sent) j.move_sent = tick;
            else j.move_ordered = false;                                    // it never left: ordered again at the next look
        } else if (command.type == sim::CommandType::GroupAttack && j.attack_ordered && j.attack_sent == kPending) {
            if (fate == Bot::Fate::Sent) j.attack_sent = tick;
            else j.attack_ordered = false;
        }
    }
}

void OpportunityTask::end_job(TaskContext& c, const Job& job) {
    c.ledger.release(job.striker, id());
}

void OpportunityTask::step(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    for (auto it = shunned_.begin(); it != shunned_.end();) it = it->second <= now ? shunned_.erase(it) : std::next(it);
    for (auto it = bad_stands_.begin(); it != bad_stands_.end();) it = it->second <= now ? bad_stands_.erase(it) : std::next(it);
    if (!plan.opportunity || !v.has_grid() || tactics_.guard_stance) {
        if (!jobs_.empty()) {
            for (const Job& j : jobs_) end_job(c, j);
            jobs_.clear();
        }
        return;
    }
    for (auto it = stood_.begin(); it != stood_.end();) it = find_ant(v.others(), it->first) == nullptr ? stood_.erase(it) : std::next(it);
    for (const AntView& e : v.others()) {
        Stand& st = stood_[e.id];
        if (st.since == 0 || st.tile != e.tile || e.state == sim::UnitState::Walking) {
            st.tile = e.tile;
            st.since = now == 0 ? 1 : now;
        }
    }
    run_jobs(c);
    if (jobs_.size() < plan.opportunity_jobs) {
        const size_t before = jobs_.size();
        seek(c);
        if (jobs_.size() > before) run_jobs(c);                                 // (the first order of a new job leaves at this look)
    }
}

// The jobs in hand: what became of them, and the next order of each
void OpportunityTask::run_jobs(TaskContext& c) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    for (size_t i = 0; i < jobs_.size();) {
        Job& j = jobs_[i];
        const AntView* a = find_ant(v.mine(), j.striker);
        const AntView* e = find_ant(v.others(), j.target);
        bool done = false;
        if (a == nullptr || c.ledger.owner(j.striker) != id() || a->hp < 2 || a->holding || a->carried_points > 0) {
            done = true;                                                        // the ant is gone, badly hurt, or taken by a task of a higher rank
        } else if (e == nullptr || e->state == sim::UnitState::Drowning || e->state == sim::UnitState::Dead) {
            if (j.blows > 0) ++kills_;
            done = true;
        } else if (j.attack_sent != kPending && (e->state == sim::UnitState::Flinch || e->state == sim::UnitState::Knockback || e->state == sim::UnitState::Burn)) {
            if (now > j.attack_sent + 80u) done = true;                         // it was struck: it flies and plays its clip; the next look finds it dead or standing
        } else if (!attackable(v, *e)) {
            done = true;                                                        // it stepped on a power-up, went into a hill, or is in a clip that no order reaches
            shunned_[j.target] = now + kShunTicks;
            ++gave_up_;
        } else if (now > j.decided + plan.opportunity_ticks) {
            done = true;
            shunned_[j.target] = now + kShunTicks;
            ++gave_up_;
        } else {
            const bool same = e->tile == j.target_tile;
            const KillShot shot = kill_shot(grid, e->tile, e->hp, e->type, j.stand, a->type);
            if (j.blows == 0 && (!same || !shot.kills())) {                     // it moved, or the ground changed: begun again from the next look
                done = true;
                shunned_[j.target] = now + 40u;
                ++gave_up_;
            } else if (j.blows > 0 && (!same || !shot.kills() || j.blows >= 2)) {
                done = true;                                                    // struck and alive: not again and again
                shunned_[j.target] = now + kShunTicks;
                ++gave_up_;
            } else if (a->tile == j.stand && a->idle() && !v.has_pending_path(a->id)) {
                if (!j.attack_ordered) {                                        // on the tile: the blow
                    c.orders.attack({a->id}, e->tile, Priority::Urgent);
                    j.attack_ordered = true;
                    j.attack_sent = kPending;
                    ++j.blows;
                    ++blows_;
                } else if (j.attack_sent != kPending && now >= j.attack_sent + kBlowTicks) {
                    j.attack_ordered = false;                                   // idle again after its blow and the target stands: strike once more (the job ends above at the second blow)
                }
            } else if (!j.move_ordered && a->tile != j.stand) {
                c.orders.move({a->id}, j.stand, Priority::Urgent);
                j.move_ordered = true;
                j.move_sent = kPending;
            } else if (j.move_sent != kPending && now >= j.move_sent + kApproachTicks && a->idle() && a->tile != j.stand && !v.has_pending_path(a->id)) {
                bad_stands_[key_of(j.stand)] = now + kBadStandTicks;            // the ant stands idle and not on the tile: nothing took it there
                done = true;
                ++gave_up_;
            }
        }
        if (done) {
            end_job(c, j);
            jobs_.erase(jobs_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

// A new job: the best (enemy ant, tile to strike from, striker) that the model of the engine says dies of one blow
void OpportunityTask::seek(TaskContext& c) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    const bool reserve = v.mine().size() <= tactics_.reserve_of(v);                                    // the last ants stay out of a fight that can be lost (a drowning costs nothing)
    std::vector<const AntView*> strikers;
    for (const AntView& a : v.mine()) {
        if (may_strike(v, c.ledger, id(), a) && c.ledger.owner(a.id) != id()) strikers.push_back(&a);
    }
    if (strikers.empty()) return;
    std::vector<Candidate> cands;
    for (const AntView& e : v.others()) {
        if (is_ally(v, e.team) || e.hp == 0 || shunned(e.id, now) || !attackable(v, e) || striker_of(e.id) != 0) continue;
        const bool walking = e.state == sim::UnitState::Walking;
        // the nearest striker: only ants within reach of the target at all
        bool near = false;
        for (const AntView* a : strikers) near = near || a->tile.chebyshev_dist(e.tile) <= plan.opportunity_reach + 1;
        if (!near) continue;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const sim::TileCoord stand{e.tile.x + dx, e.tile.y + dy};
                if (!grid.in_bounds(stand) || !flight_landing_ok(grid, stand) || grid.has_fire_at(stand) || grid.has_bomb_at(stand) || v.powerup_at(stand) != nullptr) continue;
                if (!MapInfo::walkable(grid, c.seat, stand, v.walk_context())) continue;
                const auto bad = bad_stands_.find(key_of(stand));
                if (bad != bad_stands_.end() && bad->second > now) continue;
                int32_t crowd = 0;
                for (const AntView& o : v.others()) {
                    if (o.id != e.id && !is_ally(v, o.team) && o.tile.chebyshev_dist(stand) <= 2) ++crowd;
                }
                if (crowd >= kCrowd) continue;
                for (const AntView* a : strikers) {
                    const bool at_stand = a->tile == stand;
                    if (walking && !(at_stand && a->idle())) continue;           // an enemy that walks is struck only by an ant that already stands on the right tile
                    if (!at_stand && plan.opportunity_still > 0) {
                        const auto st = stood_.find(e.id);
                        if (st == stood_.end() || now < st->second.since + plan.opportunity_still) continue;      // (it has not stood long enough to be there when the striker comes)
                    }
                    if (!at_stand && plan.opportunity_skip_guard && e.state == sim::UnitState::GuardIdle) continue;
                    const int32_t d = at_stand ? 0 : a->tile.chebyshev_dist(stand);
                    if (d > plan.opportunity_reach || (!at_stand && held_by_other(v, stand, a->id))) continue;
                    if (!at_stand && d > plan.opportunity_harvest_d) {
                        const TaskId owner = c.ledger.owner(a->id);
                        if (owner != kNoTask && c.ledger.rank(owner) == 1) continue;       // (an ant of the economy goes only for a kill that is close)
                    }
                    const KillShot shot = kill_shot(grid, e.tile, e.hp, e.type, stand, a->type);
                    if (!shot.kills()) continue;
                    if (reserve && shot.kind != KillKind::Water) continue;
                    if (!at_stand && !walk_reaches(c.map, c.seat, a->tile, stand)) continue;
                    Candidate cand;
                    cand.score = worth(shot.kind, e) - 25 * d;
                    cand.target = e.id;
                    cand.striker = a->id;
                    cand.target_tile = e.tile;
                    cand.stand = stand;
                    cand.kind = shot.kind;
                    cand.at_stand = at_stand;
                    cands.push_back(cand);
                }
            }
        }
    }
    if (cands.empty()) return;
    std::sort(cands.begin(), cands.end(), [](const Candidate& x, const Candidate& y) {
        if (x.score != y.score) return x.score > y.score;
        return x.target != y.target ? x.target < y.target : x.striker < y.striker;
    });
    // the best that the map as it is NOW lets the striker walk to (a fire wall or a bomb may have cut the way that the start analysis found): one search from the tile to strike from
    const Candidate* pick = nullptr;
    std::map<int64_t, std::vector<int32_t>> fields;
    const size_t width = grid.width();
    for (size_t i = 0; i < cands.size() && i < 6 && pick == nullptr; ++i) {
        const Candidate& k = cands[i];
        if (k.at_stand) {
            pick = &k;
            break;
        }
        auto it = fields.find(key_of(k.stand));
        if (it == fields.end()) it = fields.emplace(key_of(k.stand), MapInfo::cost_field(grid, c.seat, k.stand, v.walk_context())).first;
        const AntView* a = find_ant(v.mine(), k.striker);
        const size_t at = static_cast<size_t>(a->tile.y) * width + static_cast<size_t>(a->tile.x);
        if (at < it->second.size() && it->second[at] >= 0 && MapInfo::walking_ticks(it->second[at]) <= static_cast<int32_t>(plan.opportunity_ticks / 2)) pick = &k;
    }
    if (pick == nullptr || !c.ledger.take(pick->striker, id())) return;
    const Candidate best = *pick;
    Job j;
    j.target = best.target;
    j.striker = best.striker;
    j.target_tile = best.target_tile;
    j.stand = best.stand;
    j.kind = best.kind;
    j.decided = now;
    jobs_.push_back(j);
    ++started_;
    if (best.at_stand) ++ambushes_;
}

}  // namespace ants::ai
