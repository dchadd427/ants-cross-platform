#pragma once

// A small match made in code, for the tests of the last side standing (docs/GAMEPLAY.md, "The last side standing"; test_last_side, test_rules_last_side): a 40 x 40 map of plain ground with no food, no hill and no
// egg, two Combat Ants of seat 0 and one Worker of seat 1, a clock of one minute. Seat 0 sends its ants at the Worker every second; seat 1 does nothing. The Worker dies, seat 0 is the one side that is left with nothing
// on the map to score, and the match ends on the spot under the rules number 8; under the number 5 it plays on to the clock. The map is built here, never read from a file, and it needs nothing of the library.
//
// The match is played the same by every build that has this header's includes (it uses no call that the last-side rule added): tests/test_replay/test_rules_last_side.cpp pins what the build before the rule made of
// it (kBeforeTurns, kBeforeFinalHash), and checks that the rules number 5 still makes exactly that.

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ants::scenario {

inline constexpr uint32_t kSeed = 4;
inline constexpr uint8_t kRoster = 0x03;

inline assets::LevelData level() {
    assets::LevelData map = assets::blank_level(40, 40, std::vector<std::string>{});
    map.default_minutes = 1;
    map.boundary_param = 0;                                    // (no egg: a side is its ants)
    const auto marker = [&](uint8_t team, uint16_t x, uint16_t y, uint8_t type) {
        assets::AnthillSpawn m;
        m.tile_id = static_cast<uint16_t>(152 + team);         // (the start marker of the team)
        m.team_id = team;
        m.x = x;
        m.y = y;
        m.ant_type = type;
        map.anthill_spawns.push_back(m);
    };
    marker(0, 10, 10, 4);                                      // two Combat Ants
    marker(0, 11, 12, 4);
    marker(1, 17, 11, 0);                                      // and a Worker
    return map;
}

/// The orders of the match, made from what the world holds: every 20th turn seat 0 attacks the tile of an ant of seat 1 with all its ants
inline std::vector<sim::Command> orders(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<sim::Command> out;
    if (turn % 20 != 5) return out;
    const sim::WorldState& world = engine.get_world_state();
    sim::Command attack;
    attack.type = sim::CommandType::GroupAttack;
    attack.issuer = 0;
    bool target = false;
    for (const sim::AntSnapshot& a : world.ants) {
        if (a.hp == 0) continue;
        if (a.player_id == 1 && !target) {
            attack.tile_x = static_cast<int16_t>(a.tile_x);
            attack.tile_y = static_cast<int16_t>(a.tile_y);
            target = true;
        } else if (a.player_id == 0) {
            attack.ants.push_back(a.id);
        }
    }
    if (target && !attack.ants.empty()) out.push_back(std::move(attack));
    return out;
}

}  // namespace ants::scenario
