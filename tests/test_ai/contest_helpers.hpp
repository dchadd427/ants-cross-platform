#pragma once

// Helpers that the contest tests (contest batch, AI20.x: test_ai_race.cpp, test_ai_contest.cpp; the denial order, AI28.x: test_ai_deny.cpp) share: the attack orders of a rig, a carrier,
// the ants that the harvest orders sent to a pile, the middle pile of TREASURE and the hand-made worlds of the race and of the denial order.
#include <array>
#include <utility>

#include "ai_test.hpp"
#include "b41_helpers.hpp"

namespace contest {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;
using namespace b41;

// The attack orders of a rig (tick of the release, command), in order
inline std::vector<std::pair<uint64_t, Command>> attacks_of(const Rig& rig) {
    std::vector<std::pair<uint64_t, Command>> out;
    for (const auto& e : rig.sent) {
        if (e.second.type == CommandType::GroupAttack) out.push_back(e);
    }
    return out;
}

// A carrier of team `team` standing on `tile` with a bite of food
inline uint32_t carrier_at(sim::SimulationEngine& sim, uint8_t team, TileCoord tile) {
    const uint32_t id = sim.spawn_unit(team, sim::AntType::Worker, tile);
    sim.get_unit(id).pick_up_food(1, 25);
    return id;
}

// The ants that the harvest orders of a rig sent to the pile `pile` (a move onto a power-up tile is a pick-up, not a harvest order) until tick `until`
inline size_t ants_to_pile(const Rig& rig, const sim::SimulationEngine& sim, size_t piles, int pile, uint64_t until) {
    size_t n = 0;
    for (const auto& e : rig.proposed) {
        if (e.first > until || e.second.type != CommandType::GroupMove) continue;
        const TileCoord t = tc(e.second.tile_x, e.second.tile_y);
        if (sim.grid().has_powerup_at(t)) continue;
        if (pile_of_tile(rig.map(), piles, t) == pile) n += e.second.ants.size();
    }
    return n;
}

// The index of the pile in the middle of TREASURE (anchor 30, 29)
inline size_t treasure_centre(const MapInfo& map) {
    for (const PileInfo& p : map.piles()) {
        if (p.anchor.x == 30 && p.anchor.y == 29) return p.index;
    }
    return 0;
}

// A hand-made world for the race: six workers at the hill of team 0, one ant for each enemy team (live), a pile in the middle of the field that every enemy reaches as soon as team 0
// (27, 27: 23 tiles from every hill) and a richer pile next to the own hill (12, 12)
struct RaceWorld {
    int32_t middle{0};
    int32_t near{0};
    void build(sim::SimulationEngine& sim, uint32_t seed = 101, size_t own = 6, uint16_t near_value = 25, uint16_t middle_value = 15) {
        empty_field(sim, seed);
        middle = add_pile(sim, 27, 27, 40, middle_value);
        near = add_pile(sim, 12, 12, 40, near_value);
        for (size_t i = 0; i < own; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + static_cast<int32_t>(i % 4), 9 + static_cast<int32_t>(i / 4)});
        for (uint8_t t = 1; t < 4; ++t) sim.spawn_unit(t, sim::AntType::Worker, TileCoord{kFightHills[t].x + 3, kFightHills[t].y + 6});
    }
};

// A world for the denial order (AI28.x): four hills placed alike in the corners of a 60 x 60 field of grass, a pile in the middle that every hill reaches as soon as the others
// (three enemies compete for it), for every hill a pile on the diagonal towards the middle `reserve_at` tiles from the field's corner that no other hill reaches as soon (the hill's
// reserve), and the workers of every team beside its hill (`workers`, by team; an enemy needs one ant to be a live team). The world is drawn in one of eight orientations (`orient`:
// bit 0 mirrors left and right, bit 1 top and bottom, bit 2 swaps the axes): a team keeps its number and its corner moves with the drawing, so a rule that holds in one orientation
// and not in another, or for one seat and not for another, is a rule about the drawing, not about the game.
struct ReserveWorld {
    static constexpr int32_t kW = 60;
    int orient{0};
    int32_t middle{-1};
    std::array<int32_t, 4> reserve{{-1, -1, -1, -1}};
    std::array<TileCoord, 4> hill{};
    // team `team`'s copy of a footprint of `size` tiles whose top-left tile is `t` at team 0 (mirrored into its corner: team 1 to the right, team 2 below, team 3 across)
    static TileCoord of_team(uint8_t team, TileCoord t, int32_t size) {
        if (team == 1 || team == 3) t.x = kW - size - t.x;
        if (team == 2 || team == 3) t.y = kW - size - t.y;
        return t;
    }
    // the top-left tile of that footprint as this world draws it
    TileCoord map(TileCoord t, int32_t size) const {
        if ((orient & 4) != 0) std::swap(t.x, t.y);
        if ((orient & 1) != 0) t.x = kW - size - t.x;
        if ((orient & 2) != 0) t.y = kW - size - t.y;
        return t;
    }
    // a tile of team `team` (one that is `t` at team 0) as this world draws it: where the tests put an ant
    TileCoord tile_of(uint8_t team, TileCoord t) const { return map(of_team(team, t, 1), 1); }
    // a pile of crackers (a 2 x 2 footprint whose bottom-right cell is the anchor) whose top-left tile is `top_left` as drawn
    static int32_t pile(sim::SimulationEngine& sim, TileCoord top_left, uint16_t units, uint16_t value) { return add_pile(sim, top_left.x + 1, top_left.y + 1, units, value); }
    void build(sim::SimulationEngine& sim, int32_t reserve_at = 19, std::array<size_t, 4> workers = {{6, 1, 1, 1}}, uint16_t middle_value = 25, uint16_t reserve_value = 25,
               uint32_t ticks = 14400, uint32_t seed = 101) {
        sim.init_test_world(kW, kW, seed, ticks * sim::TICK_MS);
        for (uint8_t t = 0; t < 4; ++t) {
            hill[t] = map(of_team(t, TileCoord{4, 4}, 4), 4);
            sim.grid_mut().set_anthill(t, hill[t]);
        }
        middle = pile(sim, map(TileCoord{29, 29}, 2), 40, middle_value);
        for (uint8_t t = 0; t < 4; ++t) reserve[t] = pile(sim, map(of_team(t, TileCoord{reserve_at - 1, reserve_at - 1}, 2), 2), 20, reserve_value);
        for (uint8_t t = 0; t < 4; ++t) {
            for (size_t i = 0; i < workers[t]; ++i) sim.spawn_unit(t, sim::AntType::Worker, tile_of(t, TileCoord{9 + static_cast<int32_t>(i % 3), 5 + static_cast<int32_t>(i / 3)}));
        }
    }
};

}  // namespace contest
