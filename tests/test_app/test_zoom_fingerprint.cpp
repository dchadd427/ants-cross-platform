// Zoom fingerprint (milestone M4 of the widescreen work): what the game DRAWS and what the pointer DOES at the zoom levels 0.5 and 2, pinned as 64-bit numbers in the way that
// test_view_fingerprint.cpp pins the picture at the zoom 1 (that program, whose 551 numbers did not move when the zoom came, is the oracle of the zoom 1: at the zoom 1 the game draws
// exactly what it always drew). The classic 640 x 480 picture and the 16:9 picture of 960 x 540 each have their own numbers.
//   * "zoom.world.*": the pixels of the real software renderer's world pass (Renderer::render_world) on GAUNTLET and TINY at the corners and the middle of the map, from a half-pixel origin
//     (the zoom 2), with ants of every type and colour, effects, score bubbles, selection markers, the click marker, hit point digits and the fog of war;
//   * "zoom.app.*": whole frames of a headless Application (the HUD, the minimap with its frame of the seen world, the world) on both maps, the text boxes masked as in the zoom 1 program,
//     taken at once after the start: before the first simulation tick an ant has no clip, so its hit point digits are drawn and its sprite is not;
//   * "zoom.app.ants.*": the same frames of a match that has TICKED (the ants have their clips and are drawn), at 0.5, 1 and 2: before the first tick only the ants' hit point digits show;
//   * "zoom.radar.*": the rectangle that the minimap's frame is, for nine cameras;
//   * "zoom.ptr.edge.*" and "zoom.ptr.cursor.*": for EVERY pixel of the picture, the step of the edge scroll in screen pixels (nine cameras) and the cursor that HUD::evaluate_cursor chooses
//     (two selections); "zoom.ptr.click.*": what a click orders (the tile, the marker's world pixel) on a grid of pixels; "zoom.camera.*": the camera's conversions and clamps.
// The numbers are the same on macOS (clang, SDL 2.32) and on Linux (gcc, SDL 2.26): the pixel hashes never contain TrueType text and the zoom 0.5's smoothing is SDL's 2 x 2 average on both.
// Usage: test_zoom_fingerprint [--print | --list | --save DIR | --only PREFIX]; --print regenerates the table (DELIBERATELY, never to silence a failure).
#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/application.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/view_zoom.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

namespace {

// =====================================================================================================================================================
// Hashing, goldens, reporting
// =====================================================================================================================================================

class Fnv64 {
public:
    void byte(uint8_t b) noexcept {
        h_ ^= b;
        h_ *= 1099511628211ULL;
    }
    void u32(uint32_t v) noexcept {
        for (unsigned i = 0; i < 4; ++i) byte(static_cast<uint8_t>((v >> (8u * i)) & 0xFFu));
    }
    void i32(int32_t v) noexcept { u32(static_cast<uint32_t>(v)); }
    void flag(bool v) noexcept { byte(v ? 1 : 0); }
    void bytes(const uint8_t* p, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) byte(p[i]);
    }
    uint64_t value() const noexcept { return h_; }

private:
    uint64_t h_{14695981039346656037ULL};
};

struct Measure {
    uint64_t hash{0};
    uint64_t count{0};
};

struct Golden {
    const char* name;
    uint64_t hash;
    uint64_t count;
};

/// The golden numbers (the table at the end of the file)
const Golden* golden_table(size_t& count);

int g_checks = 0;
int g_failures = 0;
bool g_print = false;
bool g_list = false;
std::string g_only_prefix;
std::string g_save_dir;
std::map<std::string, Measure> g_recorded;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what.c_str());
    }
}

bool wanted(const std::string& name) { return g_only_prefix.empty() || name.compare(0, g_only_prefix.size(), g_only_prefix) == 0; }

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

const Golden* find_golden(const std::string& name) {
    size_t count = 0;
    const Golden* table = golden_table(count);
    for (size_t i = 0; i < count; ++i) {
        if (name == table[i].name) return &table[i];
    }
    return nullptr;
}

void record(const std::string& name, const Measure& m) {
    if (g_recorded.count(name) != 0) {
        check(false, "scenario " + name + " recorded twice");
        return;
    }
    g_recorded[name] = m;
    if (g_list) {
        std::printf("%s\n", name.c_str());
        return;
    }
    if (g_print) return;
    const Golden* g = find_golden(name);
    if (g == nullptr) {
        check(false, "no golden number for " + name + ": " + hex64(m.hash) + " / " + std::to_string(m.count));
        return;
    }
    ++g_checks;
    if (g->hash != m.hash || g->count != m.count) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: expected hash %s / %llu, actual hash %s / %llu\n", name.c_str(), hex64(g->hash).c_str(), static_cast<unsigned long long>(g->count), hex64(m.hash).c_str(),
                     static_cast<unsigned long long>(m.count));
    }
}

/// Records a canvas of the renderer (RGBA), with masked rectangles cleared first; --save writes it as a BMP (the masked pixels magenta)
struct MaskRect {
    int32_t x0, y0, x1, y1;
};

void record_picture(const std::string& name, Picture p, const std::vector<MaskRect>& mask = {}) {
    if (!wanted(name)) return;
    std::vector<uint8_t> shown;
    if (!g_save_dir.empty()) shown = p.px;
    for (const MaskRect& m : mask) {
        for (int32_t y = std::max(0, m.y0); y < std::min(p.h, m.y1); ++y) {
            for (int32_t x = std::max(0, m.x0); x < std::min(p.w, m.x1); ++x) {
                uint8_t* q = &p.px[(static_cast<size_t>(y) * static_cast<size_t>(p.w) + static_cast<size_t>(x)) * 4u];
                std::memset(q, 0, 4);
                if (!shown.empty()) {
                    uint8_t* r = &shown[(static_cast<size_t>(y) * static_cast<size_t>(p.w) + static_cast<size_t>(x)) * 4u];
                    r[0] = 255;
                    r[1] = 0;
                    r[2] = 255;
                }
            }
        }
    }
    if (!g_save_dir.empty()) {
        for (size_t i = 0; i < shown.size(); i += 4) shown[i + 3] = 255;
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(shown.data(), p.w, p.h, 32, p.w * 4, SDL_PIXELFORMAT_RGBA32);
        if (s != nullptr) {
            SDL_SaveBMP(s, (g_save_dir + "/" + name + ".bmp").c_str());
            SDL_FreeSurface(s);
        }
    }
    Fnv64 h;
    h.bytes(p.px.data(), p.px.size());
    record(name, Measure{h.value(), static_cast<uint64_t>(p.w) * static_cast<uint64_t>(p.h)});
}

// =====================================================================================================================================================
// The world pass
// =====================================================================================================================================================

struct Cam {
    const char* name;
    double x;
    double y;
};

/// The cameras of a view over a map of map_w x map_h pixels: the corners and the middle (the camera clamps what does not fit)
std::vector<Cam> cams_of(int32_t map_w, int32_t map_h) {
    return {{"tl", 0.0, 0.0}, {"mid", map_w / 2.0 - 200.0, map_h / 2.0 - 160.0}, {"br", 1.0e9, 1.0e9}, {"tr", 1.0e9, 0.0}, {"bl", 0.0, 1.0e9}};
}

void world_scenarios(const assets::AssetArchive& arc) {
    for (const Shape& s : kShapes) {
        for (const char* map : {"GAUNTLET", "TINY"}) {
            for (const float z : {0.5f, 2.0f}) {
                std::vector<Cam> cams;
                {
                    PixelRig probe(arc, map, s.cw, s.ch);
                    if (!probe.ok) {
                        check(false, std::string("the rig for ") + map + " is up");
                        continue;
                    }
                    cams = cams_of(static_cast<int32_t>(probe.tiles_w) * 32, static_cast<int32_t>(probe.tiles_h) * 32);
                }
                for (const Cam& c : cams) {
                    const std::string name = std::string("zoom.world.") + s.name + "." + map + ".z" + zoom::level_name(z) + ".cam_" + c.name;
                    if (!wanted(name)) continue;
                    PixelRig rig(arc, map, s.cw, s.ch);
                    if (!rig.ok) continue;
                    rig.look(c.x, c.y, z);
                    const LayoutRect view = rig.layout.view();
                    const int32_t wx = static_cast<int32_t>(rig.cam().x);
                    const int32_t wy = static_cast<int32_t>(rig.cam().y);
                    std::vector<uint32_t> ids;
                    const int32_t seen_w = zoom::visible(view.w, z);
                    const int32_t seen_h = zoom::visible(view.h, z);
                    populate(rig.world, arc, wx, wy, seen_w, seen_h, 120, 4242u + static_cast<uint32_t>(c.x));
                    add_effects(rig.world, wx, wy, seen_w, seen_h);
                    rig.renderer.spawn_transient_effect("xmarks", wx + 120, wy + 90);
                    for (uint32_t i = 0; i < 120; i += 4) ids.push_back(5000u + i);
                    record_picture(name, rig.shoot(static_cast<int32_t>(ids.front()), ids, 0, true));
                }
            }
        }
        // the half-pixel origin of the zoom 2, and the fog of war, on GAUNTLET
        for (const bool fog : {false, true}) {
            const std::string name = std::string("zoom.world.") + s.name + ".GAUNTLET.z2" + (fog ? ".fog" : ".half_pixel");
            if (!wanted(name)) continue;
            PixelRig rig(arc, "GAUNTLET", s.cw, s.ch);
            if (!rig.ok) continue;
            rig.look(601.5, 700.5, 2.0f);
            const LayoutRect view = rig.layout.view();
            populate(rig.world, arc, 601, 700, view.w / 2 + 1, view.h / 2 + 1, 100, 99u);
            if (fog) reveal_some(rig.world);
            record_picture(name, rig.shoot(-1, {}, 0, false));
        }
        {
            const std::string name = std::string("zoom.world.") + s.name + ".GAUNTLET.z0.5.fog";
            if (wanted(name)) {
                PixelRig rig(arc, "GAUNTLET", s.cw, s.ch);
                if (rig.ok) {
                    rig.look(100.0, 160.0, 0.5f);
                    const LayoutRect view = rig.layout.view();
                    populate(rig.world, arc, 100, 160, view.w * 2, view.h * 2, 200, 17u);
                    reveal_some(rig.world);
                    record_picture(name, rig.shoot(-1, {}, 0, false));
                }
            }
        }
    }
}

// =====================================================================================================================================================
// Whole frames of an application
// =====================================================================================================================================================

/// TrueType text is the one thing whose pixels depend on the machine's font library: the places where the picture has text are masked (fixed rectangles), as in the zoom 1 program
const MaskRect kMaskPlate{470, 466, 640, 480};
const std::vector<MaskRect> kMaskMatch = {{479, 252, 622, 268}, {480, 297, 622, 402}, {479, 422, 622, 438}, {310, 2, 402, 20}, {3, 462, 103, 480}, {161, 462, 253, 480}, {310, 462, 401, 480}};
const MaskRect kWMaskPlate{790, 526, 960, 540};
// (the bottom score labels right aligned in [113, 209), [377, 465) and [632, 719) x 524 of the 960 x 540 picture: the masks of the two left ones lay at 323 .. 423 and 481 .. 573 in the first version of this
// table, beside their labels, so "Red:" and "Blue:" were hashed, and SDL's alpha blit rounds differently on x86-64 and on ARM: the masks of tests/test_app/test_view_fingerprint.cpp's kWMaskMatch, corrected by the CI)
const std::vector<MaskRect> kWMaskMatch = {{799, 252, 942, 268}, {800, 297, 942, 462}, {799, 482, 942, 498}, {630, 2, 722, 20}, {111, 522, 211, 540}, {375, 522, 467, 540}, {630, 522, 722, 540}};

void app_scenarios() {
    for (const Shape& s : kShapes) {
        const bool wide = s.cw == kWideW;
        for (const char* map : {"GAUNTLET", "TINY"}) {
            for (const float z : {0.5f, 2.0f}) {
                const std::string name = std::string("zoom.app.") + s.name + "." + map + ".z" + zoom::level_name(z);
                if (!wanted(name)) continue;
                AppRig rig(wide ? Aspect::Wide16x9 : Aspect::Classic4x3, z, true);
                if (!rig.ok) {
                    check(false, "the application is up");
                    continue;
                }
                Application& app = rig.app;
                app.renderer().pin_animation_clock(1500);
                if (!app.start_game(maps_dir() + map + ".LVL")) {
                    check(false, std::string("the match on ") + map + " starts");
                    continue;
                }
                app.hud().dismiss_match_start_modal();
                app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_LEAVE; return we; }());     // (no game cursor in the picture)
                app.render_frame();
                Picture p;
                p.w = wide ? kWideW : kClassicW;
                p.h = wide ? kWideH : kClassicH;
                p.px.assign(static_cast<size_t>(p.w) * static_cast<size_t>(p.h) * 4u, 0);
                SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, p.px.data(), p.w * 4);
                std::vector<MaskRect> mask = wide ? kWMaskMatch : kMaskMatch;
                mask.push_back(wide ? kWMaskPlate : kMaskPlate);
                record_picture(name, p, mask);
            }
        }
    }
}

/// The same frames of a match that has TICKED (the start modal dismissed, six simulation ticks of 50 ms), at the zoom 0.5, 1 and 2: the ants are in the picture. BEFORE the first tick an
/// ant has no clip and no sprite, only its hit point digits are drawn, which is what the frames of "zoom.app.*" above (taken at once after the start, like the "px.app.match.*" of
/// suite 3.10) show. These pin the sprites of the ants through the whole path of the application: the simulation's clips, the camera, the offscreen pass at a zoom and the copy.
void app_ants_scenarios() {
    for (const Shape& s : kShapes) {
        const bool wide = s.cw == kWideW;
        for (const char* map : {"GAUNTLET", "TINY"}) {
            for (const float z : {0.5f, 1.0f, 2.0f}) {
                const std::string name = std::string("zoom.app.ants.") + s.name + "." + map + ".z" + zoom::level_name(z);
                if (!wanted(name)) continue;
                AppRig rig(wide ? Aspect::Wide16x9 : Aspect::Classic4x3, z, true);
                if (!rig.ok) {
                    check(false, "the application is up");
                    continue;
                }
                Application& app = rig.app;
                app.renderer().pin_animation_clock(1500);
                app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_LEAVE; return we; }());     // (no game cursor in the picture)
                if (!app.start_game(maps_dir() + map + ".LVL")) {
                    check(false, std::string("the match on ") + map + " starts");
                    continue;
                }
                app.hud().dismiss_match_start_modal();
                check(app.zoom() == z, name + ": the match is at the zoom that was asked for");
                const LayoutRect view = app.layout().view();
                app.render_frame();
                const Picture before = read_canvas(app, s.cw, s.ch);                       // (at tick 0: no sprites, only digits)
                for (int tick = 0; tick < 6; ++tick) app.update_simulation(0.05f);
                check(app.sim().current_tick() >= 3, name + ": the simulation has ticked");
                app.render_frame();
                Picture p = read_canvas(app, s.cw, s.ch);
                // the ants are in the picture: after the ticks the view differs from the view before them by the sprites (the digits and the terrain are the same in both)
                const int sprites = differ(before, p, view);
                check(sprites > 300, name + ": the ants are drawn after the first ticks: " + std::to_string(sprites) + " pixels of the view are not what the frame before the first tick shows");
                std::vector<MaskRect> mask = wide ? kWMaskMatch : kMaskMatch;
                mask.push_back(wide ? kWMaskPlate : kMaskPlate);
                record_picture(name, p, mask);
            }
        }
    }
}

// =====================================================================================================================================================
// The pointer
// =====================================================================================================================================================

struct SweepCam {
    const char* name;
    double x;
    double y;
};
const SweepCam kSweepCams[9] = {{"tl", 0.0, 0.0},   {"t", 400.0, 0.0},   {"tr", 1.0e9, 0.0},   {"l", 0.0, 500.0},     {"mid", 600.0, 500.0},
                                {"r", 1.0e9, 500.0}, {"bl", 0.0, 1.0e9}, {"b", 400.0, 1.0e9}, {"br", 1.0e9, 1.0e9}};

void edge_scenarios(const assets::AssetArchive& arc) {
    for (const bool wide : {false, true}) {
        const char* shape = wide ? "wide" : "classic";
        HudRig rig(arc, wide);
        const int32_t pw = rig.layout.width;
        const int32_t ph = rig.layout.height;
        for (const float z : {0.5f, 2.0f}) {
            for (const SweepCam& c : kSweepCams) {
                const std::string name = std::string("zoom.ptr.edge.") + shape + ".z" + zoom::level_name(z) + ".cam_" + c.name;
                if (!wanted(name)) continue;
                rig.look(c.x, c.y, z);
                Fnv64 h;
                uint64_t n = 0;
                for (int32_t y = 0; y < ph; ++y) {
                    for (int32_t x = 0; x < pw; ++x) {
                        const EdgeScroll e = rig.hud.edge_step(rig.camera, x, y, 50, 60, 60);
                        h.i32(e.dir);
                        h.i32(e.dx);
                        h.i32(e.dy);
                        ++n;
                    }
                }
                record(name, Measure{h.value(), n});
            }
        }
    }
}

void cursor_scenarios(const assets::AssetArchive& arc) {
    for (const bool wide : {false, true}) {
        const char* shape = wide ? "wide" : "classic";
        for (const float z : {0.5f, 2.0f}) {
            for (const char* state : {"idle", "own_worker"}) {
                for (const char* where : {"foe", "mid"}) {
                    const std::string name = std::string("zoom.ptr.cursor.") + shape + ".z" + zoom::level_name(z) + "." + state + "." + where;
                    if (!wanted(name)) continue;
                    HudRig rig(arc, wide);
                    if (std::string(state) == "own_worker") rig.hud.select_ant(rig.mine);
                    rig.camera.zoom = z;
                    if (std::string(where) == "foe") rig.camera.center_on(rig.ant(rig.foe).px - 60, rig.ant(rig.foe).py, 60, 60);
                    else rig.look(600.0, 500.0, z);
                    Fnv64 h;
                    uint64_t n = 0;
                    for (int32_t y = 0; y < rig.layout.height; ++y) {
                        for (int32_t x = 0; x < rig.layout.width; ++x) {
                            h.byte(static_cast<uint8_t>(rig.hud.evaluate_cursor(x, y, rig.world(), rig.sim.grid(), rig.camera)));
                            ++n;
                        }
                    }
                    record(name, Measure{h.value(), n});
                }
            }
        }
    }
}

void click_scenarios(const assets::AssetArchive& arc) {
    for (const bool wide : {false, true}) {
        const char* shape = wide ? "wide" : "classic";
        for (const float z : {0.5f, 2.0f}) {
            for (const SweepCam& c : {kSweepCams[0], kSweepCams[4], kSweepCams[8]}) {
                const std::string name = std::string("zoom.ptr.click.") + shape + ".z" + zoom::level_name(z) + ".cam_" + c.name;
                if (!wanted(name)) continue;
                HudRig rig(arc, wide);
                const LayoutRect view = rig.layout.view();
                Fnv64 h;
                uint64_t n = 0;
                for (int32_t y = view.y; y < view.bottom(); y += 5) {
                    for (int32_t x = view.x; x < view.right(); x += 7) {
                        rig.look(c.x, c.y, z);
                        rig.hud.select_ant(rig.mine);
                        rig.sink.commands.clear();
                        rig.markers.clear();
                        rig.click(x, y);
                        h.u32(static_cast<uint32_t>(rig.sink.commands.size()));
                        for (const auto& cmd : rig.sink.commands) {
                            h.byte(static_cast<uint8_t>(cmd.type));
                            h.i32(cmd.tile_x);
                            h.i32(cmd.tile_y);
                        }
                        h.u32(static_cast<uint32_t>(rig.markers.size()));
                        for (const auto& m : rig.markers) {
                            h.i32(m.first);
                            h.i32(m.second);
                        }
                        h.byte(static_cast<uint8_t>(rig.hud.get_selected_ant_ids().size()));
                        ++n;
                    }
                }
                record(name, Measure{h.value(), n});
            }
        }
    }
}

void radar_and_camera_scenarios(const assets::AssetArchive& arc) {
    for (const bool wide : {false, true}) {
        const char* shape = wide ? "wide" : "classic";
        for (const float z : {0.5f, 2.0f}) {
            {
                const std::string name = std::string("zoom.radar.") + shape + ".z" + zoom::level_name(z);
                if (wanted(name)) {
                    HudRig rig(arc, wide);
                    Fnv64 h;
                    uint64_t n = 0;
                    for (const SweepCam& c : kSweepCams) {
                        rig.look(c.x, c.y, z);
                        RectRenderer rec;
                        rig.hud.render(rec, rig.arc, rig.world(), rig.camera);
                        for (const auto& r : rec.frames) {
                            if (r.colour.r != 251 || r.colour.g != 251 || r.colour.b != 255) continue;
                            h.i32(r.x);
                            h.i32(r.y);
                            h.i32(r.w);
                            h.i32(r.h);
                            ++n;
                        }
                    }
                    record(name, Measure{h.value(), n});
                }
            }
            {
                const std::string name = std::string("zoom.camera.") + shape + ".z" + zoom::level_name(z);
                if (wanted(name)) {
                    const LayoutRect view = (wide ? ScreenLayout::with_size(960, 540) : ScreenLayout::classic()).view();
                    Fnv64 h;
                    uint64_t n = 0;
                    for (const int32_t tiles : {60, 40, 31, 16, 12}) {
                        for (const SweepCam& c : kSweepCams) {
                            ViewportCamera cam;
                            cam.set_view(view);
                            cam.centre_small_maps = wide;
                            cam.zoom = z;
                            cam.set_origin(c.x, c.y, static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
                            h.i32(static_cast<int32_t>(cam.x * 4.0f));
                            h.i32(static_cast<int32_t>(cam.y * 4.0f));
                            h.i32(cam.world_x);
                            h.i32(cam.world_y);
                            h.i32(cam.centre_world_x());
                            h.i32(cam.centre_world_y());
                            for (int32_t d = 0; d < view.w; d += 37) {
                                h.i32(cam.world_x_at(d));
                                h.i32(cam.world_x_edge(d));
                            }
                            for (int32_t d = 0; d < view.h; d += 41) {
                                h.i32(cam.world_y_at(d));
                                h.i32(cam.world_y_edge(d));
                            }
                            for (int32_t wx = 0; wx < tiles * 32; wx += 211) {
                                int32_t sx = 0;
                                int32_t sy = 0;
                                h.flag(cam.world_to_screen(wx, wx / 2, sx, sy));
                                h.i32(sx);
                                h.i32(sy);
                            }
                            ++n;
                        }
                    }
                    record(name, Measure{h.value(), n});
                }
            }
        }
    }
}

// =====================================================================================================================================================
// The self-check: the fingerprints can tell what they are meant to tell apart
// =====================================================================================================================================================

void self_check(const assets::AssetArchive& arc) {
    if (!g_only_prefix.empty() || g_print || g_list) return;
    // a pixel hash moves with the zoom, with the origin (one half pixel), with the map and with the picture's shape
    auto hash_of = [&](const char* map, int32_t cw, int32_t ch, double ox, double oy, float z) {
        PixelRig rig(arc, map, cw, ch);
        rig.look(ox, oy, z);
        Picture p = rig.shoot(-1, {}, 0, true);
        Fnv64 h;
        h.bytes(p.px.data(), p.px.size());
        return h.value();
    };
    const uint64_t base = hash_of("GAUNTLET", kWideW, kWideH, 600.0, 500.0, 2.0f);
    check(base == hash_of("GAUNTLET", kWideW, kWideH, 600.0, 500.0, 2.0f), "the same scene gives the same hash twice");
    check(base != hash_of("GAUNTLET", kWideW, kWideH, 600.5, 500.0, 2.0f), "a half pixel of the origin changes the hash");
    check(base != hash_of("GAUNTLET", kWideW, kWideH, 600.0, 500.5, 2.0f), "... in y too");
    check(base != hash_of("GAUNTLET", kWideW, kWideH, 600.0, 500.0, 0.5f), "the zoom changes the hash");
    check(base != hash_of("TINY", kWideW, kWideH, 600.0, 500.0, 2.0f), "the map changes the hash");
    check(base != hash_of("GAUNTLET", kClassicW, kClassicH, 600.0, 500.0, 2.0f), "the picture's shape changes the hash");
    // the pointer: a step of the scroll moves with the zoom's origin in screen pixels
    HudRig rig(arc, true);
    rig.look(600.0, 500.0, 2.0f);
    const EdgeScroll a = rig.hud.edge_step(rig.camera, 955, 270, 50, 60, 60);
    rig.look(0.0, 500.0, 2.0f);
    const EdgeScroll b = rig.hud.edge_step(rig.camera, 5, 270, 50, 60, 60);
    check(a.dir == 2 && b.dir == -1, "the east arrow shows from the middle of the map and no west arrow shows at its west edge (the self-check of the sweeps' premise)");
}

}  // namespace

// =====================================================================================================================================================
// main and the golden numbers
// =====================================================================================================================================================

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--print") g_print = true;
        else if (a == "--list") g_list = true;
        else if (a == "--only" && i + 1 < argc) g_only_prefix = argv[++i];
        else if (a == "--save" && i + 1 < argc) g_save_dir = argv[++i];
        else {
            std::fprintf(stderr, "usage: test_zoom_fingerprint [--print | --list | --save DIR | --only PREFIX]\n");
            return 2;
        }
    }
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // (the environment survives SDL_Quit, which the end of an application calls; a hint does not): nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    assets::AssetArchive arc;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot open ants.chd\n");
        return 2;
    }
    world_scenarios(arc);
    app_scenarios();
    app_ants_scenarios();
    radar_and_camera_scenarios(arc);
    edge_scenarios(arc);
    cursor_scenarios(arc);
    click_scenarios(arc);
    self_check(arc);

    if (g_print) {
        std::printf("// the golden table (%zu fingerprints)\n", g_recorded.size());
        for (const auto& kv : g_recorded) std::printf("    {\"%s\", %s, %llu},\n", kv.first.c_str(), hex64(kv.second.hash).c_str(), static_cast<unsigned long long>(kv.second.count));
        return 0;
    }
    if (g_list) return 0;
    if (g_only_prefix.empty()) {
        size_t count = 0;
        const Golden* table = golden_table(count);
        for (size_t i = 0; i < count; ++i) {
            ++g_checks;
            if (g_recorded.count(table[i].name) == 0) {
                ++g_failures;
                std::fprintf(stderr, "  FAIL: the golden number %s has no scenario\n", table[i].name);
            }
        }
    }
    std::printf("\nzoom fingerprint: %zu fingerprints, %d checks, %d failures\n", g_recorded.size(), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

namespace {
const Golden kGoldens[] = {
    {"zoom.app.ants.classic.GAUNTLET.z0.5", 0xf7f5136c579d55c3, 307200},
    {"zoom.app.ants.classic.GAUNTLET.z1", 0x0c86ca52919c34b3, 307200},
    {"zoom.app.ants.classic.GAUNTLET.z2", 0x711fb33f8873d65c, 307200},
    {"zoom.app.ants.classic.TINY.z0.5", 0x33b424e3be64836a, 307200},
    {"zoom.app.ants.classic.TINY.z1", 0x395b1b5428b0ef86, 307200},
    {"zoom.app.ants.classic.TINY.z2", 0x654d61ede4479dc0, 307200},
    {"zoom.app.ants.wide.GAUNTLET.z0.5", 0x86eb3b0f2d57563c, 518400},
    {"zoom.app.ants.wide.GAUNTLET.z1", 0x1fe1af1bc5d13ae7, 518400},
    {"zoom.app.ants.wide.GAUNTLET.z2", 0x037a90b9cd9d5067, 518400},
    {"zoom.app.ants.wide.TINY.z0.5", 0x6b02b92ef6ff8bc4, 518400},
    {"zoom.app.ants.wide.TINY.z1", 0x85a3d695f1432492, 518400},
    {"zoom.app.ants.wide.TINY.z2", 0x32217ff583631ba9, 518400},
    {"zoom.app.classic.GAUNTLET.z0.5", 0x52ecda6eb4ca0562, 307200},
    {"zoom.app.classic.GAUNTLET.z2", 0x0752deb3e7e2313c, 307200},
    {"zoom.app.classic.TINY.z0.5", 0x51ad00b81a4182c3, 307200},
    {"zoom.app.classic.TINY.z2", 0x1f97ee96bf5bb05d, 307200},
    {"zoom.app.wide.GAUNTLET.z0.5", 0x755aaffcd02bd6df, 518400},
    {"zoom.app.wide.GAUNTLET.z2", 0xc7817479071f3f8f, 518400},
    {"zoom.app.wide.TINY.z0.5", 0xb98b45312ada3849, 518400},
    {"zoom.app.wide.TINY.z2", 0x66678974b731f9f0, 518400},
    {"zoom.camera.classic.z0.5", 0x3a4dad0e646af9cc, 45},
    {"zoom.camera.classic.z2", 0x898a58705f5b9fc1, 45},
    {"zoom.camera.wide.z0.5", 0x4625c85a49d212b7, 45},
    {"zoom.camera.wide.z2", 0x46de1d5d789f8c16, 45},
    {"zoom.ptr.click.classic.z0.5.cam_br", 0x5a0ee4fe8ab188c7, 5632},
    {"zoom.ptr.click.classic.z0.5.cam_mid", 0xeb731e609ddbea9e, 5632},
    {"zoom.ptr.click.classic.z0.5.cam_tl", 0x2570b6bdcc0951ae, 5632},
    {"zoom.ptr.click.classic.z2.cam_br", 0x5e647d100a64f416, 5632},
    {"zoom.ptr.click.classic.z2.cam_mid", 0x5c60354ea96ce691, 5632},
    {"zoom.ptr.click.classic.z2.cam_tl", 0x62ca1855979f1f9e, 5632},
    {"zoom.ptr.click.wide.z0.5.cam_br", 0x771fb3733232e422, 10900},
    {"zoom.ptr.click.wide.z0.5.cam_mid", 0xda46613a8c62622c, 10900},
    {"zoom.ptr.click.wide.z0.5.cam_tl", 0xf6aa5d1f53643c7d, 10900},
    {"zoom.ptr.click.wide.z2.cam_br", 0xd20702b90f9c1a89, 10900},
    {"zoom.ptr.click.wide.z2.cam_mid", 0x1ae7411aed09eb7a, 10900},
    {"zoom.ptr.click.wide.z2.cam_tl", 0x3b83cb8107d3f5d9, 10900},
    {"zoom.ptr.cursor.classic.z0.5.idle.foe", 0xa4a58b0a80d64ca5, 307200},
    {"zoom.ptr.cursor.classic.z0.5.idle.mid", 0xc906651ac7ee9925, 307200},
    {"zoom.ptr.cursor.classic.z0.5.own_worker.foe", 0x15ed817e1a381d05, 307200},
    {"zoom.ptr.cursor.classic.z0.5.own_worker.mid", 0x08a3e714482b8545, 307200},
    {"zoom.ptr.cursor.classic.z2.idle.foe", 0xf9cdaceb4c088a65, 307200},
    {"zoom.ptr.cursor.classic.z2.idle.mid", 0xb8b2ac7f689da9e5, 307200},
    {"zoom.ptr.cursor.classic.z2.own_worker.foe", 0xd444bdd8edc49565, 307200},
    {"zoom.ptr.cursor.classic.z2.own_worker.mid", 0x94d40074049a40a5, 307200},
    {"zoom.ptr.cursor.wide.z0.5.idle.foe", 0xce78abc7391fc425, 518400},
    {"zoom.ptr.cursor.wide.z0.5.idle.mid", 0x0f7714a85311e0d5, 518400},
    {"zoom.ptr.cursor.wide.z0.5.own_worker.foe", 0x97929456b0efaae5, 518400},
    {"zoom.ptr.cursor.wide.z0.5.own_worker.mid", 0x0dc69de4726d0255, 518400},
    {"zoom.ptr.cursor.wide.z2.idle.foe", 0xa7059474797e7565, 518400},
    {"zoom.ptr.cursor.wide.z2.idle.mid", 0x434dec7e182c6165, 518400},
    {"zoom.ptr.cursor.wide.z2.own_worker.foe", 0xd90fb42453702085, 518400},
    {"zoom.ptr.cursor.wide.z2.own_worker.mid", 0xd546aeee415dae85, 518400},
    {"zoom.ptr.edge.classic.z0.5.cam_b", 0x9b603d85dacfc0e2, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_bl", 0x493ae82616f8a316, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_br", 0x9a03779e9dc092d9, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_l", 0x44d95cd06c149201, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_mid", 0xb5aa479aa4b1eabd, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_r", 0x08f03e660ee22d99, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_t", 0x192ef46972c41e4a, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_tl", 0xf3b9a29a8192b052, 307200},
    {"zoom.ptr.edge.classic.z0.5.cam_tr", 0xa65f5dc2d2fb8e95, 307200},
    {"zoom.ptr.edge.classic.z2.cam_b", 0x9b603d85dacfc0e2, 307200},
    {"zoom.ptr.edge.classic.z2.cam_bl", 0x493ae82616f8a316, 307200},
    {"zoom.ptr.edge.classic.z2.cam_br", 0x9a03779e9dc092d9, 307200},
    {"zoom.ptr.edge.classic.z2.cam_l", 0x44d95cd06c149201, 307200},
    {"zoom.ptr.edge.classic.z2.cam_mid", 0xb5aa479aa4b1eabd, 307200},
    {"zoom.ptr.edge.classic.z2.cam_r", 0x08f03e660ee22d99, 307200},
    {"zoom.ptr.edge.classic.z2.cam_t", 0x192ef46972c41e4a, 307200},
    {"zoom.ptr.edge.classic.z2.cam_tl", 0xf3b9a29a8192b052, 307200},
    {"zoom.ptr.edge.classic.z2.cam_tr", 0xa65f5dc2d2fb8e95, 307200},
    {"zoom.ptr.edge.wide.z0.5.cam_b", 0x038a2cf579102b41, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_bl", 0x01c9e9d3197b3293, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_br", 0x038a2cf579102b41, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_l", 0xcef20df609d2b663, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_mid", 0x256d9d28c4c52373, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_r", 0x256d9d28c4c52373, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_t", 0xd0a24e8428148eef, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_tl", 0x6120e14a1274a015, 518400},
    {"zoom.ptr.edge.wide.z0.5.cam_tr", 0xd0a24e8428148eef, 518400},
    {"zoom.ptr.edge.wide.z2.cam_b", 0x76375f7cfd86cacf, 518400},
    {"zoom.ptr.edge.wide.z2.cam_bl", 0x01c9e9d3197b3293, 518400},
    {"zoom.ptr.edge.wide.z2.cam_br", 0x038a2cf579102b41, 518400},
    {"zoom.ptr.edge.wide.z2.cam_l", 0xcef20df609d2b663, 518400},
    {"zoom.ptr.edge.wide.z2.cam_mid", 0x89a379e25c82e2cd, 518400},
    {"zoom.ptr.edge.wide.z2.cam_r", 0x256d9d28c4c52373, 518400},
    {"zoom.ptr.edge.wide.z2.cam_t", 0x7845bacfec608f1f, 518400},
    {"zoom.ptr.edge.wide.z2.cam_tl", 0x6120e14a1274a015, 518400},
    {"zoom.ptr.edge.wide.z2.cam_tr", 0xd0a24e8428148eef, 518400},
    {"zoom.radar.classic.z0.5", 0x4768f63a7d58db27, 9},
    {"zoom.radar.classic.z2", 0x983ee62ffc073851, 9},
    {"zoom.radar.wide.z0.5", 0x77d789e941a60e1c, 9},
    {"zoom.radar.wide.z2", 0x4bbda48161ec4a21, 9},
    {"zoom.world.classic.GAUNTLET.z0.5.cam_bl", 0x2b39c1375a9ae966, 307200},
    {"zoom.world.classic.GAUNTLET.z0.5.cam_br", 0xb95b4a5759157ea5, 307200},
    {"zoom.world.classic.GAUNTLET.z0.5.cam_mid", 0x45e9e7f1c0651c18, 307200},
    {"zoom.world.classic.GAUNTLET.z0.5.cam_tl", 0xc2beb54bf2859ba4, 307200},
    {"zoom.world.classic.GAUNTLET.z0.5.cam_tr", 0xf9b49d66448ce6d6, 307200},
    {"zoom.world.classic.GAUNTLET.z0.5.fog", 0x4a7bd7fa4e4c4546, 307200},
    {"zoom.world.classic.GAUNTLET.z2.cam_bl", 0xac86e2ba7a0bf9f3, 307200},
    {"zoom.world.classic.GAUNTLET.z2.cam_br", 0x99708d9950708f34, 307200},
    {"zoom.world.classic.GAUNTLET.z2.cam_mid", 0xc95956c52e20502d, 307200},
    {"zoom.world.classic.GAUNTLET.z2.cam_tl", 0x565124a0b94d3cf8, 307200},
    {"zoom.world.classic.GAUNTLET.z2.cam_tr", 0x986dd059f251ec1b, 307200},
    {"zoom.world.classic.GAUNTLET.z2.fog", 0x8a32cd73b549c9c5, 307200},
    {"zoom.world.classic.GAUNTLET.z2.half_pixel", 0xaf3bbf5b8ceab8f1, 307200},
    {"zoom.world.classic.TINY.z0.5.cam_bl", 0xbed2fe726c562110, 307200},
    {"zoom.world.classic.TINY.z0.5.cam_br", 0x885966ff43c26bc8, 307200},
    {"zoom.world.classic.TINY.z0.5.cam_mid", 0xea58a83cc3f45f3a, 307200},
    {"zoom.world.classic.TINY.z0.5.cam_tl", 0xf0f56567d2863e38, 307200},
    {"zoom.world.classic.TINY.z0.5.cam_tr", 0x9b4801b6d8f774f9, 307200},
    {"zoom.world.classic.TINY.z2.cam_bl", 0x9d81a4e88690e0eb, 307200},
    {"zoom.world.classic.TINY.z2.cam_br", 0xb995f408a9c6d26f, 307200},
    {"zoom.world.classic.TINY.z2.cam_mid", 0x1a4342112d9f3ce2, 307200},
    {"zoom.world.classic.TINY.z2.cam_tl", 0x029561a09d07587e, 307200},
    {"zoom.world.classic.TINY.z2.cam_tr", 0x36053cd51cbc02cb, 307200},
    {"zoom.world.wide.GAUNTLET.z0.5.cam_bl", 0xeb56c2525d244864, 518400},
    {"zoom.world.wide.GAUNTLET.z0.5.cam_br", 0xb47991f0d79c4151, 518400},
    {"zoom.world.wide.GAUNTLET.z0.5.cam_mid", 0x176e41c81aac12fc, 518400},
    {"zoom.world.wide.GAUNTLET.z0.5.cam_tl", 0xfa61a1f7879e4541, 518400},
    {"zoom.world.wide.GAUNTLET.z0.5.cam_tr", 0x1c5358cfd8cd623c, 518400},
    {"zoom.world.wide.GAUNTLET.z0.5.fog", 0x0eaaefe63de621b6, 518400},
    {"zoom.world.wide.GAUNTLET.z2.cam_bl", 0x5ca2320975d9ce5d, 518400},
    {"zoom.world.wide.GAUNTLET.z2.cam_br", 0x93e279f183d01cca, 518400},
    {"zoom.world.wide.GAUNTLET.z2.cam_mid", 0x255b2a5c47c1bd09, 518400},
    {"zoom.world.wide.GAUNTLET.z2.cam_tl", 0x8fe7b113e6aba8b4, 518400},
    {"zoom.world.wide.GAUNTLET.z2.cam_tr", 0x78c1e6b5d8385bd6, 518400},
    {"zoom.world.wide.GAUNTLET.z2.fog", 0xf4661002581aaa59, 518400},
    {"zoom.world.wide.GAUNTLET.z2.half_pixel", 0xbf78dfd53c5dde99, 518400},
    {"zoom.world.wide.TINY.z0.5.cam_bl", 0x78b859ffb22bfc9d, 518400},
    {"zoom.world.wide.TINY.z0.5.cam_br", 0x1f1e3abe2bbf1594, 518400},
    {"zoom.world.wide.TINY.z0.5.cam_mid", 0x07395110badc6f2a, 518400},
    {"zoom.world.wide.TINY.z0.5.cam_tl", 0x78b859ffb22bfc9d, 518400},
    {"zoom.world.wide.TINY.z0.5.cam_tr", 0x1f1e3abe2bbf1594, 518400},
    {"zoom.world.wide.TINY.z2.cam_bl", 0x2dfa97c51a9a97e9, 518400},
    {"zoom.world.wide.TINY.z2.cam_br", 0xd35fb9e090e0e19a, 518400},
    {"zoom.world.wide.TINY.z2.cam_mid", 0x8e96bbfe047fe823, 518400},
    {"zoom.world.wide.TINY.z2.cam_tl", 0x62244ca1cdcedbfe, 518400},
    {"zoom.world.wide.TINY.z2.cam_tr", 0xf9408c72c9c52f3e, 518400},
    {"", 0, 0},                                                           // (the sentinel: the table is never empty)
};
const Golden* golden_table(size_t& count) {
    count = sizeof(kGoldens) / sizeof(kGoldens[0]) - 1;                  // (the sentinel is not a number)
    return kGoldens;
}
}  // namespace
