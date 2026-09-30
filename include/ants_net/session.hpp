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
//
// Host migration. The original has no host once a match runs (its players form a mesh), so its host could drop out and the game went on. Here every
// machine holds the whole simulation, so "host" is only the role of sealing turns, and it can move. Guests keep links to each other (set_peer). When
// a guest loses its host (the link is closed or silent) the guests elect the lowest living seat: it proposes itself, the others accept or refuse, it
// collects how far each got, fetches the turns it misses from the one that got furthest and takes over. Its ClientSession then reports promoted();
// promote_to_host() turns it into a HostSession that goes on with the same runner and tells the other guests (Resume), who switch their host link.
// The old host and every seat that did not follow are dropped by the first turn of the new host. Commands that were in flight when the host went
// are lost. See docs/NETWORK_PORT.md.

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

/// What a machine that takes over as host knows about a guest that agreed to follow it: the link and how far the guest got
struct PeerState {
    uint8_t seat{255};
    Connection* conn{nullptr};
    uint32_t next_receive{0};     // the first turn the guest has not received yet
    uint32_t next_execute{0};     // the first turn it has not executed yet
};

class HostSession {
public:
    struct Config {
        uint8_t host_player{0};
        uint8_t epoch{0};                   // 0 for the first host, one more for every host change
        Sequencer::Config sequencer{};
        LockstepRunner::Config runner{};
        uint32_t violation_limit{8};        // undecodable or forbidden messages before a client is thrown out
        uint32_t silence_timeout_ms{60000}; // a client that sends nothing for this long is dropped (the original's 60 s drop-out)
    };

    HostSession(sim::SimulationEngine& sim, Config config);
    /// A guest that becomes the host keeps its runner (with the log of the last turns and the presentation hooks): see promote_to_host()
    HostSession(sim::SimulationEngine& sim, Config config, std::unique_ptr<LockstepRunner> runner);

    /// A remote player of the roster (before start()). The connection outlives the session.
    void add_client(uint8_t player, Connection* connection);
    /// Begins the match at `now_ms`: turn 0 is sealed at once.
    void start(uint32_t now_ms);
    /// Continues a match in which the previous host left: the next turn sealed is `resume_turn`, every survivor is told (Resume) and gets the turns
    /// it misses, and the first turn drops the old host and every seat of the roster that did not follow. Instead of start().
    void resume(uint32_t now_ms, uint32_t resume_turn, uint8_t old_host, const std::vector<PeerState>& survivors);
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

    /// Called when a player leaves the match (its connection closed, it was thrown out, it fell silent). The drop-out itself travels in the turn
    /// stream (a Drop command of the sequencer), so that every machine drops the team at the same tick; this callback is for the presentation.
    void set_on_player_left(std::function<void(uint8_t)> fn) { on_left_ = std::move(fn); }
    /// Called for every chat message the host receives from a client (already relayed) and for its own
    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }

    const std::vector<DesyncMsg>& desyncs() const noexcept { return desyncs_; }
    uint32_t turns_sealed() const noexcept { return sequencer_.next_turn(); }
    bool waiting() const noexcept { return started_ && !sequencer_.can_seal(); }
    uint8_t laggard() const noexcept { return sequencer_.laggard(); }
    bool client_present(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && clients_[player].present; }
    uint32_t violations(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? clients_[player].violations : 0; }
    LockstepRunner& runner() noexcept { return *runner_; }
    const Sequencer& sequencer() const noexcept { return sequencer_; }
    uint8_t epoch() const noexcept { return cfg_.epoch; }
    uint8_t host_player() const noexcept { return cfg_.host_player; }

private:
    struct Client {
        Connection* conn{nullptr};
        bool present{false};
        uint32_t violations{0};
        uint32_t last_heard_ms{0};          // when the client last sent anything
    };
    void poll_clients();
    void handle_message(uint8_t player, const std::vector<uint8_t>& msg);
    void violation(uint8_t player);
    void drop(uint8_t player);
    void announce_drop(uint8_t player);
    void broadcast(const std::vector<uint8_t>& msg, uint8_t except = 255);
    void report_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash);
    void run_local(uint32_t dt_ms);
    void send_turns(Connection* conn, uint32_t from_turn, uint32_t to_turn);

    Config cfg_;
    sim::SimulationEngine* sim_;
    Sequencer sequencer_;
    std::unique_ptr<LockstepRunner> runner_;
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
        uint8_t host{0};                     // the seat of the host
        LockstepRunner::Config runner{};
        uint32_t ping_every_ms{1000};
        uint32_t host_silence_ms{10000};     // a host that says nothing for this long is gone (its answers to our pings keep it alive)
        uint32_t host_alive_ms{2000};        // a host heard from this recently is alive when a peer claims otherwise
        uint32_t peer_silence_ms{5000};      // a peer that says nothing for this long is not alive
        uint32_t accept_timeout_ms{3000};    // a candidate waits this long for the answers to its proposal
        uint32_t elect_timeout_ms{4000};     // a guest waits this long for the proposal of the seat it expects to take over
        uint32_t fetch_timeout_ms{3000};     // a candidate waits this long for the turns it misses
        uint32_t retry_ms{1000};             // a candidate whose proposal was refused because the others' host lives asks again after this
        uint32_t host_refusals_limit{3};     // ... and gives up (Lost) after this many such refusals: it is this machine that lost the host
        uint32_t election_limit_ms{30000};   // no host after this long: the match cannot go on here
    };
    /// Normal: following the host. Electing / Following / Fetching: the host is gone and a new one is being chosen (no turns arrive meanwhile).
    /// Promoted: this machine is the new host (see promote_to_host). Lost: the other players cannot be reached, or this machine was cut off.
    enum class Mode : uint8_t { Normal, Electing, Following, Fetching, Promoted, Lost };
    struct Promotion {
        uint8_t epoch{0};
        uint8_t old_host{255};
        uint32_t resume_turn{0};
        std::vector<PeerState> survivors;    // the guests that agreed, with the links to them
    };

    ClientSession(sim::SimulationEngine& sim, Config config);

    void set_connection(Connection* connection) { conn_ = connection; }
    /// A link to another guest, for host migration; the connection outlives the session
    void set_peer(uint8_t seat, Connection* link);
    void start(uint32_t now_ms);
    bool started() const noexcept { return started_; }

    /// The player's command, sent to the host (which stamps the issuer). False when there is no host (an election is going on).
    bool submit(sim::Command command);
    bool chat(const std::string& text, bool team);
    /// The player quits the match
    void leave();
    /// The match is over: a host that goes now is no reason to look for a new one
    void finish() noexcept { finished_ = true; }

    void update(uint32_t now_ms);

    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }

    /// The host reported that this client's simulation differs from its own (the match must stop)
    bool desynced() const noexcept { return desynced_; }
    const DesyncMsg& desync() const noexcept { return desync_; }
    bool connected() const noexcept { return conn_ != nullptr && conn_->is_open(); }
    uint32_t rtt_ms() const noexcept { return rtt_ms_; }
    /// When anything last arrived from the host
    uint32_t last_heard_ms() const noexcept { return last_heard_ms_; }
    LockstepRunner& runner() noexcept { return *runner_; }

    uint8_t player() const noexcept { return cfg_.player; }
    Mode mode() const noexcept { return mode_; }
    /// The number of host changes so far, and the seat of the current host
    uint8_t epoch() const noexcept { return epoch_; }
    uint8_t host_seat() const noexcept { return host_seat_; }
    bool promoted() const noexcept { return mode_ == Mode::Promoted; }
    bool lost() const noexcept { return mode_ == Mode::Lost; }
    /// True while the host is gone and a new one is being chosen
    bool electing() const noexcept { return mode_ == Mode::Electing || mode_ == Mode::Following || mode_ == Mode::Fetching; }
    const Promotion& promotion() const noexcept { return promotion_; }
    /// For promote_to_host(): the runner leaves with the promotion
    std::unique_ptr<LockstepRunner> release_runner() { return std::move(runner_); }

private:
    struct Answer {
        bool answered{false};
        bool accepted{false};
        uint8_t lowest{255};
        uint32_t next_receive{0};
        uint32_t next_execute{0};
    };
    void poll_peers(uint32_t now_ms);
    void poll_host(uint32_t now_ms);
    void handle_peer_message(uint8_t seat, const std::vector<uint8_t>& msg, uint32_t now_ms);
    void on_propose(uint8_t seat, const ProposeMsg& m, uint32_t now_ms);
    void on_refuse(uint8_t seat, const RefuseMsg& m, uint32_t now_ms);
    void on_resume(uint8_t seat, const ResumeMsg& m, uint32_t now_ms);
    void begin_election(uint32_t now_ms);
    void election_tick(uint32_t now_ms);
    void propose(uint32_t now_ms);
    void withdraw(uint8_t in_favour_of);
    void resolve(uint32_t now_ms);
    void plan_fetch(uint32_t now_ms);
    void fetch_tick(uint32_t now_ms);
    void promote();
    bool peer_alive(uint8_t seat, uint32_t now_ms) const;
    bool host_alive(uint32_t now_ms) const;
    bool all_answered(uint32_t now_ms) const;
    uint8_t lowest_alive(uint32_t now_ms) const;
    uint8_t election_epoch() const noexcept { return static_cast<uint8_t>(epoch_ + 1); }
    void send_turns(Connection* link, uint32_t from_turn);
    void send_to_peer(uint8_t seat, const std::vector<uint8_t>& msg);
    void go_lost();

    Config cfg_;
    std::unique_ptr<LockstepRunner> runner_;
    Connection* conn_{nullptr};
    std::array<Connection*, sim::MAX_PLAYERS> peers_{};
    std::array<uint32_t, sim::MAX_PLAYERS> peer_heard_{};
    std::array<uint32_t, sim::MAX_PLAYERS> next_request_ok_ms_{};
    std::function<void(const ChatMsg&)> on_chat_;
    bool started_{false};
    bool finished_{false};
    bool desynced_{false};
    DesyncMsg desync_{};
    uint32_t last_ms_{0};
    uint32_t next_ping_ms_{0};
    uint32_t next_peer_ping_ms_{0};
    uint32_t ping_nonce_{0};
    uint32_t rtt_ms_{0};
    uint32_t last_heard_ms_{0};

    // host migration
    Mode mode_{Mode::Normal};
    uint8_t host_seat_{0};
    uint8_t old_host_{255};
    uint8_t epoch_{0};
    uint32_t elect_start_ms_{0};
    uint32_t wait_start_ms_{0};                         // since when we wait for a lower seat to propose
    bool proposed_{false};
    uint32_t proposal_ms_{0};
    uint8_t expected_mask_{0};                          // the peers a proposal was sent to
    uint8_t following_{255};                            // the candidate whose proposal we accepted
    uint8_t hint_{255};                                 // a lower seat that a peer named alive and that we wait for (whatever our own view says)
    uint32_t host_refusals_{0};                         // proposals refused because the host lives for the others
    uint32_t hold_until_ms_{0};                         // no new proposal before this time
    uint32_t follow_start_ms_{0};
    uint8_t fetch_from_{255};
    uint32_t fetch_target_{0};
    uint32_t fetch_progress_ms_{0};
    std::array<Answer, sim::MAX_PLAYERS> answers_{};
    std::array<bool, sim::MAX_PLAYERS> dead_{};         // seats given up on in this election
    Promotion promotion_;
};

/// Turns a promoted ClientSession into the HostSession of the new host: it keeps the runner, tells the survivors (Resume) and drops the old host and
/// every seat that did not follow. `config` supplies the timeouts and limits; the seat and the epoch come from the promotion. `prepare` runs before
/// the first drop is announced (to install the callbacks). Null unless promoted() and the runner is still there.
std::unique_ptr<HostSession> promote_to_host(ClientSession& client, sim::SimulationEngine& sim, HostSession::Config config, uint32_t now_ms,
                                             const std::function<void(HostSession&)>& prepare = {});

}  // namespace ants::net
