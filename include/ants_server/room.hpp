#pragma once

// One game on the server: the room (a host without a seat, ants::net::HostLobby), then the match (a HostSession that runs the simulation as the referee), then
// the result. A room is made from outside (RoomSpec: the map, the fog option, how many players it waits for) and starts by itself when everybody who was
// expected has joined and loaded; nobody on a client has to press anything. It lives in the server's main loop like every other part of the network code:
// no threads, no blocking, one update(now_ms) per pass.
//
//   Waiting  clients join (Hello with the room's code); when `players` seats are taken the room starts: Start goes to all, the server's own copy of the map is
//            already loaded (the room was refused otherwise), every client loads and reports, and the match begins when all have. The first player who joined is the
//            room's LEADER (the next one when it leaves); with `early_start` on, the leader's StartRequest starts the match at once with the players who are there
//            (two at least), without waiting for the rest
//   Loading  a client that cannot load the map or leaves cancels the start: back to Waiting (a few times at most)
//   Running  the referee executes the turns; a diverging client is named and the room fails; the match end (the clock, the rules) finishes it. A room that HOLDS seats
//            (RoomSpec::reconnect, protocol 10) keeps the seat of a player whose connection is lost and PAUSES the match for everybody (see "Reconnect" below)
//   Finished the result (rows as the results screen shows them) is kept; the room goes on answering its players (pings, acknowledgements: the session is frozen, it seals nothing)
//            and the connections close after a grace period, later when a player is still catching up (kGraceMs, kEndWaitMs)
//   Failed   nobody came, too many failed starts, a desync, the owner closed it: the reason is kept
//
// Reconnect (protocol 10, docs/NETWORK_PORT.md "Reconnect"). A room made with `reconnect` gives every player a KEY in its Welcome (the operating system's random bytes: the lobby's
// make_key) and passes the keys of the seats, the Start message and its limits to the match's session (HostSession::Config::hold_seats). From then on a connection that is lost
// (closed, a send fails, or nothing at all arrives for 10 s) holds its seat; nothing is sealed while a seat is away; a Hello with a seat's key reaches the running room through the
// door (RoomManager: Room::rejoin) and the session gives the player the match again from its turn log. The others may vote to go on without the seat (more than half of those who are
// there, after 30 s of absence in all) and the match's total pause is capped (30 minutes): at the cap every seat that is not present is dropped. The room is finished by its rules as before, and
// when everybody has left: a held seat counts as present, so a room whose players all lost their connection waits (until the cap) and then ends "everybody left". The wall-clock limit
// `run_ms` counts the time that the match ran, not the time that it waited. The log of the match, which the stream of a returning player is cut from, is bounded per room and by the
// server's budget (net::LogBudget), and is freed when the match is over. A room without `reconnect` is exactly what it was before: keys are zero, a Hello for a running match is
// MatchRunning, a lost connection is a drop at once. No key is ever in a status, a result file or a log line.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ants_assets/lvl_parser.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_server/map_store.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::server {

struct RoomSpec {
    std::string code;                       // the room's code (net::valid_room_code, not empty): what a client puts in its Hello
    std::string map;                        // the map's file name in the map store
    bool fog{false};                        // the Fog of War option of the match
    uint8_t players{2};                     // 2 .. 4: the match starts when this many seats are taken (and takes no more)
    bool early_start{true};                 // the room's leader (the first player who joined) may start the match before all the seats are taken: with at least two players there
                                            // (the roster is then the seats that are taken). False: the room has no leader and starts only when every seat is taken. Demo rooms have it on.
    bool has_seed{false};
    uint32_t seed{1};                       // the match's random seed (the server draws one when the spec has none)
    uint32_t wait_ms{120000};               // a room that has not started after this long fails ("nobody came", "somebody is missing")
    uint32_t load_ms{60000};                // everybody must have loaded the map this long after the start
    uint32_t keep_ms{10u * 60u * 1000u};    // a finished or failed room stays visible to status calls this long
    uint32_t run_ms{2u * 3600u * 1000u};    // a match that is still running this long after it began is ended (failed, "took too long"): a wall-clock limit on the life of
                                            // a room (it never waits for a seat, but a match that does not end by its rules would run on for ever). It counts the time that the match
                                            // ran: the time that it waited for a seat that was away does not count
    // Reconnect (protocol 10): the room holds the seat of a player whose connection is lost. OFF by default in this release (the server's `--reconnect` and the control interface's
    // "reconnect" turn it on): a server that held seats for clients that cannot come back yet would be worse than one that does not.
    bool reconnect{false};
    uint32_t vote_after_ms{net::kVoteAfterMs};      // the others may vote on going on without a seat once it has been away this long in all (5 s .. 1 h; the control key "hold_vote_seconds")
    uint32_t max_pause_ms{net::kMaxPauseMs};        // the match's total paused time is capped: at the cap every seat that is not present is dropped (1 min .. 24 h; the control key "max_pause_seconds")
    size_t max_log_bytes{net::TurnLog::kDefaultMaxBytes};    // the limit of the match's turn log (16 MiB: 25 times a busy match); past it a lost seat is dropped at once again
    uint32_t max_catch_up_ms{net::kMaxCatchUpMs};            // one absence may spend this long catching up in all, over all its attempts (10 s .. 1 h; the control key "max_catch_up_seconds"): then the
                                                             // catch-up fails, the seat is absent (the vote and the cap apply) and the key is refused
    uint32_t resume_countdown_ms{net::kResumeCountdownMs};   // after a pause of at least 3 s the match is held this long before it goes on (0 .. 60 s, 0 = none; the control key "resume_countdown_seconds")
    size_t max_connections{32};                              // the connections that the room keeps at once (everything that ever said Hello to it, and every connection that came back, until it is closed and
                                                             // nobody uses it): a flood is refused beyond this. 32 for every room the server makes; the control interface has no key for it
};

/// The bounds of the room's reconnect settings (the control interface refuses others, RoomManager::create_room too)
inline constexpr uint32_t kMinVoteAfterMs = 5u * 1000u;
inline constexpr uint32_t kMaxVoteAfterMs = 3600u * 1000u;
inline constexpr uint32_t kMinMaxPauseMs = 60u * 1000u;
inline constexpr uint32_t kMaxMaxPauseMs = 86400u * 1000u;
inline constexpr uint32_t kMinCatchUpMs = 10u * 1000u;
inline constexpr uint32_t kMaxCatchUpLimitMs = 3600u * 1000u;
inline constexpr uint32_t kMaxResumeCountdownMs = 60u * 1000u;
inline constexpr size_t kMinLogBytes = 1024;
inline constexpr size_t kMaxLogBytes = size_t{1} << 30;

enum class RoomState : uint8_t { Waiting, Loading, Running, Finished, Failed };
const char* room_state_name(RoomState state) noexcept;

/// One row of the result, as the results screen shows it (an alliance is one row)
struct RoomRow {
    uint8_t first{255};
    uint8_t second{255};                    // 255 for a single team
    std::string name;
    std::string second_name;
    int32_t score{0};
    int32_t friendly_lost{0};
    int32_t enemy_killed{0};
    int32_t new_hatched{0};
    bool winner{false};
};

struct RoomStatus {
    std::string code;
    std::string map;
    bool fog{false};
    uint8_t expected{0};
    bool early_start{true};                 // the room allows the leader's early start
    uint8_t leader{255};                    // the seat of the leader while the room waits or loads (255: none: nobody has joined yet, the room does not allow an early start, or the match runs)
    uint32_t ignored_start_requests{0};     // StartRequest messages that were heard and not acted on (a player who is not the leader, a room that cannot start, a late click of a match that runs)
    RoomState state{RoomState::Waiting};
    std::string reason;                     // why a room failed, or how it ended ("" while it runs)
    uint8_t joined{0};
    std::array<std::string, 4> names{};     // by seat; "" for an empty seat
    uint32_t ticks{0};                      // the referee's clock (20 per second)
    uint32_t turns{0};                      // turns sealed: one per tick since protocol 8 (turns of 50 ms; they were 100 ms, two ticks, before)
    uint32_t age_ms{0};
    uint16_t quitter{0xFFFF};
    std::vector<RoomRow> rows;              // the result of a finished match, best first
    // Reconnect (protocol 10). Never a key.
    bool reconnect{false};                  // the room holds the seats of players whose connections are lost
    uint32_t vote_after_ms{0};              // its rules: the vote opens after this much absence in all, the match's pauses may last this long in all
    uint32_t max_pause_ms{0};
    uint32_t max_catch_up_ms{0};            // ... one absence may spend this long catching up in all; the match is held this long after a pause (0: not at all)
    uint32_t resume_countdown_ms{0};
    uint8_t resume_s{0};                    // the countdown that follows a pause is running: the seconds that are left (0: none)
    bool paused{false};                     // the match waits for a seat (running rooms only)
    struct Absent {
        uint8_t seat{255};
        std::string name;
        bool catching_up{false};            // the player is back and is being given the match (false: its connection is lost and the seat is held)
        uint32_t away_s{0};                 // its total absence in this match, in seconds
        uint8_t progress{0};                // catching up: the percent of the match's turns that it has executed
    };
    std::vector<Absent> absent;             // the seats that are missing, longest away first
    uint8_t vote_seat{255};                 // the seat the open vote is about (255: none), the connected players who chose "continue without it", and how many players vote
    uint8_t votes_continue{0};
    uint8_t voters{0};
    uint32_t paused_s{0};                   // the match's total paused time so far (it stays what it was at the end)
    uint32_t rejoins{0};                    // seats that came back and were verified
    uint32_t drops_by_vote{0};              // seats dropped because the players voted to go on without them
    uint32_t drops_by_cap{0};               // seats dropped because the match's pauses used up max_pause_ms
    uint32_t rejoins_refused{0};            // Hellos with a key that were refused for a budget (the absence's catch-up time, three attempts a minute, three times the log's size in ten minutes)
    uint32_t catch_up_expired{0};           // absences whose catch-up time ran out
    uint64_t streamed_bytes{0};             // the bytes of the turn log that were streamed to returning players (all seats)
    uint32_t log_turns{0};                  // the match's turn log (what a returning player is given): its turns, its bytes, and whether it can still be used
    uint32_t log_bytes{0};
    bool log_usable{true};
    uint32_t connections{0};                // the connections that the room keeps (diagnostics: at most kMaxConnections of the lobby's, and those that came back)
};

class Room {
public:
    static constexpr size_t kMaxConnections = 32;           // RoomSpec::max_connections of a room that says nothing: everything that ever said Hello to it (rejected ones included): a flood is refused beyond this
    static constexpr uint32_t kRetryMs = 2000;              // the pause after a cancelled start
    static constexpr uint32_t kGraceMs = 15000;             // after the end of the match the connections stay open at least this long (a client that is level finds the results in peace) ...
    static constexpr uint32_t kEndWaitMs = 30000;           // ... and until every player that is still connected has acknowledged the last turn, at the most this long: a player that was
                                                            // 60 s behind when the match ended (the most a seat may be) needs 15 s at 4x to run what is left, and is answered (pings,
                                                            // acknowledgements) until it has

    /// `log_budget` is the server's memory for the turn logs of all its rooms (shared; it must outlive the room); null: no budget beyond the room's own limit
    Room(RoomSpec spec, MapEntry map, assets::LevelData level, uint32_t seed, uint32_t now_ms, net::LogBudget* log_budget = nullptr);

    const std::string& code() const noexcept { return spec_.code; }
    RoomState state() const noexcept { return state_; }
    /// True while a client may still join (Waiting)
    bool accepting() const noexcept { return state_ == RoomState::Waiting; }
    /// The server tells the world about the end of a room once (a log line, a result file): this is the flag for it
    bool end_reported() const noexcept { return end_reported_; }
    void mark_end_reported() noexcept { end_reported_ = true; }
    /// True once the room can be forgotten: it ended (finished or failed) and its keep time has passed
    bool expired(uint32_t now_ms) const noexcept;

    /// A client of this room: its first message (the Hello, already checked by the caller) goes to the lobby, which answers it. On success the room owns the
    /// connection (the pointer is moved from); on false (the room takes no more connections) the caller still holds it and rejects it.
    bool add_connection(std::unique_ptr<net::Connection>& connection, const std::string& address, const std::vector<uint8_t>& hello, uint32_t now_ms);
    /// A Hello with a key reaches a match that runs (the door: RoomManager) when the room holds seats and the match runs: the session decides (HostSession::accept_rejoin). True: the room
    /// owns the connection (the session points at it until it has been replaced). False: the room takes nothing; the session has answered with a Reject and closed the connection, which the
    /// caller keeps until the answer has arrived. A room that keeps max_connections connections that are all in use answers Full (it never keeps more).
    bool can_rejoin() const noexcept { return state_ == RoomState::Running && session_ != nullptr && spec_.reconnect; }
    bool rejoin(std::unique_ptr<net::Connection>& connection, const net::HelloMsg& hello, uint32_t now_ms);

    void update(uint32_t now_ms);
    /// The owner closes the room: every client is dropped, the state is Failed with `reason`
    void close(const std::string& reason, uint32_t now_ms);
    RoomStatus status(uint32_t now_ms) const;

private:
    void fail(const std::string& reason, uint32_t now_ms);
    void begin_match(uint32_t now_ms);
    void finish(const std::string& reason, uint32_t now_ms);
    void close_connections();
    void prune_connections();
    void end_log(uint32_t now_ms);

    RoomSpec spec_;
    MapEntry map_;
    assets::LevelData level_;
    uint32_t seed_{1};
    uint32_t created_ms_{0};
    uint32_t ended_ms_{0};
    RoomState state_{RoomState::Waiting};
    std::string reason_;
    uint32_t cancels_{0};
    uint32_t started_ms_{0};                 // when the match began (run_ms)
    uint32_t retry_at_ms_{0};                // after a cancelled start the room waits a moment before it tries again (a client that cannot load the map does not make a tight loop)

    net::HostLobby lobby_;
    std::vector<std::unique_ptr<net::Connection>> connections_;
    std::unique_ptr<sim::SimulationEngine> sim_;
    std::unique_ptr<net::HostSession> session_;
    uint32_t last_ticks_{0};
    uint32_t last_turns_{0};
    bool connections_closed_{false};
    bool end_reported_{false};
    net::LogBudget* log_budget_{nullptr};
    bool log_released_{false};               // the match is over: the log was freed, these are what it held at the end (and the match's pauses)
    uint32_t log_turns_{0};
    uint32_t log_bytes_{0};
    bool log_usable_{true};
    uint32_t final_pause_ms_{0};
    std::vector<RoomRow> rows_;
    uint16_t quitter_{0xFFFF};
    std::array<std::string, 4> names_{};
};

}  // namespace ants::server
