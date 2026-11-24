#pragma once

// The automatic transitions of the world art (docs/WORLD_ART.md, "Transitions"): where a map puts two plain tiles of different kinds of ground side by side with no edge tile between them, the New art draws a
// transition INSIDE the lower kind's cell along the join, in the style of that pair's own edge pieces; Classic keeps the original's hard line. Drawing only: the terrain under every cell is unchanged (a water cell
// stays water), so movement and the simulation do not change, and the transition is ground: it lies under every object, ant and fog.
//
// The kinds are ranked water < mud < gravel < dirt < slate; at a join the higher kind reaches a few pixels into the lower kind's cell. Against water it is a bank (steps of the land's own color, getting darker and
// wetter, a lit lip on the side that faces the light in the upper left, a shaded face on the other, and the bank's shadow on the water); between mud, gravel and dirt it is soft contour steps in the original's own
// band colors for the pair; slate against anything is rock (lumps with the original's grey rim and dark outline). The line's width changes with the place in the world (bumps, wide and narrow stretches, a spit
// here and an inlet there), and its corners are rounded unevenly: it comes from the Gaussian-blurred shape of the cells round (the eight neighbors), not from fixed tiles. Nothing is drawn at the map's border.
// On a grid (a cell boxed in by the higher kind, or on a checkerboard's rim) the edge is thin and straight along each joined side, nothing joined across a diagonal, so boards stay crisp grids of whole cells.
//
// Plain numbers, no SDL: the arithmetic is the approved picture's (tools in the plans of the art), in doubles, deterministic. Only plain tiles take part: an edge tile of the original is not a kind (its
// neighbors that are plain tiles of another pair meet it in a hard line, as before).

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ants::app::world_art {
struct GroundMap;
}

namespace ants::app::world_art::edges {

// The memory of the transitions (renderer_world_edges.cpp): what the pictures of the cells that have one may take, and the largest scale they are made at (a shore cell is 4, 16 or 37 KB at 1x, 2x, 3x;
// the browser keeps less and makes them at 2x at most)
#if defined(__EMSCRIPTEN__)
inline constexpr size_t kCacheLimit = size_t{4} * 1024 * 1024;
inline constexpr int32_t kMaxScale = 2;
#else
inline constexpr size_t kCacheLimit = size_t{8} * 1024 * 1024;
inline constexpr int32_t kMaxScale = 3;
#endif

inline constexpr uint8_t kWater = 0, kMud = 1, kGravel = 2, kDirt = 3, kSlate = 4;
inline constexpr uint8_t kNoKind = 255;
inline constexpr size_t kKinds = 5;

/// What the transitions read from a map: the kind of each cell (kWater .. kSlate for a plain tile of that kind, kNoKind for anything else)
struct EdgeMap {
    int32_t width{0};
    int32_t height{0};
    std::vector<uint8_t> kind;
    uint8_t at(int32_t col, int32_t row) const noexcept { return col < 0 || row < 0 || col >= width || row >= height ? kNoKind : kind[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(col)]; }
};

/// The kinds from the map the renderer keeps (world_art::GroundMap: the first letter of a tile's name, whether it is a plain tile): w, m, g, d, s of a plain tile
EdgeMap edge_map_of(const GroundMap& ground);

/// The pictures of the plain tiles that the higher kind shows inside its bank (mud, gravel, dirt, slate: index kMud .. kSlate; water is never the higher kind), RGB at the scale being made, (32 * scale)^2 pixels
struct EdgeTextures {
    std::array<const uint8_t*, kKinds> rgb{};
};

/// Whether a cell has a transition at all: it is a plain tile and one of its eight neighbors is a plain tile of a higher kind
bool has_transition(const EdgeMap& map, int32_t col, int32_t row) noexcept;

/// Whether a cell's pattern is a grid's (its edges are thin): at least three of its four sides are joins with a higher kind, or two sides are and, across two of its diagonals, a cell of its own kind whose two
/// cells between are both of a higher kind (a checkerboard's rim)
bool grid_like(const EdgeMap& map, int32_t col, int32_t row) noexcept;

/// The transition of the cell (col, row) at a scale (1, 2 or 3): RGBA, (32 * scale)^2 pixels, empty when the cell has none. It is laid over the cell's ground picture. It reads the cells within two of (col, row) only.
std::vector<uint8_t> make_overlay(const EdgeMap& map, int32_t col, int32_t row, int32_t scale, const EdgeTextures& textures);

}  // namespace ants::app::world_art::edges
