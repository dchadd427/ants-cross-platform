// The automatic transitions of the world art (include/ants_app/world_art_edges.hpp, src/ants_app/world_art_edges.cpp, renderer_world_edges.cpp; docs/WORLD_ART.md, "Transitions"): where two plain tiles of
// different kinds of ground meet with no edge tile between them, the New art draws a transition inside the lower kind's cell.
//   [kinds]    which cells have a transition (a plain tile with a higher kind among its eight neighbors) and which are a grid's
//   [overlay]  the picture of one: on the lower kind's cell only, near the join only, nothing at the map's border, thin on a grid, the same every time, a look for every pair, a lit lip toward the light
//   [render]   the real renderer: ground without a join is the same pixels, the transition lies under objects, Fog of War's stipple and the state hash are untouched, the memory is bounded, the work is within a budget
// Usage: test_world_art_edges. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/world_art.hpp"
#include "ants_app/world_art_edges.hpp"

using namespace ants;
using namespace ants::app;
namespace edges = ants::app::world_art::edges;

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

std::string pack_folder() { return std::string(ANTS_SOURCE_DIR) + "/assets/world_art"; }

/// A map from rows of letters: w water, m mud, g gravel, d dirt, s slate, . something that is not a plain tile
edges::EdgeMap make_map(const std::vector<std::string>& rows) {
    edges::EdgeMap m;
    m.height = static_cast<int32_t>(rows.size());
    m.width = static_cast<int32_t>(rows[0].size());
    for (const std::string& row : rows) {
        for (const char c : row) {
            m.kind.push_back(c == 'w' ? edges::kWater : c == 'm' ? edges::kMud : c == 'g' ? edges::kGravel : c == 'd' ? edges::kDirt : c == 's' ? edges::kSlate : edges::kNoKind);
        }
    }
    return m;
}

/// Solid-color pictures for the higher kinds' inside, at a scale
struct Solid {
    std::vector<uint8_t> pic[edges::kKinds];
    edges::EdgeTextures textures;
    explicit Solid(int32_t scale) {
        static constexpr int kColor[edges::kKinds][3] = {{0, 0, 0}, {75, 75, 75}, {143, 119, 111}, {219, 207, 199}, {150, 148, 177}};
        for (size_t k = 1; k < edges::kKinds; ++k) {
            pic[k].resize(static_cast<size_t>(32 * scale) * static_cast<size_t>(32 * scale) * 3u);
            for (size_t i = 0; i < pic[k].size(); ++i) pic[k][i] = static_cast<uint8_t>(kColor[k][i % 3]);
            textures.rgb[k] = pic[k].data();
        }
    }
};

/// The smallest distance in pixels (of a 32-pixel cell) from a pixel of the overlay's picture to the given side (0 up, 1 down, 2 left, 3 right) over the pixels that have some alpha, and the number of those
double reach(const std::vector<uint8_t>& ov, int32_t scale, int32_t side, size_t* count = nullptr) {
    const int32_t n = 32 * scale;
    double far_most = -1.0;
    size_t c = 0;
    for (int32_t y = 0; y < n; ++y) {
        for (int32_t x = 0; x < n; ++x) {
            if (ov[(static_cast<size_t>(y) * static_cast<size_t>(n) + static_cast<size_t>(x)) * 4u + 3u] == 0) continue;
            ++c;
            const double px = (x + 0.5) / scale, py = (y + 0.5) / scale;
            const double d = side == 0 ? py : side == 1 ? 32.0 - py : side == 2 ? px : 32.0 - px;
            far_most = std::max(far_most, d);
        }
    }
    if (count != nullptr) *count = c;
    return far_most;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [kinds]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_kinds() {
    group("kinds", "which cells have a transition, and which are a grid's");
    world_art::GroundMap ground;
    ground.width = 3;
    ground.height = 2;
    ground.kind = {'w', 'm', 'g', 'd', 's', 'g'};
    ground.plain = {1, 1, 1, 1, 1, 0};                         // the last is an edge tile of gravel: not a kind
    ground.object.assign(6, 0);
    const edges::EdgeMap m = edges::edge_map_of(ground);
    check(m.at(0, 0) == edges::kWater && m.at(1, 0) == edges::kMud && m.at(2, 0) == edges::kGravel && m.at(0, 1) == edges::kDirt && m.at(1, 1) == edges::kSlate && m.at(2, 1) == edges::kNoKind, "the five plain tiles are the five kinds in rank, an edge tile is none");
    check(m.at(-1, 0) == edges::kNoKind && m.at(0, 5) == edges::kNoKind, "outside the map is none");
    const edges::EdgeMap row = make_map({"wgd"});
    check(edges::has_transition(row, 0, 0) && edges::has_transition(row, 1, 0) && !edges::has_transition(row, 2, 0), "a cell has a transition when a neighbor is of a higher kind: water and gravel do, dirt (the highest here) does not");
    check(!edges::has_transition(make_map({"ggg"}), 1, 0) && !edges::has_transition(make_map({"w.g"}), 0, 0), "...a field of one kind has none, and an edge tile between is no neighbor of a kind");
    check(edges::has_transition(make_map({"wg", "ww"}), 0, 0) && edges::has_transition(make_map({"w.", ".g"}), 0, 0), "...a neighbor across a diagonal counts: a corner is rounded");
    check(!edges::has_transition(make_map({"s"}), 0, 0), "slate, the highest kind, has none");
    check(!edges::grid_like(make_map({"wgw"}), 1, 0) && !edges::grid_like(make_map({"gw", "ww"}), 1, 1), "a shore is not a grid");
    check(edges::grid_like(make_map({".g.", "gwg", ".g."}), 1, 1), "a cell with all four sides joined is a grid's");
    check(edges::grid_like(make_map({".g.", "gw.", ".g."}), 1, 1), "...three sides are enough");
    check(!edges::grid_like(make_map({"ww", "gw"}), 1, 0) && !edges::grid_like(make_map({".g.", "gw."}), 1, 1), "...two sides alone are not");
    const edges::EdgeMap chess = make_map({"wgwgw", "gwgwg", "wgwgw", "gwgwg", "wgwgw"});
    check(edges::grid_like(chess, 2, 2) && edges::grid_like(chess, 1, 1) && edges::grid_like(chess, 3, 3) && !edges::grid_like(chess, 2, 1), "the lower cells of a checkerboard are a grid's, its higher cells (nothing is higher) are not");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [overlay]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_overlay() {
    group("overlay", "the picture of one transition");
    for (const int32_t scale : {1, 2, 3}) {
        const Solid solid(scale);
        const edges::EdgeMap strip = make_map({"wwwwgggg"});
        const std::vector<uint8_t> ov = edges::make_overlay(strip, 3, 0, scale, solid.textures);
        check(ov.size() == static_cast<size_t>(32 * scale) * static_cast<size_t>(32 * scale) * 4u, "the picture of a cell is its size at " + std::to_string(scale) + "x");
        // water against gravel: near the right side of the water cell only (the map's top and bottom are its border: nothing there)
        size_t count = 0;
        const double right = reach(ov, scale, 3, &count);
        check(count > 0 && right >= 0.0, "water beside gravel has a transition at " + std::to_string(scale) + "x");
        const double left = reach(ov, scale, 2);
        check(left > 20.0, "...which lies on the side of the gravel: its farthest pixel from that side is the cell's far side: " + std::to_string(left));
        double nearest = 99.0;
        for (int32_t y = 0; y < 32 * scale; ++y) {
            for (int32_t x = 0; x < 32 * scale; ++x) {
                if (ov[(static_cast<size_t>(y) * static_cast<size_t>(32 * scale) + static_cast<size_t>(x)) * 4u + 3u] == 0) continue;
                nearest = std::min(nearest, 32.0 - (x + 0.5) / scale);
            }
        }
        check(nearest < 12.0, "...the transition begins within 12 pixels of the gravel");
        std::vector<uint8_t> inner;
        size_t farther = 0;
        for (int32_t y = 0; y < 32 * scale; ++y) {
            for (int32_t x = 0; x < 20 * scale; ++x) farther += ov[(static_cast<size_t>(y) * static_cast<size_t>(32 * scale) + static_cast<size_t>(x)) * 4u + 3u] != 0 ? 1u : 0u;
        }
        check(farther == 0, "...and nothing is drawn further than 12 pixels from the gravel (the left 20 pixels of the cell are clear)");
        // the gravel's cell has none, and a cell far from the join has none
        check(edges::make_overlay(strip, 4, 0, scale, solid.textures).empty() && edges::make_overlay(strip, 1, 0, scale, solid.textures).empty(), "the gravel's own cell and a water cell two cells from the join have none");
        check(edges::make_overlay(strip, 3, 0, scale, solid.textures) == ov, "the same cell is the same picture every time");
    }

    {   // nothing at the map's border: a water column at the left border beside gravel; its left side is the border, and the picture is a bank on the right only
        const Solid solid(3);
        const edges::EdgeMap m = make_map({"wg", "wg", "wg"});
        const std::vector<uint8_t> ov = edges::make_overlay(m, 0, 1, 3, solid.textures);
        size_t left_pixels = 0, count = 0;
        for (int32_t y = 0; y < 96; ++y) {
            for (int32_t x = 0; x < 96; ++x) {
                if (ov[(static_cast<size_t>(y) * 96u + static_cast<size_t>(x)) * 4u + 3u] == 0) continue;
                ++count;
                left_pixels += x < 40 ? 1u : 0u;
            }
        }
        check(count > 0 && left_pixels == 0, "a water cell at the map's border has a bank on its gravel side and none at the border");
        // a water cell inside, with gravel on its left only, has the bank on its left: the same picture mirrored does not appear on the right
        const edges::EdgeMap inside = make_map({"gwwww", "gwwww", "gwwww"});
        const std::vector<uint8_t> ov2 = edges::make_overlay(inside, 1, 1, 3, solid.textures);
        size_t right_pixels = 0;
        for (int32_t y = 0; y < 96; ++y) {
            for (int32_t x = 56; x < 96; ++x) right_pixels += ov2[(static_cast<size_t>(y) * 96u + static_cast<size_t>(x)) * 4u + 3u] != 0 ? 1u : 0u;
        }
        check(!ov2.empty() && right_pixels == 0, "...and a bank on the left of a water cell has nothing on its right");
    }

    {   // the cells further than two away make no difference; a diagonal neighbor does (it rounds the corner)
        const Solid solid(2);
        std::vector<std::string> rows = {"wwwwwww", "wwwwwww", "wwwwwww", "wwwgwww", "wwwwwww", "wwwwwww", "wwwwwww"};
        const edges::EdgeMap a = make_map(rows);
        rows[0][0] = 'g';
        const edges::EdgeMap far_changed = make_map(rows);
        const std::vector<uint8_t> base = edges::make_overlay(a, 3, 2, 2, solid.textures);
        check(!base.empty() && edges::make_overlay(far_changed, 3, 2, 2, solid.textures) == base, "a cell three away changes nothing in a transition");
        rows = {"wwwwwww", "wwwwwww", "wwwwwww", "wwwgwww", "wwwwwww", "wwwwwww", "wwwwwww"};
        rows[2][2] = 'g';
        check(edges::make_overlay(make_map(rows), 3, 2, 2, solid.textures) != base, "a neighbor of a higher kind next to it changes it");
    }

    {   // a look for every pair that can meet: the lower kind's cell has the picture of that pair, the higher kind's has none
        const char kinds[5] = {'w', 'm', 'g', 'd', 's'};
        int32_t pairs = 0;
        for (size_t lo = 0; lo < 4; ++lo) {
            for (size_t hi = lo + 1; hi < 5; ++hi) {
                const Solid solid(3);
                const std::string row = std::string(3, kinds[lo]) + std::string(3, kinds[hi]);
                const edges::EdgeMap m = make_map({row, row, row});
                const std::vector<uint8_t> ov = edges::make_overlay(m, 2, 1, 3, solid.textures);
                const std::vector<uint8_t> upper = edges::make_overlay(m, 3, 1, 3, solid.textures);
                size_t count = 0;
                reach(ov, 3, 3, &count);
                check(count > 100 && upper.empty(), std::string("the pair ") + kinds[lo] + " against " + kinds[hi] + " has a transition in the lower kind's cell only (" + std::to_string(count) + " pixels)");
                ++pairs;
            }
        }
        check(pairs == 10, "ten pairs");
        // the colors: rock for slate, the original's bands for the soft pairs, steps of the land's color and a shadow for water
        const auto has_color = [](const std::vector<uint8_t>& ov, int r, int g, int b) {
            for (size_t i = 0; i < ov.size(); i += 4) {
                if (ov[i + 3] == 255 && ov[i] == r && ov[i + 1] == g && ov[i + 2] == b) return true;
            }
            return false;
        };
        const Solid s3(3);
        const auto pair_overlay = [&](char lo, char hi) {
            const std::string row = std::string(3, lo) + std::string(3, hi);
            return edges::make_overlay(make_map({row, row, row}), 2, 1, 3, s3.textures);
        };
        check(has_color(pair_overlay('g', 's'), 99, 99, 99) && has_color(pair_overlay('g', 's'), 147, 147, 147), "slate against gravel has the original's rock rim: its grey and its dark outline");
        check(has_color(pair_overlay('m', 'g'), 91, 87, 75) && has_color(pair_overlay('m', 'g'), 99, 99, 99), "gravel against mud has the original's two band colors");
        check(has_color(pair_overlay('g', 'd'), 179, 167, 155) && has_color(pair_overlay('g', 'd'), 139, 139, 115), "dirt against gravel has the original's three band colors");
        const std::string land_left = "gggwww";
        check(has_color(edges::make_overlay(make_map({land_left, land_left, land_left}), 3, 1, 3, s3.textures), 17, 40, 87), "water with land on its left, which faces the light, has the bank's shadow on the water");
    }

    {   // a bank has a lit lip where it faces the light (upper left) and a shaded face where it turns away: the outermost pixels of the bank in the middle rows of a water cell with gravel on its right (the bank's
        // edge toward the water faces left and up: lit) are lighter than those of a water cell with gravel on its left (the edge faces right and down: shaded), the shadow on the water not counted
        const Solid solid(3);
        const auto outer = [&](const std::vector<uint8_t>& ov, bool from_left) {
            double sum = 0.0;
            int rows = 0;
            for (int32_t y = 30; y < 66; ++y) {
                for (int32_t k = 0; k < 96; ++k) {
                    const int32_t x = from_left ? k : 95 - k;
                    const uint8_t* p = &ov[(static_cast<size_t>(y) * 96u + static_cast<size_t>(x)) * 4u];
                    if (p[3] != 255 || (p[0] == 17 && p[1] == 40 && p[2] == 87)) continue;     // (the water's shadow is not the bank)
                    sum += 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
                    ++rows;
                    break;
                }
            }
            return rows > 20 ? sum / rows : 0.0;
        };
        const double lit = outer(edges::make_overlay(make_map({"wwwwgggg", "wwwwgggg", "wwwwgggg"}), 3, 1, 3, solid.textures), true);
        const double shaded = outer(edges::make_overlay(make_map({"ggggwwww", "ggggwwww", "ggggwwww"}), 4, 1, 3, solid.textures), false);
        check(lit > 0.0 && lit > shaded + 20.0, "the edge of a bank that faces the light is lighter than one that turns away: " + std::to_string(lit) + " against " + std::to_string(shaded));
    }

    {   // on a grid the edge is thin; a shore's is not
        const Solid solid(3);
        const edges::EdgeMap chess = make_map({"wgwgw", "gwgwg", "wgwgw", "gwgwg", "wgwgw"});
        const std::vector<uint8_t> thin = edges::make_overlay(chess, 2, 2, 3, solid.textures);
        const edges::EdgeMap shore = make_map({"wwwwgggg", "wwwwgggg", "wwwwgggg"});
        check(!thin.empty(), "a cell of a checkerboard has a transition");
        if (thin.empty()) return;
        double far_from_sides = 0.0;
        for (int32_t y = 0; y < 96; ++y) {
            for (int32_t x = 0; x < 96; ++x) {
                if (thin[(static_cast<size_t>(y) * 96u + static_cast<size_t>(x)) * 4u + 3u] == 0) continue;
                const double px = (x + 0.5) / 3.0, py = (y + 0.5) / 3.0;
                far_from_sides = std::max(far_from_sides, std::min(std::min(px, 32.0 - px), std::min(py, 32.0 - py)));
            }
        }
        check(far_from_sides < 4.5, "a grid's edge is thin: no pixel of it is further than 4.5 pixels from a side: " + std::to_string(far_from_sides));
        double shore_far = 0.0;
        const std::vector<uint8_t> nat = edges::make_overlay(shore, 3, 1, 3, solid.textures);
        for (int32_t y = 0; y < 96; ++y) {
            for (int32_t x = 0; x < 96; ++x) {
                if (nat[(static_cast<size_t>(y) * 96u + static_cast<size_t>(x)) * 4u + 3u] == 0) continue;
                shore_far = std::max(shore_far, 32.0 - (x + 0.5) / 3.0);
            }
        }
        check(shore_far > 4.5, "...and a shore's is wider: " + std::to_string(shore_far));
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// [render]
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kW = 960;
constexpr int32_t kH = 540;

bool same_pixels(const zoomtest::Picture& a, const zoomtest::Picture& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }

std::string great_sea() { return std::string(ANTS_SOURCE_DIR) + "/Community-Maps/TheGreatSea.lvl"; }

/// A rig on The Great Sea (water against gravel on many cells) with the world art and without the dense mud, the camera where the most cells have a transition
struct Sea {
    Sea(const assets::AssetArchive& arc, const world_art::Pack& pack, int32_t os = 1) : rig(arc, great_sea(), kW, kH, 0, os) {
        rig.renderer.set_world_art(&pack);
        rig.renderer.set_dense_mud(false);
        rig.renderer.set_dense_mud_budget_ms(-1);
        int32_t best = -1;
        for (int32_t y = 0; y + 14 <= static_cast<int32_t>(rig.tiles_h); ++y) {
            for (int32_t x = 0; x + 22 <= static_cast<int32_t>(rig.tiles_w); ++x) {
                int32_t count = 0;
                for (int32_t j = 0; j < 14; ++j) {
                    for (int32_t i = 0; i < 22; ++i) count += rig.renderer.cell_has_transition(x + i, y + j) ? 1 : 0;
                }
                if (count > best) best = count, col = x, row = y;
            }
        }
        rig.world = rig.engine.get_world_state();
        rig.world.ants.clear();
        rig.look(col * 32, row * 32);
    }
    zoomtest::PixelRig rig;
    int32_t col{0};
    int32_t row{0};
};

void test_render(const assets::AssetArchive& arc, const world_art::Pack& pack) {
    group("render", "the real renderer: The Great Sea's shores");
    Sea sea(arc, pack);
    if (!sea.rig.ok) {
        check(false, "the rig is up");
        return;
    }
    Renderer& r = sea.rig.renderer;
    const LayoutRect view = sea.rig.layout.view();
    const int32_t cx0 = static_cast<int32_t>(sea.rig.cam().x) / 32, cy0 = static_cast<int32_t>(sea.rig.cam().y) / 32;
    r.set_transitions(false);
    const zoomtest::Picture off = sea.rig.shoot();
    r.set_transitions(true);
    const zoomtest::Picture on = sea.rig.shoot();
    check(!same_pixels(off, on), "the shores have transitions");
    size_t joined = 0, joined_changed = 0, plain = 0, plain_same = 0;
    for (int32_t y = cy0; y < cy0 + 14; ++y) {
        for (int32_t x = cx0; x < cx0 + 22; ++x) {
            const int32_t sx = view.x + x * 32 - static_cast<int32_t>(sea.rig.cam().x), sy = view.y + y * 32 - static_cast<int32_t>(sea.rig.cam().y);
            if (sx < view.x || sy < view.y || sx + 32 > view.x + view.w || sy + 32 > view.y + view.h) continue;
            bool same = true;
            for (int32_t j = 0; j < 32 && same; ++j) same = std::memcmp(off.at(sx, sy + j), on.at(sx, sy + j), 32u * 4u) == 0;
            if (r.cell_has_transition(x, y)) {
                ++joined;
                joined_changed += same ? 0u : 1u;
            } else {
                ++plain;
                plain_same += same ? 1u : 0u;
            }
        }
    }
    check(plain >= 10 && plain_same == plain, "every one of the " + std::to_string(plain) + " cells without a join is the same pixels: " + std::to_string(plain_same));
    check(joined >= 20 && joined_changed * 10 >= joined * 7, "most of the " + std::to_string(joined) + " cells with a join are changed: " + std::to_string(joined_changed));
    check(same_pixels(sea.rig.shoot(), on), "the same frame twice is the same pixels");

    // the transition is ground: ants lie over it exactly as they lie over the ground without it
    {
        r.set_transitions(false);
        const zoomtest::Picture ground = sea.rig.shoot();
        sea.rig.world.ants.clear();
        zoomtest::populate(sea.rig.world, arc, sea.col * 32, sea.row * 32, 22 * 32, 14 * 32, 200, 4242u);
        const zoomtest::Picture ants_off = sea.rig.shoot();
        r.set_transitions(true);
        const zoomtest::Picture ants_on = sea.rig.shoot();
        size_t ant_pixels = 0, kept = 0;
        for (int32_t y = view.y; y < view.y + view.h; ++y) {
            for (int32_t x = view.x; x < view.x + view.w; ++x) {
                if (std::memcmp(ground.at(x, y), ants_off.at(x, y), 3) == 0) continue;
                ++ant_pixels;
                kept += std::memcmp(ants_off.at(x, y), ants_on.at(x, y), 3) == 0 ? 1u : 0u;
            }
        }
        check(ant_pixels > 500 && kept == ant_pixels, "all " + std::to_string(ant_pixels) + " pixels that ants and their shadows cover are the same with the transitions on (" + std::to_string(kept) + ")");
        check(!same_pixels(ants_off, ants_on), "...while the transitions are there around them");
        sea.rig.world.ants.clear();
    }

    // Fog of War: the stipple that shrouds a cell is the same pixels, and nothing is shown that the fog hides
    {
        const int32_t W = static_cast<int32_t>(sea.rig.world.width), H = static_cast<int32_t>(sea.rig.world.height);
        sea.rig.world.fog_of_war_enabled = true;
        sea.rig.world.fog_revealed.assign(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
        r.set_transitions(false);
        const zoomtest::Picture fog_off = sea.rig.shoot();
        r.set_transitions(true);
        const zoomtest::Picture fog_on = sea.rig.shoot();
        const std::vector<uint8_t> stipple = arc.get_sprite(352).to_rgba32(arc.get_palette());
        size_t cells = 0, same_cells = 0;
        for (int32_t y = cy0 + 1; y < cy0 + 13; ++y) {
            for (int32_t x = cx0 + 1; x < cx0 + 21; ++x) {
                const int32_t sx = view.x + x * 32 - static_cast<int32_t>(sea.rig.cam().x), sy = view.y + y * 32 - static_cast<int32_t>(sea.rig.cam().y);
                if (!r.cell_has_transition(x, y)) continue;
                ++cells;
                bool same = true;
                for (int32_t j = 0; j < 32; ++j) {
                    for (int32_t i = 0; i < 32; ++i) {
                        if (stipple[(static_cast<size_t>(j) * 32u + static_cast<size_t>(i)) * 4u + 3u] != 0 && std::memcmp(fog_off.at(sx + i, sy + j), fog_on.at(sx + i, sy + j), 3) != 0) same = false;
                    }
                }
                same_cells += same ? 1u : 0u;
            }
        }
        check(cells >= 15 && same_cells == cells, "the stipple of Fog of War is the same pixels on all " + std::to_string(cells) + " shrouded cells with a transition (" + std::to_string(same_cells) + ")");
        sea.rig.world.fog_of_war_enabled = false;
    }

    // the state of a match is the same whether or not the transitions are drawn
    {
        sim::SimulationEngine other;
        other.init(sea.rig.level, 1337);
        for (int32_t t = 0; t < 40; ++t) {
            sea.rig.engine.tick();
            other.tick();
            if (t % 8 == 0) {
                sea.rig.world = sea.rig.engine.get_world_state();
                sea.rig.shoot();
            }
        }
        check(sea.rig.engine.state_hash() == other.state_hash(), "the state hash after 40 ticks of a match drawn with transitions is the one of a match that was not drawn");
    }

    // the memory is bounded; with no room at all the frame is the ground alone
    {
        Sea full(arc, pack);
        full.rig.shoot();
        const size_t all_bytes = full.rig.renderer.transition_bytes();
        const size_t all_cells = full.rig.renderer.transition_cells();
        check(all_cells > 20 && all_bytes == all_cells * 4096u, "a full view keeps the picture of every cell with a transition: " + std::to_string(all_cells) + " of 4 KB at 1x");
        Sea half(arc, pack);
        half.rig.renderer.set_transition_cache_limit(all_bytes / 2);
        half.rig.shoot();
        check(half.rig.renderer.transition_bytes() <= all_bytes / 2 && half.rig.renderer.transition_cells() > 0 && half.rig.renderer.transition_cells() < all_cells, "half the room: within it (" + std::to_string(half.rig.renderer.transition_bytes()) + " of " + std::to_string(all_bytes / 2) + " bytes), some cells have theirs");
        Sea none(arc, pack);
        none.rig.renderer.set_transition_cache_limit(0);
        const zoomtest::Picture nothing = none.rig.shoot();
        none.rig.renderer.set_transitions(false);
        check(none.rig.renderer.transition_cells() == 0 && same_pixels(nothing, none.rig.shoot()), "no room at all is exactly the ground alone");
        // moving away lets go of what is not in view; coming back gives the same picture
        Sea walk(arc, pack);
        walk.rig.renderer.set_transition_cache_limit(all_bytes + all_bytes / 8);
        const zoomtest::Picture home = walk.rig.shoot();
        walk.rig.look(walk.col * 32 + 400, walk.row * 32 + 250);
        walk.rig.shoot();
        check(walk.rig.renderer.transition_bytes() <= all_bytes + all_bytes / 8, "after the view has moved the memory is within the limit: " + std::to_string(walk.rig.renderer.transition_bytes()));
        walk.rig.look(walk.col * 32, walk.row * 32);
        check(same_pixels(walk.rig.shoot(), home), "...and the first view comes back as the same picture");
    }

    // the work of a frame: a small budget makes a cell a frame and gets to the same picture
    {
        Sea slow(arc, pack);
        slow.rig.renderer.set_dense_mud_budget_ms(0);
        const zoomtest::Picture first = slow.rig.shoot();
        check(!same_pixels(first, on) && slow.rig.renderer.transition_cells() >= 1 && slow.rig.renderer.transition_cells() <= 2, "with no budget the first frame has one cell's transition (" + std::to_string(slow.rig.renderer.transition_cells()) + ")");
        zoomtest::Picture last = first;
        int32_t frames = 1;
        size_t before = 0;
        while (frames < 3000) {
            last = slow.rig.shoot();
            ++frames;
            if (slow.rig.renderer.transition_cells() == before && same_pixels(last, on)) break;
            before = slow.rig.renderer.transition_cells();
        }
        check(same_pixels(last, on), "...and goes on, a cell a frame, to the picture that the unlimited budget gives (" + std::to_string(frames) + " frames)");
    }

    // a larger output: made at the largest scale that is allowed
    {
        Sea big(arc, pack, 3);
        big.rig.renderer.set_transition_max_scale(2);
        big.rig.shoot();
        const size_t cells = big.rig.renderer.transition_cells();
        check(cells > 10 && big.rig.renderer.transition_bytes() == cells * 4u * 64u * 64u, "at the output of 3x with a limit of scale 2 the pictures are 2x (16 KB a cell): " + std::to_string(big.rig.renderer.transition_bytes()));
        Sea three(arc, pack, 3);
        three.rig.shoot();
        check(three.rig.renderer.transition_bytes() == three.rig.renderer.transition_cells() * 4u * 96u * 96u, "...and 3x (37 KB a cell) without it");
    }
    check(edges::kCacheLimit > 0 && edges::kMaxScale >= 2, "the defaults are there: " + std::to_string(edges::kCacheLimit / 1024 / 1024) + " MB, scale " + std::to_string(edges::kMaxScale));
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
    test_kinds();
    test_overlay();
    test_render(arc, pack);
    std::printf("\nworld art edges: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
