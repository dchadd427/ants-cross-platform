#include "ants_ai/gate_task.hpp"

#include <algorithm>

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
        const bool home_side = where < 0 || side < 0 || where == side;
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
    GateGoal goal = plan_gate_goal(gates, view, rules);
    if (!goal.active()) {
        signature_known_ = true;
        signature_ = sig;
        return;
    }
    signature_known_ = false;
    goal_ = std::move(goal);
    gates_ = std::move(gates);
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
    const uint8_t min_hp = goal_.kind == GoalKind::Hop ? static_cast<uint8_t>(std::min<int32_t>(10, 2 * goal_.hop.bombs + 1)) : uint8_t{1};
    std::vector<const AntView*> candidates;
    const int32_t side = goal_.kind == GoalKind::Hop ? context.map.component(context.seat, goal_.hop.from) : context.map.hill_component(context.seat);   // the walkers' component the ant has to be in
    uint8_t unusable = 0;                                                                  // ants of the kind that cannot serve: hurt, or on the far side of a wall (the start ants of a map may lie on either side)
    for (const AntView& a : view.mine()) {
        if (a.type != goal_.need) continue;
        const int32_t where = context.map.component(context.seat, a.tile);                 // (a mound tile has no component)
        if (a.hp < min_hp || (where >= 0 && side >= 0 && where != side)) {
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
    uint8_t& want = tactics_.wants[static_cast<size_t>(goal_.need)];
    want = std::max<uint8_t>(want, static_cast<uint8_t>(1u + unusable));                  // the power-up task fetches the ant (a trip for a power-up of that kind): one more than the ants that cannot serve
}

void GateGoalTask::act(TaskContext& context) {
    const AntView* ant = nullptr;
    for (const AntView& a : context.view.mine()) {
        if (a.id == ant_) ant = &a;
    }
    if (ant == nullptr || context.ledger.owner(ant_) != id()) {
        fail(context, "ant");
        return;
    }
    const uint64_t now = context.view.tick();
    if (ant->tile != seen_tile_ || ant->hp != seen_hp_ || !ant->idle()) progress_ = now;
    seen_tile_ = ant->tile;
    seen_hp_ = ant->hp;
    if (goal_.kind == GoalKind::Extinguish) act_extinguish(context, *ant);
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

void GateGoalTask::act_hop(TaskContext& context, const AntView& ant) {
    const BotView& view = context.view;
    const uint64_t now = view.tick();
    const BombHop& hop = goal_.hop;
    bool bomb_here = false;
    for (const BombView& b : view.bombs()) bomb_here = bomb_here || (b.tile == hop.bomb && b.owner == context.seat);
    if (bomb_here != seen_bomb_) {
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
            if (pu == nullptr) {                                                          // taken: by this ant, or by another's walk
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
        succeed(context);
        return;
    }
    if (!ant.idle() || !may_order(now)) return;                                           // walking, planting, thrown or stunned: wait for it
    const int32_t at = context.map.component(context.seat, ant.tile);                    // the walkers' components of the map analysis (fire walls shut, bombs walked into)
    const int32_t at_from = context.map.component(context.seat, hop.from);
    if (at >= 0 && at_from >= 0 && at != at_from) {                                                 // thrown somewhere else by a chain that stopped: it cannot come back to plant (a tile of no component, a mound or the bomb's own tile, is not that)
        fail(context, "stranded");
        return;
    }
    if (bomb_here) {
        if (ant.tile == hop.from) {
            context.orders.move({ant_}, hop.bomb);
            ++hops_ordered_;
            thrown_ = true;
        } else {
            context.orders.move({ant_}, hop.from);
        }
    } else {
        if (ant.hp < std::min<int32_t>(10, 2 * hop.bombs + 1)) {                          // a dud cost two hit points: too hurt for the chain
            fail(context, "hurt");
            return;
        }
        if (ant.tile == hop.bomb) context.orders.move({ant_}, hop.from);                  // a plant needs the tile empty: off it after a dud
        else context.orders.special(ant_, hop.bomb);
    }
    last_order_ = now;
}

void GateGoalTask::succeed(TaskContext& context) {
    ++done_;
    if (ant_ != 0) context.ledger.release(ant_, id());
    ant_ = 0;
    goal_ = GateGoal{};
    finished_ = done_ >= params_.goals_max;
    phase_ = finished_ ? Phase::Exhausted : Phase::Idle;
    next_plan_ = context.view.tick() + 20u;
    signature_known_ = false;
}

void GateGoalTask::fail(TaskContext& context, const char* why) {
    why_ = why;
    ++given_up_;
    ++failures_;
    if (ant_ != 0) context.ledger.release(ant_, id());
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
