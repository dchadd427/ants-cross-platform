#include "ants_ai/standard_tasks.hpp"
#include "standard_tasks_shared.hpp"

#include <algorithm>
#include <iterator>

#include "ants_ai/island_info.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::kMinFightHp;
using standard_detail::occupied;
using standard_detail::tile_index;

namespace {

constexpr uint64_t kAsideTicks = 80;          // an ant that was sent off the tiles in front of a hole is not sent to a raid, nor off again, for this long (4 s)

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

}  // namespace

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
uint32_t StrikeTask::enemy_weight(const AntView& e, uint64_t /*now*/) const {
    if (e.type == sim::AntType::Combat) return 8u;
    return tactics_.memory.fought_lately(e.id, 600) || e.state == sim::UnitState::Attacking ? 4u : 1u;
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
                paused_until_ = now + 600u;
            }
            target = -1;                                                           // not winnable: no strike
        }
    }
    if (target < 0) {
        if (active_) {
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
        const bool gone = a == nullptr || c.ledger.owner(it->first) != id() || a->hp < kMinFightHp || a->holding || a->carried_points > 0 || (tactics_.wall_demand && it->first == tactics_.wall_keeper);
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
        if (tactics_.wall_demand && a.id == tactics_.wall_keeper) continue;           // (the Fire Ant that keeps the walls of the thief hole stays while a thief threatens, as it does for the duel)
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
        if (rally_.x >= 0 && (v.powerup_at(rally_) != nullptr || v.grid().has_fire_at(rally_))) rally_ = rally_tile(c, static_cast<uint8_t>(target_));     // (a power-up landed on it, or it burns: no move onto it is accepted)
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

}  // namespace ants::ai
