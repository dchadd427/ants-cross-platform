#include "ants_app/lan_list.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include "ants_net/lan.hpp"

namespace ants::app {

int list_lan_rooms(uint16_t port, uint32_t listen_ms, std::ostream& out, const std::function<void(uint32_t)>& each) {
    auto browser = net::LanBrowser::open(port);
    if (!browser) {
        out << "Cannot listen on UDP port " << port << ".\n";
        return -1;
    }
    out << "Games on the local network (UDP port " << browser->port() << ", listening for " << (listen_ms / 1000) << "." << ((listen_ms % 1000) / 100) << " s):\n";
    const auto start = std::chrono::steady_clock::now();
    uint32_t now = 0;
    do {
        now = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        if (each) each(now);
        browser->update(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (now < listen_ms);
    const std::vector<net::LanRoom> rooms = browser->rooms();
    for (const net::LanRoom& r : rooms) {
        out << "  " << r.address << ":" << r.info.tcp_port << "  \"" << r.info.host_name << "\"  " << (r.info.map_name.empty() ? std::string("(no map yet)") : r.info.map_name)
            << "  " << static_cast<int>(r.info.players) << "/" << static_cast<int>(r.info.seats) << " players  " << (r.info.version.empty() ? std::string("?") : r.info.version);
        if (!r.compatible) out << "  [another version of the game: protocol " << r.info.protocol << ", this one speaks " << net::kProtocolVersion << "]";
        out << "\n";
    }
    out << (rooms.empty() ? std::string("No games found.") : std::to_string(rooms.size()) + (rooms.size() == 1 ? " game found." : " games found.")) << "\n";
    return static_cast<int>(rooms.size());
}

int lan_list_main(int argc, char* argv[]) {
    uint32_t seconds = 3;
    uint16_t port = net::kLanDiscoveryPort;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lan-list") == 0) {
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--lan-port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        }
    }
    const int found = list_lan_rooms(port, seconds * 1000u, std::cout);
    return found < 0 ? 2 : (found > 0 ? 0 : 1);
}

}  // namespace ants::app
