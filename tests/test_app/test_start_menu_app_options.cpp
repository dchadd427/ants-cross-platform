// Tests of the desktop start menu in the application: the title of the window, the names, the clipboard, the command line, the sounds, the double click, the leftovers of a game, the fill (A7.1 - A14.4)
#include "start_menu_app_fixtures.hpp"

void run_start_menu_app_options_tests() {
    TEST_CASE("A8.1 The panel refuses exactly the names that the server would rename: a battery of names is sent to a real server, and the ones the menu accepts come back as they were, the ones it refuses come back as 'Player N'") {
        Server server;
        const std::vector<std::string> names = {"Dave",        "Bobby",   "Robot",    "Bot",      "Bot Smith", "bot-9",   "Abot (x)", "Bot (Hard)", "bot (x)",  "BOT (Easy)",
                                                " Bot (x)",    "B o t (x)", "bot(x)", "  bOt(  ", "x",         "Mary Ann", "Zed and Co", "bot (",     "B O T ("};
        Hall hall{&server, nullptr, {}, nullptr, false};
        int n = 0;
        for (const std::string& name : names) {
            const std::string code = "name-battery-" + std::to_string(++n);
            ASSERT_TRUE(server.make_room(code, 2));
            Peer peer;
            hall.peers = {&peer};
            ASSERT_TRUE(peer.join("127.0.0.1", server.port(), name, code));
            ASSERT_TRUE(hall.until([&]() { return peer.net.phase() == net::NetGame::Phase::Room && server.status(code).joined == 1; }, 8000));
            const std::string at_server = server.status(code).names[0];
            std::string clean;
            std::string why;
            const bool accepted = check_player_name(name, clean, why);
            if (accepted) {
                if (at_server != clean) std::cout << "\n    '" << name << "' was accepted by the menu as '" << clean << "', the server shows '" << at_server << "'";
                ASSERT_EQ(at_server, clean);
            } else {
                if (at_server != "Player 1") std::cout << "\n    '" << name << "' was refused by the menu, the server shows '" << at_server << "'";
                ASSERT_EQ(at_server, std::string("Player 1"));
            }
            peer.net.leave();
            hall.peers.clear();
        }
    } TEST_END();

    TEST_CASE("A9.1 The clipboard: Cmd+V / Ctrl+V pastes what the system clipboard holds into the code (through the key events of the window), Copy hands the room's code to the system clipboard; a clipboard that fails says so") {
        TempDir temp;
        Server server;
        std::string clipboard = "  k7m2xq \n";
        std::vector<std::string> written;
        bool writable = true;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.clipboard_get = [&clipboard]() { return clipboard; };
        cfg.clipboard_set = [&](const std::string& text) {
            written.push_back(text);
            return writable;
        };
        ASSERT_TRUE(app.init(cfg));
        click(app, MenuId::JoinWithCode);
        click(app, MenuId::Code);
        press(app, SDLK_v, KMOD_GUI);
        ASSERT_EQ(app.start_menu().code(), std::string("k7m2xq"));
        press(app, SDLK_a, KMOD_CTRL);
        clipboard = "another-1";
        press(app, SDLK_v, KMOD_CTRL);
        ASSERT_EQ(app.start_menu().code(), std::string("another-1"));
        press(app, SDLK_ESCAPE);
        menu_host(app, "Hostess", 0, 1);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        writable = false;
        click(app, MenuId::Copy);
        ASSERT_TRUE(written.size() == 1 && written[0] == shown_code(app));
        ASSERT_HAS(app.start_menu().message(), "Copy failed");
        writable = true;
        click(app, MenuId::Copy);
        ASSERT_TRUE(written.size() == 2 && written[1] == shown_code(app));
        ASSERT_TRUE(app.start_menu().copied());
        ASSERT_TRUE(app.start_menu().message().empty());
    } TEST_END();

    TEST_CASE("A9.2 The real clipboard of SDL (the dummy video driver's here) is what the menu uses when nothing else is set: Cmd+V pastes what SDL_SetClipboardText put there (trimmed), Copy leaves the room's code for SDL_GetClipboardText") {
        TempDir temp;
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));         // no clipboard hooks: the defaults, SDL's own
        ASSERT_EQ(SDL_SetClipboardText("  k7m2xq \n"), 0);
        click(app, MenuId::JoinWithCode);
        click(app, MenuId::Code);
        press(app, SDLK_v, KMOD_GUI);
        ASSERT_EQ(app.start_menu().code(), std::string("k7m2xq"));
        press(app, SDLK_ESCAPE);
        menu_host(app, "Hostess", 0, 1);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        ASSERT_EQ(SDL_SetClipboardText("something else"), 0);
        click(app, MenuId::Copy);
        char* text = SDL_GetClipboardText();
        ASSERT_TRUE(text != nullptr);
        const std::string copied = text != nullptr ? text : "";
        if (text != nullptr) SDL_free(text);
        ASSERT_EQ(copied, shown_code(app));
        ASSERT_TRUE(app.start_menu().copied());
        ASSERT_TRUE(app.start_menu().message().empty());
    } TEST_END();

    TEST_CASE("A10.1 The command line --start-menu --headless --screenshot FILE draws the first panel and ends (the screenshots of the menu are made this way): the picture is the menu's (the orange of the original's Single / Multi screen fills most of it), not an empty frame") {
        TempDir temp;
        // (a .bmp: the game turns a .png into a PNG with Python's PIL, which a machine need not have; the bitmap is what the renderer reads back and is the same everywhere)
        // (a command line is the 16:9 picture on a desktop now, the widescreen work: this test is about the menu's own 640 x 480 picture, so it says --aspect 4:3; A10.1b is the 16:9 one)
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--aspect", "4:3", "--screenshot", temp.file("menu.bmp"), "--frames", "6", "--settings", temp.file("s.ini")};
        std::vector<char*> storage;
        const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_TRUE(cfg.start_menu && cfg.headless && cfg.startup_error.empty());
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.run(), 0);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        const std::string bmp = read_file(temp.file("menu.bmp"));
        ASSERT_TRUE(bmp.size() > 54 && bmp.substr(0, 2) == "BM");
        const auto le32 = [&bmp](size_t at) {
            uint32_t v = 0;
            for (size_t i = 4; i-- > 0;) v = (v << 8) | static_cast<uint8_t>(bmp[at + i]);
            return v;
        };
        const uint32_t offset = le32(10);
        const int32_t width = static_cast<int32_t>(le32(18));
        const int32_t height = std::abs(static_cast<int32_t>(le32(22)));
        const uint32_t bits = static_cast<uint8_t>(bmp[28]) | (static_cast<uint32_t>(static_cast<uint8_t>(bmp[29])) << 8);
        ASSERT_TRUE(bits == 32 && width > 0 && height > 0 && width * 3 == height * 4);        // a picture of the 4 : 3 screen (640 x 480), 32 bits a pixel
        ASSERT_TRUE(bmp.size() >= offset + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        size_t orange = 0;
        size_t green = 0;
        for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
            const size_t at = offset + i * 4;
            const int b = static_cast<uint8_t>(bmp[at]);
            const int g = static_cast<uint8_t>(bmp[at + 1]);
            const int r = static_cast<uint8_t>(bmp[at + 2]);
            if (r >= 190 && g >= 50 && g <= 110 && b <= 50) ++orange;
            if (r <= 70 && g >= 80 && g <= 130 && b >= 60 && b <= 110) ++green;               // the buttons' face
        }
        const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
        ASSERT_TRUE(orange * 100 >= total * 35);                                              // the background of the Single / Multi screen
        ASSERT_TRUE(green * 100 >= total * 8);                                                // and the four buttons and the banner on it
    } TEST_END();

    TEST_CASE("A10.1b The same screenshot in the 16:9 picture (the default of a desktop): the 960 x 540 canvas is the menu's own wide page (the wide frame at its edge, tile clay, the controls centred: the title plate at the top, the buttons 30 lower), and the title plate and the four buttons are pixel for pixel the 4:3 picture's, moved") {
        TempDir temp;
        struct Shot {
            int32_t width{0};
            int32_t height{0};
            bool bottom_up{true};
            std::string bmp;
            uint32_t offset{0};
            std::array<uint8_t, 3> at(int32_t x, int32_t y) const {                           // the color at the image's (x, y), top down
                const int32_t row = bottom_up ? height - 1 - y : y;
                const size_t i = offset + (static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
                return {static_cast<uint8_t>(bmp[i + 2]), static_cast<uint8_t>(bmp[i + 1]), static_cast<uint8_t>(bmp[i])};
            }
        };
        const auto take = [&](const char* aspect, const char* window, const char* name) {
            std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--aspect", aspect, "--window-size", window, "--screenshot", temp.file(name), "--frames", "6", "--settings", temp.file("s.ini")};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            Shot shot;
            if (!cfg.start_menu || !cfg.startup_error.empty()) return shot;
            Application app;
            if (!app.init(cfg) || app.run() != 0) return shot;
            shot.bmp = read_file(temp.file(name));
            if (shot.bmp.size() < 54 || shot.bmp.substr(0, 2) != "BM") return shot;
            const auto le32 = [&shot](size_t at) {
                uint32_t v = 0;
                for (size_t i = 4; i-- > 0;) v = (v << 8) | static_cast<uint8_t>(shot.bmp[at + i]);
                return v;
            };
            shot.offset = le32(10);
            shot.width = static_cast<int32_t>(le32(18));
            const int32_t signed_height = static_cast<int32_t>(le32(22));
            shot.bottom_up = signed_height > 0;
            shot.height = std::abs(signed_height);
            if (shot.bmp.size() < shot.offset + static_cast<size_t>(shot.width) * static_cast<size_t>(shot.height) * 4) shot.width = shot.height = 0;
            return shot;
        };
        const Shot classic = take("4:3", "640,480", "classic.bmp");
        const Shot wide = take("16:9", "960,540", "wide.bmp");
        ASSERT_TRUE(classic.width == 640 && classic.height == 480);
        ASSERT_TRUE(wide.width == 960 && wide.height == 540);
        const std::array<uint8_t, 3> clay{219, 75, 19};
        // (this used to compare the 640 x 480 page, centred at (160, 30) over a margin of flat clay, with the 4:3 picture; the page is the wide page now: the frame of the canvas, the tile clay)
        for (const std::pair<int, int>& p : {std::pair<int, int>{2, 2}, {957, 2}, {2, 537}, {957, 537}, {480, 2}, {2, 270}, {957, 270}, {600, 537}}) ASSERT_TRUE(wide.at(p.first, p.second) != clay);    // the frame is at the canvas's edge
        int64_t clay_pixels = 0;
        const int64_t margin_pixels = int64_t{250} * 410;                                      // the clay left of the controls (x 20 .. 269, y 90 .. 499): the tile is 97 % flat clay with isolated speckles
        for (int32_t y = 90; y < 500; ++y) {
            for (int32_t x = 20; x < 270; ++x) clay_pixels += wide.at(x, y) == clay ? 1 : 0;
        }
        ASSERT_TRUE(clay_pixels * 100 >= margin_pixels * 95 && clay_pixels < margin_pixels);   // clay, and not flat: the tile's speckles are there
        // the plates are the same pixels: the title plate (120, 28, 400 x 44) stays at the top (+160, +0), the four buttons (160, y, 320 x 50) move to the middle (+160, +30); the 1 pixel drop shadow is in
        for (const ButtonRect& r : {ButtonRect{120, 28, 401, 45}, ButtonRect{160, 118, 321, 51}, ButtonRect{160, 184, 321, 51}, ButtonRect{160, 250, 321, 51}, ButtonRect{160, 316, 321, 51}}) {
            const int32_t dy = r.y == 28 ? 0 : 30;
            int64_t differ = 0;
            for (int32_t y = 0; y < r.h; ++y) {
                for (int32_t x = 0; x < r.w; ++x) differ += wide.at(r.x + 160 + x, r.y + dy + y) != classic.at(r.x + x, r.y + y) ? 1 : 0;
            }
            ASSERT_EQ(differ, int64_t{0});
        }
    } TEST_END();

    TEST_CASE("A10.2 --start-menu together with --bot SEAT:LEVEL (the way the tests and the screenshots show bots): the single-player rows start as the command line says, Continue offers exactly those bots, and a bot of another kind than the standard one is no row") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--bot", "1:hard", "--bot", "3:easy", "--bot", "2:idle", "--settings", temp.file("s.ini")};
        std::vector<char*> storage;
        const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_TRUE(cfg.start_menu && cfg.startup_error.empty());
        ASSERT_EQ(cfg.bots.size(), size_t{3});
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_TRUE(app.start_menu().seat(1) == SeatChoice::Hard && app.start_menu().seat(3) == SeatChoice::Easy);
        ASSERT_TRUE(app.start_menu().seat(2) == SeatChoice::Empty && app.start_menu().seat(0) == SeatChoice::Empty);   // (the idle bot is the command line's own business, not a row)
        click(app, MenuId::Single);
        const std::vector<ai::BotSpec> offered = app.start_menu().bots();
        ASSERT_EQ(offered.size(), size_t{2});
        ASSERT_TRUE(offered[0].seat == 1 && offered[0].level == ai::Level::Hard && offered[1].seat == 3 && offered[1].level == ai::Level::Easy);
        ASSERT_TRUE(app.start_menu().any_bot());
    } TEST_END();

    TEST_CASE("A10.3 --start-menu together with --player 2 (the blue ants): the single-player panel offers the other three seats (green, red, black), a bot never sits at the own seat, and the match that follows is the blue player's") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--player", "2", "--settings", temp.file("s.ini")};
        std::vector<char*> storage;
        const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_TRUE(cfg.start_menu && cfg.startup_error.empty());
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.start_menu().own_seat(), 2);
        click(app, MenuId::Single);
        MenuElement row;
        ASSERT_TRUE(app.start_menu().find_element(MenuId::Seat0, row) && app.start_menu().find_element(MenuId::Seat1, row) && app.start_menu().find_element(MenuId::Seat3, row));
        ASSERT_FALSE(app.start_menu().find_element(MenuId::Seat2, row));                    // no row for the own seat
        click(app, MenuId::Seat0);                                                           // Easy at green
        click(app, MenuId::Seat3);
        click(app, MenuId::Seat3);                                                           // Medium at black
        const std::vector<ai::BotSpec> offered = app.start_menu().bots();
        ASSERT_EQ(offered.size(), size_t{2});
        ASSERT_TRUE(offered[0].seat == 0 && offered[0].level == ai::Level::Easy && offered[1].seat == 3 && offered[1].level == ai::Level::Medium);
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.local_player_id(), 2);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.local_player_id(), 2);
        ASSERT_EQ(app.sim().roster_mask(), 0x0D);                                            // blue (bit 2), green (bit 0) and black (bit 3) play; red has no ants
        ASSERT_EQ(app.sim().get_player_name(0), std::string("Bot (Easy)"));
        ASSERT_EQ(app.sim().get_player_name(3), std::string("Bot (Medium)"));
    } TEST_END();

    TEST_CASE("A10.5 --start-menu together with --teams (the way the tests and the screenshots show it): the Teams choice starts as the command line says when the seats offer it (three bots: You + Blue), a team of a seat that has no bot is no choice, and --teams without a mode does not skip the menu") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        {
            std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--bot", "1", "--bot", "2", "--bot", "3", "--teams", "0+2", "--settings", temp.file("s.ini")};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_TRUE(cfg.start_menu && cfg.startup_error.empty() && cfg.teams.set);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.start_menu().teams() == LocalTeams({true, 0, 2}));
            click(app, MenuId::Single);
            MenuElement row;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Teams, row) && row.value == "You + Blue");
            ASSERT_FALSE(std::ifstream(temp.file("s.ini")).good() && read_file(temp.file("s.ini")).find("teams=") != std::string::npos);     // nothing is written until the player changes it
        }
        {   // a team of a seat that has no bot (two bots, the team is with the third seat): free for all
            std::vector<std::string> args = {"ants", "--start-menu", "--headless", "--bot", "1", "--bot", "2", "--teams", "0+3", "--settings", temp.file("s.ini")};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_menu().teams() == LocalTeams{});
            click(app, MenuId::Single);
            MenuElement row;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Teams, row) && row.value == "Free for all");
        }
        {   // --teams alone is no mode: the start menu shows (as it does for --name)
            std::vector<std::string> args = {"ants", "--teams", "0+1"};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_TRUE(cfg.start_menu && cfg.teams.set);
        }
    } TEST_END();

    TEST_CASE("A10.4 The window's own event loop reaches the menu (the tests above call the menu's handler directly): keys, typed text and the mouse queued in SDL's event queue change the panels in the next frame, and the window's close button ends the program from the menu") {
        TempDir temp;
        Application app;
        ASSERT_TRUE(app.init(menu_config("127.0.0.1:1", temp.file("s.ini"))));
        const auto push = [](SDL_Event e) { ASSERT_TRUE(SDL_PushEvent(&e) == 1); };
        app.start_menu().update(0.4f);                                                       // (the first panel has been up for a while: Enter acts)
        push(key_event(SDLK_DOWN));                                                          // first panel: Join with a code
        push(key_event(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        push(text_event("queued-room"));                                                     // typed text goes to the field that has the focus: the code (the name is proposed)
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().code(), std::string("queued-room"));
        app.start_menu().update(0.4f);
        push(key_event(SDLK_ESCAPE));                                                        // back to the first panel
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
        MenuElement host;                                                                     // the mouse: a click on "Host an online match"
        ASSERT_TRUE(app.start_menu().find_element(MenuId::HostOnline, host));
        const int32_t x = host.rect.x + host.rect.w / 2;
        const int32_t y = host.rect.y + host.rect.h / 2;
        push(mouse_event(SDL_MOUSEMOTION, x, y));
        push(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
        push(mouse_event(SDL_MOUSEBUTTONUP, x, y));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);
        SDL_Event quit;
        std::memset(&quit, 0, sizeof(quit));
        quit.type = SDL_QUIT;
        push(quit);
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.is_running());
    } TEST_END();

    TEST_CASE("A11.1 The menu's sounds reach the mixer: the pointer over a button is silent, the press of a button plays the click (the original's buttonclick), a refusal on the Join panel plays the can't-go cue") {
        TempDir temp;
        Server server;
        Application app;
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        MenuElement single;
        ASSERT_TRUE(app.start_menu().find_element(MenuId::Single, single));
        const int32_t x = single.rect.x + single.rect.w / 2;
        const int32_t y = single.rect.y + single.rect.h / 2;
        const size_t base = app.audio_mixer().active_channel_count();
        app.handle_menu_event(mouse_event(SDL_MOUSEMOTION, x, y));
        ASSERT_EQ(app.audio_mixer().active_channel_count(), base);                           // the hover makes no sound
        app.handle_menu_event(mouse_event(SDL_MOUSEBUTTONDOWN, x, y));
        ASSERT_EQ(app.audio_mixer().active_channel_count(), base + 1);                       // the click at the press
        app.handle_menu_event(mouse_event(SDL_MOUSEBUTTONUP, x, y));
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
        press(app, SDLK_ESCAPE);
        click(app, MenuId::JoinWithCode);
        fill(app, MenuId::Name, "Bot (Hard)");
        fill(app, MenuId::Code, "cue-room-1");
        const size_t before = app.audio_mixer().active_channel_count();
        press(app, SDLK_RETURN);                                                              // Enter on the code: Join, refused (a name for computer players)
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_EQ(app.audio_mixer().active_channel_count(), before + 1);                     // the can't-go cue
    } TEST_END();

    TEST_CASE("A12.1 NetGame tells why a join failed (fail_reason, reject_reason): none while connecting, Unreachable for a port that nothing listens on, Rejected with the server's own reason (each of the six), Lost for a connection that was open and closed before the room; leave() takes the reason back") {
        Server server(0);
        RawServer raw;
        Peer p;
        Hall hall{&server, nullptr, {&p}, &raw, false};
        using FailReason = net::NetGame::FailReason;
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None);
        const uint16_t closed = []() {
            auto l = net::TcpListener::listen(0, true);
            return l ? l->port() : uint16_t{1};
        }();
        ASSERT_TRUE(p.join("127.0.0.1", closed, "Ann", "room-x"));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None);                                // not failed yet
        ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::Unreachable);
        p.net.leave();
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None);                                // (leave() starts the next attempt from nothing)
        for (const net::RejectReason reason : {net::RejectReason::Full, net::RejectReason::VersionMismatch, net::RejectReason::MatchRunning, net::RejectReason::Kicked,
                                               net::RejectReason::BadRequest, net::RejectReason::NoSuchRoom, net::RejectReason::Dropped, net::RejectReason::RejoinFailed,
                                               net::RejectReason::Superseded}) {
            raw.mode = RawServer::Mode::Reject;
            raw.reason = reason;
            ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
            ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
            ASSERT_TRUE(p.net.fail_reason() == FailReason::Rejected);
            ASSERT_TRUE(p.net.reject_reason() == reason);
            p.net.leave();
        }
        ASSERT_TRUE(p.join("127.0.0.1", server.port(), "Ann", "no-such-room"));                // a real server: the code is no room
        ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::Rejected && p.net.reject_reason() == net::RejectReason::NoSuchRoom);
        p.net.leave();
        raw.mode = RawServer::Mode::HangUp;                                                  // the connection is open, the server closes it without a word
        ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
        ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 8000));
        ASSERT_TRUE(p.net.fail_reason() == FailReason::Lost);
        p.net.leave();
        ASSERT_TRUE(p.net.fail_reason() == FailReason::None && p.net.phase() == net::NetGame::Phase::Off);
    } TEST_END();


    // ---- the review fixes of the start menu ---------------------------------------------------------------------------------------------------------------------

    TEST_CASE("A13.1 The second click of a double click is not a click on the screen that the first one opened (the controls lie on top of each other): the entries and the panels, Cancel and Back, the loading screen and Quit, the quit dialog and the Host entry; a click after the double-click time is a click") {
        TempDir temp;
        Server server;
        {   // "Host an online match" and the Host panel's Host button: the second click made a room on the server
            // (the Host panel of a room of three players, which the settings say: the rows above Host are as many as the room has, and with four players they push the button below the entry)
            const std::string a_ini = ini(temp, "a.ini");
            {
                std::ofstream out(a_ini, std::ios::binary | std::ios::app);
                out << "host_players=3\n";
            }
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), a_ini)));
            Pt entry = middle_of(app, MenuId::HostOnline);
            entry.y += 21;                                                                   // (the lower part of the entry, where the Host button of that panel lies)
            queue_click(entry, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);
            ASSERT_TRUE(lies_on(app, MenuId::Host, entry));                                 // the geometry that makes it dangerous: Host is under the entry that was clicked
            ASSERT_TRUE(app.menu_gesture_pending());
            queue_press(entry, 2);                                                           // the second click of the double click
            queue_release(entry, 2);
            app.run_frame_with_delta(0.016f);
            hall.step(200);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);                           // not Connecting, not the room
            ASSERT_TRUE(app.net() == nullptr && server.mgr.list(server.now).empty());
            // the time alone is enough (SDL gave up the count: the pointer moved between the clicks): a press that is stamped no later than the double-click time after the click that changed
            // the screen, or before it (it was queued before that click was handled), is the rest of it
            ASSERT_TRUE(app.swallow_menu_gesture(1, 0));
            // the count alone is enough too, long after the first click (a slow double click): clicks 2 is the rest of a sequence, whatever the time says
            ASSERT_TRUE(app.swallow_menu_gesture(2, 0x7FFFFFFFu));
            ASSERT_TRUE(app.menu_gesture_pending());
            // a click after the double-click time is a new click: the screen's, and the rule ends
            ASSERT_FALSE(app.swallow_menu_gesture(1, SDL_GetTicks() + Application::kDoubleClickMs + 50));
            ASSERT_FALSE(app.menu_gesture_pending());
            queue_click(entry, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));  // (the Host button works: this is the room that it makes)
            ASSERT_EQ(server.mgr.list(server.now).size(), static_cast<size_t>(1));
        }
        {   // "Single player" and the Red seat's row: the second click changed Red to an Easy bot and wrote it to the settings
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            Pt entry = middle_of(app, MenuId::Single);
            entry.y += 16;                                                                   // (a place of the button that the Red row has under it on the panel that it opens: the name field is above that row)
            queue_click(entry, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
            ASSERT_TRUE(lies_on(app, MenuId::Seat1, entry));
            queue_press(entry, 2);
            queue_release(entry, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().seat(1), SeatChoice::Empty);
            ASSERT_TRUE(app.start_menu().bots().empty());
            ASSERT_TRUE(read_file(temp.file("b.ini")).find("bots") == std::string::npos);   // nothing was saved
            // a cycler is clicked twice on purpose when the panel is the same: a double click on a row changes it twice (no screen changed)
            ASSERT_FALSE(app.swallow_menu_gesture(1, SDL_GetTicks() + 2 * Application::kDoubleClickMs));
            const Pt row = middle_of(app, MenuId::Seat2);
            queue_click(row, 1);
            queue_click(row, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().seat(2), SeatChoice::Medium);
        }
        {   // Cancel on "Connecting" and the Join panel's Back button under it: the second click went on to the first panel; the same with the Host panel's Host button at the top of Cancel
            const auto slow = std::make_shared<Lookup>();
            for (const bool host : {false, true}) {
                Application app;
                ApplicationConfig cfg = menu_config("slow.example.org:4001", ini(temp, host ? "c2.ini" : "c1.ini"));
                cfg.host_resolver = lookup_of(slow);
                ASSERT_TRUE(app.init(cfg));
                if (host) menu_host(app, "Dave", 0, 0);
                else menu_join(app, "Dave", "abc-1");
                app.pump_network(0.01f);
                ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
                MenuElement cancel;
                ASSERT_TRUE(app.start_menu().find_element(MenuId::Cancel, cancel));
                const Pt at{cancel.rect.x + cancel.rect.w / 2, cancel.rect.y + 20};                // (the Host button and Join's Back lie under the same place of Cancel)
                queue_click(at, 1);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.start_menu().panel(), host ? MenuPanel::Host : MenuPanel::Join);
                ASSERT_TRUE(lies_on(app, host ? MenuId::Host : MenuId::Back, at));
                queue_press(at, 2);
                queue_release(at, 2);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.start_menu().panel(), host ? MenuPanel::Host : MenuPanel::Join);   // not the first panel, not a new attempt
                ASSERT_TRUE(app.net() == nullptr);
                for (int waited = 0; waited < 2000 && slow->calls.load() < (host ? 2 : 1); ++waited) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // (the worker thread of the lookup counts when the system starts it: a busy machine starts it late)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));                        // (a lookup that should not be there has had its time too)
                ASSERT_EQ(slow->calls.load(), host ? 2 : 1);                                       // (the lookups of this run so far: the Join one, then the Host one; no third)
            }
            slow->release = true;
        }
        {   // the loading screen: its closing click and the second click on "Quit" ended the program
            Application app;
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                                      // a real window without a screen (as test 7.8e of the integration suite has)
            SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
            ApplicationConfig cfg = menu_config("127.0.0.1:1", ini(temp, "e.ini"));
            cfg.headless = false;
            cfg.skip_intro = false;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::Loading);
            StartMenu layout;
            layout.show_main();
            MenuElement quit;
            ASSERT_TRUE(layout.find_element(MenuId::Quit, quit));
            const Pt at{quit.rect.x + quit.rect.w / 2, quit.rect.y + quit.rect.h / 2};
            queue_click(at, 1);                                                               // the click that ends the loading screen
            queue_press(at, 2);
            queue_release(at, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_TRUE(app.is_running());                                                    // Quit was not pressed
            SDL_Delay(Application::kDoubleClickMs + 60);
            queue_press(at, 1);                                                               // ... a click of its own, after the sequence, is: the menu answers it
            queue_release(at, 1);
            app.run_frame_with_delta(0.016f);
            ASSERT_FALSE(app.is_running());
            app.shutdown();
        }
        {   // Continue leads to the quick help: a click that follows within the double-click time is the rest of Continue's click, not a press on the quick help's START! (and not on the setup screen's START either)
            for (const bool quick_help : {true, false}) {
                Application app;
                ASSERT_TRUE(app.init(menu_config(server.address(), quick_help ? temp.file("g.ini") : ini(temp, "h.ini"))));
                click(app, MenuId::Single);                                                       // (the menu's own handler: no gesture here)
                queue_click(middle_of(app, MenuId::Continue), 1);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.state(), quick_help ? AppState::QuickHelp : AppState::MapSelect);
                const Pt start = quick_help ? Pt{578, 450} : Pt{MapSelectScreen::BTN_START_X + 49, MapSelectScreen::BTN_START_Y + 13};     // START! / START: lie under the same hand
                queue_press(start, 2);                                                             // (SDL's count says it is the second click: the rule does not depend on the machine's speed)
                queue_release(start, 2);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.state(), quick_help ? AppState::QuickHelp : AppState::MapSelect);        // nothing was pressed
                SDL_Delay(Application::kDoubleClickMs + 60);
                queue_press(start, 1);                                                             // a click of its own, after the sequence
                queue_release(start, 1);
                app.run_frame_with_delta(0.016f);
                ASSERT_EQ(app.state(), quick_help ? AppState::MapSelect : AppState::Playing);
            }
        }
        {   // a match that is left through the quit dialog's Yes: the second click of the double click lands on "Host an online match" of the first panel (the Yes button lies under it)
            Application app;
            Peer ann;
            Peer bob;
            Hall hall{&server, &app, {&ann, &bob}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "f.ini"))));
            ASSERT_TRUE(server.make_room("dblclk01", 3));
            ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "dblclk01"));
            ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", "dblclk01"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            menu_join(app, "Dave", "dblclk01");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
            hall.step(6000);
            SDL_KeyboardEvent q_ev{};
            q_ev.type = SDL_KEYDOWN;
            q_ev.keysym.sym = SDLK_q;
            q_ev.keysym.mod = KMOD_LCTRL;
            app.handle_key_down(q_ev);
            ASSERT_TRUE(app.hud().is_quit_dialog_open());
            const Pt yes{204, 272};                                                           // the Yes button (180, 260, 49 x 24)
            queue_click(yes, 1);
            queue_press(yes, 2);
            queue_release(yes, 2);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(lies_on(app, MenuId::HostOnline, yes));
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);                             // the second click did not open the Host panel
            ASSERT_TRUE(app.is_running());
        }
    } TEST_END();

    TEST_CASE("A13.2 Enter pressed twice in a hurry, through the window's own events: the first opens a panel or starts an attempt, the second does nothing (it does not make a room, cycle the map, start the game, or cancel the attempt); after the settling time Enter acts; the name that was typed is written when the program ends") {
        TempDir temp;
        Server server;
        const auto key = [](SDL_Keycode k) { queue_event(key_event(k)); };
        {   // Host an online match
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            app.start_menu().update(0.4f);                                                    // (the first panel has been up for a while)
            key(SDLK_DOWN);
            key(SDLK_DOWN);
            key(SDLK_RETURN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            hall.step(300);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Host);
            ASSERT_TRUE(app.net() == nullptr && server.mgr.list(server.now).empty());        // no room was made
            ASSERT_EQ(app.start_menu().settings().host_map, 4);                              // and the map under the cursor (Treasure, the default) was not cycled by the second Enter
            app.start_menu().update(0.4f);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().settings().host_map, 5);                              // Enter of its own does cycle it (Islands)
        }
        {   // Single player
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            app.start_menu().update(0.4f);
            key(SDLK_RETURN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);                          // the game was not started
            ASSERT_EQ(app.start_menu().seat(1), SeatChoice::Empty);
            // Esc, Esc leaves the panel and does not leave the program
            app.start_menu().update(0.4f);
            key(SDLK_ESCAPE);
            key(SDLK_ESCAPE);
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
        }
        {   // Join: the Enter that joins and a second one: the attempt goes on and the server answers (it used to be cancelled, silently)
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "c.ini"))));
            app.start_menu().update(0.4f);
            key(SDLK_DOWN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
            queue_event(text_event("abc-1"));
            app.start_menu().update(0.4f);
            key(SDLK_RETURN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
            ASSERT_HAS(app.start_menu().message(), "no room with the code abc-1");           // the server's own answer, not a cancel
        }
        {   // the name that was typed and not written yet is written when the program ends (the window's close button), not at every key
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "d.ini"))));
            app.start_menu().update(0.4f);
            key(SDLK_DOWN);
            key(SDLK_RETURN);
            app.run_frame_with_delta(0.016f);
            key(SDLK_UP);                                                                      // the name field (its text selected)
            queue_event(text_event("Zed"));
            app.run_frame_with_delta(0.016f);
            ASSERT_EQ(app.start_menu().name(), std::string("Zed"));
            ASSERT_TRUE(read_file(temp.file("d.ini")).find("name=Zed") == std::string::npos);   // not yet: the field was not left
            SDL_Event quit;
            std::memset(&quit, 0, sizeof(quit));
            quit.type = SDL_QUIT;
            queue_event(quit);
            app.run_frame_with_delta(0.016f);
            ASSERT_FALSE(app.is_running());
            ASSERT_HAS(read_file(temp.file("d.ini")), "name=Zed");
        }
        {   // an input method's composition in progress is not text: only what it commits (SDL_TEXTINPUT) is
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "e.ini"))));
            click(app, MenuId::JoinWithCode);
            ASSERT_EQ(app.start_menu().selected(), MenuId::Code);
            SDL_Event editing;
            std::memset(&editing, 0, sizeof(editing));
            editing.type = SDL_TEXTEDITING;
            std::snprintf(editing.edit.text, sizeof(editing.edit.text), "%s", "ni");
            editing.edit.length = 2;
            app.handle_menu_event(editing);
            ASSERT_EQ(app.start_menu().code(), std::string(""));
            app.handle_menu_event(text_event("\xE4\xBD\xA0"));                                  // the committed character: not ASCII, refused with the line
            ASSERT_EQ(app.start_menu().code(), std::string(""));
            ASSERT_EQ(app.start_menu().message(), std::string(StartMenu::kRefusedCharsText));
        }
    } TEST_END();

    TEST_CASE("A13.3 What a menu game leaves behind: Leave of a single-player game ends the program (the original's way), and a network game that ends takes everything with it: the quit dialog and the in-match windows, the attempt that was left at its room, the reason of a lost match, the seat that was played") {
        TempDir temp;
        {   // Leave of the setup screen and the quit dialog's Yes of a single-player game end the program, with the menu or without
            Server server;
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_FALSE(app.network_active());
            app.map_select().handle_key_down(SDLK_q);                                         // Leave
            ASSERT_FALSE(app.is_running());
        }
        {
            Server server;
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            app.map_select().handle_key_down(SDLK_RETURN);                                    // START
            ASSERT_EQ(app.state(), AppState::Playing);
            for (int i = 0; i < 160; ++i) app.update_simulation(0.05f);                      // the "get ready" dialog is over
            SDL_KeyboardEvent q_ev{};
            q_ev.type = SDL_KEYDOWN;
            q_ev.keysym.sym = SDLK_q;
            q_ev.keysym.mod = KMOD_LCTRL;
            app.handle_key_down(q_ev);
            ASSERT_TRUE(app.hud().is_quit_dialog_open());
            SDL_KeyboardEvent y_ev{};
            y_ev.type = SDL_KEYDOWN;
            y_ev.keysym.sym = SDLK_y;
            app.handle_key_down(y_ev);
            ASSERT_FALSE(app.is_running());                                                    // (the menu is for network games: a single-player game ends the program as the original does)
        }
        for (const bool holds : {false, true}) {   // the room closes under a player who sits at seat 1 with the quit dialog, the match's quick help and the options open: back at the menu with all of them closed, seat 0 again (a server that holds no seats says "lost"; the default's answer is the way back's refusal)
            Server server(4, kAllMaps, holds);
            Application app;
            Peer ann;
            Hall hall{&server, &app, {&ann}, nullptr, false};
            ASSERT_TRUE(ann.join_creating("127.0.0.1", server.port(), "Ann", "gone0001", "TINY.LVL", 2));
            ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, holds ? "c_holds.ini" : "c.ini"))));
            menu_join(app, "Bob", "gone0001");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && ann.net.phase() == net::NetGame::Phase::Playing; }, 20000));
            hall.step(1000);
            ASSERT_EQ(app.local_player_id(), 1);
            ASSERT_EQ(app.hud().local_player_id(), 1);
            app.hud().open_quit_dialog();
            app.hud().open_quick_help();
            app.hud().open_options();
            ASSERT_TRUE(app.hud().is_quit_dialog_open() && app.hud().is_quick_help_open() && app.hud().is_options_open());
            ASSERT_TRUE(server.mgr.close_room("gone0001", server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 12000));
            ASSERT_FALSE(app.hud().is_quit_dialog_open());                                    // (a dialog of the match that is gone would be on the next match's screen)
            ASSERT_FALSE(app.hud().is_quick_help_open());
            ASSERT_FALSE(app.hud().is_options_open());
            ASSERT_EQ(app.local_player_id(), 0);                                              // the seat that was played is not the seat of the next game
            ASSERT_EQ(app.hud().local_player_id(), 0);
            ASSERT_TRUE(app.net_notice().empty());                                            // the reason went to the first panel (below) and is not kept
            ASSERT_HAS(app.start_menu().message(), holds ? "The match is over." : "lost");
        }
        for (const bool holds : {false, true}) {   // a room that fills while its code is on the screen starts the match; leaving it must not leave the attempt behind (the menu was told "the connection was lost" a frame later)
            Server server(4, kAllMaps, holds);
            Application app;
            Peer guest;
            Hall hall{&server, &app, {&guest}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, holds ? "e_holds.ini" : "e.ini"))));
            menu_host(app, "Hostess", 0, 1);                                                   // Tiny, two players
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            ASSERT_TRUE(guest.join("127.0.0.1", server.port(), "Guest", shown_code(app)));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 20000));
            hall.step(1000);
            ASSERT_TRUE(server.mgr.close_room(server.mgr.list(server.now)[0].code, server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 12000));
            hall.step(500);                                                                    // frames at the menu: nothing of the attempt is left to say anything
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_HAS(app.start_menu().message(), holds ? "The match is over." : "lost");          // (the match's notice, not a failure of the attempt)
            ASSERT_TRUE(app.net() == nullptr);
        }
    } TEST_END();

    TEST_CASE("A13.4 A session that is over is not a room: the setup screen, the room's panel and a join in progress each go back to the menu with the line of their own, for Failed and for Over (Over comes only from a match that is lost, so the test puts the session there); Enter on 'Continue to the room' needs the room") {
        TempDir temp;
        Server server;
        for (const net::NetGame::Phase dead : {net::NetGame::Phase::Failed, net::NetGame::Phase::Over}) {   // under the setup screen
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "a.ini"))));
            const std::string code = dead == net::NetGame::Phase::Failed ? "dead0002" : "dead0003";                   // (a room of its own for each: the server keeps the first one)
            ASSERT_TRUE(server.make_room(code, 4));
            menu_join(app, "Dave", code);
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            app.net()->force_phase_for_test(dead);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.net() == nullptr && app.is_running());
            hall.step(100);
        }
        for (const net::NetGame::Phase dead : {net::NetGame::Phase::Failed, net::NetGame::Phase::Over}) {   // on the room's panel
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "b.ini"))));
            menu_host(app, "Hostess", 0, 1);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            app.net()->force_phase_for_test(dead);
            app.pump_network(0.01f);
            ASSERT_TRUE(failed_on(app, MenuPanel::Host));
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
        }
        {   // a join in progress whose session is over (Over, or no session at all): "lost before you were in the room", not "cannot reach"
            Application app;
            RawServer raw;
            Hall hall{nullptr, &app, {}, &raw, false};
            ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "c.ini"))));
            menu_join(app, "Dave", "room-x");
            ASSERT_TRUE(hall.until([&]() { return app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Connecting && raw.accepted == 1; }, 8000));
            app.net()->force_phase_for_test(net::NetGame::Phase::Over);
            app.pump_network(0.01f);
            ASSERT_TRUE(failed_on(app, MenuPanel::Join));
            ASSERT_HAS(app.start_menu().message(), "was lost before you were in the room");
            ASSERT_TRUE(app.start_menu().message().find("Cannot reach") == std::string::npos);
            ASSERT_TRUE(app.net() == nullptr);
        }
        {   // "Continue to the room" with the match already loading: the room is no longer the screen to go to
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "d.ini"))));
            menu_host(app, "Hostess", 0, 1);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            app.net()->force_phase_for_test(net::NetGame::Phase::Loading);
            click(app, MenuId::EnterRoom);
            app.pump_network(0.0f);
            ASSERT_EQ(app.state(), AppState::StartMenu);                                      // nothing happened
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Room);
            app.net()->force_phase_for_test(net::NetGame::Phase::Room);
            click(app, MenuId::EnterRoom);
            app.pump_network(0.0f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                      // with the room it does
        }
    } TEST_END();

    TEST_CASE("A13.5 Time limits and silence: the player's limit is 20 seconds for a lookup that never ends, a server that accepts and never says Welcome ends after the room's 10 seconds; both say 'The server <name> did not answer' (one rule), a server that hangs up says 'lost before you were in the room', and NetGame names the difference (NoAnswer, Lost)") {
        TempDir temp;
        ASSERT_EQ(ApplicationConfig{}.menu_connect_timeout_ms, 20000u);                       // the production limit is the default of the config (the tests below do not set it)
        {   // a name that is never resolved: 20 s of game time, not 5 and not 60
            Application app;
            const auto lookup = std::make_shared<Lookup>();
            Hall hall{nullptr, &app, {}, nullptr, false};
            ApplicationConfig cfg = menu_config("never.example.org:4001", ini(temp, "a.ini"));
            cfg.host_resolver = lookup_of(lookup);
            ASSERT_TRUE(app.init(cfg));
            menu_join(app, "Dave", "abc-1");
            uint32_t waited = 0;
            while (waited < 30000 && !failed_on(app, MenuPanel::Join)) {
                hall.step(10);
                waited += 10;
            }
            ASSERT_TRUE(waited >= 19900 && waited <= 20300);
            ASSERT_HAS(app.start_menu().message(), "The server never.example.org:4001 did not answer");
            lookup->release = true;
        }
        {   // a server that accepts and stays silent: the lobby's welcome limit (10 s) ends it, and the line is the same
            Application app;
            RawServer raw;
            raw.mode = RawServer::Mode::Silent;
            Hall hall{nullptr, &app, {}, &raw, false};
            ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "b.ini"))));
            menu_join(app, "Dave", "abc-1");
            uint32_t waited = 0;
            while (waited < 30000 && !failed_on(app, MenuPanel::Join)) {
                hall.step(10);
                waited += 10;
            }
            ASSERT_TRUE(waited >= 9900 && waited <= 14000);                                    // (the game's clock runs ahead of the socket: the connection takes some of it)
            ASSERT_HAS(app.start_menu().message(), "did not answer");
            ASSERT_TRUE(app.start_menu().message().find("lost") == std::string::npos);
            ASSERT_EQ(raw.hellos.size(), static_cast<size_t>(1));                             // the guest asked for the room and named itself, for any seat (255)
            ASSERT_TRUE(raw.hellos[0].want_seat == 255 && raw.hellos[0].name == "Dave" && raw.hellos[0].room == "abc-1");
        }
        {   // NetGame alone: the silent server is NoAnswer after ten seconds of the guest's clock; the one that hangs up is Lost at once
            RawServer raw;
            Peer p;
            Hall hall{nullptr, nullptr, {&p}, &raw, false};
            using FailReason = net::NetGame::FailReason;
            raw.mode = RawServer::Mode::Silent;
            ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
            const uint32_t begun = p.now;
            ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 20000));
            ASSERT_TRUE(p.net.fail_reason() == FailReason::NoAnswer);
            ASSERT_TRUE(p.now - begun >= 9900 && p.now - begun <= 14000);
            p.net.leave();
            raw.mode = RawServer::Mode::HangUp;
            ASSERT_TRUE(p.join("127.0.0.1", raw.port(), "Ann", "room-x"));
            ASSERT_TRUE(hall.until([&]() { return p.net.phase() == net::NetGame::Phase::Failed; }, 20000));
            ASSERT_TRUE(p.net.fail_reason() == FailReason::Lost);
        }
        {   // the player's limit also covers a server that has accepted and says nothing (here a limit of 3 s, shorter than the room's welcome): the attempt ends at the player's limit
            Application app;
            RawServer raw;
            raw.mode = RawServer::Mode::Silent;
            Hall hall{nullptr, &app, {}, &raw, false};
            ApplicationConfig cfg = menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "b3.ini"));
            cfg.menu_connect_timeout_ms = 3000;
            ASSERT_TRUE(app.init(cfg));
            menu_join(app, "Dave", "abc-1");
            uint32_t waited = 0;
            while (waited < 30000 && !failed_on(app, MenuPanel::Join)) {
                hall.step(10);
                waited += 10;
            }
            ASSERT_TRUE(waited >= 2900 && waited <= 8000);                                     // not the room's 10 s
            ASSERT_HAS(app.start_menu().message(), "did not answer");
            ASSERT_TRUE(app.net() == nullptr);
        }
        {   // a server that hangs up before the Welcome: the line of a lost connection
            Application app;
            RawServer raw;
            raw.mode = RawServer::Mode::HangUp;
            Hall hall{nullptr, &app, {}, &raw, false};
            ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), ini(temp, "c.ini"))));
            menu_join(app, "Dave", "abc-1");
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
            ASSERT_HAS(app.start_menu().message(), "was lost before you were in the room");
        }
    } TEST_END();

    TEST_CASE("A13.6 A lookup that cannot start its thread says so (not 'the name is not known'), the player of a bot seat at the setup screen is named before START, and a server that does not offer the map that the player chose is told, and the room is left") {
        TempDir temp;
        {   // the system has no thread to give
            Application app;
            ApplicationConfig cfg = menu_config("somewhere.example.org:4001", ini(temp, "a.ini"));
            cfg.host_resolver = [](const std::string&, std::string&, std::string&) { return false; };
            cfg.host_launcher = [](std::function<void()>) { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); };
            ASSERT_TRUE(app.init(cfg));
            menu_join(app, "Dave", "abc-1");
            app.pump_network(0.01f);
            ASSERT_TRUE(failed_on(app, MenuPanel::Join));
            ASSERT_HAS(app.start_menu().message(), "Could not start the lookup of somewhere.example.org");
            ASSERT_TRUE(app.start_menu().message().find("not known") == std::string::npos);
            ASSERT_TRUE(app.net() == nullptr && app.is_running());
        }
        {   // Continue with two bots: the setup screen names them before START (the original's single-player game keeps its names)
            Server server;
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), ini(temp, "b.ini"));
            cfg.team_names[1] = "Zed";                                                         // (--team-name: a name that the command line gave to a seat)
            ASSERT_TRUE(app.init(cfg));
            click(app, MenuId::Single);
            click(app, MenuId::Seat2);                                                         // Blue: Easy
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);                                                         // Black: Hard
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                       // (before START)
            ASSERT_EQ(app.sim().get_player_name(2), std::string("Bot (Easy)"));
            ASSERT_EQ(app.sim().get_player_name(3), std::string("Bot (Hard)"));
            ASSERT_EQ(app.sim().get_player_name(1), std::string("Zed"));                       // a seat without a bot keeps what the command line named it
        }
        {   // a server whose public rooms are on Tiny and Small only, and the player asked for Islands (the menu offers the six maps of the game)
            Server server(4, {"TINY.LVL", "SMALL.LVL"});
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), ini(temp, "c.ini"))));
            menu_host(app, "Hostess", 5, 2);                                                   // Islands, three players
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
            ASSERT_HAS(app.start_menu().message(), "does not offer the Islands map");
            ASSERT_HAS(app.start_menu().message(), "TINY.LVL");                               // what the server would have played instead
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
            ASSERT_EQ(app.start_menu().settings().host_map, 5);                               // the choice stays: the player picks another map and goes on
            ASSERT_TRUE(hall.until([&]() { return server.mgr.list(server.now).empty() || server.mgr.list(server.now)[0].joined == 0; }, 4000));   // (the player left the room again)
            click(app, MenuId::HostMap);                                                       // Islands -> Tiny
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));  // a map that the server offers is made
            click(app, MenuId::Back);
            hall.step(100);
            click(app, MenuId::HostMap);                                                       // Tiny -> Small: offered too
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        }
    } TEST_END();

    TEST_CASE("A14.1 Host an online match with Medium bots in the Red, Blue and Black seats: the choice reaches the application and the room's network before the connection is made, the room's panel says what START will do, the leader's screen says it on the status line, one person alone starts the match with START and the server seats three \"Bot (Medium)\" (they play), and the choice is remembered for the next run") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        Server server;
        std::string code;
        {
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            ASSERT_EQ(app.fill_bots(), net::FillLevel::None);                                  // nothing is seated unless the player chose it
            click(app, MenuId::HostOnline);
            for (const MenuId seat : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
                click(app, seat);                                                              // Leave empty -> Easy bot
                click(app, seat);                                                              // -> Medium bot
            }
            fill(app, MenuId::HostName, "Solo");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            code = shown_code(app);
            const net::FillPlan medium = fill_of(net::FillLevel::Medium, net::FillLevel::Medium, net::FillLevel::Medium);
            ASSERT_TRUE(app.fill_bots() == medium && app.net() != nullptr && app.net()->fill_bots() == medium);      // (the leader's own seat gets nothing)
            bool says = false;
            for (const MenuElement& e : app.start_menu().elements()) says = says || e.text == "Empty seats will be Medium bots.";
            ASSERT_TRUE(says);                                                                 // the room's panel: what START will do
            click(app, MenuId::EnterRoom);
            hall.step(700);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            ASSERT_TRUE(app.map_select().leads_server_room());
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
            const size_t channels = app.audio_mixer().active_channel_count();
            app.room_key_down(SDLK_s, 0, false);                                               // START, alone: one person is enough with a fill
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels);                     // (no can't-go cue)
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
            const server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.expected == 4 && s.bots.size() == 3);
            ASSERT_TRUE(s.names[0] == "Solo" && s.names[1] == bn(code, 1, "Medium") && s.names[2] == bn(code, 2, "Medium") && s.names[3] == bn(code, 3, "Medium"));
            ASSERT_TRUE(s.bots[0].fill && s.bots[0].level == "medium" && s.bots[0].kind == "standard");
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            hall.step(net::kMatchStartDelayMs + 4000);                                         // (the server seals the first turn kMatchStartDelayMs after the match began: 4 s of play follow)
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(server.status(code).ticks > 40);
        }
        ASSERT_HAS(read_file(settings), "host_fill=none,medium,medium,medium\n");              // written when the choice changed
        {   // the next run: the Host panel shows it
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            ASSERT_TRUE(app.start_menu().settings().host_fill == fill_of(net::FillLevel::Medium, net::FillLevel::Medium, net::FillLevel::Medium));
            click(app, MenuId::HostOnline);
            for (const MenuId seat : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
                MenuElement e;
                ASSERT_TRUE(app.start_menu().find_element(seat, e) && e.value == "Medium bot");
            }
        }
        {   // a file of an earlier version has one word for every seat: the three seats after the leader's
            const std::string old_file = ini(temp, "old.ini");
            {
                std::ofstream out(old_file, std::ios::binary | std::ios::app);
                out << "host_fill=hard\n";
            }
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), old_file)));
            ASSERT_TRUE(app.start_menu().settings().host_fill == fill_of(net::FillLevel::Hard, net::FillLevel::Hard, net::FillLevel::Hard));
        }
    } TEST_END();

    TEST_CASE("A14.2 The fill belongs to the Host panel: --start-menu --fill-bots hard makes the three rows start at Hard bots, --fill-bots none,easy,none,hard each at its own level, --teams the Teams row (nothing is written until the player changes it); a player who JOINS fills nothing, whatever the panel said before (only the leader's START seats bots, and the choice is its host's)") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        {
            std::vector<std::string> args = {"ants", "--headless", "--start-menu", "--fill-bots", "hard", "--settings", settings};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_TRUE(cfg.startup_error.empty() && cfg.start_menu && cfg.fill_bots == net::FillLevel::Hard);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_menu().settings().host_fill == fill_of(net::FillLevel::Hard, net::FillLevel::Hard, net::FillLevel::Hard));
            click(app, MenuId::HostOnline);
            for (const MenuId seat : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
                MenuElement e;
                ASSERT_TRUE(app.start_menu().find_element(seat, e) && e.value == "Hard bot");
            }
        }
        {   // four words: a level for each seat (the first is the leader's and counts for nothing); --teams starts the Host panel's Teams row too
            std::vector<std::string> args = {"ants", "--headless", "--start-menu", "--fill-bots", "none,easy,none,hard", "--teams", "0+2", "--settings", settings};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_TRUE(cfg.startup_error.empty() && cfg.start_menu && cfg.fill_bots == fill_of(net::FillLevel::Easy, net::FillLevel::None, net::FillLevel::Hard) && cfg.teams.set);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            click(app, MenuId::HostOnline);
            const std::vector<std::pair<MenuId, std::string>> rows = {{MenuId::HostSeat1, "Easy bot"}, {MenuId::HostSeat2, "Leave empty"}, {MenuId::HostSeat3, "Hard bot"},
                                                                       {MenuId::HostTeams, "Green + Blue against Red + Black"}};
            for (const auto& row : rows) {
                MenuElement e;
                ASSERT_TRUE(app.start_menu().find_element(row.first, e) && e.value == row.second);
            }
        }
        ASSERT_TRUE(read_file(settings).find("host_fill") == std::string::npos && read_file(settings).find("host_teams") == std::string::npos);
        Server server;
        ASSERT_TRUE(server.make_room("JOIN-FILL", 4));
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
        click(app, MenuId::HostOnline);
        for (const MenuId seat : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
            click(app, seat);
            click(app, seat);
            click(app, seat);                                                                  // Hard bots on the Host panel
        }
        const net::FillPlan hard = fill_of(net::FillLevel::Hard, net::FillLevel::Hard, net::FillLevel::Hard);
        ASSERT_TRUE(app.start_menu().settings().host_fill == hard);
        click(app, MenuId::Host);                                                              // a room is made with it: the application's fill is Hard now
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        ASSERT_TRUE(app.fill_bots() == hard && app.net() != nullptr && app.net()->fill_bots() == hard);
        click(app, MenuId::Back);                                                              // the room is left again
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Host) && app.net() == nullptr; }, 8000));
        press(app, SDLK_ESCAPE);
        menu_join(app, "Joiner", "JOIN-FILL");                                                 // ... and then a join, the first player of a room: its leader
        ASSERT_TRUE(hall.until([&]() { return app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::None && app.net()->fill_bots() == net::FillLevel::None);
        to_setup_screen(app);
        hall.step(700);
        ASSERT_EQ(app.map_select().room().status, std::string("Press START when all players' thumbs have appeared."));
        const size_t channels = app.audio_mixer().active_channel_count();
        app.room_key_down(SDLK_s, 0, false);                                                   // alone and without a fill: the can't-go cue, as ever
        ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);
        hall.step(1000);
        ASSERT_TRUE(server.status("JOIN-FILL").state == server::RoomState::Waiting && server.status("JOIN-FILL").bots.empty());
    } TEST_END();

    TEST_CASE("A14.3 Leave Game Takes The 16:9 Setup Screen's Chat Box And Fill Footer With It: A Leader In The Room (Host Panel, Medium Bots, A Line Typed) Presses Q, Is Back At The Menu, And The Next Screen Of This Run Has No Chat Panel, No Typed Line And No Footer; The Next Room Starts With An Empty Box") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), settings);
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        ASSERT_TRUE(app.init(cfg));
        for (int round = 0; round < 2; ++round) {
            click(app, MenuId::HostOnline);
            if (round == 0) {
                for (const MenuId seat : {MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3}) {
                    click(app, seat);                                                          // Leave empty -> Easy bot
                    click(app, seat);                                                          // -> Medium bot
                }
                fill(app, MenuId::HostName, "Solo");
            }
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            click(app, MenuId::EnterRoom);
            hall.step(700);
            ASSERT_TRUE(app.state() == AppState::MapSelect && app.map_select().leads_server_room() && app.map_select().wide_layout());
            ASSERT_TRUE(app.map_select().chat_panel().visible && app.map_select().chat_panel().lines.empty());     // (the second room starts with an empty box)
            ASSERT_EQ(app.map_select().fill_footer()[1], std::string("Medium bots"));
            app.room_key_down(SDLK_t, 0, false);
            app.room_text_input("t");
            app.room_text_input("half a line");
            hall.step(30);
            ASSERT_EQ(app.map_select().chat_panel().typed, std::string("half a line"));
            app.room_key_down(SDLK_ESCAPE, 0, false);
            ASSERT_TRUE(app.net()->chat("only the leader here"));
            ASSERT_TRUE(hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
            hall.step(500);
            app.room_key_down(SDLK_q, 0, false);                                               // Leave Game: back at the menu, the room is gone
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu && app.net() == nullptr; }, 8000));
            ASSERT_FALSE(app.map_select().chat_panel().visible);
            ASSERT_TRUE(app.map_select().chat_panel().lines.empty() && app.map_select().chat_panel().typed.empty() && !app.map_select().chat_panel().caret);
            ASSERT_TRUE(app.map_select().fill_footer()[0].empty() && app.map_select().fill_footer()[1].empty());
            ASSERT_FALSE(app.room_chat().is_open());
            ASSERT_TRUE(app.room_chat_box() == nullptr);
        }
    } TEST_END();

    TEST_CASE("A14.4 Host an online match with a level for each seat and the teams: the Host panel's rows and Teams row reach the application and the room's network, the room's panel and the leader's screen say both, one person alone starts the match, the server seats an Easy bot in the Red seat and a Hard bot in the Black seat, leaves the Blue seat empty, and the match starts with Green and Red as a team (the server's referee and the leader's own engine agree), and a Join makes no teams") {
        TempDir temp;
        const std::string settings = ini(temp, "s.ini");
        Server server;
        std::string code;
        const LocalTeams green_red{true, 0, 1};
        {
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            click(app, MenuId::HostOnline);
            click(app, MenuId::HostSeat1);                                                     // Red: Easy bot
            for (int i = 0; i < 3; ++i) click(app, MenuId::HostSeat3);                         // Black: Hard bot; Blue stays empty
            click(app, MenuId::HostTeams);                                                     // Green + Red against Blue + Black
            fill(app, MenuId::HostName, "Solo");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            code = shown_code(app);
            const net::FillPlan plan = fill_of(net::FillLevel::Easy, net::FillLevel::None, net::FillLevel::Hard);
            ASSERT_TRUE(app.fill_bots() == plan && app.net() != nullptr && app.net()->fill_bots() == plan);
            // the teams are the create block's (the room makes them every time, a full room's automatic start too), not the START's choice: this player's own START teams stay free for all
            ASSERT_TRUE(code.size() == kRoomCodeChars && server.status(code).room_teams == "0+1" && server.status(code).expected == 4);
            ASSERT_TRUE(app.net()->room_teams() == green_red && app.net()->effective_teams() == green_red && !app.start_teams().set && !app.net()->start_teams().set);
            bool seats = false;
            bool teams = false;
            for (const MenuElement& e : app.start_menu().elements()) {
                seats = seats || e.text == "At START: Red gets an Easy bot, Black a Hard bot.";
                teams = teams || e.text == "Room teams: Green + Red against Blue + Black.";
            }
            ASSERT_TRUE(seats && teams);                                                       // the room's panel: what START will do
            click(app, MenuId::EnterRoom);
            hall.step(700);
            ASSERT_TRUE(app.state() == AppState::MapSelect && app.map_select().leads_server_room());
            const std::string status = app.map_select().room().status;                         // (the plan in the longest way that fits the label)
            ASSERT_TRUE(status.find("Easy") != std::string::npos && status.find("Hard") != std::string::npos && status.find("Green + Red") != std::string::npos);
            app.room_key_down(SDLK_s, 0, false);                                               // START, alone
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
            const server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 2);
            ASSERT_TRUE(s.names[0] == "Solo" && s.names[1] == bn(code, 1, "Easy") && s.names[3] == bn(code, 3, "Hard"));
            ASSERT_EQ(app.sim().roster_mask(), 0x0B);                                          // Green, Red and Black play
            ASSERT_EQ(s.teams, std::string("0+1"));
            ASSERT_EQ(s.room_teams, std::string("0+1"));                                       // (the room's own: its code named them)
            ASSERT_TRUE(s.allies[0] == 1 && s.allies[1] == 0 && s.allies[2] == sim::ALLIANCE_NONE && s.allies[3] == sim::ALLIANCE_NONE);       // the referee
            ASSERT_TRUE(app.sim().alliance_of(0) == 1 && app.sim().alliance_of(1) == 0 && app.sim().alliance_of(3) == sim::ALLIANCE_NONE);     // the leader's engine
            hall.step(net::kMatchStartDelayMs + 4000);
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(server.status(code).ticks > 40);
        }
        ASSERT_HAS(read_file(settings), "host_fill=none,easy,none,hard\n");
        ASSERT_HAS(read_file(settings), "host_teams=0+1\n");
        {   // the next run shows both on the Host panel; a Join makes no teams and fills nothing, whatever the panel said
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            click(app, MenuId::HostOnline);
            const std::vector<std::pair<MenuId, std::string>> rows = {{MenuId::HostSeat1, "Easy bot"}, {MenuId::HostSeat2, "Leave empty"}, {MenuId::HostSeat3, "Hard bot"},
                                                                       {MenuId::HostTeams, "Green + Red against Blue + Black"}};
            for (const auto& row : rows) {
                MenuElement e;
                ASSERT_TRUE(app.start_menu().find_element(row.first, e) && e.value == row.second);
            }
            ASSERT_TRUE(server.make_room("JOIN-TEAMS", 4));
            press(app, SDLK_ESCAPE);
            menu_join(app, "Joiner", "JOIN-TEAMS");
            ASSERT_TRUE(hall.until([&]() { return app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            ASSERT_TRUE(app.fill_bots() == net::FillLevel::None && !app.start_teams().set && !app.net()->start_teams().set);
        }
    } TEST_END();
}
