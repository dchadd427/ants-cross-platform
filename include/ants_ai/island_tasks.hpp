#pragma once

// The island task of the standard bot (docs/BOTS.md, "Islands"): food that lies beyond water, reached with the original's own means (island_info.hpp) through the door a person uses.
//
//   bridges      a Swimmer ant digs the chain of water tiles that makes the cheapest walk from the hill to a pile that no walker reaches (a special order onto one water tile at a time, from
//                the shore outward); the workers of the economy (HarvestTask) walk over it, because the pile has become reachable in the map as it is now
//   life         a finished bridge tile lasts 3,600 ticks and drowns whoever stands on it when it goes: the task notes when it first saw each tile finished. A bridge is RETIRED when its
//                oldest tile has less than retire_life left (a third of its life), and a pile whose way has too little life left for a round trip (the walk, the bite, the latency of the
//                bot's own orders and a margin) is CLOSED: the economy sends no new ant to it
//   guard        no ant of the economy is left on a bridge that is going: an ant on a bridge tile whose best way off has little time to spare is sent off by the way with the most (home, or
//                on to the far shore); an ant that works a closed pile is called home (or, when the way home would not last, stopped on land and kept there until a bridge makes it safe)
//   traffic      two ants that meet head-on on a one-wide bridge stop for good (the engine repaths round a standing ant, and there is no way round), so a bridge carries ONE ant of the economy:
//                the piles whose way crosses a bridge (the finished tiles that touch each other) get one ant at a time, and one pile per bridge (HarvestTask::set_pile_limits). More income
//                needs more bridges that do not touch: the planner digs the next one beside the first, not on it
//   renewal      the next bridge is planned over the tiles that will last (a tile with less life than renew_life is not counted), so a parallel bridge is dug while the old one still carries
//   hot bridges  the engine's paths are made when an ant starts walking, and a standing ant on the one-wide bridge sends the ants behind it round by another bridge, so the bot cannot know
//                which bridge an ant will walk onto. A bridge with little life left (hot_life) is therefore watched at its ends: an ant that walks towards one is stopped, and an ant on one
//                is sent off it. (Taking such a bridge down with the Swimmer was tried and left out: the Swimmer is far away when it is due, and the ants it drowns are its own)
//   wants        the Swimmers of the level (Tactics::wants): a Swimmer power-up that lies where a worker can walk to is taken by the power-up task
//
// Everything is read from the BotView (the grid is the map as it is now: bridges, bombs, ants); nothing is a promise, the next look checks it. The task is idle on every map where nothing lies
// beyond water that a chain of at most IslandInfo::kMaxChannel tiles reaches: it claims no ant, sends no order and sets no want there.

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "ants_ai/island_info.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class IslandTask final : public Task {
public:
    struct Params {
        uint32_t max_new_tiles{6};           // a bridge of more new tiles than this is not planned
        uint32_t tile_cost{150};             // what a new tile adds to the cost of a route (cost units, 0.4 tick each): the time it takes to dig it and the swimmer's way to it
        uint32_t life_ticks{3600};           // a finished bridge tile lasts this long (180 s, RE 5.3 and 5.37)
        uint32_t refresh_ticks{40};          // the map as it is now is searched this often (the planner, the closing)
        uint32_t close_margin{150};          // a new trip needs this many ticks of bridge life more than it takes (besides the latency of the bot's orders)
        uint32_t escape_margin{30};          // an ant should be off a bridge tile this many ticks before it goes
        uint32_t trigger_extra{80};          // an ant on a bridge is sent off when the time to spare on its best way off is below one look plus this
        uint32_t retire_life{500};           // a bridge whose oldest tile has less life than this (plus the latency) is retired: no pile is worked over it
        uint32_t hot_life{220};              // a bridge whose oldest tile has less life than this (plus the latency) is hot: the ants near its ends are stopped (see "hot bridges")
        uint32_t renew_life{900};            // the next bridge to a pile is planned when the one it has will be retired within this many ticks
        uint32_t tie_cost{20};               // a way over retired bridges is counted when it costs at most this much more than the best one (the engine may take it)
        uint32_t blacklist_ticks{600};       // a tile that could not be dug is left alone this long
        uint32_t swimmers{1};                // the Swimmers the bot wants (the power-up task takes them from the map)
        uint32_t builders{1};                // Swimmers that dig at a time
        uint32_t min_ticks_left{2400};       // no Swimmer is wanted and no bridge planned with less than this left on the clock
        uint32_t min_units{3};               // no bridge for a pile with fewer units than this
        uint32_t bridge_ants{1};             // ants that work piles over one bridge at a time (see "traffic" above)
        uint32_t max_bridges{3};             // bridges that carry at the same time (one ant each: the hill banks about one deposit per 100 ticks, so about three ants fill its gate); no new bridge for another pile
        bool guard{true};                    // false: the ants on a bridge and the ants of a closed pile are left alone (the tests that show what the guard prevents)
    };

    IslandTask(TaskId id, Tactics& tactics) : IslandTask(id, tactics, Params{}) {}
    IslandTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "islands"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;

    /// The economy whose ants the task watches (which ant works which pile): set by the bot, null when there is none
    void attach(const HarvestTask* economy) noexcept { economy_ = economy; }
    /// The piles that the economy must not send ants to now: reachable, but over a bridge that will not last a round trip (see the top)
    const std::set<uint32_t>& closed_piles() const noexcept { return closed_; }
    /// The piles over a bridge that may be worked now, and by how many ants (the economy sends no more)
    const std::map<uint32_t, uint32_t>& pile_limits() const noexcept { return limits_; }
    /// True once after the map changed in a way that makes new piles reachable (a chain was finished): the economy asks the map again at once, not at its next period
    bool take_reask() noexcept {
        const bool r = reask_;
        reask_ = false;
        return r;
    }
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    /// Whether anything lies beyond water that a bridge reaches (known after the first look)
    bool active() const noexcept { return active_; }
    const IslandInfo& info() const noexcept { return info_; }
    /// Tiles ordered dug, tiles seen finished, chains finished, orders that failed
    uint32_t tiles_ordered() const noexcept { return tiles_ordered_; }
    uint32_t tiles_finished() const noexcept { return tiles_finished_; }
    uint32_t chains_finished() const noexcept { return chains_finished_; }
    uint32_t failures() const noexcept { return failures_; }
    /// Ants called home because their pile was closed, ants sent off a bridge that was going, ants stopped on land because the way home would not last
    uint32_t recalls() const noexcept { return recalls_; }
    uint32_t escapes() const noexcept { return escapes_; }
    uint32_t holds() const noexcept { return holds_; }
    /// Carriers told to walk home again after a bridge tile went (their paths may have led over it)
    uint32_t repaths() const noexcept { return repaths_; }
    /// Bridges that were planned for a pile that was worked over a bridge that was to be retired soon
    uint32_t renewals() const noexcept { return renewals_; }
    /// Ants of the economy that the guard has now, and what it does with one: -1 none, 0 sends it off a bridge (Escape), 1 calls it home (Home), 2 keeps it where it stands (Hold)
    size_t guarded() const noexcept { return guarded_.size(); }
    int guard_mode(uint32_t ant) const noexcept {
        const auto it = guarded_.find(ant);
        return it == guarded_.end() ? -1 : static_cast<int>(it->second.mode);
    }
    /// The pile that the guard believes the ant works (the economy's order, or the last one: the engine's loop goes back there), -1 for none
    int64_t last_pile(uint32_t ant) const noexcept {
        const auto it = last_pile_.find(ant);
        return it == last_pile_.end() ? -1 : static_cast<int64_t>(it->second);
    }
    /// The remaining life (ticks) the task counts for the finished bridge tile, -1 for a tile that is no finished bridge
    int64_t tile_life(sim::TileCoord tile, uint64_t now) const noexcept;
    /// The tiles of the chain the builder is working on (empty: none), and the ant
    std::vector<sim::TileCoord> chain() const;
    uint32_t builder() const noexcept { return builders_.empty() ? 0u : builders_.begin()->first; }
    /// The shore tiles of the bridges that are hot now
    size_t hot_entries() const noexcept { return hot_entries_.size(); }
    /// The chain the planner would dig now, for the tests: empty when nothing is wanted or nothing is possible
    const std::vector<sim::TileCoord>& planned() const noexcept { return plan_.tiles; }
    uint32_t planned_pile() const noexcept { return plan_.pile; }
    /// The latency (ticks) that the task assumes between a look and the moment an order of it works (the profile's look interval, reaction delay and a margin)
    uint32_t latency() const noexcept { return latency_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    struct Builder {
        std::vector<sim::TileCoord> chain;   // the tiles that are dug, in the order of digging (from the hill outward)
        size_t next{0};                      // the tile that is dug or to be dug
        bool ordered{false};                 // the special order for chain[next] was proposed ...
        uint64_t decided{0};
        uint64_t sent{kPending};             // ... and left at this tick
        bool parked{true};                   // the swimmer rests off the bridge
        sim::TileCoord park{-1, -1};
    };
    struct Plan {
        std::vector<sim::TileCoord> tiles;
        uint32_t pile{0};
        int64_t score{0};
        bool renewal{false};                 // for a pile that is worked over a bridge that will be retired soon
    };
    /// An ant of the economy that the guard has taken: it is sent off a bridge (Escape), called home (Home) or kept where it stands (Hold)
    struct Guarded {
        enum class Mode : uint8_t { Escape, Home, Hold };
        Mode mode{Mode::Escape};
        uint64_t since{0};
        uint64_t ordered{0};                 // the look at which the last order was proposed
        uint32_t pile{~uint32_t{0}};         // the closed pile the ant worked at (none: it was taken on a bridge without one)
        sim::TileCoord target{-1, -1};
    };
    /// What the last refresh knows of the walk from the hill over the map as it is now
    struct Field {
        uint64_t tick{0};
        bool valid{false};
        std::vector<int32_t> cost;           // walking cost from the hill, -1: not connected
        std::vector<int32_t> z;              // the least (life + ticks from the hill) of a bridge tile on the way, kNoLife: none
        std::vector<int32_t> old_cost;       // the same over the land and the bridge tiles that are going (less than ok_life left)
        std::vector<int32_t> old_z;
        std::vector<uint8_t> walk;           // the walkable tiles
    };

    bool start(TaskContext& context);
    bool scan_bridges(const BotView& view, uint64_t now);
    void adopt_builders(TaskContext& context);
    void refresh(TaskContext& context);
    void guard(TaskContext& context);
    void step_builder(TaskContext& context, uint32_t ant, Builder& b);
    bool beyond_food(const BotView& view, const MapInfo& map, uint8_t seat) const;
    bool home_ok(int32_t index) const;
    sim::TileCoord home_tile(const TaskContext& context, bool carrying) const;

    Tactics& tactics_;
    Params params_;
    const HarvestTask* economy_{nullptr};
    bool started_{false};
    bool active_{false};
    IslandInfo info_;
    int32_t hill_comp_{-1};
    int32_t width_{0};
    uint32_t latency_{60};
    uint32_t interval_{20};
    std::map<int32_t, uint64_t> seen_;       // finished bridge tile (y * width + x) -> the tick it was first seen finished
    std::map<uint32_t, Builder> builders_;
    std::map<int32_t, uint64_t> bad_;        // tile index -> until when it is left alone (a dig that failed)
    std::map<uint32_t, Guarded> guarded_;
    std::map<uint32_t, uint32_t> last_pile_;    // ant -> the pile it was last sent to by the economy (the engine's loop takes it back there)
    std::map<uint32_t, uint64_t> idle_since_;   // an ant of the economy that stands on a bridge tile: the first look that saw it so
    std::set<uint32_t> closed_;
    std::map<uint32_t, uint32_t> limits_;
    std::set<uint32_t> holders_;             // the piles that had a bridge to themselves at the last refresh
    Field field_;
    Plan plan_;
    std::vector<int32_t> hot_entries_;       // the shore tiles of the hot bridges (refresh)
    std::map<uint32_t, sim::TileCoord> prev_tile_;   // where each ant of the economy stood at the last look
    uint64_t next_refresh_{0};
    bool reask_{false};
    uint32_t tiles_ordered_{0};
    uint32_t tiles_finished_{0};
    uint32_t chains_finished_{0};
    uint32_t failures_{0};
    uint32_t recalls_{0};
    uint32_t escapes_{0};
    uint32_t holds_{0};
    uint32_t repaths_{0};
    uint32_t renewals_{0};
    bool collapsed_{false};
};

}  // namespace ants::ai
