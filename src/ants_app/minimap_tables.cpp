#include "ants_app/minimap_tables.hpp"

#include <array>

#include "ants_sim/grid.hpp"

namespace ants::app {

namespace {

// Minimap ground truth (Ants.exe FUN_01009596): the colour of every object, palette indices
struct MinimapObject { uint16_t id; uint8_t colour; uint8_t size_flag; };
#include "minimap_tables.inc"

}  // namespace

uint16_t minimap_object_entry(uint16_t id) {
    static const std::array<uint16_t, kMinimapTileIds> table = [] {
        std::array<uint16_t, kMinimapTileIds> t{};
        for (const auto& o : kMinimapObjects) {
            if (o.id < kMinimapTileIds) t[o.id] = static_cast<uint16_t>(o.colour | (o.size_flag << 8));
        }
        return t;
    }();
    return id < kMinimapTileIds ? table[id] : uint16_t{0};
}

uint16_t minimap_object_id(const sim::TileCell& cell) {
    const uint16_t id = cell.interactive_id;
    if (id == sim::TILE_EMPTY || id == 0xFFFFu) return kMinimapNoObject;
    if (id >= sim::BOMB_BLACK && id <= sim::BOMB_GREEN) return kMinimapBomb;
    if ((id & 0x8000u) != 0) {
        switch (cell.powerup_type) {
            case 1:  return sim::PU_BOMBER;
            case 2:  return sim::PU_FIRE;
            case 3:  return sim::PU_THIEF;
            case 4:  return sim::PU_COMBAT;
            case 5:  return sim::PU_SWIMMER;
            default: return kMinimapNoObject;
        }
    }
    return id;
}

bool minimap_is_bomb(uint16_t id) { return id >= 129 && id <= 132; }

uint8_t minimap_class(const sim::TileCell& cell) {
    if (cell.terrain_type == sim::TERRAIN_WATER) return 2;
    switch (cell.surface_type) {
        case sim::SurfaceType::Slate:  return 1;
        case sim::SurfaceType::Water:  return 2;
        case sim::SurfaceType::Mud:    return 3;
        case sim::SurfaceType::Gravel: return 4;
        default:                       return 0;
    }
}

}  // namespace ants::app
