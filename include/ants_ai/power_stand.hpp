#pragma once

// Standing on a power-up (docs/BOT_CONTEST.md; plan.power_stand, off at every level: the arena shows no win; expires with v0.21.8). The original lets an ant STAND on a power-up: click the power-up, and in the window in
// which the ant has crossed into the power-up's tile and has not yet landed on its center, give any order that is accepted but goes nowhere ("Can't go there."): the pick-up is cancelled, the ant
// stays on the tile, and it is immune there (a power-up is a solid object, so no attack, reflex, bomb or fire wall reaches the ant, and nobody else can take the power-up). Clicking the tile that the
// ant stands on later takes the power-up. The window is a few ticks wide and depends on the two surfaces and the direction of the step (stand_window, measured on the engine: AI59.1).
//
//   refuge    an own ant that is hurt (plan.stand_refuge_hp or fewer hit points) with an enemy ant within plan.stand_threat tiles steps onto a power-up that it reaches first, stands on it while the enemy is
//             near, and then walks home to heal
//   deny      with plan.stand_deny an own ant that the economy cannot use (Tactics::surplus) stands on a power-up of a kind that the bot does not want and that an enemy ant is near: nobody else takes it
//   take      a power-up of a kind that the plan wants and that an own ant of the level's plain type stands on is taken by that ant (a click on its own tile)
//
// The ant first walks to a tile beside the power-up (a plain move), and when it stands there idle the task gives a CHAIN: the click on the power-up and, `stand_window` ticks later, a click on a tile
// that no walk of the ant's part of the map comes within four tiles of (the engine's goal adjustment finds nothing there: the order is accepted and the ant stops, or the path manager says "Can't go
// there."). The chain is the controller's (Orders::chain, the same machinery as the timed rows of the island play); the cancel is marked so that the reach guard, which takes out a click that no walk leads
// to, leaves it alone. What a stand needs is on the view: the ants' tiles, hit points and drawn states, the power-ups with who stands on them, the grid's surfaces.

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

/// The window of the cancel in ticks after the click that sends a Worker one tile onto a power-up: the first tick at which the ant has crossed into the tile (`lo`) and the last before it lands and takes the
/// power-up (`hi`). `from` and `to` are the surfaces of the tile the ant stands on and of the power-up's tile, `west_north` the step goes west or north (the near edge of the tile is crossed later). Water
/// is no surface of a power-up (lo and hi are 0).
struct StandWindow {
    uint8_t lo{0};
    uint8_t hi{0};
};
StandWindow stand_window(sim::SurfaceType from, sim::SurfaceType to, bool west_north) noexcept;

/// A tile for the click that cancels (a click that is accepted and goes nowhere): a tile that a walk could end on but that no walk of the ant's part of the map (8 neighbours over `mask`) reaches, with no tile of
/// the part beside it (a pocket of rocks, another island: the path manager answers "Can't go there."), else a tile that no tile of the part lies within four tiles of (the goal adjustment finds nothing: a silent
/// stop). (-1, -1) when there is none (an open field with no pocket). `mask`: BotView::walkable_mask of the seat.
sim::TileCoord cancel_tile(const BotView& view, const std::vector<uint8_t>& mask, sim::TileCoord from);

class PowerStandTask final : public Task {
public:
    struct Params {
        uint32_t look_every{8};              // the task looks for new stands at this interval (the stands under way are followed at every look)
        int32_t refuge_reach{6};             // a hurt ant looks for a power-up within this many tiles
        int32_t deny_reach{10};              // a spare ant looks for a power-up to stand on within this many tiles
        uint32_t approach_ticks{240};        // the walk to the tile beside the power-up may take this long
        uint32_t settle_ticks{36};           // after the chain has left, the look that judges it comes this long after the click on the power-up
        uint32_t tries{2};                   // the chains given for one power-up (a click that came too early leaves the ant beside the power-up)
        uint32_t black_ticks{1200};          // an (ant, power-up) pair that failed is left alone this long
        uint32_t calm_ticks{160};            // a refuge is left after this long without an enemy near (and a deny after plan.stand_deny_calm)
        uint32_t refuge_max{1200};           // ... or after this long in all
        uint32_t min_ticks_left{900};        // no stand begins with less than this on the clock
    };
    PowerStandTask(TaskId id, Tactics& tactics) : PowerStandTask(id, tactics, Params{}) {}
    PowerStandTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "power-stand"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    void finish(AntLedger& ledger) override;
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    enum class Why : uint8_t { Refuge, Deny };
    /// Chains that were given, stands that held (the ant stood on the power-up at the look that judged the chain), chains that came too early or too late (the ant did not stand), ants that took the power-up
    /// they stood on, and the ants that stand now
    uint32_t chains() const noexcept { return chains_; }
    uint32_t held() const noexcept { return held_; }
    uint32_t missed() const noexcept { return missed_; }
    uint32_t taken() const noexcept { return taken_; }
    uint32_t stands(Why why) const noexcept { return started_[static_cast<size_t>(why)]; }
    size_t standing_now() const noexcept;
    /// The tiles of the power-ups that the task has an ant on or on its way to (Tactics::stand_tiles)
    std::vector<sim::TileCoord> tiles() const;

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    enum class Phase : uint8_t { Approach, Aim, Stand };
    struct Job {
        Why why{Why::Refuge};
        Phase phase{Phase::Approach};
        sim::TileCoord power{-1, -1};        // the power-up
        sim::TileCoord beside{-1, -1};       // the tile the ant stands on when it gives the chain
        uint64_t began{0};                   // the look at which the job was made
        uint64_t ordered{0};                 // the look of the last click to `beside` (0: none yet)
        uint64_t chained{0};                 // the look at which the chain was proposed (0: none)
        uint64_t sent{kPending};             // the tick at which the click on the power-up left
        uint64_t calm_since{0};              // the look since which no enemy has been near (0: one is)
        uint32_t tries{0};
    };
    struct Tiles {
        sim::TileCoord beside{-1, -1};
        StandWindow window{};
    };
    bool plan_stand(TaskContext& context, const AntView& ant, sim::TileCoord power, Why why, int32_t reach, Job& out) const;
    bool approach_for(const BotView& view, const AntView& ant, sim::TileCoord power, Tiles& out) const;
    bool enemy_near(const BotView& view, sim::TileCoord tile, int32_t radius) const;
    bool blacklisted(uint32_t ant, sim::TileCoord tile, uint64_t now) const noexcept;
    static int64_t key_of(sim::TileCoord t) noexcept { return static_cast<int64_t>(t.y) * 4096 + t.x; }

    Tactics& tactics_;
    Params params_;
    std::map<uint32_t, Job> jobs_;                                   // by own ant
    std::map<std::pair<uint32_t, int64_t>, uint64_t> black_;         // (ant, power-up) -> until
    uint64_t last_look_{0};
    uint32_t chains_{0};
    uint32_t held_{0};
    uint32_t missed_{0};
    uint32_t taken_{0};
    uint32_t started_[2]{0, 0};
};

}  // namespace ants::ai
