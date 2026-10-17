// Tests of the network in the application: the chat box of the 16:9 setup screen and the one card of the front page
#include "network_app_fixtures.hpp"


// ---- the chat box of the 16:9 setup screen (online rooms, protocol 11): the application feeds MapSelectScreen::set_chat_panel / set_fill_footer from the room (N5.60 - N5.68) ----
void run_room_chat_box_tests() {
    using SL = SetupLayout;
    const auto centre_x = [](const LayoutRect& r) { return r.x + r.w / 2; };
    const auto centre_y = [](const LayoutRect& r) { return r.y + r.h / 2; };
    // The application of a test in the 16:9 picture (960 x 540, its window the same size so that the pixels are the picture's: a frame is run to be read back, so the headless
    // application is told to take a screenshot that never comes instead of stopping after ten frames) or in the original's 4:3 one
    const auto room_config = [](const Server& server, const std::string& room, const std::string& name, Aspect aspect, net::FillLevel fill, float zoom_level = 1.0f, bool zoom_given = false) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        cfg.aspect = aspect;
        cfg.aspect_given = true;
        cfg.fill_bots = fill;
        cfg.zoom = zoom_level;                                                                // (the remembered level of the mouse-wheel zoom: the meeting of the rooms and the zoom, N5.68 - N5.71)
        cfg.zoom_given = zoom_given;
        cfg.screenshot_path = (std::filesystem::temp_directory_path() / ("ants_box_" + room + ".png")).string();
        cfg.screenshot_frames = 1000000000;
        if (aspect == Aspect::Wide16x9) {
            cfg.has_window_size = true;
            cfg.window_w = 960;
            cfg.window_h = 540;
        }
        return cfg;
    };
    // the application is the room's leader (the first to join), Bob a bare machine who joins second; the room is TINY, 4 seats, with Fog of War when `fog`
    struct BoxRoom {
        Server server;
        Application app;
        Peer peer;
        Hall hall{server, &app, {&peer}};
    };
    const auto open_leader = [&](BoxRoom& d, const std::string& code, Aspect aspect, net::FillLevel fill, bool fog, bool with_bob, float zoom_level = 1.0f, bool zoom_given = false, sim::StartTeams room_teams = {}) -> bool {
        server::RoomSpec spec;
        spec.code = code;
        spec.map = "TINY.LVL";
        spec.players = 4;
        spec.fog = fog;
        spec.teams = room_teams;                                                             // (the room's own teams: what a create block gives a room that a Hello makes; none: the leader's START chooses)
        spec.has_seed = true;
        spec.seed = 4242;
        if (d.server.listener == nullptr || !d.server.mgr.create_room(spec, d.server.now).ok) return false;
        if (!d.app.init(room_config(d.server, code, "Ana", aspect, fill, zoom_level, zoom_given))) return false;
        if (!d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000)) return false;
        if (with_bob) {
            if (!d.peer.net.join("127.0.0.1", d.server.port(), "Bob", 255, code)) return false;
            if (!d.hall.until([&]() { return d.peer.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000)) return false;
        }
        d.hall.step(700);                                                                    // (the setup screen has shown its labels: the refresh of 500 ms)
        return true;
    };
    // the application is a GUEST: a bare machine (named Lea) joined first and leads the room, the application joins second
    const auto open_guest = [&](BoxRoom& d, const std::string& code, Aspect aspect, net::FillLevel fill, float zoom_level = 1.0f, bool zoom_given = false) -> bool {
        server::RoomSpec spec;
        spec.code = code;
        spec.map = "TINY.LVL";
        spec.players = 4;
        spec.has_seed = true;
        spec.seed = 4242;
        if (d.server.listener == nullptr || !d.server.mgr.create_room(spec, d.server.now).ok) return false;
        Hall before{d.server, nullptr, {&d.peer}};
        if (!d.peer.net.join("127.0.0.1", d.server.port(), "Lea", 255, code)) return false;
        if (!before.until([&]() { return d.peer.net.phase() == net::NetGame::Phase::Room; }, 8000)) return false;
        if (!d.app.init(room_config(d.server, code, "Ben", aspect, fill, zoom_level, zoom_given))) return false;
        if (!d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->my_seat() == 1 && d.peer.net.room().slots[1].rtt_ms != net::kRttUnknown; }, 8000)) return false;
        d.hall.step(700);
        return true;
    };
    const auto panel_lines = [](Application& app) {
        std::vector<std::string> out;
        for (const MapSelectScreen::ChatPanel::Line& l : app.map_select().chat_panel().lines) out.push_back((l.notice ? "! " : "") + l.text);
        return out;
    };
    // the pixels of a rectangle of the picture (the renderer's canvas is the picture: the window is 960 x 540), as a hash
    const auto region_hash = [](Application& app, const LayoutRect& r) {
        SDL_Rect rect{r.x, r.y, r.w, r.h};
        std::vector<uint8_t> px(static_cast<size_t>(r.w) * static_cast<size_t>(r.h) * 4u, 0);
        if (SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), &rect, SDL_PIXELFORMAT_RGBA32, px.data(), r.w * 4) != 0) return uint64_t{0};
        uint64_t h = 1469598103934665603ull;
        for (const uint8_t b : px) h = (h ^ b) * 1099511628211ull;
        return h;
    };
    const auto lines = [](const std::vector<net::ChatLine>& v) {
        std::vector<std::string> out;
        for (const net::ChatLine& l : v) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
        return out;
    };

    TEST_CASE("N5.60 The 16:9 Setup Screen Of A Room's Leader Has A Chat Box Fed From The Room: The Lines With Their Names Arrive In It (Own Line Included, Oldest First, The Newest 16 At Most), The Typed Line And Its Caret Are In The Input Box, The Status Line Keeps The Prompt (No Copy Of The Chat, No \"Say:\", No \"You:\")") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-LEAD", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        ASSERT_TRUE(app.map_select().wide_layout() && app.map_select().setup_variant() == SetupVariant::Online);
        ASSERT_TRUE(app.room_chat_box() == &SL::of(SetupVariant::Online).chat);
        ASSERT_TRUE(app.map_select().chat_panel().visible);                                     // (the box is drawn from the first frame of the room: its frame and label)
        ASSERT_TRUE(app.map_select().chat_panel().lines.empty() && app.map_select().chat_panel().typed.empty() && !app.map_select().chat_panel().caret);
        d.hall.step(5000);                                                                      // (the first five seconds of company say how to change a color: the START prompt is back after them)
        const std::string prompt = app.map_select().room().status;
        ASSERT_EQ(prompt, std::string(sim::strings::text(sim::strings::kPressStart)));
        // Bob speaks: the line is in the box with his name; the status line is not changed by it
        ASSERT_TRUE(d.peer.net.chat("hello leader"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Bob: hello leader"}));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, prompt);
        ASSERT_EQ(app.net()->status_text(), prompt);
        ASSERT_FALSE(app.map_select().room().status_input);
        // T opens the input: what is typed is in the box (not on the status line), the caret blinks
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("my reply");
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().chat_panel().typed == "my reply" && app.room_chat().is_open());
        ASSERT_TRUE(app.map_select().room().status == prompt && !app.map_select().room().status_input && app.map_select().room().status_prefix.empty());
        bool saw_on = false;
        bool saw_off = false;
        for (int i = 0; i < 80; ++i) {                                                           // (the caret's half period is 150 ms: in 800 ms it is seen on and off)
            d.hall.step(10);
            (app.map_select().chat_panel().caret ? saw_on : saw_off) = true;
            ASSERT_EQ(app.map_select().chat_panel().caret, app.room_chat().caret(static_cast<uint32_t>(app.net_clock_ms())));
        }
        ASSERT_TRUE(saw_on && saw_off);
        // Enter sends it: the room relays it back to its sender too, so it comes into the box with the sender's own name; the input is empty again; "You: ..." is nowhere
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Bob: hello leader", "Ana: my reply"}));
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().chat_panel().typed.empty() && !app.map_select().chat_panel().caret && !app.room_chat().is_open());
        ASSERT_TRUE(app.map_select().room().status == prompt && app.net()->status_text().find("You:") == std::string::npos);
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"1|Bob|hello leader", "0|Ana|my reply"}));
        // the room's talk goes on: only the newest 16 lines are given to the box, the newest last (the box wraps what it gets and shows what fits)
        for (int i = 1; i <= 20; ++i) {
            ASSERT_TRUE(d.peer.net.chat("line " + std::to_string(i)));
            d.hall.step(1100);                                                                  // (a player may say a line a second: the room's own rate limit)
        }
        ASSERT_TRUE(d.hall.until([&]() { return app.net()->pregame_chat().size() == 22; }, 8000));
        d.hall.step(30);
        const std::vector<std::string> shown = panel_lines(app);
        ASSERT_EQ(shown.size(), size_t{16});
        ASSERT_EQ(shown.front(), std::string("Bob: line 5"));
        ASSERT_EQ(shown.back(), std::string("Bob: line 20"));
        ASSERT_EQ(app.net()->pregame_chat().size(), size_t{22});                                // (the room keeps them all: the match's chat log starts with them)
        ASSERT_TRUE(app.map_select().room().status == prompt);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.61 The Chat Box Styles A Room's Own Notice As A Notice (A Line Of The Server, Not Of A Player: No Name In Front, The Status Text's Smaller Size) And A Player's Line As A Message; The Status Line Has The Prompt Of The Fog Room, Not A Copy Of The Notice") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-NOTE", Aspect::Wide16x9, net::FillLevel::Easy, true, false));
        Application& app = d.app;
        ASSERT_TRUE(app.map_select().chat_panel().visible && app.map_select().chat_panel().lines.empty());
        ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));
        app.room_key_down(SDLK_s, 0, false);                                                    // START of a leader alone with a fill in a fog room: the server says that bots cannot play with fog
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{std::string("! ") + net::kNoticeFillFog}));
        ASSERT_TRUE(app.map_select().chat_panel().lines[0].notice);
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));      // (no copy of the notice on the status line)
        ASSERT_TRUE(d.peer.net.join("127.0.0.1", d.server.port(), "Bob", 255, "BOX-NOTE"));
        ASSERT_TRUE(d.hall.until([&]() { return d.peer.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(d.peer.net.chat("hi"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{std::string("! ") + net::kNoticeFillFog, "Bob: hi"}));
        ASSERT_TRUE(app.map_select().chat_panel().lines[0].notice && !app.map_select().chat_panel().lines[1].notice);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.62 The Click Zones Of The 16:9 Screen: A Click In The Chat Box's Input Box Opens The Chat Input And So Does One In Its Lines Box, A Second Click Closes It (Sends Nothing), Any Other Click While It Is Open Closes It And Is Not The Screen's Click (START Starts Nothing); The Classic Page's Status-Line Zone And The Wide Status Box Open Nothing") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-CLICK", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        const SetupLayout& layout = SL::of(SetupVariant::Online);
        const LayoutRect input = layout.chat.input_box;
        const LayoutRect lines_box = layout.chat.lines;
        const auto quiet = [&]() { return d.server.status("BOX-CLICK").state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
        const auto click = [&](int32_t x, int32_t y) { return app.room_mouse_down(x, y, SDL_BUTTON_LEFT); };
        // a click that is nobody's: just outside the input box on every side, just outside the lines box, between them, the classic status line's zone, the wide status box, the preview, the button
        for (const auto& p : {std::pair<int32_t, int32_t>{input.x - 1, centre_y(input)}, {input.x + input.w, centre_y(input)}, {centre_x(input), input.y - 1}, {centre_x(input), input.y + input.h},
                              {lines_box.x - 1, centre_y(lines_box)}, {lines_box.x + lines_box.w, centre_y(lines_box)}, {centre_x(lines_box), lines_box.y - 1},
                              {MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5}, {centre_x(layout.prompt_text), centre_y(layout.prompt_text)},
                              {centre_x(layout.preview_box), centre_y(layout.preview_box)}, {centre_x(layout.start), centre_y(layout.start)}}) {
            ASSERT_FALSE(click(p.first, p.second));
            ASSERT_FALSE(app.room_chat().is_open());
        }
        ASSERT_FALSE(app.room_mouse_down(centre_x(input), centre_y(input), SDL_BUTTON_RIGHT));     // the right button is no click
        ASSERT_FALSE(app.room_chat().is_open());
        // the input box opens it; the release of that click is the input's
        ASSERT_TRUE(click(centre_x(input), centre_y(input)));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(input), centre_y(input), SDL_BUTTON_LEFT));
        app.room_text_input("typed with a finger");
        d.hall.step(30);
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("typed with a finger"));
        // a second click on the input box closes it and sends nothing
        ASSERT_TRUE(click(centre_x(input), centre_y(input)));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(input), centre_y(input), SDL_BUTTON_LEFT));
        d.hall.step(500);
        ASSERT_TRUE(d.peer.net.pregame_chat().empty() && quiet());
        ASSERT_TRUE(app.map_select().chat_panel().typed.empty());
        // the corners of both boxes are inside them
        for (const LayoutRect& r : {input, lines_box}) {
            for (const auto& p : {std::pair<int32_t, int32_t>{r.x, r.y}, {r.x + r.w - 1, r.y}, {r.x, r.y + r.h - 1}, {r.x + r.w - 1, r.y + r.h - 1}}) {
                ASSERT_TRUE(click(p.first, p.second));
                ASSERT_TRUE(app.room_chat().is_open());
                ASSERT_TRUE(app.room_mouse_up(p.first, p.second, SDL_BUTTON_LEFT));
                ASSERT_TRUE(click(p.first, p.second));                                            // (and closes it again)
                ASSERT_FALSE(app.room_chat().is_open());
                ASSERT_TRUE(app.room_mouse_up(p.first, p.second, SDL_BUTTON_LEFT));
            }
        }
        // a click in the lines box opens it too
        ASSERT_TRUE(click(centre_x(lines_box), centre_y(lines_box)));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(lines_box), centre_y(lines_box), SDL_BUTTON_LEFT));
        ASSERT_TRUE(click(centre_x(lines_box), centre_y(lines_box)));                              // (a second click anywhere closes it)
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(lines_box), centre_y(lines_box), SDL_BUTTON_LEFT));
        // any other click while it is open closes it and is not the screen's: START (and the map list, the classic status zone, the empty clay) start and change nothing
        const int32_t map_before = app.map_select().get_selected_index();
        for (const auto& where : {std::pair<int32_t, int32_t>{centre_x(layout.start), centre_y(layout.start)}, {centre_x(layout.down), centre_y(layout.down)}, {centre_x(layout.prompt_text), centre_y(layout.prompt_text)}, {20, 200}}) {
            ASSERT_TRUE(click(centre_x(input), centre_y(input)));
            ASSERT_TRUE(app.room_chat().is_open());
            ASSERT_TRUE(app.room_mouse_up(centre_x(input), centre_y(input), SDL_BUTTON_LEFT));
            app.room_text_input("words");
            ASSERT_TRUE(click(where.first, where.second));
            ASSERT_FALSE(app.room_chat().is_open());
            ASSERT_FALSE(app.map_select().start_button().pressed());
            ASSERT_TRUE(app.room_mouse_up(where.first, where.second, SDL_BUTTON_LEFT));
            d.hall.step(300);
            ASSERT_TRUE(quiet() && d.peer.net.pregame_chat().empty());
        }
        ASSERT_EQ(app.map_select().get_selected_index(), map_before);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.63 The Focus Rule On The 16:9 Screen: While The Chat Input Is Open S, Q, X, Enter And The Arrows Do Nothing On The Screen And Are Not Typed (The Box Shows Exactly What Was Typed), Enter Sends And Closes, Esc Closes; After It Closes START's Keys Do Nothing For 400 ms; A Held START Key Starts Nothing On A Room's Screen") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-FOCUS", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        const auto quiet = [&]() { return d.server.status("BOX-FOCUS").state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect && app.network_active(); };
        // the keys that must not open it
        app.room_key_down(SDLK_t, KMOD_CTRL, false);
        app.room_key_down(SDLK_t, KMOD_GUI, false);
        app.room_key_down(SDLK_t, 0, true);
        ASSERT_FALSE(app.room_chat().is_open());
        // T opens it and is not typed; the keys of the screen do nothing and do not enter the line
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("t");
        app.room_text_input("hello");
        for (const SDL_Keycode k : {SDLK_s, SDLK_q, SDLK_x, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_TAB}) app.room_key_down(k, 0, false);
        d.hall.step(600);
        ASSERT_TRUE(quiet() && app.room_chat().is_open());
        ASSERT_EQ(app.room_chat().text(), std::string("hello"));
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("hello"));
        // Esc closes it without sending anything; the box is empty again
        app.room_key_down(SDLK_ESCAPE, 0, false);
        d.hall.step(30);
        ASSERT_TRUE(!app.room_chat().is_open() && app.map_select().chat_panel().typed.empty() && d.peer.net.pregame_chat().empty());
        // Enter sends and closes; a second Enter (and the S of a habit) within 400 ms starts nothing
        d.hall.step(500);
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("ready");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(20);
        app.room_key_down(SDLK_RETURN, 0, false);
        app.room_key_down(SDLK_s, 0, false);
        d.hall.step(300);
        ASSERT_TRUE(quiet());
        ASSERT_TRUE(d.hall.until([&]() { return !d.peer.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"0|Ana|ready"}));
        // a held START key (the auto-repeat of Enter, the keypad's Enter, S) is no press on a room's screen, however long it has been held
        d.hall.step(500);
        for (const SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) {
            for (int i = 0; i < 5; ++i) app.room_key_down(k, 0, true);
        }
        d.hall.step(1500);
        ASSERT_TRUE(quiet());
        // and the press of S, 400 ms or more after the input closed, starts the match
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.64 The Fill Footer Of The Leader's Players' Status Box (\"Empty seats at START:\" / \"Medium bots\"): The Leader Sees The Level Of Its Fill (Follows A Change At Once), A Room Without A Fill, A Fog Room (No Bots Can Be Seated), A Guest Whatever Its Own Setting, And The Classic Page Have None") {
        const auto footer = [](Application& app) { return std::vector<std::string>{app.map_select().fill_footer()[0], app.map_select().fill_footer()[1]}; };
        const std::vector<std::string> none{"", ""};
        {
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-FILL", Aspect::Wide16x9, net::FillLevel::Medium, false, true));
            Application& app = d.app;
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Empty seats at START:", "Medium bots"}));
            app.set_fill_bots(net::FillLevel::Easy);
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Empty seats at START:", "Easy bots"}));
            app.set_fill_bots(net::FillLevel::Hard);
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Empty seats at START:", "Hard bots"}));
            app.set_fill_bots(net::FillLevel::None);
            d.hall.step(30);
            ASSERT_EQ(footer(app), none);
            app.quit();
        }
        {   // a fog room seats no bots: the footer would promise what START does not do
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-FILL-FOG", Aspect::Wide16x9, net::FillLevel::Medium, true, true));
            ASSERT_EQ(footer(d.app), none);
            d.app.quit();
        }
        {   // a guest cannot START: nothing, whatever its own --fill-bots says
            BoxRoom d;
            ASSERT_TRUE(open_guest(d, "BOX-FILL-GUEST", Aspect::Wide16x9, net::FillLevel::Medium));
            ASSERT_EQ(d.app.fill_bots(), net::FillLevel::Medium);
            ASSERT_EQ(footer(d.app), none);
            d.app.quit();
        }
        {   // the classic page has no chat box and no footer
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-FILL-CLASSIC", Aspect::Classic4x3, net::FillLevel::Medium, false, true));
            ASSERT_EQ(footer(d.app), none);
            d.app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.82 Protocol 13, The Room's Teams On The Leader's 16:9 Screen: The Foot Of The Players' Status Box Says \"Room teams:\" And The Pair When The Room Has Its Own (Whatever The Leader's Own --teams Is), \"Teams at START:\" For The Leader's Own Choice In A Room That Has None; The Leader's START Of A Room Without Teams Of Its Own Carries The Leader's Choice, And The Referee Makes It") {
        using L = net::FillLevel;
        const auto footer = [](Application& app) { return std::vector<std::string>{app.map_select().fill_footer()[0], app.map_select().fill_footer()[1]}; };
        const net::FillPlan blue_easy_black_hard(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
        {   // the room has Green + Red of its own: the leader and Bob are in, no bots yet
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-TEAMS-1", Aspect::Wide16x9, L::None, false, true, 1.0f, false, sim::StartTeams{true, 0, 1}));
            Application& app = d.app;
            ASSERT_TRUE(app.net()->room_teams() == sim::StartTeams({true, 0, 1}));
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Room teams:", "Green + Red"}));          // (the seats that play so far: the pair alone)
            app.set_start_teams(LocalTeams{true, 1, 2});                                               // the leader's own choice: the room's own teams win, nothing of it is shown
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Room teams:", "Green + Red"}));
            ASSERT_TRUE(app.net()->start_teams() == sim::StartTeams({true, 1, 2}) && app.net()->effective_teams() == sim::StartTeams({true, 0, 1}));
            app.set_fill_bots(blue_easy_black_hard);
            d.hall.step(30);
            const std::vector<std::string> both = footer(app);                                        // bots and teams: the bots in the first line, the room's teams in the second
            ASSERT_EQ(both, (std::vector<std::string>{"Bots: Blue Easy, Black Hard", "Room teams: Green + Red vs Blue + Black"}));      // (the longest way that fits the box at 12 px with the real font: "against" does not)
            app.quit();
        }
        {   // a room that has none of its own: the leader's own choice, said as a choice of START, as before
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-TEAMS-2", Aspect::Wide16x9, L::None, false, true));
            Application& app = d.app;
            ASSERT_FALSE(app.net()->room_teams().set);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"", ""}));
            app.set_start_teams(LocalTeams{true, 1, 2});
            d.hall.step(30);
            ASSERT_EQ(footer(app), (std::vector<std::string>{"Teams at START:", "Red + Blue"}));
            app.quit();
        }
        {   // a room that has none of its own (the control interface made it, as these rooms are made): the leader's START carries the leader's own choice, Red + Blue, and the referee makes it
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, "BOX-TEAMS-3", Aspect::Wide16x9, L::None, false, true));
            Application& app = d.app;
            ASSERT_TRUE(d.server.status("BOX-TEAMS-3").room_teams.empty());
            app.set_start_teams(LocalTeams{true, 1, 2});                                               // (its own choice: what the request carries, as the room has none)
            app.set_fill_bots(blue_easy_black_hard);
            d.hall.step(30);
            app.room_key_down(SDLK_s, 0, false);
            ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000));
            const server::RoomStatus s = d.server.status("BOX-TEAMS-3");
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 2 && s.room_teams.empty());
            ASSERT_EQ(s.teams, std::string("1+2"));                                                    // the leader's own choice: the room has none to say otherwise
            ASSERT_TRUE(s.allies[0] == 3 && s.allies[1] == 2 && s.allies[2] == 1 && s.allies[3] == 0);   // (Red + Blue, and the two seats left over are the other team)
            ASSERT_TRUE(app.sim().alliance_of(0) == 3 && app.sim().alliance_of(1) == 2 && d.peer.sim.alliance_of(2) == 1);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.65 A Guest's 16:9 Screen Has The Same Chat Box (The Guest Layout's): The Lines Of The Room Arrive In It, A Click In Its Input Box Or Its Lines And T Open The Input, The Line Reaches The Room, The Status Line Keeps \"Waiting For The Host To Start The Game...\"; The Guest's Keys Start Nothing") {
        BoxRoom d;
        ASSERT_TRUE(open_guest(d, "BOX-GUEST", Aspect::Wide16x9, net::FillLevel::None));
        Application& app = d.app;
        ASSERT_TRUE(app.map_select().setup_variant() == SetupVariant::Guest && app.map_select().is_guest());
        ASSERT_TRUE(app.room_chat_box() == &SL::of(SetupVariant::Guest).chat);
        ASSERT_TRUE(app.map_select().chat_panel().visible && app.map_select().chat_panel().lines.empty());
        const std::string prompt = std::string(sim::strings::text(sim::strings::kWaitingForHost));
        ASSERT_EQ(app.map_select().room().status, prompt);
        ASSERT_TRUE(d.peer.net.chat("welcome, Ben"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Lea: welcome, Ben"}));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, prompt);
        // the clicks: the Guest layout's boxes
        const SetupChatLayout& chat = SL::of(SetupVariant::Guest).chat;
        ASSERT_FALSE(app.room_mouse_down(chat.input_box.x - 1, centre_y(chat.input_box), SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_down(centre_x(chat.input_box), centre_y(chat.input_box), SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(centre_x(chat.input_box), centre_y(chat.input_box), SDL_BUTTON_LEFT));
        app.room_text_input("thanks");
        d.hall.step(30);
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("thanks"));
        for (const SDL_Keycode k : {SDLK_s, SDLK_q, SDLK_x}) app.room_key_down(k, 0, false);        // (a guest has no START, and Leave is not for a line that is typed)
        d.hall.step(30);
        ASSERT_TRUE(app.room_chat().is_open() && app.network_active() && app.map_select().chat_panel().typed == "thanks");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        ASSERT_EQ(panel_lines(app), (std::vector<std::string>{"Lea: welcome, Ben", "Ben: thanks"}));
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"0|Lea|welcome, Ben", "1|Ben|thanks"}));
        ASSERT_EQ(app.map_select().room().status, prompt);
        // T opens it for a guest too
        d.hall.step(500);
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_key_down(SDLK_ESCAPE, 0, false);
        d.hall.step(450);
        for (const SDL_Keycode k : {SDLK_s, SDLK_RETURN}) app.room_key_down(k, 0, false);
        d.hall.step(1000);
        ASSERT_TRUE(d.server.status("BOX-GUEST").state == server::RoomState::Waiting && app.state() == AppState::MapSelect);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.66 The Classic 640 x 480 Setup Screen Is Unchanged: No Chat Box (None Drawn, None Fed, No Fill Footer, No Box Zones), The Status Line Shows The Room's Lines For Five Seconds, \"You: ...\" After A Line Is Said, \"Say: ...\" While It Is Typed, And A Click On It Opens The Input") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-CLASSIC", Aspect::Classic4x3, net::FillLevel::Medium, false, true));
        Application& app = d.app;
        ASSERT_FALSE(app.map_select().wide_layout());
        ASSERT_TRUE(app.room_chat_box() == nullptr);
        ASSERT_FALSE(app.map_select().chat_panel().visible);
        ASSERT_TRUE(d.peer.net.chat("hello classic"));
        ASSERT_TRUE(d.hall.until([&]() { return !app.net()->pregame_chat().empty(); }, 8000));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("Bob: hello classic"));            // the mirror of the chat on the status line
        ASSERT_TRUE(app.map_select().chat_panel().lines.empty() && !app.map_select().chat_panel().visible);
        // the wide screen's zones are nothing here (a click at the wide input box's place: the classic page has the map's box and the players there)
        const SetupChatLayout& wide = SL::of(SetupVariant::Online).chat;
        ASSERT_FALSE(app.room_mouse_down(std::min(centre_x(wide.input_box), 600), std::min(centre_y(wide.input_box), 470), SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        // typing: the prompt "Say: ...", the caret; Enter: "You: ..."
        ASSERT_TRUE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        app.room_text_input("on the status line");
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().room().status_input && app.map_select().room().status_prefix == "Say: " && app.map_select().room().status == "on the status line");
        ASSERT_TRUE(app.map_select().chat_panel().typed.empty() && app.map_select().chat_panel().lines.empty());
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("You: on the status line"));
        ASSERT_TRUE(app.map_select().fill_footer()[0].empty() && app.map_select().fill_footer()[1].empty());     // (the level of the fill is in the status line's own prompt there)
        d.hall.step(6000);
        ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.67 Through The Window's Own Event Loop On The 16:9 Screen: T, The Typed Letters, A Click On The Input Box (Motion, Press, Release As SDL Queues Them) And Enter Reach The Chat Input; The Picture Of The Box Changes With The Lines, The Typed Text And The Caret, And Nothing Else Of The Setup Screen's Chat Column Moves") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "BOX-EVENTS", Aspect::Wide16x9, net::FillLevel::None, false, true));
        Application& app = d.app;
        const SetupChatLayout& chat = SL::of(SetupVariant::Online).chat;
        const auto push = [](SDL_Event e) { return SDL_PushEvent(&e) == 1; };
        const auto key = [&](SDL_Keycode sym) {
            SDL_Event e;
            std::memset(&e, 0, sizeof(e));
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = sym;
            return push(e);
        };
        const auto text = [&](const char* s) {
            SDL_Event e;
            std::memset(&e, 0, sizeof(e));
            e.type = SDL_TEXTINPUT;
            std::snprintf(e.text.text, sizeof(e.text.text), "%s", s);
            return push(e);
        };
        const auto click = [&](int32_t x, int32_t y) {
            SDL_Event motion;
            std::memset(&motion, 0, sizeof(motion));
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = x;
            motion.motion.y = y;
            SDL_Event press = motion;
            press.type = SDL_MOUSEBUTTONDOWN;
            press.button.x = x;
            press.button.y = y;
            press.button.button = SDL_BUTTON_LEFT;
            press.button.clicks = 1;
            SDL_Event release = press;
            release.type = SDL_MOUSEBUTTONUP;
            return push(motion) && push(press) && push(release);
        };
        const LayoutRect lines_box = chat.lines;
        const LayoutRect input = chat.input_box;
        const LayoutRect outside{0, 0, chat.box.x - 4, 540};                                       // (the left column of the screen: not the chat's)
        const auto park = [&]() {                                                                  // (the pointer is drawn by the game: it is put on the clay, away from what is measured)
            SDL_Event motion;
            std::memset(&motion, 0, sizeof(motion));
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = 700;
            motion.motion.y = 20;
            return push(motion);
        };
        ASSERT_TRUE(park());
        app.map_select().update(0.6f);                                                             // (the screen's labels, and the chat's lines, are drawn from 500 ms after it was created)
        app.run_frame_with_delta(0.016f);
        const uint64_t lines_empty = region_hash(app, lines_box);
        const uint64_t input_empty = region_hash(app, input);
        const uint64_t outside_before = region_hash(app, outside);
        ASSERT_TRUE(lines_empty != 0 && input_empty != 0);
        // a line of the room changes the lines box and nothing else of the column
        ASSERT_TRUE(d.peer.net.chat("a line for the box"));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 1; }, 8000));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(region_hash(app, lines_box) != lines_empty);
        ASSERT_EQ(region_hash(app, input), input_empty);
        // the click on the input box opens it (the picture is the canvas: window coordinates are the picture's)
        ASSERT_TRUE(click(centre_x(input), centre_y(input)));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(park());
        ASSERT_TRUE(text("hi there"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.room_chat().text(), std::string("hi there"));
        ASSERT_EQ(app.map_select().chat_panel().typed, std::string("hi there"));
        // the typed text is in the input box's picture, and the caret blinks in it: both states differ from each other and from the empty box
        std::set<uint64_t> seen;
        for (int i = 0; i < 40; ++i) {
            d.hall.step(20);
            app.run_frame_with_delta(0.001f);
            seen.insert(region_hash(app, input));
        }
        ASSERT_EQ(seen.size(), size_t{2});
        ASSERT_TRUE(seen.count(input_empty) == 0);
        // an S while the line is typed is not START, Enter sends it, and the box that showed the typed text is empty again; the other lines of the screen did not move
        ASSERT_TRUE(key(SDLK_s));
        app.run_frame_with_delta(0.016f);
        d.hall.step(500);
        ASSERT_TRUE(app.room_chat().is_open() && !app.map_select().is_locked() && d.server.status("BOX-EVENTS").state == server::RoomState::Waiting);
        ASSERT_TRUE(key(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return d.peer.net.pregame_chat().size() == 2; }, 8000));
        ASSERT_EQ(lines(d.peer.net.pregame_chat()), (std::vector<std::string>{"1|Bob|a line for the box", "0|Ana|hi there"}));
        ASSERT_TRUE(d.hall.until([&]() { return app.map_select().chat_panel().lines.size() == 2; }, 8000));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(region_hash(app, input), input_empty);
        ASSERT_EQ(region_hash(app, outside), outside_before);
        // T from the keyboard opens it the same way
        d.hall.step(500);
        ASSERT_TRUE(key(SDLK_t));
        ASSERT_TRUE(text("t"));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text().empty());
        app.quit();
    } TEST_END();

    // ---- the meeting of the online rooms and the mouse-wheel zoom (v0.1.0: the two were written apart, on v0.0.99): N5.68 - N5.72 ----
    // The wheel, the middle button and the level of the zoom through the application's own entry points, and through the window's event queue where the screen's own routing is part of the
    // question (a setup screen takes its events in handle_events, not in handle_mouse_wheel)
    uint32_t wheel_clock = 500000;
    const auto wheel_event = [&wheel_clock](int32_t y, float precise) {
        SDL_MouseWheelEvent e{};
        e.type = SDL_MOUSEWHEEL;
        wheel_clock += 16;
        e.timestamp = wheel_clock;
        e.y = y;
        e.preciseY = precise;
        e.direction = SDL_MOUSEWHEEL_NORMAL;
        return e;
    };
    const auto notch = [&](Application& app, int notches) {
        for (int i = 0; i < std::abs(notches); ++i) app.handle_mouse_wheel(wheel_event(notches > 0 ? 1 : -1, notches > 0 ? 1.0f : -1.0f));
    };
    const auto middle_click = [](Application& app, int32_t x, int32_t y) {
        SDL_MouseButtonEvent b{};
        b.type = SDL_MOUSEBUTTONDOWN;
        b.button = SDL_BUTTON_MIDDLE;
        b.state = SDL_PRESSED;
        b.clicks = 1;
        b.x = x;
        b.y = y;
        app.handle_mouse_button(b);
        b.type = SDL_MOUSEBUTTONUP;
        b.state = SDL_RELEASED;
        app.handle_mouse_button(b);
    };
    const auto queue = [](SDL_Event e) { return SDL_PushEvent(&e) == 1; };
    const auto queue_motion = [&](int32_t x, int32_t y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_MOUSEMOTION;
        e.motion.x = x;
        e.motion.y = y;
        return queue(e);
    };
    const auto queue_wheel = [&](int32_t y, float precise) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_MOUSEWHEEL;
        e.wheel.y = y;
        e.wheel.preciseY = precise;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        return queue(e);
    };
    const auto queue_button = [&](uint8_t button, bool down, int32_t x, int32_t y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.button = button;
        e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        return queue(e);
    };
    const auto queue_text = [&](const char* text) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_TEXTINPUT;
        std::snprintf(e.text.text, sizeof(e.text.text), "%s", text);
        return queue(e);
    };
    const auto queue_key = [&](SDL_Keycode sym) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = sym;
        return queue(e);
    };
    // the picture of a rectangle of the canvas: the number of distinct colors in it (a picture of a map has many, the "No preview" box a handful)
    const auto region_colors = [](Application& app, const LayoutRect& r) {
        SDL_Rect rect{r.x, r.y, r.w, r.h};
        std::vector<uint8_t> px(static_cast<size_t>(r.w) * static_cast<size_t>(r.h) * 4u, 0);
        if (SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), &rect, SDL_PIXELFORMAT_RGBA32, px.data(), r.w * 4) != 0) return 0;
        std::set<uint32_t> seen;
        for (size_t i = 0; i + 3 < px.size(); i += 4) seen.insert(static_cast<uint32_t>(px[i]) << 16 | static_cast<uint32_t>(px[i + 1]) << 8 | px[i + 2]);
        return static_cast<int>(seen.size());
    };
    // START of the room's leader (Enter, the chat input closed): the match begins on this machine and on Bob's
    const auto start_room_match = [&](BoxRoom& d, const std::string& code) -> bool {
        d.app.room_key_down(SDLK_RETURN, 0, false);
        if (!d.hall.until([&]() { return d.app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000)) return false;
        const server::RoomStatus st = d.server.status(code);
        d.app.hud().dismiss_match_start_modal();
        return st.state == server::RoomState::Running && st.bots.size() == 2;                // the fill seated the two empty seats
    };

    TEST_CASE("N5.68 The Wheel And The Middle Button Do Nothing On The 16:9 Setup Screens Of A Room (Leader And Guest, With Their Chat Box) - Not Over The Chat Box, Its Input Box, The Map Preview Or Anywhere Else - Whether The Chat Input Is Closed Or Open: Nothing Zooms, Nothing Opens Or Closes The Input, What Is Typed Stays, The Pictures Stay") {
        for (const bool leader : {true, false}) {
            BoxRoom d;
            const std::string code = leader ? "WZ-LEAD" : "WZ-GUEST";
            ASSERT_TRUE(leader ? open_leader(d, code, Aspect::Wide16x9, net::FillLevel::Medium, false, true, 2.0f, true) : open_guest(d, code, Aspect::Wide16x9, net::FillLevel::None, 2.0f, true));
            Application& app = d.app;
            const SetupVariant variant = leader ? SetupVariant::Online : SetupVariant::Guest;
            const SetupLayout& layout = SL::of(variant);
            ASSERT_TRUE(app.map_select().wide_layout() && app.map_select().setup_variant() == variant && app.map_select().chat_panel().visible);
            ASSERT_TRUE(app.remembered_zoom() == 2.0f && app.zoom() == 2.0f);                  // (the setup screen's camera is at the remembered level until a match starts; a wheel or a middle button that leaked would change it)
            app.map_select().update(0.6f);
            const LayoutRect preview = layout.preview_area();
            struct Spot {
                const char* what;
                int32_t x;
                int32_t y;
            };
            const std::vector<Spot> spots = {
                {"the chat box's lines", centre_x(layout.chat.lines), centre_y(layout.chat.lines)},
                {"the chat box's input box", centre_x(layout.chat.input_box), centre_y(layout.chat.input_box)},
                {"the map preview", centre_x(preview), centre_y(preview)},
                {"the clay at the top right", 700, 20},
                {"where a match's map view would be", 300, 200},
            };
            const LayoutRect outside{0, 0, layout.chat.box.x - 4, 540};                          // (the screen left of the chat column, the preview in it)
            struct Snap {
                float zoom{0};
                float remembered{0};
                float camera_zoom{0};
                bool open{false};
                std::string typed;
                std::string text;
                size_t lines{0};
                bool locked{false};
                AppState state{AppState::Loading};
                net::NetGame::Phase phase{net::NetGame::Phase::Off};
                uint64_t lines_px{0};
                uint64_t outside_px{0};
            };
            const auto park = [&]() { return queue_motion(700, 20); };
            const auto snap = [&]() {
                park();
                app.run_frame_with_delta(0.016f);
                Snap s;
                s.zoom = app.zoom();
                s.remembered = app.remembered_zoom();
                s.camera_zoom = app.renderer().camera().zoom;
                s.open = app.room_chat().is_open();
                s.typed = app.map_select().chat_panel().typed;
                s.text = app.room_chat().text();
                s.lines = app.map_select().chat_panel().lines.size();
                s.locked = app.map_select().is_locked();
                s.state = app.state();
                s.phase = app.net()->phase();
                s.lines_px = region_hash(app, layout.chat.lines);
                s.outside_px = region_hash(app, outside);
                return s;
            };
            const auto same = [](const Snap& a, const Snap& b) {
                return a.zoom == b.zoom && a.remembered == b.remembered && a.camera_zoom == b.camera_zoom && a.open == b.open && a.typed == b.typed && a.text == b.text && a.lines == b.lines &&
                       a.locked == b.locked && a.state == b.state && a.phase == b.phase && a.lines_px == b.lines_px && a.outside_px == b.outside_px;
            };
            // the wheel and the middle button at every spot, by the application's entry points and through the event queue
            const auto roll_everywhere = [&](const char* when) -> bool {
                for (const Spot& spot : spots) {
                    const Snap before = snap();
                    if (!queue_motion(spot.x, spot.y)) return false;
                    app.run_frame_with_delta(0.016f);
                    if (app.view_zoom_allowed(spot.x, spot.y)) {
                        std::cout << "\n    [" << when << ", " << spot.what << "] the wheel would be allowed\n";
                        return false;
                    }
                    notch(app, +1);
                    notch(app, -1);
                    notch(app, -1);
                    app.handle_mouse_wheel(wheel_event(0, 0.6f));
                    app.handle_mouse_wheel(wheel_event(0, 0.6f));
                    middle_click(app, spot.x, spot.y);
                    if (!(queue_wheel(1, 1.0f) && queue_wheel(-1, -1.0f) && queue_wheel(0, 0.6f) && queue_wheel(0, 0.6f) && queue_button(SDL_BUTTON_MIDDLE, true, spot.x, spot.y) &&
                          queue_button(SDL_BUTTON_MIDDLE, false, spot.x, spot.y))) {
                        return false;
                    }
                    app.run_frame_with_delta(0.016f);
                    d.hall.step(30);
                    const Snap after = snap();
                    if (!same(before, after)) {
                        std::cout << "\n    [" << when << ", " << spot.what << "] something changed: zoom " << before.zoom << " -> " << after.zoom << ", remembered " << before.remembered << " -> " << after.remembered
                                  << ", input open " << before.open << " -> " << after.open << ", typed '" << before.typed << "' -> '" << after.typed << "', lines " << before.lines << " -> " << after.lines
                                  << ", pictures " << (before.lines_px == after.lines_px && before.outside_px == after.outside_px ? "same" : "differ") << "\n";
                        return false;
                    }
                }
                return true;
            };
            // 1. the chat input closed: nothing opens it either (a middle click over the input box is not a click)
            ASSERT_TRUE(roll_everywhere("input closed"));
            ASSERT_FALSE(app.room_chat().is_open());
            // 2. the chat input open, with a line typed
            app.room_key_down(SDLK_t, 0, false);
            app.room_text_input("t");
            app.room_text_input("abc");
            d.hall.step(30);
            ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text() == "abc" && app.map_select().chat_panel().typed == "abc");
            ASSERT_TRUE(roll_everywhere("input open"));
            ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text() == "abc" && app.map_select().chat_panel().typed == "abc");
            // 3. the line is sent as it was typed, and the room is still the room
            app.room_key_down(SDLK_RETURN, 0, false);
            ASSERT_TRUE(d.hall.until([&]() { return !d.peer.net.pregame_chat().empty(); }, 8000));
            ASSERT_EQ(d.peer.net.pregame_chat().back().text, std::string("abc"));
            ASSERT_TRUE(!app.room_chat().is_open() && app.state() == AppState::MapSelect && app.net()->phase() == net::NetGame::Phase::Room && !app.map_select().is_locked());
            ASSERT_TRUE(app.remembered_zoom() == 2.0f && app.zoom() == 2.0f);
            app.quit();
        }
    } TEST_END();

    // The zoom levels are the player's own view: a match of the network offers the levels of a local game on the same map and screen (the series 2^(k/4) from 2 down to the map's limit,
    // max(view_w / map_w, view_h / map_h): TINY (992 x 992) over the 16:9 view of 762 x 500 ends at 762 / 992 = 0.768), and the wheel zooms through them in the match. Nothing of it is sent.
    const auto tiny_wide_levels_ok = [](const std::vector<float>& levels) {
        if (levels.size() != 7) return false;
        const float want[6] = {2.0f, 1.6817928f, 1.4142135f, 1.1892071f, 1.0f, 0.8408964f};
        for (int i = 0; i < 6; ++i) {
            if (std::fabs(levels[static_cast<size_t>(i)] - want[i]) > 1e-6f) return false;
        }
        return levels[6] >= 762.0f / 992.0f && levels[6] < 762.0f / 992.0f + 1e-5f;                  // the limit is the last level
    };
    const auto nearest_to = [](float z, const std::vector<float>& list) {
        float best = list.front();
        for (const float l : list) {
            if (std::fabs(std::log(z / l)) < std::fabs(std::log(z / best))) best = l;
        }
        return best;
    };

    TEST_CASE("N5.69 A Match Started From A Room With Bots (The Leader's Fill, A Server's Room) Has The Zoom Levels Of A Local Game On The Same Map And Screen: It Starts At The Nearest One To The Remembered 0.5 (Which Stays Remembered), The Wheel Zooms Out And In Through Them In The Match, And The Local Game After It Starts At The Level That Was Chosen") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "ZM-FILL", Aspect::Wide16x9, net::FillLevel::Medium, false, true, 0.5f, true));
        Application& app = d.app;
        ASSERT_TRUE(app.remembered_zoom() == 0.5f && app.zoom() == 0.5f);                    // (the setup screen's camera is at the remembered level until a match starts)
        ASSERT_TRUE(start_room_match(d, "ZM-FILL"));
        ASSERT_TRUE(app.network_active());
        const LayoutRect view = app.layout().view();
        const std::vector<float> levels = app.zoom_levels();
        ASSERT_TRUE(app.zoom_limits() == zoom::Limits::any() && tiny_wide_levels_ok(levels));      // (no limit of the kind of match)
        ASSERT_TRUE(app.zoom() == levels.back() && app.remembered_zoom() == 0.5f);           // 0.5 is below this map's limit: the match starts at the limit, 0.5 stays remembered
        app.note_pointer(view.x + 300, view.y + 200);
        notch(app, -1);
        ASSERT_EQ(app.zoom(), levels.back());                                                // the wheel toward cannot go beyond the map's limit
        app.handle_mouse_wheel(wheel_event(0, -0.6f));
        app.handle_mouse_wheel(wheel_event(0, -0.6f));
        ASSERT_EQ(app.zoom(), levels.back());                                                // nor can a trackpad's creep
        ASSERT_TRUE(!app.set_zoom(0.5f, view.x + 300, view.y + 200) && !app.step_zoom(-1, view.x + 300, view.y + 200));        // (0.5 is no level here: the nearest is where the view is)
        ASSERT_EQ(app.remembered_zoom(), 0.5f);                                              // none of that changed what is remembered
        notch(app, +1);                                                                      // one notch in: the next level
        ASSERT_EQ(app.zoom(), levels[5]);
        ASSERT_EQ(app.remembered_zoom(), levels[5]);                                         // a level that is chosen in the match is remembered
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        ASSERT_EQ(app.zoom(), levels[5]);                                                    // a trackpad's creep adds up: 0.6 is no notch ...
        app.handle_mouse_wheel(wheel_event(0, 0.6f));
        ASSERT_EQ(app.zoom(), levels[4]);                                                    // ... and 1.2 is one
        notch(app, +2);
        ASSERT_EQ(app.zoom(), levels[2]);
        middle_click(app, view.x + 300, view.y + 200);
        ASSERT_EQ(app.zoom(), 1.0f);                                                         // the middle button: back to 1
        notch(app, -2);
        ASSERT_EQ(app.zoom(), levels.back());                                                // two notches out from 1: the limit again
        ASSERT_TRUE(queue_motion(view.x + 300, view.y + 200) && queue_wheel(1, 1.0f));
        app.run_frame_with_delta(0.016f);                                                    // (through the window's own event loop, in the match)
        ASSERT_EQ(app.zoom(), levels[5]);
        ASSERT_TRUE(queue_motion(view.x + 300, view.y + 200) && queue_button(SDL_BUTTON_MIDDLE, true, view.x + 300, view.y + 200) && queue_button(SDL_BUTTON_MIDDLE, false, view.x + 300, view.y + 200));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.zoom(), 1.0f);
        ASSERT_EQ(app.remembered_zoom(), 1.0f);
        d.hall.step(kDialogMs + 3000);                                                       // the match goes on at a zoom of the player's own, with the server's bots (its first turn is sealed kDialogMs after it began)
        ASSERT_TRUE(!app.net()->desynced() && d.server.status("ZM-FILL").ticks > 40);
        app.note_pointer(view.x + 300, view.y + 200);                                        // (a queued button release in a headless run reads as a release outside the window)
        notch(app, -1);                                                                      // the level that the match ends at: what the next game starts with
        ASSERT_EQ(app.zoom(), levels[5]);
        app.return_to_map_select();
        ASSERT_TRUE(!app.network_active() && app.zoom_limits() == zoom::Limits::any());
        ASSERT_TRUE(app.start_game(maps_dir() + "TINY.LVL"));
        app.hud().dismiss_match_start_modal();
        ASSERT_TRUE(app.zoom() == levels[5] && app.remembered_zoom() == levels[5]);
        ASSERT_TRUE(app.zoom_levels() == levels);                                            // (the same levels as the match of the network had)
        app.note_pointer(view.x + 300, view.y + 200);
        notch(app, -1);
        ASSERT_EQ(app.zoom(), levels.back());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.70 Typing In The Match's Chat While Zooming (A Match From A Room With Bots): The Typed Line Is The Same Through Every Zoom Step Of The Wheel And The Middle Button - The Keys Of The Line (+ - =) Do Not Zoom - Enter Sends It Whole To The Room, And The Level Chosen In The Match Is What The Next Local Game Starts With") {
        BoxRoom d;
        ASSERT_TRUE(open_leader(d, "ZM-CHAT", Aspect::Wide16x9, net::FillLevel::Medium, false, true));
        Application& app = d.app;
        ASSERT_TRUE(start_room_match(d, "ZM-CHAT"));
        const LayoutRect view = app.layout().view();
        const int32_t px = view.x + 300;
        const int32_t py = view.y + 200;
        const float in1 = zoom::series(1);                                                   // one level in from 1 (1.19) and one out (0.84): the levels are not 2 and 0.5 any more
        const float out1 = zoom::series(-1);
        ASSERT_TRUE(queue_motion(px, py));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == 1.0f && app.hud().get_chat_input().empty());
        ASSERT_TRUE(queue_text("hel"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hel"));
        ASSERT_TRUE(queue_wheel(1, 1.0f));                                                   // zoom in, in the middle of the line
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == in1 && app.hud().get_chat_input() == "hel");
        ASSERT_TRUE(queue_text("lo"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hello"));
        ASSERT_TRUE(queue_text(" +-="));                                                     // the characters that a zoom key would have: they are text, the zoom stays
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == in1 && app.hud().get_chat_input() == "hello +-=");
        ASSERT_TRUE(queue_button(SDL_BUTTON_MIDDLE, true, px, py) && queue_button(SDL_BUTTON_MIDDLE, false, px, py));       // the middle button: back to 1, the line stays
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == 1.0f && app.hud().get_chat_input() == "hello +-=");
        ASSERT_TRUE(queue_motion(px, py) && queue_wheel(-1, -1.0f));                         // toward: zoomed out one level (a queued button release in a headless run reads as a release outside the window: the motion puts the pointer back)
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.zoom() == out1 && app.hud().get_chat_input() == "hello +-=");
        ASSERT_TRUE(queue_text(" team"));
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hello +-= team"));
        ASSERT_TRUE(queue_key(SDLK_RETURN));                                                 // Enter: the line goes to the room whole
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.hud().get_chat_input().empty() && log_has(app.hud().get_chat_log(), "hello +-= team"));
        ASSERT_TRUE(d.hall.until([&]() { return !d.peer.chats.empty(); }, 8000));
        ASSERT_EQ(d.peer.chats.back().text, std::string("hello +-= team"));
        ASSERT_TRUE(queue_motion(px, py) && queue_wheel(2, 2.0f));                           // zoomed in two levels (0.84 -> 1 -> 1.19), a second line while zoomed, and the match goes on
        app.run_frame_with_delta(0.016f);
        ASSERT_EQ(app.zoom(), in1);
        ASSERT_TRUE(queue_text("second"));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(queue_key(SDLK_RETURN));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(d.hall.until([&]() { return d.peer.chats.size() == 2; }, 8000));
        ASSERT_EQ(d.peer.chats.back().text, std::string("second"));
        d.hall.step(2000);
        ASSERT_TRUE(!app.net()->desynced() && app.zoom() == in1 && app.remembered_zoom() == in1);
        app.return_to_map_select();                                                          // the level chosen in the match is what a local game starts with
        ASSERT_TRUE(app.start_game(maps_dir() + "TINY.LVL"));
        ASSERT_EQ(app.zoom(), in1);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.71 The Same For The Host Of A Room On The Local Network Whose START Seats Bots Itself (This Machine Runs Them): The Match Is A Match Of The Network With The Zoom Levels Of A Local Game On Its Map - It Starts At The Nearest One To The Remembered 0.5, The Wheel Zooms Out And In, Bots Or Not - And The Local Game After It Has The Same Levels") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.fill_bots = net::FillLevel::Easy;
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
        cfg.zoom = 0.5f;
        cfg.zoom_given = true;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.remembered_zoom() == 0.5f && app.zoom() == 0.5f);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x0C);               // this machine runs the two bots of the fill ...
        ASSERT_TRUE(app.network_active());                                                   // ... and it is still a match of the network
        app.hud().dismiss_match_start_modal();
        const LayoutRect view = app.layout().view();
        const std::vector<float> levels = app.zoom_levels();
        ASSERT_TRUE(app.zoom_limits() == zoom::Limits::any() && levels.size() >= 5 && levels.front() == 2.0f);      // (no limit of the kind of match)
        ASSERT_TRUE(std::count(levels.begin(), levels.end(), 1.0f) == 1 && std::is_sorted(levels.rbegin(), levels.rend()));
        const float start = nearest_to(0.5f, levels);
        ASSERT_TRUE(app.zoom() == start && app.remembered_zoom() == 0.5f);
        const int32_t map_px = static_cast<int32_t>(app.sim().grid().width()) * 32;          // the last level is the map's limit: the view's smaller-relative side just fills it
        const float limit = std::max(static_cast<float>(view.w) / static_cast<float>(map_px), static_cast<float>(view.h) / static_cast<float>(static_cast<int32_t>(app.sim().grid().height()) * 32));
        ASSERT_TRUE(levels.back() >= limit && levels.back() < limit + 1e-5f);
        app.note_pointer(view.x + 300, view.y + 200);
        notch(app, +1);
        ASSERT_TRUE(app.zoom() > start && app.remembered_zoom() == app.zoom());              // zooming in works, and it is what is remembered then
        notch(app, -3);
        ASSERT_TRUE(app.zoom() < start || app.zoom() == levels.back());                      // zooming out works (or the view is at the map's limit)
        const float chosen = app.zoom();
        duo.step(3000 + kDialogMs);                                                          // (the host seals the first turn kDialogMs after the match began; the bots look from the first tick on)
        ASSERT_TRUE(!app.net()->desynced() && !bob.net.desynced() && app.bots()->stats(2).decisions > 0);
        ASSERT_TRUE(app.zoom() == chosen);
        app.return_to_map_select();
        ASSERT_TRUE(!app.network_active() && app.bots() == nullptr && app.zoom_limits() == zoom::Limits::any());
        ASSERT_TRUE(app.start_game(maps_dir() + "GAUNTLET.LVL"));                            // a local game on a map of 1920 x 1920 (this level list is a different one: the match's map may be another)
        ASSERT_TRUE(app.zoom() == nearest_to(chosen, app.zoom_levels()) && app.zoom_levels().front() == 2.0f);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.72 The Map Preview Of The 16:9 Setup Screens Of A Room (Leader And Guest) Is The Zoom 1 Picture Whatever The Camera Is At (--zoom 0.5 Or 2: The Camera Of The Setup Screen Is At The Remembered Level, As A Zoomed Match Leaves It): The Same Pixels As With The Camera At 1, A Picture Of The Map, And The Live Camera Keeps Its Zoom") {
        struct Shot {
            bool ok{false};
            uint64_t hash{0};
            int colors{0};
            float camera_zoom{0};
        };
        const auto preview_after = [&](bool leader, float level, int index) {
            Shot out;
            BoxRoom d;
            const std::string code = "PV-" + std::string(leader ? "L" : "G") + std::to_string(index);
            // (--zoom LEVEL --join ...: the camera of the setup screen is at the remembered level from the start, as a zoomed match leaves it; no preview has been made yet: no frame has run)
            const bool opened = leader ? open_leader(d, code, Aspect::Wide16x9, net::FillLevel::None, false, true, level, true) : open_guest(d, code, Aspect::Wide16x9, net::FillLevel::None, level, true);
            if (!opened) return out;
            Application& app = d.app;
            const LayoutRect area = SL::of(leader ? SetupVariant::Online : SetupVariant::Guest).preview_area();
            app.map_select().update(0.6f);
            app.run_frame_with_delta(0.016f);
            out.hash = region_hash(app, area);
            out.colors = region_colors(app, area);
            out.camera_zoom = app.renderer().camera().zoom;
            out.ok = out.hash != 0;
            app.quit();
            return out;
        };
        int index = 0;
        for (const bool leader : {true, false}) {
            const Shot at1 = preview_after(leader, 1.0f, index++);
            ASSERT_TRUE(at1.ok && at1.colors > 30 && at1.camera_zoom == 1.0f);              // (a picture of the map, not the "No preview" box)
            for (const float level : {0.5f, 2.0f}) {
                const Shot other = preview_after(leader, level, index++);
                if (!(other.ok && other.hash == at1.hash && other.colors == at1.colors && other.camera_zoom == level)) {
                    std::cout << "\n    [" << (leader ? "leader" : "guest") << ", camera at " << level << "] hash " << other.hash << " vs " << at1.hash << ", colors " << other.colors << " vs " << at1.colors
                              << ", camera " << other.camera_zoom << "\n";
                }
                ASSERT_TRUE(other.ok && other.hash == at1.hash && other.colors == at1.colors && other.camera_zoom == level);
            }
        }
    } TEST_END();

    TEST_CASE("N5.73 A Double Click On START After Typing Does Not Start The Match (The Review Of v0.1.0): The First Click Closes The Chat Input, The Rest Of Its Click Sequence Is Not The Screen's - A Press That SDL Counts As The Second Of A Double Click, And One That Comes Within The Double-Click Time Of The Closing Click - A Fresh Single Click 600 ms Later Is START (The 16:9 Page And The Classic One)") {
        struct Page {
            Aspect aspect;
            const char* code;
        };
        for (const Page& page : {Page{Aspect::Wide16x9, "BOX-DBL-W"}, Page{Aspect::Classic4x3, "BOX-DBL-C"}}) {
            BoxRoom d;
            ASSERT_TRUE(open_leader(d, page.code, page.aspect, net::FillLevel::Medium, false, true));
            Application& app = d.app;
            const bool wide = page.aspect == Aspect::Wide16x9;
            const SetupLayout& layout = SL::of(SetupVariant::Online);
            const int32_t open_x = wide ? centre_x(layout.chat.input_box) : MapSelectScreen::LABEL_X + 20;       // (where a click opens the input: the chat box's input box, the classic page's status line)
            const int32_t open_y = wide ? centre_y(layout.chat.input_box) : MapSelectScreen::STATUS_Y + 10;
            const int32_t sx = wide ? centre_x(layout.start) : MapSelectScreen::BTN_START_X + 5;
            const int32_t sy = wide ? centre_y(layout.start) : MapSelectScreen::BTN_START_Y + 5;
            const auto push = [](Uint32 type, int32_t x, int32_t y, uint8_t clicks) {
                SDL_Event e;
                std::memset(&e, 0, sizeof(e));
                e.type = type;
                e.button.x = x;
                e.button.y = y;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.clicks = clicks;
                e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
                return SDL_PushEvent(&e) == 1;
            };
            const auto waiting = [&]() { return d.server.status(page.code).state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
            const auto open_input = [&]() {
                app.room_mouse_down(open_x, open_y, SDL_BUTTON_LEFT);                          // (the input's own click, as a person's: press and release)
                app.room_mouse_up(open_x, open_y, SDL_BUTTON_LEFT);
                return app.room_chat().is_open();
            };
            app.map_select().update(0.6f);
            ASSERT_FALSE(app.closing_click_pending());

            // (a) typed, then a double click on START: SDL counts the presses 1 and 2; the first closes the input, the second is the rest of that click and starts nothing
            ASSERT_TRUE(open_input());
            app.room_text_input("on my way");
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 2));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 2));
            app.run_frame_with_delta(0.016f);
            d.hall.step(2000);
            ASSERT_TRUE(!app.room_chat().is_open());
            ASSERT_TRUE(app.closing_click_pending());                                         // (the rule waits for a click that begins a new sequence)
            ASSERT_TRUE(!app.map_select().start_button().pressed());
            ASSERT_TRUE(waiting());

            // (b) a double click that SDL does not count (a hand moves the pointer between the clicks): the second press, 100 ms after the closing one, is the rest of it as well
            SDL_Delay(600);                                                                   // (the double-click time of (a) is over)
            ASSERT_TRUE(open_input());
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(!app.room_chat().is_open() && app.closing_click_pending());
            SDL_Delay(100);
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            app.run_frame_with_delta(0.016f);
            d.hall.step(1500);
            ASSERT_TRUE(app.closing_click_pending() && !app.map_select().start_button().pressed() && waiting());

            // (c) a click that begins a new sequence (600 ms after the closing click, SDL counts 1): the screen's own: START asks, and the server starts the match with the two of them and the fill
            SDL_Delay(600);
            ASSERT_TRUE(push(SDL_MOUSEBUTTONDOWN, sx, sy, 1));
            ASSERT_TRUE(push(SDL_MOUSEBUTTONUP, sx, sy, 1));
            app.run_frame_with_delta(0.016f);
            ASSERT_TRUE(!app.closing_click_pending());
            ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.peer.net.phase() == net::NetGame::Phase::Playing; }, 15000));
            ASSERT_TRUE(app.map_select().is_locked());
            app.quit();
        }
    } TEST_END();
}


// The earlier front page's one card ("New match"; today's old test-room addresses of web/lobby.html make the same links) sends every player to the game page of a room of four seats with the card's plan, and the game starts the match by itself: `--seat` (the color
// of the person: any of the four), `--fill-bots` (four words: a bot row is its level, You, Friend and Nobody are none) and `--start-when N` (1 + the Friend rows: the leader's game presses START once N
// people are in). The same plan and the same N are in every link of the room, because whoever connects first leads and only the leader's game starts the match.
void run_one_card_tests() {
    using L = net::FillLevel;
    server::ServerLimits limits;
    limits.demo_rooms = 4;
    limits.demo_map = "TINY.LVL";
    limits.demo_maps = {"TINY.LVL"};
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name, uint8_t seat, const net::FillPlan& plan, uint8_t start_when) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        cfg.net_seat = seat;
        cfg.fill_bots = plan;
        cfg.net_start_when = start_when;
        return cfg;
    };
    // the create block that every link of the card's room carries (`roommap`, `roomseats`, `roomteams`): TINY, four seats, the room's teams as "A+B" or "ffa"; the server makes the room of the first Hello
    const auto card_block = [](const std::string& teams) {
        net::CreateBlock block;
        block.map_name = "TINY.LVL";
        block.seats = 4;
        if (teams != "ffa") {
            block.team_a = static_cast<uint8_t>(teams[0] - '0');
            block.team_b = static_cast<uint8_t>(teams[2] - '0');
        }
        return block;
    };

    TEST_CASE("N5.83 One Card, The Game's Arguments: --seat, --fill-bots (Four Words: A Level For A Bot Row, none For You / Friend / Nobody) And --start-when N Together; The Seat Is Any Of The Four Colors And The Plan Keeps A Bot At Green") {
        std::vector<std::string> args = {"ants", "--join-url", "wss://play.example.org/ws", "--room", "k7m2xq", "--seat", "2", "--fill-bots", "easy,medium,none,none", "--start-when", "1", "--name", "Ann", "--aspect", "16:9"};
        std::vector<char*> st;
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.startup_error.empty());
        ASSERT_TRUE(c.net_url == "wss://play.example.org/ws" && c.net_room == "k7m2xq" && c.net_seat == 2 && c.net_start_when == 1 && c.player_name == "Ann");
        ASSERT_TRUE(c.fill_bots == net::FillPlan(std::array<L, 4>{L::Easy, L::Medium, L::None, L::None}));        // (a bot at Green: the plan's seat 0 is a seat like the others)
        for (int seat = 0; seat < 4; ++seat) {                                                                   // every color can be the person's
            const std::string text = std::to_string(seat);
            args = {"ants", "--join-url", "ws://localhost/ws", "--room", "k7m2xq", "--seat", text, "--start-when", "3"};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.net_seat == seat && c.net_start_when == 3 && c.startup_error.empty());
        }
        for (int people = 1; people <= 4; ++people) {                                                            // You and up to three friends: one to four people to wait for
            const std::string text = std::to_string(people);
            args = {"ants", "--join-url", "ws://localhost/ws", "--room", "k7m2xq", "--start-when", text};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.net_start_when == people && c.startup_error.empty());
        }
        for (const char* text : {"0", "5", "40", "-1", "x", ""}) {                                               // anything else is no hook (and no error: the page only ever passes 1 - 4)
            args = {"ants", "--join-url", "ws://localhost/ws", "--room", "k7m2xq", "--start-when", text};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.net_start_when == 0 && c.startup_error.empty());
        }
    } TEST_END();

    TEST_CASE("N5.84 One Card, Bots Only: A Leader Who Sits At Any Color With --start-when 1 Starts The Match At Once, The Bots Of The Plan In Their Seats (A Bot At Green Too), The Nobody Seat Empty, The Room's Teams Made; The Plan's Word For The Leader's Own Seat Is Ignored") {
        struct Row {
            uint8_t seat;
            std::array<L, 4> plan;
            const char* code;                      // (a name: the room's choices are in the create block)
            uint8_t mask;                          // the seats that play
            const char* teams;                     // "ffa" or "A+B"
            std::array<std::pair<uint8_t, const char*>, 2> bots;
        };
        const Row rows[] = {
            {2, {L::Easy, L::Medium, L::None, L::None}, "tiny12ab", 0x07, "1+2", {{{0, "Bot (Easy)"}, {1, "Bot (Medium)"}}}},        // Blue, bots at Green and Red; Red + Blue are a team, Green plays alone
            {0, {L::None, L::None, L::Hard, L::Easy}, "tinyffa1", 0x0D, "ffa", {{{2, "Bot (Hard)"}, {3, "Bot (Easy)"}}}},                  // Green, bots at Blue and Black, Red is Nobody
            {3, {L::Medium, L::None, L::Hard, L::None}, "tiny02ab", 0x0D, "0+2", {{{0, "Bot (Medium)"}, {2, "Bot (Hard)"}}}},          // Black; Green + Blue are a team (both bots), Black plays alone, Red is Nobody
            {2, {L::Easy, L::Medium, L::Hard, L::None}, "tinyffa2", 0x07, "ffa", {{{0, "Bot (Easy)"}, {1, "Bot (Medium)"}}}},                // a word for the leader's own seat (Hard at Blue): a person holds it, the word counts for nothing
        };
        for (const Row& row : rows) {
            Server server(limits);
            Application leader;
            ApplicationConfig leader_cfg = join_config(server, row.code, "Ann", row.seat, net::FillPlan(row.plan), 1);
            leader_cfg.net_create = card_block(row.teams);
            ASSERT_TRUE(leader.init(leader_cfg));
            Hall hall{server, &leader, {}};
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing; }, 20000));                // (the hook pressed START by itself: one person and the plan's bots)
            ASSERT_EQ(leader.net()->my_seat(), row.seat);                                                        // (the room of four took the color that was asked for, whatever it is)
            server::RoomStatus s = server.status(row.code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 2);
            for (size_t i = 0; i < 2; ++i) ASSERT_TRUE(s.bots[i].seat == row.bots[i].first && s.bots[i].name == bn(row.code, row.bots[i].first, std::string(row.bots[i].second).substr(5, std::string(row.bots[i].second).size() - 6)) && s.bots[i].fill);
            ASSERT_EQ(s.names[row.seat], std::string("Ann"));
            ASSERT_EQ(leader.sim().roster_mask(), row.mask);                                                     // the person and the two bots play; the fourth seat stays empty
            ASSERT_EQ(s.teams, std::string(row.teams));
            ASSERT_EQ(server.status(row.code).ignored_start_requests, 0u);                                       // (one START, and it was the leader's)
            hall.step(kDialogMs + 1500);
            s = server.status(row.code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.ticks > 20 && !leader.net()->desynced());
            leader.quit();
        }
    } TEST_END();

    TEST_CASE("N5.85 One Card, A Friend Row: The Match Starts When The Friend Is In (--start-when 2), Whichever Of The Two Connected First And So Leads: The Same Roster, The Plan's Bot, The Seat Of The Nobody Row Empty, The Room's Teams; The Game That Does Not Lead Sends Nothing (No START Of Its Own Is Heard)") {
        for (const bool host_first : {true, false}) {
            Server server(limits);
            const std::string code = "tiny01ab";                                        // Green + Red are a team (the block of every link says so): Red is a bot of the plan, Green the host
            const net::FillPlan plan(std::array<L, 4>{L::None, L::Medium, L::None, L::None});          // Red a Medium bot, Blue is Nobody, Black is the friend's (none): the host is Green (none)
            ApplicationConfig host_cfg = join_config(server, code, "Host", 0, plan, 2);
            ApplicationConfig friend_cfg = join_config(server, code, "Pal", 3, plan, 2);                // the same plan and the same number for everybody
            host_cfg.net_create = card_block("0+1");                                                    // (and the same block: the Hello of the one who connects first makes the room, the other's is not read)
            friend_cfg.net_create = card_block("0+1");
            Application first;
            Application second;
            ASSERT_TRUE(first.init(host_first ? host_cfg : friend_cfg));
            Hall hall{server, &first, {}};
            ASSERT_TRUE(hall.until([&]() { return first.net()->phase() == net::NetGame::Phase::Room && first.net()->is_leader(); }, 8000));
            hall.step(3000);                                                                            // alone in the room: the hook wants two people, nothing is pressed
            ASSERT_TRUE(server.status(code).state == server::RoomState::Waiting && server.status(code).joined == 1 && server.status(code).ignored_start_requests == 0);
            ASSERT_EQ(first.state(), AppState::MapSelect);
            ASSERT_TRUE(second.init(host_first ? friend_cfg : host_cfg));
            hall.second = &second;
            bool second_led = false;                                                                    // (the leader's flag is only there while the room waits: it is looked at at every step)
            ASSERT_TRUE(hall.until([&]() { second_led = second_led || second.net()->is_leader(); return first.state() == AppState::Playing && second.state() == AppState::Playing; }, 20000));
            ASSERT_FALSE(second_led);
            server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.size() == 1);
            ASSERT_TRUE(s.bots[0].seat == 1 && s.bots[0].name == bn(code, 1, "Medium") && s.bots[0].fill);
            ASSERT_TRUE(s.names[0] == "Host" && s.names[3] == "Pal" && s.names[2].empty());              // (the Nobody seat stays empty: the room of four started with three)
            ASSERT_EQ(first.net()->my_seat(), host_first ? uint8_t{0} : uint8_t{3});
            ASSERT_EQ(second.net()->my_seat(), host_first ? uint8_t{3} : uint8_t{0});
            ASSERT_EQ(s.ignored_start_requests, 0u);                                                     // the game that does not lead pressed nothing (a START of a non-leader is ignored and counted)
            ASSERT_TRUE(first.sim().roster_mask() == 0x0B && second.sim().roster_mask() == 0x0B);
            ASSERT_EQ(s.teams, std::string("0+1"));                                                      // the room's team, made at this START
            ASSERT_TRUE(s.allies[0] == 1 && s.allies[1] == 0 && s.allies[3] == sim::ALLIANCE_NONE);
            hall.step(kDialogMs + 1500);
            ASSERT_TRUE(hall.identical(first.sim(), second.sim()));
            ASSERT_FALSE(first.net()->desynced() || second.net()->desynced());
            first.quit();
            second.quit();
        }
    } TEST_END();

    TEST_CASE("N5.86 The Setup Screen After The Results Of A Room's Match Is The Local Game's, Also When The Room Was Left First (The Old Order Of The Browser's Leave Game): Not The Room That Was Left - Its START Starts A New Game Instead Of The Can't-Go Cue") {
        const auto click_start = [](Application& app) {
            const int32_t x = MapSelectScreen::BTN_START_X + 5;
            const int32_t y = MapSelectScreen::BTN_START_Y + 5;
            app.map_select().handle_mouse_motion(x, y);
            app.map_select().handle_mouse_down(x, y, 1);
            app.map_select().handle_mouse_up(x, y, 1);
        };
        Server server(limits);
        const std::string code = "abcdef";
        net::CreateBlock block;                                                                    // (this Hello makes the room: TINY, four seats)
        block.map_name = "TINY.LVL";
        block.seats = 4;
        Application app;
        ApplicationConfig cfg = join_config(server, code, "Ann", 0, net::FillPlan(std::array<L, 4>{L::None, L::Medium, L::None, L::None}), 1);       // Green, one Medium bot at Red
        cfg.net_create = block;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{server, &app, {}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
        hall.step(kDialogMs + 500);
        Command quit;                                                                              // the quit dialog's Yes with one other side left ends the match: the results are up
        quit.type = CommandType::Quit;
        quit.issuer = app.local_player_id();
        ASSERT_EQ(app.net()->submit(quit).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(hall.until([&]() { return app.scorecard().is_open(); }, 8000));
        ASSERT_TRUE(app.network_active());
        app.leave_network_match();                                                                 // the room is left first (the seat is dropped at once: what the browser's Leave did, and left a dead screen) ...
        ASSERT_FALSE(app.network_active());
        app.return_to_map_select();                                                                // ... and the setup screen comes: it must not be the room's
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.scorecard().is_open());
        ASSERT_FALSE(app.map_select().room().networked);                                           // a local game's screen: the room that was left is not on it
        ASSERT_TRUE(app.map_select().can_change_setup());
        hall.step(500);                                                                            // (the server hears the Leave)
        click_start(app);
        ASSERT_EQ(app.state(), AppState::Playing);                                                 // START plays a new game here, it does not ask a room that is gone
        ASSERT_FALSE(app.network_active());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.86b A Match Of A Room That Is Left On Purpose (The Browser's Menu Button, The Quit Dialog's Yes) Tells The Site Statistics Nothing, Also In The Frames That The Old Page Runs Until The New One Is There: It Is Not A Game On This Computer") {
        int told = 0;
        Server server(limits);
        const std::string code = "abcdef";
        net::CreateBlock block;                                                                    // (this Hello makes the room: TINY, four seats)
        block.map_name = "TINY.LVL";
        block.seats = 4;
        Application app;
        app.set_on_local_match_started([&told]() { ++told; });
        ApplicationConfig cfg = join_config(server, code, "Ann", 0, net::FillPlan(std::array<L, 4>{L::None, L::Medium, L::Medium, L::Medium}), 1);       // Green and three Medium bots: the Yes of the dialog leaves, it does not end the match
        cfg.net_create = block;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{server, &app, {}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 20000));
        hall.step(kDialogMs + 500);
        ASSERT_TRUE(app.network_active());
        ASSERT_EQ(told, 0);
        app.leave_network_match();                                                                 // the room is left, the match screen stays (the page navigates away in the same breath)
        ASSERT_FALSE(app.network_active());
        ASSERT_EQ(app.state(), AppState::Playing);
        hall.step(300);                                                                            // the frames until the new page replaces this one: no game on this computer began
        ASSERT_EQ(told, 0);
        app.quit();
    } TEST_END();
}
