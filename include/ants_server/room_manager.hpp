#pragma once

// The rooms of a game server and the door they share. Every connection (TCP from a native client, WebSocket from a browser or Electron through the reverse
// proxy) arrives here without knowing its room: the first message must be a Hello within a few seconds, and its room code (protocol 6) names the room. A Hello for
// a room that does not exist, a Hello of another protocol version and anything that is not a Hello are answered with a Reject and the connection is closed;
// a Hello for a room goes to that room's lobby together with the connection.
//
// Rooms are made from outside (create_room: the control interface calls it), update() runs every room and forgets the finished ones when their keep time is over.
// Limits protect the server: the number of rooms, the number of connections that have not said Hello yet, the time they get for it.
//
// A Hello with a KEY (protocol 10) for a room whose match runs and that holds seats goes to that room (Room::rejoin: the session takes the connection over and gives the player the match
// again); a key that fits no seat, a room that does not hold seats and a room that is loading are answered MatchRunning, as a Hello without a key is, so that nothing is revealed; a room
// that is over is NoSuchRoom. A Hello with a key for a room that still waits is the lobby's (a page that was reloaded in the waiting room takes its seat over). The server's own defaults
// for what the rooms hold (reconnect, the vote, the cap, the limit of the log) and the budget that all the logs share are ServerLimits.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"

namespace ants::server {

struct ServerLimits {
    size_t max_rooms{256};
    size_t max_pending{128};                // connections that have not said Hello yet
    uint32_t hello_timeout_ms{10000};       // ... and the time they get for it
    uint32_t reject_linger_ms{2000};        // a rejected connection is kept open this long so that the answer reaches the peer
    // Demo rooms (off by default; for a public test page that has no secret to make rooms with): a Hello for a room whose code starts with "demo-" that does not
    // exist makes it, on `demo_map`, for `demo_players` players, at most `demo_rooms` of them at a time (0 = off); the code can choose the map and the players
    // (`demo_maps`). A demo room waits `demo_wait_ms` (ten minutes) for its players and is forgotten half a minute after it ended; a Hello for the code of one that
    // is over makes a new room at once. Whoever can reach the door can fill these rooms and hold them for a match: that is the price of a page that works without
    // a secret; real rooms are made by the control interface.
    size_t demo_rooms{0};
    std::string demo_map;
    uint8_t demo_players{4};
    // How long a demo room waits for its players, from the first Hello (the page's link goes to friends on other computers: they need time to arrive)
    uint32_t demo_wait_ms{10u * 60u * 1000u};
    // The maps that a Hello may choose for a demo room. The code of a demo room chooses: "demo-[<map>-][<n>p-]<anything>": <map> is the name of one of these
    // maps without its extension (any case, then a dash; file names of the maps folder, e.g. "SMALL.LVL"), <n>p the number of players, 2 to 4 ("demo-small-2p-x7k2":
    // SMALL.LVL for two). The player count may also follow a first word that is not one of these maps ("demo-medium-2p-x" on a server without MEDIUM: the default
    // map, for two), so that a page that offers a map the server does not allow still gets the players it asked for. What a code does not choose is `demo_map` and
    // `demo_players`, as before. The choice rides in the room code because the Hello has no other field (the protocol is unchanged).
    std::vector<std::string> demo_maps;
    // Reconnect (protocol 10; ants_server's --reconnect, --hold-vote-seconds, --max-pause-seconds, --max-catch-up-seconds, --resume-countdown-seconds, --log-mb): what a room holds unless its specification says otherwise. Demo rooms follow
    // `reconnect`. OFF by default in this release.
    bool reconnect{false};
    uint32_t hold_vote_ms{net::kVoteAfterMs};                       // the vote opens after a seat has been away this long in all
    uint32_t max_pause_ms{net::kMaxPauseMs};                        // a match's pauses may last this long in all; at the cap every seat that is not present is dropped
    uint32_t max_catch_up_ms{net::kMaxCatchUpMs};                   // one absence may spend this long catching up in all (--max-catch-up-seconds)
    uint32_t resume_countdown_ms{net::kResumeCountdownMs};          // the match is held this long after a pause of 3 s or more (--resume-countdown-seconds; 0: none)
    size_t room_log_bytes{net::TurnLog::kDefaultMaxBytes};          // the limit of one room's turn log (16 MiB)
    uint64_t log_budget_bytes{256ull * 1024ull * 1024ull};          // the memory that all the rooms' logs may take together; a log that cannot grow is not kept (its room drops a lost seat at once)
};

/// The code prefix of the rooms that a Hello may make when demo rooms are on
inline constexpr const char* kDemoRoomPrefix = "demo-";

/// What a restart would interrupt (the public /busy answer): the rooms whose match is loading or running, and the people (bots are not people) in the rooms that wait, load or run. Plain
/// counts: no name, no code.
struct BusyCounts {
    uint32_t matches{0};
    uint32_t players{0};
};

struct CreateResult {
    bool ok{false};
    int http_status{201};                   // what the control interface answers: 201, 400, 404, 409, 503
    std::string error;
    std::string code;                       // the room's code (generated when the spec had none)
};

class RoomManager {
public:
    RoomManager(MapStore store, ServerLimits limits = ServerLimits());

    const MapStore& store() const noexcept { return store_; }
    const ServerLimits& limits() const noexcept { return limits_; }
    /// A room's specification with the server's defaults in it (reconnect, the vote, the cap, the limit of the log): what the control interface starts from
    RoomSpec default_spec() const;
    /// What the logs of all the rooms hold now and what they may hold together (bytes)
    uint64_t log_bytes() const noexcept { return log_budget_.used(); }
    uint64_t log_budget_bytes() const noexcept { return log_budget_.limit(); }

    /// Makes a room: the map must exist in the store and load, the code must be new and valid (an empty code gets a generated one), 2 <= players <= 4, and the
    /// server must have room for one more.
    CreateResult create_room(RoomSpec spec, uint32_t now_ms);
    /// A new connection (its peer address is only used for the lobby's records): it has to say Hello soon
    void add_connection(std::unique_ptr<net::Connection> connection, const std::string& address, uint32_t now_ms);
    void update(uint32_t now_ms);

    bool status(const std::string& code, RoomStatus& out, uint32_t now_ms) const;
    std::vector<RoomStatus> list(uint32_t now_ms) const;
    /// The owner closes a room (its clients are dropped); false when there is no such room
    bool close_room(const std::string& code, uint32_t now_ms);

    /// The rooms that ended (finished or failed) since the last call, once each: what the server logs and writes to its result files
    std::vector<RoomStatus> take_ended(uint32_t now_ms);

    /// How busy the server is (see BusyCounts); cheap: a pass over the rooms
    BusyCounts busy(uint32_t now_ms) const;

    size_t room_count() const noexcept { return rooms_.size(); }
    size_t pending_count() const noexcept { return pending_.size(); }
    uint64_t rooms_created() const noexcept { return created_; }
    uint64_t connections_refused() const noexcept { return refused_; }

private:
    struct Pending {
        std::unique_ptr<net::Connection> connection;
        std::string address;
        uint32_t since_ms{0};
    };
    struct Lingering {
        std::unique_ptr<net::Connection> connection;
        uint32_t since_ms{0};
    };

    void reject(std::unique_ptr<net::Connection> connection, net::RejectReason reason, uint32_t now_ms);
    std::string new_code();
    /// Makes the demo room that a Hello names, when demo rooms are on, the code has the prefix, and there is a free one; false otherwise
    bool make_demo_room(const std::string& code, uint32_t now_ms);

    MapStore store_;
    ServerLimits limits_;
    net::LogBudget log_budget_;                  // (declared before the rooms: they give their logs back when they are destroyed)
    std::map<std::string, std::unique_ptr<Room>> rooms_;
    std::vector<Pending> pending_;
    std::vector<Lingering> lingering_;
    std::vector<RoomStatus> unreported_;     // the ends of rooms that were forgotten in the pass in which they ended
    uint64_t created_{0};
    uint64_t refused_{0};
    uint64_t code_counter_{0};
};

}  // namespace ants::server
