#pragma once

// The rooms of a game server and the door they share. Every connection (TCP from a native client, WebSocket from a browser or Electron through the reverse
// proxy) arrives here without knowing its room: the first message must be a Hello within a few seconds, and its room code (protocol 6) names the room. A Hello for
// a room that does not exist, a Hello of another protocol version and anything that is not a Hello are answered with a Reject and the connection is closed;
// a Hello for a room goes to that room's lobby together with the connection.
//
// Rooms are made from outside (create_room: the control interface calls it), update() runs every room and forgets the finished ones when their keep time is over.
// Limits protect the server: the number of rooms, the number of connections that have not said Hello yet, the time they get for it.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/room.hpp"

namespace ants::server {

struct ServerLimits {
    size_t max_rooms{256};
    size_t max_pending{128};                // connections that have not said Hello yet
    uint32_t hello_timeout_ms{10000};       // ... and the time they get for it
    uint32_t reject_linger_ms{2000};        // a rejected connection is kept open this long so that the answer reaches the peer
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

    MapStore store_;
    ServerLimits limits_;
    std::map<std::string, std::unique_ptr<Room>> rooms_;
    std::vector<Pending> pending_;
    std::vector<Lingering> lingering_;
    uint64_t created_{0};
    uint64_t refused_{0};
    uint64_t code_counter_{0};
};

}  // namespace ants::server
