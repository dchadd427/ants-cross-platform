#include "ants_server/test_maps_rooms.hpp"

namespace ants::server {

void connect_test_maps(RoomManager& rooms, TestMapStore& store, TestMapApi& api, std::function<int64_t()> clock) {
    store.set_in_use([&rooms](const std::string& file) { return rooms.map_in_use(file); });          // (a test map that a room has is not dropped to make room for another)
    api.set_on_changed([&rooms, &store, clock]() { rooms.mutable_store().set_test_maps(store.maps(clock())); });          // (a test upload does not read every approved map again)
    rooms.mutable_store().set_test_maps(store.maps(clock()));
    store.take_changed();
}

void upkeep_test_maps(RoomManager& rooms, TestMapStore& store, int64_t now_s, uint32_t now_ms) {
    if (store.upkeep(now_s, now_ms)) rooms.mutable_store().set_test_maps(store.maps(now_s));
}

}  // namespace ants::server
