#pragma once

// The engine's can't-go loop as a world (docs/GAME_REVERSE_ENGINEERING.md 5.40; the same world as tests/test_sim/test_pathmgr_gap.cpp): two hills on a 60 x 60 field, a Thief of team 0 that has raided the hill of
// team 1 and holds the loot, the entrance of the hill of team 0 shut in by a ring of rocks (no walk home), and the Thief's own part of the map a pocket of its own (so that the first slice of the path manager's search
// answers: the refusal comes at the first run after the request and the cycle is the 18 ticks of the original's 900 ms). Used by the controller's tests of the boundary click and by AI57.10.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "ai_test.hpp"

namespace ai_test {

inline constexpr TileCoord kLoopHome0{20, 20};         // team 0's hill: mound 20..23, entrance (21, 21)
inline constexpr TileCoord kLoopHome1{40, 40};         // team 1's hill: mound 40..43, raid tile (43, 42)
inline constexpr TileCoord kLoopRaid{43, 42};

/// The ring of rocks at Chebyshev distance `radius` from `centre`: what it holds is shut off from the rest of the map
inline std::vector<TileCoord> rocks_ring(ants::sim::SimulationEngine& sim, TileCoord centre, int radius) {
    std::vector<TileCoord> out;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
            sim.set_terrain(centre.x + dx, centre.y + dy, ants::sim::TERRAIN_OBSTACLE);
            out.push_back(TileCoord{centre.x + dx, centre.y + dy});
        }
    }
    return out;
}

/// The world. `after_commands` is the rule (the rules number 14 and later, the default of a match played now: the answer to a walk that an event asked for waits for the next tick's commands; false: the rules
/// before 14, every answer at once). `shut_home` false leaves the way home open (no ring round the hill of team 0). `shift` ticks pass before the raid begins, which moves the whole cycle (and its parity).
struct LoopWorld {
    ants::sim::SimulationEngine sim;
    uint32_t thief{0};
    std::vector<TileCoord> rocks;                          // the tiles of the rings
    explicit LoopWorld(bool after_commands = true, uint32_t seed = 11, bool shut_home = true, uint32_t shift = 0) {
        sim.init_test_world(60, 60, seed, 720000);
        sim.set_path_answers_after_commands(after_commands);
        sim.set_anthill(0, kLoopHome0);
        sim.set_anthill(1, kLoopHome1);
        sim.set_player_score(0, 0);
        sim.set_player_score(1, 120);
        if (shut_home) {
            rocks = rocks_ring(sim, TileCoord{22, 22}, 6);
            const std::vector<TileCoord> pocket = rocks_ring(sim, TileCoord{43, 43}, 9);
            rocks.insert(rocks.end(), pocket.begin(), pocket.end());
        }
        thief = sim.spawn_unit(0, ants::sim::AntType::Thief, kLoopRaid);
        for (uint32_t t = 0; t < shift; ++t) sim.tick();
        sim.start_thief_infiltration(thief, 1);
    }
    const ants::sim::AntUnit& unit() { return sim.get_unit(thief); }
    /// Ticks until the loot has moved to the thief and the raid clip is over
    void run_to_raid_end() {
        for (int t = 0; t < 400 && unit().state == ants::sim::UnitState::Infiltrating; ++t) sim.tick();
    }
    /// The rings go (the walls have burnt out, the wall of the enemy is down): a walk home exists
    void open_rocks() {
        for (const TileCoord& t : rocks) sim.set_terrain(t.x, t.y, ants::sim::TERRAIN_WALKABLE);
    }
    bool on_raid_tile() { return unit().pixel_x / 32 == kLoopRaid.x && unit().pixel_y / 32 == kLoopRaid.y; }
};

/// A sink with a delay that APPLIES what it is given (the arena's DelayedSink and a room in one): a command that is submitted at tick t is applied `lag` ticks later, or at the next even tick with `even`;
/// flush() after every tick of the engine, before the controller looks, applies what is due. applied_at says the tick at which it will be applied.
class ApplyingSink final : public ants::sim::CommandSink {
public:
    struct Entry {
        uint64_t sent;
        uint64_t applied;
        ants::sim::Command command;
    };
    ApplyingSink(ants::sim::SimulationEngine& sim, uint32_t lag, bool even) : sim_(sim), lag_(lag), even_(even) {}
    ants::sim::CommandResult submit(const ants::sim::Command& c) override {
        const uint64_t at = applied_at(sim_.current_tick());
        log.push_back(Entry{sim_.current_tick(), at, c});
        waiting_.push_back(Entry{sim_.current_tick(), at, c});
        ants::sim::CommandResult r;
        r.status = ants::sim::CommandResult::Status::Applied;
        return r;
    }
    uint64_t applied_at(uint64_t now) const override {
        const uint64_t due = now + lag_;
        return even_ ? due + due % 2 : due;
    }
    void flush() {
        for (size_t i = 0; i < waiting_.size();) {
            if (waiting_[i].applied > sim_.current_tick()) {
                ++i;
                continue;
            }
            const ants::sim::Command c = waiting_[i].command;
            waiting_.erase(waiting_.begin() + static_cast<std::ptrdiff_t>(i));
            sim_.apply_command(c);
        }
    }
    std::vector<Entry> log;

private:
    ants::sim::SimulationEngine& sim_;
    uint32_t lag_;
    bool even_;
    std::vector<Entry> waiting_;
};

}  // namespace ai_test
