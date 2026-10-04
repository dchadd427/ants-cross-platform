// The gate task of the standard bot (B4-1, AI10.x): "guiding for eating" (the owner's playbook), measured against the engine's own queue in a hand-made hill with a pile north of it,
// quick enough for suite 2.20. The engine's rules that it rests on are documented in docs/BOTS.md ("The gate").
//
//   AI10.1  the throughput: with the gate guided by hand a hill banks much more than with the engine's queue (8 workers), and never less (3 workers); the median gap between deposits
//           at saturation, and the far waiting tile stays empty
//   AI10.2  the task's rules: the plans (Hard only), the economy's own rescue is off, the refused click is recovered from, more carriers than the doorstep holds are parked and released
//   AI10.3  the surroundings: a rock on the queue row or on the buffer tile is never ordered onto, and the gate still works
//   AI10.4  a hill that the gate cannot guide (in the top rows: no buffer tile) keeps the economy's rescue; both are off only while the gate guides
//   AI10.5  a click that the controller refused (Fate::Filtered) blocks its tile for 900 ticks: a queue-row slot, the buffer tile, the entrance (no gate until it is over)
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

// The hill in the top rows of the map: the gate's buffer tile is searched 2 to 5 rows north of the hill and there is no such row; the pile is south of it
struct TopScene : GateScene {
    void build_top() {
        hill = TileCoord{26, 1};
        sim.init_test_world(60, 60, 5, 14400u * sim::TICK_MS);
        sim.grid_mut().set_anthill(0, hill);
        sim.grid_mut().set_anthill(1, TileCoord{4, 54});
        sim.grid_mut().set_anthill(2, TileCoord{54, 54});
        sim.grid_mut().set_anthill(3, TileCoord{54, 30});
        place_pile(sim, hill.x + 1, hill.y + 20, 900, 25, {{900, 369}, {675, 370}, {450, 371}, {225, 372}, {0, kPileGone}});
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

    TEST_CASE("AI10.4 A Hill That The Gate Cannot Guide Keeps The Economy's Rescue: In The Top Rows Of The Map There Is No Buffer Tile North Of The Hill, So The Gate Task Does Nothing And A Carrier That Stands Far Away With Its Food Must Still Be Sent Home; The Rescue Is Off Only While The Gate Really Guides")
    {
        {   // a hill in the middle rows with a pile north of it: the gate guides, the economy's rescue is off (AI10.2)
            GateScene scene;
            scene.build(8);
            Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            rig.run(1200);
            ASSERT_TRUE(rig.as<StandardBot>().gate().usable());
            ASSERT_FALSE(rig.as<StandardBot>().harvest().params().rescue);
        }
        {   // the hill in the top rows: the gate cannot guide, the rescue is on, and the carrier that stands idle far from the hill is sent home and banks its food
            TopScene scene;
            scene.build_top();
            const uint32_t carrier = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 18});
            scene.sim.get_unit(carrier).pick_up_food(1, 25);
            const Run r = play(scene, gate_plan(true), 1200, 0);
            ASSERT_EQ(r.clicks, 0u);                                                                          // the gate never guided
            ASSERT_TRUE(r.rescues >= 1);                                                                      // the economy's rescue did
            ASSERT_TRUE(r.score >= 25);                                                                       // and the food is banked
        }
        {   // the same hill with the plan of the engine's flow (no gate): the same result, so the plan with the gate loses nothing there
            TopScene scene;
            scene.build_top();
            const uint32_t carrier = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 18});
            scene.sim.get_unit(carrier).pick_up_food(1, 25);
            const Run r = play(scene, gate_plan(false), 1200, 0);
            ASSERT_TRUE(r.rescues >= 1);
            ASSERT_TRUE(r.score >= 25);
        }
    } TEST_END();

    TEST_CASE("AI10.5 A Refused Click Blocks Its Tile For 900 Ticks (Not Before, Not After): A Queue-Row Slot And The Buffer Tile Are Not Named Again While The Ant Is Taken Over At The Next Look; The Entrance Cannot Be Clicked, So The Gate Does Not Guide (The Engine's Flow And The Economy's Rescue Stay) Until The 900 Are Over")
    {
        for (const int which : {0, 1, 2}) {                                                                // a slot of the queue row, the buffer tile, the entrance
            GateScene scene;
            scene.build(0);
            const TileCoord hill = scene.hill;
            for (int i = 0; i < 8; ++i) {                                                                   // eight carriers, idle with their food, outside the doorstep: one clicks, two take a slot, five the buffer
                const uint32_t id = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{22 + i, 16});
                scene.sim.get_unit(id).pick_up_food(1, 25);
            }
            const MapInfo map(scene.sim);
            const Profile profile = profile_for(Level::Hard);
            const auto is_target = [&](const Command& c) {
                if (c.type != CommandType::GroupMove) return false;
                if (which == 0) return c.tile_y == hill.y - 1 && c.tile_x >= hill.x && c.tile_x <= hill.x + 2;
                if (which == 1) return c.tile_x == hill.x + 1 && c.tile_y == hill.y - 4;
                return c.tile_x == hill.x + 1 && c.tile_y == hill.y + 1;
            };
            GateTask gate(1);
            AntLedger ledger;
            TileCoord refused{-1, -1};
            uint32_t refused_ant = 0;
            uint64_t t0 = 0;
            std::vector<uint64_t> named_again;                // the looks after the refusal at which the refused tile was named
            std::vector<uint64_t> ant_ordered;                // ... and those at which the ant whose click was refused was ordered somewhere
            std::vector<uint64_t> moves;                      // every look after the refusal that put a move into the orders
            std::vector<std::pair<uint64_t, bool>> usable;    // (look, the gate could guide)
            const auto look = [&](uint64_t advance) {
                tick_all(scene.sim, advance);
                const BotView view = BotView::build(scene.sim, 0, &map);
                Orders orders;
                TaskContext ctx{view, orders, ledger, profile, map, 0};
                gate.step(ctx);
                const uint64_t now = view.tick();
                if (refused.x >= 0) usable.emplace_back(now, gate.usable());
                bool moved = false;
                for (const Intent& in : orders.intents()) {
                    const Command& cmd = in.command;
                    if (cmd.type != CommandType::GroupMove) {
                        gate.on_command(cmd, Bot::Fate::Sent, now);
                        continue;
                    }
                    moved = true;
                    if (refused.x < 0 && is_target(cmd)) {                                                  // the first click of the kind is the one that the filter refuses
                        refused = TileCoord{cmd.tile_x, cmd.tile_y};
                        refused_ant = cmd.ants[0];
                        t0 = now;
                    } else if (refused.x >= 0) {
                        if (std::find(cmd.ants.begin(), cmd.ants.end(), refused_ant) != cmd.ants.end()) ant_ordered.push_back(now);
                    }
                    if (refused.x >= 0 && cmd.tile_x == refused.x && cmd.tile_y == refused.y) {
                        if (now > t0) named_again.push_back(now);
                        gate.on_command(cmd, Bot::Fate::Filtered, now);                                      // what the controller tells when the filter refuses the click
                    } else {
                        gate.on_command(cmd, Bot::Fate::Sent, now);                                          // (nothing is applied: the carriers stand where they stood)
                    }
                }
                if (moved && refused.x >= 0 && now > t0) moves.push_back(now);
            };
            look(1);
            ASSERT_TRUE(refused.x >= 0);                                                                    // the gate named a tile of the kind at its first look ...
            ASSERT_EQ(t0, 1u);
            ASSERT_TRUE(gate.usable());
            ASSERT_TRUE(gate.blocked(refused, t0) && gate.blocked(refused, t0 + 899));                      // ... and the refusal blocks it for 900 ticks, to the tick
            ASSERT_FALSE(gate.blocked(refused, t0 + 900));
            for (uint64_t at = t0 + 20; at <= t0 + 1000; at += 20) look(20);
            ASSERT_TRUE(refused_ant != 0);
            // while the tile is blocked it is never named: the first look that names it again is the first after the 900 ticks
            ASSERT_FALSE(named_again.empty());
            ASSERT_TRUE(named_again.front() >= t0 + 900);                                                   // not before ...
            ASSERT_TRUE(named_again.front() <= t0 + 900 + 60);                                              // ... and not much after: the carriers are taken over again every 24 ticks or so
            if (which != 2) {
                for (const auto& u : usable) ASSERT_TRUE(u.second);                                         // a slot or the buffer: the gate guides all the time
                ASSERT_TRUE(!ant_ordered.empty() && ant_ordered.front() <= t0 + 40);                        // the ant whose click was refused is taken over again at once, and sent elsewhere
            } else {
                for (const auto& u : usable) {
                    if (u.first <= t0 + 900) ASSERT_EQ(u.second, u.first >= t0 + 900);                      // the entrance refuses the click: no gate (the economy's rescue is on) until it is over
                }
                for (const uint64_t m : moves) ASSERT_TRUE(m >= t0 + 900);                                  // and no order of the gate at all meanwhile
            }
        }
    } TEST_END();
}
