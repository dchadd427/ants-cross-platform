// View fingerprint, part 3: the pixels of the real renderer and of whole application frames, and the application's own pointer gate.
// (See test_view_fingerprint.cpp for what the whole program pins.)
#include "view_fingerprint_fixtures.hpp"

namespace {

Canvas read_canvas(SDL_Renderer* sr) {
    Canvas c;
    c.ok = SDL_RenderReadPixels(sr, nullptr, SDL_PIXELFORMAT_RGBA32, c.px.data(), kW * 4) == 0;
    if (!c.ok) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    return c;
}

/// --save DIR: writes the canvas as DIR/<name>.bmp (opaque; the masked pixels magenta), to look at what a changed hash is about
void save_canvas(const std::string& name, const Canvas& c) {
    std::vector<uint8_t> shown = c.px;
    for (size_t i = 0; i < shown.size(); i += 4) {
        if (shown[i + 3] == 0) {
            shown[i] = 255;
            shown[i + 1] = 0;
            shown[i + 2] = 255;
        }
        shown[i + 3] = 255;
    }
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(shown.data(), kW, kH, 32, kW * 4, SDL_PIXELFORMAT_RGBA32);
    if (s == nullptr) return;
    if (SDL_SaveBMP(s, (g_save_dir + "/" + name + ".bmp").c_str()) != 0) std::fprintf(stderr, "cannot write %s/%s.bmp\n", g_save_dir.c_str(), name.c_str());
    SDL_FreeSurface(s);
}

/// Hashes (masked) and records one canvas
void record_canvas(const std::string& name, Canvas c, const std::vector<MaskRect>& mask = {}) {
    if (!wanted(name)) return;
    const Measure m = canvas_measure(c, mask);
    if (!g_save_dir.empty()) save_canvas(name, c);
    record(name, m);
}

/// A real renderer over a hidden 640 x 480 window (the software renderer, the dummy video driver) with a map loaded, a pinned animation clock and a world of its own
struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, const std::string& map) : arc(archive) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);                                   // (an Application that ended before has quit SDL)
        win = SDL_CreateWindow("view-fingerprint", 0, 0, kW, kH, SDL_WINDOW_HIDDEN);
        if (win == nullptr) return;
        if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map + ".LVL")) return;
        engine.init(level, 1337);
        if (!renderer.init(win, archive)) return;
        renderer.set_level(level);
        renderer.pin_animation_clock(1500);
        renderer.set_hud_team(0);
        world = engine.get_world_state();
        world.ants.clear();
        ok = true;
    }
    ~RendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RendererRig(const RendererRig&) = delete;
    RendererRig& operator=(const RendererRig&) = delete;

    void look(int32_t x, int32_t y) {
        renderer.camera().x = static_cast<float>(x);
        renderer.camera().y = static_cast<float>(y);
        renderer.camera().clamp_to_bounds(level.width(), level.height());
    }
    int32_t cam_x() { return renderer.camera().world_x; }
    int32_t cam_y() { return renderer.camera().world_y; }

    /// One frame of the world pass (the HUD is not drawn: the canvas outside the playfield stays black), recorded as a pixel hash
    void shoot(const std::string& name, int32_t selected_id = -1, const std::vector<uint32_t>& selected_ids = {}, int32_t base_team = -1, bool hp = false) {
        if (!wanted(name)) return;
        renderer.set_show_hp(hp);
        renderer.begin_frame();
        renderer.render_world(world, engine.grid(), selected_id, selected_ids, false, false, -1, -1, base_team, 0.0f);
        record_canvas(name, read_canvas(renderer.get_sdl_renderer()));
    }

    const assets::AssetArchive& arc;
    SDL_Window* win{nullptr};
    assets::LevelData level;
    sim::SimulationEngine engine;
    Renderer renderer;
    sim::WorldState world;
    bool ok{false};
};

}  // namespace

void world_pixel_scenarios(const assets::AssetArchive& arc) {
    // The playfield at the corners and edges of two maps (a 60 x 60 map and TINY, which is smaller than two views): ants of every type, colour and facing, effects and
    // score bubbles, all straddling the borders of the playfield; the canvas outside the playfield must stay black
    for (const char* map : {"MEDIUM", "TINY"}) {
        const int32_t tiles = std::string(map) == "TINY" ? 31 : 60;
        for (const Cam& c : nine_cams(tiles, tiles)) {
            const std::string name = std::string("px.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            RendererRig rig(arc, map);
            check(rig.ok, "the renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 90, 4242u + static_cast<uint32_t>(c.x));
            add_effects(rig.world, rig.cam_x(), rig.cam_y());
            rig.shoot(name);
        }
    }
    // The other maps of the original (their terrain, objects and food): the corner and the middle
    for (const char* map : {"GAUNTLET", "ISLANDS", "SMALL", "TREASURE"}) {
        const int32_t tiles = std::string(map) == "SMALL" ? 40 : 60;
        for (const Cam& c : {nine_cams(tiles, tiles)[0], nine_cams(tiles, tiles)[4]}) {
            const std::string name = std::string("px.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            RendererRig rig(arc, map);
            check(rig.ok, "the renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 40, 17u + static_cast<uint32_t>(c.x));
            rig.shoot(name);
        }
    }
    // Odd origins: a view one pixel off a border, a view at a tile boundary and half a tile off it
    for (const auto& o : {std::pair<int32_t, int32_t>{1, 1}, std::pair<int32_t, int32_t>{32, 64}, std::pair<int32_t, int32_t>{17, 49}, std::pair<int32_t, int32_t>{1477, 1479},
                          std::pair<int32_t, int32_t>{733, 1}}) {
        const std::string name = "px.world.MEDIUM.origin_" + std::to_string(o.first) + "_" + std::to_string(o.second);
        if (!wanted(name)) continue;
        RendererRig rig(arc, "MEDIUM");
        if (!rig.ok) continue;
        rig.look(o.first, o.second);
        populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 60, 99u + static_cast<uint32_t>(o.first));
        rig.shoot(name);
    }
    // Selection markers (the ears of every selected ant), the hill's brackets, the hit point digits, a crowd, the fog of war, the click marker
    if (wanted_group("px.world.MEDIUM.selected")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(600, 600);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 60, 31337u);
            std::vector<uint32_t> ids;
            for (uint32_t i = 0; i < 60; i += 4) ids.push_back(5000u + i);
            rig.shoot("px.world.MEDIUM.selected", static_cast<int32_t>(ids.front()), ids);
            rig.shoot("px.world.MEDIUM.selected_hp", static_cast<int32_t>(ids.front()), ids, -1, true);
        }
    }
    if (wanted_group("px.world.MEDIUM.hill_selected")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            int32_t hx = 0;
            int32_t hy = 0;
            for (const auto& hill : rig.engine.grid().anthills()) {
                if (hill.team_id == 0) { hx = hill.x * 32; hy = hill.y * 32; break; }
            }
            rig.look(hx - 200, hy - 200);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 40, 777u);
            rig.shoot("px.world.MEDIUM.hill_selected", -1, {}, 0);
        }
    }
    if (wanted_group("px.world.MEDIUM.crowd")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(300, 900);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 400, 5u);
            rig.shoot("px.world.MEDIUM.crowd");
        }
    }
    if (wanted_group("px.world.MEDIUM.fog")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(500, 500);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 80, 2024u);
            rig.world.fog_of_war_enabled = true;
            rig.world.fog_revealed.assign(static_cast<size_t>(rig.world.width) * rig.world.height, 0);
            uint32_t seed = 6u;
            for (uint32_t y = 0; y < rig.world.height; ++y) {
                for (uint32_t x = 0; x < rig.world.width; ++x) {
                    const int32_t dx = static_cast<int32_t>(x) - 24;
                    const int32_t dy = static_cast<int32_t>(y) - 22;
                    const int32_t ex = static_cast<int32_t>(x) - 30;
                    const int32_t ey = static_cast<int32_t>(y) - 29;
                    const bool open = dx * dx + dy * dy < 30 || ex * ex + ey * ey < 12 || lcg(seed) % 11u == 0;
                    rig.world.fog_revealed[static_cast<size_t>(y) * rig.world.width + x] = open ? 1 : 0;
                }
            }
            rig.shoot("px.world.MEDIUM.fog");
            rig.look(0, 0);
            rig.shoot("px.world.MEDIUM.fog.corner_tl");
        }
    }
    if (wanted_group("px.world.MEDIUM.click_marker")) {
        RendererRig rig(arc, "MEDIUM");
        if (rig.ok) {
            rig.look(400, 400);
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 20, 8u);
            rig.renderer.spawn_transient_effect("xmarks", 400 + 200, 400 + 150);
            rig.shoot("px.world.MEDIUM.click_marker");
        }
    }
    // The cursors that have one frame (the animated ones follow the wall clock): the hot spot and the clip at every edge of the screen
    {
        const struct { CursorType type; const char* name; } kinds[] = {{CursorType::Normal, "normal"}, {CursorType::Select, "select"}, {CursorType::ScrollN, "n"},
                                                                      {CursorType::ScrollNE, "ne"}, {CursorType::ScrollE, "e"}, {CursorType::ScrollSE, "se"},
                                                                      {CursorType::ScrollS, "s"}, {CursorType::ScrollSW, "sw"}, {CursorType::ScrollW, "w"},
                                                                      {CursorType::ScrollNW, "nw"}};
        if (wanted_group("px.cursor")) {
            RendererRig rig(arc, "TINY");
            if (rig.ok) {
                for (const auto& k : kinds) {
                    rig.renderer.begin_frame();
                    const int32_t spots[8][2] = {{320, 240}, {0, 0}, {639, 0}, {0, 479}, {639, 479}, {1, 240}, {320, 478}, {638, 100}};
                    for (const auto& s : spots) rig.renderer.render_software_cursor(k.type, s[0], s[1]);
                    record_canvas(std::string("px.cursor.") + k.name, read_canvas(rig.renderer.get_sdl_renderer()));
                }
            }
        }
    }
}

namespace {

// =====================================================================================================================================================
// What a whole Application frame puts on the canvas (a headless Application: the real renderer, the real HUD, the real screens)
// =====================================================================================================================================================


/// TrueType text is the one thing whose pixels depend on the font library of the machine, so the places where the classic picture has text are masked (fixed rectangles,
/// the same on every machine: a text can never reach beyond the box of its label). Everything else is hashed. The rectangles are half open (x0, y0, x1, y1).
/// The frame-rate counter and the version text (the bottom right corner, right aligned at x = 632: their width depends on the font and the version changes every release)
const MaskRect kMaskPlate{470, 466, 640, 480};

/// The setup screen's labels: the map name (36, 312) 179 px, its description (36, 380) 293 px, the prompt (36, 447) 293 px, the four players' names (415, 95 + 50 i) 120 px
const std::vector<MaskRect> kMaskSetup = {{32, 308, 222, 352}, {32, 376, 336, 440}, {32, 443, 336, 480}, {411, 93, 539, 119}, {411, 143, 539, 169}, {411, 193, 539, 219}, {411, 243, 539, 269}};

/// The match screen's labels: the status line (481, 254) 139 px, the chat log (482, 299) 138 x 101, the chat input (481, 424) 139 px, the score labels (the local one (312..399, 4), the
/// bottom ones (5..101, 163..251, 312..399) x 464)
const std::vector<MaskRect> kMaskMatch = {{479, 252, 622, 268}, {480, 297, 622, 402}, {479, 422, 622, 438}, {310, 2, 402, 20}, {3, 462, 103, 480}, {161, 462, 253, 480}, {310, 462, 401, 480}};

/// The dialogs of the match screen: the "Get ready" dialog's two labels
const std::vector<MaskRect> kMaskStartModal = {{128, 108, 372, 272}, {128, 288, 372, 314}};

/// The quit dialog's prompt
const std::vector<MaskRect> kMaskQuit = {{128, 178, 392, 342}};

std::vector<MaskRect> masks(std::initializer_list<const std::vector<MaskRect>*> lists) {
    std::vector<MaskRect> all{kMaskPlate};
    for (const auto* l : lists) all.insert(all.end(), l->begin(), l->end());
    return all;
}

/// An Application with the animation clock of its renderer and the clock of its HUD pinned. A headless one starts on the setup screen; one with a window
/// (SDL's dummy video and audio drivers: nothing is shown or heard) starts on the loading screen, which a headless application skips. The settings go
/// to a place that does not exist: neither the user's stored options are read nor any file is written.
struct AppRig {
    explicit AppRig(bool window = false) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = app_config();
        cfg.headless = !window;
        if (window) cfg.settings_path = "view_fingerprint_no_such_folder/settings.ini";
        ok = app.init(cfg);
        if (!ok) return;
        app.renderer().pin_animation_clock(1500);
        app.hud().set_ticks_function(&test_clock);
        g_now_ms = 0;
        app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));      // the pointer is outside: no game cursor (the setup of a frame can bring it back)
    }
    void pointer_at(int32_t x, int32_t y) {
        app.handle_window_event(window_event(SDL_WINDOWEVENT_ENTER));
        app.note_pointer(x, y);
    }
    std::string map_path(const char* name) const { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL"; }
    /// A match on a map: the clocks are pinned again (the match's HUD has been reset) and the camera is where the start view put it
    bool start_match(const char* map) {
        if (!app.start_game(map_path(map))) return false;
        app.hud().reset();                                        // the chat box's caret starts from the pinned clock
        g_now_ms = 0;
        app.hud().start_match_modal();
        return true;
    }
    void shot(const std::string& name, const std::vector<MaskRect>& mask) {
        if (!wanted(name)) return;
        app.render_frame();
        record_canvas(name, read_canvas(app.renderer().get_sdl_renderer()), mask);
    }
    void look(int32_t x, int32_t y) {
        ViewportCamera& cam = app.renderer().camera();
        cam.x = static_cast<float>(x);
        cam.y = static_cast<float>(y);
        cam.clamp_to_bounds(app.sim().grid().width(), app.sim().grid().height());
    }
    Application app;
    bool ok{false};
};

}  // namespace

void app_pixel_scenarios() {
    // The loading screen (an application with a window: the progress bar grows with the 50 ms ticks of the loading state)
    if (wanted_group("px.app.loading")) {
        AppRig rig(true);
        check(rig.ok, "the application with a window is up");
        if (rig.ok) {
            check(rig.app.state() == AppState::Loading, "an application with a window starts on the loading screen");
            const char* video = SDL_GetCurrentVideoDriver();
            const char* audio = SDL_GetCurrentAudioDriver();
            check(video != nullptr && std::string(video) == "dummy" && audio != nullptr && std::string(audio) == "dummy", "the window and the sound of this application are SDL's dummy drivers: nothing is shown or heard");
            rig.shot("px.app.loading.start", masks({}));
            for (int tick = 0; tick < 12; ++tick) rig.app.update_simulation(0.05f);
            rig.shot("px.app.loading.tick12", masks({}));
            for (int tick = 12; tick < 25; ++tick) rig.app.update_simulation(0.05f);
            rig.shot("px.app.loading.tick25", masks({}));
            rig.pointer_at(200, 300);                                                // the arrow over the loading screen
            rig.shot("px.app.loading.cursor", masks({}));
        }
    }
    // The setup screen: before its refresh (no labels), after it, with the START button hovered
    if (wanted_group("px.app.setup")) {
        AppRig rig;
        check(rig.ok, "the headless application is up");
        if (rig.ok) {
            check(rig.app.state() == AppState::MapSelect, "a headless application starts on the setup screen");
            rig.shot("px.app.setup.before_refresh", masks({}));
            rig.app.map_select().update(0.5f);
            rig.shot("px.app.setup.refreshed", masks({&kMaskSetup}));
            rig.app.map_select().handle_mouse_motion(MapSelectScreen::BTN_START_X + 5, MapSelectScreen::BTN_START_Y + 5);
            rig.shot("px.app.setup.hover_start", masks({&kMaskSetup}));
            rig.pointer_at(300, 200);                                                // the game's cursor on the setup screen (the arrow)
            rig.shot("px.app.setup.cursor", masks({&kMaskSetup}));
        }
    }
    // The quick help: START! resting, hovered, pressed
    if (wanted_group("px.app.quickhelp")) {
        AppRig rig;
        if (rig.ok) {
            rig.app.finish_loading();
            check(rig.app.state() == AppState::QuickHelp, "the quick help follows the loading screen");
            rig.shot("px.app.quickhelp.rest", masks({}));
            rig.app.quick_help_move(540, 445);
            rig.shot("px.app.quickhelp.hover_start", masks({}));
            rig.app.quick_help_press(540, 445);
            rig.shot("px.app.quickhelp.pressed_start", masks({}));
            rig.app.quick_help_release(700, 700);                                    // released off the button: cancelled
            rig.pointer_at(100, 100);
            rig.shot("px.app.quickhelp.cursor", masks({}));
        }
    }
    // The match screen: the "Get ready" dialog, then the HUD over the world at the start view and at the corners and edges of the map
    if (wanted_group("px.app.match")) {
        AppRig rig;
        if (rig.ok && rig.start_match("MEDIUM")) {
            check(rig.app.state() == AppState::Playing, "start_game puts the application into the match screen");
            rig.shot("px.app.match.modal", masks({&kMaskMatch, &kMaskStartModal}));
            rig.app.hud().dismiss_match_start_modal();
            rig.shot("px.app.match.start_view", masks({&kMaskMatch}));
            const int32_t tiles = static_cast<int32_t>(rig.app.sim().grid().width());
            for (const Cam& c : nine_cams(tiles, tiles)) {
                rig.look(c.x, c.y);
                rig.shot(std::string("px.app.match.cam_") + c.name, masks({&kMaskMatch}));
            }
            rig.look(700, 700);
            rig.app.hud().open_quit_dialog();
            rig.shot("px.app.match.quit_dialog", masks({&kMaskMatch, &kMaskQuit}));
            rig.app.hud().close_quit_dialog();
            rig.pointer_at(300, 300);                                                // the arrow over the map
            rig.shot("px.app.match.cursor_map", masks({&kMaskMatch}));
            rig.pointer_at(10, 240);                                                 // a scroll arrow (the map can scroll to the west from here)
            rig.shot("px.app.match.cursor_edge", masks({&kMaskMatch}));
        }
    }
}

/// The application's own pointer gate (it hands the pointer to the HUD's input task only while it is inside the 640 x 480 screen and inside the window): the step of the view
/// after one 50 ms input task, for every pointer position of the screen and eight pixels beyond it, from three cameras
void app_pointer_scenarios() {
    if (!wanted_group("ptr.app")) return;
    AppRig rig;
    if (!rig.ok || !rig.start_match("MEDIUM")) {
        check(false, "the application for the pointer sweeps is up");
        return;
    }
    rig.app.hud().dismiss_match_start_modal();
    const int32_t tiles = static_cast<int32_t>(rig.app.sim().grid().width());
    ViewportCamera& cam = rig.app.renderer().camera();
    const auto nine = nine_cams(tiles, tiles);
    for (const Cam& c : {nine[4], nine[0], nine[8]}) {
        const std::string name = std::string("ptr.app.edge_gate.cam_") + c.name;
        if (!wanted(name)) continue;
        Fnv64 h;
        uint64_t n = 0;
        uint64_t scrolled_inside = 0;
        for (int32_t y = -8; y < kH + 8; ++y) {
            for (int32_t x = -8; x < kW + 8; ++x) {
                cam.x = static_cast<float>(c.x);
                cam.y = static_cast<float>(c.y);
                cam.clamp_to_bounds(static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
                SDL_MouseMotionEvent ev{};
                ev.type = SDL_MOUSEMOTION;
                ev.x = x;
                ev.y = y;
                rig.app.handle_mouse_motion(ev);
                rig.app.handle_camera_panning(0.05f);
                h.i32(cam.world_x - c.x);
                h.i32(cam.world_y - c.y);
                if ((cam.world_x != c.x || cam.world_y != c.y) && x >= 0 && x < kW && y >= 0 && y < kH) ++scrolled_inside;
                ++n;
            }
        }
        check(scrolled_inside > 0, "the pointer sweep from camera " + std::string(c.name) + " scrolls the view somewhere");
        record(name, Measure{h.value(), n});
    }
    // a pointer that has left the window does not scroll the map, wherever it was last seen
    if (wanted("ptr.app.edge_gate.left_window")) {
        Fnv64 h;
        uint64_t n = 0;
        for (int32_t x = 0; x < kW; x += 3) {
            cam.x = static_cast<float>(nine[4].x);
            cam.y = static_cast<float>(nine[4].y);
            cam.clamp_to_bounds(static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
            SDL_MouseMotionEvent ev{};
            ev.type = SDL_MOUSEMOTION;
            ev.x = x;
            ev.y = 2;
            rig.app.handle_mouse_motion(ev);
            rig.app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));
            rig.app.handle_camera_panning(0.05f);
            h.i32(cam.world_x - nine[4].x);
            h.i32(cam.world_y - nine[4].y);
            h.flag(rig.app.pointer_outside());
            ++n;
        }
        record("ptr.app.edge_gate.left_window", Measure{h.value(), n});
    }
}
