#pragma once

// The ferry of the standard bot (docs/BOTS.md, "Islands"): a Swimmer that digs no bridge carries food across the water. It is sent like a worker (one move onto a cell of the pile) to the pile
// that pays best per tick of ITS trip, a search over land and water with the Swimmer's step weights, and the engine's harvest loop does the rest. No bridge is needed, none can collapse and none
// is jammed. Idle without a Swimmer that nobody else holds. Everything is read from the BotView; an order that did nothing is learned from the next look.

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ants_ai/tasks.hpp"

namespace ants::ai {

class FerryTask final : public Task {
public:
    struct Params {
        uint32_t per_pile{2};                // Swimmers at one pile (the hill banks about one deposit per 100 ticks, and the workers use the gate as well)
        uint32_t reask_ticks{100};           // the search over land and water is made again this often (the map changes: bridges, bombs, walls)
        uint32_t fail_after{160};            // an ordered Swimmer that is still where it was, empty, this long after the look that ordered it did not obey: the pile is left alone for it
        uint32_t blacklist_ticks{900};
        uint32_t rescue_after{900};          // a Swimmer that stands idle with food this long is sent to the hill's entrance (the engine's loop ended without a deposit)
        uint32_t endgame_margin{40};         // a trip needs this many ticks more than it takes before the clock runs out
    };

    explicit FerryTask(TaskId id) : FerryTask(id, Params{}) {}
    FerryTask(TaskId id, const Params& params) : Task(id), params_(params) {}
    const char* name() const noexcept override { return "ferry"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    /// Swimmers sent to a pile, orders that did nothing, Swimmers sent home with their food
    uint32_t sent() const noexcept { return sent_; }
    uint32_t failures() const noexcept { return failures_; }
    uint32_t rescues() const noexcept { return rescues_; }
    /// The Swimmers that work a pile now
    size_t working() const noexcept { return recs_.size(); }
    /// The pile that the Swimmer works (false: none)
    bool assigned_pile(uint32_t ant, uint32_t& pile) const noexcept {
        const auto it = recs_.find(ant);
        if (it == recs_.end()) return false;
        pile = it->second.pile;
        return true;
    }
    /// The ticks of one round trip to the pile for a Swimmer, as of the last search (-1: not known or no way)
    int32_t trip_ticks(uint32_t pile) const noexcept {
        const auto it = trips_.find(pile);
        return it == trips_.end() ? -1 : it->second;
    }

private:
    struct Rec {
        uint32_t pile{0};
        uint64_t decided{0};                 // the look that ordered it
        uint64_t sent{0};                    // the tick at which the order left (Bot::Fate::Sent); 0: not yet
        bool dropped{false};                 // the order never left (the budget, the controller's filter): the Swimmer is ordered again
        sim::TileCoord origin{};             // where the Swimmer stood
    };

    void search(TaskContext& context);

    Params params_;
    std::map<uint32_t, Rec> recs_;           // Swimmer -> its order
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> excluded_;   // (Swimmer, pile) -> until when the Swimmer is kept away from the pile
    std::map<uint32_t, uint64_t> carrying_;  // a Swimmer that stands idle with food: the first look that saw it so
    std::map<uint32_t, int32_t> trips_;      // pile -> ticks of a round trip
    std::map<uint32_t, sim::TileCoord> clicks_;   // pile -> the cell to click (a cell that is food now)
    std::vector<int32_t> cost_;              // the Swimmer's cost from the hill over land and water (-1: no way); empty before the first search
    uint64_t searched_{0};
    uint32_t sent_{0};
    uint32_t failures_{0};
    uint32_t rescues_{0};
};

}  // namespace ants::ai
