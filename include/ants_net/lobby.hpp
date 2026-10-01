#pragma once

// The room before a match: joining, the roster, the map and fog choice, and the start barrier. The original has no host / join interface (an
// external lobby starts every machine with its roster on the command line); the flow here keeps its rules: the host picks the map and starts, every
// machine loads the same map and reports it, the match begins when everybody is loaded (the original's ROSTER -> LOADED -> map check -> READY
// barriers), no late join, no host migration.
//
//   client: Hello -> Welcome (seat) -> Room updates ... Start -> load -> Loaded -> Begin
//   host:   Room broadcast on every change; start(): Start to all; all Loaded -> Begin; a failure or a leaver -> Cancel, back to the room
//
// Both classes are pure logic over Connection, driven from the main loop like the sessions. The connections stay owned by the caller; when the match
// begins the host lobby hands them (seat -> connection) to the HostSession.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"

namespace ants::net {

class HostLobby {
public:
    struct Config {
        std::string host_name{"Host"};
        std::string room_code;               // "" for a LAN / direct host; a server names its room: a Hello for another room is Rejected NoSuchRoom
        uint8_t host_seat{0};                // the host's own seat; 255 (kNoSeat) for a dedicated server: the host takes no seat, every seat is a guest's
        uint32_t hello_timeout_ms{10000};    // a connection that does not say Hello in this time is closed
        uint32_t load_timeout_ms{60000};     // everybody must report Loaded in this time
        uint32_t violation_limit{8};
        uint32_t ping_every_ms{1000};        // the round trip to every guest is measured this often (the thumbs of the setup screen)
        uint8_t min_players{2};              // can_start() needs this many seats taken (a dedicated server's room: the host holds none of them)
        uint8_t max_players{sim::MAX_PLAYERS};   // a Hello beyond this many seats taken is Rejected Full (a server's room that expects 3 players takes no fourth)
    };
    enum class Phase : uint8_t { Room, Loading, Begun };
    struct Event {
        enum class Type : uint8_t { Joined, Left, Rejected, LoadFailed, Cancelled, Begun };
        Type type{Type::Joined};
        uint8_t seat{255};
    };

    HostLobby() : HostLobby(Config{}) {}
    explicit HostLobby(Config config);

    void set_map(const std::string& map_name);
    void set_fog(bool fog);
    const std::string& map_name() const noexcept { return room_.map_name; }
    bool fog() const noexcept { return room_.fog; }

    /// A connection that the listener accepted; it becomes a seat when its Hello is accepted. `address` is where the connection came from (the host
    /// part only): with the port the guest announces it tells the other guests where to reach it during the match (host migration).
    void add_connection(Connection* connection, uint32_t now_ms, const std::string& address = std::string());
    /// The same for a connection whose first message (its Hello) was read already: a server reads it to find the room by its code, then hands both over.
    void add_connection(Connection* connection, uint32_t now_ms, const std::string& address, const std::vector<uint8_t>& hello_message);
    void update(uint32_t now_ms);

    Phase phase() const noexcept { return phase_; }
    const RoomMsg& room() const noexcept { return room_; }
    size_t players() const noexcept;
    bool occupied(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && room_.slots[seat].state != SlotState::Empty; }
    bool can_start() const noexcept { return phase_ == Phase::Room && players() >= cfg_.min_players && !room_.map_name.empty(); }
    /// Removes the guest of a seat (Reject Kicked)
    void kick(uint8_t seat);
    /// The measured round trip to a guest (the host itself: 0); false while nothing has come back yet
    bool measured(uint8_t seat) const noexcept;
    uint32_t rtt_ms(uint8_t seat) const noexcept;
    /// Every seated guest has been measured: "all players' thumbs have appeared"
    bool all_measured() const noexcept;

    /// Sends Start to everybody: the host loads the map itself too and reports host_loaded(). False unless can_start().
    bool start(uint32_t seed, uint64_t map_hash, uint32_t now_ms);
    void host_loaded(bool ok);
    /// The host abandons the start (Cancel to everybody, back to the room)
    void cancel();
    const StartMsg& start_info() const noexcept { return start_; }
    /// Once Begun: the connection of a seat (nullptr for the host's own seat and empty seats)
    Connection* connection_of(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS ? guests_[seat].conn : nullptr; }

    std::vector<Event> take_events();

private:
    struct Guest {
        Connection* conn{nullptr};
        bool loaded{false};
        uint32_t violations{0};
        uint32_t next_ping_ms{0};
        uint32_t ping_nonce{0};                  // the last ping sent; its send time is ping_sent[nonce % 8] (a slow link has several in flight)
        uint32_t ping_sent[8]{};
        bool measured{false};
        uint32_t rtt_ms{0};
        std::string address;                     // where the guest's connection came from
        uint16_t listen_port{0};                 // the port on which it accepts the other guests during the match (0: none)
    };
    struct Pending {
        Connection* conn;
        uint32_t since_ms;
        std::string address;
    };
    void broadcast_room();
    void broadcast(const std::vector<uint8_t>& msg);
    void remove_guest(uint8_t seat, bool notify_reject, RejectReason reason);
    void violation(uint8_t seat);
    void handle_hello(Pending& p, const std::vector<uint8_t>& msg, uint32_t now_ms, bool& consumed);
    void handle_guest_message(uint8_t seat, const std::vector<uint8_t>& msg);
    void check_all_loaded();
    void cancel_with(CancelMsg::Reason reason, uint8_t player);

    Config cfg_;
    RoomMsg room_;
    StartMsg start_;
    uint32_t last_update_ms_{0};      // the clock of the current update (the measurements use it)
    Phase phase_{Phase::Room};
    std::array<Guest, sim::MAX_PLAYERS> guests_{};
    bool host_loaded_{false};
    uint32_t load_started_ms_{0};
    std::vector<Pending> pending_;
    std::vector<Event> events_;
};

class ClientLobby {
public:
    struct Config {
        std::string name{"Player"};
        uint32_t welcome_timeout_ms{10000};
        uint16_t listen_port{0};             // where this guest accepts the other guests during the match, announced in Hello (0: nowhere)
        uint8_t want_seat{255};              // the seat this guest asks for in Hello (0 .. 3; 255: any). Taken, or the host's: the first free seat
        std::string room;                    // the room of a server (valid_room_code), "" for a LAN / direct host
        std::string token;                   // the credential that came with the room code ("" when none)
    };
    enum class Phase : uint8_t { Connecting, Joining, InRoom, Loading, Loaded, Begun, Rejected, Closed };
    struct Event {
        enum class Type : uint8_t { RoomChanged, StartRequested, Begun, Cancelled, Rejected, Disconnected };
        Type type{Type::RoomChanged};
    };

    ClientLobby(Connection* connection, Config config) : conn_(connection), cfg_(std::move(config)) {}

    /// Says Hello now when the connection is open (and no Hello has been sent): a browser tab that is not drawn runs no frames, so no update() comes, and the
    /// server closes a connection that does not say Hello within seconds. update() does the same on its own; the timer of the Hello starts at the next update().
    void send_hello();
    void update(uint32_t now_ms);
    Phase phase() const noexcept { return phase_; }
    uint8_t my_seat() const noexcept { return seat_; }
    const RoomMsg& room() const noexcept { return room_; }
    /// The host's Start (valid from Loading on): the map, the seed, the roster
    const StartMsg& start_info() const noexcept { return start_; }
    RejectReason reject_reason() const noexcept { return reject_; }
    CancelMsg::Reason cancel_reason() const noexcept { return cancel_reason_; }
    /// The seat that caused the cancel (a player who left, a machine that could not load the map), 255 when unknown
    uint8_t cancel_player() const noexcept { return cancel_player_; }

    /// The map named by start_info() is loaded (ok) or cannot be (missing file, a different file)
    void report_loaded(bool ok);
    void leave();
    std::vector<Event> take_events();

private:
    Connection* conn_;
    Config cfg_;
    Phase phase_{Phase::Connecting};
    RoomMsg room_;
    StartMsg start_;
    uint8_t seat_{255};
    RejectReason reject_{RejectReason::BadRequest};
    CancelMsg::Reason cancel_reason_{CancelMsg::Reason::HostCancelled};
    uint8_t cancel_player_{255};
    uint32_t joined_at_ms_{0};
    bool joined_stamp_pending_{false};      // the Hello went out through send_hello(): update() stamps the time
    std::vector<Event> events_;
};

}  // namespace ants::net
