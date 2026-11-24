#pragma once

// What a replay pass counts about a match besides the totals that the engine keeps (docs/REPLAYS.md "The match pages"): the score and the kills over time, who killed whom and when, the damage, the raids
// on the hills, the power-ups taken, the food carried, the bombs that went off, when a seat ran out of ants, the orders of every seat by minute and by kind, and how many events of each of five kinds (a death, a bomb, a
// theft, a fire, a hit) happened in every 5 seconds (the field `action` that the activity curve of the replay page is drawn from, docs/HEATMAPS.md "The activity curve").
//
// It only WATCHES: the collector is given to replay::play() as its hooks and reads what the engine already says, through the public readers (the counters, the world after every tick, the record of an ant
// that is gone: the engine keeps it, with the team of the last damage, until the match is over) and the queues of cues. It changes nothing, so a played match ends in the same hashes with or without it
// (tests/test_server/test_history_details.cpp checks it). No clock, no disk, no database, no thread: pure, like make_history_record(). The server turns what it found into the `details` of a match's record
// (ants_server/history_details.hpp), in the same pass that counts the totals.
//
// What this cannot tell (it needs an event sink in the engine, a later change): what caused each hit (a blow, a bomb, a fire wall) and the type of the ant that struck. The tile of every lost ant is read from the
// same snapshot of the ants (the tile of its last look). The ants are read with SimulationEngine::get_ant_snapshots: the ants alone, so a catch-up that counts hundreds of turns a frame does not copy the map for each of them.
//
// The same pass is also run LIVE, while a match is played (ants_app/live_match_stats.hpp): the application hands every tick that its confirmed engine executes to on_tick(), and the numbers that the results screen
// shows (ants_app/match_report.hpp) are made from what it counted. Counted live or from the recording, the numbers are the same (tests/test_app/test_match_report.cpp checks it): so the pass reads the world and the
// counters only, never the engine's queues of cues and news (a game's own code drains them: a pass that took them would take the sounds of the match), and a tick that it was not told of makes it fail. (A pass over a recording, StatsPlayback, has replay::Playback clear both queues after every tick, so nothing piles up there.)

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

    /// The kinds of event that the activity curve of the replay page counts (docs/HEATMAPS.md "The activity curve"; the counts are `action`): the Auto camera's big events that are a fight and not food or a bridge. The order is the one of the
    /// keys of the stored field (ants_server/history_action.hpp).
    enum class Action : uint8_t { Death, Bomb, Theft, Fire, Hit, Count };
    static constexpr size_t kActionKinds = static_cast<size_t>(Action::Count);
    static const char* action_name(Action kind) noexcept;        // "death", "bomb", "theft", "fire", "hit"

    /// The totals of every seat every kSampleTurns turns (the first after turn 100)
    struct Sample {
        uint32_t turn{0};
        std::array<int32_t, kSeats> box{};                 // the score box (a seat's score and its ally's)
        std::array<int32_t, kSeats> score{};               // the seat's own score, as the scoreboard counts it
        std::array<uint32_t, kSeats> killed{};
    };
    /// An ant that died. `killer` is the team of the last damage it took (kNoKiller: none, or its own: a team does not kill itself), `victim_type` the ant's own type (AntType as a number: a Worker until it takes a
    /// power-up), `drowned` that it fell in the water, `tile_x` / `tile_y` the tile the ant stood on when the world last showed it (where it died: the map of deaths, ants_server/history_deaths.hpp).
    struct Kill {
        uint32_t turn{0};
        uint8_t killer{kNoKiller};
        uint8_t victim{0};
        uint8_t victim_type{0};
        bool drowned{false};
        int32_t tile_x{0};
        int32_t tile_y{0};
    };
    /// A Thief started to raid a hill; `points` is the loot that the raid took (the hill lost it, the thief carried it off: 0 for a hill with nothing); `tile_x` / `tile_y` the tile the thief stood on when the raid began
    struct Raid {
        uint32_t turn{0};
        uint8_t thief{0};
        uint8_t victim{0};
        int32_t points{0};
        int32_t tile_x{0};
        int32_t tile_y{0};
    };
    /// A bomb that went off: the seat that planted it, the turn of the tick in which it was gone, the tile it lay on
    struct BombOff {
        uint32_t turn{0};
        uint8_t owner{0};
        int32_t tile_x{0};
        int32_t tile_y{0};
    };
    /// A fire that a seat lit (the engine's own count of fires lit says when; the fire wall that appeared says where: `has_tile` is false for a fire that the engine counted and that left no wall, which it does not place
    /// on the tiles at the entrance of a hill)
    struct FireLit {
        uint32_t turn{0};
        uint8_t seat{0};
        bool has_tile{false};
        int32_t tile_x{0};
        int32_t tile_y{0};
    };

    bool ok{false};                                        // false: the pass could not count this match (`why`); nothing below is to be believed
    std::string why;
    uint32_t turns{0};
    uint8_t roster{0};                                     // bit s: seat s plays
    uint32_t map_width{0};                                 // the grid's size in tiles (0 until the first tick was looked at): the bounds of the tiles of `kills`
    uint32_t map_height{0};
    std::vector<Sample> samples;
    std::vector<Kill> kills;                               // in the order they happened
    std::vector<Raid> raids;
    std::array<uint32_t, kSeats> damage_dealt{};           // hit points that its ants took from the other seats' ants (an ant counts for the team of the last damage it took)
    std::array<uint32_t, kSeats> damage_taken{};
    std::array<std::array<uint32_t, kSeats>, kSeats> damage_from{};   // [the team of the damage][the seat that lost the points]; a seat can hurt itself (its own fire wall)
    /// action[kind][bucket]: events of that kind in the 5 seconds (kSampleTurns turns) of the bucket, bucket b being the turns 100 b to 100 b + 99 (the same second as the kill's: turn / 20). A death is an entry of
    /// `kills`, a theft one of `raids`, a bomb one of `bombs_off` and a fire one of the seats' `fires_lit`; a hit is an ant that lost hit points, counted once for each ant in each bucket (so a long fight is as big as the ants in it, not as long as it lasts).
    std::array<std::vector<uint32_t>, kActionKinds> action;
    std::array<std::array<uint32_t, 5>, kSeats> powerups{};            // [seat][bomber, fire, thief, combat, swimmer]: power-ups taken that changed the ant's type
    // What the results screen needs besides the totals (ants_app/match_report.hpp); all of it only added, nothing above changed
    std::vector<BombOff> bomb_events;                      // the bombs that went off, in the order they did (their count is `bombs_off`)
    std::vector<FireLit> fire_events;                      // the fires that were lit, in the order they were (their count is `fires_lit`)
    std::array<uint32_t, kSeats> bombs_planted{};          // the engine's own count of bombs a seat planted
    std::array<uint32_t, kSeats> fires_lit{};              // ... and of fires it lit
    /// hit_credit[bucket][seat]: the hits of `action` (an ant that lost hit points, once for an ant in a bucket) that were taken by the ants of OTHER seats from damage of this seat's team, the team of the last damage that
    /// the ant had taken (a hit that no team did, or that a seat did to its own ants, is nobody's)
    std::vector<std::array<uint32_t, kSeats>> hit_credit;
    /// false: the place or the owner of a bomb or a fire could not be told for sure (a bomb was defused in the same tick as another went off, and nothing says which was which; a fire wall that no count of fires lit accounts
    /// for). The totals are still right. `ok` does not depend on it: the match pages never read the lists above; the results screen does, and shows no honors then.
    bool events_ok{true};
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
    /// A collector for a match that is counted while it is played (the application's live pass, ants_app/live_match_stats.hpp): the seats that play (bit s: seat s) and a guess of how many turns it will have (0: none)
    explicit MatchStatsCollector(uint8_t roster, uint32_t expected_turns = 0);
    MatchStatsCollector(const MatchStatsCollector&) = delete;
    MatchStatsCollector& operator=(const MatchStatsCollector&) = delete;
    /// The hooks to play with; they point at this collector, which must outlive the play
    Hooks hooks();
    /// What the hooks call. A command: before the engine is given it (the pass counts the orders of every seat by kind; it needs the engine as it stands before the command, which a live game cannot always give: the live pass counts none).
    void on_command(uint32_t turn, const sim::Command& command, sim::SimulationEngine& engine);
    /// A tick has run: `turns` ticks have been played so far (1 after the first). Reads the engine; changes nothing and takes nothing out of its queues.
    void on_tick(uint32_t turns, sim::SimulationEngine& engine);
    /// The pass cannot be believed (a tick was not seen): `ok` of the result is false and `why` says it. The first reason stays.
    void fail(const char* what);
    bool failed() const noexcept { return failed_; }
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
        uint32_t hit_bucket{0xFFFFFFFFu};                  // the bucket of `action` in which it was last counted as hurt (a hit is counted once for an ant in a bucket)
    };
    void count(MatchStats::Action kind, uint32_t turns, uint32_t events = 1);
    void credit_hit(uint32_t bucket, uint8_t seat);
    void track_bombs(sim::SimulationEngine& engine, uint32_t turns, uint32_t planted, uint32_t defused, const std::vector<sim::AntSnapshot>& ants);
    void track_fires(sim::SimulationEngine& engine, uint32_t turns, const std::array<uint32_t, MatchStats::kSeats>& lit_now);

    MatchStats stats_;
    uint8_t roster_{0};
    bool failed_{false};
    std::vector<Ant> ants_;                                // by ant id (the engine numbers ants 1, 2, 3 ...)
    std::array<int32_t, MatchStats::kSeats> prev_score_{};
    std::array<uint32_t, MatchStats::kSeats> prev_hatched_{};
    std::array<uint32_t, MatchStats::kSeats> prev_fires_{};
    std::array<bool, MatchStats::kSeats> was_wiped_{};
    uint32_t bombs_planted_{0};
    uint32_t bombs_defused_{0};
    struct Spot {                                          // a bomb or a fire wall on the map: its tile and the seat that owns it
        int32_t x{0};
        int32_t y{0};
        uint8_t owner{255};
    };
    static std::vector<Spot> scan(sim::SimulationEngine& engine, bool bombs);
    std::vector<Spot> bombs_;                              // the bombs on the map as last looked at
    std::vector<Spot> fires_;                              // the fire walls on the map as last looked at (the owner of a wall is the seat that lit it; the map's own walls are known from the first tick)
};

struct StatsPlay {
    Outcome outcome;
    MatchStats stats;
};

/// replay::play() with a collector: the outcome is the one that plain play() gives (same hashes), the stats what the pass counted (not `ok` when the match did not play out, or the pass failed)
StatsPlay play_with_stats(const Replay& replay, const assets::LevelData& level);

/// play_with_stats() in pieces (replay::Playback with the collector as its hooks): advance() plays a few turns, take() gives what play_with_stats() gives once the play is over. The same rules as Playback's
/// (`replay` and `level` outlive it, one thread).
class StatsPlayback {
public:
    StatsPlayback(const Replay& replay, const assets::LevelData& level);
    StatsPlayback(const StatsPlayback&) = delete;
    StatsPlayback& operator=(const StatsPlayback&) = delete;
    /// Plays at most `turns` more turns; true when the play is over (done())
    bool advance(uint32_t turns) { return playback_.advance(turns); }
    bool done() const noexcept { return playback_.done(); }
    uint32_t turns_played() const noexcept { return playback_.turns_played(); }
    bool next_takes_hash() const noexcept { return playback_.next_takes_hash(); }       // (see Playback)
    /// The result of a play that is over; moves the stats out, so once
    StatsPlay take();

private:
    MatchStatsCollector collector_;          // (before the playback: its hooks point at the collector)
    Playback playback_;
};

}  // namespace ants::replay
