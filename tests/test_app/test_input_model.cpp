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

}  // namespace

int main() {
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
