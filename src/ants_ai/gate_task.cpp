#include "ants_ai/gate_task.hpp"

#include <algorithm>
#include <deque>

namespace ants::ai {

namespace {

uint64_t mix(uint64_t h, uint64_t v) noexcept { return (h ^ v) * 1099511628211ull; }

}  // namespace

// What the ants of the gate goal come from: the own Fire ants and Bombers with the side of the walls they stand on, and the power-ups on the map by tile and kind. A change of it is a reason to look again
// (an ant is hatched or hurt, a typed ant drops its old power-up on the hill's side), also for a task that ran out of attempts
uint64_t GateGoalTask::sources(const TaskContext& context) const {
    uint64_t h = 1469598103934665603ull;
    const int32_t side = context.map.hill_component(context.seat);
    for (const AntView& a : context.view.mine()) {
        if (a.type != sim::AntType::Fire && a.type != sim::AntType::Bomber) continue;
        const int32_t where = context.map.component(context.seat, a.tile);
        const bool home_side = (gates_.home() >= 0 && gates_.component(a.tile) == gates_.home()) || where < 0 || side < 0 || where == side;
        h = mix(h, (static_cast<uint64_t>(a.id) * 8u + static_cast<uint64_t>(a.type)) * 2u + (home_side ? 1u : 0u));
    }
    h = mix(h, 0x50C);
    for (const PowerUpView& p : context.view.powerups()) h = mix(h, (static_cast<uint64_t>(p.tile.y) * 4096u + static_cast<uint64_t>(p.tile.x)) * 8u + static_cast<uint64_t>(p.kind));
    return h;
}

uint64_t GateGoalTask::signature(const TaskContext& context) const {
    const BotView& view = context.view;
    uint64_t h = sources(context);
    for (const FireWallView& f : view.fire_walls()) h = mix(h, static_cast<uint64_t>(f.tile.y) * 4096u + static_cast<uint64_t>(f.tile.x));
    h = mix(h, 0xB0B);
    for (const BombView& b : view.bombs()) h = mix(h, (static_cast<uint64_t>(b.tile.y) * 4096u + static_cast<uint64_t>(b.tile.x)) * 8u + b.owner);
    h = mix(h, 0xF00D);
    for (const PileView& p : view.piles()) h = mix(h, p.index);
    return h;
}

void GateGoalTask::on_command(const sim::Command&, Bot::Fate, uint64_t) {}

void GateGoalTask::finish(AntLedger& ledger) {
    ledger.release_all(id());
    if (phase_ != Phase::Exhausted) phase_ = Phase::Idle;
}

void GateGoalTask::cancel(AntLedger& ledger) {
    ledger.release_all(id());
    ant_ = 0;
    goal_ = GateGoal{};
    return_hop_ = BombHop{};
    island_target_ = sim::TileCoord{-1, -1};
    if (phase_ != Phase::Exhausted) phase_ = Phase::Idle;
    signature_known_ = false;
}

void GateGoalTask::step(TaskContext& context) {
    const BotView& view = context.view;
    if (!view.has_grid()) return;
    const uint64_t now = view.tick();
    if (phase_ == Phase::Exhausted) {                                                     // out of attempts: a new source of the ant (a Bomber hatched, a power-up of the kind on the hill's side) is a new start
        if (finished_ || revivals_ >= params_.revivals_max || sources(context) == exhausted_sources_) return;
        ++revivals_;
        failures_ = 0;
        phase_ = Phase::Idle;
        signature_known_ = false;
        next_plan_ = now;
    }
    if (phase_ == Phase::Idle) {
        plan(context);
        return;
    }
    if (now >= attempt_start_ + params_.attempt_ticks) {
        fail(context, "attempt");
        return;
    }
    if (now >= progress_ + params_.step_ticks) {
        fail(context, "step");
        return;
    }
    if (view.ticks_left() < 300) {                                                        // the match is over for all purposes: nothing is held
        fail(context, "clock");
        return;
    }
    if (phase_ == Phase::Fetch) fetch(context);
    else act(context);
}

void GateGoalTask::plan(TaskContext& context) {
    const BotView& view = context.view;
    const uint64_t now = view.tick();
    if (done_ >= params_.goals_max) {
        finished_ = true;
        phase_ = Phase::Exhausted;
        return;
    }
    if (now < pause_until_ || now < next_plan_ || view.ticks_left() < params_.min_ticks_left) return;
    next_plan_ = now + params_.look_every;
    const uint64_t sig = signature(context);
    if (signature_known_ && sig == signature_) return;                                    // nothing changed since the look that found no goal
    GateInfo gates = GateInfo::analyse(context.map, view.grid(), context.seat);
    GoalRules rules;
    rules.hop_bombs = params_.hop_bombs;
    rules.shore_breach = true;
    rules.field_piles = tactics_.field_piles;
    GateGoal goal = plan_gate_goal(gates, view, rules);
    if (!goal.active()) {
        signature_known_ = true;
        signature_ = sig;
        return;
    }
    signature_known_ = false;
    goal_ = std::move(goal);
    gates_ = std::move(gates);
    return_hop_ = BombHop{};
    island_target_ = sim::TileCoord{-1, -1};
    phase_ = Phase::Fetch;
    ant_ = 0;
    attempt_start_ = now;
    progress_ = now;
    last_order_ = 0;
    landed_ = false;
    thrown_ = false;
    seen_tile_ = sim::TileCoord{-1, -1};
    seen_hp_ = 0;
    seen_bomb_ = false;
    ++attempts_;
    if (first_goal_ == 0) first_goal_ = now;
}

void GateGoalTask::fetch(TaskContext& context) {
    const BotView& view = context.view;
    const uint64_t now = view.tick();
    gates_ = GateInfo::analyse(context.map, view.grid(), context.seat);
    const uint8_t min_hp = goal_.kind == GoalKind::Hop ? static_cast<uint8_t>(std::min<int32_t>(10, 2 * goal_.hop.bombs + 1)) : uint8_t{1};
    std::vector<const AntView*> candidates;
    const int32_t req_comp = goal_.kind == GoalKind::Hop
                                 ? (goal_.hop.from_comp >= 0 ? goal_.hop.from_comp : gates_.component(goal_.hop.from))
                                 : gates_.home();
    const int32_t side = goal_.kind == GoalKind::Hop ? context.map.component(context.seat, goal_.hop.from) : context.map.hill_component(context.seat);
    uint8_t unusable = 0;                                                                  // ants of the kind that cannot serve: hurt, or on the far side of a wall (the start ants of a map may lie on either side)
    bool has_home_worker = false;
    const AntView* home_fire = nullptr;
    const AntView* pocket_ant = nullptr;
    sim::TileCoord pocket_pu{-1, -1};
    for (const AntView& a : view.mine()) {
        const int32_t ac = gates_.component(a.tile);
        const int32_t where = context.map.component(context.seat, a.tile);                 // (a mound tile has no component)
        const bool on_side = (req_comp >= 0 && ac >= 0) ? (ac == req_comp)
                                                        : ((where < 0 && req_comp == gates_.home()) || (req_comp < 0 && (where < 0 || side < 0 || where == side)));
        if (!on_side && pocket_ant == nullptr && a.idle() && a.takes_orders()) {
            for (const PowerUpView& p : view.powerups()) {
                if (p.standing_ant != 0 || a.tile.chebyshev_dist(p.tile) != 1) continue;
                bool touches_req = false;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (gates_.component(sim::TileCoord{p.tile.x + dx, p.tile.y + dy}) == req_comp) touches_req = true;
                    }
                }
                if (touches_req) {
                    pocket_ant = &a;
                    pocket_pu = p.tile;
                    break;
                }
            }
        }
        if (on_side && a.type == view.default_ant_type() && a.takes_orders()) has_home_worker = true;
        if (on_side && a.type == sim::AntType::Fire && a.idle() && a.takes_orders() && a.hp >= min_hp && home_fire == nullptr) home_fire = &a;
        if (a.type != goal_.need || view.powerup_at(a.tile) != nullptr) continue;
        if (a.hp < min_hp || !on_side) {
            ++unusable;
            continue;
        }
        if (a.takes_orders()) candidates.push_back(&a);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const AntView* x, const AntView* y) { return x->hp > y->hp; });
    for (const AntView* a : candidates) {                                                 // the healthiest first; one that a task of the same rank or above holds is left to it
        if (!context.ledger.take(a->id, id())) continue;
        ant_ = a->id;
        phase_ = Phase::Act;
        progress_ = now;
        return;
    }
    if (pocket_ant != nullptr && may_order(now)) {
        context.orders.pick_up(pocket_ant->id, pocket_pu);
        last_order_ = now;
        progress_ = now;
        return;
    }
    if (goal_.need == sim::AntType::Bomber && !has_home_worker && home_fire != nullptr && may_order(now)) {
        const PowerUpView* best_pu = nullptr;
        int32_t best_score = 1 << 20;
        for (const PowerUpView& p : view.powerups()) {
            if (p.kind != sim::AntType::Bomber || p.standing_ant != 0) continue;
            int32_t req_neighbors = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (gates_.component(sim::TileCoord{p.tile.x + dx, p.tile.y + dy}) == req_comp) ++req_neighbors;
                }
            }
            if (req_neighbors == 0) continue;
            const int32_t score = (req_neighbors >= 2 ? 0 : 1000) + home_fire->tile.chebyshev_dist(p.tile);
            if (score < best_score) {
                best_score = score;
                best_pu = &p;
            }
        }
        if (best_pu != nullptr) {
            context.orders.pick_up(home_fire->id, best_pu->tile);
            last_order_ = now;
            progress_ = now;
            return;
        }
    }
    uint8_t& want = tactics_.wants[static_cast<size_t>(goal_.need)];
    want = std::max<uint8_t>(want, static_cast<uint8_t>(1u + unusable));                  // the power-up task fetches the ant (a trip for a power-up of that kind): one more than the ants that cannot serve
}

void GateGoalTask::act(TaskContext& context) {
    const AntView* ant = nullptr;
    for (const AntView& a : context.view.mine()) {
        if (a.id == ant_) ant = &a;
    }
    if (ant == nullptr || context.ledger.owner(ant_) != id() || (!landed_ && ant->type != goal_.need)) {
        fail(context, "ant");
        return;
    }
    const uint64_t now = context.view.tick();
    if (ant->tile != seen_tile_ || ant->hp != seen_hp_ || !ant->idle()) progress_ = now;
    seen_tile_ = ant->tile;
    seen_hp_ = ant->hp;
    if (goal_.kind == GoalKind::Extinguish) act_extinguish(context, *ant);
    else if (goal_.kind == GoalKind::Defuse) act_defuse(context, *ant);
    else act_hop(context, *ant);
}

void GateGoalTask::act_extinguish(TaskContext& context, const AntView& ant) {
    const uint64_t now = context.view.tick();
    bool burning = false;
    for (const FireWallView& f : context.view.fire_walls()) burning = burning || f.tile == goal_.cell;
    if (!burning) {
        ++extinguishes_;
        succeed(context);
        return;
    }
    if (ant.idle() && may_order(now)) {
        context.orders.special(ant_, goal_.cell);
        last_order_ = now;
    }
}

void GateGoalTask::act_defuse(TaskContext& context, const AntView& ant) {
    const uint64_t now = context.view.tick();
    bool armed = false;
    for (const BombView& b : context.view.bombs()) armed = armed || b.tile == goal_.cell;             // (a bomb of any color: the engine defuses them all, and a bomb of the seat's own color is a wall for its walkers)
    if (!armed) {
        ++defuses_;
        progress_ = now;
        GateInfo gates = GateInfo::analyse(context.map, context.view.grid(), context.seat);
        if (!gates.empty() && goal_.target < gates.piles().size()) {
            const GateTarget& t = gates.piles()[goal_.target];
            if (!t.free() && t.defuse_bombs > 0 && t.bomb_cell.x >= 0) {
                gates_ = std::move(gates);
                goal_.cell = t.bomb_cell;
                last_order_ = 0;
                return;
            }
            if (!t.free() && t.shore_fire_cells == 0 && t.shore_bombs > 0 && t.shore_bomb_cell.x >= 0) {
                gates_ = std::move(gates);
                goal_.cell = t.shore_bomb_cell;
                last_order_ = 0;
                return;
            }
        }
        succeed(context);
        return;
    }
    bool adj_bomb = false;
    sim::TileCoord safe_stand{-1, -1};
    constexpr int kDx[4] = {0, 1, 0, -1};
    constexpr int kDy[4] = {-1, 0, 1, 0};
    for (int d = 0; d < 4; ++d) {
        const sim::TileCoord n{goal_.cell.x + kDx[d], goal_.cell.y + kDy[d]};
        if (!context.view.grid().in_bounds(n)) continue;
        if (context.view.grid().has_bomb_at(n)) adj_bomb = true;
        if (gates_.home() >= 0 && gates_.component(n) == gates_.home()) {
            if (safe_stand.x < 0 || ant.tile.chebyshev_dist(n) < ant.tile.chebyshev_dist(safe_stand)) safe_stand = n;
        }
    }
    if (adj_bomb && safe_stand.x >= 0 && !(ant.tile == safe_stand)) {
        if (!(island_target_ == safe_stand)) {
            island_target_ = safe_stand;
            last_order_ = 0;
        }
        if (ant.idle() && may_order(now)) {
            context.orders.move({ant_}, safe_stand);
            last_order_ = now;
        }
        return;
    }
    if (!(island_target_ == goal_.cell)) {
        island_target_ = goal_.cell;
        last_order_ = 0;
    }
    if (ant.idle() && may_order(now)) {
        context.orders.special(ant_, goal_.cell);
        last_order_ = now;
    }
}

// Whether `tile` is not clear for the hop yet: a special order onto a tile with an ant on it (the plant) is refused by the controller and the engine alike, and a click on the tile in front of the bomb
// does not get the Bomber there when an ant stands on it. An own ant that has nothing to do is held by the task and sent to a free tile near by (the start ants of a walled map stand on
// the tiles of the best hops); an ant of another team, or one that is busy, is waited for (the step timer ends the attempt if it never leaves). True: not clear (an order was given, or the wait goes on).
bool GateGoalTask::tile_blocked(TaskContext& context, sim::TileCoord tile) {
    const BotView& view = context.view;
    for (const AntView& o : view.others()) {
        if (o.tile == tile) return true;
    }
    const AntView* occupant = nullptr;
    for (const AntView& o : view.mine()) {
        if (o.id != ant_ && o.tile == tile) occupant = &o;
    }
    if (occupant == nullptr) return false;
    if (!occupant->idle() || !occupant->takes_orders() || !context.ledger.take(occupant->id, id())) return true;
    const sim::Grid& grid = view.grid();
    sim::TileCoord best{-1, -1};
    for (int r = 1; r <= 3 && best.x < 0; ++r) {
        for (int dy = -r; dy <= r && best.x < 0; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const sim::TileCoord n{tile.x + dx, tile.y + dy};
                if (!grid.in_bounds(n) || n == goal_.hop.bomb || n == goal_.hop.from || n == return_hop_.bomb || n == return_hop_.from) continue;
                if (!MapInfo::walkable(grid, context.seat, n, view.walk_context()) || grid.has_bomb_at(n) || grid.has_powerup_at(n) || grid.get_cell(n).is_food) continue;
                if (gates_.component(n) != gates_.component(tile) && gates_.component(tile) >= 0) continue;
                bool taken = false;
                for (const AntView& o : view.mine()) taken = taken || o.tile == n;
                for (const AntView& o : view.others()) taken = taken || o.tile == n;
                if (taken) continue;
                best = n;
                break;
            }
        }
    }
    if (best.x >= 0) context.orders.move({occupant->id}, best);
    return true;
}

void GateGoalTask::act_hop(TaskContext& context, const AntView& ant) {
    const BotView& view = context.view;
    const uint64_t now = view.tick();
    const BombHop& hop = goal_.hop;
    bool shore_pile = false;
    for (const GateTarget& pt : gates_.piles()) {
        if (pt.shore_fire_cells >= 0 || pt.shore_bombs >= 0) shore_pile = true;
    }
    bool bomb_here = false;
    for (const BombView& b : view.bombs()) bomb_here = bomb_here || (b.tile == hop.bomb && b.owner == context.seat);
    if (!landed_ && bomb_here != seen_bomb_) {
        progress_ = now;
        if (bomb_here) ++plants_;
        seen_bomb_ = bomb_here;
    }
    const bool in_target = ant.tile == hop.land() || gates_.component(ant.tile) == hop.to_comp;
    if (!bomb_here && thrown_ && ant.idle()) {                                            // the click onto the bomb has been answered: a flight that landed, or a dud
        thrown_ = false;
        if (!in_target) ++duds_;
    }
    if (!bomb_here && in_target) landed_ = true;
    if (landed_) {
        if (goal_.powerup) {
            const std::vector<PowerUpInfo>& start = context.map.powerups();
            const sim::TileCoord tile = goal_.target < start.size() ? start[goal_.target].tile : sim::TileCoord{-1, -1};
            const PowerUpView* pu = view.powerup_at(tile);
            if (!shore_pile) {
                if (pu == nullptr) {                                                      // taken: by this ant, or by another's walk
                    succeed(context);
                    return;
                }
                if (ant.idle() && may_order(now)) {
                    context.orders.pick_up(ant_, tile);
                    ++picks_;
                    last_order_ = now;
                }
                return;
            }
            if (pu != nullptr && pu->kind == sim::AntType::Swimmer && ant.type == sim::AntType::Bomber) {
                constexpr int kR = 3;
                constexpr int kSpan = 2 * kR + 1;
                const auto idx = [&](sim::TileCoord c) -> int {
                    const int rx = c.x - (tile.x - kR);
                    const int ry = c.y - (tile.y - kR);
                    if (rx < 0 || rx >= kSpan || ry < 0 || ry >= kSpan) return -1;
                    return ry * kSpan + rx;
                };
                std::vector<int32_t> dist(kSpan * kSpan, -1);
                std::vector<sim::TileCoord> first_step(kSpan * kSpan, sim::TileCoord{-1, -1});
                std::deque<sim::TileCoord> q;
                const int start_i = idx(ant.tile);
                if (start_i >= 0) {
                    dist[static_cast<size_t>(start_i)] = 0;
                    first_step[static_cast<size_t>(start_i)] = ant.tile;
                    q.push_back(ant.tile);
                }
                while (!q.empty()) {
                    const sim::TileCoord cur = q.front();
                    q.pop_front();
                    const int ci = idx(cur);
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const sim::TileCoord nxt{cur.x + dx, cur.y + dy};
                            const int ni = idx(nxt);
                            if (ni < 0 || dist[static_cast<size_t>(ni)] >= 0 || !view.grid().in_bounds(nxt)) continue;
                            if (!MapInfo::walkable(view.grid(), context.seat, nxt, view.walk_context()) || view.grid().has_bomb_at(nxt)) continue;
                            dist[static_cast<size_t>(ni)] = dist[static_cast<size_t>(ci)] + 1;
                            first_step[static_cast<size_t>(ni)] = (cur == ant.tile) ? nxt : first_step[static_cast<size_t>(ci)];
                            q.push_back(nxt);
                        }
                    }
                }
                sim::TileCoord best_bomb{-1, -1};
                sim::TileCoord best_stand{-1, -1};
                int32_t best_d = 1 << 20;
                constexpr int kDx[4] = {0, 1, 0, -1};
                constexpr int kDy[4] = {-1, 0, 1, 0};
                for (int ry = 0; ry < kSpan; ++ry) {
                    for (int rx = 0; rx < kSpan; ++rx) {
                        const sim::TileCoord b{tile.x - kR + rx, tile.y - kR + ry};
                        if (!view.grid().in_bounds(b) || !view.grid().has_bomb_at(b)) continue;
                        if (view.grid().terrain_class_at(b) == sim::movement::kTerrainWater) continue;
                        for (int d = 0; d < 4; ++d) {
                            const sim::TileCoord s{b.x + kDx[d], b.y + kDy[d]};
                            const int si = idx(s);
                            if (si < 0 || dist[static_cast<size_t>(si)] < 0) continue;
                            if (!MapInfo::walkable(view.grid(), context.seat, s, view.walk_context()) || view.grid().has_bomb_at(s)) continue;
                            if (dist[static_cast<size_t>(si)] < best_d) {
                                best_d = dist[static_cast<size_t>(si)];
                                best_bomb = b;
                                best_stand = s;
                            }
                        }
                    }
                }
                if (best_bomb.x >= 0) {
                    if (ant.tile == best_stand) {
                        if (!(island_target_ == best_bomb)) {
                            island_target_ = best_bomb;
                            last_order_ = 0;
                            progress_ = now;
                        }
                        if (ant.idle() && may_order(now)) {
                            context.orders.special(ant_, best_bomb);
                            last_order_ = now;
                        }
                    } else {
                        const sim::TileCoord step_to = first_step[static_cast<size_t>(idx(best_stand))];
                        if (!(island_target_ == step_to)) {
                            island_target_ = step_to;
                            last_order_ = 0;
                            progress_ = now;
                        }
                        if (ant.idle() && may_order(now)) {
                            context.orders.move({ant_}, step_to);
                            last_order_ = now;
                        }
                    }
                    return;
                }
                if (ant.tile.chebyshev_dist(tile) > 1) {
                    sim::TileCoord best_adj{-1, -1};
                    int32_t adj_d = 1 << 20;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const sim::TileCoord adj{tile.x + dx, tile.y + dy};
                            const int ai = idx(adj);
                            if (ai >= 0 && dist[static_cast<size_t>(ai)] >= 0 && dist[static_cast<size_t>(ai)] < adj_d) {
                                adj_d = dist[static_cast<size_t>(ai)];
                                best_adj = adj;
                            }
                        }
                    }
                    if (best_adj.x >= 0) {
                        const sim::TileCoord step_to = first_step[static_cast<size_t>(idx(best_adj))];
                        if (!(island_target_ == step_to)) {
                            island_target_ = step_to;
                            last_order_ = 0;
                            progress_ = now;
                        }
                        if (ant.idle() && may_order(now)) {
                            context.orders.move({ant_}, step_to);
                            last_order_ = now;
                        }
                        return;
                    }
                }
            }
            if (pu != nullptr) {
                if (!(island_target_ == tile)) {
                    island_target_ = tile;
                    last_order_ = 0;
                }
                if (ant.idle() && may_order(now)) {
                    context.orders.pick_up(ant_, tile);
                    ++picks_;
                    last_order_ = now;
                }
                return;
            }
            if (ant.type == sim::AntType::Swimmer) {
                succeed(context);
                return;
            }
            if (gates_.component(ant.tile) != gates_.home()) {
                if (!ant.idle() || !may_order(now)) return;
                const BombHop bounce = GateInfo::best_map_bomb_bounce(gates_, view.grid(), ant.tile, gates_.home());
                if (bounce.bombs == 0 || ant.hp < 2 * bounce.bombs + 1) {
                    fail(context, "stranded");
                    return;
                }
                context.orders.move({ant_}, bounce.bomb);
                ++hops_ordered_;
                last_order_ = now;
                return;
            }
            if (ant.hp < 6 && context.map.hill(context.seat).present) {
                if (ant.idle() && may_order(now)) {
                    context.orders.move({ant_}, context.map.hill(context.seat).entrance);
                    last_order_ = now;
                }
                return;
            }
            succeed(context);
            return;
        }
        if (goal_.gate == GateKind::Sealed) {
            if (gates_.component(ant.tile) != gates_.home()) {
                if (!ant.idle() || !may_order(now)) return;
                if (!ant.holding) {
                    const PileInfo* pile = context.map.pile(goal_.target);
                    sim::TileCoord food_cell{-1, -1};
                    if (pile != nullptr) {
                        for (const sim::TileCoord c : pile->cells) {
                            if (view.grid().food_object_at_cell(c) >= 0) {
                                food_cell = c;
                                break;
                            }
                        }
                    }
                    if (food_cell.x < 0) {
                        fail(context, "step");
                        return;
                    }
                    context.orders.move({ant_}, food_cell);
                    last_order_ = now;
                    return;
                }
                if (return_hop_.bombs == 0) {
                    return_hop_ = GateInfo::best_hop_between(gates_, view.grid(), hop.to_comp, gates_.home());
                }
                if (return_hop_.bombs == 0) {
                    fail(context, "stranded");
                    return;
                }
                bool ret_bomb = false;
                for (const BombView& b : view.bombs()) ret_bomb = ret_bomb || (b.tile == return_hop_.bomb && b.owner == context.seat);
                if (ret_bomb != seen_bomb_) {
                    progress_ = now;
                    if (ret_bomb) ++plants_;
                    seen_bomb_ = ret_bomb;
                }
                if (ret_bomb) {
                    if (ant.tile == return_hop_.from) {
                        context.orders.move({ant_}, return_hop_.bomb);
                        ++hops_ordered_;
                    } else if (!tile_blocked(context, return_hop_.from)) {
                        context.orders.move({ant_}, return_hop_.from);
                    }
                } else {
                    if (ant.hp < std::min<int32_t>(10, 2 * return_hop_.bombs + 1)) {
                        fail(context, "hurt");
                        return;
                    }
                    if (ant.tile == return_hop_.bomb) context.orders.move({ant_}, return_hop_.from);
                    else if (!tile_blocked(context, return_hop_.bomb)) context.orders.special(ant_, return_hop_.bomb);
                }
                last_order_ = now;
                return;
            }
            if ((ant.holding || ant.hp < 6) && context.map.hill(context.seat).present) {
                if (ant.idle() && may_order(now)) {
                    context.orders.move({ant_}, context.map.hill(context.seat).entrance);
                    last_order_ = now;
                }
                return;
            }
            if (done_ > 0) --done_;
            succeed(context);
            return;
        }
        succeed(context);
        return;
    }
    if (!ant.idle() || !may_order(now)) return;                                           // walking, planting, thrown or stunned: wait for it
    const int32_t at = context.map.component(context.seat, ant.tile);                    // the walkers' components of the map analysis (fire walls shut, bombs walked into)
    const int32_t at_from = context.map.component(context.seat, hop.from);
    const bool same_side = (gates_.home() >= 0 && gates_.component(ant.tile) == gates_.home()) || (at < 0 || at_from < 0 || at == at_from);
    if (!same_side) {                                                                     // thrown somewhere else by a chain that stopped: it cannot come back to plant (a tile of no component, a mound or the bomb's own tile, is not that)
        fail(context, "stranded");
        return;
    }
    if (bomb_here) {
        if (ant.tile == hop.from) {
            context.orders.move({ant_}, hop.bomb);
            ++hops_ordered_;
            thrown_ = true;
        } else if (!tile_blocked(context, hop.from)) {
            context.orders.move({ant_}, hop.from);
        }
    } else {
        if (ant.hp < std::min<int32_t>(10, 2 * hop.bombs + 1)) {                          // a dud cost two hit points: too hurt for the chain
            fail(context, "hurt");
            return;
        }
        if (shore_pile && ant.hp < 9 && context.map.hill(context.seat).present) {
            context.orders.move({ant_}, context.map.hill(context.seat).entrance);
            last_order_ = now;
            return;
        }
        if (ant.tile == hop.bomb) context.orders.move({ant_}, hop.from);                  // a plant needs the tile empty: off it after a dud
        else if (!tile_blocked(context, hop.bomb)) context.orders.special(ant_, hop.bomb);
    }
    last_order_ = now;
}

void GateGoalTask::succeed(TaskContext& context) {
    ++done_;
    failures_ = 0;
    context.ledger.release_all(id());                                                      // (the ant of the goal and the ants that were sent aside)
    ant_ = 0;
    goal_ = GateGoal{};
    return_hop_ = BombHop{};
    island_target_ = sim::TileCoord{-1, -1};
    finished_ = done_ >= params_.goals_max;
    phase_ = finished_ ? Phase::Exhausted : Phase::Idle;
    next_plan_ = context.view.tick() + 20u;
    signature_known_ = false;
}

void GateGoalTask::fail(TaskContext& context, const char* why) {
    why_ = why;
    ++given_up_;
    ++failures_;
    context.ledger.release_all(id());
    ant_ = 0;
    goal_ = GateGoal{};
    if (failures_ > params_.retries) {
        phase_ = Phase::Exhausted;
        exhausted_sources_ = sources(context);
        return;
    }
    phase_ = Phase::Idle;
    pause_until_ = context.view.tick() + params_.pause_ticks;
    signature_known_ = false;
}

}  // namespace ants::ai
