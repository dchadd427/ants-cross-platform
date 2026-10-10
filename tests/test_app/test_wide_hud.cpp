// The wide match screen (milestone M3 of the widescreen work): the 16:9 picture of 960 x 540 with its frame grown from the original's own art, and everything that follows from it.
//   * the anchoring model (include/ants_app/shell_layout.hpp): each of the 14 pieces of the original's frame animation `uishell` at 960 x 540 (and at other sizes): where it is, how big it
//     is, which of them are stretched, and that the spans of a piece tile it without a gap or an overlap;
//   * the cuts: for every stretched piece the repeated line lies inside a run of identical lines of the piece's own plain part (measured on the art of ants.chd: the top bar's columns
//     138 - 143, the bottom strip's three cuts in 13 - 17, 186 - 190 and 345 - 348, each identical to its neighbours on all 19 rows, one in each gap between the recesses of the score boxes, and never the panel's own fill 458 - 482, the left strip's rows 294 - 314) and, for the right panel, the three pieces that span the
//     mock-up's row (canvas y = 357) are plain there between their ant decorations; a cut where the art is not plain fails the same tests;
//   * the picture: the frame composed from the spans is, pixel for pixel, the mock-up's semantics (one line repeated) written out independently, at several sizes, and with the
//     original's picture the plain 14 pieces; the real renderer draws the spans exactly (draw_sprite_region, set_origin);
//   * the HUD: it draws the frame first, offsets the panel's animations by the layout, puts the options window and the quick help of a match over the middle of the map view (the frame
//     stays around them) like its dialogs, and takes the pointer back to the numbers of those windows in the events AND in the 50 ms poll of HUD::update (every control is clicked
//     through it); the score boxes are slots (no 4 in the layout code), spread over the strip by its three cuts (columns 15, 188, 346: dx in thirds); the readout of a network match stays
//     clear of them (the match's limit is the layout's);
//   * the view: the camera clamps (a big map, a map smaller than the view: centred, black around), the edge strips of the whole picture, the start view, the minimap's frame, the cursor
//     outside a small map (a click there does nothing at all), nothing drawn onto the black around a small map (ants at the edges, levels cut from TINY.LVL), the listener;
//   * the application: the picture of a match is the whole canvas and so is every other screen (each has its own wide page: test_wide_pages), the pointer stays where it is; only a picture
//     that is smaller than the canvas (a match of the original's layout, a test hook) holds a pointer that was beside it at its nearest edge pixel; the default is 16:9 on a desktop
//     (`--aspect 4:3` and the settings key give the classic picture).
// Usage: test_wide_hud. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/shell_layout.hpp"
#include "ants_app/version.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

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

std::string str(const LayoutRect& r) {
    std::ostringstream o;
    o << "(" << r.x << ", " << r.y << ", " << r.w << " x " << r.h << ")";
    return o.str();
}

void check_rect(const LayoutRect& got, const LayoutRect& want, const std::string& what) {
    check(got == want, what + ": " + str(got) + " should be " + str(want));
}

void ensure_sdl() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) SDL_Init(SDL_INIT_VIDEO);
}

/// The renderer and the application print what font they found on std::cout: that is not of interest here
class QuietStdout {
public:
    QuietStdout() : old_(std::cout.rdbuf(sink_.rdbuf())) {}
    ~QuietStdout() { std::cout.rdbuf(old_); }
    QuietStdout(const QuietStdout&) = delete;
    QuietStdout& operator=(const QuietStdout&) = delete;

private:
    std::ostringstream sink_;
    std::streambuf* old_;
};

// =====================================================================================================================================================
// A picture of palette indices: what the art composes to
// =====================================================================================================================================================

struct Pic {
    int32_t w{0};
    int32_t h{0};
    std::vector<int16_t> idx;                       // the palette index of every pixel, -1 where nothing was drawn
    Pic(int32_t width, int32_t height) : w(width), h(height), idx(static_cast<size_t>(width) * static_cast<size_t>(height), -1) {}
    int16_t at(int32_t x, int32_t y) const { return idx[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
    void put(int32_t x, int32_t y, int16_t v) {
        if (x >= 0 && y >= 0 && x < w && y < h) idx[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = v;
    }
};

/// The pixels of the rectangle `src` of a sprite stretched to the rectangle `dst` of the picture, nearest neighbour (a source one pixel wide is that column repeated)
void blit_span(Pic& pic, const assets::Sprite& sprite, const LayoutRect& dst, const LayoutRect& src) {
    for (int32_t y = 0; y < dst.h; ++y) {
        for (int32_t x = 0; x < dst.w; ++x) {
            const int32_t sx = src.x + x * src.w / dst.w;
            const int32_t sy = src.y + y * src.h / dst.h;
            const uint8_t v = sprite.get_pixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
            if (v != assets::CHD_COLOR_KEY_INDEX) pic.put(dst.x + x, dst.y + y, v);
        }
    }
}

/// The frame composed from the production model: the pieces of `uishell`, the last stored first, each as the spans of shell_spans
Pic compose_from_spans(const assets::AssetArchive& arc, const ScreenLayout& layout) {
    Pic pic(layout.width, layout.height);
    const auto* seq = arc.find_animation("uishell");
    if (seq == nullptr || seq->subitems.empty()) return pic;
    const auto& frames = seq->subitems[0].frames;
    for (size_t k = frames.size(); k-- > 0;) {
        const auto& part = frames[k];
        const assets::Sprite& sprite = arc.get_sprite(part.sprite_index);
        const ShellSpans spans = shell_spans(shell_rule(sprite.name), part.dx, part.dy, static_cast<int32_t>(sprite.width), static_cast<int32_t>(sprite.height), layout);
        for (size_t i = 0; i < spans.count; ++i) blit_span(pic, sprite, spans.span[i].dst, spans.span[i].src);
    }
    return pic;
}

/// The approved mock-up of 2026-10-02 (a small tool that composes the frame from the original's pieces), written out here independently of the production header: the 14 pieces in the order the original draws them, each moved by the
/// right / bottom anchors and drawn with ONE of its columns or rows repeated dW or dH times (the line `col` / `row`)
struct RefPiece {
    const char* name;
    int32_t x, y;
    bool right, bottom;
    std::array<int32_t, 3> cols;     // the repeated columns (-1: none); dW is shared between them in thirds, the leftmost taking the remainder
    int32_t row;                     // the repeated row (-1: none)
};
constexpr RefPiece kReference[14] = {
    {"x0y22.bmp", 0, 22, false, false, {-1, -1, -1}, 300},        // the left strip: taller below its horizontal rule
    {"x458y22.bmp", 458, 22, true, false, {-1, -1, -1}, -1},      // the top of the right panel
    {"x458y35.bmp", 458, 35, true, false, {-1, -1, -1}, 322},     // the strip between the map and the panel: taller at canvas row 357
    {"x480y126.bmp", 480, 126, true, false, {-1, -1, -1}, -1},
    {"x480y266.bmp", 480, 266, true, false, {-1, -1, -1}, -1},
    {"x480y400.bmp", 480, 400, true, true, {-1, -1, -1}, -1},
    {"x480y466.bmp", 480, 436, true, true, {-1, -1, -1}, -1},
    {"x521y254.bmp", 621, 254, true, false, {-1, -1, -1}, 103},   // the right edge strip: taller at canvas row 357
    {"x599y35.bmp", 599, 35, true, false, {-1, -1, -1}, -1},
    {"wchat.bmp", 479, 298, true, false, {-1, -1, -1}, 59},       // the chat log's box: taller at canvas row 357
    {"wtype.bmp", 479, 423, true, true, {-1, -1, -1}, -1},
    {"wstatus.bmp", 479, 253, true, false, {-1, -1, -1}, -1},
    {"x0y0.bmp", 0, 0, false, false, {140, -1, -1}, -1},          // the top bar: wider at its plain green
    {"x17y461.bmp", 17, 461, false, true, {15, 188, 346}, -1},    // the bottom strip: wider at three plain cuts (the score boxes are spread), down by dH
};

/// The mock-up's rule (the reference tool, draw_multi): the extra width dW is shared between the cuts in thirds (a third each for three cuts; the leftmost takes the remainder), and
/// every cut column is repeated by its share
std::vector<int32_t> reference_shares(int32_t dw, size_t cuts) {
    std::vector<int32_t> shares(cuts, cuts > 0 ? dw / static_cast<int32_t>(cuts) : 0);
    if (cuts > 0) shares[0] = dw - (static_cast<int32_t>(cuts) - 1) * (dw / static_cast<int32_t>(cuts));
    return shares;
}

/// The columns of a piece of `w` columns that is widened by dw at the cut columns `cuts` (ascending): (source column, destination column within the piece), the mock-up's draw_multi
std::vector<std::pair<int32_t, int32_t>> reference_columns(int32_t w, const std::vector<int32_t>& cuts, int32_t dw) {
    const std::vector<int32_t> shares = reference_shares(dw, cuts.size());
    std::vector<std::pair<int32_t, int32_t>> columns;
    int32_t shift = 0;
    for (int32_t sx = 0; sx < w; ++sx) {
        columns.emplace_back(sx, sx + shift);
        for (size_t i = 0; i < cuts.size(); ++i) {
            if (cuts[i] != sx) continue;
            for (int32_t k = 1; k <= shares[i]; ++k) columns.emplace_back(sx, sx + shift + k);
            shift += shares[i];
        }
    }
    return columns;
}

Pic compose_reference(const assets::AssetArchive& arc, int32_t width, int32_t height) {
    Pic pic(width, height);
    const int32_t dw = width - 640;
    const int32_t dh = height - 480;
    for (const RefPiece& p : kReference) {
        const assets::Sprite* sprite = arc.find_sprite(p.name);
        if (sprite == nullptr) continue;
        const int32_t ox = p.right ? dw : 0;
        const int32_t oy = p.bottom ? dh : 0;
        std::vector<int32_t> cuts;
        for (const int32_t c : p.cols) {
            if (c >= 0) cuts.push_back(c);
        }
        const int32_t eh = p.row >= 0 ? dh : 0;
        // the columns: a source column goes to its place, shifted by the shares of the cuts left of it, and a cut column is written 1 + its share times
        const std::vector<std::pair<int32_t, int32_t>> columns = reference_columns(static_cast<int32_t>(sprite->width), cuts, dw);
        for (int32_t y = 0; y < static_cast<int32_t>(sprite->height) + eh; ++y) {
            const int32_t sy = (eh > 0 && y > p.row) ? (y <= p.row + eh ? p.row : y - eh) : y;
            for (const auto& col : columns) {
                const uint8_t v = sprite->get_pixel(static_cast<uint32_t>(col.first), static_cast<uint32_t>(sy));
                if (v != assets::CHD_COLOR_KEY_INDEX) pic.put(p.x + ox + col.second, p.y + oy + y, v);
            }
        }
    }
    return pic;
}

int64_t differing_pixels(const Pic& a, const Pic& b) {
    if (a.w != b.w || a.h != b.h) return -1;
    int64_t n = 0;
    for (size_t i = 0; i < a.idx.size(); ++i) n += a.idx[i] != b.idx[i] ? 1 : 0;
    return n;
}

// =====================================================================================================================================================
// 1. The anchoring model
// =====================================================================================================================================================

/// The 14 pieces of `uishell` as the original places them (animation frames of Table 4: the part's offset and the sprite's size) and the bounding box of each in a picture of
/// 960 x 540 (dx = 320, dy = 60), written out by hand: the right panel is pinned to the right edge, the bottom parts go down by 60, the pieces that are stretched grow
struct PieceNumbers {
    const char* name;
    LayoutRect classic;
    LayoutRect wide;
};
constexpr PieceNumbers kPieces[14] = {
    {"x17y461.bmp", {17, 461, 623, 19}, {17, 521, 943, 19}},       // the bottom strip: down by 60, 320 wider
    {"x0y0.bmp", {0, 0, 640, 22}, {0, 0, 960, 22}},                // the top bar: 320 wider
    {"wstatus.bmp", {479, 253, 143, 14}, {799, 253, 143, 14}},     // the status box: right
    {"wtype.bmp", {479, 423, 143, 14}, {799, 483, 143, 14}},       // the chat input box: right and down
    {"wchat.bmp", {479, 298, 143, 103}, {799, 298, 143, 163}},     // the chat log's box: right, 60 taller
    {"x599y35.bmp", {599, 35, 41, 91}, {919, 35, 41, 91}},         // the minimap's bezel: right
    {"x521y254.bmp", {621, 254, 19, 182}, {941, 254, 19, 242}},    // the right edge strip: right, 60 taller
    {"x480y466.bmp", {480, 436, 160, 25}, {800, 496, 160, 25}},    // the panel's bottom: right and down
    {"x480y400.bmp", {480, 400, 141, 24}, {800, 460, 141, 24}},    // the ant relief under the chat log: right and down
    {"x480y266.bmp", {480, 266, 141, 33}, {800, 266, 141, 33}},    // the chat header: right
    {"x480y126.bmp", {480, 126, 160, 128}, {800, 126, 160, 128}},  // the status card: right
    {"x458y35.bmp", {458, 35, 22, 426}, {778, 35, 22, 486}},       // the strip between the map and the panel: right, 60 taller
    {"x458y22.bmp", {458, 22, 182, 13}, {778, 22, 182, 13}},       // the top of the panel: right
    {"x0y22.bmp", {0, 22, 17, 458}, {0, 22, 17, 518}},             // the left strip: 60 taller
};

/// The spans are disjoint and their areas add up to the box's: they tile it (every span lies inside the box)
bool tiles_without_gap(const ShellSpans& spans, const LayoutRect& box) {
    int64_t area = 0;
    for (size_t i = 0; i < spans.count; ++i) {
        const LayoutRect& d = spans.span[i].dst;
        if (d.x < box.x || d.y < box.y || d.right() > box.right() || d.bottom() > box.bottom()) return false;
        area += static_cast<int64_t>(d.w) * d.h;
        for (size_t j = i + 1; j < spans.count; ++j) {
            const LayoutRect& e = spans.span[j].dst;
            if (d.x < e.right() && e.x < d.right() && d.y < e.bottom() && e.y < d.bottom()) return false;
        }
    }
    return area == static_cast<int64_t>(box.w) * box.h;
}

void test_anchoring(const assets::AssetArchive& arc) {
    group("anchors", "each of the 14 pieces of the frame at 960 x 540: where it is, how big, how it is stretched, and that its spans tile it");
    const auto* seq = arc.find_animation("uishell");
    check(seq != nullptr && !seq->subitems.empty() && seq->subitems[0].frames.size() == 14, "the animation uishell has 14 pieces");
    if (seq == nullptr || seq->subitems.empty() || seq->subitems[0].frames.size() != 14) return;
    const auto& frames = seq->subitems[0].frames;
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    check(kShellRules.size() == 14, "the model has a rule for each of the 14");
    for (size_t k = 0; k < 14; ++k) {
        const assets::Sprite& sprite = arc.get_sprite(frames[k].sprite_index);
        const PieceNumbers& want = kPieces[k];
        const std::string at = "piece " + std::to_string(k) + " " + want.name + ": ";
        check(sprite.name == want.name, at + "the animation's name is " + sprite.name);
        const LayoutRect got{frames[k].dx, frames[k].dy, static_cast<int32_t>(sprite.width), static_cast<int32_t>(sprite.height)};
        check_rect(got, want.classic, at + "the original's place and size (the archive)");
        const ShellRule* rule = shell_rule(sprite.name);
        check(rule != nullptr && std::string(kShellRules[k].sprite) == want.name && rule == &kShellRules[k], at + "the model's rule is in the animation's order");
        // the original's picture: one span, the whole piece, where the original puts it
        const ShellSpans plain = shell_spans(rule, got.x, got.y, got.w, got.h, classic);
        check(plain.count == 1 && plain.span[0].whole && plain.span[0].dst == got && plain.span[0].src == (LayoutRect{0, 0, got.w, got.h}), at + "the classic picture draws it whole at its place");
        // 960 x 540
        const ShellSpans spans = shell_spans(rule, got.x, got.y, got.w, got.h, wide);
        check(spans.count >= 1 && tiles_without_gap(spans, want.wide), at + "its spans tile " + str(want.wide));
        // the source of every span lies inside the sprite
        bool inside = true;
        for (size_t i = 0; i < spans.count; ++i) {
            const LayoutRect& s = spans.span[i].src;
            inside = inside && s.x >= 0 && s.y >= 0 && s.right() <= got.w && s.bottom() <= got.h;
        }
        check(inside, at + "every span reads inside the sprite");
    }
    // how many of them grow: the top bar and the bottom strip in width, the left strip, the strip beside the map, the chat box and the right edge strip in height
    int widened = 0, heightened = 0, moved_right = 0, moved_down = 0;
    for (size_t k = 0; k < 14; ++k) {
        widened += kShellRules[k].cut_cols[0] >= 0 ? 1 : 0;
        heightened += kShellRules[k].cut_row >= 0 ? 1 : 0;
        moved_right += kShellRules[k].right ? 1 : 0;
        moved_down += kShellRules[k].bottom ? 1 : 0;
    }
    check(widened == 2 && heightened == 4, "two pieces are widened and four are made taller");
    check(moved_right == 11 && moved_down == 4, "eleven pieces are right anchored (the top bar, the bottom strip and the left strip are not), four go down");

    // other sizes follow the same rule: right anchored +dx, bottom anchored +dy, widened +dx, heightened +dy
    const struct { int32_t w, h; } sizes[] = {{1280, 720}, {1920, 1080}, {700, 500}, {960, 480}, {640, 540}};
    for (const auto& sz : sizes) {
        const ScreenLayout l = ScreenLayout::with_size(sz.w, sz.h);
        const int32_t dx = l.dx(), dy = l.dy();
        bool ok = true;
        for (size_t k = 0; k < 14; ++k) {
            const LayoutRect c = kPieces[k].classic;
            const LayoutRect want{c.x + (kShellRules[k].right ? dx : 0), c.y + (kShellRules[k].bottom ? dy : 0), c.w + (kShellRules[k].cut_cols[0] >= 0 ? dx : 0),
                                  c.h + (kShellRules[k].cut_row >= 0 ? dy : 0)};
            const ShellSpans spans = shell_spans(&kShellRules[k], c.x, c.y, c.w, c.h, l);
            ok = ok && tiles_without_gap(spans, want);
        }
        check(ok, std::to_string(sz.w) + " x " + std::to_string(sz.h) + ": every piece's spans tile its box (right +" + std::to_string(dx) + ", down +" + std::to_string(dy) + ")");
    }
    // a name that is not a piece of the frame is one plain copy where the animation puts it
    const ShellSpans foreign = shell_spans(shell_rule("somewhere.bmp"), 10, 20, 30, 40, wide);
    check(shell_rule("somewhere.bmp") == nullptr && foreign.count == 1 && foreign.span[0].whole && foreign.span[0].dst == (LayoutRect{10, 20, 30, 40}), "a sprite that is not a piece is not moved");
}

// =====================================================================================================================================================
// 2. The cuts: the repeated line lies in the plain part of the art
// =====================================================================================================================================================

/// The number of pixels in which two lines (rows when `rows`, columns otherwise) of a sprite differ
int32_t line_diff(const assets::Sprite& s, bool rows, int32_t a, int32_t b) {
    const int32_t n = rows ? static_cast<int32_t>(s.width) : static_cast<int32_t>(s.height);
    int32_t d = 0;
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t pa = rows ? s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(a)) : s.get_pixel(static_cast<uint32_t>(a), static_cast<uint32_t>(i));
        const uint8_t pb = rows ? s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(b)) : s.get_pixel(static_cast<uint32_t>(b), static_cast<uint32_t>(i));
        d += pa != pb ? 1 : 0;
    }
    return d;
}

/// The run of identical lines around line c: the first and the last
std::pair<int32_t, int32_t> identical_run(const assets::Sprite& s, bool rows, int32_t c) {
    const int32_t n = rows ? static_cast<int32_t>(s.height) : static_cast<int32_t>(s.width);
    int32_t lo = c, hi = c;
    while (lo > 0 && line_diff(s, rows, lo - 1, lo) == 0) --lo;
    while (hi + 1 < n && line_diff(s, rows, hi, hi + 1) == 0) ++hi;
    return {lo, hi};
}

/// The distance of line c to the nearest decoration edge: a place where two neighbouring lines differ by at least `strong` pixels
int32_t distance_to_decoration(const assets::Sprite& s, bool rows, int32_t c, int32_t strong) {
    const int32_t n = rows ? static_cast<int32_t>(s.height) : static_cast<int32_t>(s.width);
    int32_t best = 1 << 20;
    for (int32_t i = 1; i < n; ++i) {
        if (line_diff(s, rows, i - 1, i) >= strong) best = std::min(best, std::min(std::abs(i - c), std::abs(i - 1 - c)));
    }
    return best;
}

void test_cuts(const assets::AssetArchive& arc) {
    group("cuts", "every repeated line lies in the plain part of its piece: inside a run of identical lines, away from the clock box, the score boxes and the ant decorations");
    auto sprite = [&](const char* name) -> const assets::Sprite& { return *arc.find_sprite(name); };
    auto rule = [&](const char* name) -> const ShellRule& { return *shell_rule(name); };

    // --- the three runs of identical lines, measured on the art
    {   // the top bar: columns 138 - 143 (the clock box's right edge is column 129 - 132, the next decoration is far right)
        const assets::Sprite& s = sprite("x0y0.bmp");
        const int32_t c = rule("x0y0.bmp").cut_cols[0];
        const auto run = identical_run(s, false, c);
        check(col_cut_count(rule("x0y0.bmp")) == 1 && rule("x0y0.bmp").cut_cols[1] < 0 && rule("x0y0.bmp").cut_cols[2] < 0, "x0y0: one cut");
        check(run.first == 138 && run.second == 143, "x0y0: the run of identical columns around the cut is 138 - 143 (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
        check(c >= run.first + 1 && c <= run.second - 1, "x0y0: the cut column " + std::to_string(c) + " has identical columns on both sides of it");
        check(c > 132, "x0y0: the cut is right of the black clock box (its edge is column 129 - 132)");
        check(line_diff(s, false, c - 1, c) == 0 && line_diff(s, false, c, c + 1) == 0, "x0y0: the neighbours of the cut continue it exactly");
    }
    {   // the bottom strip: THREE cuts, one left of each score box: the plain band left of the first box (columns 13 - 17: 52.P correction), the plain run 186 - 190 right of the first box
        // and left of the second team's label, and the plain run 345 - 348 left of the third team's label; NOT the panel's own fill, columns 458 - 482
        const assets::Sprite& s = sprite("x17y461.bmp");
        const ShellRule& strip = rule("x17y461.bmp");
        check(col_cut_count(strip) == 3 && strip.cut_cols[0] == 15 && strip.cut_cols[1] == 188 && strip.cut_cols[2] == 346, "x17y461: three cuts, at the columns 15, 188 and 346");
        const struct { int32_t col, first, last; } runs[3] = {{15, 13, 17}, {188, 186, 190}, {346, 345, 348}};
        const ScreenLayout classic = ScreenLayout::classic();
        for (size_t i = 0; i < 3; ++i) {
            const int32_t c = strip.cut_cols[i];
            const std::string at = "x17y461 cut " + std::to_string(i) + " (column " + std::to_string(c) + "): ";
            const auto run = identical_run(s, false, c);
            check(run.first == runs[i].first && run.second == runs[i].last, at + "the run of identical columns around it is " + std::to_string(runs[i].first) + " - " + std::to_string(runs[i].last) + " (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
            check(c >= run.first + 1 && c <= run.second - 1, at + "it has identical columns on both sides");
            check(static_cast<int32_t>(s.height) == 19 && line_diff(s, false, c - 1, c) == 0 && line_diff(s, false, c, c + 1) == 0, at + "it is identical to its neighbours on all 19 rows");
            int32_t transparent = 0;
            for (int32_t y = 0; y < static_cast<int32_t>(s.height); ++y) transparent += s.get_pixel(static_cast<uint32_t>(c), static_cast<uint32_t>(y)) == assets::CHD_COLOR_KEY_INDEX ? 1 : 0;
            check(transparent == 0, at + "no row of it is transparent: the repeated column is solid art");
            // the cuts and the boxes: the recess of box b (54 wide) is at column box_left - 17; cut i is left of box i + 1 and right of the recess before it, so one cut sits in each gap
            const int32_t recess_left = classic.score_slot(i + 1).box_left - 17;
            check(c < recess_left - 1, at + "it is left of the recess of the box it moves (column " + std::to_string(recess_left) + ")");
            if (i > 0) {
                const int32_t before = classic.score_slot(i).box_left - 17;
                check(c > before + 54, at + "and right of the recess of the box before it (columns " + std::to_string(before) + " - " + std::to_string(before + 53) + "): the recesses are never cut");
            }
        }
        const auto fill_run = identical_run(s, false, 470);
        check(fill_run.first >= 457 && fill_run.second >= 482, "x17y461: columns 458 - 482 are an identical run too (the control: identical alone is not enough)");
        for (size_t i = 0; i < 3; ++i) check(!(strip.cut_cols[i] >= 458 && strip.cut_cols[i] <= 482), "x17y461: ... but that run is the right panel's own fill, and no cut is in it (a stretch there puts a flat block between the end ornament and the panel)");
        // the recesses themselves are long runs of identical columns too (the score boxes' flat inside): the control that a cut inside one would pass "identical" and has to fail the position test
        check(identical_run(s, false, 100).first <= 90 && identical_run(s, false, 100).second >= 136, "x17y461: the first recess is a run of identical columns (control: cutting there would be 'identical' and is excluded by the recess test)");
    }
    {   // the left strip: rows 294 - 314, below the horizontal rule (rows 260 - 266: it stays level with the chat header)
        const assets::Sprite& s = sprite("x0y22.bmp");
        const int32_t r = rule("x0y22.bmp").cut_row;
        const auto run = identical_run(s, true, r);
        check(run.first == 294 && run.second == 314, "x0y22: the run of identical rows around the cut is 294 - 314 (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
        check(r >= run.first + 1 && r <= run.second - 1, "x0y22: the cut row " + std::to_string(r) + " has identical rows on both sides");
        check(r > 266, "x0y22: the cut is below the horizontal rule (rows 260 - 266)");
        check(line_diff(s, true, 259, 260) >= 10 && line_diff(s, true, 266, 267) >= 1, "x0y22: the rule's rows are decoration (the control: the rows 260 - 266 are not plain)");
    }

    // --- the right panel: the strip beside the map, the chat box and the right edge strip are cut at ONE canvas row, y = 357
    const char* trio[3] = {"x458y35.bmp", "wchat.bmp", "x521y254.bmp"};
    for (const char* name : trio) {
        const assets::Sprite& s = sprite(name);
        const PieceNumbers* pn = nullptr;
        for (const PieceNumbers& p : kPieces) {
            if (std::string(p.name) == name) pn = &p;
        }
        const int32_t row = rule(name).cut_row;
        check(pn != nullptr && pn->classic.y + row == 357, std::string(name) + ": the cut is at canvas row 357 (the piece starts at " + std::to_string(pn ? pn->classic.y : -1) + ", row " + std::to_string(row) + ")");
        const int32_t above = line_diff(s, true, row - 1, row);
        const int32_t below = line_diff(s, true, row, row + 1);
        const std::string at = std::string(name) + ": ";
        check(above <= 5 && below <= 5, at + "the repeated row differs from its neighbours by " + std::to_string(above) + " and " + std::to_string(below) + " pixels (at most 5)");
        if (std::string(name) != "wchat.bmp") {
            const int32_t away = distance_to_decoration(s, true, row, 10);
            check(away >= 8, at + "the nearest ant decoration (rows that differ by 10 or more pixels) is " + std::to_string(away) + " rows away (at least 8)");
        } else {
            const auto run = identical_run(s, true, row);
            check(run.first <= 40 && run.second >= 80, at + "the chat box is a flat fill: the run of identical rows around the cut is " + std::to_string(run.first) + " - " + std::to_string(run.second));
        }
    }
    // every piece of the right panel that spans canvas row 357 is cut there: none is left whole across it
    {
        int spanning = 0;
        bool all_cut = true;
        for (size_t k = 0; k < 14; ++k) {
            const LayoutRect c = kPieces[k].classic;
            if (c.x < 458 || !(c.y <= 357 && 357 < c.bottom())) continue;
            ++spanning;
            all_cut = all_cut && kShellRules[k].cut_row >= 0 && c.y + kShellRules[k].cut_row == 357;
        }
        check(spanning == 3 && all_cut, "the pieces of the right panel that span canvas row 357 are exactly the three, and each is cut at it (" + std::to_string(spanning) + ")");
        // the rejected row: the right edge strip's row 50 (canvas y 304) stretches an ant decoration into a long bar
        const assets::Sprite& edge = sprite("x521y254.bmp");
        const bool plain_neighbours = line_diff(edge, true, 49, 50) <= 5 && line_diff(edge, true, 50, 51) <= 5;
        check(!(plain_neighbours && distance_to_decoration(edge, true, 50, 10) >= 8), "the control: x521y254 row 50, which was rejected, fails the same tests (it is beside a decoration)");
    }
    // the cuts together: the model's anchors and cut lines are the ones the independent mock-up uses
    for (const RefPiece& p : kReference) {
        const ShellRule& r = rule(p.name);
        check(r.cut_cols == p.cols && r.cut_row == p.row && r.right == p.right && r.bottom == p.bottom, std::string(p.name) + ": the model's anchors and cuts agree with the mock-up (" + std::to_string(r.cut_cols[0]) + ", " + std::to_string(r.cut_row) + ")");
    }
}

// =====================================================================================================================================================
// 3. The composed picture
// =====================================================================================================================================================

void test_composition(const assets::AssetArchive& arc) {
    group("compose", "the frame composed from the spans is the mock-up's one-line repeat, pixel for pixel, at every size; the original's picture is the plain 14 pieces");
    const struct { int32_t w, h; } sizes[] = {{960, 540}, {1280, 720}, {1920, 1080}, {700, 500}, {960, 480}, {640, 540}, {640, 480}};
    for (const auto& sz : sizes) {
        const Pic spans = compose_from_spans(arc, ScreenLayout::with_size(sz.w, sz.h));
        const Pic reference = compose_reference(arc, sz.w, sz.h);
        const int64_t diff = differing_pixels(spans, reference);
        check(diff == 0, std::to_string(sz.w) + " x " + std::to_string(sz.h) + ": the spans compose the mock-up's picture (" + std::to_string(diff) + " pixels differ)");
    }
    // the picture has exactly the holes of the original (the map view and the art's own openings): the stretch leaves none
    {
        const Pic wide = compose_from_spans(arc, ScreenLayout::with_size(960, 540));
        const Pic classic = compose_from_spans(arc, ScreenLayout::classic());
        int64_t holes_wide = 0, holes_classic = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) {
                const bool in_view = x >= 17 && x < 778 && y >= 22 && y < 521;
                if (!in_view && wide.at(x, y) < 0) ++holes_wide;
            }
        }
        for (int32_t y = 0; y < 480; ++y) {
            for (int32_t x = 0; x < 640; ++x) {
                const bool in_view = x >= 17 && x < 458 && y >= 22 && y < 461;
                if (!in_view && classic.at(x, y) < 0) ++holes_classic;
            }
        }
        check(holes_wide == holes_classic, "the wide frame has exactly the original's pixels that the art leaves open (" + std::to_string(holes_classic) + "), outside the map view");
    }
    // each stretched piece at its original size equals the original, and at the new size its added lines are the repeated one
    for (size_t k = 0; k < 14; ++k) {
        const ShellRule& rule = kShellRules[k];
        if (rule.cut_cols[0] < 0 && rule.cut_row < 0) continue;
        const assets::Sprite& s = *arc.find_sprite(rule.sprite);
        const LayoutRect c = kPieces[k].classic;
        // at the original's size: the spans are the whole piece, and drawing them gives the sprite
        Pic one(c.x + c.w + 4, c.y + c.h + 4);
        const ShellSpans spans0 = shell_spans(&rule, c.x, c.y, c.w, c.h, ScreenLayout::classic());
        for (size_t i = 0; i < spans0.count; ++i) blit_span(one, s, spans0.span[i].dst, spans0.span[i].src);
        bool equal = true;
        for (int32_t y = 0; y < c.h; ++y) {
            for (int32_t x = 0; x < c.w; ++x) {
                const uint8_t v = s.get_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                equal = equal && one.at(c.x + x, c.y + y) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
            }
        }
        check(equal, std::string(rule.sprite) + " drawn at its original size is the original");
        // at 960 x 540: lines before a cut are the original's, the added lines are the cut line, the lines after are the original's, moved (a piece with several cuts: dx shared between them)
        const ScreenLayout wide = ScreenLayout::with_size(960, 540);
        const ShellSpans spans = shell_spans(&rule, c.x, c.y, c.w, c.h, wide);
        Pic two(960, 540);
        for (size_t i = 0; i < spans.count; ++i) blit_span(two, s, spans.span[i].dst, spans.span[i].src);
        const int32_t ox = rule.right ? wide.dx() : 0, oy = rule.bottom ? wide.dy() : 0;
        bool lines_ok = true;
        std::string what;
        if (rule.cut_cols[0] >= 0) {
            std::vector<int32_t> cuts;
            for (const int32_t col : rule.cut_cols) {
                if (col >= 0) cuts.push_back(col);
            }
            const std::vector<std::pair<int32_t, int32_t>> columns = reference_columns(c.w, cuts, wide.dx());
            for (const auto& col : columns) {
                for (int32_t y = 0; y < c.h; ++y) {
                    const uint8_t v = s.get_pixel(static_cast<uint32_t>(col.first), static_cast<uint32_t>(y));
                    lines_ok = lines_ok && two.at(c.x + ox + col.second, c.y + oy + y) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
                }
            }
            lines_ok = lines_ok && static_cast<int32_t>(columns.size()) == c.w + wide.dx();
            const std::vector<int32_t> shares = reference_shares(wide.dx(), cuts.size());
            what = "the columns before each cut, 1 + its share copies of the cut column (the shares:";
            for (const int32_t share : shares) what += " " + std::to_string(share);
            what += ") and the columns after it, in order";
        } else {
            const int32_t extra = wide.dy();
            const int32_t cut = rule.cut_row;
            for (int32_t line = 0; line < c.h + extra; ++line) {
                const int32_t src_line = line <= cut ? line : (line <= cut + extra ? cut : line - extra);       // the mock-up's rule
                for (int32_t i = 0; i < c.w; ++i) {
                    const uint8_t v = s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(src_line));
                    lines_ok = lines_ok && two.at(c.x + ox + i, c.y + oy + line) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
                }
            }
            what = "the lines before the cut, " + std::to_string(extra + 1) + " copies of the cut line and the lines after it, in order";
        }
        check(lines_ok, std::string(rule.sprite) + " at 960 x 540: " + what);
    }
    // the original's picture is the plain 14 pieces drawn the way the original draws them (draw_animation_frame0 order)
    {
        const Pic classic = compose_from_spans(arc, ScreenLayout::classic());
        Pic plain(640, 480);
        const auto& frames = arc.find_animation("uishell")->subitems[0].frames;
        for (size_t k = frames.size(); k-- > 0;) {
            const assets::Sprite& sp = arc.get_sprite(frames[k].sprite_index);
            blit_span(plain, sp, LayoutRect{frames[k].dx, frames[k].dy, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height)},
                      LayoutRect{0, 0, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height)});
        }
        check(differing_pixels(classic, plain) == 0, "the original's picture is the 14 plain pieces, the last stored first");
    }
}

// =====================================================================================================================================================
// 4. The real renderer draws the spans exactly
// =====================================================================================================================================================

/// What the renderer shows of a palette index of a piece: the HUD's colour of the local team 0 (TextureCache::compose_palette is the renderer's own rule)
std::array<uint8_t, 4> rgb_of(const assets::AssetArchive& arc, const std::string& sprite_name, int16_t index) {
    const auto palette = TextureCache::compose_palette(arc.get_palette(), sprite_name, 0);
    const assets::ColorRGBA c = palette[static_cast<size_t>(index)];
    return {c.r, c.g, c.b, 255};
}

struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : arc(archive), width(w), height(h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("wide-hud", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(w, h);
        ok = true;
    }
    ~RendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RendererRig(const RendererRig&) = delete;
    RendererRig& operator=(const RendererRig&) = delete;

    std::vector<uint8_t> read() {
        std::vector<uint8_t> px(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
        SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), width * 4);
        return px;
    }

    const assets::AssetArchive& arc;
    int32_t width;
    int32_t height;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

void test_renderer_draws_spans(const assets::AssetArchive& arc) {
    group("renderer", "the real renderer draws the frame's spans exactly: the repeated lines, the pieces, the origin of a window");
    for (const auto& sz : {std::pair<int32_t, int32_t>{960, 540}, std::pair<int32_t, int32_t>{1280, 720}}) {
        RendererRig rig(arc, sz.first, sz.second);
        check(rig.ok, "the renderer rig " + std::to_string(sz.first) + " x " + std::to_string(sz.second) + " is up");
        if (!rig.ok) continue;
        const ScreenLayout layout = ScreenLayout::with_size(sz.first, sz.second);
        rig.renderer.set_layout(layout);
        rig.renderer.set_hud_team(0);
        HUD hud;
        hud.set_layout(layout);
        rig.renderer.begin_frame();
        hud.render_shell(rig.renderer, arc);
        const std::vector<uint8_t> px = rig.read();
        // the expected picture: the mock-up's composition in the HUD's colours (every pixel the art does not cover is the black that the frame starts with)
        Pic reference = compose_reference(arc, sz.first, sz.second);
        // (who drew each pixel is needed for the colour: compose again piece by piece, in the same order)
        std::vector<std::array<uint8_t, 4>> want(static_cast<size_t>(sz.first) * static_cast<size_t>(sz.second), std::array<uint8_t, 4>{0, 0, 0, 255});
        {
            const int32_t dw = sz.first - 640, dh = sz.second - 480;
            for (const RefPiece& p : kReference) {
                const assets::Sprite* sprite = arc.find_sprite(p.name);
                const int32_t ox = p.right ? dw : 0, oy = p.bottom ? dh : 0, eh = p.row >= 0 ? dh : 0;
                std::vector<int32_t> cuts;
                for (const int32_t c : p.cols) {
                    if (c >= 0) cuts.push_back(c);
                }
                const std::vector<std::pair<int32_t, int32_t>> columns = reference_columns(static_cast<int32_t>(sprite->width), cuts, dw);
                for (int32_t y = 0; y < static_cast<int32_t>(sprite->height) + eh; ++y) {
                    const int32_t sy = (eh > 0 && y > p.row) ? (y <= p.row + eh ? p.row : y - eh) : y;
                    for (const auto& col : columns) {
                        const uint8_t v = sprite->get_pixel(static_cast<uint32_t>(col.first), static_cast<uint32_t>(sy));
                        if (v == assets::CHD_COLOR_KEY_INDEX) continue;
                        const int32_t px_x = p.x + ox + col.second, px_y = p.y + oy + y;
                        if (px_x >= 0 && px_x < sz.first && px_y >= 0 && px_y < sz.second) want[static_cast<size_t>(px_y) * static_cast<size_t>(sz.first) + static_cast<size_t>(px_x)] = rgb_of(arc, p.name, v);
                    }
                }
            }
        }
        (void)reference;
        int64_t bad = 0;
        for (size_t i = 0; i < want.size(); ++i) {
            if (px[i * 4] != want[i][0] || px[i * 4 + 1] != want[i][1] || px[i * 4 + 2] != want[i][2]) ++bad;
        }
        check(bad == 0, std::to_string(sz.first) + " x " + std::to_string(sz.second) + ": the renderer's frame is the mock-up's, pixel for pixel (" + std::to_string(bad) + " pixels differ)");
    }
    // a part of a sprite is drawn where the picture and the origin put it
    {
        RendererRig rig(arc, 960, 540);
        if (rig.ok) {
            rig.renderer.set_hud_team(0);
            const int32_t id = arc.find_sprite_id("scorcovr.bmp");
            const assets::Sprite& cover = arc.get_sprite(static_cast<uint32_t>(id));
            auto drawn_at = [&](int32_t x0, int32_t y0) {            // the whole sprite stretched to twice its width, at (x0, y0) of the canvas: pixel (2 i, j) is the sprite's (i, j)
                const std::vector<uint8_t> px = rig.read();
                for (int32_t j = 0; j < static_cast<int32_t>(cover.height); ++j) {
                    for (int32_t i = 0; i < static_cast<int32_t>(cover.width); ++i) {
                        const uint8_t v = cover.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
                        const std::array<uint8_t, 4> c = v == assets::CHD_COLOR_KEY_INDEX ? std::array<uint8_t, 4>{0, 0, 0, 255} : rgb_of(arc, "scorcovr.bmp", v);
                        for (int32_t k = 0; k < 2; ++k) {
                            const size_t at = (static_cast<size_t>(y0 + j) * 960 + static_cast<size_t>(x0 + 2 * i + k)) * 4;
                            if (px[at] != c[0] || px[at + 1] != c[1] || px[at + 2] != c[2]) return false;
                        }
                    }
                }
                return true;
            };
            rig.renderer.begin_frame();
            rig.renderer.draw_sprite_region(static_cast<uint32_t>(id), 10, 20, 2 * static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height), 0, 0, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height));
            check(drawn_at(10, 20), "draw_sprite_region stretches a part of a sprite to the rectangle at its place");
            rig.renderer.begin_frame();
            rig.renderer.set_origin(30, 25);
            rig.renderer.draw_sprite_region(static_cast<uint32_t>(id), 10, 20, 2 * static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height), 0, 0, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height));
            check(drawn_at(40, 45), "... moved by the origin");
            rig.renderer.set_origin(0, 0);
            rig.renderer.set_picture(LayoutRect{100, 40, 640, 480});
            rig.renderer.begin_frame();
            rig.renderer.draw_sprite_region(static_cast<uint32_t>(id), 10, 20, 2 * static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height), 0, 0, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height));
            check(drawn_at(110, 60), "... and by the corner of the picture");
        }
    }
    // set_origin moves everything that is drawn after it, and nothing after it is reset
    {
        RendererRig rig(arc, 960, 540);
        if (!rig.ok) return;
        rig.renderer.set_hud_team(0);
        rig.renderer.begin_frame();
        rig.renderer.draw_named_sprite("scorcovr.bmp", 10, 10);
        rig.renderer.set_origin(100, 50);
        check(rig.renderer.origin() == LayoutPoint{100, 50}, "set_origin keeps the origin");
        rig.renderer.draw_named_sprite("scorcovr.bmp", 10, 10);
        rig.renderer.fill_rect(300, 300, 4, 4, assets::ColorRGBA{1, 2, 3, 255});
        rig.renderer.draw_rect(400, 300, 6, 6, assets::ColorRGBA{9, 8, 7, 255});
        rig.renderer.set_origin(0, 0);
        rig.renderer.draw_named_sprite("scorcovr.bmp", 500, 100);
        const std::vector<uint8_t> px = rig.read();
        const assets::Sprite& cover = *arc.find_sprite("scorcovr.bmp");
        auto pixel_at = [&](int32_t x, int32_t y) { return std::array<uint8_t, 3>{px[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4], px[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 1], px[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 2]}; };
        auto sprite_matches_at = [&](int32_t ox, int32_t oy) {
            for (int32_t y = 0; y < static_cast<int32_t>(cover.height); ++y) {
                for (int32_t x = 0; x < static_cast<int32_t>(cover.width); ++x) {
                    const uint8_t v = cover.get_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                    const std::array<uint8_t, 4> c = v == assets::CHD_COLOR_KEY_INDEX ? std::array<uint8_t, 4>{0, 0, 0, 255} : rgb_of(arc, "scorcovr.bmp", v);
                    const auto p = pixel_at(ox + x, oy + y);
                    if (p[0] != c[0] || p[1] != c[1] || p[2] != c[2]) return false;
                }
            }
            return true;
        };
        check(sprite_matches_at(10, 10) && sprite_matches_at(110, 60) && sprite_matches_at(500, 100), "a sprite is drawn at its place, at its place plus the origin (100, 50), and again at its place after the origin is cleared");
        rig.renderer.set_origin(40, 40);
        rig.renderer.begin_frame();
        check(rig.renderer.origin() == LayoutPoint{0, 0}, "a new frame starts with no origin");
        check(pixel_at(300 + 100, 300 + 50) == (std::array<uint8_t, 3>{1, 2, 3}) && pixel_at(300, 300) != (std::array<uint8_t, 3>{1, 2, 3}), "a fill moves with the origin");
        check(pixel_at(400 + 100, 300 + 50) == (std::array<uint8_t, 3>{9, 8, 7}), "a frame moves with the origin");
        rig.renderer.begin_frame();
        rig.renderer.set_origin(7, 9);
        rig.renderer.set_clip_rect(0, 0, 10, 10);                        // a clip of a window is in the window's numbers too
        rig.renderer.fill_rect(0, 0, 50, 50, assets::ColorRGBA{200, 100, 50, 255});
        rig.renderer.clear_clip_rect();
        rig.renderer.set_origin(0, 0);
        const std::vector<uint8_t> px2 = rig.read();
        auto at2 = [&](int32_t x, int32_t y) { return std::array<uint8_t, 3>{px2[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4], px2[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 1], px2[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 2]}; };
        check(at2(7, 9) == (std::array<uint8_t, 3>{200, 100, 50}) && at2(16, 18) == (std::array<uint8_t, 3>{200, 100, 50}) && at2(17, 19) == (std::array<uint8_t, 3>{0, 0, 0}) && at2(6, 9) == (std::array<uint8_t, 3>{0, 0, 0}),
              "a clip set under an origin is the window's rectangle moved by it (10 x 10 at (7, 9))");
    }
}

// =====================================================================================================================================================
// 5. The HUD at 960 x 540
// =====================================================================================================================================================

/// Records the HUD's calls in order, with the origin that was set when each was made
class Spy : public IRenderer {
public:
    enum class Kind : uint8_t { Sprite, Region, Named, Fill, Rect, Text, Clip, ClearClip, Origin, Image, Team };
    struct Ev {
        Kind kind{Kind::Sprite};
        uint32_t id{0};                       // the sprite's id (Sprite, Region)
        std::string name;                     // the sprite's name, the text
        int32_t x{0}, y{0}, w{0}, h{0};       // as given
        int32_t sx{0}, sy{0}, sw{0}, sh{0};   // Region
        assets::ColorRGBA colour{};
        int32_t ox{0}, oy{0};                 // the origin that was set
        FontSize size{FontSize::Px12};
    };

    explicit Spy(const assets::AssetArchive& archive) : arc_(archive) {}

    void draw_sprite(uint32_t id, int32_t x, int32_t y, bool) override {
        Ev e = base(Kind::Sprite);
        e.id = id;
        e.name = arc_.get_sprite(id).name;
        e.x = x;
        e.y = y;
        events.push_back(e);
    }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool) override {
        Ev e = base(Kind::Named);
        e.name = name;
        e.x = x;
        e.y = y;
        events.push_back(e);
    }
    void draw_sprite_region(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) override {
        Ev e = base(Kind::Region);
        e.id = id;
        e.name = arc_.get_sprite(id).name;
        e.x = x; e.y = y; e.w = w; e.h = h;
        e.sx = sx; e.sy = sy; e.sw = sw; e.sh = sh;
        events.push_back(e);
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        Ev e = base(Kind::Fill);
        e.x = x; e.y = y; e.w = w; e.h = h; e.colour = c;
        events.push_back(e);
    }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        Ev e = base(Kind::Rect);
        e.x = x; e.y = y; e.w = w; e.h = h; e.colour = c;
        events.push_back(e);
    }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c) override { text(s, x, y, c, FontSize::Px12); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { text(s, x, y, c, size); }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override { events.push_back(base(Kind::Team)); }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override {
        Ev e = base(Kind::Clip);
        e.x = x; e.y = y; e.w = w; e.h = h;
        events.push_back(e);
    }
    void clear_clip_rect() override { events.push_back(base(Kind::ClearClip)); }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t*) override {
        Ev e = base(Kind::Image);
        e.x = x; e.y = y; e.w = w; e.h = h;
        events.push_back(e);
    }
    void set_origin(int32_t x, int32_t y) override {
        Ev e = base(Kind::Origin);
        e.x = x;
        e.y = y;
        events.push_back(e);
        ox_ = x;
        oy_ = y;
    }

    /// A sprite drawn with this image name at this place (as given: the origin is not added)
    bool has_sprite_at(const std::string& name, int32_t x, int32_t y) const {
        for (const Ev& e : events) {
            if (e.kind == Kind::Sprite && e.name == name && e.x == x && e.y == y) return true;
        }
        return false;
    }
    size_t count(Kind k) const {
        size_t n = 0;
        for (const Ev& e : events) n += e.kind == k ? 1 : 0;
        return n;
    }
    std::vector<Ev> of(Kind k) const {
        std::vector<Ev> out;
        for (const Ev& e : events) {
            if (e.kind == k) out.push_back(e);
        }
        return out;
    }

    std::vector<Ev> events;

private:
    Ev base(Kind k) const {
        Ev e;
        e.kind = k;
        e.ox = ox_;
        e.oy = oy_;
        return e;
    }
    void text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) {
        Ev e = base(Kind::Text);
        e.name = s;
        e.x = x;
        e.y = y;
        e.colour = c;
        e.size = size;
        events.push_back(e);
    }
    const assets::AssetArchive& arc_;
    int32_t ox_{0}, oy_{0};
};

uint32_t g_clock = 0;
uint32_t clock_fn() { return g_clock; }

/// A HUD over a 60 x 60 engine world, with an injected clock (the pedestals' chains and the caret read it)
struct HudRig {
    explicit HudRig(const assets::AssetArchive& archive, const ScreenLayout& l, uint8_t local = 0, uint32_t map_tiles = 60) : arc(archive), layout(l) {
        g_clock = 0;
        sim.init_test_world(map_tiles, map_tiles, 1, 600000);
        hud.set_ticks_function(&clock_fn);
        hud.init(local);
        hud.set_layout(layout);
        hud.set_sim_query(&sim);
        cam.set_view(layout.view());
        cam.centre_small_maps = !layout.is_classic();
        cam.clamp_to_bounds(map_tiles, map_tiles);
    }
    /// A frame at time `at` (the pedestals' chains start at the first frame: render once at 0 and again later)
    Spy frame(uint32_t at = 10000) {
        if (!first_done) {
            first_done = true;
            Spy first(arc);
            g_clock = 0;
            hud.render(first, arc, world(), cam);
        }
        g_clock = at;
        Spy spy(arc);
        hud.render(spy, arc, world(), cam);
        return spy;
    }
    sim::WorldState world() const { return override_world ? *override_world : sim.get_world_state(); }
    void click(int32_t x, int32_t y, uint8_t button = SDL_BUTTON_LEFT) {
        hud.handle_mouse_down(x, y, button, sim, cam);
        hud.handle_mouse_up(x, y, button, sim, cam);
    }

    const assets::AssetArchive& arc;
    ScreenLayout layout;
    sim::SimulationEngine sim;
    HUD hud;
    ViewportCamera cam;
    bool first_done{false};
    std::optional<sim::WorldState> override_world;
};

void test_hud_draws_the_frame(const assets::AssetArchive& arc) {
    group("hud-frame", "the HUD draws the frame first, piece by piece in the original's order, and the panel's animations where the layout puts them");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const auto* seq = arc.find_animation("uishell");
    const auto& frames = seq->subitems[0].frames;
    // the expected calls of the frame: the 14 pieces, the last stored first, as the spans of the model
    struct Expect {
        bool region;
        uint32_t id;
        LayoutRect dst;
        LayoutRect src;
    };
    auto expected_calls = [&](const ScreenLayout& layout) {
        std::vector<Expect> out;
        for (size_t k = frames.size(); k-- > 0;) {
            const assets::Sprite& sp = arc.get_sprite(frames[k].sprite_index);
            const ShellSpans spans = shell_spans(shell_rule(sp.name), frames[k].dx, frames[k].dy, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height), layout);
            for (size_t i = 0; i < spans.count; ++i) out.push_back({!spans.span[i].whole, frames[k].sprite_index, spans.span[i].dst, spans.span[i].src});
        }
        return out;
    };
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const bool classic = layout.is_classic();
        const std::string at = std::string(classic ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        const Spy spy = rig.frame();
        const std::vector<Expect> want = expected_calls(layout);
        // the first sprite or region calls of the frame are the pieces
        std::vector<Spy::Ev> drawn;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite || e.kind == Spy::Kind::Region) drawn.push_back(e);
            if (drawn.size() == want.size()) break;
        }
        bool same = drawn.size() == want.size();
        for (size_t i = 0; same && i < want.size(); ++i) {
            const Spy::Ev& e = drawn[i];
            if (want[i].region) {
                same = e.kind == Spy::Kind::Region && e.id == want[i].id && LayoutRect{e.x, e.y, e.w, e.h} == want[i].dst && LayoutRect{e.sx, e.sy, e.sw, e.sh} == want[i].src;
            } else {
                same = e.kind == Spy::Kind::Sprite && e.id == want[i].id && e.x == want[i].dst.x && e.y == want[i].dst.y;
            }
        }
        check(same, at + "the first " + std::to_string(want.size()) + " calls are the frame's spans, the last stored piece first");
        check(classic ? spy.count(Spy::Kind::Region) == 0 : spy.count(Spy::Kind::Region) == 22, at + (classic ? "no piece is drawn in parts" : "six pieces are drawn in parts: the bottom strip in seven (three cuts), the top bar and the four taller pieces in three each (22 calls)"));
        // the panel's backing fill comes before the frame, the whole panel
        const LayoutRect fill = layout.panel_fill();
        bool fill_first = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Fill) {
                fill_first = LayoutRect{e.x, e.y, e.w, e.h} == fill;
                break;
            }
            if (e.kind == Spy::Kind::Sprite) break;
        }
        check(fill_first, at + "the backing fill " + str(fill) + " is drawn before the frame");
    }

    // the animations of the panel: where the original draws them plus (dx, dy) of the part of the panel they are in
    {
        const sim::AntType type = sim::AntType::Bomber;
        for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
            const int32_t dx = layout.dx(), dy = layout.dy();
            const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
            HudRig rig(arc, layout);
            const uint32_t ant = rig.sim.spawn_unit(0, type, sim::TileCoord{5, 5});
            rig.sim.tick();
            rig.hud.select_ant(ant);
            rig.hud.update(rig.sim.get_world_state(), 0);
            const Spy spy = rig.frame();
            // the move pedestal at rest (butmovu: labmov (483, 141), butmovu (490, 162), butup (477, 157)), the bomb pedestal (butbomu: butbomu (549, 162), labbom (544, 140), butup (538, 157)) and
            // the Stop button (labcan (595, 180), butcanu (595, 198))
            check(spy.has_sprite_at("labmov.bmp", 483 + dx, 141) && spy.has_sprite_at("butmovu.bmp", 490 + dx, 162) && spy.has_sprite_at("butup.bmp", 477 + dx, 157), at + "the move pedestal is at (477, 157) + (dx, 0)");
            check(spy.has_sprite_at("butbomu.bmp", 549 + dx, 162) && spy.has_sprite_at("labbom.bmp", 544 + dx, 140) && spy.has_sprite_at("butup.bmp", 538 + dx, 157), at + "the bomb pedestal is at (538, 157) + (dx, 0)");
            check(spy.has_sprite_at("labcan.bmp", 595 + dx, 180) && spy.has_sprite_at("butcanu.bmp", 595 + dx, 198), at + "the Stop button is at (595, 180) + (dx, 0)");
            check(spy.has_sprite_at("butup.bmp", 477 + dx, 157), at + "(the pedestals are in the top part of the panel: down by 0)");
            // the buttons "Send to" and the covers of the chat input
            check(spy.has_sprite_at("butallu.bmp", 532 + dx, 443 + dy), at + "[All] is at (532, 443) + (dx, dy)");
            rig.hud.options().chat = false;
            const Spy off = rig.frame();
            check(off.has_sprite_at("chcovr2.bmp", 478 + dx, 421 + dy) && off.has_sprite_at("chcovr2.bmp", 478 + dx, 436 + dy) && off.has_sprite_at("chcovr2.bmp", 478 + dx, 445 + dy),
                  at + "with the chat off the three covers are at (478, 421 / 436 / 445) + (dx, dy)");
            rig.hud.options().chat = true;
            // the top bar's buttons: the small label of a hovered button and the art of a pressed one are the animations' parts moved by (dx, 0)
            const struct { const char* name; int32_t x; const char* hover; const char* pressed; } buttons[] = {{"Help", 476, "buthlpr", "buthlpd"}, {"Options", 525, "butoptr", "butoptd"}, {"Quit", 579, "butqitr", "butqitd"}};
            for (const auto& b : buttons) {
                for (const bool pressed : {false, true}) {
                    HudRig other(arc, layout);
                    other.hud.handle_mouse_motion(b.x + dx + 5, 7 + 5, other.sim, other.cam);
                    if (pressed) other.hud.handle_mouse_down(b.x + dx + 5, 7 + 5, SDL_BUTTON_LEFT, other.sim, other.cam);
                    const Spy shot = other.frame();
                    const auto* anim = arc.find_animation(pressed ? b.pressed : b.hover);
                    bool all = anim != nullptr && !anim->subitems.empty();
                    if (all) {
                        for (const auto& part : anim->subitems[0].frames) all = all && shot.has_sprite_at(arc.get_sprite(part.sprite_index).name, part.dx + dx, part.dy);
                    }
                    check(all, at + std::string(b.name) + (pressed ? " pressed: the art is at its place + (dx, 0)" : " hovered: the label is at its place + (dx, 0)"));
                }
            }
        }
        // every sprite of the panel that the original draws right of x = 477 is moved by dx and no sprite of the panel is left at the old place: the pedestal art is not at x = 477
        HudRig rig(arc, wide);
        const uint32_t ant = rig.sim.spawn_unit(0, type, sim::TileCoord{5, 5});
        rig.sim.tick();
        rig.hud.select_ant(ant);
        rig.hud.update(rig.sim.get_world_state(), 0);
        const Spy spy = rig.frame();
        check(!spy.has_sprite_at("butup.bmp", 477, 157) && !spy.has_sprite_at("labcan.bmp", 595, 180), "960 x 540: nothing of the panel is left at the original's place");
    }
    // the pedestal slot draws its animation's parts moved by the offset it is given, both ways
    {
        PedestalSlot slot;
        slot.request(arc, PedestalKind::Move, 1, 0);
        Spy plain(arc);
        slot.draw(plain, arc, 20000);                                          // (the chain has run: the resting picture)
        Spy moved(arc);
        slot.draw(moved, arc, 20000, 7, 9);
        bool same = !plain.events.empty() && plain.events.size() == moved.events.size();
        for (size_t i = 0; same && i < plain.events.size(); ++i) same = moved.events[i].name == plain.events[i].name && moved.events[i].x == plain.events[i].x + 7 && moved.events[i].y == plain.events[i].y + 9;
        check(same, "PedestalSlot::draw moves every part of its picture by (dx, dy)");
        check(plain.has_sprite_at("butup.bmp", 477, 157), "(and with no offset the pedestal is the original's at (477, 157))");
    }
    // the egg tray and the lunchbox follow
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const int32_t dx = layout.dx();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        sim::WorldState world = rig.sim.get_world_state();
        world.player_eggs[0] = 3;
        rig.override_world = world;
        rig.hud.select_base(0);
        const Spy spy = rig.frame();
        const auto* egg3 = arc.find_animation("egg3");
        bool eggs = egg3 != nullptr && !egg3->subitems.empty();
        if (eggs) {
            for (const auto& part : egg3->subitems[0].frames) eggs = eggs && spy.has_sprite_at("egg.bmp", part.dx + dx, part.dy);
        }
        check(eggs, at + "the egg tray egg3 is at its place + (dx, 0)");
        check(spy.has_sprite_at("labcan.bmp", 595 + dx, 180) && spy.has_sprite_at("butcanu.bmp", 595 + dx, 198), at + "the Stop button of a selected hill is at (595, 180) + (dx, 0)");
    }
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const int32_t dx = layout.dx();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
        rig.sim.tick();
        sim::WorldState world = rig.sim.get_world_state();
        for (auto& a : world.ants) a.is_holding = true;
        rig.override_world = world;
        rig.hud.select_ant(ant);
        rig.hud.update(world, 0);
        const Spy spy = rig.frame();
        check(spy.has_sprite_at("lunchicon.bmp", 597 + dx, 131), at + "the lunchbox indicator is at (597, 131) + (dx, 0)");
    }
    // the glow of the Move pedestal (it follows the wall clock: any of its pictures): the pointer over the map with an own ant selected shows the Move cursor, and the settled pedestal glows
    for (const ScreenLayout& layout : {ScreenLayout::classic(), wide}) {
        const int32_t dx = layout.dx();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
        rig.sim.tick();
        rig.hud.select_ant(ant);
        rig.hud.update(rig.sim.get_world_state(), 0);
        rig.frame(1000);                                                       // the pedestal rises
        const sim::WorldState world = rig.sim.get_world_state();
        check(rig.hud.evaluate_cursor(300, 200, world, rig.sim.grid(), rig.cam) == CursorType::Move, at + "the pointer over the map with an own ant selected: the Move cursor");
        const Spy spy = rig.frame(20000);
        int glow = 0;
        bool placed = true;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite && e.name.rfind("butdef", 0) == 0) {
                ++glow;
                placed = placed && e.x >= 477 + dx && e.x < 540 + dx;
            }
        }
        check(glow >= 3 && placed, at + "the Move pedestal's glow (butdef pictures, " + std::to_string(glow) + " parts) is drawn at (477 .. 540) + dx");
    }
}

void test_score_slots(const assets::AssetArchive& arc) {
    group("slots", "the score boxes are slots: the local team's in the top bar, the others' in the bottom strip, in the order of the teams; the bottom boxes are spread evenly over the strip");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    // the model: the bottom strip is widened at three cuts, dx shared in thirds (the left cut takes the remainder), a box and its label move by what the cuts left of the box add
    check(classic.bottom_slot_count() == 3 && wide.bottom_slot_count() == 3 && ScreenLayout::with_size(1920, 1080).bottom_slot_count() == 3, "every picture holds three bottom slots (the original's)");
    check(wide.cut_share(0, 3) == 108 && wide.cut_share(1, 3) == 106 && wide.cut_share(2, 3) == 106 && wide.cut_share(0, 1) == 320 && wide.cut_share(0, 3) + wide.cut_share(1, 3) + wide.cut_share(2, 3) == 320,
          "960 x 540: the three cuts add 108, 106 and 106 (320 in all); a piece with one cut gets all of dx");
    check(wide.score_slot(1) == ScoreSlot{113, 209, 524, 213} && wide.score_slot(2) == ScoreSlot{377, 465, 524, 468} && wide.score_slot(3) == ScoreSlot{632, 719, 524, 722},
          "960 x 540: the boxes are at x 213, 468 and 722 (the labels anchored to them), 60 rows down");
    check(wide.score_slot(2).box_left - wide.score_slot(1).box_left == 255 && wide.score_slot(3).box_left - wide.score_slot(2).box_left == 254, "... spaced 255 and 254 apart: even");
    check(wide.score_slot(0) == ScoreSlot{632, 719, 4, 722}, "the local team's slot stays in the top bar, right anchored (box at 722)");
    check(wide.score_slot(4) == wide.score_slot(3) && wide.score_slot(40) == wide.score_slot(3) && classic.score_slot(4) == classic.score_slot(3) && classic.score_slot(9) == classic.score_slot(3),
          "a slot past the third is the third");
    {
        // an independent rule for any width: the share of the leftmost cut is dx - 2 * (dx / 3), the others' dx / 3
        bool all = true;
        bool even = true;
        for (const int32_t w : {640, 641, 642, 643, 644, 700, 854, 960, 1000, 1280, 1366, 1920, 2560, 3840}) {
            const ScreenLayout l = ScreenLayout::with_size(w, 480);
            const int32_t dx = w - 640;
            const int32_t third = dx / 3;
            const int32_t left = dx - 2 * third;
            const int32_t shift[3] = {left, left + third, left + 2 * third};
            const int32_t classic_box[3] = {105, 254, 402};
            const int32_t classic_label_right[3] = {101, 251, 399};
            const int32_t classic_label_w[3] = {96, 88, 87};                                   // the original's labels: [5, 101), [163, 251), [312, 399)
            for (size_t b = 0; b < 3; ++b) {
                const ScoreSlot s = l.score_slot(b + 1);
                all = all && s.box_left == classic_box[b] + shift[b] && s.label_right == classic_label_right[b] + shift[b] && s.label_left == s.label_right - classic_label_w[b] && s.top == 464;
            }
            all = all && l.score_slot(3).box_left == 402 + dx;                               // the last box is where it always was relative to the panel: right anchored
            even = even && (l.score_slot(2).box_left - l.score_slot(1).box_left) - (l.score_slot(3).box_left - l.score_slot(2).box_left) == 1;      // the original's own gaps are 149 and 148
        }
        check(all, "the slots of fourteen picture widths follow the rule (left cut dx - 2 * (dx / 3), the others dx / 3): box and label move together, the last box is right anchored");
        check(even, "... and the boxes stay evenly spaced: the first gap is exactly one pixel wider than the second, as the original's own (149 and 148), whatever the width");
    }
    // the art under the boxes: the recess of every box in the composed wide frame is the strip's own recess, so a box sits on its recess (and the labels' band stays plain)
    {
        const Pic frame = compose_from_spans(arc, wide);
        const assets::Sprite& strip = *arc.find_sprite("x17y461.bmp");
        for (size_t b = 1; b <= 3; ++b) {
            const ScoreSlot cl = classic.score_slot(b);
            const ScoreSlot ws = wide.score_slot(b);
            bool same = true;
            for (int32_t j = 0; j < 14; ++j) {
                for (int32_t i = 0; i < 54; ++i) {
                    const uint8_t v = strip.get_pixel(static_cast<uint32_t>(cl.box_left - 17 + i), static_cast<uint32_t>(cl.top + j - 461));
                    same = same && frame.at(ws.box_left + i, ws.top + j) == static_cast<int16_t>(v);
                }
            }
            check(same, "960 x 540: the recess under the box of slot " + std::to_string(b) + " (54 x 14 at " + std::to_string(ws.box_left) + ", " + std::to_string(ws.top) + ") is the strip's own recess, moved with its box");
        }
    }
    // the HUD: which team has which slot, by the roster and the local team
    struct Case {
        const char* what;
        uint8_t local;
        uint8_t roster;
    };
    const Case cases[] = {
        {"two teams (green local, red)", 0, 0x03},      {"three teams (green local, red, blue)", 0, 0x07}, {"four teams", 0, 0x0F},
        {"four teams, blue is the local team", 2, 0x0F}, {"two teams, black local, green", 3, 0x09},       {"three teams, red local", 1, 0x0D},
    };
    const uint8_t colours[4][3] = {{7, 67, 47}, {119, 0, 0}, {43, 39, 107}, {39, 39, 59}};
    for (const ScreenLayout& layout : {classic, wide}) {
        for (const Case& c : cases) {
            HudRig rig(arc, layout, c.local);
            rig.hud.set_roster_mask(c.roster);
            const Spy spy = rig.frame();
            const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": " + c.what + ": ";
            size_t other = 0;
            bool all = true;
            for (uint8_t team = 0; team < 4; ++team) {
                const size_t slot = team == c.local ? 0 : 1 + other++;
                const ScoreSlot want = layout.score_slot(slot);
                const bool exists = ((c.roster >> team) & 1u) != 0;
                bool box = false;
                bool cover = false;
                for (const Spy::Ev& e : spy.events) {
                    if (e.kind == Spy::Kind::Fill && e.w == 54 && e.h == 14 && e.colour.r == colours[team][0] && e.colour.g == colours[team][1] && e.colour.b == colours[team][2]) box = e.x == want.box_left && e.y == want.top;
                    if (e.kind == Spy::Kind::Named && e.name == "scorcovr.bmp" && e.x == want.box_left - 2 && e.y == want.top - 1) cover = true;
                }
                all = all && (exists ? (box && !cover) : (!box && cover));
            }
            check(all, at + "every team that plays has its box in its slot, every team that does not has the slot covered");
        }
    }
    // the labels: right aligned to the slot's label_right, 14 px high, in white
    {
        HudRig rig(arc, wide, 0);
        rig.hud.set_roster_mask(0x0F);
        rig.hud.set_team_names({"", "Reddy", "Bluey", "Blacky"});
        const Spy spy = rig.frame();
        const ScoreSlot s1 = wide.score_slot(1), s3 = wide.score_slot(3);
        bool red = false, black = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Text && e.size == FontSize::Px14 && e.name == "Reddy:") red = e.x + 6 * 6 == s1.label_right && e.y == s1.top;
            if (e.kind == Spy::Kind::Text && e.size == FontSize::Px14 && e.name == "Blacky:") black = e.x + 7 * 6 == s3.label_right && e.y == s3.top;
        }
        check(red && black, "960 x 540: the labels end at their slots' label_right (the interface measures 6 px per character)");
    }
}

// =====================================================================================================================================================
// The network's ping and delay stay clear of the score boxes
// =====================================================================================================================================================

/// The ping and delay of a network match (latency_corner.hpp) stand in the corner row left of the version when the row is free, and on two lines above the plate when it is not. "Free" is the
/// match's limit: where the row of score boxes ends. It was the original's picture's number (460) in every layout: at 960 x 540, where the boxes are spread over the strip (the third one
/// ends at 778), a row of texts at x 644 .. 828 stood on the third box.
void test_latency_clear_of_scores(const assets::AssetArchive& arc) {
    group("latency", "the ping / delay readout of a network match does not touch a score box or cover, with two, three and four teams, in both pictures");
    using NP = net::NetGame::Phase;
    RendererRig measure(arc, 960, 540);                                      // the application's own font metrics
    check(measure.ok, "a renderer for the text metrics");
    if (!measure.ok) return;
    const assets::Sprite& cover = *arc.find_sprite("scorcovr.bmp");
    struct Roster { const char* what; uint8_t mask; };
    const Roster rosters[] = {{"two teams", 0x03}, {"three teams", 0x07}, {"four teams", 0x0F}};
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1280, 720), ScreenLayout::with_size(1920, 1080)}) {
        const std::string pic = std::to_string(layout.width) + " x " + std::to_string(layout.height) + ": ";
        const CornerPlate plate = CornerPlate::for_canvas(layout.width, layout.height);
        const int32_t text_h = measure.renderer.get_text_height(FontSize::Px12);
        const int32_t fps_w = measure.renderer.get_text_width("60 FPS", FontSize::Px12);
        const int32_t ver_w = measure.renderer.get_text_width(std::string(ants::VERSION_STRING), FontSize::Px12);
        const CornerRow row = CornerRow::of(plate, fps_w, ver_w, 36, FPS_OVERLAY_SPARK_H, text_h);
        const std::optional<int32_t> limit = latency_left_limit(true, NP::Playing, CornerScreen::Match, layout);
        check(limit.has_value() && *limit == layout.score_row_right() && (!layout.is_classic() || *limit == 460), pic + "the match's limit is the layout's (" + std::to_string(limit ? *limit : -1) + ", and the original's 460 in its own picture)");
        int32_t strip_right = 0;                                              // the right edge of what the bottom row holds, over every roster
        for (const Roster& r : rosters) {
            HudRig rig(arc, layout, 0);
            rig.hud.set_roster_mask(r.mask);
            const Spy spy = rig.frame();
            std::vector<LayoutRect> row_items;                                // the boxes and the covers of the bottom row
            const int32_t top = layout.score_slot(1).top;
            for (const Spy::Ev& e : spy.events) {
                if (e.kind == Spy::Kind::Fill && e.w == 54 && e.h == 14 && e.y == top) row_items.push_back(LayoutRect{e.x, e.y, 54, 14});
                if (e.kind == Spy::Kind::Named && e.name == "scorcovr.bmp" && e.y == top - 1) row_items.push_back(LayoutRect{e.x, e.y, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height)});
            }
            check(row_items.size() == 3, pic + r.what + ": the bottom row holds three boxes or covers (" + std::to_string(row_items.size()) + ")");
            int32_t right_edge = 0;
            for (const LayoutRect& it : row_items) right_edge = std::max(right_edge, it.right());
            strip_right = std::max(strip_right, right_edge);
            // the readout: nothing measured yet, a typical one and the widest, each laid out as the application does
            const struct { const char* what; std::optional<uint32_t> ping, delay; } readouts[] = {{"nothing measured", std::nullopt, std::nullopt}, {"12 / 80 ms", 12u, 80u}, {"9999 / 9999 ms", 9999u, 9999u}};
            for (const auto& ro : readouts) {
                const int32_t pw = measure.renderer.get_text_width(ping_text(ro.ping), FontSize::Px12);
                const int32_t dw = measure.renderer.get_text_width(delay_text(ro.delay), FontSize::Px12);
                const int32_t widest = measure.renderer.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + measure.renderer.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
                const LatencyCornerLayout l = layout_latency_corner(pw, dw, widest, text_h, row.version_x, row.text_y, *limit, plate);
                const LayoutRect ping_rect{l.ping_x, l.ping_y, pw, text_h};
                const LayoutRect delay_rect{l.delay_x, l.delay_y, dw, text_h};
                bool touches = false;
                for (const LayoutRect& it : row_items) {
                    touches = touches || (ping_rect.x < it.right() && it.x < ping_rect.right() && ping_rect.y < it.bottom() && it.y < ping_rect.bottom());
                    touches = touches || (delay_rect.x < it.right() && it.x < delay_rect.right() && delay_rect.y < it.bottom() && it.y < delay_rect.bottom());
                }
                check(!touches, pic + r.what + ", " + ro.what + ": the texts (" + (l.stacked ? "stacked" : "in a row") + ", ping at x " + std::to_string(l.ping_x) + ") touch no box or cover");
                check(l.ping_x >= right_edge, pic + r.what + ", " + ro.what + ": the texts begin right of the score row (" + std::to_string(right_edge) + ")");
            }
        }
        check(*limit == strip_right + ScreenLayout::kScoreRowMargin, pic + "the limit is the right edge of the last cover (" + std::to_string(strip_right) + ") and the margin of " + std::to_string(ScreenLayout::kScoreRowMargin));
        // the control: the original's number (460) in a wider picture, where the boxes are further right, puts the row of texts on the third box
        if (!layout.is_classic()) {
            const int32_t widest = measure.renderer.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + measure.renderer.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
            const LatencyCornerLayout old = layout_latency_corner(measure.renderer.get_text_width(ping_text(12u), FontSize::Px12), measure.renderer.get_text_width(delay_text(80u), FontSize::Px12), widest, text_h, row.version_x,
                                                                  row.text_y, LATENCY_LEFT_LIMIT_MATCH, plate);
            check(!old.stacked && old.ping_x < strip_right, pic + "the control: with the original's limit (460) the texts stand in a row at x " + std::to_string(old.ping_x) + ", on the boxes (they end at " + std::to_string(strip_right) + ")");
        }
    }
}

// =====================================================================================================================================================
// 6. The pages of the original and its dialogs
// =====================================================================================================================================================

struct AllianceTable {
    sim::SimulationEngine sim;
    ViewportCamera cam;
    AllianceTable() {
        sim.init_test_world(60, 60, 5, 600000);
        const sim::TileCoord hills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
        for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, hills[p]);
        const char* names[4] = {"Alice", "Bob", "Carol", "Dave"};
        for (uint8_t p = 0; p < 4; ++p) sim.set_player_name(p, names[p]);
    }
};

void test_dialogs_and_pages(const assets::AssetArchive& arc) {
    group("windows", "the dialogs, the options window and the quick help sit over the middle of the map view with the HUD visible around them, the pointer is taken back to their numbers");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    // (the page offset, (160, 30) at 960 x 540, is gone: no page of the original is centred over a margin any more, every screen outside a match is composed for the whole canvas: test_wide_pages)
    check(wide.options_offset() == LayoutPoint{160, 30} && wide.quick_help_offset() == LayoutPoint{77, 31} && classic.options_offset() == LayoutPoint{0, 0} && classic.quick_help_offset() == LayoutPoint{0, 0},
          "the options window's offset is (160, 30) and the quick help's (77, 31) at 960 x 540: the 442 x 440 card (16, 21) and the 640 x 480 page go to the middle (397, 271) of the view; (0, 0) in the original's picture");
    for (const ScreenLayout& l : {wide, ScreenLayout::with_size(1280, 720), ScreenLayout::with_size(1920, 1080)}) {
        const LayoutPoint o = l.options_offset();
        const LayoutPoint q = l.quick_help_offset();
        const LayoutRect v = l.view();
        const std::string at = std::to_string(l.width) + " x " + std::to_string(l.height) + ": ";
        check(16 + o.x + 221 == v.x + v.w / 2 && 21 + o.y + 220 == v.y + v.h / 2, at + "the options card's centre is the view's centre");
        check(q.x + 320 == v.x + v.w / 2 && q.y + 240 == v.y + v.h / 2, at + "the quick help page's centre is the view's centre");
        check(q.x >= v.x && q.y >= v.y && q.x + 640 <= v.right() && q.y + 480 <= v.bottom(), at + "the quick help page lies inside the map view: the HUD around it stays visible");
    }
    check(wide.modal_offset() == LayoutPoint{137, 59} && classic.modal_offset() == LayoutPoint{0, 0}, "the dialog offset is (137, 59) at 960 x 540: the frame (100, 100) 320 x 224 goes to the middle (397, 271) of the view; (0, 0) in the original's picture");
    {
        const LayoutPoint m = wide.modal_offset();
        const LayoutRect view = wide.view();
        check(100 + m.x + 160 == view.x + view.w / 2 && 100 + m.y + 112 == view.y + view.h / 2, "... the dialog's centre is the view's centre");
        const ScreenLayout big = ScreenLayout::with_size(1920, 1080);
        const LayoutPoint bm = big.modal_offset();
        check(100 + bm.x + 160 == big.view().x + big.view().w / 2 && 100 + bm.y + 112 == big.view().y + big.view().h / 2, "... and in a bigger picture too");
    }

    // --- the quit dialog
    for (const ScreenLayout& layout : {classic, wide}) {
        const bool is_classic = layout.is_classic();
        const LayoutPoint m = layout.modal_offset();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        HudRig rig(arc, layout);
        rig.hud.open_quit_dialog();
        const Spy spy = rig.frame();
        // the dialog is drawn with its own numbers, between an origin and its reset
        std::vector<Spy::Ev> origins = spy.of(Spy::Kind::Origin);
        check(origins.size() >= 2 && origins.front().x == m.x && origins.front().y == m.y && origins.back().x == 0 && origins.back().y == 0, at + "the dialog is drawn under the origin (" + std::to_string(m.x) + ", " + std::to_string(m.y) + ") and the origin is cleared after it");
        bool frame_ok = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite && e.name == "dfram1.bmp") frame_ok = e.x == 100 && e.y == 100 && e.ox == m.x && e.oy == m.y;
        }
        check(frame_ok, at + "the frame is drawn at (100, 100) under that origin: it lands at (" + std::to_string(100 + m.x) + ", " + std::to_string(100 + m.y) + ")");
        // the pointer: a press on Yes at its place on screen
        bool quit = false;
        rig.hud.set_on_quit([&quit]() { quit = true; });
        const UIButton yes = rig.hud.quit_yes_button();
        rig.click(yes.x + m.x + 10, yes.y + m.y + 10);
        check(quit, at + "a click on Yes at its place on screen (" + std::to_string(yes.x + m.x + 10) + ", " + std::to_string(yes.y + m.y + 10) + ") quits");
        if (!is_classic) {
            quit = false;
            rig.hud.open_quit_dialog();
            rig.click(yes.x + 10, yes.y + 10);                                     // the original's place: now over the map, under the dialog's modal
            check(!quit && rig.hud.is_quit_dialog_open(), at + "a click at the original's place of Yes does nothing: the dialog is where the layout put it");
            // No closes it, at its place
            const UIButton no = rig.hud.quit_no_button();
            rig.click(no.x + m.x + 10, no.y + m.y + 10);
            check(!rig.hud.is_quit_dialog_open() && !quit, at + "No, at its place, closes the dialog");
            // hover follows: the Yes button is lit when the pointer is on it at its place
            rig.hud.open_quit_dialog();
            rig.hud.handle_mouse_motion(yes.x + m.x + 10, yes.y + m.y + 10, rig.sim, rig.cam);
            const Spy hover = rig.frame();
            check(hover.has_sprite_at("yes2.bmp", yes.x, yes.y), at + "the Yes button shows its hover picture when the pointer is on it at its place");
            rig.hud.handle_mouse_motion(yes.x + 10, yes.y + 10, rig.sim, rig.cam);
            const Spy not_hover = rig.frame();
            check(!not_hover.has_sprite_at("yes2.bmp", yes.x, yes.y), at + "... and not at the old place");
        }
    }

    // --- the "get ready" dialog
    {
        HudRig rig(arc, wide);
        rig.hud.start_match_modal();
        const Spy spy = rig.frame();
        bool framed = false, portrait = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite && e.name == "dfram1.bmp") framed = e.ox == 137 && e.oy == 59;
            if (e.kind == Spy::Kind::Sprite && e.ox == 137 && e.oy == 59 && e.x >= 200 && e.y >= 200 && e.x < 300 && e.y < 300) portrait = true;
        }
        check(framed && portrait, "960 x 540: the start dialog and the portrait in it are drawn under the dialog origin");
    }

    // --- the alliance dialogs: the pointer is taken back to the dialog's own numbers
    for (const ScreenLayout& layout : {classic, wide}) {
        const LayoutPoint m = layout.modal_offset();
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        AllianceTable t;
        HUD hud;
        hud.set_ticks_function(&clock_fn);
        hud.init(1);
        hud.set_layout(layout);
        hud.set_team_names({"Alice", "Bob", "Carol", "Dave"});
        t.cam.set_view(layout.view());
        sim::Command invite;
        invite.type = sim::CommandType::AllianceInvite;
        invite.issuer = 0;
        invite.other_player = 1;
        t.sim.apply_command(invite);
        hud.update(t.sim.get_world_state(), 1);
        check(hud.alliance_dialog() == HUD::AllianceDialog::Invitation, at + "Bob sees the invitation of Alice");
        // Accept is at (152, 260) 80 x 24 of the dialog: the press at the old place (without the move) is nothing, at its place on screen it accepts
        auto press_release = [&](int32_t x, int32_t y) {
            hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, t.sim, t.cam);
            hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, t.sim, t.cam);
            hud.update(t.sim.get_world_state(), 1);
        };
        if (!layout.is_classic()) {
            press_release(152 + 10, 260 + 10);
            check(hud.alliance_dialog() == HUD::AllianceDialog::Invitation && t.sim.get_world_state().player_alliances[1] != 0, at + "a click at the original's place of Accept does nothing");
        }
        {
            Spy spy(arc);
            g_clock = 5000;
            hud.render(spy, arc, t.sim.get_world_state(), t.cam);
            bool accept_drawn = false;
            for (const Spy::Ev& e : spy.events) {
                if (e.kind == Spy::Kind::Sprite && e.name == "accpt1.bmp") accept_drawn = e.x == 152 && e.y == 260 && e.ox == m.x && e.oy == m.y;
            }
            check(accept_drawn, at + "Accept is drawn at (152, 260) under the dialog origin");
        }
        hud.handle_mouse_motion(152 + m.x + 10, 260 + m.y + 10, t.sim, t.cam);                 // the hover picture of Accept, at its place on screen only
        {
            Spy hover(arc);
            g_clock = 6000;
            hud.render(hover, arc, t.sim.get_world_state(), t.cam);
            check(hover.has_sprite_at("accpt2.bmp", 152, 260), at + "Accept shows its hover picture when the pointer is on it at its place on screen");
            hud.handle_mouse_motion(152 + 10, 260 + 10, t.sim, t.cam);
            Spy away(arc);
            hud.render(away, arc, t.sim.get_world_state(), t.cam);
            check(layout.is_classic() ? away.has_sprite_at("accpt2.bmp", 152, 260) : !away.has_sprite_at("accpt2.bmp", 152, 260), at + (layout.is_classic() ? "(the original's own place is its place)" : "... and not at the original's place of it"));
            hud.handle_mouse_motion(152 + m.x + 10, 260 + m.y + 10, t.sim, t.cam);
        }
        press_release(152 + m.x + 10, 260 + m.y + 10);
        check(t.sim.get_world_state().player_alliances[1] == 0 && t.sim.get_world_state().player_alliances[0] == 1, at + "a click at Accept's place on screen forms the team of Bob and Alice");
    }

    // --- the options window and the quick help of the original during a match: its own pictures, drawn over the map view with the HUD around them
    for (const ScreenLayout& layout : {classic, wide}) {
        const bool is_classic = layout.is_classic();
        const LayoutPoint o = layout.options_offset();
        const LayoutPoint q = layout.quick_help_offset();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        {   // the options: Return to Game is (351, 425) 98 x 26 of the window's own numbers
            HudRig rig(arc, layout);
            rig.hud.open_options();
            const Spy spy = rig.frame();
            // the HUD is drawn first, and still: the 14 pieces of the frame, then the window under its origin
            size_t i_origin = spy.events.size(), i_reset = spy.events.size(), i_clip = spy.events.size(), i_unclip = spy.events.size();
            bool clay = false;
            for (size_t i = 0; i < spy.events.size(); ++i) {
                const Spy::Ev& e = spy.events[i];
                if (e.kind == Spy::Kind::Origin && e.x == o.x && e.y == o.y && i_origin == spy.events.size()) i_origin = i;
                if (e.kind == Spy::Kind::Origin && e.x == 0 && e.y == 0 && i > i_origin && i_reset == spy.events.size()) i_reset = i;
                if (e.kind == Spy::Kind::Clip && i > i_origin && i_clip == spy.events.size()) i_clip = i;
                if (e.kind == Spy::Kind::ClearClip && i > i_clip && i_unclip == spy.events.size()) i_unclip = i;
                clay = clay || (e.kind == Spy::Kind::Fill && e.colour.r == 219 && e.colour.g == 75 && e.colour.b == 19);
            }
            check(!clay, at + "no clay margin: the options window does not cover the match");
            size_t pieces_before = 0;
            for (size_t i = 0; i < i_origin && i < spy.events.size(); ++i) pieces_before += (spy.events[i].kind == Spy::Kind::Sprite || spy.events[i].kind == Spy::Kind::Region) ? size_t{1} : size_t{0};
            check(pieces_before >= 14 && spy.has_sprite_at("x480y126.bmp", 480 + layout.dx(), 126), at + "the HUD is drawn before the window: the 14 pieces of the frame (the status card at its place) come first");
            if (is_classic) {
                check(i_clip == spy.events.size(), at + "the original's own picture needs no clip");
            } else {
                const LayoutRect v = layout.view();
                bool clip_ok = false;
                for (const Spy::Ev& e : spy.events) {
                    if (e.kind == Spy::Kind::Clip && e.ox == o.x && e.oy == o.y) clip_ok = e.x == v.x - o.x && e.y == v.y - o.y && e.w == v.w && e.h == v.h;
                }
                check(i_origin < i_clip && i_clip < i_unclip && i_unclip <= i_reset && i_reset < spy.events.size() && clip_ok, at + "the window: its origin (160, 30), the clip of the map view (it stops at the view), the window, the clip off, the origin off");
            }
            // the hover picture of Return: the pointer on it at its place on screen
            rig.hud.handle_mouse_motion(351 + o.x + 10, 425 + o.y + 10, rig.sim, rig.cam);
            check(rig.frame().has_sprite_at("breturn2.bmp", 351, 425), at + "the options' Return button shows its hover picture when the pointer is on it at its place on screen");
            if (!is_classic) {
                rig.hud.handle_mouse_motion(351 + 10, 425 + 10, rig.sim, rig.cam);
                check(!rig.frame().has_sprite_at("breturn2.bmp", 351, 425), at + "... and not when it is at Return's own numbers");
            }
            // the pointer: Return at its place on screen closes the options; at the window's own numbers (without the move) it does not
            check(rig.hud.is_options_open(), at + "the options are open");
            if (!is_classic) {
                rig.click(351 + 10, 425 + 10);
                check(rig.hud.is_options_open(), at + "a click at Return's own numbers (351, 425) does nothing");
                rig.click(351 + o.x - 20, 425 + o.y + 10);
                check(rig.hud.is_options_open(), at + "a click left of Return does nothing");
            }
            rig.click(351 + o.x + 10, 425 + o.y + 10);
            check(!rig.hud.is_options_open(), at + "a click on Return at its place on screen (" + std::to_string(351 + o.x + 10) + ", " + std::to_string(425 + o.y + 10) + ") closes the options");
        }
        {   // the quick help: Return is (529, 437) 98 x 26
            HudRig rig(arc, layout);
            rig.hud.open_quick_help();
            const Spy spy = rig.frame();
            check(rig.hud.is_quick_help_open(), at + "the quick help is open");
            bool clay = false;
            for (const Spy::Ev& e : spy.events) clay = clay || (e.kind == Spy::Kind::Fill && e.colour.r == 219 && e.colour.g == 75 && e.colour.b == 19);
            check(!clay, at + "no clay margin: the quick help does not cover the match");
            check(spy.has_sprite_at("x480y126.bmp", 480 + layout.dx(), 126), at + "the HUD is drawn under it (the status card at its place)");
            rig.hud.handle_mouse_motion(529 + q.x + 10, 437 + q.y + 10, rig.sim, rig.cam);
            check(rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "the quick help's Return button shows its hover picture when the pointer is on it at its place on screen");
            if (!is_classic) {
                rig.hud.handle_mouse_motion(529 + 10, 437 + 10, rig.sim, rig.cam);
                check(!rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "... and not when it is at Return's own numbers");
                rig.hud.handle_mouse_motion(529 + o.x + 90, 437 + o.y + 10, rig.sim, rig.cam);                // (the right end of the button at the options window's place: outside the quick help's own)
                check(!rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "... nor at the place of the options window's offset (the quick help has its own)");
                rig.click(529 + 10, 437 + 10);
                check(rig.hud.is_quick_help_open(), at + "a click at the Return button's own numbers does nothing");
                bool one_origin = false;
                for (const Spy::Ev& e : spy.events) one_origin = one_origin || (e.kind == Spy::Kind::Origin && e.x == 77 && e.y == 31);
                check(one_origin, at + "the quick help is drawn under the origin (77, 31)");
            }
            rig.click(529 + q.x + 10, 437 + q.y + 10);
            check(!rig.hud.is_quick_help_open(), at + "a click on Return at its place on screen closes the quick help");
        }
    }
}

/// The options window and the quick help take the pointer from the 50 ms poll of HUD::update as well as from the events (the INPUT task of the original): a control that is pressed and
/// released while frames and ticks run in between must still be clicked (the poll once fed the window the raw position of the pointer, the original's own numbers moved by the window's
/// offset: Return, the ON / OFF switches of the options and the quick help's Return could not be clicked in the 16:9 match, as the poll took the pointer off the control and cancelled the press)
void test_window_controls_through_update(const assets::AssetArchive& arc) {
    group("window-controls", "every control of the options window and of the quick help can be clicked through the HUD's 50 ms poll (press, update, release), at its place on screen only");
    const ScreenLayout classic = ScreenLayout::classic();
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    for (const ScreenLayout& layout : {classic, wide}) {
        const LayoutPoint o = layout.options_offset();
        const LayoutPoint q = layout.quick_help_offset();
        const bool is_classic = layout.is_classic();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        // press, run `polls` updates (the poll: HUD::update re-feeds the pointer to the window), release; `place` moves the control's own numbers to the screen
        auto press_update_release = [](HudRig& rig, int32_t x, int32_t y, int polls) {
            rig.hud.handle_mouse_motion(x, y, rig.sim, rig.cam);
            rig.hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
            for (int i = 0; i < polls; ++i) rig.hud.update(rig.sim.get_world_state(), 1);
            rig.hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
        };
        // --- the options window
        {
            HudRig rig(arc, layout);
            rig.hud.open_options();
            auto at_screen = [&](int32_t x, int32_t y) { return std::make_pair(x + o.x, y + o.y); };
            // Return to Game
            {
                const auto p = at_screen(351 + 40, 425 + 12);
                press_update_release(rig, p.first, p.second, 3);
                check(!rig.hud.is_options_open(), at + "Return to Game: press, three polls, release: the window closes");
            }
            rig.hud.open_options();
            // Chat Off, Chat On, Quick Help Off, Quick Help On
            struct Toggle { const char* name; int32_t x; bool OptionsState::*field; bool value; };
            const Toggle toggles[] = {{"Chat Off", OptionsScreen::CHAT_OFF_X, &OptionsState::chat, false}, {"Chat On", OptionsScreen::CHAT_ON_X, &OptionsState::chat, true},
                                      {"Quick Help Off", OptionsScreen::HELP_OFF_X, &OptionsState::quick_help, false}, {"Quick Help On", OptionsScreen::HELP_ON_X, &OptionsState::quick_help, true}};
            for (const Toggle& t : toggles) {
                const auto p = at_screen(t.x + 20, OptionsScreen::TOGGLE_Y + 10);
                press_update_release(rig, p.first, p.second, 2);
                check(rig.hud.options().*(t.field) == t.value && rig.hud.is_options_open(), at + std::string(t.name) + ": press, two polls, release: the switch changes");
            }
            // the switches at the window's own numbers (without the offset) do nothing in a 16:9 picture
            if (!is_classic) {
                rig.hud.options().chat = true;
                press_update_release(rig, OptionsScreen::CHAT_OFF_X + 20, OptionsScreen::TOGGLE_Y + 10, 2);
                check(rig.hud.options().chat, at + "Chat Off at the window's own numbers (not moved) does nothing");
            }
            // the sliders: press on the thumb, drag, poll, release: the value follows
            const int32_t* values[3] = {&rig.hud.options().sound_volume, &rig.hud.options().music_volume, &rig.hud.options().scroll_speed};
            for (int i = 0; i < 3; ++i) {
                const int32_t before = *values[i];
                const int32_t thumb_x = OptionsScreen::SLIDER_X + ScreenSlider::TRACK_LEFT_OFFSET + (ScreenSlider::TRACK_SPAN * before) / 99;
                const int32_t y = OptionsScreen::SLIDER_Y[i] + 8;
                const int32_t target_x = thumb_x + (before > 50 ? -60 : 60);
                const auto from = at_screen(thumb_x, y);
                const auto to = at_screen(target_x, y);
                rig.hud.handle_mouse_motion(from.first, from.second, rig.sim, rig.cam);
                rig.hud.handle_mouse_down(from.first, from.second, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                rig.hud.handle_mouse_motion(to.first, to.second, rig.sim, rig.cam);
                rig.hud.update(rig.sim.get_world_state(), 1);              // the poll re-feeds the same position: the drag goes on
                rig.hud.handle_mouse_up(to.first, to.second, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                check(before > 50 ? *values[i] < before - 20 : *values[i] > before + 20, at + "slider " + std::to_string(i) + ": press on the thumb, drag, poll, release: the value moved from " + std::to_string(before) + " to " + std::to_string(*values[i]));
            }
            // an edit field takes the focus and the typed text
            {
                const auto p = at_screen(OptionsScreen::EDIT_X[2] + 20, OptionsScreen::EDIT_Y[2] + 5);
                press_update_release(rig, p.first, p.second, 1);
                rig.hud.handle_text_input("Q");
                check(!rig.hud.options().quick_chat[2].empty() && rig.hud.options().quick_chat[2].back() == 'Q', at + "the F11 field: a click at its place gives it the focus and it takes the typed letter");
            }
            check(rig.hud.is_options_open(), at + "(the window is still open: nothing but Return closes it)");
        }
        // --- the windows opened the way a player opens them: by CLICKING the HUD's own Options and Help buttons (press, release, an update between every event), then Return (the web build's
        //     report: after the click on Options a click on Return did nothing, even a second one, in 16:9; Ctrl+O first did not show it)
        {
            HudRig rig(arc, layout);
            auto step = [&](int32_t x, int32_t y, bool down_event, bool up_event) {
                rig.hud.update(rig.sim.get_world_state(), 1);
                rig.hud.handle_mouse_motion(x, y, rig.sim, rig.cam);
                rig.hud.update(rig.sim.get_world_state(), 1);
                if (down_event) {
                    rig.hud.handle_mouse_down(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                    rig.hud.update(rig.sim.get_world_state(), 1);
                    rig.hud.update(rig.sim.get_world_state(), 1);
                }
                if (up_event) {
                    rig.hud.handle_mouse_up(x, y, SDL_BUTTON_LEFT, rig.sim, rig.cam);
                    rig.hud.update(rig.sim.get_world_state(), 1);
                }
            };
            const UIButton opt = rig.hud.options_button();
            step(opt.x + opt.w / 2, opt.y + opt.h / 2, true, true);
            check(rig.hud.is_options_open() && !rig.hud.options_button().is_pressed, at + "a click on the HUD's Options button opens the options window (updates between every event)");
            // a few frames of the pointer resting on the window's Return, then the click
            const int32_t rx = 351 + o.x + 40, ry = 425 + o.y + 12;
            step(rx, ry, false, false);
            check(rig.hud.is_options_open(), at + "(the window is still open while the pointer rests on Return)");
            step(rx, ry, true, true);
            check(!rig.hud.is_options_open(), at + "the first click on Return closes the window that the Options button opened");
            const UIButton help = rig.hud.help_button();
            step(help.x + help.w / 2, help.y + help.h / 2, true, true);
            check(rig.hud.is_quick_help_open() && !rig.hud.help_button().is_pressed, at + "a click on the HUD's Help button opens the quick help");
            const int32_t qx = 529 + q.x + 40, qy = 437 + q.y + 12;
            step(qx, qy, false, false);
            step(qx, qy, true, true);
            check(!rig.hud.is_quick_help_open(), at + "the first click on its Return closes it");
            // and again: the same buttons open them a second time and Return closes them a second time
            step(opt.x + opt.w / 2, opt.y + opt.h / 2, true, true);
            step(rx, ry, true, true);
            check(!rig.hud.is_options_open(), at + "the Options button and Return a second time: closed");
            // the click that closes a window does not act on the HUD under it: the pointer is over the map there, the selection and the pedestals stay as they were
            check(!rig.hud.is_quick_help_open() && !rig.hud.is_options_open(), at + "no window is left open");
        }
        // --- the quick help
        {
            HudRig rig(arc, layout);
            rig.hud.open_quick_help();
            press_update_release(rig, 529 + q.x + 40, 437 + q.y + 12, 3);
            check(!rig.hud.is_quick_help_open(), at + "the quick help's Return: press, three polls, release: it closes");
            if (!is_classic) {
                rig.hud.open_quick_help();
                press_update_release(rig, 529 + o.x + 40, 437 + o.y + 12, 3);           // where the OPTIONS window's offset would put it
                check(rig.hud.is_quick_help_open(), at + "the quick help's Return at the options window's offset does nothing (the quick help has its own place)");
                press_update_release(rig, 529 + 40, 437 + 12, 3);
                check(rig.hud.is_quick_help_open(), at + "... nor at its own numbers");
            }
        }
    }
}

// =====================================================================================================================================================
// 7. The view: the camera, the edge strips, the start view, the minimap, small maps
// =====================================================================================================================================================

void test_camera_clamps() {
    group("camera", "the camera of the 762 x 500 view: limits on a big map, a map smaller than the view is centred and the camera is fixed there");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    auto camera = [&](bool centre) {
        ViewportCamera c;
        c.set_view(wide.view());
        c.centre_small_maps = centre;
        return c;
    };
    // a big map: 60 x 60 tiles = 1920 px; the view's origin lies in [0, 1920 - 762] x [0, 1920 - 500]
    {
        ViewportCamera c = camera(true);
        check(c.viewport_w == 762 && c.viewport_h == 500 && c.view_x == 16 && c.view_y == 21, "the camera takes the layout's view: 762 x 500 at (16, 21)");
        c.x = -300;
        c.y = -9;
        c.clamp_to_bounds(60, 60);
        check(c.world_x == 0 && c.world_y == 0, "a big map: a camera beyond the top left corner is at (0, 0)");
        c.x = 9000;
        c.y = 9000;
        c.clamp_to_bounds(60, 60);
        check(c.world_x == 1920 - 762 && c.world_y == 1920 - 500, "a big map: beyond the bottom right corner it is at (1158, 1420)");
        c.center_on(960, 960, 60, 60);
        check(c.world_x == 960 - 381 && c.world_y == 960 - 250, "center_on puts the point in the middle of the 762 x 500 view");
        c.center_on(10, 10, 60, 60);
        check(c.world_x == 0 && c.world_y == 0, "center_on near the corner is held at the map's corner");
        ViewportCamera classic;
        classic.x = 9000;
        classic.y = 9000;
        classic.clamp_to_bounds(60, 60);
        check(classic.world_x == 1920 - 442 && classic.world_y == 1920 - 440, "the original's view: the limits are (1478, 1480)");
    }
    // a map that is smaller than the view: 16 x 16 tiles = 512 px (762 wide: centred; 500 high: the map is 12 px taller than the view, so a range of 12)
    {
        ViewportCamera c = camera(true);
        c.x = 300;
        c.y = 300;
        c.clamp_to_bounds(16, 16);
        check(c.world_x == (512 - 762) / 2 && c.world_x == -125, "16 x 16: the camera is fixed at x = -125 (the map centred in the 762 px view)");
        check(c.world_y == 12, "16 x 16: vertically the map is 12 px taller than the view: the camera is held at 12 (the largest)");
        c.y = -50;
        c.clamp_to_bounds(16, 16);
        check(c.world_y == 0 && c.world_x == -125, "16 x 16: and at 0 (the smallest); x stays");
        c.center_on(256, 256, 16, 16);
        check(c.world_x == -125 && c.world_y == 6, "16 x 16: center_on centres the map in x and puts the point in the middle in y");
        // what is where: the map's left edge is 125 px right of the view's left edge
        int32_t sx = 0, sy = 0;
        c.world_to_screen(0, 0, sx, sy);
        check(sx == 16 + 125 && sy == 21 - 6, "the world pixel (0, 0) is at the screen pixel (141, 15)");
        c.world_to_screen(512, 512, sx, sy);
        check(sx == 16 + 125 + 512, "the map's right edge is at x = 653");
        // the listener is the middle of the view: the middle of the map
        check(c.world_x + 762 / 2 == 512 / 2, "the middle of the view is the middle of the map (the sound listener)");
        ViewportCamera d = camera(true);
        d.x = 5;
        d.y = 5;
        d.clamp_to_bounds(12, 12);
        check(d.world_x == (384 - 762) / 2 && d.world_y == (384 - 500) / 2 && d.world_x == -189 && d.world_y == -58, "12 x 12: both axes are centred: (-189, -58)");
        d.center_on(100, 100, 12, 12);
        check(d.world_x == -189 && d.world_y == -58, "12 x 12: center_on cannot move it");
        d.scroll_pixels(40, 40, 12, 12);
        check(d.world_x == -189 && d.world_y == -58, "12 x 12: scrolling cannot move it");
    }
    // a map exactly as wide as the view
    {
        ViewportCamera c = camera(true);
        c.x = 50;
        c.clamp_to_bounds(60, 60);
        ViewportCamera e;
        e.set_view(LayoutRect{16, 21, 640, 500});
        e.centre_small_maps = true;
        e.x = 77;
        e.clamp_to_bounds(20, 20);                       // 640 px = the view's width: the range is [0, 0]
        check(e.world_x == 0, "a map exactly as wide as the view: the camera is at 0");
    }
    // a map one pixel wider than the view has a range of one pixel (a view of 639 px, a map of 640)
    {
        ViewportCamera c;
        c.set_view(LayoutRect{16, 21, 639, 500});
        c.centre_small_maps = true;
        c.x = 5;
        c.clamp_to_bounds(20, 20);
        check(c.world_x == 1, "a map that is 1 px wider than the view: the camera may be at 1 (the range is [0, 1], not centred)");
        c.x = 0;
        c.clamp_to_bounds(20, 20);
        check(c.world_x == 0, "... and at 0");
    }
    // the original's camera keeps its rule: a map smaller than the view sits at the corner (the fingerprints of the classic picture pin it)
    {
        ViewportCamera c = camera(false);
        c.x = 40;
        c.y = 40;
        c.clamp_to_bounds(16, 16);
        check(c.world_x == 0 && c.world_y == 12, "without centre_small_maps: x = 0 (the old rule), y clamps to its range");
        ViewportCamera classic;
        classic.x = 40;
        classic.clamp_to_bounds(13, 9);
        check(classic.world_x == 0 && classic.world_y == 0, "the original's camera: a map smaller than the 442 x 440 view is at (0, 0)");
    }
    // the renderer sets the flag for every layout that is not the original's
    {
        const QuietStdout quiet;
        ensure_sdl();
        SDL_Window* win = SDL_CreateWindow("wide-hud", 0, 0, 640, 480, SDL_WINDOW_HIDDEN);
        assets::AssetArchive arc;
        arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd");
        Renderer r;
        if (win != nullptr && r.init(win, arc)) {
            check(!r.camera().centre_small_maps, "a new renderer has the original's camera rule");
            r.set_layout(wide);
            check(r.camera().centre_small_maps && r.camera().viewport_w == 762, "a wide layout centres small maps");
            r.set_layout(ScreenLayout::classic());
            check(!r.camera().centre_small_maps && r.camera().viewport_w == 442, "the original's layout does not");
            r.shutdown();
        } else {
            check(false, "a renderer for the camera rule");
        }
        if (win != nullptr) SDL_DestroyWindow(win);
    }
}

void test_edge_strips() {
    group("strips", "the edge strips run along the edges of the whole 960 x 540 picture: every pixel, the scroll arrows and the inner strips; a map smaller than the view has none");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    // an independent rule: the 12 px band along the edges gives the arrow of its side, the corners the diagonal ones
    auto expected_dir = [](int32_t x, int32_t y) {
        const int32_t h = x < 12 ? -1 : (x >= 948 ? 1 : 0);
        const int32_t v = y < 12 ? -1 : (y >= 528 ? 1 : 0);
        // N NE E SE S SW W NW
        if (v == -1 && h == 0) return 0;
        if (v == -1 && h == 1) return 1;
        if (v == 0 && h == 1) return 2;
        if (v == 1 && h == 1) return 3;
        if (v == 1 && h == 0) return 4;
        if (v == 1 && h == -1) return 5;
        if (v == 0 && h == -1) return 6;
        if (v == -1 && h == -1) return 7;
        return -1;
    };
    // a camera in the middle of a big map: every strip can scroll
    int64_t wrong = 0, bands = 0;
    for (int32_t y = 0; y < 540; ++y) {
        for (int32_t x = 0; x < 960; ++x) {
            const EdgeScroll e = edge_scroll_step(x, y, 50, 600, 600, 60, 60, wide);
            if (e.dir != expected_dir(x, y)) ++wrong;
            if (e.dir >= 0) ++bands;
        }
    }
    check(wrong == 0, "from the middle of a 60 x 60 map every one of the 518,400 pixels gives the arrow of the band it is in (" + std::to_string(wrong) + " wrong)");
    {
        // the bands are exactly the picture's frame: the pixels with x < 12 or x >= 948 or y < 12 or y >= 528 (and the quiet margin of 13 does not change that)
        int64_t frame = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) frame += (x < 12 || x >= 948 || y < 12 || y >= 528) ? 1 : 0;
        }
        check(bands == frame, "the pixels with an arrow are the 12 px frame of the 960 x 540 picture (" + std::to_string(frame) + ")");
    }
    // the inner 5 px strips move the view, the rest of the bands only show the arrow
    {
        const EdgeScroll east_inner = edge_scroll_step(958, 270, 50, 600, 600, 60, 60, wide);
        const EdgeScroll east_band = edge_scroll_step(950, 270, 50, 600, 600, 60, 60, wide);
        check(east_inner.dir == 2 && east_inner.dx > 0 && east_band.dir == 2 && east_band.dx == 0 && east_band.dy == 0, "the east strip: the inner 5 px scroll east, the rest of the 12 px band does not");
        const EdgeScroll south_inner = edge_scroll_step(480, 537, 50, 600, 600, 60, 60, wide);
        check(south_inner.dir == 4 && south_inner.dy > 0, "the south strip scrolls south");
        const EdgeScroll classic_east = edge_scroll_step(700, 270, 50, 600, 600, 60, 60);
        check(classic_east.dir == -1, "(at x = 700 the original's picture has no strip: the strips are the layout's)");
    }
    // at the map's corners only the strips that can move the view show an arrow
    {
        const EdgeScroll n_at_top = edge_scroll_step(480, 3, 50, 100, 0, 60, 60, wide);
        const EdgeScroll s_at_top = edge_scroll_step(480, 537, 50, 100, 0, 60, 60, wide);
        const EdgeScroll w_at_left = edge_scroll_step(3, 270, 50, 0, 100, 60, 60, wide);
        const EdgeScroll e_at_right = edge_scroll_step(958, 270, 50, 1920 - 762, 100, 60, 60, wide);
        check(n_at_top.dir == -1 && s_at_top.dir == 4 && w_at_left.dir == -1 && e_at_right.dir == -1, "a strip that cannot move the view shows no arrow (north at the top, west at the left, east at the right end of the map)");
    }
    // a map that is smaller than the view in both directions: no strip scrolls anywhere (12 x 12 tiles, the camera centred at (-189, -58))
    {
        int64_t arrows = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) arrows += edge_scroll_step(x, y, 50, -189, -58, 12, 12, wide).dir >= 0 ? 1 : 0;
        }
        check(arrows == 0, "a 12 x 12 map is smaller than the view in both directions: no pixel scrolls (" + std::to_string(arrows) + ")");
    }
    // 16 x 16: 512 px: narrower than the view (no east, west) and 12 px taller (north / south and the corners that have a vertical part)
    {
        const EdgeScroll e = edge_scroll_step(958, 270, 50, -125, 6, 16, 16, wide);
        const EdgeScroll w = edge_scroll_step(3, 270, 50, -125, 6, 16, 16, wide);
        const EdgeScroll n = edge_scroll_step(480, 3, 50, -125, 6, 16, 16, wide);
        const EdgeScroll s = edge_scroll_step(480, 537, 50, -125, 6, 16, 16, wide);
        check(e.dir == -1 && w.dir == -1 && n.dir == 0 && s.dir == 4, "16 x 16: only the north and south strips can move the view (it is 12 px taller than the 500 px view)");
        check(n.dy != 0 || n.dir == 0, "16 x 16: ... and they scroll");
    }
}

void test_start_view_and_minimap(const assets::AssetArchive& arc) {
    group("view", "the start view and the minimap follow the 762 x 500 view");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    // the start view: the square around the hill (ax - 160 .. ax + 192) clipped to the map, scrolled just far enough to show it in a 762 x 500 view: an independent rule
    int64_t wrong = 0, count = 0;
    for (const int32_t tiles : {60, 40, 31, 22, 14, 13}) {
        for (int32_t ty = 0; ty < tiles; ++ty) {
            for (int32_t tx = 0; tx < tiles; ++tx) {
                const int32_t ax = tx * 32 + 16, ay = ty * 32 + 16, map_px = tiles * 32;
                const int32_t r = std::min(ax + 192, map_px), b = std::min(ay + 192, map_px);
                const int32_t want_x = std::max(0, r - 762), want_y = std::max(0, b - 500);
                int32_t ox = -1, oy = -1;
                start_view_origin(tx, ty, tiles, tiles, ox, oy, wide);
                wrong += (ox != want_x || oy != want_y) ? 1 : 0;
                ++count;
            }
        }
    }
    check(wrong == 0, "the start view of every anchor tile of six maps is the one that shows the square in a 762 x 500 view (" + std::to_string(count) + " tiles, " + std::to_string(wrong) + " wrong)");
    {
        int32_t cx = 0, cy = 0, wx = 0, wy = 0;
        start_view_origin(46, 46, 60, 60, cx, cy);
        start_view_origin(46, 46, 60, 60, wx, wy, wide);
        check(cx == 1238 && cy == 1240 && wx == 918 && wy == 1180, "the far hill (46, 46) of a 60 x 60 map: the original's start view is (1238, 1240), the 762 x 500 view's (918, 1180)");
    }
    // the minimap: its frame is the view's size scaled to the image (+ 1), the whole image for a map the view covers
    auto frame_of = [&](uint32_t tiles, int32_t cam_x, int32_t cam_y) {
        sim::WorldState world;
        world.width = tiles;
        world.height = tiles;
        world.cells.assign(static_cast<size_t>(tiles) * tiles, sim::TileCell{});
        HUD hud;
        hud.set_layout(wide);
        ViewportCamera cam;
        cam.set_view(wide.view());
        cam.centre_small_maps = true;
        cam.x = static_cast<float>(cam_x);
        cam.y = static_cast<float>(cam_y);
        cam.clamp_to_bounds(tiles, tiles);
        Spy spy(arc);
        hud.render(spy, arc, world, cam);
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Rect && e.colour.r == 251 && e.colour.g == 251 && e.colour.b == 255) return LayoutRect{e.x, e.y, e.w, e.h};
        }
        return LayoutRect{-1, -1, -1, -1};
    };
    check_rect(frame_of(60, 0, 0), LayoutRect{800, 35, 48, 24}, "60 x 60: the view frame is 48 x 24 at the corner of the minimap (762 * 119 / 1920 + 1, 500 * 91 / 1920 + 1)");
    check_rect(frame_of(60, 600, 700), LayoutRect{800 + 600 * 119 / 1920, 35 + 700 * 91 / 1920, 48, 24}, "60 x 60: and it follows the camera");
    check_rect(frame_of(40, 0, 0), LayoutRect{800, 35, 71, 36}, "40 x 40: a bigger share of the map: 71 x 36");
    check_rect(frame_of(16, 0, 0), LayoutRect{800, 35, 119, 89}, "16 x 16: the view covers the 512 px wide map (the whole image's width) and 500 of its 512 px height (500 * 91 / 512 + 1 = 89)");
    check_rect(frame_of(12, 0, 0), LayoutRect{800, 35, 119, 91}, "12 x 12: the whole map is in the view: the frame is the whole image");
}

void test_small_map_pointer(const assets::AssetArchive& arc) {
    group("small-map", "on a map smaller than the view the black around it is no ground: the plain pointer, a click there does nothing at all (no deselect, no order, no marker, no pedestal change); the map itself works");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    HudRig rig(arc, wide, 0, 16);                                    // 16 x 16 tiles: the camera is at (-125, 12 at most)
    const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{4, 4});
    rig.sim.tick();
    rig.cam.x = 0;
    rig.cam.y = 0;
    rig.cam.clamp_to_bounds(16, 16);
    check(rig.cam.world_x == -125 && rig.cam.world_y == 0, "the camera of the 16 x 16 map is at (-125, 0)");
    std::vector<std::pair<int32_t, int32_t>> markers;
    rig.hud.set_on_spawn_click_marker([&markers](int32_t x, int32_t y) { markers.emplace_back(x, y); });
    const sim::WorldState world = rig.sim.get_world_state();
    rig.hud.select_ant(ant);
    rig.hud.update(world, 0);
    // the map starts at screen x = 16 + 125 = 141: left of it is black, and so is x >= 653
    const int32_t y = 300;
    check(rig.hud.evaluate_cursor(100, y, world, rig.sim.grid(), rig.cam) == CursorType::Normal, "over the black left of the map: the plain pointer");
    check(rig.hud.evaluate_cursor(140, y, world, rig.sim.grid(), rig.cam) == CursorType::Normal, "... at its last pixel (x = 140)");
    check(rig.hud.evaluate_cursor(141, y, world, rig.sim.grid(), rig.cam) == CursorType::Move, "the map's first column (x = 141) is ground: the move cursor");
    check(rig.hud.evaluate_cursor(652, y, world, rig.sim.grid(), rig.cam) == CursorType::Move, "its last column (x = 652)");
    check(rig.hud.evaluate_cursor(653, y, world, rig.sim.grid(), rig.cam) == CursorType::Normal, "... and the black right of it (x = 653)");
    // the map's last row: world y 511 is at screen y 21 + 511 = 532 - ... the view ends at 521: the whole view is ground here (the map is taller than the view's 500 only by 12)
    check(rig.hud.evaluate_cursor(300, 21, world, rig.sim.grid(), rig.cam) == CursorType::Move && rig.hud.evaluate_cursor(300, 520, world, rig.sim.grid(), rig.cam) == CursorType::Move, "the top and the bottom row of the view are ground (the map is 12 px taller than the view)");
    // a click on the black does nothing at all: the selection stays, the latched pedestal stays, no marker, no order; a click on the map orders
    {
        const int32_t pedestal_x = wide.right(482) + 12, pedestal_y = 152 + 12;                 // the Move pedestal's slot (482, 152) 43 x 73 + (dx, 0)
        rig.click(pedestal_x, pedestal_y);
        check(rig.hud.is_move_latched(), "(the Move pedestal is latched by a click on it)");
        rig.click(100, y);
        check(markers.empty() && rig.hud.get_selected_ant_id() == ant && rig.hud.get_selected_ant_ids().size() == 1 && rig.hud.is_move_latched(), "a click on the black left of the map: the ant stays selected, the pedestal stays latched, no marker");
        rig.click(700, y);
        check(markers.empty() && rig.hud.get_selected_ant_id() == ant && rig.hud.is_move_latched(), "... and on the black right of it");
        rig.click(100, y, SDL_BUTTON_RIGHT);
        check(markers.empty() && rig.hud.get_selected_ant_id() == ant && rig.hud.is_move_latched(), "a right click on the black orders nothing (no marker either)");
        // pressed on the black, released on the map: the order's tile would be the press point, which is no ground
        rig.hud.handle_mouse_down(100, y, SDL_BUTTON_RIGHT, rig.sim, rig.cam);
        rig.hud.handle_mouse_up(300, y, SDL_BUTTON_RIGHT, rig.sim, rig.cam);
        check(markers.empty() && rig.hud.is_move_latched(), "a right press on the black released on the map orders nothing either");
        rig.hud.unlatch_pedestals();
    }
    rig.click(300, y);
    check(markers.size() == 1 && markers[0].first == 300 - 16 - 125 && markers[0].second == y - 21, "a click on the map orders the world point under the pointer (159, 279)");
    // a rubber band over the black selects the ants of the map under it and does not fail
    rig.hud.clear_selection();
    rig.hud.handle_mouse_down(20, 40, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    rig.hud.handle_mouse_motion(700, 500, rig.sim, rig.cam);
    rig.hud.handle_mouse_up(700, 500, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    check(rig.hud.get_selected_ant_id() == ant, "a rubber band from the black left of the map to the black right of it selects the own ant on the map");
}

/// A map that is smaller than the view in y as well as in x: the black is above and below the map, and the boundary rows decide the pointer
void test_small_map_pointer_rows(const assets::AssetArchive& arc) {
    group("small-map-rows", "a map smaller than the view on both axes (12 x 12): the pointer's boundary on all four sides, the black above and below is no ground");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    HudRig rig(arc, wide, 0, 12);                                     // 384 x 384 px: the camera is at (-189, -58)
    const uint32_t ant = rig.sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{4, 4});
    rig.sim.tick();
    rig.cam.x = 0;
    rig.cam.y = 0;
    rig.cam.clamp_to_bounds(12, 12);
    check(rig.cam.world_x == -189 && rig.cam.world_y == -58, "the camera of the 12 x 12 map is at (-189, -58)");
    std::vector<std::pair<int32_t, int32_t>> markers;
    rig.hud.set_on_spawn_click_marker([&markers](int32_t x, int32_t y) { markers.emplace_back(x, y); });
    const sim::WorldState world = rig.sim.get_world_state();
    rig.hud.select_ant(ant);
    rig.hud.update(world, 0);
    // the map covers the screen x 205 .. 588 and y 79 .. 462
    const int32_t mid_x = 400, mid_y = 270;
    auto cursor = [&](int32_t x, int32_t y) { return rig.hud.evaluate_cursor(x, y, world, rig.sim.grid(), rig.cam); };
    check(cursor(mid_x, 78) == CursorType::Normal && cursor(mid_x, 40) == CursorType::Normal, "above the map (y = 78 and 40): the plain pointer");
    check(cursor(mid_x, 79) == CursorType::Move && cursor(mid_x, 462) == CursorType::Move, "the map's first and last row (y = 79 and 462): ground");
    check(cursor(mid_x, 463) == CursorType::Normal && cursor(mid_x, 500) == CursorType::Normal, "below the map (y = 463 and 500): the plain pointer");
    check(cursor(204, mid_y) == CursorType::Normal && cursor(205, mid_y) == CursorType::Move && cursor(588, mid_y) == CursorType::Move && cursor(589, mid_y) == CursorType::Normal, "left and right of the map: the boundary is x = 204 | 205 and 588 | 589");
    // the corners of the map
    check(cursor(205, 79) == CursorType::Move && cursor(588, 462) == CursorType::Move && cursor(204, 78) == CursorType::Normal && cursor(589, 463) == CursorType::Normal, "the corners: (205, 79) and (588, 462) are ground, one pixel outside is not");
    check(rig.hud.over_ground(mid_x, 79, rig.cam, rig.sim.grid()) && !rig.hud.over_ground(mid_x, 78, rig.cam, rig.sim.grid()) && !rig.hud.over_ground(mid_x, 463, rig.cam, rig.sim.grid()) && !rig.hud.over_ground(204, mid_y, rig.cam, rig.sim.grid()), "over_ground has the same boundary");
    // a click one pixel above the map does nothing at all, one pixel inside it orders
    rig.click(mid_x, 78);
    check(markers.empty() && rig.hud.get_selected_ant_id() == ant, "a click at y = 78 (just above the map): no marker, the ant stays selected");
    rig.click(mid_x, 463);
    check(markers.empty() && rig.hud.get_selected_ant_id() == ant, "a click at y = 463 (just below it): nothing");
    rig.click(mid_x, 79);
    check(markers.size() == 1 && markers[0].second == 79 - 21 - 58, "a click at y = 79 (the map's first row): it orders the world point (211, 0)");
}

// --- a small level made from the bytes of TINY.LVL (nothing of the community is in the repository): the columns `cols` and the rows `rows` of its two layers and the records of blocks 1 and 2
// that lie inside them, shifted; the file format is the loader's (header, dictionary, the two dimension dwords, two layers of 3 words a cell, block 1, block 2, block 3, block 4, the egg stock)
struct LevelBytes {
    std::vector<uint8_t> d;
    size_t p{0};
    uint16_t u16() { const uint16_t v = static_cast<uint16_t>(d[p] | (d[p + 1] << 8)); p += 2; return v; }
    uint32_t u32() { const uint32_t lo = u16(); const uint32_t hi = u16(); return lo | (hi << 16); }
};
void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(static_cast<uint8_t>(v & 0xFF)); o.push_back(static_cast<uint8_t>(v >> 8)); }
void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, static_cast<uint16_t>(v & 0xFFFF)); put16(o, static_cast<uint16_t>(v >> 16)); }

/// Writes a level of cols.size() x rows.size() tiles cut out of TINY.LVL to `path`; false if TINY.LVL cannot be read
bool write_small_level(const std::string& path, const std::vector<int32_t>& cols, const std::vector<int32_t>& rows) {
    LevelBytes in;
    {
        std::ifstream f(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL", std::ios::binary);
        if (!f) return false;
        in.d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    std::vector<uint8_t> out;
    const uint32_t version = in.u32(), mode = in.u32();
    const uint16_t minutes = in.u16();
    const size_t desc_at = in.p;
    in.p += 30;
    const uint16_t tcount = in.u16();
    const size_t dict_at = in.p;
    in.p += (static_cast<size_t>(tcount) + 1) * 11;
    const uint32_t src_rows = in.u32(), src_cols = in.u32();
    const size_t n = static_cast<size_t>(src_rows) * src_cols;
    std::vector<std::array<uint16_t, 3>> l1(n), l2(n);
    for (auto& c : l1) c = {in.u16(), in.u16(), in.u16()};
    for (auto& c : l2) c = {in.u16(), in.u16(), in.u16()};
    put32(out, version);
    put32(out, mode);
    put16(out, minutes);
    out.insert(out.end(), in.d.begin() + static_cast<std::ptrdiff_t>(desc_at), in.d.begin() + static_cast<std::ptrdiff_t>(desc_at) + 30);
    put16(out, tcount);
    out.insert(out.end(), in.d.begin() + static_cast<std::ptrdiff_t>(dict_at), in.d.begin() + static_cast<std::ptrdiff_t>(dict_at) + static_cast<std::ptrdiff_t>((static_cast<size_t>(tcount) + 1) * 11));
    put32(out, static_cast<uint32_t>(rows.size()));
    put32(out, static_cast<uint32_t>(cols.size()));
    for (const auto* layer : {&l1, &l2}) {
        for (const int32_t r : rows) {
            for (const int32_t c : cols) {
                const auto& cell = (*layer)[static_cast<size_t>(r) * src_cols + static_cast<size_t>(c)];
                put16(out, cell[0]);
                put16(out, cell[1]);
                put16(out, cell[2]);
            }
        }
    }
    auto index_of = [](const std::vector<int32_t>& v, int32_t x) -> int32_t {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == x) return static_cast<int32_t>(i);
        }
        return -1;
    };
    // block 1: (tile, row, column)
    const uint16_t b1_count = in.u16();
    std::vector<std::array<uint16_t, 3>> b1;
    for (uint16_t i = 0; i < b1_count; ++i) {
        const uint16_t tile = in.u16(), row = in.u16(), col = in.u16();
        const int32_t nr = index_of(rows, row), nc = index_of(cols, col);
        if (nr >= 0 && nc >= 0) b1.push_back({tile, static_cast<uint16_t>(nr), static_cast<uint16_t>(nc)});
    }
    put16(out, static_cast<uint16_t>(b1.size()));
    for (const auto& r : b1) for (const uint16_t v : r) put16(out, v);
    // block 2: food (row, column, units, points, stage count, stages)
    const uint16_t food_count = in.u16();
    std::vector<std::vector<uint16_t>> food;
    for (uint16_t i = 0; i < food_count; ++i) {
        std::vector<uint16_t> rec = {in.u16(), in.u16(), in.u16(), in.u16(), in.u16()};
        for (uint16_t k = 0; k < rec[4]; ++k) {
            rec.push_back(in.u16());
            rec.push_back(in.u16());
        }
        const int32_t nr = index_of(rows, rec[0]), nc = index_of(cols, rec[1]);
        if (nr >= 0 && nc >= 0) {
            rec[0] = static_cast<uint16_t>(nr);
            rec[1] = static_cast<uint16_t>(nc);
            food.push_back(rec);
        }
    }
    put16(out, static_cast<uint16_t>(food.size()));
    for (const auto& rec : food) for (const uint16_t v : rec) put16(out, v);
    const uint16_t b3a = in.u16(), b3b = in.u16();                                   // block 3 (the default ant type): as TINY has it
    put16(out, b3a);
    put16(out, b3b);
    put16(out, 0);                                                                    // block 4: no waypoints
    put16(out, 3);                                                                    // the egg stock
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

/// What hangs over the edge of a map that is smaller than the view is cut at the edge: the black around the map stays black (an ant, an effect or a tall sprite at the edge used to draw
/// onto it: the clip was the view, not the view that the map covers)
void test_small_map_clip(const assets::AssetArchive& arc) {
    group("small-map-draw", "on a map smaller than the view nothing draws onto the black around it: the clip is the part of the view that the map covers");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    // the clip rectangle: view and map intersected
    {
        RendererRig rig(arc, 960, 540);
        check(rig.ok, "the renderer rig is up");
        if (!rig.ok) return;
        rig.renderer.set_layout(wide);
        const LayoutRect view = wide.view();
        struct Case { uint32_t tiles; LayoutRect want; const char* what; };
        const Case cases[] = {{15, LayoutRect{157, 31, 480, 480}, "15 x 15 (480 px: smaller than the view on both axes, camera (-141, -10))"},
                              {12, LayoutRect{205, 79, 384, 384}, "12 x 12 (384 px: camera (-189, -58))"},
                              {16, LayoutRect{141, 21, 512, 500}, "16 x 16 (512 px wide, 12 px taller than the view: only x is cut)"},
                              {31, view, "31 x 31 (992 px: bigger than the view on both axes: the whole view)"},
                              {60, view, "60 x 60: the whole view"}};
        for (const Case& c : cases) {
            rig.renderer.camera().x = 0.0f;
            rig.renderer.camera().y = 0.0f;
            rig.renderer.camera().clamp_to_bounds(c.tiles, c.tiles);
            check_rect(rig.renderer.map_view_rect(c.tiles, c.tiles), c.want, c.what);
        }
        // a map as wide as the view or wider whose camera is scrolled to its far edge: the view still
        rig.renderer.camera().x = 100000.0f;
        rig.renderer.camera().y = 100000.0f;
        rig.renderer.camera().clamp_to_bounds(60, 60);
        check_rect(rig.renderer.map_view_rect(60, 60), view, "60 x 60 with the camera at its far corner: the whole view");
        // the original's own picture: every map is bigger than its view, the clip is the view
        RendererRig classic(arc, 640, 480);
        if (classic.ok) {
            classic.renderer.set_layout(ScreenLayout::classic());
            classic.renderer.camera().clamp_to_bounds(31, 31);
            check_rect(classic.renderer.map_view_rect(31, 31), ScreenLayout::classic().view(), "classic: a 31 x 31 map: the view (16, 21, 442 x 440)");
        }
    }
    // the pixels: ants half out of a 15 x 15 map at its four edges and corners leave the black black, and are drawn on the map
    {
        RendererRig rig(arc, 960, 540);
        if (!rig.ok) return;
        rig.renderer.set_layout(wide);
        rig.renderer.set_hud_team(0);
        sim::SimulationEngine sim;
        sim.init_test_world(15, 15, 1, 600000);
        sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{7, 7});
        sim.tick();
        sim::WorldState world = sim.get_world_state();
        check(!world.ants.empty(), "an ant is in the world");
        if (world.ants.empty()) return;
        const sim::AntSnapshot proto = world.ants.front();
        world.ants.clear();
        const std::pair<int32_t, int32_t> spots[] = {{0, 240}, {479, 240}, {240, 0}, {240, 479}, {0, 0}, {479, 0}, {0, 479}, {479, 479}};
        uint32_t next = 1000;
        for (const auto& spot : spots) {
            sim::AntSnapshot a = proto;
            a.id = next++;
            a.px = spot.first;
            a.py = spot.second;
            a.tile_x = spot.first / 32;
            a.tile_y = spot.second / 32;
            world.ants.push_back(a);
        }
        rig.renderer.camera().x = 0.0f;
        rig.renderer.camera().y = 0.0f;
        rig.renderer.camera().clamp_to_bounds(15, 15);
        rig.renderer.begin_frame();
        rig.renderer.render_world(world, sim.grid(), 0, {}, false, false, 0, 0, -1, 0.0f);
        const std::vector<uint8_t> px = rig.read();
        const LayoutRect view = wide.view();
        const LayoutRect map{157, 31, 480, 480};
        int64_t stray = 0, ground_changed = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) {
                const size_t i = (static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4;
                const bool black = px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0;
                if (!map.contains(x, y) && !black) ++stray;                          // nothing but black outside the map (the frame was cleared black and no HUD is drawn)
                if (map.contains(x, y) && !(px[i] == 135 && px[i + 1] == 120 && px[i + 2] == 110)) ++ground_changed;
            }
        }
        (void)view;
        check(stray == 0, "nothing is drawn outside the map: " + std::to_string(stray) + " pixels of the black around it are not black");
        check(ground_changed > 200, "the ants are drawn on the map (" + std::to_string(ground_changed) + " pixels are not plain ground)");
    }
}

// =====================================================================================================================================================
// 8. The application: the picture of each screen, the pointer, the margin, the default
// =====================================================================================================================================================

SDL_Window* find_window() {
    for (uint32_t i = 1; i < 256; ++i) {
        if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
    }
    return nullptr;
}

std::filesystem::path temp_ini(const char* stem) {
    static int counter = 0;
    return std::filesystem::temp_directory_path() / (std::string(stem) + "_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(counter++) + ".ini");
}

/// An application on the dummy video driver, headless (a hidden window), with its settings in a file of this run
struct AppFixture {
    AppFixture(Aspect aspect, bool match, int32_t window_w = 0, int32_t window_h = 0, const std::string& settings_text = std::string(), bool aspect_given = true, bool play = true) : settings(temp_ini("ants_wide_hud")) {
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        if (!settings_text.empty()) {
            std::ofstream out(settings);
            out << settings_text;
        }
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.aspect = aspect;
        cfg.aspect_given = aspect_given;
        cfg.start_in_map_select = !match;
        cfg.settings_path = settings.string();
        if (window_w > 0) {
            cfg.has_window_size = true;
            cfg.window_w = window_w;
            cfg.window_h = window_h;
        }
        QuietStdout quiet;
        ok = app.init(cfg);
        if (ok && match && play) ok = app.start_game("Original-Ants/Maps/SMALL.LVL");
        if (ok && match && play) app.hud().update(app.sim().get_world_state(), 100);        // (the start dialog is gone)
        window = find_window();
    }
    ~AppFixture() {
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    }
    AppFixture(const AppFixture&) = delete;
    AppFixture& operator=(const AppFixture&) = delete;

    void motion_at_canvas(float cx, float cy) {
        int wx = 0, wy = 0;
        SDL_RenderLogicalToWindow(app.renderer().get_sdl_renderer(), cx, cy, &wx, &wy);
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = wx;
        e.motion.y = wy;
        SDL_PushEvent(&e);
    }
    void deliver() { app.run_frame_with_delta(0.001f); }
    std::vector<uint8_t> canvas_pixels() {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(app.renderer().get_sdl_renderer(), &w, &h);
        std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
        SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
        return px;
    }

    std::filesystem::path settings;
    Application app;
    SDL_Window* window{nullptr};
    bool ok{false};
};

std::array<uint8_t, 3> pixel(const std::vector<uint8_t>& px, int32_t width, int32_t x, int32_t y) {
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    return {px[i], px[i + 1], px[i + 2]};
}

void test_picture_per_screen() {
    group("screens", "a match and every screen outside it (the setup screen, the loading screen, the quick help, the results, the start menu) are the whole 960 x 540 canvas; the pointer stays where it is");
    const LayoutRect whole{0, 0, 960, 540};
    const LayoutRect page{160, 30, 640, 480};      // (a match of the original's own layout in this canvas is centred: the only picture that is not the whole canvas)
    {
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        check(f.ok, "the 16:9 application starts on its setup screen");
        if (!f.ok) return;
        check(f.app.aspect() == Aspect::Wide16x9 && f.app.canvas() == CanvasLayout{960, 540}, "the canvas is 960 x 540");
        check(f.app.layout() == ScreenLayout::with_size(960, 540) && f.app.hud().layout() == f.app.layout() && f.app.renderer().layout() == f.app.layout(), "the application, the HUD and the renderer hold the wide layout (the match screen's) already on the setup screen");
        check_rect(f.app.picture(), whole, "the setup screen has its own wide version: the whole canvas (so has every other screen: test_wide_pages; the wide setup screen is pinned in test_wide_setup)");
        check_rect(f.app.renderer().picture(), whole, "... and the renderer draws it there");
        // the pointer: the match starts with it in the middle of its picture (the original puts it there until it is seen moving)
        f.app.note_pointer(100, 90);
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match starts");
        check_rect(f.app.picture(), whole, "the match is the whole canvas");
        check_rect(f.app.renderer().picture(), whole, "... in the renderer too");
        check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270 && !f.app.mouse_has_moved(), "the pointer starts in the middle of the match's picture: (480, 270)");
        check(f.app.state() == AppState::Playing, "the application plays");
        // the results are the whole canvas too (the wide results page, results_layout.hpp): the picture does not change, so the pointer does not move (it used to move with the corner of a centred page)
        f.app.hud().update(f.app.sim().get_world_state(), 100);
        f.app.note_pointer(300, 200);
        sim::MatchResult result;
        result.is_over = true;
        result.ally = {255, 255, 255, 255};
        result.decide_winners();
        f.app.scorecard().show(result, 0);
        f.app.update_results(0.01f);
        check(f.app.scorecard().is_open(), "the results are open");
        check_rect(f.app.picture(), whole, "the results screen is the whole canvas (its wide page)");
        check(f.app.scorecard().wide_layout(), "... and the results screen is told that it is the wide page");
        check(f.app.mouse_screen_x() == 300 && f.app.mouse_screen_y() == 200, "the pointer stays: the match's (300, 200) is the results' (300, 200)");
        // back to the setup screen: the whole canvas, the pointer stays where it is on the canvas
        f.app.return_to_map_select();
        check_rect(f.app.picture(), whole, "back on the setup screen: the whole canvas");
        check(f.app.mouse_screen_x() == 300 && f.app.mouse_screen_y() == 200 && !f.app.scorecard().is_open(), "the pointer stays");
        // the way back from a match that is still running (not from its results): the setup screen is the whole canvas at once
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a second match starts");
        check_rect(f.app.picture(), whole, "the second match is the whole canvas");
        f.app.return_to_map_select();
        check_rect(f.app.picture(), whole, "back on the setup screen from a running match: the whole canvas, at once");
        // and the loading screen / quick help: the whole canvas too
        f.app.finish_loading();
        check(f.app.state() == AppState::QuickHelp || f.app.state() == AppState::MapSelect, "the loading screen ends in the quick help or the setup screen");
        check_rect(f.app.picture(), whole, "the quick help (or the setup screen) is the whole canvas");
    }
    {   // the original's own aspect: one picture, the whole canvas, in every screen
        AppFixture f(Aspect::Classic4x3, false, 640, 480);
        check(f.ok, "the 4:3 application starts");
        if (!f.ok) return;
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "4:3: the setup screen is the whole canvas");
        check(f.app.layout().is_classic(), "4:3: the layout is the original's");
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "4:3: a match starts");
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "4:3: the match is the whole canvas");
        sim::MatchResult result;
        result.is_over = true;
        result.ally = {255, 255, 255, 255};
        result.decide_winners();
        f.app.scorecard().show(result, 0);
        f.app.update_results(0.01f);
        check(f.app.scorecard().is_open(), "4:3: the results are open");
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "4:3: the results are the whole canvas");
    }
    {   // the pointer anywhere on the canvas when the results open: the results are the whole canvas, so every canvas point is a point of the picture and nothing is held at a page's edge (the
        // clamp to a centred page's nearest edge pixel, which this block used to pin, is only for a picture that is smaller than the canvas: the match of a classic layout, below)
        AppFixture f(Aspect::Wide16x9, true, 960, 540);
        check(f.ok, "the 16:9 application runs a match (the pointer where the results open)");
        if (f.ok) {
            struct Case { int32_t x, y; const char* what; };
            const Case cases[] = {{100, 270, "left (100, 270)"}, {900, 270, "right (900, 270)"}, {480, 10, "top (480, 10)"}, {480, 535, "bottom (480, 535)"}, {5, 5, "the corner (5, 5)"}, {955, 535, "the corner (955, 535)"}, {300, 200, "inside (300, 200)"}};
            for (const Case& c : cases) {
                AppFixture g(Aspect::Wide16x9, true, 960, 540);
                g.app.note_pointer(c.x, c.y);
                sim::MatchResult result;
                result.is_over = true;
                result.ally = {255, 255, 255, 255};
                result.decide_winners();
                g.app.scorecard().show(result, 0);
                g.app.update_results(0.01f);
                check(g.app.mouse_screen_x() == c.x && g.app.mouse_screen_y() == c.y && g.app.picture() == whole, std::string("the results screen, the pointer ") + c.what + " stays: (" + std::to_string(g.app.mouse_screen_x()) + ", " + std::to_string(g.app.mouse_screen_y()) + ")");
            }
            // the way back from a match to the setup screen: the whole canvas too, so the pointer stays exactly where it is
            f.app.note_pointer(100, 270);
            f.app.return_to_map_select();
            check(f.app.mouse_screen_x() == 100 && f.app.mouse_screen_y() == 270, "the setup screen (the whole canvas), from a running match: the pointer stays where it is");
            check(f.app.start_game("Original-Ants/Maps/SMALL.LVL") && f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270, "a match starts: the pointer is in the middle of the picture, as always");
            // a picture that is smaller than the canvas (a match of the original's layout, the test hook set_layout) still holds a pointer that was beside it at its nearest edge pixel
            f.app.note_pointer(100, 270);
            f.app.set_layout(ScreenLayout::classic());
            check_rect(f.app.picture(), page, "set_layout(classic): the 640 x 480 picture, centred");
            check(f.app.mouse_screen_x() == 0 && f.app.mouse_screen_y() == 240, "... a pointer left of it is held at its left edge, level with the pointer (the page's rule, for a picture smaller than the canvas)");
            f.app.set_layout(ScreenLayout::with_size(960, 540));
        }
    }
    {   // a match that starts at once (--map): the match screen from the first frame, the pointer in the middle of it
        AppFixture f(Aspect::Wide16x9, true, 960, 540, std::string(), true, false);
        check(f.ok && f.app.state() == AppState::Playing, "a 16:9 application that starts in its match");
        if (f.ok) {
            check_rect(f.app.picture(), whole, "16:9, started in a match (no start_game): the picture is the whole canvas");
            check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270, "... and the pointer is in the middle of it: (480, 270)");
            // a layout that is set is a picture of its own size at once (the match's picture is the layout's, centred in the canvas)
            f.app.set_layout(ScreenLayout::classic());
            check_rect(f.app.picture(), page, "set_layout(classic) in a match of a 16:9 canvas: the picture is the 640 x 480 one, centred, at once");
            f.app.set_layout(ScreenLayout::with_size(960, 540));
            check_rect(f.app.picture(), whole, "set_layout(960 x 540): the whole canvas again, at once");
        }
    }
    {   // the pointer of a game that opens on a screen starts in the middle of the picture
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        check(f.ok && f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270, "a 16:9 application on its setup screen: the pointer starts in the middle of the picture, (480, 270)");
    }
}

void test_small_levels_in_the_application() {
    group("small-levels", "whole application frames of matches on small levels cut from TINY.LVL: the black around the map stays black");
    // the review's 15 x 15 level (columns and rows 6 - 23 of TINY.LVL without 12 - 14: a gummy worm hung 65 px over its edge) and an 18 x 31 one that is only narrow: a whole application frame
    // of a match on each, the black around the map is black. (The HUD is drawn over the frame's edge, so only the part of the view that the map does not cover is looked at.)
    for (int variant = 0; variant < 2; ++variant) {
        std::vector<int32_t> cols, rows;
        for (int32_t i = 6; i <= 23; ++i) {
            if (variant == 0 && i >= 12 && i <= 14) continue;
            cols.push_back(i);
        }
        if (variant == 0) rows = cols;
        else for (int32_t i = 0; i <= 30; ++i) rows.push_back(i);
        const std::filesystem::path level = std::filesystem::temp_directory_path() / ("ants_wide_small_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(variant) + ".lvl");
        const std::string name = std::to_string(cols.size()) + " x " + std::to_string(rows.size()) + " level";
        check(write_small_level(level.string(), cols, rows), "the " + name + " is written from TINY.LVL");
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); continue; }
        check(f.app.start_game(level.string()), "a match on the " + name + " starts");
        f.app.hud().update(f.app.sim().get_world_state(), 100);                                 // (the start dialog is gone)
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        const LayoutRect view = f.app.layout().view();
        const int32_t map_w = static_cast<int32_t>(cols.size()) * 32, map_h = static_cast<int32_t>(rows.size()) * 32;
        const LayoutRect map = f.app.renderer().map_view_rect(static_cast<uint32_t>(cols.size()), static_cast<uint32_t>(rows.size()));
        check(map.w == std::min(map_w, view.w) && map.h == std::min(map_h, view.h), "the " + name + ": the map covers " + std::to_string(map.w) + " x " + std::to_string(map.h) + " of the view");
        int64_t stray = 0, covered_dark = 0;
        for (int32_t y = view.y; y < view.bottom(); ++y) {
            for (int32_t x = view.x; x < view.right(); ++x) {
                const bool black = pixel(px, 960, x, y) == std::array<uint8_t, 3>{0, 0, 0};
                if (!map.contains(x, y) && !black) ++stray;
                if (map.contains(x, y) && black) ++covered_dark;
            }
        }
        check(stray == 0, "the " + name + ": the black around the map stays black (" + std::to_string(stray) + " pixels drawn on it)");
        check(covered_dark < map.w * map.h / 4, "the " + name + ": the map itself is drawn");
        std::error_code ignore;
        std::filesystem::remove(level, ignore);
    }
}

void test_margin_and_pages_in_match() {
    group("margin", "no screen of this canvas is a page over a margin of clay: the quick help is the whole canvas (the wide frame at its edge, flat clay beside the page); in a match the options window and the quick help sit over the map view and the frame is not clay");
    const std::array<uint8_t, 3> clay{219, 75, 19};
    {   // the quick help at the start: its own wide page (page_layout.hpp), no margin of clay around a page any more (this block used to pin the clay margin around the centred page)
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); return; }
        f.app.finish_loading();
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        check(f.app.state() == AppState::QuickHelp && f.app.picture() == (LayoutRect{0, 0, 960, 540}), "the quick help is the whole canvas");
        bool frame = true;
        for (const auto& p : {std::pair<int, int>{2, 2}, {957, 2}, {2, 537}, {957, 537}, {480, 2}, {2, 270}, {957, 270}, {600, 537}}) frame = frame && pixel(px, 960, p.first, p.second) != clay;
        check(frame, "the quick help: the wide frame is at the canvas's corners and edges (no margin of clay around a page)");
        bool flat = true;
        for (const auto& p : {std::pair<int, int>{100, 270}, {60, 100}, {880, 100}, {880, 300}, {300, 25}, {600, 520}}) flat = flat && pixel(px, 960, p.first, p.second) == clay;
        check(flat, "... the clay beside and around the page is flat (219, 75, 19), left, right, above and below it");
        check(pixel(px, 960, 200, 130) != clay && pixel(px, 960, 600, 300) != clay, "... and the page's own art (the two columns, centred) is on it");
    }
    {   // the setup screen of this canvas is its wide version: no clay margin around a page, the frame of the screen is at the canvas's edge
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); return; }
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        check(f.app.picture() == (LayoutRect{0, 0, 960, 540}) && pixel(px, 960, 2, 2) != clay && pixel(px, 960, 957, 537) != clay, "the setup screen: the wide screen's own frame is at the canvas's corners (no margin of clay around a page)");
    }
    {
        AppFixture f(Aspect::Wide16x9, true, 960, 540);
        if (!f.ok) { check(false, "the application starts a match"); return; }
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        std::vector<uint8_t> px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 400, 5) != clay && pixel(px, 960, 900, 300) != clay, "the match: the frame fills the canvas, no clay at the corners and the sides");
        // the options window of the original over the match: its card is centred over the map view and the HUD is around it (no clay margin)
        f.app.hud().open_options();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 400, 5) != clay && pixel(px, 960, 940, 300) != clay && pixel(px, 960, 5, 535) != clay && pixel(px, 960, 400, 535) != clay,
              "the options window over a match: the frame is still there around it (the top bar, the left strip, the right panel, the bottom strip)");
        check(pixel(px, 960, 186, 70) == clay && pixel(px, 960, 608, 70) == clay && pixel(px, 960, 190, 70) == clay && pixel(px, 960, 604, 70) == clay,
              "... the card's orange is at the same distance from the view's edges on both sides: (186, 70) and (608, 70), (190, 70) and (604, 70) (the view's middle is x = 397)");
        check(pixel(px, 960, 30, 300) != clay && pixel(px, 960, 30, 100) != clay && pixel(px, 960, 140, 100) != clay, "... and the map shows left of it (the window is no margin)");
        // the quick help
        f.app.hud().close_options();
        f.app.hud().open_quick_help();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 940, 300) != clay && pixel(px, 960, 400, 535) != clay, "the quick help over a match: the frame is around it too");
        f.app.hud().close_quick_help();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay, "closed again: the match's frame is back");
        // the dialog is over the match, not over a clay margin
        f.app.hud().open_quit_dialog();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 900, 300) != clay, "the quit dialog: the match is around it, no margin");
    }
}

/// A match with the options window shut and then open, drawn by the real application with the HUD's clock and the animations held (the caret and the pedestals read the one, the ants the other)
struct OptionsPair {
    std::vector<uint8_t> shut, open;
    int32_t width{0}, height{0};
    LayoutPoint pointer;                                    // where the cursor is drawn (in the canvas)
    LayoutRect options_button;                              // the top bar's Options button, which shows its pressed art while the window is open
    bool ok{false};
};

OptionsPair options_pair(Aspect aspect, int32_t width, int32_t height) {
    OptionsPair r;
    AppFixture f(aspect, true, width, height);
    if (!f.ok) return r;
    g_clock = 0;
    f.app.hud().set_ticks_function(&clock_fn);
    f.app.renderer().pin_animation_clock(1500);
    f.app.render_frame();
    r.shut = f.canvas_pixels();
    f.app.hud().open_options();
    f.app.render_frame();
    r.open = f.canvas_pixels();
    r.width = width;
    r.height = height;
    r.pointer = LayoutPoint{f.app.mouse_screen_x() + f.app.picture().x, f.app.mouse_screen_y() + f.app.picture().y};
    const UIButton& button = f.app.hud().options_button();
    r.options_button = LayoutRect{button.x, button.y, button.w, button.h};
    r.ok = true;
    return r;
}

/// What the options window does outside its card: every second pixel is the dither's own colour (the pixels where x + y of the window's own numbers is odd), every other pixel is what it was
/// with the window shut; returns the number of pixels that break that rule. Not looked at: the cursor's area, the corner's plate of the frame rate, the Options button (pressed art while the
/// window is open), the card's last column (the art of its right edge, with the dither over every second pixel of it) and, in the original's own 640 x 480 picture, the last column of its
/// bottom right strip (the original's pieces there are 100 wide at x = 459 and 539, so column 639 is the one that its own dim leaves out below y = 400)
int64_t dim_breaks(const OptionsPair& p, const LayoutPoint& offset, const LayoutRect& card, const std::array<uint8_t, 3>& dither, int64_t* dimmed, int64_t* kept) {
    int64_t breaks = 0;
    *dimmed = 0;
    *kept = 0;
    for (int32_t y = 0; y < p.height; ++y) {
        for (int32_t x = 0; x < p.width; ++x) {
            if (card.contains(x, y)) continue;
            if (std::abs(x - p.pointer.x) < 40 && std::abs(y - p.pointer.y) < 40) continue;
            if (x >= p.width - 150 && y >= p.height - 30) continue;
            if (x >= p.options_button.x - 4 && x < p.options_button.x + p.options_button.w + 4 && y >= p.options_button.y - 4 && y < p.options_button.y + p.options_button.h + 4) continue;
            if (p.width == 640 && x == 639 && y >= 400) continue;
            const bool odd = (((x - offset.x) + (y - offset.y)) & 1) != 0;
            const std::array<uint8_t, 3> now = pixel(p.open, p.width, x, y);
            const std::array<uint8_t, 3> was = pixel(p.shut, p.width, x, y);
            if (odd) { ++*dimmed; if (now != dither) ++breaks; }
            else if (x == card.x + card.w && y >= card.y && y < card.y + card.h) continue;
            else { ++*kept; if (now != was) ++breaks; }
        }
    }
    return breaks;
}

void test_options_dim(const assets::AssetArchive& arc) {
    group("options-dim", "the options window dims everything of the picture outside its card in the original's checker: the classic picture is the oracle for what is dimmed, the 16:9 picture dims the same around the card in the middle of its map view");
    const std::array<uint8_t, 4> d4 = rgb_of(arc, "dith200.bmp", 175);
    const std::array<uint8_t, 3> dither{d4[0], d4[1], d4[2]};
    {   // the original's own picture: the dim is the whole picture except the 442 x 440 card at (17, 20): the ring around it and the whole panel (this is what the 16:9 picture repeats)
        const OptionsPair p = options_pair(Aspect::Classic4x3, 640, 480);
        check(p.ok, "the classic application runs a match with the options window");
        if (p.ok) {
            const ScreenLayout classic = ScreenLayout::classic();
            check(classic.options_card() == (LayoutRect{17, 20, 442, 440}), "classic: the card is 442 x 440 at (17, 20)");
            int64_t dimmed = 0, kept = 0;
            const int64_t breaks = dim_breaks(p, LayoutPoint{}, classic.options_card(), dither, &dimmed, &kept);
            check(breaks == 0 && dimmed > 50000 && kept > 50000, "classic: outside the card every pixel with x + y odd is the dither's colour and every other is the shut picture's (" + std::to_string(breaks) + " breaks in " + std::to_string(dimmed + kept) + " pixels)");
        }
    }
    {   // 16:9: the card is centred in the map view (offset (160, 30)), the picture around it is dimmed the same way: the map left and right of it, above and below it, the frame, the panel
        const OptionsPair p = options_pair(Aspect::Wide16x9, 960, 540);
        check(p.ok, "the 16:9 application runs a match with the options window");
        if (p.ok) {
            const ScreenLayout wide = ScreenLayout::with_size(960, 540);
            check(wide.options_card() == (LayoutRect{177, 50, 442, 440}), "16:9: the card is 442 x 440 at (177, 50), in the middle of the map view");
            int64_t dimmed = 0, kept = 0;
            const int64_t breaks = dim_breaks(p, wide.options_offset(), wide.options_card(), dither, &dimmed, &kept);
            check(breaks == 0 && dimmed > 150000 && kept > 150000, "16:9: outside the card every pixel with x + y odd is the dither's colour and every other is the shut picture's (" + std::to_string(breaks) + " breaks in " + std::to_string(dimmed + kept) + " pixels)");
            // the places that were not dimmed before the fix: the map left of the card (x 16 - 176), and the panel
            check(pixel(p.open, 960, 100, 201) == dither && pixel(p.open, 960, 100, 200) == pixel(p.shut, 960, 100, 200), "the map left of the card is dimmed (141 pixels of the view were not)");
            check(pixel(p.open, 960, 880, 301) == dither && pixel(p.open, 960, 20, 31) == dither, "the right panel and the frame's left edge are dimmed as in the original's picture");
        }
    }
    {   // what the HUD draws: the original's own picture needs no dim of its own (the window's pieces do it), a bigger one dims four disjoint bands that are the picture minus the card,
        // each band's tiles under its own clip, before the window and with the origin at the picture's
        const ScreenLayout layouts[] = {ScreenLayout::classic(), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1280, 720)};
        for (const ScreenLayout& layout : layouts) {
            HudRig rig(arc, layout);
            rig.hud.open_options();
            const Spy spy = rig.frame();
            const std::string at = std::to_string(layout.width) + " x " + std::to_string(layout.height) + ": ";
            std::vector<LayoutRect> bands;
            size_t tiles = 0, outside = 0;
            for (size_t i = 0; i < spy.events.size(); ++i) {
                if (spy.events[i].kind != Spy::Kind::Clip || spy.events[i].ox != 0 || spy.events[i].oy != 0) continue;
                const LayoutRect band{spy.events[i].x, spy.events[i].y, spy.events[i].w, spy.events[i].h};
                size_t n = 0;
                for (size_t j = i + 1; j < spy.events.size() && spy.events[j].kind != Spy::Kind::ClearClip; ++j) {
                    if (spy.events[j].kind == Spy::Kind::Named && spy.events[j].name == "dith200.bmp") {
                        ++n;
                        if (spy.events[j].x >= band.x + band.w || spy.events[j].x + 200 <= band.x || spy.events[j].y >= band.y + band.h || spy.events[j].y + 200 <= band.y) ++outside;
                    }
                }
                if (n > 0) { bands.push_back(band); tiles += n; }
            }
            if (layout.is_classic()) {
                check(bands.empty() && tiles == 0, at + "the original's own picture draws no dim of its own (the window's pieces are it)");
                continue;
            }
            const LayoutRect card = layout.options_card();
            int64_t area = 0;
            bool disjoint = true, clear_of_card = true;
            for (size_t i = 0; i < bands.size(); ++i) {
                area += static_cast<int64_t>(bands[i].w) * bands[i].h;
                const LayoutRect& b = bands[i];
                if (b.x < card.x + card.w && b.x + b.w > card.x && b.y < card.y + card.h && b.y + b.h > card.y) clear_of_card = false;
                for (size_t j = i + 1; j < bands.size(); ++j) {
                    const LayoutRect& c = bands[j];
                    if (b.x < c.x + c.w && b.x + b.w > c.x && b.y < c.y + c.h && b.y + b.h > c.y) disjoint = false;
                }
            }
            check(bands.size() == 4 && disjoint && clear_of_card, at + "four bands, disjoint, none over the card");
            check(area == static_cast<int64_t>(layout.width) * layout.height - static_cast<int64_t>(card.w) * card.h, at + "the bands are the whole picture minus the card (" + std::to_string(area) + " pixels)");
            check(tiles >= 4 && outside == 0, at + "every tile reaches into its band (" + std::to_string(tiles) + " tiles)");
        }
    }
    {   // another size (a layout that is not the 16:9 one): the dim follows the card's offset, its parity is the window's own
        const ScreenLayout other = ScreenLayout::with_size(961, 541);
        check(other.options_card().x == ScreenLayout::kOptionsCardX + other.options_offset().x && other.options_card().y == ScreenLayout::kOptionsCardY + other.options_offset().y, "the card is the window's own rectangle moved by the options offset at any size");
    }
}

void test_default_and_options() {
    group("default", "16:9 is the default of a desktop game (the command line and the settings give the classic picture), a config made by hand stays classic, the web build's default is 16:9 too");
    auto parse = [](std::initializer_list<const char*> args) {
        std::vector<std::string> storage(args.begin(), args.end());
        std::vector<char*> argv;
        for (std::string& s : storage) argv.push_back(s.data());
        return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
    };
    check(kWebDefaultAspect == Aspect::Wide16x9, "the web build's default is 16:9 (its page gives the game --aspect: 16:9, or 4:3 for ?aspect=4:3)");
    check(kDesktopDefaultAspect == Aspect::Wide16x9, "a desktop's default is 16:9");
    check(kPlatformDefaultAspect == (
#if defined(__EMSCRIPTEN__)
              kWebDefaultAspect
#else
              kDesktopDefaultAspect
#endif
              ), "this build takes its platform's default");
    ApplicationConfig c = parse({"ants"});
    check(c.aspect == kPlatformDefaultAspect && !c.aspect_given && c.startup_error.empty(), "the command line without the option gives the platform's default and does not say it was given");
    c = parse({"ants", "--aspect", "4:3"});
    check(c.aspect == Aspect::Classic4x3 && c.aspect_given, "--aspect 4:3 gives the classic picture");
    c = parse({"ants", "--aspect", "16:9"});
    check(c.aspect == Aspect::Wide16x9 && c.aspect_given, "--aspect 16:9 gives the wide one");
    c = parse({"ants", "--aspect", "3:2"});
    check(c.aspect == kPlatformDefaultAspect && !c.aspect_given && !c.startup_error.empty(), "a refused --aspect leaves the default and refuses the game");
    check(ApplicationConfig{}.aspect == Aspect::Classic4x3 && !ApplicationConfig{}.aspect_given, "a config that is made by hand (the tests') is the original's 4:3");
    // the application: nothing said, the command line's default, the settings key, the command line beats the settings
    auto aspect_of = [](const ApplicationConfig& cfg, const std::string& settings_text) {
        std::filesystem::path path = temp_ini("ants_wide_default");
        std::error_code ignore;
        std::filesystem::remove(path, ignore);
        if (!settings_text.empty()) {
            std::ofstream out(path);
            out << settings_text;
        }
        ApplicationConfig run = cfg;
        run.headless = true;
        run.start_in_map_select = true;
        run.settings_path = path.string();
        QuietStdout quiet;
        Application app;
        Aspect result = Aspect::Classic4x3;
        ScreenLayout layout;
        if (app.init(run)) {
            result = app.aspect();
            layout = app.layout();
        }
        app.shutdown();
        std::filesystem::remove(path, ignore);
        return std::make_pair(result, layout.width);
    };
    check(aspect_of(parse({"ants"}), "").first == kPlatformDefaultAspect && aspect_of(parse({"ants"}), "").second == (kPlatformDefaultAspect == Aspect::Wide16x9 ? 960 : 640), "a game started with no option runs in the platform's default aspect, with its layout");
    check(aspect_of(parse({"ants", "--aspect", "4:3"}), "").second == 640, "--aspect 4:3: the classic layout");
#if !defined(__EMSCRIPTEN__)
    check(aspect_of(parse({"ants"}), "aspect=4:3\n").first == Aspect::Classic4x3, "the settings' key aspect=4:3 beats the default");
    check(aspect_of(parse({"ants"}), "aspect=16:9\n").first == Aspect::Wide16x9, "aspect=16:9 in the settings: 16:9");
    check(aspect_of(parse({"ants", "--aspect", "16:9"}), "aspect=4:3\n").first == Aspect::Wide16x9, "--aspect beats the settings' key");
    check(aspect_of(parse({"ants"}), "aspect=3:2\n").first == kPlatformDefaultAspect, "a value that is no shape is ignored: the default");
    check(aspect_of(ApplicationConfig{}, "").first == Aspect::Classic4x3, "a config made by hand is 4:3 whatever the platform's default");
#endif
    // the four-window rig: a game that is started with --grid and no aspect (what start_game.sh does) takes its cell's largest rectangle of the default shape
    {
        ApplicationConfig cfg = parse({"ants", "--grid", "2x2", "--cell", "1"});
        std::filesystem::path path = temp_ini("ants_wide_grid");
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.settings_path = path.string();
        QuietStdout quiet;
        Application app;
        if (app.init(cfg)) {
            const WindowRect r = app.window_rect();
            const bool wide_default = kPlatformDefaultAspect == Aspect::Wide16x9;
            check(r.w > 0 && (wide_default ? std::abs(r.h * 16 - r.w * 9) <= 16 : std::abs(r.h * 4 - r.w * 3) <= 4), "a window of the grid (--grid 2x2 --cell 1) has the shape of the default aspect: " + std::to_string(r.w) + " x " + std::to_string(r.h));
        } else {
            check(false, "a game in a grid starts");
        }
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(path, ignore);
    }
}

void test_pointer_edges_in_match() {
    group("match-pointer", "in a 16:9 match the pointer is the picture's whole 960 x 540: the east strip at the right edge scrolls, a pointer beyond a bar of the window is held on the edge");
    AppFixture f(Aspect::Wide16x9, true, 1920, 1200);                  // a 16:10 window: bars above and below the picture (scale 2: 1920 x 1080 of 1200)
    check(f.ok && f.window != nullptr, "the 16:9 application runs a match in a 16:10 window");
    if (!f.ok || f.window == nullptr) return;
    check_rect(f.app.picture(), LayoutRect{0, 0, 960, 540}, "the picture is the whole canvas");
    f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height());
    const int32_t x0 = f.app.renderer().camera().world_x;
    f.motion_at_canvas(955.5f, 270.5f);
    f.deliver();
    check(f.app.mouse_screen_x() == 955 && f.app.mouse_screen_y() == 270, "a pointer at (955, 270) of the canvas is there");
    for (int i = 0; i < 12; ++i) f.app.handle_camera_panning(0.020f);
    check(f.app.renderer().camera().world_x > x0, "the east strip at the right edge of the picture scrolls the view");
    // over the bar above the picture (the window is 1200 high: 60 rows above and below): the nearest edge pixel, and the map scrolls north
    f.app.renderer().camera().center_on(600, 600, f.app.sim().grid().width(), f.app.sim().grid().height());
    const int32_t y0 = f.app.renderer().camera().world_y;
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = SDL_GetWindowID(f.window);
    e.motion.x = 960;
    e.motion.y = 10;                                                  // inside the black bar above the picture (the picture starts at y = 60)
    SDL_PushEvent(&e);
    f.deliver();
    check(f.app.mouse_screen_y() == 0 && f.app.mouse_screen_x() == 480 && !f.app.pointer_outside(), "the pointer over the top bar of a 16:10 window is the picture's top edge pixel, level with it");
    for (int i = 0; i < 12; ++i) f.app.handle_camera_panning(0.020f);
    check(f.app.renderer().camera().world_y < y0, "... and the map scrolls north");
}

// @@MORE_TESTS_4@@

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;                                     // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    ensure_sdl();
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    test_anchoring(arc);
    test_cuts(arc);
    test_composition(arc);
    test_renderer_draws_spans(arc);
    test_hud_draws_the_frame(arc);
    test_score_slots(arc);
    test_dialogs_and_pages(arc);
    test_window_controls_through_update(arc);
    test_latency_clear_of_scores(arc);
    test_camera_clamps();
    test_edge_strips();
    test_start_view_and_minimap(arc);
    test_small_map_pointer(arc);
    test_small_map_pointer_rows(arc);
    test_small_map_clip(arc);
    test_picture_per_screen();
    test_small_levels_in_the_application();
    test_margin_and_pages_in_match();
    test_options_dim(arc);
    test_default_and_options();
    test_pointer_edges_in_match();

    std::printf("\nwide hud: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
