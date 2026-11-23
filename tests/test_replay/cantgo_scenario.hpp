#pragma once

// A small match of the original's game made in code, for the tests of the rules number 14 (test_rules_cantgo; docs/GAME_REVERSE_ENGINEERING.md 5.40, "The gap before the path manager's answer"): a 40 x 40 map of plain
// ground, a clock of one minute, a hill for each team (team 0's in the north-west corner, inside a closed ring of solid cells that no walk gets through; team 1's in the south-east, with the Thief, in a pocket of its own) and no egg. Seat 0 has one Thief,
// seat 1 one Worker that stays away. The Thief raids team 1's hill (the order at turn 5 is a click on the hill), stands on the raid tile when the clip is over, and at turn 150 is ordered into its own hill, which
// no walk reaches: "Can't go there.", the can't-go clip, and at its end the engine sends it to the idle tile of its own hill, which no walk reaches either, again and again (the loop of the original,
// docs/GAME_REVERSE_ENGINEERING.md 5.40). From turn 160 on, every turn, the seat orders the Thief to a free tile beside the raid tile, but at turn 199 to a tile inside the ring that no walk reaches (the manager refuses it in that very tick, so that the state at the hash of turn 200 has a refusal that the rules from 14 hold). With the path manager at the end of the tick (the rules numbers before 14) no
// order of those meets the Thief between the end of a clip and the manager's answer, the loop never ends and the Thief is still on the raid tile when the clock runs out; from 14 the first turn that finds the Thief
// without a path takes the order and it walks to the tile and stays. The match uses no call that the rules number 14 added (the player of a recording sets the rules of its number up with replay::begin_match), so
// tests/test_replay/test_rules_cantgo.cpp can pin what the build before the number made of it under the number 13 (taken by playing this very header against that build, not against this one). The orders come
// at fixed turns, so a match under either number holds the same ones.

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace ants::cantgo_scenario {

inline constexpr uint32_t kSeed = 17;
inline constexpr uint8_t kRoster = 0x03;                       // seats 0 and 1
inline constexpr uint32_t kMaxTurns = 1200;                    // (one minute)
inline constexpr uint32_t kRaidOrder = 5;                      // the click on team 1's hill
inline constexpr uint32_t kHomeOrder = 150;                    // the click on the own hill, which no walk reaches
inline constexpr uint32_t kFreeFrom = 160;                     // from here on, every turn, the order to the free tile
inline constexpr uint32_t kShutInOrder = 199;                  // the last turn before the hash at turn 200: the order to a tile of the ring's inside that no walk reaches, which the manager refuses in this very tick
inline constexpr sim::TileCoord kOwnHill{4, 4};                // team 0's hill: the mound 4..7, the entrance (5, 5), the idle tile (8, 8)
inline constexpr sim::TileCoord kEnemyHill{30, 30};            // team 1's hill: the mound 30..33, the raid tile (33, 32)
inline constexpr sim::TileCoord kRaidTile{33, 32};
inline constexpr sim::TileCoord kFree{37, 34};                 // a tile that the Thief reaches from the raid tile
inline constexpr sim::TileCoord kShutIn{3, 3};                 // a free tile inside team 0's ring

inline assets::LevelData level() {
    assets::LevelData map = assets::blank_level(40, 40, std::vector<std::string>{});
    map.default_minutes = 1;
    map.boundary_param = 0;                                    // (no egg: a side is its ants)
    const auto cell = [&](int32_t x, int32_t y) { return static_cast<size_t>(y) * map.width + static_cast<size_t>(x); };
    const auto ring = [&](int32_t cx, int32_t cy, int32_t radius) {            // a closed ring of solid cells at Chebyshev distance `radius` from the centre: what it holds cannot be reached from outside
        for (int32_t y = cy - radius; y <= cy + radius; ++y) {
            for (int32_t x = cx - radius; x <= cx + radius; ++x) {
                if (x < 0 || y < 0 || x >= static_cast<int32_t>(map.width) || y >= static_cast<int32_t>(map.height)) continue;
                if (std::max(std::abs(x - cx), std::abs(y - cy)) == radius) map.layer1_terrain[cell(x, y)].flags = static_cast<uint16_t>(map.layer1_terrain[cell(x, y)].flags | 1u);
            }
        }
    };
    ring(6, 6, 5);                                             // round team 0's hill and its idle tile: the entrance and the idle tile cannot be reached from outside
    ring(32, 31, 7);                                           // round team 1's hill and the Thief's side: a pocket of 169 tiles, which one slice of the path manager searches whole, so that a refused walk is answered by the first run
    const auto hill = [&](uint8_t team, sim::TileCoord at) {   // (a hill cell of each team: without hill cells the start markers would become the hills)
        auto& h = map.layer2_interactive[cell(at.x, at.y)];
        h.tile_index = static_cast<uint16_t>(248 - team);
        h.flags = 0;
        h.properties = static_cast<uint16_t>((at.x << 8) | at.y);
    };
    hill(0, kOwnHill);
    hill(1, kEnemyHill);
    const auto marker = [&](uint8_t team, uint16_t x, uint16_t y, sim::AntType type) {
        assets::AnthillSpawn m;
        m.tile_id = static_cast<uint16_t>(152 + team);         // (the start marker of the team)
        m.team_id = team;
        m.x = x;
        m.y = y;
        m.ant_type = static_cast<uint8_t>(type);
        map.anthill_spawns.push_back(m);
    };
    marker(0, 36, 33, sim::AntType::Thief);                    // seat 0's Thief, three tiles from the raid tile
    marker(1, 25, 6, sim::AntType::Worker);                    // and seat 1's Worker far away
    return map;
}

/// The orders of the turn: the click on team 1's hill, the click on the own hill, and from turn kFreeFrom every turn the order to the free tile
inline std::vector<sim::Command> orders(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<sim::Command> out;
    if (turn != kRaidOrder && turn != kHomeOrder && turn < kFreeFrom) return out;
    uint32_t thief = 0;
    for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
        if (a.hp != 0 && a.player_id == 0 && a.type == sim::AntType::Thief) thief = a.id;
    }
    if (thief == 0) return out;
    sim::Command c;
    c.type = sim::CommandType::GroupMove;
    c.issuer = 0;
    c.ants.push_back(thief);
    const sim::TileCoord to = turn == kRaidOrder ? sim::TileCoord{kEnemyHill.x + 1, kEnemyHill.y + 1} : turn == kHomeOrder ? sim::TileCoord{kOwnHill.x + 1, kOwnHill.y + 1} : turn == kShutInOrder ? kShutIn : kFree;
    c.tile_x = static_cast<int16_t>(to.x);
    c.tile_y = static_cast<int16_t>(to.y);
    out.push_back(std::move(c));
    return out;
}

/// Where the Thief of seat 0 stands (its tile), or {-1, -1} when it is gone
inline sim::TileCoord thief_tile(const sim::SimulationEngine& engine) {
    for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
        if (a.hp != 0 && a.player_id == 0 && a.type == sim::AntType::Thief) return sim::TileCoord{a.tile_x, a.tile_y};
    }
    return sim::TileCoord{-1, -1};
}

}  // namespace ants::cantgo_scenario
