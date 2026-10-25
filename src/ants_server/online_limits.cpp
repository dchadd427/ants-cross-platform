#include "ants_server/online_limits.hpp"

#include "ants_assets/lvl_parser.hpp"

namespace ants::server {

namespace {

// 6000 as "6,000" (the sentence of the map maker's page writes its numbers the same way)
std::string with_commas(uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

}  // namespace

// One ant for every start marker of a color (team 0 to 3); the records of block 1 that are no marker (a plant, team 255) start no ant. The work is one pass over the records that the loader kept, and the loader's
// limits (assets::LevelLimits) bound how many there are, so a map that a stranger sent costs no more than loading it did.
uint64_t OnlineLimits::start_ants(const assets::LevelData& level) {
    uint64_t ants = 0;
    for (const assets::AnthillSpawn& spawn : level.anthill_spawns()) {
        if (spawn.team_id < 4) ++ants;
    }
    return ants;
}

std::string OnlineLimits::too_many_ants_notice(uint64_t start_ants) {
    return "This map starts with " + with_commas(start_ants) + " ants; an online game allows up to " + with_commas(kMaxStartAnts) + ".";
}

}  // namespace ants::server
