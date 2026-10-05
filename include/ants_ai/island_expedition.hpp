#pragma once

// The expedition of the standard bot (docs/BOTS.md, "Islands"): where no Swimmer lies within a walk of the hill (ISLANDS: the twelve are in rows of power-ups in the corners of the map, on the far
// side of the water), a crew of plain workers is flown there bomb by bomb (island_info.hpp, Flight) and one worker takes one token of the row (Fire, Fire, Swimmer, ...) and walks out.
//
//   route   the fewest flights over islands that have a Bomber to take, to the row whose entrance is nearest to where the last flight lands
//   leg     one flight (S, B, L) of the analysis, kept for every hop of the leg; the Bomber of the island plants on B from a tile beside it, the crew ant on S is clicked onto the bomb; a bomb
//           that was a dud (a fifth) is planted again; nobody stands on L (an ant there is thrown into the water by the next one that lands); an ant with fewer than min_hp hit points does not hop
//   row     the crew ant nearest to the end of the row takes the first token (a planned pick-up) and walks out, then the next one goes in
//
// Everything is read from the BotView and nothing is a promise: an ant that died, a bomb that is gone, an enemy on the shore are seen at the next look and the leg or the row is planned
// again. Idle where a Swimmer lies within a walk (SMALL), where no row has a Swimmer left, and while the bot has the Swimmers it wants.

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ants_ai/island_info.hpp"
#include "ants_ai/island_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class ExpeditionTask final : public Task {
public:
    struct Params {
        uint32_t crew_min{3};                // fewer plain ants than this for the crew (the Bomber of the first leg is another) and nobody goes: two Fire tokens and a Swimmer take three
        uint32_t crew_spare{1};              // ants beyond the number that the tokens need (a dud costs an ant 2 hit points, a blow more)
        uint32_t min_hp{4};                  // an ant with fewer hit points does not hop (each explosion costs 2, a dud as well)
        uint32_t order_gap{60};              // no second order to the same ant within this many ticks (more at a level that is slow to act: twice its latency)
        uint32_t plant_wait{50};             // a plant order that has made no bomb after this many ticks (and the latency of the level) is given again
        uint32_t hop_wait{160};              // a hop order after which the ant is still on the island this long: the bomb was a dud
        uint32_t stuck_ticks{2400};          // no progress for this long: the expedition is given up (and tried again after retry_ticks)
        uint32_t retry_ticks{900};
        uint32_t min_ticks_left{3600};       // nothing starts with less than this left on the clock
    };

    ExpeditionTask(TaskId id, Tactics& tactics) : ExpeditionTask(id, tactics, Params{}) {}
    ExpeditionTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "expedition"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    void finish(AntLedger& ledger) override;

    /// The island task whose analysis the expedition reads (set by the bot); null: the task does nothing
    void attach(const IslandTask* islands) noexcept { islands_ = islands; }
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    bool active() const noexcept { return !route_.empty(); }
    /// The islands of the route (the hill's first, the island with the row last), the row (IslandInfo::token_groups index) and the end of it that the crew goes in at; -1 when none
    const std::vector<int32_t>& route() const noexcept { return route_; }
    int32_t group() const noexcept { return group_; }
    int32_t entrance() const noexcept { return entrance_; }
    /// Bombs planted, hops ordered, landings seen (a crew ant that stands on a later island of the route), bombs that were duds, tokens taken by the crew, Swimmers among them
    uint32_t planted() const noexcept { return planted_; }
    uint32_t hops() const noexcept { return hops_; }
    uint32_t landings() const noexcept { return landings_; }
    uint32_t duds() const noexcept { return duds_; }
    uint32_t taken() const noexcept { return taken_; }
    uint32_t swimmers_taken() const noexcept { return swimmers_taken_; }
    /// Expeditions that were planned, and given up (no progress, nobody left)
    uint32_t planned() const noexcept { return planned_; }
    uint32_t given_up() const noexcept { return given_up_; }
    /// The ticks of the first plant, the first landing and the first Swimmer taken (0: not yet)
    uint64_t first_plant() const noexcept { return first_plant_; }
    uint64_t first_landing() const noexcept { return first_landing_; }
    uint64_t first_swimmer() const noexcept { return first_swimmer_; }
    /// The ants that the task holds now (the crew, and the Bombers of the legs)
    size_t crew_size() const noexcept { return crew_.size(); }
    /// The Bomber of the leg (0: none yet)
    uint32_t bomber_of(size_t leg) const noexcept { return leg < legs_.size() ? legs_[leg].bomber : 0u; }
    /// The flight of the leg as it is chosen now; null before one is
    const Flight* flight_of(size_t leg) const noexcept { return leg < legs_.size() && legs_[leg].chosen ? &legs_[leg].flight : nullptr; }

private:
    struct Leg {
        bool chosen{false};
        Flight flight;                       // the flight that is flown now (S, B, L)
        sim::TileCoord station{-1, -1};      // where the Bomber stands when it plants (beside B, not S)
        uint32_t bomber{0};
        uint64_t bomber_ordered{0};          // the look at which the Bomber was last sent (to the power-up, to its station)
        uint64_t plant_ordered{0};
        uint64_t hop_ordered{0};
        uint32_t hopper{0};
        sim::TileCoord hopper_from{-1, -1};
    };

    /// The tiles round the end of the row where the crew goes in, by the steps of a walk from it (the tunnel itself left out): `park` three steps away (where the crew waits for its turn) and
    /// `leave` six (where the ants that took a token wait). The first two steps are the throat: an ant that stands idle there shuts the way of the ants that walk out
    struct Stations {
        sim::TileCoord park{-1, -1};
        sim::TileCoord leave{-1, -1};
        std::map<int32_t, int32_t> steps;    // tile index -> steps from the entrance (tiles farther than six are not in it)
        int32_t steps_of(const sim::Grid& grid, sim::TileCoord t) const {
            const auto it = steps.find(t.y * static_cast<int32_t>(grid.width()) + t.x);
            return it == steps.end() ? 99 : it->second;
        }
        static constexpr int32_t kThroat = 3;   // fewer steps than this from the entrance
    };

    bool plan(TaskContext& context);
    void stations(const TaskContext& context, Stations& out) const;
    void drive_leg(TaskContext& context, size_t leg);
    void note_taken(TaskContext& context);
    void drive_row(TaskContext& context);
    bool choose_flight(TaskContext& context, size_t leg, const std::vector<const AntView*>& hoppers);
    bool bomber_source(const TaskContext& context, int32_t island, sim::TileCoord* token) const;
    void release_all(TaskContext& context);
    bool may_order(uint32_t ant, uint64_t now) const;
    uint32_t swimmers_wanted() const;

    Tactics& tactics_;
    Params params_;
    const IslandTask* islands_{nullptr};
    int32_t hill_comp_{-1};
    uint32_t gap_{60};                       // the order gap of this look (scaled by the latency of the level)
    std::vector<int32_t> route_;             // the islands of the route (empty: no expedition)
    std::vector<Leg> legs_;                  // legs_[i] flies from route_[i] to route_[i + 1]
    int32_t group_{-1};                      // the row (IslandInfo::token_groups index) and the end of it that the crew goes in at
    int32_t entrance_{-1};
    std::vector<sim::TileCoord> row_;        // the tiles of the row in the order of a walk from that end
    std::set<uint32_t> crew_;                // the ants that fly and take tokens (plain ants when they were taken)
    std::set<uint32_t> done_;                // crew ants that took a token: they walk out of the row and are free
    std::set<std::pair<uint32_t, uint32_t>> landed_;   // (ant, index in the route) of the landings seen
    std::map<uint32_t, uint64_t> ordered_;   // ant -> the look of the last order to it
    std::map<uint32_t, sim::TileCoord> row_ant_;       // the ant that goes for a token: ant -> the tile of its token
    std::map<uint32_t, sim::AntType> row_before_;      // ... and what it was before
    uint64_t progress_{0};
    uint64_t retry_at_{0};
    uint32_t planted_{0};
    uint32_t hops_{0};
    uint32_t landings_{0};
    uint32_t duds_{0};
    uint32_t taken_{0};
    uint32_t swimmers_taken_{0};
    uint32_t planned_{0};
    uint32_t given_up_{0};
    uint64_t first_plant_{0};
    uint64_t first_landing_{0};
    uint64_t first_swimmer_{0};
};

}  // namespace ants::ai
