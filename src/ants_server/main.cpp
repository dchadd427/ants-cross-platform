// ants_server: the dedicated game server. A headless program: it hosts many rooms, each a host without a seat that runs the match as the referee (docs/NETWORK_PORT.md).
//
//   ants_server --maps DIR [--port 4001] [--ws-port 4002] [--ctl-port 4010] [--public] [--ws-any-interface] [--ctl-any-interface] [--results-dir DIR] [--max-rooms N]
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
//   --max-rooms N      the most rooms at a time (default 256)
//   --version, --help
//
// The control interface's secret comes from the environment (ANTS_SERVER_SECRET), never from the command line (a command line is visible to every user).

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
    size_t max_rooms{256};
};

void usage(FILE* to) {
    std::fprintf(to,
                 "usage: ants_server --maps DIR [--port 4001] [--ws-port N] [--ctl-port N] [--public] [--ws-any-interface] [--ctl-any-interface]\n"
                 "                    [--results-dir DIR] [--max-rooms N]\n"
                 "  the control interface needs the secret in the environment variable ANTS_SERVER_SECRET\n");
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
        } else if (a == "--max-rooms") {
            o.max_rooms = static_cast<size_t>(std::strtoul(value("--max-rooms"), nullptr, 10));
            if (o.max_rooms == 0) {
                std::fprintf(stderr, "--max-rooms takes a positive number\n");
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
    std::error_code ec;
    if (!std::filesystem::is_directory(o.maps_dir, ec)) {
        std::fprintf(stderr, "the maps folder '%s' does not exist\n", o.maps_dir.c_str());
        return 2;
    }
    if (!o.results_dir.empty()) std::filesystem::create_directories(o.results_dir, ec);

    std::string secret;
    if (o.ctl_port != 0) {
        const char* env = std::getenv("ANTS_SERVER_SECRET");
        if (env == nullptr || *env == '\0') {
            std::fprintf(stderr, "the control interface needs a secret in the environment variable ANTS_SERVER_SECRET\n");
            return 2;
        }
        secret = env;
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
    ants::server::RoomManager rooms{ants::server::MapStore(o.maps_dir), limits};

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    log(std::string("ants_server ") + std::string(ants::VERSION_STRING) + " (network protocol " + std::to_string(ants::net::kProtocolVersion) + "), maps in " + o.maps_dir);
    if (tcp) log("TCP game port " + std::to_string(tcp->port()) + (o.is_public ? " (all interfaces)" : " (this machine only)"));
    if (ws) log("WebSocket port " + std::to_string(ws->port()) + (o.ws_any_interface ? " (all interfaces: the host must restrict it)" : " (this machine only: put a TLS proxy in front)"));
    if (http) log("control interface on port " + std::to_string(http->port()) + (o.ctl_any_interface ? " (all interfaces: the host must restrict it, bearer secret)" : " (this machine only, bearer secret)"));

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
            log("room " + s.code + " " + ants::server::room_state_name(s.state) + ": " + s.reason + " (map " + s.map + ", " + std::to_string(s.ticks) + " ticks)");
            if (!o.results_dir.empty()) {
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
