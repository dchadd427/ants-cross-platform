#pragma once

// The one line that a network match shows at the top of the playfield when something is wrong or has just happened (remake text: the original has none). Which line it is
// is decided here, from what the network layer reports, so that the priorities and the words can be tested without a window:
//
//   1. "Out of sync: the match has stopped."        two machines disagree: the match is frozen (red)
//   2. "The host left. Choosing a new host..."      no turns arrive until the guests have agreed (a game on the local network)
//   3. "Waiting for the other players..."           this machine has waited a second for a turn; "Waiting for Bob..." when the host knows who holds the game up (a host with a
//                                                   seat waits for a peer that is more than 3 s behind)
//   4. "Catching up..."                             this machine is more than 3 s behind a server's room (it was away) and runs the backlog down at up to four times normal speed
//   5. "Bob is lagging (12 s behind)"               a server's room does not wait for a player that falls behind: the others are told who it is, once a second, while it is
//                                                   more than 3 s behind and until it is within one second (ants_net/session.hpp)
//   6. the match notice                             "Bob is the host now." for a few seconds
// A line of a lower number never hides one of a higher number: a machine that is itself cut off says so (3) before it says who else is slow (5).

#include <cstdint>
#include <string>

namespace ants::app {

struct NetOverlayInput {
    bool desynced{false};
    bool electing{false};
    uint32_t stalled_ms{0};          // how long this machine has waited for a turn (NetGame::stalled_ms)
    std::string waiting_for;         // the player that holds the game up, when a host with a seat knows ("" when none)
    bool catching_up{false};         // NetGame::catching_up
    int lag_seat{-1};                // the player that the server announced as lagging (0 .. 3), -1 when none
    std::string lag_name;            // its name ("" when it has none: "Player 3")
    uint32_t lag_behind_ms{0};       // how far behind the match it was at the last announcement
    std::string notice;              // NetGame::match_notice
};

struct NetOverlayLine {
    std::string text;                // empty: nothing to say
    bool alarm{false};               // drawn in red
};

/// A machine waits for a turn this long before it says so
inline constexpr uint32_t NET_WAIT_MESSAGE_MS = 1000;

NetOverlayLine net_overlay_line(const NetOverlayInput& in);

}  // namespace ants::app
