#pragma once

// FieldTask: the field breach (field_plan.hpp, docs/BOT_GATES.md "The field breach") with ONE ant. Medium and Hard, on a gate map whose pile lies behind a field at the start
// (Tactics::field_map: its route has fire cells or bombs, and no walk with only one kind of them open reaches it), for the piles of Tactics::field_piles.
//
//   look       plan_field on the grid as it is; with work to do, next_field_step for the breach ant once it stands, and that step's order
//   the ant    one ant does the whole breach (it takes power-ups, defuses, bridges, puts fire out): the bot's ant that is nearest to the kind the work needs (an ant of the kind, or one
//              that walks to a power-up of it), the hill's side first; held at the rank of the gate goals
//   orders     one at a time, and only to an ant that stands: the next once the one before has left (Bot::on_command) and the ant stands again. A step that changes nothing of the ant
//              (tile, hit points, kind) for `step_ticks`, the same order `repeats_max` times with nothing of the ant changed, two power-ups taken in turn, or a step that is Stuck
//              gives the ant up: it is not taken again for `rest_ticks`, and after `ants_max` ants given up the task waits until the bot's ants or the power-ups change
//   bombs      the bombs of the seat (or its ally) at the task's first look are the map's own: work like any other bomb; one is forgotten once it is gone (a bomb planted on its
//              tile later is the ant's own, a hop's)
//   end        the work done (no fire cell or bomb on the route, and a Swimmer for a route that swims): the ant is free again, and the task looks again every `look_every` ticks

#include <cstdint>
#include <map>

#include "ants_ai/field_plan.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class FieldTask final : public Task {
public:
    struct Params {
        uint32_t look_every{100};            // the look while there is no work (or no ant for it)
        uint32_t settle_ticks{6};            // after an order has left: the ant has not started yet
        uint32_t lost_ticks{120};            // an order that has not left after this long is taken as lost
        uint32_t step_ticks{900};            // no change of the ant for this long while it is ordered: given up (45 s)
        uint32_t rest_ticks{2400};           // an ant given up is not taken again for this long
        uint32_t ants_max{4};                // ants given up before the task waits for a change of the bot's ants or power-ups
        uint32_t min_ticks_left{1200};       // no breach starts with less than this on the clock
        uint32_t repeats_max{4};             // the same order this many times in a row with no change of the ant: given up
    };

    FieldTask(TaskId id, Tactics& tactics) : FieldTask(id, tactics, Params{}) {}
    FieldTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "field"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    void finish(AntLedger& ledger) override;
    /// The bot goes to the plain economy (the stall detector's fallback): the ant is given back, nothing counts as given up
    void cancel(AntLedger& ledger);
    void set_params(const Params& params) { params_ = params; }

    // ---- for the bot, the tests and the reports ----
    /// There is work, and an ant does it or will
    bool active() const noexcept { return active_; }
    /// The task has not given up for good (the map's own bombs are its work: the counters leave them alone while this holds)
    bool live() const noexcept { return !resting_; }
    uint32_t ant() const noexcept { return ant_; }
    const FieldStep& last_step() const noexcept { return last_; }
    FieldWork work() const noexcept { return work_; }
    uint32_t orders() const noexcept { return orders_; }
    uint32_t count(FieldAction action) const noexcept;
    uint32_t given_up() const noexcept { return given_up_; }
    uint64_t done_tick() const noexcept { return done_tick_; }

private:
    void order(TaskContext& context, const AntView& ant, const FieldStep& step);
    void give_up(TaskContext& context, uint64_t now);
    uint32_t choose_ant(TaskContext& context, const FieldPlan& plan, uint64_t now);
    uint64_t sources(const BotView& view) const;

    Tactics& tactics_;
    Params params_;
    FieldHints hints_;
    FieldStep last_;
    FieldWork work_{FieldWork::None};
    uint32_t ant_{0};
    bool active_{false};
    bool resting_{false};
    uint64_t rest_sources_{0};              // the bot's ants and power-ups when the task began to wait
    uint64_t next_look_{0};
    bool pending_{false};                   // an order was proposed and has not left yet
    uint64_t proposed_at_{0};
    uint64_t sent_at_{0};
    uint64_t progress_{0};                  // the last tick the ant changed (or was taken)
    sim::TileCoord seen_tile_{-1, -1};
    uint8_t seen_hp_{0};
    sim::AntType seen_type_{sim::AntType::Worker};
    std::map<uint32_t, uint64_t> rest_until_;   // ants given up, and until when
    uint32_t given_up_{0};
    uint32_t orders_{0};
    uint32_t counts_[11]{};
    uint64_t done_tick_{0};                 // when the work was first found done
    sim::TileCoord takes_[4]{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};   // the last power-ups taken, the latest first
    uint32_t repeats_{0};                   // the same order in a row with the ant unchanged
    sim::TileCoord ordered_tile_{-1, -1};   // the ant when it was last ordered
    uint8_t ordered_hp_{0};
    sim::AntType ordered_type_{sim::AntType::Worker};
};

}  // namespace ants::ai
