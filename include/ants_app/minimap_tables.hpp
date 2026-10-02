#pragma once

// The colours of the minimap, shared by the match screen's minimap (hud.cpp, Ants.exe FUN_01009596) and the map preview of the setup screen (map_preview.hpp): the speckle table of the
// terrain classes, the colour of every object by its tile id (the 215 records of 0x1001c50), the fog colours and the ants' dot colours. Moved here from hud.cpp without a change of
// value, so that the preview paints a map with the very colours the minimap does.

#include <cstddef>
#include <cstdint>

namespace ants::sim {
struct TileCell;
}

namespace ants::app {

// Speckle table of the terrain classes (0x1001c28): 5 palette indices per class 0..5, picked with rand() % 5 by the minimap (the preview takes the first of each: a flat colour)
inline constexpr uint8_t kMinimapClassColours[6][5] = {
    {251, 201, 249, 251, 251},   // 0 gravel
    {235, 235, 235, 235, 235},   // 1 slate
    { 37,  37,  37,  37,  37},   // 2 water
    { 77,  77,  77,  77,  77},   // 3 mud
    {231, 232, 233, 231, 231},   // 4 dirt
    {  0,   0,   0,   0,   0}    // 5 (unused class)
};
// Colours of unexplored cells by class (0x1001c48)
inline constexpr uint8_t kMinimapFogColours[8] = { 244, 237, 225, 245, 236, 0, 0, 0 };
// Ant dot colours by remake player id (green, red, blue, black) = original colour {3,2,1,0} (FUN_0101aa65)
inline constexpr uint8_t kMinimapAntColours[4] = { 47, 158, 211, 239 };

// The colour table of the original (0x1001c50 copied by the constructor FUN_01009056 into a table indexed by the tile id): entry = colour | size << 8. The table is
// cleared first, so a tile id without a record has colour 0 and size 0
inline constexpr size_t kMinimapTileIds = 1344;
uint16_t minimap_object_entry(uint16_t id);

// The layer-2 id of a snapshot cell as the original's painter reads it: the remake keeps a bomb it planted as 100 .. 103 and a dropped power-up as 0x8000 | type, the original
// as 129 .. 132 and the power-up's own tile id
inline constexpr uint16_t kMinimapNoObject = 0x7ffe;
inline constexpr uint16_t kMinimapBomb = 129;
uint16_t minimap_object_id(const sim::TileCell& cell);

// FUN_01008bc6: the four bomb ids 0x81 .. 0x84
bool minimap_is_bomb(uint16_t id);

// Terrain class of a snapshot cell (0 gravel, 1 slate, 2 water, 3 mud, 4 dirt)
uint8_t minimap_class(const sim::TileCell& cell);

}  // namespace ants::app
