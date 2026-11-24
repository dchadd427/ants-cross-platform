// The automatic transitions of the world art (world_art_edges.hpp): which cells have one, and the picture of one.
#include "ants_app/world_art_edges.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "ants_app/world_art.hpp"

namespace ants::app::world_art::edges {

namespace {

constexpr double kSigma = 3.2;                          // the blur of the cells' shape, in world pixels: it rounds the corners and gives the distance from a join
constexpr int32_t kTable = 561;                         // the blurred edge of a straight join, 14 pixels in to 14 pixels out, to read a distance from
constexpr double kMapWidth = 32.0;

struct Rgb {
    int r, g, b;
};

// The land's own color (the bank's steps are this times 0.84, 0.66 and 0.50); index by kind
constexpr Rgb kBase[kKinds] = {{0, 0, 0}, {75, 75, 75}, {143, 119, 111}, {219, 207, 199}, {150, 148, 177}};
constexpr Rgb kWaterShadow = {17, 40, 87};
// the original's band colors from the higher kind outward, for the soft pairs (lower, higher); slate's rim from the rock outward: a light step, the grey and the dark outline
constexpr Rgb kMudGravel[2] = {{99, 99, 99}, {91, 87, 75}};
constexpr Rgb kGravelDirt[3] = {{179, 167, 155}, {159, 151, 135}, {139, 139, 115}};
constexpr Rgb kMudDirt[5] = {{171, 171, 171}, {147, 147, 147}, {127, 127, 127}, {99, 99, 99}, {91, 87, 75}};
constexpr Rgb kRock[3] = {{147, 147, 147}, {127, 127, 127}, {99, 99, 99}};

struct Band {
    const Rgb* colors;
    int count;
};

Band soft_band(uint8_t lo, uint8_t hi) {
    if (lo == kMud && hi == kGravel) return {kMudGravel, 2};
    if (lo == kGravel && hi == kDirt) return {kGravelDirt, 3};
    return {kMudDirt, 5};                                // mud against dirt: the only other soft pair
}

double clamp(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

/// Smooth noise in 0 .. 1 that depends only on the place in the world, so that neighboring cells agree
double world_noise(double wx, double wy, double period, uint64_t seed) {
    const double fx = wx / period, fy = wy / period;
    const double fx0 = std::floor(fx), fy0 = std::floor(fy);
    double tx = fx - fx0, ty = fy - fy0;
    tx = tx * tx * (3.0 - 2.0 * tx);
    ty = ty * ty * (3.0 - 2.0 * ty);
    const auto h = [&](double a, double b) {
        uint64_t v = (static_cast<uint64_t>(static_cast<int64_t>(a)) * UINT64_C(0x9E3779B1) ^ (static_cast<uint64_t>(static_cast<int64_t>(b)) + UINT64_C(0x7F4A7C15)) * UINT64_C(0x85EBCA6B) ^ seed * UINT64_C(0xC2B2AE35)) & UINT64_C(0xFFFFFFFF);
        v ^= v >> 15;
        v = (v * UINT64_C(0x2C1B3C6D)) & UINT64_C(0xFFFFFFFF);
        v ^= v >> 12;
        v = (v * UINT64_C(0x297A2D39)) & UINT64_C(0xFFFFFFFF);
        v ^= v >> 15;
        return static_cast<double>(v & UINT64_C(0xFFFF)) / 65535.0;
    };
    return (h(fx0, fy0) * (1 - tx) + h(fx0 + 1, fy0) * tx) * (1 - ty) + (h(fx0, fy0 + 1) * (1 - tx) + h(fx0 + 1, fy0 + 1) * tx) * ty;
}

struct Profile {
    std::array<double, kTable> distance;                // 14 down to -14 pixels
    std::array<double, kTable> blurred;                 // the blurred edge there: increasing
    Profile() {
        for (int32_t i = 0; i < kTable; ++i) {
            distance[static_cast<size_t>(i)] = 14.0 - 28.0 * i / (kTable - 1);
            blurred[static_cast<size_t>(i)] = 0.5 * std::erfc(distance[static_cast<size_t>(i)] / (kSigma * std::sqrt(2.0)));
        }
    }
    /// About how many pixels into the lower cell a sample is, from the blurred higher land (it rounds the corners)
    double distance_of(double tot) const noexcept {
        if (tot <= blurred.front()) return distance.front();
        if (tot >= blurred.back()) return distance.back();
        const size_t hi = static_cast<size_t>(std::upper_bound(blurred.begin(), blurred.end(), tot) - blurred.begin());
        const size_t lo = hi - 1;
        const double t = (tot - blurred[lo]) / (blurred[hi] - blurred[lo]);
        return distance[lo] + t * (distance[hi] - distance[lo]);
    }
};

const Profile& profile() {
    static const Profile p;
    return p;
}

}  // namespace

EdgeMap edge_map_of(const GroundMap& ground) {
    EdgeMap m;
    m.width = ground.width;
    m.height = ground.height;
    m.kind.assign(ground.kind.size(), kNoKind);
    for (size_t i = 0; i < ground.kind.size(); ++i) {
        if (i >= ground.plain.size() || ground.plain[i] == 0) continue;
        switch (ground.kind[i]) {
            case 'w': m.kind[i] = kWater; break;
            case 'm': m.kind[i] = kMud; break;
            case 'g': m.kind[i] = kGravel; break;
            case 'd': m.kind[i] = kDirt; break;
            case 's': m.kind[i] = kSlate; break;
            default: break;
        }
    }
    return m;
}

bool has_transition(const EdgeMap& map, int32_t col, int32_t row) noexcept {
    const uint8_t lo = map.at(col, row);
    if (lo == kNoKind) return false;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const uint8_t k = map.at(col + dx, row + dy);
            if (k != kNoKind && k > lo) return true;
        }
    }
    return false;
}

bool grid_like(const EdgeMap& map, int32_t col, int32_t row) noexcept {
    const uint8_t lo = map.at(col, row);
    if (lo == kNoKind) return false;
    const auto higher = [&](int32_t x, int32_t y) { const uint8_t k = map.at(x, y); return k != kNoKind && k > lo; };
    static constexpr int32_t kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    int32_t sides = 0;
    for (const auto& d : kDirs) sides += higher(col + d[0], row + d[1]) ? 1 : 0;
    if (sides >= 3) return true;
    if (sides < 2) return false;
    int32_t rim = 0;
    for (int32_t dx = -1; dx <= 1; dx += 2) {
        for (int32_t dy = -1; dy <= 1; dy += 2) rim += (map.at(col + dx, row + dy) == lo && higher(col + dx, row) && higher(col, row + dy)) ? 1 : 0;
    }
    return rim >= 2;
}

std::vector<uint8_t> make_overlay(const EdgeMap& map, int32_t col, int32_t row, int32_t scale, const EdgeTextures& tex) {
    std::vector<uint8_t> none;
    const uint8_t lo = map.at(col, row);
    if (lo == kNoKind || lo == kSlate || scale < 1 || scale > 3 || !has_transition(map, col, row)) return none;
    // the cells round this one: their kinds and whether each is a grid's
    uint8_t kinds[3][3];
    bool thin[3][3];
    for (int32_t b = 0; b < 3; ++b) {
        for (int32_t a = 0; a < 3; ++a) {
            kinds[b][a] = map.at(col + a - 1, row + b - 1);
            thin[b][a] = grid_like(map, col + a - 1, row + b - 1);
        }
    }
    const bool this_thin = thin[1][1];
    const int32_t N = scale == 2 ? 4 : 3;                          // samples for each world pixel (3 x 3 at 1x, 2 x 2 for each pixel of 2x, 1 for each of 3x)
    const int32_t ss = N / scale;                                  // samples across for each output pixel
    const int32_t m = 5;                                           // samples all round, for the shifts of the lit lip and the shadow
    const int32_t n = 32 * N, G = n + 2 * m;
    const size_t Gs = static_cast<size_t>(G);
    const double sqrt2 = std::sqrt(2.0);
    // the blurred coverage of each of the 3 x 3 cells along a row of samples and along a column: 0.5 * (erf((edge - x) / s) - erf((edge' - x) / s))
    std::vector<double> cov_x(static_cast<size_t>(3) * Gs), cov_y(static_cast<size_t>(3) * Gs);
    for (int32_t i = 0; i < G; ++i) {
        const double x = col * kMapWidth + (i - m + 0.5) / N, y = row * kMapWidth + (i - m + 0.5) / N;
        double ex[4], ey[4];
        for (int32_t e = 0; e < 4; ++e) {
            ex[e] = 0.5 * std::erf(((col - 1 + e) * kMapWidth - x) / (kSigma * sqrt2));
            ey[e] = 0.5 * std::erf(((row - 1 + e) * kMapWidth - y) / (kSigma * sqrt2));
        }
        for (int32_t a = 0; a < 3; ++a) {
            cov_x[static_cast<size_t>(a) * Gs + static_cast<size_t>(i)] = ex[a + 1] - ex[a];
            cov_y[static_cast<size_t>(a) * Gs + static_cast<size_t>(i)] = ey[a + 1] - ey[a];
        }
    }
    std::vector<uint8_t> state(static_cast<size_t>(G) * Gs, 0);     // 0: not this kind's own sample, 1: natural edge, 2: thin edge, 3: the kind's own sample that is no bank here
    std::vector<float> dist(static_cast<size_t>(G) * Gs, 0.0f), width(static_cast<size_t>(G) * Gs, 0.0f);
    std::vector<uint8_t> higher(static_cast<size_t>(G) * Gs, 0);    // the kind of the land the sample's edge is made of
    std::vector<uint8_t> bank(static_cast<size_t>(G) * Gs, 0);                // 1: land that is higher than this kind: the bank, in the cell and beside it
    const Profile& prof = profile();
    for (int32_t j = 0; j < G; ++j) {
        const double Y = row * kMapWidth + (j - m + 0.5) / N;
        const int32_t cb = static_cast<int32_t>(std::floor(Y / kMapWidth)) - row;
        for (int32_t i = 0; i < G; ++i) {
            const double X = col * kMapWidth + (i - m + 0.5) / N;
            const int32_t ca = static_cast<int32_t>(std::floor(X / kMapWidth)) - col;
            if (ca < -1 || ca > 1 || cb < -1 || cb > 1) continue;
            const size_t at = static_cast<size_t>(j) * Gs + static_cast<size_t>(i);
            if (kinds[cb + 1][ca + 1] != lo) {                                                 // another kind, or outside the map: no edge of this kind here; land that is higher is bank
                const uint8_t other = kinds[cb + 1][ca + 1];
                bank[at] = other != kNoKind && other > lo ? 1 : 0;                             // (the map's border, an edge tile and a lower kind are no bank: no lit lip and no shadow along them)
                continue;
            }
            const bool cell_thin = thin[cb + 1][ca + 1];
            if (!this_thin && cell_thin) continue;                                             // a grid's cell beside a natural shore is not part of it
            if (this_thin && !cell_thin) { state[at] = 3; bank[at] = 0; continue; }            // (and a natural cell beside a grid's is no bank)
            const double wx = X, wy = Y;
            const double n1 = world_noise(wx, wy, 19.0, 1), n2 = world_noise(wx, wy, 7.5, 2), n3 = world_noise(wx, wy, 3.2, 3);
            if (!cell_thin) {                                                                  // the natural edge: from the blurred shape of the higher cells
                double f[kKinds] = {};
                double tot = 0.0;
                for (int32_t b = 0; b < 3; ++b) {
                    for (int32_t a = 0; a < 3; ++a) {
                        const uint8_t k = kinds[b][a];
                        if (k == kNoKind || k <= lo) continue;
                        f[k] += cov_x[static_cast<size_t>(a) * Gs + static_cast<size_t>(i)] * cov_y[static_cast<size_t>(b) * Gs + static_cast<size_t>(j)];
                    }
                }
                uint8_t best = kNoKind;
                for (uint8_t k = static_cast<uint8_t>(lo + 1); k < kKinds; ++k) {
                    tot += f[k];
                    if (best == kNoKind || f[k] > f[best]) best = k;
                }
                const double d = prof.distance_of(tot);
                const double n4 = world_noise(wx, wy, 31.0, 4);
                const double wobble = 2.1 * (n1 - 0.5) * 2 + 1.1 * (n2 - 0.5) * 2 + 0.5 * (n3 - 0.5) * 2 + 3.2 * clamp((n4 - 0.74) / 0.26, 0, 1) - 2.6 * clamp((0.24 - n4) / 0.24, 0, 1) * (n2 > 0.4 ? 1.0 : 0.0);
                double w;
                if (lo == kWater) w = clamp(3.3 + wobble, 0.7, 8.5);
                else if (best == kSlate) {
                    const double n5 = world_noise(wx, wy, 5.0, 5);
                    w = clamp(4.6 + 0.6 * wobble + 3.4 * clamp((n5 - 0.42) / 0.3, 0, 1) - 2.2 * clamp((0.3 - n5) / 0.3, 0, 1), 1.6, 9.5);
                } else w = clamp(2.2 + 0.8 * wobble, 0.4, 7.0);
                state[at] = 1;
                dist[at] = static_cast<float>(d);
                width[at] = static_cast<float>(w);
                higher[at] = best;
                bank[at] = d < w ? 1 : 0;
            } else {                                                                           // a thin edge: straight along each joined side, nothing across a diagonal
                const double lx = X - (col + ca) * kMapWidth, ly = Y - (row + cb) * kMapWidth;
                const double side[4] = {ly, kMapWidth - ly, lx, kMapWidth - lx};
                static constexpr int32_t kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
                double dmin = 1e3;
                uint8_t k_thin = kNoKind;
                for (int32_t s = 0; s < 4; ++s) {
                    const uint8_t k = map.at(col + ca + kDirs[s][0], row + cb + kDirs[s][1]);
                    if (k == kNoKind || k <= lo) continue;
                    if (side[s] < dmin) dmin = side[s], k_thin = k;
                }
                if (k_thin == kNoKind) { state[at] = 3; bank[at] = 0; continue; }
                const double small = 0.45 * (n2 - 0.5) * 2 + 0.25 * (n3 - 0.5) * 2;           // a thin edge still wavers a little
                const double w = (lo == kWater ? 2.6 : k_thin == kSlate ? 2.7 : 0.9) + small;
                state[at] = 2;
                dist[at] = static_cast<float>(dmin);
                width[at] = static_cast<float>(w);
                higher[at] = k_thin;
                bank[at] = dmin < w ? 1 : 0;
            }
        }
    }
    // the picture: the samples of this cell, averaged to the pixels of the output
    const auto bank_at = [&](int32_t i, int32_t j) { return bank[static_cast<size_t>(std::clamp(j, 0, G - 1)) * Gs + static_cast<size_t>(std::clamp(i, 0, G - 1))] != 0; };
    const int32_t shift_lit = static_cast<int32_t>(std::lround(0.95 * N)), shift_shadow_x = N, shift_shadow_y = static_cast<int32_t>(std::lround(1.1 * N));
    const int32_t px = 32 * scale;
    std::vector<double> acc(static_cast<size_t>(px) * static_cast<size_t>(px) * 4, 0.0);
    const auto put = [&](int32_t i, int32_t j, const Rgb& c, double scale_color) {
        const size_t o = (static_cast<size_t>((j - m) / ss) * static_cast<size_t>(px) + static_cast<size_t>((i - m) / ss)) * 4u;
        acc[o] += clamp(c.r * scale_color, 0, 255);
        acc[o + 1] += clamp(c.g * scale_color, 0, 255);
        acc[o + 2] += clamp(c.b * scale_color, 0, 255);
        acc[o + 3] += 1.0;
    };
    bool any = false;
    for (int32_t j = m; j < m + n; ++j) {
        for (int32_t i = m; i < m + n; ++i) {
            const size_t at = static_cast<size_t>(j) * Gs + static_cast<size_t>(i);
            const uint8_t st = state[at];
            if (st != 1 && st != 2) continue;
            const double d = dist[at], w = width[at];
            const uint8_t k = higher[at];
            const bool thin_edge = st == 2;
            const int32_t tx = (i - m) / ss, ty = (j - m) / ss;                                   // the pixel of the output that the sample is in (the picture of the higher kind's tile is read there)
            const uint8_t* pic = tex.rgb[k];
            const Rgb texel = pic != nullptr ? Rgb{pic[(static_cast<size_t>(ty) * static_cast<size_t>(px) + static_cast<size_t>(tx)) * 3u], pic[(static_cast<size_t>(ty) * static_cast<size_t>(px) + static_cast<size_t>(tx)) * 3u + 1u], pic[(static_cast<size_t>(ty) * static_cast<size_t>(px) + static_cast<size_t>(tx)) * 3u + 2u]} : kBase[k];
            if (lo == kWater) {
                const bool is_bank = bank[at] != 0;
                if (!is_bank) {                                                                    // the bank's shadow on the water
                    if (bank_at(i - shift_shadow_x, j - shift_shadow_y)) put(i, j, kWaterShadow, 1.0), any = true;
                    continue;
                }
                const bool lit = !bank_at(i - shift_lit, j - shift_lit);
                const bool dark = !bank_at(i + shift_lit, j + shift_lit);
                if (d < 0.30 * w) {
                    put(i, j, texel, 1.0);
                } else {
                    const double f = d < 0.55 * w ? 0.84 : d < 0.78 * w ? 0.66 : 0.50;
                    put(i, j, kBase[k], lit ? 1.38 * f : dark ? 0.78 * f : f);
                }
                any = true;
                continue;
            }
            const Band band = k == kSlate ? Band{kRock, 3} : soft_band(lo, k);
            const double st_w = k == kSlate ? (thin_edge ? 0.6 : 0.7) : thin_edge ? std::min(0.6, 1.9 / band.count) : (band.count > 3 ? 0.8 : 1.0);
            const double inner = k == kSlate ? w - st_w * band.count : w;
            if (d < inner) {
                put(i, j, texel, 1.0);
                any = true;
                continue;
            }
            for (int32_t q = 0; q < band.count; ++q) {
                if (d >= inner + q * st_w && d < inner + (q + 1) * st_w) {
                    put(i, j, band.colors[q], 1.0);
                    any = true;
                    break;
                }
            }
        }
    }
    if (!any) return none;
    std::vector<uint8_t> out(static_cast<size_t>(px) * static_cast<size_t>(px) * 4, 0);
    const double per_pixel = static_cast<double>(ss) * ss;
    bool visible = false;
    for (size_t o = 0; o < acc.size(); o += 4) {
        const double a = acc[o + 3] / per_pixel;
        if (acc[o + 3] <= 0.0) continue;
        out[o] = static_cast<uint8_t>(std::lround(clamp(acc[o] / acc[o + 3], 0, 255)));
        out[o + 1] = static_cast<uint8_t>(std::lround(clamp(acc[o + 1] / acc[o + 3], 0, 255)));
        out[o + 2] = static_cast<uint8_t>(std::lround(clamp(acc[o + 2] / acc[o + 3], 0, 255)));
        out[o + 3] = static_cast<uint8_t>(std::lround(clamp(a * 255.0, 0, 255)));
        visible = visible || out[o + 3] > 0;
    }
    return visible ? out : none;
}

}  // namespace ants::app::world_art::edges
