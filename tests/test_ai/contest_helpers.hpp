#pragma once

// Helpers that the contest tests (contest batch, AI20.x: test_ai_race.cpp, test_ai_contest.cpp) share: the attack orders of a rig, a carrier, the ants that the harvest orders sent to a pile, the middle pile
// of TREASURE and the hand-made world of the race.
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

}  // namespace contest
