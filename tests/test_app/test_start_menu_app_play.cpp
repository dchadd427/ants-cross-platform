// Tests of the desktop start menu in the application: single player, bots, join, host, the failures of a join, and the way back after a network game (A1.1 - A6.2)
#include "start_menu_app_fixtures.hpp"

void run_start_menu_app_play_tests() {
    TEST_CASE("A1.1 Start: a native game with the menu starts at the menu (not at the setup screen), the game under it does not run, the loading screen's end leads to it, and a config without the menu starts as it always did") {
        TempDir temp;
        Server server;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("settings.ini"))));
            ASSERT_TRUE(app.start_menu_enabled());
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_TRUE(app.net() == nullptr && app.bots() == nullptr);
            const uint64_t tick = app.sim().current_tick();
            for (int i = 0; i < 200; ++i) app.update_simulation(0.05f);                      // 10 s of game time: nothing runs under the menu
            ASSERT_EQ(app.sim().current_tick(), tick);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            app.render_frame();                                                              // the frame is drawn
            // the loading screen's end (a key, a click, the 30 ticks) leads to the menu, never to the quick help or the setup screen
            app.finish_loading();
            ASSERT_EQ(app.state(), AppState::StartMenu);
            // Quit asks the program to end
            ASSERT_TRUE(app.is_running());
            click(app, MenuId::Quit);
            app.pump_network(0.01f);
            ASSERT_FALSE(app.is_running());
        }
        {
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("settings2.ini"));
            cfg.start_menu = false;                                                          // what a config made by hand has, and what every mode flag gives
            ASSERT_TRUE(app.init(cfg));
            ASSERT_FALSE(app.start_menu_enabled());
            ASSERT_EQ(app.state(), AppState::MapSelect);
            app.finish_loading();                                                            // (the loading screen's end of a game without a menu: the quick help)
            ASSERT_EQ(app.state(), AppState::QuickHelp);
        }
        {   // Esc on the first panel is Quit, and asks nothing
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("settings3.ini"))));
            press(app, SDLK_ESCAPE);
            app.pump_network(0.01f);
            ASSERT_FALSE(app.is_running());
        }
    } TEST_END();

    TEST_CASE("A1.1b --play does not skip the start menu: a game with the menu starts at the menu, and the setup screen that the menu leads to is the setup screen (no match starts), whatever --play says (the flag is for a game that was chosen elsewhere: the web page's Play button)") {
        TempDir temp;
        Server server;
        Application app;
        ApplicationConfig cfg = menu_config(server.address(), ini(temp, "settings.ini"));                  // (no quick help: Continue leads to the setup screen at once)
        cfg.play_at_once = true;
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(app.start_menu_enabled());
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_FALSE(app.match_running());
        ASSERT_TRUE(app.net() == nullptr && app.bots() == nullptr);
        click(app, MenuId::Single);                                                                        // the flag waits for the setup screen's first visit: that visit, through the menu, starts nothing
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.match_running());
        ASSERT_TRUE(app.bots() == nullptr);
    } TEST_END();

    TEST_CASE("A1.2 Settings: the name, the bots and the host's map and players survive a restart (two Application instances on one settings file); the server key is read, never written, and --server beats it; --name beats the stored name; a bad stored server is the default") {
        TempDir temp;
        const std::string settings = temp.file("settings.ini");
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config("", settings)));
            ASSERT_TRUE(app.start_menu().server() == ServerAddress{});                       // nothing is said: the public server
            ASSERT_EQ(app.start_menu().name(), std::string("Player"));                       // the game's name for a network game
            click(app, MenuId::JoinWithCode);
            fill(app, MenuId::Name, "Maya");
            press(app, SDLK_ESCAPE);
            click(app, MenuId::Single);
            click(app, MenuId::Seat1);                                                       // red: easy
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);                                                       // black: hard
            press(app, SDLK_ESCAPE);
            click(app, MenuId::HostOnline);
            click(app, MenuId::HostMap);                                                     // Treasure (the default) -> Islands
            click(app, MenuId::HostMap);                                                     // -> Tiny
            click(app, MenuId::HostMap);                                                     // -> small
            click(app, MenuId::HostPlayers);                                                 // 2
            click(app, MenuId::HostPlayers);                                                 // 3
        }
        const std::string text = read_file(settings);
        ASSERT_HAS(text, "name=Maya\n");
        ASSERT_HAS(text, "bots=off,easy,off,hard\n");
        ASSERT_HAS(text, "host_map=small\n");
        ASSERT_HAS(text, "host_players=3\n");
        ASSERT_TRUE(text.find("server=") == std::string::npos);                              // the menu never writes the server
        {   // the file gets a server by hand (and an option of the original beside it)
            std::ofstream out(settings, std::ios::app | std::ios::binary);
            out << "server=play.example.org:4010\nSound Volume=30\n";
        }
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config("", settings)));
            ASSERT_EQ(app.start_menu().name(), std::string("Maya"));
            ASSERT_TRUE(app.start_menu().seat(0) == SeatChoice::Empty && app.start_menu().seat(1) == SeatChoice::Easy && app.start_menu().seat(2) == SeatChoice::Empty &&
                        app.start_menu().seat(3) == SeatChoice::Hard);
            ASSERT_TRUE(app.start_menu().server() == (ServerAddress{"play.example.org", 4010}));    // the stored server
            click(app, MenuId::Single);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Seat1, e) && e.value == "Easy bot");
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Seat3, e) && e.value == "Hard bot");
            press(app, SDLK_ESCAPE);
            click(app, MenuId::HostOnline);
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostMap, e) && e.value == "Small");
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostPlayers, e) && e.value == "3 players");
        }
        {   // --server beats the stored one; --name beats the stored name; neither is written back
            Application app;
            ApplicationConfig cfg = menu_config("other.example.org:5000", settings);
            cfg.player_name = "Zed";
            ASSERT_TRUE(app.init(cfg));
            ASSERT_TRUE(app.start_menu().server() == (ServerAddress{"other.example.org", 5000}));
            ASSERT_EQ(app.start_menu().name(), std::string("Zed"));
        }
        const std::string after = read_file(settings);
        ASSERT_HAS(after, "name=Maya\n");
        ASSERT_HAS(after, "server=play.example.org:4010\n");
        ASSERT_HAS(after, "Sound Volume=30\n");
        {   // a stored server that is no server is the default (with a message on stderr), never a crash
            std::ofstream out(settings, std::ios::app | std::ios::binary);
            out << "server=not a server!\n";
        }
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config("", settings)));
            ASSERT_TRUE(app.start_menu().server() == ServerAddress{});
        }
        // the server of the command line must parse: a bad one never gets to the menu (parse_arguments refuses it, init refuses the config)
        std::vector<std::string> args = {"ants", "--headless", "--start-menu", "--server", "no good", "--settings", settings};
        std::vector<char*> storage;
        const ApplicationConfig bad = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
        ASSERT_HAS(bad.startup_error, "--server no good");
        Application refused;
        ASSERT_FALSE(refused.init(bad));
    } TEST_END();

    TEST_CASE("A2.1 Single player with nobody: the match is the original's single-player game exactly: the same four teams, no bot code, the same state after the get-ready dialog (5 s in which the simulation waits) and 100 ticks as the game that never saw the menu") {
        TempDir temp;
        Server server;
        write_no_quick_help(temp.file("a.ini"));
        write_no_quick_help(temp.file("b.ini"));
        Played via_menu;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            click(app, MenuId::Single);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
            click(app, MenuId::Continue);                                                    // nobody: Continue
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                     // (the quick help is off in this file)
            ASSERT_TRUE(app.bots() == nullptr);
            app.map_select().handle_key_down(SDLK_RETURN);                                   // START on the setup screen
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.bots() == nullptr);
            via_menu = snapshot(app);
        }
        Played plain;
        {
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("b.ini"));
            cfg.start_menu = false;
            cfg.player_name = "Player";                                                      // (the panel proposes this name and names the game by its field: the game that never saw the menu gets the same)
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.state(), AppState::MapSelect);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            plain = snapshot(app);
        }
        ASSERT_EQ(via_menu.roster, 0x0F);
        ASSERT_FALSE(via_menu.bots);
        ASSERT_TRUE(same(via_menu, plain));
        ASSERT_TRUE(via_menu.dialog_up_during_99_steps && via_menu.ticks_in_the_dialog == 0 && via_menu.ticks_after == 100u);      // the dialog, in which the match did not start, and 100 ticks after it
        // with the quick help on (the default), it comes between: Continue, the quick help, the setup screen
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("c.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::QuickHelp);
            app.quick_help_key(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::MapSelect);
        }
    } TEST_END();

    TEST_CASE("A2.2 Single player with two bots: the match starts with exactly the bots that --bot 1:medium --bot 3:hard gives (same roster, names, bot seats, state after the dialog and 100 ticks, decisions); the fog option refuses START with the reason, without fog it starts") {
        TempDir temp;
        Server server;
        write_no_quick_help(temp.file("a.ini"));
        write_no_quick_help(temp.file("b.ini"));
        Played via_menu;
        std::string refusal;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            click(app, MenuId::Single);
            click(app, MenuId::Seat1);
            click(app, MenuId::Seat1);                                                       // red: medium
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);
            click(app, MenuId::Seat3);                                                       // black: hard
            ASSERT_TRUE(app.start_menu().bots().size() == 2);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            click_fog(app, true);                                                            // the setup screen's fog option: a bot would see through it
            ASSERT_TRUE(app.map_select().is_fog_of_war_enabled());
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                     // refused
            ASSERT_TRUE(app.bots() == nullptr);
            refusal = app.map_select().room().status;
            ASSERT_HAS(refusal, "Fog of War");
            click_fog(app, false);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_TRUE(app.bots() != nullptr);
            ASSERT_FALSE(app.sim().is_fog_of_war_enabled());
            ASSERT_EQ(app.sim().roster_mask(), 0x0B);                                        // the seats 0, 1 and 3; the empty seat has no hill
            ASSERT_EQ(app.sim().get_player_name(1), std::string("Bot (Medium)"));
            ASSERT_EQ(app.sim().get_player_name(3), std::string("Bot (Hard)"));
            via_menu = snapshot(app);
        }
        Played by_flags;
        {
            std::vector<std::string> args = {"ants", "--headless", "--bot", "1:medium", "--bot", "3:hard", "--name", "Player", "--settings", temp.file("b.ini")};      // (--name Player: the panel's proposal)
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_FALSE(cfg.start_menu);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            click_fog(app, true);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.map_select().room().status, refusal);                              // the same refusal, in the same words
            click_fog(app, false);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            by_flags = snapshot(app);
        }
        ASSERT_TRUE(via_menu.bots && via_menu.bot_seats == 0x0A);
        ASSERT_TRUE(same(via_menu, by_flags));
        ASSERT_TRUE(via_menu.decisions1 >= 4 && via_menu.decisions3 >= 20);                  // the bots ran
        // and only after the "Get ready" dialog: it was up for the steps 1 .. 99 and gone on the 100th, the simulation did not run in them (the match clock waits for the dialog), and so neither bot
        // looked or sent anything, by the menu and by the flags; the bots then ran for the 100 ticks that followed
        ASSERT_TRUE(via_menu.dialog_up_during_99_steps && by_flags.dialog_up_during_99_steps);
        ASSERT_TRUE(via_menu.ticks_in_the_dialog == 0 && by_flags.ticks_in_the_dialog == 0 && via_menu.ticks_after == 100u && by_flags.ticks_after == 100u);
        ASSERT_TRUE(via_menu.looks_in_the_dialog == 0 && via_menu.orders_in_the_dialog == 0 && by_flags.looks_in_the_dialog == 0 && by_flags.orders_in_the_dialog == 0);
        // the player's own seat can be another one (--player 2): the rows are the other three
        {
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("c.ini"));
            cfg.local_player_id = 2;
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.start_menu().own_seat(), 2);
            click(app, MenuId::Single);
            MenuElement e;
            ASSERT_FALSE(app.start_menu().find_element(MenuId::Seat2, e));
            click(app, MenuId::Seat0);
            ASSERT_EQ(app.start_menu().bots().size(), static_cast<size_t>(1));
            ASSERT_EQ(app.start_menu().bots()[0].seat, 0);
        }
    } TEST_END();

    TEST_CASE("A2.3 Single player with teams: the Teams choice reaches the game as --teams does (three bots and You + Red: the same teams, roster, names and state after the dialog and 100 ticks as --bot 1:medium --bot 2:medium --bot 3:medium --teams 0+1), the engine's News Flash lines are in the chat log and no invitation dialog opened, the choice is written to the settings file and is in the panel of the next run, and two bots with no team chosen are free for all") {
        TempDir temp;
        Server server;
        write_no_quick_help(temp.file("a.ini"));
        write_no_quick_help(temp.file("b.ini"));
        const auto allies = [](Application& app) {
            std::string out;
            for (uint8_t a : app.sim().get_world_state().player_alliances) out += std::to_string(a);
            return out;
        };
        Played via_menu;
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            click(app, MenuId::Single);
            for (const MenuId seat : {MenuId::Seat1, MenuId::Seat2, MenuId::Seat3}) {
                click(app, seat);
                click(app, seat);                                                            // medium
            }
            ASSERT_TRUE(app.start_menu().bots().size() == 3);
            MenuElement row;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Teams, row) && row.value == "Free for all");
            click(app, MenuId::Teams);
            ASSERT_EQ(app.start_menu().teams().a, 0);
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Teams, row) && row.value == "You + Red");
            ASSERT_HAS(read_file(temp.file("a.ini")), "teams=0+1");                          // written at once
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(allies(app), std::string("1032"));                                     // the player and Red, Blue and Black: before the first tick
            ASSERT_TRUE(app.hud().alliance_dialog() == HUD::AllianceDialog::None);
            ASSERT_EQ(app.sim().current_tick(), 0u);
            via_menu = snapshot(app);
            ASSERT_EQ(allies(app), std::string("1032"));                                     // 100 ticks on: the same
            ASSERT_TRUE(app.hud().alliance_dialog() == HUD::AllianceDialog::None);
            const std::string log = app.hud().chat_transcript(std::string());
            size_t flashes = 0;
            for (size_t at = log.find("are a team now!"); at != std::string::npos; at = log.find("are a team now!", at + 1)) ++flashes;
            ASSERT_EQ(flashes, 2u);
        }
        Played by_flags;
        {
            std::vector<std::string> args = {"ants", "--headless", "--bot", "1:medium", "--bot", "2:medium", "--bot", "3:medium", "--teams", "0+1", "--name", "Player", "--settings", temp.file("b.ini")};
            std::vector<char*> storage;
            const ApplicationConfig cfg = Application::parse_arguments(static_cast<int>(args.size()), argv_of(args, storage));
            ASSERT_FALSE(cfg.start_menu);
            ASSERT_TRUE(cfg.teams.set && cfg.teams.a == 0 && cfg.teams.b == 1);
            Application app;
            ASSERT_TRUE(app.init(cfg));
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(allies(app), std::string("1032"));
            by_flags = snapshot(app);
        }
        ASSERT_TRUE(via_menu.bots && via_menu.bot_seats == 0x0E);
        ASSERT_TRUE(same(via_menu, by_flags));                                               // the same game, hash and all
        // the next run of the program finds the choice in its panel (the seats and the team come from the file)
        {
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            ASSERT_TRUE(app.start_menu().teams() == LocalTeams({true, 0, 1}));
            click(app, MenuId::Single);
            MenuElement row;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Teams, row) && row.value == "You + Red");
            // two bots: the choice is for a seat that has none now, so it is no choice: free for all, and the game that follows has no team
            click(app, MenuId::Seat1);
            click(app, MenuId::Seat1);                                                       // Red: Medium, Hard, Empty
            ASSERT_TRUE(app.start_menu().find_element(MenuId::Teams, row) && row.value == "Free for all");
            ASSERT_HAS(read_file(temp.file("a.ini")), "teams=ffa");
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(app.sim().roster_mask(), 0x0D);                                        // the player, Blue and Black
            ASSERT_EQ(allies(app), std::string("4444"));
        }
    } TEST_END();

    TEST_CASE("A2.4 Single player with a name: the panel's field holds the name that the settings have (--name proposes it, else Player), whatever it holds when Continue is pressed is the player's name in that game (the own team's name in the simulation, the score box, the HUD, the results' local row), it is written to the settings file and is in the field of the next run, a name that Join would refuse is refused on the panel (no game, the field has the focus and the line says why), and a game that never saw the menu keeps today's rule") {
        TempDir temp;
        Server server;
        write_no_quick_help(temp.file("a.ini"));
        {   // --name proposes the name, the player types another: that one is the game's, down to the results
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("a.ini"));
            cfg.player_name = "Alice";                                                       // (--name)
            ASSERT_TRUE(app.init(cfg));
            ASSERT_EQ(app.start_menu().name(), std::string("Alice"));
            click(app, MenuId::Single);
            MenuElement field;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::SingleName, field) && field.text == "Alice");
            fill(app, MenuId::SingleName, "Zed");
            ASSERT_EQ(app.start_menu().name(), std::string("Zed"));
            click(app, MenuId::Seat1);                                                       // Red: an Easy bot, so that there is somebody in the results too
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);                                     // (the quick help is off in this file)
            ASSERT_HAS(read_file(temp.file("a.ini")), "name=Zed");                           // written when the game was asked for
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(app.sim().get_player_name(0), std::string("Zed"));                     // the own team's name in the engine's texts
            ASSERT_EQ(app.hud().team_names()[0], std::string("Zed"));                        // the score box's label
            ASSERT_EQ(app.hud().get_player_name(), std::string("Zed"));
            ASSERT_EQ(app.sim().get_player_name(1), std::string("Bot (Easy)"));
            app.hud().dismiss_match_start_modal();
            app.sim().set_match_time_remaining_ms(1000);
            for (int i = 0; i < 200 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);
            ASSERT_TRUE(app.sim().is_match_over());
            app.update_results(0.0f);
            app.update_results(0.3f);
            std::vector<std::string> names;
            for (const auto& row : app.scorecard().rows()) names.push_back(row.name);
            ASSERT_TRUE(std::find(names.begin(), names.end(), "Zed") != names.end());          // the results' local row
            ASSERT_TRUE(std::find(names.begin(), names.end(), "Alice") == names.end());
        }
        {   // the next run: the name that was typed is in the field (nothing proposes another), and Continue with it as it is makes that the game's name
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("a.ini"))));
            ASSERT_EQ(app.start_menu().name(), std::string("Zed"));
            click(app, MenuId::Single);
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.state(), AppState::Playing);
            ASSERT_EQ(app.sim().get_player_name(0), std::string("Zed"));
        }
        {   // --name beats what was stored, as it does for Join and Host
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("a.ini"));
            cfg.player_name = "Carol";
            ASSERT_TRUE(app.init(cfg));
            click(app, MenuId::Single);
            MenuElement field;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::SingleName, field) && field.text == "Carol");
        }
        write_no_quick_help(temp.file("b.ini"));
        {   // nothing stored and no --name: the panel proposes Player, and the game is called that (not the computer's user name that a game without the menu has)
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("b.ini"))));
            click(app, MenuId::Single);
            MenuElement field;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::SingleName, field) && field.text == "Player");
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.sim().get_player_name(0), std::string("Player"));
        }
        {   // a name that Join would refuse is refused on the panel: no game starts, the field has the focus, the line says why; typing takes the line away and a good name goes on
            Application app;
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("b.ini"))));
            click(app, MenuId::Single);
            fill(app, MenuId::SingleName, "Bot (Hard)");
            click(app, MenuId::Continue);
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
            ASSERT_EQ(app.start_menu().selected(), MenuId::SingleName);
            ASSERT_HAS(app.start_menu().message(), "A name like \"Bot (...)\" or \"Pip (bot)\" is for computer players");
            ASSERT_TRUE(app.bots() == nullptr && app.sim().get_player_name(0) != "Bot (Hard)");
            click(app, MenuId::SingleName);                                                  // (typing nothing keeps the selection: Ctrl+A and Backspace empty the field)
            press(app, SDLK_a, KMOD_CTRL);
            press(app, SDLK_BACKSPACE);
            click(app, MenuId::Continue);
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Single);
            ASSERT_EQ(app.start_menu().selected(), MenuId::SingleName);
            ASSERT_EQ(app.start_menu().message(), std::string("Type your name first."));
            fill(app, MenuId::SingleName, "Bob");
            ASSERT_TRUE(app.start_menu().message().empty());
            press(app, SDLK_RETURN);                                                         // Enter in the field asks for the game too
            app.pump_network(0.01f);
            ASSERT_EQ(app.state(), AppState::MapSelect);
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.sim().get_player_name(0), std::string("Bob"));
        }
        {   // a game that never saw the menu keeps its rule: --name, else the computer's user name
            Application app;
            ApplicationConfig cfg = menu_config(server.address(), temp.file("b.ini"));
            cfg.start_menu = false;
            cfg.player_name = "Carl";
            ASSERT_TRUE(app.init(cfg));
            app.map_select().handle_key_down(SDLK_RETURN);
            ASSERT_EQ(app.sim().get_player_name(0), std::string("Carl"));
        }
    } TEST_END();

    TEST_CASE("A3.1 Join a public room as the second player: Connecting, then the quick help (the option's default), then the guest's screen with the room as the server has it; the window's title carries the code; the third player fills the room and the server starts the match") {
        TempDir temp;
        Server server;
        const std::string code = "joinaaa1";
        Peer ann;
        ASSERT_TRUE(ann.join_creating("127.0.0.1", server.port(), "Ann", code, "TINY.LVL", 3));       // the first to join, with the block, makes the room and leads it
        Application app;
        Hall hall{&server, &app, {&ann}, nullptr, false};
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room && ann.net.is_leader(); }, 8000));
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        menu_join(app, "Bob", code);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_HAS(app.start_menu().elements()[1].text, "Connecting to " + server.address() + "...");
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::QuickHelp; }, 8000));
        ASSERT_TRUE(app.network_active());
        ASSERT_EQ(app.net()->phase(), net::NetGame::Phase::Room);
        ASSERT_EQ(app.net()->my_seat(), 1);
        ASSERT_FALSE(app.net()->is_leader());
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        ASSERT_EQ(app.hud().get_player_name(), std::string("Bob"));                          // the name typed for the room is the player's name on the HUD (labels, chat), as --name is
        app.quick_help_key(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        hall.step(600);
        ASSERT_TRUE(app.map_select().is_guest());                                            // the guest's screen: only Leave works
        ASSERT_FALSE(app.map_select().has_start_button());
        const auto& room = app.map_select().room();
        ASSERT_TRUE(room.networked && room.my_seat == 1 && room.seats[0].occupied && room.seats[0].name == "Ann" && room.seats[1].name == "Bob");
        ASSERT_EQ(room.status, std::string(sim::strings::text(sim::strings::kWaitingForHost)));
        ASSERT_EQ(server.status(code).joined, 2);
        ASSERT_EQ(server.status(code).names[1], std::string("Bob"));
        // the third player fills the room: the server starts the match without anybody pressing START, and the application plays it as seat 1
        Peer cat;
        hall.peers.push_back(&cat);
        ASSERT_TRUE(cat.join("127.0.0.1", server.port(), "Cat", code));
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && ann.net.phase() == net::NetGame::Phase::Playing && cat.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x07);
        ASSERT_EQ(app.sim().get_player_name(1), std::string("Bob"));
        ASSERT_EQ(app.window_title(), "Ants - room " + code);                               // the whole match
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), ann.sim));
    } TEST_END();

    TEST_CASE("A3.2 Join as the first player: the room's leader has the host's screen with START (v0.0.93); a second client joins and START starts the match for both") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        const std::string code = "joinbbb2";
        ASSERT_TRUE(server.make_room(code, 3));                                              // (a room that nobody has joined yet: the Join panel makes none, a Hello without a block is no room's maker)
        Application app;
        Peer bob;
        Hall hall{&server, &app, {&bob}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_join(app, "Leader", code);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room && app.net()->is_leader(); }, 8000));
        hall.step(600);
        ASSERT_TRUE(app.map_select().leads_server_room() && app.map_select().has_start_button() && !app.map_select().is_guest());
        ASSERT_EQ(app.map_select().room().status, std::string(sim::strings::text(sim::strings::kPressStart)));
        ASSERT_EQ(server.status(code).leader, 0);
        ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", code));
        ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room && app.net()->room().slots[1].state == net::SlotState::Client; }, 8000));
        hall.step(300);
        ASSERT_EQ(app.state(), AppState::MapSelect);                                         // two of the three seats: nobody starts it but the leader
        app.map_select().handle_key_down(SDLK_RETURN);                                      // START
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), bob.sim));
    } TEST_END();

    TEST_CASE("A3.3 Join the last seat of a room: the server starts the match at once, Welcome, Room and Start arrive together, and the menu takes that for what it is (the player is in the room and the match is loading), never for a lost connection") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        const std::string code = "lastseat";
        Peer ann;
        Application app;
        Hall hall{&server, &app, {&ann}, nullptr, false};
        ASSERT_TRUE(ann.join_creating("127.0.0.1", server.port(), "Ann", code, "TINY.LVL", 2));
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_join(app, "Bob", code);
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && ann.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_TRUE(app.start_menu().message().empty());
        ASSERT_EQ(app.local_player_id(), 1);
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), ann.sim));
    } TEST_END();

    TEST_CASE("A4.1 Host an online match: the code is six characters of the page's alphabet and says nothing else (the block of the Hello made the room), shown in two groups of three with Copy; the player is the first in the room and its leader; a second client joins with the code; Continue leads to the leader's screen and START starts a two-player match; both machines stay identical") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        std::vector<std::string> copied;
        Application app;
        Peer guest;
        Hall hall{&server, &app, {&guest}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.clipboard_set = [&copied](const std::string& text) {
            copied.push_back(text);
            return true;
        };
        ASSERT_TRUE(app.init(cfg));
        menu_host(app, "Hostess", 1, 2);                                                     // Small, three players
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_EQ(code.size(), kRoomCodeChars);                                              // the page's grammar: six characters, no map and no seats in them
        for (const char c : code) ASSERT_TRUE(std::string(kRoomCodeAlphabet).find(c) != std::string::npos);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);                               // from the moment the player is in the room
        ASSERT_EQ(app.hud().get_player_name(), std::string("Hostess"));                      // and the name typed for the room is the player's name on the HUD
        const server::RoomStatus made = server.status(code);
        ASSERT_TRUE(made.map == "SMALL.LVL" && made.expected == 3 && made.joined == 1 && made.names[0] == "Hostess" && made.leader == 0 && made.public_room);      // (the block that the Hello carried)
        ASSERT_TRUE(app.net() != nullptr && app.net()->is_leader() && app.net()->my_seat() == 0);
        // the code is shown to share: Copy puts it on the clipboard
        MenuElement big;
        for (const MenuElement& e : app.start_menu().elements()) {
            if (e.kind == MenuKind::Code) big = e;
        }
        ASSERT_EQ(big.text, room_code_display(code));                                        // (two groups of three: "k7m 2xq")
        click(app, MenuId::Copy);
        ASSERT_TRUE(copied.size() == 1 && copied[0] == code);                                // (the plain code is what is copied)
        // a second client joins with the code: the count on the panel follows
        ASSERT_TRUE(guest.join("127.0.0.1", server.port(), "Guest", code));
        ASSERT_TRUE(hall.until([&]() { return guest.net.phase() == net::NetGame::Phase::Room; }, 8000));
        hall.step(100);
        bool counted = false;
        for (const MenuElement& e : app.start_menu().elements()) counted = counted || e.text == "Players in the room: 2 of 3";
        ASSERT_TRUE(counted);
        ASSERT_EQ(guest.net.room().slots[0].name, std::string("Hostess"));
        // Continue to the room: the leader's screen with START (no quick help: the option is off here)
        click(app, MenuId::EnterRoom);
        hall.step(20);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        hall.step(600);
        ASSERT_TRUE(app.map_select().leads_server_room() && app.map_select().has_start_button());
        ASSERT_EQ(app.map_select().room().map_file, std::string("SMALL.LVL"));
        ASSERT_TRUE(app.map_select().room().seats[0].name == "Hostess" && app.map_select().room().seats[1].name == "Guest");
        app.map_select().handle_key_down(SDLK_RETURN);                                      // START: two of the three seats are taken
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_TRUE(server.status(code).state == server::RoomState::Running);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);                               // the whole match
        hall.step(3000);
        ASSERT_TRUE(hall.identical(app.sim(), guest.sim));
    } TEST_END();

    TEST_CASE("A4.2 Host: Esc / Back on the room's panel leaves the room (the server sees the player go, the title is the program's again) and returns to the Host panel; the server that cannot make a room (its public rooms are all taken) is told in words; a single-player game after that is the local player's, not the name that was typed for the room") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server(2);
        Application app;
        Peer other;
        Hall hall{&server, &app, {&other}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        const std::string local_name = app.hud().get_player_name();                          // what a single-player game calls the player (the system user)
        ASSERT_FALSE(local_name.empty());
        menu_host(app, "Hostess", 0, 0);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        press(app, SDLK_ESCAPE);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Host); }, 4000));
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(hall.until([&]() { return server.status(code).joined == 0; }, 4000));    // the server saw the player go
        ASSERT_TRUE(app.start_menu().message().empty());
        // the server has two public rooms: the first (left empty, it stays until it expires) and now one that somebody else makes; the next host attempt is refused
        ASSERT_TRUE(other.join_creating("127.0.0.1", server.port(), "Other", "taken001", "TINY.LVL", 2));
        ASSERT_TRUE(hall.until([&]() { return other.net.phase() == net::NetGame::Phase::Room; }, 8000));
        click(app, MenuId::Host);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "busy");
        ASSERT_HAS(app.start_menu().message(), "Try again");
        ASSERT_TRUE(app.net() == nullptr);
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        // the room was left: a single-player game from the menu has the name that the panel's field holds, and the field is the same text as the Host panel's (one name for the three panels): the
        // name that was typed for the room, which the field shows
        hall.peers.clear();
        press(app, SDLK_ESCAPE);                                                              // the Host panel's Back
        ASSERT_TRUE(on_panel(app, MenuPanel::Main));
        click(app, MenuId::Single);
        MenuElement name_field;
        ASSERT_TRUE(app.start_menu().find_element(MenuId::SingleName, name_field) && name_field.text == "Hostess");
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().room().networked);
        ASSERT_EQ(app.hud().get_player_name(), std::string("Hostess"));
        ASSERT_EQ(app.hud().get_player_name(), app.start_menu().name());                      // (what the field holds, and nothing else of the room: not its other players' names, not its title)
        ASSERT_EQ(app.window_title(), std::string("Ants"));
    } TEST_END();

    TEST_CASE("A4.3 Host: a room that fills up while its code is on the screen starts by itself (nobody has to press anything): the match begins, the title keeps the code, and Leave afterwards brings the player back to the menu") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        Application app;
        Peer guest;
        Hall hall{&server, &app, {&guest}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_host(app, "Hostess", 0, 1);                                                     // Tiny, two players
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_TRUE(guest.join("127.0.0.1", server.port(), "Guest", code));
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::Playing && guest.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        ASSERT_EQ(app.sim().roster_mask(), 0x03);
        ASSERT_EQ(app.local_player_id(), 0);
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        hall.step(2000);
        ASSERT_TRUE(hall.identical(app.sim(), guest.sim));
        // the player leaves the match through the quit dialog (one other side is left: that ends the match for both), and then Leave on the results
        hall.step(5000);
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        app.handle_key_down(q_ev);
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        app.handle_key_down(y_ev);
        ASSERT_TRUE(hall.until([&]() { return app.scorecard().is_open() && app.sim().is_match_over(); }, 8000));
        app.update_results(0.3f);
        app.scorecard().leave();
        ASSERT_TRUE(app.is_running());
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(app.net() == nullptr);
    } TEST_END();

    TEST_CASE("A4.4 Host: when the server holds a room with the very code that the menu made (one in 890 million), the player would be a guest of somebody else's room, not its leader: the menu says so, leaves the room again and nothing stays") {
        TempDir temp;
        Server server;
        const std::string taken = "aaaaaa";                                                  // the code that a random source of all zeros makes: 'a' is the first character of the alphabet
        ASSERT_TRUE(server.make_room(taken, 4));
        Peer owner;
        Application app;
        Hall hall{&server, &app, {&owner}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.room_code_random = []() { return 0u; };
        ASSERT_TRUE(app.init(cfg));
        ASSERT_TRUE(owner.join("127.0.0.1", server.port(), "Owner", taken));
        ASSERT_TRUE(hall.until([&]() { return owner.net.phase() == net::NetGame::Phase::Room && owner.net.is_leader(); }, 8000));
        menu_host(app, "Hostess", 0, 0);                                                     // Tiny, four players: the code is the taken one
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "room code was taken");
        ASSERT_TRUE(app.net() == nullptr);
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(hall.until([&]() { return server.status(taken).joined == 1; }, 4000));    // the owner is alone in its room again: the menu's player left it
        ASSERT_TRUE(owner.net.is_leader());
    } TEST_END();

    TEST_CASE("A4.5 Host: the server closes the room while its code is on the screen: back to the Host panel with a line of its own (The server closed the room), nothing of the room stays, and the same panel can host again") {
        TempDir temp;
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        menu_host(app, "Hostess", 0, 1);
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        const std::string code = shown_code(app);
        ASSERT_TRUE(server.mgr.close_room(code, server.now));
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_EQ(app.start_menu().message(), std::string("The server closed the room."));
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        click(app, MenuId::Host);                                                            // the same panel hosts again (a new code)
        ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
        ASSERT_TRUE(shown_code(app) != code);
    } TEST_END();

    TEST_CASE("A4.6 Host: with no map stored the panel opens on Treasure and a room that is made without choosing a map is a Treasure room (the block of the Hello names it, the server makes it on TREASURE.LVL for four players); the default is not written to the settings; a map that is stored wins after a restart (the room is made on it)") {
        TempDir temp;
        const std::string settings = ini(temp, "t.ini");
        {   // nothing stored (the fixture server's own default map is TINY.LVL: a Treasure room can only come from the block that the menu made)
            Server server;
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            click(app, MenuId::HostOnline);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostMap, e) && e.value == "Treasure");
            ASSERT_EQ(app.start_menu().settings().host_map, 4);
            fill(app, MenuId::HostName, "Hostess");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            const std::string code = shown_code(app);
            ASSERT_EQ(code.size(), kRoomCodeChars);                                          // the page's grammar: six characters; the map is in the Hello's block
            const server::RoomStatus made = server.status(code);
            ASSERT_TRUE(made.map == "TREASURE.LVL" && made.expected == 4 && made.joined == 1 && made.leader == 0);
        }
        ASSERT_TRUE(read_file(settings).find("host_map") == std::string::npos);              // nothing was chosen, so nothing was written: the default is the program's, not the player's
        {   // a stored choice wins
            {
                std::ofstream out(settings, std::ios::app | std::ios::binary);
                out << "host_map=gauntlet\n";
            }
            Server server;
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), settings)));
            click(app, MenuId::HostOnline);
            MenuElement e;
            ASSERT_TRUE(app.start_menu().find_element(MenuId::HostMap, e) && e.value == "Gauntlet");
            ASSERT_EQ(app.start_menu().settings().host_map, 3);
            fill(app, MenuId::HostName, "Hostess");
            click(app, MenuId::Host);
            ASSERT_TRUE(hall.until([&]() { return on_panel(app, MenuPanel::Room); }, 8000));
            const std::string code = shown_code(app);
            ASSERT_EQ(code.size(), kRoomCodeChars);
            ASSERT_EQ(server.status(code).map, std::string("GAUNTLET.LVL"));
        }
    } TEST_END();

    TEST_CASE("A5.1 Failures come back to the panel with a clear line, every one different: the server cannot be reached (named), no room with that code (named), a name the server would rename is refused on the panel without any connection, and the lost connection") {
        TempDir temp;
        Server server;
        Application app;
        RawServer raw;
        Hall hall{&server, &app, {}, &raw, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        // a port that nothing listens on
        const uint16_t closed = []() {
            auto l = net::TcpListener::listen(0, true);
            return l ? l->port() : uint16_t{1};
        }();
        app.start_menu().set_server(ServerAddress{"127.0.0.1", closed});
        menu_join(app, "Dave", "abc-123");
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "Cannot reach 127.0.0.1:" + std::to_string(closed));
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());                          // nothing of the attempt stays
        ASSERT_EQ(app.start_menu().name(), std::string("Dave"));                             // the fields are as they were
        ASSERT_EQ(app.start_menu().code(), std::string("abc-123"));
        // the real server: no room with that code (the case of the code is kept: the server tells the code that was sent)
        app.start_menu().set_server(ServerAddress{"127.0.0.1", server.port()});
        fill(app, MenuId::Code, "Zz-Nope-1");
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "no room with the code Zz-Nope-1");
        ASSERT_HAS(app.start_menu().message(), server.address());
        ASSERT_HAS(app.start_menu().message(), "capital letters matter");
        // a name that looks like a bot's never reaches any server (the one that would rename it is not asked)
        app.start_menu().set_server(ServerAddress{"127.0.0.1", raw.port()});
        fill(app, MenuId::Name, "Bot (Easy)");
        click(app, MenuId::Join);
        hall.step(300);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_HAS(app.start_menu().message(), "Bot (");
        ASSERT_EQ(raw.accepted, 0);
        // the server hangs up on the Hello: the connection was lost before the room
        raw.mode = RawServer::Mode::HangUp;
        fill(app, MenuId::Name, "Dave");
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "lost before you were in the room");
        ASSERT_HAS(app.start_menu().message(), "127.0.0.1:" + std::to_string(raw.port()));
        ASSERT_EQ(raw.hellos.size(), static_cast<size_t>(1));
        ASSERT_TRUE(raw.hellos[0].name == "Dave" && raw.hellos[0].room == "Zz-Nope-1" && raw.hellos[0].version == net::kProtocolVersion);   // what the server heard: the name and the code as typed
        ASSERT_TRUE(app.net() == nullptr);
    } TEST_END();

    TEST_CASE("A5.2 Every reason that a server can answer with is told in its own words: room full, another version (this game's version named), the match already running, removed, bad request, no such room, and (protocol 10) dropped, rejoin failed, taken over; hosting that is refused by NoSuchRoom says the server is busy") {
        TempDir temp;
        Application app;
        RawServer raw;
        Hall hall{nullptr, &app, {}, &raw, false};
        ASSERT_TRUE(app.init(menu_config("127.0.0.1:" + std::to_string(raw.port()), temp.file("s.ini"))));
        struct Case {
            net::RejectReason reason;
            const char* has;
        };
        const Case cases[] = {
            {net::RejectReason::Full, "is full"},
            {net::RejectReason::VersionMismatch, "another version"},
            {net::RejectReason::MatchRunning, "already started"},
            {net::RejectReason::Kicked, "removed"},
            {net::RejectReason::BadRequest, "did not accept"},
            {net::RejectReason::NoSuchRoom, "no room with the code"},
            {net::RejectReason::Dropped, "dropped from the game"},                       // (protocol 10: the answers to a Hello with a key; the menu's own never shows one, but a server may send them)
            {net::RejectReason::RejoinFailed, "could not be rejoined"},
            {net::RejectReason::Superseded, "taken over by another window"},
        };
        raw.mode = RawServer::Mode::Reject;
        std::vector<std::string> seen;
        for (const Case& c : cases) {
            raw.reason = c.reason;
            if (app.start_menu().panel() != MenuPanel::Main) press(app, SDLK_ESCAPE);
            const std::string code = "room-" + std::to_string(static_cast<int>(c.reason));
            menu_join(app, "Dave", code);
            ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
            const std::string message = app.start_menu().message();
            ASSERT_HAS(message, c.has);
            if (c.reason == net::RejectReason::VersionMismatch) ASSERT_HAS(message, std::string(VERSION_STRING));
            if (c.reason == net::RejectReason::Full || c.reason == net::RejectReason::NoSuchRoom) ASSERT_HAS(message, code);
            ASSERT_TRUE(std::find(seen.begin(), seen.end(), message) == seen.end());          // each reason has its own line
            seen.push_back(message);
            ASSERT_TRUE(app.net() == nullptr);
        }
        press(app, SDLK_ESCAPE);
        // hosting: the public room that the server cannot make is NoSuchRoom, which is told as "busy" (there is no room to look for: the code is the player's own)
        raw.reason = net::RejectReason::NoSuchRoom;
        menu_host(app, "Hostess", 0, 0);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "busy");
        ASSERT_TRUE(app.net() == nullptr);
        // and a version mismatch of a host attempt is the same words as a join's
        raw.reason = net::RejectReason::VersionMismatch;
        click(app, MenuId::Host);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host) && app.start_menu().message().find("another version") != std::string::npos; }, 8000));
    } TEST_END();

    TEST_CASE("A5.3 Cancel: Esc while the name is looked up and the Cancel button while the server has not answered end the attempt at once (no error, the panel as it was, nothing of the connection stays), and the next attempt works; a lookup that never ends, one that fails and a server that never answers end in a message") {
        TempDir temp;
        Server server;
        Application app;
        RawServer raw;
        Hall hall{&server, &app, {}, &raw, false};
        const auto lookup = std::make_shared<Lookup>();
        ApplicationConfig cfg = menu_config("slow.example.org:" + std::to_string(raw.port()), temp.file("s.ini"));
        cfg.host_resolver = lookup_of(lookup);
        cfg.menu_connect_timeout_ms = 3000;
        ASSERT_TRUE(app.init(cfg));
        // (1) cancelled during the name lookup: Esc
        menu_join(app, "Dave", "abc-1");
        hall.step(100);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_HAS(app.start_menu().elements()[1].text, "Connecting to slow.example.org:" + std::to_string(raw.port()) + "...");
        ASSERT_TRUE(app.net() == nullptr);
        press(app, SDLK_ESCAPE);
        hall.step(20);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_TRUE(app.start_menu().message().empty());                                     // a cancel is no error
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        lookup->release = true;                                                              // the abandoned lookup answers now: nothing happens
        hall.step(300);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_TRUE(app.net() == nullptr);
        ASSERT_EQ(raw.accepted, 0);
        // (2) cancelled while the server has not answered (it accepted and says nothing): the Cancel button
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return raw.accepted == 1 && app.net() != nullptr; }, 8000));
        hall.step(200);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Connecting);
        ASSERT_EQ(raw.connected(), 1);
        click(app, MenuId::Cancel);
        hall.step(100);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        ASSERT_TRUE(app.start_menu().message().empty());
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_TRUE(hall.until([&]() { return raw.connected() == 0; }, 4000));               // the connection is closed
        // (3) the next attempt works: the real server answers (no such room)
        app.start_menu().set_server(ServerAddress{"127.0.0.1", server.port()});
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "no room with the code abc-1");
        // (4) a lookup that never ends: the time limit of the attempt says so
        lookup->release = false;
        app.start_menu().set_server(ServerAddress{"slow.example.org", raw.port()});
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("did not answer") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "The server slow.example.org:" + std::to_string(raw.port()) + " did not answer");
        ASSERT_TRUE(app.net() == nullptr);
        lookup->release = true;
        hall.step(100);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Join);
        // (5) a name that cannot be found
        lookup->mode = 1;
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("Cannot find") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "Cannot find slow.example.org");
        ASSERT_TRUE(lookup->calls >= 3);
        // (6) a server that accepts and never answers: the time limit of the attempt ends the wait
        app.start_menu().set_server(ServerAddress{"127.0.0.1", raw.port()});
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 30000));
        ASSERT_HAS(app.start_menu().message(), "did not answer");
        ASSERT_TRUE(app.net() == nullptr);
    } TEST_END();

    TEST_CASE("A5.4 What a real server refuses: a room whose match has started is 'already started'; on a server without public rooms a code that nobody made is no room (the code is named); the first player of a room that exists joins at once") {
        TempDir temp;
        Server server(0);                                                                    // no public rooms: only the rooms that somebody made
        ASSERT_TRUE(server.make_room("run-room-1", 2));
        Peer ann;
        Peer bob;
        Application app;
        Hall hall{&server, &app, {&ann, &bob}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "run-room-1"));
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
        ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", "run-room-1"));
        ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Playing && bob.net.phase() == net::NetGame::Phase::Playing; }, 20000));
        menu_join(app, "Dave", "run-room-1");
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "already started");
        ASSERT_TRUE(app.net() == nullptr);
        // a code that nobody made on a server that makes no public rooms
        fill(app, MenuId::Code, "k7m 2xq");
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join) && app.start_menu().message().find("k7m2xq") != std::string::npos; }, 8000));
        ASSERT_HAS(app.start_menu().message(), "no room with the code k7m2xq");
        // hosting there is refused in the words of a busy server
        press(app, SDLK_ESCAPE);
        menu_host(app, "Hostess", 0, 0);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Host); }, 8000));
        ASSERT_HAS(app.start_menu().message(), "busy");
    } TEST_END();

    TEST_CASE("A6.1 After a network match: the results screen, then Leave: back at the start menu (not the end of the program), nothing of the game stays (the room, the net, the title, the names), and the menu works again: a single-player game can be started") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        Application app;
        Peer guest;
        Hall hall{&server, &app, {&guest}, nullptr, false};
        ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
        const std::string local_name = app.sim().get_player_name(0);                         // what a single-player game calls the player (the system user)
        ASSERT_FALSE(local_name.empty());
        std::string report;
        ASSERT_TRUE(host_join_start(hall, app, guest, "127.0.0.1", server.port(), report));
        const std::string code = server.mgr.list(server.now)[0].code;
        ASSERT_EQ(app.window_title(), "Ants - room " + code);
        hall.step(6000);                                                                     // the "get ready" dialog is over
        ASSERT_FALSE(app.sim().is_match_over());
        sim::Command quit;                                                                   // the guest quits the two-player match: the match is over
        quit.type = sim::CommandType::Quit;
        quit.issuer = 1;
        guest.net.submit(quit);
        ASSERT_TRUE(hall.until([&]() { return app.sim().is_match_over() && app.scorecard().is_open(); }, 8000));
        app.update_results(0.3f);
        ASSERT_FALSE(app.scorecard().rows().empty());
        ASSERT_EQ(app.state(), AppState::Playing);                                           // the results are on top of the match; nothing has happened to the program
        ASSERT_TRUE(app.is_running());
        ASSERT_TRUE(app.audio_mixer().music_filepath().find("INTRO") == std::string::npos);  // a match plays one of the game's pieces
        StartMenu layout;                                                                    // where the first panel's entries lie
        layout.show_main();
        MenuElement join_entry;
        ASSERT_TRUE(layout.find_element(MenuId::JoinWithCode, join_entry));
        app.handle_menu_event(mouse_event(SDL_MOUSEMOTION, join_entry.rect.x + 20, join_entry.rect.y + 20));   // the pointer rests where "Join with a code" will be (the last selection of the first panel was "Host an online match")
        app.scorecard().leave();                                                             // Leave (the button, Enter, C, Q, X)
        ASSERT_TRUE(app.is_running());
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
        ASSERT_EQ(app.start_menu().selected(), MenuId::JoinWithCode);                        // the menu selects what the resting pointer is on, as every screen does
        ASSERT_TRUE(app.audio_mixer().music_filepath().find("INTRO") != std::string::npos);  // the menu has the intro music, not the match's piece
        ASSERT_TRUE(app.start_menu().message().empty());
        ASSERT_TRUE(app.net() == nullptr && !app.network_active());
        ASSERT_FALSE(app.scorecard().is_open());
        ASSERT_EQ(app.window_title(), std::string("Ants"));
        ASSERT_TRUE(app.bots() == nullptr);
        ASSERT_EQ(app.hud().get_player_name(), local_name);                                  // the name that was typed for the room is gone from the screens at once
        // the menu works again: a single-player game from here (the name of its field, which is the one typed for the room)
        hall.peers.clear();
        click(app, MenuId::Single);
        click(app, MenuId::Continue);
        app.pump_network(0.01f);
        ASSERT_EQ(app.state(), AppState::MapSelect);
        ASSERT_FALSE(app.map_select().room().networked);
        ASSERT_EQ(app.local_player_id(), 0);
        app.map_select().handle_key_down(SDLK_RETURN);
        ASSERT_EQ(app.state(), AppState::Playing);
        ASSERT_EQ(app.sim().roster_mask(), 0x0F);                                            // the original's single-player game again
        ASSERT_FALSE(app.network_active());
        ASSERT_EQ(app.sim().get_player_name(0), std::string("Hostess"));                     // the panel's field holds the name that was typed for the room (one name for the three panels), and the game takes it
        ASSERT_EQ(app.sim().get_player_name(0), app.start_menu().name());
        ASSERT_EQ(app.sim().get_player_name(1), std::string("Red"));                         // (nothing else of the room's game: the other seats have their colors' names)
    } TEST_END();

    TEST_CASE("A6.2 A network game that is lost or left goes back to the menu too: the server closes the room in the middle of the match (the notice names it), a room that dies under the player, Leave on the room's screen, and the quit dialog's Yes; without the menu the same Leave ends the program as it always did") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server(16);
        for (const bool holds : {false, true}) {   // the room is closed under the players: the match cannot go on. A server that holds no seats (--no-reconnect) loses the connection and says so; the default (release B) holds seats: the game tries to come back and its keyed Hello is told that the match is over (a room that is gone is not made again for a key) and the server's answer is the notice
            Server closing(16, kAllMaps, holds);
            Application app;
            Peer guest;
            Hall hall{&closing, &app, {&guest}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(closing.address(), ini(temp, holds ? "closing_holds.ini" : "closing_none.ini"))));          // (a settings file for each: the menu remembers its choices in it)
            std::string report;
            const bool started = host_join_start(hall, app, guest, "127.0.0.1", closing.port(), report);
            if (!started) std::cout << "\n    (" << report << ")";                              // (what went wrong, when it does)
            ASSERT_TRUE(started);
            const std::string code = closing.mgr.list(closing.now)[0].code;
            hall.step(1000);
            ASSERT_TRUE(closing.mgr.close_room(code, closing.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 12000));
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.start_menu().panel(), MenuPanel::Main);
            ASSERT_HAS(app.start_menu().message(), holds ? "The match is over." : "lost");        // the notice is on the first panel
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
        }
        {   // Leave on the room's screen
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            ASSERT_TRUE(server.make_room("leave001", 4));                                     // (a room that exists: the Join panel makes none)
            menu_join(app, "Dave", "leave001");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(600);
            app.map_select().handle_key_down(SDLK_q);                                         // Q / X / the Leave button: leave
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
            ASSERT_TRUE(hall.until([&]() { return server.status("leave001").joined == 0; }, 4000));
        }
        {   // the room dies under a player who is on the room's screen (the server closed it): not a dead screen, the menu with the reason
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            ASSERT_TRUE(server.make_room("dead0001", 4));
            menu_join(app, "Dave", "dead0001");
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(600);
            ASSERT_TRUE(server.mgr.close_room("dead0001", server.now));
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::StartMenu; }, 8000));
            ASSERT_FALSE(app.start_menu().message().empty());
            ASSERT_TRUE(app.net() == nullptr && app.is_running());
        }
        {   // the quit dialog's Yes of a match with more than one other side left (here a three-player room): back at the menu
            Application app;
            Peer ann;
            Peer bob;
            Hall hall{&server, &app, {&ann, &bob}, nullptr, false};
            ASSERT_TRUE(app.init(menu_config(server.address(), temp.file("s.ini"))));
            ASSERT_TRUE(server.make_room("dialog01", 3));
            ASSERT_TRUE(ann.join("127.0.0.1", server.port(), "Ann", "dialog01"));
            ASSERT_TRUE(hall.until([&]() { return ann.net.phase() == net::NetGame::Phase::Room; }, 8000));
            ASSERT_TRUE(bob.join("127.0.0.1", server.port(), "Bob", "dialog01"));
            ASSERT_TRUE(hall.until([&]() { return bob.net.phase() == net::NetGame::Phase::Room; }, 8000));
            menu_join(app, "Dave", "dialog01");
            const bool started = hall.until([&]() { return app.state() == AppState::Playing; }, 20000);
            if (!started) {
                const server::RoomStatus rs = server.status("dialog01");
                std::cout << "\n    state " << static_cast<int>(app.state()) << " panel " << static_cast<int>(app.start_menu().panel()) << " message '" << app.start_menu().message()
                          << "' net phase " << (app.net() ? static_cast<int>(app.net()->phase()) : -1) << " room state " << static_cast<int>(rs.state) << " joined " << static_cast<int>(rs.joined) << " expected "
                          << static_cast<int>(rs.expected) << " ann " << static_cast<int>(ann.net.phase()) << " bob " << static_cast<int>(bob.net.phase()) << "\n";
            }
            ASSERT_TRUE(started);
            hall.step(6000);
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
            ASSERT_TRUE(app.is_running());
            ASSERT_EQ(app.state(), AppState::StartMenu);
            ASSERT_TRUE(app.net() == nullptr);
            ASSERT_EQ(app.window_title(), std::string("Ants"));
        }
        {   // a game without the menu: Leave is the end of the program, as it always was
            Application app;
            Hall hall{&server, &app, {}, nullptr, false};
            ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
            cfg.start_menu = false;
            cfg.net_role = ApplicationConfig::NetRole::Join;
            cfg.net_address = "127.0.0.1";
            cfg.net_port = server.port();
            ASSERT_TRUE(server.make_room("nomenu01", 4));
            cfg.net_room = "nomenu01";
            cfg.player_name = "Dave";
            ASSERT_TRUE(app.init(cfg));
            ASSERT_FALSE(app.start_menu_enabled());
            ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect && app.net() != nullptr && app.net()->phase() == net::NetGame::Phase::Room; }, 8000));
            hall.step(600);
            app.map_select().handle_key_down(SDLK_q);
            ASSERT_FALSE(app.is_running());
            ASSERT_FALSE(app.network_active());
        }
    } TEST_END();

    TEST_CASE("A7.1 The window's title: 'Ants' at the menu, 'Ants - room <code>' from the moment the player is in a room until the player is back at the menu (a --title is kept in it), and the program's again after a failed attempt") {
        TempDir temp;
        write_no_quick_help(temp.file("s.ini"));
        Server server;
        Application app;
        Hall hall{&server, &app, {}, nullptr, false};
        ApplicationConfig cfg = menu_config(server.address(), temp.file("s.ini"));
        cfg.title = "Ants test";
        ASSERT_TRUE(app.init(cfg));
        ASSERT_EQ(app.window_title(), std::string("Ants test"));
        ASSERT_TRUE(server.make_room("title001", 2));
        menu_join(app, "Dave", "title001");
        ASSERT_EQ(app.window_title(), std::string("Ants test"));                             // not yet: only the room's player has its code in the title
        ASSERT_TRUE(hall.until([&]() { return app.state() == AppState::MapSelect; }, 8000));
        ASSERT_EQ(app.window_title(), std::string("Ants test - room title001"));
        app.map_select().handle_key_down(SDLK_x);
        ASSERT_EQ(app.state(), AppState::StartMenu);
        ASSERT_EQ(app.window_title(), std::string("Ants test"));
        // a failed attempt leaves it alone
        app.start_menu().set_server(ServerAddress{"127.0.0.1", 1});
        click(app, MenuId::JoinWithCode);
        click(app, MenuId::Join);
        ASSERT_TRUE(hall.until([&]() { return failed_on(app, MenuPanel::Join); }, 8000));
        ASSERT_EQ(app.window_title(), std::string("Ants test"));
    } TEST_END();
}
