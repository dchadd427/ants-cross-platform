#include "ants_ai/power_stand.hpp"

#include <algorithm>

#include "ants_ai/standard_bot.hpp"
#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;
using standard_detail::occupied;
using standard_detail::tile_index;

namespace {

// The window of the cancel for every pair of surfaces (the index is the SurfaceType: grass, mud, dirt, sand), measured on the engine by the order of the ant and the order that cancels it, both given between two ticks:
// the ticks after the click that the ant has crossed into the power-up's tile (first) and that it lands (last + 1). AI59.1 plays the whole table again and compares.
struct Row {
    uint8_t es_lo, es_hi, wn_lo, wn_hi;           // east and south, west and north
};
constexpr Row kWindows[4][4] = {
    // to:  grass            mud               dirt              sand
    {{9, 12, 10, 12}, {9, 18, 10, 17}, {9, 14, 10, 14}, {9, 12, 10, 13}},          // from grass
    {{15, 19, 16, 19}, {15, 23, 16, 23}, {15, 20, 16, 20}, {15, 18, 16, 19}},      // from mud
    {{10, 14, 12, 15}, {10, 19, 12, 18}, {10, 14, 12, 14}, {10, 13, 12, 14}},      // from dirt
    {{8, 12, 9, 12}, {8, 17, 9, 15}, {8, 13, 9, 13}, {8, 11, 9, 11}},              // from sand
};

int surface_index(sim::SurfaceType s) noexcept {
    switch (s) {
        case sim::SurfaceType::Grass: return 0;
        case sim::SurfaceType::Mud: return 1;
        case sim::SurfaceType::Gravel: return 2;
        case sim::SurfaceType::Slate: return 3;
        default: return -1;
    }
}

constexpr int kStepX[4] = {1, 0, -1, 0};          // east, south, west, north: the steps that the table knows
constexpr int kStepY[4] = {0, 1, 0, -1};

sim::SurfaceType surface_at(const sim::Grid& grid, sim::TileCoord t) noexcept { return grid.get_cell(t).surface_type; }

}  // namespace

StandWindow stand_window(sim::SurfaceType from, sim::SurfaceType to, bool west_north) noexcept {
    const int a = surface_index(from);
    const int b = surface_index(to);
    if (a < 0 || b < 0) return StandWindow{};
    const Row& r = kWindows[a][b];
    return west_north ? StandWindow{r.wn_lo, r.wn_hi} : StandWindow{r.es_lo, r.es_hi};
}

sim::TileCoord cancel_tile(const BotView& view, const std::vector<uint8_t>& mask, sim::TileCoord from) {
    const sim::Grid& grid = view.grid();
    const int32_t w = static_cast<int32_t>(grid.width());
    const int32_t h = static_cast<int32_t>(grid.height());
    if (w <= 0 || h <= 0 || mask.size() != static_cast<size_t>(w) * static_cast<size_t>(h) || !grid.in_bounds(from)) return sim::TileCoord{-1, -1};
    // the ant's part of the map: every tile that a walk from `from` reaches over the mask (8 neighbours, as the engine's search steps), the tiles next to it that hold a power-up too (a click on one is a walk onto it)
    std::vector<uint8_t> part(mask.size(), 0);
    std::vector<int32_t> stack;
    part[static_cast<size_t>(from.y * w + from.x)] = 1;
    stack.push_back(from.y * w + from.x);
    while (!stack.empty()) {
        const int32_t i = stack.back();
        stack.pop_back();
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const int32_t x = i % w + dx;
                const int32_t y = i / w + dy;
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                const size_t j = static_cast<size_t>(y * w + x);
                if (mask[j] == 0 || part[j] != 0) continue;
                part[j] = 1;
                stack.push_back(y * w + x);
            }
        }
    }
    // the tiles within four of the part (a separable dilation: along x, then along y)
    std::vector<uint8_t> row(mask.size(), 0);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            if (part[static_cast<size_t>(y * w + x)] == 0) continue;
            for (int32_t k = std::max(0, x - 4); k <= std::min(w - 1, x + 4); ++k) row[static_cast<size_t>(y * w + k)] = 1;
        }
    }
    std::vector<uint8_t> close(mask.size(), 0);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            if (row[static_cast<size_t>(y * w + x)] == 0) continue;
            for (int32_t k = std::max(0, y - 4); k <= std::min(h - 1, y + 4); ++k) close[static_cast<size_t>(k * w + x)] = 1;
        }
    }
    // the first tile in reading order that is far from the part, no power-up, no mound (a click into a mound is a walk home or a raid)
    const auto usable = [&](int32_t x, int32_t y) {
        const sim::TileCoord t{x, y};
        if (view.powerup_at(t) != nullptr || view.dying_at(t)) return false;
        for (const AntView& a : view.others()) {
            if (a.tile == t) return false;
        }
        for (const AntView& a : view.mine()) {
            if (a.tile == t) return false;
        }
        for (const assets::AnthillSpawn& hill : grid.anthills()) {
            if (x >= static_cast<int32_t>(hill.x) && x <= static_cast<int32_t>(hill.x) + 3 && y >= static_cast<int32_t>(hill.y) && y <= static_cast<int32_t>(hill.y) + 3) return false;
        }
        return true;
    };
    // 1. a tile that a walk can end on in principle but that no walk of the ant's part reaches (a pocket of rocks, another island): the click is accepted, the path manager finds no way ("Can't go there.", the ant
    // stays where the cancel put it). No tile of the part within one tile of it, so that no goal adjustment moves the click into the part.
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y * w + x);
            if (mask[i] == 0 || part[i] != 0 || !usable(x, y)) continue;
            bool touches = false;
            for (int32_t dy = -1; dy <= 1 && !touches; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    const int32_t nx = x + dx;
                    const int32_t ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && part[static_cast<size_t>(ny * w + nx)] != 0) touches = true;
                }
            }
            if (!touches) return sim::TileCoord{x, y};
        }
    }
    // 2. a tile that nothing of the part comes within four tiles of (water, rocks): the goal adjustment finds nothing and the order is a silent stop
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            if (close[static_cast<size_t>(y * w + x)] == 0 && usable(x, y)) return sim::TileCoord{x, y};
        }
    }
    return sim::TileCoord{-1, -1};
}

size_t PowerStandTask::standing_now() const noexcept {
    size_t n = 0;
    for (const auto& j : jobs_) n += j.second.phase == Phase::Stand ? 1u : 0u;
    return n;
}

std::vector<sim::TileCoord> PowerStandTask::tiles() const {
    std::vector<sim::TileCoord> out;
    for (const auto& j : jobs_) out.push_back(j.second.power);
    return out;
}

bool PowerStandTask::blacklisted(uint32_t ant, sim::TileCoord tile, uint64_t now) const noexcept {
    const auto it = black_.find({ant, key_of(tile)});
    return it != black_.end() && it->second > now;
}

void PowerStandTask::finish(AntLedger& ledger) {
    jobs_.clear();
    Task::finish(ledger);
}

// TODO (fairness, for whoever turns rule 1 on: docs/BOT_CONTEST.md, AGENTS.md rule 5): the chain's timing is the controller's clock, not a person's eye. The window is counted from the tick at which the click LEFT
// (Fate::Sent, below) and the cancel is released against the tick at which the sink would APPLY it (CommandSink::applied_at), and the table of windows was measured on the engine, whose path manager delivers a
// path a variable number of ticks after the click (its queue, which no screen shows). A person clicks the power-up and clicks again when the screen shows the ant crossing into the tile; whether that reaches
// a window of three to six ticks at a human reaction time, and whether a table that rests on the path queue's timing is a thing a person could know, is for the maintainer to decide before this rule ships.
void PowerStandTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (command.type != sim::CommandType::GroupMove || command.ants.size() != 1) return;
    const auto it = jobs_.find(command.ants[0]);
    if (it == jobs_.end() || it->second.phase != Phase::Aim || it->second.sent != kPending) return;
    if (command.tile_x != it->second.power.x || command.tile_y != it->second.power.y) return;
    if (fate == Bot::Fate::Sent) {
        it->second.sent = tick;                                        // the clock of the window starts when the click LEFT
    } else {                                                           // it never left: the ant was told nothing, the chain may be given again
        it->second.chained = 0;
        ++it->second.tries;
    }
}

bool PowerStandTask::enemy_near(const BotView& v, sim::TileCoord tile, int32_t radius) const {
    for (const AntView& e : v.others()) {
        if (e.hp == 0 || e.tile.chebyshev_dist(tile) > radius) continue;
        if (v.ally() < sim::MAX_PLAYERS && e.team == v.ally()) continue;
        return true;
    }
    return false;
}

// The tile beside the power-up that the ant gives its chain from: a cardinal neighbour of the power-up (the table of windows knows the four steps), walkable, free, no power-up, of a surface the table knows;
// the nearest to the ant, the east, south, west, north in that order among equals. False when there is none or the ant cannot walk there (another island)
bool PowerStandTask::approach_for(const BotView& v, const AntView& ant, sim::TileCoord power, Tiles& out) const {
    const sim::Grid& grid = v.grid();
    const std::vector<uint8_t>& mask = v.walkable_mask(v.seat());
    const sim::SurfaceType to = surface_at(grid, power);
    int32_t best = 1 << 30;
    for (int k = 0; k < 4; ++k) {
        const sim::TileCoord a{power.x - kStepX[k], power.y - kStepY[k]};
        if (!grid.in_bounds(a) || mask[tile_index(grid, a)] == 0 || v.powerup_at(a) != nullptr) continue;
        if (a != ant.tile && occupied(v, a)) continue;
        if (v.map() != nullptr && (v.map()->ant_component(v.seat(), a) < 0 || v.map()->ant_component(v.seat(), a) != v.map()->ant_component(v.seat(), ant.tile))) continue;
        const StandWindow w = stand_window(surface_at(grid, a), to, kStepX[k] < 0 || kStepY[k] < 0);
        if (w.hi == 0) continue;
        const int32_t d = ant.tile.chebyshev_dist(a);
        if (d < best) {
            best = d;
            out.beside = a;
            out.window = w;
        }
    }
    return best < (1 << 30);
}

// Whether the ant may be sent to stand on `power`, and the job for it
bool PowerStandTask::plan_stand(TaskContext& c, const AntView& ant, sim::TileCoord power, Why why, int32_t reach, Job& out) const {
    const BotView& v = c.view;
    if (ant.tile.chebyshev_dist(power) > reach || blacklisted(ant.id, power, v.tick())) return false;
    const PowerUpView* p = v.powerup_at(power);
    if (p == nullptr || p->standing_ant != 0) return false;
    for (const auto& j : jobs_) {
        if (j.second.power == power) return false;
    }
    if (std::find(tactics_.plug_tiles.begin(), tactics_.plug_tiles.end(), power) != tactics_.plug_tiles.end()) return false;
    if (std::find(tactics_.trip_tiles.begin(), tactics_.trip_tiles.end(), power) != tactics_.trip_tiles.end()) return false;
    Tiles t;
    if (!approach_for(v, ant, power, t)) return false;
    // the ant must be there before an enemy: no enemy ant stands nearer to the tile beside the power-up than the ant does
    const int32_t mine = ant.tile.chebyshev_dist(t.beside);
    for (const AntView& e : v.others()) {
        if (e.hp == 0 || (v.ally() < sim::MAX_PLAYERS && e.team == v.ally())) continue;
        if (e.tile.chebyshev_dist(power) <= mine) return false;
    }
    out = Job{};
    out.why = why;
    out.power = power;
    out.beside = t.beside;
    out.began = v.tick();
    return true;
}

void PowerStandTask::step(TaskContext& c) {
    const LevelPlan& plan = tactics_.plan;
    const BotView& v = c.view;
    const uint64_t now = v.tick();
    if (!plan.power_stand || !v.has_grid() || v.game_mode() != sim::GameMode::HighestScore) {
        for (const auto& j : std::vector<std::pair<uint32_t, Job>>(jobs_.begin(), jobs_.end())) c.ledger.release(j.first, id());
        jobs_.clear();
        tactics_.stand_tiles.clear();
        return;
    }
    for (auto it = black_.begin(); it != black_.end();) it = it->second <= now ? black_.erase(it) : std::next(it);
    const sim::Grid& grid = v.grid();

    // ---- the stands that are under way ----
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const uint32_t id_ant = it->first;
        Job& job = it->second;
        const AntView* a = find_ant(v.mine(), id_ant);
        const PowerUpView* p = v.powerup_at(job.power);
        auto drop = [&](bool blame) {
            if (blame) black_[{id_ant, key_of(job.power)}] = now + params_.black_ticks;
            if (c.ledger.owner(id_ant) == id()) c.ledger.release(id_ant, id());
            it = jobs_.erase(it);
        };
        if (a == nullptr || c.ledger.owner(id_ant) != id() || a->hp == 0) {                      // gone, or taken by a task of a higher rank
            if (a != nullptr && c.ledger.owner(id_ant) == id()) c.ledger.release(id_ant, id());
            it = jobs_.erase(it);
            continue;
        }
        if (p == nullptr) {                                                                      // the power-up is gone: this ant took it (the chain came too late), or another did
            if (job.chained != 0 && a->type != v.default_ant_type()) ++taken_;
            else if (job.chained != 0) ++missed_;
            drop(false);
            continue;
        }
        if (p->standing_ant != 0 && p->standing_ant != id_ant) {                                 // another ant is on it
            drop(false);
            continue;
        }
        if (job.phase == Phase::Approach && a->tile != job.beside && (v.powerup_at(job.beside) != nullptr || occupied(v, job.beside))) {   // (on its way: a power-up has landed on the tile beside, or an ant stands there: a click on it is no plain move any more)
            drop(false);
            continue;
        }
        if (job.phase == Phase::Approach) {
            if (a->tile == job.beside && a->state == sim::UnitState::Idle) {
                job.phase = Phase::Aim;
                job.chained = 0;
            } else if (now > job.began + params_.approach_ticks || !a->takes_orders()) {
                drop(true);
                continue;
            } else if (a->tile != job.beside && (job.ordered == 0 || (a->idle() && now >= job.ordered + 40u))) {
                c.orders.move({a->id}, job.beside, job.why == Why::Refuge ? Priority::Urgent : Priority::Normal);
                job.ordered = now;
            }
        }
        if (job.phase == Phase::Aim) {
            if (job.chained == 0) {                                                               // give the chain, when the ant stands beside the power-up and nothing waits for the path manager
                if (a->tile != job.beside || a->state != sim::UnitState::Idle || job.tries >= params_.tries) {
                    if (job.tries >= params_.tries) {
                        drop(true);
                        continue;
                    }
                    job.phase = Phase::Approach;                                                  // (it was moved: back to the walk)
                    job.ordered = 0;
                } else if (now > job.began + params_.approach_ticks * 2u) {                          // (it has stood there too long: the path manager is never quiet, or the power-up is never free)
                    drop(true);
                    continue;
                } else {
                    size_t pending = 0;
                    for (const AntView& m : v.mine()) pending += v.has_pending_path(m.id) ? 1u : 0u;
                    if (pending <= 1 && !occupied(v, job.power)) {
                        const sim::SurfaceType from = surface_at(grid, a->tile);
                        const StandWindow w = stand_window(from, surface_at(grid, job.power), job.power.x < a->tile.x || job.power.y < a->tile.y);
                        const sim::TileCoord cancel = cancel_tile(v, v.walkable_mask(c.seat), a->tile);
                        if (w.hi == 0 || cancel.x < 0) {
                            drop(true);
                            continue;
                        }
                        std::vector<ChainStep> steps(2);
                        steps[0].tile = job.power;
                        steps[0].pickup = true;
                        steps[1].tile = cancel;
                        steps[1].gap_hi = w.hi;
                        steps[1].gap_lo = w.hi > w.lo ? static_cast<uint32_t>(w.hi) - 1u : w.hi;     // (late in the window: a path that the manager delivers a tick late moves the window later, never earlier)
                        steps[1].cancel = true;
                        c.orders.chain(a->id, steps, Priority::Urgent);
                        job.chained = now;
                        job.sent = kPending;
                        ++chains_;
                    }
                }
            } else if (job.sent != kPending && now >= job.sent + params_.settle_ticks) {         // the look that judges the chain
                if (v.standing(*a) && a->tile == job.power && p->standing_ant == id_ant) {
                    job.phase = Phase::Stand;
                    job.calm_since = 0;
                    ++held_;
                    ++started_[static_cast<size_t>(job.why)];
                } else {
                    ++missed_;                                                                    // the click came too early (the ant is beside the power-up again) or too late (the power-up is taken: see above)
                    job.chained = 0;
                    ++job.tries;
                    job.phase = Phase::Approach;
                    job.ordered = 0;
                    if (job.tries >= params_.tries) {
                        drop(true);
                        continue;
                    }
                }
            } else if (job.sent == kPending && now > job.chained + params_.settle_ticks * 2u) {   // the click never left (the budget, a look that came too late)
                job.chained = 0;
                ++job.tries;
                if (job.tries >= params_.tries) {
                    drop(true);
                    continue;
                }
            }
        }
        if (job.phase == Phase::Stand) {
            if (!v.standing(*a) || a->tile != job.power) {                                        // the ant was moved off (the engine's own orders, a blow that threw it): the stand is over
                drop(false);
                continue;
            }
            const bool refuge = job.why == Why::Refuge;
            const int32_t radius = refuge ? plan.stand_calm_radius : plan.stand_deny_radius + 2;
            if (enemy_near(v, job.power, radius)) job.calm_since = 0;
            else if (job.calm_since == 0) job.calm_since = now;
            const bool safe = job.why == Why::Deny || job.calm_since != 0;                         // (a refuge is not left for a click on the ant's own tile while an enemy is near: the ant that took it is no longer immune)
            const bool wanted = safe && plan.stand_take && a->type == v.default_ant_type() && tactics_.wants[static_cast<size_t>(p->kind)] > 0 && v.ticks_left() > 600;
            if (wanted) {                                                                         // the plan wants this kind: a click on the ant's own tile takes it
                c.orders.pick_up(a->id, job.power, Priority::Urgent);
                ++taken_;
                drop(false);
                continue;
            }
            const uint64_t calm = refuge ? params_.calm_ticks : plan.stand_deny_calm;
            if ((job.calm_since != 0 && now >= job.calm_since + calm) || (refuge && now >= job.began + params_.refuge_max) || v.ticks_left() < 200) {
                if (refuge && a->hp <= plan.idle_heal_hp && v.map() != nullptr && v.hill_enterable()) c.orders.move({a->id}, v.map()->hill(c.seat).entrance, Priority::Normal);          // (a hurt ant walks home to heal)
                drop(false);
                continue;
            }
        }
        ++it;
    }

    // ---- new stands ----
    tactics_.stand_tiles = tiles();
    if (now < last_look_ + params_.look_every || v.ticks_left() < params_.min_ticks_left || jobs_.size() >= plan.stand_max) return;
    last_look_ = now;
    if (v.default_ant_type() != sim::AntType::Worker) return;                                    // (the windows are those of a Worker)
    if (plan.stand_refuge) {
        for (const AntView& a : v.mine()) {
            if (jobs_.size() >= plan.stand_max) break;
            if (a.type != sim::AntType::Worker || a.hp > plan.stand_refuge_hp || a.hp == 0 || a.holding || a.carried_points > 0 || !a.takes_orders() || a.state == sim::UnitState::Stunned) continue;
            if (jobs_.count(a.id) != 0 || v.powerup_at(a.tile) != nullptr || !enemy_near(v, a.tile, plan.stand_threat)) continue;
            const TaskId owner = c.ledger.owner(a.id);
            if (owner == StandardBot::kIslands || owner == StandardBot::kExpedition || (owner != kNoTask && owner != id() && c.ledger.rank(owner) >= c.ledger.rank(id()))) continue;
            const PowerUpView* best = nullptr;
            Job job;
            int32_t best_d = 1 << 30;
            for (const PowerUpView& p : v.powerups()) {
                Job j;
                const int32_t d = a.tile.chebyshev_dist(p.tile);
                if (d >= best_d || !plan_stand(c, a, p.tile, Why::Refuge, params_.refuge_reach, j)) continue;
                best = &p;
                best_d = d;
                job = j;
            }
            if (best == nullptr || !c.ledger.take(a.id, id())) continue;
            jobs_[a.id] = job;
            c.orders.move({a.id}, job.beside, Priority::Urgent);
            jobs_[a.id].ordered = now;
        }
    }
    if (plan.stand_deny && tactics_.surplus > 0) {
        for (const PowerUpView& p : v.powerups()) {
            if (jobs_.size() >= plan.stand_max) break;
            if (((plan.stand_deny_kinds >> static_cast<unsigned>(p.kind)) & 1u) == 0 || p.standing_ant != 0 || tactics_.wants[static_cast<size_t>(p.kind)] > 0) continue;
            if (!enemy_near(v, p.tile, plan.stand_deny_radius)) continue;
            const AntView* best = nullptr;
            Job job;
            int32_t best_d = 1 << 30;
            for (const AntView& a : v.mine()) {                                                   // the ants that the economy has nothing for: idle, empty-handed, whole, no task of a rank above its own
                if (a.type != sim::AntType::Worker || a.hp < 8 || a.holding || a.carried_points > 0 || a.state != sim::UnitState::Idle || jobs_.count(a.id) != 0) continue;
                const TaskId owner = c.ledger.owner(a.id);
                if (owner != kNoTask && owner != StandardBot::kHarvest) continue;
                Job j;
                const int32_t d = a.tile.chebyshev_dist(p.tile);
                if (d >= best_d || !plan_stand(c, a, p.tile, Why::Deny, params_.deny_reach, j)) continue;
                best = &a;
                best_d = d;
                job = j;
            }
            if (best == nullptr || !c.ledger.take(best->id, id())) continue;
            jobs_[best->id] = job;
            c.orders.move({best->id}, job.beside, Priority::Normal);
            jobs_[best->id].ordered = now;
        }
    }
    tactics_.stand_tiles = tiles();
}

}  // namespace ants::ai
