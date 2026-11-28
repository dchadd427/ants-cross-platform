// The counts of a map by the numbers that the game server's limits of a room look at (online_limits.hpp), for the tests of the server that use them with the big maps of big_map_fixtures.hpp (tests/common).
// Everything is in an unnamed namespace.
#pragma once

#include <cstdint>

#include "ants_server/online_limits.hpp"

namespace {

// The counts of a map by the numbers that the limits of a room look at (cells, starting ants, pairs, food objects, power-ups); 0 for what a test does not look at
[[maybe_unused]] ants::server::MapCounts online_counts(uint64_t cells, uint64_t start_ants = 0, uint64_t pairs = 0, uint64_t food_objects = 0, uint64_t power_ups = 0) {
    return ants::server::MapCounts{cells, start_ants, pairs, food_objects, power_ups};
}

}  // namespace
