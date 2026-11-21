#pragma once

// The power-ups that plug a way (docs/BOT_UNSTUCK.md, "A power-up that plugs a way"; plan.plugs). The engine treats a power-up tile as solid: no walk crosses it, and a power-up that lies in a gap
// of a wall, a corridor or a doorway shuts it like a rock does, until some ant walks onto it and takes it (the order is the one click that may name it). A bot that reads the map as it is now
// (MapInfo::walkable_mask) sees such a tile as a wall, so the ants on the far side of it (the start ants of a map are dealt over the whole map), and the loot of a thief that raided there, have
// no way to the hill and stand for good.
//
// The task looks at the map every Params::look_ticks: it labels the walkable parts of the map (the same walkable mask that every task uses), finds the power-ups that touch two parts of it, one
// of them the part of the own hill (the hill's queue row), and when the other part holds an own ant or a pile with food, sends ONE ant to take the power-up: the ant of the far part that has nothing to
// do first (it was stranded), else the nearest empty-handed one of the hill's part that the economy can spare. Its type changes with the pick-up (a worker becomes the Thief, the Combat Ant, the Bomber,
// the Fire Ant or the Swimmer that the power-up makes) and the way is open for everybody. A power-up that opens nothing for the seat (no own ant, no food behind it) is left where it is.
//
// The task reads only the view (and the map's analysis for the hill) and sends plain pick-up orders; it is a task of the standard bot at the rank of the power-up trips.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class PlugTask final : public Task {
public:
    struct Params {
        uint32_t first_look{60};             // the first look comes this late (the opening has its own orders)
        uint32_t look_ticks{100};            // the map is looked at this often
        uint32_t stranded_trip_ticks{1600};  // the longest walk of an ant of the far part (it has nothing else to do)
        uint32_t home_trip_ticks{500};       // the longest walk of an ant of the hill's part (the economy gives it up for that long)
        uint32_t slack_ticks{200};           // a take that has not changed the map this long after its walk should have ended is given up
        uint32_t black_ticks{900};           // an ant that failed at a power-up, and a power-up that was given up, are left alone this long
    };
    PlugTask(TaskId id, Tactics& tactics) : PlugTask(id, tactics, Params{}) {}
    PlugTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "plugs"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    // ---- for the tests and the reports ----
    uint32_t found() const noexcept { return found_; }                    // looks at which a plug that is worth opening was found
    uint32_t sent() const noexcept { return sent_; }                      // pick-ups ordered
    uint32_t opened() const noexcept { return opened_; }                  // power-ups that were gone when the ant that was sent for them was looked at again
    uint32_t given_up() const noexcept { return given_up_; }
    size_t under_way() const noexcept { return takes_.size(); }
    const Params& params() const noexcept { return params_; }
    void set_params(const Params& p) noexcept { params_ = p; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Take {
        sim::TileCoord tile{};
        uint64_t sent{kPending};             // the tick at which the order left
        uint64_t decided{0};
        uint32_t walk{0};                    // the walk in ticks, with the pick-up clip
    };
    static int64_t key_of(sim::TileCoord t) noexcept { return static_cast<int64_t>(t.y) * 4096 + t.x; }
    bool black(uint32_t ant, sim::TileCoord tile, uint64_t tick) const noexcept;
    bool tile_black(sim::TileCoord tile, uint64_t tick) const noexcept;
    void follow(TaskContext& context);
    void look(TaskContext& context);

    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Take> takes_;                                      // by ant
    std::map<std::pair<uint32_t, int64_t>, uint64_t> black_;              // (ant, tile key) until
    std::map<int64_t, uint64_t> tile_black_;                              // tile key until
    uint64_t last_look_{0};
    uint32_t found_{0};
    uint32_t sent_{0};
    uint32_t opened_{0};
    uint32_t given_up_{0};
};

}  // namespace ants::ai
