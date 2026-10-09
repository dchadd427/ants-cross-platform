// Screen layout (milestone M1 of the widescreen work): the geometry of the picture as numbers (include/ants_app/screen_layout.hpp), and every place that used to hard-code the
// original's 640 x 480 screen reading it. Two halves:
//   * the model: `ScreenLayout::classic()` is the original's numbers, one by one (they are written out below, independently of the header: the map view (16, 21) - (458, 461),
//     the minimap (480, 35) 119 x 91, the chat log, the right panel, the four score slots, the eight edge strips of the scroll ...); a layout of another size (`with_size`) moves
//     exactly what is anchored to the right edge by dx = W - 640 and what is anchored to the bottom edge by dy = H - 480, and the map view takes the rest;
//   * the consumers: the HUD's rectangles and drawing, the edge scroll, the minimap, the start view, the pointer's limits, the corner of the frame-rate plate, the camera, the
//     renderer's view and the application (listener, pointer gate, the plate, the network overlay's box) all follow a layout that is not the classic one, consistently; and with
//     the classic one they give what they always gave (tests/test_app/test_view_fingerprint.cpp pins the picture and the pointer, this program pins the model and the wiring).
// Usage: test_screen_layout. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/fps_overlay.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/latency_corner.hpp"
#include "ants_app/net_overlay.hpp"
#include "ants_app/pointer_clamp.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/text_layout.hpp"
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

LayoutRect rect_of(const UIButton& b) { return LayoutRect{b.x, b.y, b.w, b.h}; }

/// SDL's video system on the dummy driver (an Application that ended has quit it)
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
// The model
// =====================================================================================================================================================

constexpr ScoreSlot kOriginalSlots[4] = {{312, 399, 4, 402}, {5, 101, 464, 105}, {163, 251, 464, 254}, {312, 399, 464, 402}};      // 0x1002218, 0x10021b8

void test_classic_numbers() {
    group("classic", "classic() is the original's screen: every number, one by one");
    const ScreenLayout c = ScreenLayout::classic();
    check(c.width == 640 && c.height == 480, "the screen is 640 x 480");
    check(c.dx() == 0 && c.dy() == 0, "nothing is shifted");
    check(c.is_classic() && c == ScreenLayout{} && c == ScreenLayout::with_size(640, 480), "the default layout is the classic one");
    check_rect(c.view(), LayoutRect{16, 21, 442, 440}, "the map view (Ants.exe 0x100a32b)");
    check(c.view().right() == 458 && c.view().bottom() == 461, "the view ends at (458, 461)");
    check_rect(c.minimap(), LayoutRect{480, 35, 119, 91}, "the minimap (FUN_01009596)");
    check(c.minimap().right() == 599 && c.minimap().bottom() == 126, "the minimap ends at (599, 126)");
    check_rect(c.chat_view(), LayoutRect{482, 299, 138, 101}, "the chat log's view (docs 5.56)");
    check_rect(c.panel_fill(), LayoutRect{480, 22, 160, 458}, "the right panel's backing fill");
    for (size_t k = 0; k < 4; ++k) check(c.score_slot(k) == kOriginalSlots[k], "score slot " + std::to_string(k) + " is the original's");
    check(c.score_slot(4) == kOriginalSlots[3] && c.score_slot(9) == kOriginalSlots[3], "a slot past the fourth is the fourth");
    check(c.right(476) == 476 && c.bottom(443) == 443, "anchored coordinates stay");

    // the constants that other headers keep for the classic picture are the same numbers
    check(PLAYFIELD_X == 16 && PLAYFIELD_Y == 21 && PLAYFIELD_W == 442 && PLAYFIELD_H == 440, "PLAYFIELD_ is the classic view");
    check(CANVAS_WIDTH == 640 && CANVAS_HEIGHT == 480 && kScreenW == 640 && kScreenH == 480 && kOrigViewW == 442 && kOrigViewH == 440, "the classic sizes of the canvas, the screen and the view");
    check(kScreenWidth == 640 && kScreenHeight == 480, "the pointer's classic limits");
    check(HUD::PLAYFIELD_X == 16 && HUD::PLAYFIELD_Y == 21 && HUD::MAP_LEFT == 16 && HUD::MAP_TOP == 21 && HUD::MAP_RIGHT == 458 && HUD::MAP_BOTTOM == 461, "the HUD's classic view");
    check(HUD::kChatViewX == 482 && HUD::kChatViewY == 299 && HUD::kChatViewW == 138 && HUD::kChatViewH == 101, "the HUD's classic chat view");
    bool zones_agree = true;
    for (int32_t y = 0; y < 480 && zones_agree; ++y) {
        for (int32_t x = 0; x < 640; ++x) {
            if (c.view().contains(x, y) != HUD::in_map_rect(x, y) || c.minimap().contains(x, y) != HUD::in_minimap_rect(x, y)) {
                zones_agree = false;
                break;
            }
        }
    }
    check(zones_agree, "the layout's view and minimap are the zones of HUD::in_map_rect and in_minimap_rect at every pixel");
    const CornerPlate corner = CornerPlate::classic();
    check(corner.right_edge == 632 && corner.spark_y == FPS_OVERLAY_SPARK_Y && corner.top == FPS_OVERLAY_TOP && corner.bottom == FPS_OVERLAY_BOTTOM, "the classic corner plate is the FPS_OVERLAY_ constants");
    check(FPS_OVERLAY_SPARK_Y == 468 && FPS_OVERLAY_TOP == 467 && FPS_OVERLAY_BOTTOM == 480 && corner.bottom == 480, "... that is rows 467 .. 479, the last row of the screen");
    check(LATENCY_RIGHT_EDGE == 632, "the latency texts end where the frame rate ends");
}

void test_sized_layouts() {
    group("sized", "a layout of another size moves the right-anchored parts by dx, the bottom-anchored ones by dy, and the view takes the rest");
    const ScreenLayout w = ScreenLayout::with_size(960, 540);
    check(w.width == 960 && w.height == 540 && w.dx() == 320 && w.dy() == 60 && !w.is_classic(), "960 x 540: dx = 320, dy = 60");
    check(w.right(476) == 796 && w.bottom(443) == 503, "right(x) = x + dx, bottom(y) = y + dy");
    check_rect(w.view(), LayoutRect{16, 21, 762, 500}, "the view keeps its corner and grows by dx and dy");
    check(w.view().right() == 778 && w.view().bottom() == 521, "... to (778, 521)");
    check_rect(w.minimap(), LayoutRect{800, 35, 119, 91}, "the minimap is right anchored");
    check_rect(w.chat_view(), LayoutRect{802, 299, 138, 161}, "the chat log is right anchored and takes the extra height");
    check_rect(w.panel_fill(), LayoutRect{800, 22, 160, 518}, "the panel's fill is right anchored and as tall as the screen below the top bar");
    check(w.panel_fill().right() == 960 && w.panel_fill().bottom() == 540, "... it ends at the screen's right and bottom edge");
    // (the review fixes of M3 spread the three bottom boxes over the strip, the owner's request: the strip is widened at three cuts, 108, 106 and 106 px at 960 wide, so the boxes are at
    // x 213, 468 and 722 and not together at the right end (425, 574 and 722); the top bar's slot stays right anchored; test_wide_hud pins the cuts and the art under the boxes)
    const ScoreSlot expected[4] = {{632, 719, 4, 722}, {113, 209, 524, 213}, {377, 465, 524, 468}, {632, 719, 524, 722}};
    for (size_t k = 0; k < 4; ++k) check(w.score_slot(k) == expected[k], "score slot " + std::to_string(k) + " of 960 x 540: the top bar's right anchored, the bottom strip's boxes bottom anchored and spread over the strip");
    // (M3 rewrote this line: a wide strip had room for further slots left of the first, five bottom slots at 960 wide; the spread has none: the strip holds the original's three at every size)
    check(w.bottom_slot_count() == 3 && w.score_slot(7) == w.score_slot(3) && w.score_slot(4) == w.score_slot(3), "a slot past the third is the third (three bottom slots at every width)");

    // a layout is never smaller than the original's screen
    check(ScreenLayout::with_size(600, 400) == ScreenLayout::classic() && ScreenLayout::with_size(639, 479).is_classic() && ScreenLayout::with_size(0, 0).is_classic(), "a smaller canvas gives the classic layout");
    check(ScreenLayout::with_size(1920, 1080).dx() == 1280 && ScreenLayout::with_size(1920, 1080).dy() == 600, "1920 x 1080: dx = 1280, dy = 600");
    check(ScreenLayout::with_size(800, 480).dx() == 160 && ScreenLayout::with_size(800, 480).dy() == 0, "a wider canvas of the same height only has dx");
    // the four shapes of the live aspect switch (Classic 4:3, 16:10, 16:9, 21:9): the map views of the picture that was approved
    struct Shape4 { int32_t w, h, view_w, view_h; };
    for (const Shape4& sh : {Shape4{640, 480, 442, 440}, Shape4{960, 600, 762, 560}, Shape4{960, 540, 762, 500}, Shape4{1260, 540, 1062, 500}}) {
        const ScreenLayout l = ScreenLayout::with_size(sh.w, sh.h);
        const std::string at = std::to_string(sh.w) + " x " + std::to_string(sh.h) + ": ";
        check(l.width == sh.w && l.height == sh.h && l.dx() == sh.w - 640 && l.dy() == sh.h - 480, at + "dx and dy are the canvas's difference from 640 x 480");
        check(l.view().x == 16 && l.view().y == 21 && l.view().w == sh.view_w && l.view().h == sh.view_h, at + "the map view is " + std::to_string(sh.view_w) + " x " + std::to_string(sh.view_h));
        check(l.is_classic() == (sh.w == 640), at + "only 640 x 480 is the classic layout");
    }
}

void test_anchoring_invariants() {
    group("anchors", "the same relations hold in every size: the panel meets the edges, the view meets the panel and the bottom strip, nothing leaves the screen");
    for (const ScreenLayout& l : {ScreenLayout::classic(), ScreenLayout::with_size(800, 600), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1280, 720),
                                  ScreenLayout::with_size(1920, 1080), ScreenLayout::with_size(2560, 1080), ScreenLayout::with_size(960, 600), ScreenLayout::with_size(1260, 540)}) {
        const std::string at = "at " + std::to_string(l.width) + " x " + std::to_string(l.height) + ": ";
        check(l.panel_fill().right() == l.width && l.panel_fill().bottom() == l.height, at + "the panel fill reaches the right and bottom edges");
        check(l.minimap().x - l.view().right() == 22 && l.view().x == 16 && l.view().y == 21, at + "the gap between the view and the minimap is the original's 22, the view's corner stays");
        check(l.view().bottom() == l.bottom(461), at + "the view ends where the bottom strip starts");
        check(l.chat_view().right() == l.width - 20 && l.chat_view().y == 299, at + "the chat log stands 20 px from the right edge");
        check(l.minimap().right() <= l.width && l.chat_view().bottom() < l.height, at + "the minimap and the chat log are on the screen");
        for (size_t k = 0; k < 4; ++k) {
            const ScoreSlot s = l.score_slot(k);
            check(s.box_left + 54 <= l.width && s.top + 14 <= l.height && s.label_left < s.label_right && s.label_right <= s.box_left, at + "score slot " + std::to_string(k) + " is on the screen, its label left of its box");
        }
        check(l.score_slot(0).top == 4 && l.score_slot(1).top == l.bottom(464), at + "the local slot is in the top bar, the others in the bottom strip");
    }
}

// =====================================================================================================================================================
// The original's edge strips and the scroll
// =====================================================================================================================================================

void test_original_strips() {
    group("strips", "the eight edge strips of the original (0x104b3a0, 0x104b3f0), written out here, are what the strips of a 640 x 480 screen are");
    struct R { int32_t x0, y0, x1, y1; };
    static const R kBand[8] = {{12, 0, 628, 12}, {628, 0, 640, 12}, {628, 12, 640, 468}, {628, 468, 640, 480}, {12, 468, 628, 480}, {0, 468, 12, 480}, {0, 12, 12, 468}, {0, 0, 12, 12}};
    static const R kInner[8] = {{5, 0, 635, 5}, {635, 0, 640, 5}, {635, 5, 640, 475}, {635, 475, 640, 480}, {5, 475, 635, 480}, {0, 475, 5, 480}, {0, 5, 5, 475}, {0, 0, 5, 5}};
    for (int32_t i = 0; i < 8; ++i) {
        const detail::Rect b = detail::band(i, 640, 480);
        const detail::Rect n = detail::inner(i, 640, 480);
        check(b.x0 == kBand[i].x0 && b.y0 == kBand[i].y0 && b.x1 == kBand[i].x1 && b.y1 == kBand[i].y1, "band " + std::to_string(i));
        check(n.x0 == kInner[i].x0 && n.y0 == kInner[i].y0 && n.x1 == kInner[i].x1 && n.y1 == kInner[i].y1, "inner strip " + std::to_string(i));
    }
    // the bands of any screen are disjoint, lie on its border ring, and the quiet area (13 px in) touches none of them
    for (const ScreenLayout& l : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540), ScreenLayout::with_size(1280, 720)}) {
        bool ok = true;
        int ring = 0;
        for (int32_t y = 0; y < l.height && ok; ++y) {
            for (int32_t x = 0; x < l.width; ++x) {
                int in_bands = 0;
                for (int32_t i = 0; i < 8; ++i) in_bands += detail::inside(detail::band(i, l.width, l.height), x, y) ? 1 : 0;
                const bool quiet = x >= 13 && x < l.width - 13 && y >= 13 && y < l.height - 13;
                const bool on_ring = x < 12 || y < 12 || x >= l.width - 12 || y >= l.height - 12;
                ring += in_bands;
                if (in_bands > 1 || (quiet && in_bands != 0) || (on_ring && in_bands != 1)) { ok = false; break; }
            }
        }
        check(ok, "the eight bands of " + std::to_string(l.width) + " x " + std::to_string(l.height) + " are disjoint and cover the 12 px ring, the quiet area is clear of them");
        check(ring == l.width * l.height - (l.width - 24) * (l.height - 24), "... and they hold exactly the ring's " + std::to_string(l.width * l.height - (l.width - 24) * (l.height - 24)) + " pixels");
    }
}

void test_edge_scroll_follows_layout() {
    group("scroll", "the edge scroll, the minimap and the start view follow the layout; with the classic one they give the original's steps");
    const ScreenLayout classic = ScreenLayout::classic();
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const int32_t mid_x = 600;                       // a view with every way open on a 60 x 60 map (the largest origin is 1920 - 442 = 1478 / 1920 - 440 = 1480 classic)
    const int32_t mid_y = 600;

    // the classic overload is the classic layout's: identical at every pixel of the screen and of a ring outside it
    bool same = true;
    for (int32_t y = -8; y < 488 && same; ++y) {
        for (int32_t x = -8; x < 648; ++x) {
            const EdgeScroll a = edge_scroll_step(x, y, 20, mid_x, mid_y, 60, 60);
            const EdgeScroll b = edge_scroll_step(x, y, 20, mid_x, mid_y, 60, 60, classic);
            if (a.dir != b.dir || a.dx != b.dx || a.dy != b.dy) {
                same = false;
                break;
            }
        }
    }
    check(same, "edge_scroll_step(...) is edge_scroll_step(..., classic) at every pixel");

    // the original's steps at a few pixels (the strips are the original's, the target point is the pointer scaled from the screen to the view)
    check(edge_scroll_step(639, 240, 0, mid_x, mid_y, 60, 60).dir == 2, "classic: the east strip at (639, 240)");
    check(edge_scroll_step(628, 240, 0, mid_x, mid_y, 60, 60).dir == 2 && edge_scroll_step(627, 240, 0, mid_x, mid_y, 60, 60).dir == -1, "classic: the east band begins at x = 628");
    check(edge_scroll_step(639, 240, 0, mid_x, mid_y, 60, 60).dx > 0, "classic: the pointer in the inner east strip scrolls east");
    check(edge_scroll_step(630, 240, 0, mid_x, mid_y, 60, 60).dx == 0, "classic: x = 630 is in the band (the arrow) but not in the 5 px strip that scrolls");
    // the step: the target point is the pointer scaled to the view (639 * 442 / 640 = 441), a square of 10 px each way (rate 0) around it, scrolled to just be in view
    check(edge_scroll_step(639, 240, 0, mid_x, mid_y, 60, 60).dx == 9 && edge_scroll_step(639, 240, 0, mid_x, mid_y, 60, 60).dy == 0, "classic: the pointer at (639, 240) scrolls the 442 x 440 view by (9, 0)");

    // the layout of 960 x 540: the east band is at x >= 948, the south band at y >= 528; the middle of the old screen is quiet
    check(edge_scroll_step(639, 240, 0, mid_x, mid_y, 60, 60, wide).dir == -1, "960 x 540: (639, 240), the classic east strip, is quiet");
    check(edge_scroll_step(959, 270, 0, mid_x, mid_y, 60, 60, wide).dir == 2 && edge_scroll_step(959, 270, 0, mid_x, mid_y, 60, 60, wide).dx > 0, "960 x 540: the east strip is at the right edge, (959, 270) scrolls east");
    check(edge_scroll_step(959, 270, 0, mid_x, mid_y, 60, 60, wide).dx == 9 && edge_scroll_step(959, 270, 0, mid_x, mid_y, 60, 60, wide).dy == 0, "960 x 540: ... by (9, 0): 959 * 762 / 960 = 761 is the target, the 762 px view is scrolled to just show it");
    check(edge_scroll_step(948, 270, 0, mid_x, mid_y, 60, 60, wide).dir == 2 && edge_scroll_step(947, 270, 0, mid_x, mid_y, 60, 60, wide).dir == -1, "960 x 540: the east band begins at x = 948");
    check(edge_scroll_step(480, 539, 0, mid_x, mid_y, 60, 60, wide).dir == 4 && edge_scroll_step(480, 539, 0, mid_x, mid_y, 60, 60, wide).dy > 0, "960 x 540: the south strip is at the bottom edge, (480, 539) scrolls south");
    check(edge_scroll_step(480, 479, 0, mid_x, mid_y, 60, 60, wide).dir == -1, "960 x 540: (480, 479), the classic south strip, is quiet");
    check(edge_scroll_step(959, 539, 0, mid_x, mid_y, 60, 60, wide).dir == 3, "960 x 540: the south east corner");
    check(edge_scroll_step(0, 0, 0, mid_x, mid_y, 60, 60, wide).dir == 7 && edge_scroll_step(0, 270, 0, mid_x, mid_y, 60, 60, wide).dir == 6, "960 x 540: the west and north west strips stay at the left and top");
    // the strips only count where the view can move: the view's limits follow the view's size (762 x 500: the largest origin of a 60 x 60 map is 1158 / 1420)
    check(edge_scroll_step(959, 270, 0, 1158, mid_y, 60, 60, wide).dir == -1, "960 x 540: at the largest origin (1920 - 762) the view cannot go east");
    check(edge_scroll_step(959, 270, 0, 1157, mid_y, 60, 60, wide).dir == 2, "... one pixel before it can");
    check(edge_scroll_step(480, 539, 0, mid_x, 1420, 60, 60, wide).dir == -1 && edge_scroll_step(480, 539, 0, mid_x, 1419, 60, 60, wide).dir == 4, "960 x 540: the largest y is 1920 - 500 = 1420");
    check(edge_scroll_step(639, 240, 0, 1478, mid_y, 60, 60).dir == -1 && edge_scroll_step(639, 240, 0, 1477, mid_y, 60, 60).dir == 2, "classic: the largest x is 1920 - 442 = 1478");

    // the minimap: the layout says where it is
    int32_t wx = 0;
    int32_t wy = 0;
    minimap_point(480, 35, 60, 60, wx, wy);
    check(wx == 0 && wy == 0, "classic: the minimap's corner (480, 35) is the map's corner");
    minimap_point(480 + 119, 35 + 91, 60, 60, wx, wy, classic);
    check(wx == 1920 && wy == 1920, "classic: its far corner is the map's far corner");
    minimap_point(800, 35, 60, 60, wx, wy, wide);
    check(wx == 0 && wy == 0, "960 x 540: the minimap's corner is (800, 35)");
    minimap_point(800 + 119, 35 + 91, 60, 60, wx, wy, wide);
    check(wx == 1920 && wy == 1920, "960 x 540: its far corner is the map's far corner");
    minimap_point(480, 35, 60, 60, wx, wy, wide);
    check(wx < 0 && wy == 0, "960 x 540: the classic minimap's corner is left of it");
    // the view follows the pointer on the minimap: centred on the point, the square being half the view's size each way
    EdgeScroll step = minimap_scroll_step(480 + 59, 35 + 45, 0, 0, 60, 60);
    check(step.dx == 730 && step.dy == 729, "classic: the pointer in the middle of the minimap centres the 442 x 440 view on (951, 949)");
    step = minimap_scroll_step(800 + 59, 35 + 45, 0, 0, 60, 60, wide);
    check(step.dx == 570 && step.dy == 699, "960 x 540: the same point centres the 762 x 500 view on (951, 949)");
    step = minimap_scroll_step(800, 35, 570, 699, 60, 60, wide);
    check(step.dx == -570 && step.dy == -699, "960 x 540: the minimap's corner brings the view back to the map's corner");

    // the start view: the same square around the hill, the view's size decides how far it has to scroll
    int32_t ox = 0;
    int32_t oy = 0;
    start_view_origin(10, 10, 60, 60, ox, oy);
    check(ox == 86 && oy == 88, "classic: a hill's anchor tile (10, 10) is shown by scrolling to (86, 88)");
    start_view_origin(10, 10, 60, 60, ox, oy, wide);
    check(ox == 0 && oy == 28, "960 x 540: the larger view shows it by scrolling to (0, 28)");
    start_view_origin(55, 55, 60, 60, ox, oy, wide);
    check(ox == 1158 && oy == 1420, "960 x 540: near the far corner the view stops at the largest origin (1158, 1420)");
}

// =====================================================================================================================================================
// The pointer's limits
// =====================================================================================================================================================

SDL_Event motion_at(int32_t x, int32_t y) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = SDL_MOUSEMOTION;
    e.motion.x = x;
    e.motion.y = y;
    return e;
}

SDL_Event button_at(Uint32 type, int32_t x, int32_t y) {
    SDL_Event e;
    std::memset(&e, 0, sizeof(e));
    e.type = type;
    e.button.x = x;
    e.button.y = y;
    return e;
}

void test_pointer_limits() {
    group("pointer", "the pointer's limits are the picture's: a layout moves them, a picture inside a canvas is placed first");
    const ScreenLayout classic = ScreenLayout::classic();
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);

    // the classic overload and the classic layout agree on a grid of points (negative, on the edge, beyond)
    bool same = true;
    for (int32_t x = -50; x <= 700 && same; x += 7) {
        for (int32_t y = -50; y <= 540; y += 9) {
            SDL_Event a = motion_at(x, y);
            SDL_Event b = motion_at(x, y);
            clamp_pointer_event(a);
            clamp_pointer_event(b, classic);
            SDL_Event c = button_at(SDL_MOUSEBUTTONUP, x, y);
            clamp_pointer_event(c);
            if (a.motion.x != b.motion.x || a.motion.y != b.motion.y || a.motion.x != std::clamp(x, 0, 639) || a.motion.y != std::clamp(y, 0, 479) ||
                c.button.x != a.motion.x || c.button.y != a.motion.y) {
                same = false;
                break;
            }
        }
    }
    check(same, "the original's clamp: 0 .. 639 and 0 .. 479, for motions and button events");

    SDL_Event e = motion_at(5000, -3);
    clamp_pointer_event(e, wide);
    check(e.motion.x == 959 && e.motion.y == 0, "960 x 540: a motion far right and above lands on (959, 0)");
    e = motion_at(-5, 5000);
    clamp_pointer_event(e, wide);
    check(e.motion.x == 0 && e.motion.y == 539, "960 x 540: far left and below: (0, 539)");
    e = motion_at(700, 500);
    clamp_pointer_event(e, wide);
    check(e.motion.x == 700 && e.motion.y == 500, "960 x 540: a point of the picture beyond the classic 640 x 480 is left alone");
    e = button_at(SDL_MOUSEBUTTONDOWN, 960, 540);
    clamp_pointer_event(e, wide);
    check(e.button.x == 959 && e.button.y == 539, "960 x 540: a press on the first pixel beyond is on the edge");
    e = button_at(SDL_MOUSEBUTTONUP, 959, 539);
    clamp_pointer_event(e, wide);
    check(e.button.x == 959 && e.button.y == 539, "960 x 540: a release on the last pixel stays");

    // a picture that sits inside a bigger canvas (the classic picture centred in 960 x 540): the corner is subtracted first
    const LayoutRect picture{160, 30, 640, 480};
    e = motion_at(400, 300);
    clamp_pointer_event(e, picture);
    check(e.motion.x == 240 && e.motion.y == 270, "a picture at (160, 30): canvas (400, 300) is the picture's (240, 270)");
    e = motion_at(100, 10);
    clamp_pointer_event(e, picture);
    check(e.motion.x == 0 && e.motion.y == 0, "... a pointer over the left and top bar is the picture's top left pixel");
    e = motion_at(900, 535);
    clamp_pointer_event(e, picture);
    check(e.motion.x == 639 && e.motion.y == 479, "... over the right and bottom bar: its last pixel");
    e = button_at(SDL_MOUSEBUTTONDOWN, 160, 30);
    clamp_pointer_event(e, picture);
    check(e.button.x == 0 && e.button.y == 0, "... the picture's first pixel is (0, 0) for a button event too");

    // nothing else is touched
    SDL_Event k;
    std::memset(&k, 0, sizeof(k));
    k.type = SDL_KEYDOWN;
    k.key.keysym.sym = SDLK_a;
    SDL_Event before = k;
    clamp_pointer_event(k, wide);
    check(std::memcmp(&before, &k, sizeof(SDL_Event)) == 0, "a key event is untouched");
    SDL_Event wheel;
    std::memset(&wheel, 0, sizeof(wheel));
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.x = 7;
    wheel.wheel.y = -3;
    before = wheel;
    clamp_pointer_event(wheel, picture);
    check(std::memcmp(&before, &wheel, sizeof(SDL_Event)) == 0, "a wheel event is untouched");

    // the limits and the edge strips agree: the last pixel the pointer can be on is inside the strip of the east and south bands
    for (const ScreenLayout& l : {classic, wide, ScreenLayout::with_size(1280, 720)}) {
        SDL_Event far_event = motion_at(100000, 100000);
        clamp_pointer_event(far_event, l);
        check(far_event.motion.x == l.width - 1 && far_event.motion.y == l.height - 1, "the pointer's limit is the picture's last pixel (" + std::to_string(l.width) + " x " + std::to_string(l.height) + ")");
        check(detail::inside(detail::inner(3, l.width, l.height), far_event.motion.x, far_event.motion.y), "... and that pixel is in the south east strip that scrolls");
    }
}

// =====================================================================================================================================================
// The corner of the frame-rate plate and the network overlay
// =====================================================================================================================================================

void test_plate_and_overlay() {
    group("plate", "the version and the frame rate stand in the bottom right corner of the canvas; the network overlay is centred in the map view");
    const CornerPlate classic = CornerPlate::for_canvas(640, 480);
    check(classic == CornerPlate::classic() && classic.right_edge == 632 && classic.spark_y == 468 && classic.top == 467 && classic.bottom == 480, "640 x 480: the right edge 632, the sparkline at row 468, the plate rows 467 .. 479");
    const CornerPlate wide = CornerPlate::for_canvas(960, 540);
    check(wide.right_edge == 952 && wide.spark_y == 528 && wide.top == 527 && wide.bottom == 540, "960 x 540: the right edge 952, the sparkline at row 528, the plate rows 527 .. 539");
    for (const std::pair<int32_t, int32_t>& s : {std::pair<int32_t, int32_t>{640, 480}, {960, 540}, {1280, 720}, {1920, 1080}, {854, 480}}) {
        const CornerPlate p = CornerPlate::for_canvas(s.first, s.second);
        check(p.bottom == s.second && p.right_edge == s.first - 8 && p.top == p.spark_y - 1 && p.bottom - p.top == classic.bottom - classic.top, "the plate of " + std::to_string(s.first) + " x " + std::to_string(s.second) + " ends at the last row, 8 px in from the right edge, as tall as the original's");
    }

    // the latency texts of a match stack above the plate, right aligned with the frame rate: in the corner they are given
    const int32_t pw = 70;
    const int32_t dw = 80;
    const int32_t widest = 190;
    const LatencyCornerLayout old_layout = layout_latency_corner(pw, dw, widest, 14, 560, 470, LATENCY_LEFT_LIMIT_MATCH);
    const LatencyCornerLayout same_layout = layout_latency_corner(pw, dw, widest, 14, 560, 470, LATENCY_LEFT_LIMIT_MATCH, classic);
    check(old_layout.stacked && old_layout.ping_x == same_layout.ping_x && old_layout.delay_x == same_layout.delay_x && old_layout.ping_y == same_layout.ping_y && old_layout.delay_y == same_layout.delay_y,
          "the latency layout without a corner is the classic corner's");
    check(old_layout.ping_x == 632 - pw && old_layout.delay_x == 632 - dw && old_layout.delay_y == 467 - 1 - 14 && old_layout.ping_y == old_layout.delay_y - 14, "classic: both lines end at x = 632, the lower one a row above the plate");
    // (rewritten with the review fixes of M3: the match's limit is the layout's, where the row of score boxes ends: 780 at 960 x 540 where the boxes are spread over the strip; the old
    // lines took the original's 460 and added the 160 of dx / 2 by hand, which stood the row of texts on the third box)
    const int32_t wide_limit = ScreenLayout::with_size(960, 540).score_row_right();
    check(wide_limit == 780 && ScreenLayout::classic().score_row_right() == 460, "the score row ends at 780 at 960 x 540 (the third box 722 + 58) and at 460 in the original's picture");
    const LatencyCornerLayout wide_layout = layout_latency_corner(pw, dw, widest, 14, 840, 530, LATENCY_LEFT_LIMIT_SETUP, wide);
    check(!wide_layout.stacked && wide_layout.delay_x + dw == 840 - 6, "960 x 540, a page (the setup screen's limit): the room left of the version holds the two texts in one row, ending 6 px left of the version");
    const LatencyCornerLayout wide_stacked = layout_latency_corner(pw, dw, widest, 14, 840, 530, wide_limit, wide);
    check(wide_stacked.stacked && wide_stacked.ping_x == 952 - pw && wide_stacked.delay_x == 952 - dw && wide_stacked.delay_y == 527 - 1 - 14, "960 x 540: stacked, they end at x = 952 and the lower one is a row above the plate (row 527)");

    // the network overlay's box (render_net_overlay): the original's (17 + (441 - w) / 2, 26)
    const NetOverlayBox c = net_overlay_box(ScreenLayout::classic().view(), 100, 14);
    check(c.text_x == 17 + (441 - 100) / 2 && c.text_y == 26, "classic: the overlay's text at (17 + (441 - w) / 2, 26)");
    check_rect(c.box, LayoutRect{c.text_x - 6, 23, 112, 20}, "classic: its box has 6 px at the sides and 3 above and below");
    const NetOverlayBox wide_box = net_overlay_box(ScreenLayout::with_size(960, 540).view(), 100, 14);
    check(wide_box.text_x == 17 + (761 - 100) / 2 && wide_box.text_y == 26, "960 x 540: centred in the 762 px view");
}

// =====================================================================================================================================================
// The HUD
// =====================================================================================================================================================

/// Records the HUD's draw calls
class Recorder : public IRenderer {
public:
    struct Fill { int32_t x, y, w, h; assets::ColorRGBA c; };
    struct Text { std::string s; int32_t x, y; };
    struct Image { int32_t x, y, w, h; };
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override { fills.push_back({x, y, w, h, c}); }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override { rects.push_back({x, y, w, h, c}); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA) override { texts.push_back({s, x, y}); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA, FontSize) override { texts.push_back({s, x, y}); }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override {}
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t*) override { images.push_back({x, y, w, h}); }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override { clips.push_back({x, y, w, h}); }
    std::vector<Fill> fills;
    std::vector<Fill> rects;
    std::vector<Text> texts;
    std::vector<Image> images;
    std::vector<Image> clips;
};

void test_hud_classic_rectangles() {
    group("hud-classic", "the HUD's rectangles with the classic layout are the original's, one by one");
    HUD hud;
    check(hud.layout().is_classic(), "a new HUD has the classic layout");
    check_rect(rect_of(hud.help_button()), LayoutRect{476, 7, 46, 23}, "Help");
    check_rect(rect_of(hud.options_button()), LayoutRect{525, 7, 52, 23}, "Options");
    check_rect(rect_of(hud.quit_button()), LayoutRect{579, 7, 46, 23}, "Quit");
    check_rect(rect_of(hud.get_move_pedestal_button()), LayoutRect{482, 152, 43, 73}, "slot 1: Move / hatch / ally pedestal (FUN_01028d30: (482, 152) - (525, 225))");
    check_rect(rect_of(hud.get_ability_pedestal_button()), LayoutRect{539, 152, 43, 73}, "slot 2: the ability pedestal ((539, 152) - (582, 225))");
    check_rect(rect_of(hud.stop_button()), LayoutRect{597, 189, 31, 38}, "slot 3: Stop ((597, 189) - (628, 227))");
    check_rect(rect_of(hud.hatch_button()), LayoutRect{482, 152, 43, 73}, "the hatch pedestal is slot 1");
    check_rect(rect_of(hud.team_up_button()), LayoutRect{482, 152, 43, 73}, "the ally pedestal is slot 1");
    check_rect(rect_of(hud.send_to_button()), LayoutRect{532, 443, 44, 24}, "[All]");
    check_rect(rect_of(hud.team_button()), LayoutRect{579, 443, 46, 24}, "[Team]");
    check_rect(rect_of(hud.quit_yes_button()), LayoutRect{180, 260, 49, 24}, "the quit dialog's Yes");
    check_rect(rect_of(hud.quit_no_button()), LayoutRect{292, 260, 49, 24}, "the quit dialog's No");
    bool zones = true;
    for (int32_t y = 0; y < 480 && zones; ++y) {
        for (int32_t x = 0; x < 640; ++x) {
            const bool chat = x >= 482 && x < 620 && y >= 299 && y < 400;
            if (hud.over_map(x, y) != HUD::in_map_rect(x, y) || hud.over_minimap(x, y) != HUD::in_minimap_rect(x, y) || hud.in_chat_view(x, y) != chat) {
                zones = false;
                break;
            }
        }
    }
    check(zones, "the pointer's zones (map, minimap, chat log) are the original's at every pixel of the screen");
}

void test_hud_anchoring() {
    group("hud-anchors", "a layout with dx and dy moves exactly the right-anchored and bottom-anchored rectangles by dx and dy, and nothing else");
    HUD classic;
    HUD wide;
    const ScreenLayout layout = ScreenLayout::with_size(960, 540);
    wide.set_layout(layout);
    check(wide.layout() == layout, "set_layout keeps the layout");
    const int32_t dx = layout.dx();
    const int32_t dy = layout.dy();
    struct Item {
        const char* name;
        const UIButton* c;
        const UIButton* w;
        bool right;
        bool bottom;
    };
    const Item items[] = {
        {"Help", &classic.help_button(), &wide.help_button(), true, false},
        {"Options", &classic.options_button(), &wide.options_button(), true, false},
        {"Quit", &classic.quit_button(), &wide.quit_button(), true, false},
        {"the move pedestal", &classic.get_move_pedestal_button(), &wide.get_move_pedestal_button(), true, false},
        {"the ability pedestal", &classic.get_ability_pedestal_button(), &wide.get_ability_pedestal_button(), true, false},
        {"Stop", &classic.stop_button(), &wide.stop_button(), true, false},
        {"the hatch pedestal", &classic.hatch_button(), &wide.hatch_button(), true, false},
        {"the ally pedestal", &classic.team_up_button(), &wide.team_up_button(), true, false},
        {"[All]", &classic.send_to_button(), &wide.send_to_button(), true, true},
        {"[Team]", &classic.team_button(), &wide.team_button(), true, true},
        {"the quit dialog's Yes", &classic.quit_yes_button(), &wide.quit_yes_button(), false, false},
        {"the quit dialog's No", &classic.quit_no_button(), &wide.quit_no_button(), false, false},
    };
    for (const Item& item : items) {
        const LayoutRect want = rect_of(*item.c).moved(item.right ? dx : 0, item.bottom ? dy : 0);
        check_rect(rect_of(*item.w), want, std::string(item.name) + (item.right ? (item.bottom ? " (right and bottom anchored)" : " (right anchored)") : " (a picture of the original's screen: stays)"));
    }
    // the zones follow
    check(wide.over_map(16, 21) && wide.over_map(777, 520) && !wide.over_map(778, 100) && !wide.over_map(100, 521) && !wide.over_map(15, 100), "the map zone is (16, 21) - (778, 521)");
    check(wide.over_minimap(800, 35) && wide.over_minimap(918, 125) && !wide.over_minimap(919, 100) && !wide.over_minimap(480, 35) && !wide.over_minimap(800, 34), "the minimap zone is (800, 35) - (919, 126)");
    check(wide.in_chat_view(802, 299) && wide.in_chat_view(939, 459) && !wide.in_chat_view(939, 460) && !wide.in_chat_view(482, 299), "the chat zone is (802, 299) - (940, 460): it took the extra height");
    check(classic.over_map(100, 100) && !classic.over_map(500, 100) && classic.over_minimap(480, 35), "the classic HUD's zones are untouched by the other HUD");
}

void test_hud_layout_survives_init() {
    group("hud-init", "a layout that was set earlier is not lost at the next match: init() and reset() place the rectangles from it");
    HUD hud;
    const ScreenLayout layout = ScreenLayout::with_size(960, 540);
    hud.set_layout(layout);
    const LayoutRect help = rect_of(hud.help_button());
    const LayoutRect send_to = rect_of(hud.send_to_button());
    check(help.x == 796 && send_to.x == 852 && send_to.y == 503, "the layout is in place");
    hud.init(1);
    check(hud.layout() == layout, "init keeps the layout");
    check_rect(rect_of(hud.help_button()), help, "init(1): Help is where the layout put it");
    check_rect(rect_of(hud.send_to_button()), send_to, "init(1): [All] too");
    hud.reset();
    check_rect(rect_of(hud.help_button()), help, "reset(): Help");
    check_rect(rect_of(hud.get_move_pedestal_button()), LayoutRect{802, 152, 43, 73}, "reset(): the move pedestal");
    hud.init(3);
    check_rect(rect_of(hud.stop_button()), LayoutRect{917, 189, 31, 38}, "init(3): Stop");
    check(hud.over_minimap(800, 35) && !hud.over_minimap(480, 35), "the zones follow the layout after a new match");
    // back to the classic picture
    hud.set_layout(ScreenLayout::classic());
    check_rect(rect_of(hud.help_button()), LayoutRect{476, 7, 46, 23}, "set_layout(classic): Help is back");
    check_rect(rect_of(hud.send_to_button()), LayoutRect{532, 443, 44, 24}, "set_layout(classic): [All] is back");
    // the state of a button is the match's, not the layout's: a new match clears it, a new layout does not
    HUD h2;
    h2.set_layout(layout);
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1, 600000);
    ViewportCamera cam;
    cam.world_x = 400;
    cam.world_y = 400;
    h2.handle_mouse_down(h2.help_button().x + 2, h2.help_button().y + 12, SDL_BUTTON_LEFT, sim, cam);      // (the top 12 rows are the scroll band)
    check(h2.help_button().is_pressed, "a press on Help at its place in the wide layout presses it");
    h2.set_layout(layout);
    check(h2.help_button().is_pressed, "set_layout does not release it");
    h2.init(0);
    check(!h2.help_button().is_pressed, "a new match does");
}

void test_hud_pedestal_zones() {
    group("hud-pedestals", "a press on a pedestal acts where the layout put it, and only there");
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540)}) {
        const int32_t dx = layout.dx();
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 600000);
        const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
        sim.tick();
        HUD hud;
        hud.set_layout(layout);
        hud.set_sim_query(&sim);
        ViewportCamera cam;
        cam.set_view(layout.view());
        hud.select_ant(ant);
        const std::string at = std::string(layout.is_classic() ? "classic" : "960 x 540") + ": ";
        // the old place is the map in the wide layout (no latch); the layout's place latches the Move pedestal
        hud.handle_mouse_down(485, 160, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_up(485, 160, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_move_latched() == layout.is_classic(), at + "a press at (485, 160) " + (layout.is_classic() ? "latches" : "does not latch") + " the Move pedestal");
        hud.unlatch_pedestals();
        hud.select_ant(ant);
        hud.handle_mouse_down(485 + dx, 160, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_up(485 + dx, 160, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_move_latched(), at + "a press on the pedestal at (485 + dx, 160) latches it");
        hud.unlatch_pedestals();
        hud.handle_mouse_down(482 + dx - 1, 160, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_up(482 + dx - 1, 160, SDL_BUTTON_LEFT, sim, cam);
        check(!hud.is_move_latched(), at + "one pixel left of the pedestal does not");
    }
}

uint32_t fixed_clock() { return 1000; }

void test_hud_orders_in_wide_layout() {
    group("hud-orders", "an order from the map or the minimap goes to the point under the pointer by the layout's view and minimap");
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540)}) {
        const bool classic = layout.is_classic();
        const std::string at = std::string(classic ? "classic" : "960 x 540") + ": ";
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 600000);
        const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});
        sim.tick();
        HUD hud;
        hud.set_layout(layout);
        hud.set_sim_query(&sim);
        std::vector<std::pair<int32_t, int32_t>> markers;
        hud.set_on_spawn_click_marker([&markers](int32_t x, int32_t y) { markers.emplace_back(x, y); });
        ViewportCamera cam;
        cam.set_view(layout.view());
        // a right click on the minimap orders the point under it: the middle of the minimap is the map point (951, 949)
        hud.select_ant(ant);
        const LayoutRect mini = layout.minimap();
        hud.handle_mouse_down(mini.x + 59, mini.y + 45, SDL_BUTTON_RIGHT, sim, cam);
        hud.handle_mouse_up(mini.x + 59, mini.y + 45, SDL_BUTTON_RIGHT, sim, cam);
        check(markers.size() == 1 && markers[0].first == 951 && markers[0].second == 949, at + "a right click in the middle of the minimap orders the map point (951, 949)");
        // the Move pedestal is latched; a click on the map then orders (a release without a rubber band): inside the layout's view only
        markers.clear();
        hud.select_ant(ant);
        const UIButton pedestal = hud.get_move_pedestal_button();
        hud.handle_mouse_down(pedestal.x + 5, pedestal.y + 5, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_up(pedestal.x + 5, pedestal.y + 5, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_move_latched(), at + "the Move pedestal is latched");
        hud.handle_mouse_down(300, 300, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_up(300, 300, SDL_BUTTON_LEFT, sim, cam);
        check(markers.size() == 1 && markers[0].first == 300 - 16 && markers[0].second == 300 - 21, at + "a click at (300, 300) with the pedestal latched orders the world point (284, 279)");
        if (!classic) {
            markers.clear();
            hud.select_ant(ant);
            hud.handle_mouse_down(pedestal.x + 5, pedestal.y + 5, SDL_BUTTON_LEFT, sim, cam);
            hud.handle_mouse_up(pedestal.x + 5, pedestal.y + 5, SDL_BUTTON_LEFT, sim, cam);
            hud.handle_mouse_down(700, 300, SDL_BUTTON_LEFT, sim, cam);                                // beyond the classic view, on the wide one
            hud.handle_mouse_up(700, 300, SDL_BUTTON_LEFT, sim, cam);
            check(markers.size() == 1 && markers[0].first == 700 - 16 && markers[0].second == 300 - 21, at + "a click at (700, 300), beyond the classic view, orders the world point (684, 279)");
        }
    }
}

void test_hud_drawing(const assets::AssetArchive& arc) {
    group("hud-draw", "the HUD draws its backing fill, the minimap and its view frame, the status line, the chat input and the score boxes where the layout says");
    sim::WorldState world;
    world.width = 60;
    world.height = 60;
    world.cells.assign(60u * 60u, sim::TileCell{});
    auto frame = [&](const ScreenLayout& layout, Recorder& rec) {
        HUD hud;
        hud.set_ticks_function(&fixed_clock);                                  // (the chat input's caret blinks with the clock: it is on at the moment the HUD is made)
        hud.init(0);
        hud.set_layout(layout);
        hud.post_status("Ready");
        hud.set_chat_input("hello");
        ViewportCamera camera;
        camera.set_view(layout.view());
        hud.render(rec, arc, world, camera);
    };
    Recorder c;
    frame(ScreenLayout::classic(), c);
    Recorder w;
    const ScreenLayout layout = ScreenLayout::with_size(960, 540);
    frame(layout, w);

    auto first_fill_of = [](const Recorder& r, int32_t width, int32_t height) {
        for (const auto& f : r.fills) {
            if (f.w == width && f.h == height) return f;
        }
        return Recorder::Fill{-1, -1, -1, -1, {}};
    };
    const auto cp = first_fill_of(c, 160, 458);
    check(cp.x == 480 && cp.y == 22, "classic: the backing fill is (480, 22) 160 x 458");
    const auto wp = first_fill_of(w, 160, 518);
    check(wp.x == 800 && wp.y == 22, "960 x 540: the backing fill is (800, 22) 160 x 518");

    check(c.images.size() == 1 && c.images[0].x == 480 && c.images[0].y == 35 && c.images[0].w == 119 && c.images[0].h == 91, "classic: the minimap image is drawn at (480, 35)");
    check(w.images.size() == 1 && w.images[0].x == 800 && w.images[0].y == 35 && w.images[0].w == 119 && w.images[0].h == 91, "960 x 540: the minimap image is drawn at (800, 35)");
    auto frame_of = [](const Recorder& r) {
        for (const auto& f : r.rects) {
            if (f.c.r == 251 && f.c.g == 251 && f.c.b == 255) return f;
        }
        return Recorder::Fill{-1, -1, -1, -1, {}};
    };
    // the view frame is the view's size scaled to the minimap (+ 1): 442 * 119 / 1920 = 27, 440 * 91 / 1920 = 20; 762 * 119 / 1920 = 47, 500 * 91 / 1920 = 23
    check(frame_of(c).w == 28 && frame_of(c).h == 21, "classic: the minimap's view frame is 28 x 21");
    check(frame_of(w).w == 48 && frame_of(w).h == 24, "960 x 540: the minimap's view frame is 48 x 24 (the view is 762 x 500)");
    check(frame_of(c).x == 480 && frame_of(w).x == 800, "... at the minimap's corner (the camera is at the map's corner)");

    auto text_at = [](const Recorder& r, const std::string& s) {
        for (const auto& t : r.texts) {
            if (t.s == s) return t;
        }
        return Recorder::Text{"", -1, -1};
    };
    check(text_at(c, "Ready").x == 481 && text_at(c, "Ready").y == 254, "classic: the status line at (481, 254)");
    check(text_at(w, "Ready").x == 801 && text_at(w, "Ready").y == 254, "960 x 540: the status line at (801, 254)");
    check(text_at(c, "hello").x == 481 && text_at(c, "hello").y == 424, "classic: the chat input at (481, 424)");
    check(text_at(w, "hello").x == 801 && text_at(w, "hello").y == 484, "960 x 540: the chat input at (801, 484)");
    check(text_at(c, "_").x == 481 + 30 && text_at(w, "_").x == 801 + 30 && text_at(w, "_").y == 484, "960 x 540: the chat input's caret is drawn too (it is hidden only beyond the picture's right edge, which is 960 here)");

    // the four score boxes: 54 x 14 fills in the team colours (green 7 67 47, red 119 0 0, blue 43 39 107, black 39 39 59); the local team (green) has the top bar's slot, the others the
    // bottom strip's three in colour order
    auto box_of = [](const Recorder& r, uint8_t red, uint8_t green, uint8_t blue) {
        for (const auto& f : r.fills) {
            if (f.w == 54 && f.h == 14 && f.c.r == red && f.c.g == green && f.c.b == blue) return f;
        }
        return Recorder::Fill{-1, -1, -1, -1, {}};
    };
    struct Team { const char* name; uint8_t r, g, b; ScoreSlot classic_slot; };
    const Team teams[] = {{"green (the local team)", 7, 67, 47, kOriginalSlots[0]}, {"red", 119, 0, 0, kOriginalSlots[1]}, {"blue", 43, 39, 107, kOriginalSlots[2]}, {"black", 39, 39, 59, kOriginalSlots[3]}};
    for (size_t k = 0; k < 4; ++k) {
        const auto cb = box_of(c, teams[k].r, teams[k].g, teams[k].b);
        const auto wb = box_of(w, teams[k].r, teams[k].g, teams[k].b);
        check(cb.x == teams[k].classic_slot.box_left && cb.y == teams[k].classic_slot.top, std::string("classic: the ") + teams[k].name + " team's score box is the original's");
        check(wb.x == layout.score_slot(k).box_left && wb.y == layout.score_slot(k).top, std::string("960 x 540: the ") + teams[k].name + " team's score box is the layout's slot " + std::to_string(k));
    }
}

void test_hud_pointer_follows_layout(const assets::AssetArchive& arc) {
    group("hud-pointer", "a press on the map starts the rubber band inside the layout's view, and the minimap drag centres the layout's view");
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540)}) {
        const bool classic = layout.is_classic();
        const std::string at = std::string(classic ? "classic" : "960 x 540") + ": ";
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 600000);
        sim.tick();
        HUD hud;
        hud.set_layout(layout);
        hud.set_ticks_function(&fixed_clock);
        ViewportCamera cam;
        cam.set_view(layout.view());
        cam.world_x = 400;
        cam.world_y = 400;
        // a press at (600, 300) is on the map in the wide layout (the view reaches x = 777), beyond the classic view (x < 458)
        hud.handle_mouse_down(600, 300, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_input_captured() == !classic, at + "a press at (600, 300) " + (classic ? "is beyond the view: nothing is captured" : "is on the map: the rubber band captures"));
        hud.handle_mouse_up(600, 300, SDL_BUTTON_LEFT, sim, cam);
        // the rubber band is held one pixel inside the view whatever the pointer does: a press at (300, 300), a drag to (900, 700)
        hud.handle_mouse_down(300, 300, SDL_BUTTON_LEFT, sim, cam);
        hud.handle_mouse_motion(900, 700, sim, cam);
        Recorder rec;
        hud.render(rec, arc, sim.get_world_state(), cam);
        Recorder::Fill band{-1, -1, -1, -1, {}};
        for (const auto& r : rec.rects) {
            if (r.c.r == 255 && r.c.g == 0 && r.c.b == 0) band = r;
        }
        const LayoutRect view = layout.view();
        check(band.x == 300 && band.y == 300 && band.x + band.w == view.right() - 1 && band.y + band.h == view.bottom() - 1,
              at + "the rubber band from (300, 300) is held inside the view: it ends at (" + std::to_string(view.right() - 1) + ", " + std::to_string(view.bottom() - 1) + ")");
        hud.handle_mouse_up(900, 700, SDL_BUTTON_LEFT, sim, cam);
        // the minimap drag: the pointer in the middle of the minimap centres the view on the map point under it
        cam.world_x = 0;
        cam.world_y = 0;
        cam.x = 0.0f;
        cam.y = 0.0f;
        const LayoutRect mini = layout.minimap();
        hud.handle_mouse_down(mini.x + 59, mini.y + 45, SDL_BUTTON_LEFT, sim, cam);
        check(hud.is_input_captured(), at + "a press on the minimap captures");
        check(hud.input_tick(cam, 60, 60, mini.x + 59, mini.y + 45), at + "the input task scrolls the view");
        check(cam.world_x == (classic ? 730 : 570) && cam.world_y == (classic ? 729 : 699), at + "the view is centred on the map point (951, 949) under the pointer");
        hud.handle_mouse_up(mini.x + 59, mini.y + 45, SDL_BUTTON_LEFT, sim, cam);
    }
}

void test_chat_follows_layout() {
    group("chat", "the chat log's window follows the layout's chat view: it holds more lines when the view is taller");
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    HUD classic_hud;
    HUD wide_hud;
    wide_hud.set_layout(wide);
    for (int i = 0; i < 20; ++i) {                                  // every entry is a 12 px header and a 12 px body, and one pixel between entries: 25 px
        classic_hud.add_chat_entry("Bob", "hi");
        wide_hud.add_chat_entry("Bob", "hi");
    }
    const int32_t end = classic_hud.chat_content_end();
    check(end == 37 + 20 * 25 && wide_hud.chat_content_end() == end, "the start message (37 px) and 20 entries of 25 px make a log of 537 px (the end is one past the last row)");
    check(classic_hud.chat_follow_target() == end - 1 - 101, "classic: the log follows the newest entry: its window (101 px) ends at the newest row");
    check(wide_hud.chat_follow_target() == end - 1 - 161, "960 x 540: the window is 161 px high");
    // a new layout puts the window inside the log again: the position that the 101 px window wanted is too far for the 161 px one
    classic_hud.set_layout(wide);
    check(classic_hud.chat_follow_target() == end - 161, "a taller chat view pulls the follow position back to the log's end less 161");
    classic_hud.set_layout(ScreenLayout::classic());
    check(classic_hud.chat_follow_target() == end - 161, "(and a layout that makes the view shorter does not push it forward: the follow task does that)");
}

void test_ctrl_n_follows_layout() {
    group("ctrl-n", "Ctrl+N shows the next ant by scrolling the layout's view just far enough to hold the 256 px square around it");
    for (const ScreenLayout& layout : {ScreenLayout::classic(), ScreenLayout::with_size(960, 540)}) {
        const LayoutRect view = layout.view();
        sim::SimulationEngine sim;
        sim.init_test_world(60, 60, 1, 600000);
        sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{28, 28});
        sim.tick();
        HUD hud;
        hud.set_layout(layout);
        ViewportCamera cam;
        cam.set_view(view);
        cam.x = 0.0f;
        cam.y = 0.0f;
        cam.world_x = 0;
        cam.world_y = 0;
        const int32_t px = sim.get_world_state().ants.front().px;
        const int32_t py = sim.get_world_state().ants.front().py;
        hud.handle_key_down('n', sim, cam, KMOD_CTRL);
        check(cam.world_x == px + 128 - view.w && cam.world_y == py + 128 - view.h,
              std::string(layout.is_classic() ? "classic" : "960 x 540") + ": the view scrolls to (" + std::to_string(px + 128 - view.w) + ", " + std::to_string(py + 128 - view.h) + ") to show the ant's square");
    }
}

void test_caret_limit() {
    group("caret", "an edit field's caret is hidden beyond the right edge of the picture, which is the layout's width");
    const assets::ColorRGBA white{255, 255, 255, 255};
    auto caret_drawn = [&](int32_t screen_w) {
        Recorder rec;
        draw_edit_line(rec, "ab", 900, 10, 40, true, true, white, FontSize::Px12, screen_w);
        for (const auto& t : rec.texts) {
            if (t.s == "_") return true;
        }
        return false;
    };
    check(caret_drawn(960), "a caret at x = 912 is drawn in a 960 wide picture");
    check(!caret_drawn(640), "... and not in the original's 640 wide one");
    Recorder rec;
    draw_edit_line(rec, "ab", 600, 10, 40, true, true, white, FontSize::Px12);
    bool drawn = false;
    for (const auto& t : rec.texts) drawn = drawn || t.s == "_";
    check(drawn, "without a width the original's 640 applies: a caret at x = 612 is drawn");
    Recorder far_rec;
    draw_edit_line(far_rec, "ab", 640, 10, 40, true, true, white, FontSize::Px12);
    drawn = false;
    for (const auto& t : far_rec.texts) drawn = drawn || t.s == "_";
    check(!drawn, "... one at x >= 640 is not");
}

void test_cursor_follows_layout() {
    group("cursor", "the cursor decision reads the layout: the scroll arrows are at the picture's edges, the map is the layout's view");
    sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1, 600000);
    const uint32_t ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{20, 20});
    sim.tick();
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    HUD classic_hud;
    HUD wide_hud;
    wide_hud.set_layout(wide);
    classic_hud.set_sim_query(&sim);
    wide_hud.set_sim_query(&sim);
    ViewportCamera cam;
    cam.world_x = 400;
    cam.world_y = 400;
    const auto& world = sim.get_world_state();
    auto cursor = [&](HUD& hud, int32_t x, int32_t y) { return hud.evaluate_cursor(x, y, world, sim.grid(), cam); };
    check(cursor(wide_hud, 955, 270) == CursorType::ScrollE, "960 x 540: the pointer at the right edge shows the east arrow");
    check(cursor(classic_hud, 955, 270) == CursorType::Normal, "classic: (955, 270) is beyond the screen: a plain pointer");
    check(cursor(wide_hud, 480, 535) == CursorType::ScrollS && cursor(wide_hud, 958, 536) == CursorType::ScrollSE, "960 x 540: the south and south east arrows are at the bottom edge");
    check(cursor(wide_hud, 639, 240) == CursorType::Normal, "960 x 540: the classic east edge is not an edge any more");
    check(cursor(classic_hud, 639, 240) == CursorType::ScrollE, "classic: it is");
    // the map is the layout's view: with an own ant selected the ground shows the move cursor there, a plain pointer elsewhere
    wide_hud.select_ant(ant);
    classic_hud.select_ant(ant);
    check(cursor(wide_hud, 700, 300) == CursorType::Move, "960 x 540: the ground at (700, 300), inside the 762 x 500 view, shows the move cursor");
    check(cursor(classic_hud, 700, 300) == CursorType::Normal, "classic: (700, 300) is beyond the 442 x 440 view: a plain pointer");
    check(cursor(wide_hud, 790, 300) == CursorType::Normal && cursor(wide_hud, 777, 300) == CursorType::Move, "960 x 540: the view ends at x = 778");
    check(cursor(wide_hud, 400, 521) == CursorType::Normal && cursor(wide_hud, 400, 520) == CursorType::Move, "960 x 540: and at y = 521");
    // the click goes to the world point under the pointer: the minimap of the layout scrolls, a press on the map gives an order
    ScreenLayout shifted = wide;
    shifted.view_x = 40;
    shifted.view_y = 30;
    HUD shifted_hud;
    shifted_hud.set_layout(shifted);
    shifted_hud.set_sim_query(&sim);
    const uint32_t near_ant = sim.spawn_unit(0, sim::AntType::Worker, sim::TileCoord{5, 5});          // at world (176, 176)
    sim.tick();
    cam.world_x = 0;
    cam.world_y = 0;
    cam.set_view(shifted.view());
    shifted_hud.handle_mouse_down(40 + 176, 30 + 176, SDL_BUTTON_LEFT, sim, cam);                      // its place on a view whose corner is (40, 30)
    shifted_hud.handle_mouse_up(40 + 176, 30 + 176, SDL_BUTTON_LEFT, sim, cam);
    check(shifted_hud.get_selected_ant_id() == near_ant, "a view whose corner is (40, 30): a click on the ant's place there selects it (the corner is read, not assumed to be (16, 21))");
    shifted_hud.clear_selection();
    shifted_hud.handle_mouse_down(40 + 190, 30 + 150, SDL_BUTTON_LEFT, sim, cam);                       // a rubber band over world (190, 150) - (260, 260): it only just reaches the ant at (176, 176)
    shifted_hud.handle_mouse_motion(40 + 260, 30 + 260, sim, cam);                                      // (its hit box ends at x = 196)
    shifted_hud.handle_mouse_up(40 + 260, 30 + 260, SDL_BUTTON_LEFT, sim, cam);
    check(shifted_hud.is_ant_selected(near_ant) && !shifted_hud.is_ant_selected(ant), "... and so does a rubber band over it (the band's screen rectangle is taken to the world by the view's corner)");
    check(shifted_hud.over_map(40, 30) && !shifted_hud.over_map(39, 100) && !shifted_hud.over_map(100, 29) && shifted_hud.over_map(801, 529) && !shifted_hud.over_map(802, 100) && !shifted_hud.over_map(100, 530),
          "... and its zone is (40, 30) - (802, 530)");
}

// =====================================================================================================================================================
// The camera and the renderer
// =====================================================================================================================================================

void test_camera() {
    group("camera", "the camera's view origin and size come from the layout");
    ViewportCamera cam;
    check(cam.view_x == 16 && cam.view_y == 21 && cam.viewport_w == 442 && cam.viewport_h == 440, "a new camera has the original's view");
    int32_t sx = 0;
    int32_t sy = 0;
    cam.x = 100.0f;
    cam.y = 50.0f;
    check(cam.world_to_screen(100, 50, sx, sy) && sx == 16 && sy == 21, "classic: the world pixel at the camera is the screen pixel (16, 21)");
    cam.set_view(ScreenLayout::with_size(960, 540).view());
    check(cam.view_x == 16 && cam.view_y == 21 && cam.viewport_w == 762 && cam.viewport_h == 500, "set_view takes the 960 x 540 layout's view");
    int32_t wx = 0;
    int32_t wy = 0;
    check(cam.screen_to_world(777, 520, wx, wy) && wx == 100 + 761 && wy == 50 + 499, "the last pixel of the wide view is a world point");
    check(!cam.screen_to_world(778, 100, wx, wy) && !cam.screen_to_world(100, 521, wx, wy) && !cam.screen_to_world(15, 100, wx, wy), "the pixels around it are not");
    check(cam.world_to_screen(100 + 700, 50 + 100, sx, sy) && sx == 716 && sy == 121, "a world point 700 px right of the camera is on the screen now");
    cam.center_on(976, 976, 60, 60);
    check(static_cast<int32_t>(cam.x) == 976 - 381 && static_cast<int32_t>(cam.y) == 976 - 250, "center_on centres the 762 x 500 view");
    cam.x = 5000.0f;
    cam.y = 5000.0f;
    cam.clamp_to_bounds(60, 60);
    check(static_cast<int32_t>(cam.x) == 1920 - 762 && static_cast<int32_t>(cam.y) == 1920 - 500, "the camera's largest origin is the map less the view (1158, 1420)");
    ViewportCamera shifted;
    LayoutRect odd{40, 30, 300, 200};
    shifted.set_view(odd);
    shifted.x = 10.0f;
    shifted.y = 20.0f;
    check(shifted.world_to_screen(10, 20, sx, sy) && sx == 40 && sy == 30 && shifted.screen_to_world(40, 30, wx, wy) && wx == 10 && wy == 20, "a view at another corner: the world pixel at the camera is at the corner");
}

/// A software renderer over a hidden window with a map loaded (the TINY map is 31 x 31 tiles: larger than the biggest view of this test)
struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("screen-layout", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (win == nullptr) return;
        if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL")) return;
        engine.init(level, 1337);
        if (!renderer.init(win, archive)) return;
        renderer.set_level(level);
        renderer.pin_animation_clock(1500);
        ok = true;
    }
    ~RendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RendererRig(const RendererRig&) = delete;
    RendererRig& operator=(const RendererRig&) = delete;

    /// The pixels of a rectangle of the canvas (the window is as big as the canvas: scale 1) as RGBA
    std::vector<uint8_t> read(int32_t x, int32_t y, int32_t w, int32_t h) {
        std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
        SDL_Rect r{x, y, w, h};
        SDL_RenderReadPixels(renderer.get_sdl_renderer(), &r, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
        return px;
    }
    /// How many pixels of the rectangle are still the magenta that the frame was cleared with
    int32_t magenta_in(int32_t x, int32_t y, int32_t w, int32_t h) {
        const std::vector<uint8_t> p = read(x, y, w, h);
        int32_t n = 0;
        for (size_t i = 0; i + 3 < p.size(); i += 4) n += (p[i] == 255 && p[i + 1] == 0 && p[i + 2] == 255) ? 1 : 0;
        return n;
    }
    /// The rectangle was drawn into (almost all of it: a tile may draw nothing) / was not touched
    bool painted(int32_t x, int32_t y, int32_t w, int32_t h) { return magenta_in(x, y, w, h) * 50 < w * h; }
    bool untouched(int32_t x, int32_t y, int32_t w, int32_t h) { return magenta_in(x, y, w, h) == w * h; }
    /// A frame of the map layers over a magenta canvas
    void frame() {
        SDL_Renderer* sr = renderer.get_sdl_renderer();
        SDL_SetRenderDrawColor(sr, 255, 0, 255, 255);
        SDL_RenderClear(sr);
        renderer.render_map_layers(engine.grid());
    }

    SDL_Window* win{nullptr};
    assets::LevelData level;
    sim::SimulationEngine engine;
    Renderer renderer;
    bool ok{false};
};

void test_renderer(const assets::AssetArchive& arc) {
    group("renderer", "the renderer draws and clips the map in the layout's view, and the canvas is SDL's logical size");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer rig is up");
    if (!rig.ok) return;
    SDL_Renderer* sr = rig.renderer.get_sdl_renderer();
    int32_t lw = 0;
    int32_t lh = 0;
    SDL_RenderGetLogicalSize(sr, &lw, &lh);
    check(lw == 640 && lh == 480 && rig.renderer.canvas_w() == 640 && rig.renderer.canvas_h() == 480, "a new renderer's canvas is the original's 640 x 480");
    check(rig.renderer.layout().is_classic() && rig.renderer.camera().viewport_w == 442, "... with the classic layout");
    rig.renderer.set_canvas_size(960, 540);
    SDL_RenderGetLogicalSize(sr, &lw, &lh);
    check(lw == 960 && lh == 540 && rig.renderer.canvas_w() == 960 && rig.renderer.canvas_h() == 540, "set_canvas_size gives SDL's logical size");
    SDL_RenderSetLogicalSize(sr, 100, 100);                         // (what a lost logical size looks like)
    rig.renderer.refit_canvas();
    SDL_RenderGetLogicalSize(sr, &lw, &lh);
    check(lw == 960 && lh == 540, "refit_canvas applies it again (the application does so on every size event)");

    // the classic view in a 960 x 540 canvas: 442 x 440 at (16, 21)
    rig.renderer.camera().x = 0.0f;
    rig.renderer.camera().y = 0.0f;
    rig.frame();
    check(rig.painted(16, 21, 442, 440), "classic layout: the terrain fills the 442 x 440 view");
    check(rig.untouched(0, 0, 16, 540) && rig.untouched(0, 0, 960, 21) && rig.untouched(458, 0, 502, 540) && rig.untouched(0, 461, 960, 79), "classic layout: nothing is drawn outside it");
    // the 960 x 540 layout: 762 x 500
    const ScreenLayout layout = ScreenLayout::with_size(960, 540);
    rig.renderer.set_layout(layout);
    check(rig.renderer.layout() == layout && rig.renderer.camera().viewport_w == 762 && rig.renderer.camera().viewport_h == 500 && rig.renderer.camera().view_x == 16, "set_layout gives the camera the 762 x 500 view");
    rig.frame();
    check(rig.painted(16, 21, 762, 500), "960 x 540 layout: the terrain fills the 762 x 500 view");
    check(rig.painted(458, 21, 320, 500) && rig.painted(16, 461, 762, 60), "... including what lies beyond the classic view");
    check(rig.untouched(0, 0, 16, 540) && rig.untouched(0, 0, 960, 21) && rig.untouched(778, 0, 182, 540) && rig.untouched(0, 521, 960, 19), "960 x 540 layout: and is clipped to it");
    // a view whose corner is not the original's (16, 21) is drawn where the layout says
    ScreenLayout shifted = layout;
    shifted.view_x = 40;
    shifted.view_y = 30;
    rig.renderer.set_layout(shifted);
    rig.renderer.camera().x = 0.0f;
    rig.renderer.camera().y = 0.0f;
    rig.frame();
    check(rig.painted(40, 30, 762, 500) && rig.untouched(0, 0, 40, 540) && rig.untouched(0, 0, 960, 30) && rig.untouched(802, 0, 158, 540) && rig.untouched(0, 530, 960, 10),
          "a view at (40, 30): the terrain starts at its corner and the canvas outside it stays untouched");
    rig.renderer.set_layout(layout);
    // the camera was clamped to the larger view when the layout came: the TINY map is 992 px, so the origin is at most 992 - 762
    rig.renderer.camera().x = 5000.0f;
    rig.renderer.camera().y = 5000.0f;
    rig.renderer.set_layout(layout);
    check(static_cast<int32_t>(rig.renderer.camera().x) == 992 - 762 && static_cast<int32_t>(rig.renderer.camera().y) == 992 - 500, "a layout that comes with a camera beyond its view's largest origin pulls it back");
    rig.frame();
    check(rig.painted(16, 21, 762, 500) && rig.untouched(778, 0, 182, 540), "... and the view is still full");
}

/// Ants of every type on a lattice over the world area [x0, x1) x [y0, y1) (a sprite is cut by every edge of whatever view looks at it), with every seventh selected
void populate_lattice(sim::WorldState& world, const assets::AssetArchive& arc, int32_t x0, int32_t y0, int32_t x1, int32_t y1, std::vector<uint32_t>& selected) {
    static const char kLetters[6] = {'g', 'b', 'f', 't', 'c', 's'};
    const int32_t map_w = static_cast<int32_t>(world.width) * 32;
    const int32_t map_h = static_cast<int32_t>(world.height) * 32;
    uint32_t n = 0;
    for (int32_t y = std::max(y0, 4); y < std::min(y1, map_h - 4); y += 41) {
        for (int32_t x = std::max(x0, 4); x < std::min(x1, map_w - 4); x += 37, ++n) {
            const int type = static_cast<int>(n % 6u);
            const std::string name = std::string("a") + kLetters[type] + "st" + ((n / 6u) % 2u == 0u ? "3" : "7") + "01";
            const int32_t clip = arc.find_animation_id(name);
            sim::AntSnapshot a;
            a.id = 7000u + n;
            a.player_id = static_cast<uint8_t>(n % 4u);
            a.type = static_cast<sim::AntType>(type);
            a.px = x;
            a.py = y;
            a.tile_x = x / 32;
            a.tile_y = y / 32;
            a.hp = static_cast<uint16_t>(1u + n % 10u);
            a.max_hp = 10;
            a.state = sim::UnitState::Idle;
            a.loco_clip = clip < 0 ? uint16_t{0x7FFE} : static_cast<uint16_t>(clip);
            a.loco_frame = 0;
            a.loco_mirrored = false;
            a.loco_left_ms = 100;
            world.ants.push_back(a);
            if (n % 7u == 0u) selected.push_back(a.id);
        }
    }
}

/// The wide picture shows the classic picture's world, only more of it: whatever the classic layout draws for a window of the world (terrain, objects, ants, their ears and hit
/// point digits, effects, score bubbles, the click marker, the hill's brackets) is, pixel for pixel, a part of what the 960 x 540 layout draws for the larger window
void test_renderer_world_window(const assets::AssetArchive& arc) {
    group("window", "a layout's view shows the same world as the classic view, pixel for pixel: the culling and the clip read the view's size");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer rig is up");
    if (!rig.ok) return;
    Renderer& r = rig.renderer;
    r.set_canvas_size(960, 540);
    r.set_hud_team(0);
    sim::WorldState world = rig.engine.get_world_state();
    world.ants.clear();
    const ScreenLayout classic = ScreenLayout::classic();
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const int32_t cx = 200;                                          // the TINY map is 992 px: the wide view [200, 962) x [150, 650) is inside it
    const int32_t cy = 150;
    std::vector<uint32_t> selected;
    populate_lattice(world, arc, cx - 60, cy - 60, cx + 762 + 60, cy + 500 + 60, selected);
    check(world.ants.size() > 250 && !selected.empty(), "the world has a few hundred ants over the wide view and 60 px beyond it");
    // effects and score bubbles across the classic view's right and bottom edges, and in the part that only the wide view shows
    const struct { const char* name; uint32_t duration; } kinds[4] = {{"bombex", 680}, {"sputter", 830}, {"dsplash", 460}, {"battle", 270}};
    const int32_t at[8][2] = {{412, 100}, {442, 300}, {470, 220}, {120, 410}, {300, 440}, {600, 150}, {700, 460}, {330, 470}};
    for (int i = 0; i < 8; ++i) {
        sim::VisualEffect e;
        e.anim_name = kinds[i % 4].name;
        e.px = cx + at[i][0];
        e.py = cy + at[i][1];
        e.elapsed_ms = 100u + 60u * static_cast<uint32_t>(i % 3);
        e.duration_ms = kinds[i % 4].duration;
        e.frame = static_cast<uint16_t>(e.elapsed_ms / 50u);
        e.total_frames = static_cast<uint16_t>(e.duration_ms / 50u);
        world.effects.push_back(e);
    }
    for (const std::pair<int32_t, int32_t>& p : {std::pair<int32_t, int32_t>{430, 200}, {600, 450}, {250, 440}}) {
        sim::ScoreBubble bubble;
        bubble.x = cx + p.first;
        bubble.y = cy + p.second;
        bubble.amount = p.first == 600 ? -35 : 150;
        bubble.elapsed_ms = 100;
        world.score_bubbles.push_back(bubble);
    }
    r.spawn_transient_effect("xmarks", cx + 450, cy + 200);          // the click marker, beyond the classic view's right edge
    r.set_show_hp(true);
    // fog of war: the tiles of a pattern are explored, the rest is covered (and the ants of the enemies on covered ground are hidden)
    world.fog_of_war_enabled = true;
    world.fog_revealed.assign(static_cast<size_t>(world.width) * world.height, 0);
    for (uint32_t ty = 0; ty < world.height; ++ty) {
        for (uint32_t tx = 0; tx < world.width; ++tx) {
            const int32_t ddx = static_cast<int32_t>(tx) - 17;
            const int32_t ddy = static_cast<int32_t>(ty) - 12;
            world.fog_revealed[static_cast<size_t>(ty) * world.width + tx] = (ddx * ddx + ddy * ddy < 90 || (tx * 7u + ty * 13u) % 5u == 0u) ? 1 : 0;
        }
    }
    auto frame = [&](const ScreenLayout& layout, int32_t camera_x, int32_t camera_y) {
        r.set_layout(layout);
        r.camera().x = static_cast<float>(camera_x);
        r.camera().y = static_cast<float>(camera_y);
        r.camera().clamp_to_bounds(rig.level.width(), rig.level.height());
        r.begin_frame();
        SDL_SetRenderDrawColor(r.get_sdl_renderer(), 255, 0, 255, 255);                               // (a canvas that the frame must not touch outside the view)
        SDL_RenderClear(r.get_sdl_renderer());
        r.render_world(world, rig.engine.grid(), -1, selected, false, false, -1, -1, 0, 0.0f);       // (the hill of team 0 is selected: its brackets)
    };
    frame(wide, cx, cy);
    check(r.camera().world_x == cx && r.camera().world_y == cy, "the wide camera stands where it was put");
    // the same world window drawn by the classic layout, at the camera that puts it at the classic view
    struct Shift { int32_t a, b; };
    for (const Shift& sh : {Shift{0, 0}, Shift{320, 0}, Shift{0, 60}, Shift{320, 60}, Shift{180, 30}}) {
        frame(classic, cx + sh.a, cy + sh.b);
        const std::vector<uint8_t> classic_view = rig.read(16, 21, 442, 440);
        // wide: the same world window is at the screen position (16 + a, 21 + b)
        frame(wide, cx, cy);
        const std::vector<uint8_t> part = rig.read(16 + sh.a, 21 + sh.b, 442, 440);
        check(classic_view == part, "the classic view at the camera (" + std::to_string(cx + sh.a) + ", " + std::to_string(cy + sh.b) + ") is the 960 x 540 view's window at (" + std::to_string(16 + sh.a) + ", " + std::to_string(21 + sh.b) + ")");
    }
    // the canvas outside the wide view stays untouched, the view itself is full
    frame(wide, cx, cy);
    check(rig.painted(16, 21, 762, 500) && rig.untouched(778, 0, 182, 540) && rig.untouched(0, 521, 960, 19) && rig.untouched(0, 0, 16, 540) && rig.untouched(0, 0, 960, 21), "the wide frame is full inside the view and untouched outside it");
    // the view's corner is a number of the layout too: a view at (40, 30) shows exactly the picture of the view at (16, 21), moved
    const std::vector<uint8_t> at_corner = rig.read(16, 21, 762, 500);
    ScreenLayout shifted = wide;
    shifted.view_x = 40;
    shifted.view_y = 30;
    frame(shifted, cx, cy);
    check(rig.read(40, 30, 762, 500) == at_corner, "a view whose corner is (40, 30) draws the same picture as the view at (16, 21): the origin is read from the layout at every site");
    check(rig.untouched(0, 0, 40, 540) && rig.untouched(0, 0, 960, 30) && rig.untouched(802, 0, 158, 540) && rig.untouched(0, 530, 960, 10), "... and nothing outside it");
}

// =====================================================================================================================================================
// The application
// =====================================================================================================================================================

/// Pointer events of a headless application's window, pushed through its own event loop
struct AppRig {
    Application& app;
    SDL_Window* window{nullptr};
    uint32_t id{0};

    explicit AppRig(Application& a) : app(a), window(find_window()), id(window != nullptr ? SDL_GetWindowID(window) : 0) {}
    // the application's window: the only one that exists (the ids of earlier windows of this program are gone)
    static SDL_Window* find_window() {
        for (uint32_t i = 1; i < 256; ++i) {
            if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
        }
        return nullptr;
    }
    // a motion at a canvas position (what SDL maps back to the canvas position)
    void motion_at_canvas(float cx, float cy) const {
        int wx = 0;
        int wy = 0;
        SDL_RenderLogicalToWindow(app.renderer().get_sdl_renderer(), cx, cy, &wx, &wy);
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = id;
        e.motion.x = wx;
        e.motion.y = wy;
        SDL_PushEvent(&e);
    }
    void motion_at_window(int wx, int wy) const {
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = id;
        e.motion.x = wx;
        e.motion.y = wy;
        SDL_PushEvent(&e);
    }
    void window_event(Uint8 what) const {
        SDL_Event e{};
        e.type = SDL_WINDOWEVENT;
        e.window.windowID = id;
        e.window.event = what;
        SDL_PushEvent(&e);
    }
    void deliver() const { app.run_frame_with_delta(0.001f); }       // (a headless application stops after 10 frames: this test delivers fewer)
    void scroll(int ticks) const {
        for (int i = 0; i < ticks; ++i) app.handle_camera_panning(0.020f);
    }
};

void test_application() {
    group("app", "the application gives every consumer one layout: renderer, HUD, listener, pointer limits and gate, the plate");
    QuietStdout quiet;
    Application app;
    ApplicationConfig cfg;
    cfg.headless = true;
    cfg.start_in_map_select = false;
    if (!app.init(cfg) || !app.start_game("Original-Ants/Maps/SMALL.LVL")) {
        check(false, "the application starts a match");
        return;
    }
    app.hud().update(app.sim().get_world_state(), 100);                // the get-ready dialog is gone: the edges scroll
    check(app.layout().is_classic() && app.renderer().layout() == app.layout() && app.hud().layout() == app.layout(), "a new application: renderer and HUD hold the application's layout, the classic one");
    check(app.renderer().canvas_w() == 640 && app.renderer().canvas_h() == 480, "... and the canvas is 640 x 480");
    app.update_simulation(0.05f);
    app.renderer().camera().world_x = 100;
    app.renderer().camera().world_y = 200;
    app.update_simulation(0.05f);
    check(app.audio_mixer().listener_x() == 100 + 221 && app.audio_mixer().listener_y() == 200 + 220, "classic: the sound listener is the centre of the 442 x 440 view");

    // a layout of 960 x 540 in a canvas of 960 x 540
    const ScreenLayout layout = ScreenLayout::with_size(960, 540);
    app.renderer().set_canvas_size(960, 540);
    app.set_layout(layout);
    check(app.layout() == layout && app.renderer().layout() == layout && app.hud().layout() == layout, "set_layout reaches the application, the renderer and the HUD");
    check(app.renderer().camera().viewport_w == 762 && app.renderer().camera().viewport_h == 500, "... and the camera");
    app.renderer().camera().world_x = 100;
    app.renderer().camera().world_y = 200;
    app.update_simulation(0.05f);
    check(app.audio_mixer().listener_x() == 100 + 381 && app.audio_mixer().listener_y() == 200 + 250, "960 x 540: the sound listener is the centre of the 762 x 500 view");

    const AppRig rig(app);
    check(rig.window != nullptr, "the application has a window");
    if (rig.window == nullptr) return;
    auto center = [&]() { app.renderer().camera().center_on(600, 600, app.sim().grid().width(), app.sim().grid().height()); };
    // the pointer's limits are the layout's picture: 959 x 539, not 639 x 479
    rig.motion_at_canvas(700.5f, 400.5f);
    rig.deliver();
    check(app.mouse_screen_x() == 700 && app.mouse_screen_y() == 400, "960 x 540: a pointer at (700, 400), beyond the classic screen, is where it is");
    rig.motion_at_window(40000, 40000);
    rig.deliver();
    check(app.mouse_screen_x() == 959 && app.mouse_screen_y() == 539, "960 x 540: a pointer far beyond the window is on (959, 539)");
    rig.motion_at_window(-40000, -40000);
    rig.deliver();
    check(app.mouse_screen_x() == 0 && app.mouse_screen_y() == 0, "960 x 540: far above and left: (0, 0)");
    // the pointer gate of the input task follows the layout: the east strip at the right edge of the 960 wide picture scrolls the view, which the classic gate (x < 640) would not let by
    center();
    const int32_t x0 = app.renderer().camera().world_x;
    rig.motion_at_canvas(955.5f, 270.5f);
    rig.deliver();
    check(app.mouse_screen_x() == 955, "960 x 540: the pointer is on (955, 270)");
    rig.scroll(12);
    check(app.renderer().camera().world_x > x0, "960 x 540: the east strip at the right edge scrolls the view east");
    center();
    const int32_t x1 = app.renderer().camera().world_x;
    rig.motion_at_canvas(639.5f, 270.5f);
    rig.deliver();
    rig.scroll(12);
    check(app.renderer().camera().world_x == x1, "960 x 540: (639, 270), the classic east strip, is quiet");
    // the same canvas with the classic layout: the pointer is held to 639 x 479 and the old strips work again
    app.set_layout(ScreenLayout::classic());
    rig.motion_at_window(40000, 40000);
    rig.deliver();
    check(app.mouse_screen_x() == 639 && app.mouse_screen_y() == 479, "classic layout: a pointer far beyond is on (639, 479)");
    check(app.renderer().camera().viewport_w == 442 && app.hud().layout().is_classic(), "... with the classic view");

    // every size event of the window applies SDL's logical size again: a canvas size that was lost is back
    SDL_RenderSetLogicalSize(app.renderer().get_sdl_renderer(), 100, 100);
    rig.window_event(SDL_WINDOWEVENT_SIZE_CHANGED);
    rig.deliver();
    int lw = 0;
    int lh = 0;
    SDL_RenderGetLogicalSize(app.renderer().get_sdl_renderer(), &lw, &lh);
    check(lw == 960 && lh == 540, "a size event of the window applies the canvas size again");

    // the start view of a match shows the square around the hill by the layout's view size: the larger view needs less scrolling
    {
        app.set_layout(layout);
        const int32_t map_w = static_cast<int32_t>(app.sim().grid().width());
        const int32_t map_h = static_cast<int32_t>(app.sim().grid().height());
        auto start_origin = [&](uint8_t team, const ScreenLayout& l, int32_t& ox, int32_t& oy) {
            const auto* base = app.sim().grid().find_anthill(team);
            ox = -1;
            oy = -1;
            if (base != nullptr) start_view_origin(base->x + 1, base->y + 1, map_w, map_h, ox, oy, l);
        };
        int team = -1;                                                  // a team whose hill is far enough from the map's corner for the two views to differ
        for (uint8_t t = 0; t < 4 && team < 0; ++t) {
            int32_t cx = 0, cy = 0, wx = 0, wy = 0;
            start_origin(t, ScreenLayout::classic(), cx, cy);
            start_origin(t, layout, wx, wy);
            if (cx != wx || cy != wy) team = t;
        }
        check(team >= 0, "SMALL has a hill whose start view depends on the view's size");
        if (team >= 0) {
            int32_t cx = 0, cy = 0, wx = 0, wy = 0;
            start_origin(static_cast<uint8_t>(team), ScreenLayout::classic(), cx, cy);
            start_origin(static_cast<uint8_t>(team), layout, wx, wy);
            app.set_local_player(static_cast<uint8_t>(team));           // it shows the start view
            check(app.renderer().camera().world_x == wx && app.renderer().camera().world_y == wy, "960 x 540: the start view is the one of the 762 x 500 view");
            check(app.renderer().camera().world_x != cx || app.renderer().camera().world_y != cy, "... and not the classic view's");
        }
    }

    // the corner plate (the version next to the frame rate) is in the bottom right corner of the canvas, whatever the picture is: the sparkline's bars are green
    // pixels; the classic corner (x 514 .. 632, rows 467 .. 479) has none in a 960 x 540 canvas, the canvas's own corner (x 834 .. 952, rows 527 .. 539) has them
    app.set_layout(layout);
    app.render_frame();
    SDL_Renderer* sr = app.renderer().get_sdl_renderer();
    auto green_bars = [&](int32_t x, int32_t y, int32_t w, int32_t h) {
        std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
        SDL_Rect r{x, y, w, h};
        SDL_RenderReadPixels(sr, &r, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
        int count = 0;
        for (size_t i = 0; i + 3 < px.size(); i += 4) count += (px[i + 1] >= 185 && px[i] < 100 && px[i + 2] < 120) ? 1 : 0;
        return count;
    };
    // (the window is 1280 x 960: the canvas is scaled by 4 / 3 and centred, so a rectangle of the canvas is read in window pixels: the picture's origin is (0, 120))
    int lx = 0;
    int ly = 0;
    int rx = 0;
    int ry = 0;
    SDL_RenderLogicalToWindow(sr, 834.0f, 527.0f, &lx, &ly);
    SDL_RenderLogicalToWindow(sr, 952.0f, 540.0f, &rx, &ry);
    check(green_bars(lx, ly, rx - lx, ry - ly) > 20, "960 x 540: the sparkline is in the canvas's bottom right corner");
    SDL_RenderLogicalToWindow(sr, 514.0f, 467.0f, &lx, &ly);
    SDL_RenderLogicalToWindow(sr, 632.0f, 480.0f, &rx, &ry);
    check(green_bars(lx, ly, rx - lx, ry - ly) == 0, "... and not where the 640 x 480 picture's corner would be");
    app.shutdown();
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;                                     // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    ensure_sdl();
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }

    test_classic_numbers();
    test_sized_layouts();
    test_anchoring_invariants();
    test_original_strips();
    test_edge_scroll_follows_layout();
    test_pointer_limits();
    test_plate_and_overlay();
    test_hud_classic_rectangles();
    test_hud_anchoring();
    test_hud_layout_survives_init();
    test_hud_pedestal_zones();
    test_hud_orders_in_wide_layout();
    test_hud_drawing(arc);
    test_hud_pointer_follows_layout(arc);
    test_chat_follows_layout();
    test_ctrl_n_follows_layout();
    test_caret_limit();
    test_cursor_follows_layout();
    test_camera();
    test_renderer(arc);
    test_renderer_world_window(arc);
    test_application();

    std::printf("\nscreen layout: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
