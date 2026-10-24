#pragma once

// How the test maps meet the server's rooms (docs/MAP_SUBMISSIONS.md, "Test maps"): the rooms' map store learns of every change of the held maps, a map that a room has is never dropped to make room for
// another, and the maps that are six hours old go within a minute whether anybody uploads or not. In one place, and not in main(), so that a test can hold it (test_test_maps TM3.1).

#include <cstdint>
#include <functional>

#include "ants_server/room_manager.hpp"
#include "ants_server/test_maps.hpp"
#include "ants_server/test_maps_http.hpp"

namespace ants::server {

/// Wires `store` and `api` to `rooms`: the store asks the rooms whether a map is in use (TestMapStore::set_in_use), the door tells the rooms' store of every change, and the rooms start with the held maps.
/// `clock` is the wall clock in seconds (std::time in the server).
void connect_test_maps(RoomManager& rooms, TestMapStore& store, TestMapApi& api, std::function<int64_t()> clock);

/// Called on every pass of the server's loop: once a minute (TestMapStore::kUpkeepEveryMs) the maps that are too old are dropped, and the rooms' store follows when that changed the set.
void upkeep_test_maps(RoomManager& rooms, TestMapStore& store, int64_t now_s, uint32_t now_ms);

}  // namespace ants::server
