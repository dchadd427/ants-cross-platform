#pragma once

// Two ants face to face on a bridge (docs/BOT_IDLE.md, "Head-on on a bridge"; plan.bridge_headon, every level): a bridge of finished tiles is one tile wide, and two ants that meet on it
// from opposite ends stand there. The engine's rules are the original's: an ant that finds its next tile held by a moving ant waits 300 ms and tries again, one that finds it held by a standing
// ant plans a way round with a bump, an ant of another team in the way is no way at all (the walk is refused, "Can't go there."), and there is no swap, no yield and no push. Two own ants with
// orders that go opposite ways, or an own ant and a stranger, can stand for minutes until the bridge falls (the watched match of docs/BOT_IDLE.md: 113 seconds, a Bomber and a Worker on a two-tile
// bridge, and a queue behind them). The economy keeps one ant to a bridge, which covers its own carriers and nobody else's: the heal walk of a hurt ant, the swarm's ants, a Thief, an ant of
// the expedition.
//
// What a person does: he clicks one of the two back to the shore it came from and, when the other has passed, sends it on. The task does that. At every look it notes the ants (of any team) that
// stand on a finished bridge tile and have not changed tile; two of them on neighboring bridge tiles for `stand_ticks` are a jam. The own ant that can step back to land soonest (the search
// leaves the other ant's tile out, so the way back is on its own side) is taken from the tasks of rank 4 and below and clicked to that shore tile (a plain move; a volley while it shows the
// can't-go clip, which takes no order but at the tick between two clips: docs/BOT_UNSTUCK.md), held there `hold_ticks`, and given back: its own task sends it on, and the other ant has passed
// by then. No ant is backed off twice within `cooldown_ticks`. A Swimmer is never one of the two (it swims past a bridge).
// Everything it reads is on the view: the grid's bridge pieces, the ants' tiles and drawn states. It sends plain moves.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class BridgeHeadOnTask final : public Task {
public:
    struct Params {
        uint32_t stand_ticks{60};            // two ants face to face that have not changed tile for this long are a jam (a step over a bridge tile takes 19 ticks)
        uint32_t hold_ticks{200};            // the ant that gave way is held on land this long (the other crosses a bridge of a few tiles in 60 to 100 ticks)
        uint32_t cooldown_ticks{400};        // an ant that gave way is not asked to again within this long (the jam is not its own, or the way is shut)
        int32_t retreat_tiles{8};            // the land tile that it backs off to is at most this many tiles away
        uint32_t max_hold_ants{2};           // at most this many ants are held back at a time
        uint32_t plug_ticks{100};            // an own ant that has stood idle this long on the land tile at the foot of a bridge, with an ant that cannot go within `jam_tiles`, is a plug
        int32_t jam_tiles{8};                // ... and the ant that cannot go (in the can't-go clip for a while, or standing on a bridge tile beside the plug) is within this many tiles of it (a bridge of a few tiles: its other end)
    };
    BridgeHeadOnTask(TaskId id, Tactics& tactics) : BridgeHeadOnTask(id, tactics, Params{}) {}
    BridgeHeadOnTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "bridge-head-on"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    /// Pairs of ants that were face to face on a bridge for stand_ticks (one count for every ant of the seat that was a member), and the ants that were sent back to land
    uint32_t jams() const noexcept { return jams_; }
    uint32_t backoffs() const noexcept { return backoffs_; }
    /// The ants that are held back now
    size_t held() const noexcept { return held_.size(); }
    /// The own ants that stood on the foot of a bridge with an ant behind them that could not go, and were sent off it
    uint32_t plugs() const noexcept { return plugs_; }

private:
    struct Still {
        sim::TileCoord tile{-1, -1};
        uint64_t since{0};
    };
    struct Held {
        sim::TileCoord shore{-1, -1};
        uint64_t until{0};
        uint64_t ordered{0};
    };
    struct Jam {
        sim::TileCoord tile{-1, -1};
        uint64_t since{0};
        uint64_t last{0};
        uint32_t looks{0};
    };
    void release_all(TaskContext& context);
    void clear_feet(TaskContext& context);
    /// The nearest free land tile within retreat_tiles of the ant that the flood reaches over `mask`, other than the tile of `other` (and, with `off_the_feet`, no tile at the foot of a bridge)
    sim::TileCoord shore_for(TaskContext& context, const AntView& ant, const std::vector<uint8_t>& mask, sim::TileCoord other, bool off_the_feet, int32_t& cost) const;

    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Still> still_;        // the ants (every team's, by id) on a finished bridge tile: the tile and since when
    std::map<uint32_t, Held> held_;          // the ants that gave way
    std::map<uint32_t, uint64_t> cooldown_;  // ant -> the tick until which it is not asked to give way again
    std::map<uint32_t, Jam> jam_;            // the ants (every team's) that showed the can't-go clip lately: where, since when, and at how many looks
    std::map<uint32_t, Still> idle_;         // the own ants that stand idle: the tile and since when
    uint32_t jams_{0};
    uint32_t backoffs_{0};
    uint32_t plugs_{0};
};

}  // namespace ants::ai
