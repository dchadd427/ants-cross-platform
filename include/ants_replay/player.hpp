#pragma once

// Playing a replay with no screen. The engine is set up exactly as a match sets it up (the map, the Fog of War setting, the seed, the seats that play, the names, the teams that the match
// started with), the recorded commands go in at their turns and the ticks run, so the engine passes through the states of the recorded match. The hashes in the file say whether it does:
// play() compares every one of them as it goes and stops at the first that differs.
//
// Two uses are built on it: replay_tool verify (did this match play out the same here: every hash and the final state) and replay_tool orders (every order with its time, place and ant
// type, read off the engine as it plays: a command names its ants by number, and only the engine knows what an ant is when it is ordered).

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ants_assets/lvl_parser.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::replay {

/// Finds the replay's map in `maps_dir` by the file name that the head holds (the name as written, else the same name in another case), checks that it is the same file (the head's hash)
/// and loads it. False with `error` otherwise: a missing map is refused by name, a map of the same name that is another file with both hashes.
bool load_map(const Header& head, const std::string& maps_dir, assets::LevelData& level, std::string& error);

/// Why this build does not play `head`'s match (its rules number and the game that made it): "recorded by v0.10.0 with simulation rules 0 ...". Only for a head that plays_here() says no to.
std::string rules_refusal(const Header& head);

/// Sets `engine` up as the match began: the Fog of War setting that the caller wants (a viewer shows the whole map: false; it is not part of any hash), the game mode of the head (before init, as every machine
/// of the match did), the seed, the seats that play, their names and the teams that the match started with. `level` is the map that load_match found (a viewer may hand the one that has no hill art for the seats that do not play: the match is the same).
void begin_match(sim::SimulationEngine& engine, const Replay& replay, const assets::LevelData& level, bool fog);

/// What play() calls while it plays; both are optional
struct Hooks {
    /// Before a command is applied: its turn, the command, and the engine as it stands then (the ants that the command names can be looked up)
    std::function<void(uint32_t turn, const sim::Command& command, sim::SimulationEngine& engine)> before_command;
    /// After it was applied, with what the engine said
    std::function<void(uint32_t turn, const sim::Command& command, const sim::CommandResult& result, sim::SimulationEngine& engine)> after_command;
};

struct Outcome {
    bool ran{false};                // the replay was played to its end (false: refused before it began, or it stopped at a hash that differs)
    bool ok{false};                 // it ran and every hash that the file holds matched (a complete file: its final hash too)
    bool complete{false};           // the file had its ENDS chunk (an incomplete one is played as far as it goes)
    std::string error;              // why not ok
    uint32_t turns{0};              // the turns that were played
    uint64_t hash{0};               // the engine's state hash (StateHash::total) when it stopped
    bool match_over{false};         // the engine's rules ended the match
    uint32_t hashes_checked{0};
    uint32_t first_bad_turn{0};     // the turns played when the first hash that differs was taken (0: none differed)
    std::array<int32_t, sim::MAX_PLAYERS> scores{};     // the score box of each seat when the replay ended (own score plus the ally's; 0 for a seat that does not play)
};

/// Plays `replay` on `level` (the map that load_map found). A file of other simulation rules than this build's (plays_here) is refused: it would play out differently, and `error` names both numbers.
Outcome play(const Replay& replay, const assets::LevelData& level, const Hooks& hooks = Hooks{});

/// One order of the match as it was given
struct OrderRecord {
    struct AntCount {
        sim::AntType type{sim::AntType::Worker};
        uint32_t count{0};
    };
    uint32_t turn{0};
    sim::Command command;
    std::vector<AntCount> ants;     // the named ants that stand in the match and belong to the issuer, by type, in order of first appearance
    uint32_t absent{0};             // named ants that are gone, never were, or belong to another seat (the engine leaves them out)
    bool has_from{false};           // where the first of those ants stood when the order was given
    int32_t from_x{0};
    int32_t from_y{0};
    sim::CommandResult::Status status{sim::CommandResult::Status::Ignored};
    uint32_t ants_ordered{0};       // group orders and Stop: the ants that the engine took the order for
};

/// Plays the replay and describes every command in it, in the order given. `outcome` says how the play went; when it stopped at a hash that differs, the list holds the orders before that.
std::vector<OrderRecord> list_orders(const Replay& replay, const assets::LevelData& level, Outcome& outcome);

/// "12:34.5": the game time of a turn (the time since the first tick; a turn is 50 ms)
std::string format_time(uint32_t turn);
/// "Green", or "Green (Dave)" when the seat has a name
std::string seat_label(const Header& head, uint8_t seat);
/// "Bomber", "Swimmer" ...
const char* ant_type_name(sim::AntType type) noexcept;
/// "move", "special", "attack", "stop", "hatch", "team offer" ...
const char* order_name(sim::CommandType type) noexcept;
/// "ok", "ignored" or "refused": what the engine said
const char* status_name(sim::CommandResult::Status status) noexcept;
/// The ants of an order as text: "3 Bomber, 1 Worker" (+ " + 2 not there" when some were absent)
std::string ants_text(const OrderRecord& order);
/// Where the order goes: "(45,32)" for a group order, "with Red" for an alliance command, "" for the rest
std::string place_text(const OrderRecord& order, const Header& head);

}  // namespace ants::replay
