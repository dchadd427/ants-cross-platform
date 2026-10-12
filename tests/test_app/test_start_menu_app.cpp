// The desktop start menu in the application (include/ants_app/start_menu.hpp, src/ants_app/application_menu.cpp): the whole of what a player does with it, against a REAL
// RoomManager of the dedicated server behind a real TCP listener on the loopback interface (as test_network_app does for the leader's START): single player with no bot (the
// original game exactly) and with bots (seated as --bot seats them, fog off), join a room, host a room (the code, the leader's screen, a second client, START), every way
// a join can fail, cancel, and the way back to the menu after a network game. One Application per test (SDL is initialised once per process).
//
//   test_start_menu_app                         runs the tests
//   test_start_menu_app --shots DIR [--wide]    writes the screenshots of every panel and state of the menu as DIR/*.png (they are what the menu looks like; nothing is compared); --wide: the 16:9 picture (960 x 540)
//   test_start_menu_app --real-server H:P       runs the host / join / START scenario against a game server that is already running (the gate "a real server": a native ants_server
//                                               on localhost started with --demo-rooms 4 --demo-map TINY.LVL ...: the public rooms), then exits
//   test_start_menu_app --real-fill H:P         hosts a room through the menu with "Empty seats at START" = Medium bots, starts it ALONE (the server seats three bots) and plays it
//                                               for ten seconds of real time (the rate of the ticks is measured and printed: 20 a second), against a game server that is already running
#include "start_menu_app_fixtures.hpp"

namespace {

// ---- the screenshots ---------------------------------------------------------------------------------------------------------------------------------------------------

int make_shots(const std::string& dir, bool wide) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    TempDir temp;
    Server server(1);
    Application app;
    Hall hall{&server, &app, {}, nullptr, false};
    const auto slow = std::make_shared<Lookup>();                    // a name that is slow to resolve: the "Connecting" panel stays
    ApplicationConfig cfg = menu_config(server.address(), temp.file("settings.ini"));
    if (wide) {                                                      // the 16:9 picture: the hand-made config is the original's 4:3 unless it says so
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
    }
    cfg.host_resolver = lookup_of(slow);
    cfg.clipboard_set = [](const std::string&) { return true; };
    if (!app.init(cfg)) return 1;
    app.start_menu().update(0.5f);
    shot(app, dir + "/01_first_panel.png");
    press(app, SDLK_DOWN);
    shot(app, dir + "/02_first_panel_selection.png");
    // single player: nobody, then two bots (red: medium, blue: hard) with the rule-8 line
    click(app, MenuId::Single);
    shot(app, dir + "/03_single_player_empty.png");
    click(app, MenuId::Seat1);
    click(app, MenuId::Seat1);
    click(app, MenuId::Seat2);
    click(app, MenuId::Seat2);
    click(app, MenuId::Seat2);
    shot(app, dir + "/04_single_player_two_bots.png");
    press(app, SDLK_ESCAPE);
    // join with a code: empty, typed, the name refused, connecting (a slow lookup), no such room, the server cannot be reached
    click(app, MenuId::JoinWithCode);
    shot(app, dir + "/05_join_empty.png");
    fill(app, MenuId::Code, "zz-no-such-room");
    shot(app, dir + "/06_join_typed.png");
    fill(app, MenuId::Name, "Bot (Hard)");
    click(app, MenuId::Join);
    shot(app, dir + "/07_join_name_refused.png");
    fill(app, MenuId::Name, "Dave");
    app.start_menu().set_server(ServerAddress{"localhost", server.port()});         // (a name, not an address: the lookup goes to the slow resolver above)
    click(app, MenuId::Join);
    hall.step(50);
    shot(app, dir + "/08_connecting.png");
    slow->release = true;
    if (!hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000)) return 1;
    shot(app, dir + "/09_join_no_such_room.png");
    {
        const uint16_t closed = []() {
            auto l = net::TcpListener::listen(0, true);
            return l ? l->port() : uint16_t{1};
        }();
        app.start_menu().set_server(ServerAddress{"127.0.0.1", closed});
        click(app, MenuId::Join);
        if (!hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("Cannot reach") != std::string::npos; }, 8000)) return 1;
        shot(app, dir + "/10_join_server_unreachable.png");
        app.start_menu().set_server(ServerAddress{"127.0.0.1", server.port()});
    }
    // host an online match: the panel, then the room's code, copied
    press(app, SDLK_ESCAPE);
    click(app, MenuId::HostOnline);
    click(app, MenuId::HostMap);
    click(app, MenuId::HostPlayers);
    click(app, MenuId::HostPlayers);
    fill(app, MenuId::HostName, "Dave");
    shot(app, dir + "/11_host_panel.png");
    click(app, MenuId::Host);
    if (!hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000)) return 1;
    shot(app, dir + "/12_host_room_code.png");
    click(app, MenuId::Copy);
    shot(app, dir + "/13_host_room_code_copied.png");
    // the room is left; the one public room of this server is still there, so the next host attempt is refused: the server is busy
    press(app, SDLK_ESCAPE);
    hall.step(50);
    click(app, MenuId::Host);
    if (!hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000)) return 1;
    shot(app, dir + "/14_host_server_busy.png");
    press(app, SDLK_ESCAPE);
    app.start_menu().show_main("The connection to the other players was lost.");
    shot(app, dir + "/15_first_panel_notice.png");
    // what the review fixes added: a character that the fields cannot take, the copy that failed, a server that does not offer the map, the first panel after a lost game with the old code
    click(app, MenuId::JoinWithCode);
    press(app, SDLK_a, KMOD_CTRL);
    type_text(app, "caf\xC3\xA9-room");
    shot(app, dir + "/16_join_refused_character.png");
    press(app, SDLK_ESCAPE);
    click(app, MenuId::JoinWithCode);
    shot(app, dir + "/17_join_old_code_selected.png");
    press(app, SDLK_ESCAPE);
    app.start_menu().set_clipboard([]() { return std::string(); }, [](const std::string&) { return false; });
    app.start_menu().show_room("k3n7pq", 2, 4);
    click(app, MenuId::Copy);
    shot(app, dir + "/18_room_copy_failed.png");
    app.start_menu().show_main();
    Server small(2, {"TINY.LVL", "SMALL.LVL"});
    Hall hall2{&small, &app, {}, nullptr, false};
    app.start_menu().set_server(ServerAddress{"127.0.0.1", small.port()});
    click(app, MenuId::HostOnline);
    while (app.start_menu().settings().host_map != 5) click(app, MenuId::HostMap);                         // Islands
    click(app, MenuId::Host);
    if (!hall2.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000)) {
        std::cerr << "the Host panel's line did not come: state " << static_cast<int>(app.state()) << " panel " << static_cast<int>(app.start_menu().panel()) << " message '" << app.start_menu().message() << "'\n";
        return 1;
    }
    shot(app, dir + "/19_host_map_not_offered.png");
    return 0;
}

int run_real_server(const std::string& address) {
    ServerAddress server;
    std::string why;
    if (!parse_server(address, server, why)) {
        std::cerr << why << "\n";
        return 2;
    }
    TempDir temp;
    write_no_quick_help(temp.file("settings.ini"));
    Application app;
    Peer peer;
    Hall hall{nullptr, &app, {}, nullptr, true};
    if (!app.init(menu_config(address, temp.file("settings.ini")))) return 1;
    std::string report;
    const bool ok = host_join_start(hall, app, peer, server.host, server.port, report);
    std::cout << (ok ? "REAL SERVER OK: " : "REAL SERVER FAILED: ") << report << "\n";
    return ok ? 0 : 1;
}

// The menu's Host panel with Medium bots in the three seats after the leader's against a server that is already running (the real check of the fill: a server of a docker image, on the network of
// the machine): one person, alone, starts a room for four; the server seats three "Bot (Medium)" and the match runs at 20 ticks a second
int run_real_fill(const std::string& address) {
    ServerAddress server;
    std::string why;
    if (!parse_server(address, server, why)) {
        std::cerr << why << "\n";
        return 2;
    }
    TempDir temp;
    write_no_quick_help(temp.file("settings.ini"));
    Application app;
    Hall hall{nullptr, &app, {}, nullptr, true};
    if (!app.init(menu_config(address, temp.file("settings.ini")))) return 1;
    const auto fail = [](const std::string& what) {
        std::cout << "REAL FILL FAILED: " << what << "\n";
        return 1;
    };
    click(app, MenuId::HostOnline);
    for (const MenuId seat : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
        click(app, seat);                                                              // Leave empty -> Easy bot
        click(app, seat);                                                              // -> Medium bot
    }
    fill(app, MenuId::HostName, "Solo");
    click(app, MenuId::Host);
    if (!hall.until([&]() { return on_panel(app, MenuPanel::Room) || failed_on(app, MenuPanel::Host); }, 20000)) return fail("the room's panel did not come");
    if (!on_panel(app, MenuPanel::Room)) return fail("the host attempt failed: " + app.start_menu().message());
    const std::string code = shown_code(app);
    bool says = false;
    for (const MenuElement& e : app.start_menu().elements()) says = says || e.text == "Empty seats will be Medium bots.";
    if (!says) return fail("the room's panel does not say what START will do");
    click(app, MenuId::EnterRoom);
    hall.step(20);
    to_setup_screen(app);
    hall.step(700);
    if (app.state() != AppState::MapSelect || !app.map_select().leads_server_room()) return fail("the leader's screen did not come");
    if (app.map_select().room().status != "Press START: the empty seats get Medium bots.") return fail("the status line says \"" + app.map_select().room().status + "\"");
    app.room_key_down(SDLK_s, 0, false);                                               // START, alone
    if (!hall.until([&]() { return app.state() == AppState::Playing; }, 30000)) return fail("the match did not start (the status line: \"" + app.map_select().room().status + "\")");
    if (app.sim().roster_mask() != 0x0F) return fail("the roster is not all four seats");
    for (uint8_t seat = 1; seat < 4; ++seat) {
        if (app.sim().get_player_name(seat) != "Bot (Medium)") return fail("seat " + std::to_string(seat) + " is called \"" + app.sim().get_player_name(seat) + "\"");
    }
    hall.step(1000);                                                                   // (the first second: the match settles)
    const uint64_t t0 = app.sim().current_tick();
    const auto begin = std::chrono::steady_clock::now();
    hall.step(10000);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double rate = static_cast<double>(app.sim().current_tick() - t0) / seconds;
    if (app.net()->desynced()) return fail("the match is out of sync");
    std::cout << "REAL FILL " << (rate >= 17.0 && rate <= 23.0 ? "OK" : "FAILED") << ": room " << code << ", alone with three Bot (Medium), " << (app.sim().current_tick() - t0) << " ticks in "
              << seconds << " s = " << rate << " ticks a second, no desync\n";
    return rate >= 17.0 && rate <= 23.0 ? 0 : 1;
}

int run_probe(const std::string& address) {
    TempDir temp;
    Application app;
    Hall hall{nullptr, &app, {}, nullptr, true};
    ApplicationConfig cfg = menu_config(address, temp.file("settings.ini"));
    cfg.menu_connect_timeout_ms = 30000;
    if (!app.init(cfg)) return 1;
    const auto begin = std::chrono::steady_clock::now();
    menu_join(app, "Probe", "zz-no-such-room-probe");                   // (a Join carries no block: nothing is made on the server)
    const bool answered = hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 40000);
    const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count());
    std::cout << "PROBE " << address << ": " << (answered ? "answered" : "NO ANSWER") << " after " << ms << " ms: \"" << app.start_menu().message() << "\"\n";
    return answered ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shots") == 0) {
            bool wide = false;
            for (int k = 1; k < argc; ++k) wide = wide || std::strcmp(argv[k], "--wide") == 0;
            return make_shots(argv[i + 1], wide);
        }
        if (std::strcmp(argv[i], "--real-server") == 0) return run_real_server(argv[i + 1]);
        if (std::strcmp(argv[i], "--real-fill") == 0) return run_real_fill(argv[i + 1]);
        if (std::strcmp(argv[i], "--probe") == 0) return run_probe(argv[i + 1]);
    }
    std::cout << "=== Start menu in the application ===\n";

    run_start_menu_app_play_tests();
    run_start_menu_app_options_tests();

    std::cout << "\nstart menu in the application: " << g_test_count << " tests, " << g_assert_count << " assertions, " << g_test_failures << " failures\n";
    if (g_test_count == 0) {                                      // (a misspelt or forgotten filter must not turn the suite green)
        std::cout << "\n no test ran: the filter ANTS_TEST_FILTER matches no test of this suite\n";
        return 1;
    }
    return g_test_failures == 0 ? 0 : 1;
}
