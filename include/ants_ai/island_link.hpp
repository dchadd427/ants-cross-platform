#pragma once

// The flower link of the standard bot (docs/BOTS.md, "Islands", "The flower link"; plan.island_flower_link, Hard): the crew that is flown to the Swimmers lands on a half of the ring round the
// lake, and a flower strip lies one or two water tiles from that half. A ferry Swimmer digs those tiles once (a special order onto one water tile at a time, from the ring outward), an idle
// plain ant of the crew walks over them (PowerUpTask::watch_link) and takes the Swimmer that the flower lets fall next to it. The tiles are found in the analysis of the start (IslandInfo): the
// channel between a component that holds a flower and a component that holds none and is no hill's island, at most `max_tiles` long. A finished tile lasts 3,600 ticks and is dug again only
// after it has gone (a finished tile is demolished by a special order, never dug again), and only while a free plain ant stands on the ring half and waits to cross.
// Idle where no such channel exists (every map but ISLANDS) and without the plan's switch. It sends the Swimmer's dig orders and nothing else.

#include <cstdint>
#include <map>
#include <vector>

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
        uint32_t min_life{300};              // a link whose oldest tile has less life than this counts as gone (nobody starts across it)
    };

    FlowerLinkTask(TaskId id, Tactics& tactics) : FlowerLinkTask(id, tactics, Params{}) {}
    FlowerLinkTask(TaskId id, Tactics& tactics, const Params& params) : Task(id), tactics_(tactics), params_(params) {}
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

private:
    static constexpr uint64_t kPending = ~uint64_t{0};
    void start(TaskContext& context);
    void release_digger(TaskContext& context);

    Tactics& tactics_;
    Params params_;
    const IslandTask* islands_{nullptr};
    bool started_{false};
    std::map<int32_t, uint64_t> seen_;       // finished tile (y * width + x) -> the tick it was first seen finished
    std::map<int32_t, uint64_t> bad_;        // tile -> until when it is left alone
    uint64_t next_look_{0};
    uint32_t digger_{0};
    int32_t digger_link_{-1};
    bool ordered_{false};
    uint64_t decided_{0};
    uint64_t sent_{kPending};
    sim::TileCoord ordered_tile_{-1, -1};
    uint32_t tiles_ordered_{0};
    uint32_t tiles_finished_{0};
    uint32_t failures_{0};
};

}  // namespace ants::ai
