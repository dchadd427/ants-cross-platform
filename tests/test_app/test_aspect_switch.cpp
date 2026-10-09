// The live aspect switch: `Application::set_aspect` changes the shape of the picture while the game runs (the selector under the game in the browser: Classic 4:3, 16:10, 16:9, 21:9),
// with no restart, no reconnect, and the simulation untouched.
//   * layouts: from every shape to every shape (and back) the canvas, SDL's logical size, the match screen's layout, the renderer's, the HUD's and the camera's view, and the picture are
//     the new shape's; a frame is drawn there, in the whole canvas;
//   * the match: the world point in the middle of the view stays in the middle of it, the zoom that the player chose is kept in the settings of the game and comes back where the new view
//     offers it (it is one of the levels that the new view offers while it is away), and the simulation is the one that it would have been: the state hash after every tick is the one of a
//     match that was never switched (the lock-step peers compare it, and a switched player must not drift);
//   * the pages: the setup screen, the room, the loading screen, the quick help and the results are the 960 x 540 page, centred in the 16:10 and the 21:9 canvases with black around, and
//     the whole canvas in 4:3 and 16:9; a match that starts after the switch is the new shape's;
//     (the pages are also compared with the 16:9 page, pixel for pixel; the quick help's button, the results and the pointer);
//   * the guards: an application that was never started, and the shape it already has.
// Usage: test_aspect_switch. Exit code 0 when every check passes.
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/page_layout.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/view_zoom.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_test_paths.hpp"
#include "zoom_scene.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

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

constexpr Aspect kShapes4[] = {Aspect::Classic4x3, Aspect::Wide16x10, Aspect::Wide16x9, Aspect::Ultra21x9};

/// The map view that each shape's match screen has (wider and taller canvases show more map, as 16:9 shows more than 4:3 does)
struct ViewSize {
    int32_t w;
    int32_t h;
};
ViewSize view_of(Aspect aspect) {
    switch (aspect) {
        case Aspect::Classic4x3: return {442, 440};
        case Aspect::Wide16x10: return {762, 560};
        case Aspect::Wide16x9: return {762, 500};
        case Aspect::Ultra21x9: return {1062, 500};
    }
    return {0, 0};
}

std::string nm(Aspect aspect) { return aspect_name(aspect); }

/// SDL's logical size of the application's renderer (what the canvas is as far as the window is concerned)
bool logical_size_is(Application& app, int32_t w, int32_t h) {
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(app.renderer().get_sdl_renderer(), &lw, &lh);
    return lw == w && lh == h;
}

bool same_rect(const LayoutRect& a, const LayoutRect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

/// The share of a picture that is not black
double lit_share(const Picture& p) {
    size_t lit = 0;
    for (size_t i = 0; i + 3 < p.px.size(); i += 4) {
        if (p.px[i] != 0 || p.px[i + 1] != 0 || p.px[i + 2] != 0) ++lit;
    }
    return p.px.empty() ? 0.0 : static_cast<double>(lit) / static_cast<double>(p.px.size() / 4u);
}

/// The window that the application opened (the dummy video driver has the one)
SDL_Window* find_window() {
    for (uint32_t i = 1; i < 256; ++i) {
        if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
    }
    return nullptr;
}

/// A frame of the application drawn and read back as the canvas. SDL reads the renderer's output, which is the window, not the logical canvas: the window is made the canvas's size first
/// (one pixel of the output is one pixel of the canvas then), as the shape changes it
Picture frame_of(Application& app) {
    const CanvasLayout canvas = app.canvas();
    if (SDL_Window* window = find_window()) SDL_SetWindowSize(window, canvas.width, canvas.height);
    SDL_PumpEvents();
    app.render_frame();
    return read_canvas(app, canvas.width, canvas.height);
}

/// Nothing but black outside a rectangle of the picture
bool black_outside(const Picture& p, const LayoutRect& inside_rect) {
    for (int32_t y = 0; y < p.h; ++y) {
        for (int32_t x = 0; x < p.w; ++x) {
            const bool inside = x >= inside_rect.x && x < inside_rect.x + inside_rect.w && y >= inside_rect.y && y < inside_rect.y + inside_rect.h;
            if (inside) continue;
            const uint8_t* px = p.at(x, y);
            if (px[0] != 0 || px[1] != 0 || px[2] != 0) return false;
        }
    }
    return true;
}

/// The pointer is parked at a point of the picture on screen (the game draws its own cursor there): a frame compared with another shape's must have it in the same place of the page
void park_pointer(Application& app) {
    SDL_MouseMotionEvent motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.x = 10;
    motion.y = 10;
    app.handle_mouse_motion(motion);
}

/// The picture `part` is the same as the part of `whole` that starts at (ox, oy): a page drawn in a wide canvas is the 960 x 540 page, pixel for pixel. The corner of the page where the
/// version and the frame rate are written is left out (it is the one place that changes with the clock: the readout is blended text)
bool region_equals(const Picture& whole, int32_t ox, int32_t oy, const Picture& part) {
    if (ox < 0 || oy < 0 || ox + part.w > whole.w || oy + part.h > whole.h) return false;
    const LayoutRect readout{part.w - 140, part.h - 16, 140, 16};
    for (int32_t y = 0; y < part.h; ++y) {
        for (int32_t x = 0; x < part.w; ++x) {
            if (x >= readout.x && y >= readout.y) continue;
            const uint8_t* a = whole.at(ox + x, oy + y);
            const uint8_t* b = part.at(x, y);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) return false;
        }
    }
    return true;
}

/// What the new shape must be, whatever the shape before: the canvas, SDL's logical size, the layouts everywhere, the camera's view and the picture
void check_shape(Application& app, Aspect aspect, const std::string& when) {
    const std::string name = nm(aspect) + " " + when;
    const int32_t cw = canvas_width_of(aspect);
    const int32_t ch = canvas_height_of(aspect);
    check(app.aspect() == aspect, name + ": the aspect is the new shape's");
    check(app.canvas() == CanvasLayout::of(aspect) && app.canvas() == CanvasLayout{cw, ch}, name + ": the canvas is " + std::to_string(cw) + " x " + std::to_string(ch));
    check(logical_size_is(app, cw, ch), name + ": SDL's logical size is the canvas");
    check(app.layout() == ScreenLayout::with_size(cw, ch), name + ": the match screen's layout is the canvas's");
    check(app.renderer().layout() == app.layout() && app.hud().layout() == app.layout(), name + ": the renderer and the HUD have the same layout");
    const LayoutRect view = app.layout().view();
    check(view.w == view_of(aspect).w && view.h == view_of(aspect).h, name + ": the map view is " + std::to_string(view_of(aspect).w) + " x " + std::to_string(view_of(aspect).h) + " (is " + std::to_string(view.w) + " x " + std::to_string(view.h) + ")");
    check(app.renderer().camera().viewport_w == view.w && app.renderer().camera().viewport_h == view.h, name + ": the camera's viewport is the view");
    check(same_rect(app.picture(), CanvasLayout::of(aspect).rect()) && same_rect(app.renderer().picture(), app.picture()), name + ": the match is the whole canvas, in the application and in the renderer");
}

// =====================================================================================================================================================
// Every shape to every shape
// =====================================================================================================================================================

void test_every_pair() {
    group("pairs", "from every shape to every shape, in a running match: canvas, layouts, view, picture, a frame in the whole canvas");
    AppRig rig(Aspect::Wide16x9);
    check(rig.ok, "the match starts");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.hud().dismiss_match_start_modal();
    int switches = 0;
    for (const Aspect from : kShapes4) {
        for (const Aspect to : kShapes4) {
            check(app.set_aspect(from), nm(from) + ": set_aspect says yes");
            check_shape(app, from, "(the start of the pair)");
            check(app.set_aspect(to), nm(from) + " to " + nm(to) + ": set_aspect says yes");
            check_shape(app, to, "after " + nm(from));
            const Picture p = frame_of(app);
            check(p.w == canvas_width_of(to) && p.h == canvas_height_of(to) && lit_share(p) > 0.4, nm(from) + " to " + nm(to) + ": a frame fills the new canvas (" + std::to_string(lit_share(p)) + " of it is lit)");
            ++switches;
        }
    }
    check(switches == 16, "16 pairs were walked");
    // the switch back to the first shape is what it was: the same layouts and the same picture, not just the same numbers
    check(app.set_aspect(Aspect::Wide16x9), "back to 16:9");
    check(app.layout() == ScreenLayout::with_size(960, 540) && same_rect(app.picture(), LayoutRect{0, 0, 960, 540}), "back at 16:9 the layout and the picture are the 16:9 ones exactly");
}

// =====================================================================================================================================================
// The camera and the zoom
// =====================================================================================================================================================

/// The world point at the middle of the view
void middle_of(Application& app, double& mx, double& my) {
    const ViewportCamera& c = app.renderer().camera();
    mx = static_cast<double>(c.x) + zoom::visible_exact(c.viewport_w, c.zoom) / 2.0;
    my = static_cast<double>(c.y) + zoom::visible_exact(c.viewport_h, c.zoom) / 2.0;
}

void test_camera_and_zoom() {
    group("camera", "the world point in the middle of the view stays in the middle; the zoom that the player chose comes back");
    AppRig rig(Aspect::Wide16x9);
    check(rig.ok, "the match starts");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.hud().dismiss_match_start_modal();
    const uint32_t map_w = app.sim().grid().width();
    const uint32_t map_h = app.sim().grid().height();
    ViewportCamera& cam = app.renderer().camera();
    // a world point that is in the middle of the map: every shape's view fits around it, unless the map is smaller than the view (then the map is centred in the view and the middle of the
    // view is the middle of the map, which is the same point)
    const double want_x = static_cast<double>(map_w) * TILE_SIZE / 2.0;                       // (the grid is in cells)
    const double want_y = static_cast<double>(map_h) * TILE_SIZE / 2.0;
    cam.set_origin(want_x - zoom::visible_exact(cam.viewport_w, cam.zoom) / 2.0, want_y - zoom::visible_exact(cam.viewport_h, cam.zoom) / 2.0, map_w, map_h);
    double mx = 0.0, my = 0.0;
    middle_of(app, mx, my);
    const double start_x = mx;
    const double start_y = my;
    for (const Aspect to : {Aspect::Ultra21x9, Aspect::Wide16x10, Aspect::Classic4x3, Aspect::Wide16x9}) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes");
        middle_of(app, mx, my);
        const double tolerance = 1.0;                                                         // (a view of an odd width has its middle between two pixels; level 1 rounds the origin)
        check(mx > start_x - tolerance && mx < start_x + tolerance && my > start_y - tolerance && my < start_y + tolerance,
              nm(to) + ": the middle of the view is still the world point (" + std::to_string(mx) + ", " + std::to_string(my) + ") from (" + std::to_string(start_x) + ", " + std::to_string(start_y) + ")");
    }
    // the zoom: a level that 16:9 offers is chosen (the closest-in and the farthest-out one), the other shapes take the nearest level that their view offers, and 16:9 brings back the chosen one
    const std::vector<float> levels = app.zoom_levels();
    check(levels.size() > 2, "the 16:9 view offers zoom levels");
    if (levels.size() <= 2) return;
    for (const float chosen : {levels.front(), levels.back()}) {
        const std::string what = "level " + std::to_string(chosen);
        check(app.set_aspect(Aspect::Wide16x9), what + ": at 16:9");
        const LayoutRect view = app.layout().view();
        app.set_zoom(chosen, view.w / 2, view.h / 2);
        check(app.remembered_zoom() == chosen && app.zoom() == chosen, what + ": it is chosen, remembered and in use");
        for (const Aspect to : {Aspect::Classic4x3, Aspect::Ultra21x9, Aspect::Wide16x10}) {
            check(app.set_aspect(to), what + ", " + nm(to) + ": set_aspect says yes (zoomed)");
            bool offered = false;
            for (const float l : app.zoom_levels()) offered = offered || l == app.zoom();
            check(offered, what + ", " + nm(to) + ": the zoom in use (" + std::to_string(app.zoom()) + ") is one that the new view offers");
            check(app.remembered_zoom() == chosen, what + ", " + nm(to) + ": the remembered zoom is untouched by the switch");
        }
        check(app.set_aspect(Aspect::Wide16x9), what + ": back to 16:9");
        check(app.zoom() == chosen && app.remembered_zoom() == chosen, what + ": back at 16:9 the chosen zoom is in use again (" + std::to_string(app.zoom()) + ")");
    }
    // the camera never shows beyond the map
    for (const Aspect to : kShapes4) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (bounds)");
        const ViewportCamera& c = app.renderer().camera();
        const double vis_w = zoom::visible_exact(c.viewport_w, c.zoom);
        const double vis_h = zoom::visible_exact(c.viewport_h, c.zoom);
        const double map_px_w = static_cast<double>(map_w) * TILE_SIZE;
        const double map_px_h = static_cast<double>(map_h) * TILE_SIZE;
        const bool wide_map = vis_w <= map_px_w;
        const bool tall_map = vis_h <= map_px_h;
        check((!wide_map || (c.x >= 0 && c.x + vis_w <= map_px_w + 1e-6)) && (!tall_map || (c.y >= 0 && c.y + vis_h <= map_px_h + 1e-6)), nm(to) + ": the view stays inside the map");
    }
}

// =====================================================================================================================================================
// The simulation does not see it
// =====================================================================================================================================================

/// The state hash after each of `ticks` ticks of a match; every `every` ticks (when it is more than 0) the shape changes to the next of the four
std::vector<uint64_t> hashes_of_a_run(int ticks, int every) {
    std::vector<uint64_t> hashes;
    AppRig rig(Aspect::Wide16x9);
    if (!rig.ok) return hashes;
    Application& app = rig.app;
    app.hud().dismiss_match_start_modal();
    int next = 0;
    for (int tick = 0; tick < ticks; ++tick) {
        if (every > 0 && tick % every == 0) {
            const Aspect to = kShapes4[next++ % 4];
            if (!app.set_aspect(to)) return {};
            app.render_frame();                                                              // (the frame in the new shape: the drawing reads the world, it must not change it)
        }
        app.update_simulation(0.05f);
        hashes.push_back(app.sim().state_hash().total);
    }
    return hashes;
}

/// The same, in a match that carries orders: three computer players (seats 1 - 3) play on TINY, their commands are the simulation's input
std::vector<uint64_t> hashes_of_a_bot_run(int ticks, int every, bool with_bots) {
    std::vector<uint64_t> hashes;
    ApplicationConfig cfg = base_config(Aspect::Wide16x9);
    cfg.start_in_map_select = true;
    cfg.play_at_once = true;                                                                 // (the setup screen's START path: the bots are seated and run)
    cfg.default_map_path = maps_dir() + "TINY.LVL";
    if (with_bots) {
        for (uint8_t seat = 1; seat <= 3; ++seat) {
            ai::BotSpec spec;
            spec.seat = seat;
            spec.level = ai::Level::Medium;
            cfg.bots.push_back(spec);
        }
    }
    const QuietStdout quiet;
    Application app;
    if (!app.init(cfg) || app.state() != AppState::Playing || (app.bots() != nullptr) != with_bots) return {};
    app.hud().dismiss_match_start_modal();
    int next = 0;
    for (int tick = 0; tick < ticks; ++tick) {
        if (every > 0 && tick % every == 0) {
            const Aspect to = kShapes4[next++ % 4];
            if (!app.set_aspect(to)) return {};
            app.render_frame();
        }
        app.update_simulation(0.05f);
        hashes.push_back(app.sim().state_hash().total);
    }
    return hashes;
}

void test_simulation_untouched() {
    group("sim", "the simulation is the one it would have been: the state hash after every tick equals that of a match that was never switched");
    constexpr int kTicks = 120;
    const std::vector<uint64_t> still = hashes_of_a_run(kTicks, 0);
    const std::vector<uint64_t> switched = hashes_of_a_run(kTicks, 7);
    const std::vector<uint64_t> often = hashes_of_a_run(kTicks, 1);
    check(still.size() == static_cast<size_t>(kTicks) && switched.size() == still.size() && often.size() == still.size(), "three runs of " + std::to_string(kTicks) + " ticks");
    if (still.size() != static_cast<size_t>(kTicks) || switched.size() != still.size() || often.size() != still.size()) return;
    size_t different = 0;
    for (size_t i = 1; i < still.size(); ++i) different += still[i] != still[i - 1] ? 1u : 0u;
    check(different >= 20, "the match does run: the hash changes with the ticks (" + std::to_string(different) + " of " + std::to_string(still.size() - 1) + " ticks changed it)");
    bool same_switched = true, same_often = true;
    for (size_t i = 0; i < still.size(); ++i) {
        same_switched = same_switched && switched[i] == still[i];
        same_often = same_often && often[i] == still[i];
    }
    check(same_switched, "switched every 7 ticks: the same hash at every tick");
    check(same_often, "switched before every tick: the same hash at every tick");

    // a match with orders in it: the computer players' commands go in, and the hash is still the unswitched match's at every tick
    constexpr int kBotTicks = 400;
    const std::vector<uint64_t> alone = hashes_of_a_bot_run(kBotTicks, 0, false);
    const std::vector<uint64_t> bots_still = hashes_of_a_bot_run(kBotTicks, 0, true);
    const std::vector<uint64_t> bots_switched = hashes_of_a_bot_run(kBotTicks, 9, true);
    const std::vector<uint64_t> bots_often = hashes_of_a_bot_run(kBotTicks, 1, true);
    check(alone.size() == static_cast<size_t>(kBotTicks) && bots_still.size() == alone.size() && bots_switched.size() == alone.size() && bots_often.size() == alone.size(),
          "four runs of " + std::to_string(kBotTicks) + " ticks on TINY (one without bots, three with)");
    if (alone.size() != static_cast<size_t>(kBotTicks) || bots_still.size() != alone.size() || bots_switched.size() != alone.size() || bots_often.size() != alone.size()) return;
    check(bots_still.back() != alone.back(), "the bots' orders change the match (the hash at the end is not the one of the match without them)");
    check(bots_switched == bots_still, "with bots, switched every 9 ticks: the same hash at every tick");
    check(bots_often == bots_still, "with bots, switched before every tick: the same hash at every tick");
}

// =====================================================================================================================================================
// The pages
// =====================================================================================================================================================

void test_pages() {
    group("pages", "the pages are the 960 x 540 page, centred in the wide canvases with black around, and the whole canvas in 4:3 and 16:9");
    AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false);
    check(rig.ok, "the application starts on its setup screen");
    if (!rig.ok) return;
    Application& app = rig.app;
    check(app.state() != AppState::Playing, "it is not in a match");
    park_pointer(app);
    const Picture reference = frame_of(app);                                                       // (the setup screen in 16:9: the page, which fills its canvas)
    check(reference.w == 960 && reference.h == 540 && lit_share(reference) > 0.2, "the setup screen of the 16:9 canvas is the 960 x 540 page, drawn");
    for (const Aspect to : kShapes4) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (a page)");
        const CanvasLayout canvas = CanvasLayout::of(to);
        const LayoutRect page = canvas.page();
        const bool whole = to == Aspect::Classic4x3 || to == Aspect::Wide16x9;
        check(app.canvas() == canvas && logical_size_is(app, canvas.width, canvas.height), nm(to) + ": the canvas is the shape's");
        check(whole ? same_rect(page, canvas.rect()) : (page.w == 960 && page.h == 540 && page.x == (canvas.width - 960) / 2 && page.y == (canvas.height - 540) / 2), nm(to) + ": the page is " + (whole ? "the whole canvas" : "the 960 x 540 page, centred"));
        check(same_rect(app.picture(), page) && same_rect(app.renderer().picture(), page), nm(to) + ": the picture on screen is the page, in the application and in the renderer");
        park_pointer(app);
        const Picture p = frame_of(app);
        const bool black_around = black_outside(p, page);
        check(black_around, nm(to) + ": nothing is drawn around the page");
        check(lit_share(p) > 0.2, nm(to) + ": the page is drawn");
        if (to == Aspect::Wide16x10 || to == Aspect::Ultra21x9) check(region_equals(p, page.x, page.y, reference), nm(to) + ": the page is the 16:9 page, pixel for pixel, at its place in the canvas");
    }
    // a match that starts after the switch has the shape's own screen
    for (const Aspect to : {Aspect::Ultra21x9, Aspect::Wide16x10}) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (before a match)");
        check(start_ticked_match(app, "GAUNTLET"), nm(to) + ": a match starts");
        check_shape(app, to, "in a match that started after the switch");
        check(app.state() == AppState::Playing, nm(to) + ": it is in its match");
        const Picture p = frame_of(app);
        check(lit_share(p) > 0.4, nm(to) + ": the match fills the canvas");
        // and the same application switches inside that match, then back to a page: the page of the shape
        check(app.set_aspect(Aspect::Classic4x3), "... and goes to 4:3 in it");
        check_shape(app, Aspect::Classic4x3, "in the match that began in another shape");
    }
}

// =====================================================================================================================================================
// The quick help, the results and the pointer
// =====================================================================================================================================================

void test_other_pages_and_pointer() {
    group("other pages", "the quick help and the results are pages too: the wide page, centred, drawn like the 16:9 one; the quick help's button stands where its page puts it; the pointer stays on the canvas");
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false);
        check(rig.ok, "the application starts on its setup screen");
        if (!rig.ok) return;
        Application& app = rig.app;
        app.finish_loading();                                                                         // (a game without a start menu: the quick help follows the loading screen)
        check(app.state() == AppState::QuickHelp, "the quick help is up");
        if (app.state() != AppState::QuickHelp) return;
        park_pointer(app);
        const Picture reference = frame_of(app);
        check(lit_share(reference) > 0.2, "the quick help of the 16:9 canvas is drawn");
        for (const Aspect to : {Aspect::Ultra21x9, Aspect::Wide16x10, Aspect::Classic4x3, Aspect::Wide16x9}) {
            check(app.set_aspect(to), nm(to) + ": set_aspect says yes (quick help)");
            const bool wide = to != Aspect::Classic4x3;
            const QuickHelpLayout& q = QuickHelpLayout::of(wide);
            const ButtonRect& up = app.quick_help_start_button().up_rect();
            check(up.x == q.start.x && up.y == q.start.y && up.w == q.start.w && up.h == q.start.h, nm(to) + ": the START button stands where the " + (wide ? "wide" : "classic") + " quick help puts it");
            const LayoutRect page = CanvasLayout::of(to).page();
            check(same_rect(app.picture(), page), nm(to) + ": the picture on screen is the page");
            park_pointer(app);
            const Picture p = frame_of(app);
            check(lit_share(p) > 0.2 && black_outside(p, page), nm(to) + ": the quick help is drawn, with nothing around the page");
            if (to == Aspect::Wide16x10 || to == Aspect::Ultra21x9) check(region_equals(p, page.x, page.y, reference), nm(to) + ": the quick help is the 16:9 page, pixel for pixel, at its place in the canvas");
        }
    }
    {
        AppRig rig(Aspect::Wide16x9);
        check(rig.ok, "a match starts");
        if (!rig.ok) return;
        Application& app = rig.app;
        app.hud().dismiss_match_start_modal();
        app.scorecard().show(app.sim().get_world_state().match_result, 0);                            // (a match that has ended: the results page)
        app.update_results(0.0f);
        check(app.scorecard().is_open() && !app.match_running(), "the results are up, the match is not on the screen");
        park_pointer(app);
        const Picture reference = frame_of(app);
        check(lit_share(reference) > 0.2, "the results of the 16:9 canvas are drawn");
        for (const Aspect to : {Aspect::Ultra21x9, Aspect::Wide16x10, Aspect::Classic4x3, Aspect::Wide16x9}) {
            check(app.set_aspect(to), nm(to) + ": set_aspect says yes (results)");
            const LayoutRect page = CanvasLayout::of(to).page();
            check(same_rect(app.picture(), page) && app.scorecard().is_open(), nm(to) + ": the picture on screen is the page, the results stay up");
            park_pointer(app);
            const Picture p = frame_of(app);
            check(lit_share(p) > 0.2 && black_outside(p, page), nm(to) + ": the results are drawn, with nothing around the page");
            if (to == Aspect::Wide16x10 || to == Aspect::Ultra21x9) check(region_equals(p, page.x, page.y, reference), nm(to) + ": the results are the 16:9 page, pixel for pixel, at its place in the canvas");
        }
    }
    // the pointer is a point of the picture on screen: it stays where it is on the canvas, and never outside the new picture (the nearest edge pixel)
    {
        AppRig rig(Aspect::Ultra21x9);
        check(rig.ok, "a 21:9 match starts");
        if (!rig.ok) return;
        Application& app = rig.app;
        app.hud().dismiss_match_start_modal();
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 1200;
        motion.y = 500;
        app.handle_mouse_motion(motion);
        check(app.mouse_screen_x() == 1200 && app.mouse_screen_y() == 500, "the pointer is at (1200, 500) of the 21:9 canvas");
        check(app.set_aspect(Aspect::Classic4x3), "21:9 to 4:3");
        check(app.mouse_screen_x() == 639 && app.mouse_screen_y() == 479, "... the pointer is at the 4:3 picture's nearest corner pixel (639, 479), not outside it");
        check(app.set_aspect(Aspect::Ultra21x9), "... and back to 21:9");
        check(app.mouse_screen_x() == 639 && app.mouse_screen_y() == 479, "... it stays where it was");
        check(app.set_aspect(Aspect::Wide16x10), "21:9 to 16:10");
        check(app.mouse_screen_x() == 639 && app.mouse_screen_y() == 479, "... and on to 16:10: the same canvas point (inside it)");
    }
    {
        AppRig rig(Aspect::Wide16x9, 1.0f, false, std::string(), false);                             // the setup screen: a page, whose pointer is the page's own
        check(rig.ok, "the setup screen starts");
        if (!rig.ok) return;
        Application& app = rig.app;
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 300;
        motion.y = 200;
        app.handle_mouse_motion(motion);
        check(app.set_aspect(Aspect::Ultra21x9), "16:9 to 21:9 (a page)");
        check(app.mouse_screen_x() == 150 && app.mouse_screen_y() == 200, "... the pointer keeps its place on the canvas: the page's own x is 150 less (the page starts at x 150)");
        check(app.set_aspect(Aspect::Wide16x10), "21:9 to 16:10 (a page)");
        check(app.mouse_screen_x() == 300 && app.mouse_screen_y() == 170, "... on the 16:10 canvas it is at (300, 200), the page's own (300, 170) (the page starts at y 30)");
        check(app.set_aspect(Aspect::Classic4x3), "16:10 to 4:3 (a page)");
        check(app.mouse_screen_x() == 300 && app.mouse_screen_y() == 200, "... and on the 4:3 canvas, whose page is the whole canvas, (300, 200)");
    }
}

// =====================================================================================================================================================
// Dialogs and the loading screen of a catch-up
// =====================================================================================================================================================

void test_dialogs_and_catch_up() {
    group("dialogs", "a dialog that is open stays open and is drawn in the new shape; the catch-up screen is the centred page in the wide shapes");
    AppRig rig(Aspect::Wide16x9);
    check(rig.ok, "the match starts");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.hud().start_match_modal(true);                                                              // ("Get ready to play!", until dismissed)
    check(app.hud().is_match_start_modal_active(), "the start dialog is up");
    for (const Aspect to : {Aspect::Ultra21x9, Aspect::Classic4x3, Aspect::Wide16x10, Aspect::Wide16x9}) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (start dialog)");
        check(app.hud().is_match_start_modal_active(), nm(to) + ": the start dialog is still open");
        const Picture p = frame_of(app);
        check(lit_share(p) > 0.4, nm(to) + ": a frame with the dialog is drawn (" + std::to_string(lit_share(p)) + ")");
    }
    app.hud().dismiss_match_start_modal();
    app.hud().open_options();
    check(app.hud().is_options_open(), "the options window is open");
    for (const Aspect to : {Aspect::Wide16x10, Aspect::Ultra21x9, Aspect::Classic4x3, Aspect::Wide16x9}) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (options)");
        check(app.hud().is_options_open(), nm(to) + ": the options window is still open");
        check(lit_share(frame_of(app)) > 0.4, nm(to) + ": a frame with the options window is drawn");
    }
    app.hud().close_options();
    app.hud().open_quit_dialog();
    for (const Aspect to : {Aspect::Ultra21x9, Aspect::Wide16x9}) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (quit dialog)");
        check(app.hud().is_quit_dialog_open(), nm(to) + ": the quit dialog is still open");
        check(lit_share(frame_of(app)) > 0.4, nm(to) + ": a frame with the quit dialog is drawn");
    }
    app.hud().close_quit_dialog();
    // the loading screen of a machine that catches up to the server's log: drawn as a page (centred in the wide shapes), whatever the match's own picture is
    app.force_catch_up_screen_for_test(true);
    for (const Aspect to : {Aspect::Ultra21x9, Aspect::Wide16x10, Aspect::Classic4x3, Aspect::Wide16x9}) {
        check(app.set_aspect(to), nm(to) + ": set_aspect says yes (catch-up screen)");
        const Picture p = frame_of(app);
        check(lit_share(p) > 0.2, nm(to) + ": the catch-up screen is drawn");
        check(black_outside(p, CanvasLayout::of(to).page()), nm(to) + ": nothing is drawn around the catch-up page");
    }
    app.force_catch_up_screen_for_test(false);
}

// =====================================================================================================================================================
// The guards
// =====================================================================================================================================================

void test_guards() {
    group("guards", "an application that never started, and the shape it already has");
    {
        Application never;
        check(!never.set_aspect(Aspect::Ultra21x9), "an application that never started has no canvas to change: set_aspect says no");
    }
    AppRig rig(Aspect::Wide16x10);
    check(rig.ok, "a 16:10 match starts");
    if (!rig.ok) return;
    Application& app = rig.app;
    app.hud().dismiss_match_start_modal();
    for (int i = 0; i < 4; ++i) app.update_simulation(0.05f);
    const uint64_t hash = app.sim().state_hash().total;
    const double cx = app.renderer().camera().x;
    const double cy = app.renderer().camera().y;
    check(app.set_aspect(Aspect::Wide16x10), "the shape it already has: set_aspect says yes");
    check_shape(app, Aspect::Wide16x10, "(the same shape asked for again)");
    check(app.renderer().camera().x == cx && app.renderer().camera().y == cy, "... and nothing moves");
    check(app.sim().state_hash().total == hash, "... and the simulation is as it was");
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                 // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // (the environment survives SDL_Quit, which the end of an application calls; a hint does not): nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    test_every_pair();
    test_camera_and_zoom();
    test_simulation_untouched();
    test_pages();
    test_other_pages_and_pointer();
    test_dialogs_and_catch_up();
    test_guards();
    std::printf("\naspect switch: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
