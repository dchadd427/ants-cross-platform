#pragma once

// The flower link of the standard bot (docs/BOT_ISLANDS.md, "Islands", "The flower link"; plan.island_flower_link, Hard): the crew that is flown to the Swimmers lands on a half of the ring round the
// lake, and a flower strip lies one or two water tiles from that half. A ferry Swimmer digs those tiles (a special order onto one water tile at a time, from the ring outward), and the spare
// ants of the crew walk over them to wait at the strip and take the Swimmer that the flower lets fall next to them (SwarmTask). The tiles are found in the analysis of the start (IslandInfo): the
// channels between a component that holds a flower and a component that holds none and is no hill's island, at most `max_tiles` long: every strip within reach of a ring half is a link. A finished tile lasts 3,600 ticks and is dug again only
// after it has gone (a finished tile is demolished by a special order, never dug again), and only while a free plain ant stands on the ring half and waits to cross.
// With plan.island_link_cost the link that is dug is the cheapest one that has a waiting ant (the tiles still to dig, the nearest Swimmer, the walk of the waiting ant to the strip, the foreign
// ants at the strip), not the first of the list. The same Swimmer digs the bridges of the stranded ants of island_bridge.hpp (plan.island_return, island_feeder, island_power_bridge) when no ant waits for a link.
// Idle where no such channel exists (every map but ISLANDS) and without the plan's switch. It sends the Swimmer's dig orders and, for the ants that a bridge brought home, a walk to the hill.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/island_bridge.hpp"
#include "ants_ai/island_info.hpp"
#include "ants_ai/island_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class FlowerLinkTask final : public Task {
public:
    struct Params {
        uint32_t max_tiles{2};               // a channel of more tiles than this is no link
        uint32_t min_pile_worth{1000};       // the island that is linked holds a pile worth this much (points: units times value): the ring halves of ISLANDS, not a satellite
        uint32_t life_ticks{3600};           // a finished tile lasts this long
        uint32_t refresh_ticks{20};          // the map is looked at this often
        uint32_t blacklist_ticks{600};       // a tile that could not be dug is left alone this long
        uint32_t min_ticks_left{1800};       // nothing is dug with less than this left on the clock
        uint32_t dig_ticks{400};             // an order for a tile that has not made it a bridge after this long is given up
        uint32_t job_max_swim{30};           // a bridge job is not begun when the nearest Swimmer is farther than this many tiles from its first tile
        uint32_t chain_max_swim{70};         // ... the chains of a Raid and of a Home job (a Thief's or a Fire Ant's way over several islands) are worth a longer swim: the Swimmers of a seat that has no food left to ferry stand far away
        uint32_t blocked_ticks{200};         // a bridge job whose next tile has an ant on it this long leaves the tile alone (blacklist_ticks) and gives the Swimmer back
        uint32_t min_life{300};              // a link whose oldest tile has less life than this counts as gone (nobody starts across it)
        uint32_t dig_first_ticks{34};        // (the cost of a link, plan.island_link_cost) a tile next to the shore takes this long to dig ...
        uint32_t dig_next_ticks{54};         // ... and each one after it this long (the Swimmer steps onto the finished tile first)
        uint32_t crowd_radius{7};            // an ant of another team within this many tiles of the strip's drop tile ...
        uint32_t crowd_ticks{120};           // ... adds this many ticks to the cost of the link (the race for a drop is lost by a few ticks) ...
        uint32_t crowd_cap{4};               // ... for at most this many ants
    };

    FlowerLinkTask(TaskId id, Tactics& tactics) : FlowerLinkTask(id, tactics, Params{}) {}
    FlowerLinkTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params), planner_(id) {}
    const char* name() const noexcept override { return "flower_link"; }
    void step(TaskContext& context) override;
    void on_command(const sim::Command& command, Bot::Fate fate, uint64_t tick) override;
    void finish(AntLedger& ledger) override;
    /// The island task whose analysis is read (set by the bot)
    void attach(const IslandTask* islands) noexcept { islands_ = islands; }
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    // ---- for the tests and the reports ----
    size_t links() const noexcept { return tactics_.flower_links.size(); }
    const std::vector<Tactics::FlowerLink>& link_list() const noexcept { return tactics_.flower_links; }
    uint32_t tiles_ordered() const noexcept { return tiles_ordered_; }
    uint32_t tiles_finished() const noexcept { return tiles_finished_; }
    uint32_t failures() const noexcept { return failures_; }
    /// The Swimmer that digs now (0: none)
    uint32_t digger() const noexcept { return digger_; }
    /// The planner of the bridges for stranded ants (the tiles it has had dug, the ants it sent home)
    const BridgePlanner& bridges() const noexcept { return planner_; }

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    static constexpr int64_t kJobKey = int64_t{1} << 40;     // the keys of the bridge jobs lie above the numbers of the links
    void start(TaskContext& context);
    void release_digger(TaskContext& context);
    /// The nearest Swimmer that carries nothing, has the hit points and is free for this task, or null (`tile`: where it is wanted)
    const AntView* nearest_digger(TaskContext& context, sim::TileCoord tile) const;
    /// What a link costs, in ticks (plan.island_link_cost): the tiles to dig, the Swimmer's swim to the first one, the waiting ant's walk to the strip and the foreign ants at the strip
    int64_t link_cost(TaskContext& context, const Tactics::FlowerLink& link, const AntView& waiting) const;

    Tactics& tactics_;
    Params params_;
    const IslandTask* islands_{nullptr};
    BridgePlanner planner_;
    int32_t digger_kind_{-1};                // the kind of the bridge job that the digger works for (BridgeJob::Kind), -1: a flower link
    bool started_{false};
    std::map<int32_t, uint64_t> seen_;       // finished tile (y * width + x) -> the tick it was first seen finished
    std::map<int32_t, uint64_t> bad_;        // tile -> until when it is left alone
    uint64_t next_look_{0};
    uint32_t digger_{0};
    int64_t digger_link_{-1};                // the link (its number in the list) or the job (kJobKey and its key) that the digger works for
    bool ordered_{false};
    uint64_t decided_{0};
    uint64_t blocked_since_{0};              // the look at which the tile to dig was first seen with an ant on it (0: it is free)
    uint64_t sent_{kPending};
    sim::TileCoord ordered_tile_{-1, -1};
    uint32_t tiles_ordered_{0};
    uint32_t tiles_finished_{0};
    uint32_t failures_{0};
};

}  // namespace ants::ai
