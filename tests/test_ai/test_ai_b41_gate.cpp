// The gate task of the standard bot (B4-1, AI10.x): "guiding for eating" (the owner's playbook), measured against the engine's own queue in a hand-made hill with a pile north of it,
// quick enough for suite 2.20. The engine's rules that it rests on are documented in docs/BOTS.md ("The gate").
//
//   AI10.1  the throughput: with the gate guided by hand a hill banks much more than with the engine's queue (8 workers), and never less (3 workers); the median gap between deposits
//           at saturation, and the far waiting tile stays empty
//   AI10.2  the task's rules: the plans (Hard only), the economy's own rescue is off, the refused click is recovered from, more carriers than the doorstep holds are parked and released
//   AI10.3  the surroundings: a rock on the queue row or on the buffer tile is never ordered onto, and the gate still works
#include "ai_test.hpp"
#include "b41_helpers.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

namespace {

struct GateScene {
    sim::SimulationEngine sim;
    TileCoord hill{26, 26};
    void build(size_t workers, int pile_rows = 13, uint32_t ticks = 14400, uint32_t seed = 5) {
        sim.init_test_world(60, 60, seed, ticks * sim::TICK_MS);
        sim.grid_mut().set_anthill(0, hill);
        sim.grid_mut().set_anthill(1, TileCoord{4, 4});
        sim.grid_mut().set_anthill(2, TileCoord{54, 4});
        sim.grid_mut().set_anthill(3, TileCoord{54, 54});
        place_pile(sim, hill.x + 1, hill.y - pile_rows, 900, 25, {{900, 369}, {675, 370}, {450, 371}, {225, 372}, {0, kPileGone}});
        for (size_t i = 0; i < workers; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{hill.x - 3 + static_cast<int32_t>(i % 6), hill.y - 6 - static_cast<int32_t>(i / 6)});
    }
};

// The Hard plan of the tests: only the economy and the gate (nothing else of the level's tactics is in a scene without enemies, but the plan is explicit about the gate)
LevelPlan gate_plan(bool gate, uint32_t staged = 8) {
    LevelPlan p = plan_for(Level::Hard);
    p.gate = gate;
    p.gate_max_staged = staged;
    return p;
}

struct Run {
    int32_t score{0};
    std::vector<uint64_t> gaps;          // the ticks between two deposits
    size_t ring_samples{0};              // looks at which a carrier stood on the far waiting tile of the hill (the engine's queue)
    size_t samples{0};
    uint32_t clicks{0};
    uint32_t parks{0};
    uint32_t rescues{0};
    uint32_t failures{0};
};

Run play(GateScene& scene, const LevelPlan& plan, uint64_t ticks, uint64_t warm_up = 1200) {
    Run r;
    Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 8);
    int32_t last = 0;
    uint64_t last_tick = 0;
    const TileCoord ring{scene.hill.x - 1, scene.hill.y + 3};
    for (uint64_t t = 0; t < ticks; ++t) {
        rig.tick();
        const int32_t s = scene.sim.get_player_score(0);
        if (s > last) {
            if (scene.sim.current_tick() > warm_up && last_tick > warm_up) r.gaps.push_back(scene.sim.current_tick() - last_tick);
            last_tick = scene.sim.current_tick();
            last = s;
        }
        if (scene.sim.current_tick() > warm_up && scene.sim.current_tick() % 4 == 0) {
            ++r.samples;
            bool at_ring = false;
            for (const sim::AntSnapshot& a : scene.sim.get_world_state().ants) {
                if (a.player_id == 0 && a.is_holding && std::abs(a.tile_x - ring.x) <= 1 && std::abs(a.tile_y - ring.y) <= 1) at_ring = true;
            }
            r.ring_samples += at_ring ? 1u : 0u;
        }
    }
    r.score = scene.sim.get_player_score(0);
    r.clicks = rig.as<StandardBot>().gate().entrance_clicks();
    r.parks = rig.as<StandardBot>().gate().parks();
    r.rescues = rig.as<StandardBot>().harvest().rescues();
    r.failures = rig.as<StandardBot>().gate().user_failures();
    return r;
}

uint64_t median(std::vector<uint64_t> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

void run_b41_gate_tests() {
    TEST_CASE("AI10.1 The Gate Guided By Hand Banks Much More Than The Engine's Queue: With 8 Workers On A Pile 13 Tiles North Of The Hill The Score Of 3000 Ticks Is At Least 50 Percent Higher, The Median Gap Between Deposits At Most 70 Ticks (The Engine's Queue: 90 Or More), The Far Waiting Tile Stays (Almost) Empty; With 3 Workers It Is 15 Percent Higher")
    {
        for (const size_t workers : {8u, 3u}) {
            GateScene plain_scene;
            plain_scene.build(workers);
            const Run plain = play(plain_scene, gate_plan(false), 4200);
            GateScene gate_scene;
            gate_scene.build(workers);
            const Run gated = play(gate_scene, gate_plan(true), 4200);
            ASSERT_TRUE(plain.score > 0);
            if (workers == 8) {
                ASSERT_TRUE(gated.score * 100 >= plain.score * 150);
                ASSERT_TRUE(median(gated.gaps) <= 70);
                ASSERT_TRUE(median(plain.gaps) >= 90);
                ASSERT_TRUE(gated.ring_samples * 50 <= gated.samples);                                      // under 2 percent of the looks
                ASSERT_TRUE(plain.ring_samples * 3 >= plain.samples);                                       // the engine's queue stands there a third of the time or more
                ASSERT_TRUE(gated.clicks >= 40);
                ASSERT_EQ(plain.clicks, 0u);
            } else {
                ASSERT_TRUE(gated.score * 100 >= plain.score * 115);
            }
        }
    } TEST_END();

    TEST_CASE("AI10.2 The Gate Task's Rules: Only Hard Guides The Gate (Easy And Medium Keep The Engine's Flow And Click Nothing); The Economy's Own Rescue Of A Carrier That Stands Far From The Hill Is Off While The Gate Runs (The Gate Takes The Carrier Over And It Delivers); More Carriers Than The Doorstep Holds Are Parked And Released One By One, Without A Deadlock")
    {
        ASSERT_TRUE(plan_for(Level::Hard).gate);
        ASSERT_FALSE(plan_for(Level::Medium).gate);
        ASSERT_FALSE(plan_for(Level::Easy).gate);
        for (const Level level : {Level::Easy, Level::Medium}) {
            GateScene scene;
            scene.build(8);
            Rig rig(scene.sim, 0, level, std::make_unique<StandardBot>(plan_for(level)), 4, 8);
            rig.run(2400);
            ASSERT_EQ(rig.as<StandardBot>().gate().entrance_clicks(), 0u);
            ASSERT_EQ(rig.as<StandardBot>().gate().takeovers(), 0u);
            ASSERT_TRUE(scene.sim.get_player_score(0) > 0);
        }
        // a carrier far from the hill that stands idle with its food: the economy's rescue sends it home (40 ticks, nobody queueing); with the gate the gate task takes it over and the
        // economy's rescue is off
        for (const bool gate : {false, true}) {
            GateScene scene;
            scene.build(0);
            const uint32_t carrier = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 40});
            scene.sim.get_unit(carrier).pick_up_food(1, 25);
            const Run r = play(scene, gate_plan(gate), 900, 0);
            ASSERT_TRUE(r.score >= 25);                                                                       // it delivered, in both runs (and went on harvesting)
            if (gate) {
                ASSERT_EQ(r.rescues, 0u);
                ASSERT_TRUE(r.clicks >= 1);
            } else {
                ASSERT_TRUE(r.rescues >= 1);
                ASSERT_EQ(r.clicks, 0u);
            }
        }
        // a doorstep that holds two: with eight workers the others are parked where they stand and released one by one; nothing deadlocks
        {
            GateScene plain_scene;
            plain_scene.build(8);
            const Run plain = play(plain_scene, gate_plan(false), 4200);
            GateScene scene;
            scene.build(8);
            const Run r = play(scene, gate_plan(true, 2), 4200);
            ASSERT_TRUE(r.parks > 0);
            ASSERT_TRUE(r.score * 100 >= plain.score * 80);
            ASSERT_TRUE(r.clicks >= 30);
        }
    } TEST_END();

    TEST_CASE("AI10.3 The Surroundings Of The Hill: A Rock On A Tile Of The Queue Row Or On The Buffer Tile Is Never Ordered Onto, The Gate Still Works; With Two Queue Tiles Blocked There Is No Room To Stage And The Engine's Flow Stays")
    {
        struct Case {
            std::vector<TileCoord> rocks;
            bool guided;
        };
        const TileCoord hill{26, 26};
        const std::vector<Case> cases = {
            {{TileCoord{hill.x + 2, hill.y - 1}}, true},                                    // Q2 is a rock
            {{TileCoord{hill.x, hill.y - 1}}, true},                                        // Q0 is a rock
            {{TileCoord{hill.x + 1, hill.y - 4}}, true},                                    // the buffer tile is a rock
            {{TileCoord{hill.x, hill.y - 1}, TileCoord{hill.x + 2, hill.y - 1}}, false},    // two queue tiles: no gate
        };
        for (const Case& k : cases) {
            GateScene plain_scene;
            plain_scene.build(8);
            for (const TileCoord& r : k.rocks) plain_scene.sim.set_terrain(r.x, r.y, sim::TERRAIN_OBSTACLE);
            const Run plain = play(plain_scene, gate_plan(false), 4200);
            GateScene scene;
            scene.build(8);
            for (const TileCoord& r : k.rocks) scene.sim.set_terrain(r.x, r.y, sim::TERRAIN_OBSTACLE);
            Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            rig.run(4200);
            for (const auto& e : rig.proposed) {
                for (const TileCoord& r : k.rocks) ASSERT_FALSE(e.second.tile_x == r.x && e.second.tile_y == r.y);
            }
            const int32_t score = scene.sim.get_player_score(0);
            if (k.guided) {
                ASSERT_TRUE(rig.as<StandardBot>().gate().entrance_clicks() >= 30);
                ASSERT_TRUE(score * 100 >= plain.score * 130);
            } else {
                ASSERT_EQ(rig.as<StandardBot>().gate().entrance_clicks(), 0u);
                ASSERT_TRUE(score * 100 >= plain.score * 95);
            }
        }
    } TEST_END();
}
