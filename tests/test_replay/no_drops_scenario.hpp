#pragma once

// A small match of game mode 187 made in code, for the tests of the rules number 12 (test_rules_no_drops; docs/GAMEPLAY.md, "187"): a 40 x 40 map of plain ground with no food, a clock of two minutes, no eggs, no hill cell (so the
// start markers stand in for the hills) and two teams of eight ants that begin as special types, as a map's start markers can make them: four Combat ants, a Bomber, a Fire, a Thief and a Swimmer each. The Combat ants are sent
// against the nearest enemy every ten seconds, and every ten seconds one ant that is not a Combat ant walks to the nearest power-up on the map, if there is one. Under the rules before 12 the Combat ants that die leave their power-ups
// on a free neighbour tile and the walkers that take them leave their old ones; under 12 nothing is left on the ground. The match uses no call that the rules number 12 added (the player of a recording sets the rules of its
// number up with replay::begin_match), so tests/test_replay/test_rules_no_drops.cpp can pin what the build before the number made of it under the numbers 11, 10, 7 and 6 (taken by playing this very header against that
// build, not against this one). The orders are made from the engine's own state, so a match under 12 has other orders than one under 11: each recording holds its own.

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace ants::no_drops_scenario {

inline constexpr uint32_t kSeed = 21;
inline constexpr uint8_t kRoster = 0x03;                       // seats 0 and 1
inline constexpr size_t kAntsPerTeam = 8;
inline constexpr uint32_t kOrderEvery = 200;                   // turns (ten seconds) between the orders

/// The own type of the ants of a team in the order of its start markers (the ids are 1 .. 8 for seat 0 and 9 .. 16 for seat 1): the AntType values, Combat 4, Bomber 1, Fire 2, Thief 3, Swimmer 5
inline constexpr uint8_t kTypes[kAntsPerTeam] = {4, 4, 4, 4, 1, 2, 3, 5};

inline assets::LevelData level() {
    assets::LevelData map = assets::blank_level(40, 40, std::vector<std::string>{});
    map.default_minutes = 2;
    map.boundary_param = 0;                                    // (no eggs: a team that loses its last ant is out)
    for (uint8_t team = 0; team < 2; ++team) {
        for (size_t i = 0; i < kAntsPerTeam; ++i) {
            assets::AnthillSpawn m;
            m.tile_id = static_cast<uint16_t>(152 + team);     // (the start marker of the team)
            m.team_id = team;
            m.x = static_cast<uint16_t>(12 + 3 * team);
            m.y = static_cast<uint16_t>(10 + 2 * i);
            m.ant_type = kTypes[i];
            map.anthill_spawns.push_back(m);
        }
    }
    return map;
}

inline int distance(int x0, int y0, int x1, int y1) { return std::abs(x0 - x1) + std::abs(y0 - y1); }

/// The orders of the turn, made from the state of `engine` before the turn is played. At turn 5 of every ten seconds each living Combat ant attacks the tile of the nearest living enemy ant; at turn 105 the first living ant of each
/// team that is not a Combat ant walks to the nearest power-up on the ground (the pick-up of a typed ant leaves its old power-up on a neighbour tile).
inline std::vector<sim::Command> orders(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<sim::Command> out;
    const uint32_t at = turn % kOrderEvery;
    if (at != 5 && at != 105) return out;
    const std::vector<sim::AntSnapshot> ants = engine.get_world_state().ants;
    const sim::Grid& grid = engine.grid();
    for (uint8_t team = 0; team < 2; ++team) {
        if (at == 5) {
            for (const sim::AntSnapshot& mine : ants) {
                if (mine.player_id != team || mine.hp == 0 || mine.type != sim::AntType::Combat) continue;
                const sim::AntSnapshot* best = nullptr;
                int best_distance = 1 << 30;
                for (const sim::AntSnapshot& other : ants) {
                    if (other.player_id == team || other.hp == 0) continue;
                    const int d = distance(other.tile_x, other.tile_y, mine.tile_x, mine.tile_y);
                    if (d < best_distance) { best_distance = d; best = &other; }
                }
                if (best == nullptr) continue;
                sim::Command c;
                c.type = sim::CommandType::GroupAttack;
                c.issuer = team;
                c.ants.push_back(mine.id);
                c.tile_x = static_cast<int16_t>(best->tile_x);
                c.tile_y = static_cast<int16_t>(best->tile_y);
                out.push_back(c);
            }
        } else {
            const sim::AntSnapshot* walker = nullptr;
            for (const sim::AntSnapshot& a : ants) {
                if (a.player_id == team && a.hp > 0 && a.type != sim::AntType::Combat) { walker = &a; break; }
            }
            if (walker == nullptr) continue;
            int best_distance = 1 << 30;
            sim::TileCoord target{-1, -1};
            for (uint32_t y = 0; y < grid.height(); ++y) {
                for (uint32_t x = 0; x < grid.width(); ++x) {
                    const sim::TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                    if (!grid.has_powerup_at(t)) continue;
                    const int d = distance(t.x, t.y, walker->tile_x, walker->tile_y);
                    if (d < best_distance) { best_distance = d; target = t; }
                }
            }
            if (target.x < 0) continue;
            sim::Command c;
            c.type = sim::CommandType::GroupMove;
            c.issuer = team;
            c.ants.push_back(walker->id);
            c.tile_x = static_cast<int16_t>(target.x);
            c.tile_y = static_cast<int16_t>(target.y);
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace ants::no_drops_scenario
