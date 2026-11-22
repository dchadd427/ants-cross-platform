#include "ants_ai/free_thief_task.hpp"

#include <algorithm>
#include <deque>

#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::tile_index;

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// A tile that a hill's special tiles lie on: the mound, the row in front of it, the raid tile and the tile where empty ants wait (bx - 1 .. bx + 4, by - 1 .. by + 4). The engine sends an ant that stands on
// one of them home again after a refusal, so a Thief that is clicked onto one would be in the loop again
bool near_hill(const MapInfo& map, sim::TileCoord t) {
    for (uint8_t team = 0; team < sim::MAX_PLAYERS; ++team) {
        const HillInfo& h = map.hill(team);
        if (h.present && t.x >= h.origin.x - 1 && t.x <= h.origin.x + 4 && t.y >= h.origin.y - 1 && t.y <= h.origin.y + 4) return true;
    }
    return false;
}

bool loaded(const AntView& a) { return a.holding || a.carried_points > 0; }

}  // namespace

// The tile of the Thief's walkable part (the walkable mask as it is now: the walls of fire, the power-ups and the rocks are not in it) that lies nearest to the own hill and is not one of the hills' tiles, at least
// Params::min_steps steps from the Thief; (-1, -1) when its part has none (a Thief that is shut in on the raid tile alone: the walls have to go, the Fire Ant's business)
sim::TileCoord FreeThiefTask::free_tile(const TaskContext& c, const AntView& thief) const {
    const BotView& v = c.view;
    const sim::Grid& grid = v.grid();
    const HillInfo& own = c.map.hill(c.seat);
    if (!own.present || !grid.in_bounds(thief.tile)) return sim::TileCoord{-1, -1};
    const std::vector<uint8_t> mask = MapInfo::walkable_mask(grid, c.seat, v.walk_context());
    std::vector<int32_t> steps(mask.size(), -1);
    std::deque<sim::TileCoord> open;
    steps[tile_index(grid, thief.tile)] = 0;
    open.push_back(thief.tile);
    sim::TileCoord best{-1, -1};
    int64_t best_key = 0;
    uint32_t looked = 0;
    while (!open.empty() && looked < params_.tile_search) {
        const sim::TileCoord cur = open.front();
        open.pop_front();
        ++looked;
        const int32_t here = steps[tile_index(grid, cur)];
        if (static_cast<uint32_t>(here) >= params_.min_steps && !near_hill(c.map, cur) && v.powerup_at(cur) == nullptr) {
            const int64_t key = static_cast<int64_t>(cur.chebyshev_dist(own.entrance)) * 8 + here;
            if (best.x < 0 || key < best_key) {
                best = cur;
                best_key = key;
            }
        }
        for (int k = 0; k < 8; ++k) {
            const sim::TileCoord n{cur.x + kDx[k], cur.y + kDy[k]};
            if (!grid.in_bounds(n)) continue;
            const size_t ni = tile_index(grid, n);
            if (mask[ni] == 0 || steps[ni] >= 0) continue;
            steps[ni] = here + 1;
            open.push_back(n);
        }
    }
    return best;
}

void FreeThiefTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove || command.ants.size() != 1) return;
    const auto it = recs_.find(command.ants[0]);
    if (it == recs_.end()) return;
    Rec& r = it->second;
    if (r.armed_until == 0 || r.target.x != command.tile_x || r.target.y != command.tile_y) return;     // (not the click this task proposed)
    r.armed_until = 0;
    if (fate == Bot::Fate::Sent) {
        ++sent_;
        r.sent = tick;
        r.misses = 0;
        r.next_try = tick + params_.retry_ticks;
    } else {
        ++r.misses;
        r.next_try = tick + std::min<uint64_t>(static_cast<uint64_t>(params_.retry_ticks) << std::min<uint32_t>(r.misses, 8u), params_.max_retry_ticks);
    }
}

void FreeThiefTask::step(TaskContext& c) {
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!v.has_grid()) return;
    const HillInfo& own = c.map.hill(c.seat);
    if (!own.present) return;
    MapInfo::NowField joined;
    bool joined_made = false;
    for (auto it = recs_.begin(); it != recs_.end();) {
        const AntView* a = find_ant(v.mine(), it->first);
        it = (a == nullptr || a->type != sim::AntType::Thief || !loaded(*a)) ? recs_.erase(it) : std::next(it);
    }
    for (const AntView& a : v.mine()) {
        if (a.type != sim::AntType::Thief || !loaded(a)) continue;
        Rec& r = recs_[a.id];
        if (a.state == sim::UnitState::Walking) {
            if (r.sent != kNone) {                                                      // the click left and the Thief walks: it was taken
                ++freed_;
                r.sent = kNone;
                r.freed = true;
            }
            r.looks = 0;
            continue;
        }
        if (a.state == sim::UnitState::CantGo) {
            if (r.looks == 0) r.streak = now;
            ++r.looks;
            r.last_cantgo = now;
        } else if (now > r.last_cantgo + 60u) {
            r.looks = 0;                                                                // (it has not shown the clip for a while: no loop)
        }
        const bool trapped = r.looks >= params_.trap_looks && now >= r.streak + params_.trap_ticks;
        if (trapped) {
            // in the loop: the click for the one tick of the cycle at which the engine takes it
            if (r.armed_until <= now && now >= r.next_try) {
                const sim::TileCoord tile = free_tile(c, a);
                if (tile.x >= 0) {
                    c.orders.boundary_move(a.id, tile, Priority::Urgent);
                    r.armed_until = now + kBoundaryTtlTicks;
                    r.target = tile;
                    ++proposed_;
                    last_thief_ = a.id;
                    last_target_ = tile;
                } else {
                    r.next_try = now + params_.max_retry_ticks;                         // (no tile: nothing to click to, the walls have to go)
                }
            }
            continue;
        }
        // a Thief that the click freed and that stands idle with its loot: a walk home that exists now is clicked, as the economy clicks a carrier that waits
        if (r.freed && a.state == sim::UnitState::Idle && !v.has_pending_path(a.id) && now >= r.home_after && !near_hill(c.map, a.tile)) {
            if (!joined_made) {
                joined = c.map.field_now(v.grid(), c.seat, v.walk_context());
                joined_made = true;
            }
            if (c.map.reaches_hill(joined, a.tile, a.type)) {
                c.orders.move({a.id}, own.entrance, Priority::Normal);
                r.home_after = now + params_.home_cooldown_ticks;
                ++homeward_;
            }
        }
    }
}

}  // namespace ants::ai
