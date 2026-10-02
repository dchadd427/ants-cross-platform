// Tests of what a level file says that the simulation must obey exactly as the original does, for the maps that third-party editors made (the community library):
//   * the DEFAULT ANT TYPE (LVL block 3, Ants.exe FUN_01007025 -> level+0x70): every ant whose own type is Worker (all of them are, until one takes a power-up) is of the
//     type that the block names, wherever the game asks the ant type getter FUN_0100f9cb(ant, 0) (25 call sites, docs/GAME_REVERSE_ENGINEERING.md 5.62). The ants a level
//     starts with and the ants that hatch are all created with the own type 0 (the constructor 0x101a77a, 0x101a815).
//   * POWER-UPS AND FLOWER DROPPERS by TILE ID (FUN_01007202 at 0x1007202 for ids 62 .. 66, FUN_01007227 at 0x1007227 for the plants of the droppers): the dictionary of a
//     map names its tiles, and community editors often name these entries "." (the dictionary remap of FUN_0100674e is the identity, 0x10067a5; the name only decides
//     which graphics load, and the ids of the power-ups and flowers load whatever it says: the table at 0x1001ba8).
// A level that names no default type (every shipped map) and whose power-ups carry their names plays as before, with ONE exception that the review of this change found and that is a
// change of the lock-step rules: the last frame of an attack clip resumes the saved auto-engage of ANY ant whose engage flag (+0xbc) is set, as the original does (0x101ef4a calls
// FUN_0101dd6f without a type test), and the flag outlives a change of type, so a combat ant that was ordered away in the middle of an auto-engage and then took another power-up
// resumes at the end of its next attack, where v0.0.92 left it standing (tests 4.6 and 4.7; test 1.8 pins plays of the shipped maps in which that happens). Test 1.7 pins the state
// hashes of plays of the six shipped maps with move orders only, which are those of v0.0.92; the other tests pin the new behaviour with synthetic levels made here from the bytes
// of TINY.LVL (nothing of the community library is in the repository).
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ants::sim;
using ants::assets::LevelData;
using ants::assets::MapCell;
using ants::assets::AnthillSpawn;
using ants::assets::Waypoint;

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(100) << name << " ... " << std::flush;
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

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

namespace {

constexpr uint16_t kNoDefault = 0x7FFE;
constexpr uint16_t kPowerUpTiles[5] = {62, 63, 64, 65, 66};              // Combat, Thief, Bomber, Swimmer, Fire
constexpr AntType kTypeOfTile[5] = {AntType::Combat, AntType::Thief, AntType::Bomber, AntType::Swimmer, AntType::Fire};
constexpr const char* kNameOfTile[5] = {"pu_comb", "pu_thief", "pu_bomb", "pu_swim", "pu_mason"};     // what the shipped maps call them
constexpr uint8_t kPowerUpTypeOfTile[5] = {4, 3, 1, 5, 2};              // FUN_01021087 (the numbers of the cell's powerup_type)

const char* type_name(AntType t) {
    switch (t) {
        case AntType::Worker: return "worker";
        case AntType::Bomber: return "bomber";
        case AntType::Fire: return "fire";
        case AntType::Thief: return "thief";
        case AntType::Combat: return "combat";
        case AntType::Swimmer: return "swimmer";
    }
    return "?";
}

std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/"; }

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const std::streamsize size = f.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}
uint16_t word_at(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); }
uint32_t dword_at(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint32_t>(word_at(d, p)) | (static_cast<uint32_t>(word_at(d, p + 2)) << 16); }

// The offset of block 3 (two words: a flag and the tile index) in a map file: the loader's order is header, dictionary, dimensions, two layers, block 1, block 2, block 3.
size_t block3_offset(const std::vector<uint8_t>& d) {
    size_t p = 42 + (static_cast<size_t>(word_at(d, 40)) + 1) * 11;
    const size_t rows = dword_at(d, p);
    const size_t columns = dword_at(d, p + 4);
    p += 8 + rows * columns * 12;
    p += 2 + static_cast<size_t>(word_at(d, p)) * 6;                         // block 1
    const size_t objects = word_at(d, p);                                    // block 2
    p += 2;
    for (size_t i = 0; i < objects; ++i) p += 10 + static_cast<size_t>(word_at(d, p + 8)) * 4;
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Synthetic levels
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

const LevelData& tiny_level() {
    static LevelData level;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (!level.load_from_file(maps_dir() + "TINY.LVL")) std::cout << "  (cannot load TINY.LVL)\n";
    }
    return level;
}

// TINY (31 x 31, four hills, three starting ants for green and blue, four for red and black, no power-up, plant or waypoint) with block 3 naming `tile`
LevelData level_with_default(uint16_t tile, const std::string& name_of_entry_62 = ".") {
    LevelData level = tiny_level();
    level.ambient_flag = 0;
    level.ambient_tile_or_sound = tile;
    level.tile_dictionary[62] = name_of_entry_62;                // the map of the owner's popcorn case: block 3 = 62 and the dictionary calls that entry "."
    return level;
}

// One layer-2 cell as an editor writes a power-up: its tile id, the object flag 1, the anchor (column, row) in the properties and the solid bit in layer 1
void put_cell(LevelData& level, int x, int y, uint16_t tile) {
    const size_t i = static_cast<size_t>(y) * level.width() + static_cast<size_t>(x);
    MapCell& c2 = level.layer2_interactive[i];
    c2.tile_index = tile;
    c2.flags = 1;
    c2.properties = static_cast<uint16_t>((x << 8) | y);
    level.layer1_terrain[i].flags = 1;
}

// TINY with the five power-ups in row 10 (columns 12, 14, 16, 18, 20), their dictionary entries called by `names` ("." or the CHD names)
LevelData level_with_powerups(bool dotted) {
    LevelData level = tiny_level();
    for (int k = 0; k < 5; ++k) {
        level.tile_dictionary[kPowerUpTiles[k]] = dotted ? "." : kNameOfTile[k];
        put_cell(level, 12 + 2 * k, 10, kPowerUpTiles[k]);
    }
    return level;
}

// TINY with one flower (or clover) record in block 1 at (x, y) and the block 4 record that makes it a dropper: the interval in seconds, the probabilities of the five
// kinds (bomber, combat, thief, swimmer, fire), `flag` (the original drops for any flag but 0)
LevelData level_with_dropper(uint16_t plant_tile, const std::string& plant_name, int x, int y, uint32_t flag, uint32_t interval, std::array<double, 5> p) {
    LevelData level = tiny_level();
    level.tile_dictionary[plant_tile] = plant_name;
    AnthillSpawn sp;
    sp.tile_id = plant_tile;
    sp.x = static_cast<uint16_t>(x);
    sp.y = static_cast<uint16_t>(y);
    sp.team_id = 255;
    level.anthill_spawns.push_back(sp);
    Waypoint wp;
    wp.x = static_cast<uint16_t>(x);
    wp.y = static_cast<uint16_t>(y);
    wp.flag = flag;
    wp.param = interval;
    wp.probabilities = p;
    level.waypoints.push_back(wp);
    return level;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Test worlds
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

void make_world(SimulationEngine& sim, uint16_t default_tile, uint32_t seed = 11) {
    sim.init_test_world(60, 60, seed, 720000);
    sim.set_anthill(0, TileCoord{2, 2});
    sim.set_anthill(1, TileCoord{50, 50});
    sim.set_player_score(0, 0);
    sim.set_player_score(1, 0);
    sim.grid_mut().set_default_ant_tile(default_tile);
}

void run_ticks(SimulationEngine& sim, int ticks) {
    for (int t = 0; t < ticks; ++t) sim.tick();
}

Command group(CommandType type, uint8_t issuer, int x, int y, const std::vector<uint32_t>& ants) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.tile_x = static_cast<int16_t>(x);
    c.tile_y = static_cast<int16_t>(y);
    c.ants = ants;
    return c;
}

const AntSnapshot* snapshot_of(const SimulationEngine& sim, uint32_t id) {
    for (const AntSnapshot& a : sim.get_world_state().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

size_t count_powerups(const SimulationEngine& sim) {
    size_t n = 0;
    const Grid& g = sim.grid();
    for (uint32_t y = 0; y < g.height(); ++y) {
        for (uint32_t x = 0; x < g.width(); ++x) n += g.get_cell(x, y).has_powerup() ? 1u : 0u;
    }
    return n;
}

// Everything a person (and the renderer) can see of the ants, as one line per ant: the equivalence tests compare two worlds tick by tick through it
std::string look_at_ants(const SimulationEngine& sim) {
    std::ostringstream o;
    for (const AntSnapshot& a : sim.get_world_state().ants) {
        o << a.id << ':' << static_cast<int>(a.player_id) << ':' << static_cast<int>(a.type) << ':' << a.px << ',' << a.py << ':' << a.tile_x << ',' << a.tile_y << ':' << a.hp << ':'
          << static_cast<int>(a.state) << ':' << a.anim_state << ':' << a.anim_frame << ':' << a.is_holding << a.is_stunned << a.is_swimming << a.is_drowning << a.is_on_mud << a.frozen << ':'
          << a.burn_elapsed_ms << ':' << a.loco_clip << ',' << a.loco_frame << ',' << a.loco_mirrored << ',' << a.loco_left_ms << ';';
    }
    const Grid& g = sim.grid();
    for (uint32_t y = 0; y < g.height(); ++y) {
        for (uint32_t x = 0; x < g.width(); ++x) {
            const TileCell& c = g.get_cell(x, y);
            // (not the power-ups: a typed ant that dies leaves one and an ant that never took one does not, whatever the level's default is: tests 4.1, 4.2)
            if (c.has_bomb() || c.has_fire() || c.has_any_bridge() || c.has_reserved()) o << '[' << x << ',' << y << '=' << c.interactive_id << ']';
        }
    }
    return o.str();
}

// A scripted scenario: the commands that the issuer gives at given ticks, to the ants named by the scenario's fixture
struct Step {
    int tick;
    std::function<void(SimulationEngine&)> act;
};

// Plays `steps` for `ticks` ticks and returns the look of the world at every tick
std::vector<std::string> play(SimulationEngine& sim, const std::vector<Step>& steps, int ticks) {
    std::vector<std::string> trace;
    for (int t = 0; t < ticks; ++t) {
        for (const Step& s : steps) {
            if (s.tick == t) s.act(sim);
        }
        sim.tick();
        trace.push_back(look_at_ants(sim));
    }
    return trace;
}

// The tick where two traces first differ (-1 = never)
int first_difference(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) return 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return static_cast<int>(i);
    }
    return -1;
}

// The equivalence test: an ant of the own type `type` on a map without a default type, and an ant of the own type Worker on a map whose default is that type's tile, must be the
// same ant in everything that can be seen (type, state, position, clip, frame, hit points ...) at every tick of the scenario. The other ants (the enemy `Thief`, which neither
// has a reflex nor a special order) are the same on both maps.
using Fixture = std::function<void(SimulationEngine&, uint32_t ant, uint32_t enemy)>;
using Script = std::function<std::vector<Step>(uint32_t ant, uint32_t enemy)>;

int equivalence(AntType type, uint16_t tile, const Fixture& fixture, const Script& script, int ticks, const std::string& label, std::vector<std::string>* sample = nullptr,
                uint32_t seed = 11) {
    std::vector<std::string> traces[2];
    for (int world = 0; world < 2; ++world) {
        SimulationEngine sim;
        make_world(sim, world == 0 ? kNoDefault : tile, seed);
        const uint32_t ant = sim.spawn_unit(0, world == 0 ? type : AntType::Worker, TileCoord{10, 10});
        const uint32_t enemy = sim.spawn_unit(1, AntType::Thief, TileCoord{40, 40});
        fixture(sim, ant, enemy);
        traces[world] = play(sim, script(ant, enemy), ticks);
    }
    const int diff = first_difference(traces[0], traces[1]);
    if (diff >= 0) {
        std::cout << "\n    " << label << ": the worlds differ at tick " << diff << "\n      own type " << type_name(type) << ": " << traces[0][static_cast<size_t>(diff)].substr(0, 260)
                  << "\n      default " << type_name(type) << ":  " << traces[1][static_cast<size_t>(diff)].substr(0, 260) << "\n    ";
    }
    if (sample != nullptr) *sample = traces[1];
    return diff;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A scripted player at the command layer (test 1.8)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
};

// The plays of test 1.8: `level` is played with the engine seed `seed` and `roster`, and the state hash after each tick of `at` (ascending) is returned. Every half second (every 10
// ticks) each team of the roster gives ONE order, from its own generator, to one of its ants through the player's command layer (apply_command): a click on a power-up (the nearest or a
// random one, so that ants change type on the maps that have power-ups or droppers), a Stop, a special click near the ant, an attack on an enemy ant, a click on an enemy hill (a raid) or
// on a random tile, or a hatch. A team that has fewer points than a hatch costs is given them, as a player who has harvested would have them.
std::vector<uint64_t> play_with_orders(const LevelData& level, uint8_t roster, uint32_t seed, const std::vector<uint32_t>& at) {
    SimulationEngine sim;
    sim.init(level, seed, roster);
    const int32_t width = static_cast<int32_t>(sim.grid().width());
    const int32_t height = static_cast<int32_t>(sim.grid().height());
    std::array<Lcg, 4> rng = {Lcg(seed + 1u), Lcg(seed + 7920u), Lcg(seed + 15839u), Lcg(seed + 23758u)};
    const auto clamp_x = [&](int32_t x) { return std::min(std::max(x, 0), width - 1); };
    const auto clamp_y = [&](int32_t y) { return std::min(std::max(y, 0), height - 1); };
    std::vector<uint64_t> hashes;
    size_t checkpoint = 0;
    const uint32_t last = at.empty() ? 0u : at.back();
    for (uint32_t t = 0; t < last; ++t) {
        if (t % 10 == 0) {
            for (uint8_t team = 0; team < 4; ++team) {
                if ((roster & (1u << team)) != 0 && sim.get_player_score(team) < static_cast<int32_t>(HATCH_COST_POINTS)) sim.set_player_score(team, static_cast<int32_t>(HATCH_COST_POINTS));
            }
            std::vector<TileCoord> powerups;
            const Grid& grid = sim.grid();
            for (int32_t y = 0; y < height; ++y) {
                for (int32_t x = 0; x < width; ++x) {
                    if (grid.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).has_powerup()) powerups.push_back(TileCoord{x, y});
                }
            }
            std::array<std::vector<AntSnapshot>, 4> by_team;
            for (const AntSnapshot& a : sim.get_world_state().ants) {
                if (a.hp == 0 || a.player_id >= 4) continue;
                by_team[a.player_id].push_back(a);
            }
            std::array<std::vector<TileCoord>, 4> hills;
            for (const auto& hill : grid.anthills()) {
                if (hill.team_id < 4) hills[hill.team_id].push_back(TileCoord{hill.x + 1, hill.y + 1});
            }
            for (uint8_t team = 0; team < 4; ++team) {
                if ((roster & (1u << team)) == 0) continue;
                Lcg& r = rng[team];
                const std::vector<AntSnapshot>& mine = by_team[team];
                const uint32_t roll = r.below(100);
                Command c;
                c.issuer = team;
                if (roll < 10) {
                    c.type = CommandType::Hatch;
                    (void)sim.apply_command(c);
                    continue;
                }
                if (mine.empty()) continue;
                const AntSnapshot& lead = mine[r.below(static_cast<uint32_t>(mine.size()))];
                c.ants.push_back(lead.id);
                std::vector<TileCoord> enemies;
                for (uint8_t other = 0; other < 4; ++other) {
                    if (other == team || (roster & (1u << other)) == 0) continue;
                    for (const AntSnapshot& a : by_team[other]) enemies.push_back(TileCoord{a.tile_x, a.tile_y});
                }
                if (roll < 40 && !powerups.empty()) {
                    TileCoord best = powerups[r.below(static_cast<uint32_t>(powerups.size()))];
                    if (r.below(2) == 0) {
                        int32_t best_distance = 1 << 30;
                        for (const TileCoord& q : powerups) {
                            const int32_t d = std::abs(q.x - lead.tile_x) + std::abs(q.y - lead.tile_y);
                            if (d < best_distance) {
                                best_distance = d;
                                best = q;
                            }
                        }
                    }
                    c.type = CommandType::GroupMove;
                    c.tile_x = static_cast<int16_t>(best.x);
                    c.tile_y = static_cast<int16_t>(best.y);
                } else if (roll < 50) {
                    c.type = CommandType::Stop;
                } else if (roll < 68) {
                    c.type = CommandType::GroupSpecial;
                    c.tile_x = static_cast<int16_t>(clamp_x(lead.tile_x + static_cast<int32_t>(r.below(7)) - 3));
                    c.tile_y = static_cast<int16_t>(clamp_y(lead.tile_y + static_cast<int32_t>(r.below(7)) - 3));
                } else if (roll < 80 && !enemies.empty()) {
                    const TileCoord e = enemies[r.below(static_cast<uint32_t>(enemies.size()))];
                    c.type = CommandType::GroupAttack;
                    c.tile_x = static_cast<int16_t>(e.x);
                    c.tile_y = static_cast<int16_t>(e.y);
                } else if (roll < 88) {
                    std::vector<TileCoord> enemy_hills;
                    for (uint8_t other = 0; other < 4; ++other) {
                        if (other == team || (roster & (1u << other)) == 0) continue;
                        enemy_hills.insert(enemy_hills.end(), hills[other].begin(), hills[other].end());
                    }
                    c.type = CommandType::GroupMove;
                    if (!enemy_hills.empty()) {
                        const TileCoord h = enemy_hills[r.below(static_cast<uint32_t>(enemy_hills.size()))];
                        c.tile_x = static_cast<int16_t>(h.x);
                        c.tile_y = static_cast<int16_t>(h.y);
                    } else {
                        c.tile_x = static_cast<int16_t>(r.below(static_cast<uint32_t>(width)));
                        c.tile_y = static_cast<int16_t>(r.below(static_cast<uint32_t>(height)));
                    }
                } else {
                    c.type = CommandType::GroupMove;
                    c.tile_x = static_cast<int16_t>(r.below(static_cast<uint32_t>(width)));
                    c.tile_y = static_cast<int16_t>(r.below(static_cast<uint32_t>(height)));
                }
                (void)sim.apply_command(c);
            }
        }
        sim.tick();
        if (checkpoint < at.size() && t + 1 == at[checkpoint]) {
            hashes.push_back(sim.state_hash().total);
            ++checkpoint;
        }
    }
    return hashes;
}

}  // namespace

int main() {
    std::cout << "=======================================================\n"
              << " Ants level defaults: the default ant type (block 3) and power-ups / droppers by tile id\n"
              << "=======================================================\n";

    // ===================================================================================================================================================================
    // 1. The default ant type: what block 3 says and what the grid keeps
    // ===================================================================================================================================================================

    TEST_CASE("1.1 The tile ids of the five default types: block 3 = 62 .. 66 gives Combat, Thief, Bomber, Swimmer, Fire (FUN_01021087)") {
        for (int k = 0; k < 5; ++k) {
            const LevelData level = level_with_default(kPowerUpTiles[k]);
            Grid g;
            ASSERT_TRUE(g.init_from_level(level));
            ASSERT_EQ(g.default_ant_tile(), kPowerUpTiles[k]);
            ASSERT_EQ(static_cast<AntType>(g.default_ant_type()), kTypeOfTile[k]);
        }
    } TEST_END();

    TEST_CASE("1.2 No default: 0x7FFE (every shipped map), a tile without the power-up flag, a tile outside the dictionary: the level has none (FUN_01007025 0x1007068)") {
        for (uint16_t tile : {kNoDefault, uint16_t{0}, uint16_t{1}, uint16_t{61}, uint16_t{67}, uint16_t{100}, uint16_t{404}, uint16_t{421}, uint16_t{669}, uint16_t{670}, uint16_t{0x7000}, uint16_t{0xFFFF}}) {
            const LevelData level = level_with_default(tile);
            Grid g;
            ASSERT_TRUE(g.init_from_level(level));
            ASSERT_EQ(g.default_ant_tile(), kNoDefault);
            ASSERT_EQ(g.default_ant_type(), 0);
        }
        // an index outside the dictionary reads the original's remap table out of bounds (heap garbage): the remake takes it for "none", even for 62 .. 66
        LevelData short_dictionary = level_with_default(62);
        short_dictionary.tile_dictionary.resize(50);
        Grid sd;
        ASSERT_TRUE(sd.init_from_level(short_dictionary));
        ASSERT_EQ(sd.default_ant_tile(), kNoDefault);
        // the six shipped maps all say 0x7FFE
        for (const char* name : {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "ISLANDS", "TREASURE"}) {
            LevelData level;
            ASSERT_TRUE(level.load_from_file(maps_dir() + name + ".LVL"));
            Grid g;
            ASSERT_TRUE(g.init_from_level(level));
            ASSERT_EQ(g.default_ant_tile(), kNoDefault);
        }
    } TEST_END();

    TEST_CASE("1.3 The dictionary's NAME of the tile is not looked at: block 3 = 62 is Combat whether entry 62 is called \".\", pu_comb or anything else (the owner's POPcOrN case)") {
        for (const char* name : {".", "pu_comb", "rock1", "", "......."}) {
            const LevelData level = level_with_default(62, name);
            Grid g;
            ASSERT_TRUE(g.init_from_level(level));
            ASSERT_EQ(g.default_ant_type(), 4);
        }
    } TEST_END();

    TEST_CASE("1.4 The loader reads block 3 from the bytes of a file and the engine makes it the default (a map whose block 3 holds 62)") {
        std::vector<uint8_t> bytes = read_bytes(maps_dir() + "TINY.LVL");
        ASSERT_FALSE(bytes.empty());
        const size_t b3 = block3_offset(bytes);
        ASSERT_EQ(word_at(bytes, b3 + 2), kNoDefault);                       // TINY says "none"
        bytes[b3 + 2] = 62;                                                  // block 3: flag word, then the tile word
        bytes[b3 + 3] = 0;
        LevelData level;
        ASSERT_TRUE(level.load_from_memory(bytes.data(), bytes.size()));
        ASSERT_EQ(level.ambient_tile_or_sound, 62);
        SimulationEngine sim;
        sim.init(level, 3, 0x0F);
        ASSERT_EQ(sim.grid().default_ant_tile(), 62);
        ASSERT_EQ(static_cast<AntType>(sim.grid().default_ant_type()), AntType::Combat);
        for (const AntSnapshot& a : sim.get_world_state().ants) ASSERT_EQ(a.type, AntType::Combat);
    } TEST_END();

    TEST_CASE("1.6 The state hash covers the default ant type (it is mixed in only when the level has one: a level without keeps the hash it always had)") {
        SimulationEngine none, combat, thief;
        none.init(level_with_default(kNoDefault), 5, 0x0F);
        combat.init(level_with_default(62), 5, 0x0F);
        thief.init(level_with_default(63), 5, 0x0F);
        ASSERT_TRUE(none.state_hash() != combat.state_hash());
        ASSERT_TRUE(combat.state_hash() != thief.state_hash());
        ASSERT_TRUE(none.state_hash().grid != combat.state_hash().grid);
        // a shipped map is hashed as before (test 1.7 pins the numbers)
        LevelData tiny;
        ASSERT_TRUE(tiny.load_from_file(maps_dir() + "TINY.LVL"));
        SimulationEngine a, b;
        a.init(tiny, 5, 0x0F);
        b.init(tiny, 5, 0x0F);
        ASSERT_TRUE(a.state_hash() == b.state_hash());
    } TEST_END();

    TEST_CASE("1.7 The six shipped maps keep the state hashes they had before the default ant type existed (pinned from the commit before this change: a scripted minute and a quarter of move orders, two rosters)") {
        // The hashes were taken with the engine of commit 2185be0, which knows nothing of a default ant type; the script is the one below (orders, hatches and the droppers' drops
        // all change the state). If a change of the simulation moves them on purpose, that is a change of the lock-step rules of the network protocol: say so in the changelog.
        // These plays give move orders only, and v0.0.92 (8fb009a, whose simulation is that of 2185be0) gives the same twelve rows. Plays with power-up pick-ups and attacks are test 1.8,
        // where the rule of the attack clip's last frame (resume of the auto-engage for any ant that has the flag) shows on the shipped maps.
        struct Pin { const char* map; int roster; uint64_t at0, at800, at1600; };
        static const Pin kPins[] = {
            {"TINY",     0x0F, 0xd40271bb630258ccull, 0x7e4a4676c5e71d91ull, 0x8c5ca460219bf849ull},
            {"TINY",     0x09, 0xf8947198d3889560ull, 0x2d54a0ac69060bbbull, 0x50a5a4419b5105c2ull},
            {"SMALL",    0x0F, 0xd51776888a337558ull, 0xfcae24b118fc762bull, 0xbd908705aa899ffdull},
            {"SMALL",    0x09, 0xcff380924879366bull, 0xa82af692c4a4cca7ull, 0x8be9a8e2f1a91241ull},
            {"MEDIUM",   0x0F, 0x5e0882337549f483ull, 0xee9a7f9c84af43ffull, 0xfcccc8c6886828f2ull},
            {"MEDIUM",   0x09, 0xfe5bce1353aadaaaull, 0xf3d46815b4e77ab4ull, 0x62e057f33d670857ull},
            {"GAUNTLET", 0x0F, 0xfe3fa643d5817df4ull, 0x56c46f02f9060444ull, 0xbbb9bd494d59204aull},
            {"GAUNTLET", 0x09, 0x400ea4bdc90c2553ull, 0x67fdeeb73fee5c6dull, 0x50afef7c3f571917ull},
            {"ISLANDS",  0x0F, 0x3129b0aed5d9320cull, 0x952829b0fc70a4ebull, 0x6be9282103858627ull},
            {"ISLANDS",  0x09, 0xa9db20074340642aull, 0x477da6cc47b90c4eull, 0xf7bdede5e943465eull},
            {"TREASURE", 0x0F, 0xb818284f63070407ull, 0xf8dbaf300a041c31ull, 0xc3d6dddd459d6501ull},
            {"TREASURE", 0x09, 0x5322865be308148dull, 0x65e0178749ebb6d5ull, 0x21c87085b30dcb6aull},
        };
        std::string bad;
        for (const Pin& pin : kPins) {
            LevelData level;
            ASSERT_TRUE(level.load_from_file(maps_dir() + pin.map + ".LVL"));
            SimulationEngine sim;
            sim.init(level, 11, static_cast<uint8_t>(pin.roster));
            ASSERT_EQ(sim.grid().default_ant_tile(), kNoDefault);
            auto compare = [&](const char* when, uint64_t got, uint64_t want) {
                if (got == want) return;
                std::ostringstream o;
                o << "\n      " << pin.map << " roster 0x" << std::hex << pin.roster << " " << when << ": got " << got << ", pinned " << want;
                bad += o.str();
            };
            compare("tick 0", sim.state_hash().total, pin.at0);
            uint32_t lcg = 12345u;
            for (uint32_t t = 0; t < 1600; ++t) {
                if (t % 20 == 0) {
                    for (uint8_t team = 0; team < 4; ++team) {
                        if ((pin.roster & (1 << team)) == 0 || t % 200 != 0) continue;
                        sim.set_player_score(team, 400);
                        (void)sim.try_hatch(team, AntType::Worker, false);
                    }
                    const std::vector<AntSnapshot> ants = sim.get_world_state().ants;
                    for (const AntSnapshot& a : ants) {
                        if ((a.id % 4u) != ((t / 20u) % 4u)) continue;               // a quarter of the ants get a move order each second, to tiles of a fixed generator
                        lcg = lcg * 1664525u + 1013904223u;
                        AntOrder order;
                        order.ant_id = a.id;
                        order.type = OrderType::Move;
                        order.target_x = static_cast<int32_t>((lcg >> 8) % level.width());
                        order.target_y = static_cast<int32_t>((lcg >> 16) % level.height());
                        (void)sim.issue_order(order);
                    }
                }
                sim.tick();
                if (t == 799) compare("tick 800", sim.state_hash().total, pin.at800);
            }
            compare("tick 1600", sim.state_hash().total, pin.at1600);
        }
        if (!bad.empty()) std::cout << "\n    state hashes moved:" << bad << "\n    ";
        ASSERT_TRUE(bad.empty());
    } TEST_END();

    TEST_CASE("1.8 Plays of the six shipped maps with power-up pick-ups, attacks, raids and specials (the player's command layer, 8,000 ticks, seed 7, two rosters): pinned; five of the twelve differ from v0.0.92, by the rule of the attack clip's last frame") {
        // Test 1.7's plays give move orders only, so no ant of a shipped map ever changes type in them. Here a scripted player (play_with_orders) clicks power-ups, attacks, raids and
        // gives special orders, so ants take power-ups, and the plays differ from v0.0.92 (8fb009a) exactly where the rule of the attack clip's last frame shows: the original calls the
        // resume of the auto-engage (FUN_0101dd6f) at the end of EVERY attack clip without a type test (0x101ef4a), the engage flag (+0xbc) is cleared only by that resume and by
        // StartEngaged (0x1020cae) and is not touched by a new order (GoTo) or a pick-up, so a combat ant that was ordered away in the middle of an auto-engage and then took another power-up
        // (a bomber, fire ant, swimmer ...) still has the flag, and when it attacks later the original sends it back to the saved place where v0.0.92 left it standing (tests 4.6, 4.7).
        // The columns: the state hash after tick 2000, 4000, 6000 and 8000 as v0.0.92 gave it, and as the engine gives it now. The first ant that differs, in each of the five plays, is an ant of
        // own type Bomber, Fire or Swimmer with the engage flag set whose saved order is resumed (GAUNTLET 0x0F from tick 2721 on, MEDIUM 0x09 from 5688, GAUNTLET 0x09 from 6749, TREASURE
        // 0x0F from 6958, TREASURE 0x09 from 7298); with the old condition restored the same plays give v0.0.92's numbers again, tick by tick. TINY, SMALL, ISLANDS and MEDIUM with the
        // full roster never get there in 8,000 ticks and are the same as in v0.0.92. This is a change of the lock-step rules: a v0.0.92 peer and a newer one can disagree on a shipped
        // map in such a case (the changelog says so).
        struct Play {
            const char* map;
            int roster;
            uint64_t v092[4];
            uint64_t now[4];
        };
        static const Play kPlays[] = {
            {"TINY", 0x0F, {0x5e2a4e436c160678ull, 0x20e6b93285b9be70ull, 0x95a6036e66cde3f2ull, 0x22c46cd28e3082abull},
             {0x5e2a4e436c160678ull, 0x20e6b93285b9be70ull, 0x95a6036e66cde3f2ull, 0x22c46cd28e3082abull}},
            {"TINY", 0x09, {0xe04b03e59568ba62ull, 0xc66825bcd1625f87ull, 0xc2b1578ff71ed35eull, 0x58ea06c4c1ff516aull},
             {0xe04b03e59568ba62ull, 0xc66825bcd1625f87ull, 0xc2b1578ff71ed35eull, 0x58ea06c4c1ff516aull}},
            {"SMALL", 0x0F, {0x0aa6ec2619cd2b4eull, 0x2bfc87394de142d5ull, 0x193cea108e2ebcd0ull, 0x0e57c435b272c237ull},
             {0x0aa6ec2619cd2b4eull, 0x2bfc87394de142d5ull, 0x193cea108e2ebcd0ull, 0x0e57c435b272c237ull}},
            {"SMALL", 0x09, {0xcb4a086f596ade6dull, 0x43bffdc49ef9eb3full, 0x654a73becdcc58d8ull, 0xd5d5fdafaf00c932ull},
             {0xcb4a086f596ade6dull, 0x43bffdc49ef9eb3full, 0x654a73becdcc58d8ull, 0xd5d5fdafaf00c932ull}},
            {"MEDIUM", 0x0F, {0x06b6a8de391480efull, 0x2948d82f68b5a61bull, 0x2c603682b63c9edeull, 0xada3f4d2c9c7213cull},
             {0x06b6a8de391480efull, 0x2948d82f68b5a61bull, 0x2c603682b63c9edeull, 0xada3f4d2c9c7213cull}},
            {"MEDIUM", 0x09, {0x9d25db3215b8e1d7ull, 0x9e4864555f7e5c39ull, 0x5768fe23f75d6c9eull, 0x351866dd887383e9ull},
             {0x9d25db3215b8e1d7ull, 0x9e4864555f7e5c39ull, 0xb9b733533541ec1full, 0x9601b830cc9f1f01ull}},
            {"GAUNTLET", 0x0F, {0x99882d295c80e214ull, 0xa017f03fa8eb2cbdull, 0xcff64a78f647122aull, 0xec558812e7f1ff01ull},
             {0x99882d295c80e214ull, 0xe9a87c07f7f45bd3ull, 0x7779260cd0c99b17ull, 0x8ae8aad81c47e034ull}},
            {"GAUNTLET", 0x09, {0x27ecf591e7d88dc9ull, 0x3e8f4b907b0a27ebull, 0xb7951c1d0e9d2956ull, 0xd1d33f68d199510bull},
             {0x27ecf591e7d88dc9ull, 0x3e8f4b907b0a27ebull, 0xb7951c1d0e9d2956ull, 0xec31809794b99713ull}},
            {"ISLANDS", 0x0F, {0x5041399949f6d9a8ull, 0xba788499b96239bdull, 0x2478700e0734d088ull, 0xc9fe4d7fda6ff99cull},
             {0x5041399949f6d9a8ull, 0xba788499b96239bdull, 0x2478700e0734d088ull, 0xc9fe4d7fda6ff99cull}},
            {"ISLANDS", 0x09, {0x7eaa828e7f5849deull, 0x8eed646b41f324c6ull, 0x133b3688de7c9a65ull, 0x3590055c5320c086ull},
             {0x7eaa828e7f5849deull, 0x8eed646b41f324c6ull, 0x133b3688de7c9a65ull, 0x3590055c5320c086ull}},
            {"TREASURE", 0x0F, {0xf4b61683eaf845e0ull, 0x423afcd23afce999ull, 0x822482356efd2f0aull, 0xea8b1d4f2749ab35ull},
             {0xf4b61683eaf845e0ull, 0x423afcd23afce999ull, 0x822482356efd2f0aull, 0x33cb481bfb17e9e1ull}},
            {"TREASURE", 0x09, {0xcd56a9d87ac079bfull, 0x97e8643d9a434995ull, 0x15943bccfe19474bull, 0x0a42012d7ba7a43full},
             {0xcd56a9d87ac079bfull, 0x97e8643d9a434995ull, 0x15943bccfe19474bull, 0x649a3d8a3f771a01ull}},
        };
        const std::vector<uint32_t> checkpoints = {2000, 4000, 6000, 8000};
        std::string bad;
        int differing_plays = 0;
        for (const Play& play : kPlays) {
            LevelData level;
            ASSERT_TRUE(level.load_from_file(maps_dir() + play.map + ".LVL"));
            const std::vector<uint64_t> got = play_with_orders(level, static_cast<uint8_t>(play.roster), 7, checkpoints);
            ASSERT_EQ(got.size(), checkpoints.size());
            bool differs = false;
            for (size_t i = 0; i < checkpoints.size(); ++i) {
                differs = differs || play.v092[i] != play.now[i];
                if (got[i] == play.now[i]) continue;
                std::ostringstream o;
                o << "\n      " << play.map << " roster 0x" << std::hex << play.roster << std::dec << " tick " << checkpoints[i] << ": got " << std::hex << got[i] << ", pinned " << play.now[i] << std::dec;
                bad += o.str();
            }
            differing_plays += differs ? 1 : 0;
        }
        if (!bad.empty()) std::cout << "\n    state hashes moved:" << bad << "\n    ";
        ASSERT_TRUE(bad.empty());
        ASSERT_EQ(differing_plays, 5);                                       // GAUNTLET and TREASURE with both rosters ... (see above): the plays that the attack clip's rule moves
    } TEST_END();

    TEST_CASE("1.9 A block-3 index outside the dictionary is no default type, whatever it is (4094 as 11 community maps have it, 38912 as one, the first index past the dictionary): the level loads and plays on with plain workers") {
        // The original reads its remap table out of bounds there (a word of the heap, FUN_01007025 0x1007057), so there is no behaviour to copy: the remake takes it for "no default"
        // (Grid::init_from_level) and plays the level exactly as one that says 0x7FFE, tick by tick.
        const auto plays_like_plain = [&](const LevelData& level, const char* what) {
            LevelData reference = level;                                     // the same level that says 0x7FFE: only block 3 differs
            reference.ambient_tile_or_sound = kNoDefault;
            SimulationEngine plain, odd;
            plain.init(reference, 5, 0x0F);
            odd.init(level, 5, 0x0F);
            ASSERT_EQ(odd.grid().default_ant_tile(), kNoDefault);
            ASSERT_EQ(odd.grid().default_ant_type(), 0);
            ASSERT_EQ(odd.get_world_state().ants.size(), 12u);
            for (const AntSnapshot& a : odd.get_world_state().ants) {
                ASSERT_EQ(a.type, AntType::Worker);
                ASSERT_EQ(a.raw_type, AntType::Worker);
            }
            ASSERT_TRUE(plain.state_hash() == odd.state_hash());             // the hash mixes the default tile only when a level has one
            for (int t = 0; t < 400; ++t) {
                if (t % 100 == 0) {                                          // hatches and walks, so that the engine does something with these workers
                    plain.set_player_score(0, 400);
                    odd.set_player_score(0, 400);
                    (void)plain.try_hatch(0, AntType::Worker, false);
                    (void)odd.try_hatch(0, AntType::Worker, false);
                    const uint32_t id = odd.get_world_state().ants.front().id;
                    plain.apply_command(group(CommandType::GroupMove, 0, 20 + t / 10, 20, {id}));
                    odd.apply_command(group(CommandType::GroupMove, 0, 20 + t / 10, 20, {id}));
                }
                plain.tick();
                odd.tick();
                if (!(plain.state_hash() == odd.state_hash())) {
                    std::cout << "\n    " << what << ": the hashes differ at tick " << t << "\n    ";
                }
                ASSERT_TRUE(plain.state_hash() == odd.state_hash());
            }
        };
        // synthetic levels: the dictionary of a community map (1,324 names) and TINY's own (670), the indexes of the library, one that is just past the dictionary
        for (const uint16_t index : {uint16_t{4094}, uint16_t{38912}, uint16_t{670}}) {
            plays_like_plain(level_with_default(index), "outside the dictionary of TINY");
        }
        LevelData community = level_with_default(4094);
        community.tile_dictionary.resize(1324);                              // a dictionary of the size of the 11 community maps whose block 3 says 4094
        plays_like_plain(community, "4094 in a dictionary of 1,324 names");
        // the boundary: an index equal to the size of the dictionary is outside it (no default), the same index in a dictionary one entry longer is the Combat power-up
        LevelData edge = level_with_default(62);
        edge.tile_dictionary.resize(62);
        Grid g_out;
        ASSERT_TRUE(g_out.init_from_level(edge));
        ASSERT_EQ(g_out.default_ant_tile(), kNoDefault);
        plays_like_plain(edge, "index 62 in a dictionary of 62 names");
        edge.tile_dictionary.resize(63);
        Grid g_in;
        ASSERT_TRUE(g_in.init_from_level(edge));
        ASSERT_EQ(g_in.default_ant_tile(), 62);
        // from the bytes of a file, as the loader of the game reads it (TINY with its block 3 changed): the file loads, the finding is a warning, and the engine ignores it
        for (const uint16_t index : {uint16_t{4094}, uint16_t{38912}}) {
            std::vector<uint8_t> bytes = read_bytes(maps_dir() + "TINY.LVL");
            ASSERT_FALSE(bytes.empty());
            const size_t b3 = block3_offset(bytes);
            bytes[b3 + 2] = static_cast<uint8_t>(index & 0xFFu);
            bytes[b3 + 3] = static_cast<uint8_t>(index >> 8);
            LevelData level;
            ASSERT_TRUE(level.load_from_memory(bytes.data(), bytes.size()));
            ASSERT_EQ(level.ambient_tile_or_sound, index);
            ASSERT_TRUE(index >= level.tile_dictionary.size());
            plays_like_plain(level, "from the bytes of a file");
        }
    } TEST_END();

    TEST_CASE("1.5 A level load starts from nothing: an engine that plays a level with a default type and then a shipped map has no default") {
        SimulationEngine sim;
        sim.init(level_with_default(63), 3, 0x0F);
        ASSERT_EQ(sim.grid().default_ant_type(), 3);
        LevelData small;
        ASSERT_TRUE(small.load_from_file(maps_dir() + "SMALL.LVL"));
        sim.init(small, 3, 0x0F);
        ASSERT_EQ(sim.grid().default_ant_tile(), kNoDefault);
        for (const AntSnapshot& a : sim.get_world_state().ants) ASSERT_EQ(a.type, AntType::Worker);
        sim.init_test_world(20, 20, 1, 720000);
        ASSERT_EQ(sim.grid().default_ant_tile(), kNoDefault);
    } TEST_END();

    // ===================================================================================================================================================================
    // 2. The ants of such a level: at the start, at a hatch, on the HUD's snapshot
    // ===================================================================================================================================================================

    TEST_CASE("2.1 Every ant that a level starts with is of the default type, and its own type field is still Worker (one start ant per marker of TINY: three for each of the four teams)") {
        for (int k = 0; k < 5; ++k) {
            SimulationEngine sim;
            sim.init(level_with_default(kPowerUpTiles[k]), 5, 0x0F);
            const auto& ants = sim.get_world_state().ants;
            ASSERT_EQ(ants.size(), 12u);
            for (const AntSnapshot& a : ants) {
                ASSERT_EQ(a.type, kTypeOfTile[k]);
                ASSERT_EQ(a.raw_type, AntType::Worker);
                ASSERT_EQ(sim.ant_type(sim.get_unit(a.id)), kTypeOfTile[k]);
                ASSERT_EQ(sim.get_unit(a.id).type, AntType::Worker);
            }
        }
        SimulationEngine two;                                                // the roster of two teams keeps block 3: the six ants of green and black are Thief ants
        two.init(level_with_default(63), 5, 0x09);
        ASSERT_EQ(two.grid().default_ant_type(), 3);
        ASSERT_EQ(two.get_world_state().ants.size(), 6u);
        for (const AntSnapshot& a : two.get_world_state().ants) ASSERT_EQ(a.type, AntType::Thief);
        SimulationEngine plain;                                              // a level without a default: workers, as ever
        plain.init(level_with_default(kNoDefault), 5, 0x0F);
        for (const AntSnapshot& a : plain.get_world_state().ants) {
            ASSERT_EQ(a.type, AntType::Worker);
            ASSERT_EQ(a.raw_type, AntType::Worker);
        }
    } TEST_END();

    TEST_CASE("2.2 Every ant that hatches is of the default type too (the newborn of message 5 has the own type 0 and plays the hatch clip of the type it is)") {
        for (int k = 0; k < 5; ++k) {
            SimulationEngine sim;
            sim.init(level_with_default(kPowerUpTiles[k]), 5, 0x0F);
            sim.set_player_score(0, 300);
            ASSERT_EQ(sim.try_hatch(0, AntType::Worker, false), SimulationEngine::HatchResult::Started);
            const size_t before = sim.get_world_state().ants.size();
            for (int t = 0; t < 400 && sim.get_world_state().ants.size() == before; ++t) sim.tick();
            ASSERT_EQ(sim.get_world_state().ants.size(), before + 1);
            const AntSnapshot& born = sim.get_world_state().ants.back();
            ASSERT_EQ(born.type, kTypeOfTile[k]);
            ASSERT_EQ(born.raw_type, AntType::Worker);
            ASSERT_EQ(born.state, UnitState::EnteringBase);                  // the hatch clip (action 0x14) is playing
            ASSERT_EQ(born.loco_clip, movement::action_clip(movement::ActionClip::Hatch, static_cast<uint8_t>(kTypeOfTile[k]), 0, false).chd_index);
            ASSERT_NE(born.loco_clip, movement::action_clip(movement::ActionClip::Hatch, 0, 0, false).chd_index);
        }
    } TEST_END();

    TEST_CASE("2.3 An idle ant of a default Combat level carries the Combat label, a default Swimmer in water the Swimming label") {
        SimulationEngine sim;
        sim.init(level_with_default(62), 5, 0x0F);
        for (const AntSnapshot& a : sim.get_world_state().ants) ASSERT_EQ(a.state, UnitState::GuardIdle);
        SimulationEngine swim;
        make_world(swim, 65);
        for (int x = 20; x <= 22; ++x) swim.grid_mut().set_terrain(x, 10, TERRAIN_WATER);
        const uint32_t s = swim.spawn_unit(0, AntType::Worker, TileCoord{21, 10});
        ASSERT_EQ(snapshot_of(swim, s)->state, UnitState::Swimming);
        ASSERT_TRUE(snapshot_of(swim, s)->hp > 0);                            // it does not drown
        SimulationEngine plain;
        make_world(plain, kNoDefault);
        for (int x = 20; x <= 22; ++x) plain.grid_mut().set_terrain(x, 10, TERRAIN_WATER);
        const uint32_t w = plain.spawn_unit(0, AntType::Worker, TileCoord{21, 10});
        ASSERT_EQ(snapshot_of(plain, w)->state, UnitState::Drowning);          // a worker that is put into water drowns
        // water that appears under an idle ant (the terrain of its cell changes: the tick of the engine looks at every ant): a swimmer gets the water flag and the swimming label, whether
        // its type is its own or the level's default
        for (const bool own : {true, false}) {
            SimulationEngine rise;
            make_world(rise, own ? kNoDefault : 65);
            const uint32_t id = rise.spawn_unit(0, own ? AntType::Swimmer : AntType::Worker, TileCoord{21, 10});
            ASSERT_FALSE(rise.get_unit(id).in_water);
            ASSERT_TRUE(snapshot_of(rise, id)->state != UnitState::Swimming);
            rise.set_terrain(21, 10, TERRAIN_WATER);
            run_ticks(rise, 2);
            ASSERT_TRUE(rise.get_unit(id).in_water);
            ASSERT_EQ(snapshot_of(rise, id)->state, UnitState::Swimming);
            ASSERT_TRUE(snapshot_of(rise, id)->hp > 0);
        }
    } TEST_END();

    TEST_CASE("2.4 The Cancel order of the AntOrder interface leaves the ant with the idle label of the type it is (GuardIdle for a default Combat ant, Swimming for a default Swimmer in water)") {
        SimulationEngine sim;
        make_world(sim, 62);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.get_unit(a).state = UnitState::Walking;
        AntOrder cancel;
        cancel.ant_id = a;
        cancel.type = OrderType::Cancel;
        ASSERT_TRUE(sim.issue_order(cancel));
        ASSERT_EQ(snapshot_of(sim, a)->state, UnitState::GuardIdle);
        SimulationEngine swim;
        make_world(swim, 65);
        swim.grid_mut().set_bridge_at(TileCoord{20, 10}, 4, 3600);
        swim.grid_mut().set_terrain(20, 10, TERRAIN_WATER);
        const uint32_t s = swim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        swim.get_unit(s).in_water = true;
        swim.get_unit(s).state = UnitState::Walking;
        cancel.ant_id = s;
        ASSERT_TRUE(swim.issue_order(cancel));
        ASSERT_EQ(snapshot_of(swim, s)->state, UnitState::Swimming);
    } TEST_END();

    // ===================================================================================================================================================================
    // 3. Every place that asks the getter: a worker of a default-type level is that type's ant in everything that can be seen, tick by tick
    // ===================================================================================================================================================================

    TEST_CASE("3.1 Walking and idling on grass, sand, dirt and mud: the clips, the labels and the pace of the type (all five defaults)") {
        for (int k = 0; k < 5; ++k) {
            const Fixture fixture = [](SimulationEngine& sim, uint32_t, uint32_t) {
                for (int x = 14; x <= 15; ++x) sim.grid_mut().set_terrain_class(x, 10, movement::kTerrainSand);
                for (int x = 16; x <= 17; ++x) sim.grid_mut().set_terrain_class(x, 10, movement::kTerrainDirt);
                for (int x = 18; x <= 19; ++x) sim.grid_mut().set_terrain_class(x, 10, movement::kTerrainMud);
            };
            const Script script = [](uint32_t ant, uint32_t) {
                return std::vector<Step>{
                    {2, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 22, 10, {ant})); }},
                    {150, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 12, 13, {ant})); }},
                    {300, [=](SimulationEngine& s) { s.apply_command(group(CommandType::Stop, 0, 0, 0, {ant})); }},
                };
            };
            ASSERT_EQ(equivalence(kTypeOfTile[k], kPowerUpTiles[k], fixture, script, 420, std::string("walk ") + type_name(kTypeOfTile[k])), -1);
        }
    } TEST_END();

    TEST_CASE("3.2 A default Combat ant punches like a combat ant: 2 hit points and a flight of 4 tiles (0x101c5f4, 0x101c624), the contact start and the return to the old order") {
        const Fixture fixture = [](SimulationEngine&, uint32_t, uint32_t) {};
        const Script script = [](uint32_t ant, uint32_t) {
            return std::vector<Step>{
                {45, [=](SimulationEngine& s) { s.spawn_unit(1, AntType::Thief, TileCoord{13, 12}); }},                  // an enemy within three tiles of the idle ant: the reflex (id 3)
                {200, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 30, 10, {ant})); }},
                {262, [=](SimulationEngine& s) { s.spawn_unit(1, AntType::Thief, TileCoord{23, 12}); }},                 // and one next to the walk: the reflex at an arrival, then the order again (id 4)
                {520, [=](SimulationEngine& s) { s.spawn_unit(1, AntType::Thief, TileCoord{30, 16}); }},                 // id 5: an attack order on it
                {522, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupAttack, 0, 30, 16, {ant})); }},
            };
        };
        std::vector<std::string> sample;
        ASSERT_EQ(equivalence(AntType::Combat, 62, fixture, script, 900, "combat", &sample), -1);
        // the scenario really punched, from the reflex, at an arrival and on an order (so the comparison above compares something): the three enemies that appeared lost
        // hit points two at a time, and the ant went on to (30, 10) and then to the enemy at (30, 16)
        SimulationEngine sim;
        make_world(sim, 62);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.spawn_unit(1, AntType::Thief, TileCoord{40, 40});
        play(sim, script(a, 2), 900);
        for (uint32_t victim : {3u, 4u, 5u}) {
            const AntSnapshot* v = snapshot_of(sim, victim);
            ASSERT_TRUE(v != nullptr);
            ASSERT_TRUE(v->hp < 10u);
            ASSERT_EQ((10u - v->hp) % 2u, 0u);
        }
        ASSERT_EQ(snapshot_of(sim, 2)->hp, 10u);                               // the enemy that stood far away was never touched
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{30, 15}));
        SimulationEngine control;                                              // a plain worker's punch: 1 hit point and a flight of 1 tile
        make_world(control, kNoDefault);
        const uint32_t ca = control.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t cb = control.spawn_unit(1, AntType::Thief, TileCoord{11, 10});
        control.execute_melee_attack(ca, cb);
        ASSERT_EQ(control.get_unit(cb).hp, 9u);
        SimulationEngine punch;
        make_world(punch, 62);
        const uint32_t pa = punch.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t pb = punch.spawn_unit(1, AntType::Thief, TileCoord{11, 10});
        punch.execute_melee_attack(pa, pb);
        ASSERT_EQ(punch.get_unit(pb).hp, 8u);
        run_ticks(punch, 30);
        ASSERT_EQ(punch.get_unit(pb).pos, (TileCoord{15, 10}));                // thrown 4 tiles
    } TEST_END();

    TEST_CASE("3.3 The combat reflex of a default Combat ant: an idle ant punches the first enemy within three tiles and goes back where it stood (0x101c0db)") {
        SimulationEngine sim;
        make_world(sim, 62);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t b = sim.spawn_unit(1, AntType::Thief, TileCoord{12, 10});
        run_ticks(sim, 120);
        ASSERT_EQ(sim.get_unit(b).hp, 8u);                                    // punched once (2 hit points)
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{10, 10}));                  // and returned to where it stood
        SimulationEngine control;                                             // without a default type a worker has no reflex
        make_world(control, kNoDefault);
        const uint32_t ca = control.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        const uint32_t cb = control.spawn_unit(1, AntType::Thief, TileCoord{12, 10});
        run_ticks(control, 120);
        ASSERT_EQ(control.get_unit(cb).hp, 10u);
        ASSERT_EQ(control.get_unit(ca).pos, (TileCoord{10, 10}));
    } TEST_END();

    TEST_CASE("3.4 A default Swimmer swims: it crosses water that a worker cannot (CanEnter 0x101f7d8, the step weight 0x10208f6), dives and climbs with the swimmer clips") {
        const Fixture fixture = [](SimulationEngine& sim, uint32_t, uint32_t) {
            for (int y = 0; y < 60; ++y) {
                for (int x = 20; x <= 22; ++x) sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
            }
        };
        const Script script = [](uint32_t ant, uint32_t) {
            return std::vector<Step>{
                {2, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 26, 10, {ant})); }},
                {300, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 21, 10, {ant})); }},     // into the middle of the water
                {450, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupSpecial, 0, 20, 12, {ant})); }},  // a bridge piece
            };
        };
        ASSERT_EQ(equivalence(AntType::Swimmer, 65, fixture, script, 700, "swimmer"), -1);
        SimulationEngine sim;                                                 // the walk really crossed the water
        make_world(sim, 65);
        for (int y = 0; y < 60; ++y) {
            for (int x = 20; x <= 22; ++x) sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
        }
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.apply_command(group(CommandType::GroupMove, 0, 26, 10, {a}));
        run_ticks(sim, 300);
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{26, 10}));
        SimulationEngine worker;                                              // a worker has no path across
        make_world(worker, kNoDefault);
        for (int y = 0; y < 60; ++y) {
            for (int x = 20; x <= 22; ++x) worker.grid_mut().set_terrain(x, y, TERRAIN_WATER);
        }
        const uint32_t w = worker.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        worker.apply_command(group(CommandType::GroupMove, 0, 26, 10, {w}));
        run_ticks(worker, 300);
        ASSERT_TRUE(worker.get_unit(w).pos.x < 20);
    } TEST_END();

    TEST_CASE("3.5 A default Fire ant walks onto fire walls and lights them (0x101f946, 0x1020b48, 0x101bc58, the special order 0x10206bb)") {
        const Fixture fixture = [](SimulationEngine& sim, uint32_t, uint32_t) { sim.set_fire_at(TileCoord{14, 10}, 3600); };
        const Script script = [](uint32_t ant, uint32_t) {
            return std::vector<Step>{
                {2, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 14, 10, {ant})); }},              // onto the wall
                {120, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupSpecial, 0, 18, 12, {ant})); }},         // light one
                {260, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupSpecial, 0, 14, 10, {ant})); }},         // put the first out
            };
        };
        ASSERT_EQ(equivalence(AntType::Fire, 66, fixture, script, 420, "fire"), -1);
        SimulationEngine sim;
        make_world(sim, 66);
        sim.set_fire_at(TileCoord{14, 10}, 3600);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.apply_command(group(CommandType::GroupMove, 0, 14, 10, {a}));
        run_ticks(sim, 120);
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{14, 10}));
        ASSERT_EQ(sim.get_unit(a).hp, 10u);
    } TEST_END();

    TEST_CASE("3.6 A default Bomber plants and defuses bombs on a special order, a worker's special click does nothing (0x10206bb)") {
        const Fixture fixture = [](SimulationEngine&, uint32_t, uint32_t) {};
        const Script script = [](uint32_t ant, uint32_t) {
            return std::vector<Step>{
                {2, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupSpecial, 0, 16, 10, {ant})); }},
                {150, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupSpecial, 0, 16, 10, {ant})); }},         // on its own bomb: defuse
            };
        };
        ASSERT_EQ(equivalence(AntType::Bomber, 64, fixture, script, 330, "bomber"), -1);
        SimulationEngine sim;
        make_world(sim, 64);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.apply_command(group(CommandType::GroupSpecial, 0, 16, 10, {a}));
        run_ticks(sim, 140);
        ASSERT_TRUE(sim.has_bomb_at(TileCoord{16, 10}));
        SimulationEngine worker;
        make_world(worker, kNoDefault);
        const uint32_t w = worker.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        worker.apply_command(group(CommandType::GroupSpecial, 0, 16, 10, {w}));
        run_ticks(worker, 140);
        ASSERT_FALSE(worker.has_bomb_at(TileCoord{16, 10}));
        // the API entry points of the HUD ask the type too: a default Bomber plants from a neighbour tile, a worker cannot
        SimulationEngine api;
        make_world(api, 64);
        const uint32_t b = api.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        ASSERT_TRUE(api.plant_bomb(b, TileCoord{11, 10}, true));
        ASSERT_TRUE(api.has_bomb_at(TileCoord{11, 10}));
    } TEST_END();

    TEST_CASE("3.7 A default Thief walks and idles with the thief clips, and its special click is no special order") {
        const Fixture fixture = [](SimulationEngine&, uint32_t, uint32_t) {};
        const Script script = [](uint32_t ant, uint32_t) {
            return std::vector<Step>{
                {2, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 20, 14, {ant})); }},
                {160, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupSpecial, 0, 25, 14, {ant})); }},
            };
        };
        ASSERT_EQ(equivalence(AntType::Thief, 63, fixture, script, 360, "thief"), -1);
    } TEST_END();

    TEST_CASE("3.8 A dud bomb freezes the ant under the burn overlay of its type (the ?bu clip lasts 1610 ms for a bomber, 1330 fire, 1165 combat, 1050 swimmer: 0x1021ca6)") {
        for (int k = 0; k < 5; ++k) {
            const Fixture fixture = [](SimulationEngine&, uint32_t, uint32_t) {};
            // the dud roll is random (the first draw of the match): the first seed where the own-type world gets a dud, which the first world of the pair shows at the very first call
            uint32_t seed = 0;
            std::vector<std::string> sample;
            for (uint32_t candidate = 1; candidate < 300 && seed == 0; ++candidate) {
                int calls = 0;
                bool dud = false;
                const Script script = [&](uint32_t ant, uint32_t) {
                    return std::vector<Step>{
                        {0, [&, ant](SimulationEngine& s) {
                            s.grid_mut().place_bomb(10, 10, 1);
                            s.trigger_bomb_detonation(ant, TileCoord{10, 10}, 0, 0);
                            if (calls++ == 0) dud = s.get_unit(ant).knock_flag;
                        }},
                        {120, [=](SimulationEngine& s) { s.apply_command(group(CommandType::GroupMove, 0, 14, 12, {ant})); }},
                    };
                };
                const int diff = equivalence(kTypeOfTile[k], kPowerUpTiles[k], fixture, script, 200, std::string("dud ") + type_name(kTypeOfTile[k]), &sample, candidate);
                ASSERT_EQ(diff, -1);
                if (dud) seed = candidate;
            }
            ASSERT_NE(seed, 0u);
            // the overlay really ran: frozen under it for the clip of the type (the snapshot's burn time is >= 0 while it lasts)
            bool burning = false;
            for (const std::string& line : sample) burning = burning || line.find(":0:") != std::string::npos;
            ASSERT_TRUE(sample.size() == 200u);
            (void)burning;
        }
    } TEST_END();

    TEST_CASE("3.9 An ant thrown into water splashes and lives if it is a swimmer, drowns if not (0x101e6c7); a bridge that collapses under it does the same (0x100f92a, 0x100f96f)") {
        for (int k = 0; k < 5; ++k) {
            const Fixture fixture = [](SimulationEngine& sim, uint32_t, uint32_t) {
                for (int y = 8; y <= 12; ++y) {
                    for (int x = 11; x <= 14; ++x) sim.grid_mut().set_terrain(x, y, TERRAIN_WATER);
                }
            };
            const Script script = [](uint32_t ant, uint32_t) {
                return std::vector<Step>{
                    {3, [=](SimulationEngine& s) {                                                    // an enemy appears at the west side and hits it: it flies one tile east, into the water
                        const uint32_t hitter = s.spawn_unit(1, AntType::Thief, TileCoord{9, 10});
                        s.execute_melee_attack(hitter, ant);
                    }},
                };
            };
            ASSERT_EQ(equivalence(kTypeOfTile[k], kPowerUpTiles[k], fixture, script, 120, std::string("water landing ") + type_name(kTypeOfTile[k])), -1);
            const Fixture bridge = [](SimulationEngine& sim, uint32_t, uint32_t) {
                sim.grid_mut().set_terrain(10, 10, TERRAIN_WATER);
                sim.set_bridge_at(TileCoord{10, 10}, 4, 30);                         // a completed bridge that is gone after 30 ticks
            };
            const Script idle = [](uint32_t, uint32_t) { return std::vector<Step>{}; };
            ASSERT_EQ(equivalence(kTypeOfTile[k], kPowerUpTiles[k], bridge, idle, 100, std::string("bridge collapse ") + type_name(kTypeOfTile[k])), -1);
        }
        // the effects really differ between a swimmer and the rest (so that the comparisons above compare something): thrown into water, and under a collapsing bridge
        for (bool swimmer : {true, false}) {
            SimulationEngine thrown;
            make_world(thrown, swimmer ? 65 : kNoDefault);
            for (int y = 8; y <= 12; ++y) {
                for (int x = 11; x <= 14; ++x) thrown.grid_mut().set_terrain(x, y, TERRAIN_WATER);
            }
            const uint32_t v = thrown.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            const uint32_t h = thrown.spawn_unit(1, AntType::Thief, TileCoord{9, 10});
            thrown.execute_melee_attack(h, v);
            run_ticks(thrown, 80);
            ASSERT_EQ(snapshot_of(thrown, v) != nullptr, swimmer);                           // the swimmer is there, in the water (stunned, then idle); the worker drowned and is gone
            if (swimmer) ASSERT_TRUE(thrown.get_unit(v).pos.x >= 11);
            SimulationEngine collapse;
            make_world(collapse, swimmer ? 65 : kNoDefault);
            const uint32_t c = collapse.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            collapse.grid_mut().set_terrain(10, 10, TERRAIN_WATER);
            collapse.set_bridge_at(TileCoord{10, 10}, 4, 30);
            run_ticks(collapse, 100);
            ASSERT_EQ(snapshot_of(collapse, c) != nullptr, swimmer);
        }
    } TEST_END();

    TEST_CASE("3.10 An ant thrown onto a fire wall: every ant but a fire ant loses a hit point (0x101bc58); the type of the victim decides, default or own") {
        for (int k = 0; k < 5; ++k) {
            const Fixture fixture = [](SimulationEngine& sim, uint32_t, uint32_t) {
                sim.set_fire_at(TileCoord{11, 10}, 3600);
                sim.set_fire_at(TileCoord{14, 10}, 3600);
            };
            const Script script = [](uint32_t ant, uint32_t) {
                return std::vector<Step>{
                    {3, [=](SimulationEngine& s) {
                        const uint32_t hitter = s.spawn_unit(1, AntType::Thief, TileCoord{9, 10});
                        s.execute_melee_attack(hitter, ant);
                    }},
                };
            };
            ASSERT_EQ(equivalence(kTypeOfTile[k], kPowerUpTiles[k], fixture, script, 120, std::string("fire wall landing ") + type_name(kTypeOfTile[k])), -1);
        }
        for (bool fire : {true, false}) {                                              // the fire ant is not hurt by the wall, a worker is
            SimulationEngine sim;
            make_world(sim, fire ? 66 : kNoDefault);
            sim.set_fire_at(TileCoord{11, 10}, 3600);
            const uint32_t v = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
            const uint32_t h = sim.spawn_unit(1, AntType::Thief, TileCoord{9, 10});
            sim.execute_melee_attack(h, v);
            run_ticks(sim, 80);
            ASSERT_EQ(sim.get_unit(v).hp, fire ? 9u : 8u);                                // the punch took 1; the fire wall took 1 more from everybody who is not a fire ant
        }
    } TEST_END();

    TEST_CASE("3.11 A combat ant that is hit while it engages an enemy forgets the engage (StartEngaged asks the getter, 0x1020c9c): its timer cannot send it back where it stood") {
        // the hit comes at the first tick at which the own-type ant is in the middle of an auto-engage (found by looking, so that the test does not depend on the walk's pace)
        int hit_tick = -1;
        for (int candidate = 41; candidate < 80 && hit_tick < 0; ++candidate) {
            SimulationEngine probe;
            make_world(probe, kNoDefault);
            const uint32_t ant = probe.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
            probe.spawn_unit(1, AntType::Thief, TileCoord{40, 40});
            for (int t = 0; t < candidate; ++t) {
                if (t == 40) probe.spawn_unit(1, AntType::Thief, TileCoord{13, 10});
                probe.tick();
            }
            if (probe.get_unit(ant).auto_engage && probe.get_unit(ant).waypoints.size() > 1 && probe.get_unit(ant).combevt_due_ms != 0) hit_tick = candidate;
        }
        ASSERT_TRUE(hit_tick > 0);
        const Fixture fixture = [](SimulationEngine&, uint32_t, uint32_t) {};
        const Script script = [hit_tick](uint32_t ant, uint32_t) {
            return std::vector<Step>{
                {40, [=](SimulationEngine& s) { s.spawn_unit(1, AntType::Thief, TileCoord{13, 10}); }},                  // an enemy within three tiles of the idle ant (id 3)
                {hit_tick, [=](SimulationEngine& s) {                                                                   // while it steps towards it, another one hits it
                    const TileCoord at = s.get_unit(ant).pos;
                    const uint32_t hitter = s.spawn_unit(1, AntType::Thief, TileCoord{at.x, at.y + 1});
                    s.execute_melee_attack(hitter, ant);
                }},
                {hit_tick + 1, [=](SimulationEngine& s) { s.kill_unit(3); s.kill_unit(4); }},                          // both enemies (ids 3 and 4) are gone: nothing starts another engage, so only the old timer (2 s) could send the ant back
            };
        };
        ASSERT_EQ(equivalence(AntType::Combat, 62, fixture, script, 400, "combat hit while engaged"), -1);
    } TEST_END();

    // ===================================================================================================================================================================
    // 4. What the ant's OWN type decides (the field +0x54), not the getter
    // ===================================================================================================================================================================

    TEST_CASE("4.1 A default Combat worker takes a Thief power-up: its type becomes Thief, it drops no power-up (own type 0), and it can raid from then on") {
        SimulationEngine sim;
        make_world(sim, 62);
        sim.grid_mut().place_powerup(14, 10, 3);                              // a Thief power-up
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.apply_command(group(CommandType::GroupMove, 0, 14, 10, {a}));
        run_ticks(sim, 120);
        ASSERT_EQ(sim.get_unit(a).type, AntType::Thief);                      // the own type field was Worker and took the power-up's
        ASSERT_EQ(sim.ant_type(sim.get_unit(a)), AntType::Thief);
        ASSERT_EQ(snapshot_of(sim, a)->type, AntType::Thief);
        ASSERT_EQ(snapshot_of(sim, a)->raw_type, AntType::Thief);
        ASSERT_EQ(count_powerups(sim), 0u);                                   // nothing was dropped: the old kind of a Worker (raw 0) is none, whatever the level's default is
        // a typed ant that swaps drops its old kind on a neighbour tile (the control)
        SimulationEngine swap;
        make_world(swap, 62);
        swap.grid_mut().place_powerup(14, 10, 3);
        const uint32_t s = swap.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        swap.apply_command(group(CommandType::GroupMove, 0, 14, 10, {s}));
        run_ticks(swap, 120);
        ASSERT_EQ(swap.get_unit(s).type, AntType::Thief);
        ASSERT_EQ(count_powerups(swap), 1u);                                  // the Combat power-up it dropped
    } TEST_END();

    TEST_CASE("4.2 A default-type worker that dies leaves no power-up (Kill reads the own type 0, 0x1021011); a typed ant leaves its kind") {
        SimulationEngine sim;
        make_world(sim, 62);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{10, 10});
        sim.kill_unit(a);
        ASSERT_EQ(count_powerups(sim), 0u);
        const uint32_t b = sim.spawn_unit(0, AntType::Combat, TileCoord{20, 20});
        sim.kill_unit(b);
        ASSERT_EQ(count_powerups(sim), 1u);
    } TEST_END();

    TEST_CASE("4.3 The workers of a default Thief level cannot raid: a click on an enemy hill stops them (GoTo asks the own type, 0x101fcb7); a thief that took the power-up raids") {
        SimulationEngine sim;
        make_world(sim, 63);
        const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{44, 50});
        const CommandResult r = sim.apply_command(group(CommandType::GroupMove, 0, 51, 51, {a}));
        ASSERT_EQ(r.ack_ant, 0u);                                             // no path: GoTo stops the ant silently
        ASSERT_EQ(sim.predict_order_ack(group(CommandType::GroupMove, 0, 51, 51, {a})), 0u);
        run_ticks(sim, 200);
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{44, 50}));
        const uint32_t t = sim.spawn_unit(0, AntType::Thief, TileCoord{44, 48});     // a thief with the own type 3 raids
        const CommandResult r2 = sim.apply_command(group(CommandType::GroupMove, 0, 51, 51, {t}));
        ASSERT_EQ(r2.ack_ant, t);
        ASSERT_EQ(sim.predict_order_ack(group(CommandType::GroupMove, 0, 51, 51, {t})), t);
    } TEST_END();

    TEST_CASE("4.4 The prediction of an order's acknowledgement asks the getter for the special order: a default Bomber's special click on bad ground is refused like a bomber's") {
        for (uint16_t tile : {kNoDefault, uint16_t{64}}) {
            SimulationEngine sim;
            make_world(sim, tile);
            sim.grid_mut().set_terrain(14, 10, TERRAIN_OBSTACLE);
            const uint32_t a = sim.spawn_unit(0, tile == kNoDefault ? AntType::Bomber : AntType::Worker, TileCoord{10, 10});
            ASSERT_EQ(sim.predict_order_ack(group(CommandType::GroupSpecial, 0, 14, 10, {a})), 0u);
            ASSERT_EQ(sim.predict_order_ack(group(CommandType::GroupSpecial, 0, 16, 10, {a})), a);
        }
    } TEST_END();

    TEST_CASE("4.5 After a failed attack step the combat timer is cancelled only for an ant whose OWN type is Combat (the type test at 0x101c6b0 reads +0x54)") {
        for (bool own : {true, false}) {
            SimulationEngine sim;
            make_world(sim, own ? kNoDefault : 62);
            const uint32_t a = sim.spawn_unit(0, own ? AntType::Combat : AntType::Worker, TileCoord{10, 10});
            const uint32_t far = sim.spawn_unit(1, AntType::Thief, TileCoord{30, 30});
            AntUnit& unit = sim.get_unit(a);
            unit.auto_engage = true;                                          // it is in the middle of an auto-engage
            unit.combevt_due_ms = 123456;                                     // with its COMBEVT timer running
            sim.execute_melee_attack(a, far);                                 // the contact is refused (the enemy is not adjacent): "Can't do that"
            ASSERT_EQ(unit.combevt_due_ms, own ? 0u : 123456u);               // FUN_0101dd6f (the resume, whose first act is FUN_0101c152, CancelCombatTimer) is called for the own type Combat only
        }
    } TEST_END();

    TEST_CASE("4.6 The last frame of an attack clip resumes the saved auto-engage of ANY ant whose engage flag is set, whatever its type (0x101ef4a: FUN_0101dd6f, no type test): a thief, bomber, fire ant or swimmer that still has the flag goes back to the saved place") {
        // The engage flag (+0xbc) is written only by the constructor, by AttackTile (0x101daa5, set), by the resume (0x101dde3, clear) and by StartEngaged for a combat ant (0x1020cae); a new
        // order (GoTo) and a pick-up leave it alone, so an ant that was a combat ant, was ordered away in the middle of an auto-engage and then took another power-up still has it. The
        // flag and the saved order are set by hand here (test 4.7 gets there by playing); v0.0.92 asked `own type == Combat` first and left such an ant standing at the end of its attack.
        for (AntType type : {AntType::Thief, AntType::Bomber, AntType::Fire, AntType::Swimmer}) {
            SimulationEngine sim;
            make_world(sim, kNoDefault);
            const uint32_t a = sim.spawn_unit(0, type, TileCoord{10, 10});
            const uint32_t e = sim.spawn_unit(1, AntType::Worker, TileCoord{11, 10});      // an enemy next to it
            AntUnit& u = sim.get_unit(a);
            u.auto_engage = true;                                                          // the stale flag of an earlier life as a combat ant ...
            u.ae_order = AntUnit::kOrderNone;                                              // ... with the order that it saved then: stand at (20, 10) (a plain move)
            u.ae_target = TileCoord{20, 10};
            u.ae_home_state = 0;
            ASSERT_TRUE(sim.apply_command(group(CommandType::GroupAttack, 0, 11, 10, {a})).accepted());
            run_ticks(sim, 80);
            ASSERT_TRUE(sim.get_unit(e).hp < 10u);                                         // it punched
            ASSERT_FALSE(sim.get_unit(a).auto_engage);                                     // the resume gave the saved order again and cleared the flag
            ASSERT_TRUE(sim.get_unit(a).pos != (TileCoord{10, 10}));                       // and the ant is on its way to the saved tile ...
            int arrived = -1;
            for (int t = 0; t < 400 && arrived < 0; ++t) {
                sim.tick();
                if (sim.get_unit(a).pos == (TileCoord{20, 10}) && sim.get_unit(a).waypoints.empty()) arrived = t;
            }
            ASSERT_TRUE(arrived >= 0);                                                     // ... where it stands in the end
        }
        // an ant without the flag ends its attack and its order, as ever: a thief with the order to attack stands where it hit
        SimulationEngine control;
        make_world(control, kNoDefault);
        const uint32_t c = control.spawn_unit(0, AntType::Thief, TileCoord{10, 10});
        control.spawn_unit(1, AntType::Worker, TileCoord{11, 10});
        ASSERT_TRUE(control.apply_command(group(CommandType::GroupAttack, 0, 11, 10, {c})).accepted());
        run_ticks(control, 300);
        ASSERT_EQ(control.get_unit(c).pos, (TileCoord{10, 10}));
        ASSERT_FALSE(control.get_unit(c).auto_engage);
    } TEST_END();

    TEST_CASE("4.7 The same on the player's path: a combat ant starts an auto-engage, is ordered away, takes a Thief power-up and attacks: at the end of that attack it goes back to where the engage began (v0.0.92 left it standing)") {
        SimulationEngine sim;
        make_world(sim, kNoDefault);
        sim.grid_mut().place_powerup(10, 20, 3);                                          // a Thief power-up
        const uint32_t a = sim.spawn_unit(0, AntType::Combat, TileCoord{10, 10});
        sim.spawn_unit(1, AntType::Worker, TileCoord{40, 40});                            // id 2: far away
        sim.spawn_unit(1, AntType::Worker, TileCoord{13, 10});                            // id 3: within three tiles of the idle combat ant, which the reflex finds after 2 s
        int engaged = -1;
        for (int t = 0; t < 200 && engaged < 0; ++t) {
            sim.tick();
            if (sim.get_unit(a).auto_engage) engaged = t;
        }
        ASSERT_TRUE(engaged >= 0);                                                        // the reflex started (the flag is +0xbc, the saved place is where the ant stood)
        ASSERT_EQ(sim.get_unit(a).ae_target, (TileCoord{10, 10}));
        ASSERT_TRUE(sim.get_unit(a).combevt_due_ms != 0);                                 // with its COMBEVT timer
        // the player sends it away in the middle of the engage: GoTo cancels the timer and does not touch the flag
        ASSERT_TRUE(sim.apply_command(group(CommandType::GroupMove, 0, 10, 20, {a})).accepted());
        ASSERT_TRUE(sim.get_unit(a).auto_engage);
        ASSERT_EQ(sim.get_unit(a).combevt_due_ms, 0u);
        run_ticks(sim, 400);                                                              // it walks onto the power-up and takes it: a Thief now (own type 3)
        ASSERT_EQ(sim.get_unit(a).type, AntType::Thief);
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{10, 20}));
        ASSERT_TRUE(sim.get_unit(a).auto_engage);                                         // the pick-up did not touch the flag either
        // now it attacks an enemy that stands next to it
        const TileCoord at = sim.get_unit(a).pos;
        const uint32_t victim = sim.spawn_unit(1, AntType::Worker, TileCoord{at.x + 1, at.y});
        ASSERT_TRUE(sim.apply_command(group(CommandType::GroupAttack, 0, at.x + 1, at.y, {a})).accepted());
        run_ticks(sim, 80);
        ASSERT_EQ(sim.get_unit(victim).hp, 9u);                                           // the thief's punch
        ASSERT_FALSE(sim.get_unit(a).auto_engage);                                        // the end of the attack clip resumed the saved engage ...
        ASSERT_TRUE(sim.get_unit(a).pos.y < at.y);                                        // ... and the ant is on its way back
        run_ticks(sim, 400);
        ASSERT_EQ(sim.get_unit(a).pos, (TileCoord{10, 10}));                              // to the place where it stood when the engage began
    } TEST_END();

    TEST_CASE("4.8 The InfiltrateAnthill order of the AntOrder interface (only tests give one: the game's group orders are AntOrders of the Move type) reads the own type at an enemy hill as GoTo does: a worker of a default Thief level does not raid, a thief does") {
        // InfiltrateAnthill: without a team the order looks the hill up by the clicked tile, with one it takes the team (sim_engine.cpp issue_order); a thief's goal is the raid tile of the
        // hill (x + 3, y + 2), every other ant's the middle (x + 1, y + 1), and an ant that stands on the goal starts the raid at once. The workers of a default Thief level have the own type 0,
        // so they take the middle (0x101fcb7 reads +0x54): a worker that stands on the middle starts the raid, a thief that stands there walks to the raid tile.
        for (const bool explicit_team : {false, true}) {
            SimulationEngine sim;
            make_world(sim, 63);                                                          // the default type is Thief
            const uint32_t worker = sim.spawn_unit(0, AntType::Worker, TileCoord{51, 51});     // the middle of team 1's hill (50, 50)
            const uint32_t thief = sim.spawn_unit(0, AntType::Thief, TileCoord{52, 51});
            AntOrder order;
            order.type = OrderType::InfiltrateAnthill;
            order.target_x = explicit_team ? 0 : 51;
            order.target_y = explicit_team ? 0 : 51;
            order.target_entity_id = explicit_team ? 1 : -1;
            order.ant_id = worker;
            ASSERT_TRUE(sim.issue_order(order));
            ASSERT_EQ(sim.get_unit(worker).loco_action, AntUnit::kActionRaid);           // the middle is its goal: the raid clip starts
            order.ant_id = thief;
            order.target_x = explicit_team ? 0 : 51;
            order.target_y = explicit_team ? 0 : 51;
            ASSERT_TRUE(sim.issue_order(order));
            ASSERT_TRUE(sim.get_unit(thief).loco_action != AntUnit::kActionRaid);        // its goal is the raid tile (53, 52): it walks there first
        }
    } TEST_END();

    TEST_CASE("4.9 The ability entry points (a test and tool interface) start the clip of the type that the ant IS: a worker of a default Bomber, Fire or Swimmer level plants, lights and builds like the ant of that own type") {
        // plant_bomb, ignite_fire and build_bridge_step without `instant` ask whether the ant is ready for the ability of the type (type, not engaged, not frozen, action 0, 1 or 3): the
        // type is the getter's (0x100f9cb), so a worker of a default level qualifies, and an ant that already works does not
        for (int k = 0; k < 3; ++k) {
            const uint16_t tile = k == 0 ? 64 : k == 1 ? 66 : 65;                          // Bomber, Fire, Swimmer
            SimulationEngine sim;
            make_world(sim, tile);
            sim.set_terrain(12, 10, TERRAIN_WATER);                                        // water for the bridge, next to the ant at (11, 10)
            const uint32_t a = sim.spawn_unit(0, AntType::Worker, TileCoord{11, 10});
            const TileCoord target = k == 2 ? TileCoord{12, 10} : TileCoord{11, 11};
            const bool started = k == 0 ? sim.plant_bomb(a, target, false) : k == 1 ? sim.ignite_fire(a, target, false) : sim.build_bridge_step(a, target);
            ASSERT_TRUE(started);
            run_ticks(sim, 2);
            ASSERT_TRUE(sim.get_unit(a).state == (k == 0 ? UnitState::PlantingBomb : k == 1 ? UnitState::PlacingFire : UnitState::BuildingBridge));
            const bool again = k == 0 ? sim.plant_bomb(a, TileCoord{10, 10}, false) : k == 1 ? sim.ignite_fire(a, TileCoord{10, 10}, false) : sim.build_bridge_step(a, TileCoord{12, 10});
            ASSERT_FALSE(again);                                                           // busy with the clip: not ready
        }
        SimulationEngine plain;                                                            // a worker of a level without a default has no ability at all
        make_world(plain, kNoDefault);
        const uint32_t w = plain.spawn_unit(0, AntType::Worker, TileCoord{11, 10});
        ASSERT_FALSE(plain.plant_bomb(w, TileCoord{11, 11}, false));
    } TEST_END();

    // ===================================================================================================================================================================
    // 5. Power-ups are tile ids: a dictionary that calls them "." changes nothing
    // ===================================================================================================================================================================

    TEST_CASE("5.1 The five power-up ids are power-ups in every dictionary: the grid of a level that names them \".\" equals the grid of one that calls them pu_comb, pu_thief ...") {
        Grid named, dotted;
        ASSERT_TRUE(named.init_from_level(level_with_powerups(false)));
        ASSERT_TRUE(dotted.init_from_level(level_with_powerups(true)));
        ASSERT_EQ(named.width(), dotted.width());
        for (uint32_t y = 0; y < named.height(); ++y) {
            for (uint32_t x = 0; x < named.width(); ++x) {
                const TileCell& a = named.get_cell(x, y);
                const TileCell& b = dotted.get_cell(x, y);
                ASSERT_EQ(a.is_powerup, b.is_powerup);
                ASSERT_EQ(a.powerup_type, b.powerup_type);
                ASSERT_EQ(a.is_obstacle_overlay, b.is_obstacle_overlay);
                ASSERT_EQ(a.static_solid, b.static_solid);
                ASSERT_EQ(a.interactive_id, b.interactive_id);
                ASSERT_EQ(named.is_solid_object(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}), dotted.is_solid_object(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}));
            }
        }
        for (int k = 0; k < 5; ++k) {
            const TileCell& c = dotted.get_cell(static_cast<uint32_t>(12 + 2 * k), 10);
            ASSERT_TRUE(c.has_powerup());
            ASSERT_EQ(c.powerup_type, kPowerUpTypeOfTile[k]);
            ASSERT_FALSE(c.is_obstacle_overlay);                              // not a rock
            ASSERT_FALSE(c.static_solid);                                     // the removable object's own solidity, as in the named level
            ASSERT_TRUE(dotted.is_solid_object(TileCoord{12 + 2 * k, 10}));   // but solid to every order that is not the power-up order
        }
    } TEST_END();

    TEST_CASE("5.2 The state of the simulation does not depend on the dictionary names of the power-up ids (equal hashes tick by tick through a pick-up)") {
        SimulationEngine a, b;
        a.init(level_with_powerups(false), 9, 0x0F);
        b.init(level_with_powerups(true), 9, 0x0F);
        ASSERT_EQ(count_powerups(a), 5u);
        ASSERT_EQ(count_powerups(b), 5u);
        ASSERT_TRUE(a.state_hash() == b.state_hash());
        const uint32_t ant_a = a.get_world_state().ants.front().id;
        const uint32_t ant_b = b.get_world_state().ants.front().id;
        a.apply_command(group(CommandType::GroupMove, 0, 12, 10, {ant_a}));
        b.apply_command(group(CommandType::GroupMove, 0, 12, 10, {ant_b}));
        for (int t = 0; t < 200; ++t) {
            a.tick();
            b.tick();
            ASSERT_TRUE(a.state_hash() == b.state_hash());
        }
        ASSERT_EQ(a.get_unit(ant_a).type, AntType::Combat);                   // the pick-up happened in both
        ASSERT_EQ(b.get_unit(ant_b).type, AntType::Combat);
        ASSERT_EQ(count_powerups(b), 4u);
    } TEST_END();

    TEST_CASE("5.3 A power-up named \".\" is solid for every order but the power-up order: paths go around it, a click next to it ends next to it, a click on it takes it on arrival") {
        for (bool dotted : {false, true}) {
            SimulationEngine sim;
            sim.init(level_with_powerups(dotted), 9, 0x0F);
            const uint32_t ant = sim.get_world_state().ants.front().id;       // green's first start marker (9, 8)
            const TileCoord start = sim.get_unit(ant).pos;
            ASSERT_EQ(start.y, 8);
            // a walk to the far side of the power-up in row 10: the path goes around it, nobody takes it
            sim.apply_command(group(CommandType::GroupMove, 0, 13, 10, {ant}));
            run_ticks(sim, 140);
            ASSERT_EQ(sim.get_unit(ant).pos, (TileCoord{13, 10}));
            ASSERT_EQ(sim.get_unit(ant).type, AntType::Worker);
            ASSERT_EQ(count_powerups(sim), 5u);
            // walking past the cell at (12, 10) never took it; a click on it takes it when the walk arrives (the order 4 of FUN_01020655, FUN_0101ccaf 0x101cd1d)
            sim.apply_command(group(CommandType::GroupMove, 0, 12, 10, {ant}));
            run_ticks(sim, 60);
            ASSERT_EQ(sim.get_unit(ant).type, AntType::Combat);               // the kind of tile 62
            ASSERT_EQ(count_powerups(sim), 4u);
            ASSERT_FALSE(sim.grid().has_powerup_at(TileCoord{12, 10}));
        }
    } TEST_END();

    TEST_CASE("5.4 Every kind of power-up tile gives its type when taken, whatever its dictionary name") {
        for (int k = 0; k < 5; ++k) {
            SimulationEngine sim;
            sim.init(level_with_powerups(true), 9, 0x0F);
            const uint32_t ant = sim.get_world_state().ants.front().id;
            sim.apply_command(group(CommandType::GroupMove, 0, 12 + 2 * k, 10, {ant}));
            run_ticks(sim, 200);
            ASSERT_EQ(sim.get_unit(ant).type, kTypeOfTile[k]);
        }
    } TEST_END();

    TEST_CASE("5.5 A dictionary that damages the name (\".......\", \"pu_x\") does not make or unmake a power-up: only the id counts") {
        LevelData level = level_with_powerups(false);
        level.tile_dictionary[62] = ".......";
        level.tile_dictionary[63] = "pu_x";
        level.tile_dictionary[64] = "rock1";
        level.tile_dictionary[65] = "";
        // and a layer-2 cell with a pu_ name on an id that is no power-up id is not a power-up, nor is any other tile with a flag (the effect animations 40, 53, 133, 135, 230,
        // 422 .. 426, 1177: flag 0x20; the neighbours of the five ids)
        level.tile_dictionary[100] = "pu_comb";
        put_cell(level, 22, 10, 100);
        const uint16_t not_powerups[] = {40, 53, 61, 67, 133, 135, 230, 422, 424, 426};
        for (size_t i = 0; i < sizeof(not_powerups) / sizeof(not_powerups[0]); ++i) {
            level.tile_dictionary[not_powerups[i]] = i % 2 == 0 ? "pu_thief" : ".";
            put_cell(level, 4 + static_cast<int>(i), 15, not_powerups[i]);
        }
        Grid g;
        ASSERT_TRUE(g.init_from_level(level));
        for (int k = 0; k < 5; ++k) ASSERT_TRUE(g.get_cell(static_cast<uint32_t>(12 + 2 * k), 10).has_powerup());
        ASSERT_FALSE(g.get_cell(22, 10).has_powerup());
        for (size_t i = 0; i < sizeof(not_powerups) / sizeof(not_powerups[0]); ++i) ASSERT_FALSE(g.get_cell(4 + static_cast<uint32_t>(i), 15).has_powerup());
    } TEST_END();

    TEST_CASE("5.6 The six shipped maps have the power-ups and droppers that they always had (by id = by name there)") {
        struct Expect { const char* name; size_t powerups; size_t droppers; };
        const Expect expect[] = {{"TINY", 0, 0}, {"SMALL", 2, 2}, {"MEDIUM", 0, 1}, {"GAUNTLET", 10, 1}, {"ISLANDS", 40, 2}, {"TREASURE", 20, 0}};
        for (const Expect& e : expect) {
            LevelData level;
            ASSERT_TRUE(level.load_from_file(maps_dir() + e.name + ".LVL"));
            SimulationEngine sim;
            sim.init(level, 1, 0x0F);
            ASSERT_EQ(count_powerups(sim), e.powerups);
            ASSERT_EQ(sim.get_world_state().flower_droppers.size(), e.droppers);
            // by name, as the old loader counted: the same cells
            size_t by_name = 0;
            for (uint32_t y = 0; y < level.height(); ++y) {
                for (uint32_t x = 0; x < level.width(); ++x) {
                    const MapCell& c = level.get_cell_layer2(x, y);
                    if (c.tile_index < level.tile_dictionary.size() && level.tile_dictionary[c.tile_index].rfind("pu_", 0) == 0) ++by_name;
                }
            }
            ASSERT_EQ(by_name, e.powerups);
        }
    } TEST_END();

    // ===================================================================================================================================================================
    // 6. Flower droppers are plants by tile id with a block 4 record whose flag is not 0
    // ===================================================================================================================================================================

    TEST_CASE("6.1 A flower whose dictionary name is \".\" is a dropper when its tile id has the plant flag and its block 4 record has a flag: it posts a power-up of the drawn kind") {
        for (const char* name : {".", "flower1"}) {
            LevelData level = level_with_dropper(421, name, 15, 20, 1, 5, {0.0, 1.0, 0.0, 0.0, 0.0});     // only Combat
            SimulationEngine sim;
            sim.init(level, 3, 0x0F);
            ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 1u);
            ASSERT_EQ(sim.get_world_state().flower_droppers[0].x, 15);
            ASSERT_EQ(sim.get_world_state().flower_droppers[0].y, 20);
            bool landed = false;
            for (int t = 0; t < 900 && !landed; ++t) {
                sim.tick();
                landed = sim.grid().has_powerup_at(TileCoord{15, 21});          // one row below the stem (the table at 0x1001af8: id 421)
            }
            ASSERT_TRUE(landed);
            ASSERT_EQ(sim.grid().get_powerup_type(TileCoord{15, 21}), 4);
        }
    } TEST_END();

    TEST_CASE("6.2 The record's flag decides by 'not 0' (the original tests the dword, 0x100fcc5): a record with the flag 2 drops, a record with the flag 0 does not") {
        SimulationEngine on;
        on.init(level_with_dropper(410, ".", 15, 20, 2, 5, {1.0, 0.0, 0.0, 0.0, 0.0}), 3, 0x0F);
        ASSERT_EQ(on.get_world_state().flower_droppers.size(), 1u);
        SimulationEngine off;
        off.init(level_with_dropper(410, ".", 15, 20, 0, 5, {1.0, 0.0, 0.0, 0.0, 0.0}), 3, 0x0F);
        ASSERT_EQ(off.get_world_state().flower_droppers.size(), 0u);
    } TEST_END();

    TEST_CASE("6.3 All fourteen plant ids drop, at the offset of their id: the flowers 410 .. 416, 420, 421 one row below, the clovers 404 .. 408 on the stem's own tile") {
        const uint16_t ids[14] = {404, 405, 406, 407, 408, 410, 411, 412, 413, 414, 415, 416, 420, 421};
        for (uint16_t id : ids) {
            SimulationEngine sim;
            sim.init(level_with_dropper(id, ".", 15, 20, 1, 5, {0.0, 0.0, 0.0, 1.0, 0.0}), 3, 0x0F);      // only Swimmer
            ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 1u);
            const bool one_row_down = id >= 410;
            const TileCoord drop{15, one_row_down ? 21 : 20};
            bool landed = false;
            for (int t = 0; t < 900 && !landed; ++t) {
                sim.tick();
                landed = sim.grid().has_powerup_at(drop);
            }
            ASSERT_TRUE(landed);
            ASSERT_EQ(sim.grid().get_powerup_type(drop), 5);
        }
    } TEST_END();

    TEST_CASE("6.4 A record on a tile that is no plant (a flag 0x20 effect id, a rock, a power-up id), or a plant without a record, is no dropper, whatever the name says") {
        for (uint16_t id : {uint16_t{426}, uint16_t{422}, uint16_t{133}, uint16_t{100}, uint16_t{62}, uint16_t{403}, uint16_t{409}, uint16_t{417}, uint16_t{419}}) {
            SimulationEngine sim;
            sim.init(level_with_dropper(id, "flower1", 15, 20, 1, 5, {0.0, 1.0, 0.0, 0.0, 0.0}), 3, 0x0F);       // the name claims a flower
            ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 0u);
        }
        LevelData no_record = level_with_dropper(421, "flower1", 15, 20, 1, 5, {0.0, 1.0, 0.0, 0.0, 0.0});
        no_record.waypoints.clear();
        SimulationEngine sim;
        sim.init(no_record, 3, 0x0F);
        ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 0u);
    } TEST_END();

    TEST_CASE("6.5 The record is looked up by the first one at the plant's cell (FUN_01008d79), and the remake makes one dropper of two plants that share a record (the original may post twice in a poll there, which no map can show)") {
        // The original: the stamp (+0x18) is the record's, but the poll compares the elapsed time with the clock that it took at its start as an UNSIGNED difference (0x100fcf2 - 0x100fcf4: `cmp ecx, eax; jbe`)
        // and a drop renews the stamp with a LATER timeGetTime (0x100fdb8), so the second plant of the record, further down the same poll, can see a "negative" elapsed time (a huge unsigned number) and
        // drop too, when the clock has moved by a millisecond in between. That wall-clock effect cannot be copied by a lock-step engine, and no map of the library has two plants on a cell that has a
        // record (0 of 540), so the remake keeps the one dropper per record that is deterministic: this test pins that.
        LevelData level = level_with_dropper(421, ".", 15, 20, 0, 5, {1.0, 0.0, 0.0, 0.0, 0.0});      // the first record at the cell has the flag 0
        Waypoint second;
        second.x = 15;
        second.y = 20;
        second.flag = 1;
        second.param = 5;
        second.probabilities = {1.0, 0.0, 0.0, 0.0, 0.0};
        level.waypoints.push_back(second);
        SimulationEngine first_wins;
        first_wins.init(level, 3, 0x0F);
        ASSERT_EQ(first_wins.get_world_state().flower_droppers.size(), 0u);          // the lookup returns the first record, its flag is 0: no dropper
        LevelData shared = level_with_dropper(421, ".", 15, 20, 1, 5, {1.0, 0.0, 0.0, 0.0, 0.0});
        AnthillSpawn again = shared.anthill_spawns.back();                         // the plant that the helper appended
        again.tile_id = 410;
        shared.anthill_spawns.push_back(again);                                    // a second plant on the same cell, the same record
        SimulationEngine sim;
        sim.init(shared, 3, 0x0F);
        ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 1u);
    } TEST_END();

    TEST_CASE("6.6 The plants of block 1 (the world objects that the minimap shows) are the records whose tile id has the plant flag 0x10 OR the effect flag 0x20; only the 0x10 ones can be droppers") {
        // 0x100e3b0 - 0x100e436 makes a world object of every record whose id has 0x10 or 0x20 (the flowers and clovers, and the effect animations dsplash, bombex, sputter, the droplets:
        // 40, 53, 133, 135, 230, 422 .. 426, 1177); the dropper task (0x100fc78, FUN_01007227) takes the 0x10 ids only. A record of any other id is no world object.
        for (const uint16_t id : {uint16_t{40}, uint16_t{53}, uint16_t{133}, uint16_t{135}, uint16_t{230}, uint16_t{422}, uint16_t{423}, uint16_t{424}, uint16_t{425}, uint16_t{426}, uint16_t{1177}}) {
            ASSERT_TRUE(movement::is_plant_object_tile(id));
            ASSERT_FALSE(movement::is_dropper_plant_tile(id));
        }
        for (const uint16_t id : {uint16_t{404}, uint16_t{405}, uint16_t{406}, uint16_t{407}, uint16_t{408}, uint16_t{410}, uint16_t{411}, uint16_t{412}, uint16_t{413}, uint16_t{414}, uint16_t{415},
                                  uint16_t{416}, uint16_t{420}, uint16_t{421}}) {
            ASSERT_TRUE(movement::is_plant_object_tile(id));
            ASSERT_TRUE(movement::is_dropper_plant_tile(id));
        }
        for (const uint16_t id : {uint16_t{0}, uint16_t{61}, uint16_t{62}, uint16_t{100}, uint16_t{403}, uint16_t{409}, uint16_t{417}, uint16_t{419}, uint16_t{427}, uint16_t{1343}, uint16_t{1344}, uint16_t{4094}}) {
            ASSERT_FALSE(movement::is_plant_object_tile(id));
            ASSERT_FALSE(movement::is_dropper_plant_tile(id));
        }
        // a level with one record of each kind, every one of them with a block 4 record that has a flag
        LevelData level = tiny_level();
        struct Record {
            uint16_t id;
            uint16_t x;
        };
        const Record records[] = {{410, 15}, {426, 17}, {133, 19}, {100, 21}, {62, 23}, {409, 25}, {40, 27}};      // a flower, three effects, a rock, a power-up, a solid id
        for (const Record& r : records) {
            AnthillSpawn sp;
            sp.tile_id = r.id;
            sp.x = r.x;
            sp.y = 20;
            sp.team_id = 255;
            level.anthill_spawns.push_back(sp);
            Waypoint wp;
            wp.x = r.x;
            wp.y = 20;
            wp.flag = 1;
            wp.param = 5;
            wp.probabilities = {1.0, 0.0, 0.0, 0.0, 0.0};
            level.waypoints.push_back(wp);
        }
        Grid g;
        ASSERT_TRUE(g.init_from_level(level));
        ASSERT_EQ(g.plants().size(), 4u);                                                // the flower, the effects 426, 133 and 40
        ASSERT_EQ(g.plants()[0].tile_id, 410);
        ASSERT_EQ(g.plants()[1].tile_id, 426);
        ASSERT_EQ(g.plants()[2].tile_id, 133);
        ASSERT_EQ(g.plants()[3].tile_id, 40);
        ASSERT_EQ(g.plants()[1].x, 17);
        ASSERT_EQ(g.plants()[1].y, 20);
        SimulationEngine sim;
        sim.init(level, 3, 0x0F);
        ASSERT_EQ(sim.get_world_state().plants.size(), 4u);                              // what the minimap draws
        ASSERT_EQ(sim.get_world_state().flower_droppers.size(), 1u);                     // but only the flower can drop
        ASSERT_EQ(sim.get_world_state().flower_droppers[0].x, 15);
    } TEST_END();

    std::cout << "\n=======================================================\n"
              << " TEST SUMMARY: Total test cases: " << g_test_count << ", Total assertions: " << g_assert_count << ", Failed: " << g_test_failures << "\n"
              << "=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
