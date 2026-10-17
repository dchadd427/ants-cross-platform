// Tests of the network in the application: computer players in a room and the chat of the setup screen
#include "network_app_fixtures.hpp"


// Bots that fill the empty seats at START and chat in the waiting room (protocol 11): the application's hooks (--fill-bots, --say), the leader of a server's room and the host of a room
// on the local network
void run_room_bot_tests() {
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
    const auto lines = [](const std::vector<net::ChatLine>& v) {
        std::vector<std::string> out;
        for (const net::ChatLine& l : v) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
        return out;
    };

    TEST_CASE("N5.45 Command Line: --fill-bots none|easy|medium|hard Is The Bots That This Player's START Seats In The Empty Seats Of Its Room (Any Case; Off By Default; Anything Else Or No Value Refuses To Start), --say TEXT Is A Test Hook That Says A Line In The Waiting Room") {
        std::vector<std::string> args;
        std::vector<char*> st;
        args = {"ants", "--join", "127.0.0.1:4001", "--room", "R-1"};
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.fill_bots == net::FillLevel::None && c.startup_error.empty() && c.net_say.empty());          // off by default: the START of every earlier version
        const std::pair<const char*, net::FillLevel> good[] = {{"none", net::FillLevel::None}, {"easy", net::FillLevel::Easy}, {"medium", net::FillLevel::Medium}, {"hard", net::FillLevel::Hard},
                                                                {"HARD", net::FillLevel::Hard}, {"Medium", net::FillLevel::Medium}};
        for (const auto& g : good) {
            args = {"ants", "--fill-bots", g.first, "--host"};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.fill_bots == g.second && c.startup_error.empty());
        }
        for (const char* bad : {"harder", "", "1", "easy medium", "none,easy"}) {
            args = {"ants", "--fill-bots", bad};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_TRUE(c.fill_bots == net::FillLevel::None && c.startup_error.find("--fill-bots") != std::string::npos);
            Application refuses;
            ASSERT_FALSE(refuses.init(c));                                                    // refused at startup, with the reason on stderr
        }
        args = {"ants", "--fill-bots"};                                                       // no value
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.startup_error.find("--fill-bots") != std::string::npos);
        args = {"ants", "--say", "hello there", "--join", "127.0.0.1:4001"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_say, std::string("hello there"));
    } TEST_END();

    TEST_CASE("N5.86 Command Line (Protocol 15): --room-map, --room-seats, --room-teams And --room-leader-start Make The Create Block Of A Join, --platform The Platform Byte Of Its Hello; Not Given Is No Block And No Byte (The Hello Only Joins); Anything Else Refuses To Start") {
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parse = [&](std::vector<std::string> a) {
            args = std::move(a);
            return Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        };
        const std::vector<std::string> join = {"ants", "--join", "127.0.0.1:4001", "--room", "k7m2xq"};
        const auto with = [&](std::vector<std::string> more) {
            std::vector<std::string> all = join;
            all.insert(all.end(), more.begin(), more.end());
            return parse(all);
        };
        ApplicationConfig c = parse(join);
        ASSERT_TRUE(!c.net_create.has_value() && c.net_platform == 0 && c.startup_error.empty());          // not given: the Hello only joins, and says the build's own platform
        // --room-map: a plain word is the original's file of that name (any case), a name with an extension is kept as it is, and the block's other fields stay what a block has by default
        const std::pair<const char*, const char*> maps[] = {{"treasure", "TREASURE.LVL"}, {"ISLANDS", "ISLANDS.LVL"}, {"Tiny", "TINY.LVL"}, {"my-map.lvl", "my-map.lvl"}, {"BIG MAP.LVL", "BIG MAP.LVL"}, {"x y", "X Y.LVL"}};
        for (const auto& m : maps) {
            c = with({"--room-map", m.first});
            ASSERT_TRUE(c.startup_error.empty() && c.net_create.has_value());
            ASSERT_EQ(c.net_create->map_name, std::string(m.second));
            ASSERT_TRUE(c.net_create->seats == 4 && c.net_create->team_a == net::kNoTeam && c.net_create->team_b == net::kNoTeam && c.net_create->flags == 0);
            ASSERT_TRUE(net::valid_create_block(*c.net_create));
        }
        for (const char* bad : {"", "a/b", "a\\b", "a:b", "a?b", "a*b", "a\"b", "a<b", "a>b", "a|b", ".hidden.lvl", "my.map", "x.txt", "caf\xC3\xA9"}) {
            c = with({"--room-map", bad});
            ASSERT_TRUE(c.startup_error.find("--room-map") != std::string::npos && !c.net_create.has_value());
        }
        c = with({"--room-map"});                                                                           // no value
        ASSERT_TRUE(c.startup_error.find("--room-map") != std::string::npos);
        // --room-seats: 2, 3 or 4 and nothing else (one digit: "3x" and "2.5" are no number of seats)
        for (const int seats : {2, 3, 4}) {
            c = with({"--room-seats", std::to_string(seats)});
            ASSERT_TRUE(c.startup_error.empty() && c.net_create.has_value() && c.net_create->seats == seats && c.net_create->map_name.empty());
        }
        for (const char* bad : {"1", "5", "0", "9", "-3", "3x", "2.5", "22", " 3", "three", ""}) {
            c = with({"--room-seats", bad});
            ASSERT_TRUE(c.startup_error.find("--room-seats") != std::string::npos && !c.net_create.has_value());
        }
        c = with({"--room-seats"});
        ASSERT_TRUE(c.startup_error.find("--room-seats") != std::string::npos);
        // --room-teams: ffa, or two different seats of 0 to 3 (the lower one first in the block: a pair has one spelling); the room judges the pair against the seats that play
        const std::pair<const char*, std::pair<int, int>> teams[] = {{"0+1", {0, 1}}, {"1+0", {0, 1}}, {"2+3", {2, 3}}, {"0+3", {0, 3}}, {"ffa", {255, 255}}, {"FFA", {255, 255}}};
        for (const auto& t : teams) {
            c = with({"--room-teams", t.first});
            ASSERT_TRUE(c.startup_error.empty() && c.net_create.has_value());
            ASSERT_TRUE(c.net_create->team_a == t.second.first && c.net_create->team_b == t.second.second);
            ASSERT_TRUE(net::valid_create_block(*c.net_create));
        }
        for (const char* bad : {"0+0", "0+4", "4+0", "01", "0-1", "x+y", "0+1+2", "", "none"}) {
            c = with({"--room-teams", bad});
            ASSERT_TRUE(c.startup_error.find("--room-teams") != std::string::npos && !c.net_create.has_value());
        }
        c = with({"--room-teams"});
        ASSERT_TRUE(c.startup_error.find("--room-teams") != std::string::npos);
        // --room-leader-start: a flag, and the only one that a block has
        c = with({"--room-leader-start"});
        ASSERT_TRUE(c.startup_error.empty() && c.net_create.has_value() && c.net_create->flags == net::kCreateLeaderStarts && c.net_create->leader_starts() && net::valid_create_block(*c.net_create));
        // all four, in any order: one block
        net::CreateBlock expect;
        expect.map_name = "SMALL.LVL";
        expect.seats = 3;
        expect.team_a = 0;
        expect.team_b = 1;
        expect.flags = net::kCreateLeaderStarts;
        c = with({"--room-leader-start", "--room-teams", "1+0", "--room-seats", "3", "--room-map", "small"});
        ASSERT_TRUE(c.startup_error.empty() && c.net_create.has_value() && *c.net_create == expect);
        c = with({"--room-map", "small", "--room-seats", "3", "--room-teams", "0+1", "--room-leader-start"});
        ASSERT_TRUE(c.startup_error.empty() && c.net_create.has_value() && *c.net_create == expect);
        // --platform: the six systems, and "browser-" before one for a game in a page (any case)
        const std::pair<const char*, uint8_t> platforms[] = {
            {"windows", net::kOsWindows}, {"macos", net::kOsMacos}, {"Linux", net::kOsLinux}, {"ANDROID", net::kOsAndroid}, {"ios", net::kOsIos}, {"other", net::kOsOther},
            {"browser-windows", static_cast<uint8_t>(net::kPlatformBrowser | net::kOsWindows)}, {"Browser-Linux", static_cast<uint8_t>(net::kPlatformBrowser | net::kOsLinux)},
            {"browser-other", static_cast<uint8_t>(net::kPlatformBrowser | net::kOsOther)}};
        for (const auto& p : platforms) {
            c = with({"--platform", p.first});
            ASSERT_TRUE(c.startup_error.empty() && c.net_platform == p.second && net::valid_platform(c.net_platform) && !c.net_create.has_value());
        }
        for (const char* bad : {"plan9", "", "browser", "browser-", "browser-plan9", "win", "windows ", "0"}) {
            c = with({"--platform", bad});
            ASSERT_TRUE(c.startup_error.find("--platform") != std::string::npos && c.net_platform == 0);
        }
        c = with({"--platform"});
        ASSERT_TRUE(c.startup_error.find("--platform") != std::string::npos);
        // a game that is told something wrong refuses to start, with the reason on stderr
        c = with({"--room-seats", "7"});
        Application refuses;
        ASSERT_FALSE(refuses.init(c));
    } TEST_END();

    TEST_CASE("N5.46 Leader With --fill-bots And --say: The Line Is Said Once When Two Players Are In The Room (The Other Player Hears It With The Leader's Name, The Leader Hears The Reply), The Leader's START Seats Bots In The Two Empty Seats (The Leader Did Not Need A Third Person), The Match Runs With Four Teams And The Two Machines Stay Identical; The Lines Of The Waiting Room Are Kept") {
        Server server;
        ASSERT_TRUE(server.make_room("FILL-APP", 4));
        ApplicationConfig cfg = join_config(server, "FILL-APP", "Leader");
        cfg.fill_bots = net::FillLevel::Hard;
        cfg.net_say = "hello from the leader";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_EQ(app.net()->fill_bots(), net::FillLevel::Hard);
        app.set_fill_bots(net::FillLevel::Easy);                                              // the screens' way: the player chooses, any time before START
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::Easy && app.net()->fill_bots() == net::FillLevel::Easy);
        app.set_fill_bots(net::FillLevel::Hard);
        ASSERT_TRUE(app.fill_bots() == net::FillLevel::Hard && app.net()->fill_bots() == net::FillLevel::Hard);
        hall.step(1500);
        ASSERT_TRUE(app.net()->pregame_chat().empty());                                       // alone, the hook waits for somebody to hear it
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "FILL-APP"));
        ASSERT_TRUE(hall.until([&]() { return !bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|hello from the leader"}));
        ASSERT_EQ(lines(app.net()->pregame_chat()), lines(bob.net.pregame_chat()));          // the room tells the leader its own line as well
        hall.step(2000);
        ASSERT_EQ(bob.net.pregame_chat().size(), size_t{1});                                  // once, not again and again
        ASSERT_TRUE(bob.net.chat("hi leader"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 2; }, 8000));
        ASSERT_EQ(lines(app.net()->pregame_chat()).back(), std::string("1|Bob|hi leader"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        click_start(app);                                                                     // the leader's START: the request carries the fill level
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        const server::RoomStatus s = server.status("FILL-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.expected == 4 && s.bots.size() == 2);
        ASSERT_TRUE(s.names[0] == "Leader" && s.names[1] == "Bob" && s.names[2] == bn("FILL-APP", 2, "Hard") && s.names[3] == bn("FILL-APP", 3, "Hard"));
        ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[0].level == "hard" && s.bots[0].fill && s.bots[1].seat == 3);
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);
        ASSERT_EQ(bob.sim.roster_mask(), 0x0F);
        ASSERT_EQ(app.sim().get_player_name(3), bn("FILL-APP", 3, "Hard"));                                // the screens name the bots as bots
        ASSERT_TRUE(app.bots() == nullptr);                                                   // a guest runs no bot: the server does
        hall.step(6000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim));
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        ASSERT_TRUE(server.status("FILL-APP").state == server::RoomState::Running);          // the referee agrees
        ASSERT_EQ(app.net()->pregame_chat().size(), size_t{2});                              // the waiting room's lines are still there for the match's log
        app.quit();
        ASSERT_FALSE(app.network_active());
    } TEST_END();

    TEST_CASE("N5.47 Leader Alone With --fill-bots: START Is Not The Can't-Go Cue Any More (One Person Is Enough): The Server Seats Three Bots And Starts; With Fog Of War In The Room The Leader's Status Line Says Bots Cannot Play With It And The Room Waits") {
        {
            Server server;
            ASSERT_TRUE(server.make_room("ALONE-APP", 4));
            ApplicationConfig cfg = join_config(server, "ALONE-APP", "Solo");
            cfg.fill_bots = net::FillLevel::Medium;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            const size_t channels = app.audio_mixer().active_channel_count();
            click_start(app);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);               // the click only: no can't-go cue
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing; }, 15000));
            const server::RoomStatus s = server.status("ALONE-APP");
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 3 && s.names[0] == "Solo");
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            hall.step(kDialogMs + 4000);                                                     // (the server seals the first turn kDialogMs after the match began: 4 s of play follow)
            ASSERT_FALSE(app.net()->desynced());
            ASSERT_TRUE(server.status("ALONE-APP").ticks > 40);
            app.quit();
        }
        {   // without a fill the same START is the can't-go cue, as before
            Server server;
            ASSERT_TRUE(server.make_room("ALONE-NOFILL", 4));
            Application app;
            ASSERT_TRUE(app.init(join_config(server, "ALONE-NOFILL", "Solo")));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            const size_t channels = app.audio_mixer().active_channel_count();
            click_start(app);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 2);               // the click and the cue
            hall.step(1000);
            ASSERT_TRUE(server.status("ALONE-NOFILL").state == server::RoomState::Waiting && server.status("ALONE-NOFILL").bots.empty());
        }
        {   // Fog of War: the server refuses the bots and says so; the room is not started
            Server server;
            server::RoomSpec spec;
            spec.code = "ALONE-FOG";
            spec.map = "TINY.LVL";
            spec.players = 4;
            spec.fog = true;
            ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
            ApplicationConfig cfg = join_config(server, "ALONE-FOG", "Solo");
            cfg.fill_bots = net::FillLevel::Easy;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            click_start(app);
            ASSERT_TRUE(hall.until([&]() { return app.net()->status_text() == net::kNoticeFillFog; }, 8000));      // the notice of the room is on the status line
            ASSERT_EQ(app.map_select().room().status, std::string(net::kNoticeFillFog));
            ASSERT_TRUE(app.net()->pregame_chat().size() == 1 && app.net()->pregame_chat()[0].notice());
            hall.step(1500);
            ASSERT_TRUE(server.status("ALONE-FOG").state == server::RoomState::Waiting && server.status("ALONE-FOG").bots.empty());
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }
    } TEST_END();

    TEST_CASE("N5.48 Host Of A Room On The Local Network With --fill-bots: START Seats Bots In The Empty Seats And This Machine Runs Them (The Controller Holds Exactly Those Seats); A Start That Is Cancelled Takes Them Out Again; Fog Of War Seats None") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.fill_bots = net::FillLevel::Easy;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        {   // a guest that says Hello and never answers a ping: its thumb has not appeared, so START cannot go through (the can't-go cue), and the bots that the fill seated for it go again
            auto mute = net::TcpConnection::connect("127.0.0.1", app.net()->listen_port());
            ASSERT_TRUE(mute != nullptr);
            net::HelloMsg hello;
            hello.name = "Mute";
            bool sent = false;
            const auto pass = [&](float dt) {
                app.pump_network(dt);
                std::vector<uint8_t> unread;
                mute->poll(unread);                                                          // (it reads the host's pings and never answers them)
                if (!sent && mute->is_open()) sent = mute->send(net::encode(hello));
            };
            for (int i = 0; i < 800 && app.net()->room().slots[1].state != net::SlotState::Client; ++i) {
                pass(0.010f);
                ants_test::short_pause();
            }
            ants_test::real_time_tail([&]() { return app.net()->room().slots[1].state == net::SlotState::Client; }, [&]() { pass(0.0f); });
            ASSERT_TRUE(app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms == net::kRttUnknown);
            const size_t channels = app.audio_mixer().active_channel_count();
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.audio_mixer().active_channel_count(), channels + 1);               // the cue (sound 63)
            ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Empty && app.net()->room().slots[3].state == net::SlotState::Empty);
            ASSERT_FALSE(app.map_select().is_locked());
            ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
            mute->close();
            for (int i = 0; i < 800 && app.net()->room().slots[1].state != net::SlotState::Empty; ++i) {
                app.pump_network(0.010f);
                ants_test::short_pause();
            }
            ants_test::real_time_tail([&]() { return app.net()->room().slots[1].state == net::SlotState::Empty; }, [&]() { app.pump_network(0.0f); });
            ASSERT_TRUE(app.net()->room().slots[1].state == net::SlotState::Empty);
        }
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        // Bob's machine cannot load the map: the start is cancelled and the bots that it seated go again
        bob.hold_report = true;
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return bob.report_pending; }, 8000));
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Bot && app.net()->room().slots[3].state == net::SlotState::Bot);
        ASSERT_EQ(app.net()->room().slots[2].name, "Bot (Easy)");
        bob.report_pending = false;
        bob.net.report_loaded(false);
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && !app.map_select().is_locked(); }, 8000));
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Empty && app.net()->room().slots[3].state == net::SlotState::Empty);
        ASSERT_TRUE(duo.until([&]() { return bob.net.room().slots[2].state == net::SlotState::Empty; }, 3000));                  // Bob sees the room without them
        // the same START with a machine that loads: two people and two bots, the host's machine runs the bots
        bob.hold_report = false;
        duo.step(500);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);
        ASSERT_EQ(bob.sim.roster_mask(), 0x0F);
        ASSERT_TRUE(app.bots() != nullptr);
        ASSERT_EQ(app.bots()->seat_mask(), 0x0C);                                            // exactly the seats that the fill took: 2 and 3
        ASSERT_TRUE(app.sim().get_player_name(2) == "Bot (Easy)" && app.sim().get_player_name(3) == "Bot (Easy)");
        duo.step(8000);
        ASSERT_TRUE(app.bots()->stats(2).decisions > 0 && app.bots()->stats(3).decisions > 0);
        ASSERT_EQ(app.bots()->stats(2).rejected, 0u);
        bool same = false;                                                                   // both machines stand at the same tick at some moment: their states are then equal
        for (int i = 0; i < 400 && !same; ++i) {
            if (app.sim().current_tick() == bob.sim.current_tick()) {
                ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
                same = true;
            }
            duo.step(10);
        }
        ASSERT_TRUE(same);
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
    } TEST_END();
    TEST_CASE("N5.77 Command Line: --fill-bots Takes One Word (Every Seat) Or Four Joined By Commas (The Seats 0 To 3: A Level For Each Seat, Protocol 13), Any Case; Anything Else Is A Startup Error That Names The Option And Says What Is Wrong; The Application's Setters Take A Plan Or One Level") {
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parsed = [&](std::vector<std::string> a) {
            args = std::move(a);
            return Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        };
        using L = net::FillLevel;
        const std::pair<const char*, net::FillPlan> good[] = {
            {"easy", net::FillPlan(L::Easy)},
            {"none,none,easy,hard", net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard})},
            {"NONE,Easy,MEDIUM,hard", net::FillPlan(std::array<L, 4>{L::None, L::Easy, L::Medium, L::Hard})},
            {"hard,hard,hard,hard", net::FillPlan(L::Hard)},
            {"none,none,none,none", net::FillPlan()},
        };
        for (const auto& g : good) {
            const ApplicationConfig c = parsed({"ants", "--host", "--fill-bots", g.first});
            ASSERT_TRUE(c.startup_error.empty() && c.fill_bots == g.second);
        }
        ASSERT_TRUE(parsed({"ants", "--fill-bots", "none,none,easy,hard", "--fill-bots", "medium", "--host"}).fill_bots == net::FillPlan(L::Medium));        // the last one wins
        for (const char* bad : {"none,easy", "none,none,easy", "none,none,easy,hard,hard", "none,,easy,hard", "none, none, easy, hard", ",none,easy,hard", "easy,", "none;none;easy;hard", "harder,easy,easy,easy", "1,2,3,4"}) {
            const ApplicationConfig c = parsed({"ants", "--host", "--fill-bots", bad});
            ASSERT_TRUE(c.fill_bots == net::FillPlan() && c.startup_error.find(std::string("--fill-bots ") + bad + ": ") == 0 && c.startup_error.size() > std::string("--fill-bots ") .size() + std::string(bad).size() + 4);
            Application refuses;
            ASSERT_FALSE(refuses.init(c));
        }
        const ApplicationConfig missing = parsed({"ants", "--fill-bots"});
        ASSERT_TRUE(missing.startup_error.find("--fill-bots needs none, easy, medium or hard") == 0 && missing.startup_error.find("four") != std::string::npos);
        {   // the setters: a plan, or one level (every seat); the NetGame has it
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
            cfg.teams = LocalTeams{true, 0, 2};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.net()->fill_bots() == cfg.fill_bots && app.net()->start_teams() == LocalTeams({true, 0, 2}));      // the machine's START carries both choices
            app.set_fill_bots(L::Medium);
            ASSERT_TRUE(app.fill_bots() == net::FillPlan(L::Medium) && app.net()->fill_bots() == net::FillPlan(L::Medium));
            app.set_fill_bots(net::FillPlan(std::array<L, 4>{L::None, L::Hard, L::None, L::None}));
            ASSERT_TRUE(app.fill_bots() == net::FillPlan(std::array<L, 4>{L::None, L::Hard, L::None, L::None}) && app.net()->fill_bots() == app.fill_bots());
            app.set_start_teams(LocalTeams{true, 1, 2});
            ASSERT_TRUE(app.start_teams() == LocalTeams({true, 1, 2}) && app.net()->start_teams() == LocalTeams({true, 1, 2}));
            app.set_start_teams(LocalTeams{});
            ASSERT_TRUE(!app.start_teams().set && !app.net()->start_teams().set);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.78 Protocol 13, A Server's Room With Two Games, Two Bots Of Their Own Levels And The Leader's Teams: The Prompt And The Footer Say The Plan; Every Engine (The Referee's, Both Machines') Has The Alliances Before Its First Tick, Each Game's Chat Log Has The News Flash Once For Each Pair, The Hashes Agree, Nobody Is Told That Teams Could Not Be Made") {
        using L = net::FillLevel;
        Server server;
        ASSERT_TRUE(server.make_room("TEAMS-APP", 4));
        ApplicationConfig lead_cfg = join_config(server, "TEAMS-APP", "Leader");
        lead_cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});       // Blue gets an Easy bot, Black a Hard bot (seat 1 is the guest's)
        lead_cfg.teams = LocalTeams{true, 0, 2};                                                         // Green + Blue against Red + Black: the leader and the Easy bot against the guest and the Hard bot
        Application leader;
        ASSERT_TRUE(leader.init(lead_cfg));
        Hall hall{server, &leader, {}};
        ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
        Application guest;
        ASSERT_TRUE(guest.init(join_config(server, "TEAMS-APP", "Guest")));
        hall.second = &guest;
        ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client && leader.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        hall.step(5300);                                                                                  // (the first five seconds of company say how to change a color: the plan's line is back after them)
        // what the leader is told before START: the longest way of saying it that fits the label
        const std::vector<std::string>& prompts = leader.net()->prompt_texts();
        ASSERT_TRUE(prompts.size() >= 4 && prompts.front() == "Press START: Blue gets an Easy bot, Black a Hard bot; teams Green + Blue against Red + Black.");
        const std::string shown = leader.map_select().room().status;
        ASSERT_TRUE(std::find(prompts.begin(), prompts.end(), shown) != prompts.end() && leader.map_select().prompt_fits(leader.renderer(), shown));
        for (const std::string& text : prompts) {
            if (text == shown) break;
            ASSERT_FALSE(leader.map_select().prompt_fits(leader.renderer(), text));                      // (every longer way did not fit: this is the longest that does)
        }
        ASSERT_TRUE(shown.find("Blue") != std::string::npos && shown.find("Black") != std::string::npos && shown.find("teams") != std::string::npos);
        ASSERT_EQ(guest.map_select().room().status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));      // a guest keeps its own line
        click_start(leader);
        ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
        server::RoomStatus s = server.status("TEAMS-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 2);
        ASSERT_TRUE(s.bots[0].seat == 2 && s.bots[0].level == "easy" && s.bots[0].name == bn("TEAMS-APP", 2, "Easy") && s.bots[1].seat == 3 && s.bots[1].level == "hard" && s.bots[1].name == bn("TEAMS-APP", 3, "Hard"));
        ASSERT_EQ(s.teams, std::string("0+2"));
        ASSERT_TRUE(s.allies[0] == 2 && s.allies[1] == 3 && s.allies[2] == 0 && s.allies[3] == 1);        // the referee's engine, in the dialog: tick 0
        ASSERT_EQ(s.ticks, 0u);
        for (Application* a : {&leader, &guest}) {                                                        // both games: made on the Start, before any tick
            ASSERT_EQ(a->sim().current_tick(), uint64_t{0});
            for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(a->sim().alliance_of(seat), static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1));
            ASSERT_EQ(a->sim().get_world_state().pending_invite_from[0], 255);                           // no invitation is left open (no dialog)
        }
        ASSERT_EQ(leader.net()->start_info().team_a, 0);
        ASSERT_EQ(guest.net()->start_info().team_b, 2);
        hall.step(kDialogMs + 1500);                                                                     // the match runs: the first tick, the News Flash
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        for (Application* a : {&leader, &guest}) {
            const std::string log = a->hud().chat_transcript("t");
            ASSERT_EQ(count_of(log, "are a team now!"), size_t{2});                                       // once for each pair, on each machine
            ASSERT_TRUE(log.find("Leader (Green) and " + bn("TEAMS-APP", 2, "Easy") + " (Blue) are a team now!") != std::string::npos);
            ASSERT_TRUE(log.find("Guest (Red) and " + bn("TEAMS-APP", 3, "Hard") + " (Black) are a team now!") != std::string::npos);
            ASSERT_TRUE(a->net()->pregame_chat().empty() || !a->net()->pregame_chat().back().notice());      // nobody was told that teams could not be made
        }
        ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
        ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced());
        hall.step(3000);
        s = server.status("TEAMS-APP");
        ASSERT_TRUE(s.state == server::RoomState::Running && s.ticks > 60 && s.allies[0] == 2 && s.allies[3] == 1);        // (the referee agrees to the hash of every 20th turn: a room that disagreed would have failed)
        ASSERT_TRUE(leader.bots() == nullptr && guest.bots() == nullptr);                                // the server runs the bots
        leader.quit();
        guest.quit();
    } TEST_END();

    TEST_CASE("N5.79 Protocol 13, The Host Of A Room On The Local Network: START Seats The Bots Of Their Own Levels (This Machine Runs Them: The Controller Holds Those Seats With Those Levels) And Puts The Host's Teams Into The Start; A Guest's Engine Has The Alliances At Tick 0 And Stands At The Host's State; Teams That Cannot Be Made Say Why On The Host's Status Line And In A Notice To The Guest") {
        using L = net::FillLevel;
        {
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
            cfg.teams = LocalTeams{true, 0, 2};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            duo.step(200);
            ASSERT_EQ(app.map_select().room().status.find("Blue gets an Easy bot, Black a Hard bot") == std::string::npos, false);        // the host's prompt says the plan
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            ASSERT_EQ(app.sim().roster_mask(), 0x0F);
            ASSERT_TRUE(app.bots() != nullptr);
            ASSERT_EQ(app.bots()->seat_mask(), 0x0C);                                                  // exactly seats 2 and 3, each with its own level
            ASSERT_TRUE(app.bots()->profile(2) != nullptr && app.bots()->profile(3) != nullptr);
            ASSERT_EQ(app.bots()->profile(2)->rate_milli_cps, ai::profile_for(ai::Level::Easy).rate_milli_cps);
            ASSERT_EQ(app.bots()->profile(3)->rate_milli_cps, ai::profile_for(ai::Level::Hard).rate_milli_cps);
            ASSERT_TRUE(ai::profile_for(ai::Level::Easy).rate_milli_cps != ai::profile_for(ai::Level::Hard).rate_milli_cps);
            ASSERT_TRUE(app.sim().get_player_name(2) == "Bot (Easy)" && app.sim().get_player_name(3) == "Bot (Hard)");
            ASSERT_EQ(app.net()->start_info().team_a, 0);
            ASSERT_EQ(bob.net.start_info().team_b, 2);
            ASSERT_EQ(app.sim().current_tick(), uint64_t{0});
            for (uint8_t seat = 0; seat < 4; ++seat) {
                const uint8_t want = static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1);
                ASSERT_TRUE(app.sim().alliance_of(seat) == want && bob.sim.alliance_of(seat) == want);
            }
            duo.step(kDialogMs + 3000);
            const std::string log = app.hud().chat_transcript("t");
            size_t told = 0;
            for (size_t at = log.find("are a team now!"); at != std::string::npos; at = log.find("are a team now!", at + 1)) ++told;
            ASSERT_EQ(told, size_t{2});
            ASSERT_TRUE(app.bots()->stats(2).decisions > 0 && app.bots()->stats(3).decisions > 0);
            ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() == bob.sim.current_tick(); }, 4000));
            ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
            ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
            for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(app.sim().alliance_of(seat), static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1));       // (the standard bots never broke a team)
            app.quit();
        }
        {   // teams that cannot be made: two people play (the host and Bob, no bots), the team would be both of them
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.teams = LocalTeams{true, 0, 1};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            duo.step(200);
            ASSERT_TRUE(app.map_select().room().status.find("teams Green + Red") != std::string::npos);                                // the prompt says the choice
            app.map_select().handle_key_down(SDLK_RETURN);
            const std::string text = std::string(net::kNoticeNoTeams) + "only two seats play: a team of them would end the match at once.";
            ASSERT_EQ(app.net()->status_text(), text);
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing && !bob.net.pregame_chat().empty(); }, 8000));
            ASSERT_TRUE(bob.net.pregame_chat().size() == 1 && bob.net.pregame_chat()[0].notice() && bob.net.pregame_chat()[0].text == text);
            for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_TRUE(app.sim().alliance_of(seat) == sim::ALLIANCE_NONE && bob.sim.alliance_of(seat) == sim::ALLIANCE_NONE);
            ASSERT_EQ(app.net()->start_info().team_a, net::kNoTeam);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.80 Protocol 13, The Texts Fit Their Places With The Real Font: The Leader's Prompt Has A Way Of Being Said That Fits The Two Lines Of The Status Label On The Classic Page (293 px) And On The 16:9 Page (363 px) For Every Plan And Every Teams Choice; Every Line Of The Footer Fits The Players' Status Box At 12 px At The Latest (18 px For The Footer Of Protocol 11)") {
        using L = net::FillLevel;
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        const Renderer& r = app.renderer();
        std::vector<net::FillPlan> plans = {net::FillPlan(), net::FillPlan(L::Easy), net::FillPlan(L::Medium), net::FillPlan(L::Hard)};
        for (const std::array<L, 3>& abc : {std::array<L, 3>{L::None, L::None, L::Hard}, std::array<L, 3>{L::Easy, L::None, L::Hard}, std::array<L, 3>{L::Easy, L::Hard, L::Medium},
                                            std::array<L, 3>{L::Medium, L::Medium, L::Medium}, std::array<L, 3>{L::Hard, L::Easy, L::Medium}, std::array<L, 3>{L::None, L::Medium, L::Easy},
                                            std::array<L, 3>{L::Medium, L::None, L::None}, std::array<L, 3>{L::None, L::Hard, L::None}, std::array<L, 3>{L::Easy, L::Easy, L::Hard},
                                            std::array<L, 3>{L::Hard, L::Medium, L::Hard}}) {
            plans.push_back(net::FillPlan(std::array<L, 4>{L::None, abc[0], abc[1], abc[2]}));        // (the host's own seat has no level)
        }
        std::vector<sim::StartTeams> teams = {sim::StartTeams{}};
        for (uint8_t a = 0; a < 4; ++a) {
            for (uint8_t b = 0; b < 4; ++b) {
                if (a != b) teams.push_back(sim::StartTeams{true, a, b});
            }
        }
        const std::vector<std::vector<uint8_t>> rooms = {{0}, {0, 2}, {1, 2, 3}, {0, 1, 2, 3}};
        using MS = MapSelectScreen;
        size_t checked = 0;
        size_t widest_classic = 0;
        size_t fitting = 0;
        size_t too_long = 0;
        int32_t widest_footer = 0;
        for (const net::FillPlan& plan : plans) {
            for (const sim::StartTeams& t : teams) {
                for (const std::vector<uint8_t>& people : rooms) {
                    for (const bool fog : {false, true}) {
                        net::RoomMsg room;
                        for (const uint8_t seat : people) room.slots[seat] = {net::SlotState::Client, "P", 10};
                      for (const bool room_owned : {false, true}) {                                 // (the teams are this START's own choice, or the room's own)
                        const std::vector<std::string> prompts = net::NetGame::start_prompt_texts(plan, t, room, fog, room_owned);
                        if (!prompts.empty()) {
                            // the shortest way of saying it fits the two lines of the classic label (293 px) and of the wide one (363 px); the longest that fits is what a screen picks
                            ASSERT_TRUE(wrap_label_text(r, prompts.back(), 293, FontSize::Px14).size() <= 2);
                            ASSERT_TRUE(wrap_label_text(r, prompts.back(), 363, FontSize::Px14).size() <= 2);
                            widest_classic = std::max(widest_classic, wrap_label_text(r, prompts.back(), 293, FontSize::Px14).size());
                            for (size_t i = 1; i < prompts.size(); ++i) ASSERT_TRUE(prompts[i].size() <= prompts[i - 1].size());
                            for (const std::string& text : prompts) {                              // the screen's rule is the label's own measure: two lines of 293 px on this (the classic) page
                                const bool two_lines = wrap_label_text(r, text, 293, FontSize::Px14).size() <= 2;
                                ASSERT_EQ(app.map_select().prompt_fits(r, text), two_lines);
                                (two_lines ? fitting : too_long) += 1;
                            }
                        }
                        const net::NetGame::FooterTexts footer = net::NetGame::start_footer(plan, t, room, fog, room_owned);
                        for (const std::vector<std::string>& line : footer.line) {
                            if (line.empty()) continue;
                            bool fits = false;
                            for (const std::string& text : line) {
                                const int32_t w = r.get_text_width(text, FontSize::Px12);
                                widest_footer = std::max(widest_footer, w);
                                fits = fits || w <= MS::footer_width();
                            }
                            ASSERT_TRUE(fits);                                                      // some way of saying the line fits the box at 12 px
                        }
                        ++checked;
                      }
                    }
                }
            }
        }
        ASSERT_TRUE(checked > 1000 && widest_footer > 0);
        ASSERT_TRUE(fitting > 1000 && too_long > 20);                                                // (the rule above met both answers: the long ways of a plan of three bots and teams do not fit)
        ASSERT_EQ(widest_classic, size_t{2});                                                        // (the shortest ways take the two lines that the label has, and no more)
        // the footer of protocol 11 is at 18 px: its lines fit the box at the size that the screen has always used
        ASSERT_EQ(MS::footer_font(r, "Empty seats at START:"), FontSize::Px18);
        ASSERT_EQ(MS::footer_font(r, "Medium bots"), FontSize::Px18);
        ASSERT_EQ(MS::footer_font(r, "Blue Easy, Black Hard"), FontSize::Px18);
        ASSERT_TRUE(MS::footer_font(r, "Teams: Green + Red against Blue + Black") != FontSize::Px18);
        ASSERT_EQ(MS::footer_font(r, std::string(80, 'W')), FontSize::Px12);                         // (what fits at no size is cut at 12 px by the screen)
        app.quit();
    } TEST_END();

    TEST_CASE("N5.81 Protocol 13, The Room's Own Teams In The Application (The Create Block That Made The Room Names Them): A Leader Is Told That They Are The Room's And Its Own --teams Is Not What The Room Makes (The Referee's, Both Games' Engines Have The Room's Alliances Before Their First Tick, The News Flash Comes Once For Each Pair); A Room Of Three Fills Up With Two Games And A Bare Machine And Starts By Itself With The Room's Teams On Every Engine, Nobody Having Pressed START") {
        using L = net::FillLevel;
        server::ServerLimits limits;
        limits.demo_rooms = 4;
        limits.demo_map = "TINY.LVL";
        limits.demo_maps = {"TINY.LVL"};
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        const auto tiny_block = [](uint8_t seats, uint8_t a, uint8_t b) {                                // (the create block of a leader whose Hello makes the room: TINY, `seats` seats, the pair a + b)
            net::CreateBlock block;
            block.map_name = "TINY.LVL";
            block.seats = seats;
            block.team_a = a;
            block.team_b = b;
            return block;
        };
        {   // a room of four that its create block gives Green + Red; the leader's own choice (--teams) is Red + Blue, and its START seats two bots
            Server server(limits);
            const std::string code = "k7m2xq";
            ApplicationConfig lead_cfg = join_config(server, code, "Leader");
            lead_cfg.net_create = tiny_block(4, 0, 1);
            lead_cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
            lead_cfg.teams = LocalTeams{true, 1, 2};
            Application leader;
            ASSERT_TRUE(leader.init(lead_cfg));
            Hall hall{server, &leader, {}};
            ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
            Application guest;
            ASSERT_TRUE(guest.init(join_config(server, code, "Guest")));
            hall.second = &guest;
            ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room && leader.net()->room().slots[1].state == net::SlotState::Client && leader.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            hall.step(5300);                                                                              // (the first five seconds of company say how to change a color: the plan's line is back after them)
            ASSERT_TRUE(server.status(code).room_teams == "0+1" && server.status(code).state == server::RoomState::Waiting);
            ASSERT_TRUE(leader.net()->room_teams() == sim::StartTeams({true, 0, 1}) && leader.net()->effective_teams() == sim::StartTeams({true, 0, 1}));
            ASSERT_TRUE(leader.net()->start_teams() == sim::StartTeams({true, 1, 2}));                    // (its own choice is kept, and is not what the screens show or the room makes)
            ASSERT_FALSE(guest.net()->room_teams().set && guest.net()->is_leader());
            const std::vector<std::string>& prompts = leader.net()->prompt_texts();
            ASSERT_TRUE(prompts.size() >= 4 && prompts.front() == "Press START: Blue gets an Easy bot, Black a Hard bot; the room's teams: Green + Red against Blue + Black.");
            for (const std::string& text : prompts) ASSERT_TRUE(text.find("Red + Blue") == std::string::npos);        // (the leader's own pair appears nowhere)
            const std::string shown = leader.map_select().room().status;
            ASSERT_TRUE(std::find(prompts.begin(), prompts.end(), shown) != prompts.end() && leader.map_select().prompt_fits(leader.renderer(), shown));
            ASSERT_TRUE(shown.find("Green + Red") != std::string::npos && shown.find("teams") != std::string::npos);
            click_start(leader);
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
            server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 4 && s.bots.size() == 2 && s.teams == "0+1" && s.room_teams == "0+1");
            ASSERT_TRUE(s.allies[0] == 1 && s.allies[1] == 0 && s.allies[2] == 3 && s.allies[3] == 2);   // the referee: the room's pair, and the two bots as the other team (not Red + Blue)
            ASSERT_EQ(s.ticks, 0u);
            for (Application* a : {&leader, &guest}) {
                ASSERT_EQ(a->sim().current_tick(), uint64_t{0});
                for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(a->sim().alliance_of(seat), static_cast<uint8_t>(seat ^ 1u));
            }
            ASSERT_TRUE(leader.net()->start_info().team_a == 0 && leader.net()->start_info().team_b == 1 && guest.net()->start_info().team_a == 0);
            hall.step(kDialogMs + 1500);
            for (Application* a : {&leader, &guest}) {
                const std::string log = a->hud().chat_transcript("t");
                ASSERT_EQ(count_of(log, "are a team now!"), size_t{2});
                ASSERT_TRUE(log.find("Leader (Green) and Guest (Red) are a team now!") != std::string::npos);
                ASSERT_TRUE(log.find(bn(code, 2, "Easy") + " (Blue) and " + bn(code, 3, "Hard") + " (Black) are a team now!") != std::string::npos);
                ASSERT_TRUE(a->net()->pregame_chat().empty() || !a->net()->pregame_chat().back().notice());
            }
            ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
            ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced());
            hall.step(3000);
            s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.ticks > 60 && s.allies[0] == 1 && s.allies[3] == 2);        // (a room whose referee disagreed on a hash would have failed)
            leader.quit();
            guest.quit();
        }
        {   // a room of three that its create block gives Red + Blue (1 + 2): two games and a bare machine fill it, nobody presses START: it starts by itself with the room's teams, seat 0 plays alone
            Server server(limits);
            const std::string code = "m2xq9p";
            ApplicationConfig lead_cfg = join_config(server, code, "Leader");
            lead_cfg.net_create = tiny_block(3, 1, 2);
            lead_cfg.teams = LocalTeams{true, 0, 1};                                                    // (the leader's own choice: not the room's, and nobody asks the room for it)
            Application leader;
            ASSERT_TRUE(leader.init(lead_cfg));
            Peer carl;
            Hall hall{server, &leader, {&carl}};
            ASSERT_TRUE(hall.until([&]() { return leader.net()->phase() == net::NetGame::Phase::Room && leader.net()->is_leader(); }, 8000));
            Application guest;
            ASSERT_TRUE(guest.init(join_config(server, code, "Guest")));
            hall.second = &guest;
            ASSERT_TRUE(hall.until([&]() { return guest.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(server.status(code).state == server::RoomState::Waiting && server.status(code).joined == 2);
            ASSERT_TRUE(carl.net.join("127.0.0.1", server.port(), "Carl", 255, code));                    // the third seat: the room is full
            ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing && carl.net.phase() == net::NetGame::Phase::Playing; }, 20000));
            server::RoomStatus s = server.status(code);
            ASSERT_TRUE(s.state == server::RoomState::Running && s.joined == 3 && s.bots.empty() && s.teams == "1+2" && s.room_teams == "1+2");
            ASSERT_TRUE(s.allies[0] == sim::ALLIANCE_NONE && s.allies[1] == 2 && s.allies[2] == 1 && s.allies[3] == sim::ALLIANCE_NONE);
            ASSERT_EQ(s.ticks, 0u);
            for (const sim::SimulationEngine* e : {&leader.sim(), &guest.sim(), &carl.sim}) {            // both games and the bare machine
                ASSERT_EQ(e->current_tick(), uint64_t{0});
                ASSERT_TRUE(e->alliance_of(0) == sim::ALLIANCE_NONE && e->alliance_of(1) == 2 && e->alliance_of(2) == 1 && e->alliance_of(3) == sim::ALLIANCE_NONE);
            }
            ASSERT_TRUE(leader.net()->start_info().team_a == 1 && leader.net()->start_info().team_b == 2);
            hall.step(kDialogMs + 1500);
            for (Application* a : {&leader, &guest}) {
                const std::string log = a->hud().chat_transcript("t");
                ASSERT_EQ(count_of(log, "are a team now!"), size_t{1});                                   // the one pair, on each game
                ASSERT_TRUE(log.find("Guest (Red) and Carl (Blue) are a team now!") != std::string::npos);
                ASSERT_TRUE(a->net()->pregame_chat().empty() || !a->net()->pregame_chat().back().notice());
            }
            ASSERT_TRUE(hall.identical(leader.sim(), guest.sim()));
            ASSERT_FALSE(leader.net()->desynced() || guest.net()->desynced() || carl.net.desynced());
            hall.step(3000);
            ASSERT_TRUE(server.status(code).state == server::RoomState::Running && server.status(code).ticks > 60);
            leader.quit();
            guest.quit();
        }
    } TEST_END();
}

// The chat input of a room's setup screen and what the screens say about START (protocol 11): T or a click on the status line opens the input, the focus rule, the match's chat log
// that starts with the waiting room's lines, the status line's prompt for a START that seats bots
void run_room_chat_ui_tests() {
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        return cfg;
    };
    const auto lines = [](const std::vector<net::ChatLine>& v) {
        std::vector<std::string> out;
        for (const net::ChatLine& l : v) out.push_back(std::to_string(static_cast<unsigned>(l.seat)) + "|" + l.name + "|" + l.text);
        return out;
    };
    // the leader, and a second player who has joined and been measured: START works
    struct Duo2 {
        Server server;
        Application app;
        Peer bob;
        Hall hall{server, &app, {&bob}};
    };

    TEST_CASE("N5.49 The Chat Input Of A Room's Setup Screen: T Or A Click On The Status Line Opens It (The T Is Not Typed), Enter Sends The Line To The Room And Closes It, Esc Closes It; While It Is Open S, Q, X, Enter And The Arrows Do Nothing On The Screen (A Line Is Not A START Or A Leave), The Status Line Shows What Is Typed; Afterwards The Screen's Keys Work Again; Ctrl+T, A Held T And A Local Game's Screen Do Not Open It") {
        Duo2 d;
        ASSERT_TRUE(d.server.make_room("CHATUI-1", 4));
        ASSERT_TRUE(d.app.init(join_config(d.server, "CHATUI-1", "Leader")));
        ASSERT_TRUE(d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(d.bob.net.join("127.0.0.1", d.server.port(), "Bob", 255, "CHATUI-1"));
        ASSERT_TRUE(d.hall.until([&]() { return d.bob.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        d.hall.step(700);                                                                     // (the setup screen has shown its labels: the refresh of 500 ms)
        Application& app = d.app;
        ASSERT_TRUE(app.room_chat_available() && !app.room_chat().is_open());
        // the keys that must not open it
        app.room_key_down(SDLK_t, KMOD_CTRL, false);
        app.room_key_down(SDLK_t, KMOD_GUI, false);
        app.room_key_down(SDLK_t, 0, true);                                                   // a held key's repeat
        ASSERT_FALSE(app.room_chat().is_open());
        // T opens it, and the text event of the same key press is not the first letter
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("t");
        ASSERT_EQ(app.room_chat().text(), std::string());
        app.room_text_input("hello bob");
        ASSERT_EQ(app.room_chat().text(), std::string("hello bob"));
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().room().status_input && app.map_select().room().status_prefix == "Say: " && app.map_select().room().status == "hello bob");   // the status line shows the line, typed (the screen draws the prompt, the end and a caret that blinks)
        // the screen's keys do nothing while the line is typed: not START (S), not Leave (Q, X), not the map (the arrows), and they are not typed either
        for (const SDL_Keycode k : {SDLK_s, SDLK_q, SDLK_x, SDLK_UP, SDLK_DOWN}) app.room_key_down(k, 0, false);
        d.hall.step(600);
        ASSERT_TRUE(app.room_chat().is_open() && app.room_chat().text() == "hello bob");
        ASSERT_TRUE(!app.map_select().is_locked() && app.state() == AppState::MapSelect && app.is_running() && app.network_active());
        ASSERT_TRUE(d.server.status("CHATUI-1").state == server::RoomState::Waiting);
        // Enter sends it (it is not START), closes the input, and the line is shown as said
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return !d.bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|hello bob"}));
        d.hall.step(30);
        ASSERT_EQ(app.map_select().room().status, std::string("You: hello bob"));
        ASSERT_TRUE(!app.map_select().is_locked() && d.server.status("CHATUI-1").state == server::RoomState::Waiting);
        // a click on the status line opens it; Esc closes it without sending anything
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::BTN_START_X + 5, MapSelectScreen::BTN_START_Y + 5, SDL_BUTTON_LEFT));      // START's own click is the screen's
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y - 2, SDL_BUTTON_LEFT));            // just above the label
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + MapSelectScreen::STATUS_W, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));   // just right of it
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + MapSelectScreen::STATUS_H, SDL_BUTTON_LEFT));  // just under it
        ASSERT_FALSE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_RIGHT));           // the right button is not a click
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("never sent");
        app.room_key_down(SDLK_BACKSPACE, 0, false);
        ASSERT_EQ(app.room_chat().text(), std::string("never sen"));
        app.room_key_down(SDLK_BACKSPACE, KMOD_CTRL, false);                                  // Ctrl / Cmd with Backspace clears the line
        ASSERT_EQ(app.room_chat().text(), std::string());
        app.room_text_input("x");
        app.room_key_down(SDLK_ESCAPE, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(300);
        ASSERT_EQ(d.bob.net.pregame_chat().size(), size_t{1});                                // nothing was sent
        // an empty line sends nothing and is no START
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(400);
        ASSERT_TRUE(d.bob.net.pregame_chat().size() == 1 && !app.map_select().is_locked() && d.server.status("CHATUI-1").state == server::RoomState::Waiting);
        // printable ASCII only, at most 100 characters; the status line shows the end of a long line
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input(std::string("a\x01\x7f") + "b" + "\xC3\xA9" + "c");
        ASSERT_EQ(app.room_chat().text(), std::string("abc"));
        app.room_text_input("start-marker");
        app.room_text_input(std::string(150, 'z'));
        ASSERT_EQ(app.room_chat().text().size(), size_t{100});
        ASSERT_TRUE(app.room_chat().text().rfind("abcstart-marker", 0) == 0);                // the line keeps its start (up to 100 characters)
        d.hall.step(30);
        ASSERT_TRUE(app.map_select().room().status_input && app.map_select().room().status == app.room_chat().text());     // the status line has the whole line; the screen fits it to its box (N5.53)
        app.room_key_down(SDLK_ESCAPE, 0, false);
        // the screen has its keys back (START's after the guard of 400 ms, N5.53): S starts the match for both
        d.hall.step(450);
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.50 The Chat Input Through The Window's Own Event Loop: The Key, Text And Mouse Events That SDL Queues Reach It (T, The Typed Letters, Enter, A Click On The Status Line); A Local Game's Setup Screen And A Room That Is Gone Have No Chat, And An Input That Is Open When The Match Begins Closes") {
        {   // a local game: T is nobody's key
            Application local;
            ASSERT_TRUE(local.init(headless_config()));
            ASSERT_FALSE(local.room_chat_available());
            local.room_key_down(SDLK_t, 0, false);
            ASSERT_FALSE(local.room_chat().is_open());
            ASSERT_FALSE(local.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
            local.quit();
        }
        Duo2 d;
        ASSERT_TRUE(d.server.make_room("CHATUI-2", 4));
        ASSERT_TRUE(d.app.init(join_config(d.server, "CHATUI-2", "Leader")));
        ASSERT_TRUE(d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(d.bob.net.join("127.0.0.1", d.server.port(), "Bob", 255, "CHATUI-2"));
        ASSERT_TRUE(d.hall.until([&]() { return d.bob.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        d.hall.step(700);
        Application& app = d.app;
        const auto push = [](SDL_Event e) { ASSERT_TRUE(SDL_PushEvent(&e) == 1); };
        // the keyboard, as SDL delivers it: the key press and then the text of the same key, in one batch of events
        SDL_Event t_key;
        std::memset(&t_key, 0, sizeof(t_key));
        t_key.type = SDL_KEYDOWN;
        t_key.key.keysym.sym = SDLK_t;
        push(t_key);
        SDL_Event t_text;
        std::memset(&t_text, 0, sizeof(t_text));
        t_text.type = SDL_TEXTINPUT;
        std::snprintf(t_text.text.text, sizeof(t_text.text.text), "t");
        push(t_text);
        SDL_Event hi;
        std::memset(&hi, 0, sizeof(hi));
        hi.type = SDL_TEXTINPUT;
        std::snprintf(hi.text.text, sizeof(hi.text.text), "hi");
        push(hi);
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_EQ(app.room_chat().text(), std::string("hi"));                                 // the T of the key is not in it (the frame took all three events)
        SDL_Event s_key = t_key;
        s_key.key.keysym.sym = SDLK_s;
        push(s_key);                                                                          // an S while the line is typed: not a START
        app.run_frame_with_delta(0.016f);
        d.hall.step(500);
        ASSERT_TRUE(app.room_chat().is_open() && !app.map_select().is_locked() && d.server.status("CHATUI-2").state == server::RoomState::Waiting);
        SDL_Event enter = t_key;
        enter.key.keysym.sym = SDLK_RETURN;
        push(enter);
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return !d.bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|hi"}));
        // the mouse: a click on the status line (motion, press and release, as the window queues them)
        SDL_Event motion;
        std::memset(&motion, 0, sizeof(motion));
        motion.type = SDL_MOUSEMOTION;
        motion.motion.x = MapSelectScreen::LABEL_X + 20;
        motion.motion.y = MapSelectScreen::STATUS_Y + 10;
        push(motion);
        SDL_Event press = motion;
        press.type = SDL_MOUSEBUTTONDOWN;
        press.button.x = motion.motion.x;
        press.button.y = motion.motion.y;
        press.button.button = SDL_BUTTON_LEFT;
        press.button.clicks = 1;
        push(press);
        SDL_Event release = press;
        release.type = SDL_MOUSEBUTTONUP;
        push(release);
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("typing when the match begins");
        // the match begins while the line is open (the leader's START by the button's click: the mouse works while the input is open): the input closes
        const int32_t sx = MapSelectScreen::BTN_START_X + 5;
        const int32_t sy = MapSelectScreen::BTN_START_Y + 5;
        app.map_select().handle_mouse_motion(sx, sy);
        app.map_select().handle_mouse_down(sx, sy, 1);
        app.map_select().handle_mouse_up(sx, sy, 1);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_FALSE(app.room_chat_available());                                              // (a match has its own chat: the HUD's)
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.51 The Match's Chat Log Starts With The Waiting Room's Lines: What The Players Said Before START, Their Own Included, Is The First Of The Log At The Match (In Order, Under The Names Of The Players; The Room's Own Notices Are Not Repeated); A Line Of The Match Follows") {
        Server server;
        server::RoomSpec spec;
        spec.code = "CHATUI-3";
        spec.map = "TINY.LVL";
        spec.players = 4;
        ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "CHATUI-3", "Leader")));
        Peer bob;
        Hall hall{server, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "CHATUI-3"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        hall.step(700);
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("ready when you are");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(hall.until([&]() { return bob.net.pregame_chat().size() == 1; }, 8000));
        ASSERT_TRUE(bob.net.chat("one moment"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 2; }, 8000));
        ASSERT_EQ(lines(app.net()->pregame_chat()), (std::vector<std::string>{"0|Leader|ready when you are", "1|Bob|one moment"}));
        hall.step(450);                                                                       // (START's guard after the input closed: N5.53)
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        const std::string log = app.hud().chat_transcript("t");
        const size_t first = log.find("Leader: ready when you are\n");
        const size_t second = log.find("Bob: one moment\n");
        ASSERT_TRUE(first != std::string::npos && second != std::string::npos && first < second);
        ASSERT_TRUE(log.find("(room)") == std::string::npos);
        // and the chat of the match goes on in the same log
        ASSERT_TRUE(bob.net.chat("in the match"));
        ASSERT_TRUE(hall.until([&]() { return app.hud().chat_transcript("t").find("Bob: in the match\n") != std::string::npos; }, 8000));
        app.quit();
        // the room's notices are not part of the log: a fill in a room with Fog of War is refused with a notice, the match then starts with the people who are there
        Server fog_server;
        server::RoomSpec fog_spec;
        fog_spec.code = "CHATUI-4";
        fog_spec.map = "TINY.LVL";
        fog_spec.players = 4;
        fog_spec.fog = true;
        ASSERT_TRUE(fog_server.mgr.create_room(fog_spec, fog_server.now).ok);
        ApplicationConfig cfg = join_config(fog_server, "CHATUI-4", "Leader");
        cfg.fill_bots = net::FillLevel::Medium;
        Application fog_app;
        ASSERT_TRUE(fog_app.init(cfg));
        Peer cat;
        Hall fog_hall{fog_server, &fog_app, {&cat}};
        ASSERT_TRUE(fog_hall.until([&]() { return fog_app.net()->phase() == net::NetGame::Phase::Room && fog_app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(cat.net.join("127.0.0.1", fog_server.port(), "Cat", 255, "CHATUI-4"));
        ASSERT_TRUE(fog_hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && fog_app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        fog_hall.step(700);
        fog_app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(fog_hall.until([&]() { return fog_app.state() == AppState::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_TRUE(!fog_app.net()->pregame_chat().empty() && fog_app.net()->pregame_chat()[0].notice());        // the room did say it
        ASSERT_EQ(fog_app.hud().chat_transcript("t").find(net::kNoticeFillFog), std::string::npos);              // the match's log does not repeat it
        ASSERT_EQ(fog_app.sim().roster_mask(), 0x03);
        fog_app.quit();
    } TEST_END();

    TEST_CASE("N5.52 The Status Line Of A Setup Screen That Can START With Bots Says What START Will Do: \"Press START: the empty seats get Medium bots.\" For The Leader Of A Server's Room And The Host Of A Room On The Local Network With A Fill Level, The Original's Prompt Without One, \"Fog of War is on, so START seats no bots.\" In A Room With Fog; A Guest Keeps Its Own Line") {
        // the text as a function
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Medium, false), std::string("Press START: the empty seats get Medium bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Easy, false), std::string("Press START: the empty seats get Easy bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Hard, false), std::string("Press START: the empty seats get Hard bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::Hard, true), std::string("Fog of War is on, so START seats no bots."));
        ASSERT_EQ(net::NetGame::start_prompt(net::FillLevel::None, false), std::string("Press START when all players' thumbs have appeared."));
        {   // the leader of a server's room, and a guest of it
            Server server;
            ASSERT_TRUE(server.make_room("PROMPT-1", 4));
            ApplicationConfig cfg = join_config(server, "PROMPT-1", "Leader");
            cfg.fill_bots = net::FillLevel::Medium;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            Hall hall{server, &app, {&bob}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            hall.step(100);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
            app.set_fill_bots(net::FillLevel::Hard);                                          // the screens' choice changes the line at the next frame
            hall.step(30);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Hard bots."));
            app.set_fill_bots(net::FillLevel::None);
            hall.step(30);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START when all players' thumbs have appeared."));
            app.set_fill_bots(net::FillLevel::Easy);
            ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "PROMPT-1"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(100);
            ASSERT_EQ(bob.net.status_text(), std::string("Waiting for the host to start the game..."));       // (a guest with a fill level of its own: no START, no line about it)
            bob.net.set_fill_bots(net::FillLevel::Hard);
            hall.step(100);
            ASSERT_FALSE(bob.net.status_text().find("START") != std::string::npos && bob.net.status_text().find("bots") != std::string::npos);
            app.quit();
        }
        {   // a room with Fog of War
            Server server;
            server::RoomSpec spec;
            spec.code = "PROMPT-2";
            spec.map = "TINY.LVL";
            spec.players = 4;
            spec.fog = true;
            ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
            ApplicationConfig cfg = join_config(server, "PROMPT-2", "Leader");
            cfg.fill_bots = net::FillLevel::Hard;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Hall hall{server, &app, {}};
            ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
            hall.step(100);
            ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));
            app.quit();
        }
        {   // the host of a room on the local network
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.fill_bots = net::FillLevel::Medium;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            for (int i = 0; i < 20; ++i) app.pump_network(0.010f);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START: the empty seats get Medium bots."));
            app.set_fill_bots(net::FillLevel::None);
            for (int i = 0; i < 20; ++i) app.pump_network(0.010f);
            ASSERT_EQ(app.map_select().room().status, std::string("Press START when all players' thumbs have appeared."));
            app.quit();
        }
    } TEST_END();

    // SDL events for the tests that go through the window's own event loop
    const auto push_key = [](SDL_Keycode sym, bool repeat) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = sym;
        e.key.repeat = repeat ? 1 : 0;
        return SDL_PushEvent(&e) == 1;
    };
    const auto push_button = [](Uint32 type, int32_t x, int32_t y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof(e));
        e.type = type;
        e.button.x = x;
        e.button.y = y;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.clicks = 1;
        return SDL_PushEvent(&e) == 1;
    };
    // a room of four with the application as its leader and Bob in it, the setup screen shown
    const auto open_room = [&](Duo2& d, const std::string& code) {
        if (!d.server.make_room(code, 4)) return false;
        if (!d.app.init(join_config(d.server, code, "Leader"))) return false;
        if (!d.hall.until([&]() { return d.app.net()->phase() == net::NetGame::Phase::Room && d.app.net()->is_leader(); }, 8000)) return false;
        if (!d.bob.net.join("127.0.0.1", d.server.port(), "Bob", 255, code)) return false;
        if (!d.hall.until([&]() { return d.bob.net.phase() == net::NetGame::Phase::Room && d.app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000)) return false;
        d.hall.step(700);
        return true;
    };

    TEST_CASE("N5.53 After The Chat Input Closes, START Is Not For The Next Key (A Review Fix): T, A Line, Enter, Enter (A Second Enter Right After) Starts Nothing; T, Enter, Enter Starts Nothing; Esc And Then S Starts Nothing; Neither Does The Keypad's Enter Or A Repeat; 350 ms After The Closing Still Nothing, 450 ms After It A Fresh Key Starts The Match; A Held Enter Starts Nothing On The Leader's Or A LAN Host's Screen (A Screen Of A Room) But Still Starts A Local Game (The Original's Screen)") {
        Duo2 d;
        ASSERT_TRUE(open_room(d, "GUARD-1"));
        Application& app = d.app;
        const auto room_state = [&]() { return d.server.status("GUARD-1").state; };
        const auto still_waiting = [&]() { return room_state() == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
        // RP1: T, a line, Enter (it sends and closes), a second Enter 20 ms later
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_text_input("hi bob");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(20);
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(1500);
        ASSERT_TRUE(still_waiting());
        ASSERT_EQ(d.bob.net.pregame_chat().size(), size_t{1});                                 // (the line did go: the second Enter was no line and no START)
        // RP2: T, nothing typed, Enter (it closes, sends nothing), Enter
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_RETURN, 0, false);
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(1500);
        ASSERT_TRUE(still_waiting());
        // RP4: Esc, then S (the habit of typing "ss"), and the other START keys, a repeat among them
        for (const SDL_Keycode key : {SDLK_s, SDLK_RETURN, SDLK_KP_ENTER}) {
            app.room_key_down(SDLK_t, 0, false);
            app.room_text_input("t");
            app.room_text_input("oops");
            app.room_key_down(SDLK_ESCAPE, 0, false);
            app.room_key_down(key, 0, false);
            app.room_key_down(key, 0, true);
            d.hall.step(450);                                                                  // (the guard is over by the next round)
            ASSERT_TRUE(still_waiting());
        }
        ASSERT_EQ(d.bob.net.pregame_chat().size(), size_t{1});                                 // (nothing of "oops" was sent)
        // the other keys are not held back: Q leaves, but the map keys and the like are the screen's at once (here the Down key moves nothing for a leader, but it is not swallowed: T works at once)
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_ESCAPE, 0, false);
        app.room_key_down(SDLK_t, 0, false);                                                    // T right after the closing opens it again
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_key_down(SDLK_ESCAPE, 0, false);
        // a held Enter that was never a press starts nothing, now or later (the leader's screen)
        d.hall.step(450);
        for (int i = 0; i < 5; ++i) app.room_key_down(SDLK_RETURN, 0, true);
        app.room_key_down(SDLK_s, 0, true);
        d.hall.step(1000);
        ASSERT_TRUE(still_waiting());
        for (const SDL_Keycode key : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_s}) ASSERT_TRUE(push_key(key, true));      // the same through the window's own event loop: SDL says repeat
        app.run_frame_with_delta(0.016f);
        d.hall.step(1000);
        ASSERT_TRUE(still_waiting());
        // the boundary: after Esc, 350 ms later the key is still held back, 450 ms after it is a fresh decision
        app.room_key_down(SDLK_t, 0, false);
        app.room_text_input("t");
        app.room_key_down(SDLK_ESCAPE, 0, false);
        d.hall.step(350);
        app.room_key_down(SDLK_RETURN, 0, false);
        d.hall.step(200);
        ASSERT_TRUE(still_waiting());                                                           // (the Enter at 350 ms was swallowed; the clock has moved on to 550 ms)
        // the guard is counted from the CLOSING, not from the last key: another Enter 100 ms after the swallowed one is still after more than 400 ms since the closing: it is a press
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        app.quit();

        {   // a LAN host: the room is the host's own, with a guest who is ready; a held Enter starts nothing, a press after the closing guard does
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            Application host;
            ASSERT_TRUE(host.init(cfg));
            Peer guest;
            ASSERT_TRUE(guest.net.join("127.0.0.1", host.net()->listen_port(), "Bob"));
            Duo duo{host, guest};
            ASSERT_TRUE(duo.until([&]() { return host.net()->room().slots[1].state == net::SlotState::Client && host.net()->can_start() && host.room_chat_available(); }, 8000));
            duo.step(700);
            for (int i = 0; i < 5; ++i) host.room_key_down(SDLK_RETURN, 0, true);
            host.room_key_down(SDLK_s, 0, true);
            host.room_key_down(SDLK_KP_ENTER, 0, true);
            duo.step(1000);
            ASSERT_TRUE(host.state() == AppState::MapSelect && !host.map_select().is_locked() && guest.net.phase() == net::NetGame::Phase::Room);     // nothing started
            host.room_key_down(SDLK_t, 0, false);                                                // the same guard on a LAN host: a line, Enter, Enter
            host.room_text_input("t");
            host.room_text_input("ready?");
            host.room_key_down(SDLK_RETURN, 0, false);
            host.room_key_down(SDLK_RETURN, 0, false);
            duo.step(600);
            ASSERT_TRUE(host.state() == AppState::MapSelect && !host.map_select().is_locked() && guest.net.phase() == net::NetGame::Phase::Room);
            host.room_key_down(SDLK_RETURN, 0, false);                                           // a fresh press, long after the closing: START
            ASSERT_TRUE(duo.until([&]() { return host.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            host.quit();
        }
        {   // a local game's screen is the original's: a held Enter (the repeat) STARTS it, as it always did (the rule of the repeat is for the screens of a room only; there is no chat here)
            Application local;
            ASSERT_TRUE(local.init(headless_config()));
            ASSERT_EQ(local.state(), AppState::MapSelect);
            ASSERT_FALSE(local.room_chat_available());
            local.room_key_down(SDLK_RETURN, 0, true);
            ASSERT_EQ(local.state(), AppState::Playing);
            local.quit();
        }
        {   // ... and so does the repeat of the keypad's Enter and of S; and a press starts it too
            for (const SDL_Keycode key : {SDLK_KP_ENTER, SDLK_s}) {
                Application local;
                ASSERT_TRUE(local.init(headless_config()));
                local.room_key_down(key, 0, true);
                ASSERT_EQ(local.state(), AppState::Playing);
                local.quit();
            }
            Application local;
            ASSERT_TRUE(local.init(headless_config()));
            local.room_key_down(SDLK_RETURN, 0, false);
            ASSERT_EQ(local.state(), AppState::Playing);
            local.quit();
        }
    } TEST_END();

    TEST_CASE("N5.54 A Player With No Keyboard Can Close The Chat Input (A Review Fix): A Second Click On The Status Line Closes It And Sends Nothing, A Click Anywhere Else Closes It Too (It Is Not The Screen's Click: A Click On START Does Not Start The Match Through An Open Input), The Release Of A Press That Began Before It Opened Fires Nothing; The Same Through The Window's Own Event Loop") {
        Duo2 d;
        ASSERT_TRUE(open_room(d, "GUARD-2"));
        Application& app = d.app;
        const int32_t line_x = MapSelectScreen::LABEL_X + 20;
        const int32_t line_y = MapSelectScreen::STATUS_Y + 10;
        const int32_t sx = MapSelectScreen::BTN_START_X + 5;
        const int32_t sy = MapSelectScreen::BTN_START_Y + 5;
        const auto quiet = [&]() { return d.server.status("GUARD-2").state == server::RoomState::Waiting && !app.map_select().is_locked() && app.state() == AppState::MapSelect; };
        // a click opens it; its release is the input's (nothing for the screen)
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        app.room_text_input("typed with a finger");
        ASSERT_FALSE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_RIGHT));                  // the right button is no click
        ASSERT_TRUE(app.room_chat().is_open());
        // a second click on the status line closes it and sends nothing
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));                     // (closed, and nothing pending: a release now is the screen's)
        d.hall.step(500);
        ASSERT_TRUE(d.bob.net.pregame_chat().empty() && quiet());
        ASSERT_EQ(app.room_chat().text(), std::string());
        // the closing click starts the guard of START's keys as Enter and Esc do: Enter right after a click that closed the input is no START
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));                  // (the second click closes it)
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        app.room_key_down(SDLK_RETURN, 0, false);
        app.room_key_down(SDLK_s, 0, false);
        d.hall.step(300);
        ASSERT_TRUE(quiet());
        d.hall.step(300);                                                                       // (more than 400 ms in all since the click: START keys are the screen's again, but not pressed here)
        // a click elsewhere closes it too (the map's box, the empty middle of the screen)
        for (const auto& where : {std::pair<int32_t, int32_t>{100, 320}, std::pair<int32_t, int32_t>{300, 150}, std::pair<int32_t, int32_t>{620, 470}}) {
            ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
            ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
            app.room_text_input("words");
            ASSERT_TRUE(app.room_mouse_down(where.first, where.second, SDL_BUTTON_LEFT));
            ASSERT_FALSE(app.room_chat().is_open());
            ASSERT_TRUE(app.room_mouse_up(where.first, where.second, SDL_BUTTON_LEFT));
            ASSERT_TRUE(quiet());
        }
        // a click on START while the input is open: it closes the input, the match does not start; the release fires nothing either
        ASSERT_TRUE(app.room_mouse_down(line_x, line_y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_mouse_up(line_x, line_y, SDL_BUTTON_LEFT));
        app.room_text_input("a line");
        ASSERT_TRUE(app.room_mouse_down(sx, sy, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_FALSE(app.map_select().start_button().pressed());
        ASSERT_TRUE(app.room_mouse_up(sx, sy, SDL_BUTTON_LEFT));
        d.hall.step(1500);
        ASSERT_TRUE(quiet() && d.bob.net.pregame_chat().empty());
        // a press that began on START BEFORE the input opened: T opens the input, the release is not START's click
        app.map_select().handle_mouse_motion(sx, sy);
        app.map_select().handle_mouse_down(sx, sy, SDL_BUTTON_LEFT);
        ASSERT_TRUE(app.map_select().start_button().pressed());
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(app.room_mouse_up(sx, sy, SDL_BUTTON_LEFT));
        ASSERT_FALSE(app.map_select().start_button().pressed());
        d.hall.step(1500);
        ASSERT_TRUE(quiet() && app.room_chat().is_open());                                      // (the input is still open: a release is no click on it)
        app.room_key_down(SDLK_ESCAPE, 0, false);
        // the same through the window's own event loop (motion, press, release as SDL queues them): a click on START with the input open starts nothing
        SDL_Delay(600);                                                                         // (the double-click time of the click that closed the input before is over: its rest is not this click, N5.73)
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONDOWN, line_x, line_y));
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONUP, line_x, line_y));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(app.room_chat().is_open());
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONDOWN, sx, sy));
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONUP, sx, sy));
        app.run_frame_with_delta(0.016f);
        ASSERT_FALSE(app.room_chat().is_open());
        d.hall.step(1500);
        ASSERT_TRUE(quiet());
        // and with the input closed the same click is START: the match begins (the mouse has no guard: a click is a decision; only the rest of the click that closed the input is not one, N5.73:
        // a click after its double-click time is)
        SDL_Delay(600);
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONDOWN, sx, sy));
        ASSERT_TRUE(push_button(SDL_MOUSEBUTTONUP, sx, sy));
        app.run_frame_with_delta(0.016f);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.55 The Match's Chat Log Gives A Seeded Line To The One Who Said It (A Review Fix): Bob Speaks In The Waiting Room And Leaves, Cat Takes His Seat: The Log Says Bob, Not Cat; A Line Whose Name Is Empty Falls Back To The Seat's Name") {
        Server server;
        server::RoomSpec spec;
        spec.code = "SEED-1";
        spec.map = "TINY.LVL";
        spec.players = 4;
        ASSERT_TRUE(server.mgr.create_room(spec, server.now).ok);
        Application app;
        ASSERT_TRUE(app.init(join_config(server, "SEED-1", "Leader")));
        Peer bob, cat;
        Hall hall{server, &app, {&bob, &cat}};
        ASSERT_TRUE(hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        ASSERT_TRUE(bob.net.join("127.0.0.1", server.port(), "Bob", 255, "SEED-1"));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_TRUE(bob.net.chat("I am Bob and I have something to say"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 1; }, 8000));
        bob.net.leave();                                                                     // Bob goes, his seat is free again
        hall.step(500);
        ASSERT_TRUE(cat.net.join("127.0.0.1", server.port(), "Cat", 255, "SEED-1"));
        ASSERT_TRUE(hall.until([&]() { return cat.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        ASSERT_EQ(cat.net.my_seat(), uint8_t{1});                                            // Cat sits where Bob sat
        ASSERT_TRUE(cat.net.chat("Cat here"));
        ASSERT_TRUE(hall.until([&]() { return app.net()->pregame_chat().size() == 2; }, 8000));
        hall.step(700);
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_EQ(app.sim().get_player_name(1), std::string("Cat"));                         // (the seat has another player now)
        const std::string log = app.hud().chat_transcript("t");
        ASSERT_TRUE(log.find("Bob: I am Bob and I have something to say\n") != std::string::npos);
        ASSERT_TRUE(log.find("Cat: I am Bob") == std::string::npos);
        ASSERT_TRUE(log.find("Cat: Cat here\n") != std::string::npos);
        ASSERT_TRUE(log.find("Bob: I am Bob") < log.find("Cat: Cat here"));
        app.quit();
    } TEST_END();

    TEST_CASE("N5.56 The Room's Chat Does Not Flood The Program's Log (A Review Fix): At Most 20 Lines At Once And 10 A Second, The Lines Beyond Are Counted And One Line Says How Many Were Not Logged; An Honest Room Is Logged In Full; The Application Prints Through It") {
        {   // the model
            RoomChatLog log;
            std::vector<net::ChatLine> flood;
            for (int i = 0; i < 300; ++i) flood.push_back(net::ChatLine{static_cast<uint8_t>(i % 4), "P" + std::to_string(i % 4), "line " + std::to_string(i)});
            std::vector<std::string> out = log.take(flood, 1000);
            ASSERT_EQ(out.size(), size_t{RoomChatLog::kBurst});                                // the burst, and nothing for the summary (the budget is empty)
            ASSERT_EQ(out.front(), std::string("P0: line 0"));
            ASSERT_EQ(out.back(), std::string("P3: line 19"));
            ASSERT_EQ(log.left_out(), uint64_t{300 - RoomChatLog::kBurst + 0});
            out = log.take({}, 1000);                                                          // no time has passed: still nothing
            ASSERT_TRUE(out.empty());
            out = log.take({}, 1300);                                                          // 300 ms: 3 tokens: the summary, once
            ASSERT_EQ(out.size(), size_t{1});
            ASSERT_EQ(out[0], std::string("(280 more lines were not logged)"));
            ASSERT_EQ(log.left_out(), uint64_t{0});
            ASSERT_TRUE(log.take({}, 1400).empty());                                           // (reported: nothing more to say)
            // an honest room: 40 lines over 20 seconds are all there, no summary
            RoomChatLog calm;
            size_t printed = 0;
            for (int i = 0; i < 40; ++i) {
                const std::vector<std::string> one = calm.take({net::ChatLine{0, "Ann", "hello " + std::to_string(i)}}, 5000 + static_cast<uint32_t>(i) * 500u);
                ASSERT_TRUE(one.size() == 1 && one[0] == "Ann: hello " + std::to_string(i));
                printed += one.size();
            }
            ASSERT_EQ(printed, size_t{40});
            ASSERT_EQ(calm.take({net::ChatLine{255, "", "Bots cannot play with Fog of War."}}, 30000)[0], std::string("(room): Bots cannot play with Fog of War."));
        }
        {   // the application, joined to a host that says 300 lines at once (its own lines have no budget): what reaches stderr is bounded
            Peer host;
            ASSERT_TRUE(host.net.host(0, "Alice", true));
            host.net.set_map("TINY.LVL");
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = host.net.listen_port();
            cfg.player_name = "Guest";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            Duo duo{app, host};
            ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
            std::ostringstream captured;
            std::streambuf* const saved = std::cerr.rdbuf(captured.rdbuf());
            const double clock_before = app.net_clock_ms();
            for (int i = 0; i < 300; ++i) host.net.chat("flood " + std::to_string(i));
            duo.step(300);
            const double elapsed_s = (app.net_clock_ms() - clock_before) / 1000.0;
            std::cerr.rdbuf(saved);
            size_t printed = 0;
            bool summary = false;
            std::istringstream in(captured.str());
            for (std::string line; std::getline(in, line);) {
                if (line.find("Room chat: Alice: flood ") != std::string::npos) ++printed;
                if (line.find("more lines were not logged") != std::string::npos) summary = true;
            }
            ASSERT_TRUE(printed >= RoomChatLog::kBurst && printed <= RoomChatLog::kBurst + static_cast<size_t>(RoomChatLog::kPerSecond * elapsed_s) + 1);      // the burst and the rate: not 300
            ASSERT_TRUE(summary);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.57 Typing While The Map Loads: The Leader Presses START, The Room Is Loading (A Guest Has Not Reported Yet), T Still Opens The Input, The Line Reaches The Other Player, And The Input Closes When The Match Begins") {
        Duo2 d;
        ASSERT_TRUE(open_room(d, "LOAD-1"));
        Application& app = d.app;
        d.bob.hold_report = true;                                                             // Bob loads the map and does not report: the room stays in Loading
        app.room_key_down(SDLK_s, 0, false);
        ASSERT_TRUE(d.hall.until([&]() { return d.bob.report_pending; }, 8000));
        ASSERT_TRUE(d.hall.until([&]() { return app.net()->phase() == net::NetGame::Phase::Loading; }, 8000));
        ASSERT_TRUE(app.state() == AppState::MapSelect && app.room_chat_available());
        app.room_key_down(SDLK_t, 0, false);
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("t");
        app.room_text_input("loading, loading");
        app.room_key_down(SDLK_RETURN, 0, false);
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_TRUE(d.hall.until([&]() { return !d.bob.net.pregame_chat().empty(); }, 8000));
        ASSERT_EQ(lines(d.bob.net.pregame_chat()), (std::vector<std::string>{"0|Leader|loading, loading"}));
        ASSERT_EQ(app.map_select().room().status, std::string("You: loading, loading"));
        // a click opens it while loading too, and the match beginning closes it
        ASSERT_TRUE(app.room_mouse_down(MapSelectScreen::LABEL_X + 5, MapSelectScreen::STATUS_Y + 5, SDL_BUTTON_LEFT));
        ASSERT_TRUE(app.room_chat().is_open());
        app.room_text_input("still typing");
        d.bob.report_pending = false;
        d.bob.net.report_loaded(true);
        ASSERT_TRUE(d.hall.until([&]() { return app.state() == AppState::Playing && d.bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        ASSERT_FALSE(app.room_chat().is_open());
        ASSERT_FALSE(app.room_chat_available());
        app.quit();
    } TEST_END();

    TEST_CASE("N5.58 The Host Of A Room On The Local Network With A Fill Level And Fog Of War: START Seats No Bots And The Status Line Says Why (\"Bots cannot play with Fog of War.\"); With A Guest In The Room The Match Starts Without Bots, Alone It Is The Can't-Go Cue") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.fill_bots = net::FillLevel::Medium;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        for (int i = 0; i < 30; ++i) app.pump_network(0.010f);
        app.map_select().set_selected_index(0);
        app.net()->set_fog(true);                                                              // (what the Fog of War: On button does for a host)
        for (int i = 0; i < 30; ++i) app.pump_network(0.010f);
        ASSERT_EQ(app.map_select().room().status, std::string("Fog of War is on, so START seats no bots."));       // the prompt says it before anybody presses anything
        app.map_select().handle_key_down(SDLK_RETURN);                                        // alone: no bots with fog, one person: the can't-go cue
        for (int i = 0; i < 3; ++i) app.pump_network(0.010f);
        ASSERT_EQ(app.map_select().room().status, std::string(net::kNoticeFillFog));          // the notice replaced the prompt at once: START said no, and why
        for (uint8_t seat = 1; seat < 4; ++seat) ASSERT_TRUE(app.net()->room().slots[seat].state == net::SlotState::Empty);
        ASSERT_FALSE(app.map_select().is_locked());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);                                              // two people, no bots
        ASSERT_TRUE(app.bots() == nullptr);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.59 What The Setup Screen Draws For The Status Fits Its Box (A Review Fix): The Prompt Label Is 293 x 35, Two Lines Of 14 px: The Longest Typed Line (100 Wide Letters, 100 Narrow Ones, A Line Of Short Words), The \"You: ...\" Echo, A Received \"Name: ...\" And Any Other Text Never Needs A Third Line Or A Wider Line, With The Real Font; A Text That Is Cut Ends In \"...\", A Typed Line Shows Its End With The Prompt And The Caret; The Blink Does Not Move A Word") {
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        Renderer& r = app.renderer();
        using MS = MapSelectScreen;
        const auto fits = [&](const std::vector<std::string>& got) {
            if (got.size() > MS::STATUS_LINES) return false;
            for (const std::string& l : got) {
                if (r.get_text_width(l, FontSize::Px14) > MS::STATUS_W) return false;
            }
            return static_cast<int32_t>(got.size()) * font_cell_height(FontSize::Px14) <= MS::STATUS_H;
        };
        const std::vector<std::string> samples = {
            std::string(100, 'W'), std::string(100, 'i'), std::string(100, 'm'), std::string(100, '8'), std::string(100, '_'), std::string(100, '.'),
            "the quick brown fox jumps over the lazy dog and then does it again and again until the line has a hundred chars!!",
            "a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z 0 1 2 3 4 5 6 7 8 9",
            "WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW WWWW",
            "x", "", "supercalifragilisticexpialidocious-supercalifragilisticexpialidocious-supercalifragilisticexpialidocious",
        };
        for (const std::string& text : samples) {
            // a typed line: the prompt, what is typed (up to 100 characters), the caret on and off
            for (const bool caret : {true, false}) {
                MS::RoomView v;
                v.networked = true;
                v.status_input = true;
                v.status_prefix = "Say: ";
                v.status = text.substr(0, 100);
                v.status_caret = caret;
                const std::vector<std::string> got = MS::status_lines(r, v);
                ASSERT_TRUE(fits(got));
                ASSERT_FALSE(got.empty());
                std::string joined;
                for (const std::string& l : got) joined += l;
                ASSERT_TRUE(joined.find(caret ? "_" : " ") != std::string::npos);              // (the caret's place is in the last line, a blank when it blinks off)
                if (!text.empty()) ASSERT_TRUE(v.status.size() <= 100);
            }
            // the end of a long typed line is on the screen: its last characters are in the last line
            MS::RoomView typed;
            typed.networked = true;
            typed.status_input = true;
            typed.status_prefix = "Say: ";
            typed.status = text.substr(0, 100);
            typed.status_caret = true;
            const std::vector<std::string> typed_lines = MS::status_lines(r, typed);
            std::string shown_text;
            for (const std::string& l : typed_lines) shown_text += l;
            if (text.size() >= 8) ASSERT_TRUE(shown_text.find(text.substr(text.size() > 100 ? 100 - 4 : text.size() - 4, 4) + "_") != std::string::npos);        // the last characters of the line, and the caret behind them, are on the screen
            if (text.size() > 60) ASSERT_TRUE(shown_text.rfind("Say:", 0) != 0 || shown_text.size() > 20);                    // (a long line scrolls: the prompt may be gone, the end never is)
            // the echo of what this player said, and what somebody else said (the status text, cut by the net at 84 characters), and a long notice of any kind
            for (const std::string& shown : {"You: " + text.substr(0, 100), std::string("Name: ") + text.substr(0, 100), std::string("Bots cannot play with Fog of War. ") + text, text}) {
                MS::RoomView v;
                v.networked = true;
                v.status = shown;
                const std::vector<std::string> got = MS::status_lines(r, v);
                ASSERT_TRUE(fits(got));
            }
        }
        ASSERT_TRUE(r.get_text_width(fill_choice_caption(), FontSize::Px14) <= 350);                  // (the Host panel's caption fits its rectangle with the real font: 380 px wide now, and it is within the 350 px that it had)
        {   // the same texts as the net cuts them: 84 characters
            const std::string net_cut = "Ann: " + std::string(100, 'W');
            MS::RoomView v;
            v.networked = true;
            v.status = net_cut.substr(0, net::NetGame::kStatusNoticeChars - 3) + "...";
            ASSERT_TRUE(fits(MS::status_lines(r, v)));
        }
        {   // a text that is cut ends in "..." and keeps its beginning; one that fits is untouched (and is what the original's prompt was)
            MS::RoomView v;
            v.networked = true;
            v.status = "Bob: " + std::string(100, 'W');
            const std::vector<std::string> got = MS::status_lines(r, v);
            ASSERT_EQ(got.size(), size_t{2});
            ASSERT_TRUE(got[0].rfind("Bob:", 0) == 0);
            ASSERT_EQ(got[1].substr(got[1].size() - 3), std::string("..."));
            v.status = "Press START when all players' thumbs have appeared.";
            const std::vector<std::string> original = MS::status_lines(r, v);
            ASSERT_TRUE(!original.empty() && original.size() <= 2 && original == wrap_label_text(r, v.status, MS::STATUS_W, FontSize::Px14));
            v.status = "hi";
            ASSERT_EQ(MS::status_lines(r, v), (std::vector<std::string>{"hi"}));
            v.status.clear();
            ASSERT_EQ(MS::status_lines(r, v), original);                                       // an empty status is the original's prompt
        }
        {   // the blink moves nothing: the got are the same on and off, apart from the caret's own character
            MS::RoomView v;
            v.networked = true;
            v.status_input = true;
            v.status_prefix = "Say: ";
            for (const std::string& text : {std::string("hello"), std::string(37, 'a') + " " + std::string(31, 'b'), std::string(100, 'W'), std::string(76, 'x')}) {
                v.status = text;
                v.status_caret = true;
                const std::vector<std::string> on = MS::status_lines(r, v);
                v.status_caret = false;
                const std::vector<std::string> off = MS::status_lines(r, v);
                ASSERT_EQ(on.size(), off.size());
                for (size_t i = 0; i < on.size(); ++i) {
                    ASSERT_EQ(on[i].size(), off[i].size());
                    if (i + 1 < on.size()) ASSERT_EQ(on[i], off[i]);
                }
                ASSERT_TRUE(on.back().back() == '_' && off.back().back() == ' ' && on.back().substr(0, on.back().size() - 1) == off.back().substr(0, off.back().size() - 1));
            }
        }
        app.quit();
    } TEST_END();

    TEST_CASE("N5.76 The Refusal For Another Version On The Web Page Says To Reload The Page (A Tab That Was Opened Before The Server Was Updated Is The Old Game), In Words That Fit The Setup Screen's Status Box In Two Lines On Both Pages; The Desktop Game's Words Are As They Were, And Every Other Refusal Is The Same Words In Both") {
        using net::NetGame;
        using net::RejectReason;
        const std::string desktop = NetGame::reject_text(RejectReason::VersionMismatch, false);
        const std::string web = NetGame::reject_text(RejectReason::VersionMismatch, true);
        ASSERT_EQ(desktop, std::string("This version cannot play with the host's version."));            // (the words that test_netgame N3.17 pins for a native client)
        ASSERT_TRUE(web.compare(0, desktop.size(), desktop) == 0 && web.find("Reload the page") != std::string::npos);      // the same sentence, and what to do about it
        for (const RejectReason reason : {RejectReason::Full, RejectReason::MatchRunning, RejectReason::Kicked, RejectReason::BadRequest, RejectReason::NoSuchRoom, RejectReason::Dropped,
                                          RejectReason::RejoinFailed, RejectReason::Superseded}) {
            ASSERT_EQ(NetGame::reject_text(reason, false), NetGame::reject_text(reason, true));          // nothing else changes with the page
            ASSERT_TRUE(NetGame::reject_text(reason, true).find("Reload") == std::string::npos);
        }
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        Renderer& r = app.renderer();
        using MS = MapSelectScreen;
        MS::RoomView v;                                                                                   // the classic page (the web page's "Classic 4:3"): 293 px, two lines of 14 px
        v.networked = true;
        v.status = web;
        const std::vector<std::string> refusal_lines = MS::status_lines(r, v);
        ASSERT_TRUE(!refusal_lines.empty() && refusal_lines.size() <= MS::STATUS_LINES);
        std::string joined;
        for (const std::string& l : refusal_lines) {
            ASSERT_TRUE(r.get_text_width(l, FontSize::Px14) <= MS::STATUS_W);
            joined += (joined.empty() ? "" : " ") + l;
        }
        ASSERT_EQ(joined, web);                                                                           // not cut: no "..." and every word of it is on the screen
        const SetupLayout& wide = SetupLayout::of(SetupVariant::Guest);                                   // the 16:9 page (the web page's default): its label is wrapped at 363 px without a limit of lines
        ASSERT_TRUE(wrap_label_text(r, web, wide.prompt_text.w, FontSize::Px14).size() <= MS::STATUS_LINES);
        app.quit();
    } TEST_END();

    TEST_CASE("N5.76b The Refusal For A Room That The Server Cannot Make (A Hello That Carried A Create Block, NoSuchRoom: The Cap Of Public Rooms Is Full) Says So And What To Do, Not That There Is No Such Room, In Words That Fit The Setup Screen's Status Box In Two Lines On Both Pages; A Hello Without A Block Is Told As Before") {
        using net::NetGame;
        using net::RejectReason;
        const std::string no_place = "The server cannot make a room now: it is busy, or hosts no online matches. Try again in a few minutes.";
        const std::string no_room = "There is no such room on this server.";
        for (const bool browser : {false, true}) {
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, true), no_place);               // (the Hello carried a create block: the server would have made the room, and cannot now)
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser, false), no_room);               // (no block: the room is simply not there)
            ASSERT_EQ(NetGame::reject_text(RejectReason::NoSuchRoom, browser), no_room);
            for (const RejectReason other : {RejectReason::Full, RejectReason::MatchRunning, RejectReason::Kicked, RejectReason::BadRequest, RejectReason::Dropped, RejectReason::RejoinFailed, RejectReason::Superseded}) {
                ASSERT_EQ(NetGame::reject_text(other, browser, true), NetGame::reject_text(other, browser));      // (nothing else changes with the block)
            }
        }
        Application app;
        ASSERT_TRUE(app.init(headless_config()));
        Renderer& r = app.renderer();
        using MS = MapSelectScreen;
        MS::RoomView v;                                                                                   // the classic page: 293 px, two lines of 14 px
        v.networked = true;
        v.status = no_place;
        const std::vector<std::string> place_lines = MS::status_lines(r, v);
        ASSERT_TRUE(!place_lines.empty() && place_lines.size() <= MS::STATUS_LINES);
        std::string joined;
        for (const std::string& l : place_lines) {
            ASSERT_TRUE(r.get_text_width(l, FontSize::Px14) <= MS::STATUS_W);
            joined += (joined.empty() ? "" : " ") + l;
        }
        ASSERT_EQ(joined, no_place);                                                                      // not cut: no "..." and every word of it is on the screen
        const SetupLayout& wide = SetupLayout::of(SetupVariant::Guest);                                   // the 16:9 page
        ASSERT_TRUE(wrap_label_text(r, no_place, wide.prompt_text.w, FontSize::Px14).size() <= MS::STATUS_LINES);
        app.quit();
    } TEST_END();
}
