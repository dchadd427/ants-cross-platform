#pragma once

// What a match of computer players is asked to be (docs/SERVER_REPLAYS.md, "Matches of computer players"): the choices that the admin page's "Start now" and a schedule share, and the limits that keep the matches of
// computer players from taking the server from the people who play on it. Plain data and the words that name it on the wire and in the database; the rules of what is done with it are RoomManager::start_bot_match's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_ctl/json.hpp"

namespace ants::server {

/// The most matches of computer players that run at once on a server, from the schedules and from the admin page together (they all run on the server's one thread)
inline constexpr size_t kBotMatchesMax = 8;
/// The most places that they may take together (OnlineLimits::weight: every shipped map one place, a big map several); a start that would go past it is refused
inline constexpr uint32_t kBotPlacesMax = 16;
/// A start is refused while the matches of people use more than this part of the server's places (3 of every 4): people come first
inline constexpr uint32_t kBotPeopleShareNumerator = 3;
inline constexpr uint32_t kBotPeopleShareDenominator = 4;
/// The matches of computer players that are made one after another are started this far apart, so that setting up their engines is spread over the passes of the server's loop (milliseconds)
inline constexpr uint32_t kBotStartGapMs = 1000;
/// The original's maps and the players' maps are picked at random only when the map's own time limit is this many minutes or less; a longer one is played when a choice names it, or when it is in a list of the rotation
inline constexpr uint32_t kBotRandomMaxMinutes = 30;
/// A list of the rotation holds at most this many maps
inline constexpr size_t kBotRotationMax = 100;
/// The spacing of a batch (BotWhat::every_s): whole steps of this many seconds, at most kBotSpacingMaxS (two hours)
inline constexpr uint32_t kBotSpacingStepS = 30;
inline constexpr uint32_t kBotSpacingMaxS = 7200;
/// The starts that wait for their time, from every batch together (BotMatches): a batch that would hold more is refused
inline constexpr size_t kBotWaitingMax = 32;
/// The most map files that one call of the catalog (RoomManager::bot_map_options) loads and judges when their verdicts are not kept yet: the rest are listed as not checked, and the page asks again. The game loop never waits for the whole library
inline constexpr size_t kBotCatalogLoads = 48;
inline constexpr size_t kBotLoadAll = static_cast<size_t>(-1);
/// The map files that the main loop judges in a pass in which it starts no match (BotMatches::update), until the catalog has them all: about a quarter of a millisecond each
inline constexpr size_t kBotWarmLoadsPerPass = 4;

inline constexpr const char* kBotMapRotation = "random:rotation";       // a map of the list of the rules (docs/SERVER_REPLAYS.md): the default of every schedule
inline constexpr const char* kBotMapOriginal = "random:original";       // one of the original's six maps
inline constexpr const char* kBotMapPlayers = "random:players";         // one of the approved maps of the players

enum class BotRules : uint8_t { Random, HighestScore, Kills187 };
/// The level of one bot: "rmh" draws Medium or Hard (what the server's own matches always did), "random" draws any of the three
enum class BotLevelChoice : uint8_t { RandomMediumHard, Easy, Medium, Hard, RandomAny };
enum class BotTeamsChoice : uint8_t { Random, FreeForAll, Split };

/// One match
struct BotMatchSpec {
    std::string map{kBotMapRotation};                 // kBotMapRotation, kBotMapOriginal, kBotMapPlayers or the file name of one map of the choices (RoomManager::bot_map_options)
    BotRules rules{BotRules::Random};
    uint8_t bots{0};                                  // 2 - 4; 0: two to four by a draw
    std::array<BotLevelChoice, 4> levels{BotLevelChoice::RandomMediumHard, BotLevelChoice::RandomMediumHard, BotLevelChoice::RandomMediumHard, BotLevelChoice::RandomMediumHard};
    BotTeamsChoice teams{BotTeamsChoice::Random};
    int split{0};                                     // teams Split: the number of the way in bot_splits(bots)
};

/// One map of a list of the rotation: the file's name and whether it is an approved map of the players (it is then found in the store of the approved maps only)
struct BotRotationEntry {
    std::string file;
    bool approved{false};
};
/// The two lists that "random:rotation" draws from, one for each rules (false: the original's game, true: 187): the lists that a server starts with, in the order of their draws (docs/SERVER_REPLAYS.md "The lists": their one home)
const std::vector<BotRotationEntry>& bot_rotation_defaults(bool kills_187);

/// Why a match of computer players was not made (RoomManager::start_bot_match): the admin page words each of them
enum class BotRefusal : uint8_t {
    None,
    NoReplays,        // the server keeps no replays: a match that nobody could look at again is not played
    Cap,              // kBotMatchesMax matches of computer players go already
    Places,           // the match would take the places of the matches of computer players past kBotPlacesMax
    PeopleBusy,       // the matches of people use more than their share of the server's places
    Budget,           // the replay store has no room left this hour for the recording of another match of computer players (ReplayStore::bot_saves_left): a match that would not be kept is not played
    Islands187,       // ISLANDS in 187: its water joins no two hills, so nobody could be struck
    NoMap,            // none of the maps that the choice names can be played now (or the server does not have the map)
    Failed            // a room could not be made
};

/// One map of the server's catalog (RoomManager::bot_map_options): every map that it has, with what a room can do with it
struct BotMapOption {
    std::string file;                                 // the file name: what a choice carries
    std::string group;                                // "original", "library" or "players"
    uint32_t width{0}, height{0};
    uint32_t players{0};                              // the seats that the map has
    uint32_t places{1};                               // the places that a match on it takes (OnlineLimits::weight)
    uint32_t minutes{0};                              // the map's own time limit: how long a match on it can play
    bool random{true};                                // a random pick may take it (its time limit is kBotRandomMaxMinutes or less); false: only a choice that names it, or a list of the rotation, plays it
    bool playable{true};                              // a room can play it now (false: `reason` says why not)
    bool checked{true};                               // false: its file has not been judged yet (the call's budget of loads was used up): `playable` and `reason` say nothing yet
    std::string reason;                               // why a room cannot play it ("it starts too many ants for a room", "it has no place for four seats", ...); empty when it can
};

/// A batch of matches: what Start now asks for and what a schedule starts each time it runs
struct BotWhat {
    uint32_t count{1};                                // 1 - kBotMatchesMax
    uint32_t every_s{0};                              // the spacing: 0 starts every match at once; else the first starts now and each next one this many seconds after the one before (a multiple of kBotSpacingStepS, at most kBotSpacingMaxS)
    BotMatchSpec spec;
};

/// The ways to split three or four bots in two sides (the bots are numbered from 1 in the order of their seats): `text` is the word on the wire ("12-34": bots 1 and 2 against 3 and 4), `pair` the two bots (0-based)
/// that are made a team (the others are the other side)
struct BotSplit {
    const char* text;
    std::array<uint8_t, 2> pair;
};
/// The splits for that many bots (3 or 4), none for another number
const std::vector<BotSplit>& bot_splits(uint8_t bots);

/// Reads a batch from the JSON that the admin page sends (and the database keeps): {"count", "map", "rules", "bots", "levels", "teams", "split", "every_s"} (the last may be left out: 0). False, with the error code that the page is told (`bad_count`, `bad_map`,
/// `bad_rules`, `bad_bots`, `bad_levels`, `bad_teams`, `bad_split`, `bad_spacing`), for anything that is not one of the words. A map that is a file name is only checked for its shape here (a schedule may name a map that is gone; what a
/// room can play is decided when the match is made).
bool parse_bot_what(const ctl::JsonValue& json, BotWhat& out, std::string& error);
/// The same, as the page and the database have it
ctl::JsonValue bot_what_json(const BotWhat& what);
/// A short line for the admin log: "3 matches, rotation map, rules 187, 4 bots, one every 3 minutes"
std::string bot_what_words(const BotWhat& what);
/// The spacing in words, for the log and the answers: "one every 3 minutes", "one every 1.5 minutes", "one every 1 minute"
std::string bot_spacing_words(uint32_t every_s);
/// Whether the text is the shape of a map's file name that a batch may carry (a name of 1 to 96 characters, no path, ends in .lvl in any case)
bool bot_map_name_shape_ok(const std::string& name);

}  // namespace ants::server
