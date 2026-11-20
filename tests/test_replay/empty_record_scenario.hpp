#pragma once

// A small match of the original's game made in code, for the tests of the rules number 13 (test_rules_empty_record; docs/GAME_REVERSE_ENGINEERING.md 5.40, "A lunchbox on the anchor of an emptied record"): a 40 x 40
// map of plain ground with no egg, a clock of two minutes, and two food objects that the map file puts on ONE tile, (20, 20), the way a lunchbox falls on the anchor of an emptied one: the first holds one unit and the
// second three, each unit worth 20 points, and both keep showing a lunchbox at 0 units, so the tile stays a food tile when the first is empty (a stage list may end with a tile at threshold 0). Seat 0 has one Worker
// that stays away, seat 1 six Workers that are ordered onto the tile one after the other. The original takes every bite from the FIRST object of the tile, whatever it holds: its one unit goes with the first bite and
// every later bite takes nothing from it, so the second object is never touched and every Worker that is ordered onto the tile gets its 20 points. From the rules number 13 a bite takes the first object that has a unit
// left: the first Worker empties the first object, the next three the second, and the last two find the tile empty and just stop. The match uses no call that the rules number 13 added (the player of a recording
// sets the rules of its number up with replay::begin_match), so tests/test_replay/test_rules_empty_record.cpp can pin what the build before the number made of it under the number 9 (taken by playing this very
// header against that build, not against this one). The orders come at fixed turns, so a match under either number holds the same ones.

#include "ants_assets/lvl_parser.hpp"
#include "ants_assets/lvl_writer.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ants::empty_record_scenario {

inline constexpr uint32_t kSeed = 31;
inline constexpr uint8_t kRoster = 0x03;                       // seats 0 and 1
inline constexpr int32_t kTileX = 20;                          // the anchor of both objects
inline constexpr int32_t kTileY = 20;
inline constexpr uint16_t kPoints = 20;                        // what each unit is worth
inline constexpr uint16_t kUnitsFirst = 1;                     // the units of the first object and of the second
inline constexpr uint16_t kUnitsSecond = 3;
inline constexpr size_t kBiters = 6;                           // the Workers of seat 1: more than the objects hold together
inline constexpr uint32_t kFirstOrder = 20;                    // the turn of the first Worker's order (then one every kOrderEvery)
inline constexpr uint32_t kOrderEvery = 150;
inline constexpr uint32_t kMaxTurns = 2400;                    // (two minutes)
inline constexpr uint16_t kNewsGotFood = 60;                   // text 0x3c "Got Food!"

inline assets::LevelData level() {
    assets::LevelData map = assets::blank_level(40, 40, std::vector<std::string>{});
    map.default_minutes = 2;
    map.boundary_param = 0;                                    // (no egg: a side is its ants)
    const auto marker = [&](uint8_t team, uint16_t x, uint16_t y, uint8_t type) {
        assets::AnthillSpawn m;
        m.tile_id = static_cast<uint16_t>(152 + team);         // (the start marker of the team; the first one of a team is its hill)
        m.team_id = team;
        m.x = x;
        m.y = y;
        m.ant_type = type;
        map.anthill_spawns.push_back(m);
    };
    marker(0, 4, 4, 0);                                        // seat 0: one Worker, and its hill
    marker(1, 8, 30, 0);                                       // seat 1: six Workers (the first marker is the team's hill)
    for (uint16_t i = 1; i < kBiters; ++i) marker(1, static_cast<uint16_t>(8 + 2 * i), 30, 0);
    const auto object = [&](uint16_t units) {                  // (stage tile 356, the lunchbox, is one of the tiles that every map loads; it stays at 0 units)
        assets::FoodSchedule f;
        f.x = static_cast<uint16_t>(kTileX);
        f.y = static_cast<uint16_t>(kTileY);
        f.initial_delay = units;
        f.respawn_interval = kPoints;
        f.variants.push_back(assets::FoodItemVariant{units, 356});
        f.variants.push_back(assets::FoodItemVariant{0, 356});
        map.food_schedules.push_back(f);
    };
    object(kUnitsFirst);
    object(kUnitsSecond);
    return map;
}

/// The orders of the turn: the k-th Worker of seat 1 is sent onto the tile at turn kFirstOrder + k * kOrderEvery
inline std::vector<sim::Command> orders(const sim::SimulationEngine& engine, uint32_t turn) {
    std::vector<sim::Command> out;
    if (turn < kFirstOrder || (turn - kFirstOrder) % kOrderEvery != 0 || (turn - kFirstOrder) / kOrderEvery >= kBiters) return out;
    const size_t want = (turn - kFirstOrder) / kOrderEvery;
    size_t k = 0;
    for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
        if (a.player_id != 1 || a.type != sim::AntType::Worker) continue;
        if (k++ != want) continue;
        sim::Command c;
        c.type = sim::CommandType::GroupMove;
        c.issuer = 1;
        c.ants.push_back(a.id);
        c.tile_x = static_cast<int16_t>(kTileX);
        c.tile_y = static_cast<int16_t>(kTileY);
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace ants::empty_record_scenario
