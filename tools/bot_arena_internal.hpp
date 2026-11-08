#pragma once

// What bot_arena.cpp shares with the self-test of the tool (bot_arena_selftest.cpp): the options, the maps, the matches and the functions that read the command line, play the matches and write
// the report. The types are defined here; the functions, the note of the report and the two output streams (g_out, g_err) are defined in bot_arena.cpp.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "ants_ai/arena.hpp"
#include "ants_ai/bot.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::arena {

using Clock = std::chrono::steady_clock;

struct Options {
    std::vector<std::string> maps;                 // names or paths, in the order given
    std::string maps_dir;                          // "" = the shipped maps
    std::vector<uint32_t> seeds{1};
    std::vector<ai::BotSpec> seats;                // by seat number
    bool seats_given{false};
    uint64_t ticks{0};                             // 0 = full
    uint32_t latency{3};
    bool rotate{false};
    uint32_t repeat{1};
    bool replay_check{false};
    sim::GameMode game_mode{sim::GameMode::HighestScore};   // --game-mode: the rules of every match of the run
    std::string save_replays;                      // a folder: every match is kept there as a replay (the server's store names and keeps them; the site's list shows them)
    unsigned threads{1};
    std::string out;
    bool quiet{false};
    bool wall_time{true};
    bool selftest{false};
    bool write_baselines{false};
    bool ally_standard{false};                     // the first two standard bots of a match team up (the lower seat invites)
    bool ally_pairs{false};                        // every two seats with the same standard spec team up (2 + 2 with [A, A, B, B]; the lower seat of a pair invites)
    bool help{false};
};

struct LoadedMap {
    std::string name;                // what the report calls it: the file's name without folder and extension, in capitals for a shipped map
    std::string file_name;           // the map's file as a replay names it ("TREASURE.LVL"), and the FNV-1a hash of the file (net::hash_file): another file under the same name is another map
    uint64_t file_hash{0};
    assets::LevelData level;
    bool ok{false};
    std::string error;
};

struct Job {
    size_t map{0};
    uint32_t seed{1};
    uint32_t rotation{0};
    std::vector<ai::BotSpec> bots;
};

struct MatchReport {
    Job job;
    ai::ArenaResult result;                        // after run_job the applied-command log and the hash checkpoints are gone (see `commands`): a report keeps the numbers only
    bool replay_checked{false};
    ai::ReplayResult replay;
    uint64_t commands{0};                          // how many commands were applied (the size of the log that run_job dropped)
    uint32_t plays{0};                             // how many times the match was actually played (--repeat)
    bool repeat_ok{true};
    std::string repeat_note;
    double wall_ms{0.0};
    bool ok() const { return result.error.empty() && (!replay_checked || replay.ok) && repeat_ok; }
};

// The note of the report about the kinds of bots
extern const char* const kKindsNote;

// Where the tool prints its lines and its complaints (stdout and stderr by default)
extern std::FILE* g_out;
extern std::FILE* g_err;

std::string hex64(uint64_t v);
std::string json_quote(const std::string& s);

bool parse_seeds(const std::string& text, std::vector<uint32_t>& out, std::string& err);
bool parse_seat(const std::string& text, ai::BotSpec& out, std::string& err);
bool parse_args(const std::vector<std::string>& a, Options& o, std::string& err);

std::string stem_of_path(const std::string& path);
std::vector<LoadedMap> load_maps(const Options& o);

std::string spec_text(const ai::BotSpec& s);
std::vector<std::vector<ai::BotSpec>> arrangements(const std::vector<ai::BotSpec>& given, bool rotate);

bool same_match(const ai::ArenaResult& a, const ai::ArenaResult& b);
ai::ArenaSpec spec_of(const Options& o, const LoadedMap& m, const Job& j, bool record);
MatchReport run_job(const Options& o, const std::vector<LoadedMap>& maps, const Job& job, std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)> factory);
bool make_jobs(const Options& o, size_t map_count, std::vector<Job>& jobs, std::string& error);
std::vector<MatchReport> run_jobs(const Options& o, const std::vector<LoadedMap>& maps, const std::vector<Job>& jobs,
                                  const std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>& factory, bool progress);

std::string seeds_text(const std::vector<uint32_t>& seeds);
bool write_report(std::ostream& out, const Options& o, const std::vector<LoadedMap>& maps, const std::vector<MatchReport>& reports);

// `factory` (null: the registry's bots) lets the self-test seat bots that the registry does not have (the default argument is in bot_arena.cpp)
int run_tool(const Options& o, const std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>& factory);

// The checks of the tool itself (bot_arena_selftest.cpp): 0 when every check passed
int selftest();

}  // namespace ants::arena
