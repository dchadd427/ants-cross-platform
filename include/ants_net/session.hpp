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
// The start of a match (protocol 12). start() begins the match at once, but the first turn is sealed Config::start_delay_ms later (the two product paths, a LAN host's NetGame and a server's Room, set
// kMatchStartDelayMs: the length of the "Get ready to play!" dialog). Until then no turn exists: a client's runner has not started (it begins when its first turns are in), the sequencer has no turn that
// a seat could be behind, and the lag policy (police_laggards) has nothing that waits for a player, so the wait is no stall, no lag notice, no growth of the jitter buffer and no countdown of the
// idle rule. A machine ends its dialog when its first turn executes. The reconnect machinery is as it was (a pause slides the schedule: the first turn follows the end of a pause at once).
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
//
// Coming back (protocol 10, docs/NETWORK_PORT.md "Reconnect"). A dedicated server's room may HOLD the seat of a player whose connection is LOST instead of dropping it
// (HostSession::Config::hold_seats, the server's `--reconnect`; every other host, a game on the local network above all, keeps the rules above). A connection is lost when its link
// closes, a send to it fails, or nothing at all arrives from it for 10 s (a half-open link: a power cut, a frozen machine); a player that is slow but keeps talking is lag, never a
// loss. While a seat is held nothing is sealed: the match is paused for everybody, chat goes on, commands are discarded without being counted as violations, and the others are told
// who is missing, since when, the vote, the cap and the seconds of the resume countdown (Presence, per recipient, on every change and once a second). The seat comes back with its key (a Hello that shows it, accepted by
// HostSession::accept_rejoin): the host gives the player the turns it has not got from its TurnLog (CatchUp, TurnBatch messages paced by what the player has executed, in bytes),
// the player runs them with fast_forward (nothing is drawn) and says CaughtUp with its state hash, and when the hash is the referee's the connection is the seat's again and the
// match goes on at the next turn (after a pause of 3 s or more, after a resume countdown during which nothing is sealed); a different state is answered with a Desync to that player alone (the room does not fail). Meanwhile the others may vote
// (Attendance: also about a seat that flaps, back and present), and the match's total pause is capped, absolutely: at the cap every seat that is not present is dropped. A key holder has budgets (a catch-up time per absence,
// three Hellos a minute, three times the log's size in ten minutes: attendance.hpp); beyond them a Hello is refused, RejoinFailed, and the seat stays held. A returning connection is not a client of the host until it has caught up (HostSession::Rejoiner). ClientSession is the other end: a lost link with a
// key is not the end of the match (Mode::Reconnecting): it asks its owner for a new link (wants_connection / attach), says the Hello when the link is open, is given the match again
// (Mode::Rejoining, Mode::CatchingUp) and goes on; a Reject (the seat was dropped, the room is gone, a newer window took the seat) ends it for good.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/attendance.hpp"
#include "ants_net/flood.hpp"
#include "ants_net/latency.hpp"
#include "ants_net/lockstep.hpp"
#include "ants_net/protocol.hpp"
#include "ants_net/sequencer.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/turnlog.hpp"
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

/// Reconnect (see above). A client from which nothing at all arrives for this long is LOST (a live client sends an acknowledgement every 50 ms of play and a ping a second)
inline constexpr uint32_t kLostSilenceMs = 10000;
/// ... the Presence message goes to the players this often while the match is paused (and at once on every change)
inline constexpr uint32_t kPresenceEveryMs = 1000;
/// ... the stream of the log to a player who came back: the bytes that may be on their way beyond what the player has executed (a WebSocket fails at 1 MB of backlog)
inline constexpr size_t kStreamWindowBytes = 256 * 1024;
/// ... a client gives up coming back after this long when it has seen no Presence (the server's default pause cap of 30 minutes and a minute for the last attempt): once it has, the cap
/// that Presence named (cap_s) and the margin, unless a Reject ends it earlier
inline constexpr uint32_t kReconnectGiveUpMs = (30u * 60u + 60u) * 1000u;
inline constexpr uint32_t kReconnectMarginMs = 60u * 1000u;
/// ... the longest cap that a server's room may have (86400 s: a Presence with cap_s 0xFFFF says "that or more")
inline constexpr uint32_t kMaxCapSeconds = 86400u;

class HostSession {
public:
    struct Config {
        uint8_t host_player{0};             // the host's own seat; kNoSeat (255) for a dedicated server: the host plays nobody, runs the match as the referee
        uint8_t epoch{0};                   // 0 for the first host, one more for every host change
        uint32_t start_delay_ms{0};         // the first turn is sealed this long after start(): the "Get ready to play!" dialog that every machine opens when the match begins (protocol 12:
                                            // kMatchStartDelayMs, which the LAN host's NetGame and the server's Room set; 0, the default, seals turn 0 at once, as a test rig wants). Nothing is
                                            // sealed meanwhile, so nobody is behind and nothing waits for anybody: the wait is not lag (see "The start of a match" above)
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
        // Reconnect (see above). Off: today's rules, exactly (a lost connection is a drop at once).
        bool hold_seats{false};                                 // a lost connection holds the seat and pauses the match (it needs the seat's key: set_seat_keys) instead of dropping it
        uint32_t silence_ms{kLostSilenceMs};                    // hold_seats: nothing at all from a client for this long = its connection is lost (without hold_seats it is silence_timeout_ms and a drop)
        Attendance::Config attendance{};                        // the vote after 30 s away, the catch-up stall of 20 s, the least an absence counts, the cap on the pause (30 minutes)
        size_t max_log_bytes{TurnLog::kDefaultMaxBytes};        // the limit of the turn log (16 MiB): past it a lost seat is dropped at once again
        LogBudget* log_budget{nullptr};                         // the server's memory for the logs of all its rooms (turnlog.hpp), shared: a log that it refuses is not kept
        uint32_t presence_every_ms{kPresenceEveryMs};
        size_t stream_window_bytes{kStreamWindowBytes};
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
    /// command is one that a player may send (never Drop), and false while the match is paused (a bot waits like everybody; a bot seat is never absent and never votes).
    bool submit_bot(uint8_t player, sim::Command command);
    /// Begins the match at `now_ms`: turn 0 is sealed Config::start_delay_ms later (at once when that is 0; the LAN host and the server's rooms wait kMatchStartDelayMs, protocol 12).
    void start(uint32_t now_ms);
    /// Continues a match in which the previous host left: the next turn sealed is `resume_turn`, every survivor is told (Resume) and gets the turns
    /// it misses, and the first turn drops the old host and every seat of the roster that did not follow. Instead of start().
    void resume(uint32_t now_ms, uint32_t resume_turn, uint8_t old_host, const std::vector<PeerState>& survivors);

    /// A command of the host's own player (a host without a seat has none: ignored)
    void submit_local(sim::Command command);
    /// Relays a chat text of the host's own player (a host without a seat has none: ignored). A team line goes to the players that may read it, see "Who hears a team line".
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
    /// Called for every chat message the host receives from a client (already relayed) and for its own. A host with a seat is a receiver like the guests: it hears a team line only when it
    /// is the sender or the sender's ally (the same rule as the relay's); a host without a seat (a dedicated server) is the referee and hears every line (a hook for a log: the server's rooms install none today, so nothing is logged).
    void set_on_chat(std::function<void(const ChatMsg&)> fn) { on_chat_ = std::move(fn); }

    /// WHO HEARS A TEAM LINE. The host relays every chat line, and a line that is meant for the sender's TEAM (ChatMsg::team) goes to the sender and to the sender's ally only, by the host's
    /// own alliance table AT THE MOMENT OF THE RELAY (a break takes effect within one turn, 50 ms: a line sent in the same turn as the break, or up to a turn after it, can still reach the ex-ally; a new alliance within a turn of its accept): `sim::SimulationEngine::alliance_of(sender)`, the
    /// rule that a receiver's HUD applies to the lines that it gets (HUD::receive_chat_message: the original's), so that nothing is delivered that a screen would drop, and nothing that a
    /// screen would show is withheld. A line for ALL goes to everybody. The others never get the bytes at all (a modified client that shows every line cannot read the team's talk).
    /// True when `seat` is to hear a line that `sender` says: used by the relay, and public for the tests that pin the rule.
    bool hears_chat(uint8_t sender, bool team, uint8_t seat) const noexcept;

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

    // ---- reconnect (hold_seats; see the head of this file) -----------------------------------------------------------------------------------------------------
    /// The keys of the seats (before start(); the room hands over what its lobby gave out: HostLobby::key_of). A seat whose key is all zero (a bot, an empty seat, a guest that was
    /// given none) cannot be held: its lost connection is a drop at once. The keys stay what they were for the whole match, whatever becomes of the seats (a seat that was dropped
    /// answers its owner "dropped").
    void set_seat_keys(const std::array<SeatKey, sim::MAX_PLAYERS>& keys) { keys_ = keys; }
    /// What a machine that comes back with nothing needs to load the match: the room's Start message (before start()). Without it a Hello that has no turns is refused (RejoinFailed).
    void set_rejoin_start(const StartMsg& start) {
        rejoin_start_ = start;
        have_rejoin_start_ = true;
    }
    /// A connection whose first message was a Hello with a key (the door has decoded it: the room is right, the version is right). True: the connection is taken over (the Welcome
    /// is sent, the catch-up follows) and the caller must keep it alive for as long as the session lives: the host holds the pointer until the connection is the seat's or the attempt
    /// ends, and then until it is closed and replaced (uses_connection). False: refused, a Reject has been sent and the connection is closed (the caller lets it linger so that the
    /// answer arrives). The refusals: no seat has this key (or the session does not hold seats): MatchRunning, exactly what a Hello without a key is told, so a wrong key reveals nothing;
    /// the seat was dropped: Dropped; the log is not usable, or a machine that has nothing cannot be told how to load the match: RejoinFailed; more turns than were sealed: BadRequest.
    /// A seat whose old connection is still open (a half-dead link, a second window with the same key) is taken over: the old connection is told Superseded and closed; an earlier
    /// attempt of the same seat that is still catching up is closed too. A Hello with no turns (have_turns 0) is answered Welcome, Start and, when Loaded comes, Begin and
    /// CatchUp{0, total}; one with turns is answered Welcome and CatchUp{have_turns, total} at once.
    bool accept_rejoin(Connection* conn, const HelloMsg& hello, uint32_t now_ms);
    /// True while the match is held (hold_seats only): a seat is absent or is catching up, or the resume countdown that follows such a pause is running. Nothing is sealed, commands are
    /// discarded and chat goes on. attendance().paused() is only the first of these.
    bool paused() const noexcept { return cfg_.hold_seats && (attendance_.paused() || attendance_.counting_down(last_ms_)); }
    /// The seat is held or being brought back: not present, not dropped
    bool seat_held(uint8_t seat) const noexcept {
        const Attendance::State st = attendance_.state(seat);
        return st == Attendance::State::Absent || st == Attendance::State::CatchingUp;
    }
    const Attendance& attendance() const noexcept { return attendance_; }
    const TurnLog& log() const noexcept { return log_; }
    /// The match is over: the log is freed and its bytes go back to the budget (the counters stay readable in attendance())
    void release_log() noexcept { log_.release(); }
    /// The players that are coming back now (a connection that said Hello with a key and has not caught up)
    size_t rejoiners() const noexcept { return rejoiners_.size(); }
    /// True while this session still points at the connection: a present client's, or one that is coming back. Whoever owns the connections (the room) must not free one that this
    /// answers true for (a rejoining player's link can close while its catch-up is being kept, and nobody may have deleted the object under it).
    bool uses_connection(const Connection* c) const noexcept;

private:
    struct Client {
        Connection* conn{nullptr};
        bool present{false};
        uint32_t violations{0};
        uint32_t last_heard_ms{0};          // when the client last sent anything
        MessageBudget talk;                 // flood control: every message that the client sends takes one from it
        ChatBudget chat;                    // ... and every line of chat takes one from this one as well (flood.hpp: a burst of 5, then one a second; a line beyond it is dropped)
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
    void relay_chat(const ChatMsg& m);                 // every client of the match hears an all line; a team line only the sender and its ally (hears_chat)
    void report_hash(uint8_t player, uint32_t turn, const sim::StateHash& hash);
    void run_local(uint32_t dt_ms);
    void send_turns(Connection* conn, uint32_t from_turn, uint32_t to_turn);
    void police_laggards(uint32_t now_ms);
    void announce_lag(uint8_t player, uint32_t behind_ms);
    // reconnect
    void lose(uint8_t player);
    void finish_drop(uint8_t seat);
    void pump_rejoiners(uint32_t now_ms);
    void send_presence();
    void end_lag_notice(uint8_t player);

    /// A connection that said Hello with a seat's key and is being given the match: not a client of the host until it has caught up
    struct Rejoiner {
        enum class Stage : uint8_t { AwaitLoaded, Streaming };
        Connection* conn{nullptr};
        uint8_t seat{255};
        Stage stage{Stage::Streaming};
        uint32_t total{0};                  // the turns sealed when the attempt began: the match is paused, so it cannot grow
        uint32_t next{0};                   // the next turn to send
        uint32_t acked{0};                  // the turns that the player has executed (never more than were sent)
        bool verify{false};                 // the player said CaughtUp: the referee compares the hash as soon as it has executed as many turns
        CaughtUpMsg claim;
        uint32_t violations{0};
        MessageBudget talk;                 // flood control, as for a client
    };

    Config cfg_;
    sim::SimulationEngine* sim_;
    Sequencer sequencer_;
    Attendance attendance_;
    TurnLog log_;
    std::array<SeatKey, sim::MAX_PLAYERS> keys_{};
    StartMsg rejoin_start_;
    bool have_rejoin_start_{false};
    std::vector<Rejoiner> rejoiners_;
    uint32_t next_presence_ms_{0};
    bool presence_dirty_{false};
    bool was_paused_{false};
    bool was_voting_{false};                // the last Presence went out while a vote was open (a vote that closes is told once)
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
        // Reconnect (see the head of this file): a dedicated server's room that holds seats. Off: today's rules (a lost link ends the match here).
        bool reconnect{false};               // a lost link (closed, or the host silent) is not the end: this machine asks for a new link and gets its seat back, as long as it has a key
        HelloMsg hello;                      // what to say on a new link: name, room code, token, wanted seat (the version, the key and the turns are filled in)
        SeatKey key{};                       // the key of the seat (the Welcome gave it; a machine that starts again is given it from outside)
        bool rejoin{false};                  // this machine starts from nothing and is given the match by the server: its lobby did the Hello (Welcome, Start, Loaded, Begin), the session
                                             // starts by catching up (CatchUp and TurnBatch follow)
        uint32_t reconnect_every_ms{2000};   // a new attempt this long after the last one began
        uint32_t reconnect_give_up_ms{kReconnectGiveUpMs};  // no way back after this long since the link was lost (Lost, no Reject), while no Presence has named the room's cap (31 minutes: the
                                                             // default cap and a minute); from the first Presence on, the cap it named (the last cap_s seen) and kReconnectMarginMs
        uint32_t rejoin_timeout_ms{10000};   // an attempt that has no Welcome after this long is abandoned (the next one follows)
        uint32_t catch_up_ticks{200};        // the turns that one update() runs while catching up (the owner may lower it for a slow machine: a window slices the work into frames)
    };
    /// Normal: following the host. Electing / Following / Fetching: the host is gone and a new one is being chosen (no turns arrive meanwhile).
    /// Promoted: this machine is the new host (see promote_to_host). Lost: the other players cannot be reached, or this machine was cut off, or the server said no (reject_reason()).
    /// Reconnecting: the link to the server was lost and this machine waits for its owner to make a new one (wants_connection, attach); Rejoining: a new link says Hello and waits for
    /// the Welcome; CatchingUp: the server gives this machine the turns it lacks, which it runs without drawing them, and the match goes on (Normal) when the server confirms.
    enum class Mode : uint8_t { Normal, Electing, Following, Fetching, Promoted, Lost, Reconnecting, Rejoining, CatchingUp };
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

    /// The player's command, sent to the host (which stamps the issuer). False when there is no host (an election is going on), when this machine is not following the match
    /// (it reconnects or catches up), and while the match is held (the last Presence names a seat that is missing, or a resume countdown that runs: nothing is sealed, the host would discard it).
    bool submit(sim::Command command);
    bool chat(const std::string& text, bool team);
    /// The player quits the match. A machine that is reconnecting or catching up has nothing to say to anybody: its session is over (Lost).
    void leave();
    /// The match is over: a host that goes now is no reason to look for a new one
    void finish() noexcept { finished_ = true; }

    void update(uint32_t now_ms);
    /// `gap_ms` of real time went by that the clock of update() did not count (a page that the browser did not wake for a while: the application hands the session at most a
    /// second per wake-up, so that the lock-step runner is not paid back more at once). A host that said nothing in the update that has just run (the link is read first: what
    /// waited was heard) has been silent for that time too, and its silence is counted in real time. But only a host that has been ASKED can be called silent: a page that
    /// sleeps sends no pings, and a live host that holds its turns says nothing either. So a host that has not been asked since it was last heard (no ping went out after it)
    /// gets the gap only up to kAskGraceMs short of the limit (the update that has just run sent the ping); its answer is a message, which wakes the page and is heard, and a
    /// host that does not answer is called silent by the next wake-up (the gap counts in full once a ping is outstanding). True when a stamp was touched.
    /// This is the rule of "the server is silent" in every mode in which the session waits for the server: Normal and CatchingUp (the answers to the pings: the silence that
    /// takes the machine to Reconnecting, or ends an attempt), and, counted in full, Rejoining (a Hello that no Welcome has answered) and Reconnecting (the time of the way back:
    /// the next attempt, the give-up). The caller judges the session at once afterwards (update() with the same clock), as it does after every update.
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

    // ---- reconnect (cfg.reconnect; see the head of this file) -------------------------------------------------------------------------------------------------
    /// The session is Reconnecting and the next attempt is due: the owner makes a new link to the server (a connection is made without blocking) and hands it over with attach()
    bool wants_connection(uint32_t now_ms) const noexcept;
    /// A new link to the server, made because wants_connection() said so. The session says Hello (the key and the number of turns it has) when the link is open, which may be later
    /// (a TCP link opens without blocking), and is Rejoining until the Welcome. The owner keeps the connection alive for as long as the session lives, closes the old one it replaced,
    /// and does not let a link that the session gave up on (it is closed) go on. nullptr: no link could be made, the next attempt is a few seconds away. Ignored unless Reconnecting.
    void attach(Connection* link, uint32_t now_ms);
    /// The key of the seat as this session has it (the Welcome's, or the one it was given)
    const SeatKey& key() const noexcept { return cfg_.key; }
    /// The last Presence message of the server: who is missing, the vote that is open, the cap (an empty `missing`: the match runs)
    const PresenceMsg& presence() const noexcept { return presence_; }
    /// True while the server says that the match is held: a seat is missing (it is paused for everybody) or the resume countdown that follows a pause is running. The server discards
    /// commands then, and submit() refuses them
    bool paused() const noexcept { return !presence_.missing.empty() || presence_.resume_s > 0; }
    /// The seconds of the resume countdown that are left, as the last Presence said (rounded up; 0 when none runs): the match goes on when it is over. For the screen's overlay
    uint8_t resume_seconds_left() const noexcept { return presence_.resume_s; }
    /// How long, after the link was lost, this session keeps trying: the room's cap that the last Presence named and kReconnectMarginMs, or reconnect_give_up_ms before any Presence
    uint32_t give_up_ms() const noexcept;
    /// Chooses for the vote about `seat`: continue the match without it, or keep waiting. False unless this machine follows the match (the server ignores a vote that does not fit)
    bool vote(uint8_t seat, bool continue_without);
    /// 0 .. 100: the share of the turns that the server gave this machine that it has executed, while it is catching up (100 when it has said CaughtUp and waits for the server)
    uint8_t catch_up_percent() const noexcept;
    /// This machine's link to the server was lost and it is on its way back: Reconnecting, Rejoining or CatchingUp
    bool reconnecting() const noexcept { return mode_ == Mode::Reconnecting || mode_ == Mode::Rejoining || mode_ == Mode::CatchingUp; }
    /// When the link was lost (the clock of update()) and how many new links were made since (attach), for the screen that says "trying to connect"; valid while reconnecting()
    uint32_t lost_since_ms() const noexcept { return reconnect_since_ms_; }
    uint32_t reconnect_attempts() const noexcept { return attempts_; }
    /// Why the session ended when the server said no (a Reject: dropped, no such room, a newer window took the seat, ...) and whether it did; lost() without a Reject is a link that
    /// could not be restored in reconnect_give_up_ms, a protocol failure, or the end of a match that has no way back
    bool rejected() const noexcept { return has_reject_; }
    RejectReason reject_reason() const noexcept { return reject_; }

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
    // reconnect
    void reject(RejectReason reason);
    void enter_reconnecting(uint32_t now_ms);
    void attempt_failed(uint32_t now_ms);
    void send_rejoin_hello();
    void poll_new_link(uint32_t now_ms);
    void handle_stream_message(const std::vector<uint8_t>& msg, uint32_t now_ms);
    void catch_up_step(uint32_t now_ms);
    void ping_while_catching_up(uint32_t now_ms);
    void begin_normal(uint32_t now_ms);
    void apply_presence(PresenceMsg p, uint32_t now_ms);

    Config cfg_;
    sim::SimulationEngine* sim_;                        // (the state hash that CaughtUp carries)
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
    bool asked_{false};                  // a ping went out (the oldest of them: asked_ms_) and nothing at all has come from the host since (note_gap)
    uint32_t asked_ms_{0};
    bool catching_up_{false};
    struct LagNotice {
        uint32_t behind_ms{0};              // 0: no notice
        uint32_t heard_ms{0};
    };
    std::array<LagNotice, sim::MAX_PLAYERS> lag_{};     // the notices about the other seats
    LagNotice self_lag_{};                              // the notice about this seat (self_lag_behind_ms)
    LostReason lost_reason_{LostReason::None};

    // reconnect
    PresenceMsg presence_;                              // the last Presence of the server
    bool cap_known_{false};                             // a Presence was seen: the cap it named and when (give_up_ms)
    uint16_t cap_s_{0};
    uint32_t cap_heard_ms_{0};
    bool cap_paused_{false};                            // ... and a seat was missing then (the cap was already running)
    RejectReason reject_{RejectReason::BadRequest};
    bool has_reject_{false};
    bool left_{false};                                  // leave() was called: no more attempts
    uint32_t reconnect_since_ms_{0};                    // the first loss of the link (the give-up time counts from here, through every attempt)
    uint32_t next_attempt_ms_{0};
    uint32_t attempts_{0};
    uint32_t attempt_ms_{0};                            // when the attempt that goes on began (attach)
    bool hello_pending_{false};                         // the new link was not open yet when it was attached: the Hello goes out when it is
    bool catch_known_{false};                           // CatchUp has come: the stream of turns is announced
    uint32_t catch_total_{0};
    bool caught_up_sent_{false};                        // CaughtUp is out: this machine waits for the server to confirm (Presence) or refuse
    uint32_t last_ack_{0};                              // the turns that the last acknowledgement of the catch-up said were executed

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
