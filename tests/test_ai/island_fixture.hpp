#pragma once

// The fixture of the island tests (AI15, AI16): a match on a shipped map with a standard bot on every seat, driven tick by tick through a BotController that forwards to the engine, and a watch
// on what drowns: an ant that starts to drown on a tile that was a finished bridge a tick before has had its bridge collapse under it (the guard's failure), any other drowning is a flight,
// a blow or a swim into water that is not the bridges' doing.

#include <functional>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ai_test.hpp"
#include "ants_ai/island_tasks.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_ai/tactics.hpp"

namespace island_test {

using namespace ai_test;
using ants::ai::IslandTask;
using ants::ai::LevelPlan;
using ants::ai::StandardBot;

/// The sink of a room as the engine sees it (the arena's, src/ants_ai/arena.cpp): a command released now reaches the engine at the first turn boundary (every second tick) at least `delay`
/// ticks later, in canonical order. `flush` is called after every tick, before the bots look.
class LatencySink final : public ants::sim::CommandSink {
public:
    LatencySink(ants::sim::SimulationEngine& sim, uint32_t delay) : sim_(sim), delay_(delay) {}
    ants::sim::CommandResult submit(const ants::sim::Command& c) override {
        log.emplace_back(sim_.current_tick(), c);
        pending_.push_back(Waiting{sim_.current_tick() + delay_, c});
        ants::sim::CommandResult r;
        r.status = ants::sim::CommandResult::Status::Applied;
        return r;
    }
    /// The tick at which a command released at `now` reaches the engine: the first even tick at least `delay` later (the arena's DelayedSink)
    uint64_t applied_at(uint64_t now) const override {
        const uint64_t due = now + delay_;
        return due + due % 2 + skew_per_command * log.size();
    }
    uint64_t skew_per_command{0};                                                                     // a sink that is wrong about its own clock (the test of what the bots do when their windows are missed): every command released so far adds this to what applied_at says
    void flush() {
        const uint64_t now = sim_.current_tick();
        if (now % 2 != 0 || pending_.empty()) return;
        std::vector<ants::sim::Command> due;
        std::vector<Waiting> later;
        for (Waiting& w : pending_) {
            if (w.due <= now) due.push_back(std::move(w.command));
            else later.push_back(std::move(w));
        }
        pending_ = std::move(later);
        ants::sim::canonical_order(due);
        for (const ants::sim::Command& c : due) sim_.apply_command(c);
    }
    /// A command that is lost on its way: what has been released and has not reached the engine yet is never applied (the bot was told that it left)
    void drop_pending() { pending_.clear(); }
    std::vector<std::pair<uint64_t, ants::sim::Command>> log;                                        // what was released (tick, command), as RecordingSink's

private:
    struct Waiting {
        uint64_t due;
        ants::sim::Command command;
    };
    ants::sim::SimulationEngine& sim_;
    uint32_t delay_;
    std::vector<Waiting> pending_;
};

struct Match {
    ants::sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    uint8_t roster{0};
    std::vector<uint8_t> prev_bridge;                    // the finished bridge tiles of the tick before
    std::set<uint32_t> drowning;                         // the ants that have been seen to drown
    int collapsed[4] = {0, 0, 0, 0};                     // per team: ants that started to drown on a tile that was a finished bridge a tick before
    int other_drowned[4] = {0, 0, 0, 0};                 // per team: ants that drowned elsewhere (a blow of an enemy that flung the ant into the water included: fought)
    int fought[4] = {0, 0, 0, 0};                        // per team: of those, ants with an enemy ant within three tiles when they started to drown
    uint32_t swimmers[4] = {0, 0, 0, 0};                 // the ids of the Swimmers that were given
    bool expedition{false};                              // the bots fly a crew to the Swimmers (the level's plan has it on; the tests of the island task switch it off, they give the Swimmer)
    bool ferry{false};                                   // the Swimmers that dig no bridge carry food (the level's plan has it on)
    uint32_t builders{1};                                // Swimmers that dig bridges in a plan made by hand (the labs of the island task need one; the level plans have none: they ferry)
    bool styled{false};                                  // the bots of the registry, as the arena and a room play them (a style drawn per seat, the plan of the level with `tweak` on top); else the plan alone
    uint32_t latency{0};                                 // the ticks that a released command takes to reach the engine (the arena's default is 3; 0: at once)
    std::unique_ptr<LatencySink> delayed;                // the sink, when there is a latency (then `sink` is not used)

    /// `swimmer_seats`: the seats that get a Swimmer on the first tile of their queue row (what a bomb flight or a flower would bring; the bot's own way to one is tested apart)
    /// `plan_of`: when set, the plan of the bot of the seat (the tests that need a bot without its builder, or with the guard off); else the level's
    /// `prepare`: when set, what the test does to the engine before the bots are seated (a pile of its own: the analysis of the map takes it in)
    void init(const std::string& map, uint32_t seed, uint8_t seats, Level level, uint8_t swimmer_seats, const std::function<void(LevelPlan&)>& tweak = {},
              const std::function<void(ants::sim::SimulationEngine&)>& prepare = {}) {
        roster = seats;
        start_match(sim, map, seed, seats);
        if (prepare) prepare(sim);
        const ants::ai::MapInfo probe(sim);
        for (uint8_t t = 0; t < 4; ++t) {
            if (((swimmer_seats >> t) & 1u) == 0 || !probe.hill(t).present || probe.hill(t).starts.empty()) continue;
            swimmers[t] = sim.spawn_unit(t, ants::sim::AntType::Swimmer, probe.hill(t).starts[0]);
        }
        if (latency > 0) delayed = std::make_unique<LatencySink>(sim, latency);
        else sink = std::make_unique<RecordingSink>(sim, true);
        ants::sim::CommandSink& out = delayed != nullptr ? static_cast<ants::sim::CommandSink&>(*delayed) : static_cast<ants::sim::CommandSink&>(*sink);
        ctl = std::make_unique<BotController>(sim, seed);
        std::string why;
        for (uint8_t t = 0; t < 4; ++t) {
            if (((seats >> t) & 1u) == 0) continue;
            BotSpec spec;
            spec.seat = t;
            spec.kind = "standard";
            spec.level = level;
            std::unique_ptr<StandardBot> bot;
            if (styled) {
                bot = std::make_unique<StandardBot>(level, ants::ai::Style::Random, [tweak](LevelPlan& p) { if (tweak) tweak(p); });
            } else {
                LevelPlan plan = ants::ai::plan_for(level);
                plan.island_expedition = expedition;
                plan.island_ferry = ferry;
                plan.island_builders = builders;
                if (tweak) tweak(plan);
                bot = std::make_unique<StandardBot>(plan);
            }
            if (!ctl->add(spec, std::move(bot), out, why)) std::cout << "  (cannot seat " << int(t) << ": " << why << ")\n";
        }
        prev_bridge.assign(sim.grid().cells().size(), 0);
    }

    void tick() {
        sim.tick();
        if (delayed != nullptr) delayed->flush();
        ctl->on_tick(sim);
        sim.clear_news_events();                                                                          // (after the bots looked, as the arena does: they read the news of the tick)
        sim.clear_audio_events();
        const ants::sim::Grid& grid = sim.grid();
        for (const auto& a : sim.get_world_state().ants) {
            if (a.state != ants::sim::UnitState::Drowning || !drowning.insert(a.id).second) continue;
            const size_t idx = static_cast<size_t>(a.tile_y) * grid.width() + static_cast<size_t>(a.tile_x);
            if (a.player_id >= 4) continue;
            (prev_bridge[idx] != 0 ? collapsed : other_drowned)[a.player_id] += 1;
            if (prev_bridge[idx] != 0) continue;
            for (const auto& o : sim.get_world_state().ants) {
                if (o.player_id != a.player_id && std::abs(o.tile_x - a.tile_x) <= 3 && std::abs(o.tile_y - a.tile_y) <= 3) {
                    fought[a.player_id] += 1;
                    break;
                }
            }
        }
        for (size_t i = 0; i < prev_bridge.size(); ++i) prev_bridge[i] = grid.cells()[i].has_completed_bridge() ? 1 : 0;
    }

    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) tick();
    }

    /// The commands that the bots released (tick, command)
    const std::vector<std::pair<uint64_t, ants::sim::Command>>& log() const { return delayed != nullptr ? delayed->log : sink->log; }
    StandardBot* bot(uint8_t seat) { return dynamic_cast<StandardBot*>(ctl->bot(seat)); }
    const IslandTask& island(uint8_t seat) { return bot(seat)->islands(); }
    int total_collapsed() const { return collapsed[0] + collapsed[1] + collapsed[2] + collapsed[3]; }
    /// Ants that drowned for a reason of their own bot's making (a bridge that went under them, a flight): not the blows of an enemy. An ant that drowns with an enemy within three tiles counts
    /// as fought, whoever threw it: a drowning in a crowded landing is not seen here when an enemy stands near (the matches of four bots, AI16, print how many were fought: none today). The
    /// flights are covered by the matches of one seat, with no enemy on the map (AI17.3, AI17.10)
    int total_unforced() const {
        int n = 0;
        for (int t = 0; t < 4; ++t) n += collapsed[t] + other_drowned[t] - fought[t];
        return n;
    }
};

}  // namespace island_test
