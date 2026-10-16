// Tests of the network in the application: the end of a match of a server's room as the page is told it (Application::set_on_match_over, match_over_json): the web build's page offers the
// rematch of a lobby room with it (web/front/rematch.js, docs/SERVER.md "Lobby rooms")
#include "network_app_fixtures.hpp"

#include <fstream>
#include <map>

void run_rematch_app_tests() {
    const auto join_config = [](const Server& server, const std::string& room, const std::string& name) {
        ApplicationConfig cfg = headless_config();
        cfg.net_role = ApplicationConfig::NetRole::Join;
        cfg.net_address = "127.0.0.1";
        cfg.net_port = server.port();
        cfg.net_room = room;
        cfg.player_name = name;
        return cfg;
    };

    TEST_CASE("N5.98 The Match Of A Server's Room Is Over: Each Game Tells Its Page Once (set_on_match_over), While The Key Of Its Seat Is Still Kept (The Page Reads It Then; It Is Let Go Of Right After): The Room, The Seat, The Map, The Mode, The Names And The Rows As The Server Orders Them (The Quitter's Last); A Game Of One Machine Tells Nothing") {
        Server server;
        server::RoomSpec spec;
        spec.code = "RMT-APP";
        spec.map = "TINY.LVL";
        spec.players = 2;
        spec.early_start = true;
        spec.has_seed = true;
        spec.seed = 4242;
        spec.reconnect = true;                                                                              // (the room holds the seats: each game is given the key of its seat at the Start)
        ASSERT_TRUE(server.listener != nullptr && server.mgr.create_room(spec, server.now).ok);
        Application leader;
        Application guest;
        ASSERT_TRUE(leader.init(join_config(server, "RMT-APP", "Leader")));
        ASSERT_TRUE(guest.init(join_config(server, "RMT-APP", "Guest")));
        std::vector<std::string> told[2];
        size_t kept[2] = {99, 99};
        Application* apps[2] = {&leader, &guest};
        for (size_t i = 0; i < 2; ++i) {
            Application* a = apps[i];
            a->set_on_match_over([&told, &kept, a, i](const std::string& json) {
                told[i].push_back(json);
                kept[i] = a->rejoin_store()->entries().size();
            });
        }
        Hall hall{server, &leader, {}};
        hall.second = &guest;
        ASSERT_TRUE(hall.until([&]() { return leader.state() == AppState::Playing && guest.state() == AppState::Playing; }, 15000));
        ASSERT_TRUE(leader.net()->my_seat() == 0 && guest.net()->my_seat() == 1);
        ASSERT_TRUE(leader.rejoin_store()->entries().size() == 1u && guest.rejoin_store()->entries().size() == 1u);      // (the keys of the match)
        hall.step(kDialogMs + 1500);
        ASSERT_TRUE(told[0].empty() && told[1].empty());
        // the guest quits: with one other side left the quit ends the match by its rules, on both machines
        SDL_KeyboardEvent q_ev{};
        q_ev.type = SDL_KEYDOWN;
        q_ev.keysym.sym = SDLK_q;
        q_ev.keysym.mod = KMOD_LCTRL;
        guest.handle_key_down(q_ev);
        ASSERT_TRUE(guest.hud().is_quit_dialog_open());
        SDL_KeyboardEvent y_ev{};
        y_ev.type = SDL_KEYDOWN;
        y_ev.keysym.sym = SDLK_y;
        guest.handle_key_down(y_ev);
        ASSERT_TRUE(hall.until([&]() { return leader.sim().is_match_over() && guest.sim().is_match_over(); }, 8000));
        hall.step(500);
        ASSERT_TRUE(told[0].size() == 1u && told[1].size() == 1u);
        ASSERT_TRUE(kept[0] == 1u && kept[1] == 1u);                                                       // the key was still kept when the page was told
        ASSERT_TRUE(leader.rejoin_store()->entries().empty() && guest.rejoin_store()->entries().empty());  // ... and is let go of right after (the room is over)
        const std::vector<sim::ResultRow> rows = guest.sim().get_world_state().match_result.rows(sim::PLAYER_NEUTRAL);
        ASSERT_TRUE(rows.size() == 2u && rows[0].first == 0 && !rows[0].has_second() && rows[1].first == 1);
        const std::string tail = std::string(",\"map\":\"TINY.LVL\",\"mode\":0,\"names\":[\"Leader\",\"Guest\",\"\",\"\"],\"rows\":[{\"seats\":[0],\"score\":") + std::to_string(rows[0].score) +
                                 ",\"won\":true},{\"seats\":[1],\"score\":" + std::to_string(rows[1].score) + ",\"won\":false}]}";
        ASSERT_EQ(told[0][0], "{\"room\":\"RMT-APP\",\"seat\":0" + tail);
        ASSERT_EQ(told[1][0], "{\"room\":\"RMT-APP\",\"seat\":1" + tail);
        hall.step(2000);
        ASSERT_TRUE(told[0].size() == 1u && told[1].size() == 1u);                                          // once
        leader.quit();
        guest.quit();
        // a game of one machine (no room, no key): its match ends and nothing is told
        Application solo;
        std::vector<std::string> told_solo;
        ASSERT_TRUE(solo.init(headless_config()));
        solo.set_on_match_over([&told_solo](const std::string& json) { told_solo.push_back(json); });
        ASSERT_TRUE(solo.start_game("Original-Ants/Maps/TINY.LVL"));
        solo.sim().set_match_time_remaining_ms(1000);
        for (int i = 0; i < kDialogSteps + 400 && !solo.sim().is_match_over(); ++i) solo.update_simulation(0.05f);
        ASSERT_TRUE(solo.sim().is_match_over() && told_solo.empty());
        solo.quit();
    } TEST_END();

    TEST_CASE("N5.99 What The Page Is Told, Byte For Byte: tests/data/match_over.txt Holds The Text Of match_over_json For Two Players, An Alliance In 187 (Names With A Quote And A Backslash, A Map Of The Players) And A Tie With A Bot (A Seat Out Of The Roster Has No Name); The Page's Own Code Reads Every Line (tests/scripts/web_rematch_check.js)") {
        std::map<std::string, std::string> golden;
        std::ifstream in(std::string(TEST_DATA_DIR) + "/match_over.txt");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t blank = line.find(' ');
            if (blank != std::string::npos) golden[line.substr(0, blank)] = line.substr(blank + 1);
        }
        ASSERT_EQ(golden.size(), size_t{3});
        const auto start_of = [](const std::string& map, uint8_t mode, uint8_t roster, const std::array<std::string, 4>& names) {
            net::StartMsg s;
            s.map_name = map;
            s.mode = mode;
            s.roster = roster;
            s.names = names;
            return s;
        };
        {   // two players: the higher score won
            sim::MatchResult r;
            r.present_mask = 0x3;
            r.stats[0].score = 2040;
            r.stats[1].score = 420;
            ASSERT_EQ(Application::match_over_json("k7m2xq", 1, start_of("TREASURE.LVL", 0, 0x3, {"Maple", "Sam", "", ""}), r), golden["two-players"]);
        }
        {   // 187, Maple and Juniper allied (one row, the scores added up, the lower seat first): the alliance won; the names are escaped as JSON
            sim::MatchResult r;
            r.present_mask = 0xF;
            r.ally = {2, sim::ALLIANCE_NONE, 0, sim::ALLIANCE_NONE};
            r.stats[0].score = 500;
            r.stats[1].score = 900;
            r.stats[2].score = 600;
            r.stats[3].score = 100;
            ASSERT_EQ(Application::match_over_json("r2d2c3", 3, start_of("Bomb Island.lvl", 1, 0xF, {"Maple", "Pi\"p", "Juniper", "a\\b"}), r), golden["alliance-187"]);
        }
        {   // a tie at the top: each of the two plays the winner cue on its own screen, so both rows won; the server's order keeps the lower seat first
            sim::MatchResult r;
            r.present_mask = 0xB;
            r.stats[0].score = 1500;
            r.stats[1].score = 1500;
            r.stats[3].score = 300;
            ASSERT_EQ(Application::match_over_json("t1e000", 0, start_of("SMALL.LVL", 0, 0xB, {"Maple", "Sam", "Juniper", "Pip (bot, Hard)"}), r), golden["tie-with-a-bot"]);
        }
    } TEST_END();
}
