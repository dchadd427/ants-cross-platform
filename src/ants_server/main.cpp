// ants_server: the dedicated game server. A headless program: it hosts many rooms, each a host without a seat that runs the match as the referee (docs/NETWORK_PORT.md).
//
//   ants_server --maps DIR [--port 4001] [--ws-port 4002] [--ctl-port 4010] [--public] [--ws-any-interface] [--ctl-any-interface] [--results-dir DIR] [--secret-file PATH] [--max-rooms N]
//               [--reconnect | --no-reconnect] [--hold-vote-seconds N] [--max-pause-seconds N] [--max-catch-up-seconds N]
//               [--resume-countdown-seconds N] [--log-mb N]
//               [--restart-dir DIR | --no-restart-records] [--restart-vote-seconds N] [--restart-budget-mb N]
//               [--replays-dir DIR | --no-replays] [--replay-demo] [--replays-days N] [--replays-max-mb N] [--replay-port N] [--replay-any-interface]
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
//   --results-dir DIR  every ended room writes <code>.json there, and the site statistics (the counters of GET /stats) are kept in site-stats.json (without it they live in memory)
//   --secret-file PATH where the server keeps the control secret that it makes when ANTS_SERVER_SECRET is not set (default: control-secret in the results folder)
//   --max-rooms N      the most rooms at a time (default 256)
//   --demo-rooms N     for a public test page: a Hello that carries a create block (network protocol 15: the map, the seats 2 - 4, the teams and whether a full room waits for its leader's
//                      START) for a room that does not exist makes it, at most N public rooms at a time. The code is only a name (the game makes 6 random letters and numbers). N is 1 to
//                      --max-rooms - 1 (255 by default); the option is left out to switch public rooms off (the default): 0 and more than that stop the server at startup
//   --demo-map NAME    the map of the public rooms whose block names none that the server offers (a file name of the maps folder; required with --demo-rooms)
//   --demo-maps LIST   the maps a create block may choose, file names of the maps folder separated by commas (blanks around a name are dropped; the block's map is compared
//                      with them in any case); a block that names another map (or none) gets --demo-map (needs --demo-rooms). A public room waits ten minutes for its players.
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
//   --replay-demo      keep the matches of the demo rooms too (the games of the front page, bots only included); off by default, so that only the rooms of the control interface are kept
//   --replays-days N   a replay is deleted N days after its match ended (1 - 3650, default 30)
//   --replays-max-mb N all the replays together may take N MiB, the oldest are deleted first (1 - 4096, default 100); by default the folder is on the volume that also holds the control secret and the
//                      restart records, so a match is also not kept when the disk has less than 256 MiB free, or when 120 matches were kept in the last hour
//   --replay-port N    a PUBLIC, read-only door for the list and the files of the replays (GET /replays, GET /replays/<file>; no secret, nothing else answers): 0 = off, the default. This machine only;
//                      the site's reverse proxy passes the two paths to it (docker/nginx.conf). The control interface lists and gives the files too (all of them), and deletes them, behind its secret whatever this says
//   --replay-any-interface
//                      the replay port listens on every interface (for a container only, like --ws-any-interface)
//   --version, --help
//
// SIGTERM and SIGINT stop the server within a moment: the records of the running rooms are made durable (fsync) and left where they are (docker stop: give the container a grace period, the stack
// files set 15 s), the other rooms are closed. A record is deleted when its room is over, never because the server stops.
//
// The control interface's secret comes from the environment (ANTS_SERVER_SECRET), never from the command line (a command line is visible to every user). Without
// it the server makes a random secret the first time it starts and keeps it in the secret file (POSIX: for its owner only, mode 600; Windows: the file takes the
// permissions of its folder), so a container needs no setup: the secret is printed once, the moment it is made (before anything else can stop the server); later it is read
// from the file (docker exec <container> cat /results/control-secret). A secret in the environment wins. A file that exists is used as it is (secret.hpp).

#include <atomic>
#include <chrono>
#include <csignal>
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
#include "ants_server/control.hpp"
#include "ants_server/restart_record.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_server/secret.hpp"
#include "ants_server/site_stats.hpp"

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

struct Options {
    std::string maps_dir;
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
    bool reconnect{ants::server::kReconnectByDefault};
    long hold_vote_s{30};
    long max_pause_s{1800};
    long max_catch_up_s{300};
    long resume_countdown_s{10};
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
};

void usage(FILE* to) {
    std::fprintf(to,
                 "usage: ants_server --maps DIR [--port 4001] [--ws-port N] [--ctl-port N] [--public] [--ws-any-interface] [--ctl-any-interface]\n"
                 "                    [--results-dir DIR] [--secret-file PATH] [--max-rooms N]\n"
                 "                    [--demo-rooms N --demo-map NAME [--demo-maps A.LVL,B.LVL,...]]\n"
                 "                    [--reconnect | --no-reconnect] [--hold-vote-seconds 5-3600] [--max-pause-seconds 60-86400]\n"
                 "                    [--max-catch-up-seconds 10-3600] [--resume-countdown-seconds 0-60] [--log-mb 1-256]\n"
                 "                    [--restart-dir DIR | --no-restart-records] [--restart-vote-seconds 30-3600] [--restart-budget-mb 1-4096]\n"
                 "                    [--replays-dir DIR | --no-replays] [--replay-demo] [--replays-days 1-3650] [--replays-max-mb 1-4096]\n"
                 "                    [--replay-port N] [--replay-any-interface]\n"
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
        } else if (a == "--hold-vote-seconds" || a == "--max-pause-seconds" || a == "--max-catch-up-seconds" || a == "--resume-countdown-seconds" || a == "--log-mb" || a == "--restart-vote-seconds" ||
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
            else if (a == "--restart-vote-seconds") { lo = 30; hi = 3600; target = &o.restart_vote_s; }
            else if (a == "--restart-budget-mb") { lo = 1; hi = 4096; target = &o.restart_budget_mb; }
            else if (a == "--replays-days") { lo = 1; hi = 3650; target = &o.replays_days; }
            else if (a == "--replays-max-mb") { lo = 1; hi = 4096; target = &o.replays_max_mb; }
            if (end == text || *end != '\0' || n < lo || n > hi) {
                std::fprintf(stderr, "%s takes a whole number from %ld to %ld\n", a.c_str(), lo, hi);
                return 2;
            }
            *target = n;
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
    std::error_code ec;
    if (!std::filesystem::is_directory(o.maps_dir, ec)) {
        std::fprintf(stderr, "the maps folder '%s' does not exist\n", o.maps_dir.c_str());
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
    limits.reconnect = o.reconnect;
    limits.hold_vote_ms = static_cast<uint32_t>(o.hold_vote_s * 1000);
    limits.max_pause_ms = static_cast<uint32_t>(o.max_pause_s * 1000);
    limits.max_catch_up_ms = static_cast<uint32_t>(o.max_catch_up_s * 1000);
    limits.resume_countdown_ms = static_cast<uint32_t>(o.resume_countdown_s * 1000);
    limits.room_log_bytes = static_cast<size_t>(o.log_mb) * 1024u * 1024u;
    ants::server::MapStore store{o.maps_dir};
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
    }

    if (ws) {                                                    // GET /busy on the WebSocket port: how many matches run (a deploy waits for none), no name, no code, no secret
        ws->set_status("/busy", [&rooms]() {
            const ants::server::BusyCounts b = rooms.busy(now_ms());
            return "{\"matches\":" + std::to_string(b.matches) + ",\"players\":" + std::to_string(b.players) + "}";
        });
    }
    // The numbers of the front page (site_stats.hpp): the games that ended on this server and the single-player games that browsers report, kept in the results folder when there is one
    ants::server::SiteStats stats;
    if (!o.results_dir.empty()) stats.open((std::filesystem::path(o.results_dir) / ants::server::SiteStats::kFileName).string());
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
    }
    if (o.reconnect) {
        log("rooms hold the seat of a player whose connection is lost (the default; --no-reconnect turns it off): a vote after " + std::to_string(o.hold_vote_s) + " s away, the pauses of a match capped at " + std::to_string(o.max_pause_s) +
            " s, a catch-up of at most " + std::to_string(o.max_catch_up_s) + " s per absence, a resume countdown of " + std::to_string(o.resume_countdown_s) + " s, a turn log of at most " +
            std::to_string(o.log_mb) + " MiB per room");
    } else {
        log("rooms hold no seats (--no-reconnect): a player whose connection is lost is dropped at once, unless a room's specification says \"reconnect\": true");
    }
    if (tcp) log("TCP game port " + std::to_string(tcp->port()) + (o.is_public ? " (all interfaces)" : " (this machine only)"));
    if (ws) log("WebSocket port " + std::to_string(ws->port()) + (o.ws_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only: put a TLS proxy in front)"));
    if (http) log("control interface on port " + std::to_string(http->port()) + (o.ctl_any_interface ? " (all interfaces: the host must restrict it, bearer secret)" : " (this machine only, bearer secret)"));
    if (replay_http) {
        log("public replays on port " + std::to_string(replay_http->port()) + (o.replay_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only)") +
            ": GET /replays and GET /replays/<file>, read only, no secret" + (rooms.replay_store() != nullptr ? std::string() : std::string("; the server keeps no replays, so it answers 404")));
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
    return 0;
}
