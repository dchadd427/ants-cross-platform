#include "ants_server/online_limits.hpp"

#include <algorithm>

#include "ants_assets/lvl_parser.hpp"
#include "ants_ctl/json.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::server {

// One ant for every start marker of a color (team 0 to 3); the records of block 1 that are no marker (a plant, team 255) start no ant. The work is one pass over the records that the loader kept, and the loader's
// limits (assets::LevelLimits) bound how many there are, so a map that a stranger sent costs no more than loading it did.
uint64_t OnlineLimits::start_ants(const assets::LevelData& level) {
    uint64_t ants = 0;
    for (const assets::AnthillSpawn& spawn : level.anthill_spawns()) {
        if (spawn.team_id < 4) ++ants;
    }
    return ants;
}

// The plants (block 1 records whose tile has sim::movement::is_dropper_plant_tile's flag, counted the same pass as SimulationEngine::init makes) times the waypoints (block 4 records the loader kept): the work
// of SimulationEngine::init's lookup. One pass over the records the loader kept, and the loader's limits bound how many there are (at most 65,535 of each), so a map that a stranger sent costs no more to count
// than loading it did; the multiplication cannot overflow a uint64_t (65,535 x 65,535 is about 4.3 billion).
uint64_t OnlineLimits::plant_waypoint_pairs(const assets::LevelData& level) {
    uint64_t plants = 0;
    for (const assets::AnthillSpawn& spawn : level.anthill_spawns()) {
        if (sim::movement::is_dropper_plant_tile(spawn.tile_id)) ++plants;
    }
    return plants * static_cast<uint64_t>(level.waypoints.size());
}

// The table of block 2 that Grid::init_from_level copies into the engine's food table (one object for every record that the loader kept, in file order), which the lookups of a food cell look through
uint64_t OnlineLimits::food_objects(const assets::LevelData& level) {
    return static_cast<uint64_t>(level.food_schedules.size());
}

// The cells of layer 2 that Grid::init_from_level marks as power-ups: a tile that is not empty and has the power-up flag (the ids 62 to 66); the bots' MapInfo lists the cells that are still marked when the match
// starts. One pass over the cells that the loader kept (at most as many as its limits allow).
uint64_t OnlineLimits::power_ups(const assets::LevelData& level) {
    uint64_t found = 0;
    for (const assets::MapCell& cell : level.layer2_interactive) {
        if (sim::movement::is_powerup_tile(cell.tile_index)) ++found;
    }
    return found;
}

MapCounts OnlineLimits::counts_of(const assets::LevelData& level) {
    return MapCounts{static_cast<uint64_t>(level.width()) * level.height(), start_ants(level), plant_waypoint_pairs(level), food_objects(level), power_ups(level)};
}

// The same numbers from the map core's reading of the canonical file (the doors that take a file have no loaded level): the start markers of the four colors added up, the plants times the waypoints, the food piles
// and the power-ups, every one counted by the core from the file itself, never a number that the sender gave
MapCounts OnlineLimits::counts_of_report(const ctl::JsonValue& report) {
    const auto number = [&](const char* key) { return static_cast<uint64_t>(std::max<int64_t>(0, report.get(key).as_int_or(0))); };
    uint64_t start_ants = 0;
    for (size_t s = 0; s < 4 && s < report.get("teams").size(); ++s) start_ants += static_cast<uint64_t>(std::max<int64_t>(0, report.get("teams").at(s).get("start_markers").as_int_or(0)));
    return MapCounts{number("rows") * number("columns"), start_ants, number("plants") * number("waypoints"), number("food_piles"), number("powerups")};
}

uint64_t OnlineLimits::limit_of(Term term) noexcept {
    switch (term) {
        case Term::None:
        case Term::End: return 0;
        case Term::Cells: return kMaxCells;
        case Term::StartAnts: return kMaxStartAnts;
        case Term::Pairs: return kMaxPlantWaypointPairs;
        case Term::FoodObjects: return kMaxFoodObjects;
        case Term::PowerUps: return kMaxPowerUps;
    }
    return 0;
}

uint64_t OnlineLimits::count_of(const MapCounts& c, Term term) noexcept {
    switch (term) {
        case Term::None:
        case Term::End: return 0;
        case Term::Cells: return c.cells;
        case Term::StartAnts: return c.start_ants;
        case Term::Pairs: return c.pairs;
        case Term::FoodObjects: return c.food_objects;
        case Term::PowerUps: return c.power_ups;
    }
    return 0;
}

// The one question every door asks. A limit is a line here and a case in limit_of, count_of and info: a limit that is added breaks the build of every switch that does not name it.
bool OnlineLimits::allows(const MapCounts& c) noexcept {
    return first_over(c) == Term::None;
}

Term OnlineLimits::first_over(const MapCounts& c) noexcept {
    for (const Term term : kTerms) {
        if (over(c, term)) return term;
    }
    return Term::None;
}

bool OnlineLimits::over(const MapCounts& c, Term term) noexcept {
    return term != Term::None && term != Term::End && count_of(c, term) > limit_of(term);
}

const TermInfo& OnlineLimits::info(Term term) noexcept {
    static const TermInfo kNone{"", "", "", "", "", "", ""};
    static const TermInfo kCells{"", "map_too_big_online", "is too big", " to play online yet", "65,536 cells at most, like 256 x 256", "is bigger", net::kNoticeMapTooBig};
    static const TermInfo kAnts{"ants", "map_too_many_ants_online", "starts too many ants", " to play online", "1,000 at most, all colors together", "starts more ants", net::kNoticeMapTooManyAnts};
    static const TermInfo kPairs{"pairs", "map_too_many_pairs_online", "has too many pairs of a plant and a waypoint", " to play online", "1,000,000 at most", "has more pairs of a plant and a waypoint",
                                 net::kNoticeMapTooManyPairs};
    static const TermInfo kFood{"food", "map_too_many_food_online", "has too many food objects", " to play online", "250 at most", "has more food objects", net::kNoticeMapTooManyFood};
    static const TermInfo kPowerUps{"powerups", "map_too_many_powerups_online", "has too many power-ups", " to play online", "1,000 at most", "has more power-ups", net::kNoticeMapTooManyPowerUps};
    switch (term) {
        case Term::None:
        case Term::End: return kNone;
        case Term::Cells: return kCells;
        case Term::StartAnts: return kAnts;
        case Term::Pairs: return kPairs;
        case Term::FoodObjects: return kFood;
        case Term::PowerUps: return kPowerUps;
    }
    return kNone;
}

}  // namespace ants::server
