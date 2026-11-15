// The numbers in the game's font (src/ants_app/renderer_numbers.cpp; docs/VIEW_AND_HUD.md, "Numbers in the game's font"): the hit point numbers, the damage and heal numbers and the score numbers
// drawn with the TrueType face at the output's resolution, against the classic drawing (the raster digits and the digit sprites, which stay behind Renderer::set_classic_numbers):
//   * the sizes: what shows of a number is as tall as what showed before (within a pixel), the score number as wide as the old one, and the digits are white over a near black outline;
//   * the shrink of a damage or heal number (health::scale_at: 1 at birth, 0.7 at its end, ease-out) about its own middle: it stays over the ant while it gets smaller;
//   * begin_native / end_native put SDL's logical size and the clip back; the cache of pictures and fonts is bounded; with no face the classic numbers are drawn.
// Usage: test_font_numbers. Exit code 0 when every check passes.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/health_changes.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;
namespace hc = ants::app::health;

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

std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return b; }

sim::AntSnapshot sprite_less(uint32_t id, int32_t px, int32_t py, int hp) {
    sim::AntSnapshot a;
    a.id = id;
    a.player_id = 0;
    a.type = sim::AntType::Worker;
    a.px = px;
    a.py = py;
    a.tile_x = px / 32;
    a.tile_y = py / 32;
    a.hp = static_cast<uint16_t>(hp);
    a.max_hp = 10;
    a.state = sim::UnitState::Idle;
    a.loco_clip = 0x7FFE;                                          // no sprite: only the numbers are drawn
    return a;
}

hc::ChangeTracker with_number(int32_t px, int32_t py, int amount, bool heal) {
    hc::ChangeTracker t;
    sim::WorldState w;
    w.width = 40;
    w.height = 40;
    for (uint8_t p = 0; p < w.player_alliances.size(); ++p) w.player_alliances[p] = p;
    w.ants = {sprite_less(1, px, py, heal ? 10 - amount : 10)};
    t.on_tick(w, 1, 0, 0);
    w.ants[0].hp = static_cast<uint16_t>(heal ? 10 : 10 - amount);
    t.on_tick(w, 2, 0, 0);
    return t;
}

struct Box {
    int n{0};
    int x0{1 << 30}, y0{1 << 30}, x1{-1}, y1{-1};
    int w() const { return x1 - x0 + 1; }
    int h() const { return y1 - y0 + 1; }
    double cx() const { return (x0 + x1) / 2.0; }
    double cy() const { return (y0 + y1) / 2.0; }
};

Box delta(const Picture& a, const Picture& b) {
    Box d;
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
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

constexpr hc::Setting kOff{hc::When::Off, hc::Style::Bar};
constexpr hc::Setting kNumbers{hc::When::All, hc::Style::Number};

Picture shoot(PixelRig& rig, hc::ChangeTracker* tracker, int64_t now, hc::Setting marks = kOff) {
    rig.renderer.set_health_changes(tracker, now);
    Picture p = rig.shoot_with(marks, 1.0);
    rig.renderer.set_health_changes(nullptr, 0);
    return p;
}

Picture shoot_scores(PixelRig& rig, int amount) {
    rig.world.score_bubbles.clear();
    if (amount != 0) rig.world.score_bubbles.push_back(sim::ScoreBubble{600, 500, amount, 100u});
    return rig.shoot_with(kOff, 1.0);
}

void test_rule() {
    std::printf("[shrink] the size of a number over its life\n");
    check(hc::scale_at(0) == 1.0 && hc::scale_at(-5) == 1.0, "at birth a number is whole");
    check(std::fabs(hc::scale_at(hc::kLifeMs) - hc::kEndScale) < 1e-9 && hc::scale_at(hc::kLifeMs + 100) == hc::kEndScale, "at the end of its life it is " + num(hc::kEndScale));
    check(std::fabs(hc::kEndScale - 0.70) < 1e-9, "the end size is 70 %");
    const double mid = hc::scale_at(hc::kLifeMs / 2);
    check(std::fabs(mid - 0.775) < 1e-6, "at mid-life (ease-out: 75 % of the way) it is 77.5 %: " + num(mid));
    bool down = true;
    for (int a = 1; a <= hc::kLifeMs; a += 10) down = down && hc::scale_at(a) <= hc::scale_at(a - 10) + 1e-12;
    check(down, "it never grows");
    check(1.0 - hc::scale_at(hc::kLifeMs / 4) > 1.3 * 0.25 * (1.0 - hc::kEndScale), "ease-out: more than a linear share of the shrink is done in the first quarter");
}

void test_drawing(const assets::AssetArchive& arc) {
    std::printf("[drawing] the font's numbers against the classic ones\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.look(500.0, 400.0, 1.0f);
    rig.world.ants = {sprite_less(1, 600, 500, 9)};
    // ---- the damage number: size, outline, colors
    hc::ChangeTracker t = with_number(600, 500, 2, false);
    rig.renderer.set_classic_numbers(true);
    const Picture none = shoot(rig, nullptr, 0);
    const Picture classic = shoot(rig, &t, 0);
    rig.renderer.set_classic_numbers(false);
    check(rig.renderer.font_numbers(), "a face was found: the font numbers are on (a test run from the checkout finds the bundled one)");
    if (!rig.renderer.font_numbers()) return;
    const Picture font = shoot(rig, &t, 0);
    const Box dc = delta(none, classic);
    const Box df = delta(none, font);
    check(df.n > 30 && std::abs(df.h() - dc.h()) <= 1, "-2: what shows is as tall as before (" + std::to_string(df.h()) + " against " + std::to_string(dc.h()) + ")");
    check(std::abs(df.w() - dc.w()) <= 1, "-2: and as wide as before (" + std::to_string(df.w()) + " against " + std::to_string(dc.w()) + ")");
    check(std::fabs(df.cx() - dc.cx()) <= 1.0 && std::fabs(df.cy() - dc.cy()) <= 1.0, "-2: and it stands in the same place (the middle moves " + num(df.cx() - dc.cx()) + ", " + num(df.cy() - dc.cy()) + ")");
    int white = 0, dark = 0;
    for (int y = df.y0; y <= df.y1; ++y) {
        for (int x = df.x0; x <= df.x1; ++x) {
            const uint8_t* p = font.at(x, y);
            const uint8_t* g = none.at(x, y);
            if (p[0] >= 215 && p[1] >= 215 && p[2] >= 215) ++white;
            if (std::memcmp(p, g, 3) != 0 && p[0] * 10 < g[0] * 6 && p[1] * 10 < g[1] * 6 && p[2] * 10 < g[2] * 6) ++dark;
        }
    }
    check(white >= 6, "the digits are white (" + std::to_string(white) + " white pixels)");
    check(dark >= 20, "and have a near black outline round them (" + std::to_string(dark) + " dark pixels)");
    {   // the heal number is green
        hc::ChangeTracker th = with_number(600, 500, 3, true);
        const Picture on = shoot(rig, &th, 0);
        const Box d = delta(none, on);
        int green = 0;
        for (int y = d.y0; y <= d.y1; ++y) {
            for (int x = d.x0; x <= d.x1; ++x) {
                const uint8_t* p = on.at(x, y);
                if (p[1] > 190 && p[1] > p[0] + 40 && p[1] > p[2] + 40) ++green;
            }
        }
        check(green >= 6, "+3 is pale green (" + std::to_string(green) + " pixels)");
    }
    // ---- the shrink: sizes at birth, mid-life and the end, and the middle that does not move
    {
        const int ages[4] = {0, 300, 450, 590};
        Box b[4];
        for (int i = 0; i < 4; ++i) {
            b[i] = delta(none, shoot(rig, &t, ages[i]));
            check(b[i].n > 0, "-2 at " + std::to_string(ages[i]) + " ms is drawn");
        }
        const double h0 = b[0].h(), w0 = b[0].w();
        const double e300 = hc::scale_at(300), e590 = hc::scale_at(590);
        check(std::fabs(b[1].h() - h0 * e300) <= 1.5 && std::fabs(b[1].w() - w0 * e300) <= 1.5, "at 0.3 s it is " + num(e300 * 100) + " % of its size (" + std::to_string(b[1].w()) + " x " + std::to_string(b[1].h()) + " from " + std::to_string(b[0].w()) + " x " + std::to_string(b[0].h()) + ")");
        check(std::fabs(b[3].h() - h0 * e590) <= 1.5, "at the end of its life it is about 70 % (" + std::to_string(b[3].h()) + " of " + std::to_string(b[0].h()) + ")");
        check(b[0].h() > b[1].h() && b[1].h() >= b[2].h() && b[2].h() >= b[3].h() && b[0].h() > b[3].h() + 2, "it only gets smaller");
        bool centred = true;
        for (int i = 1; i < 4; ++i) {                           // (the rise is taken out: the middle of the picture goes up by it, and by nothing else)
            const double dy = b[0].cy() - hc::rise_at(ages[i]) - b[i].cy();
            centred = centred && std::fabs(b[i].cx() - b[0].cx()) <= 1.0 && std::fabs(dy) <= 1.0;
        }
        check(centred, "it shrinks about its own middle: the middle only rises (not about a corner)");
    }
    // ---- hit points and score: the ink of the old drawing against the new
    {
        rig.world.ants = {sprite_less(1, 600, 500, 100)};
        rig.renderer.set_classic_numbers(true);
        const Picture hc_none = rig.shoot_with(kOff, 1.0);
        const Picture hc_old = rig.shoot_with(kNumbers, 1.0);
        rig.renderer.set_classic_numbers(false);
        const Picture hc_new = rig.shoot_with(kNumbers, 1.0);
        const Box o = delta(hc_none, hc_old);
        const Box n = delta(hc_none, hc_new);
        check(delta(hc_old, hc_new).n > 30, "100 (hit points): the font's picture is not the raster's");
        check(n.n > 30 && std::abs(n.h() - o.h()) <= 1, "100 (hit points): as tall as before (" + std::to_string(n.h()) + " against " + std::to_string(o.h()) + ")");
        check(std::abs(n.w() - o.w()) <= 2 && std::abs(n.x0 - o.x0) <= 1 && std::abs(n.y0 - o.y0) <= 1, "...from the same corner (" + std::to_string(n.x0 - o.x0) + ", " + std::to_string(n.y0 - o.y0) + ")");
    }
    {
        rig.world.ants.clear();
        rig.renderer.set_classic_numbers(true);
        const Picture s_none = shoot_scores(rig, 0);
        const Picture s_old = shoot_scores(rig, 1250);
        rig.renderer.set_classic_numbers(false);
        const Picture s_new = shoot_scores(rig, 1250);
        const Box o = delta(s_none, s_old);
        const Box n = delta(s_none, s_new);
        check(n.n > 30, "+1250 (score) is drawn");
        check(std::abs(n.w() - o.w()) <= 1, "+1250 is as wide as the old one, within a pixel (" + std::to_string(n.w()) + " against " + std::to_string(o.w()) + ")");
        check(std::abs(n.h() - o.h()) <= 1, "...and as tall (" + std::to_string(n.h()) + " against " + std::to_string(o.h()) + ")");
        check(std::fabs(n.cx() - o.cx()) <= 1.0 && std::fabs(n.cy() - o.cy()) <= 1.0, "...in the same place");
        const Picture s_neg_old = [&] { rig.renderer.set_classic_numbers(true); return shoot_scores(rig, -38); }();
        rig.renderer.set_classic_numbers(false);
        const Picture s_neg_new = shoot_scores(rig, -38);
        const Box no = delta(s_none, s_neg_old);
        const Box nn = delta(s_none, s_neg_new);
        check(std::abs(nn.w() - no.w()) <= 1 && std::abs(nn.h() - no.h()) <= 1, "-38 as well (" + std::to_string(nn.w()) + " x " + std::to_string(nn.h()) + " against " + std::to_string(no.w()) + " x " + std::to_string(no.h()) + ")");
    }
}

void test_output_and_cache(const assets::AssetArchive& arc) {
    std::printf("[native] the output's pixels, the restore, the cache, the fallback\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    if (!rig.ok) {
        check(false, "the rig is up");
        return;
    }
    rig.renderer.set_classic_numbers(false);
    SDL_Renderer* sdl = rig.renderer.get_sdl_renderer();
    SDL_SetWindowSize(rig.win, kWideW * 2, kWideH * 2);              // an output twice the canvas
    rig.renderer.refit_canvas();
    {
        int lw = 0, lh = 0;
        SDL_RenderGetLogicalSize(sdl, &lw, &lh);
        check(lw == kWideW && lh == kWideH, "the logical size is the canvas's");
        const SDL_Rect clip{10, 20, 300, 200};
        SDL_RenderSetClipRect(sdl, &clip);
        const Renderer::NativeFrame f = rig.renderer.begin_native();
        check(f.on && std::fabs(f.scale - 2.0) < 1e-9, "begin_native: the output has 2 pixels to a canvas pixel");
        SDL_RenderGetLogicalSize(sdl, &lw, &lh);
        check(lw == 0 && lh == 0, "...and SDL's logical size is off while the numbers are copied");
        SDL_Rect inside{};
        SDL_RenderGetClipRect(sdl, &inside);
        check(inside.x == f.x + 20 && inside.y == f.y + 40 && inside.w == 600 && inside.h == 400, "...the clip is the same rectangle in output pixels");
        rig.renderer.end_native(f);
        SDL_RenderGetLogicalSize(sdl, &lw, &lh);
        check(lw == kWideW && lh == kWideH, "end_native: the logical size is the canvas's again");
        SDL_Rect back{};
        SDL_RenderGetClipRect(sdl, &back);
        check(SDL_RenderIsClipEnabled(sdl) == SDL_TRUE && back.x == 10 && back.y == 20 && back.w == 300 && back.h == 200, "...and the clip is the one that was set");
        SDL_RenderSetClipRect(sdl, nullptr);
        const Renderer::NativeFrame g = rig.renderer.begin_native();
        rig.renderer.end_native(g);
        check(SDL_RenderIsClipEnabled(sdl) == SDL_FALSE, "...a canvas with no clip stays without one");
    }
    {   // a whole frame with all three kinds leaves the logical size and the clip as they were
        rig.look(500.0, 400.0, 1.0f);
        rig.world.ants = {sprite_less(1, 600, 500, 9)};
        rig.world.score_bubbles.push_back(sim::ScoreBubble{700, 500, 15, 100u});
        hc::ChangeTracker t = with_number(600, 500, 2, false);
        rig.renderer.set_health_changes(&t, 100);
        rig.renderer.set_health_marks(kNumbers, 1.0);
        rig.renderer.begin_frame();
        rig.renderer.render_world(rig.world, rig.engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
        rig.renderer.set_health_changes(nullptr, 0);
        int lw = 0, lh = 0;
        SDL_RenderGetLogicalSize(sdl, &lw, &lh);
        check(lw == kWideW && lh == kWideH, "after a frame with hit points, a damage number and a score: the logical size is the canvas's");
    }
    {   // output resolution: the glyphs are made at the output's size (a number twice as tall in output pixels on an output twice as big)
        rig.world.score_bubbles.clear();
        int bw = 0, bh = 0;
        check(rig.renderer.font_number_box("-2", 24, 2, bw, bh) && bh >= 22 && bh <= 24, "a picture asked 24 pixels tall is that tall (" + std::to_string(bh) + ")");
        int sw = 0, sh = 0;
        rig.renderer.font_number_box("-2", 12, 1, sw, sh);
        check(std::abs(bh - 2 * sh) <= 2 && bw > sw, "...and twice that is twice as tall: crisp at the output's size, not scaled (" + std::to_string(sh) + " and " + std::to_string(bh) + ")");
    }
    {   // the cache is bounded
        for (int i = 0; i < 700; ++i) {
            int w = 0, h = 0;
            rig.renderer.font_number_box(std::to_string(i % 97), 8 + (i % 40), 1 + (i % 3), w, h);
            if (rig.renderer.number_cache_size() > Renderer::kMaxNumberPictures || rig.renderer.number_font_count() > Renderer::kMaxNumberFonts) {
                check(false, "the cache grew past its bound at " + std::to_string(i));
                break;
            }
        }
        for (int i = 0; i < 700; ++i) {                          // (one size, many texts: the pictures reach their own bound)
            int w = 0, h = 0;
            rig.renderer.font_number_box(std::to_string(1000 + i), 14, 1, w, h);
            if (rig.renderer.number_cache_size() > Renderer::kMaxNumberPictures) {
                check(false, "the pictures grew past their bound at " + std::to_string(i));
                break;
            }
        }
        check(rig.renderer.number_cache_size() <= Renderer::kMaxNumberPictures && rig.renderer.number_font_count() <= Renderer::kMaxNumberFonts, "700 sizes and texts: the pictures and the fonts stay within their bounds (" + std::to_string(rig.renderer.number_cache_size()) + ", " + std::to_string(rig.renderer.number_font_count()) + ")");
    }
    {   // no face: the classic numbers are drawn
        SDL_SetWindowSize(rig.win, kWideW, kWideH);
        rig.renderer.refit_canvas();
        rig.look(500.0, 400.0, 1.0f);
        rig.world.ants = {sprite_less(1, 600, 500, 9)};
        hc::ChangeTracker t = with_number(600, 500, 2, false);
        rig.renderer.set_classic_numbers(true);
        const Picture classic = shoot(rig, &t, 0);
        rig.renderer.set_classic_numbers(false);
        rig.renderer.set_number_font_path("");
        check(!rig.renderer.font_numbers(), "no face: the font numbers are off");
        const Picture none_face = shoot(rig, &t, 0);
        check(delta(classic, none_face).n == 0, "...and the picture is the classic one, pixel for pixel");
        rig.renderer.set_number_font_path("no/such/face.ttf");
        const Picture bad = shoot(rig, &t, 0);
        const Picture clean = shoot(rig, nullptr, 0);
        check(delta(bad, clean).n == 0, "a face that does not open: no number and no crash");
    }
}

}  // namespace

int main(int, char**) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    test_rule();
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    test_drawing(arc);
    test_output_and_cache(arc);
    std::printf("\nnumbers in the game's font: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
