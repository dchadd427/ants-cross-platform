#pragma once

// The "people" numbers of bot_watch (docs/BOT_PEOPLE.md): per seat, how the seat answers a blow and helps its own ants, whether its hurt ants could have got home, its heals, its Fire Ants and
// their walls at enemy gates, the mines it lays round its own hill, and how much it keeps on one victim. They are the numbers by which a computer player is judged against the people it plays.
//
// The core (People) takes plain per-tick SNAPSHOTS (a list of ants, bombs and fire walls, who is allied with whom), so that the tests can feed it hand-made streams with no engine. PeopleObserver
// reads an engine after each tick (ArenaSpec::on_tick, the replay player's after_tick hook), builds the snapshot and hands it over; it changes nothing, so a match under watch has the same
// state hash as the same match without it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ants_sim/ant_unit.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::watch {

inline constexpr size_t kPeopleSeats = sim::MAX_PLAYERS;

struct AntSnap {
    uint32_t id{0};
    uint8_t seat{0};
    sim::AntType type{sim::AntType::Worker};
    sim::TileCoord tile{};
    uint16_t hp{10};
    sim::UnitState state{sim::UnitState::Idle};
    bool holding{false};
};

/// A bomb or a fire wall on a tile and the seat whose it is (255: nobody's, a map's own)
struct PlaceSnap {
    sim::TileCoord tile{};
    uint8_t owner{255};
};

/// The world after one tick, as much as the numbers need. `ally[s]` is the seat allied with seat s (sim::ALLIANCE_NONE: none): allied seats are one side in a fight.
struct Snapshot {
    uint64_t tick{0};
    std::array<uint8_t, kPeopleSeats> ally{sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
    std::vector<AntSnap> ants;
    std::vector<PlaceSnap> bombs;
    std::vector<PlaceSnap> fires;
};

/// What stays the same in a match: the seats that play, the entrance of each seat's hill ((-1, -1): none), and whether the hills are closed (the game type 187: no ant can walk home to heal)
struct Setup {
    std::array<bool, kPeopleSeats> present{};
    std::array<sim::TileCoord, kPeopleSeats> entrance{sim::TileCoord{-1, -1}, sim::TileCoord{-1, -1}, sim::TileCoord{-1, -1}, sim::TileCoord{-1, -1}};
    bool hill_closed{false};
};

/// One blow: an ant lost hit points with an enemy ant in its strike clip within two tiles; that ant struck it
struct Blow {
    uint64_t tick{0};
    uint32_t victim{0};
    uint8_t victim_seat{0};
    uint32_t striker{0};
    uint8_t striker_seat{0};
    sim::TileCoord at{};                 // where the victim stood
    bool killed{false};                  // the blow took its last hit point
};

/// The numbers of one seat (the sums of several matches when merged: the means and rates are made when the text is)
struct SeatPeople {
    uint32_t matches{0};
    // fights in which the seat was struck first
    uint32_t struck_first{0};
    uint32_t hit_back{0};                // ... the seat's side struck a blow in the fight
    uint32_t helped{0};                  // ... an ant other than the first victim struck
    std::vector<uint32_t> help_ticks;    // ... for each of those, the ticks from the first blow to the first helper's blow
    uint32_t strikers_own{0};            // ... the sums over those fights of the ants that struck: of the seat's side, and of the other side
    uint32_t strikers_enemy{0};
    uint32_t local_own{0};               // ... the sums of the most ants of the seat's side, and of the other side, within 4 tiles of the last blow at one sample
    uint32_t local_enemy{0};
    uint32_t lost_in_fights{0};          // ... the ants of the seat's side that a blow of the fight killed, and the enemy ants that a blow of the seat's side killed
    uint32_t killed_in_fights{0};
    // deaths
    uint32_t deaths{0};
    uint32_t could_walk_home{0};         // an ant that died could have walked home from where it first had 4 hit points or less
    uint32_t died_far{0};                // ... could not have
    uint32_t died_without_warning{0};    // ... never had 4 or less
    bool walk_applicable{true};          // false: the hill is closed (187), the first two of those are not counted
    uint32_t heals{0};
    uint32_t fire_ants{0};
    uint32_t fire_ants_died{0};
    uint32_t walls_at_enemy_gates{0};
    std::array<uint32_t, 3> home_mines{};   // the bombs laid within 10 tiles of the own hill's entrance by tick 3,000, 6,000 and 12,000
    uint32_t away_mines{0};
    uint32_t mine_hits_enemy{0};
    uint32_t blows_struck{0};
    uint32_t focus_pairs{0};             // pairs of the seat's consecutive blows less than 200 ticks apart ...
    uint32_t focus_same{0};              // ... on the same victim

    void merge(const SeatPeople& other);
    /// {"struck_first":..,...}: the fields of docs/BOT_PEOPLE.md, a number that has no sample (no fight, no pair) is null
    std::string json() const;
    /// One line of text for the console
    std::string line() const;
};

/// The core: feed it the snapshots of a match in order of their ticks (at least the ticks that matter: a hit point that falls between two snapshots is seen at the second)
class People {
public:
    explicit People(const Setup& setup);
    ~People();
    People(const People&) = delete;
    People& operator=(const People&) = delete;

    void tick(const Snapshot& snapshot);
    /// The end of the match: nothing is counted after it. Calling it twice does nothing.
    void finish();
    SeatPeople seat(uint8_t seat) const;
    /// Every blow that was seen, in order
    const std::vector<Blow>& blows() const noexcept;
    /// The match's mean walking speed in tiles per tick (0 while no ant that walks has been sampled twice)
    double walking_speed() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The observer that reads an engine after each tick and feeds a People: construct once per match, call tick() after every tick, finish() when it is over
class PeopleObserver {
public:
    PeopleObserver();
    ~PeopleObserver();
    void tick(const sim::SimulationEngine& engine);
    void finish();
    bool started() const noexcept { return people_ != nullptr; }
    const People& people() const { return *people_; }

private:
    std::unique_ptr<People> people_;
    Snapshot snap_;
};

/// The sums of seats over matches, by a label (the spec of the seat: "standard:hard"); `lines()` gives one line of text for each label
class PeopleTotals {
public:
    void add(const std::string& label, const SeatPeople& seat);
    void merge(const PeopleTotals& other);
    std::vector<std::string> lines() const;
    /// {"label":{...},...}
    std::string json() const;
    bool empty() const noexcept { return labels_.empty(); }

private:
    std::vector<std::string> labels_;
    std::vector<SeatPeople> sums_;
};

}  // namespace ants::watch
