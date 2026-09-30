#pragma once

// The cells a layer-2 object occupies, computed the way Ants.exe does when it places or removes one (FUN_01007a22
// PlaceObject, FUN_0100744f RemoveObject, FUN_01007d59 the frame box, FUN_01007710 the cell mask): the first frame's box
// of the tile's animation, taken from the anchor tile's top-left, gives a rectangle of tiles; a tile of that rectangle
// belongs to the object when its 32 x 32 area, cut with the box and with the first part's image, holds at least one pixel
// that is not the colour key. Used by tools/gen_food_footprints.cpp (which writes src/ants_sim/food_footprints_data.inc)
// and by the test that keeps that file in sync with ants.chd; the simulation reads the generated table.

#include <cstdint>
#include <vector>

#include "ants_assets/asset_archive.hpp"

namespace ants::assets {

struct FootprintOffset {
    int32_t dcol{0};   // columns from the anchor tile
    int32_t drow{0};   // rows from the anchor tile
};

/// Footprint of an object tile relative to its anchor, in row-major order (rows outer, columns inner); empty for a tile
/// without animation frames. The anchor cell itself is not necessarily part of it (the original writes it anyway).
std::vector<FootprintOffset> compute_object_footprint(const AssetArchive& archive, uint32_t tile_id);

} // namespace ants::assets
