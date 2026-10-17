#include "ants_ai/field_task.hpp"

#include <algorithm>

namespace ants::ai {

namespace {

uint64_t mix(uint64_t h, uint64_t v) noexcept { return (h ^ v) * 0x100000001B3ull; }

}  // namespace

uint32_t FieldTask::count(FieldAction action) const noexcept {
    const size_t i = static_cast<size_t>(action);
    return i < sizeof(counts_) / sizeof(counts_[0]) ? counts_[i] : 0;
}

// The bot's ants (id and kind) and the power-ups on the map: a task that waits after giving up starts again when they change
uint64_t FieldTask::sources(const BotView& view) const {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const AntView& a : view.mine()) h = mix(h, static_cast<uint64_t>(a.id) * 8u + static_cast<uint64_t>(a.type));
    for (const PowerUpView& p : view.powerups()) h = mix(h, (static_cast<uint64_t>(p.tile.y) * 4096u + static_cast<uint64_t>(p.tile.x)) * 8u + static_cast<uint64_t>(p.kind));
    return h;
}

void FieldTask::finish(AntLedger& ledger) {
    ledger.release_all(id());
    ant_ = 0;
}

void FieldTask::cancel(AntLedger& ledger) {
    ledger.release_all(id());
    ant_ = 0;
    pending_ = false;
    hints_.hop = BombHop{};
    active_ = false;
}

void FieldTask::on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) {
    if (!pending_ || command.ants.size() != 1 || command.ants[0] != ant_) return;
    pending_ = false;
    if (fate == Bot::Fate::Sent) sent_at_ = tick;
}

void FieldTask::give_up(TaskContext& context, uint64_t now) {
    if (ant_ != 0) {
        context.ledger.release(ant_, id());
        rest_until_[ant_] = now + params_.rest_ticks;
    }
    ant_ = 0;
    pending_ = false;
    hints_.hop = BombHop{};
    ++given_up_;
    if (params_.ants_max == 0 || given_up_ % params_.ants_max == 0) {                                              // (ants_max ants in a row could do nothing: wait for a change)
        resting_ = true;
        active_ = false;
        rest_sources_ = sources(context.view);
    }
    hints_.route.clear();                                                                 // (a new ant plans afresh: the way of the one before led nowhere)
    hints_.fire_way.clear();
    hints_.swim_way.clear();
}

// The ant that gets the work's kind soonest (field_ant_cost), not one that was given up lately; taken from the lower ranks
uint32_t FieldTask::choose_ant(TaskContext& context, const FieldPlan& plan, uint64_t now) {
    const AntView* best = nullptr;
    int32_t best_cost = -1;
    for (const AntView& a : context.view.mine()) {
        if (!a.takes_orders() || a.holding) continue;
        const auto rest = rest_until_.find(a.id);
        if (rest != rest_until_.end() && now < rest->second) continue;
        const TaskId owner = context.ledger.owner(a.id);
        if (owner != kNoTask && owner != id() && context.ledger.rank(owner) >= context.ledger.rank(id())) continue;
        const int32_t cost = field_ant_cost(plan, context.view, context.map, context.seat, a);
        if (cost < 0) continue;
        if (best == nullptr || cost < best_cost) {
            best = &a;
            best_cost = cost;
        }
    }
    if (best == nullptr || !context.ledger.take(best->id, id())) return 0;
    return best->id;
}

void FieldTask::step(TaskContext& context) {
    const BotView& view = context.view;
    if (!view.has_grid()) return;
    const uint64_t now = view.tick();
    if (resting_) {
        if (sources(view) == rest_sources_) return;
        resting_ = false;
        next_look_ = now;
    }
    const AntView* ant = nullptr;
    for (const AntView& a : view.mine()) {
        if (a.id == ant_) ant = &a;
    }
    if (ant_ != 0 && (ant == nullptr || context.ledger.owner(ant_) != id())) {               // dead, or taken by a task of a higher rank
        ant_ = 0;
        ant = nullptr;
        pending_ = false;
        hints_.hop = BombHop{};
    }
    if (ant == nullptr && now < next_look_) return;
    if (pending_) {                                                                       // the order has not left yet
        if (now < proposed_at_ + params_.lost_ticks) return;
        pending_ = false;
    }
    if (ant != nullptr && (now < sent_at_ + params_.settle_ticks || !ant->idle())) {      // the order is under way
        if (ant->tile != seen_tile_ || ant->hp != seen_hp_ || ant->type != seen_type_) progress_ = now;
        seen_tile_ = ant->tile;
        seen_hp_ = ant->hp;
        seen_type_ = ant->type;
        if (now >= progress_ + params_.step_ticks) give_up(context, now);
        return;
    }

    if (!hints_.map_bombs_known) {                                                        // (the first look: the bombs of the seat on the map now are the map's own)
        for (const BombView& b : view.bombs()) {
            if (b.owner == context.seat || (view.ally() < sim::MAX_PLAYERS && b.owner == view.ally())) hints_.map_bombs.push_back(b.tile);
        }
        hints_.map_bombs_known = true;
    }
    const auto gone = [&](const sim::TileCoord& c) { return !view.grid().has_bomb_at(c); };   // (a bomb planted later where one of them was is the ant's own: a hop's, not work)
    hints_.map_bombs.erase(std::remove_if(hints_.map_bombs.begin(), hints_.map_bombs.end(), gone), hints_.map_bombs.end());
    hints_.ant = ant_;
    hints_.piles = tactics_.field_piles;
    const FieldPlan plan = plan_field(view, context.map, context.seat, hints_);
    work_ = plan.work();
    hints_.pile = plan.pile;
    hints_.route = plan.route;
    hints_.fire_source = plan.fire_source;
    hints_.swim_source = plan.swim_source;
    hints_.fire_way = plan.fire_way;
    hints_.swim_way = plan.swim_way;
    if (work_ == FieldWork::None || (ant == nullptr && view.ticks_left() < params_.min_ticks_left)) {
        if (work_ == FieldWork::None && plan.field && done_tick_ == 0) done_tick_ = now;
        if (ant_ != 0) context.ledger.release(ant_, id());
        ant_ = 0;
        active_ = false;
        hints_.hop = BombHop{};
        next_look_ = now + params_.look_every;
        return;
    }
    active_ = true;
    if (ant == nullptr) {
        ant_ = choose_ant(context, plan, now);
        for (const AntView& a : view.mine()) {
            if (a.id == ant_) ant = &a;
        }
        if (ant == nullptr) {
            ant_ = 0;
            next_look_ = now + params_.look_every;
            return;
        }
        progress_ = now;
        hints_.hop = BombHop{};
    }
    if (ant->tile != seen_tile_ || ant->hp != seen_hp_ || ant->type != seen_type_) progress_ = now;
    seen_tile_ = ant->tile;
    seen_hp_ = ant->hp;
    seen_type_ = ant->type;
    if (now >= progress_ + params_.step_ticks) {
        give_up(context, now);
        return;
    }
    const FieldStep next = next_field_step(plan, view, context.map, context.seat, *ant, hints_);
    hints_.hop = next.hop.bombs > 0 ? next.hop : BombHop{};
    order(context, *ant, next);
}

void FieldTask::order(TaskContext& context, const AntView& ant, const FieldStep& step) {
    const uint64_t now = context.view.tick();
    const bool same = step.action == last_.action && step.tile == last_.tile && ant.tile == ordered_tile_ && ant.hp == ordered_hp_ && ant.type == ordered_type_;
    repeats_ = same ? repeats_ + 1 : 0;
    if (repeats_ >= params_.repeats_max) {                                                // the same order again and again, and nothing of the ant changed: it does not work (an ant stands on the tile, the engine refuses it)
        repeats_ = 0;
        give_up(context, now);
        return;
    }
    ordered_tile_ = ant.tile;
    ordered_hp_ = ant.hp;
    ordered_type_ = ant.type;
    ++counts_[static_cast<size_t>(step.action)];
    last_ = step;
    switch (step.action) {
        case FieldAction::None:
            context.ledger.release(ant_, id());
            ant_ = 0;
            return;
        case FieldAction::Stuck:
            give_up(context, now);
            return;
        case FieldAction::Take:
            takes_[3] = takes_[2];
            takes_[2] = takes_[1];
            takes_[1] = takes_[0];
            takes_[0] = step.tile;
            // (the only guard against a swap livelock: every Take changes the ant's kind, which renews progress_ and resets repeats_, so no other give-up rule fires)
            if (takes_[0] == takes_[2] && takes_[1] == takes_[3] && takes_[0] != takes_[1]) {   // two power-ups taken in turn: each leaves the other where the ant must pass (the engine's drop is fixed by its state)
                takes_[0] = takes_[1] = takes_[2] = takes_[3] = sim::TileCoord{-1, -1};
                give_up(context, now);
                return;
            }
            context.orders.pick_up(ant.id, step.tile, Priority::Urgent);
            break;
        case FieldAction::Step:
        case FieldAction::Walk:
        case FieldAction::Fly:
            context.orders.move({ant.id}, step.tile, Priority::Urgent);
            break;
        case FieldAction::Heal:
            context.orders.move({ant.id}, context.map.hill(context.seat).entrance, Priority::Urgent);
            break;
        case FieldAction::Defuse:
        case FieldAction::Bridge:
        case FieldAction::Extinguish:
        case FieldAction::Plant:
            context.orders.special(ant.id, step.tile, Priority::Urgent);
            break;
    }
    ++orders_;
    pending_ = true;
    proposed_at_ = now;
}

}  // namespace ants::ai
