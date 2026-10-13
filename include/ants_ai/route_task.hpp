#pragma once

// RouteTask: bomb routing for a Hard bot (docs/reverse_engineering/bomb_gates.md, "Bomb routing"). Off unless the plan's gate_goals flag is on, Hard only.
//
// Where bombs (of no team of this seat) lie on the way of the ants to the food, each Bomber clears just the bombs that are on its own way: the engine's walk (the A* of the original, predicted by
// ants::sim::PathSearch on the same step costs) from where the Bomber stands to the pile, and the walks of the hill to the pile and back. Never another team's area. The Bomber only ever steps on a tile
// without a bomb (one step per order, so the engine's own path cannot cut across a bomb) and defuses by the real rule: a special order onto the bomb from a tile next to it (N, S, W or E),
// 24 ticks. A bomb that only a diagonal step touches is reached through a cardinal neighbour of it. While bombs of the route remain, the economy keeps off the pile (a worker that walks into
// a bomb loses 2 hit points and is thrown 4 tiles), and when none is left, or the attempt ran out of time, the pile is open again. Everything is read from the BotView.

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ants_ai/tasks.hpp"

namespace ants::ai {

class RouteTask final : public Task {
public:
    struct Params {
        uint32_t look_every{60};             // the route is predicted again this often
        uint32_t order_gap{12};              // no second order to a Bomber within this many ticks after a step
        uint32_t defuse_gap{30};             // and after a defuse order (the defuse itself takes 24 ticks)
        uint32_t max_bombers{4};             // the most Bombers at the work
        uint32_t stall_ticks{1800};          // no bomb gone and no tile gained for this long (90 s): the task gives up for good
        uint32_t min_hp{3};                  // a Bomber with fewer hit points is not taken
    };
    explicit RouteTask(TaskId id) : RouteTask(id, Params{}) {}
    RouteTask(TaskId id, const Params& params) : Task(id), params_(params) {}
    const char* name() const noexcept override { return "route"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override { cancel(ledger); }
    /// The Bomber is given back (the stall detector's fallback)
    void cancel(AntLedger& ledger);
    /// The pile that the economy is to keep off (empty when the route is clear or the task gave up)
    const std::set<uint32_t>& closed_piles() const noexcept { return closed_; }

    // ---- for the tests and the reports ----
    uint32_t defuses() const noexcept { return defuses_; }
    uint32_t steps() const noexcept { return steps_; }
    size_t route_bombs() const noexcept { return route_bombs_; }
    size_t bombers() const noexcept { return workers_.size(); }
    bool gave_up() const noexcept { return gave_up_; }
    bool cleared() const noexcept { return cleared_; }
    /// The tiles of the route (the predicted walks), for the tests
    const std::vector<sim::TileCoord>& route() const noexcept { return route_; }

private:
    struct Bomber {
        uint64_t last_order{0};
        sim::TileCoord target{-1, -1};       // the bomb it was last ordered to defuse (no other Bomber is sent to it)
        std::vector<sim::TileCoord> own;     // the engine's walk from where it stood at the last look to the pile
    };
    void predict(TaskContext& context);
    void work(TaskContext& context, const AntView& ant, Bomber& bomber);

    Params params_;
    std::set<uint32_t> closed_;
    std::vector<sim::TileCoord> route_;      // tiles of the predicted walks, in order, without repeats
    std::map<uint32_t, Bomber> workers_;
    std::vector<sim::TileCoord> shared_;     // the walks of the hill's ants and the workers, to the pile and back
    uint64_t next_look_{0};
    uint64_t progress_{0};
    size_t route_bombs_{0};
    uint64_t seen_sig_{0};
    uint32_t pile_{0};
    bool have_pile_{false};
    uint32_t defuses_{0};
    uint32_t steps_{0};
    bool gave_up_{false};
    bool cleared_{false};
};

}  // namespace ants::ai
