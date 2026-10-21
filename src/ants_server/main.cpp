// ants_server: the dedicated game server. A headless program: it hosts many rooms, each a host without a seat that runs the match as the referee (docs/NETWORK_PORT.md).
//
//   ants_server --maps DIR [--port 4001] [--ws-port 4002] [--ctl-port 4010] [--public] [--ws-any-interface] [--ctl-any-interface] [--results-dir DIR] [--secret-file PATH] [--max-rooms N]
//               [--reconnect | --no-reconnect] [--hold-vote-seconds N] [--max-pause-seconds N] [--max-catch-up-seconds N]
//               [--resume-countdown-seconds N] [--start-countdown-seconds N] [--log-mb N]
//               [--restart-dir DIR | --no-restart-records] [--restart-vote-seconds N] [--restart-budget-mb N]
//               [--replays-dir DIR | --no-replays] [--replay-demo] [--replays-days N] [--replays-max-mb N] [--replay-port N] [--replay-any-interface]
//               [--bot-match-every-min N] [--db FILE | --no-db] [--no-history]
//
//   --maps DIR         the maps folder (the .lvl files that rooms may use); required
//   --port N           the TCP port of native clients (0: none; default 4001); every interface with --public, else this machine only
//   --ws-port N        the WebSocket port of browsers and Electron behind a reverse proxy that ends TLS (0: none; default 0); this machine only. It also answers the public GET /busy, GET /stats and POST /stats/local
//   --ctl-port N       the control interface: HTTP + JSON on this machine only, with a bearer secret (0: none; default 0; needs the secret)
//   --public           the TCP game port accepts connections from other machines
//   --ws-any-interface, --ctl-any-interface
//                      the WebSocket / control port listens on every interface instead of the loopback address. For a container only: a port that is published
//                      from a container does not reach a program that listens on the container's loopback address. The host decides who can connect
//                      (docker run -p 127.0.0.1:4010:4010 ...); never use these on a machine without that protection, the control interface speaks plain HTTP.
//   --results-dir DIR  every ended room writes <code>.json there, and the site statistics (the counters of GET /stats) are kept in the database, or with --no-db in site-stats.json (without a results folder they live in memory)
//   --secret-file PATH where the server keeps the control secret that it makes when ANTS_SERVER_SECRET is not set (default: control-secret in the results folder)
//   --community-maps DIR  a second folder of maps, the players' maps (Community-Maps/ of the repository: the .lvl files with maps.json and previews.json; the image has it in /community-maps). The lobby
//                      may choose every map of --maps and of this folder, and GET /maps on the replay port lists them. Left out: only the --maps folder (the default)
//   --max-rooms N      the most rooms at a time (default 256)
//   --demo-rooms N     for a public test page: a Hello that carries a create block (network protocol 15: the map, the seats 2 - 4, the teams and whether a full room waits for its leader's
//                      START) for a room that does not exist makes it, at most N public rooms at a time. The code is only a name (the game makes 6 random letters and numbers). N is 1 to
//                      --max-rooms - 1 (255 by default); the option is left out to switch public rooms off (the default): 0 and more than that stop the server at startup
//   --demo-map NAME    the map of the public rooms whose block names none that the server offers (a file name of the maps folder; required with --demo-rooms)
//   --demo-maps LIST   the maps a create block may choose, file names of the maps folder separated by commas (blanks around a name are dropped; the block's map is compared
//                      with them in any case); a block that names another map (or none) gets --demo-map (needs --demo-rooms). A public room waits ten minutes for its players.
//   --demo-lobbies N   for the front page's lobby (network protocol 16): a lobby page's Hello with a lobby create block for a room that does not exist makes a LOBBY ROOM, a room that lives while a person is in it
//                      and begins its match when its leader says so (docs/SERVER.md "Lobby rooms"), at most N waiting at a time (the one that has been empty the longest gives its place up to a new one;
//                      when every one has a person in it the page is told NoSuchRoom). A waiting lobby holds no place among the --demo-rooms: the match it begins does, from its START on. N is 0 to
//                      --max-rooms - --demo-rooms - 1 (0: no lobby rooms). Without the option a server with --demo-rooms and reconnect has 200 (fewer where --max-rooms leaves no more room); a value
//                      above 0 needs --demo-rooms, --demo-map and reconnect (a lobby holds the seat of a link that dropped), or the server stops at startup
//   --reconnect, --no-reconnect
//                      a room holds the seat of a player whose connection is lost (protocol 10): the match is paused for everybody, the seat comes back with its key, the others
//                      may vote to go on without it, the match's total pause is capped. ON by default (the game's own clients come back by themselves: release B); --no-reconnect turns it
//                      off (a lost player is dropped at once and no restart record is kept). It is the default of the rooms (the control interface's "reconnect" overrides it per room;
//                      public rooms follow it); the last of the two options wins
//   --hold-vote-seconds N
//                      the others may vote on going on without a seat once it has been away N seconds in all (5 - 3600, default 30; the control interface's "hold_vote_seconds")
//   --max-pause-seconds N
//                      the cap on a match's total paused time: at the cap every seat that is not present is dropped (60 - 86400, default 1800; the control interface's "max_pause_seconds")
//   --max-catch-up-seconds N
//                      the time that one absence may spend catching up in all, over all its attempts: then the catch-up fails, the seat is absent (the vote and the cap apply) and its key
//                      is refused (10 - 3600, default 300; the control interface's "max_catch_up_seconds")
//   --resume-countdown-seconds N
//                      after a pause of 3 s or more the match is held this long before it goes on (0 - 60, default 10, 0 = none; the control interface's "resume_countdown_seconds")
//   --start-countdown-seconds N
//                      protocol 21: a room with two or more people holds the first turn of its match this long, with the chat open, instead of the 5 s "Get ready" dialog (0 - 60, default 30, 0 = none; the
//                      control interface's "countdown_seconds"); a room of one person, with or without computer players, never counts down
//   --log-mb N         the limit of one room's turn log, which a returning player is given the match from, in MiB (1 - 256, default 16); the logs of all the rooms together may take 256 MiB
//   --restart-dir DIR  where the rooms that hold seats keep their RESTART RECORDS (restart_record.hpp, docs/NETWORK_PORT.md "Restart records"): from the start of its match to its end a room
//                      writes every sealed turn to a file here (mode 600: it holds the keys of the seats) BEFORE the turn is sent to anybody, so that a server that is stopped, crashes or is
//                      redeployed does not end the matches that run: the server that starts again finds the records, replays them (checked against stored state hashes), holds every seat
//                      and the players come back with their keys. Default: the folder "restart" in --results-dir; without a results folder the server keeps none. A record of another network
//                      protocol is not restored (the room is closed with that reason). Only rooms that hold seats (the default, unless --no-reconnect or a room's "reconnect": false) keep one
//   --no-restart-records
//                      keep no restart records (a running match ends with the server, as it did before records), whatever --results-dir says
//   --restart-vote-seconds N
//                      after a restart the others may vote on going on without a seat that has not come back once it has been away N seconds (30 - 3600, default 90: a restart is nobody's
//                      fault and every player has to notice, wait for the server, reconnect and catch up; a lost link's time is --hold-vote-seconds); the match's pause cap still applies
//   --restart-budget-mb N
//                      the disk that all the restart records together may take (1 - 4096, default 256); one record is at most 48 MiB (3 times the turn log's limit); a record that the disk or
//                      the budget refuses is deleted and its room plays on without one (its status says why)
//   --replays-dir DIR  where the server keeps the REPLAYS of the matches that are played in its rooms (replay_store.hpp, docs/REPLAYS.md "On the game server"): when a match that ran 30 seconds or more is over, its .antsrep file is kept
//                      here (the names that the players typed are in it, with no address or room code). Default: the folder "replays" in --results-dir; without a results folder the server keeps none
//   --no-replays       keep no replays, whatever --results-dir says
//   --bot-match-every-min N
//                      the server plays a match of computer players of its own every N minutes (5 - 1440; 0 = none, the default): two to four standard bots, Medium or Hard, on random seats of one of the original's six
//                      maps, at the normal speed, so that anybody can watch it live on the site and again afterwards (docs/SERVER.md "Matches of computer players"). Needs the replays (without them nothing is
//                      played): the first match begins a minute after the start, the next ones N minutes after the one before began, and none while the last is still going.
//   --db FILE          the database file (SQLite, docs/SERVER.md "The database"). Default: ants.db in --results-dir; without a results folder the server keeps none. A file that cannot be opened (not a database,
//                      damaged, not writable) or that a newer program made stops the server; the file is never changed or deleted by that
//   --no-db            keep no database, whatever --results-dir says
//   --no-history       keep no match history (docs/REPLAYS.md "The match history": what every match came to, in the database, for good). Without this option the history is kept whenever there is a database; there is
//                      no folder of its own (it lives in the database), and --no-db keeps none either. It counts the matches from the replays, while no match runs, so it needs those to add to it
//   --replay-demo      keep the matches of the demo rooms too (the games of the front page, bots only included); off by default, so that only the rooms of the control interface are kept
//   --replays-days N   a replay is deleted N days after its match ended (1 - 3650, default 30)
//   --replays-max-mb N all the replays together may take N MiB, the oldest are deleted first (1 - 4096, default 100); by default the folder is on the volume that also holds the control secret and the
//                      restart records, so a match is also not kept when the disk has less than 256 MiB free, or when 120 matches were kept in the last hour
//   --replay-port N    a PUBLIC, read-only door for the list and the files of the replays, the match history and the matches that run now (GET /replays, GET /replays/<file>, GET /history, GET /history/<id>,
//                      GET /live, GET /live/<id>; no secret, nothing else answers): 0 = off, the default. This machine only; the site's reverse proxy passes the paths to it (docker/nginx.conf). The control interface
//                      lists and gives the files and the history too (all of them), and deletes them, behind its secret whatever this says
//   --replay-any-interface
//                      the replay port listens on every interface (for a container only, like --ws-any-interface)
//   --submit-port N    the door of the MAP SUBMISSIONS (docs/MAP_SUBMISSIONS.md): a player sends a map (POST /submissions), finds their own again, withdraws one; the owner's review page asks /admin/api/...
//                      0 = off, the default. This machine only; the site's nginx passes the paths to it and sets the real client address. It needs the database and a folder for the files
//   --submit-any-interface
//                      the submission door listens on every interface (for a container only)
//   --submissions-dir DIR
//                      where the submitted maps are kept (the files, in DIR/files). Default: the folder "submissions" in --results-dir
//   --submit-max-waiting N, --submit-per-address N
//                      how many maps may wait for the owner (default 200) and how many one address may send in a UTC day (default 3)
//   --submit-ignore-client-header
//                      count an address by the connection's own address instead of the X-Client-Address that the site's nginx sets (for a door that no proxy stands in front of)
//   The owner's page needs one setting in the environment (never on the command line): ANTS_ADMIN_SESSION_SECRET (32 or more characters; it signs the sign-in cookie). Without it, every /admin/api
//   path answers 404. ANTS_ADMIN_ALLOW_HTTP=1 drops the Secure flag of the cookie, for a trial over plain http on this machine only.
//   --admin-create NAME   makes the first Admin account (3 to 32 letters, digits, dot, dash, underscore), prints its random starting password ONCE on standard output and stops. It refuses when an Admin exists.
//   --admin-reset NAME    gives that user a new random starting password (printed once), signs the user out, clears the locks and enables the account, and stops.
//                      Both need --db FILE or --results-dir DIR (ants.db in it) and may run beside the server. (docs/MAP_SUBMISSIONS.md, "Accounts")
//   --version, --help
//
// SIGTERM and SIGINT stop the server within a moment: the records of the running rooms are made durable (fsync) and left where they are (docker stop: give the container a grace period, the stack
// files set 15 s), the other rooms are closed. A record is deleted when its room is over, never because the server stops.
//
// The control interface's secret comes from the environment (ANTS_SERVER_SECRET), never from the command line (a command line is visible to every user). Without
// it the server makes a random secret the first time it starts and keeps it in the secret file (POSIX: for its owner only, mode 600; Windows: the file takes the
// permissions of its folder), so a container needs no setup: the secret is printed once, the moment it is made (before anything else can stop the server); later it is read
// from the file (docker exec <container> cat /results/control-secret). A secret in the environment wins. A file that exists is used as it is (secret.hpp).

#include <algorithm>
#include <iostream>
#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ants_app/version.hpp"
#include "ants_ctl/http.hpp"
#include "ants_net/tcp.hpp"
#include "ants_net/ws.hpp"
#include "ants_net/protocol.hpp"
#include "ants_server/admin_accounts.hpp"
#include "ants_server/admin_cli.hpp"
#include "ants_server/control.hpp"
#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/restart_record.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_server/secret.hpp"
#include "ants_server/site_stats.hpp"
#include "ants_server/submissions.hpp"
#include "ants_server/submissions_http.hpp"

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

constexpr size_t kDefaultDemoLobbies = 200;           // lobby rooms that may wait at a time when --demo-lobbies is not given (docs/SERVER.md "Lobby rooms")

struct Options {
    std::string maps_dir;
    std::string community_maps_dir;
    uint16_t port{4001};
    uint16_t ws_port{0};
    uint16_t ctl_port{0};
    bool is_public{false};
    bool ws_any_interface{false};
    bool ctl_any_interface{false};
    std::string results_dir;
    std::string secret_file;
    size_t max_rooms{256};
    size_t demo_rooms{0};
    std::string demo_map;
    std::vector<std::string> demo_maps;
    bool demo_maps_given{false};
    size_t demo_lobbies{0};                    // --demo-lobbies (protocol 16)
    bool demo_lobbies_given{false};
    bool reconnect{ants::server::kReconnectByDefault};
    long hold_vote_s{30};
    long max_pause_s{1800};
    long max_catch_up_s{300};
    long resume_countdown_s{10};
    long start_countdown_s{static_cast<long>(ants::net::kMatchCountdownSeconds)};     // --start-countdown-seconds (protocol 21)
    long log_mb{16};
    std::string restart_dir;                   // --restart-dir (empty: the folder "restart" in the results folder, when there is one)
    bool no_restart_records{false};
    long restart_vote_s{90};
    long restart_budget_mb{256};
    std::string replays_dir;                   // --replays-dir (empty: the folder "replays" in the results folder, when there is one)
    bool no_replays{false};
    bool replay_demo{false};                   // --replay-demo: the matches of the demo rooms are kept too
    long replays_days{30};
    long replays_max_mb{100};
    uint16_t replay_port{0};                   // --replay-port: the public read-only door of the replays (0: none)
    bool replay_any_interface{false};
    long bot_match_every_min{0};             // --bot-match-every-min (0: none)
    std::string db_file;                       // --db (empty: ants.db in the results folder, when there is one)
    bool no_db{false};
    bool no_history{false};                    // --no-history
    uint16_t submit_port{0};                   // --submit-port: the door of the map submissions (0: none)
    bool submit_any_interface{false};
    std::string submissions_dir;               // --submissions-dir (empty: the folder "submissions" in the results folder)
    size_t submit_max_waiting{200};
    size_t submit_per_address{3};
    bool submit_ignore_client_header{false};
    std::string admin_create, admin_reset;     // --admin-create, --admin-reset: the account commands
};

void usage(FILE* to) {
    std::fprintf(to,
                 "usage: ants_server --maps DIR [--port 4001] [--ws-port N] [--ctl-port N] [--public] [--ws-any-interface] [--ctl-any-interface]\n"
                 "                    [--results-dir DIR] [--secret-file PATH] [--max-rooms N] [--community-maps DIR]\n"
                 "                    [--demo-rooms N --demo-map NAME [--demo-maps A.LVL,B.LVL,...] [--demo-lobbies N]]\n"
                 "                    [--reconnect | --no-reconnect] [--hold-vote-seconds 5-3600] [--max-pause-seconds 60-86400]\n"
                 "                    [--max-catch-up-seconds 10-3600] [--resume-countdown-seconds 0-60] [--start-countdown-seconds 0-60] [--log-mb 1-256]\n"
                 "                    [--restart-dir DIR | --no-restart-records] [--restart-vote-seconds 30-3600] [--restart-budget-mb 1-4096]\n"
                 "                    [--replays-dir DIR | --no-replays] [--replay-demo] [--replays-days 1-3650] [--replays-max-mb 1-4096]\n"
                 "                    [--replay-port N] [--replay-any-interface] [--bot-match-every-min 0|5-1440] [--db FILE | --no-db] [--no-history]\n"
                 "                    [--submit-port N] [--submit-any-interface] [--submissions-dir DIR] [--submit-max-waiting N] [--submit-per-address N] [--submit-ignore-client-header]\n"
                 "       ants_server (--admin-create NAME | --admin-reset NAME) (--db FILE | --results-dir DIR)\n"
                 "  the control interface takes its secret from the environment variable ANTS_SERVER_SECRET; without it the server makes one and keeps it\n"
                 "  in --secret-file (default: control-secret in --results-dir)\n");
}

bool parse_port(const char* text, uint16_t& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > 65535) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

// The address of a peer without its port ("10.0.0.5:51234" -> "10.0.0.5")
std::string host_part(const std::string& peer) {
    const size_t colon = peer.rfind(':');
    return colon == std::string::npos ? peer : peer.substr(0, colon);
}

uint32_t now_ms() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void log(const std::string& line) { std::fprintf(stderr, "[ants_server %u] %s\n", now_ms() / 1000, line.c_str()); }

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                usage(stderr);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            usage(stdout);
            return 0;
        } else if (a == "--version") {
            std::printf("ants_server %s build %s (network protocol %u)\n", std::string(ants::VERSION_STRING).c_str(), std::string(ants::BUILD_ID).c_str(), static_cast<unsigned>(ants::net::kProtocolVersion));
            return 0;
        } else if (a == "--maps") {
            o.maps_dir = value("--maps");
        } else if (a == "--community-maps") {
            o.community_maps_dir = value("--community-maps");
        } else if (a == "--port" || a == "--ws-port" || a == "--ctl-port") {
            uint16_t p = 0;
            if (!parse_port(value(a.c_str()), p)) {
                std::fprintf(stderr, "%s takes a port number (0 - 65535)\n", a.c_str());
                return 2;
            }
            (a == "--port" ? o.port : a == "--ws-port" ? o.ws_port : o.ctl_port) = p;
        } else if (a == "--replay-port") {
            if (!parse_port(value("--replay-port"), o.replay_port)) {
                std::fprintf(stderr, "--replay-port takes a port number (0 - 65535)\n");
                return 2;
            }
        } else if (a == "--submit-port") {
            if (!parse_port(value("--submit-port"), o.submit_port)) {
                std::fprintf(stderr, "--submit-port takes a port number (0 - 65535)\n");
                return 2;
            }
        } else if (a == "--admin-create" || a == "--admin-reset") {
            (a == "--admin-create" ? o.admin_create : o.admin_reset) = value(a.c_str());
        } else if (a == "--submit-any-interface") {
            o.submit_any_interface = true;
        } else if (a == "--submit-ignore-client-header") {
            o.submit_ignore_client_header = true;
        } else if (a == "--submissions-dir") {
            o.submissions_dir = value("--submissions-dir");
            if (o.submissions_dir.empty()) {
                std::fprintf(stderr, "--submissions-dir takes a folder\n");
                return 2;
            }
        } else if (a == "--submit-max-waiting" || a == "--submit-per-address") {
            const char* text = value(a.c_str());
            char* end = nullptr;
            const long n = std::strtol(text, &end, 10);
            if (end == text || *end != '\0' || n < 1 || n > 100000) {
                std::fprintf(stderr, "%s takes a whole number from 1 to 100000\n", a.c_str());
                return 2;
            }
            (a == "--submit-max-waiting" ? o.submit_max_waiting : o.submit_per_address) = static_cast<size_t>(n);
        } else if (a == "--public") {
            o.is_public = true;
        } else if (a == "--ws-any-interface") {
            o.ws_any_interface = true;
        } else if (a == "--ctl-any-interface") {
            o.ctl_any_interface = true;
        } else if (a == "--replay-any-interface") {
            o.replay_any_interface = true;
        } else if (a == "--replay-demo") {
            o.replay_demo = true;
        } else if (a == "--no-replays") {
            o.no_replays = true;
        } else if (a == "--no-db") {
            o.no_db = true;
        } else if (a == "--no-history") {
            o.no_history = true;
        } else if (a == "--db") {
            o.db_file = value("--db");
            if (o.db_file.empty()) {
                std::fprintf(stderr, "--db takes a file name\n");
                return 2;
            }
        } else if (a == "--replays-dir") {
            o.replays_dir = value("--replays-dir");
            if (o.replays_dir.empty()) {
                std::fprintf(stderr, "--replays-dir takes a folder\n");
                return 2;
            }
        } else if (a == "--results-dir") {
            o.results_dir = value("--results-dir");
        } else if (a == "--secret-file") {
            o.secret_file = value("--secret-file");
        } else if (a == "--max-rooms") {
            o.max_rooms = static_cast<size_t>(std::strtoul(value("--max-rooms"), nullptr, 10));
            if (o.max_rooms == 0) {
                std::fprintf(stderr, "--max-rooms takes a positive number\n");
                return 2;
            }
        } else if (a == "--demo-rooms") {
            const char* text = value("--demo-rooms");
            char* end = nullptr;
            const long n = std::strtol(text, &end, 10);
            if (end == text || *end != '\0' || n < 1 || n > 1000000) {
                std::fprintf(stderr, "--demo-rooms takes a positive number\n");
                return 2;
            }
            o.demo_rooms = static_cast<size_t>(n);
        } else if (a == "--demo-lobbies") {
            const char* text = value("--demo-lobbies");
            char* end = nullptr;
            const long n = std::strtol(text, &end, 10);
            if (end == text || *end != '\0' || n < 0 || n > 1000000) {
                std::fprintf(stderr, "--demo-lobbies takes a number (0 switches the lobby rooms off)\n");
                return 2;
            }
            o.demo_lobbies = static_cast<size_t>(n);
            o.demo_lobbies_given = true;
        } else if (a == "--reconnect") {
            o.reconnect = true;
        } else if (a == "--no-reconnect") {
            o.reconnect = false;
        } else if (a == "--restart-dir") {
            o.restart_dir = value("--restart-dir");
            if (o.restart_dir.empty()) {
                std::fprintf(stderr, "--restart-dir takes a folder\n");
                return 2;
            }
        } else if (a == "--no-restart-records") {
            o.no_restart_records = true;
        } else if (a == "--hold-vote-seconds" || a == "--max-pause-seconds" || a == "--max-catch-up-seconds" || a == "--resume-countdown-seconds" || a == "--start-countdown-seconds" || a == "--log-mb" || a == "--restart-vote-seconds" ||
                   a == "--restart-budget-mb" || a == "--replays-days" || a == "--replays-max-mb") {
            const char* text = value(a.c_str());
            char* end = nullptr;
            const long n = std::strtol(text, &end, 10);
            long lo = 1, hi = 256;
            long* target = &o.log_mb;
            if (a == "--hold-vote-seconds") { lo = 5; hi = 3600; target = &o.hold_vote_s; }
            else if (a == "--max-pause-seconds") { lo = 60; hi = 86400; target = &o.max_pause_s; }
            else if (a == "--max-catch-up-seconds") { lo = 10; hi = 3600; target = &o.max_catch_up_s; }
            else if (a == "--resume-countdown-seconds") { lo = 0; hi = 60; target = &o.resume_countdown_s; }
            else if (a == "--start-countdown-seconds") { lo = 0; hi = 60; target = &o.start_countdown_s; }
            else if (a == "--restart-vote-seconds") { lo = 30; hi = 3600; target = &o.restart_vote_s; }
            else if (a == "--restart-budget-mb") { lo = 1; hi = 4096; target = &o.restart_budget_mb; }
            else if (a == "--replays-days") { lo = 1; hi = 3650; target = &o.replays_days; }
            else if (a == "--replays-max-mb") { lo = 1; hi = 4096; target = &o.replays_max_mb; }
            if (end == text || *end != '\0' || n < lo || n > hi) {
                std::fprintf(stderr, "%s takes a whole number from %ld to %ld\n", a.c_str(), lo, hi);
                return 2;
            }
            *target = n;
        } else if (a == "--bot-match-every-min") {
            const char* text = value("--bot-match-every-min");
            char* end = nullptr;
            const long n = std::strtol(text, &end, 10);
            if (end == text || *end != '\0' || n < 0 || n > 1440 || (n != 0 && n < 5)) {
                std::fprintf(stderr, "--bot-match-every-min takes 0 (none) or a whole number of minutes from 5 to 1440\n");
                return 2;
            }
            o.bot_match_every_min = n;
        } else if (a == "--demo-map") {
            o.demo_map = value("--demo-map");
        } else if (a == "--demo-maps") {
            o.demo_maps_given = true;
            const std::string list = value("--demo-maps");
            size_t from = 0;
            while (true) {                                         // "A.LVL,B.LVL" (blanks around a name are dropped): an empty item is a mistake, not a skipped entry
                const size_t comma = list.find(',', from);
                std::string item = list.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(item.begin());
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
                if (item.empty()) {
                    std::fprintf(stderr, "--demo-maps takes file names separated by commas (no empty name)\n");
                    return 2;
                }
                o.demo_maps.push_back(item);
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
            if (o.demo_maps.size() > 64) {
                std::fprintf(stderr, "--demo-maps takes at most 64 maps\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage(stderr);
            return 2;
        }
    }
    if (!o.admin_create.empty() || !o.admin_reset.empty()) {            // the account commands: no server, no maps folder
        if (!o.admin_create.empty() && !o.admin_reset.empty()) {
            std::fprintf(stderr, "--admin-create and --admin-reset exclude each other\n");
            return 2;
        }
        ants::server::AdminCommand command;
        command.kind = o.admin_create.empty() ? ants::server::AdminCommand::Kind::Reset : ants::server::AdminCommand::Kind::Create;
        command.name = o.admin_create.empty() ? o.admin_reset : o.admin_create;
        command.folder = o.db_file.empty() ? o.results_dir : std::string();
        command.db_path = !o.db_file.empty() ? o.db_file : o.results_dir.empty() ? std::string() : (std::filesystem::path(o.results_dir) / "ants.db").u8string();
        return ants::server::run_admin_command(command, std::cout, std::cerr);
    }
    if (o.maps_dir.empty()) {
        usage(stderr);
        return 2;
    }
    if (o.no_restart_records && !o.restart_dir.empty()) {
        std::fprintf(stderr, "--restart-dir and --no-restart-records exclude each other\n");
        return 2;
    }
    if (o.no_replays && !o.replays_dir.empty()) {
        std::fprintf(stderr, "--replays-dir and --no-replays exclude each other\n");
        return 2;
    }
    if (o.no_db && !o.db_file.empty()) {
        std::fprintf(stderr, "--db and --no-db exclude each other\n");
        return 2;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(o.maps_dir, ec)) {
        std::fprintf(stderr, "the maps folder '%s' does not exist\n", o.maps_dir.c_str());
        return 2;
    }
    if (!o.community_maps_dir.empty() && !std::filesystem::is_directory(o.community_maps_dir, ec)) {
        std::fprintf(stderr, "the community maps folder '%s' does not exist\n", o.community_maps_dir.c_str());
        return 2;
    }
    if (!o.results_dir.empty()) std::filesystem::create_directories(o.results_dir, ec);

    std::string secret;
    ants::server::SecretResult secret_info;
    if (o.ctl_port != 0) {
        std::string secret_file = o.secret_file;
        if (secret_file.empty() && !o.results_dir.empty()) secret_file = (std::filesystem::path(o.results_dir) / "control-secret").string();
        secret_info = ants::server::resolve_secret(std::getenv("ANTS_SERVER_SECRET"), secret_file);
        if (!secret_info.ok) {
            std::fprintf(stderr, "%s\n", secret_info.error.c_str());
            return 2;
        }
        secret = secret_info.secret;
        if (secret_info.source == ants::server::SecretSource::Generated) {
            // shown the moment it is stored, before the listeners and the option checks below can end the program: the next start only reads the file
#ifdef _WIN32
            const char* protection = "on Windows it takes the permissions of its folder";
#else
            const char* protection = "owner-only";
#endif
            log("control secret made now and stored in " + secret_info.path + " (" + protection + "); it is shown here this once: " + secret_info.secret);
        }
    }

    std::unique_ptr<ants::net::TcpListener> tcp;
    std::unique_ptr<ants::net::WsListener> ws;
    std::unique_ptr<ants::ctl::HttpServer> http;
    std::unique_ptr<ants::ctl::HttpServer> replay_http;           // the public, read-only door of the replays (--replay-port)
    std::unique_ptr<ants::ctl::HttpServer> submit_http;           // the door of the map submissions (--submit-port)
    if (o.port != 0) {
        tcp = ants::net::TcpListener::listen(o.port, !o.is_public);
        if (!tcp) {
            std::fprintf(stderr, "cannot listen on TCP port %u\n", static_cast<unsigned>(o.port));
            return 1;
        }
    }
    if (o.ws_port != 0) {
        ws = ants::net::WsListener::listen(o.ws_port, !o.ws_any_interface);
        if (!ws) {
            std::fprintf(stderr, "cannot listen on WebSocket port %u\n", static_cast<unsigned>(o.ws_port));
            return 1;
        }
    }
    if (o.ctl_port != 0) {
        http = ants::ctl::HttpServer::listen(o.ctl_port, secret, !o.ctl_any_interface);
        if (!http) {
            std::fprintf(stderr, "cannot listen on control port %u\n", static_cast<unsigned>(o.ctl_port));
            return 1;
        }
    }
    if (o.replay_port != 0) {
        replay_http = ants::ctl::HttpServer::listen_public(o.replay_port, !o.replay_any_interface);
        if (!replay_http) {
            std::fprintf(stderr, "cannot listen on replay port %u\n", static_cast<unsigned>(o.replay_port));
            return 1;
        }
    }
    if (o.submit_port != 0) {
        ants::ctl::DoorOptions door;
        door.max_body_bytes = ants::server::SubmissionApi::max_body();
        door.request_timeout_ms = 30000;                           // (an Extended map is up to 4 MiB: a slow line needs its time, and only four such bodies are read at once)
        door.body_limit = [](const ants::ctl::HttpRequest& head) { return ants::server::SubmissionApi::body_limit(head); };
        submit_http = ants::ctl::HttpServer::listen_door(o.submit_port, door, !o.submit_any_interface);
        if (!submit_http) {
            std::fprintf(stderr, "cannot listen on submission port %u\n", static_cast<unsigned>(o.submit_port));
            return 1;
        }
    }
    if (!tcp && !ws) {
        std::fprintf(stderr, "no game port: give --port or --ws-port\n");
        return 2;
    }

    ants::server::ServerLimits limits;
    limits.max_rooms = o.max_rooms;
    if (o.demo_rooms >= o.max_rooms) {
        std::fprintf(stderr, "--demo-rooms must be smaller than --max-rooms (%zu), so that rooms made by the control interface keep their places\n", o.max_rooms);
        return 2;
    }
    limits.demo_rooms = o.demo_rooms;
    limits.demo_map = o.demo_map;
    limits.demo_maps = o.demo_maps;
    // Lobby rooms (protocol 16) have a pool of their own beside the public rooms, and the two together leave a place for the rooms of the control interface. A value that is given is kept or refused; without one
    // a server that has public rooms and keeps seats offers the default number, as many of it as --max-rooms leaves room for (none when it leaves none).
    if (o.demo_lobbies_given && o.demo_lobbies > 0) {
        if (o.demo_rooms == 0) {
            std::fprintf(stderr, "--demo-lobbies needs --demo-rooms (and --demo-map)\n");
            return 2;
        }
        if (!o.reconnect) {
            std::fprintf(stderr, "--demo-lobbies needs reconnect: a lobby room holds the seat of a link that dropped (it cannot go with --no-reconnect)\n");
            return 2;
        }
        if (o.demo_rooms + o.demo_lobbies >= o.max_rooms) {
            std::fprintf(stderr, "--demo-rooms (%zu) and --demo-lobbies (%zu) together must be smaller than --max-rooms (%zu), so that rooms made by the control interface keep their places\n", o.demo_rooms, o.demo_lobbies, o.max_rooms);
            return 2;
        }
        limits.demo_lobbies = o.demo_lobbies;
    } else if (!o.demo_lobbies_given && o.demo_rooms > 0 && o.reconnect) {
        limits.demo_lobbies = std::min(kDefaultDemoLobbies, o.max_rooms - o.demo_rooms - 1);      // (demo_rooms < max_rooms was checked above)
    }
    limits.reconnect = o.reconnect;
    limits.hold_vote_ms = static_cast<uint32_t>(o.hold_vote_s * 1000);
    limits.max_pause_ms = static_cast<uint32_t>(o.max_pause_s * 1000);
    limits.max_catch_up_ms = static_cast<uint32_t>(o.max_catch_up_s * 1000);
    limits.resume_countdown_ms = static_cast<uint32_t>(o.resume_countdown_s * 1000);
    limits.countdown_s = static_cast<uint32_t>(o.start_countdown_s);
    limits.room_log_bytes = static_cast<size_t>(o.log_mb) * 1024u * 1024u;
    ants::server::MapStore store{o.maps_dir, o.community_maps_dir};
    {
        std::vector<std::string> map_notes;
        const size_t listed = store.scan(&map_notes);                 // (every map is listed and hashed once, now; GET /maps lists them, the lobby may choose any of them)
        log(std::to_string(listed) + " maps listed" + (o.community_maps_dir.empty() ? std::string() : " (maps folder and community folder " + o.community_maps_dir + ")"));
        for (const std::string& line : map_notes) log("maps: " + line);
    }
    if (o.demo_maps_given && o.demo_rooms == 0) {
        std::fprintf(stderr, "--demo-maps needs --demo-rooms (and --demo-map)\n");
        return 2;
    }
    if (o.demo_rooms > 0) {
        ants::server::MapEntry entry;
        std::string why;
        if (o.demo_map.empty() || !store.find(o.demo_map, entry, &why)) {
            std::fprintf(stderr, "--demo-rooms needs --demo-map with a map of the maps folder%s%s\n", why.empty() ? "" : ": ", why.c_str());
            return 2;
        }
        for (const std::string& name : o.demo_maps) {
            why.clear();
            if (!store.find(name, entry, &why)) {
                std::fprintf(stderr, "--demo-maps: '%s' is not a map of the maps folder%s%s\n", name.c_str(), why.empty() ? "" : ": ", why.c_str());
                return 2;
            }
        }
    }
    ants::server::RoomManager rooms{std::move(store), limits};
    if (o.demo_rooms > 0) {
        std::string chooseable;
        for (const std::string& name : o.demo_maps) chooseable += (chooseable.empty() ? "" : ", ") + name;
        log("public rooms on: up to " + std::to_string(o.demo_rooms) + " at a time, made by a Hello's create block (its seats and teams), map " + o.demo_map + (chooseable.empty() ? std::string() : " unless the block names one of " + chooseable));
        log(limits.demo_lobbies > 0 ? "lobby rooms on: up to " + std::to_string(limits.demo_lobbies) + " waiting at a time, made by a lobby page's Hello (a lobby holds no public place until its leader starts the match)"
                                    : std::string("lobby rooms off") + (o.demo_lobbies_given ? " (--demo-lobbies 0)" : !o.reconnect ? " (--no-reconnect)" : " (--max-rooms leaves no place for them beside --demo-rooms)"));
    }

    if (ws) {                                                    // GET /busy on the WebSocket port: how many matches run (a deploy waits for none), no name, no code, no secret
        ws->set_status("/busy", [&rooms]() {
            const ants::server::BusyCounts b = rooms.busy(now_ms());
            return "{\"matches\":" + std::to_string(b.matches) + ",\"players\":" + std::to_string(b.players) + "}";
        });
    }
    // The numbers of the front page (site_stats.hpp): the games that ended on this server and the single-player games that browsers report. They are kept in the database (opened below), else in
    // the results folder when there is one, else in memory.
    ants::server::SiteStats stats;
    if (ws) {                                                    // GET /stats (numbers only) and POST /stats/local (a browser tells that a single-player game began: counted, answered 204)
        ws->add_status("/stats", [&rooms, &stats]() { return stats.json(rooms.busy(now_ms())); });
        ws->set_post("/stats/local", [&stats]() { stats.count_local(); });
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    log(std::string("ants_server ") + std::string(ants::VERSION_STRING) + " build " + std::string(ants::BUILD_ID) + " (network protocol " + std::to_string(ants::net::kProtocolVersion) + "), maps in " + o.maps_dir);
    // The database (docs/SERVER.md "The database"): the file --db, else ants.db in the results folder, unless switched off. It is opened, brought to the schema of this program and kept open until the server
    // stops. Whatever cannot be done (a file that is no database, a newer one, a read-only folder) stops the server: the file is left as it is, and --no-db starts the server without one.
    ants::db::Database database;
    {
        std::string path = o.db_file;
        if (o.no_db) {
            log("database is off (--no-db)");
        } else if (path.empty() && o.results_dir.empty()) {
            log("database is off: no --results-dir or --db to keep it in");
        } else {
            if (path.empty()) path = (std::filesystem::path(o.results_dir) / "ants.db").u8string();
            std::filesystem::create_directories(std::filesystem::u8path(path).parent_path(), ec);          // (an empty parent, a file in the current folder, is fine)
            int version = 0;
            std::string journal;
            const ants::server::PrivateCreation private_files;                           // (the database, and the -wal and -shm that SQLite makes after it, are for this user only)
            const ants::db::Error err = ants::db::open_and_migrate(database, path, ants::server::db_migrations(), ants::db::OpenOptions(), &version, &journal);
            if (!err.ok()) {
                std::fprintf(stderr, "database %s: %s\n(the file was not changed; --no-db starts the server without a database)\n", path.c_str(), err.message.c_str());
                return 1;
            }
            log("database " + path + ": schema version " + std::to_string(version) + " (journal mode " + journal + ")" +
                (journal == "wal" ? std::string() : std::string("; the file system gave no WAL, the rollback journal is used")));
            for (const std::string& line : ants::server::tighten_private_files(path, o.db_file.empty() ? o.results_dir : std::string())) log(line);          // (the accounts' hashes are in this file)
        }
    }
    {
        const std::string legacy = o.results_dir.empty() ? std::string() : (std::filesystem::path(o.results_dir) / ants::server::SiteStats::kFileName).string();
        if (database.is_open()) stats.open_db(database, legacy);               // (a site-stats.json of an older server is read into the database once)
        else if (!legacy.empty()) stats.open(legacy);                          // --no-db: the file, as it always was
    }
    // Restart records (restart_record.hpp): in --restart-dir, else in the folder "restart" of the results folder, unless switched off. An explicit folder that cannot be used stops the server
    // (the operator asked for it); the default one is given up with a line in the log.
    {
        ants::server::RestartConfig rc;
        if (!o.no_restart_records) {
            if (!o.restart_dir.empty()) rc.dir = o.restart_dir;
            else if (!o.results_dir.empty()) rc.dir = (std::filesystem::path(o.results_dir) / "restart").string();
        }
        rc.identity.game_version = std::string(ants::VERSION_STRING);
        rc.identity.protocol = ants::net::kProtocolVersion;
        rc.identity.build_id = std::string(ants::BUILD_ID);
        rc.budget_bytes = static_cast<uint64_t>(o.restart_budget_mb) * 1024u * 1024u;
        rc.restart_vote_after_ms = static_cast<uint32_t>(o.restart_vote_s * 1000);
        std::string why;
        if (!rc.dir.empty() && !rooms.enable_restart_records(rc, why)) {
            if (!o.restart_dir.empty()) {
                std::fprintf(stderr, "%s\n", why.c_str());
                return 1;
            }
            log("restart records are off: " + why);
        }
        if (rooms.restart_store() != nullptr) {
            log("restart records in " + rc.dir + ": a running match of a room that holds seats survives a restart (budget " + std::to_string(o.restart_budget_mb) + " MiB, the others may vote on a seat that has not come back after " +
                std::to_string(o.restart_vote_s) + " s)" + (o.reconnect ? std::string() : std::string("; no room holds seats unless its specification says so (--no-reconnect), so none is kept now")));
        } else if (o.no_restart_records) {
            log("restart records are off (--no-restart-records): a running match ends with the server");
        } else if (rc.dir.empty()) {
            log("restart records are off: no --results-dir or --restart-dir to keep them in: a running match ends with the server");
        }
    }
    // Replays (replay_store.hpp): in --replays-dir, else in the folder "replays" of the results folder, unless switched off. An explicit folder that cannot be used stops the server (the operator asked for it);
    // the default one is given up with a line in the log.
    {
        ants::server::ReplayConfig rc;
        if (!o.no_replays) {
            if (!o.replays_dir.empty()) rc.dir = o.replays_dir;
            else if (!o.results_dir.empty()) rc.dir = (std::filesystem::path(o.results_dir) / "replays").string();
        }
        rc.keep_days = static_cast<uint32_t>(o.replays_days);
        rc.max_bytes = static_cast<uint64_t>(o.replays_max_mb) * 1024u * 1024u;
        rc.game_version = std::string(ants::VERSION_STRING);
        rc.build_id = std::string(ants::BUILD_ID);
        std::string why;
        if (!rc.dir.empty() && !rooms.enable_replays(rc, o.replay_demo, why)) {
            if (!o.replays_dir.empty()) {
                std::fprintf(stderr, "%s\n", why.c_str());
                return 1;
            }
            log("replays are off: " + why);
        }
        for (const std::string& line : rooms.take_notices()) log(line);                 // (what the store found in its folder)
        if (const ants::server::ReplayStore* kept = rooms.replay_store()) {                      // (the log says what the store was given, not what was asked for)
            log("replays kept in " + kept->config().dir + " for " + std::to_string(kept->config().keep_days) + " days, at most " + std::to_string(kept->config().max_bytes / (1024 * 1024)) + " MiB; " +
                (rooms.replays_include_demo() ? "the matches of demo rooms are kept too (--replay-demo)" : "the matches of demo rooms are not kept (--replay-demo keeps them)"));
        } else if (o.no_replays) {
            log("replays are off (--no-replays): no match is kept");
        } else if (rc.dir.empty()) {
            log("replays are off: no --results-dir or --replays-dir to keep them in");
        }
        if (o.bot_match_every_min > 0) {
            if (rooms.replay_store() != nullptr) {
                rooms.enable_bot_matches(static_cast<uint32_t>(o.bot_match_every_min), now_ms());
                log("the server plays a match of computer players every " + std::to_string(o.bot_match_every_min) + " minutes, the first one in a minute (--bot-match-every-min)");
            } else {
                log("--bot-match-every-min is ignored: the server keeps no replays, and a match that nobody could look at again is not played");
            }
        }
    }
    // The match history (history_store.hpp): in the database, unless switched off. It counts the matches that the replay store keeps. Whatever is wrong with it only switches the history off, with a line in the log.
    if (o.no_history) {
        log("the match history is off (--no-history)");
    } else if (!database.is_open()) {
        log("the match history is off: the server has no database (see --db and --no-db)");
    } else {
        std::string history_why;
        if (!rooms.enable_history(database, history_why)) log("the match history is off: " + history_why);
        for (const std::string& line : rooms.take_notices()) log(line);
        if (rooms.history_store() != nullptr) {
            log(std::string("match history kept for good in the database") + (rooms.replay_store() != nullptr ? "; the matches of the replay store are counted while no match runs" : "; the server keeps no replays, so nothing is added"));
        }
    }
    // The map submissions (submissions.hpp, docs/MAP_SUBMISSIONS.md): the store needs the database and a folder; the owner's page needs two settings of the environment. What is missing switches a part off with a line in
    // the log; a door that was asked for and cannot work stops the server (the operator asked for it).
    std::unique_ptr<ants::server::SubmissionStore> submissions;
    std::unique_ptr<ants::server::AdminAccounts> accounts;
    std::unique_ptr<ants::server::SubmissionApi> submit_api;
    if (submit_http) {
        if (!database.is_open()) {
            std::fprintf(stderr, "--submit-port needs the database (see --db and --no-db)\n");
            return 1;
        }
        ants::server::SubmissionConfig sc;
        sc.dir = !o.submissions_dir.empty() ? o.submissions_dir : o.results_dir.empty() ? std::string() : (std::filesystem::path(o.results_dir) / "submissions").string();
        if (sc.dir.empty()) {
            std::fprintf(stderr, "--submit-port needs --results-dir or --submissions-dir to keep the maps in\n");
            return 1;
        }
        sc.max_waiting = o.submit_max_waiting;
        sc.per_address_per_day = o.submit_per_address;
        submissions = std::make_unique<ants::server::SubmissionStore>(database, sc, [&rooms](const std::string& key) { return rooms.store().library_has_name(key); });
        std::string why;
        if (!submissions->open(static_cast<int64_t>(std::time(nullptr)), why)) {
            std::fprintf(stderr, "map submissions: %s\n", why.c_str());
            return 1;
        }
        accounts = std::make_unique<ants::server::AdminAccounts>(database);
        if (!accounts->open(why)) {
            std::fprintf(stderr, "accounts: %s\n", why.c_str());
            return 1;
        }
        ants::server::SubmissionApiConfig ac;
        if (const char* session_key = std::getenv("ANTS_ADMIN_SESSION_SECRET")) ac.session_secret = session_key;
        const char* plain_http = std::getenv("ANTS_ADMIN_ALLOW_HTTP");
        ac.cookie_secure = !(plain_http != nullptr && std::string(plain_http) == "1");
        ac.trust_client_header = !o.submit_ignore_client_header;
        submit_api = std::make_unique<ants::server::SubmissionApi>(*submissions, *accounts, ac);
        auto refresh = [&rooms, &submissions]() {
            rooms.mutable_store().set_approved(submissions->approved_maps());
            rooms.mutable_store().set_test_maps(submissions->test_maps(static_cast<int64_t>(std::time(nullptr))));
        };
        refresh();
        submissions->take_approved_changed();
        submit_api->set_on_approved_changed(refresh);
        log("map submissions on: files in " + sc.dir + ", at most " + std::to_string(sc.max_waiting) + " waiting and " + std::to_string(sc.per_address_per_day) + " a day from one address; " +
            (submit_api->owner_api_on() ? "the owner's review page is on" + std::string(ac.cookie_secure ? "" : " (ANTS_ADMIN_ALLOW_HTTP: the cookie is not marked Secure)") : "the owner's review page is OFF (ANTS_ADMIN_SESSION_SECRET, 32 characters or more, is needed)") +
            "; " + std::to_string(submissions->approved_maps().size()) + " approved maps in the lobby's list");
        for (const std::string& line : submissions->take_notices()) log(line);
        for (const std::string& line : accounts->take_notices()) log(line);
    }
    if (o.reconnect) {
        log("rooms hold the seat of a player whose connection is lost (the default; --no-reconnect turns it off): a vote after " + std::to_string(o.hold_vote_s) + " s away, the pauses of a match capped at " + std::to_string(o.max_pause_s) +
            " s, a catch-up of at most " + std::to_string(o.max_catch_up_s) + " s per absence, a resume countdown of " + std::to_string(o.resume_countdown_s) + " s, a turn log of at most " +
            std::to_string(o.log_mb) + " MiB per room");
    } else {
        log("rooms hold no seats (--no-reconnect): a player whose connection is lost is dropped at once, unless a room's specification says \"reconnect\": true");
    }
    log(o.start_countdown_s > 0 ? "a room with two or more people holds the first turn of its match " + std::to_string(o.start_countdown_s) + " s, with the chat open (--start-countdown-seconds)"
                                : "no countdown before the first turn (--start-countdown-seconds 0): the plain 5 s dialog");
    if (tcp) log("TCP game port " + std::to_string(tcp->port()) + (o.is_public ? " (all interfaces)" : " (this machine only)"));
    if (ws) log("WebSocket port " + std::to_string(ws->port()) + (o.ws_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only: put a TLS proxy in front)"));
    if (submit_http) log("map submissions on port " + std::to_string(submit_http->port()) + (o.submit_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only)") + ": POST /submissions, GET /submissions, GET /submissions/<id>, POST /submissions/<id>/withdraw and /admin/api/..., no secret");
    if (http) log("control interface on port " + std::to_string(http->port()) + (o.ctl_any_interface ? " (all interfaces: the host must restrict it, bearer secret)" : " (this machine only, bearer secret)"));
    if (replay_http) {
        log("public replays on port " + std::to_string(replay_http->port()) + (o.replay_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only)") +
            ": GET /replays, GET /replays/<file>, GET /history, GET /history/<id>, GET /live, GET /live/<id> and GET /maps, read only, no secret" + (rooms.replay_store() != nullptr ? std::string() : std::string("; the server keeps no replays, so it answers 404")));
    }
    if (http) {
        using ants::server::SecretSource;
        if (secret_info.source == SecretSource::File) {
            log("control secret read from " + secret_info.path);
        } else if (secret_info.source == SecretSource::Environment) {
            log("control secret from the environment variable ANTS_SERVER_SECRET");
        }                                          // made now: shown above, once
    }

    if (rooms.restart_store() != nullptr) {                    // the matches that were running when the server stopped come back, paused until their players do: the records are judged here, their replays run
        rooms.restore_rooms(now_ms(), []() { return g_stop.load(); });         // in slices from rooms.update() below, so that the server serves meanwhile (a stop that comes meanwhile leaves every record on disk)
        for (const std::string& line : rooms.take_notices()) log(line);
    }
    uint32_t last_submission_upkeep_ms = now_ms();
    while (!g_stop) {
        const uint32_t now = now_ms();
        if (tcp) {
            for (int i = 0; i < 32; ++i) {
                auto c = tcp->accept();
                if (!c) break;
                const std::string address = host_part(c->peer());
                rooms.add_connection(std::move(c), address, now);
            }
        }
        if (ws) {
            for (int i = 0; i < 32; ++i) {
                auto c = ws->accept();
                if (!c) break;
                const std::string address = host_part(c->peer());
                rooms.add_connection(std::move(c), address, now);
            }
        }
        rooms.update(now);
        if (http) http->update(now, [&](const ants::ctl::HttpRequest& request) { return ants::server::handle_control(rooms, request, now); });
        if (submit_http && submit_api) {
            submit_http->update(now, [&](const ants::ctl::HttpRequest& request) { return submit_api->handle(request, static_cast<int64_t>(std::time(nullptr)), now); });
            if (static_cast<uint32_t>(now - last_submission_upkeep_ms) >= 3600u * 1000u) {                 // (once an hour: the files of declined maps, old rows and counters)
                last_submission_upkeep_ms = now;
                submissions->maintenance(static_cast<int64_t>(std::time(nullptr)));
                if (submissions->take_approved_changed()) {
                    rooms.mutable_store().set_approved(submissions->approved_maps());
                    rooms.mutable_store().set_test_maps(submissions->test_maps(static_cast<int64_t>(std::time(nullptr))));
                }
                if (accounts) {
                    accounts->maintenance(static_cast<int64_t>(std::time(nullptr)));
                    for (const std::string& line : accounts->take_notices()) log(line);          // (a refused write of the accounts: never a password, a name or an address)
                }
            }
            for (const std::string& line : submissions->take_notices()) log(line);
        }
        if (replay_http) replay_http->update(now, [&](const ants::ctl::HttpRequest& request) { return ants::server::handle_public_replays(rooms, request); });
        for (const ants::server::RoomStatus& s : rooms.take_ended(now)) {
            stats.count_ended(s);                                  // (a match that ran 600 ticks counts, public rooms too; a shorter one, or a room that never began, counts nothing)
            const bool demo = s.public_room;
            if (demo && s.ticks == 0) continue;                    // a public room that nobody completed: no line, no file (a peer chooses these codes, nothing may pile up)
            std::string held;                                      // a room that held seats says what came of it (never a key)
            if (s.reconnect) {
                held = ", paused " + std::to_string(s.paused_s) + " s, " + std::to_string(s.rejoins) + " back, dropped " + std::to_string(s.drops_by_vote) + " by vote and " + std::to_string(s.drops_by_cap) + " by the cap, " + std::to_string(s.rejoins_refused) +
                       " refused by a budget";
            }
            if (s.replay_kept) held += ", kept as " + s.replay_file;                       // (the replay of the match: its file name is the map and the end time, no code and no name of a person)
            log("room " + s.code + " " + ants::server::room_state_name(s.state) + ": " + s.reason + " (map " + s.map + ", " + std::to_string(s.ticks) + " ticks" + held + ")");
            if (!o.results_dir.empty() && !demo) {
                std::ofstream out(std::filesystem::path(o.results_dir) / (s.code + ".json"), std::ios::binary | std::ios::trunc);
                out << ants::ctl::to_json(ants::server::status_to_json(s)) << "\n";
            }
        }
        for (const std::string& line : rooms.take_notices()) log(line);          // (a record that the disk refused, ...)
        stats.save_if_due();
        for (const std::string& line : stats.take_notices()) log(line);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    log("stopping");
    // The records of the running rooms are made durable and stay where they are (a record is deleted when its room is over, never because the server stops); the other rooms are closed, as before.
    const auto stopping = std::chrono::steady_clock::now();
    const size_t kept = rooms.shutdown(now_ms());
    for (const std::string& line : rooms.take_notices()) log(line);
    stats.save();                                                // (the counters reach the file whatever the last ten seconds did)
    for (const std::string& line : stats.take_notices()) log(line);
    if (rooms.restart_store() != nullptr) {
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stopping).count();
        log("stopped: " + std::to_string(kept) + " restart record(s) made durable and kept (" + std::to_string(took) + " ms)");
    }
    rooms.disable_history();                                     // (it lets go of the database)
    if (database.is_open()) {
        const ants::db::Error err = database.close();
        if (!err.ok()) log("database: closing failed: " + err.message);
    }
    return 0;
}
