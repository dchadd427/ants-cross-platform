#pragma once

// One game on the server: the room (a host without a seat, ants::net::HostLobby), then the match (a HostSession that runs the simulation as the referee), then
// the result. A room is made from outside (RoomSpec: the map, the fog option, how many players it waits for) and starts by itself when everybody who was
// expected has joined and loaded; nobody on a client has to press anything. It lives in the server's main loop like every other part of the network code:
// no threads, no blocking, one update(now_ms) per pass.
//
//   Waiting  clients join (Hello with the room's code); when `players` seats are taken the room starts: Start goes to all, the server's own copy of the map is
//            already loaded (the room was refused otherwise), every client loads and reports, and the match begins when all have. The first player who joined is the
//            room's LEADER (the next one when it leaves); with `early_start` on, the leader's StartRequest starts the match at once with the players who are there
//            (two at least), without waiting for the rest. With a FILL LEVEL in the request (protocol 11) the room first seats a bot of that level, named "Bot (Medium)", in
//            every seat that is still empty (up to `players`), and then one person is enough; a room with Fog of War refuses the bots (bots and fog never mix, docs/BOTS.md rule
//            8: the leader is told, and the match starts without them if two people are there), and so does a map that the filled roster cannot play. Only the leader's request seats
//            bots: a room that fills up, or starts by itself, never does
//   Loading  a client that cannot load the map or leaves cancels the start: back to Waiting (a few times at most); the bots that the fill seated go again, so the room is what it was
//   Running  the referee executes the turns (and runs the room's bots: ants_ai's BotController over the referee's own engine, called after every tick, its commands go
//            through the session's sequencer like a person's, `HostSession::submit_bot`; a bot seat has no connection, is never absent, never votes and is never counted as
//            connected); a diverging client is named and the room fails; the match end (the clock, the rules) finishes it. A room that HOLDS seats
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
// `run_ms` counts the time since the match began (the five seconds of the start dialog before the first turn included, protocol 12), not the time that it waited for a seat. The log of the match, which the stream of a returning player is cut from, is bounded per room and by the
// server's budget (net::LogBudget), and is freed when the match is over. A room without `reconnect` is exactly what it was before: keys are zero, a Hello for a running match is
// MatchRunning, a lost connection is a drop at once. No key is ever in a status, a result file or a log line.
//
// Restart records (restart_record.hpp, docs/NETWORK_PORT.md "Restart records"). A room that holds seats keeps a RESTART RECORD on the server's results volume from the moment its match is being
// started (the start message and the keys are known) until the room is over: every sealed turn is written to it BEFORE the turn is sent to anybody, the referee's state hash every 20th turn, so
// that a server that is stopped, crashes or is redeployed does not end the match. A server that starts again calls Room::replay with the record (RoomManager::restore_rooms does, for every record
// of its folder, inside a budget) and then Room::begin_restored: the engine is made from the start message, every sealed turn is replayed and checked against the stored hashes, every seat of a person is held ABSENT (the match is paused until its
// player comes back with its key, protocol 10, with a longer wait before the others may vote than for a lost link), the bots sit down again at the restored tick (their tasks are soft: they look at the
// world and go on), and the room keeps its code, so that the players' clients find it. A record that cannot be restored (another network protocol, a map that has changed, a replay that does not
// agree with the hashes, too old) ends the room with the reason (Room::refused: a failed room that the status shows and the log reports). A finished or failed room deletes its record; a server that is told
// to stop (RoomManager::shutdown) makes the records durable and leaves them where they are.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/restart_record.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::server {

struct RoomSpec {
    std::string code;                       // the room's code (net::valid_room_code, not empty): what a client puts in its Hello
    std::string map;                        // the map's file name in the map store
    bool fog{false};                        // the Fog of War option of the match
    uint8_t players{2};                     // 2 .. 4: the match starts when this many seats are taken (and takes no more)
    bool early_start{true};                 // the room's leader (the first player who joined) may start the match before all the seats are taken: with at least two players there
                                            // (the roster is then the seats that are taken). False: the room has no leader and starts only when every seat is taken. Demo rooms have it on.
    /// Computer players that sit in the room from the start (docs/BOTS.md, B6; the control interface's "bots": [{"seat": 2, "bot": "medium"}]): each takes a seat that counts towards `players`,
    /// is shown as a bot ("Bot (Medium)") and is run by the server as a virtual client. At least one seat must be left for a person, the seats are distinct and Fog of War is off
    /// (RoomManager::create_room refuses anything else). Empty: no bot code runs in the room, unless its leader's START asks for a fill.
    std::vector<ai::BotSpec> bots;
    bool has_seed{false};
    uint32_t seed{1};                       // the match's random seed (the server draws one when the spec has none)
    uint32_t wait_ms{120000};               // a room that has not started after this long fails ("nobody came", "somebody is missing")
    uint32_t load_ms{60000};                // everybody must have loaded the map this long after the start
    uint32_t keep_ms{10u * 60u * 1000u};    // a finished or failed room stays visible to status calls this long
    uint32_t run_ms{2u * 3600u * 1000u};    // a match that is still running this long after it began is ended (failed, "took too long"): a wall-clock limit on the life of
                                            // a room (it never waits for a seat, but a match that does not end by its rules would run on for ever). It counts from the moment the match
                                            // began (the 5 s before its first turn, the start dialog, count: protocol 12), without the time that it waited for a seat that was away
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

/// What a restart of the server would interrupt in one room, as the public /busy answer counts it (RoomManager::busy adds the rooms up; plain numbers: no name, no code)
struct RoomBusy {
    bool match{false};                      // a match loads or runs and a person is in it (present or catching up), or the room was brought back by a restart a moment ago (kRestoredBusyWindowMs)
    uint32_t players{0};                    // the people: in a room that waits or loads, the persons in its lobby; in a match, the persons who are there and, in that window, the seats that are held for those who are not
};

/// How long after a restart a restored room still counts as a match that runs although none of its players is back (its players need that long to notice, wait for the server and come back; a
/// restart now would interrupt them again). Beyond it a room that nobody has come back to counts nothing: a room whose players never come back must not hold a deploy for the pause cap (30 minutes
/// to 24 hours).
inline constexpr uint32_t kRestoredBusyWindowMs = 5u * 60u * 1000u;

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
    /// The computer players of the room: the specification's, and the ones that the leader's START seated (`fill`); the seats stay listed after the match. `joined` counts them (a bot is a player).
    struct Bot {
        uint8_t seat{255};
        std::string kind;                   // "standard" (the bot of the three levels), "worker", "idle"
        std::string level;                  // "easy", "medium", "hard"
        std::string style;                  // the style that the specification pins ("aggressive", "economic", "raider", "defensive"), "random" for a bot that draws its own at the start of the match; "" for a kind without styles
        std::string name;                   // "Bot (Medium)"
        bool fill{false};                   // seated by the leader's START (false: by the room's specification)
    };
    std::vector<Bot> bots;
    bool bot_controller{false};             // the server built a bot controller for this room's match (only a room with a bot seat has one: a room without bots runs no bot code, docs/BOTS.md rule 8)
    uint32_t bot_start_hold{0};             // ... and its start hold in ticks (BotController::start_hold: ai::kStartHoldTicks, the product's opening; 0 without a controller): for the tests, not shown by the control interface
    uint32_t bot_decisions{0};              // how many times the room's bots have looked at the match since its controller was made (the sum of BotController::SeatStats::decisions): a controller that is called after every tick
                                            // counts up, one that is not does not (a room that was restored has a new controller: it starts from 0): for the tests, not shown by the control interface
    RoomState state{RoomState::Waiting};
    std::string reason;                     // why a room failed, or how it ended ("" while it runs)
    uint8_t joined{0};
    std::array<std::string, 4> names{};     // by seat; "" for an empty seat
    uint32_t ticks{0};                      // the referee's clock (20 per second)
    uint64_t referee_hash{0};               // the referee's state hash (StateHash::total) at `ticks` when the match ended, 0 before: what every client of the match stands at, the same tick
                                            // (the tests compare it; a lobby that keeps results can too)
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
    // Restart records (restart_record.hpp). Never a key.
    bool record_kept{false};                // a restart record of this room is on disk: a restart of the server would bring the match back (also a file that could not be deleted: record_stale)
    bool record_stale{false};               // the room is over (or its record ended) and the file of the record could not be deleted yet: the server tries again every 10 s, and a restart before that would bring the room back
    std::string record_note;                // when it is not: why (the server keeps none, the room holds no seats, the turn log passed its limit, the disk refused ...); "" while it is kept
    uint64_t record_bytes{0};               // the size of the record
    bool restored{false};                   // the room came back from a record after a restart of the server
    uint32_t restored_turns{0};             // ... holding this many turns
    uint32_t restore_ms{0};                 // ... which the replay took this long to run (real time)
    uint64_t restored_hash{0};              // ... and the referee's state hash (StateHash::total) was this at the restored tick
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
    ~Room();

    /// The server's restart records (restart_record.hpp; it must outlive the room): a room that holds seats and whose match starts keeps a record there. Set it right after the room is made, before its
    /// match is started. Null (the default): the room keeps none.
    void set_restart_store(RestartStore* store) noexcept { restart_store_ = store; }
    /// What replay() asks its caller at every 20th turn: go on, stop (the server was told to stop: everything stays as it is), or defer (the time of the whole restore is used up: this room is restored later)
    enum class ReplayCheck : uint8_t { Go, Stop, Defer };
    struct ReplayLimits {
        uint32_t budget_ms{20u * 1000u};                    // this room's replay may take this long by `clock` (the room's own cap): beyond it the record is refused as too slow
        std::function<uint32_t()> clock;                    // real milliseconds (empty: restart_steady_ms)
        std::function<ReplayCheck()> check;                 // empty: always Go
    };
    enum class ReplayResult : uint8_t { Replayed, Refused, Stopped, Deferred };
    /// The first half of bringing a match back (a server that starts again: RoomManager::restore_rooms, or the first Hello for a deferred room): the engine is made from the start message and every sealed
    /// turn of `record` is replayed, checked against the record's state hashes (the room's clocks are not touched: nothing here knows what time it is). The room must be fresh (Waiting, made from
    /// room_spec_of(record.head)). Replayed: the match is rebuilt and waits for begin_restored(). Refused, with the reason in `why`: the match cannot be brought back (the replay does not agree with the
    /// hashes, the turn log cannot hold it, it is too slow for `limits.budget_ms`, ...): the room is to be thrown away, it holds a half-built match. Stopped and Deferred are the answers of `limits.check`
    /// (not a fault of the record: nothing is said against it): the room is to be thrown away as well, and the record must be left as it is. `restart_vote_after_ms` is how long the others wait before
    /// they may vote on a seat that has not come back (never less than the room's own time).
    ReplayResult replay(const RestartLoaded& record, uint32_t restart_vote_after_ms, const ReplayLimits& limits, std::string& why);
    /// The second half: the match that replay() rebuilt begins at `now_ms`, the server's clock now. Every seat of a person is held absent from `now_ms` on (the match is paused until the players come back with
    /// their keys), the room's clocks (the wall-clock limit, the vote's wait, the pause cap) start at `now_ms` and not when the restore began, the bots sit down again at the restored tick, and the room goes
    /// on writing the same record. A match that had ended when the server stopped is Finished at once. False, with the reason, when it cannot begin.
    bool begin_restored(uint32_t now_ms, std::string& why);
    /// A room that could not be brought back: Failed from the start, with `reason`, so that the status shows it (and the log and the result file report it) and its code is not taken by a new room
    /// until its keep time is over. `head` names the room (its code, map, players and the names of the match); no engine, no record.
    static std::unique_ptr<Room> refused(const RestartHead& head, const std::string& reason, uint32_t turns, uint32_t now_ms);
    /// The room keeps a restart record now
    bool keeps_record() const noexcept { return record_ != nullptr; }
    /// Makes the record durable now (fsync): the server is told to stop. A failure ends the record (the status says why).
    void flush_record();
    /// The server stops: the record stays on disk as it is and the room writes nothing more to it (the file is closed). The room is not over.
    void release_record();

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
    /// What a restart would interrupt here (see RoomBusy): cheap, no status is built
    RoomBusy busy(uint32_t now_ms) const;

private:
    void fail(const std::string& reason, uint32_t now_ms);
    void begin_match(uint32_t now_ms);
    void finish(const std::string& reason, uint32_t now_ms);
    void close_connections();
    void prune_connections();
    void end_log(uint32_t now_ms);
    // Restart records
    RestartHead make_head() const;                           // the record's head from what the room is and what its lobby gave out (the keys): after the start was accepted
    void record_open(uint32_t now_ms);                       // the match is being started: make the record (when the server keeps records and the room holds seats)
    void record_hook_up();                                   // the session writes every sealed turn and the referee's hashes to the record
    void record_stop(const std::string& note);               // the record can no longer be kept: delete it, say why, go on
    void record_discard();                                   // the room is over (or the start was cancelled): delete the record
    void drop_record();                                      // delete the record and let the writer go; a delete that fails is remembered (stale_path_)
    void build_session(uint32_t restart_vote_after_ms);      // the session of the match, as begin_match and restore both make it (the engine is made already)
    // Bots (docs/BOTS.md B6): the specification's are seated in the lobby when the room is made; the leader's fill seats the rest at START and takes them out again when the start is cancelled
    class BotSink;
    void unseat_fill();
    bool start_bots(uint32_t seed, std::string& why);

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
    std::vector<ai::BotSpec> bot_specs_;     // the bots that sit in the room now, by seat (the specification's and the fill's)
    uint8_t fill_seats_{0};                  // bit s: seat s holds a bot that the leader's START seated (it goes again when the start is cancelled)
    std::vector<std::unique_ptr<net::Connection>> connections_;
    std::unique_ptr<sim::SimulationEngine> sim_;
    std::unique_ptr<net::HostSession> session_;
    std::vector<std::unique_ptr<sim::CommandSink>> bot_sinks_;      // (after the session and before the controller: the controller is destroyed first, then the sinks that it holds)
    std::unique_ptr<ai::BotController> bot_controller_;
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
    uint64_t final_hash_{0};                 // the referee's state hash when the match ended
    std::array<std::string, 4> names_{};
    // Restart records
    RestartStore* restart_store_{nullptr};
    std::unique_ptr<RestartWriter> record_;
    std::string record_note_;                // why there is no record (the status says it)
    std::string stale_path_;                 // the file of a record that could not be deleted: the status says kept and stale until the store has deleted it
    uint32_t next_sync_ms_{0};
    uint8_t roster_{0};                      // the seats of the match (a restored room has no lobby that knows them)
    bool from_record_{false};                // the room has no lobby that knows its players: it was made from a restart record (restored, or refused)
    bool restored_{false};
    uint32_t restored_turns_{0};
    uint32_t restore_ms_{0};
    uint64_t restored_hash_{0};
    uint32_t restored_at_ms_{0};             // when the room was brought back (the server's clock): /busy counts the room as a match for kRestoredBusyWindowMs after it
    // what replay() hands to begin_restored()
    bool replayed_{false};
    uint8_t replay_humans_{0};               // the seats of persons, and those of them that the match had dropped already
    uint8_t replay_dropped_{0};
    std::string replay_path_;                // the record, to be opened again for appending: its file, the end of its last good frame, its turns
    uint64_t replay_good_bytes_{0};
    uint32_t replay_turns_{0};
};

/// The room's specification as a restart record's head says it (the bots of the specification only: the leader's fill is the record's `fill_mask`, seated by Room::replay). A restored room always
/// holds seats. The caller checks the ranges (RoomManager does, as create_room does).
RoomSpec room_spec_of(const RestartHead& head);

}  // namespace ants::server
