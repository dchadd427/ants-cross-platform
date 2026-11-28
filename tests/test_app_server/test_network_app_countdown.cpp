// Tests of the network in the application: looking round in the countdown of a room, the list of connections (protocol 21, docs/VIEW_AND_HUD.md "The countdown card" and "The list of connections")
#include "network_app_fixtures.hpp"

void run_countdown_look_tests() {
    TEST_CASE("N5.102 Protocol 21, Looking Round In The Countdown: A Drag On The Map Moves The View, The Wheel Zooms It, The Minimap Moves It, Nothing Is Picked Or Ordered (No Command Reaches The Sink), The Card Is See-Through While The View Moves And Solid A Second After It Stops, And A Drag Still Held At The First Turn Is Nobody's Click") {
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-5";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = net::kMatchCountdownSeconds;
        spec.solo_countdown_s = net::kSoloCountdownSeconds;
        spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-5";
        cfg.player_name = "Ana";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && app.hud().is_match_countdown_active(); }, 15000));
        struct CountingSink : sim::CommandSink {
            int submitted{0};
            sim::CommandResult submit(const sim::Command&) override { ++submitted; return sim::CommandResult{}; }
        } sink;
        app.hud().set_command_sink(&sink);                                                   // (what the HUD orders goes here, and is counted)
        ViewportCamera& camera = app.renderer().camera();
        const LayoutRect view = app.hud().layout().view();
        const auto button = [&](uint32_t type, int32_t x, int32_t y) {
            SDL_MouseButtonEvent e{};
            e.type = type;
            e.button = SDL_BUTTON_LEFT;
            e.x = x;
            e.y = y;
            app.handle_mouse_button(e);
        };
        const auto motion = [&](int32_t x, int32_t y) {
            SDL_MouseMotionEvent e{};
            e.type = SDL_MOUSEMOTION;
            e.x = x;
            e.y = y;
            app.handle_mouse_motion(e);
        };
        const int32_t mx = view.x + view.w / 2;
        const int32_t my = view.y + view.h / 2;
        // the wheel zooms the view in (a small map lies whole in the view at the first level: it can be moved only when it is larger than the view)
        const float zoom_before = camera.zoom;
        SDL_MouseWheelEvent wheel{};
        wheel.type = SDL_MOUSEWHEEL;
        wheel.y = 1;
        motion(mx, my);
        for (int i = 0; i < 6; ++i) app.handle_mouse_wheel(wheel);
        ASSERT_TRUE(camera.zoom > zoom_before);
        camera.center_on(static_cast<int32_t>(app.sim().grid().width()) * 24, static_cast<int32_t>(app.sim().grid().height()) * 24, app.sim().grid().width(), app.sim().grid().height());
        const float x0 = camera.x;
        const float y0 = camera.y;
        hall.step(1600);
        ASSERT_EQ(app.hud().countdown_card_look(), 0);                                       // solid: nothing has moved the view since the zoom, a second and a half ago
        // a drag on the map: the ground follows the pointer, the card turns see-through, nothing is picked or ordered
        button(SDL_MOUSEBUTTONDOWN, mx, my);
        motion(mx - 40, my - 30);
        motion(mx - 80, my - 60);
        hall.step(100);
        ASSERT_TRUE(camera.x != x0 && camera.y != y0);
        ASSERT_TRUE(app.hud().countdown_card_look() > 0 && app.hud().is_looking());
        button(SDL_MOUSEBUTTONUP, mx - 80, my - 60);
        ASSERT_FALSE(app.hud().is_looking());
        ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u && sink.submitted == 0);
        // the card stays see-through for a second after the view stopped, then is solid again
        hall.step(800);
        ASSERT_TRUE(app.hud().countdown_card_look() == 255);
        hall.step(1500);
        ASSERT_EQ(app.hud().countdown_card_look(), 0);
        // the minimap: the left button held on it moves the view to the pointer
        const LayoutRect mini = app.hud().layout().minimap();
        const float before_x = camera.x;
        button(SDL_MOUSEBUTTONDOWN, mini.x + 3, mini.y + 3);
        for (int i = 0; i < 4; ++i) app.run_frame_with_delta(0.05f);                         // (the view follows the held button at the INPUT task's 50 ms)
        button(SDL_MOUSEBUTTONUP, mini.x + 3, mini.y + 3);
        ASSERT_TRUE(camera.x != before_x);
        ASSERT_TRUE(sink.submitted == 0 && app.hud().get_selected_ant_ids().empty());
        // a drag that the first turn finds still held: its release is nobody's click (no selection, no order)
        hall.step(2500);
        button(SDL_MOUSEBUTTONDOWN, mx, my);
        motion(mx + 10, my + 10);
        uint32_t waited = 0;
        while (app.sim().current_tick() == 0 && waited < 20000) {
            ASSERT_TRUE(app.hud().is_match_countdown_active());
            hall.step(10);
            waited += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1 && !app.hud().is_match_countdown_active());
        button(SDL_MOUSEBUTTONUP, mx + 10, my + 10);
        ASSERT_TRUE(sink.submitted == 0 && app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
        app.hud().set_command_sink(app.net());
        hall.step(3000);
        ASSERT_TRUE(app.sim().current_tick() > 40 && !app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.103 Ping Stats: A Click On The Corner's Ping Text In A Match Of A Network Game Opens The List Of Connections And The Next One Closes It (A Press Away From The Text, The Right Button And The Countdown Do Not), It Is Closed In The Next Match, It Orders Nothing, And Show Ping Off Takes The Text And The List Away") {
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-6";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = net::kMatchCountdownSeconds;
        spec.solo_countdown_s = net::kSoloCountdownSeconds;
        spec.bots = {ai::BotSpec{1, "standard", ai::Level::Medium}};
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-6";
        cfg.player_name = "Ana";
        cfg.show_ping = true;                                                                  // (the text is there only while Show ping is on)
        cfg.show_ping_given = true;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && app.hud().is_match_countdown_active(); }, 15000));
        const auto button = [&](uint32_t type, uint8_t which, int32_t x, int32_t y) {
            SDL_MouseButtonEvent e{};
            e.type = type;
            e.button = which;
            e.x = x;
            e.y = y;
            app.handle_mouse_button(e);
        };
        const auto click = [&](int32_t x, int32_t y, uint8_t which = SDL_BUTTON_LEFT) {
            button(SDL_MOUSEBUTTONDOWN, which, x, y);
            button(SDL_MOUSEBUTTONUP, which, x, y);
        };
        app.render_frame();
        click(app.connections_hit().x + 2, app.connections_hit().y + 2);
        ASSERT_FALSE(app.connections_open());                                                 // (the card is up: the list waits for the match)
        ASSERT_TRUE(hall.until([&]() { return app.sim().current_tick() > 3 && !app.hud().is_match_start_modal_active(); }, 20000));
        app.render_frame();
        const LayoutRect hit = app.connections_hit();
        ASSERT_TRUE(hit.w > 0 && hit.h > 0 && !app.connections_open());
        ASSERT_TRUE(hit.x > app.hud().layout().view().x && hit.y > app.hud().layout().view().bottom() - 80);     // (the ping text stands above the corner row, low in the picture)
        button(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT, hit.x - 20, hit.y + 2);                   // a press next to the text, a release on it: nothing
        button(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT, hit.x + 2, hit.y + 2);
        click(hit.x + 2, hit.y + 2, SDL_BUTTON_RIGHT);
        ASSERT_FALSE(app.connections_open());
        click(hit.x + 2, hit.y + 2);
        ASSERT_TRUE(app.connections_open());
        app.render_frame();
        ASSERT_TRUE(app.hud().get_selected_ant_ids().empty());
        click(hit.x + 2, hit.y + 2);
        ASSERT_FALSE(app.connections_open());
        click(hit.x + 2, hit.y + 2);
        ASSERT_TRUE(app.connections_open());
        hall.step(200);
        app.render_frame();
        ASSERT_TRUE(app.connections_open() && !app.hud().is_modal_open());                    // (it is no dialog: the match goes on under it)
        // the rows that it lists are the match's: the player's own and the computer player's
        const std::vector<ConnectionRow> rows = connection_rows(app.connection_seats(), app.net()->my_seat());
        ASSERT_EQ(rows.size(), 2u);
        ASSERT_TRUE(rows[0].name == "Ana (you)" && rows[0].tag == "server" && rows[1].tag.compare(0, 3, "bot") == 0 && rows[1].ping.empty());
        ASSERT_FALSE(rows[0].name.empty());
        // Show ping Off takes the text away, and the list that it opened with it: the list opens only while the setting is on
        const LayoutRect was = hit;
        ASSERT_TRUE(app.game_menu_set(game_menu::kShowPing, 0));
        ASSERT_FALSE(app.connections_open());
        app.render_frame();
        ASSERT_EQ(app.connections_hit().w, 0);
        click(was.x + 2, was.y + 2);
        ASSERT_FALSE(app.connections_open());
        ASSERT_TRUE(app.game_menu_set(game_menu::kShowPing, 1));
        app.render_frame();
        click(was.x + 2, was.y + 2);
        ASSERT_TRUE(app.connections_open());
    } TEST_END();

    TEST_CASE("N5.104 Protocol 21, Fairness Of Looking Round In The Countdown: With Fog Of War On Moving, Zooming And Dragging The View Reveals Nothing And Changes Nothing (The Revealed Ground, The State Hash And The Tick Stay As They Were, And No Command Is Sent)") {
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-7";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.fog = true;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = net::kMatchCountdownSeconds;
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", srv.port(), "Bob", 255, "CDN-7"));
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-7";
        cfg.player_name = "Ana";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && app.hud().is_match_countdown_active(); }, 15000));
        ASSERT_TRUE(app.sim().get_world_state().fog_of_war_enabled);
        struct CountingSink : sim::CommandSink {
            int submitted{0};
            sim::CommandResult submit(const sim::Command&) override { ++submitted; return sim::CommandResult{}; }
        } sink;
        app.hud().set_command_sink(&sink);
        const std::vector<uint8_t> revealed = app.sim().get_world_state().fog_revealed;
        const uint64_t hash = app.sim().state_hash().total;
        size_t seen = 0;
        for (uint8_t v : revealed) seen += v != 0 ? 1u : 0u;
        ASSERT_TRUE(seen > 0 && seen < revealed.size());                                    // (some ground is revealed, most is not)
        ViewportCamera& camera = app.renderer().camera();
        const LayoutRect view = app.hud().layout().view();
        const auto button = [&](uint32_t type, int32_t x, int32_t y) {
            SDL_MouseButtonEvent e{};
            e.type = type;
            e.button = SDL_BUTTON_LEFT;
            e.x = x;
            e.y = y;
            app.handle_mouse_button(e);
        };
        SDL_MouseMotionEvent move{};
        move.type = SDL_MOUSEMOTION;
        move.x = view.x + view.w / 2;
        move.y = view.y + view.h / 2;
        app.handle_mouse_motion(move);
        SDL_MouseWheelEvent wheel{};
        wheel.type = SDL_MOUSEWHEEL;
        wheel.y = 1;
        for (int i = 0; i < 6; ++i) app.handle_mouse_wheel(wheel);
        const float x0 = camera.x;
        button(SDL_MOUSEBUTTONDOWN, view.x + view.w / 2, view.y + view.h / 2);
        for (int i = 1; i <= 8; ++i) {
            move.x = view.x + view.w / 2 - 25 * i;
            move.y = view.y + view.h / 2 - 15 * i;
            app.handle_mouse_motion(move);
        }
        button(SDL_MOUSEBUTTONUP, move.x, move.y);
        ASSERT_TRUE(camera.x != x0);                                                         // (the view did move)
        hall.step(300);
        ASSERT_TRUE(app.sim().current_tick() == 0 && app.hud().is_match_countdown_active());
        ASSERT_TRUE(app.sim().get_world_state().fog_revealed == revealed);
        ASSERT_EQ(app.sim().state_hash().total, hash);
        ASSERT_EQ(sink.submitted, 0);
        app.hud().set_command_sink(app.net());
        bob.net.freeze();
    } TEST_END();
}
