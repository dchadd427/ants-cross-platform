// Bridges that a map FILE holds (docs/GAME_REVERSE_ENGINEERING.md 5.37, "Bridges"). The original's bridge pieces are the layer-2 ids 34 .. 37 (0x22 .. 0x25) and no other: 37 is the completed
// bridge, 34 .. 36 are the pieces of a build in progress, and a piece that a file holds has no collapse timer (only the completion of a swimmer's build arms one, and only with more than 180 s of
// the match left). The id 38 ("bridge4b") is no bridge at all: the terrain class of a cell compares 0x22 .. 0x25 alone, so a water cell that holds 38 stays water. The cases that exist in the library
// are 395 cells of 37 in 16 community maps (the editor leaves the dictionary entry ".") and four part-built pieces; no map holds 38.
// New file for the topic; its own small check() that counts and goes on (the shared tests/common/test_assert.hpp macros return from the case at the first failure, which the loops here do not want).
// The world is TINY with layer 2 emptied and a river of layer-1 water (tile 476) over the columns 12 .. 18 for the whole height; the banks are land.
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace ants::sim;
using ants::assets::LevelData;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_cases = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "    FAILED: " << what << " (line " << line << ")\n";
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

void run_case(const std::string& name, const std::function<void()>& fn) {
    ++g_cases;
    std::cout << "  RUNNING: " << name << " ... " << std::flush;
    const int before = g_failures;
    fn();
    if (g_failures == before) std::cout << "PASS\n";
}

constexpr uint16_t kWaterTile = 476;         // layer 1: water (the original's table, 476 .. 484)
constexpr int kRiver0 = 12, kRiver1 = 18;    // the water columns; the west bank ends at 11, the east bank starts at 19
constexpr uint16_t kDone = TILE_BRIDGE4, kInert = TILE_BRIDGE4B;
constexpr uint32_t kBuiltLife = 3600;       // the life of a bridge that a swimmer built: 180 s of 20 ticks (written out here, so that the engine's constant cannot move with the test)

LevelData river() {
    LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) std::cout << "cannot load TINY.LVL\n";
    const size_t w = level.width, h = level.height;
    const uint16_t grass = level.layer1_terrain[2 * w + 5].tile_index;
    level.food_schedules.clear();                                    // (the food piles of TINY sit on cells of the river: their stage tiles would overwrite layer 2)
    std::vector<bool> hill(level.layer2_interactive.size(), false);
    for (size_t i = 0; i < hill.size(); ++i) {                       // the four hills stay (layer 2); every other object goes, and so does the rock that lay under it
        auto& c = level.layer2_interactive[i];
        hill[i] = ants::assets::hill_team_of_id(c.tile_index) >= 0;
        if (!hill[i]) {
            c.tile_index = ants::assets::LVL_EMPTY_TILE;
            c.flags = 0;
            c.properties = 0;
        }
    }
    for (size_t y = 1; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            auto& c = level.layer1_terrain[y * w + x];
            if (hill[y * w + x]) continue;
            const bool water = x >= static_cast<size_t>(kRiver0) && x <= static_cast<size_t>(kRiver1);
            c.tile_index = water ? kWaterTile : grass;
            c.flags = 0;
        }
    }
    return level;
}

void put(LevelData& level, int x, int y, uint16_t id) {      // a cell as the library's file bridges are: flags 1, properties 0
    auto& c = level.layer2_interactive[static_cast<size_t>(y) * level.width + static_cast<size_t>(x)];
    c.tile_index = id;
    c.flags = 1;
    c.properties = 0;
}
void strip(LevelData& level, int y, uint16_t id, int x0 = kRiver0, int x1 = kRiver1) { for (int x = x0; x <= x1; ++x) put(level, x, y, id); }
void name_tile(LevelData& level, uint16_t id, const std::string& name) {
    if (level.tile_dictionary.size() <= id) level.tile_dictionary.resize(static_cast<size_t>(id) + 1, ".");
    level.tile_dictionary[id] = name;
}

void start(SimulationEngine& sim, const LevelData& level) { sim.init(level, 7, 0x0F); }
void run(SimulationEngine& sim, int ticks) { for (int t = 0; t < ticks; ++t) sim.tick(); }
const TileCell& cell(const SimulationEngine& sim, int x, int y) { return sim.grid().get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)); }
bool drowning(SimulationEngine& sim, uint32_t ant) { return sim.get_unit(ant).state == UnitState::Drowning || sim.get_unit(ant).loco_action == AntUnit::kActionDrown; }
bool well(SimulationEngine& sim, uint32_t ant) { return !sim.get_unit(ant).removed && sim.get_unit(ant).is_alive() && !drowning(sim, ant) && sim.get_unit(ant).hp == 10u; }

AntOrder special_click(uint32_t ant, int x, int y) {
    AntOrder o{};
    o.ant_id = ant;
    o.type = OrderType::BuildBridge;         // (the swimmer's special click: the ant's own classification makes it a build or a demolish)
    o.target_x = x;
    o.target_y = y;
    return o;
}

// A swimmer on the west bank clicks the cell (12, y): the ant refuses ("Can't do that...", the can't clip, no order), and the cell is as it was after a good while
void expect_refused(const LevelData& level, int y, uint16_t id) {
    SimulationEngine sim;
    start(sim, level);
    const uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{kRiver0 - 1, y});
    sim.clear_news_events();
    sim.issue_order(special_click(swimmer, kRiver0, y));
    CHECK(sim.has_news_event(0, strings::kCantDoThat));
    CHECK(sim.get_unit(swimmer).loco_action == AntUnit::kActionCantGo && sim.get_unit(swimmer).orig_order == AntUnit::kOrderNone);
    bool started = false;
    for (int t = 0; t < 600; ++t) {
        sim.tick();
        started = started || sim.get_unit(swimmer).loco_action == AntUnit::kActionBridgeBuild || sim.get_unit(swimmer).loco_action == AntUnit::kActionBridgeDemolish;
    }
    CHECK(!started);
    CHECK(cell(sim, kRiver0, y).interactive_id == id && cell(sim, kRiver0, y).timer_ticks == 0);
}

// Both readers of a level give the cells of one row (x = 12 .. 16 hold 34, 35, 36, 37, 38) the same state
void expect_loaded(const Grid& g, int y) {
    const uint16_t ids[5] = {34, 35, 36, 37, 38};
    for (int k = 0; k < 5; ++k) {
        const TileCoord t{kRiver0 + k, y};
        const TileCell& c = g.get_cell(t);
        const bool bridge = k < 4;
        CHECK(c.interactive_id == ids[k] && c.timer_ticks == 0 && c.interactive_owner == 255 && !c.is_obstacle_overlay);
        CHECK(c.has_any_bridge() == bridge && g.has_bridge_at(t) == bridge && g.get_bridge_stage(t) == (bridge ? k + 1 : 0));
        CHECK(c.has_completed_bridge() == (k == 3) && c.has_partial_bridge() == (k < 3));
        CHECK(g.terrain_class_at(t) == (bridge ? movement::kTerrainMud : movement::kTerrainWater));
    }
}

// One world with every kind of file piece and an ant on some of them, played to the same three moments (a state hash at each)
struct Mixed {
    std::array<uint64_t, 3> hash{};
};
Mixed play_mixed() {
    LevelData level = river();
    strip(level, 5, kDone);
    for (int k = 0; k < 4; ++k) put(level, kRiver0 + k, 8, static_cast<uint16_t>(34 + k));
    strip(level, 8, kDone, kRiver0 + 4, kRiver1);
    strip(level, 20, kInert);
    SimulationEngine sim;
    start(sim, level);
    const std::array<uint32_t, 5> ants = {sim.spawn_unit(0, AntType::Worker, TileCoord{13, 5}), sim.spawn_unit(1, AntType::Worker, TileCoord{15, 5}),
                                          sim.spawn_unit(2, AntType::Swimmer, TileCoord{17, 5}), sim.spawn_unit(3, AntType::Worker, TileCoord{13, 8}),
                                          sim.spawn_unit(1, AntType::Swimmer, TileCoord{15, 20})};
    Mixed out;
    const int stops[3] = {1200, 3601, 6000};
    int at = 0;
    for (int k = 0; k < 3; ++k) {
        run(sim, stops[k] - at);
        at = stops[k];
        out.hash[static_cast<size_t>(k)] = sim.state_hash().total;
        for (int x = kRiver0; x <= kRiver1; ++x) {
            CHECK(cell(sim, x, 5).interactive_id == kDone && cell(sim, x, 5).timer_ticks == 0);
            CHECK(cell(sim, x, 8).interactive_id == (x < kRiver0 + 4 ? 34 + x - kRiver0 : kDone) && cell(sim, x, 8).timer_ticks == 0);
            CHECK(cell(sim, x, 20).interactive_id == kInert && cell(sim, x, 20).timer_ticks == 0);       // (38 never turns into anything and never gets a timer)
        }
        for (const uint32_t a : ants) CHECK(well(sim, a));
    }
    return out;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Bridges that a map file holds (layer-2 ids 34 .. 38)\n"
              << "=======================================================\n";

    run_case("FBR1.1 The Ids 34 .. 37 Of A File Load As Bridge Pieces With No Timer (Stage 1 .. 4, Mud, No Team), Whatever The Dictionary Calls Them; 38 Is No Bridge: Water, Stage 0; A Grid And An Engine Agree", [] {
        const char* names[3][2] = {{".", "."}, {"bridge4a", "bridge4b"}, {"rock1", "."}};      // (34 .. 37, 38)
        for (const auto& pair : names) {
            LevelData level = river();
            for (int k = 0; k < 5; ++k) put(level, kRiver0 + k, 3, static_cast<uint16_t>(34 + k));
            for (uint16_t id = 34; id <= 37; ++id) name_tile(level, id, pair[0]);
            name_tile(level, 38, pair[1]);
            Grid g;
            CHECK(g.init_from_level(level));
            expect_loaded(g, 3);
            for (int x = kRiver0; x <= kRiver0 + 4; ++x) g.regress_bridge(static_cast<uint32_t>(x), 3);       // (the stage machine: a piece goes one stage down, the last one empty; 38 is no stage and stays)
            const uint16_t down[5] = {TILE_EMPTY, 34, 35, 36, 38};
            for (int k = 0; k < 5; ++k) CHECK(g.get_cell(static_cast<uint32_t>(kRiver0 + k), 3u).interactive_id == down[k]);
            SimulationEngine sim;
            start(sim, level);
            expect_loaded(sim.grid(), 3);
            CHECK(sim.has_bridge_at(TileCoord{15, 3}) && sim.get_bridge_stage(TileCoord{15, 3}) == 4);
            CHECK(!sim.has_bridge_at(TileCoord{16, 3}) && sim.get_bridge_stage(TileCoord{16, 3}) == 0);
        }
    });

    run_case("FBR2.1 A Non-Swimmer Of Each Team Crosses A Strip Of 37 Over The River: It Arrives On The Other Bank, Never Drowns", [] {
        LevelData level = river();
        const int rows[4] = {5, 11, 14, 17};
        for (const int y : rows) strip(level, y, kDone);
        SimulationEngine sim;
        start(sim, level);
        uint32_t ant[4];
        for (int t = 0; t < 4; ++t) {
            ant[t] = sim.spawn_unit(static_cast<uint8_t>(t), AntType::Worker, TileCoord{10, rows[t]});
            sim.issue_move_order(ant[t], TileCoord{20, rows[t]});
        }
        bool used[4] = {false, false, false, false};
        bool safe = true;
        for (int tick = 0; tick < 800; ++tick) {
            sim.tick();
            for (int t = 0; t < 4; ++t) {
                safe = safe && well(sim, ant[t]);
                used[t] = used[t] || (sim.get_unit(ant[t]).pos.x >= kRiver0 && sim.get_unit(ant[t]).pos.x <= kRiver1);
            }
        }
        CHECK(safe);
        for (int t = 0; t < 4; ++t) CHECK(used[t] && sim.get_unit(ant[t]).pos == (TileCoord{20, rows[t]}));
    });

    run_case("FBR3.1 A File Bridge Has No Timer: After 3,601 And 6,000 Ticks Every Piece Is As The File Had It (37, 34 .. 36 And The Inert 38), And Nobody On Them Drowned", [] {
        play_mixed();
    });

    run_case("FBR4.1 A Swimmer's Bridge Completes With The Timer 3,600 And Is Gone Exactly 3,600 Ticks Later: A Non-Swimmer On It Starts Drowning That Tick", [] {
        LevelData level = river();
        SimulationEngine sim;
        start(sim, level);
        const uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{11, 11});
        sim.issue_order(special_click(swimmer, 12, 11));
        CHECK(sim.get_unit(swimmer).orig_order == AntUnit::kOrderBridgeBuild);
        int armed = -1;
        for (int t = 1; t <= 300 && armed < 0; ++t) {
            sim.tick();
            if (cell(sim, 12, 11).timer_ticks != 0) armed = t;           // (the tick in which the build ends)
        }
        CHECK(armed > 0 && cell(sim, 12, 11).interactive_id == kDone && cell(sim, 12, 11).timer_ticks == kBuiltLife && cell(sim, 12, 11).interactive_owner == 0);
        const uint32_t worker = sim.spawn_unit(1, AntType::Worker, TileCoord{12, 11});
        bool there = true;
        for (uint32_t t = 1; t < kBuiltLife; ++t) {
            sim.tick();
            there = there && cell(sim, 12, 11).interactive_id == kDone && well(sim, worker);
        }
        CHECK(there && cell(sim, 12, 11).timer_ticks == 1u);
        sim.tick();
        CHECK(cell(sim, 12, 11).is_empty_overlay() && !sim.has_bridge_at(TileCoord{12, 11}));
        CHECK(drowning(sim, worker) && sim.get_unit(worker).hp == 10u);
        CHECK(well(sim, swimmer));
    });

    run_case("FBR4.2 A Bridge That A Swimmer Builds With 150 Seconds Of The Match Left Gets No Timer And Stays", [] {
        SimulationEngine sim;
        start(sim, river());
        sim.set_match_time_remaining_ms(150000);
        const uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{11, 11});
        sim.issue_order(special_click(swimmer, 12, 11));
        for (int t = 0; t < 300 && !(cell(sim, 12, 11).has_completed_bridge() && sim.get_unit(swimmer).loco_action != AntUnit::kActionBridgeBuild); ++t) sim.tick();
        CHECK(cell(sim, 12, 11).has_completed_bridge() && cell(sim, 12, 11).timer_ticks == 0);
        const uint32_t worker = sim.spawn_unit(1, AntType::Worker, TileCoord{12, 11});
        run(sim, 2500);                                                    // (125 s of the 150 s that are left)
        CHECK(cell(sim, 12, 11).has_completed_bridge() && cell(sim, 12, 11).timer_ticks == 0 && well(sim, worker));
    });

    run_case("FBR5.1 A Swimmer's Click On A File Bridge Is A Demolish: Four Passes (37, 36, 35, 34, Gone) About 10 Ticks Apart; A Worker On It, Enemy Or Friendly, Drowns At The Last Pass; A Swimmer On It Splashes And Lives; The Neighbors Stay", [] {
        for (int variant = 0; variant < 3; ++variant) {                    // 0 an enemy worker, 1 a worker of the demolisher's team, 2 an enemy swimmer on the demolished cell
            LevelData level = river();
            strip(level, 11, kDone);
            SimulationEngine sim;
            start(sim, level);
            const uint32_t demolisher = sim.spawn_unit(0, AntType::Swimmer, TileCoord{11, 11});
            const uint32_t victim = sim.spawn_unit(variant == 1 ? 0 : 1, variant == 2 ? AntType::Swimmer : AntType::Worker, TileCoord{12, 11});
            const uint32_t bystander = sim.spawn_unit(2, AntType::Worker, TileCoord{13, 11});
            sim.issue_order(special_click(demolisher, 12, 11));
            CHECK(sim.get_unit(demolisher).orig_order == AntUnit::kOrderBridgeDemolish);
            const uint16_t shows[4] = {36, 35, 34, TILE_EMPTY};
            int when[4] = {-1, -1, -1, -1};
            bool early = false;
            for (int t = 1; t <= 200 && when[3] < 0; ++t) {
                sim.tick();
                const uint16_t id = cell(sim, 12, 11).interactive_id;
                for (int k = 0; k < 4; ++k) {
                    if (when[k] < 0 && id == shows[k]) when[k] = t;
                    if (id == shows[k] && when[k] == t && k < 3) CHECK(cell(sim, 12, 11).interactive_owner == 0);      // (the tile is the demolisher's team's at once)
                }
                early = early || (when[3] < 0 && drowning(sim, victim));
            }
            CHECK(when[0] > 0 && when[1] > when[0] && when[2] > when[1] && when[3] > when[2]);
            for (int k = 1; k < 4; ++k) CHECK(when[k] - when[k - 1] >= 8 && when[k] - when[k - 1] <= 12);
            CHECK(!early);
            if (variant == 2) {
                bool splash = false;
                for (const auto& fx : sim.get_world_state().effects) splash = splash || fx.anim_name == "dsplash";
                run(sim, 2);
                CHECK(splash && well(sim, victim) && sim.get_unit(victim).state == UnitState::Swimming);
            } else {
                CHECK(drowning(sim, victim) && sim.get_unit(victim).hp == 10u);
            }
            CHECK(well(sim, bystander) && well(sim, demolisher));
            run(sim, 100);
            for (int x = 13; x <= kRiver1; ++x) CHECK(cell(sim, x, 11).interactive_id == kDone && cell(sim, x, 11).timer_ticks == 0);
        }
    });

    run_case("FBR5.2 The Demolish Has No Terrain Test: A File Bridge On Plain Grass Is Demolished All The Same, And An Enemy Worker Standing On It Drowns At The Last Pass", [] {
        LevelData level = river();
        put(level, 9, 14, kDone);
        SimulationEngine sim;
        start(sim, level);
        const uint32_t demolisher = sim.spawn_unit(0, AntType::Swimmer, TileCoord{8, 14});
        const uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{9, 14});
        sim.issue_order(special_click(demolisher, 9, 14));
        CHECK(sim.get_unit(demolisher).orig_order == AntUnit::kOrderBridgeDemolish);
        bool early = false;
        int t = 0;
        for (; t < 200 && !cell(sim, 9, 14).is_empty_overlay(); ++t) {
            sim.tick();
            early = early || (!cell(sim, 9, 14).is_empty_overlay() && drowning(sim, victim));
        }
        CHECK(t < 200 && !early && drowning(sim, victim));
    });

    run_case("FBR6.1 Part-Built Pieces (34, 35, 36) That A File Holds: Walkable (Mud), Permanent, Nothing Advances, A Swimmer's Special Click Is Refused", [] {
        LevelData level = river();
        const int rows[3] = {11, 14, 17};
        for (int k = 0; k < 3; ++k) {
            strip(level, rows[k], kDone);
            put(level, kRiver0, rows[k], static_cast<uint16_t>(34 + k));
        }
        SimulationEngine sim;
        start(sim, level);
        uint32_t ant[3];
        for (int k = 0; k < 3; ++k) {
            CHECK(sim.grid().terrain_class_at(TileCoord{kRiver0, rows[k]}) == movement::kTerrainMud && cell(sim, kRiver0, rows[k]).has_partial_bridge());
            ant[k] = sim.spawn_unit(static_cast<uint8_t>(k), AntType::Worker, TileCoord{10, rows[k]});
            sim.issue_move_order(ant[k], TileCoord{20, rows[k]});
        }
        run(sim, 800);
        for (int k = 0; k < 3; ++k) {
            CHECK(well(sim, ant[k]) && sim.get_unit(ant[k]).pos == (TileCoord{20, rows[k]}));
            CHECK(cell(sim, kRiver0, rows[k]).interactive_id == 34 + k && cell(sim, kRiver0, rows[k]).timer_ticks == 0);
            expect_refused(level, rows[k], static_cast<uint16_t>(34 + k));
        }
    });

    run_case("FBR7.1 The Id 38 On Water Is No Bridge: Not Passable For A Non-Swimmer (Who Stays On The Bank), A Placed Non-Swimmer Drowns, A Thrown One Drowns, A Swimmer Swims In It", [] {
        LevelData level = river();
        strip(level, 11, kInert);
        put(level, 15, 17, kInert);
        SimulationEngine sim;
        start(sim, level);
        const TileCoord t{14, 11};
        CHECK(cell(sim, 14, 11).interactive_id == kInert && sim.grid().terrain_class_at(t) == movement::kTerrainWater);
        CHECK(!cell(sim, 14, 11).is_passable(false) && cell(sim, 14, 11).is_passable(true));
        CHECK(!sim.can_unit_traverse(AntType::Worker, t, 0) && sim.can_unit_traverse(AntType::Swimmer, t, 0));
        const uint32_t walker = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 11});
        sim.issue_move_order(walker, TileCoord{20, 11});
        run(sim, 600);
        CHECK(well(sim, walker) && sim.get_unit(walker).pos.x <= kRiver0 - 1);
        const uint32_t placed = sim.spawn_unit(1, AntType::Worker, TileCoord{13, 11});
        CHECK(drowning(sim, placed));
        const uint32_t swimmer = sim.spawn_unit(2, AntType::Swimmer, TileCoord{16, 11});
        run(sim, 2);
        CHECK(well(sim, swimmer) && sim.get_unit(swimmer).state == UnitState::Swimming);
        const uint32_t thrown = sim.spawn_unit(3, AntType::Worker, TileCoord{11, 17});
        sim.apply_knockback(thrown, 10 * 32 + 16, 17 * 32 + 16, 4, 4);      // 4 tiles east: the cell (15, 17)
        for (int i = 0; i < 100 && !drowning(sim, thrown); ++i) sim.tick();
        CHECK(drowning(sim, thrown) && sim.get_unit(thrown).pos == (TileCoord{15, 17}));
    });

    run_case("FBR7.2 A Swimmer's Special Click On A 38 Over Water Is Refused (No Demolish, No Build, Nothing Changes); The Same Click On Plain Water Builds", [] {
        LevelData level = river();
        put(level, kRiver0, 11, kInert);
        expect_refused(level, 11, kInert);
        SimulationEngine sim;
        start(sim, level);
        const uint32_t swimmer = sim.spawn_unit(0, AntType::Swimmer, TileCoord{kRiver0 - 1, 14});
        sim.issue_order(special_click(swimmer, kRiver0, 14));
        CHECK(sim.get_unit(swimmer).orig_order == AntUnit::kOrderBridgeBuild);
        run(sim, 100);
        CHECK(cell(sim, kRiver0, 14).has_completed_bridge());
    });

    run_case("FBR8.1 Two Plays Of The Same World End In The Same State Hash At Ticks 1,200, 3,601 And 6,000", [] {
        const Mixed a = play_mixed();
        const Mixed b = play_mixed();
        CHECK(a.hash == b.hash);
        CHECK(a.hash[0] != a.hash[1] && a.hash[1] != a.hash[2]);
    });

    run_case("FBR9.1 Water World (Community-Maps, When It Is There): Its 96 Cells Of 37 Load With No Timer And Are All Still There After 6,000 Ticks, Two Plays Alike", [] {
        LevelData level;
        if (!level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/water world.lvl")) {
            std::cout << "(not in this checkout) ";
            return;
        }
        uint64_t hash[2] = {0, 0};
        for (int play = 0; play < 2; ++play) {
            SimulationEngine sim;
            sim.init(level, 4242, 0x0F);
            for (int tick = 0; tick < 6000; ++tick) {
                if (tick == 0 || tick == 3601) {
                    size_t pieces = 0, timed = 0;
                    for (uint32_t y = 0; y < sim.grid().height(); ++y) {
                        for (uint32_t x = 0; x < sim.grid().width(); ++x) {
                            pieces += sim.grid().get_cell(x, y).interactive_id == kDone ? 1u : 0u;
                            timed += sim.grid().get_cell(x, y).has_any_bridge() && sim.grid().get_cell(x, y).timer_ticks != 0 ? 1u : 0u;
                        }
                    }
                    CHECK(pieces == 96 && timed == 0);
                }
                sim.tick();
            }
            hash[play] = sim.state_hash().total;
        }
        CHECK(hash[0] == hash[1]);
    });

    std::cout << "\nfile bridges: " << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
