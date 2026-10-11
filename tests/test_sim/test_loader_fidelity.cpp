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

    run_case("LF1.6 The Community Library: 94 Maps Hold Power-Ups With No Solid Bit (3,132 Cells), 36 Hold Food Cells Without It (183 Cells), 2 Hold Fire Wall Cells Without It (439 Cells); None Of Them Is An Obstacle Any More, And A Fire Wall Cell With The Bit Still Is", [] {
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
        CHECK(pu_maps == 94 && pu_cells == 3132);
        CHECK(fire_maps == 2 && fire_cells == 439);
        CHECK(food_maps == 36 && food_cells == 183);
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

    std::cout << "\n" << g_cases << " cases, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
