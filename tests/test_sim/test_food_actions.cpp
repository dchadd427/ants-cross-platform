// Golden tests of the food of the original game (Ants.exe): a food pile is an object of the map (LVL Block 2: anchor, units,
// points per unit, stage list); an ordered ant walks up to it, plays the grab clip as action 5, and the cleanup of that action
// (FUN_0101e342) takes one unit, makes the ant carry the object's points, posts "Got Food!" and redraws a pile whose stage
// changed; the ant then goes home. A lunchbox is a food object of one unit. (docs/GAME_REVERSE_ENGINEERING.md 5.40)
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ants::sim;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(96) << name << " ... " << std::flush;
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

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

namespace {

namespace mv = ants::sim::movement;

constexpr int kTickMs = 50;
constexpr uint16_t kNewsGotFood = 60;          // text 0x3c "Got Food!"
constexpr uint16_t kNewsAlreadyHaveFood = 17;  // text 0x11 "Can't - already have food."
constexpr uint16_t kGone = 0x7FFE;             // stage tile "the pile is gone"

// A food object as an LVL Block-2 entry describes it: `units` units of `value` points and the stage list {threshold, tile}.
int32_t place_pile(SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value,
                   const std::vector<std::pair<uint16_t, uint16_t>>& stages) {
    FoodObject o;
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

// Crackers (tiles 369..372 cover 2x2 cells around the anchor) with 4 stages of `units` = 4: 4, 3, 2, 1 units.
int32_t place_crackers(SimulationEngine& sim, int32_t col, int32_t row, uint16_t value = 25) {
    return place_pile(sim, col, row, 4, value, {{4, 369}, {3, 370}, {2, 371}, {1, 372}, {0, kGone}});
}

void make_world(SimulationEngine& sim, uint32_t seed = 1) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{2, 2});
    sim.set_anthill(1, TileCoord{50, 50});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
}

void run_ms(SimulationEngine& sim, int ms) {
    for (int t = 0; t < ms / kTickMs; ++t) sim.tick();
}

// Ticks until `pred` holds or `max_ms` passed; returns the elapsed time in ms (-1 = never).
int wait_ms(SimulationEngine& sim, int max_ms, const std::function<bool()>& pred) {
    for (int t = 0; t <= max_ms / kTickMs; ++t) {
        if (pred()) return t * kTickMs;
        sim.tick();
    }
    return -1;
}

TileCoord tile_of(const AntUnit& a) { return TileCoord{a.pixel_x / 32, a.pixel_y / 32}; }

const FoodObject& object(const SimulationEngine& sim, int32_t index) {
    return sim.grid().food_objects()[static_cast<size_t>(index)];
}

uint16_t layer2(const SimulationEngine& sim, int32_t x, int32_t y) {
    return sim.grid().get_cell(TileCoord{x, y}).interactive_id;
}

// The clip the grab plays for an ant type facing `dir` (the colour-0 row of the original's static table).
mv::MotionClip harvest_clip(uint8_t type, Direction dir) {
    return mv::action_clip(mv::ActionClip::Harvest, type, static_cast<uint8_t>(dir), false);
}

int count_news(std::vector<NewsEvent>& ev, uint16_t string_id) {
    int n = 0;
    for (const auto& e : ev) if (e.string_id == string_id) ++n;
    return n;
}

int count_sound(std::vector<AudioEvent>& ev, uint32_t id) {
    int n = 0;
    for (const auto& e : ev) if (e.sound_id == id) ++n;
    return n;
}

bool load_map(const char* name, ants::assets::LevelData& level) {
    return level.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL");
}

// ------------------------------------------------------------------------------------------------
// Suite 1: the food objects of the original maps
// ------------------------------------------------------------------------------------------------

void run_suite_1_maps() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] 1. Food objects of the original maps\n"
              << "=======================================================\n";

    TEST_CASE("1.1 Every Block-2 entry of the six maps is one food object: anchor, units, points per unit, stage list") {
        struct Expect { const char* map; size_t count; };
        const Expect maps[] = {{"GAUNTLET", 2}, {"ISLANDS", 10}, {"MEDIUM", 4}, {"SMALL", 5}, {"TINY", 14}, {"TREASURE", 18}};
        for (const auto& m : maps) {
            ants::assets::LevelData lv;
            ASSERT_TRUE(load_map(m.map, lv));
            SimulationEngine sim;
            sim.init(lv, 1);
            const auto& objs = sim.grid().food_objects();
            ASSERT_EQ(objs.size(), m.count);
            ASSERT_EQ(lv.food_schedules.size(), m.count);
            for (size_t i = 0; i < objs.size(); ++i) {
                const auto& fs = lv.food_schedules[i];
                ASSERT_EQ(objs[i].col, fs.x);                        // anchor column
                ASSERT_EQ(objs[i].row, fs.y);                        // anchor row
                ASSERT_EQ(objs[i].units, fs.initial_delay);          // the "delay" field is the number of units
                ASSERT_EQ(objs[i].remaining, fs.initial_delay);
                ASSERT_EQ(objs[i].value, fs.respawn_interval);       // the "interval" field is the points per unit
                ASSERT_EQ(objs[i].thresholds.size(), fs.variants.size());
                ASSERT_EQ(objs[i].stage_tiles.size(), fs.variants.size());
            }
        }
    } TEST_END();

    TEST_CASE("1.2 SMALL: the pretzel pile at (21, 10) holds 30 units of 25 points and shows its first stage (tile 264)") {
        ants::assets::LevelData lv;
        ASSERT_TRUE(load_map("SMALL", lv));
        SimulationEngine sim;
        sim.init(lv, 1);
        const int32_t obj = sim.grid().food_object_first_at(TileCoord{21, 10});
        ASSERT_EQ(obj, 2);
        ASSERT_EQ(object(sim, obj).units, 30u);
        ASSERT_EQ(object(sim, obj).value, 25u);
        ASSERT_EQ(object(sim, obj).stage_tile(), 264u);
        ASSERT_EQ(layer2(sim, 21, 10), 264u);
        // the cells of the tile's footprint carry the tile too and belong to the object
        const mv::FootprintSpan span = mv::food_footprint(264);
        ASSERT_TRUE(span.count > 0);
        for (size_t k = 0; k < span.count; ++k) {
            const TileCoord c{21 + span.cells[k].dcol, 10 + span.cells[k].drow};
            ASSERT_EQ(layer2(sim, c.x, c.y), 264u);
            ASSERT_EQ(sim.grid().food_object_at_cell(c), obj);
        }
    } TEST_END();

    TEST_CASE("1.3 The stage list ends at its first entry without a tile: MEDIUM's corn pile keeps a leftover tile at 0 units, the chocolate pile too") {
        ants::assets::LevelData lv;
        ASSERT_TRUE(load_map("MEDIUM", lv));
        SimulationEngine sim;
        sim.init(lv, 1);
        // (10, 10): 100 units, stages 100 -> 361, 70 -> 362, 40 -> 363, 20 -> 364, 0 -> 365 (a leftover crumb, never gone)
        FoodObject corn = object(sim, sim.grid().food_object_first_at(TileCoord{10, 10}));
        ASSERT_EQ(corn.units, 100u);
        ASSERT_EQ(corn.stage_tile(), 361u);
        corn.remaining = 70;
        ASSERT_EQ(corn.stage_tile(), 362u);
        corn.remaining = 41;
        ASSERT_EQ(corn.stage_tile(), 362u);
        corn.remaining = 40;
        ASSERT_EQ(corn.stage_tile(), 363u);
        corn.remaining = 20;
        ASSERT_EQ(corn.stage_tile(), 364u);
        corn.remaining = 1;
        ASSERT_EQ(corn.stage_tile(), 364u);
        corn.remaining = 0;
        ASSERT_EQ(corn.stage_tile(), 365u);
        // (8, 51): the eggs end in a stage without a tile: the pile is gone at 0 units
        FoodObject eggs = object(sim, sim.grid().food_object_first_at(TileCoord{8, 51}));
        ASSERT_EQ(eggs.units, 60u);
        ASSERT_EQ(eggs.stage_tile(), 385u);
        eggs.remaining = 10;
        ASSERT_EQ(eggs.stage_tile(), 388u);
        eggs.remaining = 0;
        ASSERT_EQ(eggs.stage_tile(), kGone);
    } TEST_END();

    TEST_CASE("1.4 A stage list whose first threshold exceeds the units starts in that stage (SMALL (20, 31): 30 units, first stage from 40)") {
        ants::assets::LevelData lv;
        ASSERT_TRUE(load_map("SMALL", lv));
        SimulationEngine sim;
        sim.init(lv, 1);
        const int32_t obj = sim.grid().food_object_first_at(TileCoord{20, 31});
        ASSERT_EQ(object(sim, obj).units, 30u);
        ASSERT_EQ(object(sim, obj).thresholds[0], 40u);
        ASSERT_EQ(object(sim, obj).stage_tile(), 264u);
        ASSERT_EQ(layer2(sim, 20, 31), 264u);
    } TEST_END();

    TEST_CASE("1.5 TREASURE has three objects on the anchor (59, 2) and two on (2, 59): the first one is found by anchor, the last one by cell; the last tile wins on the map") {
        ants::assets::LevelData lv;
        ASSERT_TRUE(load_map("TREASURE", lv));
        SimulationEngine sim;
        sim.init(lv, 1);
        const auto& objs = sim.grid().food_objects();
        // file order: 8 = fdpez2p1 (342, 30 points), 10 and 11 = fdpezp1 (340, 50 points)
        ASSERT_EQ(sim.grid().food_object_first_at(TileCoord{59, 2}), 8);
        ASSERT_EQ(objs[8].value, 30u);
        ASSERT_EQ(objs[10].value, 50u);
        ASSERT_EQ(objs[11].value, 50u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{59, 2}), 11);
        // the objects show one after the other on the same anchor: the second replaced the first one's cells, the third found its tile
        // (340) already on the anchor and changed nothing
        ASSERT_EQ(layer2(sim, 59, 2), 340u);
        // 7 = fdpez2b1 (343, 30 points), 9 = fdpezb1 (341, 50 points)
        ASSERT_EQ(sim.grid().food_object_first_at(TileCoord{2, 59}), 7);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{2, 59}), 9);
        ASSERT_EQ(layer2(sim, 2, 59), 341u);
    } TEST_END();
}

// ------------------------------------------------------------------------------------------------
// Suite 2: the object table and its tiles
// ------------------------------------------------------------------------------------------------

void run_suite_2_objects() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] 2. Food objects: units, stages and tiles\n"
              << "=======================================================\n";

    TEST_CASE("2.1 TakeFood takes min(n, units left), returns the points of the units taken and tells when the stage tile changed") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_crackers(sim, 21, 21, 25);
        bool changed = true;
        ASSERT_EQ(sim.grid_mut().take_food(pile, 1, changed), 25u);
        ASSERT_TRUE(changed);                         // 4 -> 3 units: stage 369 -> 370
        ASSERT_EQ(object(sim, pile).remaining, 3u);
        ASSERT_EQ(sim.grid_mut().take_food(pile, 2, changed), 50u);
        ASSERT_TRUE(changed);                         // 3 -> 1 unit: 370 -> 372
        ASSERT_EQ(object(sim, pile).stage_tile(), 372u);
        ASSERT_EQ(sim.grid_mut().take_food(pile, 5, changed), 25u);   // only one unit was left
        ASSERT_TRUE(changed);                         // 372 -> the last stage (gone)
        ASSERT_EQ(object(sim, pile).remaining, 0u);
        ASSERT_EQ(sim.grid_mut().take_food(pile, 1, changed), 0u);    // nothing to take: no check, no change
        ASSERT_FALSE(changed);
        ASSERT_EQ(object(sim, pile).remaining, 0u);
    } TEST_END();

    TEST_CASE("2.2 A unit that keeps the stage tile changes nothing on the map (units 30 -> 29 of a pile whose tile changes at 20)") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_pile(sim, 21, 21, 3, 20, {{3, 369}, {0, kGone}});
        bool changed = true;
        sim.grid_mut().take_food(pile, 1, changed);
        ASSERT_FALSE(changed);
        ASSERT_EQ(layer2(sim, 21, 21), 369u);
    } TEST_END();

    TEST_CASE("2.3 SetTile of a smaller stage clears the cells the new footprint no longer covers (burger: 16 cells -> 8 cells)") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_pile(sim, 30, 30, 10, 10, {{10, 253}, {6, 255}, {0, kGone}});
        const mv::FootprintSpan big = mv::food_footprint(253);
        const mv::FootprintSpan small = mv::food_footprint(255);
        ASSERT_EQ(big.count, 16u);
        ASSERT_EQ(small.count, 8u);
        for (size_t k = 0; k < big.count; ++k) {
            ASSERT_EQ(layer2(sim, 30 + big.cells[k].dcol, 30 + big.cells[k].drow), 253u);
        }
        // the cell (1, 1) belongs to the first stage only
        ASSERT_TRUE(sim.grid().get_cell(TileCoord{31, 31}).is_food);
        bool changed = false;
        sim.grid_mut().take_food(pile, 4, changed);        // 10 -> 6 units: stage 253 -> 255
        ASSERT_TRUE(changed);
        sim.grid_mut().set_food_tile(TileCoord{30, 30}, object(sim, pile).stage_tile());
        for (size_t k = 0; k < small.count; ++k) {
            ASSERT_EQ(layer2(sim, 30 + small.cells[k].dcol, 30 + small.cells[k].drow), 255u);
        }
        ASSERT_EQ(layer2(sim, 31, 31), TILE_EMPTY);
        ASSERT_FALSE(sim.grid().get_cell(TileCoord{31, 31}).is_food);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{31, 31}), -1);
        ASSERT_FALSE(sim.grid().is_solid_object(TileCoord{31, 31}));
        // the last stage: the pile is gone, every cell is free
        sim.grid_mut().take_food(pile, 6, changed);
        ASSERT_TRUE(changed);
        sim.grid_mut().set_food_tile(TileCoord{30, 30}, object(sim, pile).stage_tile());
        for (size_t k = 0; k < small.count; ++k) {
            ASSERT_EQ(layer2(sim, 30 + small.cells[k].dcol, 30 + small.cells[k].drow), TILE_EMPTY);
        }
        ASSERT_EQ(layer2(sim, 30, 30), TILE_EMPTY);
    } TEST_END();

    TEST_CASE("2.4 A food cell is solid for walkers and belongs to its object; a cell next to the footprint does not") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_crackers(sim, 21, 21);
        // crackers: (20, 20), (21, 20), (20, 21), (21, 21)
        for (int32_t y = 20; y <= 21; ++y) {
            for (int32_t x = 20; x <= 21; ++x) {
                ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{x, y}), pile);
                ASSERT_TRUE(sim.grid().is_solid_object(TileCoord{x, y}));
            }
        }
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{19, 20}), -1);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{22, 21}), -1);
        ASSERT_FALSE(sim.grid().is_solid_object(TileCoord{19, 20}));
    } TEST_END();

    TEST_CASE("2.5 A lunchbox is a food object of one unit worth the points it holds, on its own tile; the object table only grows") {
        SimulationEngine sim;
        make_world(sim);
        const size_t before = sim.grid().food_objects().size();
        sim.grid_mut().drop_lunchbox(15, 15, 40);
        ASSERT_EQ(sim.grid().food_objects().size(), before + 1);
        const int32_t lunch = sim.grid().food_object_first_at(TileCoord{15, 15});
        ASSERT_EQ(lunch, static_cast<int32_t>(before));
        ASSERT_EQ(object(sim, lunch).units, 1u);
        ASSERT_EQ(object(sim, lunch).value, 40u);
        ASSERT_EQ(object(sim, lunch).stage_tile(), 356u);
        ASSERT_EQ(layer2(sim, 15, 15), 356u);
        ASSERT_TRUE(sim.has_lunchbox_at({15, 15}));
        ASSERT_EQ(sim.get_lunchbox_points({15, 15}), 40u);
        // once its unit is taken it is gone from the map, but its record stays (indices are stable)
        bool changed = false;
        ASSERT_EQ(sim.grid_mut().take_food(lunch, 1, changed), 40u);
        ASSERT_TRUE(changed);
        sim.grid_mut().set_food_tile(TileCoord{15, 15}, object(sim, lunch).stage_tile());
        ASSERT_FALSE(sim.has_lunchbox_at({15, 15}));
        ASSERT_EQ(sim.get_lunchbox_points({15, 15}), 0u);
        ASSERT_EQ(sim.grid().food_objects().size(), before + 1);
        sim.grid_mut().drop_lunchbox(15, 15, 60);            // another one on the same tile is a new record
        ASSERT_EQ(sim.grid().food_objects().size(), before + 2);
        ASSERT_EQ(sim.get_lunchbox_points({15, 15}), 60u);
    } TEST_END();
}

// ------------------------------------------------------------------------------------------------
// Suite 3: the harvest
// ------------------------------------------------------------------------------------------------

void run_suite_3_harvest() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] 3. Harvest: grab clip, bite, carrying, going home\n"
              << "=======================================================\n";

    TEST_CASE("3.1 The grab clip: faces the food, plays ?gf, the bite is taken at the clip's end and the ant carries the object's points home") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_crackers(sim, 15, 10, 25);           // cells (14..15, 9..10)
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{13, 10});
        sim.clear_news_events();
        sim.issue_move_order(ant, TileCoord{15, 10});
        const AntUnit& a = sim.get_unit(ant);
        ASSERT_EQ(a.orig_order, AntUnit::kOrderHarvest);
        ASSERT_EQ(a.orig_order_tile, (TileCoord{15, 10}));

        // the pile is the next tile of its path: the walk ends on the spot and the grab starts
        const int t_start = wait_ms(sim, 1500, [&]() { return a.loco_action == AntUnit::kActionHarvest; });
        ASSERT_TRUE(t_start >= 0);
        ASSERT_EQ(a.state, UnitState::HarvestingFood);
        ASSERT_EQ(a.facing, Direction::East);
        ASSERT_EQ(tile_of(a), (TileCoord{13, 10}));
        ASSERT_EQ(a.pixel_x, 13 * 32 + 16);
        ASSERT_EQ(a.pixel_y, 10 * 32 + 16);
        ASSERT_TRUE(a.waypoints.empty());
        ASSERT_FALSE(a.is_holding());
        ASSERT_EQ(object(sim, pile).remaining, 4u);

        // the cue of the clip (sound 77 at its fifth frame) plays once; the bite is taken when the clip ends: the clip's frames
        // plus the first frame once more (the start step of the loco player books the first frame twice, as with every clip)
        const mv::MotionClip clip = harvest_clip(mv::kAntWorker, Direction::East);
        ASSERT_TRUE(clip.valid());
        const int expected = static_cast<int>(clip.total_duration_ms() + clip.duration(0));
        int sounds = 0;
        int t_end = -1;
        for (int t = 0; t <= 1500; t += kTickMs) {
            if (a.is_holding()) { t_end = t; break; }
            sim.tick();
            auto ev = sim.poll_audio_events();
            sounds += count_sound(ev, 77) + count_sound(ev, 66);
        }
        ASSERT_TRUE(t_end >= 0);
        ASSERT_TRUE(std::abs(t_end - expected) < kTickMs + kTickMs);
        ASSERT_EQ(sounds, 1);
        ASSERT_EQ(a.carried_food, 1);
        ASSERT_EQ(a.carried_points, 25u);                     // the object's points per unit
        ASSERT_EQ(a.harvest_origin, (TileCoord{15, 10}));
        ASSERT_EQ(object(sim, pile).remaining, 3u);
        ASSERT_EQ(layer2(sim, 15, 10), 370u);                 // 3 units: the next stage
        auto news = sim.poll_news_events();
        ASSERT_TRUE(sim.has_news_event(0, kNewsGotFood) || count_news(news, kNewsGotFood) == 1);

        // it idles on its tile centre and goes home: the enter order (2) to its own entrance
        ASSERT_EQ(a.pixel_x, 13 * 32 + 16);
        ASSERT_EQ(a.pixel_y, 10 * 32 + 16);
        ASSERT_TRUE(wait_ms(sim, 600, [&]() { return a.orig_order == AntUnit::kOrderHome; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return a.loco_action == AntUnit::kActionWalk; }) >= 0);
    } TEST_END();

    TEST_CASE("3.2 \"Got Food!\" (text 60) goes to the owner of the ant, once per bite") {
        SimulationEngine sim;
        make_world(sim);
        place_crackers(sim, 15, 10);
        const uint32_t ant = sim.spawn_unit(1, AntType::Worker, TileCoord{13, 10});
        sim.clear_news_events();
        sim.issue_move_order(ant, TileCoord{15, 10});
        int got = 0;
        uint8_t target = 255;
        for (int t = 0; t < 40; ++t) {
            sim.tick();
            for (const auto& n : sim.poll_news_events()) {
                if (n.string_id == kNewsGotFood) { ++got; target = n.target_player; }
            }
        }
        ASSERT_EQ(got, 1);
        ASSERT_EQ(target, 1);
    } TEST_END();

    TEST_CASE("3.3 Every ant type grabs with its own clip (?gf): duration and cue by type and direction, taken at the clip's end") {
        const AntType types[] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
        for (int ti = 0; ti < 6; ++ti) {
            SimulationEngine sim;
            make_world(sim);
            place_crackers(sim, 15, 10);
            const uint32_t ant = sim.spawn_unit(0, types[ti], TileCoord{13, 10});
            sim.issue_move_order(ant, TileCoord{15, 10});
            const AntUnit& a = sim.get_unit(ant);
            ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return a.loco_action == AntUnit::kActionHarvest; }) >= 0);
            const mv::MotionClip clip = harvest_clip(static_cast<uint8_t>(ti), Direction::East);
            ASSERT_TRUE(clip.valid());
            int t_taken = -1;
            for (int t = 0; t <= 1500; t += kTickMs) {
                if (a.is_holding()) { t_taken = t; break; }
                sim.tick();
            }
            ASSERT_TRUE(t_taken >= 0);
            ASSERT_TRUE(std::abs(t_taken - static_cast<int>(clip.total_duration_ms() + clip.duration(0))) < 2 * kTickMs);
        }
    } TEST_END();

    TEST_CASE("3.4 A pile takes one unit per bite through all its stages and is gone (tiles, cells, solidity) after the last one") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_crackers(sim, 15, 10, 30);
        const uint16_t stages[4] = {369, 370, 371, 372};
        for (int bite = 0; bite < 4; ++bite) {
            ASSERT_EQ(layer2(sim, 15, 10), stages[bite]);
            ASSERT_EQ(layer2(sim, 14, 9), stages[bite]);
            const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{13, 10});
            sim.issue_move_order(ant, TileCoord{15, 10});
            const AntUnit& a = sim.get_unit(ant);
            ASSERT_TRUE(wait_ms(sim, 3000, [&]() { return a.is_holding(); }) >= 0);
            ASSERT_EQ(a.carried_points, 30u);
            ASSERT_EQ(object(sim, pile).remaining, static_cast<uint16_t>(3 - bite));
            // the ant leaves for its hill: make room for the next one
            sim.kill_unit(ant);
            run_ms(sim, 2000);
        }
        ASSERT_EQ(layer2(sim, 15, 10), TILE_EMPTY);
        ASSERT_EQ(layer2(sim, 14, 9), TILE_EMPTY);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{15, 10}), -1);
        ASSERT_FALSE(sim.grid().is_solid_object(TileCoord{15, 10}));
    } TEST_END();

    TEST_CASE("3.5 An ant that carries food does not eat: \"Can't - already have food.\" (text 17), it goes home and keeps the food") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_crackers(sim, 15, 10);
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{13, 10});
        AntUnit& a = sim.get_unit(ant);
        a.holding = 1;
        a.carried_food = 1;
        a.carried_points = 40;
        sim.clear_news_events();
        sim.issue_move_order(ant, TileCoord{15, 10});
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return sim.has_news_event(0, kNewsAlreadyHaveFood); }) >= 0);
        ASSERT_EQ(a.orig_order, AntUnit::kOrderHome);
        ASSERT_EQ(a.harvest_origin, (TileCoord{15, 10}));     // it remembers the food it was sent to (+0xf4)
        ASSERT_EQ(a.carried_points, 40u);                     // and keeps what it has
        ASSERT_EQ(object(sim, pile).remaining, 4u);
        ASSERT_NE(a.loco_action, AntUnit::kActionHarvest);
        ASSERT_FALSE(sim.has_news_event(0, kNewsGotFood));
    } TEST_END();

    TEST_CASE("3.6 An ant that arrives at a pile without units left just stops (no grab, no text)") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_pile(sim, 15, 10, 1, 25, {{1, 372}, {0, 372}});     // the last stage keeps a tile at 0 units
        bool changed = false;
        sim.grid_mut().take_food(pile, 1, changed);
        sim.grid_mut().set_food_tile(TileCoord{15, 10}, object(sim, pile).stage_tile());
        ASSERT_EQ(object(sim, pile).remaining, 0u);
        ASSERT_EQ(sim.grid().food_object_at_cell(TileCoord{15, 10}), pile);
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{13, 10});
        sim.clear_news_events();
        sim.issue_move_order(ant, TileCoord{15, 10});
        const AntUnit& a = sim.get_unit(ant);
        bool grabbed = false;
        for (int t = 0; t < 60; ++t) {
            sim.tick();
            grabbed = grabbed || a.loco_action == AntUnit::kActionHarvest;
        }
        ASSERT_FALSE(grabbed);
        ASSERT_FALSE(a.is_holding());
        ASSERT_EQ(a.orig_order, AntUnit::kOrderNone);
        ASSERT_EQ(a.loco_action, AntUnit::kActionIdle);
        ASSERT_FALSE(sim.has_news_event(0, kNewsGotFood));
        ASSERT_FALSE(sim.has_news_event(0, kNewsAlreadyHaveFood));
    } TEST_END();

    TEST_CASE("3.7 Two ants that both start biting the last unit both get food (the bite is not checked again at its end)") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_pile(sim, 21, 21, 1, 25, {{1, 372}, {0, kGone}});
        const uint32_t w1 = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 20});
        const uint32_t w2 = sim.spawn_unit(0, AntType::Worker, TileCoord{22, 21});
        sim.issue_move_order(w1, TileCoord{21, 21});
        sim.issue_move_order(w2, TileCoord{21, 21});
        const AntUnit& u1 = sim.get_unit(w1);
        const AntUnit& u2 = sim.get_unit(w2);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return u1.loco_action == AntUnit::kActionHarvest && u2.loco_action == AntUnit::kActionHarvest; }) >= 0);
        ASSERT_EQ(object(sim, pile).remaining, 1u);
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return u1.is_holding() && u2.is_holding(); }) >= 0);
        ASSERT_EQ(u1.carried_points, 25u);
        ASSERT_EQ(u2.carried_points, 25u);
        ASSERT_EQ(object(sim, pile).remaining, 0u);
        ASSERT_EQ(layer2(sim, 21, 21), TILE_EMPTY);
    } TEST_END();

    TEST_CASE("3.8 A hit during the grab still gives the food (the cleanup of action 5 does the bite, whatever ends the clip)") {
        SimulationEngine sim;
        make_world(sim);
        const int32_t pile = place_crackers(sim, 15, 10, 25);
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{13, 10});
        const uint32_t foe = sim.spawn_unit(1, AntType::Worker, TileCoord{12, 10});
        sim.issue_move_order(ant, TileCoord{15, 10});
        const AntUnit& a = sim.get_unit(ant);
        ASSERT_TRUE(wait_ms(sim, 1500, [&]() { return a.loco_action == AntUnit::kActionHarvest; }) >= 0);
        run_ms(sim, 100);
        ASSERT_FALSE(a.is_holding());
        sim.clear_news_events();
        sim.execute_melee_attack(foe, ant);
        // the clip is cut, the ant is hit, and it has the food and the pile is a unit poorer
        ASSERT_NE(a.loco_action, AntUnit::kActionHarvest);
        ASSERT_TRUE(a.is_holding());
        ASSERT_EQ(a.carried_points, 25u);
        ASSERT_EQ(object(sim, pile).remaining, 3u);
        ASSERT_TRUE(sim.has_news_event(0, kNewsGotFood));
    } TEST_END();

    TEST_CASE("3.9 After it has deposited its food the ant walks back to the pile and bites again, until the pile is gone; then it just walks to the empty anchor") {
        SimulationEngine sim;
        make_world(sim);
        sim.grid_mut().set_anthill(0, TileCoord{10, 10});
        const int32_t pile = place_pile(sim, 21, 14, 2, 25, {{2, 369}, {1, 370}, {0, kGone}});
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{19, 14});
        sim.issue_move_order(ant, TileCoord{21, 14});
        const AntUnit& a = sim.get_unit(ant);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return a.is_holding(); }) >= 0);
        ASSERT_EQ(object(sim, pile).remaining, 1u);
        ASSERT_EQ(sim.get_player_score(0), 0);
        // it enters the hill, the food scores when the enter clip ends, and it comes back for the next bite
        ASSERT_TRUE(wait_ms(sim, 30000, [&]() { return sim.get_player_score(0) == 25; }) >= 0);
        ASSERT_FALSE(a.is_holding());
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return a.orig_order == AntUnit::kOrderHarvest; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 30000, [&]() { return a.is_holding(); }) >= 0);
        ASSERT_EQ(object(sim, pile).remaining, 0u);
        ASSERT_EQ(layer2(sim, 21, 14), TILE_EMPTY);
        ASSERT_TRUE(wait_ms(sim, 30000, [&]() { return sim.get_player_score(0) == 50; }) >= 0);
        // the pile is gone: the ant's order to the anchor is an ordinary walk that ends on the free tile
        ASSERT_TRUE(wait_ms(sim, 30000, [&]() { return a.orig_order == AntUnit::kOrderMove; }) >= 0);
        ASSERT_TRUE(wait_ms(sim, 30000, [&]() { return a.orig_order == AntUnit::kOrderNone && a.loco_action == AntUnit::kActionIdle; }) >= 0);
        ASSERT_EQ(tile_of(a), (TileCoord{21, 14}));
    } TEST_END();

    TEST_CASE("3.10 A harvest order gets the ants to a pile the way every group order does: closest first, one acknowledgement") {
        SimulationEngine sim;
        make_world(sim);
        place_crackers(sim, 21, 21);
        const uint32_t far = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 21});
        const uint32_t near = sim.spawn_unit(0, AntType::Worker, TileCoord{17, 21});
        const uint32_t ack = sim.issue_group_move_order({far, near}, TileCoord{21, 21});
        ASSERT_EQ(ack, near);
        ASSERT_EQ(sim.get_unit(far).orig_order, AntUnit::kOrderHarvest);
        ASSERT_EQ(sim.get_unit(near).orig_order, AntUnit::kOrderHarvest);
    } TEST_END();

    TEST_CASE("3.11 \"Can't go there.\" with food keeps the food source (+0xf4): a carrier that is sent home later by hand still walks back to its pile (Ants.exe 0x100cbe7: stop, SetActionDefault(0xb), text 58, nothing else)") {
        SimulationEngine sim;
        make_world(sim);
        place_crackers(sim, 15, 10);
        // a team-mate stands idle on the doorway of the hill (3, 2), the only walkable tile in front of the entrance (3, 3): a same-team occupant without a path or an order costs 8000
        const uint32_t blocker = sim.spawn_unit(0, AntType::Worker, TileCoord{3, 2});
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, TileCoord{13, 10});
        const AntUnit& a = sim.get_unit(ant);
        sim.clear_news_events();
        sim.issue_move_order(ant, TileCoord{15, 10});
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return a.is_holding(); }) >= 0);
        ASSERT_EQ(a.harvest_origin, (TileCoord{15, 10}));
        // the walk home is refused: "Can't go there." (text 58)
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return sim.has_news_event(0, strings::kCantGoThere) && a.state == UnitState::CantGo; }) >= 0);
        ASSERT_EQ(a.harvest_origin, (TileCoord{15, 10}));     // the original's count-0 branch writes nothing but the action: the pile is still remembered
        ASSERT_TRUE(wait_ms(sim, 2000, [&]() { return a.state == UnitState::Idle; }) >= 0);
        ASSERT_TRUE(a.is_holding());
        ASSERT_EQ(a.orig_order, AntUnit::kOrderNone);         // it stands there with its food: nothing retries
        ASSERT_EQ(a.harvest_origin, (TileCoord{15, 10}));
        run_ms(sim, 2000);
        ASSERT_TRUE(a.is_holding() && a.state == UnitState::Idle);
        // the doorway is free again and a hand sends it home: it delivers, and then it walks back to the pile it came from (order 5), not to the idle tile beside the hill
        sim.kill_unit(blocker);
        sim.issue_move_order(ant, TileCoord{3, 3});
        ASSERT_TRUE(wait_ms(sim, 30000, [&]() { return !a.is_holding(); }) >= 0);
        ASSERT_EQ(sim.get_player_score(0), 25);
        ASSERT_TRUE(wait_ms(sim, 6000, [&]() { return a.orig_order == AntUnit::kOrderHarvest; }) >= 0);
        ASSERT_EQ(a.orig_order_tile, (TileCoord{15, 10}));
    } TEST_END();
}

// ------------------------------------------------------------------------------------------------
// Suite 4: objects with the same anchor (TREASURE.LVL)
// ------------------------------------------------------------------------------------------------

void run_suite_4_duplicates() {
    std::cout << "\n=======================================================\n"
              << " [SUITE] 4. Two objects on one anchor\n"
              << "=======================================================\n";

    TEST_CASE("4.1 TREASURE (59, 2): the click classifies the LAST object of the cell (50 points), the bite is taken from the FIRST object of the anchor") {
        ants::assets::LevelData lv;
        ASSERT_TRUE(load_map("TREASURE", lv));
        SimulationEngine sim;
        sim.init(lv, 1);
        const auto& objs = sim.grid().food_objects();
        const uint16_t units_first = objs[8].units;
        const uint16_t units_last = objs[11].units;
        // a free tile next to the pile
        TileCoord start{-1, -1};
        for (int32_t dy = -3; dy <= 3 && start.x < 0; ++dy) {
            for (int32_t dx = -3; dx <= 0 && start.x < 0; ++dx) {
                const TileCoord t{59 + dx, 2 + dy};
                if (!sim.grid().in_bounds(t) || sim.grid().food_object_at_cell(t) >= 0) continue;
                if (sim.grid().get_cell(t).is_passable()) start = t;
            }
        }
        ASSERT_TRUE(start.x >= 0);
        const uint32_t ant = sim.spawn_unit(0, AntType::Worker, start);
        sim.issue_move_order(ant, TileCoord{59, 2});
        const AntUnit& a = sim.get_unit(ant);
        ASSERT_EQ(a.orig_food_id, 11);                                  // the classification found the last object of the cell
        ASSERT_TRUE(wait_ms(sim, 20000, [&]() { return a.is_holding(); }) >= 0);
        ASSERT_EQ(a.carried_points, 50u);                               // its points per unit go with the ant
        ASSERT_EQ(objs[8].remaining, static_cast<uint16_t>(units_first - 1));   // ... but StartHarvest took the first object of the anchor
        ASSERT_EQ(objs[11].remaining, units_last);
    } TEST_END();
}

}  // namespace

int main() {
    std::cout << "\n=======================================================\n"
              << " ANTS - ORIGINAL FOOD ACTIONS GOLDEN TESTS\n"
              << "=======================================================\n";
    run_suite_1_maps();
    run_suite_2_objects();
    run_suite_3_harvest();
    run_suite_4_duplicates();

    std::cout << "\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n"
              << " Total Assertions: " << g_assert_count << "\n"
              << " Failures:         " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
