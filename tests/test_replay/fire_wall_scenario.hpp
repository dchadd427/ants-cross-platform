#pragma once

// A small match made in code, for the tests of the loader's rules numbers (test_rules_fire_wall; docs/reverse_engineering/map_loader_fidelity.md, section 8): a 40 x 40 map of plain ground whose dictionary names the
// fire wall `wallup04` (the map maker's entry) and holds one fire wall of the file at (20, 20), solid by its file bit, in the middle of the row where the sides stand: seat 0 has a Fire Ant east of it (21, 20) and two
// Combat Ants to the west, seat 1 a Bomber at (19, 20) and a Worker far away (the match goes on to the clock, a minute). Seat 0's Fire Ant puts the fire wall out at turn 5, which leaves a put-out cell that the loader of the
// rules numbers 5 to 8 marks with the obstacle overlay of its named entry (the state hash holds it from tick 0) and the loader of the rules numbers 9 and 10 does not; from turn 65 the Combat Ants attack the Bomber every
// 20th turn and it dies, and its power-up goes to a free neighbor tile (the cell where the fire stood is one of them under the rules of the original and none under the old loader, which the cases of
// tests/test_sim/test_loader_fidelity.cpp pin on a single free neighbor). Each team has a hill in a corner: a map without hill cells would take its start markers for hills. The map is built here, never read from a
// file, and it needs nothing of the library.
//
// The match is played the same by every build that has this header's includes (it uses no call that the rules number 9 added): tests/test_replay/test_rules_fire_wall.cpp pins what the build before the number
// made of it under the numbers 8 and 7 (taken by playing this very header against that build, not against this one).

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ants::fire_wall_scenario {

inline constexpr uint32_t kSeed = 6;
inline constexpr uint8_t kRoster = 0x03;
inline constexpr sim::TileCoord kWall{20, 20};          // the fire wall of the file

inline assets::LevelData level() {
    assets::LevelData map = assets::blank_level(40, 40, std::vector<std::string>{});
    map.default_minutes = 1;
    map.boundary_param = 0;                                    // (no egg: a side is its ants)
    if (map.tile_dictionary.size() <= 134) map.tile_dictionary.resize(135, ".");
    map.tile_dictionary[134] = "wallup04";
    const auto cell = [&](int32_t x, int32_t y) { return static_cast<size_t>(y) * map.width + static_cast<size_t>(x); };
    map.layer1_terrain[cell(kWall.x, kWall.y)].flags = static_cast<uint16_t>(map.layer1_terrain[cell(kWall.x, kWall.y)].flags | 1u);
    auto& fire = map.layer2_interactive[cell(kWall.x, kWall.y)];
    fire.tile_index = 134;                                     // the fire wall, as the map maker's Objects tool writes it: its own anchor in the third word
    fire.flags = 1;
    fire.properties = static_cast<uint16_t>((kWall.x << 8) | kWall.y);
    const auto hill = [&](uint8_t team, int32_t x, int32_t y) {         // (a hill of each team in a corner: without hill cells the start markers would become the hills)
        auto& cell_of_hill = map.layer2_interactive[cell(x, y)];
        cell_of_hill.tile_index = static_cast<uint16_t>(248 - team);
        cell_of_hill.flags = 0;
        cell_of_hill.properties = static_cast<uint16_t>((x << 8) | y);
    };
    hill(0, 2, 2);
    hill(1, 33, 33);
    const auto marker = [&](uint8_t team, uint16_t x, uint16_t y, uint8_t type) {
        assets::AnthillSpawn m;
        m.tile_id = static_cast<uint16_t>(152 + team);         // (the start marker of the team)
        m.team_id = team;
        m.x = x;
        m.y = y;
        m.ant_type = type;
        map.anthill_spawns.push_back(m);
    };
    marker(0, 21, 20, 2);                                      // the Fire Ant, east of the fire wall
    marker(0, 14, 20, 4);                                      // two Combat Ants in the row west of the dead end
    marker(0, 15, 20, 4);
    marker(1, 19, 20, 1);                                      // the Bomber in the dead end
    marker(1, 30, 30, 0);                                      // and a Worker far away
    return map;
}

/// The orders of the match: turn 5 the Fire Ant puts the fire wall out; from turn 65, every 20th turn, the Combat Ants attack the Bomber's tile
inline std::vector<sim::Command> orders(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<sim::Command> out;
    if (turn != 5 && !(turn >= 65 && turn % 20 == 5)) return out;
    const sim::WorldState& world = engine.get_world_state();
    sim::Command c;
    c.issuer = 0;
    if (turn == 5) {
        c.type = sim::CommandType::GroupSpecial;
        c.tile_x = static_cast<int16_t>(kWall.x);
        c.tile_y = static_cast<int16_t>(kWall.y);
        for (const sim::AntSnapshot& a : world.ants) {
            if (a.hp != 0 && a.player_id == 0 && a.type == sim::AntType::Fire) c.ants.push_back(a.id);
        }
    } else {
        c.type = sim::CommandType::GroupAttack;
        bool target = false;
        for (const sim::AntSnapshot& a : world.ants) {
            if (a.hp == 0) continue;
            if (a.player_id == 1 && a.type == sim::AntType::Bomber && !target) {
                c.tile_x = static_cast<int16_t>(a.tile_x);
                c.tile_y = static_cast<int16_t>(a.tile_y);
                target = true;
            } else if (a.player_id == 0 && a.type == sim::AntType::Combat) {
                c.ants.push_back(a.id);
            }
        }
        if (!target) c.ants.clear();
    }
    if (!c.ants.empty()) out.push_back(std::move(c));
    return out;
}

}  // namespace ants::fire_wall_scenario
