// The ant health marks (include/ants_app/health_marks.hpp: the rule, the colors and the sizes; src/ants_app/renderer_health.cpp: the drawing; docs/VIEW_AND_HUD.md, "Ant health marks"):
//   * the rule with no window: which ant wears a mark in each mode (the 10 hit points that every ant has at most, the boundary at 9 and 10, a dead or a frozen ant), the three colors and their
//     edges (the selection ears' own), the size of the bar at every zoom and on a canvas that is shown small, the place of a bar and of a number, the words of the settings file;
//   * the drawing on a real renderer over a map (the software renderer, the dummy video driver), every pixel against numbers written down here and not taken from the code that draws: a bar's
//     frame, track and fill at the zoom 1, 2 and 0.5, the fill of 1 to 10 hit points in its color, a number in its font, colors and outline, the numbers left out below the zoom 0.5 (and shown at every level of the wheel's series from 1 to 0.5), the thicker bar
//     on a canvas that is shown small, no mark in Off, hurt ants only in Hurt, an ant that is dead or frozen with none, the enemies' and the allies' marks, and Fog of War: an ant that the fog hides has none;
//   * the zoom 1 pass drawn straight and through the offscreen target is the same picture with the marks on.
// The setting's storage and Ctrl+L are in test_game_menu and test_pointer_model; the recording's and the spectator's frame in test_replay_app.
// Usage: test_health_marks. Exit code 0 when every check passes.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/health_marks.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;
namespace hm = ants::app::health;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

std::string num(long long v) { return std::to_string(v); }

// ---- the rule ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_rule() {
    std::printf("[rule] who wears a mark, in which color\n");
    for (int max_hp : {10, 0}) {
        for (int hp = -1; hp <= 12; ++hp) {
            const bool alive = hp >= 1;
            const bool hurt = alive && hp < max_hp;
            check(hm::shown(hm::When::Hurt, hp, max_hp, false) == hurt, "Hurt ants: hp " + num(hp) + " of " + num(max_hp) + " is " + (hurt ? "marked" : "not marked"));
            check(hm::shown(hm::When::All, hp, max_hp, false) == alive, "All ants: hp " + num(hp) + " of " + num(max_hp) + " is " + (alive ? "marked" : "not marked"));
            check(!hm::shown(hm::When::Off, hp, max_hp, false), "Off: nobody, hp " + num(hp));
            check(!hm::shown(hm::When::Hurt, hp, max_hp, true) && !hm::shown(hm::When::All, hp, max_hp, true), "a frozen ant wears none, hp " + num(hp));
        }
    }
    check(hm::shown(hm::When::Hurt, 9, 10, false) && !hm::shown(hm::When::Hurt, 10, 10, false), "the boundary: 9 of 10 is hurt, 10 of 10 is not");
    check(hm::shown(hm::When::Hurt, 1, 10, false), "an ant with one hit point left is hurt");

    for (int hp = 1; hp <= 10; ++hp) {
        const hm::Band want = hp <= 2 ? hm::Band::Red : (hp >= 9 ? hm::Band::Green : hm::Band::Amber);
        check(hm::band(hp) == want, "the band of " + num(hp) + " hit points: red 1 and 2, amber 3 to 8, green 9 and 10");
    }
    check(hm::kGreenFrom == 9 && hm::kRedUpTo == 2, "the edges are the selection ears' (dogears from 9, redears up to 2)");
    const hm::Color green = hm::color(hm::Band::Green), amber = hm::color(hm::Band::Amber), red = hm::color(hm::Band::Red);
    check(green.r == 60 && green.g == 214 && green.b == 60 && amber.r == 255 && amber.g == 196 && amber.b == 24 && red.r == 236 && red.g == 44 && red.b == 44, "the three colors");

    check(!hm::number_fits(0.49) && hm::number_fits(0.5) && hm::number_fits(0.59) && hm::number_fits(0.6) && hm::number_fits(2.0) && !hm::number_fits(0.4), "a number is left out below the zoom 0.5, and stays from it");
    for (int k = 0; k >= -4; --k) check(hm::number_fits(static_cast<double>(zoom::series(k))), "the level " + std::to_string(zoom::series(k)) + " of the wheel's series (1 and the three steps out to 0.5) shows its numbers");
    check(!hm::number_fits(static_cast<double>(zoom::series(-5))), "...and the step beyond 0.5 does not");
    check(hm::kNumberMinZoom * 32.0 == 2.0 * hm::kCellW, "the edge is where a 10 (two cells) is as wide as the 32 pixels of a tile");
    check(!hm::thick_for(1.0) && !hm::thick_for(2.0) && hm::thick_for(2.01) && hm::thick_for(3.0), "a canvas shown at less than half its size (more than 2 picture pixels for a CSS pixel) gets thick bars");
}

void test_geometry() {
    std::printf("[geometry] the bar and the number on the screen\n");
    struct H { double zoom; int plain; int thick; };
    for (const H& h : {H{0.3, 2, 3}, H{0.5, 2, 3}, H{0.5946, 2, 3}, H{0.59, 2, 3}, H{0.6, 3, 5}, H{0.84, 3, 5}, H{1.0, 3, 5}, H{1.49, 3, 5}, H{1.5, 5, 8}, H{2.0, 5, 8}}) {
        check(hm::bar_height(h.zoom, false) == h.plain && hm::bar_height(h.zoom, true) == h.thick, "the bar's height at the zoom " + std::to_string(h.zoom) + ": " + num(h.plain) + ", thick " + num(h.thick));
    }
    const hm::Bar b = hm::bar(100, 200, 5, 10, 1.0, false);
    check(b.framed && b.outline.x == 88 && b.outline.y == 165 && b.outline.w == 24 && b.outline.h == 5, "the frame of a bar at the zoom 1: 24 x 5, its first pixel 12 left of the ant and 35 above");
    check(b.track.x == 89 && b.track.y == 166 && b.track.w == 22 && b.track.h == 3, "the track: 22 x 3, its lower edge 31 above the ant");
    check(b.fill.x == 89 && b.fill.y == 166 && b.fill.w == 11 && b.fill.h == 3, "the fill of 5 hit points of 10 is half the track");
    check(b.color.r == 255 && b.color.g == 196, "...in amber");
    const int fills[10] = {2, 4, 7, 9, 11, 13, 15, 18, 20, 22};
    for (int hp = 1; hp <= 10; ++hp) check(hm::bar(0, 0, hp, 10, 1.0, false).fill.w == fills[hp - 1], "the fill of " + num(hp) + " of 10 is " + num(fills[hp - 1]) + " pixels of 22");
    check(hm::bar(0, 0, 1, 1000, 1.0, false).fill.w == 1, "a fill is one pixel at least");
    check(hm::bar(0, 0, 12, 10, 1.0, false).fill.w == 22 && hm::bar(0, 0, 5, 0, 1.0, false).fill.w == 22, "an ant with more than its most (or none given) has a full bar");
    const hm::Bar z2 = hm::bar(100, 200, 10, 10, 2.0, false);
    check(z2.track.w == 44 && z2.track.h == 5 && z2.track.y == 200 - 62 - 5 && z2.outline.w == 46 && z2.outline.h == 7, "at the zoom 2 the bar is 44 x 5 and 62 pixels over the ant, in its frame");
    const hm::Bar z05 = hm::bar(100, 200, 10, 10, 0.5, false);
    check(!z05.framed && z05.track.w == 11 && z05.track.h == 2 && z05.outline.w == 0, "at the zoom 0.5 it is 11 x 2 and has no frame");
    check(hm::bar(100, 200, 10, 10, 0.1, false).track.w == 6, "and never narrower than 6 pixels");
    check(hm::bar(100, 200, 10, 10, 1.0, true).track.h == 5 && hm::bar(100, 200, 10, 10, 0.5, true).framed, "thick: 5 pixels at the zoom 1, and the bar of the far zoom gets its frame");

    const hm::Digits one = hm::number(100, 200, 7, 1.0);
    const hm::Digits two = hm::number(100, 200, 10, 1.0);
    check(one.x == 97 && one.y == 155 && two.x == 93 && two.y == 155, "a number is centered over the ant and 14 above the line 31 above it: one digit at 97, two at 93");
    check(two.covers.w == 18 && two.covers.h == 17 && two.covers.x == 92 && two.covers.y == 154, "...with its outline it covers 18 x 17 pixels");
    check(hm::number(100, 200, 10, 2.0).y == 200 - 62 - 14 && hm::number(100, 200, 10, 0.6).y == 200 - 19 - 14, "its height follows the zoom, its size does not");
    check(hm::overlaps(hm::Rect{0, 0, 10, 10}, hm::Rect{9, 9, 5, 5}) && !hm::overlaps(hm::Rect{0, 0, 10, 10}, hm::Rect{10, 0, 5, 5}) && !hm::overlaps(hm::Rect{0, 0, 10, 10}, hm::Rect{0, -5, 10, 5}), "overlap: touching is not overlapping");
}

void test_words() {
    std::printf("[words] the settings file's words\n");
    for (const hm::When w : {hm::When::Hurt, hm::When::All, hm::When::Off}) {
        hm::When back = hm::When::Off;
        check(hm::parse(hm::word(w), back) && back == w, std::string("round trip of ") + hm::word(w));
    }
    for (const hm::Style s : {hm::Style::Bar, hm::Style::Number}) {
        hm::Style back = hm::Style::Number;
        check(hm::parse(hm::word(s), back) && back == s, std::string("round trip of ") + hm::word(s));
    }
    for (const std::string bad : {"", "HURT", "hurt ", "1", "on", "off ", "numbers"}) {
        hm::When w = hm::When::All;
        hm::Style st = hm::Style::Number;
        check(!hm::parse(bad, w) && w == hm::When::All && !hm::parse(bad, st) && st == hm::Style::Number, "'" + bad + "' is no word and changes nothing");
    }
    check(std::string(hm::word(hm::When::Hurt)) == "hurt" && std::string(hm::word(hm::When::All)) == "all" && std::string(hm::word(hm::When::Off)) == "off" &&
              std::string(hm::word(hm::Style::Bar)) == "bar" && std::string(hm::word(hm::Style::Number)) == "number", "the words of the settings file: hurt, all, off, bar, number");
    check(hm::Setting{}.when == hm::When::Hurt && hm::Setting{}.style == hm::Style::Bar, "the default is a bar on hurt ants");
}

// ---- the drawing ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

sim::AntSnapshot ant(uint32_t id, uint8_t player, int32_t px, int32_t py, int hp, bool frozen = false) {
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = player;
    a.type = sim::AntType::Worker;
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    a.hp = static_cast<uint16_t>(hp);
    a.max_hp = 10;
    a.state = sim::UnitState::Idle;
    a.loco_clip = 0x7FFE;                                         // no sprite: only the marks are drawn
    a.frozen = frozen;
    return a;
}

struct Delta {
    int n{0};
    int x0{1 << 30};
    int y0{1 << 30};
    int x1{-1};
    int y1{-1};
};

/// The pixels of `rect` (clipped to the picture) that differ between two pictures
Delta delta(const Picture& a, const Picture& b, int x0, int y0, int x1, int y1) {
    Delta d;
    for (int y = std::max(0, y0); y <= std::min(a.h - 1, y1); ++y) {
        for (int x = std::max(0, x0); x <= std::min(a.w - 1, x1); ++x) {
            if (std::memcmp(a.at(x, y), b.at(x, y), 3) == 0) continue;
            ++d.n;
            d.x0 = std::min(d.x0, x);
            d.y0 = std::min(d.y0, y);
            d.x1 = std::max(d.x1, x);
            d.y1 = std::max(d.y1, y);
        }
    }
    return d;
}
Delta delta(const Picture& a, const Picture& b) { return delta(a, b, 0, 0, a.w - 1, a.h - 1); }

bool is(const Picture& p, int x, int y, int r, int g, int b) { return p.at(x, y)[0] == r && p.at(x, y)[1] == g && p.at(x, y)[2] == b; }

const hm::Setting kHurtBar{hm::When::Hurt, hm::Style::Bar};
const hm::Setting kAllBar{hm::When::All, hm::Style::Bar};
const hm::Setting kHurtNumber{hm::When::Hurt, hm::Style::Number};
const hm::Setting kOff{hm::When::Off, hm::Style::Bar};

/// The screen position of a world point: the view's corner and the offset from the camera's origin times the zoom
void screen_of(PixelRig& rig, int32_t wx, int32_t wy, double zoom, int& sx, int& sy) {
    const LayoutRect v = rig.layout.view();
    sx = v.x + static_cast<int>(std::floor((static_cast<double>(wx) - static_cast<double>(rig.cam().x)) * zoom));
    sy = v.y + static_cast<int>(std::floor((static_cast<double>(wy) - static_cast<double>(rig.cam().y)) * zoom));
}

void test_bars(const assets::AssetArchive& arc) {
    std::printf("[bars] a bar, pixel for pixel, at three zooms\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.look(500.0, 400.0, 1.0f);
    int sx = 0, sy = 0;
    screen_of(rig, 600, 500, 1.0, sx, sy);

    rig.world.ants = {ant(1, 0, 600, 500, 5)};
    const Picture none = rig.shoot_with(kOff);
    const Picture bar = rig.shoot_with(kHurtBar);
    Delta d = delta(none, bar);
    check(d.n > 100 && d.x0 == sx - 12 && d.x1 == sx + 11 && d.y0 == sy - 35 && d.y1 == sy - 31, "zoom 1: the bar is 24 x 5 pixels at (" + num(sx - 12) + ", " + num(sy - 35) + "): " + num(d.x0) + ".." + num(d.x1) + " x " + num(d.y0) + ".." + num(d.y1));
    bool fill_ok = true, track_ok = true;
    for (int y = sy - 34; y <= sy - 32; ++y) {
        for (int x = sx - 11; x < sx + 11; ++x) {
            if (x < sx) fill_ok = fill_ok && is(bar, x, y, 255, 196, 24);                 // 11 of 22 pixels: 5 of 10 hit points, amber
            else track_ok = track_ok && is(bar, x, y, 40, 40, 40);
        }
    }
    check(fill_ok, "...its fill is 11 amber pixels (255, 196, 24)");
    check(track_ok, "...and the rest of its track is the dark gray (40, 40, 40)");
    int dark = 0;                                                                    // the frame's pixel is the map's, mostly black (230 of 255) over it
    for (int c = 0; c < 3; ++c) dark = std::max(dark, std::abs(static_cast<int>(bar.at(sx - 12, sy - 35)[c]) - static_cast<int>(none.at(sx - 12, sy - 35)[c]) * 25 / 255));
    check(dark <= 2, "...in a frame of black (230 of 255) over the map");

    // the fill and its color for 1 to 10 hit points
    const int fills[10] = {2, 4, 7, 9, 11, 13, 15, 18, 20, 22};
    for (int hp = 1; hp <= 10; ++hp) {
        rig.world.ants = {ant(1, 0, 600, 500, hp)};
        const Picture p = rig.shoot_with(kAllBar);
        const int r = hp <= 2 ? 236 : (hp >= 9 ? 60 : 255), g = hp <= 2 ? 44 : (hp >= 9 ? 214 : 196), b = hp <= 2 ? 44 : (hp >= 9 ? 60 : 24);
        const int x_first = sx - 11, x_last = sx - 11 + fills[hp - 1] - 1;
        bool colored = true;
        for (int x = x_first; x <= x_last; ++x) colored = colored && is(p, x, sy - 33, r, g, b);
        check(colored, num(hp) + " hit points: " + num(fills[hp - 1]) + " pixels in (" + num(r) + ", " + num(g) + ", " + num(b) + ")");
        check(x_last == sx + 10 || is(p, x_last + 1, sy - 33, 40, 40, 40), num(hp) + " hit points: the track follows the fill");
    }

    // the modes
    rig.world.ants = {ant(1, 0, 600, 500, 10)};
    check(delta(rig.shoot_with(kOff), rig.shoot_with(kHurtBar)).n == 0, "Hurt ants: an ant with 10 of 10 has no bar");
    const Picture full = rig.shoot_with(kAllBar);
    d = delta(rig.shoot_with(kOff), full);
    bool green = true;
    for (int x = sx - 11; x < sx + 11; ++x) green = green && is(full, x, sy - 33, 60, 214, 60);
    check(d.n > 100 && green, "All ants: the same ant has a full green bar");
    rig.world.ants = {ant(1, 0, 600, 500, 1)};
    check(delta(rig.shoot_with(kOff), rig.shoot_with(kOff)).n == 0, "(the pictures of one scene are the same)");
    check(delta(rig.shoot_with(kOff), rig.shoot_with(hm::Setting{hm::When::Off, hm::Style::Number})).n == 0, "Off: no mark, in either look, on the ant with one hit point");

    // zoom 2: a bar of 44 x 5, 62 pixels above the ant
    rig.world.ants = {ant(1, 0, 600, 500, 10)};
    rig.look(500.0, 400.0, 2.0f);
    screen_of(rig, 600, 500, 2.0, sx, sy);
    const Picture n2 = rig.shoot_with(kOff);
    const Picture b2 = rig.shoot_with(kAllBar);
    d = delta(n2, b2);
    check(d.n > 200 && d.x0 == sx - 23 && d.x1 == sx + 22 && d.y0 == sy - 68 && d.y1 == sy - 62, "zoom 2: 46 x 7 pixels with the frame, the lower edge 62 above the ant: " + num(d.x0) + ".." + num(d.x1) + " x " + num(d.y0) + ".." + num(d.y1));
    bool full2 = true;
    for (int y = sy - 67; y <= sy - 63; ++y)
        for (int x = sx - 22; x < sx + 22; ++x) full2 = full2 && is(b2, x, y, 60, 214, 60);
    check(full2, "...and 44 x 5 of green inside it");

    // zoom 0.5: 11 x 2 pixels, no frame (the track is see-through: only its extent is looked at), the fill opaque
    rig.world.ants = {ant(1, 0, 600, 500, 5)};
    rig.look(500.0, 400.0, 0.5f);
    screen_of(rig, 600, 500, 0.5, sx, sy);
    const Picture n05 = rig.shoot_with(kOff);
    const Picture b05 = rig.shoot_with(kHurtBar);
    d = delta(n05, b05);
    check(d.n > 10 && d.x0 == sx - 5 && d.x1 == sx + 5 && d.y0 == sy - 18 && d.y1 == sy - 17, "zoom 0.5: 11 x 2 pixels, the lower edge 16 above the ant: " + num(d.x0) + ".." + num(d.x1) + " x " + num(d.y0) + ".." + num(d.y1));
    bool fill05 = true;
    for (int y = sy - 18; y <= sy - 17; ++y)
        for (int x = sx - 5; x < sx - 5 + 6; ++x) fill05 = fill05 && is(b05, x, y, 255, 196, 24);
    check(fill05, "...with 6 pixels of amber for 5 hit points");
}

void test_numbers(const assets::AssetArchive& arc) {
    std::printf("[numbers] a number, its font, its color and its outline; left out below the zoom 0.5\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.world.ants = {ant(1, 0, 600, 500, 7)};
    for (const float z : {1.0f, 2.0f, 0.7f, 0.5946f, 0.5f, 0.49f, 0.25f}) {
        rig.look(500.0, 400.0, z);
        int sx = 0, sy = 0;
        screen_of(rig, 600, 500, z, sx, sy);
        const Picture none = rig.shoot_with(kOff);
        const Picture with = rig.shoot_with(kHurtNumber);
        // the reference: "7" in the fixed font, amber, outlined by black on the eight sides, alone on magenta
        rig.renderer.begin_frame();
        rig.renderer.fill_rect(0, 0, kWideW, kWideH, ants::assets::ColorRGBA{255, 0, 255, 255});
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx != 0 || dy != 0) rig.renderer.draw_fixed_text("7", 100 + dx, 100 + dy, ants::assets::ColorRGBA{0, 0, 0, 255});
        rig.renderer.draw_fixed_text("7", 100, 100, ants::assets::ColorRGBA{255, 196, 24, 255});
        const Picture ref = rig.read();
        auto in_ref = [&](int x, int y) { return x >= 0 && y >= 0 && x < ref.w && y < ref.h && !is(ref, x, y, 255, 0, 255); };
        int ref_n = 0;
        for (int y = 0; y < ref.h; ++y)
            for (int x = 0; x < ref.w; ++x) ref_n += in_ref(x, y) ? 1 : 0;
        const int nx = sx - 4 + 1;                                                          // one digit: centered, 4 left of the ant and one to the right
        const int ny = sy - static_cast<int>(std::lround(31.0 * static_cast<double>(z))) - 14;
        int changed = 0, wrong = 0, amber = 0;
        for (int y = 0; y < with.h; ++y) {
            for (int x = 0; x < with.w; ++x) {
                if (std::memcmp(with.at(x, y), none.at(x, y), 3) == 0) continue;
                ++changed;
                const int rx = x - nx + 100, ry = y - ny + 100;
                if (!in_ref(rx, ry) || std::memcmp(with.at(x, y), ref.at(rx, ry), 3) != 0) ++wrong;
                if (is(with, x, y, 255, 196, 24)) ++amber;
            }
        }
        const std::string at = "zoom " + std::to_string(z) + ": ";
        if (z < 0.5f) {
            check(changed == 0, at + "no number below the zoom 0.5 (" + num(changed) + " pixels)");
        } else {
            check(ref_n > 40 && changed > ref_n * 9 / 10 && changed <= ref_n && wrong == 0 && amber > 10, at + "the number is the fixed font's 7, amber with a black outline, the same " + num(ref_n) + " pixels at every zoom, over the ant (changed " + num(changed) + ", wrong " + num(wrong) + ")");
        }
    }
    // a number is a number even for an ant that a bar would show with one pixel: 10 shows two digits, each in the green of 9 and 10
    rig.world.ants = {ant(1, 0, 600, 500, 10)};
    rig.look(500.0, 400.0, 1.0f);
    int sx = 0, sy = 0;
    screen_of(rig, 600, 500, 1.0, sx, sy);
    const Delta ten = delta(rig.shoot_with(kOff), rig.shoot_with(hm::Setting{hm::When::All, hm::Style::Number}));
    check(ten.n > 40 && ten.x0 >= sx - 8 && ten.x1 <= sx + 8 && ten.y1 <= sy - 31 && ten.y0 >= sy - 46, "All ants, Number: a 10 is two digits over the ant, inside 16 x 17 pixels");
}

void test_which_ants(const assets::AssetArchive& arc) {
    std::printf("[ants] which ants wear a mark: hurt and all, own and enemy, dead and frozen, and the fog\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.look(500.0, 400.0, 1.0f);
    // six ants in a row, 80 pixels apart: A (own, 10), B (own, 4), C (enemy, 4), D (dead), E (frozen, 4), F (enemy, 10)
    const int32_t xs[6] = {540, 620, 700, 780, 860, 940};
    rig.world.ants = {ant(1, 0, xs[0], 560, 10), ant(2, 0, xs[1], 560, 4), ant(3, 1, xs[2], 560, 4), ant(4, 0, xs[3], 560, 0), ant(5, 0, xs[4], 560, 4, true), ant(6, 1, xs[5], 560, 10)};
    Picture none;
    auto marked = [&](const Picture& p, int i) {
        int sx = 0, sy = 0;
        screen_of(rig, xs[i], 560, 1.0, sx, sy);
        return delta(none, p, sx - 14, sy - 40, sx + 14, sy - 28).n > 20;
    };
    auto row = [&](hm::Setting s) {
        none = rig.shoot_with(kOff);                                                 // (the same scene without marks: with the fog, the fog's picture)
        const Picture p = rig.shoot_with(s);
        std::string out;
        for (int i = 0; i < 6; ++i) out += marked(p, i) ? 'X' : '.';
        return out;
    };
    check(row(kHurtBar) == ".XX...", "Hurt ants: the hurt ants, own and enemy; not the full ones, the dead or the frozen one: " + row(kHurtBar));
    check(row(kAllBar) == "XXX..X", "All ants: every living ant that is not frozen: " + row(kAllBar));
    check(row(kOff) == "......", "Off: none");
    check(row(kHurtNumber) == ".XX...", "Hurt ants as numbers: the same ants: " + row(kHurtNumber));
    check(row(hm::Setting{hm::When::All, hm::Style::Number}) == "XXX..X", "All ants as numbers: the same ants: " + row(hm::Setting{hm::When::All, hm::Style::Number}));

    // Fog of War: the enemy ants on ground that is not explored are not drawn, and wear no mark; the player's own ants always do
    rig.world.fog_of_war_enabled = true;
    rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
    check(row(kAllBar) == "XX....", "under fog, nothing explored: the own ants wear their marks, the enemies' (C, F) do not: " + row(kAllBar));
    rig.world.fog_revealed[static_cast<size_t>(560 / 32) * rig.world.width + static_cast<size_t>(xs[2] / 32)] = 1;
    check(row(kAllBar) == "XXX...", "the tile of the enemy C explored: C wears its mark again, F (not explored) does not: " + row(kAllBar));
    rig.world.fog_of_war_enabled = false;
    check(row(kAllBar) == "XXX..X", "(the fog off: the marks as before)");

    // an ally (the alliance holds both ways) is not an enemy: its hurt ants wear marks, with or without the fog; an alliance that one side does not return is none
    rig.world.player_alliances = {1, 0, 4, 4};
    rig.world.ants = {ant(1, 0, xs[0], 560, 4), ant(2, 1, xs[1], 560, 4), ant(3, 2, xs[2], 560, 4)};
    check(row(kHurtBar) == "XXX...", "own, allied and other team's ants, 4 of 10: all three wear a bar: " + row(kHurtBar));
    rig.world.fog_of_war_enabled = true;
    rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
    check(row(kHurtBar) == "XX....", "under fog, nothing explored: the own and the allied ant wear a bar, the other team's does not: " + row(kHurtBar));
    rig.world.player_alliances = {1, 4, 4, 4};
    check(row(kHurtBar) == "X.....", "under fog, an alliance that team 1 does not return is none: its ant is hidden and wears no bar: " + row(kHurtBar));
    rig.world.fog_of_war_enabled = false;
    rig.world.player_alliances = {4, 4, 4, 4};

    // an ant whose most is not 10 (a rule that may come): hurt is below its own most
    rig.world.ants = {ant(1, 0, xs[0], 560, 10)};
    rig.world.ants[0].max_hp = 12;
    check(row(kHurtBar) == "X.....", "an ant with 10 of 12 is hurt: its own most counts, not a constant: " + row(kHurtBar));
}

void test_thick_and_target(const assets::AssetArchive& arc) {
    std::printf("[thick] a canvas that is shown small gets thicker bars; the zoom 1 pass through the target is the same picture\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.look(500.0, 400.0, 1.0f);
    int sx = 0, sy = 0;
    screen_of(rig, 600, 500, 1.0, sx, sy);
    rig.world.ants = {ant(1, 0, 600, 500, 5)};
    const Picture none = rig.shoot_with(kOff);
    struct T { double per_css; int outline_h; };
    for (const T& t : {T{1.0, 5}, T{2.0, 5}, T{2.5, 7}, T{4.0, 7}}) {
        const Delta d = delta(none, rig.shoot_with(kHurtBar, t.per_css));
        check(d.n > 100 && d.y1 == sy - 31 && d.y1 - d.y0 + 1 == t.outline_h && d.x0 == sx - 12 && d.x1 == sx + 11, "a picture shown at " + std::to_string(t.per_css) + " pixels for a CSS pixel: the bar with its frame is " + num(t.outline_h) + " pixels high: " + num(d.y1 - d.y0 + 1));
    }
    const Delta shown_small = delta(none, rig.shoot_with(kHurtNumber, 4.0));
    const Delta shown_big = delta(none, rig.shoot_with(kHurtNumber, 1.0));
    check(shown_small.n == shown_big.n && shown_small.n > 40, "a number is the same size whatever the page shows");

    // the zoom 1 pass drawn straight into the view and through the offscreen target
    sim::WorldState& world = rig.world;
    world.ants.clear();
    for (int i = 0; i < 40; ++i) world.ants.push_back(ant(static_cast<uint32_t>(10 + i), static_cast<uint8_t>(i % 3), 520 + (i % 8) * 70, 440 + (i / 8) * 60, 1 + i % 10));
    for (const hm::Setting s : {kHurtBar, kAllBar, kHurtNumber}) {
        rig.renderer.set_force_world_target(false);
        const Picture direct = rig.shoot_with(s);
        rig.renderer.set_force_world_target(true);
        const uint64_t passes = rig.renderer.world_target_passes();
        const Picture target = rig.shoot_with(s);
        rig.renderer.set_force_world_target(false);
        check(rig.renderer.world_target_passes() == passes + 1, "the forced pass went through the offscreen target");
        check(differ(direct, target, rig.layout.view()) == 0 && differ(direct, rig.shoot_with(kOff), rig.layout.view()) > 500, std::string("zoom 1, ") + hm::word(s.when) + " " + hm::word(s.style) + ": the target gives the direct picture, which has marks");
    }
}

// The zooms that a player is at: a match starts at the zoom 1 (Application::choose_zoom: no remembered zoom), and the wheel goes out one level a notch (view_zoom.hpp: the series 2^(k/4) and the map's own
// limit). A number is on the picture at every one of them as far as numbers go, as the bar is.
void test_play_zooms(const assets::AssetArchive& arc) {
    std::printf("[play zooms] the number at the zoom that a match starts at and at every notch of the wheel out from it\n");
    for (const char* map : {"MEDIUM", "GAUNTLET"}) {
        PixelRig rig(arc, map, kWideW, kWideH);
        check(rig.ok, std::string(map) + ": the rig is up");
        if (!rig.ok) continue;
        zoom::Fit fit;
        fit.view_w = rig.layout.view().w;
        fit.view_h = rig.layout.view().h;
        fit.map_w = static_cast<int64_t>(rig.tiles_w) * 32;
        fit.map_h = static_cast<int64_t>(rig.tiles_h) * 32;
        const std::vector<float> levels = zoom::levels(fit, zoom::Limits::any());
        float z = zoom::kNormal;
        int notches = 0;
        int drawn = 0;
        rig.world.ants = {ant(1, 0, 600, 500, 7)};
        for (;;) {
            rig.look(500.0, 400.0, z);
            int sx = 0, sy = 0;
            screen_of(rig, 600, 500, static_cast<double>(z), sx, sy);
            const Delta d = delta(rig.shoot_with(kOff), rig.shoot_with(kHurtNumber));
            const std::string at = std::string(map) + ", notch " + num(notches) + " out from the start, zoom " + std::to_string(z) + ": ";
            if (z >= 0.5f) {
                ++drawn;
                check(d.n > 40 && d.x0 >= sx - 8 && d.x1 <= sx + 8 && d.y1 < sy, at + "a 7 is on the picture over the ant (" + num(d.n) + " pixels)");
            } else {
                check(d.n == 0, at + "below 0.5 there is none (" + num(d.n) + " pixels)");
            }
            const float next = zoom::step(z, -1, fit, zoom::Limits::any());
            if (next == z || notches >= 8) break;
            z = next;
            ++notches;
        }
        check(levels.size() >= 5 && drawn >= 5, std::string(map) + ": the map lets the wheel go out from 1 to 0.5 at least (levels " + num(static_cast<long long>(levels.size())) + ", with numbers " + num(drawn) + ")");
        check(zoom::step(zoom::step(zoom::step(zoom::kNormal, -1, fit, zoom::Limits::any()), -1, fit, zoom::Limits::any()), -1, fit, zoom::Limits::any()) < 0.6f,
              std::string(map) + ": the third notch out is below 0.6, the zoom where the numbers used to stop");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    test_rule();
    test_geometry();
    test_words();
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    test_bars(arc);
    test_numbers(arc);
    test_which_ants(arc);
    test_thick_and_target(arc);
    test_play_zooms(arc);
    std::printf("\nhealth marks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
