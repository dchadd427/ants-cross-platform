// The dense mud of the world art (include/ants_app/world_art_mud.hpp, src/ants_app/world_art_mud.cpp, renderer_world_mud.cpp; docs/WORLD_ART.md, "Dense mud"): a big mud field looks different in its deep middle.
//   [golden]    the noise, the depth of the mud and the wet and patch looks equal the values of the approved generator (tests/data/world_art_mud_golden.txt, made by tools/world_art/mud_reference.py)
//   [depth]     the map's border is no bank; a bank has depth 0; a strip of mud two cells wide or a small field has no look at all
//   [looks]     a look is exactly nothing before its first depth; deeper it darkens, tints and ripples; the same cell gives the same looks
//   [bubbles]   shallow cells keep the approved bubbles; deep ones have bigger bubbles and a share of the plain ones bubble on the original's own time
//   [render]    the real renderer: banks and shallow mud are the approved pictures, the deep middle is not; the same frame twice is the same; a small budget gets there frame by frame; the caches are bounded
// Usage: test_world_art_mud. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/world_art.hpp"
#include "ants_app/world_art_mud.hpp"

using namespace ants;
using namespace ants::app;
namespace mud = ants::app::world_art::mud;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::string pack_folder() { return std::string(ANTS_SOURCE_DIR) + "/assets/world_art"; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [golden]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_golden() {
    group("golden", "the noise, the depth and the looks against the approved generator's values");
    std::ifstream in(std::string(ANTS_SOURCE_DIR) + "/tests/data/world_art_mud_golden.txt");
    check(static_cast<bool>(in), "the golden file is there");
    std::string line;
    std::getline(in, line);
    check(line == "ants-world-art-mud-golden 1", "its header");
    std::vector<std::string> rows;
    struct Rec {
        std::string kind;
        std::vector<double> v;
    };
    std::vector<Rec> recs;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        Rec r;
        words >> r.kind;
        if (r.kind == "mask") {
            std::string row;
            words >> row;
            rows.push_back(row);
            continue;
        }
        double x;
        while (words >> x) r.v.push_back(x);
        recs.push_back(r);
    }
    check(rows.size() == 20 && rows[0].size() == 24, "the synthetic map is 24 x 20 cells");
    std::vector<uint8_t> cells;
    for (const std::string& row : rows) {
        for (const char c : row) cells.push_back(c == '#' ? 1 : 0);
    }
    mud::Depth depth;
    depth.build(24, 20, cells);
    size_t counts[7] = {};
    for (const Rec& r : recs) {
        if (r.kind == "hash" && r.v.size() == 4) {
            ++counts[0];
            check(near(mud::lattice_value(static_cast<int64_t>(r.v[0]), static_cast<int64_t>(r.v[1]), static_cast<int64_t>(r.v[2])), r.v[3], 1e-8), "hash " + std::to_string(counts[0]));
        } else if (r.kind == "vnoise" && r.v.size() == 4) {
            ++counts[1];
            check(near(mud::value_noise(r.v[0], r.v[1], static_cast<int64_t>(r.v[2])), r.v[3], 1e-8), "vnoise " + std::to_string(counts[1]));
        } else if (r.kind == "fbm" && r.v.size() == 10) {
            ++counts[2];
            check(near(mud::fbm(r.v[0], r.v[1], r.v[2], static_cast<int64_t>(r.v[3]), static_cast<int32_t>(r.v[4]), r.v[5], r.v[6], r.v[7], r.v[8]), r.v[9], 1e-8), "fbm " + std::to_string(counts[2]));
        } else if (r.kind == "depth" && r.v.size() == 3) {
            ++counts[3];
            check(near(depth.at(r.v[0], r.v[1]), r.v[2], 2e-5), "depth at (" + std::to_string(r.v[0]) + ", " + std::to_string(r.v[1]) + ")");
        } else if (r.kind == "celldepth" && r.v.size() == 3) {
            ++counts[4];
            check(near(depth.cell(static_cast<int32_t>(r.v[0]), static_cast<int32_t>(r.v[1])), r.v[2], 2e-5), "cell depth " + std::to_string(counts[4]));
        } else if (r.kind == "wetpatch" && r.v.size() == 8) {
            ++counts[5];
            const mud::Look look = mud::wet_and_patch_at(r.v[0], r.v[1], depth.at(r.v[0], r.v[1]));
            bool ok = true;
            for (size_t k = 0; k < 3; ++k) ok = ok && near(look.mult[k], r.v[2 + k], 2e-5) && near(look.add[k], r.v[5 + k], 2e-4);
            check(ok, "wet and patch looks at (" + std::to_string(r.v[0]) + ", " + std::to_string(r.v[1]) + ")");
        } else if (r.kind == "ripple_median" && r.v.size() == 1) {
            ++counts[6];
            check(near(mud::kRippleMedian, r.v[0], 1e-9), "the median length of the ripple field's gradient");
        }
    }
    check(counts[0] == 12 && counts[1] == 12 && counts[2] == 16 && counts[3] == 40 && counts[4] == 20 && counts[5] == 40 && counts[6] == 1, "every record of the file was compared");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [depth] [looks]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> mud_map(int32_t w, int32_t h, const std::vector<std::string>& not_mud) {
    std::vector<uint8_t> m(static_cast<size_t>(w) * static_cast<size_t>(h), 1);
    for (const std::string& rect : not_mud) {                  // "x0,y0,x1,y1" cells, inclusive
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        std::sscanf(rect.c_str(), "%d,%d,%d,%d", &x0, &y0, &x1, &y1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) m[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 0;
        }
    }
    return m;
}

void test_depth_and_looks() {
    group("depth", "the depth of the mud, and which fields have looks at all");
    mud::Depth big;
    big.build(20, 20, mud_map(20, 20, {}));
    check(big.any_deep() && near(big.cell(10, 10), mud::kDepthCap, 1e-6), "a field with no bank at all is as deep as the depth goes (the map's border is no bank): " + std::to_string(big.cell(10, 10)));
    mud::Depth lake;
    lake.build(20, 20, mud_map(20, 20, {"8,8,11,11"}));
    check(near(lake.at(8 * 32 - 0.01, 9 * 32 + 16), 0.0, 0.07) && near(lake.at(7 * 32, 9 * 32 + 16), 1.0, 0.07), "a bank has depth 0 and a point one cell from it depth 1");
    check(lake.at(2 * 32 + 16, 2 * 32 + 16) > 3.0, "far from the lake the depth is more than three tiles");
    mud::Depth strip;
    strip.build(24, 12, mud_map(24, 12, {"0,0,23,4", "0,7,23,11"}));       // two cells wide
    check(!strip.any_deep(), "a strip of mud two cells wide has no look: its deepest point is a tile from a bank");
    bool all_identity = true;
    for (int32_t x = 0; x < 24; ++x) all_identity = all_identity && mud::CellLooks::compute(strip, x, 5).identity() && mud::CellLooks::compute(strip, x, 6).identity();
    check(all_identity, "...and the looks of its cells are nothing");
    mud::Depth four;
    four.build(24, 12, mud_map(24, 12, {"0,0,23,3", "0,8,23,11"}));        // four cells wide: the middle is two tiles from a bank
    check(four.any_deep(), "a strip four cells wide has a middle that begins to change");

    group("looks", "the looks grow with the depth and are exactly nothing before it");
    const mud::Look shallow = mud::wet_and_patch_at(100.0, 100.0, 1.0);
    check(shallow.mult[0] == 1.0 && shallow.mult[1] == 1.0 && shallow.mult[2] == 1.0 && shallow.add[0] == 0.0 && shallow.add[1] == 0.0 && shallow.add[2] == 0.0, "at depth 1 the looks are exactly nothing");
    const mud::Look deep = mud::wet_and_patch_at(100.0, 100.0, 7.0);
    check(deep.mult[0] < 0.9 && deep.mult[1] < 0.9 && deep.mult[2] < 0.9 && deep.mult[2] <= deep.mult[0], "at depth 7 the mud is darker, a little more on blue than on red");
    double sheen_or_patch = 0.0;
    for (int i = 0; i < 40; ++i) {
        const mud::Look l = mud::wet_and_patch_at(37.0 * i, 91.0 * i + 5.0, 7.0);
        sheen_or_patch = std::max(sheen_or_patch, std::fabs(l.add[0]) + std::fabs(l.add[1]) + std::fabs(l.add[2]));
    }
    check(sheen_or_patch > 3.0, "...and somewhere there is a sheen or a patch: " + std::to_string(sheen_or_patch));
    double previous = 1.0;
    bool darker = true;
    for (double d = 1.5; d <= 6.0; d += 0.5) {                  // the same place at growing depth: never lighter (the wander is the same at a place)
        const double m = mud::wet_and_patch_at(555.0, 777.0, d).mult[0];
        darker = darker && m <= previous + 1e-12;
        previous = m;
    }
    check(darker, "the same place gets no lighter as the depth grows");

    mud::CellLooks a = mud::CellLooks::compute(big, 10, 10), b = mud::CellLooks::compute(big, 10, 10);
    double m1[3], a1[3], r1 = 0.0, m2[3], a2[3], r2 = 0.0;
    bool same = true;
    for (int i = 0; i < 32; i += 3) {
        a.at(i + 0.5, 31.5 - i, m1, a1, r1);
        b.at(i + 0.5, 31.5 - i, m2, a2, r2);
        same = same && m1[0] == m2[0] && a1[1] == a2[1] && r1 == r2;
    }
    check(!a.identity() && same, "the looks of a cell are the same every time");
    double ripples = 0.0;
    for (int j = 0; j < 64; ++j) {
        for (int i = 0; i < 64; ++i) {
            a.at(i / 2.0 + 0.25, j / 2.0 + 0.25, m1, a1, r1);
            ripples = std::max(ripples, std::fabs(r1));
        }
    }
    check(ripples > 1.0 && ripples < 6.0, "the deep middle has ripples of a few levels: " + std::to_string(ripples));
    // no seam: the looks of a cell and its neighbor meet (the value on each side of the border, half a pixel apart, differs as little as inside)
    mud::CellLooks left = mud::CellLooks::compute(big, 9, 10), right = mud::CellLooks::compute(big, 10, 10);
    double worst = 0.0;
    for (int j = 0; j < 32; ++j) {
        double ml[3], al[3], rl = 0.0, mr[3], ar[3], rr = 0.0;
        left.at(31.9, j + 0.5, ml, al, rl);
        right.at(0.1, j + 0.5, mr, ar, rr);
        worst = std::max(worst, std::max(std::fabs(ml[0] - mr[0]) * 255.0, std::max(std::fabs(al[0] - ar[0]), std::fabs(rl - rr))));
    }
    check(worst < 1.5, "the looks of two neighboring cells meet at their border: the largest step across it is " + std::to_string(worst) + " levels");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [bubbles]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_bubbles() {
    group("bubbles", "which bubble a deep cell shows");
    bool unchanged = true;
    for (int32_t c = 0; c < 40; ++c) {
        for (uint32_t native : {640u, 641u, 645u, 646u, 650u, 651u, 657u, 901u}) unchanged = unchanged && mud::choose_bubble(c, c * 3 % 17, native, 777u * static_cast<uint32_t>(c), 2.1).sprite == native && mud::choose_bubble(c, c * 3 % 17, native, 5u, 2.1).size == 0;
    }
    check(unchanged, "above depth 2.2 every cell shows what it shows: no bigger bubble, no new one");
    bool grown = true;
    int32_t sizes[3] = {0, 0, 0};
    for (int32_t c = 0; c < 200; ++c) {
        for (uint32_t native : {641u, 643u, 646u, 650u}) {
            const mud::BubbleChoice choice = mud::choose_bubble(c, c / 7, native, 1000u, 8.0);
            grown = grown && choice.sprite == native && (choice.size == 1 || choice.size == 2);
            ++sizes[choice.size];
        }
    }
    check(grown && sizes[1] > 0 && sizes[2] > 0, "a deep cell's own bubble frame is the same frame, grown to a medium or a large bubble (medium " + std::to_string(sizes[1]) + ", large " + std::to_string(sizes[2]) + ")");
    int32_t large_at[2] = {0, 0};
    for (int32_t c = 0; c < 400; ++c) {
        large_at[0] += mud::choose_bubble(c, c % 13, 643u, 0u, 3.0).size == 2 ? 1 : 0;
        large_at[1] += mud::choose_bubble(c, c % 13, 643u, 0u, 8.0).size == 2 ? 1 : 0;
    }
    check(large_at[1] > large_at[0], "deeper, more of the bubbles are the large ones (" + std::to_string(large_at[0]) + " of 400 at depth 3, " + std::to_string(large_at[1]) + " at depth 8)");
    bool bursts = true;
    for (int32_t c = 0; c < 100; ++c) {
        for (uint32_t native : {651u, 652u, 655u, 657u, 901u}) bursts = bursts && mud::choose_bubble(c, c / 3, native, 123u * static_cast<uint32_t>(c), 8.0) .sprite == native && mud::choose_bubble(c, c / 3, native, 5u, 8.0).size == 0;
    }
    check(bursts, "the original's biggest bursts stay as they are, however deep");
    // plain cells: a share bubble by themselves, on the original's time (1,200 ms idle, then five frames, 1,600 ms in all)
    int32_t bubbling = 0, plain_cells = 0;
    std::vector<int32_t> cells;
    for (int32_t c = 0; c < 400; ++c) {
        ++plain_cells;
        bool ever = false;
        for (uint32_t t = 0; t < 1600; t += 10) ever = ever || mud::choose_bubble(c, 3 + c % 29, 640u, t, 8.0).sprite != 640u;
        if (ever) {
            ++bubbling;
            cells.push_back(c);
        }
    }
    check(bubbling > plain_cells * 30 / 100 && bubbling < plain_cells * 60 / 100, "about 45 of 100 of the deepest plain cells bubble by themselves: " + std::to_string(bubbling) + " of " + std::to_string(plain_cells));
    bool timed = !cells.empty();
    for (size_t k = 0; k < std::min<size_t>(cells.size(), 25); ++k) {
        const int32_t c = cells[k];
        std::vector<int32_t> ms(660, 0);                         // how long each frame is shown in a cycle of 1,600 ms
        for (uint32_t t = 0; t < 1600; ++t) {
            const mud::BubbleChoice b = mud::choose_bubble(c, 3 + c % 29, 640u, t, 8.0);
            if (b.sprite != 640u) ++ms[b.sprite];
        }
        const bool small_set = ms[641] > 0, large_set = ms[646] > 0;
        const size_t first = small_set ? 641u : 646u;
        timed = timed && small_set != large_set && ms[first] == 100 && ms[first + 1] == 100 && ms[first + 2] == 100 && ms[first + 3] == 50 && ms[first + 4] == 50;
        // the clock repeats every 1,600 ms
        timed = timed && mud::choose_bubble(c, 3 + c % 29, 640u, 100u, 8.0).sprite == mud::choose_bubble(c, 3 + c % 29, 640u, 1700u, 8.0).sprite;
    }
    check(timed, "a cell that bubbles by itself shows its five frames of one set for 100, 100, 100, 50 and 50 ms of every 1,600, from a start of its own");
    check(mud::choose_bubble(1, 1, 640u, 0u, 8.0).sprite == mud::choose_bubble(1, 1, 640u, 0u, 8.0).sprite && mud::cell_hash(5, 9, 6) == mud::cell_hash(5, 9, 6) && mud::cell_hash(5, 9, 6) != mud::cell_hash(9, 5, 6), "the choice is by the cell and the clock alone");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [render]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kW = 960;
constexpr int32_t kH = 540;

bool same_pixels(const zoomtest::Picture& a, const zoomtest::Picture& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }

bool same_block(const zoomtest::Picture& a, const zoomtest::Picture& b, int32_t x, int32_t y, int32_t w, int32_t h) {
    for (int32_t j = y; j < y + h; ++j) {
        if (std::memcmp(a.at(x, j), b.at(x, j), static_cast<size_t>(w) * 4u) != 0) return false;
    }
    return true;
}

std::string mud_wars() { return std::string(ANTS_SOURCE_DIR) + "/Community-Maps/Mud Wars.lvl"; }

void test_render(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("render", "the real renderer: banks and shallow mud are the approved pictures, the deep middle is not");
    zoomtest::PixelRig rig(arc, mud_wars(), kW, kH);
    if (!rig.ok) {
        check(false, "the rig is up");
        return;
    }
    rig.renderer.set_world_art(&pack);
    rig.renderer.set_dense_mud_budget_ms(-1);
    // the place of the map whose view has the most cells beside a bank and the most deep ones
    int32_t best_x = 0, best_y = 0, best_score = -1;
    for (int32_t y = 0; y + 500 <= static_cast<int32_t>(rig.tiles_h) * 32; y += 32) {
        for (int32_t x = 0; x + 762 <= static_cast<int32_t>(rig.tiles_w) * 32; x += 32) {
            int32_t near_bank = 0, deep = 0;
            for (int32_t r = y / 32; r < y / 32 + 14; ++r) {
                for (int32_t c = x / 32; c < x / 32 + 22; ++c) {
                    const double d = rig.renderer.mud_depth_at_cell(c, r);
                    near_bank += d > 0.0 && d <= 0.6 ? 1 : 0;
                    deep += d >= 3.0 ? 1 : 0;
                }
            }
            const int32_t score = std::min(near_bank, deep);
            if (score > best_score) best_score = score, best_x = x, best_y = y;
        }
    }
    rig.look(best_x, best_y);
    const LayoutRect view = rig.layout.view();
    const int32_t c0 = static_cast<int32_t>(rig.cam().x) / 32, r0 = static_cast<int32_t>(rig.cam().y) / 32;
    rig.renderer.set_dense_mud(false);
    const zoomtest::Picture without = rig.shoot();
    rig.renderer.set_dense_mud(true);
    const zoomtest::Picture dense = rig.shoot();
    check(!same_pixels(without, dense), "the deep mud of a big field is not the approved picture");
    size_t shallow = 0, shallow_same = 0, deep = 0, deep_changed = 0;
    for (int32_t r = r0; r < r0 + 14; ++r) {
        for (int32_t c = c0; c < c0 + 22; ++c) {
            const int32_t sx = view.x + c * 32 - static_cast<int32_t>(rig.cam().x), sy = view.y + r * 32 - static_cast<int32_t>(rig.cam().y);
            if (sx < view.x || sy < view.y || sx + 32 > view.x + view.w || sy + 32 > view.y + view.h) continue;
            const double d = rig.renderer.mud_depth_at_cell(c, r);
            if (d > 0.0 && d <= 0.6) {
                ++shallow;
                shallow_same += same_block(without, dense, sx, sy, 32, 32) ? 1u : 0u;
            } else if (d >= 3.0) {
                ++deep;
                deep_changed += same_block(without, dense, sx, sy, 32, 32) ? 0u : 1u;
            }
        }
    }
    check(shallow >= 6 && shallow_same == shallow, "every one of the " + std::to_string(shallow) + " cells beside a bank is the approved picture (" + std::to_string(shallow_same) + ")");
    check(deep >= 20 && deep_changed >= deep * 9 / 10, "the cells three tiles or more from a bank are changed: " + std::to_string(deep_changed) + " of " + std::to_string(deep));
    check(same_pixels(rig.shoot(), dense), "the same frame twice is the same pixels");
    // the looks cross the cells' borders without a seam: they add no more of a step across a border than inside a cell (the approved mud has patches that stop at a tile's edge: that step is its own)
    {
        double across[2] = {0.0, 0.0}, inside[2] = {0.0, 0.0};
        int32_t n = 0;
        for (int32_t r = r0 + 1; r < r0 + 13; ++r) {
            for (int32_t c = c0 + 1; c < c0 + 21; ++c) {
                if (rig.renderer.mud_depth_at_cell(c, r) < 4.0 || rig.renderer.mud_depth_at_cell(c - 1, r) < 4.0) continue;
                const int32_t sx = view.x + c * 32 - static_cast<int32_t>(rig.cam().x), sy = view.y + r * 32 - static_cast<int32_t>(rig.cam().y);
                for (int32_t j = 2; j < 30; ++j) {
                    for (int which = 0; which < 2; ++which) {
                        const zoomtest::Picture& pic = which == 0 ? without : dense;
                        const auto lum = [&](int32_t x) { const uint8_t* p = pic.at(x, sy + j); return static_cast<double>(p[0]) + p[1] + p[2]; };
                        across[which] += std::fabs(lum(sx) - lum(sx - 1));
                        inside[which] += std::fabs(lum(sx + 16) - lum(sx + 15));
                    }
                    ++n;
                }
            }
        }
        const double added_across = (across[1] - across[0]) / std::max(n, 1), added_inside = (inside[1] - inside[0]) / std::max(n, 1);
        check(n > 100 && added_across <= added_inside + 0.25, "the looks add no more of a step across a cell's border than inside it: " + std::to_string(added_across) + " against " + std::to_string(added_inside) + " (the approved mud's own: " + std::to_string(across[0] / std::max(n, 1)) + " across, " + std::to_string(inside[0] / std::max(n, 1)) + " inside)");
    }
    // a small budget gets to the same picture, a few cells in a frame
    {
        zoomtest::PixelRig slow(arc, mud_wars(), kW, kH);
        slow.renderer.set_world_art(&pack);
        slow.renderer.set_dense_mud_budget_ms(0);
        slow.look(best_x, best_y);
        const zoomtest::Picture first = slow.shoot();
        check(!same_pixels(first, dense) && slow.renderer.mud_looks_cells() >= 1, "with no budget the first frame has the looks of one cell and the rest approved");
        size_t before = 0;
        int32_t frames = 1;
        zoomtest::Picture last = first;
        while (frames < 2000) {
            last = slow.shoot();
            ++frames;
            if (slow.renderer.mud_looks_cells() == before && same_pixels(last, dense)) break;
            before = slow.renderer.mud_looks_cells();
        }
        check(same_pixels(last, dense), "...and goes on, a cell a frame, to the picture that the unlimited budget gives (" + std::to_string(frames) + " frames)");
    }
    // the caches are bounded: once the view has moved, the cells that are not in it are let go (the limit says how many bytes may stay), and when it comes back it is the same picture
    {
        const auto cells_in = [&](double x, double y, size_t limit, size_t& kept_after_first) {
            zoomtest::PixelRig far(arc, mud_wars(), kW, kH);
            far.renderer.set_world_art(&pack);
            far.renderer.set_dense_mud_budget_ms(-1);
            far.renderer.set_dense_mud_cache_limit(limit);
            far.look(0, y);
            far.shoot();
            kept_after_first = far.renderer.mud_baked_cells();
            far.look(x, y);
            far.shoot();
            return far.renderer.mud_baked_cells();
        };
        size_t first_alone = 0, ignore = 0;
        const size_t unbounded = cells_in(230, best_y, 1024u * 1024u * 1024u, first_alone);
        const size_t second_alone = [&]() {
            zoomtest::PixelRig only(arc, mud_wars(), kW, kH);
            only.renderer.set_world_art(&pack);
            only.renderer.set_dense_mud_budget_ms(-1);
            only.look(230, best_y);
            only.shoot();
            return only.renderer.mud_baked_cells();
        }();
        const size_t bounded = cells_in(230, best_y, 1, ignore);
        check(first_alone > 0 && unbounded > second_alone, "without a limit the cells of the first view stay (" + std::to_string(unbounded) + " kept, the second view alone has " + std::to_string(second_alone) + ")");
        check(bounded == second_alone, "with a small limit only the cells of the view that is shown stay (" + std::to_string(bounded) + " of " + std::to_string(second_alone) + ")");
        zoomtest::PixelRig again(arc, mud_wars(), kW, kH);
        again.renderer.set_world_art(&pack);
        again.renderer.set_dense_mud_budget_ms(-1);
        again.renderer.set_dense_mud_cache_limit(1);
        again.look(0, best_y);
        const zoomtest::Picture home = again.shoot();
        again.look(230, best_y);
        again.shoot();
        again.look(0, best_y);
        check(same_pixels(again.shoot(), home), "...and the first view comes back as the same picture");
    }
    // the bubbles move with the template clock: a deep field at two moments of the bubbling time
    {
        rig.renderer.pin_animation_clock(1000);
        const zoomtest::Picture t1 = rig.shoot();
        rig.renderer.pin_animation_clock(1300);
        const zoomtest::Picture t2 = rig.shoot();
        check(!same_pixels(t1, t2), "the bubbles of the deep field are not the same at two moments of the bubbling time");
    }
    // a map whose mud is shallow everywhere is not changed at all; TREASURE's crossing is narrow and is changed only in its deepest part
    {
        zoomtest::PixelRig islands(arc, "ISLANDS", kW, kH);
        islands.renderer.set_world_art(&pack);
        islands.renderer.set_dense_mud_budget_ms(-1);
        bool unchanged = true;
        for (const double x : {0.0, 600.0, 1100.0, 1500.0}) {
            islands.look(x, 300);
            islands.renderer.set_dense_mud(false);
            const zoomtest::Picture a = islands.shoot();
            islands.renderer.set_dense_mud(true);
            unchanged = unchanged && same_pixels(a, islands.shoot());
        }
        check(unchanged, "ISLANDS, where no mud is deeper than a tile, is the same picture with and without the looks");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                  // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    assets::AssetArchive arc;
    world_art::Pack pack;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !pack.load(pack_folder())) {
        std::fprintf(stderr, "cannot open ants.chd or the world art: %s\n", pack.what().c_str());
        return 2;
    }
    test_golden();
    test_depth_and_looks();
    test_bubbles();
    test_render(arc, pack);
    std::printf("\nworld art mud: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
