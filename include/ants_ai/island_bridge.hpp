#pragma once

// The bridges for ants that stand away from the hill (docs/BOT_ISLANDS.md, "Islands", "Bridges for ants"; plan.island_return, island_feeder, island_power_bridge, Hard, ISLANDS): the flower link task
// (island_link.hpp) lends its Swimmer to a second kind of dig. An own ant that is no Swimmer, that no task holds and that has stood still for `island_bridge_idle` ticks on an island other than the
// walk from the hill reaches is "stranded" (the economy counts such an ant as unplaced and leaves it where it is). A bridge is dug for it when the chain of channels from its island to what it
// needs is at most `island_bridge_tiles` water tiles that do not stand, in the original's own way: one special order onto one water tile at a time (a Swimmer builds a tile in three passes, which
// any ant walks over for 3,600 ticks), one dig at a time, the nearest Swimmer that carries nothing, the tiles of a chain in the order of a walk from the ant's side.
//
//   Return   the ant has at most `return_hp` hit points and the chain leads to the hill's island: the ant walks home, which heals it (the hill's door restores all hit points), and the economy has it.
//   Feeder   the chain leads to the hill's island and opens points of food that the hill could not walk to before (the ant's own island, or one on the way): the economy sends the ant to eat.
//   Power    a power-up that the plan wants (Tactics::wants) lies on an island that a chain of a few tiles joins to the ant's island: the pick-up task takes it from the bridge's far side. A Fire hat is
//            fetched only by an ant that can walk home, as everywhere (plan.fire_home_only).
//
// A job is for one ant; it is given up when the ant is gone, moves, or is taken by a task, and a tile that could not be dug is left alone for a while (the link task's blacklist). An own ant that stands on a finished tile of a channel with little life left (the tile was dug in the match, by whom ever: a bridge piece that the map file holds stands for ever) is walked to the nearest
// shore (the swarm and the pick-up task send ants over the new way, and two of them that meet on a one-wide bridge stand there until it falls). The planner reads the view, the map's analysis and the grid
// as they are now and gives plain moves and nothing else; the digs are the link task's.
// No threads, no clock but the view's tick, no randomness.

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "ants_ai/island_info.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

struct BridgeJob {
    enum class Kind : uint8_t { Return = 0, Feeder = 1, Power = 2 };
    Kind kind{Kind::Return};
    uint32_t ant{0};                         // the ant that the bridge is for
    std::vector<sim::TileCoord> tiles;       // the water tiles of the first channel of the chain that does not stand, from the ant's side outward
    int32_t missing{0};                      // the tiles of the whole chain that do not stand
    /// The identity of the job: the same ant and kind are the same job while its chain is dug channel by channel
    int64_t key() const noexcept { return (static_cast<int64_t>(ant) << 2) | static_cast<int64_t>(kind); }
};

class BridgePlanner {
public:
    struct Params {
        uint32_t return_hp{4};               // a Return bridge is for an ant with at most this many hit points
        uint32_t min_swimmers{3};            // no bridge is dug while the bot has fewer Swimmers (the ferry's: a dig takes one of them from the food)
        uint32_t min_ticks_left{2400};       // ... or with less than this left on the clock (a bridge has to be walked over and used)
        uint32_t food_min_points{120};       // a feeder bridge opens at least this much food (units times points a unit)
        uint32_t home_gap{120};              // a hurt ant that was told to walk home is told again after this long, if it stands still
        uint32_t home_orders{4};             // ... at most this many orders in all
        uint32_t life_ticks{3600};           // a finished tile lasts this long (docs/GAMEPLAY.md, "Swimmer")
        uint32_t evacuate_life{600};         // an ant on a tile of a bridge of the planner's that has less life left than this is walked to the nearest shore ...
        uint32_t evacuate_gap{60};           // ... and told again after this long if it still stands on a bridge
    };

    explicit BridgePlanner(TaskId owner) : owner_(owner) {}

    /// The best job at this look (the fewest tiles to dig; Return before Feeder before Power; the lower ant number), or null. The finished channels with life left join their islands, the island of another
    /// team's hill and every island that stands joined to it is closed to the chain. `swimmers` is the number of Swimmers the bot has; `blocked` says that a
    /// tile is left alone (a dig of it failed lately)
    const BridgeJob* look(TaskContext& context, const Tactics& tactics, const IslandInfo& info, uint32_t swimmers, const std::function<bool(sim::TileCoord)>& blocked);
    /// What every look does first: the age of the finished tiles of the channels, and the ants that stand on one that is about to go (walked to the nearest shore)
    void upkeep(TaskContext& context, const IslandInfo& info);
    /// The ants that a Return bridge was dug for walk home once the chain stands, and the planner keeps them from the economy until they have healed
    void walk_home(TaskContext& context);
    /// A tile of a job was ordered
    void note_ordered(BridgeJob::Kind kind) noexcept { ++ordered_[static_cast<size_t>(kind)]; }

    // ---- for the tests and the reports ----
    uint32_t tiles_ordered(BridgeJob::Kind kind) const noexcept { return ordered_[static_cast<size_t>(kind)]; }
    uint32_t tiles_ordered() const noexcept { return ordered_[0] + ordered_[1] + ordered_[2]; }
    /// Ants that were sent home over a bridge (a move to the hill's door), and ants walked off a bridge that was about to go
    uint32_t homeward() const noexcept { return homeward_; }
    uint32_t evacuated() const noexcept { return evacuated_; }

private:
    struct Still {
        sim::TileCoord tile{-1, -1};
        uint64_t since{0};
    };
    struct Home {
        uint64_t last{0};
        uint32_t orders{0};
    };

    TaskId owner_;
    Params params_;
    BridgeJob job_;
    std::map<uint32_t, Still> still_;        // the ants that stand free and still: where, and since when
    std::set<uint32_t> returning_;           // the ants that a Return job was seen for: they walk home when the way stands
    std::map<uint32_t, Home> home_;          // the ants that were told to walk home
    static constexpr uint64_t kFromTheFile = ~uint64_t{0};
    bool primed_{false};                     // the first look has been made: a bridge piece that stood then is the map file's, and stands for ever
    std::map<int32_t, uint64_t> since_;      // finished tile of a channel (y * width + x) -> the tick it was first seen finished (kFromTheFile: it was there at the first look)
    std::map<uint32_t, uint64_t> evacuated_at_;   // ant -> the tick it was last told to leave a bridge
    uint32_t ordered_[3]{0, 0, 0};
    uint32_t homeward_{0};
    uint32_t evacuated_{0};
};

}  // namespace ants::ai
