// ants_server: the dedicated game server. A headless program: it hosts many rooms, each a host without a seat that runs the match as the referee (docs/NETWORK_PORT.md).
//
//   ants_server --maps DIR [--port 4001] [--ws-port 4002] [--ctl-port 4010] [--public] [--ws-any-interface] [--ctl-any-interface] [--results-dir DIR] [--secret-file PATH] [--max-rooms N]
//
//   --maps DIR         the maps folder (the .lvl files that rooms may use); required
//   --port N           the TCP port of native clients (0: none; default 4001); every interface with --public, else this machine only
//   --ws-port N        the WebSocket port of browsers and Electron behind a reverse proxy that ends TLS (0: none; default 0); this machine only
//   --ctl-port N       the control interface: HTTP + JSON on this machine only, with a bearer secret (0: none; default 0; needs the secret)
//   --public           the TCP game port accepts connections from other machines
//   --ws-any-interface, --ctl-any-interface
//                      the WebSocket / control port listens on every interface instead of the loopback address. For a container only: a port that is published
//                      from a container does not reach a program that listens on the container's loopback address. The host decides who can connect
//                      (docker run -p 127.0.0.1:4010:4010 ...); never use these on a machine without that protection, the control interface speaks plain HTTP.
//   --results-dir DIR  every ended room writes <code>.json there
//   --secret-file PATH where the server keeps the control secret that it makes when ANTS_SERVER_SECRET is not set (default: control-secret in the results folder)
//   --max-rooms N      the most rooms at a time (default 256)
//   --demo-rooms N     for a public test page: a Hello for a not yet existing room "demo-..." makes it (4 players, the --demo-map), at most N at a time. N is 1 to
//                      --max-rooms - 1 (255 by default); the option is left out to switch demo rooms off (the default): 0 and more than that stop the server at startup
//   --demo-map NAME    the map of the demo rooms (a file name of the maps folder; required with --demo-rooms)
//   --version, --help
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

#include "ants_app/version.hpp"
#include "ants_ctl/http.hpp"
#include "ants_net/tcp.hpp"
#include "ants_net/ws.hpp"
#include "ants_server/control.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_server/secret.hpp"

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
};

void usage(FILE* to) {
    std::fprintf(to,
                 "usage: ants_server --maps DIR [--port 4001] [--ws-port N] [--ctl-port N] [--public] [--ws-any-interface] [--ctl-any-interface]\n"
                 "                    [--results-dir DIR] [--secret-file PATH] [--max-rooms N]\n"
                 "                    [--demo-rooms N --demo-map NAME]\n"
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
            std::printf("ants_server %s (network protocol %u)\n", std::string(ants::VERSION_STRING).c_str(), static_cast<unsigned>(ants::net::kProtocolVersion));
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
        } else if (a == "--public") {
            o.is_public = true;
        } else if (a == "--ws-any-interface") {
            o.ws_any_interface = true;
        } else if (a == "--ctl-any-interface") {
            o.ctl_any_interface = true;
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
        } else if (a == "--demo-map") {
            o.demo_map = value("--demo-map");
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
    ants::server::MapStore store{o.maps_dir};
    if (o.demo_rooms > 0) {
        ants::server::MapEntry entry;
        std::string why;
        if (o.demo_map.empty() || !store.find(o.demo_map, entry, &why)) {
            std::fprintf(stderr, "--demo-rooms needs --demo-map with a map of the maps folder%s%s\n", why.empty() ? "" : ": ", why.c_str());
            return 2;
        }
    }
    ants::server::RoomManager rooms{std::move(store), limits};
    if (o.demo_rooms > 0) log("demo rooms on: up to " + std::to_string(o.demo_rooms) + " at a time, 4 players, map " + o.demo_map);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    log(std::string("ants_server ") + std::string(ants::VERSION_STRING) + " (network protocol " + std::to_string(ants::net::kProtocolVersion) + "), maps in " + o.maps_dir);
    if (tcp) log("TCP game port " + std::to_string(tcp->port()) + (o.is_public ? " (all interfaces)" : " (this machine only)"));
    if (ws) log("WebSocket port " + std::to_string(ws->port()) + (o.ws_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only: put a TLS proxy in front)"));
    if (http) log("control interface on port " + std::to_string(http->port()) + (o.ctl_any_interface ? " (all interfaces: the host must restrict it, bearer secret)" : " (this machine only, bearer secret)"));
    if (http) {
        using ants::server::SecretSource;
        if (secret_info.source == SecretSource::File) {
            log("control secret read from " + secret_info.path);
        } else if (secret_info.source == SecretSource::Environment) {
            log("control secret from the environment variable ANTS_SERVER_SECRET");
        }                                          // made now: shown above, once
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
        for (const ants::server::RoomStatus& s : rooms.take_ended(now)) {
            const bool demo = s.code.compare(0, std::strlen(ants::server::kDemoRoomPrefix), ants::server::kDemoRoomPrefix) == 0;
            if (demo && s.ticks == 0) continue;                    // a demo room that nobody completed: no line, no file (a peer chooses these codes, nothing may pile up)
            log("room " + s.code + " " + ants::server::room_state_name(s.state) + ": " + s.reason + " (map " + s.map + ", " + std::to_string(s.ticks) + " ticks)");
            if (!o.results_dir.empty() && !demo) {
                std::ofstream out(std::filesystem::path(o.results_dir) / (s.code + ".json"), std::ios::binary | std::ios::trunc);
                out << ants::ctl::to_json(ants::server::status_to_json(s)) << "\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    log("stopping");
    for (const ants::server::RoomStatus& s : rooms.list(now_ms())) rooms.close_room(s.code, now_ms());
    return 0;
}
