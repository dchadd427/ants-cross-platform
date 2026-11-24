#pragma once

// The world art: the new pictures of what lies on the map (docs/WORLD_ART.md), as sprite sheets that tools/world_art makes and assets/world_art/ holds. The original's pictures stay: the setting "HUD art"
// (New, the default, or Classic: docs/VIEW_AND_HUD.md) chooses between this set and the original's own sprites, which Classic draws exactly as before. Look only: nothing here reaches the simulation, the
// rules or a replay. No SDL in this header: the manifest, the choice of the sheet and the choice of a ground tile's picture are plain numbers, so that a test can run them.
//
// A picture REPLACES one sprite of the original, by the sprite's number: wherever the game would draw sprite N (in a tile's animation, in a bridge's) it draws the picture of N when the set has one, at the
// same place (the animation's own offsets) and in the same size, so the animations, their times and their footprints are the original's. A picture that does not fit (another size than the sprite's times
// the scale) is not used; the sprite is drawn as the original has it. A plain ground tile has sixteen pictures (the variants): which one a cell shows is ground_variants().
//
// The files: `world_art.txt` (the manifest, one record a line) and the PNG sheets of its PAGES. A page is a set of sheets that holds the pictures of one kind of thing, so that no sheet is above 4096 pixels and a
// page is a texture only when something of it is drawn: page 0 is the ground, the bridge and the effects (batches 1 to 3), page 1 the grass and the plants. Every page has a sheet for each scale, 1x, 2x and 3x
// (the same drawing at 1, 2 and 3 pixels for each world pixel). The records:
//     ants-world-art 2
//     sheet <page> <scale> <png file> <width> <height>                               three rows for each page, in order: page 0 scale 1, 2, 3, page 1 scale 1, 2, 3 ...; the size of that file in pixels
//     sprite <number> <variant> <page> <w> <h> <x1> <y1> <x2> <y2> <x3> <y3>        one for each picture, sorted by number and variant
// `w` and `h` are the original sprite's size at 1x; the picture at scale s is (w * s) x (h * s) pixels with its top left at (xs, ys) of the sheet of its page at that scale and a ring of one pixel round it that
// repeats its own edge (so that a filtered copy never takes a pixel of a neighbor).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ants_app/png_decode.hpp"

namespace ants::app::world_art {

/// How many pictures a plain ground tile has: the plain one (variant 0) and fifteen more
inline constexpr int32_t kVariants = 16;
/// The scales of the sheets
inline constexpr std::array<int32_t, 3> kScales = {1, 2, 3};
/// How many pages a manifest may have, and the page of the ground (the tiles' pictures, the bridge, the effects: what the mud's and the edges' own drawing cuts its colors from)
inline constexpr int32_t kMaxPages = 4;
inline constexpr int32_t kGroundPage = 0;

struct Picture {
    uint32_t number{0};                 // the original's sprite number
    uint8_t variant{0};                 // 0: the plain picture, 1 .. 15: the others of a plain ground tile
    uint8_t page{0};                    // the page whose sheets hold it
    int32_t w{0};                       // the original sprite's size at 1x
    int32_t h{0};
    std::array<int32_t, 3> x{};         // the picture's top left corner in its page's sheet of scale 1, 2 and 3
    std::array<int32_t, 3> y{};
};

class Pack {
public:
    /// Reads the manifest text. False (what() says why) when it is not one: another header, a record that is none of the two kinds, a sheet row missing, repeated or out of order, a sprite that is repeated, is
    /// on a page that is not there or does not lie inside its sheet with its ring at some scale, a size or a number that cannot be a sprite's
    bool parse(const std::string& manifest);
    /// Reads `<folder>/world_art.txt` and looks at the header of each sheet (it must be there, 8-bit RGBA, the size the manifest states); the sheets are decoded when a texture needs them (sheet_pixels)
    bool load(const std::string& folder);
    const std::string& what() const noexcept { return what_; }
    bool loaded() const noexcept { return loaded_; }
    const std::string& folder() const noexcept { return folder_; }

    const std::vector<Picture>& pictures() const noexcept { return pictures_; }
    /// The picture of a sprite: the one of that variant, else the plain one (variant 0), else none
    const Picture* find(uint32_t number, uint8_t variant) const noexcept;
    int32_t page_count() const noexcept { return pages_; }
    const std::string& sheet_file(int32_t page, int32_t scale) const;
    int32_t sheet_width(int32_t page, int32_t scale) const noexcept { return in_range(page, scale) ? sheets_[index(page, scale)].w : 0; }
    int32_t sheet_height(int32_t page, int32_t scale) const noexcept { return in_range(page, scale) ? sheets_[index(page, scale)].h : 0; }

    /// The pixels of the sheet of a page at a scale, read and decoded from its file. False (what() does not change; the caller says) when the file is missing or damaged or is not the size the manifest states
    bool sheet_pixels(int32_t page, int32_t scale, DecodedImage& out) const;

    /// A number that changes with every manifest that was read (a renderer that keeps textures of a set knows it by this, not by the address)
    uint64_t generation() const noexcept { return generation_; }

private:
    std::string what_;
    std::string folder_;
    bool loaded_{false};
    uint64_t generation_{0};
    struct Sheet {
        std::string file;
        int32_t w{0};
        int32_t h{0};
    };
    bool in_range(int32_t page, int32_t scale) const noexcept { return page >= 0 && page < pages_ && scale >= 1 && scale <= 3; }
    static size_t index(int32_t page, int32_t scale) noexcept { return static_cast<size_t>(page) * 3u + static_cast<size_t>(scale - 1); }
    int32_t pages_{0};
    std::array<Sheet, static_cast<size_t>(kMaxPages) * 3u> sheets_{};          // page by page: the sheet of scale 1, 2 and 3
    std::vector<Picture> pictures_;
    std::unordered_map<uint64_t, size_t> index_;     // (number << 8 | variant) -> position in pictures_
};

/// What the choice of a ground tile's picture reads from a map: for every cell (row by row) the first letter of its tile's name in lower case (the kind of ground: g gravel, d dirt, s slate, m mud, w water;
/// 0 for a cell that is not one of the ground tiles 431 .. 669), whether the tile is a plain one (its name's second and third character are "01": g01a, M01b, d01c ...) and whether the map has an object on it
/// (its second layer is not empty)
struct GroundMap {
    int32_t width{0};
    int32_t height{0};
    std::vector<char> kind;
    std::vector<uint8_t> plain;
    std::vector<uint8_t> object;
};

/// Which of its sixteen pictures each cell of plain ground shows (0 for every other cell). It reads the map as loaded and the cell's own place, nothing else (no clock, no simulation state, no machine: whole-number
/// arithmetic), so the same map is drawn the same way by every game. The classes: calm (pictures 0 .. 5), stony (6 .. 8), damp beside another kind of ground (9 .. 11), worn and pale in a corridor or a pocket
/// (12, 13), cracked or dark (14, 15). The rule is the one that the approved pictures were made with: a cell beside another kind (the distance to its own kind's edge is one) is damp or stony; in a corridor
/// or a pocket (two cells or less across) worn or cracked; scuffed round the objects; broad patches of stony, pale and cracked ground; and in the open plain a calm one, one cell in fourteen stony.
std::vector<uint8_t> ground_variants(const GroundMap& map);

}  // namespace ants::app::world_art
