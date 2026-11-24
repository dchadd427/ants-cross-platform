#include "ants_ai/thief_guard.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/opportunity_task.hpp"
#include "ants_ai/standard_tasks.hpp"
#include "ants_sim/movement_tables.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;

namespace {

constexpr int32_t kStrikeRange = 3;               // a fighter this close to a walking Thief is sent at it (the Thief comes to meet it: head on, a blow lands; across its way the two miss)
constexpr uint64_t kBlowGap = 8;                  // an attack order is repeated no sooner than this after the last, and only for a fighter that stands idle after its blow
constexpr uint64_t kChaseGap = 50;                // a fighter that walks at a target is told again after this long (the target has moved)
constexpr uint64_t kMoveGap = 80;                 // a move to the post is repeated no sooner than this
constexpr uint64_t kRaidWait = 140;               // a Thief on the raid tile is waited for this long (the raid clip is 75 ticks)
constexpr uint64_t kLingerTicks = 60;             // a meeting stays this long after the Thief stopped being a target (a blow may have thrown it off the radius)
constexpr int32_t kLootRadius = 10;               // a Thief with loot is struck within this many tiles of the raid tile
constexpr size_t kMaxMeets = 2;
constexpr size_t kReadyForTrap = 2;               // fighters that stand ready at the posts before a wall is put out

bool is_ally(const BotView& v, uint8_t team) noexcept { return v.ally() < sim::MAX_PLAYERS && team == v.ally(); }

size_t open_east_tiles(const sim::Grid& grid, const HillInfo& hill) noexcept {
    size_t n = 0;
    for (const sim::TileCoord& e : east_tiles(hill)) {
        const EastTile k = classify_tile(grid, e);
        n += (k == EastTile::Open || k == EastTile::Bare) ? 1u : 0u;
    }
    return n;
}

size_t wall_east_tiles(const sim::Grid& grid, const HillInfo& hill) noexcept {
    size_t n = 0;
    for (const sim::TileCoord& e : east_tiles(hill)) n += classify_tile(grid, e) == EastTile::Wall ? 1u : 0u;
    return n;
}

// An ant that may be sent: it can strike at all, is of the level's own kind, a Combat Ant, or a Swimmer that stands on land, and is free or holds a chore of rank 2 or less (the economy, the guard, the ferry)
bool fighter_ok(const BotView& v, const AntLedger& ledger, TaskId mine, const AntView& a, bool idle_only) noexcept {
    if (!can_strike(v, a) || (idle_only && !a.idle() && ledger.owner(a.id) != mine)) return false;               // (the answer mode: an ant that stands idle, not a worker on its way to a pile)
    if (a.type == sim::AntType::Swimmer) {
        if (!v.has_grid() || v.grid().terrain_class_at(a.tile) == sim::movement::kTerrainWater) return false;
    } else if (a.type != v.default_ant_type() && a.type != sim::AntType::Combat) {
        return false;
    }
    const TaskId owner = ledger.owner(a.id);
    return owner == kNoTask || owner == mine || ledger.rank(owner) <= 2;
}

}  // namespace

sim::TileCoord ThiefGuardTask::post_of(const HillInfo& hill, size_t n) noexcept {
    static const int32_t kRows[5] = {2, 1, 3, 0, 4};
    return sim::TileCoord{hill.origin.x + 5, hill.origin.y + kRows[n % 5]};
}

std::vector<uint32_t> ThiefGuardTask::fighters_of(uint32_t thief) const {
    std::vector<uint32_t> out;
    const auto it = meets_.find(thief);
    if (it == meets_.end()) return out;
    for (const auto& f : it->second.fighters) out.push_back(f.first);
    return out;
}

void ThiefGuardTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.ants.size() != 1 || (command.type != sim::CommandType::GroupMove && command.type != sim::CommandType::GroupAttack)) return;
    for (auto& m : meets_) {
        const auto it = m.second.fighters.find(command.ants[0]);
        if (it == m.second.fighters.end() || it->second.sent != kPending) continue;
        if (fate == Bot::Fate::Sent) it->second.sent = tick;
        else if (command.type == sim::CommandType::GroupAttack) it->second.struck = 0;       // the click was refused: ordered again at the next look
        else it->second.moved = 0;
        return;
    }
}

void ThiefGuardTask::release_all(TaskContext& c) {
    for (const auto& m : meets_) {
        for (const auto& f : m.second.fighters) c.ledger.release(f.first, id());
    }
    meets_.clear();
}

void ThiefGuardTask::end_meet(TaskContext& c, std::map<uint32_t, Meet>::iterator it, bool gone) {
    for (const auto& f : it->second.fighters) c.ledger.release(f.first, id());
    if (gone) ++thieves_gone_;
    meets_.erase(it);
}

// Whether an enemy Thief is a target now: the loot thief near the hill, the Thief at the raid tile (waited for), the one that comes nearer or stands within the radius (a Thief whose way
// is shut by the walls is a target only while it stands in front of them)
bool ThiefGuardTask::wanted(const BotView& v, const HillInfo& hill, const AntView& e, bool gate_closed, uint64_t /*now*/) const {
    const LevelPlan& plan = tactics_.plan;
    const int32_t d = e.tile.chebyshev_dist(hill.raid);
    if (e.state == sim::UnitState::Infiltrating || e.tile == hill.raid) return true;
    if (e.holding) return d <= kLootRadius && attackable(v, e);
    if (d > plan.thief_guard_radius) return false;
    if (gate_closed) return false;                                                        // the walls shut the hole: a Thief in front of them is no threat, and nobody stands guard for it
    if (e.state != sim::UnitState::Walking) return true;                                  // it stands (in front of the hole, or after a blow)
    for (const ThiefSighting& s : tactics_.memory.thieves()) {
        if (s.ant != e.id) continue;
        return s.before.x >= 0 && s.before.chebyshev_dist(hill.raid) > d;                 // it comes nearer (a Thief that was not in sight a look ago is judged at the next look)
    }
    return true;
}

// The fighters of a meeting. A BLOCKER stands on a tile in front of the hole that nothing else shuts (an ant that stands on all three shuts the hole: a raid order ends in "Can't go there." at
// once, whoever gave it; measured in the engine), a STRIKER waits on the column just east of them and goes at a Thief that stands still. The blockers are taken for every open tile (at most
// plan.thief_guard_block), the strikers up to plan.thief_guard_force; the nearest free ants, Combat Ants first, never the last ants of the bot. No blocker is put on a tile once the Thief is on
// the raid tile: it would shut the Thief in with the loot (and the loot is lost to its owner and to everybody else for as long as it stands)
void ThiefGuardTask::top_up(TaskContext& c, Meet& meet, const AntView& thief, const HillInfo& hill) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const size_t reserve = tactics_.reserve_of(v);
    const sim::Grid& grid = v.grid();
    size_t fighting = 0;
    for (const auto& m : meets_) fighting += m.second.fighters.size();
    std::vector<sim::TileCoord> used;                                                    // the posts that fighters hold already
    for (const auto& m : meets_) {
        for (const auto& f : m.second.fighters) used.push_back(f.second.post);
    }
    const bool at_raid = thief.state == sim::UnitState::Infiltrating || thief.tile == hill.raid;
    // what is wanted: a blocker for each open tile in front of the hole that nobody holds, and strikers up to the force
    std::vector<sim::TileCoord> open;
    if (!at_raid && !plan.thief_guard_no_block) {
        for (const sim::TileCoord& e : east_tiles(hill)) {
            const EastTile k = classify_tile(grid, e);
            if ((k == EastTile::Open || k == EastTile::Bare) && v.powerup_at(e) == nullptr && std::find(used.begin(), used.end(), e) == used.end()) open.push_back(e);
        }
    }
    size_t blockers = 0;
    size_t strikers = 0;
    for (const auto& f : meet.fighters) (f.second.blocker ? blockers : strikers) += 1;
    const size_t want_blockers = std::min<size_t>(open.size() + blockers, plan.thief_guard_block);
    const size_t want_strikers = plan.thief_guard_force;
    if (blockers >= want_blockers && strikers >= want_strikers) return;
    const sim::TileCoord centre = post_of(hill, 0);
    struct Cand {
        const AntView* a;
        int rank;
        int32_t d;
    };
    std::vector<Cand> cands;
    for (const AntView& a : v.mine()) {
        if (!fighter_ok(v, c.ledger, id(), a, !plan.thief_guard) || c.ledger.owner(a.id) == id()) continue;
        const int32_t d = a.tile.chebyshev_dist(centre);
        if (d > static_cast<int32_t>(plan.thief_guard_reach)) continue;
        if (!walk_reaches(c.map, c.seat, a.tile, centre)) continue;
        cands.push_back(Cand{&a, a.type == sim::AntType::Combat ? 0 : a.type == sim::AntType::Swimmer ? 2 : 1, d});
    }
    // the nearest first (a blocker must be there before the Thief); Combat Ants strike (their fists), the plain ants block
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d != y.d ? x.d < y.d : x.a->id < y.a->id; });
    const auto take = [&](const Cand& k, bool block) {
        sim::TileCoord post{-1, -1};
        if (block) {
            for (const sim::TileCoord& t : open) {
                if (std::find(used.begin(), used.end(), t) == used.end() && !(occupied(v, t) && k.a->tile != t)) {
                    post = t;
                    break;
                }
            }
        } else {
            for (size_t n = 0; n < 5 && post.x < 0; ++n) {
                const sim::TileCoord t = post_of(hill, n);
                if (!grid.in_bounds(t) || grid.has_fire_at(t) || grid.has_bomb_at(t) || v.powerup_at(t) != nullptr) continue;
                if (std::find(used.begin(), used.end(), t) != used.end() || (occupied(v, t) && k.a->tile != t)) continue;
                if (MapInfo::walkable(grid, c.seat, t, v.walk_context())) post = t;
            }
        }
        if (post.x < 0 || !c.ledger.take(k.a->id, id())) return false;
        Fighter f;
        f.blocker = block;
        f.post = post;
        meet.fighters[k.a->id] = f;
        used.push_back(post);
        ++fighting;
        return true;
    };
    std::vector<bool> taken(cands.size(), false);
    for (int pass = 0; pass < 2; ++pass) {
        const bool block = pass == 0;
        // blockers: the nearest (a Combat Ant is better at the post of a striker, so it comes last); strikers: Combat Ants first, then the nearest
        std::vector<size_t> order;
        for (size_t i = 0; i < cands.size(); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
            const bool cx = cands[x].rank == 0;
            const bool cy = cands[y].rank == 0;
            return cx != cy ? (block ? cy : cx) : false;
        });
        for (const size_t i : order) {
            if (taken[i]) continue;
            if (block ? blockers >= want_blockers : strikers >= want_strikers) break;
            if (v.mine().size() <= reserve + fighting + 1) return;                        // the last ants stay out of it
            if (take(cands[i], block)) {
                taken[i] = true;
                (block ? blockers : strikers) += 1;
            }
        }
    }
}

void ThiefGuardTask::run(TaskContext& c, Meet& meet, const AntView& e, const HillInfo& hill, bool gate_closed) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const uint64_t now = v.tick();
    const sim::Grid& grid = v.grid();
    const bool e_hit = attackable(v, e);
    const bool standing = e.state != sim::UnitState::Walking;
    size_t ready = 0;
    for (auto it = meet.fighters.begin(); it != meet.fighters.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || c.ledger.owner(it->first) != id() || a->hp < 3 || a->holding || a->carried_points > 0) {
            if (a != nullptr && c.ledger.owner(it->first) == id()) c.ledger.release(it->first, id());
            it = meet.fighters.erase(it);
            continue;
        }
        Fighter& f = it->second;
        const int32_t de = a->tile.chebyshev_dist(e.tile);
        const bool at_post = a->tile == f.post || (!f.blocker && a->tile.chebyshev_dist(f.post) <= 1);
        if (at_post && a->idle()) ++ready;
        const bool kill_mode = standing && meet.stood_since != 0 && now >= meet.stood_since + plan.thief_guard_kill_after;
        if ((!f.blocker || kill_mode) && e_hit && (de <= kStrikeRange || (standing && de <= static_cast<int32_t>(plan.thief_guard_reach)))) {
            // the blow: an idle fighter at once (the order that left a few ticks ago has struck by now), a fighter that walks at the target when its order is old
            const bool idle_again = a->idle() && !v.has_pending_path(a->id) && (f.struck == 0 || now >= f.struck + kBlowGap);
            const bool stale = a->state == sim::UnitState::Walking && f.struck != 0 && now >= f.struck + kChaseGap && de > 1;
            if (idle_again || stale) {
                c.orders.attack({a->id}, e.tile, Priority::Urgent);
                f.struck = now;
                f.sent = kPending;
                ++strikes_;
            }
        } else if (a->tile != f.post && now >= f.moved + kMoveGap && (a->idle() || (a->state == sim::UnitState::Walking && f.moved == 0))) {
            if (v.powerup_at(f.post) == nullptr && !grid.has_fire_at(f.post) && !grid.has_bomb_at(f.post)) {
                c.orders.move({a->id}, f.post, Priority::Urgent);
                f.moved = now;
                f.sent = kPending;
            }
        }
        ++it;
    }
    // the trap: the Thief stands on the raid tile (it has raided, or nothing was there), every tile in front of the hole is shut and at least one of them is a wall of the own keeper: with the strikers
    // ready, one wall goes out, and the Thief is struck as it steps out
    if (plan.thief_trap && walls_ != nullptr && gate_closed && e.holding && e.tile == hill.raid && (e.idle() || e.state == sim::UnitState::CantGo) && wall_east_tiles(grid, hill) > 0) {
        if (ready > 0 && ready >= std::min<size_t>(kReadyForTrap, meet.fighters.size()) && now >= meet.trap_asked + plan.thief_trap_wait) {
            walls_->request_open(now + plan.thief_trap_wait);
            meet.trap_asked = now;
            ++trap_opens_;
        }
    }
}

void ThiefGuardTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    const LevelPlan& plan = tactics_.plan;
    const HillInfo& hill = c.map.hill(c.seat);
    if (!(plan.thief_guard || plan.thief_answer) || !v.has_grid() || !hill.present || tactics_.guard_stance) {
        if (!meets_.empty()) release_all(c);
        return;
    }
    const sim::Grid& grid = v.grid();
    const bool gate_closed = open_east_tiles(grid, hill) == 0;
    for (auto it = rest_.begin(); it != rest_.end();) it = it->second <= now ? rest_.erase(it) : std::next(it);
    // the meetings in hand: the Thief is still a target, or was a moment ago
    for (auto it = meets_.begin(); it != meets_.end();) {
        Meet& m = it->second;
        const AntView* e = find_ant(v.others(), m.thief);
        if (e == nullptr || e->hp == 0 || e->state == sim::UnitState::Dead || e->state == sim::UnitState::Drowning || is_ally(v, e->team)) {
            end_meet(c, it++, true);
            continue;
        }
        if (e->state == sim::UnitState::Walking) m.stood_since = 0;
        else if (m.stood_since == 0) m.stood_since = now;
        if (wanted(v, hill, *e, gate_closed, now)) m.last_threat = now;
        const bool raiding = e->state == sim::UnitState::Infiltrating || e->tile == hill.raid;
        const uint64_t limit = raiding ? kRaidWait + 120u : plan.thief_guard_ticks;
        if (now > m.last_threat + kLingerTicks || now > m.started + std::max<uint64_t>(limit, plan.thief_guard_ticks)) {
            if (now > m.started + std::max<uint64_t>(limit, plan.thief_guard_ticks)) rest_[m.thief] = now + plan.thief_guard_rest;      // (it ran out its time: the fighters go back to work for a while)
            end_meet(c, it++, false);
            continue;
        }
        ++it;
    }
    // new meetings: the targets nearest to the raid tile first
    std::vector<const AntView*> targets;
    for (const AntView& e : v.others()) {
        if (e.type != sim::AntType::Thief || e.hp == 0 || is_ally(v, e.team) || meets_.count(e.id) != 0) continue;
        const auto rest = rest_.find(e.id);
        if (rest != rest_.end() && rest->second > now && e.state != sim::UnitState::Walking) continue;         // (one that stands in front of the hole is left alone while the bot rests; one that walks in is met)
        if (wanted(v, hill, e, gate_closed, now)) targets.push_back(&e);
    }
    std::stable_sort(targets.begin(), targets.end(), [&](const AntView* x, const AntView* y) { return x->tile.chebyshev_dist(hill.raid) < y->tile.chebyshev_dist(hill.raid); });
    for (const AntView* e : targets) {
        if (meets_.size() >= kMaxMeets) break;
        Meet m;
        m.thief = e->id;
        m.started = now;
        m.last_threat = now;
        Meet& slot = meets_.emplace(e->id, std::move(m)).first->second;
        top_up(c, slot, *e, hill);
        if (slot.fighters.empty()) {
            meets_.erase(e->id);                                                          // nobody is free: the next look tries again
            continue;
        }
        ++meets_started_;
    }
    for (auto& m : meets_) {
        const AntView* e = find_ant(v.others(), m.first);
        if (e == nullptr) continue;
        if (m.second.fighters.size() < plan.thief_guard_force) top_up(c, m.second, *e, hill);
        run(c, m.second, *e, hill, gate_closed);
    }
}

}  // namespace ants::ai
