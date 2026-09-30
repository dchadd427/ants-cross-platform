#pragma once

// The two halves of a lock-step match over a Connection: the host's session (sequencer, broadcast, its own simulation) and a client's session.
// Both are driven from the game's main loop with a monotonic millisecond clock and never block.
//
//   client: submit(command) -> CommandMsg -> host
//   host:   Sequencer::submit (issuer stamped) ... every 100 ms: seal a turn -> TurnMsg to every client and to its own runner
//   both:   LockstepRunner executes the turns on the local SimulationEngine (2 ticks each), then reports TurnAck and, every 10 turns, HashMsg
//   host:   compares the hashes; a mismatch is broadcast as DesyncMsg
//
// The roster and the shared start state (map, seed, fog) are established before start() by the lobby; the sessions only run the match.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/lockstep.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/transport.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::net {

class HostSession {
public:
    struct Config {
        uint8_t host_player{0};
        Sequencer::Config sequencer{};
        LockstepRunner::Config runner{};
        uint32_t violation_limit{8};        // undecodable or forbidden messages before a client is thrown out
    };

    HostSession(sim::SimulationEngine& sim, Config config);

    /// A remote player of the roster (before start()). The connection outlives the session.
    void add_client(uint8_t player, Connection* connection);
    /// Begins the match at `now_ms`: turn 0 is sealed at once.
    void start(uint32_t now_ms);
    bool started() const noexcept { return started_; }

    /// A command of the host's own player
    void submit_local(sim::Command command);
    /// Relays a chat text of the host's own player
    void chat_local(const std::string& text, bool team);

    void update(uint32_t now_ms);

    /// Stops sealing turns: the match is over, or two peers disagree (a desync freezes the match so that its state can be examined). Peers still
    /// execute the turns they have. There is no un-freeze: a frozen match is finished.
    void freeze() noexcept { frozen_ = true; }
    bool frozen() const noexcept { return frozen_; }

    /// Called when a player leaves the match (its connection closed, it was thrown out): the game eliminates it (drop-out)
    void set_on_player_left(std::function<void(uint8_t)> fn) { on_left_ = std::move(fn); }
    /// Called for every chat message the host receives from a client (already relayed) and for its own
    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }

    const std::vector<DesyncMsg>& desyncs() const noexcept { return desyncs_; }
    uint32_t turns_sealed() const noexcept { return sequencer_.next_turn(); }
    bool waiting() const noexcept { return started_ && !sequencer_.can_seal(); }
    uint8_t laggard() const noexcept { return sequencer_.laggard(); }
    bool client_present(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && clients_[player].present; }
    uint32_t violations(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? clients_[player].violations : 0; }
    LockstepRunner& runner() noexcept { return runner_; }
    const Sequencer& sequencer() const noexcept { return sequencer_; }

private:
    struct Client {
        Connection* conn{nullptr};
        bool present{false};
        uint32_t violations{0};
    };
    void poll_clients();
    void handle_message(uint8_t player, const std::vector<uint8_t>& msg);
    void violation(uint8_t player);
    void drop(uint8_t player);
    void broadcast(const std::vector<uint8_t>& msg, uint8_t except = 255);
    void report_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash);
    void run_local(uint32_t dt_ms);

    Config cfg_;
    Sequencer sequencer_;
    LockstepRunner runner_;
    std::array<Client, sim::MAX_PLAYERS> clients_{};
    std::vector<DesyncMsg> desyncs_;
    std::function<void(uint8_t)> on_left_;
    std::function<void(const ChatMsg&)> on_chat_;
    bool started_{false};
    bool frozen_{false};
    uint32_t last_ms_{0};
    uint32_t next_seal_ms_{0};
};

class ClientSession {
public:
    struct Config {
        uint8_t player{1};
        LockstepRunner::Config runner{};
        uint32_t ping_every_ms{1000};
    };

    ClientSession(sim::SimulationEngine& sim, Config config);

    void set_connection(Connection* connection) { conn_ = connection; }
    void start(uint32_t now_ms);
    bool started() const noexcept { return started_; }

    /// The player's command, sent to the host (which stamps the issuer)
    bool submit(sim::Command command);
    bool chat(const std::string& text, bool team);
    /// The player quits the match
    void leave();

    void update(uint32_t now_ms);

    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }

    /// The host reported that this client's simulation differs from its own (the match must stop)
    bool desynced() const noexcept { return desynced_; }
    const DesyncMsg& desync() const noexcept { return desync_; }
    bool connected() const noexcept { return conn_ != nullptr && conn_->is_open(); }
    uint32_t rtt_ms() const noexcept { return rtt_ms_; }
    LockstepRunner& runner() noexcept { return runner_; }

private:
    Config cfg_;
    LockstepRunner runner_;
    Connection* conn_{nullptr};
    std::function<void(const ChatMsg&)> on_chat_;
    bool started_{false};
    bool desynced_{false};
    DesyncMsg desync_{};
    uint32_t last_ms_{0};
    uint32_t next_ping_ms_{0};
    uint32_t ping_nonce_{0};
    uint32_t rtt_ms_{0};
};

}  // namespace ants::net
