#include "ants_server/online_limits.hpp"

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::server {

// One ant for every start marker of a color (team 0 to 3); the records of block 1 that are no marker (a plant, team 255) start no ant. The work is one pass over the records that the loader kept, and the loader's
// limits (assets::LevelLimits) bound how many there are, so a map that a stranger sent costs no more than loading it did.
uint64_t OnlineLimits::start_ants(const assets::LevelData& level) {
    uint64_t ants = 0;
    for (const assets::AnthillSpawn& spawn : level.anthill_spawns()) {
        if (spawn.team_id < 4) ++ants;
    }
    return ants;
}

// The plants (block 1 records whose tile has sim::movement::is_dropper_plant_tile's flag, counted the same pass as SimulationEngine::init makes) times the waypoints (block 4 records the loader kept): the work
// of SimulationEngine::init's lookup. One pass over the records the loader kept, and the loader's limits bound how many there are (at most 65,535 of each), so a map that a stranger sent costs no more to count
// than loading it did; the multiplication cannot overflow a uint64_t (65,535 x 65,535 is about 4.3 billion).
uint64_t OnlineLimits::plant_waypoint_pairs(const assets::LevelData& level) {
    uint64_t plants = 0;
    for (const assets::AnthillSpawn& spawn : level.anthill_spawns()) {
        if (sim::movement::is_dropper_plant_tile(spawn.tile_id)) ++plants;
    }
    return plants * static_cast<uint64_t>(level.waypoints.size());
}

}  // namespace ants::server
