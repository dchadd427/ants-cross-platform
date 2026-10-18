#pragma once

// A small picture of a map for the owner's review page and for the lobby's list (docs/MAP_SUBMISSIONS.md, "Pictures"): the same colors that the map maker's canvas uses (web/front/mapmaker_tiles.js:
// a color for each kind of ground and a few for the kinds of object, the four teams' hills marked), not the game's own pictures. A PNG with a palette and uncompressed blocks, written here
// without a library: it is a few kilobytes, and a map is read once.

#include <cstdint>
#include <string>

namespace ants::assets {
class LevelData;
}

namespace ants::server {

/// The PNG of the level, at most `max_side` pixels on its longer side (a small map is enlarged by a whole factor, a big one is sampled). Empty when the level has no cells.
std::string render_preview_png(const assets::LevelData& level, uint32_t max_side = 128);

}  // namespace ants::server
