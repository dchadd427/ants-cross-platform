// Bombs that a map FILE holds (docs/reverse_engineering/map_file_bombs.md). The original's bomb tiles are the layer-2 ids 129 .. 132 (FUN_01008bc6; Table 4 of ants.chd: redbomb, greenbomb,
// blackbomb, bluebomb), and the original reads nothing else of a bomb cell: an ant that ends a step on one sets it off, whoever it is (0x101bb42, 0x101d44f). 208 community maps
// carry them (the editor leaves their dictionary entries "."), and the remake used to take them for solid obstacles that nothing draws. Grid::init_from_level now turns each into the
// planted bomb of its color (id 100 + the color's team) owned by the team that the cell's second word names, so that every rule of a planted bomb applies to it unchanged.
// New file for the topic; its own small check() that counts and goes on (the shared tests/common/test_assert.hpp macros return from the case at the first failure, which the loops here do not want).
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// (original tile id, the team whose color it is: 0 green, 1 red, 2 blue, 3 black)
struct BombTile {
    uint16_t id;
    uint8_t team;
    const char* name;
};
constexpr BombTile kBombs[4] = {{129, 1, "redbomb"}, {130, 0, "greenbomb"}, {131, 3, "blackbomb"}, {132, 2, "bluebomb"}};

LevelData tiny() {
    LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) std::cout << "cannot load TINY.LVL\n";
    return level;
}

void set_layer2(LevelData& level, uint32_t x, uint32_t y, uint16_t id, uint16_t flags = 1, uint16_t properties = 0x0101) {
    auto& c = level.layer2_interactive[static_cast<size_t>(y) * level.width + x];
    c.tile_index = id;
    c.flags = flags;
    c.properties = properties;
}

void set_layer2(LevelData& level, TileCoord t, uint16_t id, uint16_t flags = 1, uint16_t properties = 0x0101) {
    set_layer2(level, static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y), id, flags, properties);
}

const TileCell& cell_at(const Grid& g, TileCoord t) { return g.get_cell(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y)); }

void name_tile(LevelData& level, uint16_t id, const std::string& name) {
    if (level.tile_dictionary.size() <= id) level.tile_dictionary.resize(static_cast<size_t>(id) + 1, ".");
    level.tile_dictionary[id] = name;
}

// Clear layer 2 of the level and hold a row of open ground: the cells (x0 .. x0+n-1, y) all plain walkable terrain
LevelData open_ground() {
    LevelData level = tiny();
    for (auto& c : level.layer2_interactive) {
        c.tile_index = ants::assets::LVL_EMPTY_TILE;
        c.flags = 0;
        c.properties = 0;
    }
    return level;
}

// Spots of open ground: the tile and the block of tiles around it (3 to the west, 2 to the east, one row down) are walkable grass, not solid, nothing on them. The first `count`
// of them in row-major order, at least 7 tiles apart.
std::vector<TileCoord> open_tiles(const LevelData& level, size_t count) {
    Grid g;
    g.init_from_level(level);
    std::vector<TileCoord> out;
    const auto open = [&](int32_t x, int32_t y) {
        if (x < 1 || y < 1 || x >= static_cast<int32_t>(g.width()) - 1 || y >= static_cast<int32_t>(g.height()) - 1) return false;
        const TileCell& c = g.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        return c.terrain_type == TERRAIN_WALKABLE && c.surface_type == SurfaceType::Grass && !g.is_solid_object(TileCoord{x, y}) && c.is_empty_overlay() && c.base_owner_team == 255;
    };
    for (int32_t y = 1; y < static_cast<int32_t>(g.height()) - 1 && out.size() < count; ++y) {
        for (int32_t x = 4; x < static_cast<int32_t>(g.width()) - 3 && out.size() < count; ++x) {
            bool block = true;
            for (int32_t dy = 0; dy <= 1; ++dy)
                for (int32_t dx = -3; dx <= 2; ++dx) block = block && open(x + dx, y + dy);
            for (const TileCoord& o : out) block = block && (std::abs(o.x - x) >= 7 || std::abs(o.y - y) >= 7);
            if (block) out.push_back(TileCoord{x, y});
        }
    }
    return out;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Bombs that a map file holds (layer-2 ids 129 .. 132)\n"
              << "=======================================================\n";

    run_case("FB1.1 Each Original Bomb Id Becomes The Planted Bomb Of Its Color (129 Red, 130 Green, 131 Black, 132 Blue), Whatever The Dictionary Calls It: \".\", Its Own Name, Or Another Object's", [] {
        const char* names[3] = {".", "", "rock1"};     // (the editor's "unnamed", an empty name, a wrong one)
        for (int variant = 0; variant < 4; ++variant) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 4);
            CHECK(at.size() == 4);
            if (at.size() != 4) return;
            for (size_t k = 0; k < 4; ++k) {
                set_layer2(level, at[k], kBombs[k].id);
                name_tile(level, kBombs[k].id, variant == 3 ? std::string(kBombs[k].name) : std::string(names[variant]));
            }
            Grid g;
            CHECK(g.init_from_level(level));
            for (size_t k = 0; k < 4; ++k) {
                const TileCell& c = cell_at(g, at[k]);
                CHECK(c.has_bomb());
                CHECK(c.interactive_id == BOMB_BLACK + kBombs[k].team);      // (the planted bomb's id: 100 + the owner's team)
                CHECK(c.interactive_owner == 255);                             // (the file's owner word is 0x0101, an editor leftover: no team)
                CHECK(g.has_bomb_at(at[k]));
                CHECK(!c.is_obstacle() && !c.is_obstacle_overlay);
                CHECK(!g.is_solid_object(at[k]));                              // walkable: the first ant that steps there sets it off
                CHECK(c.is_passable());
                CHECK(!c.can_place_bomb());                                    // (a tile with a bomb takes no second one)
                CHECK(!c.is_food && !c.is_powerup && !c.has_food() && !c.has_powerup());
                CHECK(c.anchor_x == -1 && c.anchor_y == -1);                   // (the file's anchor bytes are junk: 0x0101, 0x5858 ... ; a planted bomb has none)
            }
        }
    });

    run_case("FB1.2 The Ids Around Them Are Not Bombs: 128 And 133 Are No Bombs (Objects Like Any Other: A \".\" Entry Makes No Obstacle Overlay Of Them), The Fire Wall 134 Stays A Fire Wall; A Cell With The Anchor Bit Clear Is A Bomb All The Same", [] {
        LevelData level = open_ground();
        const std::vector<TileCoord> at = open_tiles(level, 5);
        CHECK(at.size() == 5);
        if (at.size() != 5) return;
        set_layer2(level, at[0], 128);
        set_layer2(level, at[1], 133);
        set_layer2(level, at[2], TILE_FIREWALL);
        set_layer2(level, at[3], 131, 0, 0);                       // (the file has such cells: flags 0, 62, 65 ... 15 of 51,000)
        set_layer2(level, at[4], 130, 62, 22616);
        for (const uint16_t id : {uint16_t{128}, uint16_t{133}, uint16_t{134}}) name_tile(level, id, ".");
        Grid g;
        CHECK(g.init_from_level(level));
        CHECK(!cell_at(g, at[0]).has_bomb() && !cell_at(g, at[0]).is_obstacle_overlay && cell_at(g, at[0]).interactive_id == 128);
        CHECK(!cell_at(g, at[1]).has_bomb() && !cell_at(g, at[1]).is_obstacle_overlay && cell_at(g, at[1]).interactive_id == 133);
        CHECK(cell_at(g, at[2]).has_fire() && !cell_at(g, at[2]).has_bomb());
        CHECK(cell_at(g, at[3]).has_bomb() && cell_at(g, at[3]).interactive_id == BOMB_BLACK + 3 && cell_at(g, at[3]).interactive_owner == 0);
        CHECK(cell_at(g, at[4]).has_bomb() && cell_at(g, at[4]).interactive_id == BOMB_BLACK + 0 && cell_at(g, at[4]).interactive_owner == 255);
    });

    run_case("FB1.3 A Level Load Is Not Cumulative: Loading A Level Without File Bombs After One With Them Leaves No Bomb", [] {
        LevelData with = open_ground();
        const std::vector<TileCoord> at = open_tiles(with, 1);
        CHECK(at.size() == 1);
        if (at.empty()) return;
        set_layer2(with, at[0], 129);
        Grid g;
        CHECK(g.init_from_level(with) && g.has_bomb_at(at[0]));
        CHECK(g.init_from_level(open_ground()) && !g.has_bomb_at(at[0]));
    });

    run_case("FB2.1 An Ant Of Another Team Ordered Onto A File Bomb Sets It Off: The Bomb Is Gone, The Ant Lost 2 Hit Points (Credit: The Bomb's Color) And Was Thrown; All Four Colors", [] {
        for (const BombTile& b : kBombs) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 1);
            CHECK(at.size() == 1);
            if (at.empty()) return;
            set_layer2(level, at[0], b.id);
            name_tile(level, b.id, ".");
            SimulationEngine sim;
            sim.init(level, 7, 0x0F);
            CHECK(sim.has_bomb_at(at[0]));
            const uint8_t victim_team = static_cast<uint8_t>((b.team + 1) % 4);
            const TileCoord start{at[0].x - 3, at[0].y};
            const uint32_t ant = sim.spawn_unit(victim_team, AntType::Worker, start);
            const uint16_t hp0 = sim.get_unit(ant).hp;
            sim.issue_move_order(ant, at[0]);
            bool gone = false;
            for (int t = 0; t < 400 && !gone; ++t) {
                sim.tick();
                gone = !sim.has_bomb_at(at[0]);
            }
            CHECK(gone);
            CHECK(sim.get_unit(ant).hp == hp0 - 2u || sim.get_unit(ant).removed);
        }
    });

    run_case("FB2.2 A File Bomb Behaves Exactly Like A Planted One: The Same Order Played Twice, Once On A Level That Holds The File Bomb And Once On The Same Level With The Bomb Planted After The Load, Ends In The Same State Hash At Every Tick", [] {
        for (const BombTile& b : kBombs) {
            LevelData with = open_ground();
            const std::vector<TileCoord> at = open_tiles(with, 1);
            CHECK(at.size() == 1);
            if (at.empty()) return;
            set_layer2(with, at[0], b.id, 1, b.team);        // (the owner word names the color's team: the bomb a bomber of that color would plant)
            name_tile(with, b.id, ".");
            LevelData without = open_ground();
            SimulationEngine a, p;
            a.init(with, 99, 0x0F);
            p.init(without, 99, 0x0F);
            p.grid_mut().place_bomb(static_cast<uint32_t>(at[0].x), static_cast<uint32_t>(at[0].y), b.team);
            CHECK(a.state_hash().total == p.state_hash().total);
            const uint8_t victim_team = static_cast<uint8_t>((b.team + 2) % 4);
            const uint32_t va = a.spawn_unit(victim_team, AntType::Worker, TileCoord{at[0].x - 3, at[0].y});
            const uint32_t vp = p.spawn_unit(victim_team, AntType::Worker, TileCoord{at[0].x - 3, at[0].y});
            a.issue_move_order(va, at[0]);
            p.issue_move_order(vp, at[0]);
            bool same = true;
            bool went_off = false;
            for (int t = 0; t < 300; ++t) {
                a.tick();
                p.tick();
                same = same && a.state_hash().total == p.state_hash().total;
                went_off = went_off || !a.has_bomb_at(at[0]);
            }
            CHECK(same);
            CHECK(went_off);
        }
    });

    run_case("FB2.3 The Bomb Of A Color Blocks The Paths Of Its Own Team (And Its Allies) And Not The Others', As Every Planted Bomb Does (R7): A Red Ant Does Not Walk Over A Red File Bomb On Its Way, A Green One Does", [] {
        LevelData level = open_ground();
        const std::vector<TileCoord> at = open_tiles(level, 1);
        CHECK(at.size() == 1);
        if (at.empty()) return;
        set_layer2(level, at[0], 129, 1, 1);      // red, owned by team 1
        SimulationEngine sim;
        sim.init(level, 5, 0x0F);
        const TileCell& cell = cell_at(sim.grid(), at[0]);
        CHECK(cell.has_bomb() && cell.interactive_owner == 1);
        // the planted bomb's rule, asked of the same grid: a bomb of the ant's own team is a wall for its pathing, an enemy's is not (src/ants_sim/movement_system.cpp, R7)
        const uint32_t red = sim.spawn_unit(1, AntType::Worker, TileCoord{at[0].x - 2, at[0].y});
        const uint32_t green = sim.spawn_unit(0, AntType::Worker, TileCoord{at[0].x - 2, at[0].y + 1});
        // a walk through the bomb's tile to the far side: the red ant goes around, the green ant walks onto it and sets it off
        sim.issue_move_order(red, TileCoord{at[0].x + 2, at[0].y});
        for (int t = 0; t < 200; ++t) sim.tick();
        CHECK(sim.has_bomb_at(at[0]));                         // (the red ant went around: the bomb is still there)
        CHECK(sim.get_unit(red).hp == 10u);
        CHECK(sim.get_unit(red).pos == (TileCoord{at[0].x + 2, at[0].y}));    // (and it arrived)
        // the green ant ordered through the same tile walks onto the red bomb and sets it off
        sim.issue_move_order(green, at[0]);
        for (int t = 0; t < 200 && sim.has_bomb_at(at[0]); ++t) sim.tick();
        CHECK(!sim.has_bomb_at(at[0]));
        CHECK(sim.get_unit(green).hp == 8u || sim.get_unit(green).removed);
    });

    run_case("FB1.4 The Owner Is The Cell's Second Word, Not The Color: 0 .. 4 As They Are, Anything Above Matches Nobody (255); The Color Stays The Tile's", [] {
        const uint16_t words[] = {0, 1, 2, 3, 4, 5, 257, 513, 22616, 65535};
        for (const uint16_t word : words) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 1);
            CHECK(at.size() == 1);
            if (at.empty()) return;
            set_layer2(level, at[0], 131, 1, word);
            Grid g;
            CHECK(g.init_from_level(level));
            const TileCell& c = cell_at(g, at[0]);
            CHECK(c.has_bomb() && c.interactive_id == BOMB_BLACK + 3);
            CHECK(c.interactive_owner == (word <= 4 ? word : 255));
        }
    });

    run_case("FB2.4 A File Bomb On A Solid Cell Of Layer 1 Keeps The File's Solid Bit Until It Goes Off, And Then Leaves The Cell Open (SetTile Clears It; Row 0 Never); A Planted Bomb's Cell Was Not Solid And Stays So", [] {
        LevelData level = open_ground();
        const std::vector<TileCoord> at = open_tiles(level, 2);
        CHECK(at.size() == 2);
        if (at.size() != 2) return;
        for (const TileCoord& t : at) {
            set_layer2(level, t, 130, 1, 0);
            level.layer1_terrain[static_cast<size_t>(t.y) * level.width + static_cast<size_t>(t.x)].flags |= 1u;
        }
        Grid g;
        CHECK(g.init_from_level(level));
        CHECK(g.is_solid_object(at[0]) && g.is_solid_object(at[1]) && g.has_bomb_at(at[0]));
        g.clear_bomb(static_cast<uint32_t>(at[0].x), static_cast<uint32_t>(at[0].y));
        CHECK(!g.has_bomb_at(at[0]) && !g.is_solid_object(at[0]));
        CHECK(g.is_solid_object(at[1]));                       // (the other bomb is still there)
        // row 0 is solid for ever
        g.get_cell_mut(static_cast<uint32_t>(at[1].x), 0).static_solid = true;
        g.get_cell_mut(static_cast<uint32_t>(at[1].x), 0).interactive_id = BOMB_BLACK;
        g.clear_bomb(static_cast<uint32_t>(at[1].x), 0);
        CHECK(g.get_cell(static_cast<uint32_t>(at[1].x), 0).static_solid);
    });

    run_case("FB3.1 Bombz Away (Community-Maps): Of Its 896 Cells Of Id 131, 895 Become Black Bombs (The 896th Is The Anchor Of A Food Object, Whose Stage Tile Replaces It); The Map Plays Twice To The Same State Hash", [] {
        LevelData level;
        const std::string path = std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Bombz Away.lvl";
        CHECK(level.load_from_file(path));
        size_t in_file = 0;
        for (const auto& c : level.layer2_interactive) in_file += (c.tile_index == 131) ? 1u : 0u;
        CHECK(in_file == 896);
        Grid g;
        CHECK(g.init_from_level(level));
        size_t bombs = 0, black = 0;
        for (uint32_t y = 0; y < g.height(); ++y) {
            for (uint32_t x = 0; x < g.width(); ++x) {
                const TileCell& c = g.get_cell(x, y);
                if (c.has_bomb()) {
                    ++bombs;
                    black += (c.interactive_id == BOMB_BLACK + 3) ? 1u : 0u;      // (the color is the tile's: 131)
                    CHECK(!c.is_obstacle_overlay);
                }
            }
        }
        // 895, not 896: a food object of the map has its anchor on the cell (15, 15), which holds a bomb in layer 2, and the level loader puts the object's stage tile on its anchor (SetTile,
        // Ants.exe 0x1006d19) where the bomb was: the food is there, as in the original
        CHECK(bombs == 895);
        CHECK(black == 895);
        CHECK(g.get_cell(15, 15).is_food && !g.get_cell(15, 15).has_bomb());
        uint64_t hash[2] = {0, 0};
        size_t left[2] = {0, 0};
        for (int run = 0; run < 2; ++run) {
            SimulationEngine sim;
            sim.init(level, 4242, 0x0F);
            for (int t = 0; t < 1200; ++t) sim.tick();
            hash[run] = sim.state_hash().total;
            for (uint32_t y = 0; y < sim.grid().height(); ++y)
                for (uint32_t x = 0; x < sim.grid().width(); ++x) left[run] += sim.grid().get_cell(x, y).has_bomb() ? 1u : 0u;
        }
        CHECK(hash[0] == hash[1]);
        CHECK(left[0] == left[1]);
        CHECK(left[0] <= 895);
    });

    run_case("FB3.2 POPcOrN (Community-Maps): Its 250 Bomb Cells (249 After A Food Anchor Or A Plant Took One) Are Bombs In The Grid And No Obstacle, Its 62 Fire Walls Are Fire, Its 32 Power-Ups Are Power-Ups; The Block-3 Default Type Is Combat For Every Ant; Two Plays End In One State Hash", [] {
        LevelData level;
        CHECK(level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/POPcOrN.lvl"));
        size_t bomb_cells = 0, fire_cells = 0;
        for (const auto& c : level.layer2_interactive) {
            bomb_cells += (c.tile_index >= 129 && c.tile_index <= 132) ? 1u : 0u;
            fire_cells += (c.tile_index == TILE_FIREWALL) ? 1u : 0u;
        }
        CHECK(bomb_cells == 250 && fire_cells == 62);
        Grid g;
        CHECK(g.init_from_level(level));
        size_t bombs = 0, fires = 0, powerups = 0;
        for (uint32_t y = 0; y < g.height(); ++y) {
            for (uint32_t x = 0; x < g.width(); ++x) {
                const TileCell& c = g.get_cell(x, y);
                if (c.has_bomb()) CHECK(!c.is_obstacle_overlay);       // (a fire wall named "." is still an overlay in the loader: a separate matter, not part of this change)
                bombs += c.has_bomb() ? 1u : 0u;
                fires += c.has_fire() ? 1u : 0u;
                powerups += c.has_powerup() ? 1u : 0u;
            }
        }
        CHECK(bombs == 249 && fires == 62 && powerups == 32);
        CHECK(g.default_ant_tile() == PU_COMBAT);
        uint64_t hash[2] = {0, 0};
        for (int run = 0; run < 2; ++run) {
            SimulationEngine sim;
            sim.init(level, 77, 0x0F);
            if (run == 0) {
                size_t combat = 0, ants = 0;
                for (const auto& a : sim.get_world_state().ants) { ++ants; combat += (a.type == AntType::Combat) ? 1u : 0u; }
                CHECK(ants > 0 && combat == ants);
            }
            for (int t = 0; t < 1200; ++t) sim.tick();
            hash[run] = sim.state_hash().total;
        }
        CHECK(hash[0] == hash[1]);
    });

    std::cout << "\nfile bombs: " << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
