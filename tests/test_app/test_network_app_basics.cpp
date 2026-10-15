// Tests of the network in the application: the command line, the computer players, the window, a host, a guest
#include "network_app_fixtures.hpp"

void run_command_line_tests() {
    TEST_CASE("N5.1 Command Line: --name, -N<team><name> (the original's), --team-name, -pnum: (the original's spelling) and -pnum=, --host [port], --join host[:port], --port, --loopback") {
        std::vector<std::string> args;
        std::vector<char*> st;
        args = {"ants", "--name", "Alice Smith", "-N1Bob", "-N3Dave", "-pnum=2", "--team-name", "0", "Zed", "--headless"};
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.player_name, "Alice Smith");
        ASSERT_EQ(c.team_names[1], "Bob");
        ASSERT_EQ(c.team_names[3], "Dave");
        ASSERT_EQ(c.team_names[0], "Zed");
        ASSERT_EQ(c.team_names[2], "");
        ASSERT_EQ(c.local_player_id, 2);
        ASSERT_TRUE(c.headless);
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::None);
        // the original spells it with a colon (string "pnum:" at 0x1047134)
        args = {"ants", "-pnum:3"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.local_player_id, 3);
        args = {"ants", "-pnum:1", "-pnum=2"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.local_player_id, 2);                                                 // the last one wins, whichever spelling
        // --room CODE and --token T (protocol 6: a server's room and the credential that came with it)
        args = {"ants", "--join", "play.example.org:4001", "--room", "ROOM-42", "--token", "abc.DEF-123", "--name", "Ann"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_room, "ROOM-42");
        ASSERT_EQ(c.net_token, "abc.DEF-123");
        // --join-url URL (a server's WebSocket door: how the browser build joins; a native game only parses it)
        args = {"ants", "--join-url", "wss://play.example.org/ws", "--room", "ROOM-42", "--seat", "2", "--name", "Web"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Join);
        ASSERT_EQ(c.net_url, "wss://play.example.org/ws");
        ASSERT_EQ(c.net_room, "ROOM-42");
        ASSERT_EQ(c.net_seat, 2);
        args = {"ants", "--join", "host"};
        ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st)).net_url, "");
        ASSERT_EQ(Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st)).net_address, "host");
        {                                                                                  // a native NetGame cannot join through a WebSocket (it joins with TCP)
            ants::sim::SimulationEngine sim;
            ants::net::NetGame game(sim);
            ASSERT_FALSE(game.join_url("wss://play.example.org/ws", "Web"));
            ASSERT_FALSE(game.active());
        }
        args = {"ants", "--room", "no spaces allowed"};                                  // not a room code: ignored
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_room, "");
        args = {"ants", "--room", "a/../b"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_room, "");
        const uint8_t unset = Application::parse_arguments(1, argv_of(args = {"ants"}, st)).local_player_id;
        for (const char* bad : {"-pnum:4", "-pnum:7", "-pnum:-1", "-pnum:"}) {
            args = {"ants", bad};
            c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            ASSERT_EQ(c.local_player_id, unset);                                         // no team 4 or 7, no digits: ignored
        }
        args = {"ants", "-N", "-N9x", "-N4y", "-N0"};                                   // nothing valid: no team 9, no team 4, an empty name
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.team_names[0], "");
        for (const auto& n : c.team_names) ASSERT_EQ(n, "");
        args = {"ants", "--host"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Host && c.net_port == 4001);       // the original's port
        args = {"ants", "--host", "5555", "--name", "Queen", "--loopback"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Host && c.net_port == 5555 && c.net_loopback_only && c.player_name == "Queen");
        args = {"ants", "--host", "--name", "Queen"};                                    // no port: the next word is not a number
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_port == 4001 && c.player_name == "Queen");
        args = {"ants", "--join", "10.0.0.5:4444", "--name", "Bob"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::Join && c.net_address == "10.0.0.5" && c.net_port == 4444 && c.player_name == "Bob");
        args = {"ants", "--join", "example.org"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_address == "example.org" && c.net_port == 4001);
        args = {"ants", "--join", "fe80::1"};                                            // several colons: an IPv6 address, no port
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_address == "fe80::1" && c.net_port == 4001);
        args = {"ants", "--join", "host", "--port", "7000"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.net_address == "host" && c.net_port == 7000);
        args = {"ants", "--host"};                                                       // the room announces itself on the game's discovery port ...
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.lan_port, net::kLanDiscoveryPort);
        args = {"ants", "--host", "--lan-port", "4555"};                                 // ... or on another one ...
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.lan_port, 4555);
        args = {"ants", "--host", "--no-lan"};                                           // ... or on none
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.lan_port, 0);
    } TEST_END();

    TEST_CASE("N5.1b Command Line: --seat, --title, --window-pos, --window-size, --grid, --cell, --display, --audio-focus (and what is refused)") {
        std::vector<std::string> args;
        std::vector<char*> st;
        ApplicationConfig c = Application::parse_arguments(0, nullptr);
        ASSERT_EQ(c.net_seat, 255);                                                      // defaults: any seat, no layout, no grid, the display of the window, sound always
        ASSERT_FALSE(c.has_window_pos || c.has_window_size || c.audio_follows_focus);
        ASSERT_EQ(c.grid_cols, 0);
        ASSERT_EQ(c.display_index, -1);
        args = {"ants", "--join", "host", "--seat", "2", "--title", "Ants - Blue"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_seat, 2);
        ASSERT_EQ(c.title, "Ants - Blue");
        args = {"ants", "--seat", "4"};                                                  // no fifth colour: ignored
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_seat, 255);
        args = {"ants", "--seat", "-1"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_EQ(c.net_seat, 255);
        args = {"ants", "--window-pos", "100,-20", "--window-size", "800,600"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.has_window_pos && c.window_x == 100 && c.window_y == -20);          // off the left / top of the main display is legal (other displays)
        ASSERT_TRUE(c.has_window_size && c.window_w == 800 && c.window_h == 600);
        args = {"ants", "--window-pos", "nonsense", "--window-size", "10x10"};           // garbage and a window smaller than the game's minimum: not taken
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_FALSE(c.has_window_pos);
        ASSERT_FALSE(c.has_window_size);
        args = {"ants", "--grid", "2x2", "--cell", "3", "--display", "1", "--audio-focus"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.grid_cols == 2 && c.grid_rows == 2 && c.grid_cell == 3 && c.display_index == 1 && c.audio_follows_focus);
        args = {"ants", "--grid", "0x2", "--cell", "-5", "--display", "-7"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.grid_cols == 0 && c.grid_rows == 0);                                // a grid needs at least one cell each way
        ASSERT_EQ(c.grid_cell, 0);
        ASSERT_EQ(c.display_index, -1);
    } TEST_END();

    TEST_CASE("N5.0b --lan-list: what the local network offers is listed (host, address, map, players, version), a silent network says so") {
        uint16_t port = 0;
        {
            auto probe = net::LanBrowser::open(0);                                       // a free UDP port for this test (the game's own port 4001 stays untouched)
            ASSERT_TRUE(probe != nullptr);
            port = probe->port();
        }
        std::ostringstream none;
        ASSERT_EQ(list_lan_rooms(port, 300, none), 0);
        ASSERT_TRUE(none.str().find("No games found.") != std::string::npos);
        Peer host;
        host.net.set_discovery(port, true);
        host.net.set_game_version("v9.9.9");
        ASSERT_TRUE(host.net.host(0, "Queen Anne", true));
        host.net.set_map("TINY.LVL");
        std::ostringstream out;
        const int found = list_lan_rooms(port, 2500, out, [&](uint32_t now) {
            host.now = 1000 + now;
            host.update();
        });
        ASSERT_EQ(found, 1);
        const std::string text = out.str();
        ASSERT_TRUE(text.find("\"Queen Anne\"") != std::string::npos);
        ASSERT_TRUE(text.find("TINY.LVL") != std::string::npos);
        ASSERT_TRUE(text.find("1/4 players") != std::string::npos);
        ASSERT_TRUE(text.find("v9.9.9") != std::string::npos);
        ASSERT_TRUE(text.find("127.0.0.1:" + std::to_string(host.net.listen_port())) != std::string::npos);      // the address to connect to and the room's TCP port
        ASSERT_TRUE(text.find("1 game found.") != std::string::npos);
    } TEST_END();

    TEST_CASE("N5.7 Local Game: The Score Labels Of Teams Without A Name Show Only When Asked For (the browser build has no other players, so no placeholders)") {
        ApplicationConfig cfg = headless_config();
        cfg.player_name = "Alice";
        cfg.team_names[2] = "Carol";
        cfg.label_unnamed_teams = false;                                                  // what the browser build sets
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.hud().roster_mask(), 0x05);                                         // the own team (0) and the named team 2: no "Red:", no "Black:"
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);                                         // the four teams still exist in the game itself
        ASSERT_TRUE(ApplicationConfig{}.label_unnamed_teams);                             // the native default keeps the four labels
    } TEST_END();

    TEST_CASE("N5.8 Local Game: By Default Every Team Has A Score Label (colour words for the unnamed ones)") {
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.hud().roster_mask(), 0x0F);
    } TEST_END();

    TEST_CASE("N5.2 Names Reach Every Place That Shows One (local game): the simulation's texts, the HUD's labels, the results rows, the own label") {
        ApplicationConfig cfg = headless_config();
        cfg.player_name = "Alice";
        cfg.team_names[1] = "Bob";
        cfg.team_names[2] = "Carol";
        cfg.local_player_id = 0;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_FALSE(app.network_active());
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.hud().get_player_name(), "Alice");
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");                                // the own name belongs to the own team
        ASSERT_EQ(app.sim().get_player_name(1), "Bob");
        ASSERT_EQ(app.sim().get_player_name(2), "Carol");
        ASSERT_EQ(app.sim().get_player_name(3), sim::strings::colour_name(0));            // no name given: the colour word (black)
        ASSERT_EQ(app.hud().team_names()[0], "Alice");
        ASSERT_EQ(app.hud().team_names()[1], "Bob");
        ASSERT_EQ(app.hud().team_names()[3], "");
        // the texts of the simulation carry the names: an alliance, a drop-out
        app.sim().apply_command([&]() { Command c; c.type = CommandType::Drop; c.issuer = 1; return c; }());
        bool named = false;
        for (const auto& n : app.sim().poll_news_events()) named = named || n.message_text.find("Bob dropped out of the game!") != std::string::npos;
        ASSERT_TRUE(named);
        // a network game never sends the user and machine name by default
        ApplicationConfig none = headless_config();
        ASSERT_TRUE(none.player_name.empty());
    } TEST_END();
}

namespace {

// AI6: computer players in the application (docs/BOTS.md): the command line, the roster and the names of a local game, the refusals, a room whose host runs a bot
void click_fog(Application& app, bool on) {
    const int32_t x = (on ? MapSelectScreen::BTN_FOW_ON_X : MapSelectScreen::BTN_FOW_OFF_X) + 2;
    const int32_t y = (on ? MapSelectScreen::BTN_FOW_ON_Y : MapSelectScreen::BTN_FOW_OFF_Y) + 2;
    app.map_select().handle_mouse_motion(x, y);                                         // a button acts at the release
    app.map_select().handle_mouse_down(x, y, 1);
    app.map_select().handle_mouse_up(x, y, 1);
}

// A bot of the test's own that acts in a way that it chooses (the registry has the idle bot and the worker bot): at every look it sends its first ant to one of two tiles, alternately
class MarchingBot final : public ai::Bot {
public:
    const char* kind() const noexcept override { return "marching"; }
    void start(const ai::BotContext& c) override { seat = c.seat; }
    void think(const ai::BotView& v, ai::Orders& o) override {
        ++looks;
        if (v.mine().empty()) return;
        o.move({v.mine()[0].id}, sim::TileCoord{looks % 2 == 0 ? 8 : 14, looks % 2 == 0 ? 8 : 14});          // (inside every shipped map)
    }
    void on_command(const sim::Command&, Fate fate, uint64_t) override { fates[static_cast<size_t>(fate)]++; }
    uint8_t seat{255};
    int looks{0};
    int fates[4]{};
};

ai::BotSpec bot_spec(const char* text) {
    ai::BotSpec spec;
    std::string why;
    if (!ai::parse_bot_spec(text, spec, why)) std::cout << "    (bad spec " << text << ": " << why << ")\n";
    return spec;
}

}  // namespace

void run_bot_tests() {
    TEST_CASE("AI6.1 Command Line: --bot SEAT[:SPEC] Is Repeatable And Keeps Its Order; A Spec That Does Not Parse Is A Startup Error; Nothing Without --bot") {
        std::vector<std::string> args;
        std::vector<char*> st;
        args = {"ants", "--bot", "1", "--bot", "2:hard", "--bot", "3:idle:easy", "--headless"};
        ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.startup_error.empty() && c.headless);
        ASSERT_EQ(c.bots.size(), 3u);
        ASSERT_TRUE(c.bots[0].seat == 1 && c.bots[0].kind == "standard" && c.bots[0].level == ai::Level::Medium);
        ASSERT_TRUE(c.bots[1].seat == 2 && c.bots[1].kind == "standard" && c.bots[1].level == ai::Level::Hard);
        ASSERT_TRUE(c.bots[2].seat == 3 && c.bots[2].kind == "idle" && c.bots[2].level == ai::Level::Easy);
        args = {"ants", "--bot", "2:worker", "-pnum=1"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.size() == 1 && c.bots[0].kind == "worker" && c.local_player_id == 1 && c.startup_error.empty());
        // what does not parse: a startup error with the reason (the first one), the good specs around it are kept
        args = {"ants", "--bot", "1", "--bot", "7", "--bot", "fast"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.size() == 1 && c.startup_error.find("--bot 7") != std::string::npos && c.startup_error.find("0, 1, 2 or 3") != std::string::npos);
        args = {"ants", "--bot", "2:genius"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.empty() && c.startup_error.find("genius") != std::string::npos);
        args = {"ants", "--bot"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.empty() && !c.startup_error.empty());
        // and the program refuses to start with it (nothing is opened)
        {
            Application app;
            c.headless = true;
            ASSERT_FALSE(app.init(c));
            ASSERT_TRUE(app.bots() == nullptr);
        }
        // no --bot: no bots, no error
        args = {"ants", "--headless", "-N1Bob"};
        c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        ASSERT_TRUE(c.bots.empty() && c.startup_error.empty());
        ASSERT_TRUE(ApplicationConfig{}.bots.empty() && ApplicationConfig{}.startup_error.empty());
    } TEST_END();

    TEST_CASE("AI6.13 Command Line: --teams ffa | A+B (Two Different Seats Of 0 To 3, ffa In Any Case Is The Default, The Last One Wins) Is The Choice Of The START That This Machine Leads (A Game On This Computer Makes It At Once, A Room (--host, --join, --join-url) Sends It With Its START Since Protocol 13): Anything Else Is A Startup Error That Names The Option, ffa Is Fine Everywhere, And It Is No Mode (The Start Menu Still Shows)") {
        std::vector<std::string> args;
        std::vector<char*> st;
        const auto parsed = [&](std::vector<std::string> a) {
            args = std::move(a);
            return Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
        };
        {   // valid
            ApplicationConfig c = parsed({"ants", "--teams", "0+1", "--headless"});
            ASSERT_TRUE(c.teams.set && c.teams.a == 0 && c.teams.b == 1 && c.startup_error.empty());
            c = parsed({"ants", "--headless", "--teams", "3+2"});
            ASSERT_TRUE(c.teams.set && c.teams.a == 3 && c.teams.b == 2 && c.startup_error.empty());        // (the order is kept: the first seat invites)
            for (const char* ffa : {"ffa", "FFA", "Ffa"}) {
                c = parsed({"ants", "--teams", ffa, "--headless"});
                ASSERT_TRUE(!c.teams.set && c.startup_error.empty());
            }
            c = parsed({"ants", "--teams", "2+3", "--teams", "ffa", "--headless"});                           // the last one wins
            ASSERT_TRUE(!c.teams.set && c.startup_error.empty());
            c = parsed({"ants", "--teams", "ffa", "--teams", "1+0", "--headless"});
            ASSERT_TRUE(c.teams.set && c.teams.a == 1 && c.teams.b == 0 && c.startup_error.empty());
            c = parsed({"ants", "--headless"});                                                              // not asked for: free for all
            ASSERT_TRUE(!c.teams.set && c.startup_error.empty());
            c = parsed({"ants", "--bot", "1", "--bot", "2", "--bot", "3", "--teams", "0+1"});                 // with the bots: both are kept
            ASSERT_TRUE(c.bots.size() == 3 && c.teams.set && c.teams.a == 0 && c.teams.b == 1 && c.startup_error.empty());
        }
        {   // anything else is a startup error that names the option (and the game does not start)
            for (const char* bad : {"0+0", "1+1", "0+4", "4+0", "01", "0+", "+1", "0 + 1", "0+1+2", "fff", "none", "-1+0", ""}) {
                ApplicationConfig c = parsed({"ants", "--headless", "--teams", bad});
                ASSERT_TRUE(!c.teams.set);
                ASSERT_TRUE(c.startup_error.find(std::string("--teams ") + bad + ":") == 0);
                Application app;
                ASSERT_FALSE(app.init(c));
            }
            ApplicationConfig same = parsed({"ants", "--headless", "--teams", "2+2"});
            ASSERT_TRUE(same.startup_error.find("two different seats") != std::string::npos);
            ApplicationConfig missing = parsed({"ants", "--headless", "--teams"});
            ASSERT_TRUE(missing.startup_error.find("--teams needs ffa or two seats like 0+1") == 0);
            ApplicationConfig first = parsed({"ants", "--headless", "--teams", "9", "--teams", "0+1"});        // the first problem is the one that is told
            ASSERT_TRUE(first.startup_error.find("--teams 9:") == 0);
            ApplicationConfig two = parsed({"ants", "--headless", "--teams", "9", "--teams", "8"});           // (and a second bad value does not replace it)
            ASSERT_TRUE(two.startup_error.find("--teams 9:") == 0);
        }
        {   // a room takes --teams too (protocol 13: it is the choice of the START that this machine leads; changed on purpose: protocol 12 refused it with "--teams is for a game on this computer"),
            // whatever the order of the options; ffa is the default of a room too
            for (const std::vector<std::string>& line : {std::vector<std::string>{"ants", "--teams", "0+1", "--host"}, std::vector<std::string>{"ants", "--host", "--teams", "0+1"},
                                                          std::vector<std::string>{"ants", "--join", "127.0.0.1", "--teams", "0+1"}, std::vector<std::string>{"ants", "--teams", "0+1", "--join-url", "ws://example.test/ws"}}) {
                const ApplicationConfig c = parsed(line);
                ASSERT_TRUE(c.startup_error.empty() && c.teams.set && c.teams.a == 0 && c.teams.b == 1 && c.net_role != ApplicationConfig::NetRole::None);
            }
            ApplicationConfig ok = parsed({"ants", "--teams", "ffa", "--host"});
            ASSERT_TRUE(ok.startup_error.empty() && !ok.teams.set);
            for (const char* bad : {"0+0", "0+4", "none", ""}) {                                              // a bad value is a startup error in a room too
                const ApplicationConfig c = parsed({"ants", "--host", "--teams", bad});
                ASSERT_TRUE(!c.teams.set && c.startup_error.find(std::string("--teams ") + bad + ":") == 0);
            }
            const ApplicationConfig both = parsed({"ants", "--join", "127.0.0.1", "--room", "R-1", "--fill-bots", "none,none,easy,hard", "--teams", "0+2"});      // (the two choices of one START)
            ASSERT_TRUE(both.startup_error.empty() && both.teams.set && both.teams.a == 0 && both.teams.b == 2 && both.fill_bots == net::FillPlan(std::array<net::FillLevel, 4>{net::FillLevel::None, net::FillLevel::None, net::FillLevel::Easy, net::FillLevel::Hard}));
        }
        {   // it is no mode: a native game that is started with --teams alone still shows the start menu, which takes the choice as the panel's own
            ApplicationConfig c = parsed({"ants", "--teams", "0+1"});
            ASSERT_TRUE(c.start_menu && c.teams.set);
            c = parsed({"ants", "--teams", "0+1", "--map", "Original-Ants/Maps/TINY.LVL"});
            ASSERT_FALSE(c.start_menu);
        }
    } TEST_END();

    TEST_CASE("AI6.2 Refusals At Startup: A Seat Clash With The Local Player (Whatever The Order Of The Options), Two Bots On One Seat, --join With --bot, A Room's Host Seat") {
        const auto refused = [](const std::function<void(ApplicationConfig&)>& setup) {
            ApplicationConfig cfg = headless_config();
            setup(cfg);
            Application app;
            const bool ok = app.init(cfg);
            return !ok && app.bots() == nullptr;
        };
        ASSERT_TRUE(refused([](ApplicationConfig& c) { c.bots = {bot_spec("0")}; }));                                      // the local player sits at seat 0 by default
        ASSERT_TRUE(refused([](ApplicationConfig& c) { c.local_player_id = 1; c.bots = {bot_spec("1")}; }));
        ASSERT_TRUE(refused([](ApplicationConfig& c) { c.bots = {bot_spec("2"), bot_spec("2:hard")}; }));
        ASSERT_TRUE(refused([](ApplicationConfig& c) {
            c.net_role = ApplicationConfig::NetRole::Join;
            c.net_address = "127.0.0.1";
            c.bots = {bot_spec("1")};
        }));
        ASSERT_TRUE(refused([](ApplicationConfig& c) {
            c.net_role = ApplicationConfig::NetRole::Host;
            c.net_port = 0;
            c.net_loopback_only = true;
            c.bots = {bot_spec("0")};                                                                                  // the host sits at seat 0
        }));
        // via the command line the order of the options does not matter: --bot first, the seat afterwards
        {
            std::vector<std::string> args = {"ants", "--bot", "1", "-pnum=1", "--headless", "--no-lan"};
            std::vector<char*> st;
            ApplicationConfig c = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, st));
            Application app;
            ASSERT_FALSE(app.init(c));
        }
        // and a good one starts
        {
            ApplicationConfig cfg = headless_config();
            cfg.bots = {bot_spec("1"), bot_spec("2:hard")};
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.bots() == nullptr);                                                                        // the setup screen: no game yet, no controller yet
        }
    } TEST_END();

    TEST_CASE("AI6.3 Local Game With Bots: The Roster Is The Local Player And The Bots (No Hill, No Ants For The Empty Seat), The Names Are The Bots' (An Explicit Name Wins), The Controller Runs On The Ticks And Stops With The Match") {
        ApplicationConfig cfg = headless_config();
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("1:medium"), bot_spec("3:idle:hard")};
        cfg.team_names[3] = "Zed";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.sim().roster_mask(), 0x0B);                                                                      // seats 0, 1 and 3
        ASSERT_EQ(app.sim().grid().anthills().size(), 3u);
        ASSERT_EQ(app.hud().roster_mask(), 0x0B);
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");
        ASSERT_EQ(app.sim().get_player_name(1), "Bot (Medium)");
        ASSERT_EQ(app.sim().get_player_name(2), sim::strings::colour_name(1));                                          // nobody sits there: the colour word of seat 2 (red)
        ASSERT_EQ(app.sim().get_player_name(3), "Zed");                                                                 // -N / --team-name wins over "Bot (Idle)"
        ASSERT_EQ(app.hud().team_names()[1], "Bot (Medium)");
        ASSERT_EQ(app.hud().team_names()[3], "Zed");
        ASSERT_TRUE(app.bots() != nullptr);
        ASSERT_EQ(app.bots()->seat_mask(), 0x0A);
        ASSERT_TRUE(app.bots()->stats(1).decisions == 0 && app.bots()->stats(3).decisions == 0);
        ASSERT_TRUE(ants_of(app.sim(), 2).empty() && !ants_of(app.sim(), 1).empty());
        const uint64_t hash0 = app.sim().state_hash().total;
        // the ticks of the local loop reach the controller: the bots look at the world (the idle one never acts; the standard one sends its ants to the food).
        // The first kDialogSteps steps are the "Get ready" dialog, in which the simulation waits, so 400 ticks take kDialogSteps + 400 steps
        for (int i = 0; i < kDialogSteps + 400; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.sim().current_tick(), 400u);
        // medium: every 20 ticks, hard: every 4, the first look on tick 1 + seat (the simulation did not run behind the dialog, so tick 1 is the first there is): seat 1 on 2, 22, ... 382 (20 looks),
        // seat 3 on 4, 8, ... 400 (100 looks)
        ASSERT_TRUE(app.bots()->stats(1).decisions == 20 && app.bots()->stats(3).decisions == 100);
        ASSERT_TRUE(app.bots()->stats(1).released >= 1 && app.bots()->stats(3).released == 0 && app.bots()->stats(1).filtered == 0);
        ASSERT_TRUE(app.sim().state_hash().total != hash0);                                                              // the world moved on (food, queues, clock)
        // the match ends: the results name the bots, and the controller falls silent
        app.sim().set_match_time_remaining_ms(1000);
        for (int i = 0; i < 200 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);
        ASSERT_TRUE(app.sim().is_match_over());
        app.update_results(0.0f);
        app.update_results(0.3f);
        ASSERT_FALSE(app.scorecard().rows().empty());
        std::vector<std::string> row_names;
        for (const auto& row : app.scorecard().rows()) row_names.push_back(row.name);
        ASSERT_TRUE(std::find(row_names.begin(), row_names.end(), "Bot (Medium)") != row_names.end());
        ASSERT_TRUE(std::find(row_names.begin(), row_names.end(), "Alice") != row_names.end());
        ASSERT_TRUE(std::find(row_names.begin(), row_names.end(), "Zed") != row_names.end());
        ASSERT_EQ(row_names.size(), 3u);                                                                                 // the empty seat has no row
        const uint32_t looks = app.bots()->stats(1).decisions;
        for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.bots()->stats(1).decisions, looks);
        // back to the setup screen: no bots; a new game builds new ones
        app.return_to_map_select();
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/TINY.LVL"));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->stats(1).decisions == 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x0B);
    } TEST_END();

    TEST_CASE("AI6.4 Local Game With --map And --bot: The Game Is Running At Once With The Roster Of The Taken Seats; The Local Player May Sit Anywhere") {
        ApplicationConfig cfg = headless_config();
        cfg.start_in_map_select = false;
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        cfg.local_player_id = 1;
        cfg.bots = {bot_spec("2:hard")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x06);
        ASSERT_EQ(app.sim().grid().anthills().size(), 2u);
        ASSERT_EQ(app.sim().get_player_name(2), "Bot (Hard)");
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                                         // (a game that starts straight into its match has no "Get ready" dialog to wait for)
        for (int i = 0; i < 100; ++i) app.update_simulation(0.05f);
        ASSERT_EQ(app.sim().current_tick(), 100u);                                                                      // the match runs at once
        ASSERT_TRUE(app.bots()->stats(2).decisions >= 24);                                                              // (the bot looks from tick 3 on, every 4 ticks)
        // the same game without a bot has no controller and all four teams, as ever
        ApplicationConfig plain = headless_config();
        plain.start_in_map_select = false;
        plain.default_map_path = "Original-Ants/Maps/TINY.LVL";
        Application normal;
        ASSERT_TRUE(normal.init(plain));
        ASSERT_TRUE(normal.bots() == nullptr);
        ASSERT_EQ(normal.sim().roster_mask(), 0x0F);
    } TEST_END();

    TEST_CASE("AI6.5 Fog Of War And Bots: START Is Refused With The Reason On The Setup Screen And On stderr; Without Fog It Starts; A Game Without --bot Plays With Fog As Ever And Has No Controller") {
        ApplicationConfig cfg = headless_config();
        cfg.bots = {bot_spec("1")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        click_fog(app, true);
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_FALSE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_TRUE(app.map_select().room().status.find("Fog of War") != std::string::npos);                              // the prompt line of the setup screen says why
        click_fog(app, false);
        ASSERT_FALSE(app.map_select().is_fog_of_war_enabled());
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.map_select().room().status.empty());                                                               // the refusal is gone
        ASSERT_TRUE(app.bots() != nullptr && !app.sim().is_fog_of_war_enabled());
        // no --bot: fog works, nothing of the bots exists
        ApplicationConfig none = headless_config();
        Application normal;
        ASSERT_TRUE(normal.init(none));
        click_fog(normal, true);
        ASSERT_TRUE(normal.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_TRUE(normal.sim().is_fog_of_war_enabled());
        ASSERT_TRUE(normal.bots() == nullptr);
        ASSERT_EQ(normal.sim().roster_mask(), 0x0F);
        for (int i = 0; i < 50; ++i) normal.update_simulation(0.05f);
        ASSERT_TRUE(normal.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.11 --play Does Nothing In A Room: A Host With The Flag And A Map Stays On The Room's Setup Screen (No Match Starts, No Bot Runs); The Flag Is For A Game Of One Machine") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.play_at_once = true;
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());
        ASSERT_TRUE(app.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.11b --alone Does Nothing In A Room: A Host With The Option Plays The Seats Of Its Room (Two People: Seats 0 And 1), And The Guest Plays The Same Game; The Option Is For A Game Of One Machine") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.alone = true;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(bob.sim.roster_mask(), 0x03);
        ASSERT_TRUE(app.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.11c --alone Does Not Reach A Room Before Its START Either: A Host With The Option And A Map Starts With The Simulation Of All Four Teams (Init Made One For The Command Line's Own Game Only), And Has The Room's Setup Screen") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.alone = true;
        cfg.start_in_map_select = false;                                       // --map: init makes the simulation (before the room): a game of one machine would make it for the seat alone
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);
        ASSERT_EQ(app.sim().get_world_state().anthills.size(), size_t{4});
    } TEST_END();

    TEST_CASE("AI6.11d --game-mode 187 Is The Room's Mode When The Application Hosts: The Room Shows It To A Guest Before START, The Start Carries It, And Both Machines Play A Game Without Food (The Same State); No Option Is The Original's Game") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.game_mode = 1;
        cfg.game_mode_given = true;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        ASSERT_EQ(app.net()->room().mode, 1);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start() && bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_EQ(bob.net.room().mode, 1);                                                                                 // the guest's setup screen is told
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(bob.net.start_info().mode, 1);
        ASSERT_TRUE(app.sim().game_mode() == sim::GameMode::Kills187 && bob.sim.game_mode() == sim::GameMode::Kills187);
        for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        for (const auto& cell : bob.sim.get_world_state().cells) ASSERT_FALSE(cell.is_food);
        ASSERT_TRUE(app.recorder() != nullptr && app.recorder()->replay().head.mode == 1);
        duo.step(4000 + kDialogMs);
        app.net()->freeze();                                                                                               // the host stops sealing: what is in flight arrives, then both are at the same tick
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        // no option: the room is the original's
        ApplicationConfig cfg0 = headless_config();
        cfg0.net_role = ApplicationConfig::NetRole::Host;
        cfg0.net_port = 0;
        cfg0.net_loopback_only = true;
        cfg0.player_name = "Alice";
        Application plain;
        ASSERT_TRUE(plain.init(cfg0));
        ASSERT_EQ(plain.net()->room().mode, 0);
    } TEST_END();

    TEST_CASE("AI6.11e --game-mode 187 With --teams When The Application Hosts: The Start Carries Both, Both Machines Have The Alliances At Tick 0 In The Game Without Food (The Teams Are Not Dropped In The Mode), The Same State To The End Of The Run, And The Recording Has Both In Its Head") {
        using L = net::FillLevel;
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.game_mode = 1;
        cfg.game_mode_given = true;
        cfg.fill_bots = net::FillPlan(std::array<L, 4>{L::None, L::None, L::Easy, L::Hard});
        cfg.teams = LocalTeams{true, 0, 2};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->room().slots[1].rtt_ms != net::kRttUnknown; }, 8000));
        duo.step(200);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);
        ASSERT_EQ(app.net()->start_info().mode, 1);
        ASSERT_EQ(bob.net.start_info().mode, 1);
        ASSERT_TRUE(app.net()->start_info().team_a == 0 && bob.net.start_info().team_a == 0 && bob.net.start_info().team_b == 2);
        ASSERT_TRUE(app.sim().game_mode() == sim::GameMode::Kills187 && bob.sim.game_mode() == sim::GameMode::Kills187);
        ASSERT_EQ(app.sim().current_tick(), uint64_t{0});
        for (uint8_t seat = 0; seat < 4; ++seat) {                                                                         // (Green + Red against Blue + Black: the same on the host's engine and on the guest's)
            const uint8_t want = static_cast<uint8_t>(seat == 0 ? 2 : seat == 1 ? 3 : seat == 2 ? 0 : 1);
            ASSERT_TRUE(app.sim().alliance_of(seat) == want && bob.sim.alliance_of(seat) == want);
        }
        for (const auto& cell : app.sim().get_world_state().cells) ASSERT_FALSE(cell.is_food);
        for (const auto& cell : bob.sim.get_world_state().cells) ASSERT_FALSE(cell.is_food);
        ASSERT_TRUE(app.recorder() != nullptr && app.recorder()->replay().head.mode == 1 && app.recorder()->replay().head.teams == sim::StartTeams({true, 0, 2}));
        duo.step(4000 + kDialogMs);
        app.net()->freeze();                                                                                               // the host stops sealing: what is in flight arrives, then both are at the same tick
        duo.step(3000);
        ASSERT_TRUE(app.sim().current_tick() > 60);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        for (uint8_t seat = 0; seat < 4; ++seat) ASSERT_EQ(app.sim().alliance_of(seat), bob.sim.alliance_of(seat));      // (whatever the bots did to the teams, both engines agree)
        app.quit();
    } TEST_END();

    TEST_CASE("AI6.6 Room With A Bot: The Host's Setup Screen Shows The Bot, Fog Is Refused, START Runs The Bot On The Host's Machine, The Guest Stays Bit-Identical Without Any Bot Code") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:medium")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active() && app.net()->is_host());
        app.pump_network(0.01f);
        ASSERT_TRUE(app.map_select().room().seats[2].occupied && app.map_select().room().seats[2].name == "Bot (Medium)");
        ASSERT_TRUE(app.map_select().room().seats[2].thumb == MapSelectScreen::Thumb::Good);
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Bot);
        ASSERT_TRUE(app.bots() == nullptr);                                                                                // no match yet
        click_fog(app, true);                                                                                              // refused: a bot would see through it, the screen shows Off again
        ASSERT_FALSE(app.net()->room().fog);
        ASSERT_FALSE(app.map_select().is_fog_of_war_enabled());
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        ASSERT_EQ(bob.net.my_seat(), 1);                                                                                   // the first free seat: the bot has seat 2
        ASSERT_TRUE(bob.net.room().slots[2].state == net::SlotState::Bot && bob.net.room().slots[2].name == "Bot (Medium)");
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.sim().roster_mask(), 0x07);
        ASSERT_EQ(app.sim().get_player_name(2), "Bot (Medium)");
        ASSERT_EQ(app.hud().team_names()[2], "Bot (Medium)");
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04);
        duo.step(15000 + kDialogMs);                                                                                       // (the host seals the first turn kDialogMs after the match began: 15 s of play follow)
        ASSERT_TRUE(app.bots()->stats(2).decisions >= 10);                                                                  // it looks every second of the match
        ASSERT_EQ(app.bots()->stats(2).rejected, 0u);
        ASSERT_EQ(bob.sim.roster_mask(), 0x07);
        app.net()->freeze();                                                                                                // the host stops sealing: what is in flight arrives, then both are at the same tick
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());                                                         // the guest ran no bot code and plays the same game
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
        app.return_to_map_select();
        ASSERT_TRUE(app.bots() == nullptr);
    } TEST_END();

    TEST_CASE("AI6.7 A Bot That Acts, In A Local Game: Its Commands Go Through The Application's Door Into The Simulation With The Bot's Seat, None Is Rejected, And Its Ant Really Moves") {
        ApplicationConfig cfg = headless_config();
        cfg.bots = {bot_spec("1:hard")};
        cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x02);
        const std::vector<uint32_t> mine = ants_of(app.sim(), 1);
        ASSERT_FALSE(mine.empty());
        sim::TileCoord start{};
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == mine[0]) start = sim::TileCoord{a.tile_x, a.tile_y};
        }
        for (int i = 0; i < kDialogSteps + 200; ++i) app.update_simulation(0.05f);                                        // (200 ticks after the dialog)
        const auto& st = app.bots()->stats(1);
        ASSERT_TRUE(st.released >= 1);
        ASSERT_EQ(st.rejected, 0u);                                                                                      // the engine accepted what the door carried
        ASSERT_EQ(st.filtered, 0u);
        bool moved = false;
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == mine[0]) moved = a.tile_x != start.x || a.tile_y != start.y || a.state == sim::UnitState::Walking;
        }
        ASSERT_TRUE(moved);                                                                                              // the order reached the ant
        ASSERT_TRUE(ants_of(app.sim(), 0).size() > 0 && app.bots()->stats(1).decisions >= 40);
    } TEST_END();

    TEST_CASE("AI6.8 A Bot That Acts, In A Host's Room: Its Commands Go Through The Room's Door Into The Sequencer With The Bot's Seat, Reach The Guest In The Turn Stream, And Both Machines Stay Bit-Identical") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:hard")};
        cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04);
        const std::vector<uint32_t> bots_ants = ants_of(app.sim(), 2);
        ASSERT_FALSE(bots_ants.empty());
        duo.step(20000);
        const auto& st = app.bots()->stats(2);
        ASSERT_TRUE(st.released >= 3);
        ASSERT_EQ(st.rejected, 0u);                                                                                      // (the sink answers Applied when the sequencer took the command)
        app.net()->freeze();
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        // the guest saw the bot's orders: its copy of the bot's first ant stands where the host's does, and that is not where it started (the guest ran no bot code)
        sim::TileCoord on_host{};
        sim::TileCoord on_guest{};
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == bots_ants[0]) on_host = sim::TileCoord{a.tile_x, a.tile_y};
        }
        for (const auto& a : bob.sim.get_world_state().ants) {
            if (a.id == bots_ants[0]) on_guest = sim::TileCoord{a.tile_x, a.tile_y};
        }
        ASSERT_TRUE(on_host.x == on_guest.x && on_host.y == on_guest.y);
        app.quit();
        app.return_to_map_select();
    } TEST_END();

    // A left click at the middle of one of the own ants of a running match, the way the window delivers it (the camera is put on the ant first: the start view may not show it)
    const auto click_own_ant = [](Application& app, uint32_t ant) {
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id != ant) continue;
            app.renderer().camera().center_on(a.px, a.py, app.sim().grid().width(), app.sim().grid().height());
            int32_t sx = 0;
            int32_t sy = 0;
            if (!app.renderer().camera().world_to_screen(a.px, a.py, sx, sy)) return false;
            SDL_MouseButtonEvent b{};
            b.type = SDL_MOUSEBUTTONDOWN;
            b.button = SDL_BUTTON_LEFT;
            b.state = SDL_PRESSED;
            b.clicks = 1;
            b.x = sx;
            b.y = sy;
            app.handle_mouse_button(b);
            b.type = SDL_MOUSEBUTTONUP;
            b.state = SDL_RELEASED;
            app.handle_mouse_button(b);
            return true;
        }
        return false;
    };

    TEST_CASE("AI6.9 The Opening Of A Local Game (--bot 1:LEVEL, Every Level): The \"Get Ready\" Dialog Is Up For 100 Steps Of 50 Ms Of The Frame Clock And The Simulation Does Not Run Meanwhile (Tick 0 For The Whole Dialog, The Match's Clock Shows Its Full Time), So The Bot Neither Looks Nor Sends Anything; Tick 1 Runs On The Step After The Dialog's Last, The Bot's First Look Is On Tick 1 + Seat And Its First Order 75 To 125 Percent Of Its Reaction Time Later; A Click On An Own Ant Selects Nothing In The Dialog And Selects It After It") {
        for (const char* spec_text : {"1:easy", "1:medium", "1:hard"}) {
            const ai::BotSpec spec = bot_spec(spec_text);
            const ai::Profile profile = ai::profile_for(spec.level);
            ApplicationConfig cfg = headless_config();
            cfg.bots = {spec};
            cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };       // acts at every look
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
            ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x02);
            ASSERT_TRUE(app.bots()->start_hold() == ai::kStartHoldTicks);                                 // every product path has the opening of the product: one token, a first look on the first tick
            const std::vector<uint32_t> bots_ants = ants_of(app.sim(), 1);
            const std::vector<uint32_t> own = ants_of(app.sim(), 0);
            ASSERT_TRUE(!bots_ants.empty() && !own.empty());
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            const uint32_t full_time = app.sim().get_match_time_remaining_ms();
            ASSERT_TRUE(full_time > 0);
            const uint64_t still_hash = app.sim().state_hash().total;
            for (int step = 1; step <= kDialogSteps - 1; ++step) {                                       // steps 1 .. 99
                app.update_simulation(0.05f);
                ASSERT_EQ(app.sim().current_tick(), 0u);                                                  // the simulation waits for the dialog ...
                ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);                            // ... the clock shows the match's full time (the HUD's clock text is a function of it) ...
                ASSERT_TRUE(app.hud().is_match_start_modal_active());                                    // ... the dialog is up ...
                ASSERT_TRUE(app.bots()->stats(1).decisions == 0 && app.bots()->stats(1).released == 0);  // ... and the bot has neither looked nor sent anything to the engine (nothing ticked)
            }
            ASSERT_TRUE(app.sim().state_hash().total == still_hash);                                      // the world is a still picture behind the dialog
            ASSERT_TRUE(click_own_ant(app, own[0]));                                                      // step 99: the dialog takes the click
            ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            app.update_simulation(0.05f);                                                                  // step 100: the dialog closes, the simulation has not run yet
            ASSERT_FALSE(app.hud().is_match_start_modal_active());
            ASSERT_EQ(app.sim().current_tick(), 0u);
            ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);
            ASSERT_TRUE(click_own_ant(app, own[0]));                                                      // the same click, now: the ant is selected (a person can order from the first tick)
            ASSERT_FALSE(app.hud().get_selected_ant_ids().empty());
            ASSERT_TRUE(app.hud().get_selected_ant_id() != 0u);
            app.update_simulation(0.05f);                                                                  // step 101: tick 1 runs, right after the dialog
            ASSERT_EQ(app.sim().current_tick(), 1u);
            ASSERT_TRUE(app.sim().get_match_time_remaining_ms() < full_time);                             // the clock runs from here
            ASSERT_EQ(app.bots()->stats(1).decisions, 0u);                                                 // (the bot's first look is on tick 2: seat 1)
            app.update_simulation(0.05f);                                                                  // tick 2
            ASSERT_EQ(app.bots()->stats(1).decisions, 1u);
            ASSERT_EQ(app.bots()->stats(1).released, 0u);
            uint64_t first = 0;
            for (int t = 3; t <= 400; ++t) {
                app.update_simulation(0.05f);
                if (first == 0 && app.bots()->stats(1).released > 0) first = static_cast<uint64_t>(t);
            }
            ASSERT_TRUE(first != 0);
            ASSERT_TRUE(first >= 2u + profile.reaction_delay * 3 / 4 && first <= 2u + profile.reaction_delay * 5 / 4);       // 75 to 125 percent of the reaction time after the first look
            ASSERT_EQ(app.bots()->stats(1).rejected, 0u);
        }
    } TEST_END();

    TEST_CASE("AI6.10 The Opening Of A Host's Room: The Host Seals The First Turn 5 s After The Match Began (Protocol 12), So The Dialog Is Up Exactly While No Tick Has Run, The Bot Seat Looks And Sends Nothing Before It, The Guest's Engine Has Run No Tick Either, No Turn Carries A Command Of The Bot's Seat Before The Bot's First Look Plus Its Reaction Time, And The Bot Does Play After It") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:hard")};
        cfg.bot_factory = [](const ai::BotSpec&) { return std::make_unique<MarchingBot>(); };
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        std::vector<std::pair<uint64_t, Command>> turn_stream;                                          // what the guest's engine applied, and its tick count at the time
        bob.net.set_on_command([&](const Command& c, const sim::CommandResult&) { turn_stream.emplace_back(bob.sim.current_tick(), c); });
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.bots() != nullptr && app.bots()->seat_mask() == 0x04 && app.bots()->start_hold() == ai::kStartHoldTicks);
        ASSERT_TRUE(app.hud().is_match_start_modal_active());
        ASSERT_EQ(app.sim().current_tick(), 0u);
        // the dialog is up exactly while the host has run no tick: no tick runs for kDialogMs (the first turn is sealed that long after the match began), and the bot has not looked meanwhile
        uint32_t waited_ms = 0;
        while (app.sim().current_tick() == 0 && waited_ms < 20000) {
            ASSERT_TRUE(app.hud().is_match_start_modal_active());
            ASSERT_TRUE(app.bots()->stats(2).decisions == 0 && app.bots()->stats(2).released == 0);
            ASSERT_EQ(bob.sim.current_tick(), 0u);                                                      // the guest's engine waits for the first turn as well
            duo.step(10);
            waited_ms += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                           // the dialog ended with the first turn that ran
        ASSERT_TRUE(waited_ms >= kDialogMs && waited_ms <= kDialogMs + 150);                             // 5 s, and the second turn (the runner starts with two in hand) and a step
        duo.step(15000);
        ASSERT_TRUE(app.bots()->stats(2).released >= 3 && app.bots()->stats(2).rejected == 0);
        app.net()->freeze();
        duo.step(3000);                                                                                    // what is in flight arrives and executes
        std::vector<uint64_t> bot_ticks;
        for (const auto& e : turn_stream) {
            if (e.second.issuer == 2) bot_ticks.push_back(e.first);
        }
        ASSERT_TRUE(bot_ticks.size() >= 3);                                                                // the bot played (its commands are in the guest's turn stream, with its seat)
        const ai::Profile hard = ai::profile_for(ai::Level::Hard);
        ASSERT_TRUE(bot_ticks.front() >= 3u + hard.reaction_delay * 3 / 4);                                // the first look is on tick 3 (seat 2), its first order leaves 6 to 10 ticks later (and travels through a turn)
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
        app.return_to_map_select();
    } TEST_END();

    TEST_CASE("AI6.12 A Room With A Bot Says Why Its Bot Declined: Bob's Invitation Is Accepted (Three Teams Play), The Host's Own Invitation To The Bot Is Declined And ONE Line Of The Chat Log Says That The Bot Has A Teammate (The Slot Of The Room Says Bot), Bob's Refusal Of The Host's Invitation Is A Person's And Adds No Line; Both Machines Stay Identical") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        cfg.bots = {bot_spec("2:hard")};
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->room().slots[1].state == net::SlotState::Client && app.net()->can_start(); }, 8000));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_TRUE(app.net()->room().slots[2].state == net::SlotState::Bot);
        ASSERT_TRUE(duo.until([&]() { return app.sim().current_tick() > 0 && bob.sim.current_tick() > 0; }, 8000));
        duo.step(1000);
        const std::string has_teammate = "Bot (Hard) already has a teammate.";
        const auto count_of = [](const std::string& text, const std::string& what) {
            size_t n = 0;
            for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
            return n;
        };
        const auto transcript = [&]() { return app.hud().chat_transcript(std::string()); };
        // Bob (seat 1) asks the bot (seat 2): three teams play, the bot accepts
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.other_player = 2;
        ASSERT_EQ(bob.net.submit(invite).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(duo.until([&]() { return app.sim().stats_manager().are_allies(1, 2) && bob.sim.stats_manager().are_allies(1, 2); }, 20000));
        ASSERT_EQ(count_of(transcript(), "teammate."), 0u);                                       // nothing was declined yet
        // the host asks Bob, and Bob (a person) says no: the original's text only
        app.hud().request_team_up(app.sim(), 1);
        ASSERT_TRUE(duo.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Waiting; }, 5000));
        Command deny;
        deny.type = CommandType::AllianceDeny;
        deny.other_player = 0;
        ASSERT_EQ(bob.net.submit(deny).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(duo.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::None && app.hud().status_line().text() == "Bob rejected teaming up"; }, 5000));
        duo.step(600);
        ASSERT_EQ(count_of(transcript(), "teammate."), 0u);
        ASSERT_EQ(count_of(transcript(), "three or more teams"), 0u);
        // the host asks the bot, which has Bob as its teammate: it declines, and the line names it as the room does
        app.hud().request_team_up(app.sim(), 2);
        bool said_no = false;
        ASSERT_TRUE(duo.until([&]() {
            said_no = said_no || app.hud().status_line().text() == "Bot (Hard) rejected teaming up";
            return count_of(transcript(), has_teammate) > 0;
        }, 20000));
        ASSERT_TRUE(said_no);                                                                      // (the original's text came first, and stays)
        ASSERT_EQ(count_of(transcript(), "News Flash: " + has_teammate), 1u);
        duo.step(3000);
        ASSERT_EQ(count_of(transcript(), has_teammate), 1u);                                       // once
        app.net()->freeze();
        duo.step(3000);
        ASSERT_EQ(app.sim().current_tick(), bob.sim.current_tick());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced() || bob.net.desynced());
        app.quit();
        app.return_to_map_select();
    } TEST_END();
}


void run_host_tests() {
    TEST_CASE("N5.3 Host: The Setup Screen Is The Room (names, thumbs, the original's prompt); START Loads Everywhere; The Match Runs On The Lock-Step Ticks") {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Host;
        cfg.net_port = 0;
        cfg.net_loopback_only = true;
        cfg.player_name = "Alice";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active());
        ASSERT_TRUE(app.net()->is_host());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_TRUE(app.net()->listen_port() != 0);
        // alone in the room: one row, the original's prompt, START refused with the can't-go cue (nobody to play with)
        app.pump_network(0.01f);
        ASSERT_TRUE(app.map_select().room().networked && app.map_select().room().is_host);
        ASSERT_TRUE(app.map_select().room().seats[0].occupied && app.map_select().room().seats[0].name == "Alice");
        ASSERT_FALSE(app.map_select().room().seats[1].occupied);
        ASSERT_EQ(app.map_select().room().status, std::string(sim::strings::text(sim::strings::kPressStart)));
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);

        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
        Duo duo{app, bob};
        ASSERT_TRUE(duo.until([&]() { return app.net()->can_start(); }, 8000));
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.seats[1].occupied && room.seats[1].name == "Bob");
        ASSERT_TRUE(room.seats[0].thumb == MapSelectScreen::Thumb::Good && room.seats[1].thumb == MapSelectScreen::Thumb::Good);
        ASSERT_EQ(bob.net.room().slots[0].name, "Alice");
        // the host picks the map and the fog through the setup screen's controls; the guest's room follows
        int32_t small_index = -1;
        for (size_t i = 0; i < app.map_select().get_maps().size(); ++i) {
            if (app.map_select().get_maps()[i].filename == "SMALL.LVL") small_index = static_cast<int32_t>(i);
        }
        ASSERT_TRUE(small_index >= 0);
        app.map_select().set_selected_index(small_index);
        const int32_t fx = MapSelectScreen::BTN_FOW_ON_X + 2;
        const int32_t fy = MapSelectScreen::BTN_FOW_ON_Y + 2;
        app.map_select().handle_mouse_motion(fx, fy);                                   // the button acts at the release
        app.map_select().handle_mouse_down(fx, fy, 1);
        app.map_select().handle_mouse_up(fx, fy, 1);
        duo.step(200);
        ASSERT_EQ(bob.net.room().map_name, "SMALL.LVL");
        ASSERT_TRUE(bob.net.room().fog);
        // START
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_TRUE(app.sim().is_fog_of_war_enabled());
        ASSERT_EQ(app.sim().grid().anthills().size(), 2u);
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");
        ASSERT_EQ(app.sim().get_player_name(1), "Bob");
        ASSERT_EQ(app.hud().team_names()[1], "Bob");
        ASSERT_EQ(app.hud().roster_mask(), 0x03);
        ASSERT_TRUE(app.hud().is_modal_open());                                          // "Get ready" for at least 5 s
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());

        // play: orders through the HUD's own sink, both ways, and chat
        int predicted = 0;
        int given = 0;
        uint32_t next = 0;
        for (uint32_t t = 0; t < 30000 + kDialogMs; t += 10) {                           // (the host seals the first turn kDialogMs after the match began: 30 s of play follow)
            if (t >= next) {
                next = t + 900;
                const auto mine = ants_of(app.sim(), 0);
                const auto theirs = ants_of(bob.sim, 1);
                if (!mine.empty()) {
                    const sim::CommandResult r = app.net()->submit(order(0, mine[(t / 900) % mine.size()], static_cast<int16_t>((t / 10) % 40), static_cast<int16_t>((t / 25) % 40)));
                    ++given;
                    predicted += r.ack_ant != 0 ? 1 : 0;
                }
                if (!theirs.empty()) bob.net.submit(order(1, theirs[(t / 900) % theirs.size()], static_cast<int16_t>((t / 15) % 40), static_cast<int16_t>((t / 30) % 40)));
            }
            duo.step(10);
        }
        ASSERT_TRUE(given > 20 && predicted > given / 2);
        ASSERT_FALSE(app.hud().is_modal_open());                                         // the ticks reached the HUD (post_tick), the modal ended
        ASSERT_TRUE(app.net()->turns_executed() > 500);                                  // (turns of 50 ms: the 250 of 100 ms that this stood for, twice)
        // chat both ways
        app.hud().set_chat_input("hello Bob");
        app.hud().send_chat(false);
        bob.net.chat("hello Alice", false);
        duo.step(1000);
        bool host_text = false;
        for (const auto& c : bob.chats) host_text = host_text || (c.text == "hello Bob" && c.sender == 0);
        ASSERT_TRUE(host_text);
        ASSERT_TRUE(log_has(app.hud().get_chat_log(), "Bob:") && log_has(app.hud().get_chat_log(), "hello Alice"));
        // a frame renders with the network overlay code path
        app.render_frame();
        // the guest leaves: its team drops on the host at the same tick, the host plays on
        bob.net.leave();
        duo.step(3000);
        ASSERT_TRUE(app.sim().is_player_dropped(1));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(log_has(app.hud().get_chat_log(), "dropped out of"));
        app.quit();
        ASSERT_FALSE(app.network_active());
    } TEST_END();

    TEST_CASE("N5.90 The site statistics' hook is for a game on this computer only: the same counter that hears a local game hears nothing of a match of the network, on the host that plays it or on a guest, however long it runs (the page counts the online games on the server's side)") {
        {   // the control: a game on this computer is heard
            int told = 0;
            ApplicationConfig cfg = headless_config();
            cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
            cfg.play_at_once = true;
            Application local;
            local.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(local.init(cfg));
            ASSERT_TRUE(local.state() == AppState::Playing && !local.network_active());
            local.hud().dismiss_match_start_modal();
            for (int i = 0; i < 10; ++i) local.update_simulation(0.05f);
            ASSERT_EQ(told, 1);
            local.shutdown();
        }
        {   // the host of a room, with a guest: START, the dialog, half a minute of play
            int told = 0;
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            Application app;
            app.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(app.init(cfg));
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return app.net()->can_start(); }, 8000));
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 8000));
            duo.step(kDialogMs + 3000);
            ASSERT_TRUE(app.net()->turns_executed() > 20 && app.sim().current_tick() > 20);       // the match ran
            ASSERT_EQ(told, 0);
            app.quit();
        }
        {   // a guest of a room (the application is seat 2 of three): the same
            int told = 0;
            Peer host;
            Peer bob;
            Application app;
            app.set_on_local_match_started([&told]() { ++told; });
            ASSERT_TRUE(start_three(host, bob, app, "Cat"));
            Trio trio{app, host, bob};
            trio.step(kDialogMs + 3000);
            ASSERT_TRUE(app.net()->turns_executed() > 20 && app.sim().current_tick() > 20);
            ASSERT_EQ(told, 0);
            app.quit();
        }
    } TEST_END();

    TEST_CASE("N5.3b Host: before the host chooses anything its room is on the map that its setup screen highlights, TREASURE.LVL, and the guest's screen shows it (the host's own choice still wins, Down from the last map wraps to the first); a Maps folder without TREASURE.LVL gives the first map of its list") {
        {
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            Application app;
            ASSERT_TRUE(app.init(cfg));
            const MapSelectScreen& screen = app.map_select();
            ASSERT_EQ(screen.get_maps()[static_cast<size_t>(screen.get_selected_index())].filename, "TREASURE.LVL");
            ASSERT_EQ(app.net()->room().map_name, "TREASURE.LVL");                          // the room is on it from the start: START needs a chosen map, and this is the host's
            Peer bob;
            ASSERT_TRUE(bob.net.join("127.0.0.1", app.net()->listen_port(), "Bob"));
            Duo duo{app, bob};
            ASSERT_TRUE(duo.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && bob.net.room().map_name == "TREASURE.LVL"; }, 8000));
            app.map_select().handle_key_down(SDLK_DOWN);                                      // TREASURE.LVL is the last map of the list: Down wraps to the first
            duo.step(200);
            ASSERT_EQ(bob.net.room().map_name, "GAUNTLET.LVL");
            app.quit();
        }
        {   // a folder that has no TREASURE.LVL (here two copies of TINY): the first map of its list, as before
            namespace fs = std::filesystem;
            std::random_device unique;
            const fs::path folder = fs::temp_directory_path() / ("ants_host_maps_test_" + std::to_string(unique()));
            std::error_code ec;
            fs::create_directories(folder, ec);
            for (const char* name : {"TINY.LVL", "OCEAN.LVL"}) fs::copy_file(maps_dir() + "TINY.LVL", folder / name, fs::copy_options::overwrite_existing, ec);
            ApplicationConfig cfg = headless_config();
            cfg.net_role = ApplicationConfig::NetRole::Host;
            cfg.net_port = 0;
            cfg.net_loopback_only = true;
            cfg.player_name = "Alice";
            cfg.maps_dir = folder.string();
            {
                Application app;
                const bool ok = app.init(cfg);
                const std::string room_map = ok ? app.net()->room().map_name : std::string();
                if (ok) app.quit();
                fs::remove_all(folder, ec);
                ASSERT_TRUE(ok);
                ASSERT_EQ(room_map, "OCEAN.LVL");
            }
        }
    } TEST_END();
}

void run_guest_tests() {
    TEST_CASE("N5.4 Guest: The Room Shows The Host's Choice And The Original's Waiting Text; Only Leave Works; Playing As Seat 1; Team Switching Is Off") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        Application app;                                                                  // no --name: the default in a network game is "Player"
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.network_active());
        ASSERT_FALSE(app.net()->is_host());
        // a guest that is still connecting shows itself alone (the original's slot 0 is the local machine from the start, with the good thumb)
        app.pump_network(0.0f);
        if (app.net()->phase() == net::NetGame::Phase::Connecting) {
            const auto& early = app.map_select().room();
            int shown = 0;
            for (const auto& seat : early.seats) shown += seat.occupied ? 1 : 0;
            ASSERT_TRUE(early.networked && !early.is_host && shown == 1);
            ASSERT_TRUE(early.seats[early.my_seat].occupied && early.seats[early.my_seat].name == "Player" && early.seats[early.my_seat].thumb == MapSelectScreen::Thumb::Good);
        }
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.networked && !room.is_host && room.my_seat == 1);
        ASSERT_TRUE(room.seats[0].occupied && room.seats[0].name == "Alice" && room.seats[1].occupied && room.seats[1].name == "Player");
        {   // the guest's rows: itself first, then the host (the original's slot 0 is the local machine, slot 1 the host)
            const std::array<int8_t, 4> rows = MapSelectScreen::row_seats(room);
            ASSERT_TRUE(rows[0] == 1 && rows[1] == 0 && rows[2] == -1 && rows[3] == -1);
        }
        ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        // the host's choice arrives; the guest's controls do nothing
        host.net.set_map("TINY.LVL");
        host.net.set_fog(true);
        duo.step(300);
        ASSERT_EQ(app.map_select().get_maps()[static_cast<size_t>(app.map_select().get_selected_index())].filename, "TINY.LVL");
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_EQ(app.map_select().room().map_file, std::string("TINY.LVL"));            // what a guest shows: the host's file name
        const int32_t idx = app.map_select().get_selected_index();
        app.map_select().handle_mouse_motion(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3);
        app.map_select().handle_mouse_down(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3, 1);
        app.map_select().handle_mouse_up(MapSelectScreen::BTN_DOWN_X + 3, MapSelectScreen::BTN_DOWN_Y + 3, 1);
        app.map_select().handle_key_down(SDLK_DOWN);
        app.map_select().handle_key_down(SDLK_RETURN);
        app.map_select().handle_key_down(SDLK_s);
        ASSERT_EQ(app.map_select().get_selected_index(), idx);
        ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        // the host starts a two-player match on the 31 x 31 map
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(31337, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(app.sim().get_player_name(0), "Alice");
        ASSERT_EQ(app.sim().get_player_name(1), "Player");
        ASSERT_EQ(app.hud().get_player_name(), "Player");
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());
        // Ctrl+1 does nothing (the original has no team switch key); in a network game the player is its seat in any case
        SDL_KeyboardEvent key{};
        key.type = SDL_KEYDOWN;
        key.keysym.sym = SDLK_1;
        key.keysym.mod = KMOD_CTRL;
        app.handle_key_down(key);
        ASSERT_EQ(app.local_player_id(), 1);
        // play 20 s: the guest's orders go through the host and come back in the turns
        uint32_t next = 0;
        for (uint32_t t = 0; t < 20000; t += 10) {
            if (t >= next) {
                next = t + 700;
                const auto mine = ants_of(app.sim(), 1);
                if (!mine.empty()) app.net()->submit(order(1, mine[(t / 700) % mine.size()], static_cast<int16_t>((t / 10) % 31), static_cast<int16_t>((t / 20) % 31)));
                const auto theirs = ants_of(host.sim, 0);
                if (!theirs.empty()) host.net.submit(order(0, theirs[(t / 700) % theirs.size()], static_cast<int16_t>((t / 12) % 31), static_cast<int16_t>((t / 24) % 31)));
            }
            duo.step(10);
        }
        host.net.freeze();
        duo.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());                    // the whole pipeline, bit-identical
        ASSERT_FALSE(app.net()->desynced());
        ASSERT_EQ(app.sim().current_tick(), host.sim.current_tick());
        // the host leaves: the only guest takes over (host migration) and the old host's team drops out; no team is left besides the guest, so the drop-out
        // decides the match at once (0x100d172) and the results screen opens
        host.net.leave();
        ASSERT_TRUE(duo.until([&]() { return app.net()->is_host(); }, 8000));
        ASSERT_TRUE(app.network_active());
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.net()->match_notice(), "You are the host now.");
        app.render_frame();                                                               // the overlay draws the notice
        duo.step(3000);
        ASSERT_TRUE(app.sim().is_player_dropped(0));
        ASSERT_FALSE(app.sim().is_player_dropped(1));
        ASSERT_TRUE(app.sim().is_match_over());
        ASSERT_TRUE(app.scorecard().is_open());
        // the player leaves the match: back to the local setup screen
        app.return_to_map_select();
        ASSERT_FALSE(app.network_active());
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().room().networked);
        ASSERT_TRUE(app.map_select().can_change_setup());                                 // a local setup screen again
    } TEST_END();

    TEST_CASE("N5.5 Guest: A Machine That Cannot Load The Map Says So In The Original's Words And Cancels The Start For Everybody") {
        Peer host;
        host.check_hash = false;                                                          // the host loads its own file fine; only the guest disagrees
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return host.net.can_start(); }, 8000));
        // a map that this machine's list does not know: the host asks for a file named like a map that is not there
        const uint64_t bogus_hash = 0x1234;
        host.net.set_map("SMALL.LVL");
        duo.step(200);
        ASSERT_TRUE(host.net.start_match(1, bogus_hash));                               // the guest's file has another hash
        ASSERT_TRUE(duo.until([&]() { return host.saw(net::NetGame::Event::Type::Cancelled); }, 8000));
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        ASSERT_TRUE(app.map_select().room().status.find("SMALL.LVL") != std::string::npos);
        ASSERT_TRUE(host.net.status_text().find("Bob") != std::string::npos);
    } TEST_END();

    TEST_CASE("N5.6 Guest: When The Host Leaves A Three-Player Match The Application Follows The Lowest Other Seat, Says So, And Stays Identical To The New Host") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", host.net.listen_port(), "Bob"));
        for (int i = 0; i < 800 && bob.net.phase() != net::NetGame::Phase::Room; ++i) {          // Bob is seated first (seat 1), the application second
            host.now += 10;
            host.update();
            bob.now += 10;
            bob.update();
            ants_test::short_pause();
        }
        ants_test::real_time_tail([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, [&]() { host.update(); bob.update(); });
        ASSERT_TRUE(bob.net.phase() == net::NetGame::Phase::Room && bob.net.my_seat() == 1);
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Carol";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Trio trio{app, host, bob};
        ASSERT_TRUE(trio.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 2 && host.net.can_start(); }, 8000));
        host.net.set_map("SMALL.LVL");
        trio.step(200);
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "SMALL.LVL", hash));
        ASSERT_TRUE(host.net.start_match(77, hash));
        ASSERT_TRUE(trio.until([&]() {
            return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing && bob.net.phase() == net::NetGame::Phase::Playing;
        }, 8000));
        ASSERT_EQ(app.local_player_id(), 2);
        trio.step(3000);
        host.net.leave();
        ASSERT_TRUE(trio.until([&]() { return bob.net.is_host() && app.net()->host_seat() == 1; }, 10000));
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_TRUE(app.network_active());
        ASSERT_FALSE(app.net()->is_host());
        ASSERT_EQ(app.net()->match_notice(), "Bob is the host now.");
        app.render_frame();                                                               // the overlay draws the notice
        // the game goes on with the two of them: the old host is dropped everywhere, an order of the application reaches Bob's simulation
        const uint32_t before = app.net()->turns_executed();
        trio.step(2000);
        ASSERT_TRUE(app.net()->turns_executed() > before + 20);                      // 2 s: forty turns of 50 ms
        ASSERT_TRUE(app.sim().is_player_dropped(0) && bob.sim.is_player_dropped(0));
        ASSERT_FALSE(app.sim().is_player_dropped(1) || app.sim().is_player_dropped(2));
        const auto mine = ants_of(app.sim(), 2);
        ASSERT_FALSE(mine.empty());
        int16_t gx = 0, gy = 0;
        ASSERT_TRUE(open_goal_near_hill(app.sim(), 2, gx, gy));
        app.net()->submit(order(2, mine[0], gx, gy));
        trio.step(2000);
        ASSERT_EQ(bob.sim.get_unit(mine[0]).orig_order, sim::AntUnit::kOrderMove);
        bob.net.freeze();
        trio.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.11 Quit Over The Network: The Guest Quits A Two-Player Match (Ctrl+Q, Y): The Quit Command Ends It On Both Machines With The Guest As Quitter, The Results Open, Both Stay Identical") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        host.net.set_map("TINY.LVL");                                                    // the application chooses the first map of its list right away; a bare host does the same
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Bob";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && host.net.can_start(); }, 8000));
        host.net.set_map("TINY.LVL");
        duo.step(300);
        uint64_t hash = 0;
        ASSERT_TRUE(net::hash_file(maps_dir() + "TINY.LVL", hash));
        ASSERT_TRUE(host.net.start_match(31337, hash));
        ASSERT_TRUE(duo.until([&]() { return app.state() == AppState::Playing && host.net.phase() == net::NetGame::Phase::Playing; }, 8000));
        ASSERT_EQ(app.local_player_id(), 1);
        duo.step(6000);                                                                   // the "get ready" dialog takes every key for its five seconds
        ASSERT_FALSE(app.hud().is_modal_open());
        ASSERT_FALSE(app.sim().is_match_over());
        ASSERT_EQ(app.sim().other_sides(1), 1u);                                          // two teams: the host is the one other side
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
        ASSERT_TRUE(app.is_running());                                                    // the quit is the end of the match, the results follow
        ASSERT_TRUE(duo.until([&]() { return app.sim().is_match_over() && host.sim.is_match_over(); }, 4000));
        ASSERT_EQ(app.sim().quitter(), 1);
        ASSERT_EQ(host.sim.quitter(), 1);
        ASSERT_FALSE(app.sim().is_player_dropped(1));
        ASSERT_TRUE(app.scorecard().is_open());
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash());
        ASSERT_TRUE(host.sim.get_world_state().match_result.is_winner(0));                // the host's screen plays the winner cue, the quitter's the other
        ASSERT_FALSE(app.sim().get_world_state().match_result.is_winner(1));
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.9 Teaming Over The Network: An Offer Reaches The Application As A Question, Accept Makes The Team Everywhere, Team Chat Reaches Only Allies, In The Sender's Colour") {
        Peer host;
        Peer bob;
        Application app;
        ASSERT_TRUE(start_three(host, bob, app, "Carol"));
        Trio trio{app, host, bob};
        ViewportCamera& camera = app.renderer().camera();
        // The match opens with its "Get ready to play!" dialog and the host seals the first turn kDialogMs later; a command that reaches the host BEFORE that is discarded (protocol 12: no honest client sends one,
        // its dialog takes every click and key until its own first turn has run). So the offers of this test wait for the first tick of every machine, as a person's would: Bob and the host are bare
        // machines with no dialog, and the application's HUD is asked directly (a click would be swallowed by the dialog until then).
        ASSERT_TRUE(trio.until([&]() { return app.sim().current_tick() > 0 && bob.sim.current_tick() > 0 && host.sim.current_tick() > 0; }, 8000));
        trio.step(1000);
        // a team message of Alice before there is any team: the application is not her ally and never sees it
        host.net.chat("secret plan", true);
        trio.step(600);
        ASSERT_FALSE(log_has(app.hud().get_chat_log(), "secret plan"));
        // Bob offers the application's team a team: the offer goes through the turns and arrives as the original's question
        Command invite;
        invite.type = CommandType::AllianceInvite;
        invite.other_player = 2;
        ASSERT_EQ(bob.net.submit(invite).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Invitation; }, 5000));
        ASSERT_EQ(app.hud().alliance_dialog_team(), 1);
        ASSERT_EQ(app.hud().alliance_dialog_text(), "Bob (Red) invites you to form a team.  Would you like to accept?");
        ASSERT_TRUE(app.hud().is_modal_open());
        // Accept (A): the answer is a command of the application's seat; every machine makes the team at the same turn
        app.hud().handle_key_down('a', app.sim(), camera);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_TRUE(trio.until([&]() {
            return app.sim().stats_manager().are_allies(1, 2) && bob.sim.stats_manager().are_allies(1, 2) && host.sim.stats_manager().are_allies(1, 2);
        }, 5000));
        trio.step(300);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);                // the question does not come back
        ASSERT_TRUE(log_joined(app.hud().get_chat_log()).find("Bob (Red) and Carol (Blue) are a team now!") != std::string::npos);   // the news flash, wrapped by the chat box
        // Bob's team message reaches his ally in his team's colour (red, index 2); Alice's, sent to her team, still does not
        bob.net.chat("we are allies", true);
        host.net.chat("secret plan two", true);
        trio.step(800);
        const auto& log = app.hud().get_chat_log();
        size_t header = log.size();
        for (size_t i = 0; i < log.size(); ++i) {
            if (log[i] == "Bob (To Teammate):") header = i;
        }
        ASSERT_TRUE(header < log.size());
        ASSERT_EQ(app.hud().get_chat_line_colour(header), 2);
        ASSERT_TRUE(log_has(log, "we are allies"));
        ASSERT_FALSE(log_has(log, "secret plan"));
        // the application's own team message goes through the host to its ally (and nobody else: the host, Alice, is not an ally) and is marked as a team message
        app.hud().set_chat_input("hi Bob");
        app.hud().send_chat(true);
        trio.step(800);
        bool bob_got = false;
        for (const auto& c : bob.chats) bob_got = bob_got || (c.text == "hi Bob" && c.team && c.sender == 2);
        ASSERT_TRUE(bob_got);
        bool alice_got = false;                                                           // (the host relays; it does not hear what it was not sent either)
        for (const auto& c : host.chats) alice_got = alice_got || c.text == "hi Bob";
        ASSERT_FALSE(alice_got);
        host.net.freeze();
        trio.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash() && app.sim().state_hash() == host.sim.state_hash());
        ASSERT_FALSE(app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.10 Teaming Over The Network: The Application Asks And Waits; A Refusal Closes The Waiting Dialog, Withdraw (W) Takes The Offer Back Everywhere") {
        Peer host;
        Peer bob;
        Application app;
        ASSERT_TRUE(start_three(host, bob, app, "Carol"));
        Trio trio{app, host, bob};
        ViewportCamera& camera = app.renderer().camera();
        // The match opens with its "Get ready to play!" dialog and the host seals the first turn kDialogMs later; a command that reaches the host BEFORE that is discarded (protocol 12: no honest client sends one,
        // its dialog takes every click and key until its own first turn has run). So the offers of this test wait for the first tick of every machine, as a person's would: Bob and the host are bare
        // machines with no dialog, and the application's HUD is asked directly (a click would be swallowed by the dialog until then).
        ASSERT_TRUE(trio.until([&]() { return app.sim().current_tick() > 0 && bob.sim.current_tick() > 0 && host.sim.current_tick() > 0; }, 8000));
        trio.step(1000);
        app.hud().request_team_up(app.sim(), 1);                                          // the ally pedestal of Bob's hill
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Waiting; }, 5000));
        ASSERT_EQ(app.hud().alliance_dialog_team(), 1);
        ASSERT_EQ(app.hud().alliance_dialog_text(), "Waiting for Bob (Red) to respond to your offer to team up.");
        ASSERT_EQ(bob.sim.get_world_state().pending_invite_from[1], 2);                   // Bob's simulation holds the offer as well
        // Bob refuses: the waiting dialog closes, the status line says so
        Command deny;
        deny.type = CommandType::AllianceDeny;
        deny.other_player = 2;
        ASSERT_EQ(bob.net.submit(deny).status, sim::CommandResult::Status::Applied);
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::None; }, 5000));
        trio.step(300);
        ASSERT_EQ(app.hud().status_line().text(), "Bob rejected teaming up");
        ASSERT_FALSE(app.sim().stats_manager().are_allies(1, 2));
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        // again, and this time the application takes the offer back with W
        app.hud().request_team_up(app.sim(), 1);
        ASSERT_TRUE(trio.until([&]() { return app.hud().alliance_dialog() == HUD::AllianceDialog::Waiting; }, 5000));
        app.hud().handle_key_down('w', app.sim(), camera);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        ASSERT_TRUE(trio.until([&]() {
            return app.sim().get_world_state().pending_invite_from[1] == 255 && bob.sim.get_world_state().pending_invite_from[1] == 255 &&
                   host.sim.get_world_state().pending_invite_from[1] == 255;
        }, 5000));
        trio.step(300);
        ASSERT_EQ(app.hud().alliance_dialog(), HUD::AllianceDialog::None);
        host.net.freeze();
        trio.step(3000);
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash() && app.sim().state_hash() == host.sim.state_hash());
    } TEST_END();

    TEST_CASE("N5.74 The \"Get Ready\" Dialog Of A Guest (A Match On The Local Network, Protocol 12): It Opens When The Match Begins And Ends With The Guest's First Executed Turn, Not Before And Not After (Up At Every 10 ms Step While No Tick Has Run, Gone The Step The First One Ran); In Those 5 s The Portrait Moves, The Clock Shows The Full Time And The Screen Says Nothing (No Waiting Message, No Lag, No Pause); No Click Selects Until It Is Gone") {
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
        ASSERT_TRUE(app.hud().is_match_start_modal_active() && app.sim().current_tick() == 0 && app.hud().match_start_modal_ticks() <= 1u);
        const uint32_t full_time = app.sim().get_match_time_remaining_ms();
        ASSERT_TRUE(full_time > 0);
        const uint32_t mine = static_cast<uint32_t>(ants_of(app.sim(), 1).front());
        uint32_t waited = 0;
        while (app.sim().current_tick() == 0 && waited < 20000) {
            ASSERT_TRUE(app.hud().is_match_start_modal_active());                              // up exactly while no tick has run ...
            ASSERT_EQ(app.sim().get_match_time_remaining_ms(), full_time);                      // ... the clock shows the match's full time ...
            ASSERT_TRUE(app.net_overlay_now().text.empty());                                    // ... the screen says nothing: no "Waiting for the other players...", no lag, no "Catching up...", no notice ...
            ASSERT_TRUE(app.net()->stalled_ms() == 0 && app.net()->laggard() == 255 && !app.net()->lag_notice() && !app.net()->catching_up() && !app.net()->electing() && !app.net()->desynced());
            if (waited == 2000) {                                                               // ... and a click on an own ant selects nothing
                for (const auto& a : app.sim().get_world_state().ants) {
                    if (a.id != mine) continue;
                    app.renderer().camera().center_on(a.px, a.py, app.sim().grid().width(), app.sim().grid().height());
                    int32_t sx = 0;
                    int32_t sy = 0;
                    ASSERT_TRUE(app.renderer().camera().world_to_screen(a.px, a.py, sx, sy));
                    ASSERT_TRUE(app.hud().handle_mouse_down(sx, sy, 1, app.sim(), app.renderer().camera()));         // (taken by the dialog)
                    app.hud().handle_mouse_up(sx, sy, 1, app.sim(), app.renderer().camera());
                    ASSERT_TRUE(app.hud().get_selected_ant_ids().empty() && app.hud().get_selected_ant_id() == 0u);
                }
            }
            duo.step(10);
            waited += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active());                                  // gone on the very step that the first turn ran
        ASSERT_TRUE(waited >= kDialogMs && waited <= kDialogMs + 250);                          // 5 s, and the link, the second turn (the buffer) and a step
        ASSERT_TRUE(app.hud().match_start_modal_ticks() >= static_cast<uint32_t>(kDialogSteps) - 2u);    // the portrait moved in real time, 50 ms at a time, all the while (the wait is no hang)
        duo.step(3000);
        ASSERT_TRUE(app.sim().get_match_time_remaining_ms() < full_time);                       // the clock runs from the first tick
        ASSERT_TRUE(app.net_overlay_now().text.empty() && app.net()->stalled_ms() == 0);
        host.net.freeze();
        duo.step(2000);
        ASSERT_TRUE(app.sim().state_hash() == host.sim.state_hash() && !app.net()->desynced());
    } TEST_END();

    TEST_CASE("N5.76 Protocol 21, The Countdown Card Of A Server's Room Of Two People: The Dialog Counts 30 s Down From The Begin And Says The Seconds In The Status Line, Shows Who Plays, Keeps The Map Out Of Reach And The Chat In It, Takes Ctrl+Q To Leave, And Is Gone With The First Executed Turn") {
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-1";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = net::kMatchCountdownSeconds;
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", srv.port(), "Bob", 255, "CDN-1"));
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-1";
        cfg.player_name = "Ana";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && app.hud().is_match_start_modal_active(); }, 15000));
        const uint32_t begin = srv.now;                                                      // (the frame of the Begin: within a few steps of it)
        ASSERT_TRUE(app.hud().is_match_countdown_active() && app.hud().is_modal_open());
        const int32_t first = app.countdown_seconds_left();
        ASSERT_TRUE(first == 30 || first == 29);
        ASSERT_EQ(app.sim().current_tick(), 0u);
        ASSERT_EQ(app.hud().status_line().text(), "Match starts in " + std::to_string(first) + " s");       // the status line says it, above the chat box
        hall.step(5000);
        const int32_t later = app.countdown_seconds_left();
        ASSERT_TRUE(later >= 23 && later <= 26);                                              // the count follows the clock
        ASSERT_EQ(app.hud().status_line().text(), "Match starts in " + std::to_string(later) + " s");
        ASSERT_TRUE(app.sim().current_tick() == 0 && bob.sim.current_tick() == 0 && bob.net.phase() == net::NetGame::Phase::Playing);
        // the map is out of reach: a click on an own ant selects nothing (the dialog takes it), as in the 5 s dialog
        const uint32_t mine = static_cast<uint32_t>(ants_of(app.sim(), static_cast<int>(app.local_player_id())).front());
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
        // the chat is the player's: the line is typed, Enter sends it, the other machine hears it with the sender's seat; the keys of the match are only letters for the line
        ASSERT_TRUE(app.hud().chat_takes_text());
        app.hud().handle_text_input("hi Bob");
        ASSERT_EQ(app.hud().get_chat_input(), std::string("hi Bob"));
        SDL_KeyboardEvent enter{};
        enter.type = SDL_KEYDOWN;
        enter.keysym.sym = SDLK_RETURN;
        app.handle_key_down(enter);
        ASSERT_TRUE(app.hud().get_chat_input().empty());
        ASSERT_TRUE(hall.until([&]() { return !bob.chats.empty(); }, 3000));
        ASSERT_TRUE(bob.chats.size() == 1 && bob.chats[0].text == "hi Bob" && bob.chats[0].sender == app.local_player_id());
        {   // a key as the keyboard makes it, through the application's own event loop: the key event, then the text event (SDL_TEXTINPUT). The letter is in the line ONCE.
            SDL_Event key_event{};
            key_event.type = SDL_KEYDOWN;
            key_event.key.type = SDL_KEYDOWN;
            key_event.key.state = SDL_PRESSED;
            key_event.key.keysym.sym = SDLK_a;
            SDL_PushEvent(&key_event);
            SDL_Event text_event{};
            text_event.type = SDL_TEXTINPUT;
            text_event.text.type = SDL_TEXTINPUT;
            std::snprintf(text_event.text.text, sizeof text_event.text.text, "a");
            SDL_PushEvent(&text_event);
            app.run_frame_with_delta(0.001f);
            ASSERT_EQ(app.hud().get_chat_input(), std::string("a"));
        }
        SDL_KeyboardEvent a_key{};
        a_key.type = SDL_KEYDOWN;
        a_key.keysym.sym = SDLK_a;
        a_key.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(a_key);                                                           // Ctrl+A selects nothing and types nothing (there is no select-all here)
        ASSERT_TRUE(app.hud().get_chat_input() == "a" && app.hud().get_selected_ant_ids().empty());
        SDL_KeyboardEvent backspace{};
        backspace.type = SDL_KEYDOWN;
        backspace.keysym.sym = SDLK_BACKSPACE;
        app.handle_key_down(backspace);
        ASSERT_TRUE(app.hud().get_chat_input().empty());
        SDL_KeyboardEvent f1{};
        f1.type = SDL_KEYDOWN;
        f1.keysym.sym = SDLK_F1;
        app.handle_key_down(f1);
        ASSERT_FALSE(app.hud().is_quick_help_open());                                         // every other key stays out of the match
        app.hud().set_chat_input(std::string());
        ASSERT_TRUE(app.hud().is_match_countdown_active());
        // the Quit button's key: Ctrl+Q asks, N keeps the player in the countdown
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open());
        SDL_KeyboardEvent n_ev{};
        n_ev.type = SDL_KEYDOWN;
        n_ev.keysym.sym = SDLK_n;
        app.handle_key_down(n_ev);
        ASSERT_FALSE(app.hud().is_quit_dialog_open());
        ASSERT_TRUE(app.hud().is_match_countdown_active() && app.is_running());
        // the card goes with the first executed turn, 30 s after the Begin (the link, the second turn and a step on top)
        uint32_t waited = srv.now - begin;
        while (app.sim().current_tick() == 0 && waited < 40000) {
            ASSERT_TRUE(app.hud().is_match_countdown_active());
            hall.step(10);
            waited += 10;
        }
        ASSERT_TRUE(app.sim().current_tick() >= 1);
        ASSERT_FALSE(app.hud().is_match_start_modal_active() || app.hud().is_match_countdown_active());
        ASSERT_EQ(app.countdown_seconds_left(), -1);
        ASSERT_TRUE(waited >= net::kMatchCountdownSeconds * 1000u - 150 && waited <= net::kMatchCountdownSeconds * 1000u + 400);
        hall.step(3000);
        ASSERT_TRUE(app.sim().current_tick() > 40 && !app.net()->desynced());
        ASSERT_TRUE(app.sim().state_hash() == bob.sim.state_hash() || hall.identical(app.sim(), bob.sim));
        bob.net.freeze();
    } TEST_END();

    TEST_CASE("N5.78 Protocol 21, Quit In The Countdown Leaves For Good: Ctrl+Q And Yes End The Program (No Quit Command Can Be Sealed Before The First Turn), The Server Drops The Seat In The First Turn And The Player Who Stayed Wins At Once") {
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-3";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = 12;                                                                // (a shorter wait: the test is about leaving in it)
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", srv.port(), "Bob", 255, "CDN-3"));
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-3";
        cfg.player_name = "Ana";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.hud().is_match_countdown_active() && bob.net.phase() == net::NetGame::Phase::Playing; }, 15000));
        const uint8_t ana = app.local_player_id();
        ASSERT_TRUE(ana < 2 && app.net()->phase() == net::NetGame::Phase::Playing);
        hall.step(2000);
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        ASSERT_TRUE(app.hud().is_quit_dialog_open() && app.is_running());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_FALSE(app.is_running());                                                       // the quit dialog's Yes: leave_game, not a Quit command that no turn would carry
        ASSERT_TRUE(hall.until([&]() { return bob.sim.is_match_over() && bob.sim.is_player_dropped(ana); }, 30000));       // the first turn carries the drop: Bob is the only side left
        ASSERT_TRUE(bob.sim.get_world_state().match_result.is_winner(bob.net.my_seat()));
        ASSERT_FALSE(bob.net.desynced());
    } TEST_END();

    TEST_CASE("N5.77 Protocol 21, A Room Whose Setting Is 0 Keeps The 5 s Dialog Of Every Match Before (The Begin Says 0): The Dialog Takes Every Key, No Countdown Card, The Chat Is Not Open, The First Turn Comes After 5 s") {
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-2";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = 0;                                                                  // (a room that does not count down says 0 in its Begin, as a room of one person and a game on the local network do: S3.192, N4.4c)
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", srv.port(), "Bob", 255, "CDN-2"));
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-2";
        cfg.player_name = "Ana";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && app.hud().is_match_start_modal_active(); }, 15000));
        ASSERT_FALSE(app.hud().is_match_countdown_active());
        ASSERT_EQ(app.countdown_seconds_left(), -1);
        ASSERT_FALSE(app.hud().chat_takes_text());                                             // the dialog covers the chat as before
        app.hud().handle_text_input("hi");
        SDL_KeyboardEvent a_key{};
        a_key.type = SDL_KEYDOWN;
        a_key.keysym.sym = SDLK_a;
        app.handle_key_down(a_key);
        ASSERT_TRUE(app.hud().get_chat_input().empty());
        ASSERT_TRUE(app.hud().status_line().text().find("Match starts") == std::string::npos);
        uint32_t waited = 0;
        while (app.sim().current_tick() == 0 && waited < 20000) {
            hall.step(10);
            waited += 10;
        }
        ASSERT_TRUE(waited >= kDialogMs - 300 && waited <= kDialogMs + 400);
        bob.net.freeze();
    } TEST_END();

    TEST_CASE("N5.79 Protocol 21, The Native Game's Fullscreen Button On The Countdown Card Is Wired To The Game's Fullscreen: A Click On It (Press And Release) Puts The Window Fullscreen, The Next One Takes It Back (A Real Window On SDL's Dummy Video Driver; The Page's Own Button Does It In A Browser)") {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");                                            // (SDL_Quit clears the hints: set before every start)
        SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
        Server srv;
        ASSERT_TRUE(srv.listener != nullptr);
        server::RoomSpec spec;
        spec.code = "CDN-3";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.countdown_s = net::kMatchCountdownSeconds;
        ASSERT_TRUE(srv.mgr.create_room(spec, srv.now).ok);
        Peer bob;
        ASSERT_TRUE(bob.net.join("127.0.0.1", srv.port(), "Bob", 255, "CDN-3"));
        const std::filesystem::path settings = std::filesystem::temp_directory_path() / "ants_n579_settings.ini";       // (the player's own settings are never read or written)
        ApplicationConfig cfg = headless_config();
        cfg.headless = false;                                                                  // a real window, without a screen
        cfg.skip_intro = true;
        cfg.settings_path = settings.string();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = srv.port();
        cfg.net_room = "CDN-3";
        cfg.player_name = "Ana";
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Hall hall{srv, &app, {&bob}};
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && app.hud().is_match_countdown_active(); }, 15000));
        SDL_Window* window = SDL_GetWindowFromID(1);
        ASSERT_TRUE(window != nullptr);
        const auto fullscreen = [&]() { return (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0; };
        ASSERT_FALSE(fullscreen());
        const LayoutRect b = app.hud().countdown_button_rect();
        ASSERT_TRUE(app.hud().handle_mouse_down(b.x + 5, b.y + 5, SDL_BUTTON_LEFT, app.sim(), app.renderer().camera()));
        ASSERT_FALSE(fullscreen());                                                            // a press only holds the button
        app.hud().handle_mouse_up(b.x + 5, b.y + 5, SDL_BUTTON_LEFT, app.sim(), app.renderer().camera());
        ASSERT_TRUE(fullscreen());                                                             // the release on it acts
        app.hud().handle_mouse_down(b.x + 5, b.y + 5, SDL_BUTTON_LEFT, app.sim(), app.renderer().camera());
        app.hud().handle_mouse_up(b.x + 5, b.y + 5, SDL_BUTTON_LEFT, app.sim(), app.renderer().camera());
        ASSERT_FALSE(fullscreen());                                                            // and the next click is the way back
        ASSERT_TRUE(app.hud().is_match_countdown_active());                                    // the card is still up through it all
        bob.net.freeze();
        std::error_code ignored;
        std::filesystem::remove(settings, ignored);
    } TEST_END();
}


void run_window_tests() {
    TEST_CASE("N5.15 Window: --window-size and --window-pos put the window where they say") {
        ApplicationConfig cfg = headless_config();
        cfg.has_window_size = true;
        cfg.window_w = 800;
        cfg.window_h = 600;
        cfg.has_window_pos = true;
        cfg.window_x = 30;
        cfg.window_y = 40;
        Application app;
        ASSERT_TRUE(app.init(cfg));
        const WindowRect r = app.window_rect();
        ASSERT_TRUE(r.w == 800 && r.h == 600 && r.x == 30 && r.y == 40);
    } TEST_END();

    TEST_CASE("N5.16 Window: --grid 2x2 gives four windows that lie inside the display and do not overlap, in reading order") {
        WindowRect cells[4];
        SDL_Rect area{0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            ApplicationConfig cfg = headless_config();
            cfg.grid_cols = 2;
            cfg.grid_rows = 2;
            cfg.grid_cell = i;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            cells[i] = app.window_rect();
            ASSERT_TRUE(cells[i].w >= kMinWindowWidth && cells[i].h >= kMinWindowHeight);
            ASSERT_TRUE(std::abs(cells[i].w * 3 - cells[i].h * 4) <= 4);              // the game's own 4:3 (to a pixel of rounding)
            if (i == 0) ASSERT_EQ(SDL_GetDisplayUsableBounds(0, &area), 0);
            ASSERT_TRUE(cells[i].x >= area.x && cells[i].y >= area.y);
            ASSERT_TRUE(cells[i].x + cells[i].w <= area.x + area.w && cells[i].y + cells[i].h <= area.y + area.h);
        }
        ASSERT_TRUE(cells[0].x + cells[0].w <= cells[1].x && cells[2].x + cells[2].w <= cells[3].x);       // left to right
        ASSERT_TRUE(cells[0].y + cells[0].h <= cells[2].y && cells[1].y + cells[1].h <= cells[3].y);       // top to bottom
        ASSERT_TRUE(cells[0].x == cells[2].x && cells[1].x == cells[3].x && cells[0].y == cells[1].y && cells[2].y == cells[3].y);
    } TEST_END();

    TEST_CASE("N5.17 Pointer: leaving the window stops the edge scroll and hides the game's cursor; any pointer event in the window ends it") {
        ApplicationConfig cfg = headless_config();
        Application app;
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
        app.hud().dismiss_match_start_modal();                                           // (the match start notice takes the mouse until it is gone)
        SDL_WindowEvent leave{};
        leave.type = SDL_WINDOWEVENT;
        leave.event = SDL_WINDOWEVENT_LEAVE;
        SDL_WindowEvent enter = leave;
        enter.event = SDL_WINDOWEVENT_ENTER;
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.x = 0;                                                                    // the left scroll strip
        motion.y = 240;
        auto scrolled = [&]() {
            app.renderer().camera().world_x = 400;
            app.renderer().camera().world_y = 400;
            for (int i = 0; i < 6; ++i) app.handle_camera_panning(0.05f);
            return app.renderer().camera().world_x != 400 || app.renderer().camera().world_y != 400;
        };
        ASSERT_FALSE(app.pointer_outside());
        app.handle_mouse_motion(motion);
        ASSERT_TRUE(scrolled());                                                         // the pointer rests on the edge: the view scrolls
        app.handle_window_event(leave);
        ASSERT_TRUE(app.pointer_outside());
        ASSERT_FALSE(scrolled());                                                        // the window has been left: the last position must not keep scrolling
        app.handle_window_event(enter);
        ASSERT_FALSE(app.pointer_outside());
        ASSERT_TRUE(scrolled());
        app.handle_window_event(leave);
        ASSERT_TRUE(app.pointer_outside());
        motion.x = 320;
        motion.y = 240;
        app.handle_mouse_motion(motion);                                                 // a motion event can only come from a pointer inside the window
        ASSERT_FALSE(app.pointer_outside());
        app.handle_window_event(leave);
        SDL_MouseButtonEvent press{};
        press.type = SDL_MOUSEBUTTONDOWN;
        press.button = SDL_BUTTON_LEFT;
        press.x = 320;
        press.y = 240;
        app.handle_mouse_button(press);                                                  // ... and so can a button event
        ASSERT_FALSE(app.pointer_outside());
    } TEST_END();

    TEST_CASE("N5.18 Sound follows the focus with --audio-focus only") {
        for (int follow = 0; follow < 2; ++follow) {
            ApplicationConfig cfg = headless_config();
            cfg.audio_follows_focus = follow != 0;
            Application app;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_game("Original-Ants/Maps/SMALL.LVL"));
            const int volume = app.audio_mixer().sound_volume();
            ASSERT_TRUE(volume > 0);
            SDL_WindowEvent lost{};
            lost.type = SDL_WINDOWEVENT;
            lost.event = SDL_WINDOWEVENT_FOCUS_LOST;
            app.handle_window_event(lost);
            ASSERT_EQ(app.audio_mixer().sound_volume(), follow != 0 ? 0 : volume);            // a window without the focus is silent ...
            SDL_WindowEvent gained = lost;
            gained.event = SDL_WINDOWEVENT_FOCUS_GAINED;
            app.handle_window_event(gained);
            ASSERT_EQ(app.audio_mixer().sound_volume(), volume);                               // ... and the sound is back with the focus
        }
    } TEST_END();
    TEST_CASE("N5.19 Guest: --seat N asks the host for that colour (the start script seats window i in colour i)") {
        Peer host;
        ASSERT_TRUE(host.net.host(0, "Alice", true));
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = host.net.listen_port();
        cfg.player_name = "Dave";
        cfg.net_seat = 3;                                                                // black, although seats 1 and 2 are free
        Application app;
        ASSERT_TRUE(app.init(cfg));
        Duo duo{app, host};
        ASSERT_TRUE(duo.until([&]() { return app.net()->phase() == net::NetGame::Phase::Room && app.net()->my_seat() == 3; }, 8000));
        ASSERT_EQ(host.net.room().slots[3].name, "Dave");
    } TEST_END();
}
