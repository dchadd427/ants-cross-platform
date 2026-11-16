// AI44: the flower link and the swarm (island_link.hpp, island_swarm.hpp, docs/BOTS.md "The swarm"). ISLANDS, one Hard bot on seat 0: a Swimmer digs the one tile between the western half of the ring
// and the top flower strip, a spare ant of the ring walks over, waits at the strip and takes the Swimmer that the flower lets fall; the corner rows that nobody works are taken on foot. The expedition,
// the miner and the mines are off, so the ants that a test puts on the ring are the only ones there (the tests of the rows on foot switch the expedition on).
#include <functional>
#include <memory>

#include "ai_test.hpp"
#include "b41_helpers.hpp"
#include "island_fixture.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_sim/movement_tables.hpp"

namespace {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

struct Lab {
    sim::SimulationEngine sim;
    std::unique_ptr<RecordingSink> sink;
    std::unique_ptr<BotController> ctl;
    uint8_t seat_{0};

    explicit Lab(const std::function<void(LevelPlan&)>& tweak, const char* map = "ISLANDS", uint8_t roster = 0x01, uint8_t seat = 0) : seat_(seat) {
        start_match(sim, map, 1, roster);
        sink = std::make_unique<RecordingSink>(sim, true);
        ctl = std::make_unique<BotController>(sim, 1);
        LevelPlan plan = plan_for(Level::Hard);
        plan.island_expedition = false;
        plan.island_fly_miner = false;
        plan.island_mines = false;
        plan.island_builders = 0;
        plan.island_swarm_rows = 0;                                                             // (a test of the rows on foot says so)
        if (tweak) tweak(plan);
        BotSpec spec;
        spec.seat = seat;
        spec.kind = "standard";
        spec.level = Level::Hard;
        std::string why;
        ctl->add(spec, std::make_unique<StandardBot>(plan), *sink, why);
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            sim.tick();
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
        }
    }
    StandardBot* bot() { return dynamic_cast<StandardBot*>(ctl->bot(seat_)); }
    const sim::AntSnapshot* ant(uint32_t id) const {
        for (const auto& a : sim.get_world_state().ants) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    bool bridge_at(TileCoord t) const { return sim.grid().get_cell(t).has_completed_bridge(); }
};

const TileCoord kBridge{23, 7};          // the water tile between the western half of the ring (22, 6) and the top strip (24, 6)
const TileCoord kDrop{30, 5};            // the drop tile of the top flower
const TileCoord kRing{20, 6};            // a tile of the western half of the ring, near the bridge
const TileCoord kStrip{27, 4};           // a tile of the top strip

bool on_strip(const sim::AntSnapshot* a) { return a != nullptr && a->tile_x >= 24 && a->tile_y <= 6; }

/// The tiles of the corner rows of ISLANDS (the groups of seven tokens), found on a fresh engine before anything is taken
std::vector<std::vector<TileCoord>> row_tiles(sim::SimulationEngine& sim, uint8_t team = 0) {
    const MapInfo map(sim);
    const IslandInfo info = IslandInfo::analyse(map, sim.grid(), team);
    std::vector<std::vector<TileCoord>> out;
    for (const TokenGroup& g : info.token_groups()) {
        if (g.members.size() < 7) continue;
        std::vector<TileCoord> tiles;
        for (const uint32_t m : g.members) tiles.push_back(map.powerups()[m].tile);
        out.push_back(std::move(tiles));
    }
    return out;
}

/// The Swimmer power-ups that still lie in each of those rows, counted on the engine's grid
std::vector<uint32_t> swimmers_in_rows(const sim::SimulationEngine& sim, const std::vector<std::vector<TileCoord>>& rows) {
    std::vector<uint32_t> out;
    for (const std::vector<TileCoord>& tiles : rows) {
        uint32_t n = 0;
        for (const TileCoord t : tiles) n += sim.grid().has_powerup_at(t) && sim.grid().get_powerup_type(t) == 5 ? 1u : 0u;
        out.push_back(n);
    }
    return out;
}

}  // namespace

void run_flower_link_tests() {
    TEST_CASE("AI44.1 The Links Are Found In The Analysis: On ISLANDS Each Ring Half Is Joined To Every Strip Whose Flower Is One Or Two Water Tiles Away (West To The Top Strip At (23, 7), East To The Bottom One At (36, 52), And Each Half To The Other Strip), Not A Satellite; On SMALL There Is None")
    {
        Lab lab({});
        lab.run(50);
        const auto& links = lab.bot()->flower_link().link_list();
        ASSERT_EQ(links.size(), 4u);
        bool west = false;
        bool east = false;
        size_t to_top = 0;
        size_t to_bottom = 0;
        for (const auto& l : links) {
            ASSERT_TRUE(l.tiles.size() == 1u || l.tiles.size() == 2u);
            west = west || (l.tiles.size() == 1u && l.tiles[0] == kBridge && l.drop == kDrop);
            east = east || (l.tiles.size() == 1u && l.tiles[0] == TileCoord{36, 52} && l.drop == TileCoord{30, 56});
            to_top += l.drop == kDrop ? 1u : 0u;
            to_bottom += l.drop == TileCoord{30, 56} ? 1u : 0u;
        }
        ASSERT_TRUE(west && east);
        ASSERT_EQ(to_top, 2u);                                                                  // (two links end at each drop tile: the swarm's ants of both ring halves wait there)
        ASSERT_EQ(to_bottom, 2u);
        Lab small({}, "SMALL");
        small.run(50);
        ASSERT_EQ(small.bot()->flower_link().link_list().size(), 0u);
    } TEST_END();

    TEST_CASE("AI44.2 A Swimmer Digs The Tile For An Ant That Waits On The Ring Half, And The Ant Walks Over To The Strip (A Swimmer And A Worker On The Ring): The Tile Stands Within 500 Ticks And The Worker Is On The Strip")
    {
        Lab lab({});
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
        const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
        lab.run(500);
        ASSERT_TRUE(lab.bridge_at(kBridge));
        ASSERT_TRUE(lab.bot()->flower_link().tiles_ordered() >= 1u);
        ASSERT_TRUE(on_strip(lab.ant(worker)));
        ASSERT_EQ(lab.bot()->flower_link().failures(), 0u);
        lab.run(900);                                                                           // (the first drop lands at tick 1,217: if it is a Swimmer the worker has taken it)
        ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Worker || lab.ant(worker)->raw_type == sim::AntType::Swimmer);
    } TEST_END();

    TEST_CASE("AI44.3 Nothing Is Dug For Nobody, Or With The Switch Off: A Swimmer Alone On The Ring Digs Nothing (The Hill's Own Workers Are On Their Island: They Wait For Nothing On The Ring); With `island_flower_link` Off The Worker Stays")
    {
        {
            Lab lab({});
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
            lab.run(1200);
            ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
            ASSERT_TRUE(!lab.bridge_at(kBridge));
        }
        {
            Lab lab([](LevelPlan& p) { p.island_flower_link = false; });
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
            lab.run(2000);
            ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
            ASSERT_TRUE(!on_strip(lab.ant(worker)));
        }
    } TEST_END();

    TEST_CASE("AI44.4 The Ant At The Strip Takes The Swimmer That Lies On The Drop Tile (A Worker On The Strip, A Swimmer Power-Up Put On The Drop Tile): It Is A Swimmer Within 400 Ticks; With Three Swimmers Already And `island_swarm_swimmers` 3 It Takes Nothing, With 8 It Takes It")
    {
        for (const uint32_t most : {3u, 8u}) {
            Lab lab([most](LevelPlan& p) { p.island_swarm_swimmers = most; p.island_ferry = false; });
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 4});
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kStrip);
            lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
            lab.run(400);
            ASSERT_TRUE(lab.ant(worker) != nullptr);
            ASSERT_EQ(lab.ant(worker)->raw_type == sim::AntType::Swimmer, most == 8u);
        }
    } TEST_END();

    TEST_CASE("AI44.5 A Fire Ant On The Strip Takes The Swimmer As Well, And Leaves Its Hat; A Thief Drop Is Left Alone (Not By A Free Ant On The Strip Of A Bot That Wants Thieves Of Its Own: With No Place For The Swarm There)")
    {
        {
            Lab lab({});
            const uint32_t fire = lab.sim.spawn_unit(0, sim::AntType::Fire, kStrip);
            lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
            lab.run(400);
            ASSERT_TRUE(lab.ant(fire)->raw_type == sim::AntType::Swimmer);
        }
        {
            Lab lab([](LevelPlan& p) { p.island_swarm_stage = 0; }, "ISLANDS", 0x03);                           // (no place for the swarm at the strip: the worker is free for the pick-up task)
            const uint32_t walker = lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 3});                // (an enemy ant that walks: the bot sees a team that plays, and wants Thieves of its own)
            lab.sim.apply_command(b41::command_of(CommandType::GroupMove, 1, {walker}, 52, 4));
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kStrip);
            lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 3);
            lab.run(500);
            ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Worker);
        }
    } TEST_END();

    TEST_CASE("AI44.6 A Tile That Is Gone Is Dug Again While An Ant Waits (Three Swimmers And One Worker On The Ring; A Second Worker Comes At Tick 3,500, When The Tile Has Little Life Left): The Tile Is Dug At Once, Goes At 3,600 Ticks, Is Dug Again For The Second Worker, Which Crosses; Nobody Drowns; With `island_swarm_swimmers` 3 (The Bot Has The Three It Wants) Nothing Is Dug")
    {
        for (const uint32_t most : {8u, 3u}) {
            Lab lab([most](LevelPlan& p) { p.island_swarm_swimmers = most; });
            for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6 - i});
            lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
            lab.run(3500);
            const uint32_t late = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{19, 5});
            lab.run(2200);
            if (most == 3u) {
                ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 0u);
                continue;
            }
            ASSERT_EQ(lab.bot()->flower_link().tiles_ordered(), 2u);                             // (the tile went at 3,600 ticks and was dug again for the ant that came)
            ASSERT_EQ(lab.bot()->flower_link().failures(), 0u);
            ASSERT_TRUE(lab.ant(late) != nullptr);
            ASSERT_TRUE(on_strip(lab.ant(late)) || lab.ant(late)->raw_type == sim::AntType::Swimmer);       // (nobody went down with the tile)
        }
    } TEST_END();

    TEST_CASE("AI44.7 An Enemy Ant Beside The Drop Tile Does Not Make A Trip For The Drop Give It Up (No Place For The Swarm At The Strip, The Bot Has No Swimmer; An Enemy Worker On The Tile Next To The Drop Tile, Equally Near): The Free Worker Takes The Swimmer Within 400 Ticks")
    {
        Lab lab([](LevelPlan& p) { p.island_swarm_stage = 0; p.island_ferry = false; }, "ISLANDS", 0x03);
        const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{29, 4});
        lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{31, 4});
        lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
        lab.run(400);
        ASSERT_TRUE(lab.ant(worker) != nullptr);
        ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Swimmer);
    } TEST_END();

    TEST_CASE("AI44.8 An Ant With Fewer Than Three Hit Points Is Not Sent To The Strip, One With Three Is (A Whole Worker And A Hurt One On The Ring Half, A Swimmer That Digs The Tile For The First)")
    {
        for (const uint32_t hp : {2u, 3u}) {
            Lab lab({});
            lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
            lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);                                 // (a whole ant: the tile is dug for it)
            const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{19, 5});
            lab.sim.get_unit(worker).hp = static_cast<uint16_t>(hp);
            lab.run(700);
            ASSERT_TRUE(lab.bridge_at(kBridge));
            ASSERT_TRUE(lab.ant(worker) != nullptr);
            ASSERT_EQ(on_strip(lab.ant(worker)), hp == 3u);
        }
    } TEST_END();

    TEST_CASE("AI44.9 Two Ants Wait At One Strip, Not Three (`island_swarm_stage` 2): Three Workers On The Ring Half Beside The Dug Tile, The Third Does Not Stand Beside The Drop Tile Of The Top Strip")
    {
        Lab lab([](LevelPlan& p) { p.island_ferry = false; });
        for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 4});
        std::vector<uint32_t> workers;
        for (const TileCoord t : {TileCoord{19, 5}, TileCoord{19, 6}, kRing}) workers.push_back(lab.sim.spawn_unit(0, sim::AntType::Worker, t));
        lab.run(900);
        uint32_t beside = 0;
        for (const uint32_t w : workers) {
            const sim::AntSnapshot* a = lab.ant(w);
            ASSERT_TRUE(a != nullptr);
            beside += std::max(std::abs(a->tile_x - kDrop.x), std::abs(a->tile_y - kDrop.y)) <= 1 ? 1u : 0u;
        }
        ASSERT_EQ(beside, 2u);                                                                  // (the third is sent to the other strip: each strip has its own two before one has three)
    } TEST_END();

    TEST_CASE("AI44.10 A Plain Ant Is Sent Before A Bomber (One Strip Place, A Bomber And A Worker On The Ring Half): The Worker Takes The Swimmer That Lies On The Drop Tile, The Bomber Stays A Bomber")
    {
        Lab lab([](LevelPlan& p) { p.island_swarm_stage = 1; p.island_ferry = false; });
        for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 4});
        const uint32_t bomber = lab.sim.spawn_unit(0, sim::AntType::Bomber, TileCoord{19, 6});
        const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
        lab.run(700);
        lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
        lab.run(400);
        ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Swimmer);
        ASSERT_TRUE(lab.ant(bomber)->raw_type == sim::AntType::Bomber);
    } TEST_END();

    TEST_CASE("AI44.11 The Rows Of Empty Seats Are Taken On Foot (One Hard Bot On Seat 0, The Other Seats Empty, Seed 1): After Its Own Row Is Done Its Spare Ants Walk To The Other Row Of The Ring Half And Take Swimmers There; A Switch Of 0 Takes None")
    {
        for (const uint32_t after : {1300u, 0u}) {
            Lab lab([after](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = after; });
            const auto tiles = row_tiles(lab.sim);
            ASSERT_EQ(tiles.size(), 4u);
            lab.run(2000);
            ASSERT_EQ(lab.bot()->expedition().foot_rows() >= 1u, after != 0u);                  // (the row of an empty seat is nobody's from `island_swarm_rows`, not from `island_swarm_idle`)
            lab.run(4000);
            const std::vector<uint32_t> rows = swimmers_in_rows(lab.sim, tiles);
            uint32_t left = 0;
            for (const uint32_t n : rows) left += n;
            if (after == 0u) {
                ASSERT_EQ(lab.bot()->expedition().foot_rows(), 0u);
                ASSERT_EQ(left, 9u);                                                            // (three Swimmers of the own row are gone, the three rows of the empty seats are whole)
                continue;
            }
            ASSERT_TRUE(lab.bot()->expedition().foot_rows() >= 1u);
            ASSERT_TRUE(left <= 8u);                                                            // (at least one Swimmer of another row is taken)
        }
    } TEST_END();

    TEST_CASE("AI44.12 The Row Of A Seat That Plays Is Left Alone Until `island_swarm_idle` (Seats 0 And 2 On The West Half, Seat 2 Does Not Move): Its Swimmers Are All There At Tick 3,000 With The Switch At 6,000, And Taken After That")
    {
        for (const uint32_t idle : {6000u, 2400u}) {
            Lab lab([idle](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = 1300; p.island_swarm_idle = idle; }, "ISLANDS", 0x05);
            const auto tiles = row_tiles(lab.sim);
            lab.run(3000);
            const std::vector<uint32_t> rows = swimmers_in_rows(lab.sim, tiles);
            uint32_t whole = 0;
            for (const uint32_t n : rows) whole += n == 3u ? 1u : 0u;
            if (idle == 6000u) {
                ASSERT_EQ(lab.bot()->expedition().foot_rows(), 0u);
                ASSERT_TRUE(whole >= 3u);                                                       // (the bot took one row; the row of seat 2 and the two of the east half are whole)
            } else {
                ASSERT_TRUE(lab.bot()->expedition().foot_rows() >= 1u);
            }
        }
    } TEST_END();

    TEST_CASE("AI44.13 Nothing Changes Where There Is No Flower Link (SMALL, One Hard Bot): No Row Is Taken On Foot And The Swimmers It Wants Stay The Level's")
    {
        Lab lab([](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = 1; }, "SMALL");
        lab.run(3000);
        ASSERT_EQ(lab.bot()->expedition().foot_rows(), 0u);
        ASSERT_EQ(lab.bot()->swarm().sent(), 0u);
    } TEST_END();

    TEST_CASE("AI44.14 A Seat That Is Not In The Roster Has No Row Of Its Own, Whichever Seat The Bot Has (One Hard Bot On Seat 2, The Other Seats Empty): The Other Row Of The West Half Is Under Way By Tick 2,000, Long Before `island_swarm_idle`")
    {
        Lab lab([](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = 1300; }, "ISLANDS", 0x04, 2);
        lab.run(2000);
        ASSERT_TRUE(lab.bot()->expedition().foot_rows() >= 1u);
    } TEST_END();

    TEST_CASE("AI44.15 A Row That Somebody Has Begun Is Left Alone (One Hard Bot On Seat 0; At Tick 10 One Swimmer Of The Bottom Row Of The West Half Is Gone): No Row Is Taken On Foot In 4,000 Ticks And The Two Swimmers Are There")
    {
        Lab lab([](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = 1300; });
        const auto tiles = row_tiles(lab.sim);
        ASSERT_EQ(tiles.size(), 4u);
        const std::vector<TileCoord>& row = tiles[2];                                           // (the rows come in the order of IslandInfo::token_groups: (3, 1), (56, 1), (0, 56), (59, 56))
        ASSERT_TRUE(row.front().x == 0 && row.front().y == 56);
        lab.run(10);
        for (const TileCoord t : row) {
            if (lab.sim.grid().get_powerup_type(t) != 5) continue;
            lab.sim.grid_mut().clear_powerup(t.x, t.y);
            break;
        }
        lab.run(4000);
        ASSERT_EQ(lab.bot()->expedition().foot_rows(), 0u);
        ASSERT_EQ(swimmers_in_rows(lab.sim, tiles)[2], 2u);
    } TEST_END();

    TEST_CASE("AI44.16 An Enemy Ant Within Eight Tiles Of A Row Means That Somebody Works It (One Hard Bot On Seat 0, Seat 1 In The Roster; An Enemy Worker Eight Tiles From The First Token Of The Bottom Row Of The West Half): No Row On Foot; With Nine Tiles The Row Is Taken")
    {
        for (const int gap : {8, 9}) {
            Lab lab([](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = 1300; }, "ISLANDS", 0x03);
            const auto tiles = row_tiles(lab.sim);
            ASSERT_EQ(tiles.size(), 4u);
            lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{tiles[2].front().x, tiles[2].front().y - gap});
            lab.run(4000);
            ASSERT_EQ(lab.bot()->expedition().foot_rows() >= 1u, gap == 9);
        }
    } TEST_END();

    TEST_CASE("AI44.17 The Last Row That Nobody Works Is Worked Once, Not Planned Again At Every Second Look (One Hard Bot On Seat 0; Enemy Workers Beside The Two Rows Of The East Half, So That Only The Bottom Row Of The West Half Is Open, And Two Plain Workers On The West Half): At Most Three Rows On Foot In 6,000 Ticks And At Least One Swimmer Of That Row Is Gone")
    {
        Lab lab([](LevelPlan& p) { p.island_expedition = true; p.island_swarm_rows = 1300; }, "ISLANDS", 0x03);
        const auto tiles = row_tiles(lab.sim);
        ASSERT_EQ(tiles.size(), 4u);
        lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{tiles[1].front().x - 4, tiles[1].front().y + 1});
        lab.sim.spawn_unit(1, sim::AntType::Worker, TileCoord{tiles[3].front().x - 3, tiles[3].front().y - 4});
        lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{1, 30});                           // (two plain ants on the west half: the Fire that the bot wants takes the crew's spare)
        lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{1, 31});
        lab.run(6000);
        ASSERT_TRUE(lab.bot()->expedition().foot_rows() >= 1u);
        ASSERT_TRUE(lab.bot()->expedition().foot_rows() <= 3u);
        ASSERT_TRUE(swimmers_in_rows(lab.sim, tiles)[2] <= 2u);
    } TEST_END();

    TEST_CASE("AI44.18 The Ants At The Strips Go Free When The Bot Has The Swimmers It Wants (`island_swarm_swimmers` 4, Three Swimmers, A Worker That Waits At The Strip): A Fourth Swimmer Frees The Worker, And It Takes No Drop After That")
    {
        Lab lab([](LevelPlan& p) { p.island_swarm_swimmers = 4; p.island_ferry = false; });
        for (int i = 0; i < 3; ++i) lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{10 + i, 4});
        const uint32_t worker = lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
        lab.run(700);
        ASSERT_EQ(lab.bot()->swarm().staged(), 1u);
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{13, 4});
        lab.run(60);
        ASSERT_EQ(lab.bot()->swarm().staged(), 0u);
        lab.sim.grid_mut().place_powerup(kDrop.x, kDrop.y, 5);
        lab.run(400);
        ASSERT_TRUE(lab.ant(worker)->raw_type == sim::AntType::Worker);
    } TEST_END();

    TEST_CASE("AI44.19 An Ant That Carries Food Is Not Sent To The Strip (A Whole Worker Digs The Way, A Worker With Food Stands Beside It): The Whole One Is At The Strip, The Carrier Is Not")
    {
        Lab lab({});
        lab.sim.spawn_unit(0, sim::AntType::Swimmer, TileCoord{19, 6});
        lab.sim.spawn_unit(0, sim::AntType::Worker, kRing);
        const uint32_t carrier = lab.sim.spawn_unit(0, sim::AntType::Worker, TileCoord{19, 5});
        lab.sim.get_unit(carrier).pick_up_food();
        lab.run(700);
        ASSERT_TRUE(lab.bridge_at(kBridge));
        ASSERT_TRUE(lab.ant(carrier) != nullptr);
        ASSERT_TRUE(!on_strip(lab.ant(carrier)));
    } TEST_END();

    TEST_CASE("AI44.20 The Crew's Spares Go Free While The Row Is Still Being Worked (One Hard Bot, The Expedition On): At Tick 1,100 Two Swimmers Are Taken, The Third Is Still To Come, And Fewer Than Three Of The Crew Are Held")
    {
        Lab lab([](LevelPlan& p) { p.island_expedition = true; });
        lab.run(1100);
        const ExpeditionTask& e = lab.bot()->expedition();
        ASSERT_TRUE(e.active());
        ASSERT_EQ(e.swimmers_taken(), 2u);
        ASSERT_TRUE(e.crew_size() < 3u);                                                        // (the ant for the third Swimmer and one spare: the others went to the swarm)
    } TEST_END();

    TEST_CASE("AI44.21 A Row On Foot That An Ant On Its Way Answers With \"Can't Go There.\" Is Left Alone (Three Hard Bots On Seats 0 To 2 As The Arena Plays Them, Seed 1): No Ant Is Told To Walk To One Tile Beside A Row More Than Ten Times In 3,000 Ticks, And A Row Or Two Were Given Up, Not Every Second Look")
    {
        island_test::Match m;
        m.expedition = true;
        m.ferry = true;
        m.builders = 0;
        m.styled = true;
        m.latency = 3;
        m.init("ISLANDS", 1, 0x07, Level::Hard, 0, [](LevelPlan& p) { p.island_flower_link = true; p.island_crew_spare = 2; });
        const auto rows = row_tiles(m.sim);
        ASSERT_EQ(rows.size(), 4u);
        m.run(3000);
        const auto beside_a_row = [&](int x, int y) {
            for (const std::vector<TileCoord>& row : rows) {
                for (const TileCoord t : row) {
                    if (std::max(std::abs(t.x - x), std::abs(t.y - y)) <= 8) return true;
                }
            }
            return false;
        };
        std::map<std::tuple<uint8_t, uint32_t, int, int>, uint32_t> told;                       // (seat, ant, tile) -> orders
        uint32_t most = 0;
        for (const auto& entry : m.log()) {
            const sim::Command& c = entry.second;
            if (c.type != sim::CommandType::GroupMove || c.ants.size() != 1 || !beside_a_row(c.tile_x, c.tile_y)) continue;
            most = std::max(most, ++told[std::make_tuple(c.issuer, c.ants[0], int(c.tile_x), int(c.tile_y))]);
        }
        ASSERT_TRUE(most <= 10u);
        uint32_t blocks = 0;
        for (uint8_t seat = 0; seat < 3; ++seat) blocks += m.bot(seat)->expedition().foot_blocks();
        ASSERT_TRUE(blocks >= 1u && blocks <= 4u);                                              // (a row that was given up is not planned again for a while: with no pause it would be given up at every second look)
    } TEST_END();
}
