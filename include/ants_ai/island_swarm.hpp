#pragma once

// The swarm of the standard bot (docs/BOTS.md, "Islands", "The swarm"; plan.island_flower_link, Hard): the Swimmers of the corner rows are taken in the first minute and then there are none left (twelve
// tokens, three to a corner), but the two flower strips let a Swimmer fall every minute (70 percent of the drops) and an ant that stands beside the drop tile takes it. Every ant that no other
// task holds, that a walk over the bridges that stand reaches the strip from (the crew's spares on the ring half, a Bomber that flew over) and that is not a Swimmer yet goes to the strip and
// waits there beside the drop tile, `stage` ants to a strip, and the nearest of them walks onto the Swimmer when one lies on the drop tile. A typed ant takes the drop as well (it leaves its
// old hat on a free tile beside it). The links to the strips are the flower link's (island_link.hpp: the channels of at most two water tiles from the ring halves, found in the analysis of
// the start), the tiles are dug by its Swimmer when a free ant waits on the ring half. The task stops asking for ants when the bot has `island_swarm_swimmers` Swimmers.
// Idle where no link exists (every map but ISLANDS) and without the plan's switch. It sends moves and pick-ups and nothing else.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/island_info.hpp"
#include "ants_ai/island_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class SwarmTask final : public Task {
public:
    struct Params {
        uint32_t refresh_ticks{20};          // the walks to the drop tiles are searched this often
        uint32_t min_ticks_left{900};        // nothing is asked of an ant with less than this left on the clock
        uint32_t order_gap{80};              // an ant that stands still away from its place is told again after this long
        uint32_t take_wait{240};             // a pick-up that has not ended in a Swimmer after this long is given up (the walk from the place is a tile or two)
        uint32_t max_walk_ticks{1500};       // an ant that is farther than this from a strip (by the walk) is not sent
    };

    SwarmTask(TaskId id, Tactics& tactics) : SwarmTask(id, tactics, Params{}) {}
    SwarmTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
    const char* name() const noexcept override { return "swarm"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;
    /// The island task whose analysis is read (set by the bot)
    void attach(const IslandTask* islands) noexcept { islands_ = islands; }
    void set_params(const Params& params) { params_ = params; }
    const Params& params() const noexcept { return params_; }

    /// An ant that waits at a strip with no pick-up ordered: the expedition takes it for a row on foot (a Swimmer that lies in a row is sure, a drop is not), and the swarm lets it go
    bool spare(uint32_t ant) const noexcept {
        const auto it = staged_.find(ant);
        return it != staged_.end() && it->second.picking == 0;
    }
    void give_up(uint32_t ant, AntLedger& ledger) {
        if (staged_.erase(ant) != 0) ledger.release(ant, id());
    }

    // ---- for the tests and the reports ----
    /// Ants that wait at a strip now, pick-ups ordered, Swimmers made (an ant of the task that is a Swimmer now), ants sent to a strip in all
    size_t staged() const noexcept { return staged_.size(); }
    uint32_t pickups() const noexcept { return pickups_; }
    uint32_t made() const noexcept { return made_; }
    uint32_t sent() const noexcept { return sent_; }
    /// The tick of the first and of the third Swimmer made (0: not yet)
    uint64_t first_made() const noexcept { return first_made_; }

private:
    struct Place {
        size_t link{0};                      // Tactics::flower_links index
        sim::TileCoord spot{-1, -1};         // the tile beside the drop tile that the ant waits on
        uint64_t ordered{0};                 // the look of the last walk order (0: none yet)
        uint64_t picking{0};                 // the look at which a pick-up of the drop was ordered (0: none)
        sim::AntType before{sim::AntType::Worker};
    };
    const std::vector<int32_t>& field_for(TaskContext& context, size_t link);
    sim::TileCoord free_spot(TaskContext& context, size_t link, const std::vector<int32_t>& field, sim::TileCoord from, uint32_t self) const;
    void release_all(AntLedger& ledger);

    Tactics& tactics_;
    Params params_;
    const IslandTask* islands_{nullptr};
    std::map<uint32_t, Place> staged_;       // ant -> where it waits
    std::vector<std::vector<int32_t>> fields_;   // by link: the walking cost onto the drop tile from every tile, searched at fields_tick_
    uint64_t fields_tick_{0};
    bool fields_valid_{false};
    uint32_t pickups_{0};
    uint32_t made_{0};
    uint32_t sent_{0};
    uint64_t first_made_{0};
};

}  // namespace ants::ai
