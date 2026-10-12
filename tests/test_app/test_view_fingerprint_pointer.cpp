// View fingerprint, part 2: the pointer, every pixel of the 640 x 480 screen (edge scroll, minimap, camera, cursor, click).
// (See test_view_fingerprint.cpp for what the whole program pins.)
#include "view_fingerprint_fixtures.hpp"

namespace {

std::vector<Cam> cams_of(int32_t tiles_w, int32_t tiles_h) {
    const auto nine = nine_cams(tiles_w, tiles_h);
    return std::vector<Cam>(nine.begin(), nine.end());
}

}  // namespace

void edge_scenarios() {
    // The 60 x 60 map at the default scroll rate: one case for every camera (away from the borders and at each of the eight borders and corners)
    for (const Cam& c : nine_cams(60, 60)) {
        const std::string name = "ptr.edge.60x60.rate50.cam_" + std::string(c.name);
        if (wanted(name)) record(name, edge_sweep(60, 60, 50, {c}));
    }
    // The other rates (the half extent is rate + 10) and other map sizes, over all nine cameras at once
    const struct { int32_t w, h, rate; } more[] = {{60, 60, 0}, {60, 60, 99}, {31, 31, 50}, {40, 40, 25}, {14, 14, 50}, {80, 50, 75}, {13, 9, 50}};
    for (const auto& m : more) {
        const std::string name = "ptr.edge." + map_name(m.w, m.h) + ".rate" + std::to_string(m.rate) + ".all9";
        if (wanted(name)) record(name, edge_sweep(m.w, m.h, m.rate, cams_of(m.w, m.h)));
    }
    // The pointer outside the screen (a window with bars around the picture reports positions beyond it): a ring of 16 pixels around the 640 x 480 pixels
    {
        const std::string name = "ptr.edge.60x60.rate50.outside_ring.mid";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t y = -16; y < kH + 16; ++y) {
                for (int32_t x = -16; x < kW + 16; ++x) {
                    if (x >= 0 && x < kW && y >= 0 && y < kH) continue;
                    hash_scroll(h, edge_scroll_step(x, y, 50, 739, 740, 60, 60));
                    ++n;
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
}

// ---- the minimap: where the pointer is, what world point it is, how the view follows it; and the start view of a match

void minimap_scenarios() {
    record("ptr.minimap.rect", sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_minimap_rect(x, y)); }));
    record("ptr.class.map_rect", sweep([](Fnv64& h, int32_t x, int32_t y) { h.flag(HUD::in_map_rect(x, y)); }));
    {
        HUD hud;
        record("ptr.class.chat_view", sweep([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.in_chat_view(x, y)); }));
    }
    // the world point under the pointer (no clamp) for several map sizes
    const struct { int32_t w, h; } sizes[] = {{60, 60}, {31, 31}, {40, 40}, {14, 14}, {80, 50}};
    for (const auto& s : sizes) {
        const std::string name = "ptr.minimap.point." + map_name(s.w, s.h);
        if (!wanted(name)) continue;
        record(name, sweep([&](Fnv64& h, int32_t x, int32_t y) {
                   int32_t wx = 0;
                   int32_t wy = 0;
                   minimap_point(x, y, s.w, s.h, wx, wy);
                   h.i32(wx);
                   h.i32(wy);
               }));
    }
    // one input tick with the button held on the minimap: the step that the view takes towards the point under the pointer, from every camera
    for (const auto& s : {std::pair<int32_t, int32_t>{60, 60}, std::pair<int32_t, int32_t>{31, 31}, std::pair<int32_t, int32_t>{14, 14}}) {
        const std::string name = "ptr.minimap.scroll." + map_name(s.first, s.second) + ".all9";
        if (!wanted(name)) continue;
        Fnv64 h;
        uint64_t n = 0;
        for (const Cam& c : nine_cams(s.first, s.second)) {
            for (int32_t y = 0; y < kH; ++y) {
                for (int32_t x = 0; x < kW; ++x) {
                    hash_scroll(h, minimap_scroll_step(x, y, c.x, c.y, s.first, s.second));
                    ++n;
                }
            }
        }
        record(name, Measure{h.value(), n});
    }
    // the start view of a match: the view that shows the square around the hill's anchor tile, for every anchor of several maps
    {
        const std::string name = "ptr.start_view.origins";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& s : sizes) {
                for (int32_t ty = 0; ty < s.h; ++ty) {
                    for (int32_t tx = 0; tx < s.w; ++tx) {
                        int32_t ox = 0;
                        int32_t oy = 0;
                        start_view_origin(tx, ty, s.w, s.h, ox, oy);
                        h.i32(ox);
                        h.i32(oy);
                        ++n;
                    }
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
}

// ---- the camera: limits, centring, the two conversions

void camera_scenarios() {
    const struct { uint32_t w, h; } maps[] = {{60, 60}, {31, 31}, {14, 14}, {13, 9}, {80, 50}};
    if (wanted("view.camera.clamp")) {
        Fnv64 h;
        uint64_t n = 0;
        const float values[] = {-500.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 7.0f, 100.25f, 441.0f, 442.0f, 443.0f, 1000.0f, 1477.5f, 1478.0f, 1479.0f, 1600.0f, 5000.0f};
        for (const auto& m : maps) {
            for (float vx : values) {
                for (float vy : values) {
                    ViewportCamera cam;
                    cam.x = vx;
                    cam.y = vy;
                    cam.clamp_to_bounds(m.w, m.h);
                    h.i32(static_cast<int32_t>(cam.x * 4.0f));
                    h.i32(static_cast<int32_t>(cam.y * 4.0f));
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
        }
        record("view.camera.clamp", Measure{h.value(), n});
    }
    if (wanted("view.camera.center_on")) {
        Fnv64 h;
        uint64_t n = 0;
        for (const auto& m : maps) {
            for (int32_t wy = -64; wy < static_cast<int32_t>(m.h) * 32 + 64; wy += 37) {
                for (int32_t wx = -64; wx < static_cast<int32_t>(m.w) * 32 + 64; wx += 41) {
                    ViewportCamera cam;
                    cam.center_on(wx, wy, m.w, m.h);
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
        }
        record("view.camera.center_on", Measure{h.value(), n});
    }
    if (wanted("view.camera.scroll_pixels")) {
        Fnv64 h;
        uint64_t n = 0;
        for (const auto& m : maps) {
            ViewportCamera cam;
            for (int32_t i = 0; i < 400; ++i) {                      // a deterministic walk that hits every border of the map
                const int32_t dx = ((i * 7) % 61) - 30;
                const int32_t dy = ((i * 11) % 53) - 26;
                cam.scroll_pixels(dx * (1 + i / 50), dy * (1 + i / 50), m.w, m.h);
                h.i32(cam.world_x);
                h.i32(cam.world_y);
                ++n;
            }
        }
        record("view.camera.scroll_pixels", Measure{h.value(), n});
    }
    // screen -> world for every pixel, from several cameras; world -> screen for a lattice of world points
    for (const Cam& c : {Cam{"origin", 0, 0}, Cam{"mid", 739, 740}, Cam{"far", 1478, 1480}}) {
        const std::string name = std::string("view.camera.screen_to_world.") + c.name;
        if (wanted(name)) {
            const ViewportCamera cam = camera_at(c.x, c.y);
            record(name, sweep([&](Fnv64& h, int32_t x, int32_t y) {
                       int32_t wx = -1;
                       int32_t wy = -1;
                       const bool ok = cam.screen_to_world(x, y, wx, wy);
                       h.flag(ok);
                       if (ok) {
                           h.i32(wx);
                           h.i32(wy);
                       }
                   }));
        }
        const std::string name2 = std::string("view.camera.world_to_screen.") + c.name;
        if (wanted(name2)) {
            const ViewportCamera cam = camera_at(c.x, c.y);
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t wy = c.y - 80; wy < c.y + PLAYFIELD_H + 80; wy += 9) {
                for (int32_t wx = c.x - 80; wx < c.x + PLAYFIELD_W + 80; wx += 9) {
                    int32_t sx = 0;
                    int32_t sy = 0;
                    const bool in = cam.world_to_screen(wx, wy, sx, sy);
                    h.flag(in);
                    h.i32(sx);
                    h.i32(sy);
                    ++n;
                }
            }
            record(name2, Measure{h.value(), n});
        }
    }
}

namespace {

/// How many pixels got each cursor, over all the cursor sweeps (the scenes must reach every cursor of the original)
std::array<uint64_t, 16> g_cursor_seen{};

Measure cursor_sweep(PointerScene& ps, HUD& hud, const ViewportCamera& cam) {
    const sim::WorldState& world = ps.world();
    const sim::Grid& grid = ps.sim.grid();
    return sweep([&](Fnv64& h, int32_t x, int32_t y) {
        const CursorType c = hud.evaluate_cursor(x, y, world, grid, cam);
        ++g_cursor_seen[static_cast<size_t>(c)];
        h.byte(static_cast<uint8_t>(c));
    });
}

}  // namespace

void cursor_scenarios() {
    const std::vector<CursorState> states = {
        {"idle", false, [](HUD&, PointerScene&, ViewportCamera&) {}},
        {"own_worker", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"own_group", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.set_selected_ant_ids({p.worker, p.worker2}); }},
        {"own_bomber", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.bomber); }},
        {"own_thief", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.thief); }},
        {"own_combat", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.combat); }},
        {"bomber_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.bomber);
             h.handle_mouse_down(560, 180, SDL_BUTTON_LEFT, p.sim, cam);            // the ability pedestal (slot 2) latches
             h.handle_mouse_up(560, 180, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"worker_move_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500, 170, SDL_BUTTON_LEFT, p.sim, cam);            // the move pedestal (slot 1) latches
             h.handle_mouse_up(500, 170, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"enemy_inspected", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.foe); }},
        {"hill_own", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.select_base(0); }},
        {"hill_enemy", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.select_base(1); }},
        {"fog_idle", true, [](HUD&, PointerScene&, ViewportCamera&) {}},
        {"fog_own_worker", true, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"options_open", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.open_options(); }},
        {"band_dragging", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, p.sim, cam);
             h.handle_mouse_motion(300, 260, p.sim, cam);                           // a rubber band wider than 4 px
         }},
        {"button_captured", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500, 18, SDL_BUTTON_LEFT, p.sim, cam);             // the Help button holds the pointer: the edge strips do not run
         }},
    };
    // every state from the middle camera (the objects are in view) ...
    for (const CursorState& s : states) {
        const std::string name = std::string("ptr.cursor.") + s.name + ".cam_mid400";
        if (!wanted(name)) continue;
        PointerScene ps(s.fog);
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 0;
        hud.init(0);
        hud.set_sim_query(&ps.sim);
        ViewportCamera cam = camera_at(400, 400);
        s.setup(hud, ps, cam);
        record(name, cursor_sweep(ps, hud, cam));
    }
    // ... and the plain selection from the corners and edges of the map (the scroll arrows depend on where the view can still move)
    for (const char* state : {"idle", "own_worker"}) {
        for (const Cam& c : nine_cams(60, 60)) {
            const std::string name = std::string("ptr.cursor.") + state + ".cam_" + c.name;
            if (!wanted(name)) continue;
            PointerScene ps;
            HUD hud;
            hud.set_ticks_function(&test_clock);
            g_now_ms = 0;
            hud.init(0);
            hud.set_sim_query(&ps.sim);
            if (std::string(state) == "own_worker") hud.select_ant(ps.worker);
            const ViewportCamera cam = camera_at(c.x, c.y);
            record(name, cursor_sweep(ps, hud, cam));
        }
    }
    if (g_only_prefix.empty()) {                                    // the scenes reach every cursor of the original (a sweep that never meets a cursor pins nothing about it)
        const struct { CursorType type; const char* name; } kinds[] = {{CursorType::Normal, "arrow"}, {CursorType::Select, "select"}, {CursorType::Move, "move"}, {CursorType::Target, "target"},
                                                                      {CursorType::Attack, "attack"}, {CursorType::ScrollN, "scroll N"}, {CursorType::ScrollNE, "scroll NE"},
                                                                      {CursorType::ScrollE, "scroll E"}, {CursorType::ScrollSE, "scroll SE"}, {CursorType::ScrollS, "scroll S"},
                                                                      {CursorType::ScrollSW, "scroll SW"}, {CursorType::ScrollW, "scroll W"}, {CursorType::ScrollNW, "scroll NW"},
                                                                      {CursorType::Food, "food"}};
        for (const auto& k : kinds) check(g_cursor_seen[static_cast<size_t>(k.type)] > 0, std::string("the cursor sweeps meet the ") + k.name + " cursor");
    }
}

void click_scenarios() {
    struct State {
        const char* name;
        std::function<void(HUD&, PointerScene&)> setup;
        int32_t stride;
        uint8_t button;
        std::function<void(PointerScene&)> scene;               // changes the engine's world before the HUD looks at it (the invitations of the alliance dialogs)
        const char* expects;                                    // what some click must have done: c command, q quit, o option changed, d dialog answered, s selection, l latch, m view scrolled, p pointer captured, x a dialog closed
    };
    auto no_scene = [](PointerScene&) {};
    const std::vector<State> states = {
        {"idle", [](HUD&, PointerScene&) {}, 4, SDL_BUTTON_LEFT, no_scene, "mp"},
        {"own_worker", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, 4, SDL_BUTTON_LEFT, no_scene, "csplmp"},
        {"own_bomber", [](HUD& h, PointerScene& p) { h.select_ant(p.bomber); }, 4, SDL_BUTTON_LEFT, no_scene, "csl"},
        {"own_group", [](HUD& h, PointerScene& p) { h.set_selected_ant_ids({p.worker, p.worker2, p.thief}); }, 4, SDL_BUTTON_LEFT, no_scene, "cs"},
        {"hill_own", [](HUD& h, PointerScene&) { h.select_base(0); }, 4, SDL_BUTTON_LEFT, no_scene, "cs"},
        {"hill_enemy", [](HUD& h, PointerScene&) { h.select_base(1); }, 4, SDL_BUTTON_LEFT, no_scene, "s"},
        // four hills and the alliance of the scene: the ally pedestal of an enemy hill asks before the team is broken
        {"hill_enemy.ally_break", [](HUD& h, PointerScene&) { h.select_base(1); }, 4, SDL_BUTTON_LEFT,
         [](PointerScene& p) { p.sim.grid_mut().set_anthill(2, sim::TileCoord{40, 40}); p.sim.grid_mut().set_anthill(3, sim::TileCoord{5, 45}); p.sim.tick(); }, "sd"},
        // no alliance and four hills: the ally pedestal of an enemy hill offers a team at once
        {"hill_enemy.ally_offer", [](HUD& h, PointerScene&) { h.select_base(1); }, 4, SDL_BUTTON_LEFT,
         [](PointerScene& p) { p.sim.break_alliance(0); p.sim.grid_mut().set_anthill(2, sim::TileCoord{40, 40}); p.sim.grid_mut().set_anthill(3, sim::TileCoord{5, 45}); p.sim.tick(); }, "cs"},
        {"own_worker.right_button", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, 4, SDL_BUTTON_RIGHT, no_scene, "c"},
        {"own_bomber.right_button", [](HUD& h, PointerScene& p) { h.select_ant(p.bomber); }, 4, SDL_BUTTON_RIGHT, no_scene, "c"},
        // the dialogs and screens that take every click: where their buttons are
        {"dialog.quit", [](HUD& h, PointerScene&) { h.open_quit_dialog(); }, 4, SDL_BUTTON_LEFT, no_scene, "qx"},
        {"dialog.quickhelp", [](HUD& h, PointerScene&) { h.open_quick_help(); }, 4, SDL_BUTTON_LEFT, no_scene, "x"},
        {"dialog.options", [](HUD& h, PointerScene&) { h.open_options(); }, 4, SDL_BUTTON_LEFT, no_scene, "ox"},
        {"dialog.start_modal", [](HUD& h, PointerScene&) { h.start_match_modal(); }, 4, SDL_BUTTON_LEFT, no_scene, ""},
        {"dialog.invitation", [](HUD&, PointerScene&) {}, 4, SDL_BUTTON_LEFT, [](PointerScene& p) { p.sim.propose_alliance(1, 0); p.sim.tick(); }, "cd"},
        {"dialog.waiting", [](HUD&, PointerScene&) {}, 4, SDL_BUTTON_LEFT, [](PointerScene& p) { p.sim.propose_alliance(0, 1); p.sim.tick(); }, "cd"},
        {"dialog.breakconfirm", [](HUD& h, PointerScene& p) { h.request_team_up(p.sim, 1); }, 4, SDL_BUTTON_LEFT, no_scene, "cd"},
    };
    for (const State& s : states) {
        const std::string name = std::string("ptr.click.") + s.name;
        if (!wanted(name)) continue;
        PointerScene ps;
        s.scene(ps);
        ClickTally tally;
        const ClickProbe probe{ps, s.setup, s.button, &tally};
        const Measure m = refined_sweep(probe, s.stride);
        const uint64_t n = m.count;
        record(name, m);
        // the scenario does what it says: some click orders, answers, closes, latches, scrolls ... (a fingerprint of clicks that do nothing would pin nothing)
        for (const char* e = s.expects; *e != '\0'; ++e) {
            uint64_t seen = 0;
            const char* what = "";
            switch (*e) {
                case 'c': seen = tally.commands; what = "issues a command"; break;
                case 'q': seen = tally.quit; what = "quits"; break;
                case 'o': seen = tally.options; what = "changes an option"; break;
                case 'd': seen = tally.dialog; what = "opens or answers an alliance dialog"; break;
                case 's': seen = tally.selection; what = "keeps a selection"; break;
                case 'l': seen = tally.latched; what = "latches a pedestal"; break;
                case 'm': seen = tally.scrolled; what = "scrolls the view"; break;
                case 'p': seen = tally.captured; what = "captures the pointer"; break;
                case 'x': seen = tally.closed; what = "closes the dialog"; break;
                default: break;
            }
            check(seen > 0, std::string("click scenario ") + s.name + ": some click " + what);
        }
        check(tally.clicks == n, std::string("click scenario ") + s.name + ": every click was probed");
    }
}

/// The full-screen screens' buttons: the setup screen (a local host, a room's host, a room's guest) and the results screen, at every second pixel: which button is hovered or
/// pressed, which callback runs, what changes
void screen_click_scenarios() {
    using MS = MapSelectScreen;
    struct Variant {
        const char* name;
        bool networked;
        bool host;
    };
    for (const Variant& v : {Variant{"local", false, true}, Variant{"room_host", true, true}, Variant{"room_guest", true, false}}) {
        const std::string name = std::string("ptr.screen.setup.") + v.name;
        if (!wanted(name)) continue;
        MS base = make_setup_screen();
        base.update(0.5f);
        if (v.networked) {
            MS::RoomView view;
            view.networked = true;
            view.is_host = v.host;
            view.my_seat = v.host ? uint8_t{0} : uint8_t{1};
            view.seats[0] = {true, "Alice", MS::Thumb::Good};
            view.seats[1] = {true, "Bob", MS::Thumb::Ok};
            base.set_room(view);
        }
        const auto probe = [&](Fnv64& h, int32_t x, int32_t y) {
            {
                MS screen = base;
                int started = 0;
                int left = 0;
                int maps = 0;
                int fogs = 0;
                std::vector<uint32_t> sounds;
                screen.set_on_start([&](const std::string&) { ++started; });
                screen.set_on_quit([&]() { ++left; });
                screen.set_on_map_changed([&](const std::string&) { ++maps; });
                screen.set_on_fog_changed([&](bool) { ++fogs; });
                screen.set_on_play_sfx([&](uint32_t id) { sounds.push_back(id); });
                screen.handle_mouse_motion(x, y);
                h.flag(screen.up_button().hovered());
                h.flag(screen.down_button().hovered());
                h.flag(screen.start_button().hovered());
                screen.handle_mouse_down(x, y, 1);
                h.flag(screen.up_button().pressed());
                h.flag(screen.down_button().pressed());
                h.flag(screen.start_button().pressed());
                screen.handle_mouse_up(x, y, 1);
                h.i32(started);
                h.i32(left);
                h.i32(maps);
                h.i32(fogs);
                h.i32(screen.get_selected_index());
                h.flag(screen.is_fog_of_war_enabled());
                h.flag(screen.is_locked());
                h.u32(static_cast<uint32_t>(sounds.size()));
                for (uint32_t id : sounds) h.u32(id);
            }
        };
        record(name, refined_sweep(probe, 4));
    }
    for (const bool rows : {false, true}) {
        const std::string name = std::string("ptr.screen.results.") + (rows ? "rows" : "waiting");
        if (!wanted(name)) continue;
        sim::MatchResult result;
        result.is_over = true;
        result.stats[0].score = 300;
        result.stats[1].score = 200;
        result.decide_winners();
        const auto probe = [&](Fnv64& h, int32_t x, int32_t y) {
            {
                ScorecardModal card;
                int left = 0;
                int replayed = 0;
                std::vector<uint32_t> sounds;
                card.set_on_quit([&]() { ++left; });
                card.set_on_replay([&]() { ++replayed; });
                card.set_on_play_sfx([&](uint32_t id) { sounds.push_back(id); });
                card.show(result, 0);
                if (rows) card.update(0.25f);
                card.handle_mouse_motion(x, y);
                h.flag(card.is_quit_hovered());
                h.flag(card.handle_mouse_down(x, y));
                h.flag(card.is_quit_pressed());
                h.flag(card.handle_mouse_up(x, y));
                h.i32(left);
                h.i32(replayed);
                h.u32(static_cast<uint32_t>(sounds.size()));
                for (uint32_t id : sounds) h.u32(id);
            }
        };
        record(name, refined_sweep(probe, 4));
    }
}
