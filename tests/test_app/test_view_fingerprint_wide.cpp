// View fingerprint, part 4: the wide match screen (milestone M3), the same safety net for the 16:9 picture of 960 x 540.
// (See test_view_fingerprint.cpp for what the whole program pins.)
#include "view_fingerprint_fixtures.hpp"

namespace {
constexpr int32_t kWViewW = 762;                              // its map view
constexpr int32_t kWViewH = 500;

/// The nine view origins on a map of tiles_w x tiles_h tiles in the 762 x 500 view: away from the borders, at the four corners and the four edges; on an axis where the map is
/// smaller than the view there is one origin, the centred one (negative)
std::array<Cam, 9> wide_nine_cams(int32_t tiles_w, int32_t tiles_h) {
    const int32_t mx = tiles_w * 32 - kWViewW;
    const int32_t my = tiles_h * 32 - kWViewH;
    const int32_t x0 = mx > 0 ? 0 : mx / 2, x1 = mx > 0 ? mx / 2 : mx / 2, x2 = mx > 0 ? mx : mx / 2;
    const int32_t y0 = my > 0 ? 0 : my / 2, y1 = my > 0 ? my / 2 : my / 2, y2 = my > 0 ? my : my / 2;
    return {{{"tl", x0, y0}, {"t", x1, y0}, {"tr", x2, y0}, {"l", x0, y1}, {"mid", x1, y1}, {"r", x2, y1}, {"bl", x0, y2}, {"b", x1, y2}, {"br", x2, y2}}};
}

ViewportCamera wide_camera_at(int32_t x, int32_t y) {
    ViewportCamera cam = camera_at(x, y);
    cam.set_view(kWide.view());
    cam.centre_small_maps = true;
    return cam;
}

// ---- the HUD's draw calls at 960 x 540

void hud_wide_scenarios(const assets::AssetArchive& arc) {
    constexpr int32_t kMaxX = static_cast<int32_t>(kMap) * 32 - kWViewW;
    constexpr int32_t kMaxY = static_cast<int32_t>(kMap) * 32 - kWViewH;
    const auto make = [&](uint8_t local = 0, uint32_t w = kMap, uint32_t h = kMap) { return std::make_unique<Scene>(arc, local, w, h, &kWide); };

    for (uint8_t p = 0; p < 4; ++p) {                                           // the "Get ready" dialog over the wide frame, for each local colour
        auto s = make(p);
        s->hud.start_match_modal();
        s->look(100, 80);
        s->shoot("hud.wide.start.modal.p" + std::to_string(p));
    }
    for (uint8_t p = 0; p < 4; ++p) {
        auto s = make(p);
        s->shoot("hud.wide.start.idle.p" + std::to_string(p));
    }
    {
        const char* names[7] = {"worker", "worker2", "bomber", "fire", "thief", "combat", "swimmer"};
        for (uint32_t i = 0; i < 7; ++i) {
            auto s = make(0);
            s->hud.select_ant(1 + i);
            s->tick(0);
            s->shoot(std::string("hud.wide.sel.one.") + names[i]);
        }
    }
    {
        auto s = make(0);
        s->hud.set_selected_ant_ids({1, 2, 3, 4, 5});
        s->tick(0);
        s->shoot("hud.wide.sel.group");
        auto c = make(0);
        for (auto& a : c->world.ants) if (a.player_id == 0) a.is_holding = true;
        c->hud.set_selected_ant_ids({1, 2});
        c->tick(0);
        c->shoot("hud.wide.sel.group.carrying");
        auto e = make(0);
        e->hud.select_ant(12);
        e->tick(0);
        e->shoot("hud.wide.sel.enemy");
        auto own = make(0);
        own->hud.select_base(0);
        own->tick(0);
        own->shoot("hud.wide.sel.hill.own");
        auto few = make(0);
        few->world.player_eggs[0] = 3;
        few->hud.select_base(0);
        few->tick(0);
        few->shoot("hud.wide.sel.hill.own.3eggs");
        auto enemy = make(0);
        enemy->hud.select_base(1);
        enemy->tick(0);
        enemy->shoot("hud.wide.sel.hill.enemy");
        auto blue = make(2);
        blue->hud.select_ant(21);
        blue->tick(0);
        blue->shoot("hud.wide.sel.one.p2");
    }
    {   // the pedestals rising
        const uint32_t moments[3] = {100, 450, 1200};
        for (uint32_t i = 0; i < 3; ++i) {
            auto s = make(0);
            s->hud.select_ant(3);
            s->tick(0);
            s->shoot("hud.wide.pedestal.rising." + std::to_string(moments[i]), moments[i]);
        }
    }
    // the chat: the log takes the extra height (161 px), the input box is 60 px lower
    auto fill_chat = [](Scene& s) {
        s.hud.add_chat_entry("Alice", "Hello everybody, this is a rather long message that has to wrap over several lines of the log window.", false, 3);
        s.hud.add_chat_entry("Bob", "Team only: attack the red hill together!", true, 2);
        s.hud.add_chat_entry("Carol", "ok", false, 1);
        s.hud.add_news_flash(65000, "Black dropped out of the game!");
    };
    {
        auto s = make(0);
        fill_chat(*s);
        s->hud.set_chat_input("Typing a message to all of you");
        s->shoot("hud.wide.chat.typing.caret_on", 10000);
        s->shoot("hud.wide.chat.typing.caret_off", 10100);
        auto l = make(0);
        l->hud.set_chat_input("This message is much too long to fit into the one line box of the chat input field at the bottom");
        l->shoot("hud.wide.chat.typing.long");
        auto m = make(0);                                        // many entries: the taller log shows more of them
        for (int i = 0; i < 14; ++i) m->hud.add_chat_entry("Player" + std::to_string(i), "message number " + std::to_string(i) + " of the long conversation", i % 3 == 0, i % 4);
        for (int i = 0; i < 60; ++i) {
            g_now_ms += 50;
            m->hud.update(m->world, 1);
        }
        m->shoot("hud.wide.chat.scrolled", 9000);
        auto t = make(0);                                        // an ally: [Team] exists; hovered and pressed buttons ([All] (532, 443, 44 x 24) moved by (320, 60))
        t->world.player_alliances = {1, 0, 255, 255};
        fill_chat(*t);
        t->tick(0);
        t->shoot("hud.wide.chat.team.buttons");
        t->move(555 + 320, 455 + 60);
        t->shoot("hud.wide.chat.team.hover_all");
        t->move(600 + 320, 455 + 60);
        t->press(600 + 320, 455 + 60);
        t->shoot("hud.wide.chat.team.pressed_team");
        auto off = make(0);
        off->hud.options().chat = false;
        fill_chat(*off);
        off->shoot("hud.wide.chat.off");
    }
    // the minimap and its view frame (762 x 500 of the map) at the corners and edges; maps of other sizes, the small ones centred
    {
        auto s = make(0);
        s->shoot("hud.wide.minimap.teams");
        const struct { const char* name; int32_t x, y; } cams[] = {{"tl", 0, 0}, {"tr", kMaxX, 0}, {"bl", 0, kMaxY}, {"br", kMaxX, kMaxY}, {"top", 700, 0}, {"left", 0, 700}, {"mid", 700, 700}};
        for (const auto& c : cams) {
            auto t = make(0);
            t->look(c.x, c.y);
            t->shoot(std::string("hud.wide.minimap.frame.") + c.name);
        }
        const struct { uint32_t w, h; } sizes[] = {{31, 31}, {40, 40}, {14, 14}, {16, 16}, {12, 12}, {80, 50}, {13, 9}};
        for (const auto& sz : sizes) {
            auto t = make(0, sz.w, sz.h);
            t->camera.x = 100000.0f;                                // (the far corner, held by the camera's rule: a small axis is centred)
            t->camera.y = 100000.0f;
            t->camera.clamp_to_bounds(sz.w, sz.h);
            t->shoot("hud.wide.minimap.size." + std::to_string(sz.w) + "x" + std::to_string(sz.h));
        }
    }
    {
        auto s = make(0);                                        // fog
        s->world.fog_of_war_enabled = true;
        s->world.fog_revealed.assign(static_cast<size_t>(kMap) * kMap, 0);
        for (uint32_t y = 0; y < kMap; ++y) {
            for (uint32_t x = 0; x < kMap; ++x) {
                const int32_t dx = static_cast<int32_t>(x) - 14;
                const int32_t dy = static_cast<int32_t>(y) - 12;
                if (dx * dx + dy * dy < 120 || (x >= 40 && y >= 40 && (x + y) % 3 == 0)) s->world.fog_revealed[static_cast<size_t>(y) * kMap + x] = 1;
            }
        }
        s->hud.select_ant(1);
        s->tick(0);
        s->shoot("hud.wide.fog.on");
    }
    // the score boxes: slots by the team's order, whatever the number of teams
    {
        auto s = make(0);
        s->world.player_alliances = {1, 0, 255, 255};
        s->world.player_scores = {500, 500, 450, 100};
        s->shoot("hud.wide.scores.allied");
        auto b = make(0);
        b->world.player_alliances = {255, 255, 3, 2};
        b->world.player_scores = {10, 20, 530, 530};
        b->shoot("hud.wide.scores.allied.bottom_pair");
        auto d = make(0);
        d->world.dropped_mask = 0x04;
        d->shoot("hud.wide.scores.dropped");
        for (const uint8_t mask : std::array<uint8_t, 5>{0x03, 0x07, 0x0B, 0x0F, 0x0D}) {
            auto t = make(mask == 0x0D ? 1 : 0);
            t->hud.set_roster_mask(mask);
            t->shoot("hud.wide.scores.roster." + std::to_string(static_cast<unsigned>(mask)));
        }
        auto u = make(3);
        u->hud.set_roster_mask(0x09);
        u->shoot("hud.wide.scores.two_players.black_local");
        auto n = make(0);
        n->hud.set_player_name("Local Player With A Long Name");
        n->hud.set_team_names({"", "Redmond", "A Very Long Name Indeed", "Dave"});
        n->shoot("hud.wide.scores.names");
    }
    // the windows of the original: the dialogs over the middle of the map view (origin (137, 59)), the pages centred over the clay (origin (160, 30))
    {
        auto s = make(0);
        s->hud.open_quit_dialog();
        s->shoot("hud.wide.quit.open");
        s->move(200 + 137, 270 + 59);
        s->shoot("hud.wide.quit.hover_yes");
        s->move(300 + 137, 270 + 59);
        s->press(300 + 137, 270 + 59);
        s->shoot("hud.wide.quit.pressed_no");
    }
    {
        auto s = make(0);
        s->hud.open_options();
        s->shoot("hud.wide.options.open");
        auto t = make(0);
        t->hud.options().sound_volume = 30;
        t->hud.options().music_volume = 80;
        t->hud.options().scroll_speed = 99;
        t->hud.options().chat = false;
        t->hud.options().quick_chat[0] = "Hello!";
        t->hud.open_options();
        t->shoot("hud.wide.options.changed");
        t->move(375 + 160, 435 + 30);
        t->shoot("hud.wide.options.hover_return");
    }
    {
        auto s = make(0);
        s->hud.open_quick_help();
        s->shoot("hud.wide.quickhelp.open");
        s->move(540 + 160, 445 + 30);
        s->shoot("hud.wide.quickhelp.hover_return");
        s->press(540 + 160, 445 + 30);
        s->shoot("hud.wide.quickhelp.pressed_return");
    }
    {
        auto s = make(1);
        s->world.pending_invite_from[1] = 0;
        s->tick(0);
        s->shoot("hud.wide.dialog.invitation");
        s->move(160 + 137, 270 + 59);
        s->shoot("hud.wide.dialog.invitation.hover_accept");
        auto w = make(0);
        w->world.pending_invite_from[1] = 0;
        w->tick(0);
        w->shoot("hud.wide.dialog.waiting");
        auto b = make(0);
        b->sim.form_alliance(0, 2);
        b->hud.request_team_up(b->sim, 1);
        b->shoot("hud.wide.dialog.breakconfirm");
    }
    {   // the top bar's buttons, moved by (320, 0)
        const struct { const char* name; int32_t x; } buttons[] = {{"help", 490 + 320}, {"options", 550 + 320}, {"quit", 600 + 320}};
        for (const auto& b : buttons) {
            auto s = make(0);
            s->move(b.x, 18);
            s->shoot(std::string("hud.wide.topbar.hover_") + b.name);
            s->press(b.x, 18);
            s->shoot(std::string("hud.wide.topbar.pressed_") + b.name);
        }
    }
    {   // the rubber band over the wide map view
        auto s = make(0);
        s->press(120, 100);
        s->move(330, 260);
        s->shoot("hud.wide.marquee.band");
        auto t = make(0);
        t->press(120, 100);
        t->move(900, 700);                                       // held one pixel inside the view: it ends at (777, 520)
        t->shoot("hud.wide.marquee.clamped");
        auto u = make(0);
        u->press(600, 300);
        u->move(2, 2);
        u->shoot("hud.wide.marquee.clamped_low");
    }
    {
        auto s = make(0);
        s->hud.post_status("A status text that is much too long to fit into the status box of 139 pixels width");
        s->shoot("hud.wide.status.long");
    }
}

void ptr_wide_scenarios() {
    // the zones of the picture: the map view, the minimap, the chat log
    {
        HUD hud;
        hud.set_layout(kWide);
        record("ptr.wide.class.map_view", sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.over_map(x, y)); }));
        record("ptr.wide.class.minimap", sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.over_minimap(x, y)); }));
        record("ptr.wide.class.chat_view", sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.flag(hud.in_chat_view(x, y)); }));
    }
    // the edge strips: every pixel of the picture from nine cameras of the 60 x 60 map, and the other maps (the small ones from their one, centred, camera)
    for (const Cam& c : wide_nine_cams(60, 60)) {
        const std::string name = "ptr.wide.edge.60x60.rate50.cam_" + std::string(c.name);
        if (wanted(name)) record(name, wide_edge_sweep(60, 60, 50, {c}));
    }
    const struct { int32_t w, h, rate; } more[] = {{60, 60, 0}, {60, 60, 99}, {31, 31, 50}, {40, 40, 25}, {14, 14, 50}, {16, 16, 50}, {12, 12, 50}, {80, 50, 75}, {13, 9, 50}};
    for (const auto& m : more) {
        const std::string name = "ptr.wide.edge." + map_name(m.w, m.h) + ".rate" + std::to_string(m.rate) + ".all9";
        if (!wanted(name)) continue;
        const auto nine = wide_nine_cams(m.w, m.h);
        record(name, wide_edge_sweep(m.w, m.h, m.rate, std::vector<Cam>(nine.begin(), nine.end())));
    }
    {   // the pointer outside the picture: a ring of 16 pixels around it
        const std::string name = "ptr.wide.edge.60x60.rate50.outside_ring.mid";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t y = -16; y < kWH + 16; ++y) {
                for (int32_t x = -16; x < kWW + 16; ++x) {
                    if (x >= 0 && x < kWW && y >= 0 && y < kWH) continue;
                    hash_scroll(h, edge_scroll_step(x, y, 50, 600, 700, 60, 60, kWide));
                    ++n;
                }
            }
            record(name, Measure{h.value(), n});
        }
    }
    // the minimap: the world point under every pixel, the view's step towards it from nine cameras
    const struct { int32_t w, h; } sizes[] = {{60, 60}, {31, 31}, {40, 40}, {14, 14}, {16, 16}, {80, 50}};
    for (const auto& s : sizes) {
        const std::string name = "ptr.wide.minimap.point." + map_name(s.w, s.h);
        if (!wanted(name)) continue;
        record(name, sweep_wide([&](Fnv64& h, int32_t x, int32_t y) {
                   int32_t wx = 0, wy = 0;
                   minimap_point(x, y, s.w, s.h, wx, wy, kWide);
                   h.i32(wx);
                   h.i32(wy);
               }));
    }
    for (const auto& s : {std::pair<int32_t, int32_t>{60, 60}, std::pair<int32_t, int32_t>{31, 31}, std::pair<int32_t, int32_t>{14, 14}, std::pair<int32_t, int32_t>{16, 16}}) {
        const std::string name = "ptr.wide.minimap.scroll." + map_name(s.first, s.second) + ".all9";
        if (!wanted(name)) continue;
        Fnv64 h;
        uint64_t n = 0;
        for (const Cam& c : wide_nine_cams(s.first, s.second)) {
            for (int32_t y = 0; y < kWH; ++y) {
                for (int32_t x = 0; x < kWW; ++x) {
                    hash_scroll(h, minimap_scroll_step(x, y, c.x, c.y, s.first, s.second, kWide));
                    ++n;
                }
            }
        }
        record(name, Measure{h.value(), n});
    }
    {   // the start view of every anchor tile of six maps in the 762 x 500 view
        const std::string name = "ptr.wide.start_view.origins";
        if (wanted(name)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& s : {std::pair<int32_t, int32_t>{60, 60}, std::pair<int32_t, int32_t>{31, 31}, std::pair<int32_t, int32_t>{40, 40}, std::pair<int32_t, int32_t>{14, 14},
                                  std::pair<int32_t, int32_t>{16, 16}, std::pair<int32_t, int32_t>{80, 50}}) {
                for (int32_t ty = 0; ty < s.second; ++ty) {
                    for (int32_t tx = 0; tx < s.first; ++tx) {
                        int32_t ox = 0, oy = 0;
                        start_view_origin(tx, ty, s.first, s.second, ox, oy, kWide);
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

void camera_wide_scenarios() {
    const struct { uint32_t w, h; } maps[] = {{60, 60}, {31, 31}, {14, 14}, {16, 16}, {12, 12}, {13, 9}, {80, 50}};
    for (const bool centre : {true, false}) {
        const std::string suffix = centre ? "" : ".corner";
        if (wanted("view.wide.camera.clamp" + suffix)) {
            Fnv64 h;
            uint64_t n = 0;
            const float values[] = {-500.0f, -1.0f, 0.0f, 0.5f, 7.0f, 100.25f, 441.0f, 761.0f, 762.0f, 763.0f, 1000.0f, 1157.5f, 1158.0f, 1159.0f, 1420.0f, 1600.0f, 5000.0f};
            for (const auto& m : maps) {
                for (float vx : values) {
                    for (float vy : values) {
                        ViewportCamera cam = wide_camera_at(0, 0);
                        cam.centre_small_maps = centre;
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
            record("view.wide.camera.clamp" + suffix, Measure{h.value(), n});
        }
        if (wanted("view.wide.camera.center_on" + suffix)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& m : maps) {
                for (int32_t wy = -64; wy < static_cast<int32_t>(m.h) * 32 + 64; wy += 37) {
                    for (int32_t wx = -64; wx < static_cast<int32_t>(m.w) * 32 + 64; wx += 41) {
                        ViewportCamera cam = wide_camera_at(0, 0);
                        cam.centre_small_maps = centre;
                        cam.center_on(wx, wy, m.w, m.h);
                        h.i32(cam.world_x);
                        h.i32(cam.world_y);
                        ++n;
                    }
                }
            }
            record("view.wide.camera.center_on" + suffix, Measure{h.value(), n});
        }
        if (wanted("view.wide.camera.scroll_pixels" + suffix)) {
            Fnv64 h;
            uint64_t n = 0;
            for (const auto& m : maps) {
                ViewportCamera cam = wide_camera_at(0, 0);
                cam.centre_small_maps = centre;
                for (int32_t i = 0; i < 400; ++i) {
                    const int32_t dx = ((i * 7) % 61) - 30;
                    const int32_t dy = ((i * 11) % 53) - 26;
                    cam.scroll_pixels(dx * (1 + i / 50), dy * (1 + i / 50), m.w, m.h);
                    h.i32(cam.world_x);
                    h.i32(cam.world_y);
                    ++n;
                }
            }
            record("view.wide.camera.scroll_pixels" + suffix, Measure{h.value(), n});
        }
    }
    // screen -> world and world -> screen of a centred camera (a 16 x 16 map: the camera is at (-125, 0)) and of a camera in the middle of a big map
    for (const Cam& c : {Cam{"origin", 0, 0}, Cam{"mid", 700, 700}, Cam{"far", 1158, 1420}, Cam{"small16", -125, 6}}) {
        const std::string name = std::string("view.wide.camera.screen_to_world.") + c.name;
        if (wanted(name)) {
            const ViewportCamera cam = wide_camera_at(c.x, c.y);
            record(name, sweep_wide([&](Fnv64& h, int32_t x, int32_t y) {
                       int32_t wx = -1, wy = -1;
                       const bool ok = cam.screen_to_world(x, y, wx, wy);
                       h.flag(ok);
                       if (ok) {
                           h.i32(wx);
                           h.i32(wy);
                       }
                   }));
        }
        const std::string name2 = std::string("view.wide.camera.world_to_screen.") + c.name;
        if (wanted(name2)) {
            const ViewportCamera cam = wide_camera_at(c.x, c.y);
            Fnv64 h;
            uint64_t n = 0;
            for (int32_t wy = c.y - 80; wy < c.y + kWViewH + 80; wy += 9) {
                for (int32_t wx = c.x - 80; wx < c.x + kWViewW + 80; wx += 9) {
                    int32_t sx = 0, sy = 0;
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

/// The cursor at every pixel of the wide picture for a fixed state of the selection (the scene of the classic sweeps, seen from (400, 400) in the 762 x 500 view)
Measure wide_cursor_sweep(PointerScene& ps, HUD& hud, const ViewportCamera& cam) {
    const sim::WorldState& world = ps.world();
    const sim::Grid& grid = ps.sim.grid();
    return sweep_wide([&](Fnv64& h, int32_t x, int32_t y) { h.byte(static_cast<uint8_t>(hud.evaluate_cursor(x, y, world, grid, cam))); });
}

void cursor_wide_scenarios() {
    const std::vector<CursorState> states = {
        {"idle", false, [](HUD&, PointerScene&, ViewportCamera&) {}},
        {"own_worker", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"own_bomber", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.bomber); }},
        {"own_thief", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.thief); }},
        {"bomber_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.bomber);
             h.handle_mouse_down(560 + 320, 180, SDL_BUTTON_LEFT, p.sim, cam);      // the ability pedestal (slot 2) latches, moved by dx
             h.handle_mouse_up(560 + 320, 180, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"worker_move_latched", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500 + 320, 170, SDL_BUTTON_LEFT, p.sim, cam);
             h.handle_mouse_up(500 + 320, 170, SDL_BUTTON_LEFT, p.sim, cam);
         }},
        {"enemy_inspected", false, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.foe); }},
        {"hill_own", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.select_base(0); }},
        {"fog_own_worker", true, [](HUD& h, PointerScene& p, ViewportCamera&) { h.select_ant(p.worker); }},
        {"options_open", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.open_options(); }},
        {"quit_open", false, [](HUD& h, PointerScene&, ViewportCamera&) { h.open_quit_dialog(); }},
        {"band_dragging", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(100, 100, SDL_BUTTON_LEFT, p.sim, cam);
             h.handle_mouse_motion(300, 260, p.sim, cam);
         }},
        {"button_captured", false,
         [](HUD& h, PointerScene& p, ViewportCamera& cam) {
             h.select_ant(p.worker);
             h.handle_mouse_down(500 + 320, 18, SDL_BUTTON_LEFT, p.sim, cam);
         }},
    };
    for (const CursorState& s : states) {
        const std::string name = std::string("ptr.wide.cursor.") + s.name + ".cam_mid400";
        if (!wanted(name)) continue;
        PointerScene ps(s.fog);
        HUD hud;
        hud.set_ticks_function(&test_clock);
        g_now_ms = 0;
        hud.init(0);
        hud.set_layout(kWide);
        hud.set_sim_query(&ps.sim);
        ViewportCamera cam = wide_camera_at(400, 400);
        s.setup(hud, ps, cam);
        record(name, wide_cursor_sweep(ps, hud, cam));
    }
    for (const char* state : {"idle", "own_worker"}) {
        for (const Cam& c : wide_nine_cams(60, 60)) {
            const std::string name = std::string("ptr.wide.cursor.") + state + ".cam_" + c.name;
            if (!wanted(name)) continue;
            PointerScene ps;
            HUD hud;
            hud.set_ticks_function(&test_clock);
            g_now_ms = 0;
            hud.init(0);
            hud.set_layout(kWide);
            hud.set_sim_query(&ps.sim);
            if (std::string(state) == "own_worker") hud.select_ant(ps.worker);
            record(name, wide_cursor_sweep(ps, hud, wide_camera_at(c.x, c.y)));
        }
    }
}

void click_wide_scenarios() {
    struct State {
        const char* name;
        std::function<void(HUD&, PointerScene&)> setup;
        uint8_t button;
        const char* expects;
    };
    const std::vector<State> states = {
        {"idle", [](HUD&, PointerScene&) {}, SDL_BUTTON_LEFT, "mp"},
        {"own_worker", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, SDL_BUTTON_LEFT, "csplmp"},
        {"own_bomber", [](HUD& h, PointerScene& p) { h.select_ant(p.bomber); }, SDL_BUTTON_LEFT, "csl"},
        {"hill_own", [](HUD& h, PointerScene&) { h.select_base(0); }, SDL_BUTTON_LEFT, "cs"},
        {"own_worker.right_button", [](HUD& h, PointerScene& p) { h.select_ant(p.worker); }, SDL_BUTTON_RIGHT, "c"},
        {"dialog.quit", [](HUD& h, PointerScene&) { h.open_quit_dialog(); }, SDL_BUTTON_LEFT, "qx"},
        {"dialog.quickhelp", [](HUD& h, PointerScene&) { h.open_quick_help(); }, SDL_BUTTON_LEFT, "x"},
        {"dialog.options", [](HUD& h, PointerScene&) { h.open_options(); }, SDL_BUTTON_LEFT, "ox"},
        {"dialog.start_modal", [](HUD& h, PointerScene&) { h.start_match_modal(); }, SDL_BUTTON_LEFT, ""},
    };
    for (const State& s : states) {
        const std::string name = std::string("ptr.wide.click.") + s.name;
        if (!wanted(name)) continue;
        PointerScene ps;
        ClickTally tally;
        ClickProbe probe{ps, s.setup, s.button, &tally};
        probe.layout = &kWide;
        const Measure m = refined_sweep(probe, 4, kWW, kWH);
        record(name, m);
        for (const char* e = s.expects; *e != '\0'; ++e) {
            uint64_t seen = 0;
            const char* what = "";
            switch (*e) {
                case 'c': seen = tally.commands; what = "issues a command"; break;
                case 'q': seen = tally.quit; what = "quits"; break;
                case 'o': seen = tally.options; what = "changes an option"; break;
                case 's': seen = tally.selection; what = "keeps a selection"; break;
                case 'l': seen = tally.latched; what = "latches a pedestal"; break;
                case 'm': seen = tally.scrolled; what = "scrolls the view"; break;
                case 'p': seen = tally.captured; what = "captures the pointer"; break;
                case 'x': seen = tally.closed; what = "closes the dialog"; break;
                default: break;
            }
            check(seen > 0, std::string("wide click scenario ") + s.name + ": some click " + what);
        }
        check(tally.clicks == m.count, std::string("wide click scenario ") + s.name + ": every click was probed");
    }
}

// ---- what the real renderer draws on the 960 x 540 canvas

struct WideCanvas {
    std::vector<uint8_t> px = std::vector<uint8_t>(static_cast<size_t>(kWW) * static_cast<size_t>(kWH) * 4u, 0);
};

WideCanvas read_wide_canvas(SDL_Renderer* sr) {
    WideCanvas c;
    if (SDL_RenderReadPixels(sr, nullptr, SDL_PIXELFORMAT_RGBA32, c.px.data(), kWW * 4) != 0) std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    return c;
}

void record_wide_canvas(const std::string& name, WideCanvas c, const std::vector<MaskRect>& mask = {}) {
    if (!wanted(name)) return;
    for (const MaskRect& m : mask) {
        for (int32_t y = std::max(0, m.y0); y < std::min(kWH, m.y1); ++y) {
            for (int32_t x = std::max(0, m.x0); x < std::min(kWW, m.x1); ++x) std::memset(&c.px[(static_cast<size_t>(y) * kWW + static_cast<size_t>(x)) * 4u], 0, 4);
        }
    }
    Fnv64 h;
    h.bytes(c.px.data(), c.px.size());
    if (!g_save_dir.empty()) {
        std::vector<uint8_t> shown = c.px;
        for (size_t i = 0; i < shown.size(); i += 4) {
            if (shown[i + 3] == 0) {
                shown[i] = 255;
                shown[i + 1] = 0;
                shown[i + 2] = 255;
            }
            shown[i + 3] = 255;
        }
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(shown.data(), kWW, kWH, 32, kWW * 4, SDL_PIXELFORMAT_RGBA32);
        if (s != nullptr) {
            SDL_SaveBMP(s, (g_save_dir + "/" + name + ".bmp").c_str());
            SDL_FreeSurface(s);
        }
    }
    record(name, Measure{h.value(), kWPixels});
}

/// A real renderer over a hidden 960 x 540 window with the wide layout, a map loaded (or none: a synthetic world), a pinned clock
struct WideRendererRig {
    WideRendererRig(const assets::AssetArchive& archive, const std::string& map, uint32_t synthetic_tiles = 0) : arc(archive) {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        win = SDL_CreateWindow("view-fingerprint-wide", 0, 0, kWW, kWH, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(kWW, kWH);
        renderer.set_layout(kWide);
        renderer.pin_animation_clock(1500);
        renderer.set_hud_team(0);
        if (synthetic_tiles > 0) {
            engine.init_test_world(synthetic_tiles, synthetic_tiles, 1, 600000);
        } else {
            if (!level.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + map + ".LVL")) return;
            engine.init(level, 1337);
            renderer.set_level(level);
        }
        world = engine.get_world_state();
        world.ants.clear();
        ok = true;
    }
    ~WideRendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    WideRendererRig(const WideRendererRig&) = delete;
    WideRendererRig& operator=(const WideRendererRig&) = delete;
    void look(int32_t x, int32_t y, uint32_t tiles_w, uint32_t tiles_h) {
        renderer.camera().x = static_cast<float>(x);
        renderer.camera().y = static_cast<float>(y);
        renderer.camera().clamp_to_bounds(tiles_w, tiles_h);
    }
    int32_t cam_x() { return renderer.camera().world_x; }
    int32_t cam_y() { return renderer.camera().world_y; }
    void shoot(const std::string& name, const std::vector<MaskRect>& mask = {}) {
        if (!wanted(name)) return;
        renderer.begin_frame();
        renderer.render_world(world, engine.grid(), -1, {}, false, false, -1, -1, -1, 0.0f);
        record_wide_canvas(name, read_wide_canvas(renderer.get_sdl_renderer()), mask);
    }
    const assets::AssetArchive& arc;
    SDL_Window* win{nullptr};
    assets::LevelData level;
    sim::SimulationEngine engine;
    Renderer renderer;
    sim::WorldState world;
    bool ok{false};
};

/// TrueType text is masked as in the classic frames: the boxes of the labels of the wide picture (fixed rectangles) and the plate in the canvas's corner
const MaskRect kWMaskPlate{790, 526, 960, 540};

/// The match screen's labels: the status line (801, 254) 139 px, the chat log (802, 299) 138 x 161, the chat input (801, 484) 139 px, the score labels (the local one at (632 .. 719, 4), the
/// bottom ones right aligned in [113, 209), [377, 465) and [632, 719) x 524: ScreenLayout::score_slot(1 .. 3) of the 960 x 540 picture, whose cuts move the classic slots (5 .. 101, 163 .. 251,
/// 312 .. 399) right by 108, 214 and 320). The masks of the two left ones used to lie at 323 .. 423 and 481 .. 573, beside their labels, so "Red:" and "Blue:" were hashed: SDL's alpha blit
/// rounds differently on x86-64 and on ARM, so the 22 fingerprints that show them failed on every x86-64 machine (the Linux and Windows jobs of .github/workflows/ci.yml found it)
const std::vector<MaskRect> kWMaskMatch = {{799, 252, 942, 268}, {800, 297, 942, 462}, {799, 482, 942, 498}, {630, 2, 722, 20}, {111, 522, 211, 540}, {375, 522, 467, 540}, {630, 522, 722, 540}};

/// The "Get ready" dialog's two labels and the quit dialog's prompt, moved by the dialog origin (137, 59)
const std::vector<MaskRect> kWMaskStartModal = {{265, 167, 509, 331}, {265, 347, 509, 373}};

const std::vector<MaskRect> kWMaskQuit = {{265, 237, 529, 401}};

/// The setup screen's labels in the wide screen (the setup screen of a 960 x 540 canvas is the whole canvas, no page: tests/test_app/test_wide_setup.cpp pins the screen in detail): the map's name,
/// its description and the prompt, the four players' names, the caption under the map preview. The preview itself is NOT masked: it is the game's own render of the map (map_preview.hpp), so the
/// three px.wide.app.setup.* fingerprints that show it (refreshed, hover_start, cursor) were regenerated when the preview changed from minimap colour blocks to that render (before_refresh has none)
const std::vector<MaskRect> kWMaskSetup = {{35, 370, 287, 392}, {35, 438, 401, 460}, {34, 505, 402, 529}, {733, 93, 857, 115}, {733, 143, 857, 165}, {733, 193, 857, 215}, {733, 243, 857, 265}, {349, 359, 657, 381}};

/// The options screen's edit fields and the results rows are text too: the whole page's text boxes are not known one by one, so the pages that carry TrueType text are masked as a
/// whole where it appears (the results' rows, the options' four quick-chat fields)
const std::vector<MaskRect> kWMaskResults = {{95, 262, 930, 290}, {95, 350, 930, 378}, {95, 400, 930, 428}};      // the three rows' text of the wide results page: the names (x 100) and the numbers (x 805 ...)

const std::vector<MaskRect> kWMaskOptions = {{250, 398, 396, 418}, {250, 430, 396, 450}, {460, 398, 606, 418}, {460, 430, 606, 450}};

std::vector<MaskRect> wide_masks(std::initializer_list<const std::vector<MaskRect>*> lists) {
    std::vector<MaskRect> all{kWMaskPlate};
    for (const auto* l : lists) all.insert(all.end(), l->begin(), l->end());
    return all;
}

void world_pixel_wide_scenarios(const assets::AssetArchive& arc) {
    // the playfield of 762 x 500 at the corners and edges of two maps (MEDIUM 60 x 60; TINY 31 x 31 = 992 px: narrower than 1.5 views), ants of every kind straddling the borders
    for (const char* map : {"MEDIUM", "TINY"}) {
        const int32_t tiles = std::string(map) == "TINY" ? 31 : 60;
        for (const Cam& c : wide_nine_cams(tiles, tiles)) {
            const std::string name = std::string("px.wide.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            WideRendererRig rig(arc, map);
            check(rig.ok, "the wide renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y, static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 140, 4242u + static_cast<uint32_t>(c.x), kWViewW, kWViewH);
            add_effects(rig.world, rig.cam_x(), rig.cam_y(), kWViewW, kWViewH);
            rig.shoot(name);
        }
    }
    for (const char* map : {"GAUNTLET", "ISLANDS", "SMALL", "TREASURE"}) {
        const int32_t tiles = std::string(map) == "SMALL" ? 40 : 60;
        for (const Cam& c : {wide_nine_cams(tiles, tiles)[0], wide_nine_cams(tiles, tiles)[4]}) {
            const std::string name = std::string("px.wide.world.") + map + ".cam_" + c.name;
            if (!wanted(name)) continue;
            WideRendererRig rig(arc, map);
            check(rig.ok, "the wide renderer rig for " + std::string(map) + " is up");
            if (!rig.ok) continue;
            rig.look(c.x, c.y, static_cast<uint32_t>(tiles), static_cast<uint32_t>(tiles));
            populate_view(rig.world, arc, rig.cam_x(), rig.cam_y(), 60, 17u + static_cast<uint32_t>(c.x), kWViewW, kWViewH);
            rig.shoot(name);
        }
    }
    // the maps that are smaller than the view (a synthetic world: flat colours): centred, black around it; 16 x 16 is 12 px taller than the view, 12 x 12 is smaller on both axes
    for (const uint32_t tiles : {16u, 12u}) {
        const std::string name = "px.wide.world.small" + std::to_string(tiles);
        if (!wanted(name)) continue;
        WideRendererRig rig(arc, "", tiles);
        check(rig.ok, "the wide renderer rig for a " + std::to_string(tiles) + " x " + std::to_string(tiles) + " world is up");
        if (!rig.ok) continue;
        rig.look(100000, 100000, tiles, tiles);
        check(rig.cam_x() == (static_cast<int32_t>(tiles) * 32 - 762) / 2, "the " + std::to_string(tiles) + " x " + std::to_string(tiles) + " map is centred across the view");
        rig.shoot(name);
    }
}

// ---- whole application frames at 960 x 540: the pages and the match

struct WideAppRig {
    WideAppRig() {
        const QuietStdout quiet;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg = app_config();
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;                              // the canvas and the window are the same size: one pixel of the canvas is one pixel read back
        cfg.window_w = kWW;
        cfg.window_h = kWH;
        cfg.window_width = kWW;
        cfg.window_height = kWH;
        ok = app.init(cfg);
        if (!ok) return;
        app.renderer().pin_animation_clock(1500);
        app.hud().set_ticks_function(&test_clock);
        g_now_ms = 0;
        app.handle_window_event(window_event(SDL_WINDOWEVENT_LEAVE));
    }
    void pointer_at(int32_t x, int32_t y) {
        app.handle_window_event(window_event(SDL_WINDOWEVENT_ENTER));
        app.note_pointer(x, y);
    }
    std::string map_path(const char* name) const { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + name + ".LVL"; }
    bool start_match(const char* map) {
        if (!app.start_game(map_path(map))) return false;
        app.hud().reset();
        g_now_ms = 0;
        app.hud().start_match_modal();
        return true;
    }
    void shot(const std::string& name, const std::vector<MaskRect>& mask) {
        if (!wanted(name)) return;
        app.render_frame();
        record_wide_canvas(name, read_wide_canvas(app.renderer().get_sdl_renderer()), mask);
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

void app_pixel_wide_scenarios() {
    // the setup screen of the 16:9 canvas (its wide version: the whole canvas) and the quick help (its wide page: the whole canvas too; the loading screen needs a window)
    if (wanted_group("px.wide.app.setup")) {
        WideAppRig rig;
        check(rig.ok, "the wide application is up");
        if (rig.ok) {
            check(rig.app.picture() == (LayoutRect{0, 0, 960, 540}), "the wide application's setup screen is the whole canvas (its own wide version, not a page)");
            rig.shot("px.wide.app.setup.before_refresh", wide_masks({}));
            rig.app.map_select().update(0.5f);
            rig.shot("px.wide.app.setup.refreshed", wide_masks({&kWMaskSetup}));
            rig.app.map_select().handle_mouse_motion(MapSelectScreen::BTN_START_X + 5 + 320, MapSelectScreen::BTN_START_Y + 5 + 60);        // START! of the wide screen
            rig.shot("px.wide.app.setup.hover_start", wide_masks({&kWMaskSetup}));
            rig.pointer_at(300, 200);
            rig.shot("px.wide.app.setup.cursor", wide_masks({&kWMaskSetup}));
        }
    }
    // the setup screen of a room in the 16:9 picture with its chat box: the art of the box (the black chat box, the input box, the label's place), the fill footer's place and the players' box; the
    // TrueType text (the chat's lines, the typed line, the label "Chat", the footer, the names) is masked, its draw calls are pinned by screen.wide.room.*
    if (wanted_group("px.wide.app.setup.room")) {
        for (const bool leader : {true, false}) {
            WideAppRig rig;
            if (!rig.ok) continue;
            MapSelectScreen::RoomView view;
            view.networked = true;
            view.is_host = false;
            view.leader = leader;
            view.my_seat = leader ? 0 : 1;
            view.seats[0] = {true, leader ? "Ana" : "Lea", MapSelectScreen::Thumb::Good};
            view.seats[1] = {true, "Ben", leader ? MapSelectScreen::Thumb::Ok : MapSelectScreen::Thumb::Good};
            view.map_file = "ISLANDS.LVL";
            view.status = leader ? "Press START: the empty seats get Medium bots." : "Waiting for the host to start the game...";
            rig.app.map_select().set_room(view);
            MapSelectScreen::ChatPanel panel;
            panel.visible = true;
            panel.lines = {{"Fog of War is on, so START seats no bots.", true}, {"Ben: hi, which map?", false}, {"Ana: TINY first, then ISLANDS", false}};
            panel.typed = "ready in a minute";
            panel.caret = true;
            rig.app.map_select().set_chat_panel(panel);
            if (leader) rig.app.map_select().set_fill_footer("Empty seats at START:", "Medium bots");
            rig.app.map_select().update(0.5f);
            const SetupLayout& layout = SetupLayout::of(leader ? SetupVariant::Online : SetupVariant::Guest);
            std::vector<MaskRect> text = kWMaskSetup;
            text.push_back({layout.chat.label_x - 2, layout.chat.label_y - 2, layout.chat.label_x + 46, layout.chat.label_y + 24});                 // "Chat"
            text.push_back({layout.chat.lines.x, layout.chat.lines.y, layout.chat.lines.x + layout.chat.lines.w, layout.chat.lines.y + layout.chat.lines.h});   // the lines
            text.push_back({layout.chat.input_text.x - 2, layout.chat.input_text.y - 2, layout.chat.input_text.x + layout.chat.input_text.w, layout.chat.input_text.y + 24});   // the typed line
            text.push_back({layout.footer_x - 2, layout.footer_y1 - 2, layout.footer_x + 200, layout.footer_y2 + 22});                                // the footer's two lines
            text.push_back({layout.caption_centre_x - 154, layout.caption_y - 2, layout.caption_centre_x + 154, layout.caption_y + 22});               // the caption under the map preview (the room layouts' own place: the single screen's is in kWMaskSetup)
            rig.shot(leader ? "px.wide.app.setup.room_leader" : "px.wide.app.setup.room_guest", wide_masks({&text}));
        }
    }
    if (wanted_group("px.wide.app.quickhelp")) {
        WideAppRig rig;
        if (rig.ok) {
            rig.app.finish_loading();
            check(rig.app.state() == AppState::QuickHelp, "the quick help follows the loading screen");
            rig.shot("px.wide.app.quickhelp.rest", wide_masks({}));
            rig.app.quick_help_move(895, 510);                                          // START! of the wide page (849, 497, 98 x 27: the bottom right corner)
            rig.shot("px.wide.app.quickhelp.hover_start", wide_masks({}));
        }
    }
    if (wanted_group("px.wide.app.results")) {
        WideAppRig rig;
        if (rig.ok && rig.start_match("SMALL")) {
            sim::MatchResult mr;
            mr.is_over = true;
            mr.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
            const int32_t scores[4] = {900, 600, 420, 100};
            for (size_t p = 0; p < 4; ++p) {
                mr.stats[p].score = scores[p];
                mr.stats[p].friendly_lost = static_cast<uint32_t>(5 + p);
                mr.stats[p].enemy_killed = static_cast<uint32_t>(18 - 4 * p);
                mr.stats[p].new_hatched = static_cast<uint32_t>(25 - 5 * p);
            }
            mr.decide_winners();
            rig.app.scorecard().show(mr, 0);
            rig.app.scorecard().update(0.25f);
            rig.shot("px.wide.app.results", wide_masks({&kWMaskResults}));
        }
    }
    // the match screen: the "Get ready" dialog, the start view of every map, the corners and edges of MEDIUM, the windows of the original
    if (wanted_group("px.wide.app.match")) {
        WideAppRig rig;
        if (rig.ok && rig.start_match("MEDIUM")) {
            check(rig.app.picture() == (LayoutRect{0, 0, 960, 540}), "the wide match is the whole canvas");
            rig.shot("px.wide.app.match.modal", wide_masks({&kWMaskMatch, &kWMaskStartModal}));
            rig.app.hud().dismiss_match_start_modal();
            rig.shot("px.wide.app.match.start_view", wide_masks({&kWMaskMatch}));
            const int32_t tiles = static_cast<int32_t>(rig.app.sim().grid().width());
            for (const Cam& c : wide_nine_cams(tiles, tiles)) {
                rig.look(c.x, c.y);
                rig.shot(std::string("px.wide.app.match.cam_") + c.name, wide_masks({&kWMaskMatch}));
            }
            rig.look(700, 700);
            rig.app.hud().open_quit_dialog();
            rig.shot("px.wide.app.match.quit_dialog", wide_masks({&kWMaskMatch, &kWMaskQuit}));
            rig.app.hud().close_quit_dialog();
            rig.app.hud().open_options();
            rig.shot("px.wide.app.match.options_page", wide_masks({&kWMaskMatch, &kWMaskOptions}));
            rig.app.hud().close_options();
            rig.app.hud().open_quick_help();
            rig.shot("px.wide.app.match.quickhelp_page", wide_masks({&kWMaskMatch}));
            rig.app.hud().close_quick_help();
            rig.pointer_at(300, 300);
            rig.shot("px.wide.app.match.cursor_map", wide_masks({&kWMaskMatch}));
            rig.pointer_at(10, 240);
            rig.shot("px.wide.app.match.cursor_edge", wide_masks({&kWMaskMatch}));
            rig.pointer_at(955, 270);                                 // the east strip at the right edge of the 960 wide picture
            rig.shot("px.wide.app.match.cursor_edge_east", wide_masks({&kWMaskMatch}));
        }
    }
    for (const char* map : {"TINY", "SMALL", "ISLANDS", "GAUNTLET", "TREASURE"}) {
        const std::string name = std::string("px.wide.app.start.") + map;
        if (!wanted(name)) continue;
        WideAppRig rig;
        if (rig.ok && rig.start_match(map)) {
            rig.app.hud().dismiss_match_start_modal();
            rig.shot(name, wide_masks({&kWMaskMatch}));
        }
    }
}

}  // namespace

void wide_scenarios(const assets::AssetArchive& arc) {
    hud_wide_scenarios(arc);
    ptr_wide_scenarios();
    camera_wide_scenarios();
    cursor_wide_scenarios();
    click_wide_scenarios();
    world_pixel_wide_scenarios(arc);
    app_pixel_wide_scenarios();
}
