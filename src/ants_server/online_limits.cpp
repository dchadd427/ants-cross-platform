#include "ants_server/online_limits.hpp"

#include "ants_assets/lvl_parser.hpp"

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

}  // namespace ants::server
