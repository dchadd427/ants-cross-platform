#pragma once

// The miner's flight (docs/BOTS.md, "Islands"; plan.island_fly_miner, Hard): once the expedition is over, one free Bomber is flown to the island of the ring pile (the food that no walker
// reaches and that the Swimmers of several hills carry from), on bombs of its own, as the Bomber of the expedition flies on: it plants on B from a tile beside it, walks to the shore tile S and steps
// onto the bomb, and is thrown onto L (island_info.hpp, Flight). From there MineTask lays mines round the pile (docs/BOTS.md, "The mines at the ring pile"). The flight is retried after a dud
// (an ant burning on B), at most four times; it is given up after 1,500 ticks. The task reads only the view and the analysis of the start; it sends plain moves and the plant order.

#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/island_info.hpp"
#include "ants_ai/island_expedition.hpp"
#include "ants_ai/island_tasks.hpp"
#include "ants_ai/tactics.hpp"
#include "ants_ai/tasks.hpp"

namespace ants::ai {

class MinerFlightTask final : public Task {
public:
    MinerFlightTask(TaskId id, Tactics& tactics) : Task(id), tactics_(tactics) {}
    const char* name() const noexcept override { return "miner_flight"; }
    void step(TaskContext& context) override;
    void finish(AntLedger& ledger) override;
    /// The island task whose analysis is read and the expedition that must be over (set by the bot)
    void attach(const IslandTask* islands, const ExpeditionTask* expedition) noexcept {
        islands_ = islands;
        expedition_ = expedition;
    }

    // ---- for the tests and the reports ----
    uint32_t started() const noexcept { return started_; }
    uint32_t hops() const noexcept { return hops_; }
    uint32_t planted() const noexcept { return planted_; }
    uint32_t landed() const noexcept { return landed_; }
    uint32_t gave_up() const noexcept { return gave_up_; }
    bool flying() const noexcept { return job_.ant != 0; }
    uint64_t first_start() const noexcept { return first_start_; }      // the tick of the first flight begun and of the first landing (0: never)
    uint64_t first_landing() const noexcept { return first_landing_; }

private:
    struct Job {
        uint32_t ant{0};
        int32_t target{-1};                  // the island to reach
        int32_t here{-1};                    // the island the ant is on (a flight may be one of several)
        bool chosen{false};
        Flight flight;
        sim::TileCoord station{-1, -1};
        uint64_t begun{0};
        uint64_t ordered{0};
        uint64_t plant_at{0};
        uint64_t hop_at{0};
        uint32_t attempts{0};
    };
    void end_job(TaskContext& context, bool landed);
    bool begin(TaskContext& context);
    bool choose(TaskContext& context, const AntView& ant);
    void drive(TaskContext& context);

    Tactics& tactics_;
    const IslandTask* islands_{nullptr};
    const ExpeditionTask* expedition_{nullptr};
    Job job_;
    std::map<uint32_t, uint64_t> shunned_;   // Bomber -> not before this tick
    uint32_t started_{0};
    uint32_t hops_{0};
    uint32_t planted_{0};
    uint32_t landed_{0};
    uint32_t gave_up_{0};
    uint64_t first_start_{0};
    uint64_t first_landing_{0};
};

}  // namespace ants::ai
