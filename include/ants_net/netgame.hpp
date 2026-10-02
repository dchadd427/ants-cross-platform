#pragma once

// The network as the application sees it: one object that owns the listener and the connections, runs the room before the match and the session
// during it, and gives the HUD its CommandSink. It is driven from the main loop with a monotonic millisecond clock (`update`), never blocks, has no
// threads, and does not know about SDL, the renderer or the map files: the application loads the map when the room says so and reports back.
//
//   host:   host(port, name) -> room (guests join, the host picks the map and the fog) -> start_match(seed, map hash) -> every machine loads
//           (StartRequested event, report_loaded) -> Begun -> the match runs -> freeze() at its end
//   client: join(address, port, name) -> room (follows the host's map and fog) -> StartRequested -> load, report_loaded -> Begun -> the match runs
//   leader: the first player in a dedicated server's room (protocol 7) is the room's leader (is_leader()); request_start() asks the server to start with the players who are
//           there (the server may refuse: fewer than two players), the rest is as for any client
//
// During the match the guests are also linked to each other (each guest listens on a port that the host passes on with the roster; the links are made
// while the map loads). When the host goes, the guests agree on the lowest living seat as the new host and the match goes on (see session.hpp); the
// events HostChanged and PlayerLeft report it, HostLeft only when no new host could be found.
//
// Raw TCP (LAN and development) is the transport of native builds; a WebAssembly build has none yet (host() and join() return false there) until the
// WebRTC transport of the network port arrives. The class does not care which Connection it talks to.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ants_net/lan.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

/// FNV-1a 64 over the bytes of a file (the map identity that the start barrier compares); false when the file cannot be read.
bool hash_file(const std::string& path, uint64_t& out);

/// What the player is told when the match is lost to this machine (NetGame::status_text after the event HostLeft): the reason as the session knows it. A server that closed
/// the link of a machine that held half a minute of the match unplayed dropped it for being away (a hidden tab, a process that was stopped); anything else is a link that is gone.
std::string match_lost_text(ClientSession::LostReason reason);

class NetGame final : public sim::CommandSink {
public:
    enum class Role : uint8_t { None, Host, Client };
    enum class Phase : uint8_t {
        Off,           // not networked
        Connecting,    // client: connecting and waiting for the host's welcome
        Room,          // in the room before a match
        Loading,       // the host said Start: the application loads the map and reports
        Playing,       // the match runs
        Over,          // the session ended (the host left, the connection is gone)
        Failed         // could not join (refused, full, wrong version, connection failed)
    };
    /// Why a join ended in Phase::Failed (None until it has): the server was never reached, the server answered with a Reject (reject_reason()), the connection was lost after it
    /// was made but before the room was joined, or the room's connection ended once the player was in it
    enum class FailReason : uint8_t { None, Unreachable, Rejected, Lost, Closed };
    struct Event {
        enum class Type : uint8_t {
            RoomChanged,      // somebody joined or left, or the host changed the map or the fog
            StartRequested,   // load start_info() now and call report_loaded()
            Begun,            // everybody is loaded: the match runs
            Cancelled,        // the start failed (a player left, a map did not load): back in the room
            PlayerLeft,       // during the match: `seat` dropped out
            HostLeft,         // during the match (client): the host is gone and no new host could be agreed: the match cannot go on here
            Desync,           // two machines disagree: the match is frozen
            Failed,           // joining failed, see status_text()
            HostChanged       // during the match: the host left and `seat` is the new host (this machine when it is our own seat)
        };
        Type type{Type::RoomChanged};
        uint8_t seat{255};
    };

    explicit NetGame(sim::SimulationEngine& sim);
    ~NetGame() override;
    NetGame(const NetGame&) = delete;
    NetGame& operator=(const NetGame&) = delete;

    // ---- set-up (non-blocking) -------------------------------------------------------------------------------------------------------------------
    /// Opens a room on `port` (0 = any free port, see listen_port()); `loopback_only` accepts only this machine (tests). False when the port cannot be
    /// used or there is no transport.
    bool host(uint16_t port, const std::string& name, bool loopback_only = false);
    /// Starts joining the room at address:port. False when the address cannot be used or there is no transport; the outcome arrives as events.
    /// `want_seat` (0 .. 3) asks the host for that seat (the colour: 0 green, 1 red, 2 blue, 3 black); a seat that is taken gives the first free one; 255 = any.
    bool join(const std::string& address, uint16_t port, const std::string& name, uint8_t want_seat = 255, const std::string& room = std::string(), const std::string& token = std::string());
    /// The same through a WebSocket (ws:// or wss:// URL, the game server's door behind its proxy): the browser build's only way to join. False when the URL cannot
    /// be used or there is no WebSocket (every native build: it joins with TCP). A server's room has no host migration and no links between guests.
    bool join_url(const std::string& url, const std::string& name, uint8_t want_seat = 255, const std::string& room = std::string(), const std::string& token = std::string());
    /// Leaves for good: tells the others (a guest says Leave), closes every connection. The others see the host or the guest gone.
    void leave();

    // ---- the room on the local network -------------------------------------------------------------------------------------------------------------
    /// Before host(): the UDP port on which the open room announces itself to the games of the local network (see lan.hpp; 0 = not at all, the default is
    /// kLanDiscoveryPort). `loopback_only` keeps the announcements on this machine (tests); a room opened with host(..., loopback_only = true) does that anyway.
    void set_discovery(uint16_t udp_port, bool loopback_only = false);
    /// The game's version text that the announcements carry for the list of games (the application sets it; empty: none)
    void set_game_version(const std::string& text) { game_version_ = text; }
    /// True while the room announces itself: the host, in the room, before the start (no late join), with a UDP socket
    bool announcing() const noexcept;

    // ---- every frame -----------------------------------------------------------------------------------------------------------------------------
    void update(uint32_t now_ms);
    /// Real time that the clock of update() did not count (the browser build, a hidden page: the application hands the network at most a second per wake-up): a host that
    /// said nothing in the last update has been silent for `ms` more (ClientSession::note_gap), and the session judges it now. Only a match as a guest is touched.
    void note_gap(uint32_t ms);
    std::vector<Event> take_events();
    /// The browser build only: called from the browser's event loop when the server's connection has news (a message arrived, an error, the link closed), whether or
    /// not the page draws frames. A hidden page runs none and the browser slows its timers, but the WebSocket's events still come: the application makes its
    /// background step from here (Application::background_pump), which calls update() and take_events() like a frame does. The function may end the session (leave()).
    /// A native build never calls it (its transports are polled by the frame loop).
    void set_on_wake(std::function<void()> fn) { on_wake_ = std::move(fn); }

    // ---- state -----------------------------------------------------------------------------------------------------------------------------------
    Phase phase() const noexcept { return phase_; }
    /// True for the room's owner, and for a guest that took over during the match
    bool is_host() const noexcept { return role_ == Role::Host || host_session_ != nullptr; }
    bool active() const noexcept { return role_ != Role::None; }
    uint8_t my_seat() const noexcept { return seat_; }
    /// Valid in Phase::Failed (None before): what went wrong, so that the application can say it in its own words
    FailReason fail_reason() const noexcept { return fail_reason_; }
    /// The server's reason when fail_reason() is Rejected
    RejectReason reject_reason() const noexcept { return reject_reason_; }
    uint16_t listen_port() const noexcept { return listen_port_; }
    /// A guest: the port on which the other guests connect to it during the match (0 when it has none)
    uint16_t peer_port() const noexcept { return peer_port_; }
    /// The room as this machine sees it (the host's own copy, or the last one received)
    const RoomMsg& room() const noexcept { return room_; }
    /// One line for the screen, in the original's words: what the machine is waiting for ("Press START when all players' thumbs have appeared.",
    /// "Waiting for the host to start the game...", "Trying to connect to the host..." and its 30 s / 60 s successors), or why it failed
    const std::string& status_text() const noexcept { return status_; }
    /// The thumb beside a player's name: the host's measured round trip to that seat (the host's own seat is always good)
    LinkQuality seat_quality(uint8_t seat) const noexcept;
    /// A client in the room (or loading the match) whose seat the last Room message names as the leader: the first player in a dedicated server's room, then, when
    /// it leaves, the earliest of those who are left (protocol 7). Never true for the host of a LAN room, and in a server's room that does not allow an early start.
    bool is_leader() const noexcept;
    /// The leader asks the server to start the match now with the players who are in the room (StartRequest). False (nothing is sent) when this machine is not the leader, the
    /// room is not open, or fewer than two players are in it: the same answer as the host's START gives when `start_match` refuses (the application plays the can't-go cue).
    /// True means the request was sent, not that the server will start: it starts at once when it can, otherwise nothing happens.
    bool request_start();

    // ---- what the network costs this player (latency.hpp), shown next to the frame rate -----------------------------------------------------------
    /// The round trip to the host in ms, as this machine measures it (the mean of its last few Ping / Pong round trips, in the room and in the match). The host has no
    /// link to itself: 0 (a guest that took over as host too). Empty before the first answer came back, when there is nothing to measure, and when the last answer is older
    /// than three seconds (PingMeter::kStaleAfterMs: the host says nothing, or this machine was not run for a while: an old reading is not shown as if it were one).
    std::optional<uint32_t> ping_ms() const;
    /// The delay of this player's own commands in ms: the real time from sending one (a guest) or handing it to the sequencer (the host) to the tick that applies it on
    /// this machine, the median of the last five. Empty until a command has been applied, and when the last one was applied more than ten seconds ago.
    std::optional<uint32_t> command_delay_ms() const;

    // ---- the host's controls in the room ---------------------------------------------------------------------------------------------------------
    /// Host only: the map every machine will load (a plain .LVL file name of the maps folder) and the Fog of War option
    void set_map(const std::string& map_name);
    /// Fog of War on is refused while a bot sits in the room (a bot would see through it): the option stays off and the status line says why
    void set_fog(bool fog);
    /// Host only, in the room: a computer player takes `seat` (docs/BOTS.md). The room shows it as a bot with the good thumb; the machine's own bot (ants_ai) is
    /// built by the application once the match begins and sends its commands with submit_bot(). False when the seat is taken, the room is full or Fog of War is on.
    bool add_bot(uint8_t seat, const std::string& name);
    void remove_bot(uint8_t seat);
    /// Host only, during the match: a command of the bot at `seat` (the issuer is stamped with that seat). False unless this machine is the host and the seat is a bot seat.
    /// The simulation's verdict arrives with the turn, like every command's; a bot ignores it.
    bool submit_bot(uint8_t seat, const sim::Command& command);
    /// Host only, at least two players: sends Start (the map named in the room, `seed`, the roster) to everybody and expects report_loaded()
    bool start_match(uint32_t seed, uint64_t map_hash);
    bool can_start() const;
    /// The map, seed, roster and names of the match (valid from Loading on)
    const StartMsg& start_info() const noexcept { return start_; }
    /// The application loaded the map of start_info() (true) or could not (a missing file, a different file)
    void report_loaded(bool ok);

    // ---- the match ---------------------------------------------------------------------------------------------------------------------------------
    /// The HUD's sink: the command is stamped with this machine's seat and queued for the next turn; the answer carries the predicted acknowledgement
    /// (the ant that will say "Yessir!") so that the click feels immediate although the order reaches the simulation a few turns later.
    sim::CommandResult submit(const sim::Command& command) override;
    /// Chat: the message goes through the host and comes back to everybody (own text included); use the callback to show it
    void chat(const std::string& text, bool team);
    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }
    /// After every simulation tick / for every applied command (see LockstepRunner); may be set before the match begins
    void set_on_tick(std::function<void()> fn);
    void set_on_command(std::function<void(const sim::Command&, const sim::CommandResult&)> fn);
    /// The match is over: the host stops sealing turns
    void freeze();

    /// True while the runner stands at a turn boundary and the next turn has not arrived (see LockstepRunner::stalled)
    bool stalled() const;
    /// How long the runner has stood still for want of a turn: the time since it last ran a tick (0 while it runs; the application shows "Waiting for the other players..." at
    /// one second)
    uint32_t stalled_ms() const;
    /// The host: the seat that holds the game up (255 when none does). A dedicated server never waits for anybody (see lag_notice).
    uint8_t laggard() const;
    /// A dedicated server's room: the player that the server announced as lagging, and how far behind the match it is in ms. The match goes on without waiting for it (it
    /// catches up on its own); this is the one-line notice for the others. Empty when nobody lags, and in games of the local network (their host waits: laggard()).
    struct LagNotice {
        uint8_t seat{255};
        uint32_t behind_ms{0};
    };
    std::optional<LagNotice> lag_notice() const;
    /// This machine is more than 3 s behind the match and runs the backlog down at up to four times normal speed (a dedicated server's room): "Catching up..."
    bool catching_up() const;
    /// The server told this player that it is the one who lags, and how far behind the match it is in ms (a dedicated server's room): the notice that reaches a player whose
    /// own link is slow, so that its backlog is on the way and not in its queue and "Catching up..." has nothing to say. Empty when none, and in games of the local network.
    std::optional<uint32_t> self_lag_behind_ms() const;
    bool desynced() const;
    /// Milliseconds into the current tick, for smooth drawing between ticks
    uint32_t sub_tick_ms() const;
    /// The turn the local machine executes next
    uint32_t turns_executed() const;
    /// The host is gone and the guests are agreeing on a new one: no turns arrive meanwhile (the game shows a message)
    bool electing() const;
    /// The seat that seals the turns now
    uint8_t host_seat() const noexcept { return known_host_; }
    /// What just happened in the match ("Bob is the host now."), for five seconds; empty otherwise
    std::string match_notice() const;

private:
    void update_host();
    void update_client();
    void update_host_session();
    void promote();
    void pump_peers();
    void begin_peer_links();
    /// The part of joining that every transport shares: the uplink is ready, the lobby asks for its room and seat
    void begin_client(std::unique_ptr<Connection> uplink, uint16_t peer_port, const std::string& name, uint8_t want_seat, const std::string& room, const std::string& token);
    void close_peer_links();
    void refresh_status();
    void set_notice(std::string text);
    void begin_match();
    void install_hooks();
    void shutdown_transport();
    void announce_room();
    LockstepRunner* runner() const;

    sim::SimulationEngine& sim_;
    Role role_{Role::None};
    Phase phase_{Phase::Off};
    uint8_t seat_{255};
    uint16_t listen_port_{0};
    uint32_t now_{0};
    std::string status_;
    FailReason fail_reason_{FailReason::None};
    RejectReason reject_reason_{RejectReason::BadRequest};
    std::string notice_;                    // a message that replaces the standing prompt for a few seconds (a cancelled start, ...)
    uint32_t notice_until_ms_{0};
    uint32_t phase_since_ms_{0};
    bool loaded_reported_{false};
    RoomMsg room_;
    StartMsg start_;
    std::vector<Event> events_;
    bool desync_reported_{false};
    uint8_t known_host_{255};               // the seat of the host as far as this machine knows (changes with a host migration)
    uint16_t peer_port_{0};                 // guest: the port on which the other guests connect (announced in Hello)
    uint16_t discovery_port_{kLanDiscoveryPort};   // 0: the room is not announced
    bool discovery_loopback_only_{false};
    [[maybe_unused]] bool room_loopback_only_{false};   // host(): the door accepts this machine only, so the announcements stay here too (native builds)
    [[maybe_unused]] uint32_t room_id_{0};              // names the room in the announcements (native builds)
    std::string game_version_;

    std::function<void(const ChatMsg&)> on_chat_;
    std::function<void()> on_wake_;
    std::function<void()> on_tick_;
    std::function<void(const sim::Command&, const sim::CommandResult&)> on_command_;

    // transport (owned here, borrowed by the lobbies and sessions)
    struct Transport;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<HostLobby> host_lobby_;
    std::unique_ptr<ClientLobby> client_lobby_;
    std::unique_ptr<HostSession> host_session_;
    std::unique_ptr<ClientSession> client_session_;
};

}  // namespace ants::net
