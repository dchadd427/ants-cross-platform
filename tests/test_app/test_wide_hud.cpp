// The wide match screen (milestone M3 of the widescreen work): the 16:9 picture of 960 x 540 with its frame grown from the original's own art, and everything that follows from it.
//   * the anchoring model (include/ants_app/shell_layout.hpp): each of the 14 pieces of the original's frame animation `uishell` at 960 x 540 (and at other sizes): where it is, how big it
//     is, which of them are stretched, and that the spans of a piece tile it without a gap or an overlap;
//   * the cuts: for every stretched piece the repeated line lies inside a run of identical lines of the piece's own plain part (measured on the art of ants.chd: the top bar's columns
//     138 - 143, the bottom strip's 13 - 17 and never the panel's own fill 458 - 482, the left strip's rows 294 - 314) and, for the right panel, the three pieces that span the
//     owner's row (canvas y = 357) are plain there between their ant decorations; a cut where the art is not plain fails the same tests;
//   * the picture: the frame composed from the spans is, pixel for pixel, the owner's mock-up semantics (one line repeated) written out independently, at several sizes, and with the
//     original's picture the plain 14 pieces; the real renderer draws the spans exactly (draw_sprite_region, set_origin);
//   * the HUD: it draws the frame first, offsets the panel's animations by the layout, puts the pages of the original (quick help, options) centred over a clay margin and its dialogs
//     over the middle of the map view, and takes the pointer back to the numbers of those windows; the score boxes are slots (no 4 in the layout code), more of them fit the wide strip;
//   * the view: the camera clamps (a big map, a map smaller than the view: centred, black around), the edge strips of the whole picture, the start view, the minimap's frame, the cursor
//     outside a small map, the listener;
//   * the application: the picture of a match is the whole canvas, every other screen is the original's 640 x 480 page centred in it, the pointer follows the change; the default is 16:9
//     on a desktop (`--aspect 4:3` and the settings key give the classic picture).
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
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/shell_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

using namespace ants;
using namespace ants::app;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

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

/// The owner-approved mock-up of 2026-10-02 (a small tool that composes the frame from the original's pieces), written out here independently of the production header: the 14 pieces in the order the original draws them, each moved by the
/// right / bottom anchors and drawn with ONE of its columns or rows repeated dW or dH times (the line `col` / `row`)
struct RefPiece {
    const char* name;
    int32_t x, y;
    bool right, bottom;
    int32_t col, row;        // the repeated line (-1: none)
};
constexpr RefPiece kReference[14] = {
    {"x0y22.bmp", 0, 22, false, false, -1, 300},        // the left strip: taller below its horizontal rule
    {"x458y22.bmp", 458, 22, true, false, -1, -1},      // the top of the right panel
    {"x458y35.bmp", 458, 35, true, false, -1, 322},     // the strip between the map and the panel: taller at canvas row 357
    {"x480y126.bmp", 480, 126, true, false, -1, -1},
    {"x480y266.bmp", 480, 266, true, false, -1, -1},
    {"x480y400.bmp", 480, 400, true, true, -1, -1},
    {"x480y466.bmp", 480, 436, true, true, -1, -1},
    {"x521y254.bmp", 621, 254, true, false, -1, 103},   // the right edge strip: taller at canvas row 357
    {"x599y35.bmp", 599, 35, true, false, -1, -1},
    {"wchat.bmp", 479, 298, true, false, -1, 59},       // the chat log's box: taller at canvas row 357
    {"wtype.bmp", 479, 423, true, true, -1, -1},
    {"wstatus.bmp", 479, 253, true, false, -1, -1},
    {"x0y0.bmp", 0, 0, false, false, 140, -1},          // the top bar: wider at its plain green
    {"x17y461.bmp", 17, 461, false, true, 15, -1},      // the bottom strip: wider at its plain band, down by dH
};

Pic compose_reference(const assets::AssetArchive& arc, int32_t width, int32_t height) {
    Pic pic(width, height);
    const int32_t dw = width - 640;
    const int32_t dh = height - 480;
    for (const RefPiece& p : kReference) {
        const assets::Sprite* sprite = arc.find_sprite(p.name);
        if (sprite == nullptr) continue;
        const int32_t ox = p.right ? dw : 0;
        const int32_t oy = p.bottom ? dh : 0;
        const int32_t ew = p.col >= 0 ? dw : 0;
        const int32_t eh = p.row >= 0 ? dh : 0;
        for (int32_t y = 0; y < static_cast<int32_t>(sprite->height) + eh; ++y) {
            const int32_t sy = (eh > 0 && y > p.row) ? (y <= p.row + eh ? p.row : y - eh) : y;
            for (int32_t x = 0; x < static_cast<int32_t>(sprite->width) + ew; ++x) {
                const int32_t sx = (ew > 0 && x > p.col) ? (x <= p.col + ew ? p.col : x - ew) : x;
                const uint8_t v = sprite->get_pixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
                if (v != assets::CHD_COLOR_KEY_INDEX) pic.put(p.x + ox + x, p.y + oy + y, v);
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
        widened += kShellRules[k].cut_col >= 0 ? 1 : 0;
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
            const LayoutRect want{c.x + (kShellRules[k].right ? dx : 0), c.y + (kShellRules[k].bottom ? dy : 0), c.w + (kShellRules[k].cut_col >= 0 ? dx : 0),
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
        const int32_t c = rule("x0y0.bmp").cut_col;
        const auto run = identical_run(s, false, c);
        check(run.first == 138 && run.second == 143, "x0y0: the run of identical columns around the cut is 138 - 143 (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
        check(c >= run.first + 1 && c <= run.second - 1, "x0y0: the cut column " + std::to_string(c) + " has identical columns on both sides of it");
        check(c > 132, "x0y0: the cut is right of the black clock box (its edge is column 129 - 132)");
        check(line_diff(s, false, c - 1, c) == 0 && line_diff(s, false, c, c + 1) == 0, "x0y0: the neighbours of the cut continue it exactly");
    }
    {   // the bottom strip: the plain band left of the first score box, columns 13 - 17 (52.P correction); NOT the panel's own fill, columns 458 - 482
        const assets::Sprite& s = sprite("x17y461.bmp");
        const int32_t c = rule("x17y461.bmp").cut_col;
        const auto run = identical_run(s, false, c);
        check(run.first == 13 && run.second == 17, "x17y461: the run of identical columns around the cut is 13 - 17 (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
        check(c >= run.first + 1 && c <= run.second - 1, "x17y461: the cut column " + std::to_string(c) + " has identical columns on both sides");
        check(c < 86, "x17y461: the cut is left of the first score box's recess (its edge is column 86): the strip, its black top line and the recesses stay one continuous strip");
        const auto fill_run = identical_run(s, false, 470);
        check(fill_run.first >= 457 && fill_run.second >= 482, "x17y461: columns 458 - 482 are an identical run too (the control: identical alone is not enough)");
        check(!(c >= 458 && c <= 482), "x17y461: ... but that run is the right panel's own fill, and the cut is not in it (a stretch there puts a flat block between the end ornament and the panel)");
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
        // the row the owner rejected: the right edge strip's row 50 (canvas y 304) stretches an ant decoration into a long bar
        const assets::Sprite& edge = sprite("x521y254.bmp");
        const bool plain_neighbours = line_diff(edge, true, 49, 50) <= 5 && line_diff(edge, true, 50, 51) <= 5;
        check(!(plain_neighbours && distance_to_decoration(edge, true, 50, 10) >= 8), "the control: x521y254 row 50, which the owner rejected, fails the same tests (it is beside a decoration)");
    }
    // the cuts together: the model's anchors and cut lines are the ones the independent mock-up uses
    for (const RefPiece& p : kReference) {
        const ShellRule& r = rule(p.name);
        check(r.cut_col == p.col && r.cut_row == p.row && r.right == p.right && r.bottom == p.bottom, std::string(p.name) + ": the model's anchors and cut agree with the mock-up (" + std::to_string(r.cut_col) + ", " + std::to_string(r.cut_row) + ")");
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
        if (rule.cut_col < 0 && rule.cut_row < 0) continue;
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
        // at 960 x 540: lines before the cut are the original's, the added lines are the cut line, the lines after are the original's, moved
        const ScreenLayout wide = ScreenLayout::with_size(960, 540);
        const ShellSpans spans = shell_spans(&rule, c.x, c.y, c.w, c.h, wide);
        Pic two(960, 540);
        for (size_t i = 0; i < spans.count; ++i) blit_span(two, s, spans.span[i].dst, spans.span[i].src);
        const int32_t ox = rule.right ? wide.dx() : 0, oy = rule.bottom ? wide.dy() : 0;
        bool lines_ok = true;
        const int32_t extra = rule.cut_col >= 0 ? wide.dx() : wide.dy();
        const int32_t cut = rule.cut_col >= 0 ? rule.cut_col : rule.cut_row;
        const int32_t length = rule.cut_col >= 0 ? c.w : c.h;
        for (int32_t line = 0; line < length + extra; ++line) {
            const int32_t src_line = line <= cut ? line : (line <= cut + extra ? cut : line - extra);       // the mock-up's rule
            const int32_t across = rule.cut_col >= 0 ? c.h : c.w;
            for (int32_t i = 0; i < across; ++i) {
                const uint8_t v = rule.cut_col >= 0 ? s.get_pixel(static_cast<uint32_t>(src_line), static_cast<uint32_t>(i)) : s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(src_line));
                const int32_t px = rule.cut_col >= 0 ? c.x + ox + line : c.x + ox + i;
                const int32_t py = rule.cut_col >= 0 ? c.y + oy + i : c.y + oy + line;
                lines_ok = lines_ok && two.at(px, py) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
            }
        }
        check(lines_ok, std::string(rule.sprite) + " at 960 x 540: the lines before the cut, " + std::to_string(extra + 1) + " copies of the cut line and the lines after it, in order");
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
                const int32_t ox = p.right ? dw : 0, oy = p.bottom ? dh : 0, ew = p.col >= 0 ? dw : 0, eh = p.row >= 0 ? dh : 0;
                for (int32_t y = 0; y < static_cast<int32_t>(sprite->height) + eh; ++y) {
                    const int32_t sy = (eh > 0 && y > p.row) ? (y <= p.row + eh ? p.row : y - eh) : y;
                    for (int32_t x = 0; x < static_cast<int32_t>(sprite->width) + ew; ++x) {
                        const int32_t sx = (ew > 0 && x > p.col) ? (x <= p.col + ew ? p.col : x - ew) : x;
                        const uint8_t v = sprite->get_pixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
                        if (v == assets::CHD_COLOR_KEY_INDEX) continue;
                        const int32_t px_x = p.x + ox + x, px_y = p.y + oy + y;
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
        check(classic ? spy.count(Spy::Kind::Region) == 0 : spy.count(Spy::Kind::Region) == 18, at + (classic ? "no piece is drawn in parts" : "six pieces are drawn in three parts each (18 calls)"));
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
    group("slots", "the score boxes are slots: the local team's in the top bar, the others' in the bottom strip, in the order of the teams; more slots fit the wide strip");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    // the model
    check(classic.bottom_slot_count() == 3 && wide.bottom_slot_count() == 5, "the classic strip holds three bottom slots, the 960 wide one five");
    check(wide.score_slot(4) == ScoreSlot{186, 274, 524, 277} && wide.score_slot(5) == ScoreSlot{38, 126, 524, 129}, "slots 4 and 5 are one and two pitches (148) left of the first bottom slot (box 425): boxes 277 and 129, labels 88 wide");
    check(wide.score_slot(6) == wide.score_slot(5) && wide.score_slot(40) == wide.score_slot(5), "a slot past the last that fits is the last");
    check(classic.score_slot(4) == classic.score_slot(3) && classic.score_slot(9) == classic.score_slot(3), "the classic strip has no room left of the first slot: a slot past the third is the third");
    {
        bool ordered = true;
        for (size_t k = 4; k <= wide.bottom_slot_count(); ++k) {
            ordered = ordered && wide.score_slot(k).box_left < wide.score_slot(k - 1).box_left;
            ordered = ordered && wide.score_slot(k).label_left >= ScreenLayout::kBottomBandLeft && wide.score_slot(k).label_right == wide.score_slot(k).box_left - 3;
        }
        check(ordered, "every further slot is left of the one before and its label begins right of the band's start (35)");
        const ScreenLayout huge = ScreenLayout::with_size(1920, 1080);
        // independent count: boxes at 105 + 1280 - 148 * n, a label of 88 + 3 before each: the first n with box - 91 < 35 is the end
        size_t expected = 3;
        for (int32_t n = 1; (105 + 1280) - 148 * n - 91 >= 35; ++n) expected = 3 + static_cast<size_t>(n);
        check(huge.bottom_slot_count() == expected && expected == 11, "1920 x 1080 has " + std::to_string(expected) + " bottom slots (an independent count)");
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
    group("windows", "the dialogs sit over the middle of the map view, the pages of the original are centred over a clay margin, the pointer is taken back to their numbers");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    check(wide.page_offset() == LayoutPoint{160, 30} && classic.page_offset() == LayoutPoint{0, 0}, "the page offset is (160, 30) at 960 x 540 and (0, 0) in the original's picture");
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

    // --- the pages: the options and the quick help of the original are 640 x 480 screens
    for (const ScreenLayout& layout : {classic, wide}) {
        const LayoutPoint p = layout.page_offset();
        const bool is_classic = layout.is_classic();
        const std::string at = std::string(is_classic ? "classic" : "960 x 540") + ": ";
        {   // the options: Return to Game is (351, 425) 98 x 26 of the page
            HudRig rig(arc, layout);
            rig.hud.open_options();
            const Spy spy = rig.frame();
            if (is_classic) {
                bool margin = false, clip = false;
                for (const Spy::Ev& e : spy.events) {
                    margin = margin || (e.kind == Spy::Kind::Fill && e.colour.r == 219 && e.colour.g == 75 && e.colour.b == 19);
                    clip = clip || (e.kind == Spy::Kind::Clip && e.w == 640 && e.h == 480);
                }
                check(!margin && !clip, at + "the options page covers the whole picture: no margin, no clip");
            } else {
                // order: the margin fill (the whole picture, clay), the origin of the page, the clip of its 640 x 480 screen, the page, the clip off, the origin off
                size_t i_fill = spy.events.size(), i_origin = spy.events.size(), i_clip = spy.events.size(), i_unclip = spy.events.size(), i_reset = spy.events.size();
                for (size_t i = 0; i < spy.events.size(); ++i) {
                    const Spy::Ev& e = spy.events[i];
                    if (e.kind == Spy::Kind::Fill && e.w == 960 && e.h == 540 && e.colour.r == 219 && e.colour.g == 75 && e.colour.b == 19 && i_fill == spy.events.size()) i_fill = i;
                    if (e.kind == Spy::Kind::Origin && e.x == 160 && e.y == 30) i_origin = i;
                    if (e.kind == Spy::Kind::Clip && e.x == 0 && e.y == 0 && e.w == 640 && e.h == 480 && e.ox == 160 && e.oy == 30) i_clip = i;
                    if (e.kind == Spy::Kind::ClearClip && i > i_clip && i_unclip == spy.events.size()) i_unclip = i;
                    if (e.kind == Spy::Kind::Origin && e.x == 0 && e.y == 0 && i > i_origin) i_reset = i;
                }
                check(i_fill < i_origin && i_origin < i_clip && i_clip < i_unclip && i_unclip <= i_reset && i_reset < spy.events.size(),
                      at + "the page: the clay margin, the origin (160, 30), the clip of its screen, the page, the clip off, the origin off");
                bool fill_fill = false;
                for (const Spy::Ev& e : spy.events) {
                    if (e.kind == Spy::Kind::Fill && e.ox == 0 && e.oy == 0 && e.w == 960 && e.h == 540) fill_fill = e.colour.r == 219 && e.colour.g == 75 && e.colour.b == 19;
                }
                check(fill_fill, at + "the margin is the clay of the pages: (219, 75, 19)");
            }
            // the hover picture of Return: the pointer on it at its place on screen
            rig.hud.handle_mouse_motion(351 + p.x + 10, 425 + p.y + 10, rig.sim, rig.cam);
            check(rig.frame().has_sprite_at("breturn2.bmp", 351, 425), at + "the options' Return button shows its hover picture when the pointer is on it at its place on screen");
            if (!is_classic) {
                rig.hud.handle_mouse_motion(351 + 10, 425 + 10, rig.sim, rig.cam);
                check(!rig.frame().has_sprite_at("breturn2.bmp", 351, 425), at + "... and not when it is at Return's own numbers");
            }
            // the pointer: Return at its place on screen closes the options; at the page's own numbers (without the move) it does not
            check(rig.hud.is_options_open(), at + "the options are open");
            if (!is_classic) {
                rig.click(351 + 10, 425 + 10);
                check(rig.hud.is_options_open(), at + "a click at Return's own numbers (351, 425) does nothing");
                rig.click(351 + p.x - 20, 425 + p.y + 10);
                check(rig.hud.is_options_open(), at + "a click left of Return does nothing");
            }
            rig.click(351 + p.x + 10, 425 + p.y + 10);
            check(!rig.hud.is_options_open(), at + "a click on Return at its place on screen (" + std::to_string(351 + p.x + 10) + ", " + std::to_string(425 + p.y + 10) + ") closes the options");
        }
        {   // the quick help: Return is (529, 437) 98 x 26
            HudRig rig(arc, layout);
            rig.hud.open_quick_help();
            const Spy spy = rig.frame();
            check(rig.hud.is_quick_help_open(), at + "the quick help is open");
            rig.hud.handle_mouse_motion(529 + p.x + 10, 437 + p.y + 10, rig.sim, rig.cam);
            check(rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "the quick help's Return button shows its hover picture when the pointer is on it at its place on screen");
            if (!is_classic) {
                rig.hud.handle_mouse_motion(529 + 10, 437 + 10, rig.sim, rig.cam);
                check(!rig.frame().has_sprite_at("breturn2.bmp", 529, 437), at + "... and not when it is at Return's own numbers");
            }
            if (!is_classic) {
                rig.click(529 + 10, 437 + 10);
                check(rig.hud.is_quick_help_open(), at + "a click at the Return button's own numbers does nothing");
                bool one_origin = false;
                for (const Spy::Ev& e : spy.events) one_origin = one_origin || (e.kind == Spy::Kind::Origin && e.x == 160 && e.y == 30);
                check(one_origin, at + "the quick help is drawn under the page origin (160, 30)");
            }
            rig.click(529 + p.x + 10, 437 + p.y + 10);
            check(!rig.hud.is_quick_help_open(), at + "a click on Return at its place on screen closes the quick help");
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
    group("small-map", "on a map smaller than the view the black around it is no ground: the plain pointer, a click there deselects and orders nothing; the map itself works");
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
    // a click on the black deselects and orders nothing; a click on the map orders
    rig.click(100, y);
    check(markers.empty() && rig.hud.get_selected_ant_ids().empty() && rig.hud.get_selected_ant_id() == 0, "a click on the black deselects the ant and orders nothing");
    rig.hud.select_ant(ant);
    rig.hud.update(world, 0);
    rig.click(300, y);
    check(markers.size() == 1 && markers[0].first == 300 - 16 - 125 && markers[0].second == y - 21, "a click on the map orders the world point under the pointer (159, 279)");
    // a rubber band over the black selects the ants of the map under it and does not fail
    rig.hud.clear_selection();
    rig.hud.handle_mouse_down(20, 40, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    rig.hud.handle_mouse_motion(700, 500, rig.sim, rig.cam);
    rig.hud.handle_mouse_up(700, 500, SDL_BUTTON_LEFT, rig.sim, rig.cam);
    check(rig.hud.get_selected_ant_id() == ant, "a rubber band from the black left of the map to the black right of it selects the own ant on the map");
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
    group("screens", "a match is the whole 960 x 540 canvas, every other screen is the original's 640 x 480 page centred in it; the pointer follows the change");
    const LayoutRect whole{0, 0, 960, 540};
    const LayoutRect page{160, 30, 640, 480};
    {
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        check(f.ok, "the 16:9 application starts on its setup screen");
        if (!f.ok) return;
        check(f.app.aspect() == Aspect::Wide16x9 && f.app.canvas() == CanvasLayout{960, 540}, "the canvas is 960 x 540");
        check(f.app.layout() == ScreenLayout::with_size(960, 540) && f.app.hud().layout() == f.app.layout() && f.app.renderer().layout() == f.app.layout(), "the application, the HUD and the renderer hold the wide layout (the match screen's) already on the setup screen");
        check_rect(f.app.picture(), page, "the setup screen is the original's page, centred");
        check_rect(f.app.renderer().picture(), page, "... and the renderer draws it there");
        // the pointer: the match starts with it in the middle of its picture (the original puts it there until it is seen moving)
        f.app.note_pointer(100, 90);
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match starts");
        check_rect(f.app.picture(), whole, "the match is the whole canvas");
        check_rect(f.app.renderer().picture(), whole, "... in the renderer too");
        check(f.app.mouse_screen_x() == 480 && f.app.mouse_screen_y() == 270 && !f.app.mouse_has_moved(), "the pointer starts in the middle of the match's picture: (480, 270)");
        check(f.app.state() == AppState::Playing, "the application plays");
        // the results are a page again: the pointer keeps its place on the canvas, so its numbers (the picture's own) move with the corner
        f.app.hud().update(f.app.sim().get_world_state(), 100);
        f.app.note_pointer(300, 200);
        sim::MatchResult result;
        result.is_over = true;
        result.ally = {255, 255, 255, 255};
        result.decide_winners();
        f.app.scorecard().show(result, 0);
        f.app.update_results(0.01f);
        check(f.app.scorecard().is_open(), "the results are open");
        check_rect(f.app.picture(), page, "the results screen is the original's page, centred");
        check(f.app.mouse_screen_x() == 300 - 160 && f.app.mouse_screen_y() == 200 - 30, "the pointer moved with the corner: the match's (300, 200) is the page's (140, 170)");
        // back to the setup screen: a page too, the pointer stays
        f.app.return_to_map_select();
        check_rect(f.app.picture(), page, "back on the setup screen: the page");
        check(f.app.mouse_screen_x() == 140 && f.app.mouse_screen_y() == 170 && !f.app.scorecard().is_open(), "the pointer stays");
        // the way back from a match that is still running (not from its results): the setup screen is a page at once
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a second match starts");
        check_rect(f.app.picture(), whole, "the second match is the whole canvas");
        f.app.return_to_map_select();
        check_rect(f.app.picture(), page, "back on the setup screen from a running match: the page, at once");
        // and the loading screen / quick help: pages too
        f.app.finish_loading();
        check(f.app.state() == AppState::QuickHelp || f.app.state() == AppState::MapSelect, "the loading screen ends in a page");
        check_rect(f.app.picture(), page, "the quick help (or the setup screen) is a page");
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
    {   // the pointer of a game that opens on a page starts in the middle of that page
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        check(f.ok && f.app.mouse_screen_x() == 320 && f.app.mouse_screen_y() == 240, "a 16:9 application on its setup screen: the pointer starts in the middle of the page, (320, 240)");
    }
}

void test_margin_and_pages_in_match() {
    group("margin", "the clay around the original's pages, in the screens and in a match; the match's frame is not clay");
    const std::array<uint8_t, 3> clay{219, 75, 19};
    {
        AppFixture f(Aspect::Wide16x9, false, 960, 540);
        if (!f.ok) { check(false, "the application starts"); return; }
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        const std::vector<uint8_t> px = f.canvas_pixels();
        bool margin = true;
        for (const auto& p : {std::pair<int, int>{0, 0}, {159, 0}, {80, 270}, {0, 539}, {800, 100}, {959, 100}, {400, 10}, {400, 29}, {400, 510}, {400, 520}}) margin = margin && pixel(px, 960, p.first, p.second) == clay;
        check(margin, "the setup screen: the margin around the page is clay (219, 75, 19), left, right, above and below it");
        check(pixel(px, 960, 160, 30) != clay || pixel(px, 960, 161, 31) != clay, "... and the page's own frame starts at (160, 30)");
    }
    {
        AppFixture f(Aspect::Wide16x9, true, 960, 540);
        if (!f.ok) { check(false, "the application starts a match"); return; }
        f.app.renderer().pin_animation_clock(1500);
        f.app.render_frame();
        std::vector<uint8_t> px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) != clay && pixel(px, 960, 400, 5) != clay && pixel(px, 960, 900, 300) != clay, "the match: the frame fills the canvas, no clay at the corners and the sides");
        // the options of the original over the match: a 640 x 480 page, centred, clay around it
        f.app.hud().open_options();
        f.app.render_frame();
        px = f.canvas_pixels();
        bool margin = true;
        for (const auto& p : {std::pair<int, int>{0, 0}, {159, 100}, {800, 100}, {400, 10}, {400, 29}, {400, 515}}) margin = margin && pixel(px, 960, p.first, p.second) == clay;
        check(margin, "the options of the original over a match: clay left, right, above and below the page");
        check(pixel(px, 960, 5, 5) == clay && pixel(px, 960, 940, 300) == clay, "... and the match's frame is gone behind it");
        // the quick help
        f.app.hud().close_options();
        f.app.hud().open_quick_help();
        f.app.render_frame();
        px = f.canvas_pixels();
        check(pixel(px, 960, 5, 5) == clay && pixel(px, 960, 940, 300) == clay, "the quick help over a match: clay around the page too");
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

void test_default_and_options() {
    group("default", "16:9 is the default of a desktop game (the command line and the settings give the classic picture), a config made by hand stays classic, the web build stays 4:3");
    auto parse = [](std::initializer_list<const char*> args) {
        std::vector<std::string> storage(args.begin(), args.end());
        std::vector<char*> argv;
        for (std::string& s : storage) argv.push_back(s.data());
        return Application::parse_arguments(static_cast<int>(argv.size()), argv.data());
    };
#if defined(__EMSCRIPTEN__)
    check(kPlatformDefaultAspect == Aspect::Classic4x3, "the web build's default is 4:3 until its page shows 16:9");
#else
    check(kPlatformDefaultAspect == Aspect::Wide16x9, "a desktop's default is 16:9");
#endif
    ApplicationConfig c = parse({"ants"});
    check(c.aspect == kPlatformDefaultAspect && !c.aspect_given && c.startup_error.empty(), "the command line without the option gives the platform's default and does not say it was given");
    c = parse({"ants", "--aspect", "4:3"});
    check(c.aspect == Aspect::Classic4x3 && c.aspect_given, "--aspect 4:3 gives the classic picture");
    c = parse({"ants", "--aspect", "16:9"});
    check(c.aspect == Aspect::Wide16x9 && c.aspect_given, "--aspect 16:9 gives the wide one");
    c = parse({"ants", "--aspect", "21:9"});
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
    check(aspect_of(parse({"ants"}), "aspect=21:9\n").first == kPlatformDefaultAspect, "a value that is neither is ignored: the default");
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

int main() {
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
    test_camera_clamps();
    test_edge_strips();
    test_start_view_and_minimap(arc);
    test_small_map_pointer(arc);
    test_picture_per_screen();
    test_margin_and_pages_in_match();
    test_default_and_options();
    test_pointer_edges_in_match();

    std::printf("\nwide hud: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
