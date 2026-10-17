// The map loader as the original's (docs/reverse_engineering/map_loader_fidelity.md): where Grid::init_from_level decided by a dictionary NAME or by a rule of its own, it takes what the
// original takes (the tile id, the tables, the file's solid bit). One section per fix, each with a sweep of the community library that pins how many maps it concerns.
// New file for the topic; its own small check() that counts and goes on (the shared tests/common/test_assert.hpp macros return from the case at the first failure, which the loops here do not want).
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
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

LevelData tiny() {
    LevelData level;
    if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) std::cout << "cannot load TINY.LVL\n";
    return level;
}

LevelData community(const std::string& file) {
    LevelData level;
    if (!level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/" + file)) std::cout << "cannot load " << file << "\n";
    return level;
}

// Every map of the community library, in name order
void for_each_community_map(const std::function<void(const std::string&, const LevelData&)>& fn) {
    std::vector<std::string> names;
    for (const auto& e : std::filesystem::directory_iterator(std::string(ANTS_SOURCE_DIR) + "/Community-Maps")) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".lvl") names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    for (const std::string& n : names) {
        LevelData level;
        if (level.load_from_file(std::string(ANTS_SOURCE_DIR) + "/Community-Maps/" + n)) fn(n, level);
    }
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

// The layer-1 solid bit of a cell, as the file holds it
void set_solid_bit(LevelData& level, TileCoord t, bool solid) {
    auto& c = level.layer1_terrain[static_cast<size_t>(t.y) * level.width + static_cast<size_t>(t.x)];
    c.flags = static_cast<uint16_t>(solid ? (c.flags | 1u) : (c.flags & ~1u));
}

void name_tile(LevelData& level, uint16_t id, const std::string& name) {
    if (level.tile_dictionary.size() <= id) level.tile_dictionary.resize(static_cast<size_t>(id) + 1, ".");
    level.tile_dictionary[id] = name;
}

const TileCell& cell_at(const Grid& g, TileCoord t) { return g.get_cell(static_cast<uint32_t>(t.x), static_cast<uint32_t>(t.y)); }

// TINY with layer 2 emptied
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

// A tile id that is food in the original (tile flag 0x02) and that TINY's dictionary names as food, so that the case holds before and after food is decided by the flag
uint16_t a_food_id(const LevelData& level) {
    for (uint16_t id = 0; id < level.tile_dictionary.size() && id < movement::kTileIdCount; ++id) {
        if ((movement::tile_flags_of(id) & movement::kTileFlagFood) != 0 && level.tile_dictionary[id].rfind("fd", 0) == 0) return id;
    }
    return 0;
}

// An ant of team 0 walks from the tile `from` to the tile `to` along a row: the tiles it stood on
std::set<std::pair<int32_t, int32_t>> walk(SimulationEngine& sim, TileCoord from, TileCoord to, int ticks, bool* arrived) {
    const uint32_t ant = sim.spawn_unit(0, AntType::Worker, from);
    sim.issue_move_order(ant, to);
    std::set<std::pair<int32_t, int32_t>> seen;
    for (int t = 0; t < ticks; ++t) {
        sim.tick();
        const TileCoord p = sim.get_unit(ant).pos;
        seen.insert({p.x, p.y});
    }
    if (arrived != nullptr) *arrived = sim.get_unit(ant).pos == to;
    return seen;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " The map loader as the original's\n"
              << "=======================================================\n";

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 1: the solidity of a power-up, food, a fire wall and a lunchbox is the file's layer-1 solid bit (FUN_0100cf0f), nothing else
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF1.1 A Power-Up Whose Layer-1 Solid Bit Is Clear In The File Is Walkable Ground; With The Bit Set It Is An Obstacle (Whatever The Dictionary Calls The Id)", [] {
        for (const bool bit : {false, true}) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 1);
            CHECK(at.size() == 1);
            if (at.empty()) return;
            set_layer2(level, at[0], 62);
            name_tile(level, 62, ".");
            set_solid_bit(level, at[0], bit);
            Grid g;
            CHECK(g.init_from_level(level));
            CHECK(cell_at(g, at[0]).has_powerup());
            CHECK(g.is_solid_object(at[0]) == bit);
            CHECK(cell_at(g, at[0]).static_solid == bit);           // (the file's bit, kept as it is)
        }
    });

    run_case("LF1.2 Food, A Fire Wall And A Lunchbox Placed By The File Are Solid Exactly When The File's Bit Says So", [] {
        const LevelData base = open_ground();
        const uint16_t food = a_food_id(base);
        CHECK(food != 0);
        struct Kind { uint16_t id; const char* what; };
        for (const Kind& k : {Kind{food, "food"}, Kind{TILE_FIREWALL, "fire wall"}, Kind{TILE_LUNCHBOX, "lunchbox"}}) {
            for (const bool bit : {false, true}) {
                LevelData level = base;
                const std::vector<TileCoord> at = open_tiles(level, 1);
                CHECK(at.size() == 1);
                if (at.empty()) return;
                set_layer2(level, at[0], k.id);
                set_solid_bit(level, at[0], bit);
                Grid g;
                CHECK(g.init_from_level(level));
                const TileCell& c = cell_at(g, at[0]);
                CHECK(c.interactive_id == k.id);
                CHECK(g.is_solid_object(at[0]) == bit);
                if (g.is_solid_object(at[0]) != bit) std::cout << "    (" << k.what << ", bit " << bit << ")\n";
            }
        }
    });

    run_case("LF1.3 An Ant Walks Straight Across A File Power-Up That Has No Solid Bit (Nothing Happens, The Power-Up Stays); With The Bit It Goes Around", [] {
        for (const bool bit : {false, true}) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 1);
            CHECK(at.size() == 1);
            if (at.empty()) return;
            set_layer2(level, at[0], 63);
            set_solid_bit(level, at[0], bit);
            SimulationEngine sim;
            sim.init(level, 5, 0x0F);
            bool arrived = false;
            const auto seen = walk(sim, TileCoord{at[0].x - 3, at[0].y}, TileCoord{at[0].x + 2, at[0].y}, 300, &arrived);
            CHECK(arrived);
            CHECK((seen.count({at[0].x, at[0].y}) != 0) == !bit);   // (across it, or around it)
            CHECK(sim.grid().has_powerup_at(at[0]));                 // (a power-up is taken by an order to its tile, never by a walk over it)
        }
    });

    run_case("LF1.4 What The Game Places Sets The Bit And What It Removes Clears It (Row 0 Never): A Dropped Power-Up, A Fire Wall, A Lunchbox, The Placeholder; The Footprint Of A Food Pile Follows Its Stage", [] {
        LevelData level = open_ground();
        const std::vector<TileCoord> at = open_tiles(level, 4);
        CHECK(at.size() == 4);
        if (at.size() != 4) return;
        SimulationEngine sim;
        sim.init(level, 5, 0x0F);
        Grid& g = sim.grid_mut();
        const auto solid = [&](TileCoord t) { return cell_at(g, t).static_solid; };
        // a power-up dropped (place_powerup) and taken (clear_powerup)
        CHECK(!solid(at[0]));
        g.place_powerup(at[0].x, at[0].y, 4);
        CHECK(solid(at[0]) && g.is_solid_object(at[0]));
        g.clear_powerup(at[0].x, at[0].y);
        CHECK(!solid(at[0]) && !g.is_solid_object(at[0]));
        // a fire wall lit and burnt out
        g.place_firewall(static_cast<uint32_t>(at[1].x), static_cast<uint32_t>(at[1].y), 0);
        CHECK(solid(at[1]));
        g.clear_firewall(static_cast<uint32_t>(at[1].x), static_cast<uint32_t>(at[1].y));
        CHECK(!solid(at[1]));
        // a lunchbox dropped (a food object of one unit) and eaten
        g.drop_lunchbox(static_cast<uint32_t>(at[2].x), static_cast<uint32_t>(at[2].y), 10);
        CHECK(cell_at(g, at[2]).has_lunchbox() && solid(at[2]));
        g.set_food_tile(at[2], 0x7FFE);
        CHECK(!solid(at[2]) && cell_at(g, at[2]).is_empty_overlay());
        // the placeholder of a plant in progress (set_layer2 with 160) is solid, the bomb that replaces it is not
        g.set_layer2(static_cast<uint32_t>(at[3].x), static_cast<uint32_t>(at[3].y), TILE_RESERVED, 0);
        CHECK(solid(at[3]));
        g.place_bomb(static_cast<uint32_t>(at[3].x), static_cast<uint32_t>(at[3].y), 0);
        CHECK(!solid(at[3]) && cell_at(g, at[3]).has_bomb());
        // row 0 is solid for good
        CHECK(solid(TileCoord{4, 0}));
        g.place_powerup(4, 0, 4);
        g.clear_powerup(4, 0);
        CHECK(solid(TileCoord{4, 0}));
    });

    run_case("LF1.5 POPcOrN.lvl (Community-Maps): Its 32 Power-Ups Have No Solid Bit And Are Walkable, Its 62 Fire Wall Cells Have It And Are Solid; The Top Room Can Be Reached Through The Corridors", [] {
        const LevelData level = community("POPcOrN.lvl");
        Grid g;
        CHECK(g.init_from_level(level));
        int powerups = 0, walkable = 0, fire = 0, fire_solid = 0;
        for (uint32_t y = 0; y < g.height(); ++y) {
            for (uint32_t x = 0; x < g.width(); ++x) {
                const TileCell& c = g.get_cell(x, y);
                const TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                if (c.has_powerup()) {
                    ++powerups;
                    if (!g.is_solid_object(t)) ++walkable;
                }
                if (c.has_fire()) {
                    ++fire;
                    if (g.is_solid_object(t)) ++fire_solid;
                }
            }
        }
        CHECK(powerups == 32 && walkable == 32);
        CHECK(fire == 62 && fire_solid == 62);
        // an ant walks through a plug of power-ups: a power-up with a free tile north and south of it, the south one reachable from the north one
        TileCoord from{-1, -1}, to{-1, -1};
        for (uint32_t y = 1; y + 1 < g.height() && from.x < 0; ++y) {
            for (uint32_t x = 0; x < g.width() && from.x < 0; ++x) {
                const TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                const TileCoord n{t.x, t.y - 1}, s{t.x, t.y + 1};
                const auto free_tile = [&](TileCoord q) { const TileCell& c = cell_at(g, q); return !g.is_solid_object(q) && c.is_empty_overlay() && c.terrain_type == TERRAIN_WALKABLE; };
                if (cell_at(g, t).has_powerup() && free_tile(n) && free_tile(s) && !g.is_solid_object(t)) { from = n; to = s; }
            }
        }
        CHECK(from.x >= 0);
        if (from.x < 0) return;
        SimulationEngine sim;
        sim.init(level, 5, 0x0F);
        bool arrived = false;
        walk(sim, from, to, 400, &arrived);
        CHECK(arrived);
    });

    run_case("LF1.6 The Community Library: 92 Maps Hold Power-Ups With No Solid Bit (2,913 Cells), 35 Hold Food Cells Without It (182 Cells), 2 Hold Fire Wall Cells Without It (439 Cells); None Of Them Is An Obstacle Any More, And A Fire Wall Cell With The Bit Still Is", [] {
        int pu_maps = 0, pu_cells = 0, food_maps = 0, food_cells = 0, fire_maps = 0, fire_cells = 0;
        int still_solid_powerups = 0, fire_with_bit_not_solid = 0;
        for_each_community_map([&](const std::string& name, const LevelData& level) {
            Grid g;
            if (!g.init_from_level(level)) return;
            int pu = 0, fd = 0, fi = 0;
            for (uint32_t y = 0; y < level.height; ++y) {
                for (uint32_t x = 0; x < level.width; ++x) {
                    const uint16_t id = level.get_cell_layer2(x, y).tile_index;
                    if (id == ants::assets::LVL_EMPTY_TILE || id == 0xFFFFu) continue;
                    const bool bit = (level.get_cell_layer1(x, y).flags & 1u) != 0;
                    const TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                    const bool power = movement::is_powerup_tile(id);
                    const bool food = (movement::tile_flags_of(id) & movement::kTileFlagFood) != 0;
                    const bool fire = id == TILE_FIREWALL;
                    if (!bit && power) {
                        ++pu;
                        // solid for another reason of the original's: a Block-4 record marks its cell, a hill's footprint is solid (and row 0 always is)
                        bool other = y == 0;
                        for (const auto& wp : level.waypoints) other = other || (wp.x == x && wp.y == y);
                        for (const auto& ah : g.anthills()) other = other || (t.x >= ah.x - 1 && t.x <= ah.x + 4 && t.y >= ah.y - 1 && t.y <= ah.y + 4);
                        // (a food object that was put on the cell replaced the power-up, as in the original)
                        if (g.is_solid_object(t) && !other && cell_at(g, t).has_powerup()) { ++still_solid_powerups; std::cout << "\n    still solid: " << x << "," << y << " in " << name; }
                    }
                    if (!bit && food) ++fd;
                    if (!bit && fire) ++fi;
                    if (bit && fire && !g.is_solid_object(t)) ++fire_with_bit_not_solid;
                }
            }
            pu_maps += pu > 0; pu_cells += pu;
            food_maps += fd > 0; food_cells += fd;
            fire_maps += fi > 0; fire_cells += fi;
        });
        std::cout << "\n    power-ups without the bit: " << pu_cells << " cells in " << pu_maps << " maps; food: " << food_cells << " in " << food_maps << "; fire: " << fire_cells << " in " << fire_maps << "\n";
        CHECK(pu_maps == 92 && pu_cells == 2913);
        CHECK(fire_maps == 2 && fire_cells == 439);
        CHECK(food_maps == 35 && food_cells == 182);
        CHECK(still_solid_powerups == 0);
        CHECK(fire_with_bit_not_solid == 0);
    });

    run_case("LF1.7 The Six Shipped Maps Are Unchanged By It: Every Power-Up, Pile, Lunchbox And Fire Wall Cell In Their Files Has The Solid Bit, So Each Cell Is Solid Exactly When It Was When The Objects Counted As Solid By Themselves", [] {
        for (const char* file : {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "ISLANDS", "TREASURE"}) {
            LevelData level;
            CHECK(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + file + ".LVL"));
            Grid g;
            CHECK(g.init_from_level(level));
            int objects = 0, differing = 0;
            for (uint32_t y = 1; y < g.height(); ++y) {
                for (uint32_t x = 0; x < g.width(); ++x) {
                    const TileCell& c = g.get_cell(x, y);
                    const TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                    const bool object = c.has_fire() || c.has_lunchbox() || c.has_powerup() || c.has_food() || c.has_reserved();
                    objects += object;
                    const bool before = c.static_solid || object || c.terrain_type == TERRAIN_OBSTACLE;       // (the rule of v0.12.9, base holes aside)
                    if (!c.is_base_hole && before != g.is_solid_object(t)) ++differing;
                }
            }
            CHECK(differing == 0);
            if (differing != 0) std::cout << "    (" << file << ": " << differing << " cells differ, of " << objects << " object cells)\n";
        }
    });

    run_case("LF1.8 A Map Whose Power-Ups Have No Solid Bit Plays Deterministically: POPcOrN Played Twice From Fresh Engines With The Same Orders Ends In One State Hash", [] {
        const LevelData level = community("POPcOrN.lvl");
        const auto play = [&]() {
            SimulationEngine sim;
            sim.init(level, 7, 0x0F);
            uint32_t lcg = 99u;
            for (uint32_t t = 0; t < 900; ++t) {
                if (t % 20 == 0) {
                    for (const AntSnapshot& a : sim.get_world_state().ants) {
                        if ((a.id % 4u) != ((t / 20u) % 4u)) continue;
                        lcg = lcg * 1664525u + 1013904223u;
                        AntOrder order;
                        order.ant_id = a.id;
                        order.type = OrderType::Move;
                        order.target_x = static_cast<int32_t>((lcg >> 8) % level.width);
                        order.target_y = static_cast<int32_t>((lcg >> 16) % level.height);
                        (void)sim.issue_order(order);
                    }
                }
                sim.tick();
            }
            return sim.state_hash().total;
        };
        const uint64_t first = play();
        CHECK(first == play());
    });

    run_case("LF1.9 The Six Original Maps: Every Power-Up Cell Has The Layer-1 Solid Bit In The File, So Power-Ups Are Obstacles There (Taken Only By A Click On Them) And An Ant Does Not Walk Across One; On POPcOrN.lvl None Has The Bit And They Are Walkable", [] {
        long total = 0, with_bit = 0, solid_in_grid = 0;
        for (const char* file : {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "ISLANDS", "TREASURE"}) {
            LevelData level;
            CHECK(level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + file + ".LVL"));
            Grid g;
            CHECK(g.init_from_level(level));
            long n = 0, bit = 0, solid = 0;
            for (uint32_t y = 0; y < level.height; ++y) {
                for (uint32_t x = 0; x < level.width; ++x) {
                    if (!movement::is_powerup_tile(level.get_cell_layer2(x, y).tile_index)) continue;
                    ++n;
                    bit += (level.get_cell_layer1(x, y).flags & 1u) != 0;
                    solid += g.is_solid_object(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)});
                }
            }
            std::cout << "\n    " << file << ": " << n << " power-up cells, " << bit << " with the solid bit, " << solid << " solid in the grid";
            total += n; with_bit += bit; solid_in_grid += solid;
            CHECK(bit == n && solid == n);
        }
        std::cout << "\n";
        // POPcOrN: the opposite
        const LevelData pop = community("POPcOrN.lvl");
        Grid g;
        CHECK(g.init_from_level(pop));
        long n = 0, bit = 0, solid = 0;
        for (uint32_t y = 0; y < pop.height; ++y)
            for (uint32_t x = 0; x < pop.width; ++x)
                if (movement::is_powerup_tile(pop.get_cell_layer2(x, y).tile_index)) {
                    ++n;
                    bit += (pop.get_cell_layer1(x, y).flags & 1u) != 0;
                    solid += g.is_solid_object(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)});
                }
        std::cout << "    POPcOrN: " << n << " power-up cells, " << bit << " with the bit, " << solid << " solid\n";
        CHECK(n == 32 && bit == 0 && solid == 0);
        CHECK(total > 0 && with_bit == total && solid_in_grid == total);
        // and on an original map an ant ordered across a power-up goes around it (never stands on it); the power-up stays
        LevelData level = open_ground();
        const std::vector<TileCoord> at = open_tiles(level, 1);
        CHECK(at.size() == 1);
        if (at.empty()) return;
        set_layer2(level, at[0], 63);
        set_solid_bit(level, at[0], true);
        SimulationEngine sim;
        sim.init(level, 5, 0x0F);
        bool arrived = false;
        const auto seen = walk(sim, TileCoord{at[0].x - 3, at[0].y}, TileCoord{at[0].x + 2, at[0].y}, 300, &arrived);
        CHECK(arrived && seen.count({at[0].x, at[0].y}) == 0 && sim.grid().has_powerup_at(at[0]));
    });

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 2: the hill of a color is the layer-2 id 245 .. 248 and its start marker the block-1 id 152 .. 155, never a dictionary name (FUN_0100ecac, table 0x1002468)
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF2.1 The Ids: Hills 245 Black, 246 Blue, 247 Red, 248 Green; Start Markers 152 Black, 153 Blue, 154 Green, 155 Red (The Remake's Teams: 0 Green, 1 Red, 2 Blue, 3 Black)", [] {
        using ants::assets::hill_team_of_id;
        using ants::assets::start_marker_team_of_id;
        CHECK(hill_team_of_id(245) == 3 && hill_team_of_id(246) == 2 && hill_team_of_id(247) == 1 && hill_team_of_id(248) == 0);
        CHECK(start_marker_team_of_id(152) == 3 && start_marker_team_of_id(153) == 2 && start_marker_team_of_id(154) == 0 && start_marker_team_of_id(155) == 1);
        for (uint16_t id : {uint16_t{0}, uint16_t{151}, uint16_t{156}, uint16_t{244}, uint16_t{249}, uint16_t{0x7FFE}}) CHECK(hill_team_of_id(id) < 0 && start_marker_team_of_id(id) < 0);
        // TINY's own dictionary says the same (the shipped maps name them as the original does)
        const LevelData level = tiny();
        CHECK(level.tile_dictionary[245] == "BLACKHILL" && level.tile_dictionary[246] == "BLUEHILL" && level.tile_dictionary[247] == "REDHILL" && level.tile_dictionary[248] == "GREENHILL");
        CHECK(level.tile_dictionary[152] == "BSTART" && level.tile_dictionary[153] == "USTART" && level.tile_dictionary[154] == "GSTART" && level.tile_dictionary[155] == "RSTART");
    });

    run_case("LF2.2 A Dictionary That Damages Or Permutes The Names Of The Hills And Start Markers Changes Nothing: The Same State Hash At The Start And 200 Ticks Later As TINY's Own", [] {
        const LevelData original = tiny();
        const auto hash_of = [&](const LevelData& level, uint32_t ticks) {
            SimulationEngine sim;
            sim.init(level, 3, 0x0F);
            for (uint32_t t = 0; t < ticks; ++t) sim.tick();
            return sim.state_hash().total;
        };
        const uint64_t at0 = hash_of(original, 0), at200 = hash_of(original, 200);
        LevelData dotted = original;
        for (const uint16_t id : std::initializer_list<uint16_t>{245, 246, 247, 248, 152, 153, 154, 155}) dotted.tile_dictionary[id] = ".";
        CHECK(hash_of(dotted, 0) == at0 && hash_of(dotted, 200) == at200);
        LevelData permuted = original;
        std::swap(permuted.tile_dictionary[245], permuted.tile_dictionary[248]);      // black <-> green
        std::swap(permuted.tile_dictionary[246], permuted.tile_dictionary[247]);      // blue <-> red
        std::swap(permuted.tile_dictionary[152], permuted.tile_dictionary[155]);
        std::swap(permuted.tile_dictionary[153], permuted.tile_dictionary[154]);
        CHECK(hash_of(permuted, 0) == at0 && hash_of(permuted, 200) == at200);
        LevelData misnamed = original;                                                 // the names of other entries
        misnamed.tile_dictionary[245] = "rock1";
        misnamed.tile_dictionary[154] = "hill";
        CHECK(hash_of(misnamed, 0) == at0 && hash_of(misnamed, 200) == at200);
        // and the four teams have their hill and their three start ants where TINY puts them
        SimulationEngine sim;
        sim.init(permuted, 3, 0x0F);
        CHECK(sim.grid().anthills().size() == 4);
        for (uint8_t team = 0; team < 4; ++team) {
            CHECK(sim.grid().find_anthill(team) != nullptr);
        }
    });

    run_case("LF2.3 A Team Without A Player Loses Its Hill And Its Start Ants By The Id, Whatever The Dictionary Calls Them (LevelData::for_roster)", [] {
        LevelData level = tiny();
        for (const uint16_t id : std::initializer_list<uint16_t>{245, 246, 247, 248, 152, 153, 154, 155}) level.tile_dictionary[id] = ".";
        const LevelData two = level.for_roster(0x09);                  // green and black play
        int hills[4] = {0, 0, 0, 0}, starts[4] = {0, 0, 0, 0};
        for (const auto& c : two.layer2_interactive) if (ants::assets::hill_team_of_id(c.tile_index) >= 0) ++hills[ants::assets::hill_team_of_id(c.tile_index)];
        for (const auto& sp : two.anthill_spawns) if (sp.team_id < 4) ++starts[sp.team_id];
        CHECK(hills[0] == 16 && hills[3] == 16 && hills[1] == 0 && hills[2] == 0);
        CHECK(starts[0] == 3 && starts[3] == 3 && starts[1] == 0 && starts[2] == 0);
        SimulationEngine sim;
        sim.init(level, 3, 0x09);
        CHECK(sim.grid().anthills().size() == 2 && sim.grid().find_anthill(0) != nullptr && sim.grid().find_anthill(3) != nullptr);
    });

    run_case("LF2.4 The Community Library: 7 Maps Had A Hill Found By A Damaged Or Permuted Name (4 Lost A Hill, 3 Swapped Colors), 6 Had A Start Marker Read By Name Differently; None Is A Difference Now", [] {
        // the loader's rule before this change, by name: the hill tile names of lvl_parser / grid, the substrings of the start markers
        const auto old_hill_team = [](std::string n) {
            for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return n == "greenhill" ? 0 : n == "redhill" ? 1 : n == "bluehill" ? 2 : n == "blackhill" ? 3 : -1;
        };
        const auto old_start_team = [](const std::string& n) {
            const auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
            if (has("GSTART") || has("GREENHILL") || has("ghill") || has("gstart")) return 0;
            if (has("RSTART") || has("REDHILL") || has("rhill") || has("rstart")) return 1;
            if (has("USTART") || has("BLUEHILL") || has("blhill") || has("ustart")) return 2;
            if (has("BSTART") || has("BLACKHILL") || has("bkhill") || has("bstart")) return 3;
            return 255;
        };
        std::vector<std::string> hill_maps, start_maps;
        for_each_community_map([&](const std::string& name, const LevelData& level) {
            bool hills_differ = false, starts_differ = false;
            for (const auto& c : level.layer2_interactive) {
                if (c.tile_index >= level.tile_dictionary.size()) { if (ants::assets::hill_team_of_id(c.tile_index) >= 0) hills_differ = true; continue; }
                if (old_hill_team(level.tile_dictionary[c.tile_index]) != ants::assets::hill_team_of_id(c.tile_index)) hills_differ = true;
            }
            for (const auto& sp : level.anthill_spawns) {
                const std::string& n = level.get_tile_name(sp.tile_id);
                if (old_start_team(n) != static_cast<int>(sp.team_id)) starts_differ = true;     // (team_id is the id's team now)
            }
            if (hills_differ) hill_maps.push_back(name);
            if (starts_differ) start_maps.push_back(name);
        });
        std::cout << "\n    hills differ in " << hill_maps.size() << " maps, start markers in " << start_maps.size() << "\n";
        CHECK((hill_maps == std::vector<std::string>{"CRISS CROSS.lvl", "Conquer.lvl", "LIFELESS.lvl", "NO LIMIT.lvl", "TREZ OF COMBAT.lvl", "TREZ OF LIFE.lvl", "X-pRtfight.lvl"}));
        CHECK((start_maps == std::vector<std::string>{"BOMBERWORLD.lvl", "CRISS CROSS.lvl", "Conquer.lvl", "NO LIMIT.lvl", "TREZ OF COMBAT.lvl", "TREZ OF LIFE.lvl"}));
        // every map of the library that holds hills has them all, found by id: each present color has 16 cells and a hill in the grid
        int maps_with_hills = 0, wrong = 0;
        for_each_community_map([&](const std::string&, const LevelData& level) {
            int cells[4] = {0, 0, 0, 0};
            for (const auto& c : level.layer2_interactive) if (ants::assets::hill_team_of_id(c.tile_index) >= 0) ++cells[ants::assets::hill_team_of_id(c.tile_index)];
            Grid g;
            if (!g.init_from_level(level)) return;
            bool any = false;
            for (uint8_t t = 0; t < 4; ++t) {
                any = any || cells[t] > 0;
                bool found = false;
                for (const auto& a : g.anthills()) found = found || a.team_id == t;
                if ((cells[t] > 0) != found && !(cells[0] + cells[1] + cells[2] + cells[3] == 0)) ++wrong;
            }
            maps_with_hills += any;
        });
        CHECK(wrong == 0);
        CHECK(maps_with_hills > 500);
    });

    run_case("LF2.5 CRISS CROSS And Conquer (Community-Maps) Have All Four Teams Now: A Hill And Their Start Ants For Each", [] {
        for (const char* file : {"CRISS CROSS.lvl", "Conquer.lvl"}) {
            const LevelData level = community(file);
            SimulationEngine sim;
            sim.init(level, 3, 0x0F);
            CHECK(sim.grid().anthills().size() == 4);
            int ants_of[4] = {0, 0, 0, 0};
            for (const auto& a : sim.get_world_state().ants) if (a.player_id < 4) ++ants_of[a.player_id];
            CHECK(ants_of[0] > 0 && ants_of[1] > 0 && ants_of[2] > 0 && ants_of[3] > 0);
        }
    });

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 3: the terrain class of a layer-1 cell is the original's table by tile id (FUN_01008af7): water 476 .. 484, mud 431 .. 474 (even), dirt 523 .. 569 ..., never the dictionary's name
    // or a flag bit of the cell other than the solid bit
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF3.1 A Layer-1 Id Of Water, Mud Or Dirt Is That Terrain Whatever The Dictionary Calls It (476 Water, 435 .. 444 Mud, 559 Dirt, A Sand Id): The Class Table By Id, For Every Id Of The CHD", [] {
        for (uint16_t id = 0; id < movement::kTileIdCount; ++id) {
            LevelData level = open_ground();
            level.layer1_terrain[40].tile_index = id;
            level.layer1_terrain[40].flags = 0;
            for (const char* name : {".", "", "whatever"}) {
                name_tile(level, id, name);
                Grid g;
                CHECK(g.init_from_level(level));
                const TileCoord t{static_cast<int32_t>(40 % level.width), static_cast<int32_t>(40 / level.width)};
                if (g.terrain_class_at(t) != movement::terrain_class_of_tile(id)) { CHECK(false); std::cout << "    (id " << id << " name \"" << name << "\")\n"; return; }
                CHECK((cell_at(g, t).terrain_type == TERRAIN_WATER) == (movement::terrain_class_of_tile(id) == movement::kTerrainWater));
                CHECK(cell_at(g, t).terrain_type != TERRAIN_OBSTACLE);
            }
        }
        const auto class_of = [](uint16_t id) { LevelData level = open_ground(); level.layer1_terrain[40].tile_index = id; name_tile(level, id, "."); Grid g; g.init_from_level(level); return g.terrain_class_at(TileCoord{static_cast<int32_t>(40 % level.width), static_cast<int32_t>(40 / level.width)}); };
        CHECK(class_of(476) == movement::kTerrainWater);
        for (uint16_t id = 435; id <= 444; ++id) CHECK(class_of(id) == movement::kTerrainMud);
        CHECK(class_of(559) == movement::kTerrainDirt);
        CHECK(class_of(571) == movement::kTerrainSand);
    });

    run_case("LF3.2 Layer-1 Flag Bit 0x100 Is Not Water: The Original Keeps Bit 0 Of The Cell Word And Nothing Else (FUN_010069d8); A Tile Of Grass With The Bit Is Grass, Named Or Not", [] {
        LevelData base = open_ground();
        const uint16_t grass = 300;                                  // (a tile of the table's class 0)
        base.layer1_terrain[40].tile_index = grass;
        CHECK(movement::terrain_class_of_tile(grass) == movement::kTerrainGrass);
        for (const char* name : {".", ""}) {
            for (const uint16_t flags : {uint16_t{0x100}, uint16_t{0x101}, uint16_t{0xFFFE}}) {
                LevelData level = base;
                level.layer1_terrain[40].flags = flags;
                name_tile(level, grass, name);
                Grid g;
                CHECK(g.init_from_level(level));
                const TileCoord t{static_cast<int32_t>(40 % level.width), static_cast<int32_t>(40 / level.width)};
                CHECK(cell_at(g, t).terrain_type == TERRAIN_WALKABLE && g.terrain_class_at(t) == movement::kTerrainGrass);
                CHECK(cell_at(g, t).static_solid == ((flags & 1u) != 0));            // (bit 0 is the solid bit and the only one that is kept)
            }
        }
    });

    run_case("LF3.3 The Tile Ids 1 And 2 Are Grass In The Original's Table (The Remake Made 1 A Wall And 2 Water When The Dictionary Left Them \".\")", [] {
        CHECK(movement::terrain_class_of_tile(1) == movement::kTerrainGrass && movement::terrain_class_of_tile(2) == movement::kTerrainGrass);
        for (const uint16_t id : {uint16_t{1}, uint16_t{2}}) {
            LevelData level = open_ground();
            level.layer1_terrain[40].tile_index = id;
            level.layer1_terrain[40].flags = 0;
            name_tile(level, id, ".");
            Grid g;
            CHECK(g.init_from_level(level));
            const TileCoord t{static_cast<int32_t>(40 % level.width), static_cast<int32_t>(40 / level.width)};
            CHECK(cell_at(g, t).terrain_type == TERRAIN_WALKABLE && !g.is_solid_object(t));
        }
    });

    run_case("LF3.4 A Lake Named \".\" Stops A Worker And Not A Swimmer; Mud Named \".\" Is Mud (The Walk Takes The Mud's Pace)", [] {
        const auto run = [&](uint16_t id, AntType type, bool* arrived) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 1);
            if (at.empty()) { CHECK(false); return TileCoord{0, 0}; }
            const int32_t x0 = at[0].x - 3;
            for (uint32_t y = 0; y < level.height; ++y)
                for (int32_t x = x0 + 2; x <= x0 + 4; ++x) {                     // a band three columns wide across the whole map: no way around
                    level.layer1_terrain[y * level.width + static_cast<uint32_t>(x)].tile_index = id;
                    level.layer1_terrain[y * level.width + static_cast<uint32_t>(x)].flags = 0;
                }
            name_tile(level, id, ".");
            SimulationEngine sim;
            sim.init(level, 5, 0x0F);
            const uint32_t ant = sim.spawn_unit(0, type, TileCoord{x0, at[0].y});
            sim.issue_move_order(ant, TileCoord{x0 + 7, at[0].y});
            for (int t = 0; t < 600; ++t) sim.tick();
            *arrived = sim.get_unit(ant).pos == TileCoord{x0 + 7, at[0].y};
            return sim.get_unit(ant).pos;
        };
        bool worker_arrives = true, swimmer_arrives = false, mud_worker_arrives = false;
        run(476, AntType::Worker, &worker_arrives);
        run(476, AntType::Swimmer, &swimmer_arrives);
        run(440, AntType::Worker, &mud_worker_arrives);
        CHECK(!worker_arrives);
        CHECK(swimmer_arrives);
        CHECK(mud_worker_arrives);
    });

    run_case("LF3.5 The Community Library: 14 Maps Had A Layer-1 Class Of Their Own Making (Water 476 In 1: 960 Cells; Mud 435 .. 444 In 5 Of The Gant2 Family And eze jungal; Dirt 559 In 5 Of The Turtle World Family; Flag 0x100 In 3; The Ids 1 And 2 In 2) And Are The Original's Now", [] {
        std::set<std::string> changed;
        long water476 = 0, mud = 0, dirt559 = 0, flag100 = 0, id1 = 0, id2 = 0;
        for_each_community_map([&](const std::string& name, const LevelData& level) {
            Grid g;
            if (!g.init_from_level(level)) return;
            for (uint32_t y = 0; y < level.height; ++y) {
                for (uint32_t x = 0; x < level.width; ++x) {
                    const auto& c = level.get_cell_layer1(x, y);
                    const std::string& n = level.get_tile_name(c.tile_index);
                    const bool dot = n.empty() || n == ".";
                    if (dot && c.tile_index == 476) ++water476;
                    // the loader's rule before this change
                    uint8_t old_type = TERRAIN_WALKABLE;
                    uint8_t old_class = movement::kTerrainGrass;
                    if (dot) {
                        if (c.tile_index == 2 || (c.flags & 0x100u) != 0) old_type = TERRAIN_WATER;
                        else if (c.tile_index == 1) old_type = TERRAIN_OBSTACLE;
                    } else {
                        old_class = movement::terrain_class_of_tile(c.tile_index);
                        old_type = old_class == movement::kTerrainWater ? TERRAIN_WATER : TERRAIN_WALKABLE;
                    }
                    const TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                    const TileCell& cell = cell_at(g, t);
                    // the layer-1 class of the cell, as the loader stored it (terrain_class_at would say mud under a bridge of layer 2, and the cells beside a hill are made walkable whatever they are)
                    const uint8_t now_class = cell.surface_type == SurfaceType::Water ? movement::kTerrainWater : cell.is_mud ? movement::kTerrainMud : cell.surface_type == SurfaceType::Slate ? movement::kTerrainSand
                                              : cell.surface_type == SurfaceType::Gravel ? movement::kTerrainDirt : movement::kTerrainGrass;
                    if (cell.terrain_type == TERRAIN_OBSTACLE) continue;                       // (the mound of a hill: obstacle in both)
                    if (now_class != movement::terrain_class_of_tile(c.tile_index)) { CHECK(false); std::cout << "\n    " << name << " (" << x << "," << y << ") id " << c.tile_index << " class " << int{now_class} << " want " << int{movement::terrain_class_of_tile(c.tile_index)} << " type " << int{cell.terrain_type} << " l2 " << cell.interactive_id; return; }         // (the table's, by id)
                    const int old_effective = old_type == TERRAIN_OBSTACLE ? 100 : old_type == TERRAIN_WATER ? int{movement::kTerrainWater} : int{old_class};
                    const bool differs = old_effective != now_class;
                    if (!differs) continue;
                    changed.insert(name);
                    if (dot && c.tile_index >= 435 && c.tile_index <= 444) ++mud;
                    else if (dot && c.tile_index == 559) ++dirt559;
                    else if (dot && c.tile_index == 1) ++id1;
                    else if (dot && c.tile_index == 2) ++id2;
                    else if (dot && (c.flags & 0x100u) != 0) ++flag100;
                }
            }
        });
        std::cout << "\n    " << changed.size() << " maps: water 476 " << water476 << " cells, mud " << mud << ", dirt 559 " << dirt559 << ", flag 0x100 " << flag100 << ", id 1 " << id1 << ", id 2 " << id2 << "\n";
        const std::set<std::string> want = {"!TurtleWorld.lvl", "#Turtle World.lvl", "'s 4 Player Royal.lvl", "Bloom.lvl", "Gant2.lvl", "Sand Box.lvl", "SandBox.lvl", "Turtle World.lvl", "Turtle.lvl", "Wata fight.LVL",
                                            "eze jungal.lvl", "gant 2.lvl", "gauntlet2.lvl", "sand-box.lvl"};
        CHECK(changed == want);
        CHECK(water476 == 960);
    });

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 4: block 1 holds start markers and plants (tile flag 0x10 or 0x20); every other record is ignored by the original (FUN_0100dbb0)
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF4.1 Block-1 Records The Original Ignores (Ids 0, 63 .. 66, 148, 151, 296 ...) Change Nothing In The Engine, Whatever Their Dictionary Entries Say, Even \"flower\"", [] {
        const LevelData base = open_ground();
        const std::vector<TileCoord> at = open_tiles(base, 4);
        CHECK(at.size() == 4);
        if (at.size() != 4) return;
        const auto hash_of = [&](const LevelData& level) {
            SimulationEngine sim;
            sim.init(level, 3, 0x0F);
            const uint64_t first = sim.state_hash().total;
            for (int t = 0; t < 100; ++t) sim.tick();
            return std::make_pair(first, sim.state_hash().total);
        };
        const auto reference = hash_of(base);
        size_t k = 0;
        for (const uint16_t id : {uint16_t{0}, uint16_t{63}, uint16_t{64}, uint16_t{66}, uint16_t{148}, uint16_t{151}, uint16_t{296}}) {
            for (const char* name : {".", "flower1", "rock1", "GREENHILL"}) {
                LevelData level = base;
                name_tile(level, id, name);
                ants::assets::AnthillSpawn sp;
                sp.tile_id = id;
                sp.x = static_cast<uint16_t>(at[k % 4].x);
                sp.y = static_cast<uint16_t>(at[k % 4].y);
                sp.team_id = ants::assets::start_marker_team_of_id(id) >= 0 ? static_cast<uint8_t>(ants::assets::start_marker_team_of_id(id)) : uint8_t{255};
                level.anthill_spawns.push_back(sp);
                ++k;
                CHECK(sp.team_id == 255);
                CHECK(hash_of(level) == reference);
                Grid g;
                CHECK(g.init_from_level(level) && g.plants().empty() && !cell_at(g, at[(k - 1) % 4]).is_obstacle_overlay);
            }
        }
    });

    run_case("LF4.2 A Plant Is A Plant By Its Id: A Flower Record Is A World Object And Its Stem An Obstacle Overlay, A Record Of Another Id With The Same Name Is Neither", [] {
        for (const bool plant : {true, false}) {
            LevelData level = open_ground();
            const std::vector<TileCoord> at = open_tiles(level, 1);
            CHECK(at.size() == 1);
            if (at.empty()) return;
            const uint16_t id = plant ? uint16_t{410} : uint16_t{148};
            name_tile(level, id, "flower1");
            ants::assets::AnthillSpawn sp;
            sp.tile_id = id;
            sp.x = static_cast<uint16_t>(at[0].x);
            sp.y = static_cast<uint16_t>(at[0].y);
            sp.team_id = 255;
            level.anthill_spawns.push_back(sp);
            Grid g;
            CHECK(g.init_from_level(level));
            CHECK(g.plants().size() == (plant ? 1u : 0u));
            CHECK(cell_at(g, at[0]).is_obstacle_overlay == plant);
        }
    });

    run_case("LF4.3 A Map Without Hill Cells Falls Back To Its Start Markers For Hills, Never To Its Plants Or Other Records", [] {
        LevelData level = tiny();
        for (auto& c : level.layer2_interactive) if (ants::assets::hill_team_of_id(c.tile_index) >= 0) c = ants::assets::MapCell{};
        const size_t markers = level.anthill_spawns.size();
        ants::assets::AnthillSpawn plant;
        plant.tile_id = 410;
        plant.x = 3;
        plant.y = 20;
        plant.team_id = 255;
        level.anthill_spawns.push_back(plant);
        ants::assets::AnthillSpawn ignored = plant;
        ignored.tile_id = 148;
        ignored.x = 5;
        level.anthill_spawns.push_back(ignored);
        Grid g;
        CHECK(g.init_from_level(level));
        CHECK(g.anthills().size() == markers);
        for (const auto& a : g.anthills()) CHECK(a.team_id < 4);
    });

    run_case("LF4.4 The Community Library: 721 Block-1 Records Of Ids The Original Ignores (0, 63, 66, 148, 151, 296) In 17 Maps; No Other Record Is A Start Marker Or A Plant By Name Only", [] {
        long ignored = 0;
        std::set<std::string> maps;
        std::set<uint16_t> ids;
        for_each_community_map([&](const std::string& name, const LevelData& level) {
            for (const auto& sp : level.anthill_spawns) {
                if (ants::assets::start_marker_team_of_id(sp.tile_id) >= 0 || movement::is_plant_object_tile(sp.tile_id)) continue;
                ++ignored;
                maps.insert(name);
                ids.insert(sp.tile_id);
            }
        });
        std::cout << "\n    " << ignored << " records in " << maps.size() << " maps, ids:";
        for (const uint16_t id : ids) std::cout << " " << id;
        std::cout << "\n";
        CHECK(ignored == 721 && maps.size() == 17);
        CHECK((ids == std::set<uint16_t>{0, 63, 66, 148, 151, 296}));
    });

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 5: food is the tile flag 0x02 of the id (FUN_010071dd, 87 ids), not a name that starts with fd or food
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF5.1 A Layer-2 Id With The Food Flag Is Food Whatever The Dictionary Calls It (\".\", Nothing, Another Object's Name) And Is No Obstacle Overlay; An Entry Called fd... Of An Id Without The Flag (344, 287) Is Not Food", [] {
        const LevelData base = open_ground();
        const uint16_t food = a_food_id(base);
        CHECK(food != 0);
        CHECK((movement::tile_flags_of(344) & movement::kTileFlagFood) == 0 && (movement::tile_flags_of(287) & movement::kTileFlagFood) == 0);
        for (const char* name : {".", "", "rock1", "fd_something"}) {
            LevelData level = base;
            const std::vector<TileCoord> at = open_tiles(level, 2);
            CHECK(at.size() == 2);
            if (at.size() != 2) return;
            set_layer2(level, at[0], food);
            set_layer2(level, at[1], 344);
            name_tile(level, food, name);
            name_tile(level, 344, std::string(name).rfind("fd", 0) == 0 ? name : "fdpez2b1");
            Grid g;
            CHECK(g.init_from_level(level));
            CHECK(cell_at(g, at[0]).is_food && cell_at(g, at[0]).has_food() && !cell_at(g, at[0]).is_obstacle_overlay);
            CHECK(!cell_at(g, at[1]).is_food && !cell_at(g, at[1]).has_food());
        }
    });

    run_case("LF5.2 The Stage Tile Of A Food Object Makes Food Cells Only When Its Id Has The Flag (The Library Has Stage Tiles Of Other Kinds: Grass, Rocks, Power-Ups)", [] {
        LevelData level = open_ground();
        const uint16_t food = a_food_id(level);
        const std::vector<TileCoord> at = open_tiles(level, 2);
        CHECK(at.size() == 2 && food != 0);
        if (at.size() != 2 || food == 0) return;
        for (const uint16_t tile : {food, uint16_t{294}}) {                 // 294 medrock2: an object, no food
            LevelData l = level;
            name_tile(l, 294, "medrock2");
            ants::assets::FoodSchedule fs;
            fs.y = static_cast<uint16_t>(at[0].y);
            fs.x = static_cast<uint16_t>(at[0].x);
            fs.initial_delay = 5;
            fs.respawn_interval = 10;
            ants::assets::FoodItemVariant v;
            v.weight = 5;
            v.tile_id = tile;
            fs.variants.push_back(v);
            l.food_schedules.push_back(fs);
            Grid g;
            CHECK(g.init_from_level(l));
            CHECK(cell_at(g, at[0]).interactive_id == tile);
            CHECK(cell_at(g, at[0]).is_food == (tile == food));
        }
    });

    run_case("LF5.3 The Community Library: 29 Maps Hold Food Ids That Their Dictionaries Call \".\" Or Leave Unnamed, 2 Hold fd... Entries Of Ids That Are Not Food (!TreasureIsland, Arena101); The Grid's Food Is The Flag In Every Map", [] {
        std::set<std::string> flag_only, name_only;
        long mismatches = 0;
        for_each_community_map([&](const std::string& name, const LevelData& level) {
            Grid g;
            if (!g.init_from_level(level)) return;
            for (uint32_t y = 0; y < level.height; ++y) {
                for (uint32_t x = 0; x < level.width; ++x) {
                    const uint16_t id = level.get_cell_layer2(x, y).tile_index;
                    if (id == ants::assets::LVL_EMPTY_TILE || id == 0xFFFFu) continue;
                    const std::string& n = level.get_tile_name(id);
                    std::string lo = n;
                    for (char& c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    const bool dot = n.empty() || n == ".";
                    const bool old_food = !dot && (lo.rfind("fd", 0) == 0 || lo.rfind("food", 0) == 0);
                    const bool flag = (movement::tile_flags_of(id) & movement::kTileFlagFood) != 0;
                    if (old_food && !flag) name_only.insert(name);
                    if (!old_food && flag) flag_only.insert(name);
                    const TileCell& c = g.get_cell(x, y);
                    if (c.interactive_id == id && c.is_food != flag && !c.is_powerup && !c.has_bomb()) ++mismatches;     // (a pile of a food object may have replaced the cell: then it holds another id)
                }
            }
        });
        std::cout << "\n    flag only: " << flag_only.size() << " maps, name only: " << name_only.size() << " maps, cells where the grid differs from the flag: " << mismatches << "\n";
        CHECK(flag_only.size() == 29);
        CHECK((name_only == std::set<std::string>{"!TreasureIsland.lvl", "Arena101.lvl"}));
        CHECK(mismatches == 0);
    });

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 6: SetTile refuses a tile whose graphic is not loaded (FUN_01007352): an id the dictionary calls "." that is not one of the 62 forced ids
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF6.1 Which Graphics Are Loaded: Every Dictionary Entry But \".\", Plus The 62 Forced Ids Whatever Their Entry; An Id Past The Dictionary Is Not; A World Made By Hand Has All", [] {
        LevelData level = tiny();
        for (std::string& name : level.tile_dictionary) name = ".";
        level.tile_dictionary[300] = "";                              // (an empty entry is not \".\": kept)
        level.tile_dictionary[301] = "........";                      // (nor is a longer run of dots)
        level.tile_dictionary[302] = "medrock1";
        Grid g;
        CHECK(g.init_from_level(level));
        int forced = 0;
        for (uint16_t id = 0; id < movement::kTileIdCount; ++id) {
            const bool is_forced = (id >= 34 && id <= 37) || id == 40 || id == 53 || (id >= 62 && id <= 66) || (id >= 129 && id <= 135) || id == 160 || (id >= 162 && id <= 178) || id == 230 ||
                                   (id >= 245 && id <= 248) || id == 356 || (id >= 404 && id <= 408) || (id >= 410 && id <= 416) || id == 420 || id == 421 || (id >= 422 && id <= 426) || id == 1177;
            forced += is_forced;
            const bool want = is_forced || id == 300 || id == 301 || id == 302;
            if (g.tile_graphic_loaded(id) != want) { CHECK(false); std::cout << "    (id " << id << ")\n"; return; }
        }
        CHECK(forced == 62);
        Grid by_hand;
        CHECK(by_hand.init_empty(8, 8));
        for (uint16_t id = 0; id < 2000; ++id) CHECK(by_hand.tile_graphic_loaded(id));
        // a level that is read after one with a dictionary keeps nothing of it
        level = tiny();
        CHECK(g.init_from_level(level) && g.tile_graphic_loaded(2) == (level.tile_dictionary[2] != "."));
    });

    run_case("LF6.2 A Food Object Whose Stage Tile Is Called \".\" (And Is Not A Forced Id): The Old Tile At The Anchor Is Cleared And The Stage Tile Is Not Placed; Called By A Name, It Is Placed", [] {
        const LevelData base = open_ground();
        const uint16_t food = a_food_id(base);
        const std::vector<TileCoord> at = open_tiles(base, 1);
        CHECK(food != 0 && at.size() == 1);
        if (food == 0 || at.empty()) return;
        for (const bool loaded : {true, false}) {
            LevelData level = base;
            level.food_schedules.clear();
            set_layer2(level, at[0], 294);                              // what the file holds at the anchor (an object of another kind)
            name_tile(level, 294, "medrock2");
            name_tile(level, food, loaded ? "fdname" : ".");
            ants::assets::FoodSchedule fs;
            fs.y = static_cast<uint16_t>(at[0].y);
            fs.x = static_cast<uint16_t>(at[0].x);
            fs.initial_delay = 5;
            fs.respawn_interval = 10;
            ants::assets::FoodItemVariant v;
            v.weight = 5;
            v.tile_id = food;
            fs.variants.push_back(v);
            level.food_schedules.push_back(fs);
            Grid g;
            CHECK(g.init_from_level(level));
            CHECK(g.food_objects().size() == 1);                        // (the object is there with its units)
            CHECK(loaded ? cell_at(g, at[0]).interactive_id == food : cell_at(g, at[0]).is_empty_overlay());
            CHECK(!loaded ? !cell_at(g, at[0]).is_food && !g.is_solid_object(at[0]) : cell_at(g, at[0]).is_food);
        }
    });

    run_case("LF6.3 The Same At Run Time: The Pile Whose Next Stage Is A Tile That Is Not Loaded Vanishes At The Bite That Changes Its Stage (The Object Keeps Its Units)", [] {
        LevelData level = open_ground();
        const uint16_t food = a_food_id(level);
        const std::vector<TileCoord> at = open_tiles(level, 1);
        CHECK(food != 0 && at.size() == 1);
        if (food == 0 || at.empty()) return;
        uint16_t second = 0;
        for (uint16_t id = 0; id < level.tile_dictionary.size() && second == 0; ++id)
            if (id != food && (movement::tile_flags_of(id) & movement::kTileFlagFood) != 0 && level.tile_dictionary[id].rfind("fd", 0) == 0) second = id;
        CHECK(second != 0);
        level.food_schedules.clear();
        name_tile(level, second, ".");                                   // the next stage's entry is left out
        ants::assets::FoodSchedule fs;
        fs.y = static_cast<uint16_t>(at[0].y);
        fs.x = static_cast<uint16_t>(at[0].x);
        fs.initial_delay = 4;
        fs.respawn_interval = 10;
        ants::assets::FoodItemVariant a, b;
        a.weight = 4; a.tile_id = food;
        b.weight = 2; b.tile_id = second;
        fs.variants = {a, b};
        level.food_schedules.push_back(fs);
        Grid g;
        CHECK(g.init_from_level(level));
        CHECK(cell_at(g, at[0]).interactive_id == food);
        bool changed = false;
        g.take_food(0, 2, changed);
        CHECK(changed);
        g.set_food_tile(at[0], g.food_objects()[0].stage_tile());
        CHECK(cell_at(g, at[0]).is_empty_overlay());
        CHECK(g.food_objects()[0].remaining == 2);
    });

    run_case("LF6.4 The Forced Ids Are Placed Whatever The Dictionary Says: A Fire Wall, The Placeholder, A Bridge Stage And A Bomb Through set_layer2; A Tile That Is Not Loaded Is Refused After Clearing The Old One", [] {
        LevelData level = open_ground();
        const std::vector<TileCoord> at = open_tiles(level, 2);
        CHECK(at.size() == 2);
        if (at.size() != 2) return;
        for (uint16_t id = 0; id < level.tile_dictionary.size(); ++id) level.tile_dictionary[id] = ".";
        Grid g;
        CHECK(g.init_from_level(level));
        for (const uint16_t id : {uint16_t{134}, uint16_t{160}, uint16_t{34}, uint16_t{37}, uint16_t{129}, uint16_t{356}}) {
            g.set_layer2(static_cast<uint32_t>(at[0].x), static_cast<uint32_t>(at[0].y), id, 0);
            CHECK(cell_at(g, at[0]).interactive_id == id);
        }
        g.set_layer2(static_cast<uint32_t>(at[0].x), static_cast<uint32_t>(at[0].y), TILE_RESERVED, 0);
        CHECK(cell_at(g, at[0]).static_solid);
        g.set_layer2(static_cast<uint32_t>(at[0].x), static_cast<uint32_t>(at[0].y), 300, 0);        // 300 is called "." and not forced
        CHECK(cell_at(g, at[0]).is_empty_overlay() && !cell_at(g, at[0]).static_solid);
    });

    run_case("LF6.5 The Community Library: The Block-2 Stage Tiles Of 34 Maps Name Entries That Are \".\" And Not Forced (The Original Does Not Place Them)", [] {
        long maps = 0, refused_cells = 0;
        for_each_community_map([&](const std::string&, const LevelData& level) {
            Grid g;
            if (!g.init_from_level(level)) return;
            bool any = false;
            for (const auto& fs : level.food_schedules) {
                for (const auto& v : fs.variants) {
                    if (v.tile_id == ants::assets::LVL_EMPTY_TILE || v.tile_id == 0x7FFE) continue;
                    if (!g.tile_graphic_loaded(v.tile_id)) { any = true; ++refused_cells; }
                }
            }
            maps += any;
        });
        std::cout << "\n    " << maps << " maps, " << refused_cells << " stage tiles\n";
        CHECK(maps == 34);                                                  // (the notes' 40 also count 5 maps whose entry is empty: strcmp(\"\", \".\") != 0, so the original keeps those graphics)
    });

    // ---------------------------------------------------------------------------------------------------------------------------------------------
    // Fix 7: a layer-2 cell whose dictionary entry is "." (or empty) is no obstacle overlay by that name
    // ---------------------------------------------------------------------------------------------------------------------------------------------

    run_case("LF7.1 An Object Whose Entry Is \".\" Or Empty Blocks Exactly As Its Solid Bit Says: Bit Clear, It Is Walkable Ground (No Obstacle Overlay); Bit Set, It Is Solid", [] {
        for (const char* name : {".", ""}) {
            for (const bool bit : {false, true}) {
                LevelData level = open_ground();
                const std::vector<TileCoord> at = open_tiles(level, 1);
                CHECK(at.size() == 1);
                if (at.empty()) return;
                set_layer2(level, at[0], 294);                      // an object (medrock2) in a dictionary that left its entry out
                name_tile(level, 294, name);
                set_solid_bit(level, at[0], bit);
                Grid g;
                CHECK(g.init_from_level(level));
                const TileCell& c = cell_at(g, at[0]);
                CHECK(!c.is_obstacle_overlay && c.interactive_id == 294);
                CHECK(g.is_solid_object(at[0]) == bit);
                CHECK(c.is_passable() == true);                    // (the cell itself does not block; the solid bit does, in is_solid_object)
            }
        }
    });

    run_case("LF7.2 A Start Ant Stands On Its Marker When A \".\" Object Without A Solid Bit Lies There (The Overlay Made The Cell Impassable And Moved The Ant); A Named Rock Still Moves It", [] {
        for (const int variant : {0, 1, 2}) {                      // 0: "." and no bit, 1: "." with the bit, 2: a named rock without the bit
            LevelData level = tiny();
            const ants::assets::AnthillSpawn* marker = nullptr;
            for (const auto& sp : level.anthill_spawns) if (sp.team_id == 0) { marker = &sp; break; }
            CHECK(marker != nullptr);
            if (marker == nullptr) return;
            const TileCoord m{marker->x, marker->y};
            set_layer2(level, m, 294);
            name_tile(level, 294, variant == 2 ? "medrock2" : ".");
            set_solid_bit(level, m, variant == 1);
            SimulationEngine sim;
            sim.init(level, 3, 0x01);
            bool on_marker = false;
            for (const auto& a : sim.get_world_state().ants) on_marker = on_marker || (a.player_id == 0 && a.tile_x == m.x && a.tile_y == m.y);
            if (variant != 2) CHECK(on_marker);                     // (the cell's own passability is the terrain and the overlay: the solid bit is asked of paths, not of the spawn)
            else CHECK(!on_marker);
        }
    });

    run_case("LF7.3 The Community Library: No Layer-2 Cell Is An Obstacle Overlay By An Empty Or \".\" Entry Any More; The Cells Named That Way Are Counted", [] {
        long dot_cells = 0, dot_overlays = 0;
        long maps_with_dots = 0;
        for_each_community_map([&](const std::string&, const LevelData& level) {
            Grid g;
            if (!g.init_from_level(level)) return;
            long here = 0;
            for (uint32_t y = 0; y < level.height; ++y) {
                for (uint32_t x = 0; x < level.width; ++x) {
                    const uint16_t id = level.get_cell_layer2(x, y).tile_index;
                    if (id == ants::assets::LVL_EMPTY_TILE || id == 0xFFFFu) continue;
                    const std::string& n = level.get_tile_name(id);
                    if (!(n.empty() || n == ".")) continue;
                    ++dot_cells;
                    ++here;
                    const TileCell& c = g.get_cell(x, y);
                    // (a hill's mound keeps its overlay: that is the hill, not the name)
                    if (c.is_obstacle_overlay && c.interactive_id == id && c.base_owner_team == 255) ++dot_overlays;
                }
            }
            maps_with_dots += here > 0;
        });
        std::cout << "\n    cells named \".\" or empty: " << dot_cells << " in " << maps_with_dots << " maps; of them overlays: " << dot_overlays << "\n";
        CHECK(dot_overlays == 0);
        CHECK(dot_cells > 90000);
        CHECK(dot_cells == 99904 && maps_with_dots == 465);                  // (the 534 maps of the folder; 101,806 in 484 of the 558 that were first taken in)
    });

    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
