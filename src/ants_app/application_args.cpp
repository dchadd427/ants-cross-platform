// The command line of the game (application.hpp, ApplicationConfig): every option of the program, what it sets and which combinations it refuses.

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ants_app/application.hpp"
#include "ants_ai/bot.hpp"

namespace ants::app {

namespace {

// The map file that --room-map names: a name with an extension is the file as it is ("my-map.lvl"), a plain word is the original's file of that name ("treasure" is TREASURE.LVL)
std::string room_map_file(const std::string& text) {
    if (text.find('.') != std::string::npos) return text;
    std::string file;
    for (const char c : text) file.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    return file + ".LVL";
}

} // anonymous namespace

ApplicationConfig Application::parse_arguments(int argc, char* argv[]) {
    ApplicationConfig cfg{};
    cfg.aspect = kPlatformDefaultAspect;                                      // 16:9 on a desktop; --aspect and the settings' key win. (A config that is made by hand, as the tests do, is the original's 4:3.)
#if defined(__EMSCRIPTEN__)
    cfg.label_unnamed_teams = false;                                          // no other players in the browser build: no placeholder labels
#endif

    // The desktop start menu (start_menu.hpp) is shown by a native game that is started without a mode: every option that starts something directly (a match, a room, a test run, a
    // screenshot) says so here and skips the menu; the options that only set something up (--name, --settings, --seed, --team-name, --title, the window options ...) do not
    bool mode_given = false;
    bool menu_forced = false;
    bool direct_match = false;                                                 // --map, --open-options, --scorecard: a match starts at once (the menu cannot come first)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
            mode_given = true;
        } else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            cfg.default_map_path = argv[++i];
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--settings") == 0 && i + 1 < argc) {
            cfg.settings_path = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.random_seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            cfg.fullscreen = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            cfg.screenshot_path = argv[++i];
            mode_given = true;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            cfg.screenshot_frames = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--select-ant") == 0 && i + 1 < argc) {
            cfg.select_ant_id = std::stoi(argv[++i]);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--select-base") == 0 && i + 1 < argc) {
            cfg.select_base_team = std::stoi(argv[++i]);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--open-options") == 0) {
            cfg.open_options = true;
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--show-grid") == 0) {
            cfg.show_tile_grid = true;
        } else if (std::strcmp(argv[i], "--map-select") == 0) {
            cfg.start_in_map_select = true;
            mode_given = true;                                                 // "start on the setup screen": today's start, without the menu
        } else if (std::strcmp(argv[i], "--player") == 0 && i + 1 < argc) {
            cfg.local_player_id = static_cast<uint8_t>(std::stoi(argv[++i]));
            mode_given = true;
        } else if (std::strcmp(argv[i], "--scorecard") == 0) {
            cfg.show_scorecard = true;
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            cfg.player_name = argv[++i];                                       // this player's name: the room, the HUD, chat, the results
        } else if (std::strcmp(argv[i], "--team-name") == 0 && i + 2 < argc) {   // --team-name <0-3> <name>, for a local game
            const int team = std::atoi(argv[i + 1]);
            if (team >= 0 && team < 4) cfg.team_names[static_cast<size_t>(team)] = argv[i + 2];
            i += 2;
        } else if (argv[i][0] == '-' && argv[i][1] == 'N' && argv[i][2] >= '0' && argv[i][2] <= '3') {
            cfg.team_names[static_cast<size_t>(argv[i][2] - '0')] = argv[i] + 3;   // the original's -N<team><name>
        } else if (std::strncmp(argv[i], "-pnum=", 6) == 0 || std::strncmp(argv[i], "-pnum:", 6) == 0) {
            const int team = std::atoi(argv[i] + 6);                           // the original's local team (its own spelling has the colon, 0x1047134)
            if (team >= 0 && team < 4) cfg.local_player_id = static_cast<uint8_t>(team);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            cfg.net_role = ApplicationConfig::NetRole::Host;
            mode_given = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {   // an optional port
                cfg.net_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            }
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            cfg.net_role = ApplicationConfig::NetRole::Join;
            mode_given = true;
            std::string target = argv[++i];                                    // host or host:port
            const size_t colon = target.rfind(':');
            if (colon != std::string::npos && target.find(':') == colon) {
                cfg.net_port = static_cast<uint16_t>(std::stoul(target.substr(colon + 1)));
                target.resize(colon);
            }
            cfg.net_address = target;
        } else if (std::strcmp(argv[i], "--join-url") == 0 && i + 1 < argc) {
            cfg.net_role = ApplicationConfig::NetRole::Join;
            mode_given = true;
            cfg.net_url = argv[++i];                                           // ws:// or wss://: through a game server's WebSocket door
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg.net_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--loopback") == 0) {
            cfg.net_loopback_only = true;
        } else if (std::strcmp(argv[i], "--lan-port") == 0 && i + 1 < argc) {
            cfg.lan_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--no-lan") == 0) {
            cfg.lan_port = 0;                                                  // the room is not announced on the local network
        } else if (std::strcmp(argv[i], "--room") == 0 && i + 1 < argc) {
            const std::string code = argv[++i];
            if (net::valid_room_code(code)) cfg.net_room = code;               // letters, digits, '_' and '-' (up to 32); anything else is ignored
            mode_given = true;
        } else if (std::strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            cfg.net_token = argv[++i];
            mode_given = true;
        } else if (std::strcmp(argv[i], "--seat") == 0 && i + 1 < argc) {
            const int seat = std::atoi(argv[++i]);                             // the color to sit in: 0 green, 1 red, 2 blue, 3 black
            if (seat >= 0 && seat < 4) cfg.net_seat = static_cast<uint8_t>(seat);
            mode_given = true;
        } else if (std::strcmp(argv[i], "--start-when") == 0 && i + 1 < argc) {
            const int players = std::atoi(argv[++i]);                          // a test hook: the leader of a server's room presses START once this many players are in
            if (players >= 1 && players <= 4) cfg.net_start_when = static_cast<uint8_t>(players);       // (1: a leader with --fill-bots starts alone)
        } else if (std::strcmp(argv[i], "--room-map") == 0) {                  // the create block of a join (protocol 15): the room that the server makes if it has none of the code
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--room-map needs a map: one of tiny, small, medium, gauntlet, treasure, islands, or a file name of the server's maps (TREASURE.LVL)";
            } else {
                const std::string file = room_map_file(argv[++i]);
                if (!net::valid_map_name(file)) {
                    if (cfg.startup_error.empty()) cfg.startup_error = "--room-map " + std::string(argv[i]) + ": not a map name (a word like treasure, or a file name that ends in .LVL and has none of / \\ : * ? \" < > | in it)";
                } else {
                    if (!cfg.net_create) cfg.net_create.emplace();
                    cfg.net_create->map_name = file;
                }
            }
        } else if (std::strcmp(argv[i], "--room-seats") == 0) {
            const std::string text = i + 1 < argc ? argv[i + 1] : "";
            const int seats = text.size() == 1 && text[0] >= '2' && text[0] <= '4' ? text[0] - '0' : 0;      // (one digit: "3x" and "2.5" are no number of seats)
            if (i + 1 >= argc || seats == 0) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--room-seats needs 2, 3 or 4";
                if (i + 1 < argc) ++i;
            } else {
                ++i;
                if (!cfg.net_create) cfg.net_create.emplace();
                cfg.net_create->seats = static_cast<uint8_t>(seats);
            }
        } else if (std::strcmp(argv[i], "--room-teams") == 0) {
            LocalTeams teams;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--room-teams needs ffa or two seats like 0+1";
            } else if (!parse_local_teams(argv[++i], teams, why)) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--room-teams " + std::string(argv[i]) + ": " + why;
            } else {
                if (!cfg.net_create) cfg.net_create.emplace();
                cfg.net_create->set_teams(teams);
            }
        } else if (std::strcmp(argv[i], "--room-leader-start") == 0) {         // a full room waits for its leader's START
            if (!cfg.net_create) cfg.net_create.emplace();
            cfg.net_create->flags = static_cast<uint8_t>(cfg.net_create->flags | net::kCreateLeaderStarts);
        } else if (std::strcmp(argv[i], "--platform") == 0) {                  // what this game tells the room about itself (protocol 15)
            if (i + 1 >= argc || !net::parse_platform(argv[i + 1], cfg.net_platform)) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--platform needs windows, macos, linux, android, ios or other (browser-windows, ... for a game in a web page)";
                if (i + 1 < argc) ++i;
            } else {
                ++i;
            }
        } else if (std::strcmp(argv[i], "--fill-bots") == 0) {                // the bots that this player's START seats in the empty seats of its room (protocol 11; a level for each seat since 13)
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--fill-bots needs none, easy, medium or hard (or four of them, one for each seat: none,none,easy,hard)";
            } else {
                std::string why;
                if (!net::parse_fill_plan(argv[++i], cfg.fill_bots, why) && cfg.startup_error.empty()) cfg.startup_error = "--fill-bots " + std::string(argv[i]) + ": " + why;
            }
        } else if (std::strcmp(argv[i], "--say") == 0 && i + 1 < argc) {
            cfg.net_say = argv[++i];                                           // a test hook: a line to say in the waiting room
        } else if (std::strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            cfg.title = argv[++i];
        } else if (std::strcmp(argv[i], "--window-pos") == 0 && i + 1 < argc) {
            cfg.has_window_pos = parse_pair(argv[++i], cfg.window_x, cfg.window_y);
        } else if (std::strcmp(argv[i], "--window-size") == 0) {                    // --window-size WxH or W,H (at least 320x240); anything else is refused, as --aspect's is
            int32_t w = 0;
            int32_t h = 0;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--window-size needs WIDTHxHEIGHT or WIDTH,HEIGHT (for example 1280x720)";
            } else if (parse_window_size(argv[++i], w, h, why)) {
                cfg.has_window_size = true;
                cfg.window_w = w;
                cfg.window_h = h;
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--window-size " + why;
            }
        } else if (std::strcmp(argv[i], "--grid") == 0 && i + 1 < argc) {
            if (!parse_grid(argv[++i], cfg.grid_cols, cfg.grid_rows)) cfg.grid_cols = cfg.grid_rows = 0;
        } else if (std::strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
            cfg.grid_cell = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--display") == 0 && i + 1 < argc) {
            cfg.display_index = std::max(-1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--audio-focus") == 0) {
            cfg.audio_follows_focus = true;
        } else if (std::strcmp(argv[i], "--server") == 0) {                    // the game server of the start menu's Join and Host: HOST[:PORT]
            ServerAddress parsed;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--server needs HOST[:PORT], e.g. --server beta.playants.org:4001";
            } else {
                cfg.server = argv[++i];
                if (!parse_server(cfg.server, parsed, why) && cfg.startup_error.empty()) cfg.startup_error = "--server " + cfg.server + ": " + why;
            }
        } else if (std::strcmp(argv[i], "--start-menu") == 0) {                // forces the start menu (also headless, with --screenshot: the tests and the screenshots)
            menu_forced = true;
        } else if (std::strcmp(argv[i], "--aspect") == 0) {                      // --aspect NAME: a name of the shape table (canvas_layout.hpp kAspectTable)
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--aspect needs " + aspect_names(" or ");
            } else if (parse_aspect(argv[++i], cfg.aspect, why)) {
                cfg.aspect_given = true;
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--aspect " + why;
            }
        } else if (std::strcmp(argv[i], "--zoom") == 0) {                        // --zoom 0.05 .. 2: the zoom of the map view that a match starts with, the nearest level that its map offers (view_zoom.hpp)
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--zoom needs a number from 0.05 to 2";
            } else if (zoom::parse_level(argv[++i], cfg.zoom, why)) {
                cfg.zoom_given = true;
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--zoom " + why;
            }
        } else if (std::strcmp(argv[i], "--prediction") == 0) {                  // --prediction on | off: the prediction of the player's own orders in a match of the network (net::Prediction)
            if (i + 1 >= argc || !parse_switch(argv[i + 1], cfg.prediction)) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--prediction needs on or off";
                if (i + 1 < argc) ++i;
            } else {
                ++i;
                cfg.prediction_given = true;
            }
        } else if (std::strcmp(argv[i], "--no-prediction") == 0) {              // the same as --prediction off
            cfg.prediction = false;
            cfg.prediction_given = true;
        } else if (std::strcmp(argv[i], "--bot") == 0) {                       // a computer player: --bot SEAT[:SPEC], repeatable (docs/BOTS.md)
            mode_given = true;
            ai::BotSpec spec;
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--bot needs SEAT[:SPEC], e.g. --bot 1:hard";
            } else if (ai::parse_bot_spec(argv[++i], spec, why)) {
                cfg.bots.push_back(spec);
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--bot " + std::string(argv[i]) + ": " + why;
            }
        } else if (std::strcmp(argv[i], "--teams") == 0) {                     // the teams of a game on this computer: ffa | A+B (docs/BOTS.md, "Alliances")
            std::string why;
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--teams needs ffa or two seats like 0+1";
            } else if (!parse_local_teams(argv[++i], cfg.teams, why) && cfg.startup_error.empty()) {
                cfg.startup_error = "--teams " + std::string(argv[i]) + ": " + why;
            }
        } else if (std::strcmp(argv[i], "--game-mode") == 0) {                 // the rules of the games this machine makes: highest-score (the original's) | 187 (ApplicationConfig::game_mode)
            if (i + 1 >= argc) {
                if (cfg.startup_error.empty()) cfg.startup_error = "--game-mode needs highest-score or 187";
            } else if (net::parse_game_mode_name(argv[++i], cfg.game_mode)) {
                cfg.game_mode_given = true;
            } else if (cfg.startup_error.empty()) {
                cfg.startup_error = "--game-mode " + std::string(argv[i]) + ": the game types are highest-score and 187";
            }
        } else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {   // the match of a .antsrep file (ApplicationConfig::replay_path)
            cfg.replay_path = argv[++i];
            cfg.start_in_map_select = false;
            mode_given = direct_match = true;
        } else if (std::strcmp(argv[i], "--replay-live") == 0) {               // the file of --replay is a copy of a match that is still going on (ApplicationConfig::replay_live)
            cfg.replay_live = true;
        } else if (std::strcmp(argv[i], "--play") == 0) {                      // the setup screen's own START at its first visit (ApplicationConfig::play_at_once)
            cfg.play_at_once = true;
            mode_given = true;
        } else if (std::strcmp(argv[i], "--alone") == 0) {                     // a game for one: only the local player's seat plays (ApplicationConfig::alone)
            cfg.alone = true;
            mode_given = true;
        }
    }
    if (!cfg.replay_path.empty() && cfg.startup_error.empty() && (cfg.net_role != ApplicationConfig::NetRole::None || !cfg.bots.empty() || cfg.alone || cfg.play_at_once)) {
        cfg.startup_error = "--replay shows a recorded match: it cannot be combined with --host, --join, --join-url, --bot, --alone or --play.";
    }
    if (cfg.replay_live && cfg.replay_path.empty() && cfg.startup_error.empty()) cfg.startup_error = "--replay-live follows the file of --replay: it needs --replay FILE.";
    if (cfg.game_mode_given && cfg.startup_error.empty()) {      // the mode is the choice of whoever makes the game: this machine for a game of its own and for the room of --host, nobody else's
        if (!cfg.replay_path.empty()) cfg.startup_error = "--game-mode cannot be combined with --replay: the file says which game was played.";
        else if (cfg.net_role == ApplicationConfig::NetRole::Join || !cfg.net_room.empty() || cfg.net_create) cfg.startup_error = "--game-mode is the choice of the room's host: it cannot be used with --join, --join-url, --room or the --room-* options of a room that a server makes.";
    }
    if (cfg.alone && !cfg.bots.empty() && cfg.startup_error.empty()) cfg.startup_error = "--alone cannot be used with --bot: a game for one has no other player.";
    if (cfg.play_at_once) cfg.start_in_map_select = true;                      // (--map names the map; it would start it at once, without the screens and without the START's own path)
#if !defined(__EMSCRIPTEN__)
    if (menu_forced) {
        // The menu comes first and chooses the match: an option that starts a match or a room at once cannot be combined with it, nor can --alone, which decides who plays (the menu's Single player does)
        if ((direct_match || cfg.net_role != ApplicationConfig::NetRole::None) && cfg.startup_error.empty()) {
            cfg.startup_error = "--start-menu cannot be combined with --map, --open-options, --scorecard, --host, --join or --join-url: they start a match or a room at once";
        } else if (cfg.alone && cfg.startup_error.empty()) {
            cfg.startup_error = "--start-menu cannot be combined with --alone: the menu's Single player chooses who plays.";
        }
        cfg.start_menu = true;
    } else {
        cfg.start_menu = !mode_given;
    }
#else
    (void)mode_given;                                                          // the browser build never shows the menu (its page has its own controls)
    (void)menu_forced;
    (void)direct_match;
#endif
    return cfg;
}

} // namespace ants::app
