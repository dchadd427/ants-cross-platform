#pragma once

// The network as the application sees it: one object that owns the listener and the connections, runs the room before the match and the session
// during it, and gives the HUD its CommandSink. It is driven from the main loop with a monotonic millisecond clock (`update`), never blocks, has no
// threads, and does not know about SDL, the renderer or the map files: the application loads the map when the room says so and reports back.
//
//   host:   host(port, name) -> room (guests join, the host picks the map and the fog) -> start_match(seed, map hash) -> every machine loads
//           (StartRequested event, report_loaded) -> Begun -> the match runs -> freeze() at its end
//   client: join(address, port, name) -> room (follows the host's map and fog) -> StartRequested -> load, report_loaded -> Begun -> the match runs
//
// Raw TCP (LAN and development) is the transport of native builds; a WebAssembly build has none yet (host() and join() return false there) until the
// WebRTC transport of the network port arrives. The class does not care which Connection it talks to.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/lobby.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

/// FNV-1a 64 over the bytes of a file (the map identity that the start barrier compares); false when the file cannot be read.
bool hash_file(const std::string& path, uint64_t& out);

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
    struct Event {
        enum class Type : uint8_t {
            RoomChanged,      // somebody joined or left, or the host changed the map or the fog
            StartRequested,   // load start_info() now and call report_loaded()
            Begun,            // everybody is loaded: the match runs
            Cancelled,        // the start failed (a player left, a map did not load): back in the room
            PlayerLeft,       // during the match: `seat` dropped out
            HostLeft,         // during the match (client): the connection to the host is gone
            Desync,           // two machines disagree: the match is frozen
            Failed            // joining failed, see status_text()
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
    bool join(const std::string& address, uint16_t port, const std::string& name);
    /// Leaves for good: tells the others (a guest says Leave), closes every connection. The others see the host or the guest gone.
    void leave();

    // ---- every frame -----------------------------------------------------------------------------------------------------------------------------
    void update(uint32_t now_ms);
    std::vector<Event> take_events();

    // ---- state -----------------------------------------------------------------------------------------------------------------------------------
    Role role() const noexcept { return role_; }
    Phase phase() const noexcept { return phase_; }
    bool is_host() const noexcept { return role_ == Role::Host; }
    bool active() const noexcept { return role_ != Role::None; }
    uint8_t my_seat() const noexcept { return seat_; }
    uint16_t listen_port() const noexcept { return listen_port_; }
    /// The room as this machine sees it (the host's own copy, or the last one received)
    const RoomMsg& room() const noexcept { return room_; }
    /// One line for the screen, in the original's words: what the machine is waiting for ("Press START when all players' thumbs have appeared.",
    /// "Waiting for the host to start the game...", "Trying to connect to the host..." and its 30 s / 60 s successors), or why it failed
    const std::string& status_text() const noexcept { return status_; }
    /// The thumb beside a player's name: the host's measured round trip to that seat (the host's own seat is always good)
    LinkQuality seat_quality(uint8_t seat) const noexcept;

    // ---- the host's controls in the room ---------------------------------------------------------------------------------------------------------
    /// Host only: the map every machine will load (a plain .LVL file name of the maps folder) and the Fog of War option
    void set_map(const std::string& map_name);
    void set_fog(bool fog);
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

    /// True while the next turn is due and has not arrived (the game shows "waiting")
    bool stalled() const;
    /// How long the runner has been stalled (0 when it is not)
    uint32_t stalled_ms() const;
    /// The host: the seat that holds the game up (255 when none does)
    uint8_t laggard() const;
    bool desynced() const;
    /// Milliseconds into the current tick, for smooth drawing between ticks
    uint32_t sub_tick_ms() const;
    uint32_t rtt_ms() const;
    /// The turn the local machine executes next
    uint32_t turns_executed() const;

private:
    void update_host();
    void update_client();
    void refresh_status();
    void set_notice(std::string text);
    void begin_match();
    void install_hooks();
    void shutdown_transport();
    LockstepRunner* runner() const;

    sim::SimulationEngine& sim_;
    Role role_{Role::None};
    Phase phase_{Phase::Off};
    std::string name_;
    uint8_t seat_{255};
    uint16_t listen_port_{0};
    uint32_t now_{0};
    std::string status_;
    std::string notice_;                    // a message that replaces the standing prompt for a few seconds (a cancelled start, ...)
    uint32_t notice_until_ms_{0};
    uint32_t phase_since_ms_{0};
    bool loaded_reported_{false};
    RoomMsg room_;
    StartMsg start_;
    std::vector<Event> events_;
    bool desync_reported_{false};
    uint32_t stall_since_ms_{0};
    bool stall_active_{false};

    std::function<void(const ChatMsg&)> on_chat_;
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
