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
//   Running  the referee executes the turns; a diverging client is named and the room fails; the match end (the clock, the rules) finishes it
//   Finished the result (rows as the results screen shows them) is kept; the connections close after a short grace period
//   Failed   nobody came, too many failed starts, a desync, the owner closed it: the reason is kept

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ants_assets/lvl_parser.hpp"
#include "ants_net/lobby.hpp"
#include "ants_net/session.hpp"
#include "ants_net/transport.hpp"
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
    uint32_t run_ms{2u * 3600u * 1000u};    // a match that is still running this long after it began is ended (failed, "took too long"): the game clock only advances while
                                            // every player executes turns, so without a wall-clock limit a seat that stalls could hold the room for ever
};

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
    uint32_t turns{0};
    uint32_t age_ms{0};
    uint16_t quitter{0xFFFF};
    std::vector<RoomRow> rows;              // the result of a finished match, best first
};

class Room {
public:
    static constexpr size_t kMaxConnections = 32;           // everything that ever said Hello to this room (rejected ones included): a flood is refused beyond this
    static constexpr uint32_t kRetryMs = 2000;              // the pause after a cancelled start
    static constexpr uint32_t kGraceMs = 15000;             // after the end of the match the connections stay open this long, then they are closed

    Room(RoomSpec spec, MapEntry map, assets::LevelData level, uint32_t seed, uint32_t now_ms);

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
    std::vector<RoomRow> rows_;
    uint16_t quitter_{0xFFFF};
    std::array<std::string, 4> names_{};
};

}  // namespace ants::server
