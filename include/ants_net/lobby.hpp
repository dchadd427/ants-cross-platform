#pragma once

// The room before a match: joining, the roster, the map and fog choice, and the start barrier. The original has no host / join interface (an
// external lobby starts every machine with its roster on the command line); the flow here keeps its rules: the host picks the map and starts, every
// machine loads the same map and reports it, the match begins when everybody is loaded (the original's ROSTER -> LOADED -> map check -> READY
// barriers), no late join, no host migration.
//
//   client: Hello -> Welcome (seat) -> Room updates ... Start -> load -> Loaded -> Begin
//   host:   Room broadcast on every change; start(): Start to all; all Loaded -> Begin; a failure or a leaver -> Cancel, back to the room
//
// A dedicated server's room (a host without a seat) has a LEADER (protocol 7): the first player who joined, and when it leaves the earliest of those who are left. The Room
// message names it to everybody (RoomMsg::leader); it may send StartRequest, and the room (ants_server) then starts the match with the players who are there, when it can.
// A host that holds a seat (LAN / direct) has no leader and ignores StartRequest.
//
// Keys (protocol 10, docs/NETWORK_PORT.md "Reconnect"). A host that is given a key maker (HostLobby::Config::make_key: a dedicated server, which has the operating system's random
// generator; ants_net itself never reads it, the library is built for the web too) gives every guest a KEY with its Welcome, and a guest that shows the key of a seat in its Hello gets
// that seat back: in the room (Phase::Room) the new connection TAKES THE SEAT OVER at once (same seat, same key, same place in the order of the Welcomes, so the leader stays the
// leader; the old connection is told Superseded and closed; the thumb is measured again; the room is broadcast), which is what a page that is reloaded in the waiting room needs before
// the old link is known to be dead. In a match that is loading or running a key is not the lobby's business (the session of the match answers, docs/NETWORK_PORT.md): the lobby says
// MatchRunning to it as to any Hello. A key that fits no seat is no offence: the Hello is the Hello of a new player. Without a key maker (a LAN or direct host) there are no keys,
// every Welcome carries the zero key and every Hello's key is ignored.
//
// Both classes are pure logic over Connection, driven from the main loop like the sessions. The connections stay owned by the caller; when the match
// begins the host lobby hands them (seat -> connection) to the HostSession.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_net/flood.hpp"
#include "ants_net/latency.hpp"
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
        /// A dedicated server's room only (host_seat = kNoSeat; a host that holds a seat has no leader whatever this says): the first guest to join leads the room
        /// (RoomMsg::leader) and its StartRequest is passed on to the room (Event::LeaderStart) when the room can start. False: the room has no leader and every StartRequest
        /// is ignored (the server's `early_start` option of a room).
        bool early_start{true};
        /// Flood control (flood.hpp): the messages that one guest may send, a token bucket. A message beyond it is not handled and is a violation (eight throw the guest out).
        uint32_t message_burst{kMessageBurst};
        uint32_t messages_per_second{kMessagesPerSecond};
        /// Makes the key of a seat (a server fills it with 16 random bytes of the operating system): true when `key` was filled. Called once for every guest that is welcomed. A key that is
        /// all zero, that equals the key of another guest, or a maker that returns false, gives the guest NO key (the zero key in its Welcome: no way back for it); the lobby tries a few
        /// times before it gives up. Unset (the default: a LAN or direct host): no guest has a key.
        std::function<bool(SeatKey&)> make_key;
    };
    enum class Phase : uint8_t { Room, Loading, Begun };
    struct Event {
        /// LeaderStart (a dedicated server's room): the leader (`seat`) asked to start now and can_start() holds; the owner of the lobby decides and calls start()
        enum class Type : uint8_t { Joined, Left, Rejected, LoadFailed, Cancelled, Begun, LeaderStart };
        Type type{Type::Joined};
        uint8_t seat{255};
    };

    HostLobby() : HostLobby(Config{}) {}
    explicit HostLobby(Config config);

    void set_map(const std::string& map_name);
    void set_fog(bool fog);
    const std::string& map_name() const noexcept { return room_.map_name; }
    bool fog() const noexcept { return room_.fog; }
    /// The seat of the room's leader (kNoLeader when there is none: a host that holds a seat, a room without early start, nobody has joined yet)
    uint8_t leader() const noexcept { return room_.leader; }
    /// How many StartRequest messages were heard and not acted on: from a guest that is not the leader (every guest of a host that holds a seat), after the room started loading,
    /// from a room that cannot start (too few players). A request is no offence (the leader's second click on START arrives after the Start) as long as a guest does not send more
    /// than kIgnoredStartRequestsAllowed of them: each one after those is a violation. A malformed one is a violation at once.
    uint32_t ignored_start_requests() const noexcept { return ignored_start_requests_; }

    /// A connection that the listener accepted; it becomes a seat when its Hello is accepted. `address` is where the connection came from (the host
    /// part only): with the port the guest announces it tells the other guests where to reach it during the match (host migration).
    void add_connection(Connection* connection, uint32_t now_ms, const std::string& address = std::string());
    /// The same for a connection whose first message (its Hello) was read already: a server reads it to find the room by its code, then hands both over.
    void add_connection(Connection* connection, uint32_t now_ms, const std::string& address, const std::vector<uint8_t>& hello_message);
    void update(uint32_t now_ms);

    Phase phase() const noexcept { return phase_; }
    const RoomMsg& room() const noexcept { return room_; }
    size_t players() const noexcept;
    /// The seats that a person holds (the host and the guests, not the bots)
    size_t humans() const noexcept;
    bool occupied(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS && room_.slots[seat].state != SlotState::Empty; }
    /// A match starts when enough seats are taken, a map is chosen, and at least one of the players is a person (a room of bots alone has nobody to play for)
    bool can_start() const noexcept { return phase_ == Phase::Room && players() >= cfg_.min_players && humans() >= 1 && !room_.map_name.empty(); }
    /// Removes the guest of a seat (Reject Kicked)
    void kick(uint8_t seat);
    /// A computer player takes `seat` (docs/BOTS.md): a slot in state Bot with no connection behind it. It counts as a player, its thumb is always good
    /// (round trip 0) and the start never waits for it. False unless the room is open (Room phase), the seat is free, there is room for another player and
    /// Fog of War is off (a bot would see through the fog). `name` is what the room shows ("Bot (Medium)"); the room's wire format is unchanged.
    bool add_bot(uint8_t seat, const std::string& name);
    /// Takes a bot out of the room (a bot seat of a started match stays)
    void remove_bot(uint8_t seat);
    /// True when a seat holds a bot
    bool has_bot() const noexcept;
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
    /// The key that the guest of a seat was given with its Welcome: all zero for a seat without a guest (the host's own, a bot's, an empty one), for a lobby without a key maker, and for a guest
    /// that the maker gave none. Stays what it was after Begun (the session of the match is given the keys of the seats from here).
    SeatKey key_of(uint8_t seat) const noexcept { return seat < sim::MAX_PLAYERS ? guests_[seat].key : SeatKey{}; }
    /// How many times a connection took a seat over with its key (the old one was closed)
    uint32_t takeovers() const noexcept { return takeovers_; }

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
        uint32_t join_order{0};                  // 1, 2, 3, ... in the order of the Welcomes: the earliest guest still here leads a server's room
        SeatKey key{};                           // protocol 10: the key of the seat, handed out with the Welcome (all zero: none)
        MessageBudget talk;                      // flood control: every message that the guest sends takes one from it
        uint32_t ignored_start_requests{0};      // the StartRequests of this guest that were ignored (the first kIgnoredStartRequestsAllowed are free)
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
    SeatKey new_key() const;
    uint8_t seat_of_key(const SeatKey& key) const noexcept;
    void take_over(uint8_t seat, Pending& p, const HelloMsg& hello);
    void handle_guest_message(uint8_t seat, const std::vector<uint8_t>& msg);
    void check_all_loaded();
    void cancel_with(CancelMsg::Reason reason, uint8_t player);
    /// A dedicated server's room that allows an early start has a leader
    bool leads() const noexcept { return cfg_.host_seat >= sim::MAX_PLAYERS && cfg_.early_start; }
    /// The leader is the guest with the earliest Welcome among those who are here (recomputed whenever somebody joins or leaves, before the room is broadcast)
    void elect_leader();

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
    uint32_t joins_{0};                      // the Welcomes sent so far (Guest::join_order)
    uint32_t ignored_start_requests_{0};
    uint32_t takeovers_{0};
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
        uint32_t ping_every_ms{1000};        // the guest measures its own round trip to the host this often once it has a seat (the "ping" next to the frame rate)
        SeatKey key{};                       // protocol 10: the key of the seat that this machine had (a page that was reloaded, a game that was started again): the Hello shows it and the
                                             // server gives the seat back. All zero: a new player. Its turns (Hello::have_turns) are 0: this lobby starts from nothing
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
    /// This machine leads the room (the last Room message names its seat as the leader; a dedicated server's room only)
    bool is_leader() const noexcept {
        return (phase_ == Phase::InRoom || phase_ == Phase::Loading || phase_ == Phase::Loaded) && seat_ < sim::MAX_PLAYERS && room_.leader == seat_;
    }
    /// The leader asks the server to start the match now with the players who are here (StartRequest). False unless this machine leads an open room (InRoom) and the message went
    /// out. The server decides: it starts only when it can (two players at least) and says nothing to a request that it cannot honour.
    bool request_start();
    /// The host's Start (valid from Loading on): the map, the seed, the roster
    const StartMsg& start_info() const noexcept { return start_; }
    RejectReason reject_reason() const noexcept { return reject_; }
    /// The connection was open at some time (the Hello went out): a lobby that closed without it never reached its server
    bool was_open() const noexcept { return was_open_; }
    /// The server accepted the connection and sent no Welcome within `welcome_timeout_ms` (10 s): the lobby closed the connection itself (a server that does not answer, not one that hung up)
    bool welcome_timed_out() const noexcept { return welcome_timed_out_; }
    /// The key that the Welcome carried (valid from InRoom on): all zero when the host offers no way back (a LAN or direct host, a room that holds no seats)
    const SeatKey& key() const noexcept { return key_; }
    /// The Welcome said that a Hello with a key was accepted into a match that is running or loading (kWelcomeRejoin): the server goes on with Start (this machine starts from nothing),
    /// no Room message comes, and the flow is Welcome -> Start -> report_loaded -> Begin -> Begun as for anybody. False after the Welcome of a seat in the room (also one that took its
    /// seat over in the waiting room: the room follows).
    bool rejoined() const noexcept { return rejoined_; }
    CancelMsg::Reason cancel_reason() const noexcept { return cancel_reason_; }
    /// The seat that caused the cancel (a player who left, a machine that could not load the map), 255 when unknown
    uint8_t cancel_player() const noexcept { return cancel_player_; }

    /// The map named by start_info() is loaded (ok) or cannot be (missing file, a different file)
    void report_loaded(bool ok);
    void leave();
    std::vector<Event> take_events();
    /// This guest's own measurement of its round trip to the host (latency.hpp): it pings the host once a second from the moment it has a seat (the room's host and a
    /// server's room answer a guest's Ping, so the protocol is unchanged); nothing is measured before the first answer
    const PingMeter& ping() const noexcept { return ping_; }

private:
    Connection* conn_;
    Config cfg_;
    Phase phase_{Phase::Connecting};
    RoomMsg room_;
    StartMsg start_;
    uint8_t seat_{255};
    SeatKey key_{};
    bool rejoined_{false};
    RejectReason reject_{RejectReason::BadRequest};
    CancelMsg::Reason cancel_reason_{CancelMsg::Reason::HostCancelled};
    uint8_t cancel_player_{255};
    uint32_t joined_at_ms_{0};
    bool joined_stamp_pending_{false};      // the Hello went out through send_hello(): update() stamps the time
    PingMeter ping_;
    uint32_t next_ping_ms_{0};
    bool ping_armed_{false};                // the first ping goes out as soon as the guest has a seat (the deadline is taken from the clock then: clock.hpp)
    bool was_open_{false};                  // the connection was open when the Hello was sent
    bool welcome_timed_out_{false};         // no Welcome came within the limit (see welcome_timed_out())
    std::vector<Event> events_;
};

}  // namespace ants::net
