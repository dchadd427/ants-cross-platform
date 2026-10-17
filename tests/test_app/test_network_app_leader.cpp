// Tests of the network in the application: the leader of a dedicated server's room, ping and delay, the hidden page of the web build
#include "network_app_fixtures.hpp"


// The first player in the room of a dedicated server is its LEADER (protocol 7): its setup screen is the host's, with START, in a "server room" mode
void run_leader_tests() {
    const auto click_start = [](Application& app) {
        const int32_t x = MapSelectScreen::BTN_START_X + 5;
        const int32_t y = MapSelectScreen::BTN_START_Y + 5;
        app.map_select().handle_mouse_motion(x, y);
        app.map_select().handle_mouse_down(x, y, 1);
        app.map_select().handle_mouse_up(x, y, 1);
    };
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        return cfg;
    };
    using Rows = std::array<int8_t, 4>;
    using L = net::FillLevel;
    // the leader's screen (protocol 14): the middle of a player's row, a press on it (a click: down and up), the screen's own 500 ms before it shows its players, the room's notices as the player reads them
    const auto row_centre = [](Application& app, size_t row) {
        const LayoutRect r = app.map_select().player_row_rect(row);
        return std::pair<int32_t, int32_t>(r.x + r.w / 2, r.y + r.h / 2);
    };
    const auto press_row = [&](Application& app, size_t row) {
        const std::pair<int32_t, int32_t> at = row_centre(app, row);
        app.map_select().handle_mouse_motion(at.first, at.second);
        app.map_select().handle_mouse_down(at.first, at.second, 1);
        app.map_select().handle_mouse_up(at.first, at.second, 1);
    };
    const auto settle = [](Application& app) {
        for (int i = 0; i < 6; ++i) app.run_frame_with_delta(0.1f);
    };
    const auto notices_of = [](Application& app) {
        std::vector<std::string> out;
        for (const net::ChatLine& line : app.net()->pregame_chat()) {
            if (line.notice()) out.push_back(line.text);
        }
        return out;
    };
    const auto plan = [](L green, L red, L blue, L black) { return net::FillPlan(std::array<L, 4>{green, red, blue, black}); };

    TEST_CASE("N5.20 Leader: The First Player Of A Server's Room Has The Host's Screen With START (The Room's Map And Fog Shown, Nothing To Change), START With Nobody Else Gives The Can't-Go Cue And Sends Nothing, With A Second Player The Server Starts The Match With Both") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-APP", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-APP", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 0 && app.net()->room().leader == 0; }, 8000));
        // alone: the leader. The screen is the host's (START, Up, Down, Fog) but the setup is the room's, and the prompt is the host's
        ASSERT_TRUE(app.net()->is_leader() && !app.net()->is_host());
        {
            const auto& room = app.map_select().room();
            ASSERT_TRUE(room.networked && !room.is_host && room.leader && room.my_seat == 0);
            ASSERT_FALSE(app.map_select().is_guest());
            ASSERT_TRUE(app.map_select().leads_server_room() && app.map_select().has_start_button());
            ASSERT_FALSE(app.map_select().can_change_setup());                              // the map and the fog are the room's
            ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kPressStart)));
            ASSERT_EQ(room.map_file, std::string("TINY.LVL"));
            ASSERT_EQ(app.map_select().get_maps()[static_cast<size_t>(app.map_select().get_selected_index())].filename, "TINY.LVL");
            ASSERT_TRUE(room.seats[0].occupied && room.seats[0].name == "Leader" && !room.seats[1].occupied);
        }
        // Up, Down, the wrap and the Fog buttons change nothing (and tell nobody)
        {
            const int32_t index = app.map_select().get_selected_index();
            const std::string map_before = app.net()->room().map_name;
            const auto press = [&](int32_t x, int32_t y) {
                app.map_select().handle_mouse_motion(x, y);
                app.map_select().handle_mouse_down(x, y, 1);
                app.map_select().handle_mouse_up(x, y, 1);
            };
            press(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3);
            press(MapSelectScreen::BTN_UP_X + 3, MapSelectScreen::BTN_UP_Y + 3);
            press(MapSelectScreen::BTN_FOW_ON_X + 2, MapSelectScreen::BTN_FOW_ON_Y + 2);
            for (int i = 0; i < 12; ++i) {                                                  // (more than the six maps: the wrap)
                app.map_select().handle_key_down(SDLK_DOWN);
                app.map_select().handle_key_down(SDLK_UP);
            }
            app.map_select().handle_key_down(SDLK_UP);
            ASSERT_EQ(app.map_select().get_selected_index(), index);
            ASSERT_FALSE(app.map_select().is_fog_of_war_enabled());
            hall.step(300);
            ASSERT_EQ(app.net()->room().map_name, map_before);
            ASSERT_FALSE(app.net()->room().fog);
            ASSERT_EQ(server.status("LEAD-APP").map, std::string("TINY.LVL"));
            ASSERT_FALSE(server.status("LEAD-APP").fog);
        }
        // START with nobody to play with: the host's answer to START with too few players, the can't-go cue, and nothing else
        {
            const size_t channels = app.audio_mixer().active_channel_count();
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);              // the cue (sound 63)
            click_start(app);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 3);              // the click on the pressed button and the cue again
            hall.step(500);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.map_select().is_locked());
            const server::RoomStatus s = server.status("LEAD-APP");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0);       // nothing was sent: the client knows
        }
        // a second player: the leader keeps the screen, the other one has the guest's
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-APP"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_EQ(app.map_select().room().status, std::string("Tap a player to change their color."));            // (the leader's first company: how to change a color, for five seconds; looked at as soon as he is there)
        ASSERT_TRUE(hall.until([&]() { return app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_TRUE(app.net()->is_leader() && app.map_select().leads_server_room());
        ASSERT_FALSE(bob.net.is_leader());
        ASSERT_FALSE(bob.net.request_start());                                              // a guest sends nothing
        ASSERT_EQ(bob.net.room().leader, 0);
        ASSERT_EQ(bob.net.status_text(), std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        hall.step(5000);
        ASSERT_EQ(app.map_select().room().status, std::string(sim::strings::text(sim::strings::kPressStart)));
        ASSERT_TRUE(app.map_select().room().seats[1].occupied && app.map_select().room().seats[1].name == "Bob");
        ASSERT_TRUE(MapSelectScreen::row_seats(app.map_select().room()) == Rows({0, 1, -1, -1}));
        // START: the server starts the match with the two of them (a room for four that is half full)
        ASSERT_EQ(server.status("LEAD-APP").expected, 4);
        bob.hold_report = true;                                                              // Bob's machine is slow to report the map: the room stays in its loading phase
        click_start(app);
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading && bob.report_pending; }, 10000));
        {   // while the map loads the leader keeps the host's screen, locked: START sends nothing more (a second click, Enter, S), Leave works
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_TRUE(app.net()->is_leader() && app.map_select().leads_server_room() && !app.map_select().is_guest());
            ASSERT_TRUE(app.map_select().is_locked());
            click_start(app);
            app.map_select().handle_key_down(SDLK_RETURN);
            app.map_select().handle_key_down(SDLK_s);
            hall.step(300);
            ASSERT_EQ(server.status("LEAD-APP").ignored_start_requests, 0u);                // nothing was sent
            ASSERT_TRUE(server.status("LEAD-APP").state == server::RoomState::Loading);
        }
        bob.hold_report = false;
        bob.release_report();
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 10000));
        const server::RoomStatus s = server.status("LEAD-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 2 && s.expected == 4);
        ASSERT_TRUE(s.names[0] == "Leader" && s.names[1] == "Bob" && s.names[2].empty());
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(bob.sim.roster_mask(), 0x03);
        ASSERT_TRUE(app.map_select().is_locked());                                          // the Start locked the screen: nothing more to press
        hall.step(5000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim));
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        ASSERT_TRUE(server.status("LEAD-APP").state == server::RoomState::Running);          // the referee saw no desync
        app.quit();
        ASSERT_FALSE(app.network_active());
    } TEST_END();

    TEST_CASE("N5.21 Leader: A Player Who Is Not The Leader Keeps The Guest's Screen (START Does Nothing); When The Leader Leaves The Next One Gets The Host's Screen At The Next Room Message And Starts The Match With The Players Who Are There") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-SWITCH", 4));
        Peer ann;                                                                           // the first to join: the leader
        ASSERT_TRUE(ann.net.join("127.0.0.1", server.port(), "Ann", 255, "LEAD-SWITCH"));
        Hall hall{server, nullptr, {&ann}};
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room && ann.net.is_leader(); }, 8000));
        ASSERT_EQ(ann.net.status_text(), std::string(sim::strings::text(sim::strings::kPressStart)));       // the leader has the host's prompt
        ASSERT_FALSE(ann.net.request_start());                                              // alone in the room: nobody to play with, nothing is sent
        hall.step(300);
        ASSERT_TRUE(server.status("LEAD-SWITCH").ignored_start_requests == 0 && server.status("LEAD-SWITCH").state == server::RoomState::Waiting);
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-SWITCH", "Second")));
        Peer cat;
        hall.app = &app;
        hall.peers.push_back(&cat);
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 1 && app.net()->room().leader == 0; }, 8000));
        // not the leader: the guest's screen, the guest's prompt, nothing to press
        ASSERT_FALSE(app.net()->is_leader());
        {
            const auto& room = app.map_select().room();
            ASSERT_TRUE(room.networked && !room.is_host && !room.leader && room.my_seat == 1);
            ASSERT_TRUE(app.map_select().is_guest() && !app.map_select().leads_server_room() && !app.map_select().has_start_button());
            ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
            ASSERT_TRUE(MapSelectScreen::row_seats(room) == Rows({1, 0, -1, -1}));          // itself first, then the other
            app.map_select().handle_key_down(SDLK_RETURN);
            app.map_select().handle_key_down(SDLK_s);
            click_start(app);
            hall.step(1000);
            const server::RoomStatus s = server.status("LEAD-SWITCH");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);      // nothing was sent
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }
        // the leader leaves: the application is the earliest player who is left, and the screen changes with the next Room message
        ann.net.leave();
        ASSERT_TRUE(hall.until([&]() { return app.net()->is_leader(); }, 8000));
        ASSERT_EQ(app.net()->room().leader, 1);
        hall.step(100);
        {
            const auto& room = app.map_select().room();
            ASSERT_TRUE(room.leader && app.map_select().leads_server_room() && !app.map_select().is_guest() && app.map_select().has_start_button());
            ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kPressStart)));
            ASSERT_TRUE(room.seats[1].occupied && room.seats[1].name == "Second" && !room.seats[0].occupied);
            ASSERT_TRUE(MapSelectScreen::row_seats(room) == Rows({1, -1, -1, -1}));           // itself first, as before: the rows do not jump when the screen changes
            ASSERT_EQ(server.status("LEAD-SWITCH").leader, 1);
            ASSERT_EQ(server.status("LEAD-SWITCH").joined, 1);
        }
        // alone again: START is the can't-go cue, nothing is sent
        app.map_select().handle_key_down(SDLK_RETURN);
        hall.step(500);
        ASSERT_TRUE(server.status("LEAD-SWITCH").state == server::RoomState::Waiting && server.status("LEAD-SWITCH").ignored_start_requests == 0);
        // another player joins (seat 0 is free: it takes it); the leader does not change; Enter starts the match with the two of them
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "LEAD-SWITCH"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[0].state == net::SlotState::Client; }, 8000));
        ASSERT_TRUE(app.net()->is_leader() && !cat.net.is_leader());
        ASSERT_EQ(cat.net.my_seat(), 0);
        ASSERT_TRUE(MapSelectScreen::row_seats(app.map_select().room()) == Rows({1, 0, -1, -1}));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 10000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);                                           // the seats 0 and 1 (Ann's seat was taken by Cat)
        ASSERT_EQ(cat.sim.roster_mask(), 0x03);
        ASSERT_TRUE(server.status("LEAD-SWITCH").state == server::RoomState::Running && server.status("LEAD-SWITCH").expected == 4);
        hall.step(3000);
        ASSERT_TRUE(hall.identical(app.sim(), cat.sim));
    } TEST_END();

    TEST_CASE("N5.22 Leader: --start-when N (The Front Page's Card And The Headless Test Clients Give It) Presses START For The Leader Of A Server's Room Once N Players Are In (1 To 4 Only: 1 Is For A Leader With --fill-bots; 2 To 4 Only Before Protocol 11); A Game That Does Not Say It Never Does") {
        {   // the command line
            std::vector<std::string> args = {"ants", "--join", "127.0.0.1:4001", "--room", "R-1", "--start-when", "2"};
            std::vector<char*> storage;
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 2);
            for (const char* value : {"1", "3", "4"}) {
                args = {"ants", "--start-when", value};
                ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, static_cast<uint8_t>(value[0] - '0'));
            }
            for (const char* value : {"0", "5", "-2", "many", ""}) {                         // not a number of players of a match: no hook (1 is one since protocol 11: a person and the bots of a fill)
                args = {"ants", "--start-when", value};
                ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 0);
            }
            args = {"ants", "--start-when"};                                                // no value
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 0);
            args = {"ants", "--join", "127.0.0.1:4001"};
            ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage)).net_start_when, 0);       // off by default
        }
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-HOOK", 4));
        ApplicationConfig cfg = join_config(server, "LEAD-HOOK", "Hook");
        cfg.net_start_when = 3;                                                             // the hook presses START when three are in
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        Peer cat;
        Hall hall{server, &app, {&bob, &cat}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));       // (in, and the leader, before Bob connects: two Hellos over two sockets can be read in either order)
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-HOOK"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_TRUE(app.net()->is_leader());                                                // (the application joined first)
        hall.step(3000);                                                                    // two are in, the hook wants three: nothing is pressed
        ASSERT_TRUE(server.status("LEAD-HOOK").state == server::RoomState::Waiting && server.status("LEAD-HOOK").ignored_start_requests == 0);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "LEAD-HOOK"));
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x07);                                           // the three of them: the room for four started without the fourth
        ASSERT_TRUE(server.status("LEAD-HOOK").state == server::RoomState::Running && server.status("LEAD-HOOK").joined == 3);
        hall.step(3000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim) && hall.identical(app.sim(), cat.sim));
    } TEST_END();

    // The quick help and the leader's START (the quick help's START! lies under it). Events go through the application's own loop, one frame per delivery (a headless application
    // stops after ten frames): the headless window is twice the picture, so a picture point (580, 450), on both START buttons, is the window point (1160, 900).
    const auto button_event = [](Uint32 type, Uint8 clicks) {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = clicks;
        e.button.x = 1160;
        e.button.y = 900;
        SDL_PushEvent(&e);
    };
    const auto key_event = [](SDL_Keycode sym, bool repeat) {
        SDL_Event e{};
        e.type = SDL_KEYDOWN;
        e.key.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
        e.key.state = SDL_PRESSED;
        e.key.repeat = repeat ? 1 : 0;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        SDL_PushEvent(&e);
    };

    TEST_CASE("N5.23 Leader: The Quick Help Does Not Press The Leader's START: The Presses That Continue The Click That Closed It (A Second And A Third Click That SDL Counts, A Quick Click That It Does Not Count, Within The Double-Click Time) Close Nothing And Start Nothing; A Click That Begins After That Time Does Start The Match") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-QH", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-QH", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-QH"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        // two players are in: a request of the leader now starts the match, so whatever reaches START shows
        app.finish_loading();
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // the click on START! of the quick help
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.mouse_screen_x(), 580);                                       // (the window point is the picture point (580, 450): on both buttons)
        ASSERT_EQ(app.mouse_screen_y(), 450);
        ASSERT_TRUE(app.map_select().leads_server_room() && app.closing_click_pending());
        ASSERT_FALSE(app.quick_help_start_button().pressed());
        button_event(SDL_MOUSEBUTTONDOWN, 2);                                       // the second click of the double click (SDL counts it): it would press the leader's START
        button_event(SDL_MOUSEBUTTONUP, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.map_select().start_button().hovered());                     // (the pointer is on it: a hover, nothing pressed)
        button_event(SDL_MOUSEBUTTONDOWN, 3);                                       // and a third
        button_event(SDL_MOUSEBUTTONUP, 3);
        app.run_frame_with_delta(0.001f);
        SDL_Delay(150);
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // a click that SDL does not count as part of the sequence (a hand moves the pointer more than a pixel
        button_event(SDL_MOUSEBUTTONUP, 1);                                         // between the clicks of a double click), 150 ms later: still within the double-click time
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        hall.step(800);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().is_locked());
        ASSERT_TRUE(app.closing_click_pending());                                   // (no new click has begun)
        {
            const server::RoomStatus s = server.status("LEAD-QH");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);      // nothing was asked of the server
        }
        SDL_Delay(600);                                                             // the double-click time is over
        button_event(SDL_MOUSEBUTTONDOWN, 2);                                       // (a press that SDL counts as part of a sequence is swallowed whenever it comes: SDL decides that)
        button_event(SDL_MOUSEBUTTONUP, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.closing_click_pending());
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // a click that begins now: the screen's. START asks, and the server starts the match with the two of them
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.closing_click_pending());
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(server.status("LEAD-QH").state == server::RoomState::Running && server.status("LEAD-QH").joined == 2);
        ASSERT_TRUE(app.map_select().is_locked());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.24 Leader: A Held Key Does Not Press The Leader's START Either: The Enter That Closed The Quick Help (Or S) Repeats Without Effect, A Fresh Click Starts The Match (A Key Leaves No Memory Of A Click); A Click That Began On The Quick Help And Ended After A Key Closed It Starts Nothing") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-KEY", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-KEY", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-KEY"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        app.finish_loading();
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // a press on START! of the quick help that is still held when a key closes the quick help
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(app.quick_help_start_button().pressed());
        key_event(SDLK_RETURN, false);                                              // Enter closes it ...
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.quick_help_start_button().pressed());                      // (the quick help's button is let go of)
        ASSERT_TRUE(app.map_select().leads_server_room());
        key_event(SDLK_RETURN, true);                                               // ... and is held: its repeats, and the repeats of S
        key_event(SDLK_RETURN, true);
        key_event(SDLK_KP_ENTER, true);
        key_event(SDLK_s, true);
        key_event(SDLK_RETURN, true);
        button_event(SDL_MOUSEBUTTONUP, 1);                                         // and the mouse button of the press that began on the quick help is released over START
        app.run_frame_with_delta(0.001f);
        hall.step(800);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().is_locked());
        {
            const server::RoomStatus s = server.status("LEAD-KEY");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);
        }
        ASSERT_FALSE(app.closing_click_pending());                                  // a key that closes the quick help leaves no memory of a click behind
        button_event(SDL_MOUSEBUTTONDOWN, 1);                                       // so a fresh click, here a moment after the key (inside the double-click time), is the screen's:
        button_event(SDL_MOUSEBUTTONUP, 1);                                         // START asks, and the server starts the match
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(server.status("LEAD-KEY").state == server::RoomState::Running && server.status("LEAD-KEY").joined == 2);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.25 Leader: What The Quick Help Left Behind Does Not Outlive The Setup Screen It Led To: A Click That Closed It Is Remembered Only Until A New Click Begins Or The Setup Screen Is Created Again") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-STALE", 4));
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "LEAD-STALE", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-STALE"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_FALSE(app.closing_click_pending());
        app.finish_loading();
        button_event(SDL_MOUSEBUTTONDOWN, 1);
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.closing_click_pending());
        key_event(SDLK_RETURN, false);                                              // the match is started with the key: no click comes, the memory of the old one stays
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(app.closing_click_pending());
        app.return_to_map_select();                                                 // the match is left: the setup screen is created again, and nothing of the quick help is left on it
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.closing_click_pending());
    } TEST_END();

    TEST_CASE("N5.26 LAN Host: The Original's Own Host Screen Is Not Guarded Against A Double Click, Because The Original Is Not: The Second Click Of A Double Click On START! Presses The Host's START And Starts The Match (A Held Enter Does Not: Its Repeat Is Not A Press, N5.53)") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.finish_loading();
        button_event(SDL_MOUSEBUTTONDOWN, 1);
        button_event(SDL_MOUSEBUTTONUP, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.closing_click_pending());                                  // a host is not a machine that joined a room: nothing is held back
        button_event(SDL_MOUSEBUTTONDOWN, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(app.map_select().start_button().pressed());                     // the second click presses the host's START
        button_event(SDL_MOUSEBUTTONUP, 2);
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
    } TEST_END();

    // The same at 16:9: the setup screen is the wide screen (the whole 960 x 540 canvas, the headless window is that size: a window point is a canvas point). The quick help is the wide page too:
    // its START! is anchored to the bottom right corner, (849 .. 947, 497 .. 524), and so lies over the leader's START of the wide screen, (846 .. 944, 499 .. 526), 3 px right and 2 px up as the
    // original's own two buttons do (the quick help was a centred page here, with its START! at (740, 480), clear of the leader's START: the closing click is now a click ON that START).
    const auto wide_button = [](Uint32 type, int32_t x, int32_t y, Uint8 clicks) {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1));
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = clicks;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    };

    TEST_CASE("N5.26w Leader At 16:9: The Wide Setup Screen Is The Online Variant; The Quick Help's Closing Click Still Does Not Press ITS START (A Click Within The Double-Click Time Is The Rest Of That Gesture Wherever It Falls, And Nothing Is Asked Of The Server); A Click After That Time Asks, And The Server Starts The Match") {
        Server server;
        ASSERT_TRUE(server.make_room("LEAD-WIDE", 4));
        ApplicationConfig cfg = join_config(server, "LEAD-WIDE", "Leader");
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "LEAD-WIDE"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        app.run_frame_with_delta(0.001f);
        ASSERT_TRUE(app.map_select().wide_layout() && app.map_select().leads_server_room());
        ASSERT_TRUE(app.map_select().setup_variant() == SetupVariant::Online);
        ASSERT_TRUE(app.map_select().start_button().up_rect() == ButtonRect({846, 499, 98, 27}));
        app.finish_loading();
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::QuickHelp);
        ASSERT_TRUE(app.picture() == LayoutRect({0, 0, 960, 540}));                 // (the quick help is the whole canvas: its wide page)
        ASSERT_TRUE(app.quick_help_start_button().up_rect() == ButtonRect({849, 497, 98, 27}));
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 510, 1);                              // the click on START! of the quick help (in the bottom right corner)
        wide_button(SDL_MOUSEBUTTONUP, 895, 510, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.picture() == LayoutRect({0, 0, 960, 540}));
        ASSERT_TRUE(app.closing_click_pending());
        ASSERT_TRUE(app.map_select().start_button().hovered());                     // (the pointer is where the click was, over the wide START: a hover, nothing pressed)
        ASSERT_FALSE(app.map_select().start_button().pressed());
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 512, 1);                              // a quick click on the wide START: the rest of the gesture (within the double-click time), swallowed
        wide_button(SDL_MOUSEBUTTONUP, 895, 512, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.map_select().start_button().hovered());                     // (the pointer is there: a hover, nothing pressed)
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 512, 2);                              // a second click that SDL counts
        wide_button(SDL_MOUSEBUTTONUP, 895, 512, 2);
        app.run_frame_with_delta(0.001f);
        hall.step(800);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().is_locked());
        {
            const server::RoomStatus s = server.status("LEAD-WIDE");
            ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.joined == 2);      // nothing was asked of the server
        }
        SDL_Delay(600);                                                             // the double-click time is over
        wide_button(SDL_MOUSEBUTTONDOWN, 895, 512, 1);                              // a click that begins now: the screen's. START asks, and the server starts the match with the two of them
        wide_button(SDL_MOUSEBUTTONUP, 895, 512, 1);
        app.run_frame_with_delta(0.001f);
        ASSERT_FALSE(app.closing_click_pending());
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(server.status("LEAD-WIDE").state == server::RoomState::Running && server.status("LEAD-WIDE").joined == 2);
        app.quit();
    } TEST_END();

    // Protocol 14: the leader of a server's room puts a player in another color by pressing the player's row of the Players' Status box (the classic page and the 16:9 one)
    TEST_CASE("N5.91 Leader, The Colors (Protocol 14): A Press On A Player's Row Moves That Player To The Next Free Color, On The Classic Page And On The 16:9 One; The Plan's Bots Follow, The Player Is Told By The Room, The Leader's Own Row Moves The Leader, A Quick Second Press Waits For The Room's Answer; A Release Off The Row, The Right Button, A Row Of Nobody, A Screen That Has Not Shown Its Players, A Guest's Screen And A Locked One Do Nothing; The Match Starts With The Colors As They Are") {
        const std::string hint = "Tap a player to change their color.";

        for (const bool wide : {false, true}) {
            Server server;
            const std::string code = wide ? "MOVE-WIDE" : "MOVE-CLASSIC";
            ASSERT_TRUE(server.make_room(code, 4));
            ApplicationConfig cfg = join_config(server, code, "Ann");
            cfg.fill_bots = plan(L::None, L::None, L::Hard, L::None);                       // a Hard bot for Blue: START seats it there while Blue is empty
            if (wide) {
                cfg.aspect = Aspect::Wide16x9;
                cfg.aspect_given = true;
                cfg.has_window_size = true;
                cfg.window_w = 960;
                cfg.window_h = 540;
            }
            Application leader;
            ASSERT_TRUE(leader.init(cfg));
            Hall hall{server, &leader, {}};
            ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
            leader.run_frame_with_delta(0.001f);
            ASSERT_EQ(leader.state(), AppState::MapSelect);
            ASSERT_EQ(leader.map_select().wide_layout(), wide);
            // the screen shows its players 500 ms after it was made: until then there are no rows to press
            ASSERT_TRUE(!leader.map_select().refreshed() && !leader.map_select().can_move_players());
            press_row(leader, 0);
            ASSERT_TRUE(leader.map_select().pressed_row() == -1 && leader.map_select().hovered_row() == -1);
            hall.step(300);
            ASSERT_TRUE(leader.net()->my_seat() == 0 && server.status(code).seat_moves == 0 && server.status(code).ignored_seat_moves == 0);
            settle(leader);
            ASSERT_TRUE(leader.map_select().refreshed() && leader.map_select().can_move_players());
            ASSERT_TRUE(leader.map_select().room().status != hint);                                    // (alone: nobody to move yet, and nothing is said about it)

            // Gus is the second player: the leader's first company, and the line says how to change a color (for the five seconds that a notice is shown)
            Application guest;
            ASSERT_TRUE(guest.init(join_config(server, code, "Gus")));
            hall.second = &guest;
            ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
            hall.step(30);
            ASSERT_EQ(guest.net()->my_seat(), 1);
            ASSERT_EQ(leader.map_select().room().status, hint);
            ASSERT_EQ(guest.map_select().room().status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));      // (a guest is not told: it cannot move anybody)
            hall.step(5200);
            ASSERT_TRUE(leader.map_select().room().status.find("Blue") != std::string::npos && leader.map_select().room().status.find("Hard") != std::string::npos);        // the START prompt is back
            settle(guest);
            ASSERT_FALSE(guest.map_select().can_move_players());
            ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 1, -1, -1}));

            // the rows: one box wide and one row high, row after row; a row of nobody and the points around the rows are no rows
            {
                const LayoutRect r0 = leader.map_select().player_row_rect(0);
                const LayoutRect r1 = leader.map_select().player_row_rect(1);
                ASSERT_TRUE(r0.w > 100 && r0.h > 20 && r1.x == r0.x && r1.w == r0.w && r1.h == r0.h && r1.y == r0.bottom());
                ASSERT_TRUE(leader.map_select().player_row_rect(3).bottom() == r0.y + 4 * r0.h);
                // the four rows fill the inside of the black box of the picture and the light stays in it (the classic picture's box measures 200 x 200 from (371, 83) on the screen; the 16:9 page's is its layout's)
                if (wide) {
                    const SetupLayout& layout = SetupLayout::of(leader.map_select().setup_variant());
                    ASSERT_TRUE(r0.x == layout.players_box.x + 4 && r0.w == SetupLayout::kBoxInnerPlayersW && r0.y >= layout.players_box.y + 4);
                    ASSERT_TRUE(leader.map_select().player_row_rect(3).bottom() <= layout.players_box.y + 4 + SetupLayout::kBoxInnerPlayersH);
                } else {
                    ASSERT_TRUE(r0.x == 371 && r0.w == 200 && r0.y == 83 && leader.map_select().player_row_rect(3).bottom() == 283);
                }
                const auto at = [&](size_t row) { return row_centre(leader, row); };
                ASSERT_EQ(leader.map_select().player_row_at(at(0).first, at(0).second), 0);
                ASSERT_EQ(leader.map_select().player_row_at(at(1).first, at(1).second), 1);
                ASSERT_EQ(leader.map_select().player_row_at(at(2).first, at(2).second), -1);               // (nobody is there)
                ASSERT_EQ(leader.map_select().player_row_at(at(3).first, at(3).second), -1);
                ASSERT_EQ(leader.map_select().player_row_at(r0.x, r0.y), 0);                               // (the corners that count)
                ASSERT_EQ(leader.map_select().player_row_at(r1.right() - 1, r1.bottom() - 1), 1);
                ASSERT_EQ(leader.map_select().player_row_at(r0.x - 1, at(0).second), -1);
                ASSERT_EQ(leader.map_select().player_row_at(r0.right(), at(0).second), -1);
                ASSERT_EQ(leader.map_select().player_row_at(at(0).first, r0.y - 1), -1);
                // the pointer lights the row it is over: nobody's row and the space around are not lit
                leader.map_select().handle_mouse_motion(at(1).first, at(1).second);
                ASSERT_EQ(leader.map_select().hovered_row(), 1);
                leader.map_select().handle_mouse_motion(at(2).first, at(2).second);
                ASSERT_EQ(leader.map_select().hovered_row(), -1);
                leader.map_select().handle_mouse_motion(at(0).first, at(0).second);
                ASSERT_EQ(leader.map_select().hovered_row(), 0);
                leader.map_select().handle_mouse_motion(r0.x - 5, r0.y - 5);
                ASSERT_EQ(leader.map_select().hovered_row(), -1);
                // the guest's screen has the same rows and none of them can be pressed
                const std::pair<int32_t, int32_t> g = row_centre(guest, 1);
                ASSERT_EQ(guest.map_select().player_row_at(g.first, g.second), -1);
            }

            // presses that move nobody: the right button, a release on another row, a row of nobody, a guest's rows (it asks nothing: a guest's request would be counted as ignored)
            {
                const std::pair<int32_t, int32_t> gus_row = row_centre(leader, 1);
                leader.map_select().handle_mouse_motion(gus_row.first, gus_row.second);
                leader.map_select().handle_mouse_down(gus_row.first, gus_row.second, 3);
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
                leader.map_select().handle_mouse_up(gus_row.first, gus_row.second, 3);
                leader.map_select().handle_mouse_down(gus_row.first, gus_row.second, 1);
                ASSERT_EQ(leader.map_select().pressed_row(), 1);                                            // (the press holds the row lit)
                const std::pair<int32_t, int32_t> ann_row = row_centre(leader, 0);
                leader.map_select().handle_mouse_motion(ann_row.first, ann_row.second);
                leader.map_select().handle_mouse_up(ann_row.first, ann_row.second, 1);                      // (let go on Ann's row)
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
                press_row(leader, 2);
                press_row(leader, 3);
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
                press_row(guest, 0);
                press_row(guest, 1);
                ASSERT_TRUE(guest.map_select().pressed_row() == -1 && guest.map_select().hovered_row() == -1);
                ASSERT_FALSE(guest.net()->request_move_seat(2));                                             // (a guest sends nothing, whoever it asks for)
                hall.step(400);
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.names[0] == "Ann" && s.names[1] == "Gus" && s.seat_moves == 0 && s.ignored_seat_moves == 0);
                ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::Hard, L::None));
            }

            // a press on Gus's row: the click of the buttons, and with the release Gus is put in the next free color, Blue
            const std::vector<std::string> guest_notices = notices_of(guest);
            {
                const size_t channels = leader.audio_mixer().active_channel_count();
                const std::pair<int32_t, int32_t> at = row_centre(leader, 1);
                leader.map_select().handle_mouse_motion(at.first, at.second);
                leader.map_select().handle_mouse_down(at.first, at.second, 1);
                ASSERT_EQ(leader.audio_mixer().active_channel_count(), channels + 1);
                ASSERT_EQ(leader.map_select().pressed_row(), 1);
                leader.map_select().handle_mouse_up(at.first, at.second, 1);
                ASSERT_EQ(leader.map_select().pressed_row(), -1);
            }
            ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[2].state == net::SlotState::Client && guest.net()->my_seat() == 2; }, 8000));
            hall.step(30);
            ASSERT_TRUE(leader.net()->room().slots[2].name == "Gus" && leader.net()->room().slots[1].state == net::SlotState::Empty && leader.net()->room().slots[0].name == "Ann");
            ASSERT_TRUE(leader.net()->my_seat() == 0 && leader.net()->is_leader() && guest.net()->room().leader == 0);
            ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 2, -1, -1}));         // (Gus's row is the second row still)
            ASSERT_TRUE(MapSelectScreen::row_seats(guest.map_select().room()) == Rows({2, 0, -1, -1}));
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None) && leader.net()->fill_bots() == leader.fill_bots());      // the bot that was for Blue is for Red now: START seats the same bots as before
            ASSERT_TRUE(leader.net()->prompt_texts().front().find("Red gets a Hard bot") != std::string::npos);
            ASSERT_TRUE(leader.map_select().room().status != hint);                                                // (the hint came once)
            {
                const std::vector<std::string> told = notices_of(guest);
                ASSERT_TRUE(told.size() == guest_notices.size() + 1 && told.back() == "Ann moved you to Blue.");   // Gus is told by the room
            }
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.names[0] == "Ann" && s.names[1].empty() && s.names[2] == "Gus" && s.seat_moves == 1 && s.ignored_seat_moves == 0 && s.leader == 0);
            }

            // two quick presses (a double click): the second is held back, and Gus goes on by one color only (to Black)
            hall.step(500);                                                                                        // (the last request was half a second ago: its double click is over)
            press_row(leader, 1);
            press_row(leader, 1);
            ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[3].state == net::SlotState::Client && guest.net()->my_seat() == 3; }, 8000));
            hall.step(500);
            ASSERT_TRUE(leader.net()->room().slots[2].state == net::SlotState::Empty && leader.net()->room().slots[3].name == "Gus");
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.seat_moves == 2 && s.ignored_seat_moves == 0);                                       // (a second request would have found Blue empty: ignored, and counted)
            }
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None));                           // (two colors that no bot is for: the plan stays)

            // Gus at Black goes on to the next free color round the table: Green is Ann's, so Red; the bot of Red goes to the color that Gus leaves
            press_row(leader, 1);
            ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[1].state == net::SlotState::Client && guest.net()->my_seat() == 1; }, 8000));
            hall.step(30);
            ASSERT_TRUE(leader.net()->room().slots[1].name == "Gus" && leader.net()->room().slots[3].state == net::SlotState::Empty);
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::None, L::Hard));
            ASSERT_TRUE(leader.net()->prompt_texts().front().find("Black gets a Hard bot") != std::string::npos);
            ASSERT_TRUE(server.status(code).seat_moves == 3 && server.status(code).ignored_seat_moves == 0);

            // the leader's own row moves the leader (to Blue: Red is Gus's); it is the leader still and the room does not tell it of its own move (the last request was half a second ago: a double click is over)
            const size_t leader_notices = notices_of(leader).size();
            hall.step(500);
            press_row(leader, 0);
            ASSERT_TRUE(hall.until([&]() { return leader.net()->my_seat() == 2 && guest.net()->room().leader == 2; }, 8000));
            hall.step(30);
            ASSERT_TRUE(leader.net()->is_leader() && leader.map_select().leads_server_room() && leader.map_select().room().my_seat == 2);
            ASSERT_TRUE(leader.net()->room().slots[0].state == net::SlotState::Empty && leader.net()->room().slots[2].name == "Ann" && leader.net()->room().slots[1].name == "Gus");
            ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({2, 1, -1, -1}));          // (the leader's own seat first)
            ASSERT_EQ(notices_of(leader).size(), leader_notices);
            ASSERT_TRUE(server.status(code).seat_moves == 4 && server.status(code).leader == 2);
            ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::None, L::Hard));

            // START: the match is made with the colors as they are now, Gus plays Red, Ann Blue, and the Hard bot Black
            {
                const server::RoomStatus before = server.status(code);
                ASSERT_TRUE(before.state == server::RoomState::Waiting && before.joined == 2 && before.ignored_start_requests == 0);
            }
            leader.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 1 && s.expected == 4);
                ASSERT_TRUE(s.bots[0].seat == 3 && s.bots[0].level == "hard" && s.bots[0].name == bn(code, 3, "Hard") && s.bots[0].fill);
                ASSERT_TRUE(s.names[0].empty() && s.names[1] == "Gus" && s.names[2] == "Ann" && s.names[3] == bn(code, 3, "Hard"));
            }
            ASSERT_TRUE(leader.local_player_id() == 2 && guest.local_player_id() == 1);
            ASSERT_TRUE(leader.sim().roster_mask() == 0x0E && guest.sim().roster_mask() == 0x0E);
            // the screen is locked now, and a press on a row asks nothing more of the room
            ASSERT_FALSE(leader.map_select().can_move_players());
            press_row(leader, 1);
            hall.step(kDialogMs + 1500);
            ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
            ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced());
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.state == server::RoomState::Running && s.seat_moves == 4 && s.ignored_seat_moves == 0 && s.ticks > 20);
            }
            leader.quit();
            guest.quit();
        }
    } TEST_END();

    TEST_CASE("N5.92 Leader, The Colors (Protocol 14): A Double Click Is One Press (Nothing Goes Out Within Half A Second Of The Last Request, Whatever The Room Has Said); The Press After It Waits For The Room's Answer, Or A Second (A Request That The Room Cannot Do Is Not Answered At All); The Plan Of Bots Follows What The Room Shows And Not What Was Asked, However Often It Was Asked; A Guest Sends Nothing") {
        Server server;
        ASSERT_TRUE(server.make_room("MOVE-LATCH", 4));
        ApplicationConfig cfg = join_config(server, "MOVE-LATCH", "Ann");
        cfg.fill_bots = plan(L::None, L::None, L::Hard, L::None);                           // a Hard bot for Blue
        Application leader;
        ASSERT_TRUE(leader.init(cfg));
        Hall hall{server, &leader, {}};
        ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
        Application guest;
        ASSERT_TRUE(guest.init(join_config(server, "MOVE-LATCH", "Gus")));
        hall.second = &guest;
        ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        hall.step(100);
        ASSERT_EQ(guest.net()->my_seat(), 1);
        ASSERT_TRUE(guest.net()->room().leader == 0 && !guest.net()->is_leader());
        ASSERT_FALSE(guest.net()->request_move_seat(0));                                   // a guest sends nothing, whoever it asks for
        ASSERT_FALSE(guest.net()->request_move_seat(1));
        ASSERT_FALSE(leader.net()->request_move_seat(2));                                  // nobody sits in Blue
        ASSERT_FALSE(leader.net()->request_move_seat(255));
        hall.step(100);
        ASSERT_TRUE(server.status("MOVE-LATCH").seat_moves == 0 && server.status("MOVE-LATCH").ignored_seat_moves == 0);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::Hard, L::None));

        // the first press is sent (Gus to Blue, the next free color); a second one, at once or less than half a second later, is a double click: held back, with or without an answer
        ASSERT_TRUE(leader.net()->request_move_seat(1));
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        ASSERT_FALSE(leader.net()->request_start());                                       // (START waits for the answer too: N5.96)
        // only the leader's clock runs now, so no answer comes (a request that the room cannot do is not answered either): at 500 ms the double click is over, and the press waits for the answer or for a second
        for (int i = 0; i < 4; ++i) leader.pump_network(0.1f);                              // 400 ms
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        leader.pump_network(0.1f);                                                          // 500 ms
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        for (int i = 0; i < 4; ++i) leader.pump_network(0.1f);                              // 900 ms
        ASSERT_FALSE(leader.net()->request_move_seat(1));
        for (int i = 0; i < 2; ++i) leader.pump_network(0.1f);                              // 1100 ms
        ASSERT_TRUE(leader.net()->request_move_seat(1));                                    // (the same request again: the room still seats its people as it did)
        ASSERT_FALSE(leader.net()->request_move_seat(1));                                   // ... and that one is a double click again
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::None, L::Hard, L::None));        // (nothing was shown yet: the plan is where it was, though two requests went out)

        // the room runs: it moves Gus once and finds Red empty for the second request, which it ignores and counts, no offence. The leader's clock stands still and it reads the news with no time passing
        hall.app = nullptr;
        ASSERT_TRUE(hall.until([&]() { const server::RoomStatus s = server.status("MOVE-LATCH"); return s.seat_moves == 1 && s.ignored_seat_moves == 1; }, 8000));
        ASSERT_TRUE(hall.until([&]() { return guest.net()->my_seat() == 2; }, 8000));
        ASSERT_TRUE(leader.net()->room().slots[1].state == net::SlotState::Client);          // (the leader has not read it yet)
        const auto read_leader = [&](const std::function<bool()>& cond) {
            for (int i = 0; i < 2000 && !cond(); ++i) {
                leader.pump_network(0.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return cond();
        };
        ASSERT_TRUE(read_leader([&]() { return leader.net()->room().slots[2].state == net::SlotState::Client; }));
        ASSERT_TRUE(leader.net()->room().slots[2].name == "Gus" && leader.net()->room().slots[1].state == net::SlotState::Empty);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None) && leader.net()->fill_bots() == leader.fill_bots());      // the plan followed the room, once: the bot that was for Blue is for Red
        {
            const server::RoomStatus s = server.status("MOVE-LATCH");
            ASSERT_TRUE(s.seat_moves == 1 && s.ignored_seat_moves == 1 && s.names[2] == "Gus" && s.leader == 0);
            ASSERT_TRUE(leader.net()->phase() == net::NetGame::Phase::Room && guest.net()->phase() == net::NetGame::Phase::Room);
        }

        // the room has answered, but no time has passed since the last request: a double click, still. Half a second after it, with the room's answer shown, the press goes out at once (Gus goes on to Black)
        ASSERT_FALSE(leader.net()->request_move_seat(2));
        leader.pump_network(0.4f);
        ASSERT_FALSE(leader.net()->request_move_seat(2));
        leader.pump_network(0.1f);
        ASSERT_TRUE(leader.net()->request_move_seat(2));
        hall.app = &leader;
        ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[3].name == "Gus" && guest.net()->my_seat() == 3; }, 8000));
        ASSERT_TRUE(leader.net()->room().slots[2].state == net::SlotState::Empty);
        ASSERT_TRUE(server.status("MOVE-LATCH").seat_moves == 2 && server.status("MOVE-LATCH").ignored_seat_moves == 1);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None));         // (two colors that no bot is for: the plan stays)

        // a request that the room ignores changes nothing in the plan: Cat takes Red while the leader has not read the news, so its press for Gus (Black goes to Red) finds Red held
        leader.pump_network(0.6f);                                                           // (more than half a second since the last request)
        Peer cat;
        hall.peers.push_back(&cat);
        hall.app = nullptr;
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 1, "MOVE-LATCH"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && cat.net.my_seat() == 1; }, 8000));
        ASSERT_TRUE(leader.net()->room().slots[1].state == net::SlotState::Empty);           // (the leader has not read it)
        ASSERT_TRUE(leader.net()->request_move_seat(3));
        ASSERT_TRUE(hall.until([&]() { return server.status("MOVE-LATCH").ignored_seat_moves == 2; }, 8000));
        ASSERT_TRUE(read_leader([&]() { return leader.net()->room().slots[1].name == "Cat"; }));
        ASSERT_TRUE(leader.net()->room().slots[3].name == "Gus" && server.status("MOVE-LATCH").seat_moves == 2);
        ASSERT_TRUE(leader.fill_bots() == plan(L::None, L::Hard, L::None, L::None));         // Cat sat down and nobody moved: the plan stays
        hall.app = &leader;
        leader.quit();
        guest.quit();
    } TEST_END();

    TEST_CASE("N5.93 Leader, The Colors (Protocol 14): A Press Holds The Seat Of The Row's Player, Not The Row's Place: When The Rows Close Up Before The Release (A Player Left) The Release Moves Nobody, And The Same Press On The Row That The Player Has Moved To Does") {
        Server server;
        ASSERT_TRUE(server.make_room("MOVE-HOLD", 4));
        Application leader;
        ASSERT_TRUE(leader.init(join_config(server, "MOVE-HOLD", "Ann")));
        Peer gus;
        Peer cat;
        Hall hall{server, &leader, {&gus, &cat}};
        ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
        ASSERT_TRUE(gus.net.join("127.0.0.1", server.port(), "Gus", 255, "MOVE-HOLD"));
        ASSERT_TRUE(hall.until([&]() { return gus.net.phase() == net::NetGame::Phase::Room && gus.net.my_seat() == 1; }, 8000));
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "MOVE-HOLD"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && cat.net.my_seat() == 2 && leader.net()->room().slots[2].state == net::SlotState::Client; }, 8000));
        settle(leader);
        hall.step(50);
        ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 1, 2, -1}));
        // the press is on Gus's row (the second); Gus leaves, the rows close up and Cat's row is the second; the release is where the press was
        const std::pair<int32_t, int32_t> at = row_centre(leader, 1);
        leader.map_select().handle_mouse_motion(at.first, at.second);
        leader.map_select().handle_mouse_down(at.first, at.second, 1);
        ASSERT_EQ(leader.map_select().pressed_row(), 1);
        gus.net.leave();
        ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[1].state == net::SlotState::Empty; }, 8000));
        hall.step(50);
        ASSERT_TRUE(MapSelectScreen::row_seats(leader.map_select().room()) == Rows({0, 2, -1, -1}));
        leader.map_select().handle_mouse_up(at.first, at.second, 1);
        ASSERT_EQ(leader.map_select().pressed_row(), -1);
        hall.step(500);
        ASSERT_TRUE(server.status("MOVE-HOLD").seat_moves == 0 && server.status("MOVE-HOLD").ignored_seat_moves == 0);
        ASSERT_TRUE(leader.net()->room().slots[2].name == "Cat" && leader.net()->room().slots[3].state == net::SlotState::Empty);        // Cat sits where she sat
        // the whole press on Cat's row moves Cat (to Black: the next free color after Blue)
        press_row(leader, 1);
        ASSERT_TRUE(hall.until([&]() { return leader.net()->room().slots[3].name == "Cat"; }, 8000));
        ASSERT_TRUE(server.status("MOVE-HOLD").seat_moves == 1 && server.status("MOVE-HOLD").ignored_seat_moves == 0);
        leader.quit();
    } TEST_END();

    TEST_CASE("N5.94 Leader, The Colors (Protocol 14): The Hint Is For The Waiting Room (A START That Is Pressed While It Shows Leaves The Loading Screen With Its Own Words); A Net Game That Leaves A Room And Joins Another One Starts Afresh: The Hint Again, And No Wait For The Last Press Of The Room That Is Gone") {
        const std::string hint = "Tap a player to change their color.";
        Server server;
        ASSERT_TRUE(server.make_room("MOVE-HINT-A", 4));
        ASSERT_TRUE(server.make_room("MOVE-HINT-B", 4));
        Peer lea;
        Peer bob;
        Peer dan;
        dan.hold_report = true;                                                              // (Dan's machine is slow to report the map: the second room stays in its loading phase)
        Hall hall{server, nullptr, {&lea, &bob, &dan}};
        ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, "MOVE-HINT-A"));
        ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
        ASSERT_TRUE(lea.net.status_text() != hint);                                          // (alone: nobody to move)
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "MOVE-HINT-A"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 8000));
        ASSERT_EQ(lea.net.status_text(), hint);                                              // the leader's first company
        hall.step(5200);                                                                     // (the hint is shown for five seconds)
        ASSERT_TRUE(lea.net.status_text() != hint);
        ASSERT_TRUE(lea.net.request_move_seat(1));                                           // Bob goes to Blue: the press is the last of this room
        ASSERT_TRUE(hall.until([&]() { return lea.net.room().slots[2].state == net::SlotState::Client; }, 0));      // (from here on the game clock stands still: no time has passed since the press when the next one is made, whatever the machine's speed)
        lea.net.leave();
        // another room, a fresh session: the second person brings the hint again, and the first press is not held for the press that was made in the room that is gone
        ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, "MOVE-HINT-B"));
        ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 0));
        ASSERT_TRUE(dan.net.join("127.0.0.1", server.port(), "Dan", 255, "MOVE-HINT-B"));
        ASSERT_TRUE(hall.until([&]() { return dan.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 0));
        ASSERT_EQ(lea.net.status_text(), hint);
        ASSERT_TRUE(lea.net.request_move_seat(1));
        ASSERT_TRUE(hall.until([&]() { return lea.net.room().slots[2].state == net::SlotState::Client; }, 8000));
        // START while the hint shows: the room loads, and the status line says so (the screen is locked: nothing is left to tap)
        ASSERT_EQ(lea.net.status_text(), hint);
        ASSERT_TRUE(lea.net.request_start());
        ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Loading && dan.report_pending; }, 10000));
        ASSERT_TRUE(lea.net.status_text() != hint);
        ASSERT_TRUE(lea.net.status_text() == std::string(sim::strings::text(sim::strings::kLoadingGame)) || lea.net.status_text() == std::string(sim::strings::text(sim::strings::kWaitingForOthers)));
    } TEST_END();

    TEST_CASE("N5.96 Leader, The Colors (Protocol 14): A START Pressed While A Color Change Has Not Been Answered Waits (The Plan Of Bots Follows The Room's Answer, So A START Before It Would Carry The Colors As They Were, With The Player Already Moved): Nothing Is Sent, And Once The Room Has Answered The START Goes With The Plan As The Answer Made It; A Request That The Room Never Answers Holds START For A Second Only") {
        {
            Server server;
            const std::string code = "MOVE-START-A";
            ASSERT_TRUE(server.make_room(code, 4));
            Peer lea;
            Peer gus;
            Hall hall{server, nullptr, {&lea, &gus}};
            ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, code));
            ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
            lea.net.set_fill_bots(plan(L::None, L::None, L::Hard, L::None));               // a Hard bot for Blue
            ASSERT_TRUE(gus.net.join("127.0.0.1", server.port(), "Gus", 255, code));
            ASSERT_TRUE(hall.until([&]() { return gus.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 8000));
            ASSERT_TRUE(lea.net.request_move_seat(1));                                       // Gus to Blue: the Hard bot is for Red then
            ASSERT_FALSE(lea.net.request_start());                                           // (the START of the reviewer's scenario: pressed before the answer, it would seat nobody in Blue and Red)
            ASSERT_TRUE(lea.net.fill_bots() == plan(L::None, L::None, L::Hard, L::None));
            ASSERT_TRUE(hall.until([&]() { return lea.net.room().slots[2].state == net::SlotState::Client; }, 8000));
            ASSERT_TRUE(lea.net.fill_bots() == plan(L::None, L::Hard, L::None, L::None));    // the plan followed the answer
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.state == server::RoomState::Waiting && s.ignored_start_requests == 0 && s.seat_moves == 1 && s.names[2] == "Gus");      // (nothing was sent: no START, not even an ignored one)
            }
            ASSERT_TRUE(lea.net.request_start());                                            // the answer is in: START goes, with the plan as it is
            ASSERT_TRUE(hall.until([&]() { const server::RoomStatus s = server.status(code); return s.state != server::RoomState::Waiting && s.bots.size() == 1; }, 10000));
            {
                const server::RoomStatus s = server.status(code);
                ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].level == "hard" && s.bots[0].name == bn(code, 1, "Hard") && s.bots[0].fill);                 // the Hard bot plays Red, Gus Blue
                ASSERT_TRUE(s.names[0] == "Lea" && s.names[1] == bn(code, 1, "Hard") && s.names[2] == "Gus" && s.names[3].empty() && s.ignored_start_requests == 0);
            }
        }
        {   // a request that the room does not answer (only this machine's clock runs): START is held for a second, not for ever
            Server server;
            const std::string code = "MOVE-START-B";
            ASSERT_TRUE(server.make_room(code, 4));
            Peer lea;
            Peer gus;
            Hall hall{server, nullptr, {&lea, &gus}};
            ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, code));
            ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
            ASSERT_TRUE(gus.net.join("127.0.0.1", server.port(), "Gus", 255, code));
            ASSERT_TRUE(hall.until([&]() { return gus.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 8000));
            hall.step(100);
            ASSERT_TRUE(lea.net.request_move_seat(1));
            const auto lea_waits = [&](uint32_t ms) { for (uint32_t t = 0; t < ms; t += 100) { lea.now += 100; lea.update(); } };
            lea_waits(900);
            ASSERT_FALSE(lea.net.request_start());                                           // 900 ms and no answer yet
            lea_waits(100);
            ASSERT_TRUE(lea.net.request_start());                                            // a second: the wait is over, START goes
        }
    } TEST_END();

    TEST_CASE("N5.97 Leader, The Colors (Protocol 14): The Hint Gives Way To A Line That Is Showing On The Status Line (A Player Who Comes And Speaks In One Reading); START Takes Away The Hint And Nothing Else (A Notice Shown At START Stays, As It Did Before The Hint)") {
        const std::string hint = "Tap a player to change their color.";
        {
            Server server;
            const std::string code = "MOVE-NOTE-A";
            ASSERT_TRUE(server.make_room(code, 4));
            Peer lea;
            Peer bob;
            Hall hall{server, nullptr, {&lea, &bob}};
            ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, code));
            ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
            hall.peers = {&bob};                                                             // Lea's machine does not look while Bob comes and speaks
            ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, code));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(bob.net.chat("hi"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.pregame_chat().size() == 1; }, 8000));      // (the room has said it to everybody)
            hall.step(30);
            for (int i = 0; i < 2000 && lea.net.room().slots[1].state != net::SlotState::Client; ++i) {
                lea.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            for (int i = 0; i < 20; ++i) {                                                   // (the line may come a moment after the room: a reading each)
                lea.update();
                if (lea.net.status_text() == "Bob: hi") break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(lea.net.room().slots[1].name == "Bob");
            ASSERT_EQ(lea.net.status_text(), std::string("Bob: hi"));                       // the line that Bob said is on the status line, not the hint
        }
        {
            Server server;
            const std::string code = "MOVE-NOTE-B";
            ASSERT_TRUE(server.make_room(code, 4));
            Peer lea;
            Peer dan;
            dan.hold_report = true;                                                          // (Dan's machine is slow to report the map: the room stays in its loading phase)
            Hall hall{server, nullptr, {&lea, &dan}};
            ASSERT_TRUE(lea.net.join("127.0.0.1", server.port(), "Lea", 255, code));
            ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Room && lea.net.is_leader(); }, 8000));
            ASSERT_TRUE(dan.net.join("127.0.0.1", server.port(), "Dan", 255, code));
            ASSERT_TRUE(hall.until([&]() { return dan.net.phase() == net::NetGame::Phase::Room && lea.net.room().slots[1].state == net::SlotState::Client; }, 8000));
            ASSERT_EQ(lea.net.status_text(), hint);
            ASSERT_TRUE(dan.net.chat("ready?"));
            ASSERT_TRUE(hall.until([&]() { return lea.net.status_text() == "Dan: ready?"; }, 8000));      // the line takes the place of the hint
            ASSERT_TRUE(lea.net.request_start());
            ASSERT_TRUE(hall.until([&]() { return lea.net.phase() == net::NetGame::Phase::Loading && dan.report_pending; }, 10000));
            ASSERT_EQ(lea.net.status_text(), std::string("Dan: ready?"));                   // START took away the hint only: a line that is showing stays its five seconds
        }
    } TEST_END();

    TEST_CASE("N5.75 The \"Get Ready\" Dialog In A Server's Room (Protocol 12): A Leader Who Starts A Room With Bots Sees The Dialog For The 5 s Before The Referee's First Turn, Up Exactly While No Tick Has Run And Gone The Step The First One Ran; The Referee Seals Nothing And Runs Nothing Meanwhile, The Screen Says Nothing, The Bots Look And Send Nothing; A Click Selects Nothing Until It Is Gone") {
        Server server;
        ASSERT_TRUE(server.make_room("DIALOG-APP", 4));
        ApplicationConfig cfg = join_config(server, "DIALOG-APP", "Solo");
        cfg.fill_bots = net::FillLevel::Hard;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{server, &app, {}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        click_start(app);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 15000));
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0);
        ASSERT_TRUE(server.status("DIALOG-APP").state == server::RoomState::Running && server.status("DIALOG-APP").bots.size() == 3);
        const uint32_t full_time = app.sim().get_match_time_remaining_ms();
        const uint32_t mine = static_cast<uint32_t>(ants_of(app.sim(), 0).front());
        uint32_t waited = 0;
        while (app.sim().current_tick() == 0 && waited < 20000) {
            ASSERT_TRUE(app.hud().is_match_start_modal_active());                              // up exactly while no tick has run
            ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);
            const server::RoomStatus st = server.status("DIALOG-APP");
            if (st.turns == 0) ASSERT_TRUE(st.ticks == 0 && !st.paused);                         // the referee seals nothing and runs nothing before its first turn
            ASSERT_TRUE(app.net_overlay_now().text.empty() && app.net()->stalled_ms() == 0 && !app.net()->lag_notice() && !app.net()->catching_up() && !app.net()->self_lag_behind_ms());
            if (waited == 2000) {
                for (const auto& a : app.sim().get_world_state().ants) {
                    if (a.id != mine) continue;
                    app.renderer().camera().center_on(a.px, a.py, app.sim().grid().width(), app.sim().grid().height());
                    int32_t sx = 0;
                    int32_t sy = 0;
                    ASSERT_TRUE(app.renderer().camera().world_to_screen(a.px, a.py, sx, sy));
                    ASSERT_TRUE(app.hud().handle_mouse_down(sx, sy, 1, app.sim(), app.renderer().camera()));
                    app.hud().handle_mouse_up(sx, sy, 1, app.sim(), app.renderer().camera());
                    ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
                }
            }
            hall.step(10);
            waited += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                  // gone on the step that the first turn ran
        ASSERT_TRUE(waited >= kDialogMs - 100 && waited <= kDialogMs + 400);                    // 5 s (the Begin was read a step or two before the check above) and the link, the second turn, the frames
        ASSERT_TRUE(server.status("DIALOG-APP").bot_start_hold == ai::kStartHoldTicks);         // the room's bots open like the product's: one token, a look on tick 1 + seat
        hall.step(3000);
        ASSERT_TRUE(server.status("DIALOG-APP").ticks > 40 && !app.net()->desynced() && app.net_overlay_now().text.empty());
        app.quit();
    } TEST_END();
}



// ping and delay next to the frame rate (latency_corner.hpp, ants_net/latency.hpp): what a guest and a host measure in the room and in the match, and what the corner does
void run_latency_tests() {
    TEST_CASE("N5.27 Latency: A Guest Measures Its Ping In The Room And Its Ping And The Delay Of Its Own Commands In The Match; The Corner Draws Them; Nothing Waits At The End") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_FALSE(app.net()->ping_ms().has_value());                                   // nothing is measured before the guest has a seat
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        // the room: the guest pings the host once it has a seat (the game's clock is virtual and steps 10 ms: the answer is read one step later)
        ASSERT_TRUE(duo.until([&]() { return app.net()->ping_ms().has_value(); }, 8000));
        ASSERT_TRUE(*app.net()->ping_ms() <= 30);
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());                           // no command yet: "-"
        app.render_frame();                                                                // the room's corner: one row left of the version
        // the match
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(777, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.net()->ping_ms().has_value());                                     // the room's measurement carries over to the first moments of the match
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        uint32_t next = 0;
        for (uint32_t t = 0; t < 12000; t += 10) {
            if (t >= next) {
                next = t + 700;
                const auto mine = ants_of(app.sim(), 1);
                if (!mine.empty()) app.net()->submit(order(1, mine[(t / 700) % mine.size()], static_cast<int16_t>((t / 10) % 31), static_cast<int16_t>((t / 20) % 31)));
            }
            duo.step(10);
        }
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() <= 30);
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        // the delay: the way there (up to one step), the wait for the next 50 ms turn (0 - 50), the way back (up to one step), and one turn of jitter buffer (50 - 60)
        ASSERT_TRUE(*app.net()->command_delay_ms() >= 50 && *app.net()->command_delay_ms() <= 150);
        ASSERT_TRUE(app.net()->stalled_ms() < 1000);                                       // turns are flowing: no "Waiting for the other players..."
        app.render_frame();                                                                // the match's corner: two lines above the row
        // a player who gives no order for more than ten seconds sees "delay -" (the number is not about now any more), and the next order brings it back
        duo.step(9000);
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        duo.step(2500);
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        app.render_frame();                                                                // (the corner draws the dash)
        for (uint32_t t = 0; t < 2000 && !app.net()->command_delay_ms().has_value(); t += 10) {
            if (t % 300 == 0) {
                const auto mine = ants_of(app.sim(), 1);
                if (!mine.empty()) app.net()->submit(order(1, mine[(t / 300) % mine.size()], static_cast<int16_t>(5 + t % 20), 7));
            }
            duo.step(10);
        }
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        // the results screen of a match shows the readout too (one row: nothing stands in the corner row there)
        app.return_to_map_select();
        ASSERT_FALSE(app.network_active());
        ASSERT_FALSE(app.net() != nullptr && app.net()->ping_ms().has_value());
    } TEST_END();

    TEST_CASE("N5.28 Latency: A Host's Ping Is 0 (it has no link to itself), The Delay Of Its Own Commands Is The Wait For The Seal Plus One Turn Of Buffer") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() == 0);        // in the room, alone
        ASSERT_FALSE(app.net()->command_delay_ms().has_value());
        int32_t tiny = -1;
        for (size_t i = 0; i < app.map_select().get_maps().size(); ++i) {
            if (app.map_select().get_maps()[i].filename == "TINY.LVL") tiny = static_cast<int32_t>(i);
        }
        ASSERT_TRUE(tiny >= 0);
        app.map_select().set_selected_index(tiny);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() == 0);        // in the match
        uint32_t next = 0;
        for (uint32_t t = 0; t < 12000; t += 10) {
            if (t >= next) {
                next = t + 700;
                const auto mine = ants_of(app.sim(), 0);
                if (!mine.empty()) app.net()->submit(order(0, mine[(t / 700) % mine.size()], static_cast<int16_t>((t / 10) % 31), static_cast<int16_t>((t / 20) % 31)));
            }
            duo.step(10);
        }
        ASSERT_TRUE(app.net()->command_delay_ms().has_value());
        ASSERT_TRUE(*app.net()->command_delay_ms() >= 50 && *app.net()->command_delay_ms() <= 120);        // 0 - 50 for the seal, 50 for the buffer, one step of rounding
        app.render_frame();                                                                  // the host's corner: "ping 0 ms", its own delay
    } TEST_END();

    TEST_CASE("N5.30 The Network Keeps Real Time: A Frame Of 400 ms Lets The Runner Pay Back Eight Turns At Once (the local simulation's clamp of 100 ms would run two and leave the rest in the queue)") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4242, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        duo.step(kDialogMs + 1000);                                                       // the first turn is sealed kDialogMs after the match began; then the match runs: one turn of buffer
        ASSERT_TRUE(app.net()->stalled_ms() < 1000);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                            // (the dialog ended with the first turn)
        // the window is busy for 400 ms (a hitch): the host goes on sealing, its turns wait in the socket
        for (int i = 0; i < 40; ++i) {
            host.now += 10;
            host.update();
            ants_test::short_pause();
        }
        const uint32_t before = app.net()->turns_executed();
        app.run_frame_with_delta(0.4f);                                                   // the frame that follows the hitch: 400 ms of real time
        const uint32_t paid_back = app.net()->turns_executed() - before;
        ASSERT_TRUE(paid_back >= 6);                                                      // 400 ms of ticks at once: eight turns of 50 ms (a clamp to 100 ms would run two)
        duo.step(1000);
        ASSERT_TRUE(app.net()->stalled_ms() < 1000);
    } TEST_END();

    TEST_CASE("N5.31 A Ping Reading Older Than Three Seconds Is Not Shown: The Host Stops Answering For 3.5 s (The Corner Says \"ping -\"), Then Answers Again (A Number Again)") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(4343, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        duo.step(3000);
        ASSERT_TRUE(app.net()->ping_ms().has_value() && *app.net()->ping_ms() <= 30);
        // the host's machine stops (its process hangs: it reads nothing, answers nothing); the guest's clock goes on
        const auto guest_only = [&](uint32_t ms) {
            for (uint32_t t = 0; t < ms; t += 10) {
                app.pump_network(0.010f);
                app.update_simulation(0.010f);
                ants_test::short_pause();
            }
        };
        guest_only(1500);
        ASSERT_TRUE(app.net()->ping_ms().has_value());                                    // the last answer is 1.5 s old at the least and 2.5 s at the most (a ping goes out every second): still a reading
        guest_only(2200);                                                                 // 3.7 s after the host stopped: the last answer is older than 3 s whenever it came
        ASSERT_FALSE(app.net()->ping_ms().has_value());                                   // "ping -"
        app.render_frame();                                                               // (the corner draws the dash)
        duo.step(1500);                                                                   // the host is back: the answers that waited come (the pings it did not answer), the new ones too
        ASSERT_TRUE(app.net()->ping_ms().has_value());
    } TEST_END();

    TEST_CASE("N5.29 Latency Corner With The Real Font: The Row Of The Setup And Results Screens And The Two Lines Of The Match Clear The Version, The Frame Rate, The Sparkline And The Score Boxes") {
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Renderer& r = app.renderer();
        const int32_t text_h = r.get_text_height(FontSize::Px12);
        const int32_t fps_w = r.get_text_width("144 FPS", FontSize::Px12);                  // the widest frame rate text that matters (three digits)
        const int32_t text_x = 632 - fps_w;
        const int32_t spark_x = text_x - 36 - 6;
        const int32_t ver_w = r.get_text_width(std::string(ants::VERSION_STRING), FontSize::Px12);
        const int32_t ver_x = spark_x - ver_w - 6;
        const int32_t text_y = FPS_OVERLAY_SPARK_Y + (FPS_OVERLAY_SPARK_H - text_h) / 2;
        const int32_t widest = r.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP + r.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
        struct Box { int32_t x, y, w, h; };
        auto overlap = [](const Box& a, const Box& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; };
        for (const uint32_t ms : {0u, 7u, 42u, 230u, 1234u, 9999u}) {
            const std::string p = ping_text(ms);
            const std::string d = delay_text(ms);
            const int32_t pw = r.get_text_width(p, FontSize::Px12);
            const int32_t dw = r.get_text_width(d, FontSize::Px12);
            const Box version{ver_x, text_y, ver_w, text_h};
            const Box plate{spark_x - 1, FPS_OVERLAY_TOP, 36 + 2, FPS_OVERLAY_BOTTOM - FPS_OVERLAY_TOP};
            const Box fps{text_x, text_y, fps_w, text_h};
            // setup and results screens: one row, left of the version; the prompt box of the setup screen (36 .. 329, picture to 336, rows 441 .. 468) is further left
            for (const int32_t limit : {LATENCY_LEFT_LIMIT_SETUP, LATENCY_LEFT_LIMIT_RESULTS}) {
                const LatencyCornerLayout l = layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, limit);
                ASSERT_FALSE(l.stacked);
                const Box ping{l.ping_x, l.ping_y, pw, text_h};
                const Box delay{l.delay_x, l.delay_y, dw, text_h};
                ASSERT_FALSE(overlap(ping, version) || overlap(delay, version) || overlap(ping, plate) || overlap(delay, plate) || overlap(ping, fps) || overlap(delay, fps) || overlap(ping, delay));
                ASSERT_TRUE(l.ping_x >= limit);
                ASSERT_TRUE(l.ping_y == text_y && l.delay_y == text_y);                      // level with the frame rate, in the corner row
            }
            ASSERT_TRUE(layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, LATENCY_LEFT_LIMIT_SETUP).ping_x > 336);                  // right of the prompt box's picture
            // match: two lines above the row. They clear the version, the sparkline plate, the frame rate and the score boxes (bottom row, to x = 458) and the chat input box
            // (x 479 .. 622, rows 423 .. 436); the only HUD piece they touch is the right edge of the "Send to: All" button (x 532 .. 576, rows 443 .. 467), by at most 7 pixels
            const LatencyCornerLayout m = layout_latency_corner(pw, dw, widest, text_h, ver_x, text_y, LATENCY_LEFT_LIMIT_MATCH);
            ASSERT_TRUE(m.stacked);
            const Box ping{m.ping_x, m.ping_y, pw, text_h};
            const Box delay{m.delay_x, m.delay_y, dw, text_h};
            ASSERT_FALSE(overlap(ping, version) || overlap(delay, version) || overlap(ping, plate) || overlap(delay, plate) || overlap(ping, fps) || overlap(delay, fps) || overlap(ping, delay));
            const Box score_boxes{0, 461, 458, 19};
            const Box chat_input{479, 423, 143, 14};
            ASSERT_FALSE(overlap(ping, score_boxes) || overlap(delay, score_boxes) || overlap(ping, chat_input) || overlap(delay, chat_input));
            // the "Send to: All" button's box is x 532 .. 575, rows 443 .. 466 (its picture ends a pixel or two inside it): the lines end at the frame rate's right edge, so a longer
            // text reaches further into the box: "delay 230 ms" 7 px, "ping 230 ms" 2, a number of one or two digits none, "delay 9999 ms" 13 (and "ping 9999 ms" 9)
            const Box all_button{532, 443, 44, 24};
            auto touches = [&](const Box& line) { return std::max(0, std::min(all_button.x + all_button.w, line.x + line.w) - std::max(all_button.x, line.x)); };
            ASSERT_TRUE(touches(delay) <= 13 && touches(ping) <= 9);
            ASSERT_TRUE(ms >= 1000 || (touches(delay) <= 7 && touches(ping) <= 2));
            ASSERT_TRUE(ms >= 100 || (touches(delay) == 0 && touches(ping) == 0));
            ASSERT_TRUE(m.ping_x >= 0 && m.delay_x >= 0 && m.delay_y + text_h <= 480 && m.ping_y >= 436);
        }
    } TEST_END();

    TEST_CASE("N5.29b Latency Corner In The 16:9 Match: The Application's Readout Stands On Two Lines In The Corner, Right Of The Row Of Score Boxes That Are Spread Over The Wide Bottom Strip (The Match's Limit Is The Layout's, Not The Original's 460), And In The Original's Own Picture Still On Two Lines Right Of 460") {
        for (const Aspect aspect : {Aspect::Wide16x9, Aspect::Classic4x3}) {
            Peer host;
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            ApplicationConfig cfg = headless_config();
            cfg.aspect = aspect;
            cfg.aspect_given = true;
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = host.net.listen_port();
            cfg.player_name = "Bob";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Duo duo{app, host};
            ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
            app.render_frame();
            ASSERT_TRUE(app.last_latency_layout().has_value());                         // the room's corner: one row (a page; no match yet)
            uint64_t hash = 0;
            ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(4242, hash));
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            duo.step(300);
            app.render_frame();
            ASSERT_TRUE(app.last_latency_layout().has_value());
            const LatencyCornerLayout l = *app.last_latency_layout();
            const ScreenLayout& layout = app.layout();
            ASSERT_EQ(layout.width, aspect == Aspect::Wide16x9 ? 960 : 640);
            const int32_t row_right = layout.score_row_right();                         // 780 at 960 x 540 (the third box 722 + 58), 460 in the original's picture
            ASSERT_EQ(row_right, aspect == Aspect::Wide16x9 ? 780 : 460);
            ASSERT_TRUE(l.stacked);                                                     // a match's corner row has room for no row of texts, in either picture
            ASSERT_TRUE(l.ping_x >= row_right && l.delay_x >= row_right);               // right of the last score box and its cover
            ASSERT_EQ(l.delay_y + app.renderer().get_text_height(FontSize::Px12) + 1, layout.height - (480 - FPS_OVERLAY_TOP));     // the lower line is a row above the plate
            ASSERT_TRUE(l.ping_y + app.renderer().get_text_height(FontSize::Px12) <= l.delay_y);
            // a frame on a page of the original's (the results) is a row left of the version, and a frame of a match with no network draws nothing: checked where the screens are made
            app.return_to_map_select();
            app.render_frame();
            ASSERT_FALSE(app.last_latency_layout().has_value());                        // the connection is closed with the match: nothing is drawn
        }
    } TEST_END();
}

namespace {

// ------------------------------------------------------------------------------------------------------------------------------------------
// A hidden page (the web build): a hidden or minimised browser tab runs no frames and the browser slows its timers, but the page's WebSocket events still arrive. A network
// match is then driven by those wake-ups (Application::background_pump) and not by the frame loop. A native test cannot receive a browser's events, so it plays the browser:
// HiddenDuo seals the host's turns and wakes the application, once per turn, with the time that has passed. (docs/NETWORK_PORT.md, "The browser as a client: hidden tabs, and sound")
// ------------------------------------------------------------------------------------------------------------------------------------------

// A two-player match on TINY: the application is the guest (seat 1) of a bare host (Alice, seat 0); both are in the room, START has run, the match is playing and the page is shown.
// A headless application ends its run by itself after ten frames; `endless_frames` keeps it running for a test that runs many frames.
bool start_two(Peer& host, Application& app, uint32_t seed, bool endless_frames = false) {
    if (!host.net.host(0, "Alice", true)) return false;
    host.net.set_map("TINY.LVL");
    ApplicationConfig cfg = headless_config();
    cfg.net_role = ApplicationConfig::NetRole::Join;
    cfg.net_address = "127.0.0.1";
    cfg.net_port = host.net.listen_port();
    cfg.player_name = "Bob";
    if (endless_frames) {
        cfg.screenshot_path = "ants_hidden_page_unused.bmp";                            // (the countdown of a screenshot run is the other way to end a headless run: it never gets to 1)
        cfg.screenshot_frames = 1 << 30;
    }
    if (!app.init(cfg)) return false;
    Duo duo{app, host};
    if (!duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000)) return false;
    host.net.set_map("TINY.LVL");
    duo.step(300);
    uint64_t hash = 0;
    if (!net::hash_file(maps_dir() + "TINY.LVL", hash) || !host.net.start_match(seed, hash)) return false;
    if (!duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000)) return false;
    duo.step(6000);                                                                   // the "get ready" dialog takes every key for its five seconds
    return !app.hud().is_modal_open();
}

// The host seals turns for `ms` of game time and nobody polls them on the application's side (the page is not woken)
void host_runs(Peer& host, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        host.now += 10;
        host.update();
        ants_test::short_pause();
    }
}

// The browser with the page hidden: no frame runs; the host seals a turn every kTurnMs of game time, and each turn that arrives wakes the application with the time since the last one
struct HiddenDuo;
struct VirtualClock;
void advance_clock(VirtualClock* clock, double seconds);

struct HiddenDuo {
    Application& app;
    Peer& peer;
    uint32_t wakes{0};                                  // the wake-ups that made a step
    uint32_t clock_ms{0};
    VirtualClock* clock{nullptr};                       // the application's clock, if the test keeps one: it goes with the game time
    void step(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 10) {
            peer.now += 10;
            peer.update();
            ants_test::short_pause();
            advance_clock(clock, 0.010);
            clock_ms += 10;
            if (clock_ms % net::kTurnMs == 0 && app.background_pump_after(static_cast<float>(net::kTurnMs) / 1000.0f)) ++wakes;
        }
    }
    bool until(const std::function<bool()>& cond, uint32_t max_ms) {
        for (uint32_t t = 0; t < max_ms; t += 10) {
            if (cond()) return true;
            step(10);
        }
        return ants_test::real_time_tail(cond, [&]() { peer.update(); app.background_pump_after(0.0f); });       // (real time for a late kernel, the clocks standing still: see Duo)
    }
};

// The clock of the application's frames and wake-ups (Application::set_clock): time moves when the test moves it. Every rule of the time is then checked exactly and without
// sleeping (a test that sleeps for a window is late on a busy machine). The unit is SDL's performance counter (24 MHz on a Mac, a nanosecond on Linux); it never starts at 0
// (a stamp of 0 means "none yet") and starts a day in, so that a test may set it back by seconds without the unsigned counter wrapping.
struct VirtualClock {
    uint64_t counter{86400ull * SDL_GetPerformanceFrequency()};
    void set(Application& app) { app.set_clock([this]() { return counter; }); }
    static uint64_t units(double seconds) { return static_cast<uint64_t>(seconds * static_cast<double>(SDL_GetPerformanceFrequency()) + 0.5); }
    void advance(double seconds) { counter += units(seconds); }
    void back(double seconds) { counter -= units(seconds); }
};
void advance_clock(VirtualClock* clock, double seconds) {
    if (clock != nullptr) clock->advance(seconds);
}

// The seconds that one turn takes (a frame loop that delivers a frame at least once per turn keeps up with the server by itself)
double turn_seconds() { return static_cast<double>(net::kTurnMs) / 1000.0; }

// The hidden seat has noticed that its host is gone: it elects, it became the host (a two-player match) or the match is over for it
bool host_noticed_gone(Application& app) {
    return app.net()->electing() || app.net()->is_host() || app.net()->phase() != net::NetGame::Phase::Playing;
}

// A hidden page whose frame loop still delivers a frame every `frame_ms` (0: none): the host seals a turn every kTurnMs of play, and each turn wakes the application (a message of
// the server) just as the browser would. `seconds` of play go by on the virtual clock. Returns the ticks that the application executed per second of play.
double hidden_ticks_per_second(Peer& host, Application& app, VirtualClock& vc, uint32_t frame_ms, uint32_t seconds) {
    const uint64_t before = app.sim().current_tick();
    for (uint32_t t = 10; t <= seconds * 1000u; t += 10) {
        vc.advance(0.010);
        host.now += 10;
        host.update();
        ants_test::short_pause();
        if (frame_ms > 0 && (t + 30) % frame_ms == 0) app.run_frame();                   // a frame comes (its phase is not the turns': they are two clocks)
        if (t % net::kTurnMs == 0) app.background_pump();                                // a message of the server wakes the page (one just after a frame: it stands down)
    }
    return static_cast<double>(app.sim().current_tick() - before) / static_cast<double>(seconds);
}

}  // namespace

void run_hidden_page_tests() {
    TEST_CASE("N5.32 Hidden Page: A Network Match Runs On The Wake-Ups Instead Of Frames (Turns Executed And Acknowledged, No Frame, No Picture), The Machines Stay Bit-Identical, The Frame Loop Has The Match Back When The Page Is Shown") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 31337));
        Duo duo{app, host};
        VirtualClock clock;
        clock.set(app);                                                                   // (the time of the frames and the wake-ups is the test's)
        const float fps_start = app.get_current_fps();
        app.run_frame_with_delta(0.040f);                                                 // a frame of a shown page changes the frame statistics: the probe for "a frame ran"
        const float fps_shown = app.get_current_fps();
        ASSERT_TRUE(fps_shown != fps_start);
        std::random_device unique;                                                        // (a name of its own: two test runs on one machine must not meet at one file)
        const std::string shot = (std::filesystem::temp_directory_path() / ("ants_hidden_page_test_" + std::to_string(unique()) + ".bmp")).string();
        std::remove(shot.c_str());
        app.renderer().request_screenshot(shot);                                          // the next frame that is drawn writes this file

        app.set_page_hidden(true);
        ASSERT_TRUE(app.page_hidden());
        ASSERT_TRUE(app.background_driven());
        const uint32_t executed_before = app.net()->turns_executed();
        const uint64_t tick_at_hide = app.sim().current_tick();
        const uint32_t pumps_at_hide = app.background_pumps();
        ASSERT_TRUE(app.background_pump_after(0.0f));                                     // the frame of the shown page that has just run says nothing about the hidden one: the first wake-up steps
        HiddenDuo hidden{app, host};
        hidden.clock = &clock;
        hidden.step(30000);                                                               // 30 s without one frame
        const uint32_t turns = 30000 / net::kTurnMs;
        ASSERT_EQ(hidden.wakes, turns);
        ASSERT_EQ(app.background_pumps() - pumps_at_hide, turns + 1);
        const uint32_t executed = app.net()->turns_executed() - executed_before;
        ASSERT_TRUE(executed + 4 >= turns && executed <= turns + 1);                      // every turn that the host sealed ran here too, give or take the jitter buffer
        ASSERT_TRUE(host.net.turns_executed() - app.net()->turns_executed() <= 4);
        ASSERT_FALSE(host.net.desynced());                                                // the host compared the hashes that this machine sent while it was hidden
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
        ASSERT_EQ(app.get_current_fps(), fps_shown);                                      // no frame ran ...
        ASSERT_FALSE(std::ifstream(shot).good());                                         // ... and nothing was drawn
        const uint32_t pumps = app.background_pumps();
        app.run_frame_with_delta(0.040f);                                                 // a browser that still runs the frames of a page that it calls hidden: the frame is a frame, it draws ...
        ASSERT_TRUE(app.get_current_fps() != fps_shown);
        ASSERT_TRUE(std::ifstream(shot).good());
        std::remove(shot.c_str());
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // ... and the wake-up stands down while the frames come at least once per turn
        ASSERT_EQ(app.background_pumps(), pumps);
        clock.advance(turn_seconds() - 0.001);
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // (a hair under a turn after the frame: still standing down)
        ASSERT_EQ(app.background_pumps(), pumps);
        clock.advance(0.002);
        ASSERT_TRUE(app.background_pump_after(0.1f));                                     // a hair over: the frames are slower than the turns, the wake-up drives the match again
        ASSERT_EQ(app.background_pumps(), pumps + 1);

        app.set_page_hidden(false);                                                       // shown again: the frame loop has the match back
        ASSERT_FALSE(app.page_hidden());
        ASSERT_FALSE(app.background_driven());
        {   // the browser's console gets one line about the period: how long, how far the match went, how many wake-ups stepped it
            const std::string line = app.last_hidden_line();
            const uint64_t ticks = app.sim().current_tick() - tick_at_hide;
            ASSERT_TRUE(ticks + 12 >= 30 * 1000 / net::LockstepRunner::kTickMs);                  // (the match ran at its pace: 20 ticks a second, the buffer behind it)
            ASSERT_TRUE(line.rfind("The page was hidden for 30.", 0) == 0);
            ASSERT_TRUE(line.find("; the match advanced by " + std::to_string(ticks) + " ticks and " + std::to_string(app.background_pumps() - pumps_at_hide) +
                                  " wake-ups stepped it in the background") != std::string::npos);
        }
        const float fps_hidden_frame = app.get_current_fps();
        app.renderer().request_screenshot(shot);
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.get_current_fps() != fps_hidden_frame);
        ASSERT_TRUE(std::ifstream(shot).good());                                          // and draws again
        std::remove(shot.c_str());
        host.net.freeze();                                                                // the host stops sealing; both machines run what they have and stand still
        duo.step(2000);
        ASSERT_EQ(app.net()->turns_executed(), host.net.turns_executed());
        ASSERT_EQ(app.sim().current_tick(), host.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());                     // the same game, tick for tick, hash for hash
    } TEST_END();

    TEST_CASE("N5.33 Hidden Page: One Driver At A Time - The Wake-Up Of A Shown Page Does Nothing, The Wake-Ups Of A Hidden Page Stand Down While Its Frames Come At Least Once Per Turn, No Step Starts Inside Another") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 4242));
        VirtualClock clock;
        clock.set(app);
        host_runs(host, 5 * net::kTurnMs);                                                // shown: five turns arrive and nobody polls them yet
        const uint32_t waiting = app.net()->turns_executed();
        ASSERT_FALSE(app.background_pump_after(0.5f));                                    // a wake-up of a shown page does nothing, whatever it says
        ASSERT_FALSE(app.background_pump());
        ASSERT_EQ(app.net()->turns_executed(), waiting);
        ASSERT_EQ(app.background_pumps(), 0u);

        app.set_page_hidden(true);
        const float fps = app.get_current_fps();
        ASSERT_TRUE(app.background_pump_after(0.100f));                                   // a hidden page: the wake-up drives the match, the turns that waited run, no frame
        ASSERT_TRUE(app.net()->turns_executed() > waiting);
        ASSERT_EQ(app.background_pumps(), 1u);
        ASSERT_EQ(app.get_current_fps(), fps);
        app.run_frame_with_delta(0.050f);                                                 // a frame in a hidden page (the browser still draws it): the frame loop drives the match ...
        ASSERT_TRUE(app.get_current_fps() != fps);
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // ... and the wake-up stands down while frames come (less than a turn after the last)
        ASSERT_FALSE(app.background_pump());
        ASSERT_EQ(app.background_pumps(), 1u);
        clock.advance(0.3);

        uint32_t inside = 0;                                                              // no step starts inside another: from a tick of a step every way in is refused
        uint32_t refused = 0;
        app.net()->set_on_tick([&]() {
            const uint32_t before = app.background_pumps();
            ++inside;
            if (!app.background_pump_after(0.1f)) ++refused;
            if (!app.background_pump()) ++refused;
            app.run_frame_with_delta(0.1f);                                               // (it has no result: it must change nothing)
            if (app.background_pumps() == before) ++refused;
        });
        host_runs(host, 3 * net::kTurnMs);
        const uint32_t pumps = app.background_pumps();
        ASSERT_TRUE(app.background_pump_after(0.2f));
        ASSERT_EQ(app.background_pumps(), pumps + 1);                                     // the outer step counts once, whatever was tried inside it
        ASSERT_TRUE(inside >= 2);                                                         // ticks ran, so the nested attempts happened
        ASSERT_EQ(refused, inside * 3);                                                   // and every one of them was refused

        app.set_page_hidden(false);                                                       // a shown page: a frame never starts inside a frame, and nothing wakes it from inside either
        uint32_t leaked = 0;
        uint32_t ran_inside = 0;
        app.net()->set_on_tick([&]() {
            ++ran_inside;
            const float before = app.get_current_fps();
            app.run_frame_with_delta(0.1f);                                               // refused: it would change the frame statistics a second time
            if (app.get_current_fps() != before) ++leaked;
            if (app.background_pump_after(0.1f)) ++leaked;
        });
        host_runs(host, 3 * net::kTurnMs);
        app.run_frame_with_delta(0.050f);
        ASSERT_TRUE(ran_inside >= 1);
        ASSERT_EQ(leaked, 0u);
    } TEST_END();

    TEST_CASE("N5.34 Hidden Page: The Time Accounting Of The Wake-Ups - A Wake-Up Gives The Network At Most A Second And Never Less Than Nothing (A Page That Was Asleep Is Not Paid Back At Once), Frames And Wake-Ups Share One Timer") {
        {   // a live host and a long pause: the network clock and the runner get one second, whatever the page says
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 777));
            app.set_page_hidden(true);
            host_runs(host, 30 * net::kTurnMs);                                           // thirty turns wait
            bool every_wake_stepped = true;
            const auto given = [&](float elapsed) {                                       // what one wake-up of `elapsed` seconds gives the network clock (ms)
                const double before = app.net_clock_ms();
                if (!app.background_pump_after(elapsed)) every_wake_stepped = false;
                return app.net_clock_ms() - before;
            };
            const uint32_t turns_before = app.net()->turns_executed();
            const double ten_minutes = given(600.0f);
            ASSERT_TRUE(std::abs(ten_minutes - 1000.0) < 0.001);                          // ten minutes of "time" give exactly one second ...
            const uint32_t ran = app.net()->turns_executed() - turns_before;
            ASSERT_TRUE(ran >= 3 && ran <= 4 * 1000 / net::kTurnMs);                      // ... which the runner pays out under its own limits: at most the ticks that that second holds at its fastest speed (it runs a backlog down at up to 4x, also in a long step)
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);                  // (the host is alive: what waited in the link was read first, it is no silence)
            ASSERT_FALSE(app.net()->electing());
            ASSERT_FALSE(app.net()->is_host());
            ASSERT_TRUE(std::abs(given(5.0f) - 1000.0) < 0.001);                          // five seconds: one (a cap of eight seconds would let all five through)
            ASSERT_TRUE(std::abs(given(1.0f) - 1000.0) < 0.001);                          // the cap itself
            ASSERT_TRUE(std::abs(given(0.4f) - 400.0) < 0.01);                            // less than the cap counts in full (a cap of 0.3 would cut it)
            ASSERT_TRUE(std::abs(given(0.05f) - 50.0) < 0.01);
            ASSERT_TRUE(std::abs(given(0.0f)) < 0.001);
            ASSERT_TRUE(std::abs(given(-5.0f)) < 0.001);                                  // a clock that went backwards counts for nothing: it must not wrap the network's clock
            ASSERT_TRUE(every_wake_stepped);
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
            ASSERT_FALSE(app.net()->electing());
            ASSERT_FALSE(app.net()->is_host());                                           // (a clock that wrapped made the session think the host had been silent for ever)
            ASSERT_FALSE(app.net()->desynced());
        }
        {   // frames and wake-ups share one timer: the time between two of them is counted once
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 779));
            VirtualClock clock;
            clock.set(app);
            app.run_frame();                                                              // (the frame loop's timer starts)
            app.set_page_hidden(true);
            clock.advance(0.250);
            const double before = app.net_clock_ms();
            ASSERT_TRUE(app.background_pump());                                           // the wake-up counts the quarter second ...
            ASSERT_TRUE(std::abs(app.net_clock_ms() - before - 250.0) < 0.01);
            clock.advance(0.040);
            app.set_page_hidden(false);
            const double frame_before = app.net_clock_ms();                               // (the page is shown: the last wake-up of set_page_hidden counted the 40 ms)
            ASSERT_TRUE(std::abs(frame_before - before - 290.0) < 0.01);
            clock.advance(0.030);
            app.run_frame();                                                              // ... and the frame after it measures from there, not from the frame before: 30 ms and no more
            ASSERT_TRUE(std::abs(app.net_clock_ms() - frame_before - 30.0) < 0.5);
        }
    } TEST_END();

    TEST_CASE("N5.35 Hidden Page: The Hidden Hours Are Not One Huge Frame - The First Frame After The Page Is Shown Measures From The Moment It Was Shown") {
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        VirtualClock clock;
        clock.set(app);
        app.run_frame();                                                                  // the first frame starts the frame loop's timer
        float before = app.get_current_fps();
        clock.advance(0.25);
        app.run_frame();                                                                  // the control: a quarter second between two frames is one frame of 100 ms (the clamp), the rate drops
        ASSERT_TRUE(app.get_current_fps() < before - 3.0f);
        before = app.get_current_fps();
        app.set_page_hidden(true);
        clock.advance(0.25);                                                              // the same quarter second, while the page is hidden
        app.set_page_hidden(false);
        app.run_frame();                                                                  // is no frame at all
        ASSERT_TRUE(app.get_current_fps() > before - 3.0f);
        app.set_page_hidden(true);
        clock.advance(3 * 3600.0);                                                        // and three hours hidden are no frame either
        app.set_page_hidden(false);
        app.run_frame();
        ASSERT_TRUE(app.get_current_fps() > before - 3.0f);
    } TEST_END();

    TEST_CASE("N5.36 Hidden Page: No Sound Effects Are Played While The Page Is Hidden (The Events Are Dropped, Not Saved Up), The Sound Is Back When It Is Shown") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 9090));
        Duo duo{app, host};
        Command hatch;                                                                    // "can't hatch" (the score is below 200): the cue of the clicking machine
        hatch.type = CommandType::Hatch;
        const size_t quiet = app.audio_mixer().active_channel_count();                    // (the match's start voice plays in a shown page: nothing mixes without a device, so it stays)
        ASSERT_TRUE(quiet >= 1);
        app.set_page_hidden(true);
        HiddenDuo hidden{app, host};
        ASSERT_EQ(app.net()->submit(hatch).status, sim::CommandResult::Status::Applied);
        hidden.step(1500);
        ASSERT_TRUE(app.sim().poll_audio_events().empty());                               // the queue of a page that nobody hears is drained all the same, it must not grow
        ASSERT_EQ(app.audio_mixer().active_channel_count(), quiet);                       // and nothing was played
        ASSERT_EQ(hidden.wakes, 1500 / net::kTurnMs);                                     // (the wake-ups did step: 1.5 s of turns)
        ASSERT_EQ(app.net()->submit(hatch).status, sim::CommandResult::Status::Applied);  // a hidden page whose frames run all the same (the browser still draws it) keeps its sound:
        host_runs(host, 2 * net::kTurnMs);                                                // the ticks of a frame are not a background step
        for (int i = 0; i < 4 && app.audio_mixer().active_channel_count() == quiet; ++i) app.run_frame_with_delta(0.1f);        // (a frame runs one turn)
        ASSERT_TRUE(app.audio_mixer().active_channel_count() > quiet);
        app.audio_mixer().stop_all();
        app.set_page_hidden(false);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);
        ASSERT_EQ(app.net()->submit(hatch).status, sim::CommandResult::Status::Applied);
        duo.step(1500);
        ASSERT_TRUE(app.audio_mixer().active_channel_count() > 0u);                       // the same cue is heard in a page that is shown
        {   // the sounds that no tick makes (a button, the can't-go cue) are silent in a step too: called from inside a step (a tick of it) they play nothing ...
            Peer host2;
            Application app2;
            ASSERT_TRUE(start_two(host2, app2, 9191));
            Duo duo2{app2, host2};
            const size_t quiet2 = app2.audio_mixer().active_channel_count();
            uint32_t ticks_seen = 0;
            app2.net()->set_on_tick([&]() {
                ++ticks_seen;
                app2.hud().play_sfx(sim::SoundID::ButtonClick);
                app2.map_select().play_sfx(sim::SoundID::CantGo);
                app2.scorecard().play_sfx(sim::SoundID::NavButtonClick);
            });
            app2.set_page_hidden(true);
            HiddenDuo hidden2{app2, host2};
            hidden2.step(600);
            ASSERT_TRUE(ticks_seen >= 4);
            ASSERT_EQ(app2.audio_mixer().active_channel_count(), quiet2);
            app2.set_page_hidden(false);
            duo2.step(600);
            ASSERT_TRUE(ticks_seen >= 8);
            ASSERT_TRUE(app2.audio_mixer().active_channel_count() > quiet2);              // ... and in a page that is shown the same calls are heard
        }
    } TEST_END();

    TEST_CASE("N5.37 Hidden Page: When The Page Is Shown The Last Wake-Up Brings The Clocks Up To Date, Then The Frame Loop Has The Match And Nothing Steps Twice") {
        Peer host;
        Application app;
        ASSERT_TRUE(start_two(host, app, 2468));
        VirtualClock clock;
        clock.set(app);
        app.run_frame();                                                                  // (the frame loop's timer starts)
        app.set_page_hidden(true);
        host_runs(host, 4 * net::kTurnMs);                                                // four turns are on their way and nobody woke the page
        clock.advance(0.3);
        const uint32_t before = app.net()->turns_executed();
        ASSERT_EQ(app.background_pumps(), 0u);
        app.set_page_hidden(false);
        ASSERT_TRUE(app.net()->turns_executed() > before);                                // the turns that waited ran at the moment the page was shown
        ASSERT_EQ(app.background_pumps(), 1u);
        ASSERT_FALSE(app.background_driven());
        ASSERT_FALSE(app.background_pump_after(0.1f));                                    // nothing steps in the background any more
        app.set_page_hidden(false);                                                       // (shown twice is shown once)
        ASSERT_EQ(app.background_pumps(), 1u);
    } TEST_END();

    TEST_CASE("N5.38 Hidden Page: A Local Game Is Not Driven By Wake-Ups - It Stands Still With The Page, Frames Run As Before; A Page That Was Never Told Is Shown") {
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        ASSERT_FALSE(app.page_hidden());
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.set_page_hidden(true);
        ASSERT_TRUE(app.page_hidden());
        ASSERT_FALSE(app.background_driven());                                            // no room, no match of the network
        ASSERT_FALSE(app.background_pump_after(1.0f));
        ASSERT_FALSE(app.background_pump());
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.hud().match_start_modal_ticks() == 0u && app.sim().current_tick() == 0u);   // the "Get ready" dialog of a local game: nothing of it passed in the wake-ups
        const float fps = app.get_current_fps();
        app.run_frame_with_delta(0.040f);                                                 // the frame loop is the only driver of a local game
        ASSERT_TRUE(app.get_current_fps() != fps);
        ASSERT_EQ(app.background_pumps(), 0u);
        ASSERT_EQ(app.hud().match_start_modal_ticks(), 0u);                               // (40 ms of the frame clock are not a whole step of 50 ms yet)
        app.run_frame_with_delta(0.040f);
        ASSERT_TRUE(app.hud().match_start_modal_ticks() == 1u && app.sim().current_tick() == 0u);   // the frames count the dialog, 50 ms a step, and the simulation waits for it
        app.set_page_hidden(false);
        ASSERT_FALSE(app.page_hidden());
    } TEST_END();

    TEST_CASE("N5.39 Hidden Page: A Page That Is Hidden Before The Match Starts Is Driven Through The Room, The Start And The Match By The Wake-Ups Alone") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        app.set_page_hidden(true);                                                        // a background tab: the page never draws a frame
        HiddenDuo hidden{app, host};
        ASSERT_TRUE(hidden.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        ASSERT_EQ(app.net()->my_seat(), 1);
        host.net.set_map("TINY.LVL");
        hidden.step(300);
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(5150, hash));
        ASSERT_TRUE(hidden.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), 0u);                          // a match that begins in a hidden page has no start voice (the shown page's has one: N5.36)
        // the "Get ready" dialog of a hidden page ends on time as well: with the first turn that executes, which a wake-up runs (post_tick), kDialogMs after the match began (the host seals
        // the first turn that long after it) and before the second turn's time: a page that never draws a frame does not need one to start its match
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0);
        uint32_t waited = 0;
        while (app.hud().is_match_start_modal_active() && waited < 20000) {
            hidden.step(10);
            waited += 10;
        }
        ASSERT_FALSE(app.hud().is_match_start_modal_active());
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_TRUE(waited >= kDialogMs - 20 && waited <= kDialogMs + 150);               // (the Begin was read a step or two before the check above)
        hidden.step(10000);
        ASSERT_TRUE(app.net()->turns_executed() + 6 >= 10000 / net::kTurnMs);             // the match runs here although no frame ever did
        ASSERT_FALSE(host.net.desynced());
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
    } TEST_END();

    TEST_CASE("N5.40 Hidden Page: Nothing Starts Or Changes The Music While The Page Is Hidden - A Match That Begins, Ends Or Is Lost In A Hidden Page Leaves Its Music For When The Page Is Shown, Once") {
        const std::string intro = "Original-Ants/INTRO.mp3";
        const auto in_game = [](const std::string& file) {
            return file == "Original-Ants/ANTS2A.mp3" || file == "Original-Ants/ANTS2B.mp3" || file == "Original-Ants/ANTSFUN3.mp3";
        };
        // a match that begins while the page is hidden: the intro of the setup screen plays on, the match's music starts when the page is shown (or at the next frame, when the
        // hidden page still draws: a frame is what a page that is heard runs). `mode`: 0 the page is shown; 1 a frame of the hidden page comes first; 2 the Begin of the match
        // waits in the link and is read by the very last wake-up that the page makes when it is shown: its music starts then, not at some later frame
        const auto match_begins_hidden = [&](int mode, uint32_t seed) {
            Peer host;
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = host.net.listen_port();
            cfg.player_name = "Bob";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);                         // the setup screen plays the intro
            app.set_page_hidden(true);
            HiddenDuo hidden{app, host};
            ASSERT_TRUE(hidden.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
            host.net.set_map("TINY.LVL");
            hidden.step(300);
            uint64_t hash = 0;
            ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
            ASSERT_TRUE(host.net.start_match(seed, hash));
            if (mode == 2) {
                ASSERT_TRUE(hidden.until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading; }, 8000));      // the Start is handled: the map is loaded and reported
                host_runs(host, 300);                                                     // the host takes the report and begins the match; nobody wakes the page
                ASSERT_EQ(app.state(), AppState::MapSelect);
                ASSERT_EQ(app.audio_mixer().music_filepath(), intro);
                app.set_page_hidden(false);                                               // the last wake-up of the hidden page reads the Begin ...
                ASSERT_EQ(app.state(), AppState::Playing);
                ASSERT_TRUE(in_game(app.audio_mixer().music_filepath()));                 // ... and the music of the match is playing when the page is shown, not at some frame after it
                ASSERT_TRUE(app.audio_mixer().is_music_playing());
                return;
            }
            ASSERT_TRUE(hidden.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);                         // the match began and nothing was started: the intro plays on
            hidden.step(3000);
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);
            if (mode == 1) {
                app.run_frame_with_delta(0.016f);                                         // a frame of the hidden page: the music of the match starts now ...
                ASSERT_TRUE(in_game(app.audio_mixer().music_filepath()));
                ASSERT_TRUE(app.audio_mixer().is_music_playing());
            }
            const std::string before_shown = app.audio_mixer().music_filepath();
            app.set_page_hidden(false);
            const std::string piece = app.audio_mixer().music_filepath();
            ASSERT_TRUE(in_game(piece));                                                  // the page is shown: the music of the match is playing
            ASSERT_TRUE(app.audio_mixer().is_music_playing());
            if (mode == 1) ASSERT_EQ(piece, before_shown);                                // (and showing the page did not start it a second time)
            app.set_page_hidden(true);                                                    // once: a hidden period in which nothing happens starts nothing
            hidden.step(1000);
            app.set_page_hidden(false);
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);                         // (a second start would pick another piece: never the one that played last)
            app.run_frame_with_delta(0.016f);                                             // and the frame that follows does not start it again
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);
        };
        match_begins_hidden(0, 4711);
        match_begins_hidden(1, 4712);
        match_begins_hidden(2, 4713);
        {   // the match ends while the page is hidden: the music closes when the page is shown (the original closes it at the end of a match)
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 8181));
            const std::string piece = app.audio_mixer().music_filepath();
            ASSERT_TRUE(in_game(piece));
            app.set_page_hidden(true);
            HiddenDuo hidden{app, host};
            hidden.step(500);
            Command quit;
            quit.type = CommandType::Quit;
            quit.issuer = 0;
            ASSERT_EQ(host.net.submit(quit).status, sim::CommandResult::Status::Applied);
            ASSERT_TRUE(hidden.until([&]() { return app.scorecard().is_open(); }, 5000));
            hidden.step(500);
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);                         // the match is over, the results are up, and the music was not touched
            ASSERT_TRUE(app.audio_mixer().is_music_playing());
            app.set_page_hidden(false);
            ASSERT_FALSE(app.audio_mixer().is_music_playing());                           // the page is shown: it closes
        }
        {   // the server is lost while the page is hidden: the setup screen is back, its intro starts when the page is shown
            auto server = std::make_unique<Server>();
            ASSERT_TRUE(server->make_room("HIDDEN-LOST", 2));
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = server->port();
            cfg.net_room = "HIDDEN-LOST";
            cfg.player_name = "Leader";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", server->port(), "Bob", 255, "HIDDEN-LOST"));
            {
                Hall hall{*server, &app, {&bob}};
                ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
                hall.step(500);
            }
            const std::string piece = app.audio_mixer().music_filepath();
            ASSERT_TRUE(in_game(piece));
            app.set_page_hidden(true);
            uint32_t clock_ms = 0;
            const auto hidden_step = [&](uint32_t ms) {                                   // the other machines run and each turn of the server wakes the page
                for (uint32_t t = 0; t < ms; t += 10) {
                    bob.now += 10;
                    bob.update();
                    if (server) {
                        server->now += 10;
                        server->update();
                    }
                    ants_test::short_pause();
                    clock_ms += 10;
                    if (clock_ms % net::kTurnMs == 0) app.background_pump_after(static_cast<float>(turn_seconds()));
                }
            };
            hidden_step(1000);
            ASSERT_EQ(app.state(), AppState::Playing);
            server.reset();                                                               // the server goes away under the hidden page: its connection closes
            for (int i = 0; i < 300 && app.state() != AppState::MapSelect; ++i) hidden_step(10);
            ants_test::real_time_tail([&]() { return app.state() == AppState::MapSelect; }, [&]() { bob.update(); app.background_pump_after(0.0f); });
            ASSERT_EQ(app.state(), AppState::MapSelect);                                  // the step noticed it: the setup screen is back ...
            ASSERT_EQ(app.audio_mixer().music_filepath(), piece);                         // ... and no music was started: the piece of the match plays on
            app.set_page_hidden(false);
            ASSERT_EQ(app.audio_mixer().music_filepath(), intro);                         // the page is shown: the intro of the setup screen starts
            ASSERT_TRUE(app.audio_mixer().is_music_playing());
        }
    } TEST_END();

    TEST_CASE("N5.41 Hidden Page: A Seat Whose Hidden Page Still Gets A Few Frames A Second Keeps Up With The Turns (The Frames Are Too Few To Carry Them: The Wake-Ups Step Between Them)") {
        const struct { uint32_t frame_ms; const char* what; } rates[] = {
            {1000, "a frame a second"}, {250, "four frames a second"}, {220, "a frame every 220 ms"}, {20, "fifty frames a second"}, {0, "no frame"},
        };
        for (const auto& rate : rates) {
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 6161, true));
            VirtualClock clock;
            clock.set(app);
            app.run_frame();                                                              // (the frame loop's timer starts)
            app.set_page_hidden(true);
            const uint32_t pumps_before = app.background_pumps();
            const double per_second = hidden_ticks_per_second(host, app, clock, rate.frame_ms, 20);
            if (!(per_second >= 19.0 && per_second <= 21.0)) std::cout << "\n    [" << rate.what << ": " << per_second << " ticks a second]\n";
            ASSERT_TRUE(per_second >= 19.0 && per_second <= 21.0);                        // 20 ticks a second of play, as the others have them (give or take the buffer)
            const uint32_t pumps = app.background_pumps() - pumps_before;
            if (rate.frame_ms > 0 && rate.frame_ms <= net::kTurnMs / 2) ASSERT_EQ(pumps, 0u);                // frames at least once per turn carry the match (with their sound): the wake-ups stand down
            if (rate.frame_ms == 0 || rate.frame_ms >= 2 * net::kTurnMs) ASSERT_TRUE(pumps >= 20 * 1000 / net::kTurnMs / 2);   // a slow frame loop (or none) cannot: the wake-ups carry it
            ASSERT_FALSE(host.net.desynced());
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(host.net.turns_executed() - app.net()->turns_executed() <= 6);    // not left behind: the server's sequencer would have waited for it
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
        }
    } TEST_END();

    TEST_CASE("N5.42 Hidden Page: The Silence Of The Host Is Real Time - A Host That Was Asked And Does Not Answer Is Called Silent By The Wake-Up After Its Ten Seconds, A Host With Data Waiting Or One That Answers (Even When It Holds Its Turns) Is Never Taken For Silent") {
        {   // a host that stops: the page is woken once a minute (a throttled timer). The first wake-up asks it (it has not been asked since it spoke), the next one decides
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5151));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);                                            // the last that the host says ...
            ASSERT_TRUE(app.background_pump());                                           // ... is read here: the silence starts now
            clock.advance(60.0);
            ASSERT_TRUE(app.background_pump());                                           // a minute of nothing, in ONE wake-up: nobody has asked the host since it spoke (a sleeping page pings
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);                  // nobody), so it is asked now and not condemned
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(2.9);
            ASSERT_TRUE(app.background_pump());                                           // 2.9 s after the question: the host has had less than the grace (3 s) to answer
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(0.2);
            ASSERT_TRUE(app.background_pump());                                           // 3.1 s: it has not answered
            ASSERT_TRUE(host_noticed_gone(app));
        }
        {   // the same when the page sleeps ten minutes between its two wake-ups
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5152));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);
            ASSERT_TRUE(app.background_pump());
            clock.advance(600.0);
            ASSERT_TRUE(app.background_pump());                                           // asks
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(600.0);
            ASSERT_TRUE(app.background_pump());                                           // the host did not answer in ten minutes: silent
            ASSERT_TRUE(host_noticed_gone(app));
        }
        {   // wake-ups every second and a half (a throttled page that still gets a timer): each hands the session half a second that the clock did not count, and the first after ten seconds
            Peer host;                                                                    // of silence notices (the host is asked at the first of them and the gap counts in full from then on)
            Application app;
            ASSERT_TRUE(start_two(host, app, 5155));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);
            ASSERT_TRUE(app.background_pump());
            for (int i = 0; i < 6; ++i) {                                                 // 9 s of silence
                clock.advance(1.5);
                ASSERT_TRUE(app.background_pump());
                ASSERT_FALSE(host_noticed_gone(app));
            }
            clock.advance(1.5);
            ASSERT_TRUE(app.background_pump());                                           // 10.5 s
            ASSERT_TRUE(host_noticed_gone(app));
        }
        {   // a live host with data waiting: its turns and pings wait in the link while the page sleeps, and the wake-up reads them first. Once a minute, for three minutes: never silent
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5153));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            ASSERT_TRUE(app.background_pump());
            for (int minute = 0; minute < 3; ++minute) {
                host_runs(host, 2000);                                                    // (the host's own clock: it is alive, it says things)
                clock.advance(60.0);
                const uint32_t turns_before = app.net()->turns_executed();
                ASSERT_TRUE(app.background_pump());
                ASSERT_TRUE(app.net()->turns_executed() > turns_before);                  // what waited ran (a second's worth of it)
                ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
                ASSERT_FALSE(app.net()->electing());
                ASSERT_FALSE(app.net()->is_host());
                ASSERT_FALSE(host_noticed_gone(app));
            }
            ASSERT_FALSE(host.net.desynced());
            ASSERT_FALSE(app.net()->desynced());
        }
        {   // a live host that holds its turns (a server that waits for a player who lags, a match that is paused) is quiet but answers pings: the wake-up asks, the answer is a
            Peer host;                                                                    // message that wakes the page and is heard, and the host is never taken for silent
            Application app;
            ASSERT_TRUE(start_two(host, app, 5156));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host.net.freeze();                                                            // nothing is sealed any more: only the answers to pings come
            host_runs(host, 2 * net::kTurnMs);
            ASSERT_TRUE(app.background_pump());
            for (int minute = 0; minute < 3; ++minute) {
                clock.advance(60.0);
                ASSERT_TRUE(app.background_pump());                                       // the timer's wake-up: asks
                ASSERT_FALSE(host_noticed_gone(app));
                host_runs(host, 200);                                                     // the host answers ...
                ASSERT_TRUE(app.background_pump());                                       // ... and the answer wakes the page
                ASSERT_FALSE(host_noticed_gone(app));
            }
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Playing);
            ASSERT_FALSE(app.net()->is_host());
        }
        {   // a shown page is not touched: its frames hand the session no gap (a frame counts at most a second), whatever the clock does between two of them
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 5154, true));
            VirtualClock clock;
            clock.set(app);
            app.run_frame();
            for (int i = 0; i < 3; ++i) {                                                 // the host says nothing (nobody runs it) and 20 s pass between the frames
                clock.advance(20.0);
                app.run_frame();
                ASSERT_FALSE(host_noticed_gone(app));
            }
            ASSERT_TRUE(app.net_clock_ms() > 0.0);
        }
    } TEST_END();

    TEST_CASE("N5.43 Hidden Page: A Wake-Up After The Application Has Stopped Does Nothing, And A Clock That Goes Backwards Counts For Nothing") {
        {
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 3131));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            ASSERT_TRUE(app.background_pump());
            for (int i = 0; i < 12 && app.is_running(); ++i) app.run_frame_with_delta(0.016f);   // a headless run ends by itself after ten frames
            ASSERT_FALSE(app.is_running());
            ASSERT_TRUE(app.network_active());                                            // (the network is still there: the stop is what keeps the wake-up out)
            ASSERT_TRUE(app.page_hidden());
            const uint32_t pumps = app.background_pumps();
            const double network_clock = app.net_clock_ms();
            clock.advance(1.0);
            ASSERT_FALSE(app.background_pump_after(0.1f));
            ASSERT_FALSE(app.background_pump());
            ASSERT_EQ(app.background_pumps(), pumps);
            ASSERT_TRUE(std::abs(app.net_clock_ms() - network_clock) < 0.001);
        }
        {
            Peer host;
            Application app;
            ASSERT_TRUE(start_two(host, app, 3132));
            VirtualClock clock;
            clock.set(app);
            app.set_page_hidden(true);
            host_runs(host, 2 * net::kTurnMs);
            clock.advance(2.0);
            ASSERT_TRUE(app.background_pump());
            const double network_clock = app.net_clock_ms();
            clock.back(5.0);                                                              // the clock goes back five seconds
            ASSERT_TRUE(app.background_pump());
            ASSERT_TRUE(std::abs(app.net_clock_ms() - network_clock) < 0.001);            // it counts for nothing (and does not wrap the network's clock)
            ASSERT_FALSE(app.net()->is_host());
            ASSERT_FALSE(host_noticed_gone(app));
            clock.advance(0.5);
            ASSERT_TRUE(app.background_pump());                                           // and the time goes on from the moment it was set back to
            ASSERT_TRUE(std::abs(app.net_clock_ms() - network_clock - 500.0) < 0.01);
            app.run_frame_with_delta(0.016f);                                             // a frame of the hidden page ... and the clock goes back behind it: the stamp of that frame
            clock.back(5.0);                                                              // says nothing any more, the frames are not taken to be in charge, a driver is needed
            ASSERT_TRUE(app.background_pump());
            clock.advance(0.2);
            ASSERT_TRUE(app.background_pump());
            ASSERT_FALSE(host_noticed_gone(app));
        }
    } TEST_END();

    TEST_CASE("N5.44 Hidden Page: The Console's Line About A Hidden Period Says What Happened (A Room Is Not A Match That Advanced By 0 Ticks) And Is Not Repeated By A Tab That Is Flicked Through") {
        ASSERT_EQ(Application::hidden_period_line(0.5, 100, 10, 1.0e9), std::string());                               // shorter than a second
        ASSERT_EQ(Application::hidden_period_line(61.24, 1224, 672, 1.0e9),
                  std::string("The page was hidden for 61.2 s; the match advanced by 1224 ticks and 672 wake-ups stepped it in the background"));
        ASSERT_EQ(Application::hidden_period_line(25.0, 0, 51, 1.0e9),
                  std::string("The page was hidden for 25.0 s; 51 wake-ups kept the connection going in the background (no match tick ran)"));
        ASSERT_EQ(Application::hidden_period_line(30.0, 600, 0, 1.0e9), std::string());                               // no wake-up stepped: nothing to tell
        ASSERT_EQ(Application::hidden_period_line(30.0, 600, 10, 9.9), std::string());                                // another line less than ten seconds ago
        ASSERT_FALSE(Application::hidden_period_line(30.0, 600, 10, 10.1).empty());
        ASSERT_TRUE(Application::hidden_period_line(1.0, 20, 5, 1.0e9).find("1.0 s") != std::string::npos);
        // a period in a room: the wake-ups answer the server, no tick runs, and the line does not claim that a match advanced
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        VirtualClock clock;
        clock.set(app);
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
        app.set_page_hidden(true);
        for (int i = 0; i < 51; ++i) {                                                    // 25 s in a room, a wake-up every half second
            clock.advance(0.5);
            host.now += 500;
            host.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            app.background_pump();
        }
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        app.set_page_hidden(false);
        const std::string line = app.last_hidden_line();
        ASSERT_TRUE(line.find("25.") != std::string::npos);
        ASSERT_TRUE(line.find("wake-ups kept the connection going") != std::string::npos);
        ASSERT_TRUE(line.find("advanced by 0 ticks") == std::string::npos);
        // a tab that is flicked through (258 times) says nothing, and a tab that is left for two seconds at a time says it now and then, not every time
        int lines = 0;
        for (int i = 0; i < 258; ++i) {
            app.set_page_hidden(true);
            clock.advance(0.08);
            app.background_pump();
            clock.advance(0.07);
            app.set_page_hidden(false);
            if (!app.last_hidden_line().empty()) ++lines;
        }
        ASSERT_EQ(lines, 0);
        for (int i = 0; i < 20; ++i) {                                                    // two seconds hidden, one second shown: a minute
            app.set_page_hidden(true);
            clock.advance(1.0);
            app.background_pump();
            clock.advance(1.0);
            app.set_page_hidden(false);
            if (!app.last_hidden_line().empty()) ++lines;
            clock.advance(1.0);
        }
        ASSERT_TRUE(lines >= 4 && lines <= 6);                                            // one at the most every ten seconds
        {   // each hidden period tells its own ticks and wake-ups, not the sums with the periods before it (a short one says nothing, but its ticks and wake-ups were there)
            Peer host2;
            Application app2;
            ASSERT_TRUE(start_two(host2, app2, 1717));
            VirtualClock clock2;
            clock2.set(app2);
            HiddenDuo hidden2{app2, host2};
            hidden2.clock = &clock2;
            Duo duo2{app2, host2};
            app2.set_page_hidden(true);
            hidden2.step(800);
            app2.set_page_hidden(false);
            ASSERT_TRUE(app2.last_hidden_line().empty());                                 // 0.8 s: nothing to say
            duo2.step(500);
            app2.set_page_hidden(true);
            const uint64_t tick_at_hide = app2.sim().current_tick();
            const uint32_t pumps_at_hide = app2.background_pumps();
            hidden2.step(2000);
            app2.set_page_hidden(false);
            const uint64_t ticks = app2.sim().current_tick() - tick_at_hide;
            ASSERT_TRUE(ticks >= 30);
            ASSERT_EQ(app2.last_hidden_line(), "The page was hidden for 2.0 s; the match advanced by " + std::to_string(ticks) + " ticks and " +
                                                   std::to_string(app2.background_pumps() - pumps_at_hide) + " wake-ups stepped it in the background");
        }
    } TEST_END();
}
