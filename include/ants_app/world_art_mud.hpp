#pragma once

// The dense mud of the world art (docs/WORLD_ART.md, "Dense mud"): a big field of mud looks different in its deep middle from its banks. It is darker and wetter, with a dull sheen, faint ripples and
// swirls, broad brown and gray patches, and bubbles that are bigger and more frequent; every look grows with the DEPTH, the distance in tiles from a point to the nearest cell that is not plain mud
// (a bank has depth 0, a point four tiles from every bank has depth 4). The map's border is not a bank, and an object that stands on mud does not count. A look is a function of the world pixel and the
// depth, nothing else, so it crosses tile borders without a seam, repeats nowhere (value noise on a hashed lattice, each octave on a turned lattice) and is the same for every player; each look's weight is
// exactly 0 until its first depth, so a small mud area and the banks keep the approved pictures. It is drawing only: no rule of the game reads any of it.
//
// This is the C++ port of the approved generator; `tools/world_art/mud_fields.py` is the same math in numpy and `tests/data/world_art_mud_golden.txt` holds values of it that test_world_art_mud compares
// this code with. Plain numbers, no SDL. The ripples differ from the approved picture in one way, on purpose: the gradient that places their lines is taken on the grid of the cell's samples (two for each
// world pixel) and not on the approved picture's supersampled array, and the median of its length is the constant kRippleMedian that the reference measured.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ants::app::world_art::mud {

inline constexpr double kTile = 32.0;
inline constexpr double kDepthCap = 7.0;               // depths above this are the same
inline constexpr int32_t kGridStep = 4;                // the depth grid's spacing in world pixels
inline constexpr double kWander = 0.45;                // the depth lines wander by this many tiles at most (no iso-line follows the cell grid)
inline constexpr double kFirstLook = 1.5;              // the depth at which the first look (the patches) begins; a cell that is shallower than this less the wander is the approved picture
inline constexpr double kRippleMedian = 0.001720718;   // the median length of the ripple field's gradient (tools/world_art/mud_reference.py)

// ---- the memory of the dense mud (renderer_world_mud.cpp): what the baked pictures and the looks of cells may take together, and the largest scale that the mud is baked at. A big mud field in view at 2x
// needs about 11 MB; the browser, which a phone runs, gets half of that and the 1x pictures (about 6 MB for the view), the mud's own pictures (7.7 MB at 2x, 1.9 MB at 1x) come on top.
#if defined(__EMSCRIPTEN__)
inline constexpr size_t kCacheLimit = size_t{8} * 1024 * 1024;
inline constexpr int32_t kMaxScale = 1;
#else
inline constexpr size_t kCacheLimit = size_t{16} * 1024 * 1024;
inline constexpr int32_t kMaxScale = 2;
#endif

// ---- the noise (the approved generator's, whole-number hash, doubles)

/// A lattice point's value, 0 to 1, from integers
double lattice_value(int64_t ix, int64_t iy, int64_t seed);
/// Smooth value noise in lattice units, 0 to 1 (a quintic blend of the four lattice points round (x, y))
double value_noise(double x, double y, int64_t seed);
/// Fractal value noise, about 0 to 1 around 0.5. `wavelength` in world pixels of the first octave; `stretch` lengthens the features along `angle` (degrees)
double fbm(double x, double y, double wavelength, int64_t seed, int32_t octaves = 3, double gain = 0.5, double stretch0 = 1.0, double stretch1 = 1.0, double angle = 0.0);
/// 0 up to `a`, 1 from `b`, a smooth step between
double ramp(double v, double a, double b);

// ---- the depth

class Depth {
public:
    /// `mud` holds one byte for each cell of a map of width x height cells, row by row: 1 for a cell of plain mud (any of its bubbling tiles). Nothing else counts as mud.
    void build(int32_t width, int32_t height, const std::vector<uint8_t>& mud);
    bool empty() const noexcept { return grid_.empty(); }
    /// The depth in tiles at a world point, bilinear on the grid of the 4-pixel steps (a point outside the grid takes the nearest grid point's)
    double at(double x, double y) const noexcept;
    /// ... at the center of a cell
    double cell(int32_t col, int32_t row) const noexcept { return at(col * kTile + 16.0, row * kTile + 16.0); }
    /// The deepest point of a cell on the grid; a cell whose deepest point is shallower than kFirstLook less kWander shows the approved picture
    double deepest(int32_t col, int32_t row) const noexcept;
    bool any_deep() const noexcept { return any_deep_; }
    int32_t width() const noexcept { return width_; }
    int32_t height() const noexcept { return height_; }

private:
    int32_t width_{0};
    int32_t height_{0};
    int32_t grid_w_{0};
    int32_t grid_h_{0};
    bool any_deep_{false};
    std::vector<float> grid_;               // depth in tiles at the centers of the grid cells
    std::vector<float> deepest_;            // for every map cell: the largest grid value inside it
};

// ---- the looks at a point

/// What the looks make of the mud's color at one world pixel, without the ripples: its color times `mult` plus `add` (levels). `depth` is the approved depth at the point, the wander not yet added.
struct Look {
    double mult[3]{1.0, 1.0, 1.0};
    double add[3]{0.0, 0.0, 0.0};
};
Look wet_and_patch_at(double x, double y, double depth);
/// The depth that the looks use at a point: the depth and the wander of the lines
double wandered_depth(double x, double y, double depth);

// ---- a cell

/// The looks of one cell, kept small: the smooth ones (the color factor, the levels added) are sampled once for two world pixels (18 x 18 samples, one more all round), the ripples twice for each world pixel
/// (66 x 66, one more all round), so that a picture of any scale reads them without a seam at the cell's border; 6.3 KB a cell. Empty (identity()) for a cell where nothing changes.
class CellLooks {
public:
    static constexpr int32_t kSmooth = 18;             // samples of the smooth looks, 2 world pixels apart, the first at -1
    static constexpr int32_t kRipple = 66;             // samples of the ripples, half a world pixel apart, the first at -0.25 ... : (a - 0.5) / 2
    static constexpr size_t kBytes = 6 * kSmooth * kSmooth + static_cast<size_t>(kRipple) * kRipple;     // what a cell with looks takes
    bool identity() const noexcept { return smooth_.empty(); }
    /// The looks at the world offset (u, v) inside the cell (0 .. 32): bilinear; mult 0 .. 1, add and ripple in levels
    void at(double u, double v, double mult[3], double add[3], double& ripple) const noexcept;
    size_t bytes() const noexcept { return smooth_.size() + ripple_.size(); }
    /// Computes the looks of the cell (col, row) of a map; the work is about 3 ms.
    static CellLooks compute(const Depth& depth, int32_t col, int32_t row);

private:
    std::vector<uint8_t> smooth_;      // [mult r g b, add r g b][row][column]: mult in 1/255, add in 1/4 level with 128 for none
    std::vector<int8_t> ripple_;       // [row][column] in 1/8 level
};

/// The picture of an RGB cell with the looks laid over it: `rgb` is the plain picture of the cell at `scale` (w = 32 * scale square, 3 bytes a pixel), `plain` the plain mud's picture of the same variant (the
/// ripples go on the pixels that are plain mud and not on a bubble), the result is RGBA, opaque
std::vector<uint8_t> apply_looks(const CellLooks& looks, const uint8_t* rgb, const uint8_t* plain, int32_t scale);

// ---- the bubbles

/// The mud sprites of the original (the plain mud, its bubble frames and the biggest bursts) and the two sets of bubble frames that grow
inline constexpr uint32_t kPlainMud = 640;
inline constexpr uint32_t kBubbleFirst = 641;
inline constexpr uint32_t kBubbleLast = 650;
/// Whether a sprite number is a picture of mud (640 .. 657 and 901)
bool is_mud_sprite(uint32_t sprite) noexcept;

/// A number from 0 to 1 that belongs to a cell and a salt (a hash)
double cell_hash(int32_t col, int32_t row, int32_t salt);

struct BubbleChoice {
    uint32_t sprite{0};                 // the sprite number whose picture is shown (a frame of the bubble, or the plain mud)
    int32_t size{0};                    // 0: the approved picture, 1: the medium bubble, 2: the large one
};

/// What a cell of plain mud shows at the template clock `clock_ms`: `native` is the sprite of the cell's own animation now; `depth` the depth at its center. Deep cells have bigger bubbles, and a share of the
/// deepest plain cells bubble by themselves on the original's bubbling time (1,600 ms: 1,200 idle, then five frames) from a start of their own. The biggest bursts and everything else stay as they are.
BubbleChoice choose_bubble(int32_t col, int32_t row, uint32_t native, uint32_t clock_ms, double depth);

}  // namespace ants::app::world_art::mud
