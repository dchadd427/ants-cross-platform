#include "ants_ai/heal_task.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

#include "ants_ai/defence.hpp"
#include "ants_ai/standard_bot.hpp"

namespace ants::ai {

namespace {

constexpr int32_t kCostPerTile = 20;           // the cost field's step over grass
constexpr int32_t kCalmWalkTiles = 6;          // with no danger an ant tops up only when its walk is this short
constexpr int32_t kMoundTiles = 1;             // an ant this close to the entrance is on the mound or at the door: it is not ordered
constexpr int32_t kThiefTiles = 4;             // the keeper of the walls stays while an enemy Thief is this close to the raid tile of the own hill
constexpr uint64_t kReorderTicks = 40;         // an ant is ordered again no sooner than this
constexpr uint32_t kMaxOrders = 8;             // ... and at most this many times in all
constexpr uint32_t kMaxRefusals = 2;           // an ant that shows the can't-go reaction this many times (an order that no walk can carry out) is given up on
constexpr uint64_t kGiveUpTicks = 600;         // an ant that is not healed this long after it was taken is given back
constexpr uint64_t kLeftAloneTicks = 600;      // ... and not taken again for this long
constexpr uint64_t kSampleTicks = 40;          // the walk of an ant at an earlier look counts as "the last look" only when it is not older than this

const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

}  // namespace

int32_t walk_home_tiles(const MapInfo& map, uint8_t seat, sim::TileCoord tile) noexcept {
    const std::vector<int32_t>& field = map.cost_field_of(seat);
    if (field.empty()) return tile.chebyshev_dist(map.hill(seat).entrance);
    if (tile.x < 0 || tile.y < 0 || tile.x >= map.width() || tile.y >= map.height()) return -1;
    const size_t at = static_cast<size_t>(tile.y) * static_cast<size_t>(map.width()) + static_cast<size_t>(tile.x);
    if (at >= field.size() || field[at] < 0) return -1;
    return (field[at] + kCostPerTile - 1) / kCostPerTile;
}

bool striker_near(const BotView& view, sim::TileCoord tile, int32_t radius) noexcept {
    for (const AntView& e : view.others()) {
        if (e.holding || (view.ally() < sim::MAX_PLAYERS && e.team == view.ally())) continue;
        if (e.tile.chebyshev_dist(tile) <= radius) return true;
    }
    return false;
}

bool heal_wanted(const LevelPlan& plan, uint8_t hp, bool danger, bool recent_blow, int32_t walk) noexcept {
    if (hp > plan.heal_hp || walk < 0 || walk > plan.heal_walk_tiles) return false;
    return danger || recent_blow || (hp <= plan.heal_calm_hp && walk <= kCalmWalkTiles);
}

void HealTask::release_all(AntLedger& ledger) {
    for (const auto& j : jobs_) ledger.release(j.first, id());
    jobs_.clear();
}

void HealTask::finish(AntLedger& ledger) { release_all(ledger); }

void HealTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const LevelPlan& plan = tactics_.plan;
    const HillInfo& hill = c.map.hill(c.seat);
    if (!plan.heal || !hill.present || !v.hill_enterable()) {                    // off, no hill, or the hill is closed (the game type 187): nothing is ordered, and what was held is given back
        release_all(c.ledger);
        seen_.clear();
        hurt_.clear();
        left_alone_.clear();
        return;
    }
    const uint64_t now = v.tick();
    for (const Hit& h : tactics_.memory.hits()) {
        if (h.hp_now > 0) hurt_[h.ant] = now;
    }
    for (auto it = hurt_.begin(); it != hurt_.end();) it = now > it->second + plan.heal_recent_ticks ? hurt_.erase(it) : std::next(it);
    for (auto it = seen_.begin(); it != seen_.end();) it = now > it->second.tick + kSampleTicks ? seen_.erase(it) : std::next(it);
    for (auto it = left_alone_.begin(); it != left_alone_.end();) it = now >= it->second ? left_alone_.erase(it) : std::next(it);

    // the walk of a hurt ant at the last look (when it was seen then), and now: an ant whose walk fell is on its way home
    const auto sample = [&](const AntView& a, int32_t walk) {
        Seen before;
        const auto it = seen_.find(a.id);
        if (it != seen_.end() && now <= it->second.tick + kSampleTicks) before = it->second;
        seen_[a.id] = Seen{now, walk};
        return before;
    };
    // a ring of fire walls round the gate shuts the hill: no click into it is carried out. The walking field of the map as it is now is made once, at the first ant that needs the answer
    MapInfo::NowField joined;
    bool joined_made = false;
    const auto reaches = [&](const AntView& a) {
        if (!plan.cantgo_aware || !v.has_grid()) return true;
        if (!joined_made) {
            joined = c.map.field_now(v.grid(), c.seat, v.walk_context());
            joined_made = true;
        }
        return c.map.reaches_hill(joined, a.tile, a.type);
    };
    std::vector<uint32_t> go;                                                    // the ants that are ordered at this look

    // 1. the ants held: healed or gone (given back), given up on, or ordered again
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        if (a == nullptr || c.ledger.owner(it->first) != id()) {                  // dead, or taken by a task above this one
            it = jobs_.erase(it);
            continue;
        }
        if (a->hp >= sim::AntUnit::MAX_HP) {
            c.ledger.release(it->first, id());
            ++healed_;
            it = jobs_.erase(it);
            continue;
        }
        Job& job = it->second;
        if (a->state == sim::UnitState::CantGo) {                                 // an order that the engine refused: no walk joins the ant to the hill (it was thrown into a pocket)
            if (!job.cant_go) ++job.refused;
            job.cant_go = true;
        } else {
            job.cant_go = false;
        }
        if ((now >= job.taken + kGiveUpTicks && a->state != sim::UnitState::EnteringBase) || job.refused >= kMaxRefusals) {                // (an ant in its enter clip is healed in a moment: it is waited for)
            c.ledger.release(it->first, id());
            left_alone_[it->first] = now + kLeftAloneTicks;
            ++gave_up_;
            it = jobs_.erase(it);
            continue;
        }
        const int32_t walk = walk_home_tiles(c.map, c.seat, a->tile);
        const Seen before = sample(*a, walk);
        const bool grew = before.walk >= 0 && walk > before.walk;
        const bool due = job.orders == 0 || now >= job.last_order + kReorderTicks;
        if (a->takes_orders() && a->state != sim::UnitState::Stunned && a->tile.chebyshev_dist(hill.entrance) > kMoundTiles && (a->idle() || grew) && due && job.orders < kMaxOrders && walk >= 0 && reaches(*a)) {
            go.push_back(it->first);
            job.last_order = now;
            ++job.orders;
        }
        ++it;
    }

    // 2. the ants that are hurt and not held: who goes home
    for (const AntView& a : v.mine()) {
        if (a.hp >= sim::AntUnit::MAX_HP || jobs_.count(a.id) != 0) continue;
        const int32_t walk = walk_home_tiles(c.map, c.seat, a.tile);
        const Seen before = sample(a, walk);
        if (a.hp > plan.heal_hp || left_alone_.count(a.id) != 0) continue;
        if (!a.takes_orders() || a.state == sim::UnitState::Stunned || a.tile.chebyshev_dist(hill.entrance) <= kMoundTiles) continue;
        const bool nearer = a.state == sim::UnitState::Walking && before.walk >= 0 && walk >= 0 && walk < before.walk;      // heading for the hill
        if (a.hp == 1 && nearer) continue;                                       // the engine walks an ant with 1 hit point home by itself
        const auto recent = hurt_.find(a.id);
        const bool blow = recent != hurt_.end() && now <= recent->second + plan.heal_recent_ticks;
        if (!heal_wanted(plan, a.hp, striker_near(v, a.tile, plan.heal_danger_tiles), blow, walk)) continue;
        if (tactics_.wall_demand && a.id == tactics_.wall_keeper) {              // the keeper of the walls stays while a Thief stands at the hole
            bool thief = false;
            for (const AntView& e : v.others()) thief = thief || (e.type == sim::AntType::Thief && !(v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) && e.tile.chebyshev_dist(hill.raid) <= kThiefTiles);
            if (thief) continue;
        }
        const TaskId owner = c.ledger.owner(a.id);
        if (a.type == sim::AntType::Thief && (a.state == sim::UnitState::Infiltrating || (owner == StandardBot::kRaids && near_raid_tile(v, c.map, a.tile)))) continue;     // in its raid clip, or about to raid
        if (owner == StandardBot::kIslands || owner == StandardBot::kExpedition || v.standing(a)) continue;
        if (!nearer && !reaches(a)) continue;
        if (!c.ledger.take(a.id, id())) continue;                                // a fight, a rush or a gate goal has it
        jobs_[a.id] = Job{now, nearer ? 0u : now, nearer ? 0u : 1u};
        ++started_;
        if (!nearer) go.push_back(a.id);
    }

    if (!go.empty()) {
        std::sort(go.begin(), go.end());
        c.orders.move(go, hill.entrance, Priority::Urgent);                      // one group order for every ant that is due at this look
        ++orders_;
        ordered_ += static_cast<uint32_t>(go.size());
    }
}

}  // namespace ants::ai
