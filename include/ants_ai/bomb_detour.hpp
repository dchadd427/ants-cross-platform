#pragma once

// Bombs on the way (docs/BOTS.md, "Bombs on the way"): the walk round a visible bomb of another team.
//
// The engine's path finder walks straight over a bomb of ANOTHER team (only the ant's own and its ally's bombs are walls), and the bomb goes off under the walker. A person sees every bomb
// (it is drawn in its owner's color) and clicks round it; a bot's orders were plain clicks on the target. BombDetour is the last look of a standard bot at the orders of a look (after the
// tasks and the ReachGuard): for every ant that is ordered to walk, it runs the engine's own path search (sim::PathSearch, the port of the original's A*) on the map as it is now and, when that
// walk would cross a bomb of another team, runs it again with those bombs as walls. When a walk round them exists and is not much longer, the ant is sent to ONE waypoint of that walk first (a
// plain move to a tile that no bomb covers, from which the engine's own walk to it crosses no bomb) and the order the task gave is given again when the ant is there (a person's second click).
// Every order whose walk meets no bomb leaves untouched, so a match without bombs of another team is the match without this class.
//
// The ants are looked at again at every look: a bomb that appears on a walk that is under way (a new foreign bomb, seen when the set of them grows) sends the ant round it as well, and so does a
// carrier that walks home on the engine's own order (level 2, Hard). The tasks are told about the orders that left as they proposed them (proposed()): they never see the waypoint.

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_sim/path_planner.hpp"

namespace ants::ai {

struct LevelPlan;

class BombDetour {
public:
    /// The most waypoints one order is given on its way (after that the order goes as it is)
    static constexpr uint32_t kMaxLegs = 6;

    /// The pass over the orders of a look: takes the ants whose walk would cross a bomb of another team out of their orders, puts a move to a waypoint in their place, and adds the orders that
    /// give an earlier order again to the ants that are at their waypoint. `plan.bomb_detour` is the level (0: nothing happens and nothing is remembered).
    void apply(const BotView& view, const LevelPlan& plan, const Profile& profile, Orders& orders);

    /// The commands that the tasks proposed for a command that left the controller: the same command when it is none of the waypoint orders of this class, else the orders the waypoint stands for
    /// (for the ants of `sent`), so that a task hears about the fate of its own click.
    std::vector<sim::Command> proposed(const sim::Command& sent) const;

    /// Whether the engine's walk of `ant` to the tile beside `bomb` where a defuse order takes it (the first side, N S W E, that it may enter and that is nearest to it) crosses a bomb of another
    /// team: the Bomber's defuse order is a walk first (BombTask: a Bomber that would cross another bomb walks to a tile beside the bomb that holds none, and defuses from there)
    static bool approach_crosses(const BotView& view, const AntView& ant, sim::TileCoord bomb);

    /// What became of a command that left (or never left) the controller: the ants of a waypoint order start their walk when it is sent; one that never left keeps what it had
    void note(const sim::Command& command, Bot::Fate fate, uint64_t tick);

    /// Counters (the arena report, the tests)
    uint32_t detours() const noexcept { return detours_; }
    uint32_t reissued() const noexcept { return reissued_; }
    uint32_t carriers() const noexcept { return carriers_; }
    uint32_t refused_long() const noexcept { return refused_long_; }

private:
    struct Final {
        sim::CommandType type{sim::CommandType::GroupMove};
        sim::TileCoord tile{};
        bool pickup{false};
        Priority priority{Priority::Normal};
        uint64_t since{0};
        sim::TileCoord via{-1, -1};      // the waypoint the ant is sent to (x < 0: none)
        uint64_t sent{0};                // the tick at which the order to the waypoint left the controller (0: it has not yet)
        uint64_t checked{0};             // the tick at which the walk was last looked at (an ant on the engine's repeat of a harvest walks on its own)
        uint64_t idle_since{0};          // the first look at which the ant stood still (0: it does not)
        uint32_t legs{0};
    };
    struct Via {
        std::vector<uint32_t> ants;
        sim::TileCoord via{};
        sim::Command original;
        bool pickup{false};
        uint64_t tick{0};
    };
    std::map<uint32_t, Final> finals_;
    std::vector<Via> vias_;
    std::map<uint32_t, uint64_t> carrier_seen_;       // carrier -> the bomb generation at which its way home was looked at
    std::set<uint32_t> bombs_;                         // the foreign bombs of the last look (index = y * width + x)
    uint64_t generation_{0};                           // grows when a foreign bomb appears
    sim::PathGridPool pool_;
    uint32_t detours_{0};
    uint32_t reissued_{0};
    uint32_t carriers_{0};
    uint32_t refused_long_{0};
};

}  // namespace ants::ai
