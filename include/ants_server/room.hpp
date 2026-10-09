#pragma once

// One game on the server: the room (a host without a seat, ants::net::HostLobby), then the match (a HostSession that runs the simulation as the referee), then
// the result. A room is made from outside (RoomSpec: the map, the fog option, how many players it waits for) and starts by itself when everybody who was
// expected has joined and loaded; nobody on a client has to press anything. It lives in the server's main loop like every other part of the network code:
// no threads, no blocking, one update(now_ms) per pass.
//
//   Waiting  clients join (Hello with the room's code); when `players` seats are taken the room starts: Start goes to all, the server's own copy of the map is
//            already loaded (the room was refused otherwise), every client loads and reports, and the match begins when all have. The first player who joined is the
//            room's LEADER (the next one when it leaves); with `early_start` on, the leader's StartRequest starts the match at once with the players who are there
//            (two at least), without waiting for the rest. With FILL LEVELS in the request (protocol 11; a level for each seat since 13) the room first seats a bot of the seat's level, named
//            "Bot (Medium)", in every seat that is still empty and has a level (lowest seat first, up to `players`; a seat that a person took meanwhile is skipped), and then one person is
//            enough; a room with Fog of War refuses the bots (bots and fog never mix, docs/BOTS.md rule 8: the leader is told, and the match starts without them if two people are there), and
//            so does a map that the filled roster cannot play. Only the leader's request seats bots: a room that fills up, or starts by itself, never does. The request's TEAMS (protocol 13) go
//            into the Start message when the seats that play can make them (sim::plan_start_teams), else the match starts without teams and every person in the room is told why
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
// that a server that is stopped, crashes or is redeployed does not end the match. A server that starts again brings the room back from the record in two halves (RoomManager queues the records and
// replays them in slices from update(), so that it serves meanwhile): Room::begin_replay and Room::replay_step, then Room::begin_restored: the engine is made from the start message, every sealed turn is replayed and
// checked against the stored hashes, every seat of a person is held ABSENT (the match is paused until its player comes back with its key, protocol 10, with a longer wait before the others may vote than for a
// lost link), the bots sit down again at the restored tick (their tasks are soft: they look at the world and go on), and the room keeps its code, so that the players' clients find it. A record that cannot be restored (another network protocol, a map that has changed, a replay that does not
// agree with the hashes, too old) ends the room with the reason (Room::refused: a failed room that the status shows and the log reports). A finished or failed room deletes its record; a server that is told
// to stop (RoomManager::shutdown) makes the records durable and leaves them where they are.
//
// Lobby rooms (protocol 16, docs/NETWORK_PORT.md "Protocol 16"). A room made with RoomSpec::lobby is the room that the front page waits in from its first second: a visitor's page makes it with a create block
// and invites the others with the link. It has four seats, a leader that starts it and keys, and the rules of HostLobby's lobby room (the leader's plan decides what a START seats, a seat whose connection
// ended is held for a minute, the START waits for the games of all its people). It has no wait_ms and is not full: it lives while a person is in it (a held seat counts as one) and is forgotten, without a word
// in the log or a result file, `empty_close_ms` after the last person has gone. The leader's START reaches the room in every pass while every person is a game: the room loads the plan's map (the server
// offers the maps of --demo-maps), checks that the seats which play can play it, asks the server whether it has a place for another match, and then seats the plan's bots and starts like any room; what it
// cannot do ends the START with a notice to the leader (HostLobby::end_start) and the room goes on waiting. A failed start never fails the room. A match that a lobby room has begun is a room like the others.
//
// Replays (replay_store.hpp, docs/REPLAYS.md "On the game server"). A room of a server that keeps replays records its match: ants_replay's Recorder watches every turn that the referee's own runner
// executes (LockstepRunner::set_on_executed), from the first, and when the match is over, or the room fails or is closed while it runs, the recording is written as a .antsrep file and handed to the
// server's ReplayStore. A match is kept when it ran 600 turns (30 seconds), however it ended; a seat has the name that the room showed everybody (what the person typed: replay_person_name; a seat that has no name
// of its own is the colour in the readers) or, for a computer player, its display name. A room that was brought back from a restart record does not record (the part of its match before the restart is not seen).
// The status says whether the match was kept, under what name and, if not, why. A room made with `"record": false` records nothing.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_controller.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/live_board.hpp"
#include "ants_server/replay_store.hpp"
#include "ants_server/restart_record.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/start_teams.hpp"

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
    /// The room's own teams (protocol 13; the create block's since protocol 15): the match starts with them EVERY time, when the full room starts by itself and
    /// when its leader's START starts it early; a leader's StartRequest has teams of its own only in a room that has none (sim::start_teams_for). They are checked against the seats that really play at
    /// the start (sim::plan_start_teams): when they cannot be made the match starts without them and the room says why. Not set: the leader's START chooses.
    sim::StartTeams teams;
    /// Protocol 15: a room that a visitor's create block made (RoomManager::make_public_room). The server counts these against its public-room cap (--demo-rooms), gives them the short pause cap and
    /// ends the one that has been abandoned the longest when a new one needs the place; a room that the control interface made is not one.
    bool public_room{false};
    /// Protocol 15: the room does not start by itself when every seat is taken: only its leader's START starts the match (the leader can arrange the seats first). It needs a leader, so it is
    /// for a room with `early_start` (a room that has none ignores it).
    bool leader_starts{false};
    /// Protocol 16: a lobby room (see the paragraph at the top; RoomManager::make_lobby_room makes it from a page's create block). It needs four players, an early start, a leader that starts it and reconnect
    /// (the keys), and no Fog of War, bots or teams of its own (the leader's plan decides): create_room refuses it otherwise. `hold_ms`: a seat whose connection ended is held this long; `start_wait_ms`: the
    /// leader's START waits this long for every person's game; `silence_ms`: a connection that says nothing for this long is closed; `empty_close_ms`: the room is forgotten this long after the last person has gone.
    bool lobby{false};
    uint32_t hold_ms{net::kLobbyHoldMs};
    uint32_t start_wait_ms{net::kLobbyStartWaitMs};
    uint32_t silence_ms{net::kLobbySilenceMs};
    uint32_t empty_close_ms{60u * 1000u};
    bool has_seed{false};
    uint32_t seed{1};                       // the match's random seed (the server draws one when the spec has none)
    uint32_t wait_ms{120000};               // a room that has not started after this long fails ("nobody came", "somebody is missing")
    uint32_t load_ms{60000};                // everybody must have loaded the map this long after the start
    uint32_t keep_ms{10u * 60u * 1000u};    // a finished or failed room stays visible to status calls this long
    uint32_t run_ms{2u * 3600u * 1000u};    // a match that is still running this long after it began is ended (failed, "took too long"): a wall-clock limit on the life of
                                            // a room (it never waits for a seat, but a match that does not end by its rules would run on for ever). It counts from the moment the match
                                            // began (the 5 s before its first turn, the start dialog, count: protocol 12), without the time that it waited for a seat that was away
    // Reconnect (protocol 10): the room holds the seat of a player whose connection is lost. A spec built by code says what it wants (false here); the server's rooms start from
    // RoomManager::default_spec(), which holds seats unless `--no-reconnect` or the room's own "reconnect": false says otherwise (kReconnectByDefault).
    bool reconnect{false};
    uint32_t vote_after_ms{net::kVoteAfterMs};      // the others may vote on going on without a seat once it has been away this long in all (5 s .. 1 h; the control key "hold_vote_seconds")
    uint32_t max_pause_ms{net::kMaxPauseMs};        // the match's total paused time is capped: at the cap every seat that is not present is dropped (1 min .. 24 h; the control key "max_pause_seconds")
    size_t max_log_bytes{net::TurnLog::kDefaultMaxBytes};    // the limit of the match's turn log (16 MiB: 25 times a busy match); past it a lost seat is dropped at once again
    uint32_t max_catch_up_ms{net::kMaxCatchUpMs};            // one absence may spend this long catching up in all, over all its attempts (10 s .. 1 h; the control key "max_catch_up_seconds"): then the
                                                             // catch-up fails, the seat is absent (the vote and the cap apply) and the key is refused
    uint32_t resume_countdown_ms{net::kResumeCountdownMs};   // after a pause of at least 3 s the match is held this long before it goes on (0 .. 60 s, 0 = none; the control key "resume_countdown_seconds")
    size_t max_connections{32};                              // the connections that the room keeps at once (everything that ever said Hello to it, and every connection that came back, until it is closed and
                                                             // nobody uses it): a flood is refused beyond this. 32 for every room the server makes; the control interface has no key for it
    bool record_replay{true};                                // the match is kept as a replay when the server keeps replays (the control key "record": false switches it off for the room)
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
    bool public_room{false};                // made by a visitor's create block (protocol 15): for the tests, not shown by the control interface
    bool leader_starts{false};              // the full room waits for its leader's START (protocol 15): for the tests, not shown by the control interface
    bool lobby{false};                      // a lobby room (protocol 16): the control interface shows lobby rooms as such
    bool starting{false};                   // ... whose leader's START waits for the games
    uint8_t games{0};                       // ... the seats (bit s) that hold a game: a page, a seat that is held and an empty seat do not
    std::string plan;                       // ... the plan: the kind of each colour as a letter (o open, e easy, m medium, h hard, n nobody) and the team pair ("oehn 0+3", "-" for none)
    uint8_t leader{255};                    // the seat of the leader while the room waits or loads (255: none: nobody has joined yet, the room does not allow an early start, or the match runs)
    uint32_t ignored_start_requests{0};     // StartRequest messages that were heard and not acted on (a player who is not the leader, a room that cannot start, a late click of a match that runs)
    uint32_t seat_moves{0};                 // the colours that the leader moved a player to (protocol 14: each one that the room carried out)
    uint32_t ignored_seat_moves{0};         // SeatMove messages that were heard and not acted on (a player who is not the leader, a seat that is not a player's, a late move of a match that runs)
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
    std::string teams;                      // the teams that the match started with, "ffa" or "A+B" (protocol 13; "" before the match), and the referee's own alliances by seat now (4: none): for the tests, not shown by
    std::array<uint8_t, 4> allies{4, 4, 4, 4};     // the control interface
    std::string room_teams;                 // the room's own teams, "A+B" ("" when it has none: the leader's START chooses): for the tests, not shown by the control interface
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
    uint32_t rejoins_refused{0};            // Hellos with a key that were refused for a budget (the absence's catch-up time, twelve attempts a minute, three times the log's size in ten minutes)
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
    uint32_t record_sync_ms{0};             // the interval to the record's next flush: RestartConfig::sync_every_ms, longer while a flush is slow (0: no record, or not yet flushed)
    // Replays (replay_store.hpp). The file's name is the map and the end time, never the name of a person (the file itself holds the names that the room showed).
    bool replay_kept{false};                // the match was kept as a replay on the server ...
    std::string replay_file;                // ... under this name (never a path)
    uint64_t replay_bytes{0};
    std::string replay_note;                // when it was not, or will not be: why (the server keeps none, the room records nothing, the match was too short, the store refused ...); "" while it may still be kept
    bool restored{false};                   // the room came back from a record after a restart of the server
    uint32_t restored_turns{0};             // ... holding this many turns
    uint32_t restore_ms{0};                 // ... which the replay took this long to run (real time)
    uint64_t restored_hash{0};              // ... and the referee's state hash (StateHash::total) was this at the restored tick
};

/// The name that a replay file keeps for a person's seat, from the name that the room showed everybody (the Start message's): the name as it is, in printable ASCII (every byte of anything else becomes a '?': a letter with an accent, two bytes of UTF-8, gives two), at most
/// 32 characters, the blanks at both ends cut. The words that the game itself uses for a seat that has no name of its own give "" (the readers then show the colour: "Green", not "Green (Player)"): "Player"
/// (what a game proposes and sends when nothing was typed), "Player 1" to "Player 4" (what the lobby calls a person whose name looks like a bot's, and what every screen shows for a seat without a name), and
/// nothing at all. A name that only starts like them ("Players", "Player 5", "player") is a name.
std::string replay_person_name(const std::string& shown);

/// What a lobby room asks of the server that runs it (protocol 16; RoomManager gives it to the rooms it makes). The room has no maps folder of its own and does not know the server's limits.
struct LobbyServices {
    /// The file name that the server gives a map of a leader's plan ("" when it offers none by that name: the map of the room stays)
    std::function<std::string(const std::string& name)> choose_map;
    /// Loads that file for a match (false: it is gone or cannot be played by this engine)
    std::function<bool(const std::string& file, MapEntry& entry, assets::LevelData& level)> load_map;
    /// Whether the server has a place for one more match now (and makes one when an abandoned match can give its place up)
    std::function<bool(uint32_t now_ms)> match_place;
};

class Room {
public:
    static constexpr size_t kMaxConnections = 32;           // RoomSpec::max_connections of a room that says nothing: everything that ever said Hello to it (rejected ones included): a flood is refused beyond this
    static constexpr uint32_t kRetryMs = 2000;              // the pause after a cancelled start
    // A lobby room (protocol 16) does not fail for cancelled starts as the other rooms do (kMaxFailedStarts), so a game that never loads would make the whole room load and cancel every two seconds for ever.
    // It backs off instead: the first kFreeCancels cancels in a row cost kRetryMs each, every one after them kLongPauseMs, and the count is forgotten after kCancelsForgottenMs without one. A START that is asked
    // for in a pause longer than kStandMs does not wait it out as it does a short one: its leader is told (kNoticeStartsFailed) and the room is open for its plan again.
    static constexpr uint32_t kFreeCancels = 3;
    static constexpr uint32_t kLongPauseMs = 60u * 1000u;
    static constexpr uint32_t kStandMs = 10u * 1000u;
    static constexpr uint32_t kCancelsForgottenMs = 15u * 60u * 1000u;
    static constexpr uint32_t kMaxIdleMs = 24u * 3600u * 1000u;     // idle_ms() stops here (a 32-bit clock reads an age of 49.7 days as a short one)
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
    /// The server's store of replays (replay_store.hpp; it must outlive the room): the match is recorded and kept there. Null (the default): the room records nothing, and `why_not` is what its status says. Set it
    /// right after the room is made, before its match is started.
    void set_replay_store(ReplayStore* store, const std::string& why_not = std::string());
    /// The server's board of the matches that run (live_board.hpp; it must outlive the room): the room registers the recording of its match there when the match begins and takes it off when the recording becomes
    /// a file or is dropped (and in its destructor). Null (the default): the match is not shown live. Set it right after the room is made, before its match is started.
    void set_live_board(LiveBoard* board) noexcept { live_board_ = board; }
    /// What a lobby room asks of its server (see LobbyServices): set it right after the room is made. A room that is no lobby room never asks.
    void set_lobby_services(LobbyServices services);
    /// Bringing a match back from a record, first half (RoomManager replays the queue in slices from update(), so that the server serves meanwhile). begin_replay() does everything before the turns: the
    /// bots, the engine, the session with its keys. The room must be fresh (Waiting, made from room_spec_of(record.head)) and `record` must stay alive until replay_step() has said Replayed or Refused.
    /// Refused, with the reason in `why`: the room holds a half-built match and is thrown away. `restart_vote_after_ms`: how long the others wait before they may vote on a seat that has not come back.
    enum class ReplayBegin : uint8_t { Ready, Refused };
    ReplayBegin begin_replay(const RestartLoaded& record, uint32_t restart_vote_after_ms, std::string& why);
    /// Replays turns for at most `slice_ms` by `clock` (real milliseconds; empty: restart_steady_ms), looking at it after every 20th turn (a slice runs at most 20 turns' work past its time); at every
    /// checkpoint the referee's state hash must be the stored one. More: go on in the next slice. Replayed: the match is rebuilt and waits for begin_restored(). Refused, with the reason: the replay
    /// disagrees with the hashes, the log cannot hold the match, the turns cannot be read, or the SUMMED work of the slices passed RestartConfig::replay_budget_ms (too slow). The room's clocks are not touched.
    enum class ReplayStep : uint8_t { More, Replayed, Refused };
    ReplayStep replay_step(uint32_t slice_ms, const std::function<uint32_t()>& clock, std::string& why);
    /// begin_replay() and then every slice in one go (no slice): Replayed or Refused
    ReplayStep replay(const RestartLoaded& record, uint32_t restart_vote_after_ms, std::string& why, const std::function<uint32_t()>& clock = nullptr);
    /// The turns that the replay has read so far, and the real time that its slices have taken (the sum of the work, not the time that other rooms' work took between them)
    uint32_t replay_progress() const noexcept { return replay_reader_ != nullptr ? replay_reader_->turns_read() : replay_read_; }
    uint32_t replay_work_ms() const noexcept { return replay_work_ms_; }
    /// The second half: the match that the replay rebuilt begins at `now_ms`, the server's clock now. Every seat of a person is held absent from `now_ms` on (the match is paused until the players come back with
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
    /// Made by a visitor's create block (protocol 15): one of the server's public rooms
    bool public_room() const noexcept { return spec_.public_room; }
    /// A lobby room (protocol 16) ...
    bool lobby() const noexcept { return spec_.lobby; }
    /// ... that waits (no match has been started): it is in the server's pool of lobbies, and holds no place of the public rooms
    bool lobby_waiting() const noexcept { return spec_.lobby && state_ == RoomState::Waiting; }
    /// A waiting lobby room that has had nobody in it since the last pass (a held seat is somebody) and that no Hello has reached since (the room reads it in its next pass: a lobby that a visitor was just
    /// sent to is not empty, whatever the room's last pass saw), and for how long (0 for every other room)
    bool empty_lobby() const noexcept { return lobby_waiting() && empty_ && !arrived_; }
    uint32_t empty_ms(uint32_t now_ms) const noexcept { return empty_lobby() ? now_ms - empty_since_ms_ : 0u; }
    /// How long nothing has been done in a waiting lobby room (HostLobby::activity: a ping that is answered is nothing), 0 for every other room and for one that a Hello has reached since its last pass. The manager gives
    /// the place of an idle lobby up when it needs it (never otherwise: a person may sit in a lobby for as long as they like). It stops growing at kMaxIdleMs.
    uint32_t idle_ms(uint32_t now_ms) const noexcept { return lobby_waiting() && !arrived_ ? now_ms - active_ms_ : 0u; }
    /// The owner forgets the room at once, without a word in the log or a result file (the server needs the place of a lobby that nobody is in): its clients are dropped
    void forget(uint32_t now_ms);
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
    /// `created`: this very Hello made the room (the Welcome of its new seat says so, protocol 16)
    bool add_connection(std::unique_ptr<net::Connection>& connection, const std::string& address, const std::vector<uint8_t>& hello, uint32_t now_ms, bool created = false);
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
    /// How long a running match has had no person at it, in ms: every seat of a person is held absent (nobody is present or catching up), counted from the last pass in which one was. 0 while a person is
    /// there, whatever the clock says (a stalled loop makes a time old that is not an absence), and 0 for a room that does not run.
    /// RoomManager ends the demo room with the biggest number when a Hello needs its place (kDemoAbandonedMs).
    uint32_t abandoned_ms(uint32_t now_ms) const;

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
    void sync_record(uint32_t now_ms);                       // the timed flush, and its pace (RestartConfig::slow_sync_ms)
    void end_replay() noexcept;                              // the replay is over (it is rebuilt or refused): the record is not read any more
    // Replays kept on the server (replay_store.hpp)
    void replay_begin(const net::StartMsg& start);           // the recorder of the match is made and taps the referee's runner (begin_match: before the first turn)
    void replay_end();                                       // the match is over (finish, fail while it ran): the recording is made into a file and kept, or the status says why not
    std::string replay_keep(replay::Recorder& recorder);     // the part of replay_end that makes the file and sets replay_file_ or replay_note_: the file's name, "" when the match is not kept
    void live_end(const std::string& kept_file);             // takes the recording off the live board (the file's name, "" when it was not kept); the board holds a pointer to the recorder
    bool finish_replay(const RestartLoaded& rec, size_t next_check, std::string& why);      // every turn was given: the last checks, and what begin_restored() needs
    void build_session(uint32_t restart_vote_after_ms);      // the session of the match, as begin_match and restore both make it (the engine is made already)
    // Bots (docs/BOTS.md B6): the specification's are seated in the lobby when the room is made; the leader's fill seats the rest at START and takes them out again when the start is cancelled
    class BotSink;
    void unseat_fill();
    void notify_people(const std::string& text);             // the room says a line to every person in it (a notice)
    bool start_bots(uint32_t seed, std::string& why);
    // Starting the match from the waiting room (the full room, the leader's START, a lobby room's plan)
    uint8_t roster_with(const std::vector<std::pair<uint8_t, net::FillLevel>>& fill_seats) const;
    bool start_lobby(uint32_t now_ms, uint8_t roster, const std::vector<std::pair<uint8_t, net::FillLevel>>& fill_seats, const sim::StartTeams& wanted);
    void lobby_waiting_pass(uint32_t now_ms, uint8_t asked_by, const std::array<net::FillLevel, sim::MAX_PLAYERS>& asked_fill, const sim::StartTeams& asked_teams);
    bool retry_open(uint32_t now_ms) noexcept;               // the pause after a cancelled start is over (or there was none)
    uint32_t lobby_pause_ms(uint32_t now_ms) noexcept;       // a lobby room's start was cancelled now: how long the room waits before it tries again (kFreeCancels, kLongPauseMs)

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
    uint32_t retry_at_ms_{0};                // after a cancelled start the room waits a moment before it tries again (a client that cannot load the map does not make a tight loop): until here, while retry_pending_
    bool retry_pending_{false};              // (a flag and not an old time: a lobby room waits for ever, and a time that is older than 24.8 days reads as a time to come in a signed comparison)
    uint32_t last_cancel_ms_{0};             // a lobby room: when its start was last cancelled (cancels_ counts those in a row)

    net::HostLobby lobby_;
    LobbyServices services_;                 // protocol 16: what a lobby room asks of its server
    bool empty_{false};                      // a lobby room: nobody is in it, since empty_since_ms_
    bool arrived_{false};                    // a lobby room: a connection was given to it since its last pass (its Hello is read in the next one: empty_ is stale until then)
    uint32_t seen_activity_{0};              // a lobby room: HostLobby::activity() at the last pass, and since when it is what it is
    uint32_t active_ms_{0};
    uint32_t empty_since_ms_{0};
    bool forgotten_{false};                  // the owner forgot the room (forget): it is expired at once
    std::vector<ai::BotSpec> bot_specs_;     // the bots that sit in the room now, by seat (the specification's and the fill's)
    uint8_t fill_seats_{0};                  // bit s: seat s holds a bot that the leader's START seated (it goes again when the start is cancelled)
    sim::StartTeams start_teams_;            // the teams of the match's Start (protocol 13): every engine of the match made them before its first tick
    std::vector<std::unique_ptr<net::Connection>> connections_;
    std::unique_ptr<sim::SimulationEngine> sim_;
    std::unique_ptr<net::HostSession> session_;
    std::vector<std::unique_ptr<sim::CommandSink>> bot_sinks_;      // (after the session and before the controller: the controller is destroyed first, then the sinks that it holds)
    std::unique_ptr<ai::BotController> bot_controller_;
    ReplayStore* replay_store_{nullptr};
    LiveBoard* live_board_{nullptr};
    std::string live_id_;                    // the recording's id on the live board ("": it is not there)
    std::unique_ptr<replay::Recorder> recorder_;     // the match so far (null: it is not recorded, or it is over)
    std::string replay_file_;                // the match was kept under this name ...
    uint64_t replay_bytes_{0};
    std::string replay_note_;                // ... or this says why not
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
    uint32_t sync_every_ms_{0};              // the interval to the next flush of the record: the configured one, doubled (to a limit) by every slow flush
    uint8_t roster_{0};                      // the seats of the match (a restored room has no lobby that knows them)
    bool from_record_{false};                // the room has no lobby that knows its players: it was made from a restart record (restored, or refused)
    bool restored_{false};
    uint32_t restored_turns_{0};
    uint32_t restore_ms_{0};
    uint64_t restored_hash_{0};
    uint32_t restored_at_ms_{0};             // when the room was brought back (the server's clock): /busy counts the room as a match for kRestoredBusyWindowMs after it
    uint32_t last_person_ms_{0};             // the last pass of a running match in which a person was present or catching up (abandoned_ms)
    // the replay under way (begin_replay, then replay_step until it says Replayed or Refused)
    const RestartLoaded* replay_rec_{nullptr};   // the record that the turns are read from (the caller keeps it alive)
    std::unique_ptr<RestartTurnReader> replay_reader_;
    size_t replay_next_check_{0};            // the next checkpoint of the record to compare the referee's state with
    uint32_t replay_read_{0};                // the turns that the replay read in the end (replay_progress() once the reader is gone)
    uint32_t replay_work_ms_{0};             // the real time that the slices so far have taken: the room's cap (RestartConfig::replay_budget_ms) is on this sum
    // what the replay hands to begin_restored()
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
