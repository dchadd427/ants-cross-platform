#pragma once

// Two small matches of game mode 187 made in code, for the tests of the rules number 11 (test_rules_end_fix; docs/GAMEPLAY.md, "187"): a 40 x 40 map of plain ground with no food, a clock of one minute, no hill cell (so the start
// markers stand in for the hills), and the ants of the teams that have a start marker. The roster names the seats, the eggs are the map's own.
//   lone   roster of seats 0 and 1, the ants of seat 0 only, no egg: one team is on the map and seat 1 is a seat without a team. The rules before 11 counted the seats and ended the match at the first look with seat 0 the
//          winner; the rules of 11 count the teams that began, so a match of one team goes on to the clock.
//   ghost  roster of seats 0, 1 and 2, two eggs a seat, the ants of seats 0 and 1, none for seat 2: seat 2 holds eggs and has no hill and no ant, so no egg of it can ever hatch. At turn 100 seat 1 drops out. The rules
//          before 11 keep seat 2 standing for its eggs, so the match plays on to the clock with two sides; the rules of 11 do not count it and seat 0 is the side that is left.
// Neither match uses a call that the rules number 11 added (the player of a recording sets the rules of its number up with replay::begin_match), so tests/test_replay/test_rules_end_fix.cpp can pin what the build before the
// number made of them under the numbers 10, 7 and 6 (taken by playing this very header against that build, not against this one).

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ants::end_fix_scenario {

inline constexpr uint32_t kSeed = 9;

inline uint8_t roster(bool ghost) { return ghost ? uint8_t{0x07} : uint8_t{0x03}; }

inline assets::LevelData level(bool ghost) {
    assets::LevelData map = assets::blank_level(40, 40, std::vector<std::string>{});
    map.default_minutes = 1;
    if (ghost) map.boundary_param = 2;                         // (two eggs for every seat of the roster; the lone match has none)
    else map.boundary_param = 0;
    const auto marker = [&](uint8_t team, uint16_t x, uint16_t y) {
        assets::AnthillSpawn m;
        m.tile_id = static_cast<uint16_t>(152 + team);         // (the start marker of the team)
        m.team_id = team;
        m.x = x;
        m.y = y;
        m.ant_type = 0;                                        // a Worker
        map.anthill_spawns.push_back(m);
    };
    marker(0, 10, 10);
    marker(0, 11, 12);
    if (ghost) {
        marker(1, 25, 10);
        marker(1, 26, 12);
    }
    return map;
}

/// The orders of the match: in the ghost match seat 1 drops out at turn 100 (a system command, as the sequencer makes it when a player leaves)
inline std::vector<sim::Command> orders(bool ghost, uint32_t turn) {
    std::vector<sim::Command> out;
    if (ghost && turn == 100) {
        sim::Command drop;
        drop.type = sim::CommandType::Drop;
        drop.issuer = 1;
        out.push_back(drop);
    }
    return out;
}

}  // namespace ants::end_fix_scenario
