#pragma once

// How big a map an ONLINE match may have, and what a big map costs the server (docs/SERVER.md, "Online size limit"). Until the server's load with big maps is measured, a room plays a map only up to
// kMaxCells cells (256 x 256 is the largest Classic map and the largest Extended map that is played online), kMaxStartAnts starting ants (all four colors together), kMaxPlantWaypointPairs pairs of a plant and
// a waypoint, kMaxFoodObjects food objects and kMaxPowerUps power-ups; a bigger approved or test map stays on the lists, can be opened and edited in the Map Maker and played against bots on the player's own
// computer or in the browser's test game, but a room refuses it with one sentence (net::kNoticeMapTooBig, kNoticeMapTooManyAnts, kNoticeMapTooManyPairs, kNoticeMapTooManyFood, kNoticeMapTooManyPowerUps).
// Above kHeavyStartAnts a map is also marked heavy, and the Map Maker warns that a game on a small device may run slowly (a room refuses such a map first, it is over the cap).
// The numbers are here and nowhere else. Every door asks one question about a MapCounts (allows / first_over), so a door cannot leave a limit out; a new limit is a new field of MapCounts (made from every
// number, so every place that builds one stops compiling), a new Term (before End) in the list kTerms, a row of info() and a line of counts_of (and of counts_of_report): the compiler then points at every
// place that must say what to do with it, and the places that walk through the limits all walk through kTerms.

#include <array>
#include <cstddef>
#include <cstdint>

namespace ants::assets { class LevelData; }
namespace ants::ctl { class JsonValue; }

namespace ants::server {

/// What the limits look at in a map: one number per limit. A door gets all of it from one place (counts_of for a loaded map, counts_of_report for a file that the map core has read, a store's record for a
/// listed one), never field by field. There is no default: a MapCounts is made from every number, so a limit that is added makes every place that builds one fail to compile until it says what that
/// limit counts.
struct MapCounts {
    constexpr MapCounts(uint64_t cells_, uint64_t start_ants_, uint64_t pairs_, uint64_t food_objects_, uint64_t power_ups_) noexcept
        : cells(cells_), start_ants(start_ants_), pairs(pairs_), food_objects(food_objects_), power_ups(power_ups_) {}
    uint64_t cells;                // rows times columns
    uint64_t start_ants;           // the start markers of the four colors added up
    uint64_t pairs;                // the plants (dropper plants of block 1) times the waypoints (block 4)
    uint64_t food_objects;         // the food objects (block 2 records that the loader kept)
    uint64_t power_ups;            // the cells of layer 2 that hold a power-up tile
};

/// The limits, in the order in which a map is judged: the first one that a map is over is the reason that a room gives. End is no limit: it counts them (kTerms has a place for each)
enum class Term : uint8_t { None, Cells, StartAnts, Pairs, FoodObjects, PowerUps, End };

/// Every limit once, in the order of Term: the one list that the judgement, the store's refusal and its list, and the tests walk through. A limit that is added to Term and not to this list does not compile
/// (the size follows the enum, and the check below wants every place to hold the limit of its number)
inline constexpr std::array<Term, static_cast<size_t>(Term::End) - 1> kTerms{Term::Cells, Term::StartAnts, Term::Pairs, Term::FoodObjects, Term::PowerUps};
constexpr bool terms_are_listed_in_order() noexcept {
    for (size_t i = 0; i < kTerms.size(); ++i) {
        if (kTerms[i] != static_cast<Term>(i + 1)) return false;
    }
    return true;
}
static_assert(terms_are_listed_in_order(), "kTerms lists every limit of Term (not None, not End) once, in the order of Term");

/// What a door says about a limit that a map is over (plain sentences; the numbers are the limits' own)
struct TermInfo {
    const char* why;       // the marker of GET /maps/all ("online_why"); "" for the size
    const char* code;      // the error code of the doors that take a file (a test map, a submission)
    const char* phrase;    // "starts too many ants": what is wrong with the map, after "the map X" or "This map"
    const char* tail;      // " to play online" (" yet" after the size): the words that follow the phrase when the door says what a player may do
    const char* limit;     // "1,000 at most, all colors together"
    const char* more;      // "starts more ants": the same, where a door says "than a room plays"
    const char* notice;    // the room's sentence for the leader (net::kNoticeMapTooBig and its siblings)
};

struct OnlineLimits {
    /// The most cells (rows times columns) of a map that a room plays: 65,536, so 256 x 256 or 400 x 160. Every Classic map is under it.
    static constexpr uint32_t kMaxCells = 256u * 256u;
    /// The server's place for a match (--demo-rooms) is sized for a shipped map, which is up to about 10,000 cells; a match costs the server about as much as its map has cells (one tick of a 256 x 256 map takes
    /// roughly 17 times a tick of the TREASURE map, a 512 x 512 map about 100 times), so a match takes one place for every started 10,000 cells of its map: every shipped map one place, the largest online map seven.
    static constexpr uint32_t kCellsPerPlace = 10000u;

    /// Above this many starting ants (the start markers of the four colors added up: 600 a color on a map of four) a map is heavy: a game on it may run slowly on some devices. Only a warning, for a game on the player's own
    /// device (the Map Maker's Play this map dialog, which keeps a copy of the number, `HEAVY_START_ANTS` in mapmaker_play.js); a single-player game is never refused for its ants. Such a map is over kMaxStartAnts, so a room refuses it.
    static constexpr uint32_t kHeavyStartAnts = 2400;

    /// The most starting ants (the start markers of the four colors added up, as start_ants counts them) of a map that a room plays. A match costs the one thread that serves every room more the more ants it has
    /// (setting the ants up grows with the square of their number, measured natively: 6,000 ants about 50 ms, 10,000 about 0.5 s, 40,000 about 9 s), so a map above this is refused before its match is made. 1,000 is
    /// above every map of the library (at most 248); a bigger map of the players stays on the lists and is played on the player's own device.
    static constexpr uint32_t kMaxStartAnts = 1000;
    /// A match takes one more place for every started kAntsPerPlace starting ants (see weight): 400 ants one place, 1,000 ants three
    static constexpr uint32_t kAntsPerPlace = 400u;

    /// The most pairs of a plant and a waypoint of a map that a room plays (the Map Maker's Play this map dialog keeps a copy of the number, `MAX_ONLINE_PAIRS` in mapmaker_play.js, and a test pins the two
    /// equal). Setting a match up looks at every plant that drops flowers for the first waypoint on its cell (SimulationEngine::init), so the work is the plants times the waypoints: a map within
    /// kMaxCells and kMaxStartAnts can still hold far more of both than any real map does (block 1 and block 4 each hold up to 65,535 records on their own), so this is checked on its own. 1,000,000 is 78
    /// times the largest map of the library (112 plants and 114 waypoints); a map above it stays on the lists and is played on the player's own device, as for the other limits.
    static constexpr uint64_t kMaxPlantWaypointPairs = 1000000;

    /// The most food objects of a map that a room plays (`MAX_ONLINE_FOOD` in mapmaker_play.js is the Map Maker's copy, pinned equal by a test). The engine finds the object of a food cell by looking at every object
    /// (Grid::food_object_at_cell), and setting a match up and searching paths ask for it once for every food cell and every blocked step, so the work grows with the square of the number of objects (a block 2 holds
    /// up to 65,535 of them whatever the grid's size). 250 is three times the largest map of the library (84).
    static constexpr uint64_t kMaxFoodObjects = 250;

    /// The most power-ups (cells of layer 2 that hold a power-up tile) of a map that a room plays (`MAX_ONLINE_POWERUPS` in mapmaker_play.js is the Map Maker's copy, pinned equal by a test). A bot groups the
    /// power-ups that touch each other when it first looks at a map with more than one walkable part, and plans a walk through a group from every one of its entrances, so what it does grows faster than
    /// the number of power-ups (a layer 2 holds as many as the grid has cells). 1,000 is above every map of the library (at most 896); a map above it stays on the lists and is played on the player's own
    /// device, as for the other limits.
    static constexpr uint64_t kMaxPowerUps = 1000;

    /// The limit of a term (0 for None)
    static uint64_t limit_of(Term term) noexcept;
    /// The number of a term in a map's counts (0 for None)
    static uint64_t count_of(const MapCounts& c, Term term) noexcept;
    /// Whether a room plays a map with these counts: every limit holds
    static bool allows(const MapCounts& c) noexcept;
    /// The first limit (in the order of Term) that a map with these counts is over; Term::None when a room plays it
    static Term first_over(const MapCounts& c) noexcept;
    /// Whether the map is over this one limit (whatever the others say)
    static bool over(const MapCounts& c, Term term) noexcept;
    /// What a door says about a limit (all fields empty for Term::None)
    static const TermInfo& info(Term term) noexcept;

    static constexpr bool heavy(uint64_t start_ants) noexcept { return start_ants > kHeavyStartAnts; }
    /// The start markers of the four colors of a loaded map, added up: counted from the file that the server itself loaded, never from a number that a client sent
    static uint64_t start_ants(const assets::LevelData& level);
    /// The plants of the level (the records of block 1 whose tile has the plant flag, sim::movement::is_dropper_plant_tile: the same predicate SimulationEngine::init tests) times its waypoints
    /// (level.waypoints.size()): the pairs that SimulationEngine::init's lookup looks at when it sets a match up. Counted from the file that the server itself loaded, never from a number that a client sent.
    static uint64_t plant_waypoint_pairs(const assets::LevelData& level);
    /// The food objects of the level: the records of block 2 that the loader kept (the objects that Grid::init_from_level puts into the engine's table, which every lookup of a food cell looks through)
    static uint64_t food_objects(const assets::LevelData& level);
    /// The power-ups of the level: the cells of layer 2 whose tile is a power-up (sim::movement::is_powerup_tile, the ids 62 to 66), which Grid::init_from_level marks and the bots' MapInfo lists. A food footprint
    /// that covers one takes it away in the engine, so this number can only be higher than the bots' own.
    static uint64_t power_ups(const assets::LevelData& level);
    /// Every number of a loaded map: the cells, the start ants, the pairs, the food objects and the power-ups, counted from the file that the server itself loaded
    static MapCounts counts_of(const assets::LevelData& level);
    /// The same from the map core's reading of a canonical file (core::read_report as a JSON value: rows, columns, teams[].start_markers, plants, waypoints, food_piles, powerups), for the doors that take a file
    /// and have no loaded level (a test map, a submission); a key that is not there counts as 0
    static MapCounts counts_of_report(const ctl::JsonValue& report);
    /// The places that a match on a map of `cells` cells takes (at least one)
    /// (the most of the places for its cells, one for every started kCellsPerPlace, and the places for its ants, one for every started kAntsPerPlace; the ants default to none)
    static constexpr uint32_t weight(uint64_t cells, uint64_t start_ants = 0) noexcept {
        const uint64_t by_cells = cells <= kCellsPerPlace ? 1u : (cells + kCellsPerPlace - 1) / kCellsPerPlace;
        const uint64_t by_ants = (start_ants + kAntsPerPlace - 1) / kAntsPerPlace;
        return static_cast<uint32_t>(by_cells > by_ants ? by_cells : by_ants);
    }
};

}  // namespace ants::server
