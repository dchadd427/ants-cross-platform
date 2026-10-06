// The gate task of the standard bot (B4-1, AI10.x): "guiding for eating", measured against the engine's own queue in a hand-made hill with a pile north of it,
// quick enough for suite 2.20. The engine's rules that it rests on are documented in docs/BOTS.md ("The gate").
//
//   AI10.1  the throughput: with the gate guided by hand a hill banks much more than with the engine's queue (8 workers), and never less (3 workers); the median gap between deposits
//           at saturation, and the far waiting tile stays empty
//   AI10.2  the task's rules: the plans (Hard only), the economy's own rescue is off, the refused click is recovered from, more carriers than the doorstep holds are parked and released
//   AI10.3  the surroundings: a rock on the queue row or on the buffer tile is never ordered onto, and the gate still works
//   AI10.4  a hill that the gate cannot guide (in the top rows: no buffer tile) keeps the economy's rescue; both are off only while the gate guides
//   AI10.5  a click that the controller refused (Fate::Filtered) blocks its tile for 900 ticks: a queue-row slot, the buffer tile, the entrance (no gate until it is over)
//   AI10.6  a gate whose clicks onto the entrance deliver nothing (gate_user_fails in a row: the carriers are boxed in) stops for 900 ticks, the economy's rescue is on meanwhile
//   AI10.7  a seat whose gate is answered by a world that cancels the walk of an ant and then fails (the loop of M2) banks next to nothing and burns its budget without the stall detector,
//           and banks like the worker with it (the repeat rule, the rule of the clock, both with the gate's own stop)
#include "ai_test.hpp"
#include "b41_helpers.hpp"

#include "ants_ai/worker_bot.hpp"

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

// A standard bot in a world that answers the gate's orders (the Urgent moves: the takeovers of the carriers and the clicks onto the entrance) by cancelling the walk of the ant and then failing,
// which is what a jammed causeway does ("Can't go there."): each of them leaves as a Stop of the same ants, and the bot is told that its order was sent. The loop of M2: the carriers stand where
// they stand and the gate orders them again and again.
class FutileGate final : public Bot {
public:
    explicit FutileGate(std::unique_ptr<StandardBot> inner) : inner_(std::move(inner)) {}
    const char* kind() const noexcept override { return inner_->kind(); }
    void start(const BotContext& c) override { inner_->start(c); }
    void think(const BotView& v, Orders& o) override {
        Orders mine;
        inner_->think(v, mine);
        for (const Intent& in : mine.intents()) {
            if (in.priority == Priority::Urgent && in.command.type == CommandType::GroupMove) {
                Command stop = in.command;
                stop.type = CommandType::Stop;
                stop.tile_x = 0;
                stop.tile_y = 0;
                replaced_.push_back(in.command);
                o.push_unchecked(stop, Priority::Urgent);
            } else {
                o.push_unchecked(in.command, in.priority, in.pickup);
            }
        }
    }
    void on_command(const Command& c, Fate fate, uint64_t tick) override {
        if (c.type == CommandType::Stop) {
            for (auto it = replaced_.begin(); it != replaced_.end(); ++it) {
                if (it->ants == c.ants) {
                    const Command original = *it;
                    replaced_.erase(it);
                    inner_->on_command(original, fate, tick);                       // the bot hears about its own order
                    return;
                }
            }
        }
        inner_->on_command(c, fate, tick);
    }
    const StandardBot& inner() const { return *inner_; }

private:
    std::unique_ptr<StandardBot> inner_;
    std::vector<Command> replaced_;
};

struct SeatRun {
    int32_t score{0};
    uint32_t released{0};
    uint32_t stalls{0};
    uint32_t pauses{0};
};

// Six workers on a pile 13 rows north of the hill, six minutes (7,200 ticks): `kind` 0 the worker, 1 the standard bot as it is, 2 the standard bot in the futile world; the plan is the Hard
// plan with the gate and the given switches of the detector (0 for the numbers of a rule that is off) and of the gate's own stop
SeatRun play_seat(int kind, uint32_t stall_ticks, uint32_t repeat_limit, uint32_t gate_fails, uint32_t fallback_ticks = 2400, uint32_t repeat_window = 1200, uint64_t ticks = 7200) {
    GateScene scene;
    scene.build(6);                                                                                 // (fewer than 8: the economy's rescue does nothing while 8 or more carriers stand idle at once)
    RecordingSink sink(scene.sim, true);
    BotController ctl(scene.sim, 3);
    BotSpec spec;
    spec.seat = 0;
    spec.level = Level::Hard;
    spec.kind = kind == 0 ? "worker" : "standard";
    std::string why;
    const StandardBot* standard = nullptr;
    if (kind == 0) {
        if (!ctl.add(spec, sink, why)) std::cout << "    add failed: " << why << "\n";
    } else {
        LevelPlan plan = gate_plan(true);
        plan.stall_ticks = stall_ticks;
        plan.repeat_limit = repeat_limit;
        plan.gate_user_fails = gate_fails;
        plan.fallback_ticks = fallback_ticks;
        plan.repeat_window = repeat_window;
        auto inner = std::make_unique<StandardBot>(plan);
        standard = inner.get();
        std::unique_ptr<Bot> bot = kind == 2 ? std::unique_ptr<Bot>(new FutileGate(std::move(inner))) : std::unique_ptr<Bot>(std::move(inner));
        if (!ctl.add(spec, std::move(bot), sink, why)) std::cout << "    add failed: " << why << "\n";
    }
    for (uint64_t t = 0; t < ticks; ++t) {
        scene.sim.tick();
        scene.sim.clear_news_events();
        scene.sim.clear_audio_events();
        ctl.on_tick(scene.sim);
    }
    SeatRun r;
    r.score = scene.sim.get_player_score(0);
    r.released = ctl.stats(0).released;
    r.stalls = standard != nullptr ? standard->stalls() : 0u;
    r.pauses = standard != nullptr ? standard->gate().pauses() : 0u;
    return r;
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
            GateTask::Params gate_params;
            gate_params.user_fail_limit = 0;                                                                // (the gate's own stop after failed clicks is AI10.6: the clicks of this scene all fail)
            GateTask gate(1, gate_params);
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

    TEST_CASE("AI10.6 A Gate Whose Clicks Deliver Nothing Stops: After gate_user_fails Clicks Onto The Entrance In A Row That Delivered Nothing (The Engine Ignores The Order: Nothing Is Applied Here) The Gate Guides No More For 900 Ticks, Its Orders Stop, The Economy's Rescue Is On Meanwhile, And It Tries Again When The Time Is Up; With No Limit It Clicks At Every Look For The Rest Of The Match")
    {
        for (const int variant : {0, 1, 2}) {                                                               // 0: limit 3, nothing is applied; 1: no limit; 2: limit 3, but every third click is applied (it delivers)
            const uint32_t limit = variant == 1 ? 0u : 3u;
            GateScene scene;
            scene.build(0);
            for (int i = 0; i < 3; ++i) {                                                                   // three carriers outside the doorstep; nothing the bot orders is applied
                const uint32_t id = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{23 + i, 16});
                scene.sim.get_unit(id).pick_up_food(1, 25);
            }
            LevelPlan plan = gate_plan(true);
            plan.gate_user_fails = limit;
            plan.stall_ticks = 0;                                                                           // (the detector is another test: this is the gate's own stop)
            plan.repeat_limit = 0;
            const MapInfo map(scene.sim);
            const Profile profile = profile_for(Level::Hard);
            StandardBot bot(plan);
            bot.start(BotContext{0, profile, 1, &map});
            const TileCoord entrance = map.hill(0).entrance;
            size_t click_no = 0;
            std::vector<uint64_t> clicks;                                                                   // the looks at which the GATE clicked (the economy's rescue clicks onto the entrance too)
            uint64_t paused_at = 0;
            uint32_t seen_clicks = 0;
            uint32_t rescues_before_pause = 0;
            bool rescue_on_when_paused = true;
            bool guided_rescue_off = true;
            for (uint64_t t = 1; t <= 1400; ++t) {
                tick_all(scene.sim, 1);
                if (t % 4 != 1) continue;                                                                   // a look every 4 ticks, like Hard
                const BotView view = BotView::build(scene.sim, 0, &map);
                Orders orders;
                bot.think(view, orders);
                for (const Intent& in : orders.intents()) {
                    bot.on_command(in.command, Bot::Fate::Sent, view.tick());
                    const bool click = in.command.type == CommandType::GroupMove && in.command.tile_x == entrance.x && in.command.tile_y == entrance.y;
                    if (variant == 2 && click && ++click_no % 3 == 0) {                                     // a click that reaches the engine: the carrier walks to the entrance and delivers
                        Command applied = in.command;
                        applied.issuer = 0;
                        scene.sim.apply_command(applied);
                    }
                }
                if (bot.gate().entrance_clicks() != seen_clicks) {
                    for (uint32_t k = seen_clicks; k < bot.gate().entrance_clicks(); ++k) clicks.push_back(view.tick());
                    seen_clicks = bot.gate().entrance_clicks();
                }
                if (paused_at == 0) rescues_before_pause = bot.harvest().rescues();
                if (bot.gate().pauses() >= 1 && paused_at == 0) paused_at = view.tick();
                if (paused_at != 0 && view.tick() < paused_at + 900) rescue_on_when_paused = rescue_on_when_paused && !bot.gate().usable() && bot.harvest().params().rescue;
                if (paused_at == 0 && bot.gate().usable()) guided_rescue_off = guided_rescue_off && !bot.harvest().params().rescue;
            }
            if (variant == 2) {
                ASSERT_EQ(bot.gate().pauses(), 0u);                                                         // two failed clicks and a delivery again and again: never three in a row
                ASSERT_TRUE(bot.gate().entrance_clicks() >= 6);
                ASSERT_TRUE(scene.sim.get_player_score(0) > 0);                                             // (the clicks that were applied banked)
            } else if (variant == 0) {
                ASSERT_TRUE(paused_at != 0 && paused_at < 150);                                             // three clicks that deliver nothing take a few dozen ticks
                size_t before = 0;
                size_t during = 0;
                size_t after = 0;
                for (const uint64_t c : clicks) {
                    if (c < paused_at) ++before;
                    else if (c < paused_at + 900) ++during;
                    else ++after;
                }
                ASSERT_EQ(before, 3u);
                ASSERT_EQ(during, 0u);                                                                      // not one click, and no order of the gate at all, while it stops
                ASSERT_TRUE(after >= 1);                                                                    // it tries again when the 900 ticks are over
                ASSERT_TRUE(clicks[3] >= paused_at + 900 && clicks[3] <= paused_at + 900 + 8);              // at the first look after them
                ASSERT_TRUE(rescue_on_when_paused);                                                         // the gate does not guide, so the economy's rescue and the aid are on
                ASSERT_TRUE(guided_rescue_off);                                                             // (and while it guides the rescue is off, as before)
                ASSERT_EQ(rescues_before_pause, 0u);
                ASSERT_TRUE(bot.harvest().rescues() >= 1);                                                  // the rescue sent the carriers home while the gate stopped
                ASSERT_TRUE(bot.gate().pauses() >= 1);
            } else {
                ASSERT_EQ(bot.gate().pauses(), 0u);
                ASSERT_TRUE(clicks.size() >= 60);                                                           // a click every 16 ticks or so, for the rest of the match
            }
        }
    } TEST_END();

    TEST_CASE("AI10.7 The Loop Of M2 And The Stall Detector: Six Workers On A Pile, Six Minutes; The World Answers Every Order Of The Gate (A Takeover, A Click Onto The Entrance) By Stopping The Ant And Failing; The Worker Banks What It Banks; The Standard Bot Without Any Safety Net Banks Under A Quarter Of That And Burns Its Budget (More Than 900 Commands), With The Repeat Rule Alone, The Rule Of The Clock Alone Or Both With The Gate's Own Stop (Shortened Numbers: A Window Of 300 Ticks) It Goes To The Plain Economy And Banks Like The Worker (At Least Half, At Least Three Quarters With Both), And With The Numbers That Ship At Least Half; In A World That Obeys, The Standard Bot Is Not Worse Than The Worker")
    {
        const SeatRun worker = play_seat(0, 0, 0, 0);
        ASSERT_TRUE(worker.score >= 1000);
        const SeatRun healthy = play_seat(1, 3600, 12, 16);                                                 // the defaults, in a world that obeys: the gate works and the detector keeps quiet
        ASSERT_TRUE(healthy.score >= worker.score);
        ASSERT_EQ(healthy.stalls, 0u);
        ASSERT_EQ(healthy.pauses, 0u);                                                                      // (the failed clicks of a healthy gate are far apart: a success starts the count again)
        const SeatRun loop = play_seat(2, 0, 0, 0);                                                         // no detector, no stop of the gate: the loop
        ASSERT_TRUE(loop.score * 4 <= worker.score);
        ASSERT_TRUE(loop.released > 900);                                                                   // the budget of Hard is 3 commands a second: 1,080 in six minutes
        ASSERT_EQ(loop.stalls, 0u);
        const SeatRun repeats = play_seat(2, 0, 12, 0, 2400, 300);                                          // the repeat rule alone, with the window shortened to 300 ticks
        ASSERT_TRUE(repeats.stalls >= 1);
        ASSERT_TRUE(repeats.score * 2 >= worker.score);
        const SeatRun clock = play_seat(2, 600, 0, 0, 1200);                                                // the rule of the clock alone (30 seconds without a point, a fallback of one minute)
        ASSERT_TRUE(clock.stalls >= 1);
        ASSERT_TRUE(clock.score * 2 >= worker.score);
        const SeatRun both = play_seat(2, 3600, 12, 16, 1200, 300);                                         // both rules (a short window and fallback) and the gate's own stop
        ASSERT_TRUE(both.stalls >= 1);
        ASSERT_TRUE(both.score * 4 >= worker.score * 3);
        ASSERT_TRUE(both.released < loop.released / 2);                                                     // and it does not burn the budget
        const SeatRun shipped = play_seat(2, 3600, 12, 16);                                                 // the numbers that ship (a window of 1200 ticks, a fallback of 2400): slower, but out of the loop
        ASSERT_TRUE(shipped.stalls >= 1);
        ASSERT_TRUE(shipped.score * 2 >= worker.score);
        ASSERT_TRUE(shipped.released < loop.released / 2);
    } TEST_END();

    TEST_CASE("AI10.8 A Ring Of Fire Walls Round The Gate Shuts The Hill (Every Walk Into It Is Refused: \"Can't Go There.\"): The Gate Of A Hard Bot With Seven Carriers Outside Sends Nobody Onto A Queue Tile Or The Entrance While The Ring Stands (Before: 85 Ants Ordered In 1,500 Ticks, All Refused, 207 Reactions That Began), It Does Not Guide (The Economy's Rescue Is On), And It Guides And Banks Again After The Ring Burnt Out")
    {
        GateScene scene;
        scene.build(7);
        Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
        CantGoTally tally;
        rig.count_with(&tally);
        rig.run(700);
        ASSERT_TRUE(rig.as<StandardBot>().gate().usable());
        ASSERT_TRUE(tally.seat(0).orders >= 20);                                                            // (the premise: the gate is at work)
        const CantGoTally::Seat before = tally.seat(0);
        const size_t sent_before = rig.sent.size();
        const MapInfo m(scene.sim);
        const HillInfo& h = m.hill(0);
        for (const TileCoord& t : SabotageTask::ring_of(h)) scene.sim.set_fire_at(t, 3600);                  // the ring that a sabotage lights, from tick 700 for 3,600 ticks
        rig.run(1500);
        size_t into_hill = 0;
        for (size_t i = sent_before; i < rig.sent.size(); ++i) {
            const Command& c = rig.sent[i].second;
            if (c.type != CommandType::GroupMove) continue;
            const bool queue = c.tile_y == h.origin.y - 1 && c.tile_x >= h.origin.x && c.tile_x <= h.origin.x + 2;
            const bool entrance = c.tile_x == h.entrance.x && c.tile_y == h.entrance.y;
            if (queue || entrance) into_hill += c.ants.size();
        }
        const CantGoTally::Seat after = tally.seat(0);
        ASSERT_TRUE(into_hill <= 2);                                                                        // (the orders of the last look before the ring was seen)
        ASSERT_TRUE(after.refused - before.refused <= 2);
        ASSERT_TRUE(after.began - before.began <= 6);                                                       // (the ants that stood on the queue row when the ring closed: their loop is the engine's)
        ASSERT_FALSE(rig.as<StandardBot>().gate().usable());
        const int32_t sealed_score = scene.sim.get_player_score(0);
        rig.run(3600);                                                                                      // the ring burns out at tick 4300
        ASSERT_TRUE(rig.as<StandardBot>().gate().usable());
        ASSERT_TRUE(scene.sim.get_player_score(0) >= sealed_score + 200);
    } TEST_END();

    TEST_CASE("AI10.9 A Carrier That No Walk Joins To The Hill Is Not Ours To Place (The Hill Itself Is Open): One In A Pocket Of Rocks Far From The Gate Is Named In No Order; One That Walls Of Fire Shut In After The Last Look At The Map Is Ordered Until The Field Is Made Again (200 Ticks) And Never After; The Others Are Guided All Along")
    {
        const auto ring_of_fire_round = [](sim::SimulationEngine& sim, TileCoord t) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_fire_at(TileCoord{t.x + dx, t.y + dy}, 3600);
                }
            }
        };
        for (const bool rocks : {true, false}) {
            GateScene scene;
            scene.build(4);
            if (rocks) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx != 0 || dy != 0) scene.sim.set_terrain(10 + dx, 12 + dy, sim::TERRAIN_OBSTACLE);
                    }
                }
            }
            const uint32_t shut = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 12});
            scene.sim.get_unit(shut).pick_up_food(1, 25);
            Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            if (rocks) {
                rig.run(1500);
            } else {
                rig.run(3);                                                                                 // the first look (tick 1) made the field with the ant free ...
                ring_of_fire_round(scene.sim, TileCoord{10, 12});                                            // ... and the walls come after it, before the first order leaves
                rig.run(1497);
            }
            ASSERT_TRUE(rig.as<StandardBot>().gate().usable());
            ASSERT_TRUE(rig.as<StandardBot>().gate().entrance_clicks() >= 3);                               // (the others are guided)
            size_t early = 0;
            size_t late = 0;
            for (const auto& e : rig.sent) {
                for (const uint32_t id : e.second.ants) {
                    if (id != shut) continue;
                    ++(e.first <= 260 ? early : late);
                }
            }
            ASSERT_EQ(late, 0u);
            if (rocks) ASSERT_EQ(early, 0u);                                                                // (the field of the first look already knows the rocks)
            else ASSERT_TRUE(early >= 1);                                                                   // (the premise: the field was stale for 200 ticks and the gate used it)
        }
    } TEST_END();

    TEST_CASE("AI10.10 The Economy's Rescue Sends No Carrier Into A Hill That A Ring Of Fire Walls Shut: A Carrier That Stands Idle With Its Food Far From The Hill Is Sent To The Entrance After 40 Ticks (Medium: No Gate), But Not While The Ring Stands (Before: Sent And Refused Again And Again); The Aid Of A Hit Carrier The Same")
    {
        for (const bool ring : {false, true}) {
            sim::SimulationEngine sim;
            empty_field(sim, 8);
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
            sim.get_unit(carrier).pick_up_food(1, 25);
            LevelPlan plan = plan_for(Level::Medium);
            plan.defenders = 0;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            CantGoTally tally;
            rig.count_with(&tally);
            if (ring) {
                const MapInfo m(sim);
                for (const TileCoord& t : SabotageTask::ring_of(m.hill(0))) sim.set_fire_at(t, 3600);
            }
            rig.run(900);
            const StandardBot& bot = rig.as<StandardBot>();
            if (!ring) {
                ASSERT_TRUE(bot.harvest().rescues() >= 1);                                                  // (it is sent, and banks)
                ASSERT_TRUE(sim.get_player_score(0) > 0);
            } else {
                ASSERT_EQ(bot.harvest().rescues(), 0u);
                ASSERT_EQ(tally.seat(0).refused, 0u);
                ASSERT_TRUE(sim.get_unit(carrier).holding != 0);
            }
        }
    } TEST_END();

    TEST_CASE("AI10.11 The Aid Of A Hit Carrier Sends It Home At Once, Unless A Ring Of Fire Walls Shut The Hill: The Blow Clears Its Walk And It Stands With Its Food; With The Ring Lit The Standard Bot Orders Nothing (Before: One Order Per Try, Refused); A Ring That Burns Out After 300 Ticks Has Cost It No Try: The Carrier Is Sent Home Then")
    {
        for (const int ring_ticks : {0, 3600, 300}) {
            const bool ring = ring_ticks != 0;
            sim::SimulationEngine sim;
            empty_field(sim, 8);
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 9});
            sim.get_unit(carrier).pick_up_food(1, 25);
            const uint32_t enemy = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{9, 11});
            LevelPlan plan = plan_for(Level::Medium);
            plan.defenders = 0;
            Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
            CantGoTally tally;
            rig.count_with(&tally);
            if (ring) {
                const MapInfo m(sim);
                for (const TileCoord& t : SabotageTask::ring_of(m.hill(0))) sim.set_fire_at(t, static_cast<uint32_t>(ring_ticks));
            }
            rig.run(3);
            sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, 9, 9));
            bool hit = false;
            for (int t = 0; t < 500; ++t) {
                rig.tick();
                hit = hit || sim.get_unit(carrier).hp < 10;
            }
            ASSERT_TRUE(hit);
            const StandardBot& bot = rig.as<StandardBot>();
            if (!ring || ring_ticks == 300) {
                ASSERT_TRUE(bot.aid().sent_home() >= 1);
                ASSERT_EQ(tally.seat(0).refused, 0u);
            } else {
                ASSERT_EQ(bot.aid().sent_home(), 0u);
                ASSERT_EQ(tally.seat(0).refused, 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI10.12 An Own Ant That Stands On The Ramp Shuts The Way To The Entrance Like One That Stands On The Entrance: The Gate Clicks Nobody Onto The Entrance While It Stands There (Before: 16 Clicks And 17 Orders In 400 Ticks, Every One Refused, \"Can't Go There.\", Then The Gate Stopped For 900 Ticks), And Clicks At Once When The Ant Has Gone; An Ant That Walks Over The Ramp Holds Nothing; An Ant That Never Goes Is Waited For 200 Ticks, Not Longer")
    {
        const TileCoord hill{26, 26};
        const TileCoord ramp{hill.x + 1, hill.y};
        // a world with one carrier at the doorstep and nothing else to do: the ant on the ramp stands there (no pile for it to be sent to)
        const auto world = [&](sim::SimulationEngine& sim) {
            sim.init_test_world(60, 60, 5, 14400u * sim::TICK_MS);
            sim.grid_mut().set_anthill(0, hill);
            sim.grid_mut().set_anthill(1, TileCoord{4, 4});
            sim.grid_mut().set_anthill(2, TileCoord{54, 4});
            sim.grid_mut().set_anthill(3, TileCoord{54, 54});
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 20});
            sim.get_unit(carrier).pick_up_food(1, 25);
            return carrier;
        };
        {   // (a) nobody on the ramp: the carrier is clicked onto the entrance once and banks
            sim::SimulationEngine sim;
            const uint32_t carrier = world(sim);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.run(400);
            ASSERT_EQ(rig.as<StandardBot>().gate().entrance_clicks(), 1u);
            ASSERT_EQ(tally.seat(0).refused, 0u);
            ASSERT_TRUE(sim.get_player_score(0) >= 25);
            ASSERT_TRUE(sim.get_unit(carrier).holding == 0);
        }
        {   // (b) an own ant stands on the ramp: no click, no reaction; it goes, and the carrier banks
            sim::SimulationEngine sim;
            const uint32_t carrier = world(sim);
            const uint32_t stander = sim.spawn_unit(0, sim::AntType::Worker, ramp);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.run(150);
            const GateTask& gate = rig.as<StandardBot>().gate();
            ASSERT_TRUE(sim.get_unit(stander).pos.x == ramp.x && sim.get_unit(stander).pos.y == ramp.y);        // (the premise: it stands there)
            ASSERT_TRUE(gate.usable());
            ASSERT_EQ(gate.entrance_clicks(), 0u);
            ASSERT_EQ(tally.seat(0).refused, 0u);
            ASSERT_EQ(tally.seat(0).reactions, 0u);
            ASSERT_TRUE(sim.get_unit(carrier).holding != 0);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {stander}, 20, 18));
            rig.run(300);
            ASSERT_TRUE(gate.entrance_clicks() >= 1);
            ASSERT_EQ(tally.seat(0).refused, 0u);
            ASSERT_TRUE(sim.get_player_score(0) >= 25);
        }
        {   // (c) an own ant that walks over the ramp holds nothing: the carrier is clicked at the first look
            sim::SimulationEngine sim;
            world(sim);
            const uint32_t walker = sim.spawn_unit(0, sim::AntType::Worker, ramp);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {walker}, 23, 20));
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.tick();                                                                                       // the first look: the walker is on the ramp and shows "walking"
            ASSERT_TRUE(sim.get_unit(walker).state == sim::UnitState::Walking && sim.get_unit(walker).pos.x == ramp.x && sim.get_unit(walker).pos.y == ramp.y);
            rig.run(60);
            size_t first_click = 0;
            for (const auto& e : rig.proposed) {
                if (first_click == 0 && e.second.type == CommandType::GroupMove && e.second.tile_x == hill.x + 1 && e.second.tile_y == hill.y + 1) first_click = static_cast<size_t>(e.first);
            }
            ASSERT_EQ(first_click, 1u);
            ASSERT_EQ(tally.seat(0).refused, 0u);
        }
        {   // (d) an own ant stands on the ramp and never goes: nothing moves it, so the gate waits ramp_wait_ticks (200) and then clicks as it did before (the click is refused, and the gate's stop takes over)
            sim::SimulationEngine sim;
            world(sim);
            const uint32_t stander = sim.spawn_unit(0, sim::AntType::Worker, ramp);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.run(400);
            ASSERT_TRUE(sim.get_unit(stander).pos.x == ramp.x && sim.get_unit(stander).pos.y == ramp.y);        // (the premise: it stands there all along)
            ASSERT_EQ(rig.as<StandardBot>().gate().params().ramp_wait_ticks, 200u);
            uint64_t first_click = 0;
            for (const auto& e : rig.proposed) {
                if (first_click == 0 && e.second.type == CommandType::GroupMove && e.second.tile_x == hill.x + 1 && e.second.tile_y == hill.y + 1) first_click = e.first;
            }
            ASSERT_TRUE(first_click >= 201 && first_click <= 215);                                              // (the first look is at tick 1; a look every 4 ticks)
            ASSERT_TRUE(rig.as<StandardBot>().gate().entrance_clicks() >= 1);
            ASSERT_TRUE(tally.seat(0).refused >= 1);                                                            // (what the wait spares: the engine refuses it)
        }
        {   // (e) the wait of 200 ticks starts again with every ant that comes to stand on the ramp: one stood there for 100 ticks and went (the carrier banked); at tick 400 another one stands there and
            // a second carrier waits: the gate clicks nobody in the first 150 ticks of that wait, as it did for the first ant
            sim::SimulationEngine sim;
            world(sim);
            const uint32_t first = sim.spawn_unit(0, sim::AntType::Worker, ramp);
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.run(100);
            ASSERT_EQ(rig.as<StandardBot>().gate().entrance_clicks(), 0u);
            sim.apply_command(command_of(CommandType::GroupMove, 0, {first}, 20, 18));
            rig.run(300);
            ASSERT_TRUE(sim.get_player_score(0) >= 25);                                                         // (the premise: the first carrier banked)
            const uint32_t clicks = rig.as<StandardBot>().gate().entrance_clicks();
            const uint32_t second = sim.spawn_unit(0, sim::AntType::Worker, ramp);
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 20});
            sim.get_unit(carrier).pick_up_food(1, 25);
            rig.run(150);
            ASSERT_TRUE(sim.get_unit(second).pos.x == ramp.x && sim.get_unit(second).pos.y == ramp.y);          // (the premise: it stands there all along)
            ASSERT_TRUE(sim.get_unit(carrier).holding != 0);
            ASSERT_EQ(rig.as<StandardBot>().gate().entrance_clicks(), clicks);
            ASSERT_EQ(tally.seat(0).refused, 0u);
        }
    } TEST_END();

    TEST_CASE("AI10.13 A Swimmer Is Not Cut Off By Water: A Carrier On An Island That No Walk Joins To The Hill Is Named In No Order (The Gate, The Rescue, The Aid Of A Hit Carrier), A Swimmer That Stands There Is Named In All Three (It Swims Home; Before: It Was Left Where It Was)")
    {
        const auto water_round = [](sim::SimulationEngine& sim, TileCoord t) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_terrain(t.x + dx, t.y + dy, sim::TERRAIN_WATER);
                }
            }
        };
        const auto named = [](const Rig& rig, uint32_t ant) {
            size_t n = 0;
            for (const auto& e : rig.sent) {
                for (const uint32_t id : e.second.ants) n += id == ant ? 1u : 0u;
            }
            return n;
        };
        for (const bool swims : {false, true}) {
            {   // (a) the gate (Hard): the carrier stands far from it on a one-tile island
                GateScene scene;
                scene.build(4);
                water_round(scene.sim, TileCoord{10, 12});
                const uint32_t far = scene.sim.spawn_unit(0, swims ? sim::AntType::Swimmer : sim::AntType::Worker, TileCoord{10, 12});
                scene.sim.get_unit(far).pick_up_food(1, 25);
                Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(gate_plan(true)), 4, 8);
                rig.run(600);
                ASSERT_TRUE(rig.as<StandardBot>().gate().usable());
                ASSERT_TRUE(rig.as<StandardBot>().gate().entrance_clicks() >= 1);                         // (the others are guided)
                if (swims) ASSERT_TRUE(named(rig, far) >= 1);
                else ASSERT_EQ(named(rig, far), 0u);
            }
            {   // (b) the economy's rescue (Medium: no gate) of a carrier that stands idle with its food; on a level whose ants are swimmers the swimmer is in its pool
                sim::SimulationEngine sim;
                empty_field(sim, 8);
                if (swims) sim.grid_mut().set_default_ant_tile(65);
                water_round(sim, TileCoord{12, 12});
                const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
                sim.get_unit(carrier).pick_up_food(1, 25);
                LevelPlan plan = plan_for(Level::Medium);
                plan.defenders = 0;
                Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(900);
                if (swims) ASSERT_TRUE(rig.as<StandardBot>().harvest().rescues() >= 1);
                else ASSERT_EQ(rig.as<StandardBot>().harvest().rescues(), 0u);
                if (!swims) ASSERT_EQ(named(rig, carrier), 0u);
            }
            {   // (c) the aid of a carrier that was hit (its walk lost): it stands idle with its food after the blow
                sim::SimulationEngine sim;
                empty_field(sim, 8);
                water_round(sim, TileCoord{12, 12});
                const uint32_t carrier = sim.spawn_unit(0, swims ? sim::AntType::Swimmer : sim::AntType::Worker, TileCoord{12, 12});
                sim.get_unit(carrier).pick_up_food(1, 25);
                LevelPlan plan = plan_for(Level::Medium);
                plan.defenders = 0;
                Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                rig.run(3);
                sim.get_unit(carrier).hp = 6;                                                             // the blow
                rig.run(300);
                if (swims) ASSERT_TRUE(rig.as<StandardBot>().aid().sent_home() >= 1);
                else ASSERT_EQ(rig.as<StandardBot>().aid().sent_home(), 0u);
            }
        }
    } TEST_END();

    TEST_CASE("AI10.14 The Switch cg=0 (LevelPlan::cantgo_aware False) Is The Bot As It Was: The Scenes Of AI10.8 To AI10.12 Again, And Each Of Them Shows What The Fix Prevents (The Gate Orders Into A Ringed Hill And Is Refused, Places A Carrier That Is Shut In, The Rescue And The Aid Send A Carrier Into A Shut Hill, The Entrance Is Clicked While An Ant Stands On The Ramp; The Worker Bot Is Not Changed)")
    {
        {   // (a) a ring of fire walls round the gate (AI10.8)
            GateScene scene;
            scene.build(7);
            LevelPlan plan = gate_plan(true);
            plan.cantgo_aware = false;
            Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.run(700);
            const CantGoTally::Seat before = tally.seat(0);
            const MapInfo m(scene.sim);
            for (const TileCoord& t : SabotageTask::ring_of(m.hill(0))) scene.sim.set_fire_at(t, 3600);
            rig.run(1500);
            ASSERT_TRUE(tally.seat(0).orders - before.orders >= 20);
            ASSERT_TRUE(tally.seat(0).refused - before.refused >= 20);
        }
        {   // (b) a carrier in a pocket of rocks far from the gate (AI10.9): it is named in orders to the end
            GateScene scene;
            scene.build(4);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) scene.sim.set_terrain(10 + dx, 12 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
            const uint32_t shut = scene.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10, 12});
            scene.sim.get_unit(shut).pick_up_food(1, 25);
            LevelPlan plan = gate_plan(true);
            plan.cantgo_aware = false;
            Rig rig(scene.sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 8);
            rig.run(1500);
            size_t late = 0;
            for (const auto& e : rig.sent) {
                for (const uint32_t id : e.second.ants) late += id == shut && e.first > 260 ? 1u : 0u;
            }
            ASSERT_TRUE(late >= 1);
        }
        for (const bool ring : {true}) {   // (c) the rescue (AI10.10) and (d) the aid (AI10.11) with the hill shut by a ring
            for (const bool aid : {false, true}) {
                sim::SimulationEngine sim;
                empty_field(sim, 8);
                const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, aid ? TileCoord{9, 9} : TileCoord{12, 12});
                sim.get_unit(carrier).pick_up_food(1, 25);
                const uint32_t enemy = aid ? sim.spawn_unit(1, sim::AntType::Worker, TileCoord{9, 11}) : 0u;
                LevelPlan plan = plan_for(Level::Medium);
                plan.defenders = 0;
                plan.cantgo_aware = false;
                Rig rig(sim, 0, Level::Medium, std::make_unique<StandardBot>(plan), 4, 4);
                CantGoTally tally;
                rig.count_with(&tally);
                if (ring) {
                    const MapInfo m(sim);
                    for (const TileCoord& t : SabotageTask::ring_of(m.hill(0))) sim.set_fire_at(t, 3600);
                }
                if (aid) {
                    rig.run(3);
                    sim.apply_command(command_of(CommandType::GroupAttack, 1, {enemy}, 9, 9));
                    rig.run(500);
                    ASSERT_TRUE(rig.as<StandardBot>().aid().sent_home() >= 1);
                } else {
                    rig.run(900);
                    ASSERT_TRUE(rig.as<StandardBot>().harvest().rescues() >= 1);
                }
                ASSERT_TRUE(tally.seat(0).refused >= 1);
            }
        }
        {   // (e) an own ant stands on the ramp (AI10.12): the entrance is clicked at the first look, not after the 200 ticks that the fix waits, and the click is refused
            const TileCoord hill{26, 26};
            sim::SimulationEngine sim;
            sim.init_test_world(60, 60, 5, 14400u * sim::TICK_MS);
            sim.grid_mut().set_anthill(0, hill);
            sim.grid_mut().set_anthill(1, TileCoord{4, 4});
            sim.grid_mut().set_anthill(2, TileCoord{54, 4});
            sim.grid_mut().set_anthill(3, TileCoord{54, 54});
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{27, 20});
            sim.get_unit(carrier).pick_up_food(1, 25);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{hill.x + 1, hill.y});
            LevelPlan plan = gate_plan(true);
            plan.cantgo_aware = false;
            Rig rig(sim, 0, Level::Hard, std::make_unique<StandardBot>(plan), 4, 8);
            CantGoTally tally;
            rig.count_with(&tally);
            rig.run(400);
            ASSERT_TRUE(rig.as<StandardBot>().gate().entrance_clicks() >= 1);
            ASSERT_TRUE(tally.seat(0).refused >= 1);
            uint64_t first_click = 0;
            for (const auto& e : rig.proposed) {
                if (first_click == 0 && e.second.type == CommandType::GroupMove && e.second.tile_x == hill.x + 1 && e.second.tile_y == hill.y + 1) first_click = e.first;
            }
            ASSERT_TRUE(first_click >= 1 && first_click < 100);                                                 // (the first look is at tick 1; AI10.12 (d) has the click after the 200 ticks of the wait)
        }
        {   // (f) the worker bot, the frozen yardstick, is not changed by the fixes: its rescue still sends a carrier into a hill that a ring shuts
            sim::SimulationEngine sim;
            empty_field(sim, 8);
            const uint32_t carrier = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{12, 12});
            sim.get_unit(carrier).pick_up_food(1, 25);
            Rig rig(sim, 0, Level::Medium, std::make_unique<WorkerBot>(), 4, 4);
            CantGoTally tally;
            rig.count_with(&tally);
            const MapInfo m(sim);
            for (const TileCoord& t : SabotageTask::ring_of(m.hill(0))) sim.set_fire_at(t, 3600);
            rig.run(900);
            ASSERT_TRUE(rig.as<WorkerBot>().harvest().rescues() >= 1);
            ASSERT_TRUE(tally.seat(0).refused >= 1);
        }
    } TEST_END();
}
