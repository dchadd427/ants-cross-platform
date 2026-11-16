// The history service (docs/HISTORY_SERVICE.md): counts the game server's recordings into a history of its own and serves the public pages of the replays and the match history.
//
//   ants_history --db FILE --recordings DIR --maps DIR --port N
//                [--community-maps DIR] [--game-db FILE --submissions-dir DIR] [--any-interface]
//                [--replays-days N] [--replays-max-mb N] [--settle-seconds N] [--look-ms N] [--pace-factor N] [--version]
//
//   --db FILE            the service's own database (made when it is not there; the match history and nothing else)
//   --recordings DIR     the folder that the game server keeps its recordings in (its --replays-dir); only read, nothing in it is deleted
//   --maps DIR           the original game's maps (the game server's --maps): a recording is played on its map
//   --community-maps DIR the players' maps (the game server's --community-maps)
//   --game-db FILE       the game server's database (its --db), opened read only: its match history is copied into the service's file at the first start, and the approved maps of the players are looked up in it
//   --submissions-dir DIR the game server's folder of submitted maps (its --submissions-dir), where the files of the approved maps are; needed with --game-db
//   --port N             the port of the public pages (0: any free port, the log says which); they listen on this machine only unless --any-interface (for a container only)
//   --replays-days N     what the lists say a recording is kept for (1 - 3650, default 30): the game server's --replays-days, which does the deleting
//   --replays-max-mb N   the game server's --replays-max-mb (1 - 4096, default 100), kept so that the store is made as the game server's
//   --settle-seconds N   a recording that changed less than this many seconds ago is not trusted yet (0 - 60, default 1); a new recording is counted this late at the least
//   --look-ms N          how often the folder is looked at (50 - 10000, default 250)
//   --pace-factor N      after a recording the counter waits N times what it took (0 - 20, default 1), so that a backlog does not take a whole core
//
// Exit status: 0 after SIGINT or SIGTERM, 1 when the service cannot start (one message says why), 2 for a wrong command line.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "ants_app/version.hpp"
#include "ants_ctl/http.hpp"
#include "ants_history/history_service.hpp"

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

uint32_t now_ms() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

void say_line(const std::string& line) {
    std::fprintf(stderr, "[ants_history %u] %s\n", now_ms() / 1000, line.c_str());
    std::fflush(stderr);                                 // (the log of a container or a test is read while the program runs)
}

void usage(FILE* to) {
    std::fprintf(to,
                 "usage: ants_history --db FILE --recordings DIR --maps DIR --port N [--community-maps DIR] [--game-db FILE --submissions-dir DIR] [--any-interface]\n"
                 "                    [--replays-days 1-3650] [--replays-max-mb 1-4096] [--settle-seconds 0-60] [--look-ms 50-10000] [--pace-factor 0-20] [--version]\n");
}

bool whole_number(const char* text, long lo, long hi, long& out) {
    char* end = nullptr;
    const long n = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || n < lo || n > hi) return false;
    out = n;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    ants::history::ServiceConfig cfg;
    long port = -1;
    bool any_interface = false;
    long replays_days = 30, replays_max_mb = 100, settle_s = 1, look_ms = 250, pace_factor = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s takes a value\n", name);
                usage(stderr);
                std::exit(2);
            }
            return argv[++i];
        };
        const auto number = [&](const char* name, long lo, long hi, long& out) {
            if (!whole_number(value(name), lo, hi, out)) {
                std::fprintf(stderr, "%s takes a whole number from %ld to %ld\n", name, lo, hi);
                std::exit(2);
            }
        };
        if (a == "--help" || a == "-h") {
            usage(stdout);
            return 0;
        } else if (a == "--version") {
            std::printf("ants_history %s build %s\n", std::string(ants::VERSION_STRING).c_str(), std::string(ants::BUILD_ID).c_str());
            return 0;
        } else if (a == "--db") {
            cfg.db_path = value("--db");
        } else if (a == "--recordings") {
            cfg.recordings_dir = value("--recordings");
        } else if (a == "--maps") {
            cfg.maps_dir = value("--maps");
        } else if (a == "--community-maps") {
            cfg.community_dir = value("--community-maps");
        } else if (a == "--game-db") {
            cfg.game_db_path = value("--game-db");
        } else if (a == "--submissions-dir") {
            cfg.submissions_dir = value("--submissions-dir");
        } else if (a == "--port") {
            number("--port", 0, 65535, port);
        } else if (a == "--any-interface") {
            any_interface = true;
        } else if (a == "--replays-days") {
            number("--replays-days", 1, 3650, replays_days);
        } else if (a == "--replays-max-mb") {
            number("--replays-max-mb", 1, 4096, replays_max_mb);
        } else if (a == "--settle-seconds") {
            number("--settle-seconds", 0, 60, settle_s);
        } else if (a == "--look-ms") {
            number("--look-ms", 50, 10000, look_ms);
        } else if (a == "--pace-factor") {
            number("--pace-factor", 0, 20, pace_factor);
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage(stderr);
            return 2;
        }
    }
    if (cfg.db_path.empty() || cfg.recordings_dir.empty() || cfg.maps_dir.empty() || port < 0) {
        std::fprintf(stderr, "--db, --recordings, --maps and --port are needed\n");
        usage(stderr);
        return 2;
    }
    cfg.keep_days = static_cast<uint32_t>(replays_days);
    cfg.max_bytes = static_cast<uint64_t>(replays_max_mb) * 1024u * 1024u;
    cfg.settle_s = static_cast<uint32_t>(settle_s);
    cfg.look_ms = static_cast<uint32_t>(look_ms);
    cfg.pace_factor = static_cast<uint32_t>(pace_factor);
    cfg.log = say_line;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    say_line(std::string("ants_history ") + std::string(ants::VERSION_STRING) + " build " + std::string(ants::BUILD_ID));
    auto http = ants::ctl::HttpServer::listen_public(static_cast<uint16_t>(port), !any_interface);
    if (!http) {
        std::fprintf(stderr, "cannot listen on port %ld\n", port);
        return 1;
    }
    ants::history::HistoryService service(cfg);
    std::string why;
    if (!service.start(why)) {
        std::fprintf(stderr, "%s\n", why.c_str());
        return 1;
    }
    say_line("public pages on port " + std::to_string(http->port()) + (any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only)") +
        ": GET /replays, GET /replays/<file>, GET /history, GET /history/<id> and GET /history/deaths, read only, no secret");
    while (!g_stop) {
        const uint32_t now = now_ms();
        http->update(now, [&](const ants::ctl::HttpRequest& request) { return service.handle(request, now); });
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    say_line("stopping");
    service.stop();
    return 0;
}
