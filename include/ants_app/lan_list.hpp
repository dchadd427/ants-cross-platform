#pragma once

// `ants --lan-list`: what the local network offers, without opening the game (native builds; see ants_net/lan.hpp).

#include <cstdint>
#include <functional>
#include <ostream>

namespace ants::app {

/// Listens on UDP `port` for `listen_ms` milliseconds and writes one line per room that was heard:
///   address:port  "host"  map  players/seats players  version
/// (a room of another protocol version says so). `each(now_ms)` runs between two polls: the tests keep a host alive there. Returns the number of rooms, or
/// -1 when the port cannot be listened on.
int list_lan_rooms(uint16_t port, uint32_t listen_ms, std::ostream& out, const std::function<void(uint32_t)>& each = {});

/// The command line of `--lan-list [seconds] [--lan-port N]` (3 seconds and the game's own port by default): exit code 0 when at least one room was heard,
/// 1 when none was (a script can wait for a room), 2 when the port cannot be used
int lan_list_main(int argc, char* argv[]);

}  // namespace ants::app
