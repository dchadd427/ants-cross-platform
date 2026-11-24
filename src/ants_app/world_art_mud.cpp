// The dense mud of the world art (world_art_mud.hpp): the noise, the depth of the mud, the looks of a cell and the choice of its bubbles.
#include "ants_app/world_art_mud.hpp"

#include <algorithm>
#include <cmath>

namespace ants::app::world_art::mud {

namespace {

constexpr double kPi = 3.14159265358979323846;

double smooth(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// Spreads a noise value's usual range (about .25 to .75) over 0 to 1
double stretch01(double v, double k) { return std::clamp((v - 0.5) * k + 0.5, 0.0, 1.0); }

// The look's numbers, as approved
constexpr double kWetDark[3] = {0.200, 0.220, 0.230};      // the mud's colors times (1 - dark) at full depth
constexpr double kWetVar = 0.10;                           // how much the darkness varies across the middle
constexpr double kSheenAdd[3] = {8.0, 8.5, 10.0};
constexpr double kPatchBrown[3] = {6.0, 1.5, -6.0};
constexpr double kPatchGray[3] = {5.0, 5.0, 5.5};
constexpr double kRippleAmp = 4.6;
constexpr double kRippleSigma = 2.2;
constexpr double kRippleGap = 30.0;
constexpr double kWarp = 70.0;
constexpr double kLightX = -0.55 / 0.8514693182963201;     // the light from the upper left (-0.55, -0.65), normalized
constexpr double kLightY = -0.65 / 0.8514693182963201;

double ripple_field(double x, double y) {                  // the field whose contour lines are the flow lines
    const double xs = x + kWarp * (fbm(x, y, 210.0, 55, 2) - 0.5) * 2.0;
    const double ys = y + kWarp * (fbm(x, y, 210.0, 56, 2) - 0.5) * 2.0;
    return fbm(xs, ys, 300.0, 21, 3, 0.55);
}

int16_t quantize(double v, double unit) { return static_cast<int16_t>(std::lround(v * unit)); }

}  // namespace

// ============================================================================
// The noise
// ============================================================================

double lattice_value(int64_t ix, int64_t iy, int64_t seed) {
    uint64_t n = (static_cast<uint64_t>(ix) * UINT64_C(374761393) + static_cast<uint64_t>(iy) * UINT64_C(668265263) + static_cast<uint64_t>(seed) * UINT64_C(2246822519)) & UINT64_C(0xFFFFFFFF);
    n = ((n ^ (n >> 13)) * UINT64_C(1274126177)) & UINT64_C(0xFFFFFFFF);
    n ^= n >> 16;
    return static_cast<double>(n & UINT64_C(0xFFFFFF)) / 16777216.0;
}

double value_noise(double x, double y, int64_t seed) {
    const double fx0 = std::floor(x), fy0 = std::floor(y);
    const double fx = x - fx0, fy = y - fy0;
    const double u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
    const double v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);
    const int64_t ix = static_cast<int64_t>(fx0), iy = static_cast<int64_t>(fy0);
    const double a = lattice_value(ix, iy, seed), b = lattice_value(ix + 1, iy, seed);
    const double c = lattice_value(ix, iy + 1, seed), d = lattice_value(ix + 1, iy + 1, seed);
    return (a * (1.0 - u) + b * u) * (1.0 - v) + (c * (1.0 - u) + d * u) * v;
}

double fbm(double x, double y, double wavelength, int64_t seed, int32_t octaves, double gain, double stretch0, double stretch1, double angle) {
    const double c = std::cos(angle * kPi / 180.0), s = std::sin(angle * kPi / 180.0);
    const double u0 = (x * c + y * s) / stretch0, w0 = (-x * s + y * c) / stretch1;
    double total = 0.0, amp = 1.0, norm = 0.0;
    for (int32_t o = 0; o < octaves; ++o) {
        const double turn = (33.0 * o + 17.0) * kPi / 180.0;       // every octave on a turned lattice: no feature lines up with the tile grid
        const double ca = std::cos(turn), sa = std::sin(turn);
        const double u = u0 * ca + w0 * sa, w = -u0 * sa + w0 * ca;
        const double scale = std::ldexp(1.0, o);
        total += amp * value_noise(u / wavelength * scale + 17.3 * o, w / wavelength * scale + 5.1 * o, seed + 31 * o);
        norm += amp;
        amp *= gain;
    }
    return total / norm;
}

double ramp(double v, double a, double b) { return smooth((v - a) / (b - a)); }

// ============================================================================
// The depth
// ============================================================================

namespace {
int32_t floor_div(int32_t a, int32_t b) { return static_cast<int32_t>(std::floor(static_cast<double>(a) / static_cast<double>(b))); }
}  // namespace

void Depth::build(int32_t width, int32_t height, const std::vector<uint8_t>& mud) {
    *this = Depth{};
    if (width <= 0 || height <= 0 || mud.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) return;
    width_ = width;
    height_ = height;
    grid_w_ = width * 32 / kGridStep;
    grid_h_ = height * 32 / kGridStep;
    grid_.assign(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_), static_cast<float>(kDepthCap * kTile));
    const auto is_mud = [&](int32_t x, int32_t y) { return x >= 0 && y >= 0 && x < width && y < height && mud[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] != 0; };
    const int32_t reach = static_cast<int32_t>((kDepthCap + 1.0) * kTile);
    for (int32_t cy = 0; cy < height; ++cy) {
        for (int32_t cx = 0; cx < width; ++cx) {
            if (is_mud(cx, cy)) continue;
            bool beside = false;                                    // only a cell beside mud is a bank
            for (int32_t dy = -1; dy <= 1 && !beside; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) beside = beside || is_mud(cx + dx, cy + dy);
            }
            if (!beside) continue;
            const int32_t x0 = cx * 32, x1 = x0 + 32, y0 = cy * 32, y1 = y0 + 32;
            const int32_t ia = std::max(0, floor_div(x0 - reach, kGridStep)), ib = std::min(grid_w_, floor_div(x1 + reach, kGridStep) + 1);
            const int32_t ja = std::max(0, floor_div(y0 - reach, kGridStep)), jb = std::min(grid_h_, floor_div(y1 + reach, kGridStep) + 1);
            for (int32_t j = ja; j < jb; ++j) {
                const double gy = (j + 0.5) * kGridStep;
                const double dy = std::max(std::max(y0 - gy, 0.0), gy - y1);
                for (int32_t i = ia; i < ib; ++i) {
                    const double gx = (i + 0.5) * kGridStep;
                    const double dx = std::max(std::max(x0 - gx, 0.0), gx - x1);
                    float& cell = grid_[static_cast<size_t>(j) * static_cast<size_t>(grid_w_) + static_cast<size_t>(i)];
                    cell = std::min(cell, static_cast<float>(std::hypot(dx, dy)));
                }
            }
        }
    }
    for (float& v : grid_) v = static_cast<float>(static_cast<double>(v) / kTile);
    deepest_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0.0f);
    const int32_t per_cell = 32 / kGridStep;
    for (int32_t j = 0; j < grid_h_; ++j) {
        for (int32_t i = 0; i < grid_w_; ++i) {
            float& d = deepest_[static_cast<size_t>(j / per_cell) * static_cast<size_t>(width) + static_cast<size_t>(i / per_cell)];
            d = std::max(d, grid_[static_cast<size_t>(j) * static_cast<size_t>(grid_w_) + static_cast<size_t>(i)]);
        }
    }
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) any_deep_ = any_deep_ || (is_mud(x, y) && static_cast<double>(deepest(x, y)) + kWander >= kFirstLook);
    }
}

double Depth::at(double x, double y) const noexcept {
    if (grid_.empty()) return 0.0;
    const double u = std::clamp(x / kGridStep - 0.5, 0.0, grid_w_ - 1.001);
    const double v = std::clamp(y / kGridStep - 0.5, 0.0, grid_h_ - 1.001);
    const int32_t i = static_cast<int32_t>(std::floor(u)), j = static_cast<int32_t>(std::floor(v));
    const double fu = u - i, fv = v - j;
    const auto g = [&](int32_t a, int32_t b) { return static_cast<double>(grid_[static_cast<size_t>(b) * static_cast<size_t>(grid_w_) + static_cast<size_t>(a)]); };
    return g(i, j) * (1 - fu) * (1 - fv) + g(i + 1, j) * fu * (1 - fv) + g(i, j + 1) * (1 - fu) * fv + g(i + 1, j + 1) * fu * fv;
}

double Depth::deepest(int32_t col, int32_t row) const noexcept {
    if (col < 0 || row < 0 || col >= width_ || row >= height_) return 0.0;
    return static_cast<double>(deepest_[static_cast<size_t>(row) * static_cast<size_t>(width_) + static_cast<size_t>(col)]);
}

// ============================================================================
// The looks
// ============================================================================

double wandered_depth(double x, double y, double depth) { return depth + kWander * (fbm(x, y, 190.0, 91, 2) - 0.5) * 2.0; }

Look wet_and_patch_at(double x, double y, double depth) {
    Look look;
    const double d = wandered_depth(x, y, depth);
    if (d < kFirstLook) return look;
    if (d >= 1.8) {                                                               // look 1: darker and wetter, with a dull sheen
        const double w = ramp(d, 1.8, 5.0);
        const double var = 1.0 - kWetVar * (1.0 - stretch01(fbm(x, y, 260.0, 7, 3), 3.0));
        for (int k = 0; k < 3; ++k) look.mult[k] = 1.0 - kWetDark[k] * w * var;
        if (d > 3.0) {
            const double sheen = ramp(stretch01(fbm(x, y, 260.0, 13, 3, 0.5, 1.7, 1.0, -24.0), 3.0), 0.45, 0.95) * ramp(d, 3.0, 6.0);
            for (int k = 0; k < 3; ++k) look.add[k] += kSheenAdd[k] * sheen;
        }
    }
    {                                                                             // look 4: broad brown and gray patches
        const double w = ramp(d, kFirstLook, 5.0);
        const double q = stretch01(fbm(x, y, 320.0, 41, 2), 2.4) * 2.0 - 1.0;
        const double brown = ramp(q, 0.0, 0.65), gray = ramp(-q, 0.0, 0.65);
        for (int k = 0; k < 3; ++k) look.add[k] += w * (brown * kPatchBrown[k] + gray * kPatchGray[k]);
    }
    return look;
}

CellLooks CellLooks::compute(const Depth& depth, int32_t col, int32_t row) {
    CellLooks out;
    constexpr int32_t S = kSamples;
    std::vector<double> d(static_cast<size_t>(S) * S);
    double deepest = 0.0;
    for (int32_t b = 0; b < S; ++b) {
        for (int32_t a = 0; a < S; ++a) {
            const double x = col * kTile + (a - 0.5) * 0.5, y = row * kTile + (b - 0.5) * 0.5;
            const double v = depth.at(x, y);
            d[static_cast<size_t>(b) * S + static_cast<size_t>(a)] = v;
            deepest = std::max(deepest, v);
        }
    }
    if (deepest + kWander < kFirstLook) return out;                               // a bank, a small field, a corner: the approved picture
    out.data_.assign(kPlanes * static_cast<size_t>(S) * S, 0);
    constexpr int32_t G = S + 2;                                                  // the ripple field with a margin of two samples (one for the gradient)
    std::vector<double> phi;
    bool ripples = false;
    std::vector<double> wd(static_cast<size_t>(S) * S);
    for (size_t i = 0; i < wd.size(); ++i) {
        const int32_t a = static_cast<int32_t>(i % S), b = static_cast<int32_t>(i / S);
        wd[i] = wandered_depth(col * kTile + (a - 0.5) * 0.5, row * kTile + (b - 0.5) * 0.5, d[i]);
        ripples = ripples || wd[i] >= 1.8;
    }
    if (ripples) {
        phi.resize(static_cast<size_t>(G) * G);
        for (int32_t b = 0; b < G; ++b) {
            for (int32_t a = 0; a < G; ++a) phi[static_cast<size_t>(b) * G + static_cast<size_t>(a)] = ripple_field(col * kTile + (a - 1.5) * 0.5, row * kTile + (b - 1.5) * 0.5);
        }
    }
    const size_t plane = static_cast<size_t>(S) * S;
    for (int32_t b = 0; b < S; ++b) {
        for (int32_t a = 0; a < S; ++a) {
            const size_t i = static_cast<size_t>(b) * S + static_cast<size_t>(a);
            const double x = col * kTile + (a - 0.5) * 0.5, y = row * kTile + (b - 0.5) * 0.5;
            Look look;
            if (wd[i] >= kFirstLook) look = wet_and_patch_at(x, y, d[i]);
            for (int k = 0; k < 3; ++k) {
                out.data_[static_cast<size_t>(k) * plane + i] = quantize(look.mult[k], 4096.0);
                out.data_[static_cast<size_t>(3 + k) * plane + i] = quantize(look.add[k], 16.0);
            }
            if (wd[i] < 1.8) continue;
            // the ripples: the lines of the field, a soft ridge with a lit flank and a shaded one, here and there, fading in and out along their length
            const size_t p = static_cast<size_t>(b + 1) * G + static_cast<size_t>(a + 1);
            const double gx = (phi[p + 1] - phi[p - 1]) / 1.0, gy = (phi[p + G] - phi[p - G]) / 1.0;      // central differences, 0.5 world pixel to a step
            const double g = std::hypot(gx, gy) + 1e-9;
            const double step = kRippleGap * kRippleMedian;
            const double t = phi[p] / step;
            const double sd = (t - std::floor(t) - 0.5) * step / g;                                          // world pixels to the nearest line
            const double nrm = (gx * kLightX + gy * kLightY) / g;                                            // the side that faces the light is lit
            const double side = sd * (nrm > 0 ? 1.0 : nrm < 0 ? -1.0 : 0.0);
            const double xx = side / kRippleSigma;
            const double line = -2.3 * xx * std::exp(-xx * xx);
            const double cover = ramp(fbm(x, y, 340.0, 77, 2), 0.30, 0.46);
            const double broken = ramp(fbm(x, y, 110.0, 63, 2), 0.36, 0.52);
            out.data_[6 * plane + i] = quantize(kRippleAmp * line * cover * broken * ramp(wd[i], 1.8, 5.0), 16.0);
        }
    }
    return out;
}

void CellLooks::at(double u, double v, double mult[3], double add[3], double& ripple) const noexcept {
    for (int k = 0; k < 3; ++k) {
        mult[k] = 1.0;
        add[k] = 0.0;
    }
    ripple = 0.0;
    if (data_.empty()) return;
    constexpr int32_t S = kSamples;
    const double fa = std::clamp(u * 2.0 + 0.5, 0.0, S - 1.001), fb = std::clamp(v * 2.0 + 0.5, 0.0, S - 1.001);
    const int32_t a = static_cast<int32_t>(fa), b = static_cast<int32_t>(fb);
    const double ta = fa - a, tb = fb - b;
    const size_t plane = static_cast<size_t>(S) * S;
    const size_t i = static_cast<size_t>(b) * S + static_cast<size_t>(a);
    const auto sample = [&](size_t p) {
        const double top = data_[p * plane + i] * (1 - ta) + data_[p * plane + i + 1] * ta;
        const double bottom = data_[p * plane + i + S] * (1 - ta) + data_[p * plane + i + S + 1] * ta;
        return top * (1 - tb) + bottom * tb;
    };
    for (size_t k = 0; k < 3; ++k) {
        mult[k] = sample(k) / 4096.0;
        add[k] = sample(3 + k) / 16.0;
    }
    ripple = sample(6) / 16.0;
}

std::vector<uint8_t> apply_looks(const CellLooks& looks, const uint8_t* rgb, const uint8_t* plain, int32_t scale) {
    const int32_t n = 32 * scale;
    std::vector<uint8_t> out(static_cast<size_t>(n) * static_cast<size_t>(n) * 4u, 255);
    for (int32_t j = 0; j < n; ++j) {
        for (int32_t i = 0; i < n; ++i) {
            const size_t p = static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i);
            const uint8_t* src = rgb + p * 3u;
            uint8_t* dst = &out[p * 4u];
            if (looks.identity()) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                continue;
            }
            double mult[3], add[3], ripple = 0.0;
            looks.at((i + 0.5) / scale, (j + 0.5) / scale, mult, add, ripple);
            const bool base = plain != nullptr && src[0] == plain[p * 3u] && src[1] == plain[p * 3u + 1] && src[2] == plain[p * 3u + 2];       // plain mud, not a bubble
            for (int k = 0; k < 3; ++k) dst[k] = static_cast<uint8_t>(std::clamp(std::lround(src[k] * mult[k] + add[k] + (base ? ripple : 0.0)), 0l, 255l));
        }
    }
    return out;
}

// ============================================================================
// The bubbles
// ============================================================================

bool is_mud_sprite(uint32_t sprite) noexcept { return (sprite >= 640 && sprite <= 657) || sprite == 901; }

double cell_hash(int32_t col, int32_t row, int32_t salt) {
    uint64_t n = (static_cast<uint64_t>(col) * UINT64_C(73856093) ^ static_cast<uint64_t>(row) * UINT64_C(19349663) ^ static_cast<uint64_t>(salt) * UINT64_C(83492791)) & UINT64_C(0xFFFFFFFF);
    n = ((n ^ (n >> 13)) * UINT64_C(1274126177)) & UINT64_C(0xFFFFFFFF);
    n ^= n >> 16;
    return static_cast<double>(n & UINT64_C(0xFFFFFF)) / 16777216.0;
}

namespace {

struct Step {
    uint32_t ms;
    uint32_t sprite;
};

// the original's own bubbling time (its template m01e_a: idle, then the bubble's five frames), with the frames of the small bubble or those of the larger one
constexpr Step kCycleSmall[6] = {{1200, 640}, {100, 641}, {100, 642}, {100, 643}, {50, 644}, {50, 645}};
constexpr Step kCycleLarge[6] = {{1200, 640}, {100, 646}, {100, 647}, {100, 648}, {50, 649}, {50, 650}};

}  // namespace

BubbleChoice choose_bubble(int32_t col, int32_t row, uint32_t native, uint32_t clock_ms, double depth) {
    const double wb = ramp(depth, 2.2, 5.0);                                     // 0 until the bubbles begin to change, 1 where they all have
    if (!(wb > 0.0)) return BubbleChoice{native, 0};
    const int32_t size = cell_hash(col, row, 5) < 0.18 + 0.62 * ramp(depth, 4.0, 8.0) ? 2 : 1;
    if (native >= kBubbleFirst && native <= kBubbleLast) return BubbleChoice{native, size};      // the cell's own bubble, grown
    if (native != kPlainMud) return BubbleChoice{native, 0};                      // the original's biggest bursts stay as they are
    if (cell_hash(col, row, 6) < 0.45 * wb) {                                     // a share of the deepest plain cells bubble by themselves
        const Step* cycle = cell_hash(col, row, 7) < 0.6 ? kCycleSmall : kCycleLarge;
        uint32_t total = 0;
        for (int i = 0; i < 6; ++i) total += cycle[i].ms;
        uint32_t t = (clock_ms + static_cast<uint32_t>(cell_hash(col, row, 8) * total)) % total;
        for (int i = 0; i < 6; ++i) {
            if (t < cycle[i].ms) return cycle[i].sprite >= kBubbleFirst ? BubbleChoice{cycle[i].sprite, size} : BubbleChoice{kPlainMud, 0};
            t -= cycle[i].ms;
        }
    }
    return BubbleChoice{kPlainMud, 0};
}

}  // namespace ants::app::world_art::mud
