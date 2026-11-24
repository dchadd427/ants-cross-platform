// AI54: the cheapest flower link first and the bridges for ants that stand away from the hill (island_link.hpp, island_bridge.hpp, docs/BOT_ISLANDS.md "Bridges for ants"). ISLANDS, one Hard bot on seat 0, the
// expedition, the miner and the mines off: the ants and the Swimmers that a test puts on the map are the only ones there. Hill 0 stands on the island (18 .. 28, 19 .. 28); the satellite west of it
// (10 .. 15, 22 .. 27, a Bomber power-up on it) is two water tiles away, the satellite north of it (22 .. 29, 9 .. 15, two piles of 10 units of 30) three.
#include <functional>
#include <memory>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "flower_lab.hpp"
#include "island_fixture.hpp"
#include "ants_ai/standard_bot.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

using flower_lab::Lab;

const TileCoord kWestSatellite{12, 23};      // a tile of the satellite west of hill 0
const TileCoord kWestChannel[2] = {TileCoord{17, 21}, TileCoord{16, 22}};   // the two water tiles between that satellite and the hill's island
const TileCoord kNorthSatellite{26, 14};     // a tile of the satellite north of hill 0, beside its food
const TileCoord kNorthChannel[3] = {TileCoord{23, 16}, TileCoord{24, 17}, TileCoord{25, 18}};   // the three water tiles between it and the hill's island

void swimmers(Lab& lab, int n, TileCoord at) {
    for (int i = 0; i < n; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{at.x + i, at.y});
}

int standing(const Lab& lab, const TileCoord* tiles, int n) {
    int done = 0;
    for (int i = 0; i < n; ++i) done += lab.bridge_at(tiles[i]) ? 1 : 0;
    return done;
}

int any_stage(const Lab& lab, TileCoord t) { return lab.sim.grid().get_cell(t).has_any_bridge() ? 1 : 0; }

/// The bot's own start ants that stand on a satellite are taken off the map (they take the Bomber beside them at once), and so is the Bomber token of the hill's island: the bot wants a Bomber and has none,
/// and the only ones it can reach lie beyond water
void no_bomber_in_reach(Lab& lab) {
    const MapInfo map(lab.sim);
    std::vector<uint32_t> away;
    for (const auto& a : lab.sim.get_world_state().ants) {
        if (a.player_id == 0 && map.ant_component(0, TileCoord{a.tile_x, a.tile_y}) != map.hill_component(0)) away.push_back(a.id);
    }
    for (const uint32_t id : away) lab.sim.kill_unit(id);
    lab.sim.grid_mut().clear_powerup(20, 21);
}

void plans(LevelPlan& p, bool cost, bool ret, bool feed, bool power) {
    p.island_link_cost = cost;
    p.island_return = ret;
    p.island_feeder = feed;
    p.island_power_bridge = power;
    p.island_ferry = false;
}

}  // namespace

void run_island_bridges_tests() {
    TEST_CASE("AI54.1 The Cheapest Link Is Dug First (A Swimmer And A Worker On The East Half Of The Ring Beside The Bottom Strip's One-Tile Link): The Bottom Link At (36, 52) Is Dug, Not The Top Strip's Two Tiles That Come First In The List; With `island_link_cost` Off The Swimmer Swims North")
    {
        for (const bool cost : {true, false}) {
            Lab lab([cost](LevelPlan& p) { plans(p, cost, false, false, false); });
            const uint32_t swimmer = lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{40, 53});
            lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{45, 53});
            lab.run(400);
            const sim::AntSnapshot* s = lab.ant(swimmer);
            ASSERT_TRUE(s != nullptr);
            if (cost) {
                ASSERT_TRUE(lab.bridge_at(TileCoord{36, 52}));
            } else {
                ASSERT_TRUE(!lab.bridge_at(TileCoord{36, 52}));
                ASSERT_TRUE(s->tile_y < 45);                                                    // (it is on its way to (37, 7): the first link of the list)
            }
        }
    } TEST_END();

    TEST_CASE("AI54.2 A Strip That Other Teams Crowd Is The Dearer One (A Worker On The East Half Midway Between The Two Strips, A Swimmer In The Lake, A Little Nearer To The Bottom One): The Bottom Link Is Dug; With An Enemy Worker At The Bottom Drop Tile The Top Link Is")
    {
        for (const bool crowd : {false, true}) {
            Lab lab([](LevelPlan& p) { plans(p, true, false, false, false); }, "ISLANDS", 0x03);
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{44, 32});
            lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{56, 30});
            if (crowd) lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{30, 55});
            lab.run(500);
            const int bottom = any_stage(lab, TileCoord{36, 52});
            const int top = any_stage(lab, TileCoord{37, 7}) + any_stage(lab, TileCoord{36, 7});
            ASSERT_EQ(bottom != 0, !crowd);
            ASSERT_EQ(top != 0, crowd);
        }
    } TEST_END();

    TEST_CASE("AI54.3 A Hurt Ant On A Satellite Gets A Bridge Home And Heals (A Worker With 2 Hit Points On The Satellite West Of The Hill, Three Swimmers): The Two Water Tiles Stand, The Ant Is Home With All Its Hit Points And The Planner Sent It; Without `island_return` Nothing Is Dug And It Stays")
    {
        for (const bool on : {true, false}) {
            Lab lab([on](LevelPlan& p) { plans(p, false, on, false, false); });
            swimmers(lab, 3, TileCoord{14, 19});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.sim.get_unit(ant).hp = 2;
            lab.run(1500);
            const sim::AntSnapshot* a = lab.ant(ant);
            ASSERT_TRUE(a != nullptr);
            ASSERT_EQ(standing(lab, kWestChannel, 2) == 2, on);
            ASSERT_EQ(a->hp == 10, on);
            ASSERT_EQ(lab.bot()->flower_link().bridges().homeward(), on ? 1u : 0u);
            ASSERT_EQ(lab.bot()->ledger().owner(ant), kNoTask);                                 // (the planner keeps the ant from the economy while it walks home, and gives it back when it has healed)
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Return), on ? 2u : 0u);
            if (!on) ASSERT_TRUE(a->tile_x < 16);
        }
    } TEST_END();

    TEST_CASE("AI54.4 A Whole Ant Beside Food That The Hill Cannot Reach Gets The Bridge That Opens It (A Worker With 10 Hit Points On The Satellite North Of The Hill, Three Swimmers, The Ferry Off): The Three Water Tiles Stand And The Worker Banks Points; Without `island_feeder` Nothing Is Dug; With Room For Two Tiles Only (`island_bridge_tiles` 2) Nothing Is Dug")
    {
        for (const int mode : {0, 1, 2}) {
            Lab lab([mode](LevelPlan& p) {
                plans(p, false, false, mode != 1, false);
                if (mode == 2) p.island_bridge_tiles = 2;
            });
            swimmers(lab, 3, TileCoord{26, 20});
            lab.sim.spawn_unit(0, sim::AntType::Worker, kNorthSatellite);
            lab.run(3000);
            if (mode == 0) {
                ASSERT_EQ(standing(lab, kNorthChannel, 3), 3);
                ASSERT_TRUE(lab.sim.get_player_score(0) > 0);
                ASSERT_TRUE(lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Feeder) >= 3u);
            } else {
                ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
                ASSERT_EQ(lab.sim.get_player_score(0), 0);
            }
        }
    } TEST_END();

    TEST_CASE("AI54.5 A Bridge Is Not Dug For An Ant That Would Find Nothing On The Other Side (A Whole Worker On The Satellite West Of The Hill, Which Has No Food, Three Swimmers): Nothing Is Dug With The Feeder On, Nor With The Return On (The Ant Is Not Hurt); A Hurt One Gets The Return Bridge With Both On")
    {
        for (const bool feeder : {true, false}) {
            Lab lab([feeder](LevelPlan& p) { plans(p, false, !feeder, feeder, false); });
            swimmers(lab, 3, TileCoord{14, 19});
            lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.run(1500);
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
            ASSERT_EQ(standing(lab, kWestChannel, 2), 0);
        }
        {
            Lab lab([](LevelPlan& p) { plans(p, false, true, true, false); });
            swimmers(lab, 3, TileCoord{14, 19});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.sim.get_unit(ant).hp = 3;
            lab.run(1500);
            ASSERT_EQ(standing(lab, kWestChannel, 2), 2);
            ASSERT_TRUE(lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Return) >= 2u);
        }
    } TEST_END();

    TEST_CASE("AI54.6 A Power-Up That The Plan Wants Behind Water Is Fetched Over A Bridge By A Whole Ant (An Enemy Team That Plays, `war_bombers` 2, The Bot Has No Bomber And The One Of The Hill's Island Is Gone, Three Swimmers, Seven Workers At Home): The Bot Has A Bomber Within 2,500 Ticks Over Two Water Tiles It Dug Itself; Without `island_power_bridge`, With Every Worker Hurt (4 Hit Points), Or With A Plan That Fetches Fire Only (`island_power_kinds`), It Has None And Digs Nothing")
    {
        for (const int mode : {0, 1, 2, 3}) {                                                   // (0: on, 1: the switch off, 2: on, and every ant hurt, 3: on, for Fire only)
            Lab lab([mode](LevelPlan& p) {
                plans(p, false, false, false, mode != 1);
                p.war_bombers = 2;
                if (mode == 3) p.island_power_kinds = static_cast<uint8_t>(1u << static_cast<unsigned>(sim::AntType::Fire));
            }, "ISLANDS", 0x03);
            no_bomber_in_reach(lab);
            if (mode == 2) {
                for (const auto& a : lab.sim.get_world_state().ants) {
                    if (a.player_id == 0) lab.sim.get_unit(a.id).hp = 4;
                }
            }
            const uint32_t walker = lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 3});                // (an enemy ant that walks: the bot sees a team that plays and wants Bombers for its mines)
            lab.sim.apply_command(b41::command_of(CommandType::GroupMove, 1, {walker}, 52, 4));
            swimmers(lab, 3, TileCoord{22, 19});
            lab.run(2500);
            size_t bombers = 0;
            for (const auto& a : lab.sim.get_world_state().ants) bombers += a.player_id == 0 && a.raw_type == sim::AntType::Bomber ? 1u : 0u;
            ASSERT_EQ(bombers >= 1u, mode == 0);
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(BridgeJob::Kind::Power) >= 2u, mode == 0);
            if (mode != 0) ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI54.7 The Ferry Keeps Its Swimmers (Two Swimmers, A Hurt Worker On The Satellite West Of The Hill): No Tile Is Dug With Fewer Than Three Swimmers; A Third Swimmer At Tick 1,000 Starts The Dig")
    {
        Lab lab([](LevelPlan& p) { plans(p, false, true, false, false); });
        swimmers(lab, 2, TileCoord{14, 19});
        const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
        lab.sim.get_unit(ant).hp = 2;
        lab.run(1000);
        ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{16, 19});
        lab.run(900);
        ASSERT_EQ(standing(lab, kWestChannel, 2), 2);
    } TEST_END();

    TEST_CASE("AI54.8 Nothing Is Dug Late In The Match Or Where Something Blocks A Tile (A Hurt Worker On The Satellite West Of The Hill): With A Fire Wall On A Tile Of The Channel That Channel Is Left Alone; With 2,300 Ticks Left On The Clock Nothing Is Dug")
    {
        {
            Lab lab([](LevelPlan& p) { plans(p, false, true, false, false); });
            swimmers(lab, 3, TileCoord{14, 19});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.sim.get_unit(ant).hp = 2;
            lab.run(10);                                                                        // (the analysis of the start has seen the channel)
            lab.sim.grid_mut().place_firewall(static_cast<uint32_t>(kWestChannel[1].x), static_cast<uint32_t>(kWestChannel[1].y), 1);
            lab.run(1500);
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
            ASSERT_EQ(standing(lab, kWestChannel, 2), 0);
        }
        {
            Lab lab([](LevelPlan& p) { plans(p, false, true, false, false); });
            swimmers(lab, 3, TileCoord{14, 19});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.run(14400 - 2300);
            lab.sim.get_unit(ant).hp = 2;
            lab.run(1000);
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI54.9 Where No Flower Link Exists The Planner Is Idle (SMALL, A Hurt Worker, Three Swimmers, All Three Rules On): No Tile Is Ordered")
    {
        Lab lab([](LevelPlan& p) { plans(p, true, true, true, true); }, "SMALL");
        for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 10});
        const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
        lab.sim.get_unit(ant).hp = 2;
        lab.run(2000);
        ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
    } TEST_END();

    TEST_CASE("AI54.10 No Bridge Leads Over Another Team's Hill (Seats 0 And 3 In The Roster, `island_bridge_tiles` 5, A Hurt Worker Of Seat 0 On The Food Satellite East Of The Hill Of Seat 3): The Only Way To Its Own Hill Is Over The Island Of The Other Hill, And No Tile Is Dug; Nor For A Satellite That A Finished Bridge Joins To The Island Of Another Team's Hill (Seats 0 And 2, The Channel Between The Satellite West Of Hill 0 And The Island Of Hill 2 Stands)")
    {
        {
            Lab lab([](LevelPlan& p) {
                plans(p, false, true, false, false);
                p.island_bridge_tiles = 5;                                                       // (two tiles to the island of seat 3's hill and three more to its own: the way round by the ring is eight)
            }, "ISLANDS", 0x09);
            swimmers(lab, 3, TileCoord{30, 31});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{49, 24});
            lab.sim.get_unit(ant).hp = 2;
            lab.run(2000);
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        }
        for (const bool joined : {false, true}) {
            Lab lab([](LevelPlan& p) { plans(p, false, true, false, false); }, "ISLANDS", 0x05);
            swimmers(lab, 3, TileCoord{14, 19});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.sim.get_unit(ant).hp = 2;
            lab.run(10);
            if (joined) {
                for (const TileCoord t : {TileCoord{15, 28}, TileCoord{16, 29}, TileCoord{17, 30}, TileCoord{18, 31}}) lab.sim.grid_mut().set_bridge_at(t, 4, 0);
            }
            lab.run(1500);
            ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered() == 0u, joined);
        }
    } TEST_END();

    TEST_CASE("AI54.11 A Dig Is Not Left Half Done For A Dearer Link (A Swimmer In The Lake Near The Top Link, A Worker On The East Half Near It; At Tick 40 Four Enemy Workers Stand At The Top Strip): The Two Top Tiles Stand Within 700 Ticks And The Bottom Link Is Not Touched")
    {
        Lab lab([](LevelPlan& p) { plans(p, true, false, false, false); }, "ISLANDS", 0x03);
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{45, 12});
        lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{56, 9});
        lab.run(40);
        for (int i = 0; i < 4; ++i) lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{28 + i, 5});         // (the strip is crowded now: its link is dearer than the bottom one)
        lab.run(660);
        ASSERT_TRUE(lab.bridge_at(TileCoord{37, 7}) && lab.bridge_at(TileCoord{36, 7}));
        ASSERT_EQ(any_stage(lab, TileCoord{36, 52}), 0);
    } TEST_END();

    TEST_CASE("AI54.12 A Chain Of Two Channels Is Dug When Its Tiles Together Are Within The Limit (The Channel To The Hill's Island From The North Satellite Is Shut By A Fire Wall, A Hurt Worker On That Satellite): With `island_bridge_tiles` 5 The Two Tiles Towards The Satellite North-West Of It Are Dug First; With 4 Nothing Is Dug")
    {
        for (const uint32_t limit : {5u, 4u}) {
            Lab lab([limit](LevelPlan& p) {
                plans(p, false, true, false, false);
                p.island_bridge_tiles = limit;
            });
            swimmers(lab, 3, TileCoord{26, 20});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kNorthSatellite);
            lab.sim.get_unit(ant).hp = 2;
            lab.run(10);
            lab.sim.grid_mut().place_firewall(24, 17, 1);                                       // (the middle tile of the direct way: it is no way now)
            lab.run(1200);
            const bool first = lab.bridge_at(TileCoord{21, 11}) && lab.bridge_at(TileCoord{20, 12});
            ASSERT_EQ(first, limit == 5u);
            if (limit == 4u) ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
            if (limit == 5u) {                                                                  // (the chain goes on over the channel that stands now: the second channel is dug and the ant is home and whole)
                lab.run(2500);
                ASSERT_TRUE(lab.bridge_at(TileCoord{16, 16}) && lab.bridge_at(TileCoord{17, 17}) && lab.bridge_at(TileCoord{18, 18}));
                ASSERT_TRUE(lab.ant(ant) != nullptr);
                ASSERT_EQ(lab.ant(ant)->hp, 10);
            }
        }
    } TEST_END();

    TEST_CASE("AI54.13 The Swimmer's Swim Counts In The Cost Of A Link (A Worker On The East Half Midway Between The Strips): With The Swimmer In The Lake Near The Top Link That One Is Dug, With The Swimmer Near The Bottom Link The Bottom One Is, Though It Has One Tile And The Top One Two")
    {
        for (const bool north : {true, false}) {
            Lab lab([](LevelPlan& p) { plans(p, true, false, false, false); });
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, north ? TileCoord{45, 14} : TileCoord{45, 46});
            lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{56, 30});
            lab.run(400);
            const int bottom = any_stage(lab, TileCoord{36, 52});
            const int top = any_stage(lab, TileCoord{37, 7}) + any_stage(lab, TileCoord{36, 7});
            ASSERT_EQ(top != 0, north);
            ASSERT_EQ(bottom != 0, !north);
        }
    } TEST_END();

    TEST_CASE("AI54.14 A Bridge Job Does Not Wait For Ever For A Tile With An Ant On It, And The Tiles Of A Way Are Dug From The Ant's Side (Seat 1 In The Roster, An Enemy Swimmer At Rest On The First Tile Of The Way From The Satellite West Of The Hill, A Hurt Worker There, Three Swimmers): No Tile Is Ordered And Within 600 Ticks No Swimmer Is Held For The Dig")
    {
        Lab lab([](LevelPlan& p) { plans(p, false, true, false, false); }, "ISLANDS", 0x03);
        swimmers(lab, 3, TileCoord{14, 19});
        const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
        lab.sim.get_unit(ant).hp = 2;
        lab.sim.spawn_unit(1, sim::AntType::Swimmer, kWestChannel[1]);
        lab.run(600);
        ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        ASSERT_EQ(lab.bot()->flower_link().digger(), 0u);
    } TEST_END();

    TEST_CASE("AI54.15 The Tiles To Dig Are In The Price Of A Link (A Worker On The East Half Midway Between The Strips, A Swimmer In The Lake A Tile Nearer To The Top Link): The Bottom Link, One Tile Against Two, Is Dug Though The Swimmer Is Nearer To The Top One")
    {
        Lab lab([](LevelPlan& p) { plans(p, true, false, false, false); });
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{45, 29});
        lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{56, 30});
        lab.run(400);
        ASSERT_EQ(any_stage(lab, TileCoord{36, 52}), 1);
        ASSERT_EQ(any_stage(lab, TileCoord{37, 7}) + any_stage(lab, TileCoord{36, 7}), 0);
    } TEST_END();

    TEST_CASE("AI54.16 A Swimmer That Is Too Far Is Not Sent For A Bridge For An Ant (A Hurt Worker On The Satellite West Of The Hill): With The Three Swimmers 34 Tiles From The First Tile Nothing Is Dug, With Them 14 Tiles Away The Way Stands")
    {
        for (const bool far : {true, false}) {
            Lab lab([](LevelPlan& p) { plans(p, false, true, false, false); });
            swimmers(lab, 3, far ? TileCoord{49, 50} : TileCoord{29, 30});
            const uint32_t ant = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
            lab.sim.get_unit(ant).hp = 2;
            lab.run(1500);
            ASSERT_EQ(standing(lab, kWestChannel, 2) == 2, !far);
            if (far) ASSERT_EQ(lab.bot()->flower_link().bridges().tiles_ordered(), 0u);
        }
    } TEST_END();

    TEST_CASE("AI54.17 An Ant That Stands On A Bridge Of The Planner's That Is About To Fall Is Walked Off It (A Hurt Worker On The Satellite West Of The Hill Gets The Bridge; At Tick 600 A Second Worker Is Put On Its First Tile, The Guard Of The Island Task Is Off): At Tick 4,300, After The Tiles Went, The Worker Is Alive")
    {
        Lab lab([](LevelPlan& p) {
            plans(p, false, true, false, false);
            p.island_guard = false;
            p.island_swarm_swimmers = 0;
        });
        swimmers(lab, 3, TileCoord{14, 19});
        const uint32_t hurt = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestSatellite);
        lab.sim.get_unit(hurt).hp = 2;
        lab.run(600);
        ASSERT_EQ(standing(lab, kWestChannel, 2), 2);
        const uint32_t stands = lab.sim.spawn_unit(0, sim::AntType::Worker, kWestChannel[0]);
        lab.run(3700);
        ASSERT_TRUE(lab.ant(stands) != nullptr);
        ASSERT_EQ(standing(lab, kWestChannel, 2), 0);                                           // (the tiles went at 3,600 ticks after they were finished)
        ASSERT_TRUE(lab.bot()->flower_link().bridges().evacuated() >= 1u);
    } TEST_END();

    TEST_CASE("AI54.18 No Ant Is Lost To A Bridge That The Planner Dug (Four Hard Bots On ISLANDS As The Arena Plays Them, Six Seeds, All Four Rules On): No Ant Drowns On A Tile That Was A Finished Bridge A Tick Before")
    {
        for (const uint32_t seed : {4u, 6u, 12u, 13u, 18u, 21u}) {
            island_test::Match m;
            m.expedition = true;
            m.ferry = true;
            m.builders = 0;
            m.styled = true;
            m.latency = 3;
            m.init("ISLANDS", seed, 0x0f, Level::Hard, 0, [](LevelPlan& p) { p.island_flower_link = true; p.island_crew_spare = 2; });
            m.run(14400);
            ASSERT_EQ(m.total_collapsed(), 0);
        }
    } TEST_END();
}
