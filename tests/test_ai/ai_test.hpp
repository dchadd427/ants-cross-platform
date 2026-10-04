#pragma once

// The little harness of the bot suites (the same TEST_CASE / ASSERT_* style as tests/test_net and tests/test_server) and the fixtures that more than one of the
// suites needs: a match on a shipped map, a recording sink, a scripted bot.

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

#include "ants_test_paths.hpp"

namespace ai_test {

inline int g_test_count = 0;
inline int g_test_failures = 0;
inline int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    // ANTS_TEST_FILTER=text runs only the cases whose title contains the text (for working on one test; the suite as run_tests.sh runs it has no filter)
    if (const char* filter = std::getenv("ANTS_TEST_FILTER")) {
        if (name.find(filter) == std::string::npos) return;
    }
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    if (g_test_failures == prev) std::cout << "PASS\n";
}

}  // namespace ai_test

#define TEST_CASE(name) ai_test::run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++ai_test::g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++ai_test::g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

namespace ai_test {

using ants::ai::Bot;
using ants::ai::BotController;
using ants::ai::BotSpec;
using ants::ai::BotView;
using ants::ai::Level;
using ants::ai::Orders;
using ants::ai::Priority;
using ants::sim::Command;
using ants::sim::CommandType;
using ants::sim::TileCoord;

/// A tile in one expression (a braced TileCoord{x, y} cannot be an argument of the assertion macros: the comma splits it)
inline TileCoord tc(int32_t x, int32_t y) { return TileCoord{x, y}; }

inline std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

/// A shipped map, loaded once
inline const ants::assets::LevelData& level_of(const std::string& name) {
    static std::vector<std::pair<std::string, ants::assets::LevelData>> cache;
    for (auto& e : cache) {
        if (e.first == name) return e.second;
    }
    cache.emplace_back();
    cache.back().first = name;
    if (!cache.back().second.load_from_file(maps_dir() + name + ".LVL")) std::cout << "  (cannot load " << name << ")\n";
    return cache.back().second;
}

inline std::vector<uint32_t> ants_of(const ants::sim::SimulationEngine& sim, uint8_t team) {
    std::vector<uint32_t> out;
    for (const auto& a : sim.get_world_state().ants) {
        if (a.player_id == team) out.push_back(a.id);
    }
    return out;
}

/// A sink that records what the controller releases (tick, command) and answers Applied; with `forward` it also applies the command to the simulation
class RecordingSink final : public ants::sim::CommandSink {
public:
    explicit RecordingSink(ants::sim::SimulationEngine& sim, bool forward = false) : sim_(sim), forward_(forward) {}
    ants::sim::CommandResult submit(const Command& c) override {
        log.emplace_back(sim_.current_tick(), c);
        if (forward_) return sim_.apply_command(c);
        ants::sim::CommandResult r;
        r.status = ants::sim::CommandResult::Status::Applied;
        return r;
    }
    std::vector<std::pair<uint64_t, Command>> log;

private:
    ants::sim::SimulationEngine& sim_;
    bool forward_;
};

/// A bot that does what the test tells it to in think() and remembers what became of its commands
class ScriptBot final : public Bot {
public:
    using Think = std::function<void(const BotView&, Orders&)>;
    explicit ScriptBot(Think think = {}) : think_(std::move(think)) {}
    const char* kind() const noexcept override { return "script"; }
    void start(const ants::ai::BotContext& c) override {
        started = true;
        seat = c.seat;
        profile = c.profile;
        rng_seed = c.rng_seed;
        map = c.map;
    }
    void think(const BotView& v, Orders& o) override {
        thought.push_back(v.tick());
        if (think_) think_(v, o);
    }
    void on_command(const Command& c, Fate fate, uint64_t tick) override { fates.push_back({c, fate, tick}); }
    size_t count(Fate f) const {
        size_t n = 0;
        for (const auto& e : fates) n += e.fate == f ? 1u : 0u;
        return n;
    }

    struct Seen {
        Command command;
        Fate fate;
        uint64_t tick;
    };
    bool started{false};
    uint8_t seat{255};
    ants::ai::Profile profile{};
    uint64_t rng_seed{0};
    const ants::ai::MapInfo* map{nullptr};
    std::vector<uint64_t> thought;           // the ticks of every think()
    std::vector<Seen> fates;

private:
    Think think_;
};

// The same little world as the network suites: four hills on a 60 x 60 field, `ants` workers on the grass in front of each
inline void build_world(ants::sim::SimulationEngine& sim, uint32_t seed, uint32_t ants_per_team = 12) {
    sim.init_test_world(60, 60, seed, 720000);
    const TileCoord hills[ants::sim::MAX_PLAYERS] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
    for (uint8_t p = 0; p < ants::sim::MAX_PLAYERS; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    for (uint8_t p = 0; p < ants::sim::MAX_PLAYERS; ++p) {
        const int32_t sx = hills[p].x < 30 ? 1 : -1;                              // towards the middle of the field
        const int32_t sy = hills[p].y < 30 ? 1 : -1;
        for (uint32_t i = 0; i < ants_per_team; ++i) {
            sim.spawn_unit(p, ants::sim::AntType::Worker, TileCoord{hills[p].x + sx * (1 + static_cast<int32_t>(i % 8)), hills[p].y + sy * (6 + static_cast<int32_t>(i / 8))});
        }
    }
}

/// A match on a shipped map for the teams of `roster` (the same seed on every machine)
inline void start_match(ants::sim::SimulationEngine& sim, const std::string& map, uint32_t seed, uint8_t roster) {
    sim.init(level_of(map), seed, roster);
}

/// A food pile as an LVL Block-2 entry describes it: `units` units of `value` points and the stage list {threshold, tile}; returns its index in the engine's table
inline int32_t place_pile(ants::sim::SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value, const std::vector<std::pair<uint16_t, uint16_t>>& stages) {
    ants::sim::FoodObject o;
    o.row = static_cast<uint16_t>(row);
    o.col = static_cast<uint16_t>(col);
    o.units = units;
    o.value = value;
    o.remaining = units;
    for (const auto& st : stages) {
        o.thresholds.push_back(st.first);
        o.stage_tiles.push_back(st.second);
    }
    return sim.grid_mut().add_food_object(std::move(o));
}

inline constexpr uint16_t kPileGone = 0x7FFE;

/// Crackers (tiles 369 .. 372, a 2 x 2 footprint around the anchor, shrinking) with four stages of `units` = 4: 4, 3, 2, 1 units
inline int32_t place_crackers(ants::sim::SimulationEngine& sim, int32_t col, int32_t row, uint16_t value = 25) {
    return place_pile(sim, col, row, 4, value, {{4, 369}, {3, 370}, {2, 371}, {1, 372}, {0, kPileGone}});
}

}  // namespace ai_test

void run_setup_tests();
void run_controller_tests();
void run_net_tests();
void run_view_tests();
void run_map_tests();
void run_arena_tests();
void run_b2fix_tests();
void run_b41_tests();
void run_b41_team_tests();
void run_b41_fight_tests();
void run_b41_gate_tests();
void run_b41_style_tests();
void run_b41_offence_tests();
