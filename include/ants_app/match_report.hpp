#pragma once

// What the end-of-match screen says besides the original's four numbers (docs/VIEW_AND_HUD.md, "The new look of the results screen"): the damage that every side did and took, the food it carried, its bombs, fires and
// raids, the honors (MVP, First blood, Biggest raid, or Last stand in 187) and the Play of the game. All of it is COUNTED by the replay's stats pass (ants_replay/match_stats.hpp, the code that the match pages of the
// replays use) while the match is played, on the CONFIRMED engine, for every tick that it executes; nothing here reaches the simulation, the lock-step rules or any hash.
//
// `ok` is the whole truth of the report: false (a tick was not seen, the pass failed, the match was not played from its first turn) and the screen shows the original's numbers only, never a wrong one.

#include <array>
#include <cstdint>
#include <string>

#include "ants_replay/match_stats.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/match_stats.hpp"

namespace ants::app {

struct MatchReport {
    static constexpr size_t kSeats = sim::MAX_PLAYERS;
    static constexpr uint32_t kTurnsPerSecond = 20;
    static constexpr uint32_t kWatchLeadTurns = 5 * kTurnsPerSecond;       // Watch opens the replay this long before the moment

    bool ok{false};
    uint32_t turns{0};                                                     // how long the match ran, in turns (20 a second)
    uint8_t roster{0};                                                     // bit s: seat s plays
    uint8_t winners{0};                                                    // bit s: seat s is on the winning side, the seats of the first row of result.rows(local_seat) (an alliance is two seats)
    std::array<uint32_t, kSeats> damage_done{};                            // hit points that the seat's ants took from the other seats' ants
    std::array<uint32_t, kSeats> damage_taken{};
    std::array<uint32_t, kSeats> food_trips{};                             // times an ant of the seat picked food up
    std::array<int64_t, kSeats> food_points{};                             // the points it held then
    std::array<uint32_t, kSeats> bombs_set{};
    std::array<uint32_t, kSeats> fires_lit{};
    std::array<uint32_t, kSeats> raids{};                                  // hills raided by the seat's thieves
    std::array<int64_t, kSeats> loot{};                                    // the points those raids took

    /// An honor: the seat that earned it, the number that is shown under it, and the turn it happened (0 for a total of the match)
    struct Honor {
        bool present{false};
        uint8_t seat{0};
        int64_t value{0};
        uint32_t turn{0};
    };
    Honor mvp;                                                             // of the winning side: the highest mixed share (mvp_share), `value` the damage it did; none when no seat of the side did any of the four things
    Honor first_blood;                                                     // the first kill by a seat that is not its own victim's, none that drowned: the killer, `turn` when
    Honor biggest_raid;                                                    // the raid with the most loot (a raid that took nothing is none, the earlier of two equal ones wins); `value` the points. Never in 187
    Honor last_stand;                                                      // 187 only: the seat that was out of ants last of those that did not win; `turn` when it ran out. Never in the original's game

    /// The Play of the game: the busiest 10 seconds of the match by the activity curve's weights (death 14, bomb 10, theft 6, fire 5, hit 2), credited to the seat with the largest share of them
    enum class PlayKind : uint8_t { Bomb, Raid, Fight, Fire };
    struct Play {
        bool present{false};                                               // false: no 10 seconds had two deaths' worth of action
        uint8_t seat{0};
        PlayKind kind{PlayKind::Fight};
        uint32_t count{0};                                                 // ants taken out (Bomb, Fight, Fire) or points stolen (Raid)
        uint32_t turn{0};                                                  // the moment (the busiest event of the window)
        int32_t tile_x{0};                                                 // where it was
        int32_t tile_y{0};
        uint32_t watch_turn() const noexcept { return turn > kWatchLeadTurns ? turn - kWatchLeadTurns : 0; }
    };
    Play play;
};

/// The mixed share that decides the MVP: of the four things that a side does (damage done, kills, loot, food points carried) the share of the seat in each total of the match, the shares of the things that anyone
/// did averaged with equal weight (a thing that nobody did is left out), in thousandths. Public so that a test can say it.
uint32_t mvp_share(const replay::MatchStats& stats, uint8_t seat);

/// The report of a match from what the stats pass counted. `result` says who won (its rows and alliances: the seats of the first row of result.rows(local_seat)); `mode` is the rules (187 has Last stand instead of Biggest raid).
/// ok is false (and nothing but `turns` is filled) when `stats.ok` is not, or `stats.events_ok` is not: the pass could not count the match, or could not tell the owner of a bomb or the place of a fire for sure.
/// The definitions of every number and honor are in the .cpp (and in docs/REPLAYS.md, "The match pages"). A pure function of its arguments: no clock, no disk, no floating point.
MatchReport make_match_report(const replay::MatchStats& stats, const sim::MatchResult& result, sim::GameMode mode, uint8_t local_seat);

}  // namespace ants::app
