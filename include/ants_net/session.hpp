#pragma once

// The two halves of a lock-step match over a Connection: the host's session (sequencer, broadcast, its own simulation) and a client's session.
// Both are driven from the game's main loop with a monotonic millisecond clock and never block.
//
//   client: submit(command) -> CommandMsg -> host
//   host:   Sequencer::submit (issuer stamped) ... every 50 ms: seal a turn -> TurnMsg to every client and to its own runner
//   both:   LockstepRunner executes the turns on the local SimulationEngine (one tick each), then reports TurnAck (once per frame, for the last turn it ran) and, every 20
//           turns, HashMsg
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
//
// Who waits for whom. A host with a seat (a game on the local network) stops sealing while a peer is more than 3 s behind: its friends wait for the slow machine (the
// match is theirs, and the machine may be back in a moment). A host without a seat (a dedicated server) never waits: a room is made of strangers, and one machine that
// stalls (a window in the background, a laptop that went to sleep) must not slow down the others. The server keeps sealing every 50 ms; the player that falls behind
// catches up on its own at up to four times normal speed (its runner is told by the length of its queue, lockstep.hpp) and its commands apply when they arrive; the room is
// told with a Lag message ("Bob is lagging (12 s behind)") from 3 s behind, once a second, and that it is back when it is within one second; the laggard itself gets the
// notice too ("You are lagging (12 s behind)"): a player whose own link is the slow one has its backlog on the way, not in its queue, and nothing else tells it. A player that
// is 60 s behind, or whose acks have not moved for 30 s although it is connected, is dropped like a player that left (a Drop in the turn stream): its link is closed, and a
// machine that finds its link closed with half a minute of the match unplayed in its queue says so ("You were away too long and were dropped from the match.").
// A command that does not fit into a turn (more than the 64 a player may have in one) waits for the next turns (up to 256 of them) in the sequencer instead of costing the
// player a violation: a stuck uplink that is let go delivers 76 orders at once.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/flood.hpp"
#include "ants_net/latency.hpp"
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

/// "No seat": a dedicated server's host plays no player (HostSession::Config::host_player, HostLobby::Config::host_seat)
inline constexpr uint8_t kNoSeat = 255;

/// The numbers of the lag policy of a dedicated server (see above), in ms of play: a player this far behind the match gets a notice to the others and its own "Catching up..."
inline constexpr uint32_t kLagNoticeMs = 3000;
/// ... the notice ends when the player is back within this
inline constexpr uint32_t kLagClearMs = 1000;
/// ... and while it lasts it is repeated this often, so that the number shown follows
inline constexpr uint32_t kLagNoticeEveryMs = 1000;
/// ... a player this far behind is dropped
inline constexpr uint32_t kLagDropBehindMs = 60000;
/// ... and so is a player whose acks have not moved for this long while turns wait for it (it is connected, but it runs nothing)
inline constexpr uint32_t kLagDropIdleMs = 30000;
/// A stale notice is not shown for longer than this without a renewal (the server renews it every kLagNoticeEveryMs)
inline constexpr uint32_t kLagNoticeStaleMs = 3000;
/// ... the notice about the player ITSELF lives longer: it reaches a player whose own link is the slow one (that is the case it is for) behind the turns that wait in front of it, so
/// a notice every second is read every few seconds
inline constexpr uint32_t kSelfLagNoticeStaleMs = 10000;
/// A server that closes the link of a player who holds this much of the match unplayed (the turns that this machine received and did not run) dropped it for being away: the
/// idle rule drops a player whose acknowledgements stood still for kLagDropIdleMs, so at least that much waits in its queue when it is back (a process that was stopped, a hidden
/// tab). Ten seconds are left for the turns that were still on their way. A link that fails by itself, or a server that dies, finds nothing like it.
inline constexpr uint32_t kAwayBacklogMs = kLagDropIdleMs - 10000;

class HostSession {
public:
    struct Config {
        uint8_t host_player{0};             // the host's own seat; kNoSeat (255) for a dedicated server: the host plays nobody, runs the match as the referee
        uint8_t epoch{0};                   // 0 for the first host, one more for every host change
        Sequencer::Config sequencer{};
        LockstepRunner::Config runner{};
        uint32_t violation_limit{8};        // undecodable or forbidden messages before a client is thrown out
        uint32_t silence_timeout_ms{60000}; // a client that sends nothing for this long is dropped (the original's 60 s drop-out)
        uint32_t laggard_drop_ms{0};        // a host WITH a seat: a seat that has held the match up this long (sealing stopped because it does not execute the turns) is
                                            // dropped; 0 = never: a game between friends waits for a slow machine. A dedicated server does not wait (the lag_* fields)
        uint32_t lag_notice_ms{kLagNoticeMs};            // a host WITHOUT a seat: a player this far behind the match is announced to the others (and not waited for)
        uint32_t lag_clear_ms{kLagClearMs};              // ... it is announced back when it is within this
        uint32_t lag_notice_every_ms{kLagNoticeEveryMs}; // ... the announcement is repeated this often while the lag lasts
        uint32_t lag_drop_behind_ms{kLagDropBehindMs};   // ... dropped when this far behind (0: never)
        uint32_t lag_drop_idle_ms{kLagDropIdleMs};       // ... dropped when its acks have not moved for this long while turns wait for it (0: never)
        uint32_t message_burst{kMessageBurst};                  // flood control (flood.hpp): the messages that one client may send, a token bucket; a message beyond it is not
        uint32_t messages_per_second{kMessagesPerSecond};       // handled and is a violation
    };

    HostSession(sim::SimulationEngine& sim, Config config);
    /// A guest that becomes the host keeps its runner (with the log of the last turns and the presentation hooks): see promote_to_host()
    HostSession(sim::SimulationEngine& sim, Config config, std::unique_ptr<LockstepRunner> runner);
    /// The runner's hooks capture `this` (the delay meter): a session that was copied or moved would leave them pointing at the one that is gone
    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;
    HostSession(HostSession&&) = delete;
    HostSession& operator=(HostSession&&) = delete;

    /// A remote player of the roster (before start()). The connection outlives the session.
    void add_client(uint8_t player, Connection* connection);
    /// A computer player's seat of the roster (before start(); docs/BOTS.md). It has no connection: the host acknowledges every turn for it (otherwise the
    /// sequencer would stall after max_lag_turns turns), it never reports a hash and is never dropped for silence. Its commands come from submit_bot().
    void add_bot_seat(uint8_t player);
    bool is_bot_seat(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && (bot_seats_ & (1u << player)) != 0; }
    /// A command of a bot seat's bot for the next turn (the issuer is stamped by the sequencer). False unless the match runs, the seat is a bot seat and the
    /// command is one that a player may send (never Drop).
    bool submit_bot(uint8_t player, sim::Command command);
    /// Begins the match at `now_ms`: turn 0 is sealed at once.
    void start(uint32_t now_ms);
    /// Continues a match in which the previous host left: the next turn sealed is `resume_turn`, every survivor is told (Resume) and gets the turns
    /// it misses, and the first turn drops the old host and every seat of the roster that did not follow. Instead of start().
    void resume(uint32_t now_ms, uint32_t resume_turn, uint8_t old_host, const std::vector<PeerState>& survivors);

    /// A command of the host's own player (a host without a seat has none: ignored)
    void submit_local(sim::Command command);
    /// Relays a chat text of the host's own player (a host without a seat has none: ignored)
    void chat_local(const std::string& text, bool team);
    /// True when the host plays no seat (a dedicated server): it is the sequencer and the referee only, and it never waits for a player that falls behind
    bool seatless() const noexcept { return cfg_.host_player >= sim::MAX_PLAYERS; }
    /// The players that are announced as lagging (a bit per seat), and how far behind they are in ms (a host without a seat only)
    uint8_t lagging_mask() const noexcept;
    uint32_t behind_ms(uint8_t player) const noexcept { return sequencer_.behind_turns(player) * kTurnMs; }

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
    /// True while a host with a seat has stopped sealing because a peer is more than 3 s behind (a host without a seat never waits)
    bool waiting() const noexcept { return started_ && !seatless() && !sequencer_.can_seal(); }
    uint8_t laggard() const noexcept { return seatless() ? uint8_t{255} : sequencer_.laggard(); }
    bool client_present(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS && clients_[player].present; }
    uint32_t violations(uint8_t player) const noexcept { return player < sim::MAX_PLAYERS ? clients_[player].violations : 0; }
    /// The StartRequests that reached the running match of a dedicated server (the leader's second click crossed the Start): heard and ignored, all clients together
    uint32_t ignored_start_requests() const noexcept {
        uint32_t n = 0;
        for (const Client& c : clients_) n += c.ignored_start_requests;
        return n;
    }
    LockstepRunner& runner() noexcept { return *runner_; }
    uint8_t epoch() const noexcept { return cfg_.epoch; }
    uint8_t host_player() const noexcept { return cfg_.host_player; }
    /// The delay of the host's own commands (latency.hpp): from the hand-over to the sequencer to the tick that applies them on this machine. Nothing is measured for a
    /// host without a seat.
    const CommandDelayMeter& command_delay() const noexcept { return delay_; }

private:
    struct Client {
        Connection* conn{nullptr};
        bool present{false};
        uint32_t violations{0};
        uint32_t last_heard_ms{0};          // when the client last sent anything
        MessageBudget talk;                 // flood control: every message that the client sends takes one from it
        uint32_t ignored_start_requests{0}; // its StartRequests that arrived in the running match (the first kIgnoredStartRequestsAllowed are free)
        bool lagging{false};                // announced to the others as lagging (a host without a seat)
        uint32_t next_notice_ms{0};         // when the announcement is repeated
        uint32_t acked_seen{0};             // the ack the last progress check saw, and when it moved (lag_drop_idle_ms)
        uint32_t progress_ms{0};
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
    void police_laggards(uint32_t now_ms);
    void announce_lag(uint8_t player, uint32_t behind_ms);

    Config cfg_;
    sim::SimulationEngine* sim_;
    Sequencer sequencer_;
    std::unique_ptr<LockstepRunner> runner_;
    std::array<Client, sim::MAX_PLAYERS> clients_{};
    uint8_t bot_seats_{0};                  // bit s: seat s is a bot (no connection; acknowledged by this host). A new host after a migration has none: the bots leave with the old host
    std::vector<DesyncMsg> desyncs_;
    std::function<void(uint8_t)> on_left_;
    std::function<void(const ChatMsg&)> on_chat_;
    bool started_{false};
    bool frozen_{false};
    uint32_t last_ms_{0};
    CommandDelayMeter delay_;
    uint32_t next_seal_ms_{0};
    uint8_t stall_player_{255};              // the seat that holds the match up, and since when (laggard_drop_ms)
    uint32_t stall_since_ms_{0};
};

class ClientSession {
public:
    struct Config {
        uint8_t player{1};
        uint8_t host{0};                     // the seat of the host (kNoSeat for a dedicated server's room)
        bool migration{true};                // false (a dedicated server): the host is not a player and nobody can take over: a lost link ends the match here (Lost)
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
    /// The runner's hooks capture `this` (the delay meter): a session that was copied or moved would leave them pointing at the one that is gone
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;
    ClientSession(ClientSession&&) = delete;
    ClientSession& operator=(ClientSession&&) = delete;

    void set_connection(Connection* connection) { conn_ = connection; }
    /// A link to another guest, for host migration; the connection outlives the session
    void set_peer(uint8_t seat, Connection* link);
    void start(uint32_t now_ms);

    /// The player's command, sent to the host (which stamps the issuer). False when there is no host (an election is going on).
    bool submit(sim::Command command);
    bool chat(const std::string& text, bool team);
    /// The player quits the match
    void leave();
    /// The match is over: a host that goes now is no reason to look for a new one
    void finish() noexcept { finished_ = true; }

    void update(uint32_t now_ms);
    /// `gap_ms` of real time went by that the clock of update() did not count (a page that the browser did not wake for a while: the application hands the session at most a
    /// second per wake-up, so that the lock-step runner is not paid back more at once). A host that said nothing in the update that has just run (the link is read first: what
    /// waited was heard) has been silent for that time too, and its silence is counted in real time. But only a host that has been ASKED can be called silent: a page that
    /// sleeps sends no pings, and a live host that holds its turns says nothing either. So a host that has not been asked since it was last heard (no ping went out after it)
    /// gets the gap only up to kAskGraceMs short of the limit (the update that has just run sent the ping); its answer is a message, which wakes the page and is heard, and a
    /// host that does not answer is called silent by the next wake-up (the gap counts in full once a ping is outstanding). True when the stamp was touched.
    bool note_gap(uint32_t gap_ms);
    /// How long a host has to answer the ping that asks whether it lives, before the silence of the gap is held against it (see note_gap)
    static constexpr uint32_t kAskGraceMs = 3000;

    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }

    /// The host reported that this client's simulation differs from its own (the match must stop)
    bool desynced() const noexcept { return desynced_; }
    const DesyncMsg& desync() const noexcept { return desync_; }
    bool connected() const noexcept { return conn_ != nullptr && conn_->is_open(); }
    /// The last round trip to the host that a Pong measured (0 before the first one)
    uint32_t rtt_ms() const noexcept { return ping_.last_ms(); }
    /// The round trip to the host as this machine measures it: the mean of the last few answers to the once-a-second pings (latency.hpp)
    const PingMeter& ping() const noexcept { return ping_; }
    /// The delay of this player's commands: from the send to the tick that applies them on this machine (latency.hpp)
    const CommandDelayMeter& command_delay() const noexcept { return delay_; }
    LockstepRunner& runner() noexcept { return *runner_; }

    /// A dedicated server's room (no seat for the host, no migration): the server does not wait for a player that falls behind (see above)
    bool dedicated() const noexcept { return cfg_.host >= sim::MAX_PLAYERS; }
    /// The player that the server announced as lagging (the one that is furthest behind), 255 when none; and how far behind it was at the last announcement. An announcement
    /// that was not renewed for kLagNoticeStaleMs is gone (the player is back, or dropped, or the host is silent).
    uint8_t lagging_seat() const noexcept;
    uint32_t lagging_behind_ms() const noexcept;
    /// This machine is more than 3 s behind the match (the turns it holds and has not run, in a dedicated server's room) and runs the backlog down at up to four times normal
    /// speed: the screen says "Catching up..." until it is within one second.
    bool catching_up() const noexcept { return catching_up_; }
    /// The server's own word about this player: how far behind the match it is in ms, from the last Lag notice about THIS seat that was not ended by one with 0 and did not
    /// go stale (kSelfLagNoticeStaleMs); 0 when there is none. It tells a player whose backlog is not in its own queue (its link is slow: the turns are still on their way, so
    /// "Catching up..." has nothing to say) that it is the one who lags.
    uint32_t self_lag_behind_ms() const noexcept { return last_ms_ - self_lag_.heard_ms > kSelfLagNoticeStaleMs ? 0u : self_lag_.behind_ms; }

    uint8_t player() const noexcept { return cfg_.player; }
    Mode mode() const noexcept { return mode_; }
    /// The number of host changes so far, and the seat of the current host
    uint8_t epoch() const noexcept { return epoch_; }
    uint8_t host_seat() const noexcept { return host_seat_; }
    bool promoted() const noexcept { return mode_ == Mode::Promoted; }
    bool lost() const noexcept { return mode_ == Mode::Lost; }
    /// Why the match is lost to this machine (Mode::Lost): its link to the server closed while it held kAwayBacklogMs or more of the match unplayed (it was dropped for being away),
    /// or the other players cannot be reached for any other reason
    enum class LostReason : uint8_t { None, Connection, AwayTooLong };
    LostReason lost_reason() const noexcept { return lost_reason_; }
    /// True while the host is gone and a new one is being chosen
    bool electing() const noexcept { return mode_ == Mode::Electing || mode_ == Mode::Following || mode_ == Mode::Fetching; }
    const Promotion& promotion() const noexcept { return promotion_; }
    /// For promote_to_host(): the runner leaves with the promotion
    std::unique_ptr<LockstepRunner> release_runner() {
        if (runner_) runner_->set_on_applied(nullptr);    // the observer belongs to this session, which is about to go
        return std::move(runner_);
    }

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
    void go_lost(LostReason reason = LostReason::Connection);

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
    PingMeter ping_;
    CommandDelayMeter delay_;
    uint32_t last_heard_ms_{0};
    bool catching_up_{false};
    struct LagNotice {
        uint32_t behind_ms{0};              // 0: no notice
        uint32_t heard_ms{0};
    };
    std::array<LagNotice, sim::MAX_PLAYERS> lag_{};     // the notices about the other seats
    LagNotice self_lag_{};                              // the notice about this seat (self_lag_behind_ms)
    LostReason lost_reason_{LostReason::None};
    bool asked_{false};                  // a ping went out (the oldest of them: asked_ms_) and nothing at all has come from the host since (note_gap)
    uint32_t asked_ms_{0};

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
