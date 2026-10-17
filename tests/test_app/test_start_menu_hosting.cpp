// Tests of the desktop start menu: the Teams row, the servers, room codes and names, the settings, the layout, the drawing, the command line, the name lookup and the rejoin offer (M4.5 - M12.4)
#include "start_menu_fixtures.hpp"

void run_start_menu_hosting_tests() {
    TEST_CASE("M4.5 Host: the Teams row of a room of three or four players goes round the room's choices, free for all first (Left, Right, Enter, click), and is remembered; the request carries it; a room of two has no such row; a choice that the room does not offer any more is free for all again; the room's panel says the teams") {
        const LocalTeams ffa{};
        const LocalTeams green_red{true, 0, 1};
        const LocalTeams green_blue{true, 0, 2};
        const LocalTeams green_black{true, 0, 3};
        const LocalTeams red_blue{true, 1, 2};
        // the words of a choice: the color words of the seats that play, the pair first
        ASSERT_EQ(StartMenu::host_teams_text(ffa, 4), std::string("Free for all"));
        ASSERT_EQ(StartMenu::host_teams_text(green_red, 4), std::string("Green + Red against Blue + Black"));
        ASSERT_EQ(StartMenu::host_teams_text(green_blue, 4), std::string("Green + Blue against Red + Black"));
        ASSERT_EQ(StartMenu::host_teams_text(green_black, 4), std::string("Green + Black against Red + Blue"));
        ASSERT_EQ(StartMenu::host_teams_text(green_red, 3), std::string("Green + Red against Blue"));
        ASSERT_EQ(StartMenu::host_teams_text(red_blue, 3), std::string("Red + Blue against Green"));
        ASSERT_EQ(room_teams_sentence(green_red, 4), std::string("Room teams: Green + Red against Blue + Black."));        // (they are the room's: its create block names them)
        ASSERT_EQ(room_teams_sentence(ffa, 4), std::string());
        ASSERT_EQ(room_teams_sentence(green_black, 3), std::string());                         // (a choice that the room does not offer is no teams)
        ASSERT_EQ(room_teams_sentence(green_red, 2), std::string());
        Rig r;
        r.to_panel(MenuId::HostOnline);
        ASSERT_TRUE(r.exists(MenuId::HostTeams) && r.element(MenuId::HostTeams).kind == MenuKind::Cycler);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Free for all"));            // free for all until the leader chooses
        for (int i = 0; i < 5; ++i) r.key(SDLK_DOWN);                                          // the map, the players, the three seats, the teams
        ASSERT_EQ(r.menu.selected(), MenuId::HostTeams);
        const char* const four[4] = {"Free for all", "Green + Red against Blue + Black", "Green + Blue against Red + Black", "Green + Black against Red + Blue"};
        for (int lap = 0; lap < 2; ++lap) {
            for (const char* word : four) {
                ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(word));
                r.key(SDLK_RIGHT);
            }
        }
        r.key(SDLK_LEFT);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[3]));                   // Left from the first goes to the last
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[0]));
        r.click(MenuId::HostTeams);
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[1]));
        ASSERT_TRUE(std::count(r.changes.begin(), r.changes.end(), MenuSetting::HostTeams) >= 11);
        // the request carries it
        r.click(MenuId::Host);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.teams == green_red && request.players == 4);
        r.menu.connection_failed("The server is busy.");
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[1]));                   // (a failed attempt keeps it)
        // a room of three offers its own choices: the pairs 0+1, 0+2 and 1+2 (the third seat plays alone); the choice that was made stays while the room offers it
        r.mouse_move(MenuId::HostPlayers);
        r.key(SDLK_LEFT);                                                                      // 4 -> 3: Green + Red against Blue is one of its choices
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Green + Red against Blue"));
        ASSERT_TRUE(r.menu.settings().host_teams == green_red);
        r.key(SDLK_RIGHT);                                                                     // 3 -> 4: and one of the four's
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(four[1]));
        r.click(MenuId::HostPlayers);                                                          // 4 -> 2: no teams, the choice is gone
        ASSERT_FALSE(r.exists(MenuId::HostTeams));
        ASSERT_FALSE(r.menu.settings().host_teams.set);
        r.click(MenuId::Host);
        ASSERT_TRUE(r.take().teams == ffa);
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                          // 2 -> 3
        const char* const three[4] = {"Free for all", "Green + Red against Blue", "Green + Blue against Red", "Red + Blue against Green"};
        for (const char* word : three) {
            ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(word));
            r.click(MenuId::HostTeams);
        }
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);                                                            // Red + Blue against Green
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string(three[3]));
        r.click(MenuId::HostPlayers);                                                          // 3 -> 4: the pair 1+2 is not a choice of a room of four
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Free for all"));
        ASSERT_FALSE(r.menu.settings().host_teams.set);
        // the room's panel says the teams where a message would stand (a message takes the place)
        Rig room;
        room.to_panel(MenuId::HostOnline);
        room.click(MenuId::HostTeams);
        room.click(MenuId::HostTeams);                                                         // Green + Blue against Red + Black
        room.menu.show_room("k7m2xq", 1, 4);
        const size_t with_teams = room.menu.elements().size();
        ASSERT_TRUE(has_text(room.menu.elements(), "Room teams: Green + Blue against Red + Black."));
        room.menu.show_room("k7m2xq", 1, 2);                                      // (a room of two has no teams)
        ASSERT_FALSE(has_text(room.menu.elements(), "Room teams:"));
        ASSERT_EQ(room.menu.elements().size() + 1, with_teams);
        Rig plain;
        plain.menu.show_room("k7m2xq", 1, 4);
        ASSERT_EQ(plain.menu.elements().size() + 1, with_teams);                               // free for all: the panel is what it was
        room.clipboard.writable = false;
        room.menu.show_room("k7m2xq", 1, 4);
        room.key(SDLK_c, KMOD_GUI);
        ASSERT_TRUE(has_text(room.menu.elements(), "Copy failed") && !has_text(room.menu.elements(), "Room teams:"));
        // the settings file: written under `host_teams` (ffa or the pair), read back, anything else is free for all
        {
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            MenuSettings s;
            s.host_teams = green_blue;
            s.write(store, MenuSetting::HostTeams);
            ASSERT_EQ(store.get_string("host_teams", "", 99), std::string("0+2"));
            MenuSettings back;
            back.load(store);
            ASSERT_TRUE(back.host_teams == green_blue);
            ASSERT_FALSE(back.teams.set);                                                      // (the single-player panel's choice is its own key)
            s.host_teams = ffa;
            s.write(store, MenuSetting::HostTeams);
            ASSERT_EQ(store.get_string("host_teams", "", 99), std::string("ffa"));
            back.load(store);
            ASSERT_FALSE(back.host_teams.set);
            store.set_string("host_teams", " 1+2 ");
            back.load(store);
            ASSERT_TRUE(back.host_teams == red_blue);
            for (const char* bad : {"1+1", "0+4", "01", "x", ""}) {
                store.set_string("host_teams", bad);
                back.load(store);
                ASSERT_FALSE(back.host_teams.set);
            }
            MenuSettings fresh;
            ASSERT_FALSE(fresh.host_teams.set);
        }
        // a choice that is stored for a room that does not offer it counts as free for all (the panel shows what the request will carry)
        Rig stale;
        MenuSettings stored;
        stored.name = "Player";
        stored.host_players = 4;
        stored.host_teams = red_blue;
        stale.menu.set_settings(stored);
        stale.to_panel(MenuId::HostOnline);
        ASSERT_EQ(stale.element(MenuId::HostTeams).value, std::string("Free for all"));
        stale.click(MenuId::Host);
        ASSERT_TRUE(stale.take().teams == ffa);
    } TEST_END();

    TEST_CASE("M4.6 Host: the Teams row's choice goes into the create block of the Host button (protocol 15: the room makes the teams for every start, the one when it fills up too): every map and every choice of a room of three or four, nothing for free for all, for a room of two and for a choice that the room does not offer; the block is one that a server reads (net::valid_create_block); the Host button's request carries the choice and the block made from it names it") {
        const char* files[] = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "GAUNTLET.LVL", "TREASURE.LVL", "ISLANDS.LVL"};
        size_t pairs = 0;
        for (size_t m = 0; m < kMenuMapCount; ++m) {
            for (int players = 2; players <= 4; ++players) {
                for (const sim::StartTeams& choice : sim::room_team_choices(static_cast<uint8_t>(players))) {
                    const net::CreateBlock block = make_create_block(menu_map(m), players, choice);
                    ASSERT_TRUE(net::valid_create_block(block));
                    ASSERT_EQ(block.map_name, std::string(files[m]));                                                   // the map's file: the key in capitals and ".LVL"
                    ASSERT_EQ(static_cast<int>(block.seats), players);
                    ASSERT_TRUE(block.teams() == choice);
                    ASSERT_FALSE(block.leader_starts());                                                                // (the flag is the one-link card's: the Host button does not set it)
                    pairs += choice.set ? 1u : 0u;
                }
            }
        }
        ASSERT_EQ(pairs, kMenuMapCount * 6);                                                                           // three choices for three players, three for four, on each of the six maps
        // a choice that the room does not offer is left out of the block: a room of two, a seat that the room does not have, the same team written from the other end
        const LocalTeams green_red{true, 0, 1};
        const std::vector<std::pair<int, LocalTeams>> not_offered = {
            {2, green_red}, {3, LocalTeams{true, 0, 3}}, {3, LocalTeams{true, 2, 3}}, {4, LocalTeams{true, 1, 2}}, {4, LocalTeams{true, 2, 3}}, {4, LocalTeams{true, 1, 0}},
            {4, LocalTeams{true, 0, 0}}, {0, green_red},                                                               // (0 players is a room of two)
        };
        for (const auto& c : not_offered) {
            const net::CreateBlock block = make_create_block(menu_map(4), c.first, c.second);
            ASSERT_TRUE(net::valid_create_block(block) && !block.teams().set && block.team_a == net::kNoTeam && block.team_b == net::kNoTeam);
        }
        ASSERT_TRUE(make_create_block(menu_map(4), 9, green_red).seats == 4 && make_create_block(menu_map(4), 9, green_red).teams() == green_red);      // (the players are clamped to 2 .. 4 first: nine is four)
        ASSERT_EQ(make_create_block(menu_map(4), 0, green_red).seats, 2);
        const net::CreateBlock one_link = make_create_block(menu_map(1), 3, LocalTeams{true, 1, 2}, true);                // the leader-starts flag, for the one-link card
        ASSERT_TRUE(one_link.leader_starts() && one_link.map_name == "SMALL.LVL" && one_link.seats == 3 && one_link.team_a == 1 && one_link.team_b == 2 && net::valid_create_block(one_link));
        // the Host button: the request carries the choice, and the block made from it names it
        Rig r;
        r.to_panel(MenuId::HostOnline);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);
        r.click(MenuId::HostTeams);                                                                                    // Green + Black against Red + Blue
        ASSERT_EQ(r.element(MenuId::HostTeams).value, std::string("Green + Black against Red + Blue"));
        r.click(MenuId::Host);
        const MenuRequest request = r.take();
        ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.teams == LocalTeams({true, 0, 3}) && request.players == 4);
        const net::CreateBlock block = make_create_block(menu_map(static_cast<size_t>(request.map)), request.players, request.teams);
        ASSERT_TRUE(block.map_name == "TREASURE.LVL" && block.seats == 4 && block.team_a == 0 && block.team_b == 3 && block.teams() == LocalTeams({true, 0, 3}));
        r.menu.connection_failed("The server is busy.");
        r.click(MenuId::HostPlayers);                                                                                  // 4 -> 2: the choice is gone from the request, so from the block
        r.click(MenuId::Host);
        const MenuRequest two = r.take();
        ASSERT_TRUE(two.players == 2 && !two.teams.set);
        ASSERT_FALSE(make_create_block(menu_map(static_cast<size_t>(two.map)), two.players, two.teams).teams().set);
    } TEST_END();

    TEST_CASE("M4.4 Host: the map is Treasure until a choice is stored (the list keeps the page's order, so Tiny, its first entry, is not the default) and a stored choice wins: a new menu, an empty store, a store with a word that is no map, a store with each of the six (any case); the panel shows it, the request carries it, the room's create block names it, a change is stored") {
        // the default is Treasure, the fifth of the six; the list is the page's (by size), so the first entry is Tiny
        ASSERT_EQ(kDefaultMenuMap, 4);
        ASSERT_EQ(std::string(menu_map(static_cast<size_t>(kDefaultMenuMap)).key), std::string("treasure"));
        ASSERT_EQ(std::string(menu_map(0).key), std::string("tiny"));
        ASSERT_EQ(std::string(menu_map(kMenuMapCount).key), std::string("treasure"));      // an index that is not in the list gives the default map too
        ASSERT_EQ(std::string(menu_map(99).key), std::string("treasure"));
        {
            StartMenu fresh;
            ASSERT_EQ(fresh.settings().host_map, 4);
            MenuSettings plain;
            ASSERT_EQ(plain.host_map, 4);
        }
        // nothing stored: the panel shows Treasure, the request, the code and the settings key follow it, a change is stored
        {
            Rig r;
            r.to_panel(MenuId::HostOnline);
            ASSERT_EQ(r.menu.selected(), MenuId::HostMap);
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));
            ASSERT_EQ(r.menu.settings().host_map, 4);
            for (const MenuId next : {MenuId::HostPlayers, MenuId::HostSeat1, MenuId::HostSeat2, MenuId::HostSeat3, MenuId::HostTeams, MenuId::HostName, MenuId::Host}) {
                r.key(SDLK_DOWN);                                                          // (the players, the three seats after the leader's, the teams, the name, Host)
                ASSERT_EQ(r.menu.selected(), next);
            }
            r.key(SDLK_RETURN);
            const MenuRequest request = r.take();
            ASSERT_TRUE(request.type == MenuRequest::Type::Host && request.map == 4 && request.players == 4);
            ASSERT_EQ(std::string(menu_map(static_cast<size_t>(request.map)).key), std::string("treasure"));
            const net::CreateBlock block = make_create_block(menu_map(static_cast<size_t>(request.map)), request.players);
            ASSERT_TRUE(block.map_name == "TREASURE.LVL" && block.seats == 4 && !block.teams().set);       // a block that names Treasure: the server makes the room on TREASURE.LVL
            r.menu.connection_failed("The server is busy.");
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));          // (and a failed attempt keeps it)
            TempDir temp;
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            r.menu.settings().write(store, MenuSetting::HostMap);
            ASSERT_EQ(store.get_string("host_map", "", 99), std::string("treasure"));
        }
        // the settings that come from a store: what is not one of the six words is the default, and a stored map wins, Tiny and Islands (the two ends of the list) included
        const auto shown_with = [](const std::string& ini, int& index) {
            ConfigStore store;
            store.parse(ini);
            MenuSettings settings;
            settings.load(store);
            index = settings.host_map;
            Rig r;
            r.menu.set_settings(settings);
            r.to_panel(MenuId::HostOnline);
            return r.element(MenuId::HostMap).value;
        };
        int index = -1;
        ASSERT_EQ(shown_with("", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=\n", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=bogus\n", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=tiny\n", index), std::string("Tiny"));
        ASSERT_TRUE(index == 0);
        ASSERT_EQ(shown_with("host_map=SMALL\n", index), std::string("Small"));
        ASSERT_TRUE(index == 1);
        ASSERT_EQ(shown_with("host_map=Medium\n", index), std::string("Medium"));
        ASSERT_TRUE(index == 2);
        ASSERT_EQ(shown_with("host_map=gauntlet\n", index), std::string("Gauntlet"));
        ASSERT_TRUE(index == 3);
        ASSERT_EQ(shown_with("host_map=treasure\n", index), std::string("Treasure"));
        ASSERT_TRUE(index == 4);
        ASSERT_EQ(shown_with("host_map=islands\n", index), std::string("Islands"));
        ASSERT_TRUE(index == 5);
    } TEST_END();

    TEST_CASE("M5.1 Servers: HOST, HOST:PORT, IPv4, [IPv6] and [IPv6]:PORT are read (the port 4001 when none is given); anything else is refused with a reason; what the screen shows reads back") {
        struct Good { const char* text; const char* host; uint16_t port; };
        const Good good[] = {
            {"beta.playants.org", "beta.playants.org", 4001}, {"beta.playants.org:4001", "beta.playants.org", 4001}, {"localhost:1", "localhost", 1},
            {"127.0.0.1:65535", "127.0.0.1", 65535}, {"  example.org:80  ", "example.org", 80}, {"my_host-1.local", "my_host-1.local", 4001},
            {"[::1]:4010", "::1", 4010}, {"[::1]", "::1", 4001}, {"::1", "::1", 4001}, {"fe80::1:2", "fe80::1:2", 4001}, {"a", "a", 4001},
        };
        for (const Good& g : good) {
            ServerAddress s;
            std::string why;
            ASSERT_TRUE(parse_server(g.text, s, why));
            ASSERT_TRUE(s.host == g.host && s.port == g.port);
            ServerAddress back;
            ASSERT_TRUE(parse_server(server_label(s), back, why) && back == s);            // the label is a server again
        }
        ASSERT_EQ(server_label(ServerAddress{}), std::string("beta.playants.org:4001"));
        ASSERT_EQ(server_label(ServerAddress{"::1", 4010}), std::string("[::1]:4010"));
        const char* bad[] = {"", "   ", "host:", "host:0", "host:65536", "host:99999999", "host:-1", "host:12a", "host name", "ho$t", ".host", "-host", "a..b", "[::1", "[::1]x",
                             "[::1]:", "[host]:1", "[]", "http://x", "a:b:c:zz", "host:4001:1", "tab\thost", "[1:2]", "[abc]", "host:4294967297", "host:99999999999999999999", "host:4294967296", "host:1 2"};
        for (const char* text : bad) {
            ServerAddress s;
            std::string why;
            ASSERT_FALSE(parse_server(text, s, why));
            ASSERT_FALSE(why.empty());
        }
        ServerAddress long_host;
        std::string why;
        ASSERT_TRUE(parse_server(std::string(253, 'a'), long_host, why));
        ASSERT_FALSE(parse_server(std::string(254, 'a'), long_host, why));
        std::string v6_45 = "1";                                                              // an IPv6 text is at most 45 characters (the longest, an IPv4-mapped one)
        while (v6_45.size() < 45) v6_45 += (v6_45.size() % 2 == 1) ? ":" : "1";
        ASSERT_EQ(v6_45.size(), size_t{45});
        ASSERT_TRUE(parse_server("[" + v6_45 + "]", long_host, why));
        ASSERT_FALSE(parse_server("[" + v6_45 + "1]", long_host, why));
        ASSERT_TRUE(parse_server("host:65535", long_host, why) && long_host.port == 65535);
        ASSERT_TRUE(parse_server("host:00080", long_host, why) && long_host.port == 80);
        ASSERT_TRUE(parse_server("host:000000000000000000001", long_host, why) && long_host.port == 1);          // leading zeros are no harm, and no wrap-round either way
        ASSERT_FALSE(parse_server("host name", long_host, why));
        ASSERT_TRUE(why.find("blank") != std::string::npos);                                 // a blank or a character that is no address character is named as such
        ASSERT_FALSE(parse_server("caf\xc3\xa9.org", long_host, why));
        ASSERT_TRUE(why.find("blank") != std::string::npos);
    } TEST_END();

    TEST_CASE("M5.2 Room codes of a hosted match (protocol 15): six characters of the page's alphabet (read from web/front/lobby_rules.js), no map, no seats and no teams in them, a valid room code every time, different every time; the screens show them in two groups of three and a code of another length as it is") {
        std::string page_alphabet;
        {
            std::ifstream page(std::string(ANTS_SOURCE_DIR) + "/web/front/lobby_rules.js");           // (by the source folder, not the working directory: it is read wherever the test is run from)
            ASSERT_TRUE(page.good());                                                      // a page that cannot be read, or in which the variable is renamed, FAILS the test (it used to skip it)
            std::stringstream text;
            text << page.rdbuf();
            const std::string html = text.str();
            const size_t at = html.find("var CODE_CHARS = '");
            ASSERT_TRUE(at != std::string::npos);
            page_alphabet = html.substr(at + 18, html.find('\'', at + 18) - (at + 18));
        }
        ASSERT_FALSE(page_alphabet.empty());
        ASSERT_EQ(page_alphabet, std::string(kRoomCodeAlphabet));                          // the same alphabet as the page's rules (web/front/lobby_rules.js, in the repository)
        ASSERT_EQ(std::string(kRoomCodeAlphabet).size(), static_cast<size_t>(31));
        ASSERT_EQ(kRoomCodeChars, static_cast<size_t>(6));
        for (const char look_alike : {'i', 'l', 'o', '0', '1'}) ASSERT_TRUE(std::string(kRoomCodeAlphabet).find(look_alike) == std::string::npos);      // (nothing to mistake when it is read out)
        uint32_t counter = 12345u;
        const std::function<uint32_t()> next = [&counter]() {                              // (a linear congruential generator: a counter that steps by a constant makes a code that repeats after 31 of them)
            counter = counter * 1664525u + 1013904223u;
            return counter >> 8;
        };
        std::vector<std::string> seen;
        for (int i = 0; i < 200; ++i) {
            const std::string code = make_room_code(next);
            ASSERT_EQ(code.size(), kRoomCodeChars);
            ASSERT_TRUE(net::valid_room_code(code));
            for (char c : code) ASSERT_TRUE(std::string(kRoomCodeAlphabet).find(c) != std::string::npos);
            ASSERT_TRUE(std::find(seen.begin(), seen.end(), code) == seen.end());
            seen.push_back(code);
        }
        // the map index of a key, any case
        ASSERT_TRUE(menu_map_index("ISLANDS") == 5 && menu_map_index("tiny") == 0 && menu_map_index("nope") == -1);
        // random bits that are all ones and all zeros: the first and the last character of the alphabet, never one beyond
        ASSERT_EQ(make_room_code([]() { return 0u; }), std::string("aaaaaa"));
        ASSERT_EQ(make_room_code([]() { return 0xFFFFFFFFu; }).back(), kRoomCodeAlphabet[0xFFFFFFFFu % 31u]);
        // what the screens show: two groups of three (read out, typed); the code that is copied is the plain one
        ASSERT_EQ(room_code_display("k7m2xq"), std::string("k7m 2xq"));
        ASSERT_EQ(room_code_display("RP-1"), std::string("RP-1"));                         // a code of another length (a room of the control interface) as it is
        ASSERT_EQ(room_code_display(""), std::string());
        ASSERT_EQ(room_code_display("abcdefghi"), std::string("abcdefghi"));
        ASSERT_EQ(room_code_display("abcdefgh"), std::string("abcdefgh"));                 // (and so are five, seven and eight: only six characters are two groups of three)
        ASSERT_EQ(room_code_display("abcde"), std::string("abcde"));
        ASSERT_EQ(room_code_display("abcdefg"), std::string("abcdefg"));
        ASSERT_EQ(room_code_display("abcdef"), std::string("abc def"));
    } TEST_END();

    TEST_CASE("M5.3 Names and codes: a name is cleaned (blanks at both ends, printable ASCII, 32 characters) and refused when empty or when it looks like a bot's; a code is cleaned and refused when empty, too long or not letters, digits, - and _; the case of a code is never touched") {
        std::string clean;
        std::string why;
        ASSERT_TRUE(check_player_name("  Ann  Lee  ", clean, why) && clean == "Ann  Lee");
        ASSERT_TRUE(check_player_name(std::string(50, 'a'), clean, why) && clean.size() == 32);
        ASSERT_TRUE(check_player_name(std::string(31, 'a') + " tail", clean, why) && clean == std::string(31, 'a'));      // cut at 32, and the blank that the cut leaves at its end goes
        ASSERT_FALSE(check_player_name("   ", clean, why));
        ASSERT_TRUE(why.find("name") != std::string::npos);
        ASSERT_TRUE(check_player_name("caf\xc3\xa9", clean, why) && clean == "caf");        // only printable ASCII is kept
        ASSERT_FALSE(check_player_name("", clean, why));
        ASSERT_FALSE(check_player_name("\x01\x02", clean, why));
        ASSERT_FALSE(check_player_name("Bot (x)", clean, why));
        ASSERT_TRUE(why.find("Bot (") != std::string::npos);
        ASSERT_TRUE(looks_like_bot_name("b o T(") && !looks_like_bot_name("bo") && !looks_like_bot_name("Bot") && !looks_like_bot_name("xbot("));
        ASSERT_TRUE(check_room_code("  ab-C_9 ", clean, why) && clean == "ab-C_9");
        ASSERT_TRUE(check_room_code("DEMO-SMALL-X7K2", clean, why) && clean == "DEMO-SMALL-X7K2");
        ASSERT_TRUE(check_room_code(std::string(32, 'z'), clean, why));
        ASSERT_FALSE(check_room_code(std::string(33, 'z'), clean, why));
        ASSERT_TRUE(why.find("at most 32") != std::string::npos);                          // the reason names the limit, not the alphabet
        ASSERT_FALSE(check_room_code("", clean, why));
        ASSERT_TRUE(why.find("code") != std::string::npos);
        ASSERT_TRUE(check_room_code("k7m 2xq", clean, why) && clean == "k7m2xq");       // the grouped form that the screens show comes back as the plain code
        ASSERT_TRUE(check_room_code(" k7m  2xq ", clean, why) && clean == "k7m2xq");
        ASSERT_FALSE(check_room_code("   ", clean, why));
        ASSERT_TRUE(why.find("code") != std::string::npos);
        ASSERT_FALSE(check_room_code("a.b", clean, why));
        ASSERT_TRUE(why.find("letters, digits") != std::string::npos);
        ASSERT_FALSE(check_room_code("\xc3\xa9", clean, why));
        ASSERT_EQ(StartMenu::kNameMax, net::kMaxNameChars);
        ASSERT_EQ(StartMenu::kCodeMax, net::kMaxRoomCodeChars);
    } TEST_END();

    TEST_CASE("M6.1 Settings: the name, the bots of the four seats, the host's map and players, and the server are written to the store under the remake's keys and read back; a bad value is the default; the server is read and never written by the menu") {
        TempDir temp;
        {
            ConfigStore store;
            store.set_location(temp.file("settings.ini"));
            MenuSettings s;
            s.name = "Maya";
            s.seats = {SeatChoice::Empty, SeatChoice::Easy, SeatChoice::Medium, SeatChoice::Hard};
            s.host_map = 4;
            s.host_players = 2;
            for (MenuSetting which : {MenuSetting::Name, MenuSetting::Bots, MenuSetting::HostMap, MenuSetting::HostPlayers}) s.write(store, which);
            ASSERT_EQ(store.get_string("name", "", 99), std::string("Maya"));
            ASSERT_EQ(store.get_string("bots", "", 99), std::string("off,easy,medium,hard"));
            ASSERT_EQ(store.get_string("host_map", "", 99), std::string("treasure"));
            ASSERT_EQ(store.get_int("host_players", 0, 0, 10), 2);
            ASSERT_FALSE(store.has("server"));                                             // the menu never writes the server
        }
        {   // a new store on the same file, as a restart reads it (and the original's own entries beside)
            std::ofstream out(temp.file("settings.ini"), std::ios::app);
            out << "server=play.example.org:4010\nSound Volume=30\n";
        }
        ConfigStore later;
        later.set_location(temp.file("settings.ini"));
        ASSERT_TRUE(later.load());
        MenuSettings back;
        back.load(later);
        ASSERT_EQ(back.name, std::string("Maya"));
        ASSERT_TRUE(back.seats[0] == SeatChoice::Empty && back.seats[1] == SeatChoice::Easy && back.seats[2] == SeatChoice::Medium && back.seats[3] == SeatChoice::Hard);
        ASSERT_TRUE(back.host_map == 4 && back.host_players == 2);
        ASSERT_EQ(back.server, std::string("play.example.org:4010"));
        ASSERT_EQ(later.get_int("Sound Volume", 100, 0, 100), 30);
        // anything that is not a valid value is the default
        ConfigStore junk;
        junk.parse("name=\nbots=sideways,EASY,,hard,extra\nhost_map=bogus\nhost_players=7\nserver=  \n");
        ConfigStore one_player;
        one_player.parse("host_players=1\n");
        MenuSettings o;
        o.load(one_player);
        ASSERT_EQ(o.host_players, 4);                                                      // a match for one player is no room
        MenuSettings d;
        d.load(junk);
        ASSERT_TRUE(d.name.empty() && d.server.empty());
        ASSERT_TRUE(d.seats[0] == SeatChoice::Empty && d.seats[1] == SeatChoice::Easy && d.seats[2] == SeatChoice::Empty && d.seats[3] == SeatChoice::Hard);
        ASSERT_TRUE(d.host_map == 4 && d.host_players == 4);                               // "bogus" is no map: the default, Treasure (index 4: M4.4)
        {   // what the menu is given is brought into range
            StartMenu menu;
            MenuSettings given;
            given.host_players = 9;
            given.host_map = 99;
            menu.set_settings(given);
            ASSERT_TRUE(menu.settings().host_players == 4 && menu.settings().host_map == 5);
            given.host_players = 1;
            given.host_map = -3;
            menu.set_settings(given);
            ASSERT_TRUE(menu.settings().host_players == 2 && menu.settings().host_map == 0);
        }
        ConfigStore none;
        MenuSettings n;
        n.load(none);
        ASSERT_TRUE(n.name.empty() && n.server.empty() && n.host_map == 4 && n.host_players == 4);     // nothing stored: Treasure
        for (SeatChoice c : n.seats) ASSERT_TRUE(c == SeatChoice::Empty);
        // a name that was stored too long or with odd characters is cleaned
        ConfigStore long_name;
        long_name.parse("name=" + std::string(60, 'n') + "\n");
        MenuSettings l;
        l.load(long_name);
        ASSERT_EQ(l.name.size(), static_cast<size_t>(32));
    } TEST_END();

    TEST_CASE("M6.2 Settings: the menu writes a change at once and only that one (typing a name writes `name`, a seat writes `bots`), and the values it was given come back unchanged") {
        TempDir temp;
        ConfigStore store;
        store.set_location(temp.file("settings.ini"));
        Rig r(0, "Zed");
        MenuSettings s = r.menu.settings();
        int name_writes = 0;
        r.menu.set_on_change([&](MenuSetting which) {
            name_writes += which == MenuSetting::Name ? 1 : 0;
            r.menu.settings().write(store, which);
        });
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_UP);
        r.type("Ruth");
        ASSERT_FALSE(store.has("name"));                                                   // not at every key: the file is written when the field is left (a keystroke used to write it)
        r.key(SDLK_BACKSPACE);
        ASSERT_FALSE(store.has("name"));
        r.key(SDLK_TAB);                                                                   // the field is left: now it is written
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));
        ASSERT_FALSE(store.has("bots") || store.has("host_map") || store.has("host_players"));
        r.key(SDLK_UP);                                                                    // back in the field (its text selected)
        r.key(SDLK_BACKSPACE);                                                             // ... cleared
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));                   // not written yet
        r.menu.show_main();                                                                // a panel changes: it is
        ASSERT_EQ(store.get_string("name", "", 99), std::string(""));
        ASSERT_EQ(name_writes, 2);                                                         // one write each time, never one per key (seven keys were pressed)
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);                                        // the name is empty: it is the first empty field
        r.type("Rut");
        r.menu.flush();                                                                    // what the application does when the program ends
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));
        r.menu.flush();
        ASSERT_EQ(name_writes, 3);                                                         // nothing new to write: no write
        r.menu.show_main();
        ASSERT_EQ(name_writes, 3);
        r.click(MenuId::Single);
        r.click(MenuId::Seat2);
        ASSERT_EQ(store.get_string("bots", "", 99), std::string("off,off,easy,off"));
        ASSERT_EQ(store.get_string("name", "", 99), std::string("Rut"));
        (void)s;
    } TEST_END();

    TEST_CASE("M7.1 Layout: on every panel, in the states that stress it (the longest names, codes, errors and servers), every element lies inside the frame of the 640 x 480 screen and no two elements overlap; the controls are never under the frame-rate plate") {
        for (const Variant& v : all_variants()) {
            Rig r;
            v.set_up(r);
            const std::vector<MenuElement> all = r.menu.elements();
            ASSERT_FALSE(all.empty());
            for (size_t i = 0; i < all.size(); ++i) {
                const ButtonRect& a = all[i].rect;
                const bool inside = a.x >= 16 && a.y >= 16 && a.x + a.w <= 624 - 1 && a.y + a.h <= 464 - 1 && a.w > 0 && a.h > 0;
                if (!inside) std::cout << "\n    [" << v.what << "] element " << i << " (" << a.x << "," << a.y << " " << a.w << "x" << a.h << ") is not inside the frame";
                ASSERT_TRUE(inside);
                ASSERT_TRUE(a.y + a.h <= FPS_OVERLAY_TOP);                                // the frame-rate plate and the version (bottom right) are never covered
                for (size_t j = i + 1; j < all.size(); ++j) {
                    const bool clash = intersects(a, all[j].rect);
                    if (clash) std::cout << "\n    [" << v.what << "] elements " << i << " and " << j << " overlap";
                    ASSERT_FALSE(clash);
                }
            }
        }
    } TEST_END();

    TEST_CASE("M7.2 Layout: the controls of every panel can be reached by the mouse at their middle and nowhere else is a control; the hit test agrees with the rectangles, and a pixel outside every rectangle is no control") {
        for (const Variant& v : all_variants()) {
            Rig r;
            v.set_up(r);
            const std::vector<MenuElement> all = r.menu.elements();
            for (const MenuElement& e : all) {
                if (e.id == MenuId::None) continue;
                ASSERT_EQ(r.menu.control_at(e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2), e.id);
                ASSERT_EQ(r.menu.control_at(e.rect.x, e.rect.y), e.id);
                ASSERT_EQ(r.menu.control_at(e.rect.x + e.rect.w - 1, e.rect.y + e.rect.h - 1), e.id);
                ASSERT_TRUE(r.menu.control_at(e.rect.x - 1, e.rect.y - 1) != e.id);
                ASSERT_TRUE(r.menu.control_at(e.rect.x + e.rect.w, e.rect.y + e.rect.h) != e.id);
            }
            for (const MenuElement& e : all) {                                              // the pointer over a button or a row selects it; over a field it does not (a click does)
                if (e.id == MenuId::None || e.kind == MenuKind::Field) continue;
                r.menu.on_mouse_move(e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2);
                ASSERT_EQ(r.menu.selected(), e.id);
            }
            ASSERT_EQ(r.menu.control_at(0, 0), MenuId::None);
            ASSERT_EQ(r.menu.control_at(639, 479), MenuId::None);
            ASSERT_EQ(r.menu.control_at(-5, 300), MenuId::None);
        }
    } TEST_END();

    TEST_CASE("M8.1 Drawing: every panel is drawn with the background of the original and every text of it lies inside the element that owns it; long texts are cut (the last line that fits ends with '...'), a name or code that is wider than its field is cut to it, nothing is drawn off the screen") {
        for (const int32_t scale : {1, 2}) {                                               // the estimate of the font, and a font twice as wide: the single lines are cut at their box
            for (const Variant& v : all_variants()) {
                Rig r;
                v.set_up(r);
                Recorder rec;
                rec.width_scale = scale;
                render_start_menu(rec, archive(), r.menu);
                ASSERT_FALSE(rec.sprites.empty());                                         // the sm_screen background is made of sprites
                const std::vector<MenuElement> all = r.menu.elements();
                for (const Recorder::Text& t : rec.texts) {
                    const int32_t w = rec.get_text_width(t.text, t.size);
                    const int32_t h = font_cell_height(t.size);
                    bool inside_some = false;
                    for (const MenuElement& e : all) {
                        if (t.x >= e.rect.x && t.y >= e.rect.y && t.x + w <= e.rect.x + e.rect.w && t.y + h <= e.rect.y + e.rect.h) inside_some = true;
                    }
                    if (!inside_some) std::cout << "\n    [" << v.what << ", width x" << scale << "] the text \"" << t.text << "\" at (" << t.x << "," << t.y << ") " << w << "x" << h << " is outside every element";
                    ASSERT_TRUE(inside_some);
                    ASSERT_TRUE(t.x >= 0 && t.y >= 0 && t.x + w <= StartMenu::kWidth && t.y + h <= StartMenu::kHeight);
                }
                for (const Recorder::Box& b : rec.fills) {
                    ASSERT_TRUE(b.rect.x >= 0 && b.rect.y >= 0 && b.rect.x + b.rect.w <= StartMenu::kWidth + 1 && b.rect.y + b.rect.h <= StartMenu::kHeight + 1);
                }
            }
        }
        // the long error is cut with "..." where the box ends
        Rig r;
        r.to_panel(MenuId::JoinWithCode);
        r.key(SDLK_RETURN);
        r.menu.take_request();
        r.menu.connection_failed(long_message());
        Recorder rec;
        render_start_menu(rec, archive(), r.menu);
        bool cut = false;
        for (const Recorder::Text& t : rec.texts) {
            if (t.size == FontSize::Px18 && t.text.size() > 3 && t.text.compare(t.text.size() - 3, 3, "...") == 0) cut = true;
        }
        ASSERT_TRUE(cut);
        // a short message is not cut
        Rig s;
        s.menu.show_main("The connection was lost.");
        Recorder rec2;
        render_start_menu(rec2, archive(), s.menu);
        bool whole = false;
        for (const Recorder::Text& t : rec2.texts) whole = whole || t.text == "The connection was lost.";
        ASSERT_TRUE(whole);
        // the 32 characters of the widest name show their END in the field that has the focus (the original's edit field) and never run out of it
        Rig w;
        w.to_panel(MenuId::JoinWithCode);
        w.key(SDLK_UP);
        w.type(std::string(40, 'W'));
        Recorder rec3;
        render_start_menu(rec3, archive(), w.menu);
        const MenuElement name = w.element(MenuId::Name);
        for (const Recorder::Text& t : rec3.texts) {
            if (t.text.find("WW") == std::string::npos) continue;
            ASSERT_TRUE(t.x >= name.rect.x && t.x + rec3.get_text_width(t.text, t.size) <= name.rect.x + name.rect.w);
        }
    } TEST_END();

    TEST_CASE("M8.2 Drawing: the code of a room is drawn in the largest size that fits its box (35 px for the page's codes, smaller for the longest ones), the buttons carry their words, the selected one is lit, the title is the panel's") {
        Rig r;
        r.menu.show_room("k7m2xq", 1, 4);
        Recorder a;
        render_start_menu(a, archive(), r.menu);
        bool big = false;
        for (const Recorder::Text& t : a.texts) {
            if (t.text == "k7m 2xq") big = t.size == FontSize::Px35;
        }
        ASSERT_TRUE(big);
        r.menu.show_room(std::string(32, 'W'), 1, 4);
        Recorder b;
        render_start_menu(b, archive(), r.menu);
        bool smaller = false;
        for (const Recorder::Text& t : b.texts) {
            if (t.text == std::string(32, 'W')) smaller = t.size != FontSize::Px35;
        }
        ASSERT_TRUE(smaller);
        Rig m;
        Recorder c;
        render_start_menu(c, archive(), m.menu);
        std::vector<std::string> words;
        for (const Recorder::Text& t : c.texts) words.push_back(t.text);
        for (const char* w : {"Welcome to Ants!", "Single player", "Join with a code", "Host an online match", "Quit"}) {
            ASSERT_TRUE(std::find(words.begin(), words.end(), std::string(w)) != words.end());
        }
        // the lit button has a lighter face than the others (the box fills before its label)
        const MenuElement lit = m.element(MenuId::Single);
        const MenuElement dull = m.element(MenuId::Quit);
        assets::ColorRGBA lit_face{}, dull_face{};
        for (const Recorder::Box& box : c.fills) {
            if (box.rect.x == lit.rect.x + 1 && box.rect.y == lit.rect.y + 1 && box.rect.w == lit.rect.w - 2 && box.rect.h == lit.rect.h - 2) lit_face = box.color;
            if (box.rect.x == dull.rect.x + 1 && box.rect.y == dull.rect.y + 1 && box.rect.w == dull.rect.w - 2 && box.rect.h == dull.rect.h - 2) dull_face = box.color;
        }
        ASSERT_TRUE(lit_face.g > dull_face.g);
    } TEST_END();

    TEST_CASE("M9.1 Command line: an option that starts a match, a room, a test run or a screenshot shows no menu (each of them alone); --start-menu forces it, also with --headless and --screenshot; the options that only set something up do not skip it") {
        const auto menu_for = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
#if defined(__EMSCRIPTEN__)
        ASSERT_FALSE(menu_for({}).start_menu);                                              // the web build never shows it
#else
        ASSERT_TRUE(menu_for({}).start_menu);                                               // a native game started with nothing on the command line
        // the options that choose a mode: each skips the menu (today's start exactly)
        const std::vector<std::vector<std::string>> skipping = {
            {"--map", "Original-Ants/Maps/TINY.LVL"}, {"--map-select"}, {"--play"}, {"--host"}, {"--host", "4002"}, {"--join", "127.0.0.1:4001"}, {"--join-url", "ws://x/ws"}, {"--room", "abc"},
            {"--token", "t"}, {"--seat", "1"}, {"--bot", "1:easy"}, {"--alone"}, {"--headless"}, {"--screenshot", "x.png"}, {"--player", "1"}, {"-pnum:1"}, {"-pnum=2"}, {"--select-ant", "3"},
            {"--select-base", "1"}, {"--open-options"}, {"--scorecard"}};
        for (const auto& args : skipping) {
            const ApplicationConfig c = menu_for(args);
            if (c.start_menu) std::cout << "\n    the menu was not skipped by " << args[0];
            ASSERT_FALSE(c.start_menu);
        }
        // the options that only set something up: the menu comes
        const std::vector<std::vector<std::string>> setting_up = {
            {"--name", "Bob"}, {"--game-mode", "187"}, {"--settings", "s.ini"}, {"--seed", "5"}, {"--fullscreen"}, {"--frames", "9"}, {"--show-grid"}, {"--team-name", "1", "Bob"}, {"-N1Bob"}, {"--title", "T"},
            {"--window-pos", "10,10"}, {"--window-size", "800,600"}, {"--grid", "2x2"}, {"--cell", "1"}, {"--display", "0"}, {"--audio-focus"}, {"--port", "4005"}, {"--loopback"},
            {"--lan-port", "5000"}, {"--no-lan"}, {"--start-when", "2"}, {"--server", "play.example.org:4001"}, {"--unknown-option"}};
        for (const auto& args : setting_up) {
            const ApplicationConfig c = menu_for(args);
            if (!c.start_menu) std::cout << "\n    the menu was skipped by " << args[0];
            ASSERT_TRUE(c.start_menu);
            ASSERT_TRUE(c.startup_error.empty());
        }
        // --start-menu forces it: also headless and with a screenshot (the tests and the screenshots), with a seat and with bots (they set the menu's rows)
        for (const auto& args : std::vector<std::vector<std::string>>{{"--start-menu"}, {"--headless", "--start-menu"}, {"--start-menu", "--headless", "--screenshot", "x.png"},
                                                                       {"--start-menu", "--player", "2"}, {"--start-menu", "--bot", "1:hard"}, {"--map-select", "--start-menu"}}) {
            const ApplicationConfig c = menu_for(args);
            ASSERT_TRUE(c.start_menu);
            ASSERT_TRUE(c.startup_error.empty());
        }
        // the menu comes first and chooses the match: an option that starts a match or a room at once cannot be combined with it
        for (const auto& args : std::vector<std::vector<std::string>>{{"--start-menu", "--map", "x.lvl"}, {"--start-menu", "--host"}, {"--start-menu", "--join", "h:1"},
                                                                       {"--join-url", "ws://x", "--start-menu"}, {"--start-menu", "--open-options"}, {"--start-menu", "--scorecard"}}) {
            const ApplicationConfig c = menu_for(args);
            ASSERT_FALSE(c.startup_error.empty());
            ASSERT_TRUE(c.startup_error.find("--start-menu") != std::string::npos);
        }
        // a hand-made config has no menu (the game as it was)
        ASSERT_FALSE(ApplicationConfig{}.start_menu);
#endif
    } TEST_END();

    TEST_CASE("M9.3 Command line: --play is the setup screen's own START at its first visit: no flag, no play_at_once; with it (either order of --map) the game keeps the setup screen's way, the map and the bots are the ones named, nothing else changes") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        ASSERT_FALSE(parse({}).play_at_once);
        ASSERT_FALSE(parse({"--map", tiny}).play_at_once);                                  // (the direct start of the tests and the screenshots: no flag)
        ASSERT_FALSE(parse({"--map", tiny}).start_in_map_select);                           // ... and it still starts the match at once, without the screens
        ApplicationConfig c = parse({"--play"});
        ASSERT_TRUE(c.play_at_once && c.start_in_map_select && c.default_map_path.empty() && c.startup_error.empty());
        for (const auto& args : std::vector<std::vector<std::string>>{{"--map", tiny, "--play"}, {"--play", "--map", tiny}}) {
            c = parse(args);
            ASSERT_TRUE(c.play_at_once);
            ASSERT_TRUE(c.start_in_map_select);                                             // (--map would have turned it off: the order of the two does not matter)
            ASSERT_EQ(c.default_map_path, tiny);
            ASSERT_TRUE(c.startup_error.empty());
        }
        c = parse({"--map", tiny, "--play", "--bot", "1:medium", "--bot", "2:medium", "--bot", "3:medium", "--name", "Bob"});
        ASSERT_TRUE(c.play_at_once && c.bots.size() == 3 && c.player_name == "Bob" && c.net_role == ApplicationConfig::NetRole::None);
#if !defined(__EMSCRIPTEN__)
        ASSERT_FALSE(c.start_menu);                                                          // --play is a mode: no start menu in front of it
#endif
    } TEST_END();

    TEST_CASE("M9.3b Command line: --alone is a game for one: off by default (a game of this machine without --bot plays all four colonies), a mode of its own (no start menu) that --play and --map leave as they are, and it is refused with --bot and with --start-menu, in either order, with the reason") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        ASSERT_FALSE(parse({}).alone);
        ASSERT_FALSE(parse({"--map", tiny, "--play"}).alone);                                 // (the original's single player: no option, no change)
        ApplicationConfig c = parse({"--map", tiny, "--play", "--alone", "--name", "Bob"});
        ASSERT_TRUE(c.alone && c.play_at_once && c.bots.empty() && c.startup_error.empty());
        ASSERT_TRUE(c.player_name == "Bob" && c.default_map_path == tiny && c.start_in_map_select);
        ASSERT_TRUE(c.net_role == ApplicationConfig::NetRole::None);
        c = parse({"--alone"});
        ASSERT_TRUE(c.alone && c.startup_error.empty());
#if !defined(__EMSCRIPTEN__)
        ASSERT_FALSE(c.start_menu);                                                            // --alone chooses the match's seats: no start menu in front of it (--map and --play do not need it: M9.1 has it alone)
        // the menu's Single player chooses who plays, so the two cannot be combined (the menu is not asked what the option has decided); the reason is told, in either order
        for (const auto& args : std::vector<std::vector<std::string>>{{"--start-menu", "--alone"}, {"--alone", "--start-menu"}, {"--headless", "--alone", "--start-menu"}}) {
            c = parse(args);
            ASSERT_EQ(c.startup_error, std::string("--start-menu cannot be combined with --alone: the menu's Single player chooses who plays."));
        }
        c = parse({"--start-menu", "--map", "x.lvl", "--alone"});
        ASSERT_TRUE(c.startup_error.find("--map") != std::string::npos);                        // (the first mistake is the one that is told)
#endif
        c = parse({"--alone", "--bot", "1:easy"});
        ASSERT_EQ(c.startup_error, std::string("--alone cannot be used with --bot: a game for one has no other player."));
        c = parse({"--bot", "1:easy", "--alone"});
        ASSERT_EQ(c.startup_error, std::string("--alone cannot be used with --bot: a game for one has no other player."));
        c = parse({"--bot", "9", "--alone"});
        ASSERT_TRUE(c.startup_error.find("--bot 9") == 0);                                     // (the first mistake is the one that is told)
    } TEST_END();

    TEST_CASE("M9.3c Command line: --game-mode highest-score|187 is the rules of the games this machine makes (off by default: the original's game); it only sets something up (the menu still comes), takes exactly the two words, and is refused with --replay (the file says which game was played), --join, --join-url and --room (the host of the room chooses), each with the reason") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        const std::string tiny = "Original-Ants/Maps/TINY.LVL";
        ApplicationConfig c = parse({});
        ASSERT_TRUE(c.game_mode == 0 && !c.game_mode_given && c.startup_error.empty());
        c = parse({"--game-mode", "highest-score"});
        ASSERT_TRUE(c.game_mode == 0 && c.game_mode_given && c.startup_error.empty());
        c = parse({"--game-mode", "187"});
        ASSERT_TRUE(c.game_mode == 1 && c.game_mode_given && c.startup_error.empty());
        c = parse({"--map", tiny, "--play", "--alone", "--game-mode", "187"});
        ASSERT_TRUE(c.game_mode == 1 && c.alone && c.play_at_once && c.startup_error.empty());
        c = parse({"--game-mode", "187", "--map", tiny, "--bot", "1:easy"});
        ASSERT_TRUE(c.game_mode == 1 && c.bots.size() == 1 && c.startup_error.empty());                 // (bots play it too: no rule against them)
#if !defined(__EMSCRIPTEN__)
        ASSERT_TRUE(parse({"--game-mode", "187"}).start_menu);                                           // (the option sets something up: a native game started with it shows the menu, whose games play by it)
#endif
        c = parse({"--host", "4002", "--game-mode", "187"});
        ASSERT_TRUE(c.game_mode == 1 && c.net_role == ApplicationConfig::NetRole::Host && c.startup_error.empty());         // (the room's host chooses: its Start carries the mode to every guest)
        // the two words only: no number, no other spelling, no empty word, no word with a blank, and a value that is missing
        for (const char* bad : {"0", "1", "2", "255", "", "187 ", " 187", "Highest-Score", "HIGHEST-SCORE", "kills", "highest_score", "highest-score ", "0x1", "1.0", "-1"}) {
            c = parse({"--game-mode", bad});
            if (c.startup_error.empty()) std::cout << "\n    '" << bad << "' was accepted";
            ASSERT_TRUE(c.startup_error.find("--game-mode") == 0 && c.startup_error.find("the game types are highest-score and 187") != std::string::npos);
            ASSERT_TRUE(c.game_mode == 0 && !c.game_mode_given);                                         // (a word that is not one changes nothing)
        }
        c = parse({"--game-mode"});
        ASSERT_EQ(c.startup_error, std::string("--game-mode needs highest-score or 187"));
        // the first mistake is the one that is told
        c = parse({"--bot", "9", "--game-mode", "2"});
        ASSERT_TRUE(c.startup_error.find("--bot 9") == 0);
        // the options of a game whose rules another machine chooses
        const std::string refusal_start = "--game-mode ";
        for (const auto& args : std::vector<std::vector<std::string>>{{"--replay", "x.antsrep", "--game-mode", "187"}, {"--game-mode", "187", "--replay", "x.antsrep"},
                                                                    {"--join", "127.0.0.1:4001", "--game-mode", "187"}, {"--game-mode", "187", "--join", "127.0.0.1:4001"},
                                                                    {"--join-url", "ws://x/ws", "--game-mode", "187"}, {"--game-mode", "187", "--join-url", "ws://x/ws"},
                                                                    {"--room", "abc", "--game-mode", "187"}, {"--game-mode", "187", "--room", "abc"},
                                                                    {"--room-map", "tiny", "--game-mode", "187"}, {"--game-mode", "187", "--room-map", "tiny"},       // (the options of the room that a server makes when a join finds none: the room's rules are the creator's plan, not this word)
                                                                    {"--room-seats", "3", "--game-mode", "187"}, {"--game-mode", "187", "--room-seats", "3"},
                                                                    {"--room-teams", "ffa", "--game-mode", "187"}, {"--game-mode", "187", "--room-teams", "0+1"},
                                                                    {"--room-leader-start", "--game-mode", "187"}, {"--game-mode", "187", "--room-leader-start"}}) {
            c = parse(args);
            if (c.startup_error.empty()) std::cout << "\n    no error for " << args[0] << " " << args[1];
            ASSERT_TRUE(c.startup_error.find("--game-mode ") == 0 && (c.startup_error.find("cannot be") != std::string::npos));
        }
        c = parse({"--replay", "x.antsrep", "--game-mode", "187"});
        ASSERT_EQ(c.startup_error, std::string("--game-mode cannot be combined with --replay: the file says which game was played."));
        // a mode of the original's game is no choice either: the same refusals (a replay or a room of another host is not played by this machine's word)
        c = parse({"--join", "127.0.0.1:4001", "--game-mode", "highest-score"});
        ASSERT_TRUE(c.startup_error.find("--game-mode ") == 0);
    } TEST_END();

    TEST_CASE("M9.2 Command line: --server is read as HOST[:PORT] (it overrides the settings key and the default); a bad one is a start error with the reason and the text; a missing value too; the rig of start_game.sh shows no menu") {
        const auto parse = [](std::vector<std::string> args) {
            std::vector<std::string> full = {"ants"};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<char*> arg_ptrs;
            for (std::string& a : full) arg_ptrs.push_back(a.data());
            arg_ptrs.push_back(nullptr);
            return Application::parse_arguments(static_cast<int>(full.size()), arg_ptrs.data());
        };
        ApplicationConfig ok = parse({"--server", "play.example.org:4010"});
        ASSERT_TRUE(ok.startup_error.empty());
        ASSERT_EQ(ok.server, std::string("play.example.org:4010"));
        ASSERT_EQ(parse({}).server, std::string(""));
        const std::vector<std::string> bad = {"bad host", "host:99999", "host:0", "[::1", "", "ho$t"};
        for (const std::string& text : bad) {
            const ApplicationConfig c = parse({"--server", text});
            if (c.startup_error.empty()) std::cout << "\n    '" << text << "' was accepted";
            ASSERT_FALSE(c.startup_error.empty());
            ASSERT_TRUE(c.startup_error.find("--server") != std::string::npos);
            if (!text.empty()) ASSERT_TRUE(c.startup_error.find(text) != std::string::npos);
        }
        const ApplicationConfig missing = parse({"--server"});
        ASSERT_TRUE(missing.startup_error.find("--server needs") != std::string::npos);
        // the command lines that start_game.sh prints (the four-window rig: window 0 hosts, the others join and ask for their seat; --single is a plain game)
        const std::vector<std::vector<std::string>> rig = {
            {"--name", "Antonio", "--title", "Ants - Green (Antonio)", "--grid", "2x2", "--cell", "1", "--audio-focus", "--no-lan", "--host", "4001", "--loopback"},
            {"--name", "Buzz", "--title", "Ants - Red (Buzz)", "--grid", "2x2", "--cell", "2", "--audio-focus", "--no-lan", "--join", "127.0.0.1:4001", "--seat", "1"},
            {"--name", "Clover", "--title", "Ants - Blue (Clover)", "--grid", "2x2", "--cell", "3", "--audio-focus", "--no-lan", "--join", "127.0.0.1:4001", "--seat", "2"},
            {"--name", "Dot", "--title", "Ants - Black (Dot)", "--grid", "2x2", "--cell", "0", "--audio-focus", "--no-lan", "--join", "127.0.0.1:4001", "--seat", "3"}};
        for (const auto& args : rig) ASSERT_FALSE(parse(args).start_menu);
    } TEST_END();

    TEST_CASE("M10.1 The name lookup: an address is answered at once and never goes to a resolver, a name goes to the resolver on a worker thread (Pending until it answers), a failure carries its reason, cancel abandons a lookup whose late answer then goes nowhere, and a new lookup is not confused by the old one") {
        const auto wait_for = [](HostLookup& lookup) {
            for (int i = 0; i < 4000 && lookup.poll() == HostLookup::State::Pending; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return lookup.state();
        };
        ASSERT_TRUE(HostLookup::is_numeric("127.0.0.1") && HostLookup::is_numeric("::1") && HostLookup::is_numeric("192.168.1.20") && HostLookup::is_numeric("fe80::1"));
        ASSERT_FALSE(HostLookup::is_numeric("localhost") || HostLookup::is_numeric("beta.playants.org") || HostLookup::is_numeric("example.org") || HostLookup::is_numeric("300.1.1.1") ||
                     HostLookup::is_numeric("not an address"));
        {   // an address: Done at once, the resolver is never asked
            const auto called = std::make_shared<std::atomic<int>>(0);
            const HostLookup::Resolver resolver = [called](const std::string&, std::string&, std::string&) {
                ++*called;
                return false;
            };
            HostLookup lookup;
            ASSERT_TRUE(lookup.state() == HostLookup::State::Idle);
            lookup.start("127.0.0.1", resolver);
            ASSERT_TRUE(lookup.state() == HostLookup::State::Done && lookup.poll() == HostLookup::State::Done && lookup.address() == "127.0.0.1");
            lookup.start("::1", resolver);
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Done && lookup.address() == "::1");
            ASSERT_EQ(called->load(), 0);
        }
        {   // a name: Pending until the worker has answered, then Done with the address
            const auto gate = std::make_shared<std::atomic<bool>>(false);
            HostLookup lookup;
            lookup.start("play.example.test", [gate](const std::string& host, std::string& address, std::string&) {
                while (!*gate) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                address = host == "play.example.test" ? "10.1.2.3" : "wrong host";
                return true;
            });
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Pending);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Pending);                          // the window would stay alive: poll returns at once
            *gate = true;
            ASSERT_TRUE(wait_for(lookup) == HostLookup::State::Done);
            ASSERT_EQ(lookup.address(), std::string("10.1.2.3"));
        }
        {   // a failure with its reason
            HostLookup lookup;
            lookup.start("nope.example.test", [](const std::string&, std::string&, std::string& error) {
                error = "no such name";
                return false;
            });
            ASSERT_TRUE(wait_for(lookup) == HostLookup::State::Failed);
            ASSERT_TRUE(lookup.error() == "no such name" && lookup.address().empty());
        }
        {   // cancel: Idle at once, the late answer goes nowhere (not even into the next lookup)
            const auto gate = std::make_shared<std::atomic<bool>>(false);
            const auto answers = std::make_shared<std::atomic<int>>(0);
            HostLookup lookup;
            lookup.start("slow.example.test", [gate, answers](const std::string&, std::string& address, std::string&) {
                while (!*gate) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++*answers;
                address = "1.2.3.4";
                return true;
            });
            lookup.cancel();
            ASSERT_TRUE(lookup.state() == HostLookup::State::Idle && lookup.address().empty());
            *gate = true;
            for (int i = 0; i < 2000 && answers->load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ASSERT_EQ(answers->load(), 1);                                                     // the abandoned worker did answer ...
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Idle && lookup.address().empty()); // ... into nothing
            // a lookup that replaces a running one: only the new answer counts
            const auto gate2 = std::make_shared<std::atomic<bool>>(false);
            lookup.start("first.example.test", [gate2](const std::string&, std::string& address, std::string&) {
                while (!*gate2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                address = "1.1.1.1";
                return true;
            });
            lookup.start("second.example.test", [](const std::string&, std::string& address, std::string&) {
                address = "2.2.2.2";
                return true;
            });
            ASSERT_TRUE(wait_for(lookup) == HostLookup::State::Done);
            *gate2 = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Done && lookup.address() == "2.2.2.2");
        }
        {   // the system's resolver finds this machine by its usual name, and what it finds is an address
            std::string address;
            std::string error;
            ASSERT_TRUE(HostLookup::system_resolve("localhost", address, error));
            ASSERT_TRUE(HostLookup::is_numeric(address));
            ASSERT_EQ(address, std::string("127.0.0.1"));                                      // a name that has both is answered with its IPv4 address: the game's server listens on IPv4
        }
    } TEST_END();


    // ---- the review fixes of the start menu -------------------------------------------------------------------------------------------------------------

    TEST_CASE("M10.2 The name lookup when its thread cannot be started: Failed at once with a reason of its own (start_failed), never an exception out of the menu; an address needs no thread; a launcher that works runs the job; the lookup polled while its worker answers is race free (run under TSan too)") {
        const HostLookup::Launcher none = [](std::function<void()>) { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); };
        const HostLookup::Resolver found = [](const std::string&, std::string& address, std::string&) {
            address = "10.0.0.1";
            return true;
        };
        {
            HostLookup lookup;
            lookup.start("nothread.example.test", found, none);
            ASSERT_TRUE(lookup.state() == HostLookup::State::Failed && lookup.start_failed());
            ASSERT_EQ(lookup.error(), std::string("could not start the lookup"));
            ASSERT_TRUE(lookup.address().empty());
            ASSERT_TRUE(lookup.poll() == HostLookup::State::Failed);                         // (and it stays so: nothing is pending)
            lookup.cancel();
            ASSERT_TRUE(lookup.state() == HostLookup::State::Idle && !lookup.start_failed());
            lookup.start("127.0.0.1", found, none);                                           // an address is never looked up: no thread is needed
            ASSERT_TRUE(lookup.state() == HostLookup::State::Done && !lookup.start_failed() && lookup.address() == "127.0.0.1");
            lookup.start("nothread.example.test", found, none);                               // the next start begins from nothing
            ASSERT_TRUE(lookup.start_failed());
            int launched = 0;
            const HostLookup::Launcher own_thread = [&launched](std::function<void()> job) {
                ++launched;
                std::thread(std::move(job)).detach();
            };
            lookup.start("thread.example.test", found, own_thread);
            ASSERT_FALSE(lookup.start_failed());
            ASSERT_EQ(launched, 1);
            HostLookup::State state = HostLookup::State::Pending;
            for (int i = 0; i < 5000 && (state = lookup.poll()) == HostLookup::State::Pending; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ASSERT_TRUE(state == HostLookup::State::Done && lookup.address() == "10.0.0.1");
        }
        {   // the main thread polls in a tight loop while the worker stores its answer: poll() takes the lock (without it ThreadSanitizer reports the race on the state)
            for (int round = 0; round < 300; ++round) {
                HostLookup lookup;
                lookup.start("race.example.test", found);
                HostLookup::State state = HostLookup::State::Pending;
                for (long spins = 0; spins < 50000000L && (state = lookup.poll()) == HostLookup::State::Pending; ++spins) {
                }
                ASSERT_TRUE(state == HostLookup::State::Done && lookup.address() == "10.0.0.1");
            }
        }
    } TEST_END();

    TEST_CASE("M11.1 Enter pressed twice in a hurry acts once: a new panel preselects the first INPUT (never Continue, Host or Cancel), and Enter and Space within StartMenu::kSettleMs of a panel's appearing do nothing, Esc does not quit from the first panel then; a held key never acts") {
        // Single player: Enter on the entry opens the panel; the second Enter must not start the game (Continue used to be selected) and must not change a row either
        {
            Rig r;
            r.key(SDLK_RETURN);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
            ASSERT_EQ(r.menu.selected(), MenuId::Seat1);                                   // the first row, not Continue
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Empty);                                  // (nor does it cycle the row it lands on)
            r.quick_key(SDLK_SPACE);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Empty);
            r.menu.update(0.29f);                                                          // 290 ms: still the same gesture
            r.quick_key(SDLK_KP_ENTER);
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Empty);
            r.menu.update(0.02f);                                                          // 310 ms: a person's own press
            r.quick_key(SDLK_RETURN);
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Easy);
            r.quick_key(SDLK_SPACE);
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Medium);
            r.key(SDLK_RETURN, 0, true);                                                   // a held key never acts, however old the panel is
            ASSERT_EQ(r.menu.seat(1), SeatChoice::Medium);
            r.key(SDLK_DOWN);
            r.key(SDLK_DOWN);
            r.key(SDLK_DOWN);
            ASSERT_EQ(r.menu.selected(), MenuId::Continue);                                // Continue is one Down away from the last row, and Enter on it still starts
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Single);
        }
        // Host an online match: the Host panel's selection is the map, so the second Enter makes no room
        {
            Rig r;
            r.key(SDLK_DOWN);
            r.key(SDLK_DOWN);
            r.key(SDLK_RETURN);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
            ASSERT_EQ(r.menu.selected(), MenuId::HostMap);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Host);
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Treasure"));          // the map is not cycled by it (the panel opens on the default map: Treasure)
            r.key(SDLK_RETURN);
            ASSERT_EQ(r.element(MenuId::HostMap).value, std::string("Islands"));           // a press of its own is
        }
        // Join: the Enter that joins is followed by a second one while "Connecting" shows: it must not cancel the attempt (Cancel used to be selected)
        {
            Rig r(0, "Dave");
            r.to_panel(MenuId::JoinWithCode);
            r.type("room-one");
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Join);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
            r.quick_key(SDLK_ESCAPE);                                                      // Esc cancels at once (it does not wait for the panel to settle)
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Cancel);
            r.menu.connection_cancelled();                                                 // and once more, to see that Enter and Space do nothing here however late they come
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Join);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
            r.key(SDLK_RETURN);                                                            // nothing is selected on this panel
            r.key(SDLK_SPACE);
            ASSERT_FALSE(r.menu.has_request());
        }
        // Esc, Esc: leaves the panel and does not quit the program with the second press
        {
            Rig r;
            r.to_panel(MenuId::Single);
            r.quick_key(SDLK_ESCAPE);                                                      // (the panel is new; Esc goes back from every panel but the first)
            ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
            r.quick_key(SDLK_ESCAPE);
            ASSERT_FALSE(r.menu.has_request());                                            // the same gesture: no Quit
            r.menu.update(0.31f);
            r.quick_key(SDLK_ESCAPE);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Quit);
        }
        // a failure and a room arrive on their own time: an Enter that comes right then is not a decision about the new panel
        {
            Rig r(0, "Dave");
            r.to_panel(MenuId::JoinWithCode);
            r.type("room-one");
            r.key(SDLK_RETURN);
            r.take();
            r.menu.connection_failed("There is no room with that code.");
            ASSERT_EQ(r.menu.selected(), MenuId::Join);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());                                            // (no new attempt)
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Join);
            r.menu.show_room("k7m2xq", 1, 2);
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::EnterRoom);
        }
        // the same on the Host panel's failure and on the first panel after a lost game
        {
            Rig r;
            r.to_panel(MenuId::HostOnline);
            for (int down = 0; down < 6; ++down) r.key(SDLK_DOWN);                         // (the players, the three seats after the leader's, the teams, the name: Enter in it hosts)
            ASSERT_EQ(r.menu.selected(), MenuId::HostName);
            r.key(SDLK_RETURN);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Host);
            r.menu.connection_failed("The server is busy.");
            r.quick_key(SDLK_RETURN);
            ASSERT_FALSE(r.menu.has_request());
            r.menu.show_main("The connection was lost.");
            r.quick_key(SDLK_ESCAPE);
            ASSERT_FALSE(r.menu.has_request());
            r.quick_key(SDLK_RETURN);
            ASSERT_EQ(r.menu.panel(), MenuPanel::Main);                                    // (the entry that was selected does not open either)
        }
        // the menu as it first appears: the Enter or Esc that ended the loading screen is not an answer to it
        {
            StartMenu m;
            m.set_settings(MenuSettings{});
            m.show_main();
            m.on_key(SDLK_ESCAPE, 0, false);
            ASSERT_FALSE(m.has_request());
            m.on_key(SDLK_RETURN, 0, false);
            ASSERT_EQ(m.panel(), MenuPanel::Main);
            m.update(StartMenu::kSettleMs / 1000.0f + 0.001f);
            ASSERT_TRUE(m.settled());
            m.on_key(SDLK_RETURN, 0, false);
            ASSERT_EQ(m.panel(), MenuPanel::Single);
        }
        // the mouse is not slowed by it (a click is two events, and its rest is the application's rule: test_start_menu_app)
        {
            Rig r;
            r.click(MenuId::Single);
            r.click(MenuId::Continue);
            ASSERT_TRUE(r.take().type == MenuRequest::Type::Single);
        }
    } TEST_END();

    TEST_CASE("M11.2 The Join panel's code field after a Back or a finished game: the text that is there is selected and typing replaces it (it used to append), like arriving by Tab; the first empty field has the focus") {
        Rig r(0, "Dave");
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Code);                                        // the name is there: the code is the first empty field
        ASSERT_FALSE(r.element(MenuId::Code).all_selected);                                // (empty: nothing to select)
        r.type("room-one");
        r.key(SDLK_ESCAPE);                                                                // Back ...
        r.click(MenuId::JoinWithCode);                                                     // ... and in again: the old code stands there, selected
        ASSERT_EQ(r.menu.selected(), MenuId::Code);
        ASSERT_EQ(r.menu.code(), std::string("room-one"));
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.type("room-two");
        ASSERT_EQ(r.menu.code(), std::string("room-two"));                                 // replaced, not "room-oneroom-two"
        // by the keyboard it is the same, and so is the way back after a game (show_main with a notice) and after a failed attempt
        r.key(SDLK_ESCAPE);
        r.key(SDLK_RETURN);                                                                // (the entry that was left is selected again)
        ASSERT_EQ(r.menu.panel(), MenuPanel::Join);
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.menu.show_main("The connection was lost.");
        r.click(MenuId::JoinWithCode);
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        r.type("room-three");
        ASSERT_EQ(r.menu.code(), std::string("room-three"));
        r.key(SDLK_UP);                                                                    // Tab and Up agree with the arrival: the name's text is selected too
        ASSERT_TRUE(r.element(MenuId::Name).all_selected);
        r.key(SDLK_TAB);
        ASSERT_TRUE(r.element(MenuId::Code).all_selected);
        // no name yet: the name is the first empty field, and the code that is there is not the focus
        Rig e(0, "");
        e.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(e.menu.selected(), MenuId::Name);
        // a failed attempt keeps what was typed and lights the button, as before
        Rig f(0, "Dave");
        f.to_panel(MenuId::JoinWithCode);
        f.type("room-x");
        f.key(SDLK_RETURN);
        f.take();
        f.menu.connection_failed("Nope.");
        ASSERT_EQ(f.menu.selected(), MenuId::Join);
        ASSERT_EQ(f.menu.code(), std::string("room-x"));
        f.key(SDLK_ESCAPE);
        f.click(MenuId::JoinWithCode);
        f.type("room-y");
        ASSERT_EQ(f.menu.code(), std::string("room-y"));
    } TEST_END();

    TEST_CASE("M11.3 A character that the fields cannot take is said, never dropped in silence: 'Jose-acute CJK' leaves 'Jos ' and the line 'Only letters A-Z, digits and simple punctuation.'; so does a paste; a line end of a paste and typing that is all good say nothing; the next good key takes the line away") {
        Rig r(0, "");
        r.to_panel(MenuId::JoinWithCode);
        ASSERT_EQ(r.menu.selected(), MenuId::Name);
        r.type("Jos\xC3\xA9 \xE6\x9D\x8E");                                                // "José 李" as SDL's text input delivers it (UTF-8)
        ASSERT_EQ(r.menu.name(), std::string("Jos "));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        ASSERT_TRUE(has_text(r.menu.elements(), "Only letters A-Z, digits and simple punctuation"));
        r.type("e");                                                                       // the next key takes the line away
        ASSERT_EQ(r.menu.name(), std::string("Jos e"));
        ASSERT_TRUE(r.menu.message().empty());
        r.type("abc");                                                                     // ASCII only: no line
        ASSERT_TRUE(r.menu.message().empty());
        r.type("\xC3\xA9");                                                                // only a refused character: nothing typed, the line shows
        ASSERT_EQ(r.menu.name(), std::string("Jos eabc"));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        r.key(SDLK_TAB);                                                                   // the code field takes the same
        r.key(SDLK_BACKSPACE);
        r.type("caf\xC3\xA9");
        ASSERT_EQ(r.menu.code(), std::string("caf"));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        r.key(SDLK_BACKSPACE);                                                             // Backspace takes the line away too
        ASSERT_TRUE(r.menu.message().empty());
        r.type("\x01");                                                                    // a control character is refused as well
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        // a paste: an accent in the clipboard says it, a line end or a tab does not
        r.clipboard.text = "k7m2\xC3\xA9xq";
        r.key(SDLK_a, KMOD_CTRL);
        r.key(SDLK_v, KMOD_CTRL);
        ASSERT_EQ(r.menu.code(), std::string("k7m2xq"));
        ASSERT_EQ(r.menu.message(), std::string(StartMenu::kRefusedCharsText));
        r.clipboard.text = "room-9\r\n";
        r.key(SDLK_a, KMOD_CTRL);
        r.key(SDLK_v, KMOD_CTRL);
        ASSERT_EQ(r.menu.code(), std::string("room-9"));
        ASSERT_TRUE(r.menu.message().empty());
        r.clipboard.text = "\t\n";
        r.key(SDLK_v, KMOD_CTRL);
        ASSERT_EQ(r.menu.code(), std::string("room-9"));
        ASSERT_TRUE(r.menu.message().empty());
        // the Host panel's name field too (its line stands where the server's note was)
        Rig h(0, "");
        h.to_panel(MenuId::HostOnline);
        for (int down = 0; down < 6; ++down) h.key(SDLK_DOWN);                             // (map, players, the three seats after the leader's, the teams, then the name)
        ASSERT_EQ(h.menu.selected(), MenuId::HostName);
        h.type("\xE6\x9D\x8E");
        ASSERT_EQ(h.menu.name(), std::string(""));
        ASSERT_TRUE(has_text(h.menu.elements(), "Only letters A-Z, digits and simple punctuation"));
        // the line fits its box in the drawing (it is a notice of one line at 18 px)
        Recorder rec;
        render_start_menu(rec, archive(), h.menu);
        bool whole = false;
        for (const Recorder::Text& t : rec.texts) whole = whole || t.text == StartMenu::kRefusedCharsText;
        ASSERT_TRUE(whole);
    } TEST_END();

    TEST_CASE("M11.4 What the panels say: the single-player panel says what its seats mean in both states (nobody: the original's game, the other colors stand still; a bot: no fog of war, empty seats have no ants), a bot choice that is stored for the player's own seat is not a bot; the copy failure is one line that is not cut") {
        Rig r;
        r.to_panel(MenuId::Single);
        ASSERT_TRUE(has_text(r.menu.elements(), "No bots: the original single-player game (the other colors stand still)."));
        ASSERT_FALSE(has_text(r.menu.elements(), "Bots play without fog of war"));
        r.click(MenuId::Seat2);
        ASSERT_TRUE(has_text(r.menu.elements(), "Bots play without fog of war. Empty seats have no ants."));
        ASSERT_FALSE(has_text(r.menu.elements(), "No bots:"));
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);
        r.click(MenuId::Seat2);
        ASSERT_TRUE(has_text(r.menu.elements(), "No bots:"));                              // back to nobody: back to the other line
        // a stored choice for the own seat: no row, no bot, the panel says "No bots" (the line follows the bots that will play, not the stored words)
        for (uint8_t own = 0; own < 4; ++own) {
            Rig s(own);
            MenuSettings st;
            st.name = "Player";
            st.seats = {SeatChoice::Empty, SeatChoice::Empty, SeatChoice::Empty, SeatChoice::Empty};
            st.seats[own] = SeatChoice::Hard;
            s.menu.set_settings(st);
            ASSERT_FALSE(s.menu.any_bot());
            ASSERT_TRUE(s.menu.bots().empty());
            s.to_panel(MenuId::Single);
            ASSERT_TRUE(has_text(s.menu.elements(), "No bots:") && !has_text(s.menu.elements(), "Bots play without"));
        }
        // both lines fit their box at the font's estimate (no '...'), and at a font twice as wide they are cut to their box and not beyond it
        for (const bool with_bot : {false, true}) {
            Rig d;
            d.to_panel(MenuId::Single);
            if (with_bot) d.click(MenuId::Seat1);
            Recorder rec;
            render_start_menu(rec, archive(), d.menu);
            const std::string want = with_bot ? StartMenu::kBotsLine : StartMenu::kNoBotsLine;
            std::string drawn;
            for (const Recorder::Text& t : rec.texts) {
                if (t.size == FontSize::Px18 && (want.find(t.text) != std::string::npos || t.text.find("...") != std::string::npos) && t.y >= 290 && t.y < 332) {
                    if (!drawn.empty()) drawn += " ";                                       // (a wrapped line ends where the blank was)
                    drawn += t.text;
                }
            }
            ASSERT_EQ(drawn, want);                                                        // all of it, however many lines it takes
        }
        // the copy failure: one line that says what to do, the code stands in full in its own box above it, and neither is cut
        Rig bad;
        bad.clipboard.writable = false;
        bad.menu.show_room("friends-on-gauntlet-night-2026", 1, 4);
        bad.click(MenuId::Copy);
        ASSERT_TRUE(has_text(bad.menu.elements(), "Copy failed. Write down the code above."));
        ASSERT_EQ(element_text(bad.menu.elements(), MenuKind::Code), std::string("friends-on-gauntlet-night-2026"));
        Recorder rc;
        render_start_menu(rc, archive(), bad.menu);
        bool line_whole = false;
        bool code_whole = false;
        for (const Recorder::Text& t : rc.texts) {
            line_whole = line_whole || t.text == "Copy failed. Write down the code above.";
            code_whole = code_whole || t.text == "friends-on-gauntlet-night-2026";
        }
        ASSERT_TRUE(line_whole && code_whole);
    } TEST_END();

    TEST_CASE("M11.5 A release belongs to a press that began on the panel that is up: a button that is held while the panel changes under it does nothing at its release, over whatever button of the new panel lies there") {
        Rig r;
        const MenuElement quit = r.element(MenuId::Quit);
        const int32_t x = quit.rect.x + quit.rect.w / 2;
        const int32_t y = quit.rect.y + quit.rect.h / 2;
        r.menu.on_mouse_move(x, y);
        ASSERT_TRUE(r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT));
        r.menu.show_room("k7m2xq", 1, 2);                                    // the room appears while the button is down ...
        MenuElement copy;
        ASSERT_TRUE(r.menu.find_element(MenuId::Copy, copy) && copy.rect.contains(x, y));  // ... and its Copy button lies where Quit was
        ASSERT_FALSE(r.element(MenuId::Copy).pressed);
        r.menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        ASSERT_TRUE(r.clipboard.writes.empty());                                           // nothing was copied
        ASSERT_FALSE(r.menu.has_request());                                                // nothing was asked for
        r.click(MenuId::Copy);                                                             // a click that begins on the panel does
        ASSERT_EQ(r.clipboard.writes.size(), static_cast<size_t>(1));
        // the same with a button that the next panel has too, at the same place: the Join panel's Back is held while a room that was left puts the player on the Host panel, whose Back lies under it
        // (the Host panel of a room of three players: the rows above Host and Back are as many as the room has, and only this one has its Back under the Join panel's)
        Rig j(0, "Dave");
        j.to_panel(MenuId::HostOnline);
        j.click(MenuId::HostPlayers);
        j.click(MenuId::HostPlayers);
        ASSERT_EQ(j.menu.settings().host_players, 3);
        j.to_panel(MenuId::JoinWithCode);
        const MenuElement back = j.element(MenuId::Back);
        const int32_t bx = back.rect.x + back.rect.w / 2;
        const int32_t by = back.rect.y + back.rect.h - 4;
        j.menu.on_mouse_move(bx, by);
        ASSERT_TRUE(j.menu.on_mouse_down(bx, by, SDL_BUTTON_LEFT));
        j.menu.room_left("The connection to the server was lost.");                       // the Host panel appears under the held button
        MenuElement host_back;
        ASSERT_TRUE(j.menu.find_element(MenuId::Back, host_back) && host_back.rect.contains(bx, by));
        j.menu.on_mouse_up(bx, by, SDL_BUTTON_LEFT);
        ASSERT_EQ(j.menu.panel(), MenuPanel::Host);                                        // the release did not press the other panel's Back
        ASSERT_EQ(j.menu.message(), std::string("The connection to the server was lost."));
        j.click(MenuId::Back);                                                             // a click that begins on this panel does
        ASSERT_EQ(j.menu.panel(), MenuPanel::Main);
    } TEST_END();


    // ---- the first panel's "Rejoin your match (CODE)" (a key of a running match is in the application's store, rejoin_store.hpp) ----

    TEST_CASE("M12.1 Rejoin offer: with none the first panel is exactly what it was; with one there is one more button, \"Rejoin your match (CODE)\", above Single player in the gap under the title, and nothing else of the panel moves (the default selection stays Single player: a panel never preselects a button that acts)") {
        Rig plain;
        const std::vector<MenuElement> before = plain.menu.elements();
        ASSERT_FALSE(plain.menu.rejoin().has_value());
        ASSERT_FALSE(plain.exists(MenuId::Rejoin));
        Rig r;
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{"play.example.org", 4001}});
        r.menu.show_main();
        const std::vector<MenuElement> after = r.menu.elements();
        ASSERT_EQ(after.size(), before.size() + 1);
        size_t found = 0;
        for (const MenuElement& e : after) {
            if (e.id == MenuId::Rejoin) {
                ++found;
                ASSERT_EQ(e.text, std::string("Rejoin your match (MEET-1)"));
                ASSERT_TRUE(e.kind == MenuKind::Button && e.font == FontSize::Px18 && e.centered);
                continue;
            }
            bool same = false;                                                          // every other element is where it was, with the same words
            for (const MenuElement& b : before) {
                if (b.id == e.id && b.kind == e.kind && b.text == e.text && b.rect.x == e.rect.x && b.rect.y == e.rect.y && b.rect.w == e.rect.w && b.rect.h == e.rect.h) same = true;
            }
            ASSERT_TRUE(same);
        }
        ASSERT_EQ(found, size_t{1});
        const MenuElement rejoin = r.element(MenuId::Rejoin);
        const MenuElement single = r.element(MenuId::Single);
        MenuElement title;
        for (const MenuElement& e : after) {
            if (e.kind == MenuKind::Title) title = e;
        }
        ASSERT_TRUE(rejoin.rect.y >= title.rect.y + title.rect.h && rejoin.rect.y + rejoin.rect.h <= single.rect.y);       // between the title and Single player
        ASSERT_TRUE(rejoin.rect.x + rejoin.rect.w / 2 == single.rect.x + single.rect.w / 2);                                 // centred over it
        const std::vector<MenuId> expected = {MenuId::Rejoin, MenuId::Single, MenuId::JoinWithCode, MenuId::HostOnline, MenuId::Quit};
        ASSERT_TRUE(control_ids(r.menu) == expected);
        ASSERT_EQ(r.menu.selected(), MenuId::Single);
        // the 16:9 menu moves the button with the group that the others are in (+160, +30)
        r.menu.set_wide_layout(true);
        const MenuElement wide = r.element(MenuId::Rejoin);
        ASSERT_TRUE(wide.rect.x == rejoin.rect.x + StartMenu::kWideDx && wide.rect.y == rejoin.rect.y + StartMenu::kWideMiddleDy);
        ASSERT_TRUE(r.element(MenuId::Single).rect.y == single.rect.y + StartMenu::kWideMiddleDy);
        // taking the offer away gives the panel back
        r.menu.set_wide_layout(false);
        r.menu.set_rejoin(std::nullopt);
        ASSERT_FALSE(r.exists(MenuId::Rejoin));
        ASSERT_EQ(r.menu.elements().size(), before.size());
    } TEST_END();

    TEST_CASE("M12.2 Rejoin offer: Up from Single player selects it (Down from Quit wraps to it), Enter or Space on it asks the application to rejoin that room, seat and server (the panel's name, cleaned; \"Player\" when it is not a name that the server would take) and the panel says \"Rejoining your match in room CODE...\"; the settling rule holds for it like for every button") {
        Rig r(0, "Dave");
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{"play.example.org", 4444}});
        r.menu.show_main();
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        r.key(SDLK_UP);
        ASSERT_EQ(r.menu.selected(), MenuId::Quit);                                      // (the wrap)
        r.key(SDLK_DOWN);
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        r.menu.show_main();                                                              // a fresh panel (the first panel keeps the selection that it had): Enter in a hurry does nothing
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        r.quick_key(SDLK_RETURN);
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        r.key(SDLK_RETURN);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        ASSERT_TRUE(r.menu.has_request());
        const MenuRequest q = r.take();
        ASSERT_TRUE(q.type == MenuRequest::Type::Rejoin && q.room == "MEET-1" && q.seat == 2 && q.name == "Dave");
        ASSERT_TRUE(q.server.host == "play.example.org" && q.server.port == 4444);
        ASSERT_TRUE(has_text(r.menu.elements(), "Rejoining your match in room MEET-1..."));
        ASSERT_EQ(r.menu.connect_origin(), MenuPanel::Main);
        // Space acts like Enter on a button; a name that the server would rename (a bot's) is not sent
        Rig s(0, "Bot (Hard)");
        s.menu.set_rejoin(RejoinOffer{"R2", 0, ServerAddress{}});
        s.menu.show_main();
        s.key(SDLK_UP);
        s.key(SDLK_SPACE);
        const MenuRequest q2 = s.take();
        ASSERT_TRUE(q2.type == MenuRequest::Type::Rejoin && q2.name == "Player" && q2.room == "R2" && q2.seat == 0);
        // nothing offered, nothing to ask: Up from Single player is Quit, and the other entries ask nothing
        Rig n;
        n.key(SDLK_UP);
        ASSERT_EQ(n.menu.selected(), MenuId::Quit);
    } TEST_END();

    TEST_CASE("M12.3 Rejoin offer: a click is a press and a release on the button (the click sound at the press); leaving the button before the release cancels it; the clicks of the other entries are what they were") {
        Rig r;
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 1, ServerAddress{}});
        r.menu.show_main();
        const MenuElement b = r.element(MenuId::Rejoin);
        const int32_t x = b.rect.x + b.rect.w / 2;
        const int32_t y = b.rect.y + b.rect.h / 2;
        r.menu.on_mouse_move(x, y);
        ASSERT_EQ(r.menu.selected(), MenuId::Rejoin);
        ASSERT_TRUE(r.menu.on_mouse_down(x, y, SDL_BUTTON_LEFT));
        ASSERT_TRUE(r.element(MenuId::Rejoin).pressed);
        ASSERT_EQ(r.sounds.count(sim::SoundID::ButtonClick), 1);
        ASSERT_FALSE(r.menu.has_request());                                              // the action is at the release
        const MenuElement single = r.element(MenuId::Single);
        r.menu.on_mouse_move(single.rect.x + 5, single.rect.y + 5);                      // the pointer leaves the button: it is no longer pressed
        r.menu.on_mouse_up(single.rect.x + 5, single.rect.y + 5, SDL_BUTTON_LEFT);
        ASSERT_FALSE(r.menu.has_request());
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        r.click(MenuId::Rejoin);
        ASSERT_EQ(r.menu.panel(), MenuPanel::Connecting);
        const MenuRequest q = r.take();
        ASSERT_TRUE(q.type == MenuRequest::Type::Rejoin && q.room == "MEET-1" && q.seat == 1);
        r.menu.connection_cancelled();
        r.click(MenuId::Single);                                                         // the others work as they did
        ASSERT_EQ(r.menu.panel(), MenuPanel::Single);
    } TEST_END();

    TEST_CASE("M12.4 Rejoin offer: a rejoin that fails comes back to the FIRST panel with the reason (not to Join or Host), a cancel comes back to it with nothing said, and Esc on \"Rejoining...\" cancels; the offer is whatever the owner gives next") {
        Rig r;
        r.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        r.menu.show_main();
        r.key(SDLK_UP);
        r.key(SDLK_RETURN);
        r.take();
        r.menu.connection_failed("Sorry, you have been dropped from the game.");
        ASSERT_EQ(r.menu.panel(), MenuPanel::Main);
        ASSERT_EQ(r.menu.message(), std::string("Sorry, you have been dropped from the game."));
        ASSERT_TRUE(has_text(r.menu.elements(), "Sorry, you have been dropped from the game."));
        ASSERT_TRUE(r.exists(MenuId::Rejoin));                                           // (the owner takes the offer away when the key was let go of)
        r.menu.set_rejoin(std::nullopt);
        ASSERT_FALSE(r.exists(MenuId::Rejoin));
        // Cancel and Esc
        Rig c;
        c.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        c.menu.show_main();
        c.key(SDLK_UP);
        c.key(SDLK_RETURN);
        c.take();
        ASSERT_EQ(c.menu.panel(), MenuPanel::Connecting);
        c.key(SDLK_ESCAPE);
        ASSERT_EQ(c.take().type, MenuRequest::Type::Cancel);
        c.menu.connection_cancelled();
        ASSERT_EQ(c.menu.panel(), MenuPanel::Main);
        ASSERT_TRUE(c.menu.message().empty());
        ASSERT_TRUE(c.exists(MenuId::Rejoin) && c.exists(MenuId::Single));
        // a Join after it still fails back to Join (the origin is the panel that asked)
        c.to_panel(MenuId::JoinWithCode);
        c.type("Dave");
        c.key(SDLK_DOWN);
        c.type("ROOM-9");
        c.key(SDLK_RETURN);
        c.take();
        c.menu.connection_failed("The room is full.");
        ASSERT_EQ(c.menu.panel(), MenuPanel::Join);
        // the selection that stood on the button does not stay on a button that is gone
        Rig d;
        d.menu.set_rejoin(RejoinOffer{"MEET-1", 2, ServerAddress{}});
        d.menu.show_main();
        d.key(SDLK_UP);
        ASSERT_EQ(d.menu.selected(), MenuId::Rejoin);
        d.menu.set_rejoin(std::nullopt);
        ASSERT_EQ(d.menu.selected(), MenuId::Single);
        d.menu.show_main();
        ASSERT_EQ(d.menu.selected(), MenuId::Single);
    } TEST_END();
}
