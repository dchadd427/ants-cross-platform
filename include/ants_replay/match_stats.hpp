#pragma once

// What a replay pass counts about a match besides the totals that the engine keeps (docs/REPLAYS.md "The match pages"): the score and the kills over time, who killed whom and when, the damage, the raids
// on the hills, the power-ups taken, the food carried, the bombs that went off, when a seat ran out of ants, and the orders of every seat by minute and by kind.
//
// It only WATCHES: the collector is given to replay::play() as its hooks and reads what the engine already says, through the public readers (the counters, the world after every tick, the record of an ant
// that is gone: the engine keeps it, with the team of the last damage, until the match is over) and the queues of cues. It changes nothing, so a played match ends in the same hashes with or without it
// (tests/test_server/test_history_details.cpp checks it). No clock, no disk, no database, no thread: pure, like make_history_record(). The server turns what it found into the `details` of a match's record
// (ants_server/history_details.hpp), in the same pass that counts the totals.
//
// What this cannot tell (it needs an event sink in the engine, a later change): what caused each hit (a blow, a bomb, a fire wall) and the type of the ant that struck.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"

namespace ants::replay {

struct MatchStats {
    static constexpr size_t kSeats = sim::MAX_PLAYERS;
    static constexpr uint32_t kSampleTurns = 100;          // the totals are read every 100 turns (5 seconds)
    static constexpr uint32_t kMinuteTurns = 1200;         // a minute of play
    static constexpr uint8_t kNoKiller = 255;

    /// The kinds of order that a seat gives, as the page counts them. A special order is what the ant's type does with it: the first ant of the group that has an ability type decides (bomb, fire, bridge, raid);
    /// for a group of workers and combat ants it is a plain "special". Quit and Drop are no orders.
    enum class OrderKind : uint8_t { Move, Attack, Bomb, Fire, Bridge, Raid, Special, Stop, Hatch, Team, Count };
    static constexpr size_t kOrderKinds = static_cast<size_t>(OrderKind::Count);
    static const char* order_kind_name(OrderKind kind) noexcept;     // "move", "attack", "bomb", "fire", "bridge", "raid", "special", "stop", "hatch", "team"

    /// The totals of every seat every kSampleTurns turns (the first after turn 100)
    struct Sample {
        uint32_t turn{0};
        std::array<int32_t, kSeats> box{};                 // the score box (a seat's score and its ally's)
        std::array<int32_t, kSeats> score{};               // the seat's own score, as the scoreboard counts it
        std::array<uint32_t, kSeats> killed{};
    };
    /// An ant that died. `killer` is the team of the last damage it took (kNoKiller: none, or its own: a team does not kill itself), `victim_type` the ant's own type (AntType as a number: a Worker until it takes a
    /// power-up), `drowned` that it fell in the water.
    struct Kill {
        uint32_t turn{0};
        uint8_t killer{kNoKiller};
        uint8_t victim{0};
        uint8_t victim_type{0};
        bool drowned{false};
    };
    /// A Thief started to raid a hill; `points` is the loot that the raid took (the hill lost it, the thief carried it off: 0 for a hill with nothing)
    struct Raid {
        uint32_t turn{0};
        uint8_t thief{0};
        uint8_t victim{0};
        int32_t points{0};
    };

    bool ok{false};                                        // false: the pass could not count this match (`why`); nothing below is to be believed
    std::string why;
    uint32_t turns{0};
    uint8_t roster{0};                                     // bit s: seat s plays
    std::vector<Sample> samples;
    std::vector<Kill> kills;                               // in the order they happened
    std::vector<Raid> raids;
    std::array<uint32_t, kSeats> damage_dealt{};           // hit points that its ants took from the other seats' ants (an ant counts for the team of the last damage it took)
    std::array<uint32_t, kSeats> damage_taken{};
    std::array<std::array<uint32_t, kSeats>, kSeats> damage_from{};   // [the team of the damage][the seat that lost the points]; a seat can hurt itself (its own fire wall)
    std::array<std::array<uint32_t, 5>, kSeats> powerups{};            // [seat][bomber, fire, thief, combat, swimmer]: power-ups taken that changed the ant's type
    std::array<uint32_t, kSeats> food_trips{};             // times an ant picked food up
    std::array<int64_t, kSeats> food_points{};             // the points it held then
    std::array<int64_t, kSeats> banked{};                  // what the hill gained: the score's rises, a thief's loot included, the cost of a hatch not taken off
    std::array<int64_t, kSeats> hatch_cost{};              // points paid for hatching
    std::array<uint32_t, kSeats> bombs_off{};              // bombs of the seat that an ant walked onto
    std::array<int32_t, kSeats> wipe_turn{-1, -1, -1, -1}; // the first turn at which the seat had no ant (an egg hatches one for free), -1 never
    std::array<uint32_t, kSeats> wipes{};                  // how many times it ran out of ants
    std::array<int32_t, kSeats> out_turn{-1, -1, -1, -1};  // the first turn at which it had no ant, no egg and no hatch running: out of the match, -1 never
    /// orders[kind][minute][seat]: orders given in that minute of play (the last minute may be short)
    std::array<std::vector<std::array<uint32_t, kSeats>>, kOrderKinds> orders;
    std::array<std::array<uint32_t, 6>, kSeats> orders_naming{};       // [seat][AntType]: orders that name at least one ant of the type
};

/// Counts a match while play() plays it: hand hooks() to play(), then take() the result. One collector for one play. A hook never throws (an exception inside the pass makes `ok` false and the pass stops
/// counting; the match plays on, and its totals do not depend on the pass).
class MatchStatsCollector {
public:
    explicit MatchStatsCollector(const Replay& replay);
    MatchStatsCollector(const MatchStatsCollector&) = delete;
    MatchStatsCollector& operator=(const MatchStatsCollector&) = delete;
    /// The hooks to play with; they point at this collector, which must outlive the play
    Hooks hooks();
    /// The result, once play() has returned (`turns` is what play() played). Moves the data out.
    MatchStats take(uint32_t turns_played);

private:
    struct Ant {
        bool alive{false};
        uint8_t player{255};
        uint8_t own_type{0};
        uint16_t hp{0};
        bool holding{false};
        uint8_t target_team{255};
        int32_t tile_x{0};
        int32_t tile_y{0};
        uint32_t last_seen{0};                             // the turn at which the world last showed it
    };
    void on_command(uint32_t turn, const sim::Command& command, sim::SimulationEngine& engine);
    void on_tick(uint32_t turns, sim::SimulationEngine& engine);
    void fail(const char* what);

    MatchStats stats_;
    uint8_t roster_{0};
    bool failed_{false};
    std::vector<Ant> ants_;                                // by ant id (the engine numbers ants 1, 2, 3 ...)
    std::array<int32_t, MatchStats::kSeats> prev_score_{};
    std::array<uint32_t, MatchStats::kSeats> prev_hatched_{};
    std::array<bool, MatchStats::kSeats> was_wiped_{};
    uint32_t bombs_planted_{0};
    uint32_t bombs_defused_{0};
    struct Bomb {
        int32_t x{0};
        int32_t y{0};
        uint8_t owner{255};
    };
    std::vector<Bomb> bombs_;                              // the bombs on the map as last looked at
};

struct StatsPlay {
    Outcome outcome;
    MatchStats stats;
};

/// replay::play() with a collector: the outcome is the one that plain play() gives (same hashes), the stats what the pass counted (not `ok` when the match did not play out, or the pass failed)
StatsPlay play_with_stats(const Replay& replay, const assets::LevelData& level);

}  // namespace ants::replay
