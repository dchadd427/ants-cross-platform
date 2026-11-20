// The numbers in the game's font (src/ants_app/renderer_numbers.cpp; docs/VIEW_AND_HUD.md, "Numbers in the game's font"): the hit point numbers, the damage and heal numbers and the score numbers
// drawn with the TrueType face at the output's resolution, against the classic drawing (the raster digits and the digit sprites, which stay behind Renderer::set_classic_numbers):
//   * the sizes: the hit point and the damage and heal numbers are plain digits with nothing round them (no pixel of one is darker than both the ground and the digit's color: no border, on every
//     face and at every output scale), as tall and in the place that the classic digits stand less the room that the old border took; the score number (and the results page's) keeps its outline and is
//     as wide as the old one;
//   * the shrink of a damage or heal number (health::scale_at: 1 at birth, 0.7 at its end, ease-out) about its own middle: it stays over the ant while it gets smaller;
//   * begin_native / end_native put SDL's logical size and the clip back; the cache of pictures and fonts is bounded; with no face the classic numbers are drawn.
// Usage: test_font_numbers. Exit code 0 when every check passes.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
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

}  // namespace

#include "font_score_cases.hpp"      // (the team scores: the HUD boxes, the results page, the fit; they use check() above)

namespace {

constexpr int kGrow = 2;      // what the thick outline of the score numbers adds to what shows, at 1:1: 2 * (2 - 1) output pixels (the glyphs keep the size that the old one-pixel outline left them)
constexpr int kShorter = 2;   // the hit point digits at 1:1 against the raster's: 8 pixels tall, not 10, and 2 pixels in from its corner (the room that their old border took)

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

/// How many pixels of `box` are darker, in some channel, than both the ground's value (the frame without the number, `none`) and the number's own color (r, g, b), by more than `kBlendSlack` levels: a number
/// drawn plainly (digits white or green, edge pixels that color blended with what is behind them) has none, a border has them all round the digits (it darkens the ground by tens of levels).
/// The slack is SDL's: its software renderer on x86 (2.30 of Linux) rounds the blend of an almost transparent texel down by up to 2 levels of the ground, for any texture.
constexpr int kBlendSlack = 4;
int unbordered(const Picture& none, const Picture& with, const Box& box, int r, int g, int b) {
    const int color[3] = {r, g, b};
    int n = 0;
    for (int y = box.y0; y <= box.y1; ++y) {
        for (int x = box.x0; x <= box.x1; ++x) {
            bool darker = false;
            for (int c = 0; c < 3; ++c) darker = darker || with.at(x, y)[c] + kBlendSlack < std::min<int>(none.at(x, y)[c], color[c]);
            n += darker ? 1 : 0;
        }
    }
    return n;
}

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
    check(df.n > 30 && std::abs(df.h() - dc.h()) <= 1, "-2: as tall as the classic digits, 10 pixels at 1:1 (" + std::to_string(df.h()) + " against " + std::to_string(dc.h()) + ")");
    check(std::abs(df.w() - dc.w()) <= 2, "-2: and about as wide (" + std::to_string(df.w()) + " against " + std::to_string(dc.w()) + ")");
    int natural_w = 0, natural_h = 0;                          // (the glyphs at their own width, 10 pixels tall: the face is wider than the classic digits, so the number is squeezed to about 70 % of it)
    rig.renderer.font_number_box("-2", 10, 0, natural_w, natural_h);
    check(natural_w > 0 && df.w() <= natural_w * 85 / 100 && df.w() >= natural_w * 60 / 100, "-2: squeezed to the classic digits' room, not at the face's own width (" + std::to_string(df.w()) + " of " + std::to_string(natural_w) + ")");
    check(std::fabs(df.cx() - dc.cx()) <= 1.0 && std::fabs(df.cy() - dc.cy()) <= 1.0, "-2: and it stands in the same place (the middle moves " + num(df.cx() - dc.cx()) + ", " + num(df.cy() - dc.cy()) + ")");
    int white = 0;
    for (int y = df.y0; y <= df.y1; ++y) {
        for (int x = df.x0; x <= df.x1; ++x) {
            const uint8_t* p = font.at(x, y);
            if (p[0] >= 215 && p[1] >= 215 && p[2] >= 215) ++white;
        }
    }
    check(white >= 6, "the digits are white (" + std::to_string(white) + " white pixels)");
    check(unbordered(none, font, df, 255, 255, 255) == 0, "and have no border: no pixel of -2 is darker than both the ground and white (" + std::to_string(unbordered(none, font, df, 255, 255, 255)) + " pixels)");
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
        check(unbordered(none, on, d, 132, 236, 120) == 0, "...and has no border either (" + std::to_string(unbordered(none, on, d, 132, 236, 120)) + " pixels darker than both the ground and that green)");
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
        check(n.n > 30 && std::abs((o.h() - n.h()) - kShorter) <= 1, "100 (hit points): the digits are 8 pixels tall at 1:1, " + std::to_string(kShorter) + " less than the raster's (" + std::to_string(n.h()) + " against " + std::to_string(o.h()) + ")");
        check(std::abs((n.x0 - o.x0) - kShorter) <= 1 && std::abs((n.y0 - o.y0) - kShorter) <= 1, "...standing " + std::to_string(kShorter) + " pixels in from the raster's corner, where they stood in their old border (" + std::to_string(n.x0 - o.x0) + ", " + std::to_string(n.y0 - o.y0) + ")");
        check(unbordered(hc_none, hc_new, n, 255, 255, 255) == 0, "...and no border: no pixel of the 100 is darker than both the ground and white (" + std::to_string(unbordered(hc_none, hc_new, n, 255, 255, 255)) + " pixels)");
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
        check(std::abs((n.w() - o.w()) - kGrow) <= 3, "+1250 is as wide as the old one and the outline's growth, within a few pixels (" + std::to_string(n.w()) + " against " + std::to_string(o.w()) + ")");
        check(std::abs((n.h() - o.h()) - kGrow) <= 1, "...and as tall as it and that growth (" + std::to_string(n.h()) + " against " + std::to_string(o.h()) + ")");
        check(std::fabs(n.cx() - o.cx()) <= 1.0 && std::fabs(n.cy() - o.cy()) <= 1.0, "...in the same place");
        const Picture s_neg_old = [&] { rig.renderer.set_classic_numbers(true); return shoot_scores(rig, -38); }();
        rig.renderer.set_classic_numbers(false);
        const Picture s_neg_new = shoot_scores(rig, -38);
        const Box no = delta(s_none, s_neg_old);
        const Box nn = delta(s_none, s_neg_new);
        check(std::abs((nn.w() - no.w()) - kGrow) <= 3 && std::abs((nn.h() - no.h()) - kGrow) <= 1, "-38 as well (" + std::to_string(nn.w()) + " x " + std::to_string(nn.h()) + " against " + std::to_string(no.w()) + " x " + std::to_string(no.h()) + ")");
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
        const bool got = rig.renderer.font_number_box("-2", 24, 1, bw, bh);
        check(got && bh >= 23 && bh <= 25, "a picture asked 24 pixels tall is that tall (" + std::to_string(bh) + ")");
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
        check(rig.renderer.number_cache_size() <= Renderer::kMaxNumberPictures && rig.renderer.number_font_count() <= Renderer::kMaxNumberFonts, "700 sizes and texts: the pictures and the faces stay within their bounds (" + std::to_string(rig.renderer.number_cache_size()) + ", " + std::to_string(rig.renderer.number_font_count()) + ")");
    }
    {   // a full cache drops the oldest quarter of the pictures, not all of them, and the faces stay open
        rig.renderer.set_number_font_path(rig.renderer.number_font_path());          // (an empty cache)
        const size_t kMax = Renderer::kMaxNumberPictures;
        int w = 0, h = 0;
        for (size_t i = 0; i < kMax; ++i) rig.renderer.font_number_box(std::to_string(20000 + i), 14, 1, w, h);
        check(rig.renderer.number_cache_size() == kMax, "the pictures fill to their bound (" + std::to_string(rig.renderer.number_cache_size()) + ")");
        const size_t opens = rig.renderer.number_font_opens();
        check(rig.renderer.number_font_count() == 1, "one size: one face (" + std::to_string(rig.renderer.number_font_count()) + ")");
        rig.renderer.font_number_box("20000", 14, 1, w, h);                         // (the oldest one is asked for again: now it is the newest)
        rig.renderer.font_number_box("33333", 14, 1, w, h);                         // (one more: the cache is full)
        check(rig.renderer.number_cache_size() == kMax - kMax / 4 + 1, "a full cache drops a quarter, not all: " + std::to_string(rig.renderer.number_cache_size()) + " of " + std::to_string(kMax) + " are left, and the new one");
        check(rig.renderer.number_font_opens() == opens && rig.renderer.number_font_count() == 1, "the face is not opened again, nor closed, by the eviction (" + std::to_string(rig.renderer.number_font_opens() - opens) + " opens)");
        const size_t before = rig.renderer.number_cache_size();
        rig.renderer.font_number_box("20000", 14, 1, w, h);
        check(rig.renderer.number_cache_size() == before, "the picture that was used last stayed (least recently used goes first)");
        rig.renderer.font_number_box("20001", 14, 1, w, h);
        check(rig.renderer.number_cache_size() == before + 1, "the oldest one that was not used again went");
        check(rig.renderer.number_font_opens() == opens + 0, "...and made again with the face that is open");
    }
    {   // the faces are bounded too: the oldest quarter goes at the bound, the pictures stay
        rig.renderer.set_number_font_path(rig.renderer.number_font_path());
        int w = 0, h = 0;
        const size_t kFonts = Renderer::kMaxNumberFonts;
        for (size_t i = 0; i < kFonts; ++i) rig.renderer.font_number_box("5", static_cast<int32_t>(8 + i), 1, w, h);
        check(rig.renderer.number_font_count() == kFonts, "the faces fill to their bound (" + std::to_string(rig.renderer.number_font_count()) + ")");
        const size_t pictures = rig.renderer.number_cache_size();
        rig.renderer.font_number_box("5", static_cast<int32_t>(8 + kFonts), 1, w, h);
        check(rig.renderer.number_font_count() == kFonts - kFonts / 4 + 1, "at the bound a quarter of the faces goes, not all: " + std::to_string(rig.renderer.number_font_count()));
        check(rig.renderer.number_cache_size() == pictures + 1, "...and no picture goes with them");
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


// A picture of the OUTPUT at its own size (PixelRig::read assumes an output as big as the canvas)
Picture shoot_output(PixelRig& rig, hc::ChangeTracker* tracker, int64_t now) {
    rig.renderer.set_health_changes(tracker, now);
    rig.renderer.set_health_marks(kOff, 1.0);
    rig.renderer.begin_frame();
    rig.renderer.render_world(rig.world, rig.engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
    rig.renderer.set_health_changes(nullptr, 0);
    Picture p;
    SDL_GetRendererOutputSize(rig.renderer.get_sdl_renderer(), &p.w, &p.h);
    p.px.assign(static_cast<size_t>(p.w) * static_cast<size_t>(p.h) * 4u, 0);
    SDL_RenderReadPixels(rig.renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, p.px.data(), p.w * 4);
    return p;
}

// The face (Libre Franklin Black beside the Medium), its fallback, the thick outline per output scale
void test_face_and_outline(const assets::AssetArchive& arc) {
    std::printf("[face] the numbers' face, the fallback, the outline per output scale\n");
    // ---- the outline's rule: a multiple of the old one-pixel outline, never less than 2 output pixels
    check(hc::kNumberOutlineFactor == 2, "the outline is twice the old one");
    check(hc::number_outline_px(1.0) == 2 && hc::number_outline_px(1.33) == 2 && hc::number_outline_px(0.4) == 2, "scale 1 (a 1:1 canvas, a desktop window at 1.33x): 2 output pixels");
    check(hc::number_outline_px(2.0) == 4 && hc::number_outline_px(2.11) == 4, "scale 2 (a phone at 2.1x): 4 output pixels");
    check(hc::number_outline_px(3.0) == 6, "scale 3: 6 output pixels");
    // ---- the face file: Libre Franklin Black, in the folder that the Medium is bundled from
    {
        std::ifstream f("Original-Ants/LibreFranklin-Black.ttf", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(bytes.size() > 100000 && bytes.compare(0, 4, std::string("\0\1\0\0", 4)) == 0, "Original-Ants/LibreFranklin-Black.ttf is a TrueType file (" + std::to_string(bytes.size()) + " bytes)");
        const std::string wide("L\0i\0b\0r\0e\0 \0F\0r\0a\0n\0k\0l\0i\0n\0 \0B\0l\0a\0c\0k", 40);
        const std::string ascii = "Libre Franklin Black";
        const std::string be("\0L\0i\0b\0r\0e\0 \0F\0r\0a\0n\0k\0l\0i\0n\0 \0B\0l\0a\0c\0k", 40);
        check(bytes.find(ascii) != std::string::npos || bytes.find(be) != std::string::npos || bytes.find(wide) != std::string::npos, "...and it names itself Libre Franklin Black");
        std::ifstream lic("Original-Ants/LibreFranklin-OFL.txt", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(lic)), std::istreambuf_iterator<char>());
        check(text.find("SIL OPEN FONT LICENSE Version 1.1") != std::string::npos && text.find("Libre Franklin Project Authors") != std::string::npos, "the licence beside it covers the family (SIL Open Font License 1.1, Libre Franklin Project Authors)");
    }
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.renderer.set_classic_numbers(false);
    const std::string black = "Original-Ants/LibreFranklin-Black.ttf";
    const std::string medium = "Original-Ants/LibreFranklin-Medium.ttf";
    check(rig.renderer.number_font_path() == black, "the renderer's numbers use the Black file (" + rig.renderer.number_font_path() + ")");
    // ---- the fallback: no Black file, the face that draw_text uses, the same outline
    check(Renderer::choose_number_face(medium, black) == black, "the Black file is there: it is the face");
    check(Renderer::choose_number_face(medium, "no/such/black.ttf") == medium, "no Black file: the face that draw_text uses (the Medium)");
    check(Renderer::choose_number_face("", "no/such/black.ttf").empty() && Renderer::choose_number_face("", black).empty(), "no face at all: none (the classic digits are drawn)");
    rig.look(500.0, 400.0, 1.0f);
    rig.world.ants = {sprite_less(1, 600, 500, 9)};
    hc::ChangeTracker t = with_number(600, 500, 2, false);
    const Picture none = shoot(rig, nullptr, 0);
    rig.renderer.set_number_font_path(black);
    const Picture with_black = shoot(rig, &t, 0);
    rig.renderer.set_number_font_path(medium);
    const Picture with_medium = shoot(rig, &t, 0);
    rig.renderer.set_number_font_path(Renderer::choose_number_face(medium, "no/such/black.ttf"));
    const Picture fallback = shoot(rig, &t, 0);
    check(delta(none, with_black).n > 30 && delta(none, with_medium).n > 30, "both faces draw a number");
    check(delta(with_black, with_medium).n > 20, "the Black and the Medium are different pictures");
    check(delta(with_medium, fallback).n == 0, "no Black file: the number is the Medium's, pixel for pixel");
    const Box bb = delta(none, with_black), bm = delta(none, with_medium);
    check(bb.h() == bm.h() && std::abs(bb.w() - bm.w()) <= 3, "...and as big as the Black one (" + std::to_string(bm.w()) + " x " + std::to_string(bm.h()) + " against " + std::to_string(bb.w()) + " x " + std::to_string(bb.h()) + ")");
    check(unbordered(none, with_black, bb, 255, 255, 255) == 0 && unbordered(none, with_medium, bm, 255, 255, 255) == 0, "...both without a border (" + std::to_string(unbordered(none, with_black, bb, 255, 255, 255)) + " and " + std::to_string(unbordered(none, with_medium, bm, 255, 255, 255)) + " pixels darker than both the ground and white)");
    rig.renderer.set_number_font_path(black);
    // ---- the outline grows outward: the glyph keeps the size the old one left, what shows is 2 * (o - base) taller
    {
        int w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0, w4 = 0, h4 = 0;
        rig.renderer.font_number_box("100", 24, 1, w1, h1);
        rig.renderer.font_number_box("100", 24, 2, w2, h2);
        rig.renderer.font_number_box("100", 24, 4, w3, h3);
        rig.renderer.font_number_box("100", 24, 8, w4, h4);
        check(std::abs(h1 - 24) <= 1, "an old (1 pixel) outline: as tall as asked (" + std::to_string(h1) + ")");
        check(std::abs((h2 - h1) - 2) <= 1, "2 pixels: the outline grew outward, 2 pixels taller (" + std::to_string(h2) + ")");
        check(std::abs((h3 - 24) - 4) <= 1 && std::abs((h4 - 24) - 8) <= 1, "4 and 8 pixels (a phone's): 4 and 8 pixels taller (" + std::to_string(h3) + ", " + std::to_string(h4) + ")");
        check(w2 > w1, "...and wider (" + std::to_string(w1) + ", " + std::to_string(w2) + ", " + std::to_string(w3) + ")");
    }
    // ---- a damage number at output scales 1, 2 and 3: the old ink (12 canvas pixels) less its margins (2 x scale), and no border at any of them
    for (int k = 1; k <= 3; ++k) {
        SDL_SetWindowSize(rig.win, kWideW * k, kWideH * k);
        rig.renderer.refit_canvas();
        rig.look(500.0, 400.0, 1.0f);
        const Picture n0 = shoot_output(rig, nullptr, 0);
        const Picture n1 = shoot_output(rig, &t, 0);
        const Box d = delta(n0, n1);
        const int want = 12 * k - 2 * k;
        check(d.n > 30 && std::abs(d.h() - want) <= 2, "-2 at output scale " + std::to_string(k) + ": " + std::to_string(d.h()) + " pixels tall (" + std::to_string(want) + ": the digits of 12 x scale, less their margins)");
        check(unbordered(n0, n1, d, 255, 255, 255) == 0, "-2 at output scale " + std::to_string(k) + ": no border (" + std::to_string(unbordered(n0, n1, d, 255, 255, 255)) + " pixels darker than both the ground and white)");
        hc::ChangeTracker th = with_number(600, 500, 7, true);
        const Picture n2 = shoot_output(rig, &th, 0);
        const Box dh = delta(n0, n2);
        check(dh.n > 30 && unbordered(n0, n2, dh, 132, 236, 120) == 0, "+7 at output scale " + std::to_string(k) + ": no border (" + std::to_string(unbordered(n0, n2, dh, 132, 236, 120)) + " pixels darker than both the ground and the green)");
        const Picture faded = shoot_output(rig, &t, 450);
        const Box df = delta(n0, faded);
        check(df.n > 20 && unbordered(n0, faded, df, 255, 255, 255) == 0, "-2 at output scale " + std::to_string(k) + " and 0.45 s, fading and smaller: no border either (" + std::to_string(unbordered(n0, faded, df, 255, 255, 255)) + " pixels)");
    }
    SDL_SetWindowSize(rig.win, kWideW, kWideH);
    rig.renderer.refit_canvas();
}

// The health number on a small canvas (a phone: the digits are drawn `digit_scale` times as big) and at every output scale: plain white, nothing round it, on any ground
void test_phone_digits_have_no_border(const assets::AssetArchive& arc) {
    std::printf("[border] the hit point number on a phone and at every output scale has no border\n");
    PixelRig rig(arc, "MEDIUM", kWideW, kWideH);
    check(rig.ok, "the rig is up");
    if (!rig.ok) return;
    rig.renderer.set_classic_numbers(false);
    rig.world.ants = {sprite_less(1, 600, 500, 10), sprite_less(2, 900, 700, 8)};
    struct Case { int k; double per_css; };
    for (const Case& c : {Case{1, 1.0}, Case{1, 1.25}, Case{2, 1.4}, Case{2, 2.0}, Case{3, 2.5}, Case{3, 3.0}, Case{3, 9.0}}) {
        SDL_SetWindowSize(rig.win, kWideW * c.k, kWideH * c.k);
        rig.renderer.refit_canvas();
        for (const double look : {0.0, 150.0}) {
            rig.look(500.0 + look, 400.0 + look, 1.0f);
            rig.renderer.set_health_marks(kOff, c.per_css);
            rig.renderer.begin_frame();
            rig.renderer.render_world(rig.world, rig.engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
            Picture none;
            SDL_GetRendererOutputSize(rig.renderer.get_sdl_renderer(), &none.w, &none.h);
            none.px.assign(static_cast<size_t>(none.w) * static_cast<size_t>(none.h) * 4u, 0);
            SDL_RenderReadPixels(rig.renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, none.px.data(), none.w * 4);
            rig.renderer.set_health_marks(kNumbers, c.per_css);
            rig.renderer.begin_frame();
            rig.renderer.render_world(rig.world, rig.engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
            Picture with;
            with.w = none.w;
            with.h = none.h;
            with.px.assign(none.px.size(), 0);
            SDL_RenderReadPixels(rig.renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, with.px.data(), with.w * 4);
            const Box d = delta(none, with);
            const std::string tag = "output x" + std::to_string(c.k) + ", " + num(c.per_css) + " picture pixels per CSS pixel: ";
            check(d.n > 30, tag + "the numbers are drawn (" + std::to_string(d.n) + " pixels changed)");
            check(unbordered(none, with, d, 255, 255, 255) == 0, tag + "no border: no pixel is darker than both the ground and white (" + std::to_string(unbordered(none, with, d, 255, 255, 255)) + ")");
        }
    }
    SDL_SetWindowSize(rig.win, kWideW, kWideH);
    rig.renderer.refit_canvas();
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
    test_face_and_outline(arc);
    test_phone_digits_have_no_border(arc);
    scorecases::test_hud(arc);
    scorecases::test_results(arc);
    scorecases::test_fit(arc);
    scorecases::test_clock(arc);
    scorecases::test_hud_pixels(arc);
    scorecases::test_clock_pixels(arc);
    std::printf("\nnumbers in the game's font: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
