#pragma once

// The rooms of a game server and the door they share. Every connection (TCP from a native client, WebSocket from a browser or Electron through the reverse
// proxy) arrives here without knowing its room: the first message must be a Hello within a few seconds, and its room code (protocol 6) names the room. A Hello for
// a room that does not exist, a Hello of another protocol version and anything that is not a Hello are answered with a Reject and the connection is closed;
// a Hello for a room goes to that room's lobby together with the connection.
//
// Rooms are made from outside (create_room: the control interface calls it), update() runs every room and forgets the finished ones when their keep time is over.
// Limits protect the server: the number of rooms, the number of connections that have not said Hello yet, the time they get for it.
//
// A Hello with a KEY (protocol 10) for a room whose match runs and that holds seats goes to that room (Room::rejoin: the session takes the connection over and gives the player the match
// again); a key that fits no seat, a room that does not hold seats and a room that is loading are answered MatchRunning, as a Hello without a key is, so that nothing is revealed; a room
// that is over is NoSuchRoom. A Hello with a key for a room that still waits is the lobby's (a page that was reloaded in the waiting room takes its seat over). A Hello for a room whose restart
// record still waits for its replay is PARKED (the connection and the Hello wait in the manager, a minute at the most) and goes through this door as soon as the room is restored. The server's
// own defaults for what the rooms hold (reconnect, the vote, the cap, the limit of the log) and the budget that all the logs share are ServerLimits.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ants_net/protocol.hpp"
#include "ants_net/transport.hpp"
#include "ants_net/turnlog.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/restart_record.hpp"
#include "ants_server/room.hpp"

namespace ants::server {

class ParkedLink;                                // (room_manager.cpp: the connection of a Hello that waits for its room)

/// Whether the rooms of a server hold the seat of a player whose connection is lost unless something says otherwise (ServerLimits::reconnect, the server's `--reconnect` / `--no-reconnect`: the rooms
/// that the control interface makes and the demo rooms follow it). ON since release B: the game's own clients come back by themselves (NetGame, the application's screens, the page's Rejoin).
inline constexpr bool kReconnectByDefault = true;

struct ServerLimits {
    size_t max_rooms{256};
    size_t max_pending{128};                // connections that have not said Hello yet
    uint32_t hello_timeout_ms{10000};       // ... and the time they get for it
    uint32_t reject_linger_ms{2000};        // a rejected connection is kept open this long so that the answer reaches the peer
    uint32_t park_timeout_ms{60000};        // a Hello for a room that waits for its replay (a restart record) waits this long for the room, then its connection is dropped (no hello timeout applies meanwhile)
    // Demo rooms (off by default; for a public test page that has no secret to make rooms with): a Hello for a room whose code starts with "demo-" that does not
    // exist makes it, on `demo_map`, for `demo_players` players, at most `demo_rooms` of them at a time (0 = off); the code can choose the map and the players
    // (`demo_maps`). A demo room waits `demo_wait_ms` (ten minutes) for its players and is forgotten half a minute after it ended; a Hello for the code of one that
    // is over makes a new room at once. Whoever can reach the door can fill these rooms and hold them for a match: that is the price of a page that works without
    // a secret; real rooms are made by the control interface.
    size_t demo_rooms{0};
    std::string demo_map;
    uint8_t demo_players{4};
    // How long a demo room waits for its players, from the first Hello (the page's link goes to friends on other computers: they need time to arrive)
    uint32_t demo_wait_ms{10u * 60u * 1000u};
    // The maps that a Hello may choose for a demo room. The code of a demo room chooses: "demo-[<map>-][<n>p-]<anything>": <map> is the name of one of these
    // maps without its extension (any case, then a dash; file names of the maps folder, e.g. "SMALL.LVL"), <n>p the number of players, 2 to 4 ("demo-small-2p-x7k2":
    // SMALL.LVL for two). The player count may also follow a first word that is not one of these maps ("demo-medium-2p-x" on a server without MEDIUM: the default
    // map, for two), so that a page that offers a map the server does not allow still gets the players it asked for. What a code does not choose is `demo_map` and
    // `demo_players`, as before. The choice rides in the room code because the Hello has no other field (the protocol is unchanged).
    std::vector<std::string> demo_maps;
    // Reconnect (protocol 10; ants_server's --reconnect / --no-reconnect, --hold-vote-seconds, --max-pause-seconds, --max-catch-up-seconds, --resume-countdown-seconds, --log-mb): what a room holds unless its specification says otherwise. Demo rooms follow
    // `reconnect`. ON by default (kReconnectByDefault).
    bool reconnect{kReconnectByDefault};
    uint32_t hold_vote_ms{net::kVoteAfterMs};                       // the vote opens after a seat has been away this long in all
    uint32_t max_pause_ms{net::kMaxPauseMs};                        // a match's pauses may last this long in all; at the cap every seat that is not present is dropped
    uint32_t max_catch_up_ms{net::kMaxCatchUpMs};                   // one absence may spend this long catching up in all (--max-catch-up-seconds)
    uint32_t resume_countdown_ms{net::kResumeCountdownMs};          // the match is held this long after a pause of 3 s or more (--resume-countdown-seconds; 0: none)
    size_t room_log_bytes{net::TurnLog::kDefaultMaxBytes};          // the limit of one room's turn log (16 MiB)
    uint64_t log_budget_bytes{256ull * 1024ull * 1024ull};          // the memory that all the rooms' logs may take together; a log that cannot grow is not kept (its room drops a lost seat at once)
};

/// The code prefix of the rooms that a Hello may make when demo rooms are on
inline constexpr const char* kDemoRoomPrefix = "demo-";

/// A demo room's cap on the match's pauses: the match of a page's link that nobody comes back to must not hold one of the few demo places for the server's 30 minutes. A demo room takes the smaller of
/// this and the server's cap; the rooms of the control interface keep the server's.
inline constexpr uint32_t kDemoMaxPauseMs = 10u * 60u * 1000u;

/// A demo room whose people have all been gone this long is abandoned: when a Hello needs a demo place and none is free, the demo room that has been abandoned the longest is ended and its place is
/// given to the new room (RoomManager::make_demo_room). A room with a person at its match, or in its lobby, is never ended for that. The floor keeps a blip of every link at once (a proxy that
/// restarts) from costing a match.
inline constexpr uint32_t kDemoAbandonedMs = 60u * 1000u;

/// What a restart would interrupt (the public /busy answer): the rooms whose match is loading or running with a person in it (a room that a restart brought back also for its first minutes: Room::busy),
/// and the people (bots are not people) in the rooms that wait, load or run. Plain counts: no name, no code; exactly these two fields (tools/deploy_wait.py accepts nothing else).
struct BusyCounts {
    uint32_t matches{0};
    uint32_t players{0};
};

struct CreateResult {
    bool ok{false};
    int http_status{201};                   // what the control interface answers: 201, 400, 404, 409, 503
    std::string error;
    std::string code;                       // the room's code (generated when the spec had none)
};

/// What became of one restart record (RoomManager::restore_rooms decides some at once, update() the rest as their replays end)
struct RestoreItem {
    enum class Outcome : uint8_t {
        Restored,           // the room is back: running, paused until its players come back
        Ended,              // the record was read, but its match cannot go on (or its replay did not agree): the room is there as a failed room with the reason (status, log, result file)
        Unreadable          // the file is no record that can be read (corrupt, another format, too big, a code that is taken): nothing of it is left but a line in the log
    };
    std::string file;       // the record's file name (never a path)
    std::string code;       // the room's code ("" when the file could not be read)
    Outcome outcome{Outcome::Unreadable};
    std::string note;       // why it was not restored (Restored: what came back)
    uint32_t turns{0};      // the turns that the record held
    uint32_t replay_ms{0};  // Restored: the room's own replay work, in real time (the sum of its slices)
};
struct RestoreReport {
    std::vector<RestoreItem> items;
    size_t queued{0};       // records that were judged good and wait for their replay, which update() does in slices (restore_rooms: how many there were; their items come as the replays end)
    bool stopped{false};    // the server was told to stop while the records were being judged: the restore ended at once; the records that it had not judged, and the queued ones, are left on disk as they were
    size_t left_on_disk{0}; // ... how many records that was
    size_t count(RestoreItem::Outcome outcome) const noexcept {
        size_t n = 0;
        for (const RestoreItem& i : items) n += i.outcome == outcome ? 1u : 0u;
        return n;
    }
};

class RoomManager {
public:
    RoomManager(MapStore store, ServerLimits limits = ServerLimits());
    ~RoomManager();

    /// Starts to keep restart records (restart_record.hpp): every room that holds seats keeps one from the start of its match to its end, and restore_rooms() can bring the rooms of the records of the
    /// folder back. Call it before any room is made. False, with the reason, when the folder cannot be used (the server then keeps none). An empty `config.dir` keeps none and is true.
    bool enable_restart_records(RestartConfig config, std::string& why);
    /// The server's restart records (null when it keeps none)
    const RestartStore* restart_store() const noexcept { return restart_.get(); }
    /// Reads the records of the folder, newest first (by the time of their last write), and judges each one: a record that cannot be read is a line in the log and is deleted; one that cannot be restored
    /// (another network protocol, a map that is gone or has changed, too old, no room for it, ...) becomes a FAILED room with the reason and is moved to `refused`; every other record is QUEUED (its code is
    /// taken: create_room answers 409). update() replays the queue in slices of RestartConfig::restore_slice_ms a pass (a room for which a Hello waits first, then the newest record), so the server serves
    /// while it restores, and a room begins when ITS replay ends, with a clock that starts then (Room::begin_restored). `should_stop` is asked between the records that are judged: when it says yes the
    /// restore ends and every record that is left stays on disk as it was (RestoreReport::stopped). Call it once, after enable_restart_records() and before the first update(). The report has what was
    /// decided at once; restore_report() has every record's fate as it is decided.
    RestoreReport restore_rooms(uint32_t now_ms, const std::function<bool()>& should_stop = nullptr);
    /// Every record's fate so far: refused or unreadable at once, restored when its replay ended, refused in its replay (the queued records are not in it yet)
    const RestoreReport& restore_report() const noexcept { return report_; }
    /// Records that wait for their replay or are being replayed (their rooms are no rooms yet), and Hellos that wait for such a room
    size_t restoring_count() const noexcept { return restoring_.size(); }
    size_t parked_count() const noexcept { return parked_.size(); }
    /// The server is told to stop: every room that keeps a record makes it durable (fsync) and leaves it on disk, the other rooms are closed. Returns the number of records that were kept. Nothing is
    /// deleted: a record is deleted when its room is over, never because the server stops.
    size_t shutdown(uint32_t now_ms);
    /// Lines for the server's log about the restart records (a record that was written no more, a room that was restored or not): once each, never a key
    std::vector<std::string> take_notices();

    const MapStore& store() const noexcept { return store_; }
    const ServerLimits& limits() const noexcept { return limits_; }
    /// A room's specification with the server's defaults in it (reconnect, the vote, the cap, the limit of the log): what the control interface starts from
    RoomSpec default_spec() const;
    /// What the logs of all the rooms hold now and what they may hold together (bytes)
    uint64_t log_bytes() const noexcept { return log_budget_.used(); }
    uint64_t log_budget_bytes() const noexcept { return log_budget_.limit(); }

    /// Makes a room: the map must exist in the store and load, the code must be new and valid (an empty code gets a generated one), 2 <= players <= 4, and the
    /// server must have room for one more.
    CreateResult create_room(RoomSpec spec, uint32_t now_ms);
    /// A new connection (its peer address is only used for the lobby's records): it has to say Hello soon
    void add_connection(std::unique_ptr<net::Connection> connection, const std::string& address, uint32_t now_ms);
    void update(uint32_t now_ms);

    bool status(const std::string& code, RoomStatus& out, uint32_t now_ms) const;
    std::vector<RoomStatus> list(uint32_t now_ms) const;
    /// The owner closes a room (its clients are dropped); false when there is no such room
    bool close_room(const std::string& code, uint32_t now_ms);

    /// The rooms that ended (finished or failed) since the last call, once each: what the server logs and writes to its result files
    std::vector<RoomStatus> take_ended(uint32_t now_ms);

    /// How busy the server is (see BusyCounts); cheap: a pass over the rooms
    BusyCounts busy(uint32_t now_ms) const;

    size_t room_count() const noexcept { return rooms_.size(); }
    size_t pending_count() const noexcept { return pending_.size(); }
    uint64_t rooms_created() const noexcept { return created_; }
    uint64_t connections_refused() const noexcept { return refused_; }

private:
    struct Pending {
        std::unique_ptr<net::Connection> connection;
        std::string address;
        uint32_t since_ms{0};
    };
    struct Lingering {
        std::unique_ptr<net::Connection> connection;
        uint32_t since_ms{0};
    };

    // Restart records. A record that was judged good waits in `restoring_` (newest first) until update() has replayed it: the room is made when its replay starts and goes to `rooms_` when it ends.
    struct Restoring {
        std::string code;
        std::string path;
        RestartHead head;                        // as it was judged
        RestoreItem item;                        // file, code, turns
        std::unique_ptr<RestartLoaded> rec;      // the record, read again when its replay starts (the bytes of the records that wait are not kept); declared before `room`: the room's reader points into it
        std::unique_ptr<Room> room;              // the half-built room, from the first slice on
    };
    /// A Hello for a room that waits for its replay: the connection (a wrapper that keeps what the peer sends after its Hello) and the decoded Hello wait here until the room is restored
    struct Parked {
        std::unique_ptr<net::Connection> connection;
        ParkedLink* link{nullptr};
        std::string address;
        std::vector<uint8_t> message;            // the Hello as it came (the lobby of a room reads it again)
        net::HelloMsg hello;
        std::string code;
        uint32_t since_ms{0};
    };
    enum class JobEnd : uint8_t { More, Restored, Ended, Unreadable };
    void judge_record(const std::string& path, uint32_t now_ms);
    /// "" when the map of the head is on this server, is the file that the match was played on and can be played by its seats (entry and level are made); else the refusal
    std::string map_refusal(const RestartHead& head, MapEntry& entry, assets::LevelData& level) const;
    void refuse_record(const std::string& path, const RestartHead& head, RestoreItem item, const std::string& refusal, bool has_place, uint32_t now_ms);
    void restore_step(uint32_t now_ms);
    size_t next_to_restore() const;
    JobEnd advance(Restoring& job, uint32_t slice_ms, const std::function<uint32_t()>& clock, uint32_t now_ms);
    JobEnd end_job(Restoring& job, const std::string& refusal, uint32_t now_ms);
    bool restoring_has(const std::string& code) const;
    /// A Hello (a decoded one, from a connection that no room has yet) finds its room: given to it, parked for a room that waits for its replay, or rejected
    void route_hello(std::unique_ptr<net::Connection> connection, const std::string& address, const std::vector<uint8_t>& message, const net::HelloMsg& hello, uint32_t now_ms);
    void park(std::unique_ptr<net::Connection> connection, const std::string& address, const std::vector<uint8_t>& message, const net::HelloMsg& hello, const Restoring& job, uint32_t now_ms);
    void release_parked(const std::string& code, uint32_t now_ms);
    void reject(std::unique_ptr<net::Connection> connection, net::RejectReason reason, uint32_t now_ms);
    std::string new_code();
    /// Makes the demo room that a Hello names, when demo rooms are on, the code has the prefix, and there is a free place (or one can be had: evict_abandoned_demo); false otherwise
    bool make_demo_room(const std::string& code, uint32_t now_ms);
    /// Ends the demo room that has been abandoned the longest (at least kDemoAbandonedMs: every seat of a person held absent, nobody back): its clients are dropped, its record is deleted, its log is
    /// freed, its end is reported, and it is forgotten at once so that its place can be taken. False when no demo room qualifies (a room with a person present never does).
    bool evict_abandoned_demo(uint32_t now_ms);

    MapStore store_;
    ServerLimits limits_;
    net::LogBudget log_budget_;                  // (declared before the rooms: they give their logs back when they are destroyed)
    std::unique_ptr<RestartStore> restart_;      // (also before the rooms: their records point at it)
    std::map<std::string, std::unique_ptr<Room>> rooms_;
    bool stale_armed_{false};                    // files that could not be deleted wait for a retry (RestartStore::retry_stale): when the next one is due
    uint32_t next_stale_retry_ms_{0};
    std::vector<Restoring> restoring_;           // the records that wait for their replay, newest first (after the rooms: destroyed before them, with the log budget and the store still there)
    RestoreReport report_;                       // what became of the records
    std::vector<Parked> parked_;
    std::vector<Pending> pending_;
    std::vector<Lingering> lingering_;
    std::vector<RoomStatus> unreported_;     // the ends of rooms that were forgotten in the pass in which they ended
    uint64_t created_{0};
    uint64_t refused_{0};
    uint64_t code_counter_{0};
};

}  // namespace ants::server
