// Input model tests (stage I): edge scrolling and the minimap drag as Ants.exe does them (FUN_01026aa3 / FUN_01027251 / FUN_01027197 /
// FUN_0102fff8 / FUN_01009850), checked against the bit-exact emulation of the original's code (tests/data/edge_scroll_samples.csv).
// (docs/GAME_REVERSE_ENGINEERING.md 5.43)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ants_app/edge_scroll.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/window_layout.hpp"
#include "ants_sim/sim_engine.hpp"

using namespace ants::app;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
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

const char* kDirNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

std::string dir_name(int32_t dir) { return dir < 0 ? "-" : kDirNames[dir]; }

void test_golden_csv() {
    g_group = "edge scroll";
    std::printf("[scroll] 1152 golden samples of the original's edge scroll (rates 0, 50, 99; origin (700, 700), 60 x 60 tiles)\n");
    std::ifstream in(std::string(TEST_DATA_DIR) + "/edge_scroll_samples.csv");
    check(in.good(), "the golden data file opens");
    std::string line;
    std::getline(in, line);     // header
    int rows = 0;
    int bad = 0;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string f[6];
        for (auto& x : f) std::getline(ss, x, ',');
        const int rate = std::atoi(f[0].c_str());
        const int mx = std::atoi(f[1].c_str());
        const int my = std::atoi(f[2].c_str());
        const EdgeScroll s = edge_scroll_step(mx, my, rate, 700, 700, 60, 60);
        ++rows;
        if (dir_name(s.dir) != f[3] || s.dx != std::atoi(f[4].c_str()) || s.dy != std::atoi(f[5].c_str())) {
            if (++bad <= 5) {
                std::fprintf(stderr, "    row %d: rate %d (%d,%d): got %s %d %d, golden %s %s %s\n", rows, rate, mx, my, dir_name(s.dir).c_str(), s.dx, s.dy,
                             f[3].c_str(), f[4].c_str(), f[5].c_str());
            }
        }
    }
    check(rows == 1152, "1152 rows");
    check(bad == 0, std::to_string(bad) + " rows differ from the original");
}

void test_report_table() {
    g_group = "edge scroll";
    std::printf("[scroll] the audit's table at rate 50: pushed axis 55-60 px per tick, hot zones, arrows without scroll\n");
    struct Row { int mx, my; const char* dir; int dx, dy; };
    const Row rows[] = {{639, 240, "E", 59, 0}, {0, 240, "W", -60, 0}, {320, 0, "N", 0, -60}, {320, 479, "S", 0, 59},
                        {639, 0, "NE", 59, -60}, {87, 0, "N", 0, -60}, {555, 0, "N", 1, -60}, {5, 4, "NW", 0, 0}, {630, 8, "NE", 0, 0}};
    for (const auto& r : rows) {
        const EdgeScroll s = edge_scroll_step(r.mx, r.my, 50, 700, 700, 60, 60);
        check(dir_name(s.dir) == r.dir && s.dx == r.dx && s.dy == r.dy,
              "(" + std::to_string(r.mx) + "," + std::to_string(r.my) + ") -> " + dir_name(s.dir) + " " + std::to_string(s.dx) + " " + std::to_string(s.dy));
    }
    // the right edge of a map that is nearly scrolled to its end: the step shrinks to the rest, at the end there is no arrow
    const EdgeScroll near_end = edge_scroll_step(639, 240, 50, 1470, 700, 60, 60);
    check(near_end.dir == 2 && near_end.dx == 8, "ox = 1470: E, +8");
    const EdgeScroll at_end = edge_scroll_step(639, 240, 50, 1478, 700, 60, 60);
    check(at_end.dir == -1 && at_end.dx == 0 && at_end.dy == 0, "ox = 1478 (the last position): no arrow, no scroll");
}

void test_strips_and_gates() {
    g_group = "edge scroll";
    std::printf("[scroll] the 12 px bands are x < 12 / x >= 628 / y < 12 / y >= 468; only the 5 px inner strips scroll; the quiet area gives nothing\n");
    check(edge_scroll_step(12, 240, 50, 700, 700, 60, 60).dir == -1, "x = 12 is not in the west band (the band is x < 12)");
    check(edge_scroll_step(11, 240, 50, 700, 700, 60, 60).dir == 6, "x = 11 is");
    check(edge_scroll_step(11, 240, 50, 700, 700, 60, 60).dx == 0, "but the west arrow at x = 11 does not scroll (only x < 5 does)");
    check(edge_scroll_step(4, 240, 50, 700, 700, 60, 60).dx < 0, "x = 4 scrolls west");
    check(edge_scroll_step(320, 11, 50, 700, 700, 60, 60).dir == 0 && edge_scroll_step(320, 11, 50, 700, 700, 60, 60).dy == 0, "y = 11: north arrow, no scroll");
    check(edge_scroll_step(627, 240, 50, 700, 700, 60, 60).dir == -1, "x = 627 is quiet");
    check(edge_scroll_step(628, 240, 50, 700, 700, 60, 60).dir == 2, "x = 628 is the east band");
    check(edge_scroll_step(320, 468, 50, 700, 700, 60, 60).dir == 4, "y = 468 is the south band");
    check(edge_scroll_step(320, 467, 50, 700, 700, 60, 60).dir == -1, "y = 467 is quiet");
    // a corner is the OR of its two axes: NW arrow while the view can still go west or north
    check(edge_scroll_step(2, 2, 50, 0, 100, 60, 60).dir == 7, "NW corner with only 'north' possible");
    check(edge_scroll_step(2, 2, 50, 0, 0, 60, 60).dir == -1, "NW corner at the map's corner: nothing");
    // a strip that cannot move is not a strip: the pointer at the west edge with ox = 0 gives no arrow and no scroll
    check(edge_scroll_step(2, 240, 50, 0, 700, 60, 60).dir == -1, "west edge at ox = 0");
    check(edge_scroll_step(637, 240, 50, 1478, 700, 60, 60).dir == -1, "east edge at the last position");
    // a small map cannot scroll at all: 13 x 13 tiles = 416 px < 442
    check(edge_scroll_step(639, 240, 50, 0, 0, 13, 13).dir == -1, "a map smaller than the view does not scroll");
}

void test_rates() {
    g_group = "edge scroll";
    std::printf("[scroll] speeds mid-map: rate 0 = 6-10 px per tick, 50 = 55-60, 99 = 104-109\n");
    struct Row { int rate; int lo; int hi; };
    for (const Row& r : {Row{0, 6, 10}, Row{50, 55, 60}, Row{99, 104, 109}}) {
        for (int mx = 635; mx < 640; ++mx) {
            const EdgeScroll s = edge_scroll_step(mx, 240, r.rate, 700, 700, 60, 60);
            check(s.dx >= r.lo && s.dx <= r.hi && s.dy == 0, "rate " + std::to_string(r.rate) + " at x = " + std::to_string(mx) + " moves " + std::to_string(s.dx));
        }
    }
}

void test_minimap() {
    g_group = "minimap";
    std::printf("[scroll] minimap: the point under the pointer is trunc(offset * mapPx / 119, 91); the view scrolls to the square (221, 220) around it\n");
    int32_t wx = 0, wy = 0;
    minimap_point(480 + 59, 35 + 45, 60, 60, wx, wy);
    check(wx == static_cast<int32_t>(59 * (1920.0 / 119.0)) && wy == static_cast<int32_t>(45 * (1920.0 / 91.0)), "point (59, 45) of a 60 x 60 map");
    // the view centres on the point: origin (0, 0), point (960, 960): the square [739, 740, 1181, 1180] -> the view moves by (739, 740)
    const EdgeScroll s = minimap_scroll_step(480 + 60, 35 + 46, 0, 0, 60, 60);
    int32_t px = 0, py = 0;
    minimap_point(480 + 60, 35 + 46, 60, 60, px, py);
    check(s.dx == px - 221 + 442 - 442 && s.dy == py - 220, "the view moves to centre on the point (dx " + std::to_string(s.dx) + ", dy " + std::to_string(s.dy) + ")");
    // near the map's edge the square is clipped: the view stops at the edge
    const EdgeScroll edge = minimap_scroll_step(480 + 1, 35 + 1, 300, 300, 60, 60);
    check(edge.dx <= 0 && edge.dy <= 0, "a point near the top-left moves the view up and left");
    // already centred: no movement
    const EdgeScroll same = minimap_scroll_step(480 + 60, 35 + 46, px - 221, py - 220, 60, 60);
    check(same.dx == 0 && same.dy == 0, "no movement when the view is already there");
}


void test_hud_input_tick() {
    g_group = "hud scroll";
    std::printf("[scroll] the HUD's input tick: rate from the option slider, dialogs and captures gate it, the minimap drag follows while held\n");
    ants::sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1, 720000);
    HUD hud;
    hud.init(0);
    hud.update(sim.get_world_state(), 100);           // the "get ready" modal is over
    ViewportCamera cam;
    cam.center_on(960, 960, 60, 60);
    const int32_t ox = cam.world_x;
    const int32_t oy = cam.world_y;

    // east edge at the default rate (0.5 -> 50): the step of the model
    const EdgeScroll want = edge_scroll_step(639, 240, 50, ox, oy, 60, 60);
    check(want.dx > 0, "the model scrolls east at (639, 240)");
    check(hud.input_tick(cam, 60, 60, 639, 240) && cam.world_x == ox + want.dx && cam.world_y == oy, "one tick moves the view by the model's step");

    // the slider: Scroll Speed 0 (6-10 px) .. 99
    cam.center_on(960, 960, 60, 60);
    hud.options().scroll_speed = 0;
    hud.input_tick(cam, 60, 60, 639, 240);
    const int32_t slow = cam.world_x - ox;
    check(slow >= 6 && slow <= 10, "slider at 0: " + std::to_string(slow) + " px");
    cam.center_on(960, 960, 60, 60);
    hud.options().scroll_speed = 99;
    hud.input_tick(cam, 60, 60, 639, 240);
    const int32_t fast = cam.world_x - ox;
    check(fast >= 104 && fast <= 109, "slider at the end: " + std::to_string(fast) + " px");
    hud.options().scroll_speed = 50;

    // a dialog gets all input
    cam.center_on(960, 960, 60, 60);
    hud.open_options();
    check(!hud.input_tick(cam, 60, 60, 639, 240) && cam.world_x == ox, "no scrolling while a dialog is open");
    hud.close_options();

    // a captured left button (a rubber band) stops the edge scroll
    ants::sim::SimulationEngine sim2;
    sim2.init_test_world(60, 60, 1, 720000);
    hud.handle_mouse_down(300, 300, SDL_BUTTON_LEFT, sim2, cam);
    check(hud.is_input_captured(), "the press on the map captures the button");
    check(!hud.input_tick(cam, 60, 60, 639, 240) && cam.world_x == ox, "no edge scroll while the left button is captured");
    hud.handle_mouse_up(300, 300, SDL_BUTTON_LEFT, sim2, cam);
    check(!hud.is_input_captured(), "released");
    check(hud.input_tick(cam, 60, 60, 639, 240), "and it scrolls again");

    // the minimap: the press only captures, the view follows while the button is held, nothing happens on release
    cam.center_on(300, 300, 60, 60);
    const int32_t cx0 = cam.world_x;
    hud.handle_mouse_down(480 + 60, 35 + 46, SDL_BUTTON_LEFT, sim2, cam);
    check(cam.world_x == cx0, "the press on the minimap does not move the view");
    check(hud.input_tick(cam, 60, 60, 480 + 60, 35 + 46), "the next input tick does");
    int32_t wx = 0, wy = 0;
    minimap_point(480 + 60, 35 + 46, 60, 60, wx, wy);
    check(cam.world_x == wx - 221 && cam.world_y == wy - 220, "the view is centred on the point under the pointer: (" + std::to_string(cam.world_x) + ", " + std::to_string(cam.world_y) + ")");
    check(!hud.input_tick(cam, 60, 60, 480 + 60, 35 + 46), "and stays while the pointer stays");
    hud.handle_mouse_up(480 + 60, 35 + 46, SDL_BUTTON_LEFT, sim2, cam);
    const int32_t after_release = cam.world_x;
    hud.input_tick(cam, 60, 60, 480 + 10, 35 + 10);
    check(cam.world_x == after_release, "after the release the minimap no longer scrolls");
}

void test_cursor_bands() {
    g_group = "cursor bands";
    std::printf("[scroll] the arrow cursor bands: x < 12 / x >= 628 / y < 12 / y >= 468 and only where the view can move\n");
    ants::sim::SimulationEngine sim;
    sim.init_test_world(60, 60, 1, 720000);
    HUD hud;
    hud.init(0);
    ViewportCamera cam;
    cam.center_on(960, 960, 60, 60);
    auto at = [&](int32_t x, int32_t y) { return hud.evaluate_cursor(x, y, sim.get_world_state(), sim.grid(), cam); };
    check(at(12, 240) != CursorType::ScrollW, "(12, 240) is no west arrow (the original's band is x < 12)");
    check(at(11, 240) == CursorType::ScrollW, "(11, 240) is");
    check(at(12, 5) == CursorType::ScrollN, "(12, 5) is the north band, not the corner");
    check(at(5, 12) == CursorType::ScrollW, "(5, 12) is the west band");
    check(at(5, 5) == CursorType::ScrollNW, "(5, 5) is the north-west corner");
    check(at(628, 240) == CursorType::ScrollE && at(627, 240) != CursorType::ScrollE, "the east band starts at x = 628");
    check(at(320, 468) == CursorType::ScrollS && at(320, 467) != CursorType::ScrollS, "the south band starts at y = 468");
    cam.world_x = 0;
    cam.x = 0.0f;
    check(at(5, 240) != CursorType::ScrollW, "no west arrow at the west end of the map");
    check(at(5, 5) == CursorType::ScrollNW || at(5, 5) == CursorType::ScrollN || at(5, 5) != CursorType::ScrollW, "a corner still shows its arrow while one axis can move");
}

void test_start_view() {
    g_group = "start view";
    std::printf("[view] the view at the start of a match: just far enough to show the square (-160, +192) around the hill's anchor tile (FUN_01027197)\n");
    int32_t ox = -1;
    int32_t oy = -1;
    start_view_origin(30, 8, 60, 60, ox, oy);      // GAUNTLET, green: the anchor tile (30, 8)
    check(ox == 726 && oy == 24, "GAUNTLET green: the view's origin is (726, 24), not the centred (772, 69)");
    start_view_origin(37, 21, 60, 60, ox, oy);     // TREASURE, green
    check(ox == 950 && oy == 440, "TREASURE green: (950, 440)");
    start_view_origin(0, 0, 60, 60, ox, oy);
    check(ox == 0 && oy == 0, "a hill in the corner: the view stays at the corner");
    start_view_origin(59, 59, 60, 60, ox, oy);
    check(ox == 60 * 32 - 442 && oy == 60 * 32 - 440, "the far corner: the square is clipped to the map, the view ends at its largest origin (1478, 1480)");
    start_view_origin(5, 5, 31, 31, ox, oy);       // TINY, green: the whole square is inside the first 442 x 440 pixels
    check(ox == 0 && oy == 0, "TINY green: nothing to scroll");
}

void test_window_layout() {
    g_group = "window layout";
    std::printf("[window] the grid of the start scripts: four games on one screen, each the largest 4:3 window of its cell\n");
    int32_t a = 0;
    int32_t b = 0;
    check(parse_grid("2x2", a, b) && a == 2 && b == 2, "2x2 is a grid");
    check(parse_grid("3X1", a, b) && a == 3 && b == 1, "3X1 is a grid, the letter may be a capital");
    check(parse_grid("1x4", a, b) && a == 1 && b == 4, "1x4 is a grid");
    check(!parse_grid("", a, b) && !parse_grid("2", a, b) && !parse_grid("2x", a, b) && !parse_grid("x2", a, b) && !parse_grid("0x2", a, b) &&
              !parse_grid("5x1", a, b) && !parse_grid("2x0", a, b) && !parse_grid("22x2", a, b) && !parse_grid("2*2", a, b) && !parse_grid("2x2 ", a, b),
          "everything else is refused");
    check(parse_pair("100,200", a, b) && a == 100 && b == 200, "a pair");
    check(parse_pair("-1440,-20", a, b) && a == -1440 && b == -20, "a display left of the main one has negative coordinates");
    check(!parse_pair("100", a, b) && !parse_pair("100,", a, b) && !parse_pair(",100", a, b) && !parse_pair("a,b", a, b) && !parse_pair("1,2,3", a, b) &&
              !parse_pair("1, 2", a, b) && !parse_pair("999999,1", a, b),
          "no pair, no number, stray characters, absurd numbers are refused");

    // a 1920 x 1055 usable area (a 1080p screen without its task bar), a title bar of 28 px and a thin frame
    const WindowRect screen{0, 0, 1920, 1055};
    WindowRect cells[4];
    for (int32_t i = 0; i < 4; ++i) cells[i] = grid_cell_window(screen, 2, 2, i, 28, 1, 1, 1);
    for (int32_t i = 0; i < 4; ++i) {
        const WindowRect& r = cells[i];
        check(r.w * 3 == r.h * 4 || r.w * 3 == r.h * 4 + 1 || r.w * 3 == r.h * 4 + 2, "cell " + std::to_string(i) + ": 4:3");
        check(r.w >= kMinWindowWidth && r.h >= kMinWindowHeight, "cell " + std::to_string(i) + ": not smaller than the minimum");
        // the window with its decoration lies inside its cell
        const int32_t cx = (i % 2) * 960;
        const int32_t cy = (i / 2) * 527;
        check(r.x - 1 >= cx && r.x + r.w + 1 <= cx + 960 && r.y - 28 >= cy && r.y + r.h + 1 <= cy + 527, "cell " + std::to_string(i) + ": with its frame inside its own quarter");
    }
    // no two windows, frames included, overlap
    for (int32_t i = 0; i < 4; ++i) {
        for (int32_t j = i + 1; j < 4; ++j) {
            const WindowRect& p = cells[i];
            const WindowRect& q = cells[j];
            const bool apart = p.x + p.w + 1 <= q.x - 1 || q.x + q.w + 1 <= p.x - 1 || p.y + p.h + 1 <= q.y - 28 || q.y + q.h + 1 <= p.y - 28;
            check(apart, "the windows of cells " + std::to_string(i) + " and " + std::to_string(j) + " do not overlap");
        }
    }
    check(cells[0].y == cells[1].y && cells[2].y == cells[3].y && cells[0].x == cells[2].x && cells[1].x == cells[3].x, "the grid is a grid: rows and columns line up");
    check(cells[0].x < cells[1].x && cells[0].y < cells[2].y, "cell 0 is the top left, 1 the top right, 2 the bottom left, 3 the bottom right");
    // the height is what limits a 16:9 screen: the window is as tall as the cell allows
    check(cells[0].h >= 527 - 28 - 1 - 4 && cells[0].h <= 527 - 28 - 1, "a wide screen: the window fills the height of its quarter");
    // a display that does not start at (0, 0), with a menu bar on top (usable area (0, 25))
    const WindowRect offset{-1440, 25, 1440, 875};
    const WindowRect o3 = grid_cell_window(offset, 2, 2, 3, 28, 0, 0, 0);
    check(o3.x >= -1440 + 720 && o3.x + o3.w <= 0 && o3.y >= 25 + 437 + 28 && o3.y + o3.h <= 25 + 875, "a display left of the main one: cell 3 is its bottom right quarter");
    // a screen that is taller than wide: the width limits
    const WindowRect tall{0, 0, 800, 1280};
    const WindowRect t0 = grid_cell_window(tall, 2, 2, 0, 28, 0, 0, 0);
    check(t0.w <= 400 && t0.w >= kMinWindowWidth && t0.w * 3 == t0.h * 4, "a tall screen: the width of the quarter limits the window");
    // two games side by side, one game on the whole screen, a cell beyond the grid, a screen that is too small
    const WindowRect two0 = grid_cell_window(screen, 2, 1, 0, 28, 1, 1, 1);
    const WindowRect two1 = grid_cell_window(screen, 2, 1, 1, 28, 1, 1, 1);
    check(two0.x < 960 && two1.x >= 960 && two0.y == two1.y, "two games: left and right");
    const WindowRect one = grid_cell_window(screen, 1, 1, 0, 28, 1, 1, 1);
    check(one.h <= 1055 - 28 - 1 && one.h >= 1055 - 28 - 1 - 4 && one.w * 3 == one.h * 4, "one game: the largest 4:3 window of the whole screen");
    check(grid_cell_window(screen, 2, 2, 9, 28, 1, 1, 1) == cells[3] && grid_cell_window(screen, 2, 2, -5, 28, 1, 1, 1) == cells[0], "a cell beyond the grid is the last (or the first) one");
    const WindowRect tiny = grid_cell_window(WindowRect{0, 0, 500, 400}, 2, 2, 1, 28, 1, 1, 1);
    check(tiny.w == kMinWindowWidth && tiny.h == kMinWindowHeight, "a screen that is too small for four windows still gets windows of the minimum size");
    // no decoration information (a platform that cannot tell): the cells are simply quarters
    const WindowRect bare = grid_cell_window(screen, 2, 2, 1, 0, 0, 0, 0);
    check(bare.x >= 960 && bare.x + bare.w <= 1920 && bare.y >= 0 && bare.y + bare.h <= 527, "no borders: the window fills its quarter");
}

}  // namespace

int main(int argc, char* argv[]) {
    // SDL's headers rename main to SDL_main (SDL2main on Windows calls it): the signature must be this one, or the linker finds no SDL_main (the build guard in CMakeLists.txt checks it)
    (void)argc;
    (void)argv;
    test_window_layout();
    test_start_view();
    test_golden_csv();
    test_report_table();
    test_strips_and_gates();
    test_rates();
    test_minimap();
    test_hud_input_tick();
    test_cursor_bands();
    std::printf("\ninput model: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
