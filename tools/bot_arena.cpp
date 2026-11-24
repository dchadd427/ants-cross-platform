// bot_arena: plays one or many matches of computer players headless, with the real engine and the BotController (docs/BOTS.md). The tool every later bot is measured
// with: the same arguments always give the same matches, bit for bit, on any machine, whatever the number of threads.
//
// Usage:
//   bot_arena [--map NAMES] [--seeds A..B] [--seat N=KIND[:LEVEL[:STYLE]]]... [--ticks full|N] [--latency-ticks N] [--rotate] [--repeat N] [--replay-check]
//             [--threads N] [--out report.json] [--quiet] [--no-wall-time] [--maps-dir DIR] [--tune K=V,...] [--teams A+B] [--ally-standard | --ally-pairs]
//   bot_arena --selftest
//   bot_arena --write-baselines [--threads N] [--maps-dir DIR] > tests/test_ai/baselines.inc
//
//   --map NAMES       a comma list of shipped maps by name (TINY, SMALL, MEDIUM, GAUNTLET, TREASURE, ISLANDS, or "shipped" for all six) or paths of .LVL files (default TINY)
//   --seeds A..B      the engine and controller seeds: "3", "1..8", "1,4,9..12" (default 1)
//   --seat N=SPEC     a bot on seat N (0 green, 1 red, 2 blue, 3 black); SPEC is KIND, KIND:LEVEL, LEVEL, KIND:LEVEL:STYLE or LEVEL:STYLE: KIND idle, worker, standard (or one of the
//                     bench bots of tools/bench_aggressor.hpp, which are not bots of the game: aggressor, aggressor2 (a double-thief opening), rusher (the contested middle first),
//                     saboteur, gang (harvests, raids and fights as a group), gang2 (the same, every free ant in the group), turtle (shuts itself in: walls, bombs, its own piles only), aggr1 .. aggr9 (an aggressor with that many strikers)); LEVEL easy, medium, hard; STYLE aggressive, economic, raider, defensive or random (a standard
//                     bot's style: Hard plays aggressive or raider only; none: it draws its own per match). KIND may be `standard+K=V,K=V`, a standard bot with its own tuning of
//                     the keys of apply_tune (the tournaments' ablations).
//                     Repeat it for every seat that plays. Default: four standard bots at medium level. A seat that is not named has no hill and no ants. idle stands still; worker harvests (B3, the frozen yardstick); standard is the standard bot (B4-1: the worker's economy plus the tactics of its level).
//   --ticks full|N    play until the match is over (the map's own length, default) or at most N ticks (50 ms each)
//   --latency-ticks N the sink latency: 0 applies a command the moment a bot releases it; N > 0 plays like a lock-step room (applied at the first 100 ms turn
//                     boundary at least N ticks later, in canonical order); default 3
//   --rotate          play every distinct arrangement of the given bots over the given seats (up to 24 for four different bots): seats are not symmetric on a map, so a comparison of bots must rotate them
//   --repeat N        play every match N times and require identical results (finds any nondeterminism)
//   --game-mode M     the rules of every match: highest-score (the original's game, the default) or 187 (no food, the kills are the score, docs/GAMEPLAY.md); the report names the mode only when it is 187
//   --replay-check    re-feed the commands that were applied into a FRESH engine with no bot at all and require the same state hash at every 20th tick and at the end
//   --swimmer-trace   after every match one SWT line per seat (the ticks of its 3rd, 5th and 8th Swimmer, the most it had alive, the Swimmers alive and the score at every minute, its idle ants
//                     from the third minute on, the water tiles its Swimmers finished as bridges, the power-ups its ants took by kind) and one SWL line (the power-ups left on the ground at every minute); nothing is sent, the match is the same
//   --tune K=V,...    the same tuning for every standard bot of the run (the keys of apply_tune). BOT_DIAG=1 in the environment makes the standard bots print what their tasks did (counts of attack orders, raids, walls, ...) to stderr when a match is over.
//   --teams A+B       the match starts with seats A and B as a team and the other two as the other team (the rooms' and the game's own way: apply_start_teams after init; seats given as digits 0 to 3); free for all by default
//   --ally-standard   the first two standard bots of a match team up (the lower seat invites at its first look, the other accepts by its accept rule); --ally-pairs: every two seats with the same standard spec team up (2 + 2 for [A, A, B, B]). Test-only: no bot of the game invites.
//   --threads N       matches played at the same time (default 1; every match is independent, the report is sorted by map, seed and arrangement)
//   --out FILE        write the JSON report (fixed key order; the maps' NAMES only, no paths). The file is opened BEFORE the first match (an unwritable path costs nothing) and
//                     checked after the last: a report that could not be written completely is exit code 2, never a silent success. Like --save-replays it is never written into Original-Ants/ or Community-Maps/ nor over a map that is played (tools/library_guard.hpp): exit code 2
//   --no-wall-time    leave the wall clock time of each match out of the report, so that the file is bit-reproducible
//   --maps-dir DIR    where map names are looked for (default: the shipped maps)
//   --selftest        check the tool itself (determinism, replay check, report, threads, the tool end to end on a temporary folder)
//   --write-baselines print the table that tests/test_ai/baselines.inc pins (the worker bot at the three levels on the six shipped maps, a fixed seed set, seats rotated:
//                     include/ants_ai/baselines.hpp) to stdout; the other options (but --threads and --maps-dir) are ignored. Regenerate it ON PURPOSE, when the bot or the
//                     hill's banking changed.
//
// Exit code: 0 when every match was played and every check passed, 1 on a finding (a match that could not be played, a replay or repeat that differs), 2 on bad usage or when the
// report (or the match list: too many matches) cannot be handled.
//
// The checks of --selftest are in bot_arena_selftest.cpp; what they share with this file (the options, the maps, the matches, the report) is declared in bot_arena_internal.hpp.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ants_ai/arena.hpp"
#include "ants_ai/baselines.hpp"
#include "ants_ai/bot.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_app/version.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_server/replay_store.hpp"
#include "bench_aggressor.hpp"
#include "bot_arena_internal.hpp"
#include "bot_arena_tune.hpp"
#include "library_guard.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define ARENA_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define ARENA_PRINTF(fmt_index, first_arg)
#endif

namespace fs = std::filesystem;
namespace ai = ants::ai;
namespace sim = ants::sim;
namespace assets = ants::assets;
namespace net = ants::net;
namespace replay = ants::replay;
namespace server = ants::server;

namespace ants::arena {

constexpr const char* kShippedMaps[] = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"};
constexpr size_t kMaxMatches = 200000;

const char* const kKindsNote =
    "idle stands still; worker harvests (B3, the frozen yardstick); standard is the standard bot (B4-1: the economy of the worker plus the tactics of its level)";

ARENA_PRINTF(1, 2) std::string fmt(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    std::string s;
    if (n > 0) {
        s.resize(static_cast<size_t>(n));
        std::vsnprintf(&s[0], s.size() + 1, format, copy);
    }
    va_end(copy);
    return s;
}

std::string hex64(uint64_t v) { return fmt("%016llx", static_cast<unsigned long long>(v)); }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t from = 0;
    for (;;) {
        const size_t at = s.find(sep, from);
        out.push_back(s.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return out;
}

bool parse_uint(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    out = v;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------------------------------------------------------------

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        const uint8_t c = static_cast<uint8_t>(ch);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20 || c >= 0x7F) out += fmt("\\u%04x", static_cast<unsigned>(c));        // only ASCII is written as it is; any other byte as a Latin-1 character
        else out += ch;
    }
    out += '"';
    return out;
}

// Pretty printed, keys in the order they are written. With a sink the text is handed over by drain() (called at the end of each match) instead of piling up in one string, so
// the memory of a report does not grow with the number of matches; without a sink text() is the whole report.
class JsonWriter {
public:
    explicit JsonWriter(std::ostream* sink = nullptr) : sink_(sink) {}
    /// Hands what has been written to the sink (if there is one) and forgets it: the layout state (depth, commas) is kept
    void drain() {
        if (sink_ == nullptr) return;
        sink_->write(out_.data(), static_cast<std::streamsize>(out_.size()));
        out_.clear();
    }
    void begin_object() { prefix(); out_ += '{'; stack_.push_back({0}); }
    void end_object() { close('}'); }
    void begin_array() { prefix(); out_ += '['; stack_.push_back({0}); }
    void end_array() { close(']'); }
    void key(const std::string& k) { prefix(); out_ += json_quote(k) + ": "; key_pending_ = true; }
    void value(const std::string& v) { prefix(); out_ += json_quote(v); }
    void field(const std::string& k, const std::string& v) { key(k); prefix(); out_ += json_quote(v); }
    void field(const std::string& k, const char* v) { field(k, std::string(v)); }
    void field(const std::string& k, uint64_t v) { key(k); prefix(); out_ += std::to_string(v); }
    void field_signed(const std::string& k, int64_t v) { key(k); prefix(); out_ += std::to_string(v); }
    void field(const std::string& k, double v, int decimals) { key(k); prefix(); out_ += fmt("%.*f", decimals, v); }
    void field_bool(const std::string& k, bool v) { key(k); prefix(); out_ += v ? "true" : "false"; }
    const std::string& text() const { return out_; }

private:
    struct Level { size_t count; };
    void newline() { out_ += '\n'; out_.append(stack_.size() * 2, ' '); }
    void prefix() {
        if (key_pending_) { key_pending_ = false; return; }
        if (stack_.empty()) return;
        if (stack_.back().count++ > 0) out_ += ',';
        newline();
    }
    void close(char c) {
        const bool empty = stack_.back().count == 0;
        stack_.pop_back();
        if (!empty) newline();
        out_ += c;
    }
    std::ostream* sink_{nullptr};
    std::string out_;
    std::vector<Level> stack_;
    bool key_pending_{false};
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------------------------------------------

void print_usage(std::FILE* to) {
    std::fprintf(to,
        "Usage: bot_arena [--map NAMES] [--seeds A..B] [--seat N=KIND[:LEVEL[:STYLE]]]... [--ticks full|N] [--latency-ticks N] [--rotate] [--repeat N]\n"
        "                 [--game-mode highest-score|187] [--replay-check] [--save-replays DIR] [--threads N] [--out report.json] [--quiet] [--no-wall-time] [--maps-dir DIR] [--tune K=V,...] [--teams A+B] [--ally-pairs]\n"
        "       bot_arena --selftest\n"
        "       bot_arena --write-baselines [--threads N] [--maps-dir DIR] > tests/test_ai/baselines.inc\n"
        "Plays matches of computer players headless with the real engine. See the top of tools/bot_arena.cpp and docs/BOTS.md.\n"
        "  --map NAMES        shipped maps by name (TINY SMALL MEDIUM GAUNTLET TREASURE ISLANDS, or 'shipped') or .LVL paths, comma separated (default TINY)\n"
        "  --seeds A..B       \"3\", \"1..8\" or \"1,4,9..12\" (default 1)\n"
        "  --seat N=SPEC      a bot on seat N (0 to 3); SPEC is KIND, KIND:LEVEL, LEVEL, KIND:LEVEL:STYLE or LEVEL:STYLE (kinds idle worker standard and the bench bots aggressor aggressor2\n"
        "                     saboteur rusher gang gang2 turtle aggr1 .. aggr9; levels easy medium hard; styles aggressive economic raider defensive random, standard bots only: Hard plays aggressive or raider).\n"
        "                     KIND may be standard+K=V,K=V: a standard bot with its own tuning (see --tune). Default: four standard bots at medium level. %s.\n"
        "  --game-mode M      highest-score (the original's game, default) or 187 (no food, the kills are the score)\n"
        "  --teams A+B        seats A and B start as a team and the other two as the other team (digits 0 to 3, like the game's --teams; default: free for all)\n"
        "  --ticks full|N     until the match is over (default) or at most N ticks\n"
        "  --latency-ticks N  sink latency in ticks (default 3; 0 = commands applied at once)\n"
        "  --rotate           every distinct arrangement of the bots over the seats\n"
        "  --ally-standard    the first two standard bots of a match team up: the lower seat invites at its first look, the other accepts by its accept rule (test-only: no bot of the game invites)\n"
        "  --ally-pairs       every two seats that have the same standard spec team up (2 + 2 for [A, A, B, B]; the lower seat of a pair invites; test-only)\n"
        "  --repeat N         play every match N times and require identical results\n"
        "  --replay-check     replay the applied commands into a fresh engine without any bot: same hash at every 20th tick and at the end\n"
        "  --stuck-detail     after every match one STUCK line for each of the first stuck spans (when, seat, ant, place, state, order, what it holds): see docs/BOT_UNSTUCK.md\n"
        "  --swimmer-trace    one SWT line per seat after every match (Swimmers, score, idle ants, power-ups taken) and one SWL line (power-ups left on the ground): see the top of tools/bot_arena.cpp\n"
        "  --save-replays DIR keep every match that was played as a replay file (.antsrep) in DIR, named as the game server names its own (ants-MAP-DATE-TIMEZ.antsrep; docs/REPLAYS.md): the\n"
        "                     players are \"Bot (Level)\". Pointed at the replay folder of a game server, the matches show in the site's list of matches (Matches) like any other\n"
        "  --threads N        matches at the same time (default 1)\n"
        "  --out FILE         write the JSON report\n"
        "  --quiet            no line per match\n"
        "  --no-wall-time     leave wall times out of the report (the file is then bit-reproducible)\n"
        "  --tune K=V,...     ablations of the standard bot's plan, for the tournaments (the keys are those of apply_tune in tools/bot_arena_tune.cpp)\n"
        "  --maps-dir DIR     where map names are looked for\n"
        "  --selftest         check the tool itself\n"
        "  --write-baselines  print the pinned reference table of the worker bot (tests/test_ai/baselines.inc) to stdout\n",
        kKindsNote);
}

// "3", "1..8", "1,4,9..12"
bool parse_seeds(const std::string& text, std::vector<uint32_t>& out, std::string& err) {
    std::vector<uint32_t> seeds;
    for (const std::string& part : split(text, ',')) {
        const size_t dots = part.find("..");
        uint64_t a = 0;
        uint64_t b = 0;
        if (dots == std::string::npos) {
            if (!parse_uint(part, a) || a > 0xFFFFFFFFu) { err = "bad seed '" + part + "'"; return false; }
            b = a;
        } else if (!parse_uint(part.substr(0, dots), a) || !parse_uint(part.substr(dots + 2), b) || a > b || b > 0xFFFFFFFFu) {
            err = "bad seed range '" + part + "' (A..B with A <= B)";
            return false;
        }
        if (b - a >= kMaxMatches) { err = "too many seeds in '" + part + "'"; return false; }
        for (uint64_t s = a; s <= b; ++s) seeds.push_back(static_cast<uint32_t>(s));
        if (seeds.size() > kMaxMatches) { err = "too many seeds"; return false; }
    }
    if (seeds.empty()) { err = "no seed given"; return false; }
    out = std::move(seeds);
    return true;
}

// "N=KIND:LEVEL" (also "N:KIND:LEVEL"): the spec of ants::ai::parse_bot_spec with the seat in front. The kind "aggressor" (any case) is the arena's own bench bot
// (tools/bench_aggressor.hpp: it raids and harasses, it is not in the registry): the registry parses it as a worker and the kind is put back afterwards
bool parse_seat(const std::string& text, ai::BotSpec& out, std::string& err) {
    std::string spec = text;
    const size_t eq = spec.find('=');
    if (eq != std::string::npos) spec[eq] = ':';
    bool aggressor = false;
    std::string bench_kind = "aggressor";
    std::vector<std::string> parts = split(spec, ':');
    for (size_t i = 1; i < parts.size(); ++i) {
        const std::string bench = ai::bench::bench_kind_of(parts[i]);                  // aggressor, ..., gang (any case): a bench bot of tools/bench_aggressor.hpp
        if (!bench.empty()) {
            aggressor = true;
            bench_kind = bench;
            parts[i] = "worker";
        } else if (upper(parts[i]).rfind("STANDARD+", 0) == 0) {                      // "standard+K=V,K=V": a standard bot with its own tuning, for the duels of two plans in one match
            aggressor = true;
            bench_kind = "standard" + parts[i].substr(8);
            parts[i] = "worker";
        }
    }
    ai::Style pinned = ai::Style::Random;
    if (aggressor && parts.size() == 4) {                                              // "KIND:LEVEL:STYLE" of a tuned standard bot: the registry's parser would refuse a style for 'worker'
        if (!ai::parse_style(parts[3], pinned)) { err = "unknown bot style '" + parts[3] + "'"; return false; }
        parts.pop_back();
    }
    if (aggressor) {
        spec.clear();
        for (size_t i = 0; i < parts.size(); ++i) spec += (i == 0 ? "" : ":") + parts[i];
    }
    if (!ai::parse_bot_spec(spec, out, err)) return false;
    if (aggressor) out.kind = bench_kind;
    if (pinned != ai::Style::Random) {
        if (!ai::style_allowed(out.level, pinned)) { err = std::string("a ") + ai::level_name(out.level) + " bot cannot play the " + ai::style_name(pinned) + " style"; return false; }
        out.style = pinned;
    }
    return true;
}

// --tune KEY=VALUE,...: the ablations of the standard bot's plan: every standard bot of the run gets the plan of its level with these values put over it.
// Set while the options are parsed, before any match (and thread) starts, and only read afterwards.
std::vector<std::pair<std::string, int64_t>> g_tune;

// BOT_DIAG=1: the standard bots print what their tasks did when the match is over (experiments only)
std::unique_ptr<ai::Bot> diag_wrap(std::unique_ptr<ai::StandardBot> bot, const ai::BotSpec& spec) {
    if (std::getenv("BOT_DIAG") == nullptr) return bot;
    return std::make_unique<ai::bench::DiagBot>(std::move(bot), spec.kind + ":" + ai::level_name(spec.level) + (spec.style != ai::Style::Random ? std::string(":") + ai::style_name(spec.style) : std::string()));
}

// The bots of the registry, and the arena's bench bots
// The tuning keys of a seat: the global --tune first, then the seat's own `standard+K=V,K=V`; false for a key or a value that does not parse
bool tuning_of(const ai::BotSpec& spec, std::vector<std::pair<std::string, int64_t>>& out) {
    out = g_tune;
    if (spec.kind.rfind("standard+", 0) != 0) return true;
    for (const std::string& kv : split(spec.kind.substr(9), ',')) {
        const size_t eq = kv.find('=');
        uint64_t num = 0;
        if (eq == std::string::npos || !parse_uint(kv.substr(eq + 1), num)) return false;
        out.emplace_back(kv.substr(0, eq), static_cast<int64_t>(num));
    }
    return true;
}

void apply_tuning(ai::LevelPlan& plan, const std::vector<std::pair<std::string, int64_t>>& keys) {
    for (const auto& t : keys) {
        std::string err;
        apply_tune(plan, t.first, t.second, err);
    }
}

std::unique_ptr<ai::Bot> arena_factory(const ai::BotSpec& spec) {
    if (spec.kind.rfind("standard+", 0) == 0) {                                      // a tuned standard bot: the tuning of this seat over the global one
        std::vector<std::pair<std::string, int64_t>> keys;
        if (!tuning_of(spec, keys)) return nullptr;
        for (const auto& t : keys) {                                                 // (a key that does not exist refuses the match)
            ai::LevelPlan probe;
            std::string err;
            if (!apply_tune(probe, t.first, t.second, err)) return nullptr;
        }
        bool styled = false;                                                         // `styled=1`: draw the style and the variations like the registry's bot, then put the tuning over them (the duels of two plans)
        for (const auto& t : keys) styled = styled || (t.first == "styled" && t.second != 0);
        if (spec.style == ai::Style::Random && !styled) {                            // no style named: the level's neutral plan, no variations (the ablations of the tournaments)
            ai::LevelPlan plan = ai::plan_for(spec.level);
            apply_tuning(plan, keys);
            return diag_wrap(std::make_unique<ai::StandardBot>(plan), spec);
        }
        return diag_wrap(std::make_unique<ai::StandardBot>(spec.level, spec.style, [keys](ai::LevelPlan& p) { apply_tuning(p, keys); }), spec);
    }
    if (std::unique_ptr<ai::Bot> bench = ai::bench::make_bench_bot(spec.kind, spec.level)) return bench;      // aggressor, aggressor2, rusher, saboteur, aggr1 .. aggr9, gang
    if (spec.kind == "standard" && !g_tune.empty()) {
        std::vector<std::pair<std::string, int64_t>> keys = g_tune;
        return diag_wrap(std::make_unique<ai::StandardBot>(spec.level, spec.style, [keys](ai::LevelPlan& p) { apply_tuning(p, keys); }), spec);
    }
    std::unique_ptr<ai::Bot> made = ai::make_bot(spec);
    if (made != nullptr && spec.kind == "standard" && std::getenv("BOT_DIAG") != nullptr) {                  // (the experiments count what a registry bot does too)
        return diag_wrap(std::unique_ptr<ai::StandardBot>(static_cast<ai::StandardBot*>(made.release())), spec);
    }
    return made;
}

bool parse_args(const std::vector<std::string>& a, Options& o, std::string& err) {
    bool maps_given = false;
    for (size_t i = 0; i < a.size(); ++i) {
        const std::string& s = a[i];
        const auto value = [&](const char* name, std::string& v) {
            if (i + 1 >= a.size()) { err = std::string(name) + " needs a value"; return false; }
            v = a[++i];
            return true;
        };
        std::string v;
        uint64_t n = 0;
        if (s == "--help" || s == "-h") o.help = true;
        else if (s == "--selftest") o.selftest = true;
        else if (s == "--write-baselines") o.write_baselines = true;
        else if (s == "--rotate") o.rotate = true;
        else if (s == "--ally-standard") o.ally_standard = true;
        else if (s == "--ally-pairs") o.ally_pairs = true;
        else if (s == "--replay-check") o.replay_check = true;
        else if (s == "--swimmer-trace") o.swimmer_trace = true;
        else if (s == "--stuck-detail") o.stuck_detail = true;
        else if (s == "--save-replays") {
            if (!value("--save-replays", v)) return false;
            if (v.empty()) { err = "--save-replays needs a folder"; return false; }
            o.save_replays = v;
        }
        else if (s == "--quiet") o.quiet = true;
        else if (s == "--no-wall-time") o.wall_time = false;
        else if (s == "--map") {
            if (!value("--map", v)) return false;
            if (!maps_given) o.maps.clear();
            maps_given = true;
            for (const std::string& m : split(v, ',')) {
                if (m.empty()) { err = "an empty map name in '" + v + "'"; return false; }
                if (upper(m) == "SHIPPED") { for (const char* name : kShippedMaps) o.maps.push_back(name); }
                else o.maps.push_back(m);
            }
        } else if (s == "--seeds") {
            if (!value("--seeds", v) || !parse_seeds(v, o.seeds, err)) return false;
        } else if (s == "--seat") {
            if (!value("--seat", v)) return false;
            ai::BotSpec spec;
            if (!parse_seat(v, spec, err)) { err = "--seat " + v + ": " + err; return false; }
            if (!o.seats_given) o.seats.clear();
            o.seats_given = true;
            for (const ai::BotSpec& other : o.seats) {
                if (other.seat == spec.seat) { err = "--seat: seat " + std::to_string(static_cast<unsigned>(spec.seat)) + " given twice"; return false; }
            }
            o.seats.push_back(spec);
        } else if (s == "--game-mode") {
            if (!value("--game-mode", v)) return false;
            uint8_t mode = 0;
            if (!net::parse_game_mode_name(v, mode)) { err = "--game-mode needs 'highest-score' or '187', not '" + v + "'"; return false; }
            o.game_mode = static_cast<sim::GameMode>(mode);
        } else if (s == "--teams") {
            if (!value("--teams", v)) return false;
            std::string why;
            if (!sim::parse_start_teams(v, o.teams, why)) { err = "--teams " + v + ": " + why; return false; }
        } else if (s == "--ticks") {
            if (!value("--ticks", v)) return false;
            if (v == "full") o.ticks = 0;
            else if (!parse_uint(v, n) || n == 0 || n > 100000000u) { err = "--ticks needs 'full' or a number of ticks, not '" + v + "'"; return false; }
            else o.ticks = n;
        } else if (s == "--latency-ticks") {
            if (!value("--latency-ticks", v)) return false;
            if (!parse_uint(v, n) || n > 600) { err = "--latency-ticks needs a number from 0 to 600, not '" + v + "'"; return false; }
            o.latency = static_cast<uint32_t>(n);
        } else if (s == "--repeat") {
            if (!value("--repeat", v)) return false;
            if (!parse_uint(v, n) || n < 1 || n > 16) { err = "--repeat needs a number from 1 to 16, not '" + v + "'"; return false; }
            o.repeat = static_cast<uint32_t>(n);
        } else if (s == "--threads") {
            if (!value("--threads", v)) return false;
            if (!parse_uint(v, n) || n < 1 || n > 256) { err = "--threads needs a number from 1 to 256, not '" + v + "'"; return false; }
            o.threads = static_cast<unsigned>(n);
        } else if (s == "--out") {
            if (!value("--out", o.out)) return false;
            if (o.out.empty()) { err = "--out needs a file name (an empty one, from an unset shell variable perhaps, is not 'no report': leave --out out for that)"; return false; }
        } else if (s == "--tune") {
            if (!value("--tune", v)) return false;
            g_tune.clear();
            for (const std::string& kv : split(v, ',')) {
                const size_t eq = kv.find('=');
                uint64_t num = 0;
                if (eq == std::string::npos || !parse_uint(kv.substr(eq + 1), num)) { err = "--tune needs KEY=NUMBER,..., not '" + kv + "'"; return false; }
                ai::LevelPlan probe;
                if (!apply_tune(probe, kv.substr(0, eq), static_cast<int64_t>(num), err)) return false;
                g_tune.emplace_back(kv.substr(0, eq), static_cast<int64_t>(num));
            }
        } else if (s == "--maps-dir") {
            if (!value("--maps-dir", o.maps_dir)) return false;
        } else {
            err = "unknown option " + s;
            return false;
        }
    }
    if (!maps_given) o.maps = {"TINY"};
    if (!o.seats_given) {
        o.seats.clear();
        for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
            ai::BotSpec spec;
            spec.seat = seat;
            o.seats.push_back(spec);                           // standard, medium
        }
    }
    std::sort(o.seats.begin(), o.seats.end(), [](const ai::BotSpec& x, const ai::BotSpec& y) { return x.seat < y.seat; });
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------------------------------------------------------------

// Anything with a folder separator or an extension is a path, the rest is a name
bool looks_like_path(const std::string& what) {
    return what.find('/') != std::string::npos || what.find('\\') != std::string::npos || what.find('.') != std::string::npos;
}

// What the report calls a map that was given as a path: the last component without its extension (the folders are separated by '/' or by '\\', whichever system wrote the path;
// the report never holds a folder, found or not)
std::string stem_of_path(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base.erase(dot);
    return base;
}

// A name is looked for in the maps folder (NAME.LVL or NAME.lvl, any case of the extension); anything with a folder separator or an extension is a path
bool resolve_map(const std::string& what, const std::string& dir, fs::path& out) {
    std::error_code ec;
    if (looks_like_path(what)) {
        out = what;
        return fs::is_regular_file(out, ec);
    }
    const fs::path base = dir.empty() ? fs::path(ORIGINAL_ASSETS_DIR) / "Maps" : fs::path(dir);
    for (const std::string& stem : {upper(what), what}) {
        for (const char* ext : {".LVL", ".lvl", ".Lvl"}) {
            const fs::path p = base / (stem + ext);
            if (fs::is_regular_file(p, ec)) {
                out = p;
                return true;
            }
        }
    }
    return false;
}

std::vector<LoadedMap> load_maps(const Options& o) {
    std::vector<LoadedMap> maps(o.maps.size());
    for (size_t i = 0; i < o.maps.size(); ++i) {
        LoadedMap& m = maps[i];
        fs::path p;
        if (!resolve_map(o.maps[i], o.maps_dir, p)) {
            m.name = looks_like_path(o.maps[i]) ? stem_of_path(o.maps[i]) : o.maps[i];           // never the folders of a path that does not exist
            m.error = "map '" + m.name + "' not found";
            continue;
        }
        const std::string stem = stem_of_path(p.string());
        m.name = looks_like_path(o.maps[i]) ? stem : upper(stem);
        if (!m.level.load_from_file(p.string(), nullptr, assets::LevelLimits::for_play_extended())) {
            m.error = "map '" + stem + "' does not load";
            continue;
        }
        m.file_name = p.filename().string();
        if (!net::hash_file(p.string(), m.file_hash)) m.file_name.clear();           // (no hash: the matches of this map are not saved)
        m.ok = true;
    }
    return maps;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Arrangements: every distinct way to put the given bots on the given seats
// ---------------------------------------------------------------------------------------------------------------------------------

std::string spec_text(const ai::BotSpec& s) { return s.kind + ":" + ai::level_name(s.level) + (s.style != ai::Style::Random ? std::string(":") + ai::style_name(s.style) : std::string()); }

// The arrangements in a fixed order: the given one first, then the others in lexicographic order of their texts. Equal bots (same kind and level) are not told apart, so four equal
// bots have one arrangement.
std::vector<std::vector<ai::BotSpec>> arrangements(const std::vector<ai::BotSpec>& given, bool rotate) {
    std::vector<std::vector<ai::BotSpec>> out;
    out.push_back(given);
    if (!rotate || given.size() < 2) return out;
    std::vector<uint8_t> seats;
    std::vector<std::string> texts;
    for (const ai::BotSpec& s : given) {
        seats.push_back(s.seat);
        texts.push_back(spec_text(s));
    }
    std::vector<std::string> sorted_texts = texts;
    std::sort(sorted_texts.begin(), sorted_texts.end());
    std::vector<size_t> by_text;                          // index into `given` of one bot per text
    std::vector<std::string> perm = sorted_texts;
    std::map<std::string, ai::BotSpec> bot_of;
    for (const ai::BotSpec& s : given) bot_of.emplace(spec_text(s), s);
    do {
        if (perm == texts) continue;                      // the given arrangement is first already
        std::vector<ai::BotSpec> a;
        for (size_t k = 0; k < perm.size(); ++k) {
            ai::BotSpec b = bot_of[perm[k]];
            b.seat = seats[k];
            a.push_back(b);
        }
        out.push_back(std::move(a));
    } while (std::next_permutation(perm.begin(), perm.end()));
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Matches
// ---------------------------------------------------------------------------------------------------------------------------------

// Two plays of one match are the same match: every number of the result is equal (the log is not compared: only the first play records one)
bool same_match(const ai::ArenaResult& a, const ai::ArenaResult& b) {
    if (a.error != b.error || a.ticks != b.ticks || a.steps != b.steps || a.hash != b.hash || a.match_over != b.match_over || a.initial_ticks != b.initial_ticks ||
        a.checkpoints != b.checkpoints || a.seats.size() != b.seats.size() || a.news_events != b.news_events || a.audio_events != b.audio_events || a.peak_queue != b.peak_queue ||
        a.reachable_units_left != b.reachable_units_left || a.last_food != b.last_food || a.landings != b.landings || a.landed != b.landed) {
        return false;
    }
    for (size_t i = 0; i < a.seats.size(); ++i) {
        const ai::ArenaSeatResult& x = a.seats[i];
        const ai::ArenaSeatResult& y = b.seats[i];
        if (x.spec.seat != y.spec.seat || x.runs != y.runs || x.score != y.score || x.shown_score != y.shown_score || x.ants != y.ants || x.eggs != y.eggs || x.hatched != y.hatched ||
            x.banked != y.banked || x.raided != y.raided || x.kills != y.kills || x.losses != y.losses || x.stats.decisions != y.stats.decisions || x.stats.intents != y.stats.intents ||
            x.stats.released != y.stats.released || x.stats.expired != y.stats.expired || x.stats.pruned != y.stats.pruned || x.stats.superseded != y.stats.superseded ||
            x.stats.filtered != y.stats.filtered || x.stats.rejected != y.stats.rejected || x.stalls != y.stalls || x.cantgo != y.cantgo || x.cantgo_began != y.cantgo_began ||
            x.orders != y.orders || x.attack_orders != y.attack_orders || x.bombs_planted != y.bombs_planted || x.fires_lit != y.fires_lit || x.war_ticks != y.war_ticks || x.fire_hunts != y.fire_hunts || x.mines_planted != y.mines_planted || x.ramp_unjams != y.ramp_unjams || x.rushes != y.rushes || x.rush_ticks != y.rush_ticks || x.rush_attacks != y.rush_attacks || x.refused_orders != y.refused_orders || x.refused_again != y.refused_again || x.took != y.took || x.took_at_flowers != y.took_at_flowers || x.ant_ticks != y.ant_ticks || x.idle_ticks != y.idle_ticks || x.after_ant_ticks != y.after_ant_ticks || x.after_idle_ticks != y.after_idle_ticks || x.raids != y.raids || x.raid_points != y.raid_points || x.steals != y.steals || x.steal_points != y.steal_points || x.stuck_ants != y.stuck_ants || x.stuck_ticks != y.stuck_ticks || x.stuck_looping != y.stuck_looping || x.stuck_blocked != y.stuck_blocked || x.stuck_carriers != y.stuck_carriers || !(x.expedition == y.expedition) || !(x.miner_flight == y.miner_flight)) {
            return false;
        }
    }
    return true;
}

template <class T>
void release_memory(std::vector<T>& v) {
    std::vector<T>().swap(v);
}

// The factory of a match with --ally-standard: the standard bot of the lower seat of the first two is wrapped in an inviter for the other
std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)> ally_factory(const Job& j) {
    int first = -1;
    int second = -1;
    for (const ai::BotSpec& b : j.bots) {
        if (b.kind != "standard") continue;
        if (first < 0 || b.seat < first) {
            second = first;
            first = b.seat;
        } else if (second < 0 || b.seat < second) {
            second = b.seat;
        }
    }
    if (first < 0 || second < 0) return arena_factory;
    if (first > second) std::swap(first, second);
    return [first, second](const ai::BotSpec& spec) -> std::unique_ptr<ai::Bot> {
        std::unique_ptr<ai::Bot> bot = arena_factory(spec);
        if (bot != nullptr && spec.seat == first) return std::make_unique<ai::bench::InviterBot>(std::move(bot), static_cast<uint8_t>(second));
        return bot;
    };
}

// The factory of a match with --ally-pairs: of every two seats with the same standard spec (kind and level) the lower one invites the other
std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)> ally_pairs_factory(const Job& j) {
    std::map<std::string, std::vector<uint8_t>> by_spec;
    for (const ai::BotSpec& b : j.bots) {
        if (b.kind.rfind("standard", 0) == 0) by_spec[spec_text(b)].push_back(b.seat);
    }
    std::map<uint8_t, uint8_t> inviter_of;                     // inviting seat -> invited seat
    for (auto& entry : by_spec) {
        if (entry.second.size() != 2) continue;
        std::sort(entry.second.begin(), entry.second.end());
        inviter_of[entry.second[0]] = entry.second[1];
    }
    return [inviter_of](const ai::BotSpec& spec) -> std::unique_ptr<ai::Bot> {
        std::unique_ptr<ai::Bot> bot = arena_factory(spec);
        const auto it = inviter_of.find(spec.seat);
        if (bot != nullptr && it != inviter_of.end()) return std::make_unique<ai::bench::InviterBot>(std::move(bot), it->second);
        return bot;
    };
}

ai::ArenaSpec spec_of(const Options& o, const LoadedMap& m, const Job& j, bool record) {
    ai::ArenaSpec s;
    s.level = &m.level;
    s.seed = j.seed;
    s.bots = j.bots;
    s.max_ticks = o.ticks;
    s.latency_ticks = o.latency;
    s.game_mode = o.game_mode;
    s.teams = o.teams;
    s.record = record;
    s.extra_kinds = {"aggressor", "aggressor2", "saboteur", "rusher", "gang", "gang2", "turtle", "aggr1", "aggr2", "aggr3", "aggr4", "aggr5", "aggr6", "aggr7", "aggr8", "aggr9"};
    for (const ai::BotSpec& b : j.bots) {
        if (b.kind.rfind("standard+", 0) == 0) s.extra_kinds.push_back(b.kind);
    }
    return s;
}

// --save-replays: the matches that were played are kept as replay files through the game server's own store (ants_server::ReplayStore: the names, the age and size limits, the way a file is written
// whole), so that a folder that a server reads shows them in its list like the matches of its rooms. The store is single threaded: one lock. A file is the match played again into a fresh engine
// with a recorder (the bots' commands as the arena applied them, at the step they were applied), which is also the proof that the match is nothing but its commands.
struct ReplaySink {
    std::mutex lock;
    std::unique_ptr<server::ReplayStore> store;
    std::vector<std::string> saved;
    std::vector<std::string> notes;
    size_t too_short{0};                           // matches that ran under server::kReplayMinTurns (the server keeps none of those either: the site's list is of matches of 30 seconds or more)
};
ReplaySink* g_replay_sink = nullptr;

void save_replay(const LoadedMap& m, const Job& job, const ai::ArenaSpec& spec, const ai::ArenaResult& played) {
    if (g_replay_sink == nullptr || !played.error.empty() || m.file_name.empty() || !net::valid_map_name(m.file_name)) return;
    if (played.steps < server::kReplayMinTurns) {
        std::lock_guard<std::mutex> guard(g_replay_sink->lock);
        ++g_replay_sink->too_short;
        return;
    }
    replay::Header head;
    head.game_version = std::string(ants::VERSION_STRING);
    head.build_id = std::string(ants::BUILD_ID).substr(0, replay::kMaxTextBytes);
    head.venue = "bot arena";
    head.map_name = m.file_name;
    head.map_hash = m.file_hash;
    head.seed = spec.seed;
    head.fog = false;
    head.set_mode(static_cast<uint8_t>(spec.game_mode));
    head.teams = spec.teams;
    for (const ai::BotSpec& b : job.bots) {
        if (b.seat >= sim::MAX_PLAYERS) return;
        head.roster = static_cast<uint8_t>(head.roster | (1u << b.seat));
        head.names[b.seat] = ai::bot_display_name(b);          // ("Bot (Medium)": the name the rooms of the server show)
    }
    sim::SimulationEngine sim;
    sim.set_fog_of_war_enabled(false);
    sim.set_game_mode(spec.game_mode);
    sim.init(m.level, spec.seed, head.roster);
    sim::apply_start_teams(sim, spec.teams);
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) sim.set_player_name(seat, ((head.roster >> seat) & 1u) != 0 ? head.names[seat] : std::string());
    replay::Recorder rec(head);
    size_t next = 0;
    const auto give = [&](uint64_t step) {
        for (; next < played.log.size() && played.log[next].step == step; ++next) {
            rec.on_command(played.log[next].command);
            sim.apply_command(played.log[next].command);
        }
    };
    give(0);
    for (uint64_t step = 1; step <= played.steps; ++step) {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
        rec.on_tick(sim);
        give(step);
    }
    std::string error;
    const std::vector<uint8_t> bytes = rec.finish(sim, error);
    std::lock_guard<std::mutex> guard(g_replay_sink->lock);
    if (bytes.empty()) {
        g_replay_sink->notes.push_back("not saved: " + (error.empty() ? std::string("nothing to write") : error));
        return;
    }
    const server::ReplaySave result = g_replay_sink->store->save(bytes);
    if (result.kept) g_replay_sink->saved.push_back(result.file);
    else g_replay_sink->notes.push_back("not saved: " + result.note);
}

// --swimmer-trace: what the seats' Swimmers did, and where their ants stood (read from the engine every 10th tick; nothing is sent and the match is the same). A Swimmer counts while it lives.
// The idle count is the ants of a seat that stand still (state Idle) from the third minute on; `off_strip` leaves out those within two tiles of a flower's drop tile (the ants that wait for a drop)
struct SwimmerTrace {
    static constexpr size_t kMinutes = 12;
    static constexpr uint64_t kIdleFrom = 2400;               // ticks: the first two minutes are the opening
    std::array<std::array<uint64_t, 9>, 4> reached{};        // seat -> tick at which it first had n Swimmers alive (index n, 1..8; 0 = never)
    std::array<uint32_t, 4> most{};
    std::array<std::array<int32_t, kMinutes>, 4> score{};    // seat -> score at the end of each minute
    std::array<std::array<uint32_t, kMinutes>, 4> alive{};   // seat -> Swimmers alive at the end of each minute
    std::array<uint64_t, 4> idle{};                          // seat -> ant-looks (10 ticks each) of ants that stood idle from kIdleFrom on
    std::array<uint64_t, 4> idle_off_strip{};                // ... of them, not near a drop tile
    std::array<uint64_t, 4> ants{};                          // seat -> all ant-looks from kIdleFrom on
    std::array<std::array<uint32_t, kMinutes>, 2> left{};    // [0] power-ups on the ground at the end of each minute (the flowers' drops left out), [1] of them Swimmers
    std::array<uint32_t, 4> bridged{};                       // seat -> water tiles that its Swimmers finished as bridges (a tile that is dug again after it went counts again)
    std::vector<uint8_t> standing_;                          // the finished bridge tiles at the last look (index y * width + x)
    void look(const sim::SimulationEngine& sim) {
        const uint64_t tick = sim.current_tick();
        if (tick % 10 != 0) return;
        const std::vector<sim::TileCell>& cells = sim.grid().cells();
        const bool first = standing_.size() != cells.size();                                 // (the bridge pieces that the map file holds are there at the first look: nobody finished them)
        if (first) standing_.assign(cells.size(), 0);
        for (size_t i = 0; i < cells.size(); ++i) {
            const bool done = cells[i].has_completed_bridge();
            if (!first && done && standing_[i] == 0 && cells[i].interactive_owner < 4) ++bridged[cells[i].interactive_owner];
            standing_[i] = done ? 1 : 0;
        }
        std::array<uint32_t, 4> n{};
        const sim::WorldState& ws = sim.get_world_state();
        for (const sim::AntSnapshot& a : ws.ants) {
            if (a.player_id >= 4 || a.hp <= 0 || a.state == sim::UnitState::Dead || a.state == sim::UnitState::Drowning) continue;
            if (a.raw_type == sim::AntType::Swimmer) ++n[a.player_id];
            if (tick < kIdleFrom) continue;
            ++ants[a.player_id];
            if (a.state != sim::UnitState::Idle) continue;
            ++idle[a.player_id];
            bool near_drop = false;
            for (const sim::FlowerDropperSnapshot& d : ws.flower_droppers) near_drop = near_drop || std::max(std::abs(a.tile_x - d.drop_x), std::abs(a.tile_y - d.drop_y)) <= 2;
            if (!near_drop) ++idle_off_strip[a.player_id];
        }
        for (size_t t = 0; t < 4; ++t) {
            most[t] = std::max(most[t], n[t]);
            for (size_t k = 1; k <= 8 && k <= n[t]; ++k) {
                if (reached[t][k] == 0) reached[t][k] = tick;
            }
            if (tick % 1200 == 0 && tick / 1200 >= 1 && tick / 1200 <= kMinutes) {
                score[t][tick / 1200 - 1] = sim.get_player_score(static_cast<uint8_t>(t));
                alive[t][tick / 1200 - 1] = n[t];
            }
        }
        if (tick % 1200 == 0 && tick / 1200 >= 1 && tick / 1200 <= kMinutes) {
            const sim::Grid& grid = sim.grid();
            uint32_t all = 0;
            uint32_t swimmers = 0;
            for (int32_t y = 0; y < static_cast<int32_t>(grid.height()); ++y) {
                for (int32_t x = 0; x < static_cast<int32_t>(grid.width()); ++x) {
                    const sim::TileCoord t{x, y};
                    if (!grid.has_powerup_at(t)) continue;
                    bool flower = false;
                    for (const sim::FlowerDropperSnapshot& d : ws.flower_droppers) flower = flower || (d.drop_x == x && d.drop_y == y);
                    if (flower) continue;
                    ++all;
                    swimmers += grid.get_powerup_type(t) == static_cast<uint8_t>(sim::AntType::Swimmer) ? 1u : 0u;
                }
            }
            left[0][tick / 1200 - 1] = all;
            left[1][tick / 1200 - 1] = swimmers;
        }
    }
};

void print_swimmer_trace(const LoadedMap& m, const Job& job, const SwimmerTrace& t, const ai::ArenaResult& result) {
    for (const ai::BotSpec& b : job.bots) {
        if (b.seat >= 4) continue;
        std::string line = fmt("SWT %s seed=%u rot=%u seat=%u kind=%s sw3=%llu sw5=%llu sw8=%llu most=%u alive=", m.name.c_str(), job.seed, job.rotation, static_cast<unsigned>(b.seat), b.kind.c_str(),
                               static_cast<unsigned long long>(t.reached[b.seat][3]), static_cast<unsigned long long>(t.reached[b.seat][5]), static_cast<unsigned long long>(t.reached[b.seat][8]), t.most[b.seat]);
        for (size_t k = 0; k < SwimmerTrace::kMinutes; ++k) line += fmt("%s%u", k == 0 ? "" : ",", t.alive[b.seat][k]);
        line += " score=";
        for (size_t k = 0; k < SwimmerTrace::kMinutes; ++k) line += fmt("%s%d", k == 0 ? "" : ",", t.score[b.seat][k]);
        line += fmt(" idle=%llu idle_off=%llu ants=%llu bridges=%u", static_cast<unsigned long long>(t.idle[b.seat]), static_cast<unsigned long long>(t.idle_off_strip[b.seat]), static_cast<unsigned long long>(t.ants[b.seat]), t.bridged[b.seat]);
        for (const ai::ArenaSeatResult& s : result.seats) {
            if (s.spec.seat != b.seat) continue;
            line += " took=";
            for (size_t k = 1; k < s.took.size(); ++k) line += fmt("%s%u", k == 1 ? "" : ",", s.took[k]);   // Bomber, Fire, Thief, Combat, Swimmer
        }
        std::fprintf(g_out, "%s\n", line.c_str());
    }
    std::string line = fmt("SWL %s seed=%u rot=%u left=", m.name.c_str(), job.seed, job.rotation);
    for (size_t k = 0; k < SwimmerTrace::kMinutes; ++k) line += fmt("%s%u", k == 0 ? "" : ",", t.left[0][k]);
    line += " left_swimmers=";
    for (size_t k = 0; k < SwimmerTrace::kMinutes; ++k) line += fmt("%s%u", k == 0 ? "" : ",", t.left[1][k]);
    std::fprintf(g_out, "%s\n", line.c_str());
}

// --stuck-detail: the spans of the stuck tally (ArenaResult::stuck), one line each
void print_stuck_detail(const LoadedMap& m, const Job& job, const ai::ArenaResult& result) {
    static const char* const kType[] = {"Worker", "Bomber", "Fire", "Thief", "Combat", "Swimmer"};
    static const char* const kOrder[] = {"none", "move", "home", "attack", "powerup", "harvest", "ignite", "extinguish", "plant", "defuse", "bomb", "raid", "?", "bridge", "demolish"};
    for (const ai::StuckTally::Episode& e : result.stuck) {
        const size_t type = static_cast<size_t>(e.type);
        std::fprintf(g_out, "STUCK %s seed %u rot %u t=%llu seat %u ant %u %s at (%d,%d) state %d order %s (%d,%d) hold %d carried %d\n", m.name.c_str(), job.seed, job.rotation,
                     static_cast<unsigned long long>(e.tick), static_cast<unsigned>(e.seat), e.ant, type < 6 ? kType[type] : "?", e.x, e.y, static_cast<int>(e.state),
                     e.order < 15 ? kOrder[e.order] : "?", e.order_x, e.order_y, e.holding ? 1 : 0, e.carried);
    }
}

MatchReport run_job(const Options& o, const std::vector<LoadedMap>& maps, const Job& job, std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)> factory) {
    MatchReport r;
    r.job = job;
    const LoadedMap& m = maps[job.map];
    if (!m.ok) {
        r.result.error = m.error;
        return r;
    }
    const Clock::time_point started = Clock::now();
    ai::ArenaSpec spec = spec_of(o, m, job, o.replay_check || g_replay_sink != nullptr);
    spec.factory = factory ? factory : o.ally_pairs ? ally_pairs_factory(job) : o.ally_standard ? ally_factory(job) : std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>(arena_factory);
    SwimmerTrace trace;
    if (o.swimmer_trace) spec.on_tick = [&trace](const sim::SimulationEngine& sim) { trace.look(sim); };
    r.result = ai::play_match(spec);
    if (o.swimmer_trace && r.result.error.empty()) print_swimmer_trace(m, job, trace, r.result);
    if (o.stuck_detail && r.result.error.empty()) print_stuck_detail(m, job, r.result);
    r.plays = 1;
    if (r.result.error.empty() && o.replay_check) {
        r.replay_checked = true;
        r.replay = ai::replay_commands(spec, r.result);
    }
    if (r.result.error.empty()) save_replay(m, job, spec, r.result);
    for (uint32_t again = 1; again < o.repeat && r.result.error.empty(); ++again) {
        ai::ArenaSpec s2 = spec_of(o, m, job, false);
        s2.factory = spec.factory;
        const ai::ArenaResult second = ai::play_match(s2);
        ++r.plays;
        if (!same_match(r.result, second)) {
            r.repeat_ok = false;
            r.repeat_note = fmt("play %u differs: ticks %llu / %llu, hash %s / %s", again + 1, static_cast<unsigned long long>(r.result.ticks), static_cast<unsigned long long>(second.ticks),
                                hex64(r.result.hash).c_str(), hex64(second.hash).c_str());
        }
    }
    // The report needs the NUMBER of commands, not the commands, and the checkpoints only served the replay and repeat checks: a whole run keeps one MatchReport per match, and with
    // the real bots a log is hundreds of kilobytes
    r.commands = r.result.log.size();
    release_memory(r.result.log);
    release_memory(r.result.checkpoints);
    r.wall_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return r;
}

// The matches of a run: maps x seeds x arrangements. The count is checked BEFORE a single Job is built (overflow-safe): a Job costs about 260 bytes, so a request for tens of millions
// has to be refused, not tried. `error` says why not.
bool make_jobs(const Options& o, size_t map_count, std::vector<Job>& jobs, std::string& error) {
    jobs.clear();
    const std::vector<std::vector<ai::BotSpec>> arr = arrangements(o.seats, o.rotate);
    uint64_t total = map_count;
    for (const uint64_t factor : {static_cast<uint64_t>(o.seeds.size()), static_cast<uint64_t>(arr.size()), static_cast<uint64_t>(o.repeat)}) {
        if (factor != 0 && total > kMaxMatches / factor) {
            error = fmt("%zu map(s) x %zu seed(s) x %zu arrangement(s) x %u repeat(s) are too many matches (at most %zu)", map_count, o.seeds.size(), arr.size(), o.repeat, kMaxMatches);
            return false;
        }
        total *= factor;
    }
    jobs.reserve(static_cast<size_t>(total / std::max<uint32_t>(1, o.repeat)));
    for (size_t m = 0; m < map_count; ++m) {
        for (uint32_t seed : o.seeds) {
            for (size_t r = 0; r < arr.size(); ++r) {
                Job j;
                j.map = m;
                j.seed = seed;
                j.rotation = static_cast<uint32_t>(r);
                j.bots = arr[r];
                jobs.push_back(std::move(j));
            }
        }
    }
    return true;
}

// Plays every job, `threads` at a time; the reports come back in the order of the jobs (map, seed, arrangement)
std::vector<MatchReport> run_jobs(const Options& o, const std::vector<LoadedMap>& maps, const std::vector<Job>& jobs,
                                  const std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>& factory, bool progress) {
    std::vector<MatchReport> reports(jobs.size());
    std::atomic<size_t> next{0};
    std::atomic<size_t> printed{0};
    const auto work = [&]() {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= jobs.size()) return;
            reports[i] = run_job(o, maps, jobs[i], factory);
            if (progress) {
                const size_t done = printed.fetch_add(1) + 1;
                if (done % 50 == 0 || done == jobs.size()) {
                    std::fprintf(stderr, "  %zu of %zu matches played\n", done, jobs.size());
                }
            }
        }
    };
    const unsigned n = std::max(1u, std::min<unsigned>(o.threads, static_cast<unsigned>(std::max<size_t>(1, jobs.size()))));
    if (n == 1) {
        work();
    } else {
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < n; ++t) pool.emplace_back(work);
        for (std::thread& t : pool) t.join();
    }
    return reports;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------------------------------------------------------------

std::string seeds_text(const std::vector<uint32_t>& seeds) {
    std::string out;
    for (size_t i = 0; i < seeds.size();) {
        size_t j = i;
        while (j + 1 < seeds.size() && static_cast<uint64_t>(seeds[j + 1]) == static_cast<uint64_t>(seeds[j]) + 1) ++j;       // in 64 bits: 4294967295 is followed by 0 without being "the next"
        if (!out.empty()) out += ",";
        out += j > i ? fmt("%u..%u", seeds[i], seeds[j]) : fmt("%u", seeds[i]);
        i = j + 1;
    }
    return out;
}

// The names of the power-up kinds in the report, by sim::AntType (1 Bomber .. 5 Swimmer)
const char* kind_name(size_t kind) {
    static const char* const kNames[6] = {"worker", "bomber", "fire", "thief", "combat", "swimmer"};
    return kind < 6 ? kNames[kind] : "?";
}

// Writes the report to `out` one match at a time (nothing is built as one big string); false when the stream is not good afterwards
bool write_report(std::ostream& out, const Options& o, const std::vector<LoadedMap>& maps, const std::vector<MatchReport>& reports) {
    JsonWriter j(&out);
    j.begin_object();
    j.field("tool", "bot_arena");
    j.field("format", uint64_t{8});                                   // 2: banked and raided replace the engine's food_deposited, food_stolen and food_lost (never fed, always 0); 3: the can't-go counts per seat; 4: the flowers (landings per match, the power-ups taken per seat); 5: the expedition per seat; 6: the Bomber flight to the ring pile per seat; 7: the idle tally per seat, last_food and reachable_units_left per match; 8: the stuck tally per seat and the steals completed (idle.steals, idle.steal_points)
    j.field("note", kKindsNote);
    j.key("options");
    j.begin_object();
    j.key("maps");
    j.begin_array();
    for (const LoadedMap& m : maps) j.value(m.name);
    j.end_array();
    j.field("seeds", seeds_text(o.seeds));
    j.field("ticks", o.ticks == 0 ? std::string("full") : std::to_string(o.ticks));
    j.field("latency_ticks", uint64_t{o.latency});
    j.field_bool("rotate", o.rotate);
    j.field("repeat", uint64_t{o.repeat});
    j.field_bool("replay_check", o.replay_check);
    if (o.teams.set) j.field("teams", sim::start_teams_text(o.teams));                                                       // (only when teams were asked for: a report of free for all is the report of before)
    if (o.game_mode != sim::GameMode::HighestScore) j.field("game_mode", sim::game_mode_name(o.game_mode));      // (only when it is not the original's game: a report of the original rules is the report of before)
    j.end_object();
    j.key("matches");
    j.begin_array();
    size_t failures = 0;
    uint64_t ticks = 0;
    for (const MatchReport& r : reports) {
        if (!r.ok()) ++failures;
        ticks += r.result.ticks;
        j.begin_object();
        j.field("map", maps[r.job.map].name);
        j.field("seed", uint64_t{r.job.seed});
        j.field("rotation", uint64_t{r.job.rotation});
        j.field_bool("ok", r.ok());
        if (!r.result.error.empty()) {
            j.field("error", r.result.error);
            j.end_object();
            j.drain();
            continue;
        }
        j.field("ticks", r.result.ticks);
        j.field("match_ticks", r.result.initial_ticks);
        j.field_bool("match_over", r.result.match_over);
        j.field("hash", hex64(r.result.hash));
        j.field("reachable_units_left", uint64_t{r.result.reachable_units_left});            // the units left on the piles that some hill walks to
        j.field("last_food", r.result.last_food);                           // the last look at which the food on the map fell (0: never)
        j.field("landings", uint64_t{r.result.landings});
        j.key("landed");                                                    // by the kind of ant that the landing makes
        j.begin_object();
        for (size_t k = 1; k < r.result.landed.size(); ++k) j.field(kind_name(k), uint64_t{r.result.landed[k]});
        j.end_object();
        j.key("drops");                                                     // the power-ups that dead typed ants left: owner seat, kind, tile, tick seen, tick taken (0: never), seat of the taker (255: none seen)
        j.begin_array();
        for (const ai::DropTally::Drop& d : r.result.drops) {
            j.begin_object();
            j.field("owner", uint64_t{d.owner});
            j.field("kind", kind_name(d.kind));
            j.field_signed("x", d.x);
            j.field_signed("y", d.y);
            j.field("tick", d.tick);
            j.field("taken", d.taken);
            j.field("taker", uint64_t{d.taker});
            j.field("others", uint64_t{d.others});
            j.field_signed("hill_dist", d.hill_dist);
            j.field("nearest", uint64_t{d.nearest});
            j.end_object();
        }
        j.end_array();
        j.key("seats");
        j.begin_array();
        for (const ai::ArenaSeatResult& s : r.result.seats) {
            j.begin_object();
            j.field("seat", uint64_t{s.spec.seat});
            j.field("bot", spec_text(s.spec));
            j.field("runs", s.runs);
            j.field("style", s.style);
            j.field_signed("score", s.score);
            j.field_signed("shown_score", s.shown_score);
            j.field("ants", uint64_t{s.ants});
            j.field("eggs", uint64_t{s.eggs});
            j.field("hatched", uint64_t{s.hatched});
            j.field("banked", uint64_t{s.banked});
            j.field("raided", uint64_t{s.raided});
            j.field("kills", uint64_t{s.kills});
            j.field("losses", uint64_t{s.losses});
            j.field("stalls", uint64_t{s.stalls});
            j.field("orders", uint64_t{s.orders});
            j.field("attack_orders", uint64_t{s.attack_orders});
            j.field("bombs_planted", uint64_t{s.bombs_planted});
            j.field("fires_lit", uint64_t{s.fires_lit});
            j.key("war_ticks");                                             // the ticks a standard bot spent at every war tier (0: not behind enough .. 3)
            j.begin_object();
            for (size_t k = 0; k < s.war_ticks.size(); ++k) j.field("t" + std::to_string(k), uint64_t{s.war_ticks[k]});
            j.end_object();
            j.key("stand");                                                 // the stand batch (docs/BOTS.md): hunts of the Fire Ant that fires the gate, mines laid, ants sent off the ramp, rushes begun, their ticks and attack orders
            j.begin_object();
            j.field("fire_hunts", uint64_t{s.fire_hunts});
            j.field("mines_planted", uint64_t{s.mines_planted});
            j.field("ramp_unjams", uint64_t{s.ramp_unjams});
            j.field("rushes", uint64_t{s.rushes});
            j.field("rush_ticks", uint64_t{s.rush_ticks});
            j.field("rush_attacks", uint64_t{s.rush_attacks});
            j.end_object();
            j.field("refused_orders", uint64_t{s.refused_orders});
            j.field("refused_again", uint64_t{s.refused_again});
            j.field("cantgo", uint64_t{s.cantgo});
            j.field("cantgo_began", uint64_t{s.cantgo_began});
            j.key("took");                                                  // the power-ups the seat's ants took, by the kind of ant they became
            j.begin_object();
            for (size_t k = 1; k < s.took.size(); ++k) j.field(kind_name(k), uint64_t{s.took[k]});
            j.end_object();
            j.field("took_at_flowers", uint64_t{s.took_at_flowers});
            j.key("idle");                                                  // the idle tally (docs/BOT_TACTICS.md, "When the food is gone"): ant-ticks, the idle ones, the same two from the last food taken to the end, raids begun and the points they took
            j.begin_object();
            j.field("ant_ticks", s.ant_ticks);
            j.field("idle_ticks", s.idle_ticks);
            j.field("after_ant_ticks", s.after_ant_ticks);
            j.field("after_idle_ticks", s.after_idle_ticks);
            j.field("raids", uint64_t{s.raids});
            j.field("raid_points", uint64_t{s.raid_points});
            j.field("steals", uint64_t{s.steals});
            j.field("steal_points", uint64_t{s.steal_points});
            j.end_object();
            j.key("stuck");                                                 // the stuck tally (docs/BOT_UNSTUCK.md): the ants that stood nowhere for 10 seconds, the ant-ticks past them, and the spans by what the ant was doing (looping in the can't-go clip, blocked with an order, a carrier with no order)
            j.begin_object();
            j.field("ants", uint64_t{s.stuck_ants});
            j.field("ticks", s.stuck_ticks);
            j.field("looping", uint64_t{s.stuck_looping});
            j.field("blocked", uint64_t{s.stuck_blocked});
            j.field("carriers", uint64_t{s.stuck_carriers});
            j.end_object();
            j.key("expedition");                                            // the flights over water of a standard bot (all 0 where it made none); the ticks are 0 for what never happened
            j.begin_object();
            j.field("planned", uint64_t{s.expedition.planned});
            j.field("given_up", uint64_t{s.expedition.given_up});
            j.field("planted", uint64_t{s.expedition.planted});
            j.field("hops", uint64_t{s.expedition.hops});
            j.field("landings", uint64_t{s.expedition.landings});
            j.field("duds", uint64_t{s.expedition.duds});
            j.field("taken", uint64_t{s.expedition.taken});
            j.field("swimmers_taken", uint64_t{s.expedition.swimmers_taken});
            j.field("first_plant", s.expedition.first_plant);
            j.field("first_landing", s.expedition.first_landing);
            j.field("first_swimmer", s.expedition.first_swimmer);
            j.end_object();
            j.key("miner_flight");                                          // the Bomber that flies to the island of the ring pile (all 0 where none flew)
            j.begin_object();
            j.field("started", uint64_t{s.miner_flight.started});
            j.field("planted", uint64_t{s.miner_flight.planted});
            j.field("hops", uint64_t{s.miner_flight.hops});
            j.field("landed", uint64_t{s.miner_flight.landed});
            j.field("gave_up", uint64_t{s.miner_flight.gave_up});
            j.field("first_start", s.miner_flight.first_start);
            j.field("first_landing", s.miner_flight.first_landing);
            j.end_object();
            j.field("decisions", uint64_t{s.stats.decisions});
            j.field("intents", uint64_t{s.stats.intents});
            j.field("released", uint64_t{s.stats.released});
            j.field("expired", uint64_t{s.stats.expired});
            j.field("pruned", uint64_t{s.stats.pruned});
            j.field("superseded", uint64_t{s.stats.superseded});
            j.field("filtered", uint64_t{s.stats.filtered});
            j.field("rejected", uint64_t{s.stats.rejected});
            j.field("commands_per_second", static_cast<double>(s.milli_commands_per_second(r.result.ticks)) / 1000.0, 3);
            j.end_object();
        }
        j.end_array();
        if (r.replay_checked) {
            j.key("replay");
            j.begin_object();
            j.field_bool("ok", r.replay.ok);
            j.field("commands", r.commands);
            if (!r.replay.ok) j.field("first_bad_tick", r.replay.first_bad_tick);
            j.end_object();
        }
        if (r.plays > 1) {
            j.key("repeat");
            j.begin_object();
            j.field("plays", uint64_t{r.plays});
            j.field_bool("identical", r.repeat_ok);
            if (!r.repeat_ok) j.field("note", r.repeat_note);
            j.end_object();
        }
        if (o.wall_time) j.field("wall_ms", r.wall_ms, 1);
        j.end_object();
        j.drain();
    }
    j.end_array();
    j.key("summary");
    j.begin_object();
    j.field("matches", uint64_t{reports.size()});
    j.field("failures", uint64_t{failures});
    j.field("ticks", ticks);
    j.end_object();
    j.end_object();
    j.drain();
    out << '\n';
    out.flush();
    return out.good();
}

std::string seats_line(const MatchReport& r) {
    std::string s;
    for (const ai::ArenaSeatResult& seat : r.result.seats) {
        s += fmt(" [%u %s %d]", static_cast<unsigned>(seat.spec.seat), spec_text(seat.spec).c_str(), seat.score);
    }
    return s;
}

// Where the tool prints its lines and its complaints (stdout and stderr; the self-test points them at scratch files while it runs the tool end to end)
std::FILE* g_out = stdout;
std::FILE* g_err = stderr;

void print_match(const std::vector<LoadedMap>& maps, const MatchReport& r) {
    if (!r.result.error.empty()) {
        std::fprintf(g_out, "%-9s seed %u #%u: NOT PLAYED: %s\n", maps[r.job.map].name.c_str(), r.job.seed, r.job.rotation, r.result.error.c_str());
        return;
    }
    std::fprintf(g_out, "%-9s seed %u #%u:%s  ticks %llu%s  hash %s  %s%s\n", maps[r.job.map].name.c_str(), r.job.seed, r.job.rotation, seats_line(r).c_str(),
                 static_cast<unsigned long long>(r.result.ticks), r.result.match_over ? " (over)" : "", hex64(r.result.hash).c_str(),
                 r.replay_checked ? (r.replay.ok ? "replay ok" : "REPLAY DIFFERS") : "", r.repeat_ok ? "" : "  REPEAT DIFFERS");
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The tool
// ---------------------------------------------------------------------------------------------------------------------------------

// `factory` (null: the registry's bots) lets the self-test seat bots that the registry does not have
int run_tool(const Options& o, const std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>& factory = nullptr) {
    // The report file is opened BEFORE the first match: a path that cannot be written is found in a moment, not after a run of hours
    std::vector<std::string> played = o.maps;       // (the owner's rule: nothing into the map library, nor over a map that is played, whether it was given as a path or as a name that is looked up; both options are asked before one file is opened)
    for (const std::string& what : o.maps) { fs::path p; if (resolve_map(what, o.maps_dir, p)) played.push_back(p.string()); }
    if (library_guard::refused("bot_arena", "--out", o.out, played, g_err) || library_guard::refused("bot_arena", "--save-replays", o.save_replays, played, g_err)) return 2;
    std::ofstream report;
    if (!o.out.empty()) {
        report.open(o.out, std::ios::binary | std::ios::trunc);
        if (!report) {
            std::fprintf(g_err, "bot_arena: cannot write %s\n", o.out.c_str());
            return 2;
        }
    }
    std::vector<LoadedMap> maps = load_maps(o);
    std::vector<Job> jobs;
    std::string why;
    if (!make_jobs(o, maps.size(), jobs, why)) {
        std::fprintf(g_err, "bot_arena: %s\n", why.c_str());
        return 2;
    }
    ReplaySink sink;
    if (!o.save_replays.empty()) {                    // (opened before the first match, like the report file: a folder that cannot be used is found in a moment)
        server::ReplayConfig rc;
        rc.dir = o.save_replays;
        rc.keep_days = 3650;                          // (the arena's store never deletes: the folder may be the server's, whose own limits are its own to apply)
        rc.max_bytes = 4096ull * 1024ull * 1024ull;
        rc.max_saves_per_hour = 1000000;              // (a run of many matches is one person's doing, not a flood of the server's rooms)
        rc.game_version = std::string(ants::VERSION_STRING);
        rc.build_id = std::string(ants::BUILD_ID).substr(0, replay::kMaxTextBytes);
        sink.store = std::make_unique<server::ReplayStore>(rc);
        std::string reason;
        if (!sink.store->prepare(reason)) {
            std::fprintf(g_err, "bot_arena: --save-replays: %s\n", reason.c_str());
            return 2;
        }
        for (const std::string& note : sink.store->take_notes()) std::fprintf(g_out, "bot_arena: %s\n", note.c_str());      // (what the folder held)
        g_replay_sink = &sink;
    }
    struct SinkOff { ~SinkOff() { g_replay_sink = nullptr; } } sink_off;
    std::string kinds;
    for (const ai::BotSpec& s : o.seats) kinds += (kinds.empty() ? "" : ", ") + std::to_string(static_cast<unsigned>(s.seat)) + "=" + spec_text(s);
    std::fprintf(g_out, "bot_arena: %zu match(es): maps %zu, seeds %s, %zu arrangement(s) of [%s], ticks %s, latency %u%s%s\n", jobs.size(), maps.size(), seeds_text(o.seeds).c_str(),
                 arrangements(o.seats, o.rotate).size(), kinds.c_str(), o.ticks == 0 ? "full" : std::to_string(o.ticks).c_str(), o.latency, o.replay_check ? ", replay check" : "",
                 o.repeat > 1 ? fmt(", every match %u times", o.repeat).c_str() : "");
    const Clock::time_point started = Clock::now();
    const std::vector<MatchReport> reports = run_jobs(o, maps, jobs, factory, jobs.size() > 100);
    if (!o.quiet) {
        for (const MatchReport& r : reports) print_match(maps, r);
    }
    size_t failures = 0;
    uint64_t ticks = 0;
    for (const MatchReport& r : reports) {
        if (!r.ok()) ++failures;
        ticks += r.result.ticks;
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    std::fprintf(g_out, "bot_arena: %zu match(es), %llu ticks, %.2f s%s\n", reports.size(), static_cast<unsigned long long>(ticks), seconds, failures == 0 ? ", every check passed" : "");
    if (failures != 0) std::fprintf(g_out, "bot_arena: %zu match(es) FAILED (not played, replay or repeat differs)\n", failures);
    if (g_replay_sink != nullptr) {
        std::fprintf(g_out, "bot_arena: %zu replay(s) saved in %s\n", sink.saved.size(), o.save_replays.c_str());
        if (!o.quiet) {
            for (const std::string& file : sink.saved) std::fprintf(g_out, "  %s\n", file.c_str());
        }
        if (sink.too_short > 0) std::fprintf(g_out, "bot_arena: %zu match(es) not saved: they ran under 30 seconds (%u turns), which the server's list does not show either\n", sink.too_short, static_cast<unsigned>(server::kReplayMinTurns));
        for (const std::string& note : sink.notes) std::fprintf(g_out, "bot_arena: %s\n", note.c_str());
        for (const std::string& note : sink.store->take_notes()) std::fprintf(g_out, "bot_arena: %s\n", note.c_str());
    }
    {
        // what the seats' ants said "Can't go there." (docs/BOT_PLAY.md, "The can't-go loop"): all reactions, the first of each, the orders given and the orders that were refused
        uint64_t cantgo = 0, began = 0, orders = 0, refused = 0, again = 0;
        for (const MatchReport& r : reports) {
            for (const ai::ArenaSeatResult& s : r.result.seats) {
                cantgo += s.cantgo;
                began += s.cantgo_began;
                orders += s.orders;
                refused += s.refused_orders;
                again += s.refused_again;
            }
        }
        if (cantgo != 0 || orders != 0) {
            std::fprintf(g_out, "bot_arena: can't-go: %llu reactions (%llu first ones, the rest repeats), %llu orders, %llu refused (%.1f per 1,000 orders), %llu of them again an order that was refused before\n",
                         static_cast<unsigned long long>(cantgo), static_cast<unsigned long long>(began), static_cast<unsigned long long>(orders), static_cast<unsigned long long>(refused),
                         orders == 0 ? 0.0 : 1000.0 * static_cast<double>(refused) / static_cast<double>(orders), static_cast<unsigned long long>(again));
        }
    }
    {
        // the ants that stood nowhere for 10 seconds (docs/BOT_UNSTUCK.md): the spans, the ant-minutes past the first 10 seconds, the ones that held food or loot, per seat-match
        uint64_t spans = 0, stuck_ticks = 0, looping = 0, blocked = 0, carriers = 0, seat_matches = 0;
        for (const MatchReport& r : reports) {
            for (const ai::ArenaSeatResult& s : r.result.seats) {
                spans += s.stuck_ants;
                stuck_ticks += s.stuck_ticks;
                looping += s.stuck_looping;
                blocked += s.stuck_blocked;
                carriers += s.stuck_carriers;
                ++seat_matches;
            }
        }
        if (spans != 0) {
            std::fprintf(g_out, "bot_arena: stuck: %llu spans of an ant that stood nowhere for 10 s (%.2f per seat and match; %llu looping in the can't-go clip, %llu blocked with an order, %llu carriers with no order), %.1f ant-minutes past the first 10 s\n",
                         static_cast<unsigned long long>(spans), seat_matches == 0 ? 0.0 : static_cast<double>(spans) / static_cast<double>(seat_matches), static_cast<unsigned long long>(looping),
                         static_cast<unsigned long long>(blocked), static_cast<unsigned long long>(carriers), static_cast<double>(stuck_ticks) / 1200.0);
        }
    }
    {
        // the droplets that landed on the flowers and what the seats' ants took of them (docs/BOT_TACTICS.md, "The flowers")
        uint64_t landings = 0, took = 0, at_flowers = 0;
        for (const MatchReport& r : reports) {
            landings += r.result.landings;
            for (const ai::ArenaSeatResult& s : r.result.seats) {
                for (size_t k = 1; k < s.took.size(); ++k) took += s.took[k];
                at_flowers += s.took_at_flowers;
            }
        }
        if (landings != 0) {
            std::fprintf(g_out, "bot_arena: flowers: %llu landings, %llu power-ups taken by the seats' ants, %llu of them at a flower (%.1f per 100 landings)\n", static_cast<unsigned long long>(landings),
                         static_cast<unsigned long long>(took), static_cast<unsigned long long>(at_flowers), 100.0 * static_cast<double>(at_flowers) / static_cast<double>(landings));
        }
    }
    {
        // the expeditions over water, per seat (docs/BOT_ISLANDS.md, "Islands"): the matches in which one was planned, the mean tick of the first bomb, the first landing and the first Swimmer
        // (over the matches that got so far), the Swimmers taken a match, the attempts given up, and the duds among the bombs
        struct Row {
            uint64_t matches{0}, plants{0}, landings{0}, swimmers{0}, taken{0}, given_up{0}, bombs{0}, duds{0};
            double plant_sum{0.0}, landing_sum{0.0}, swimmer_sum{0.0};
        };
        std::array<Row, 4> rows{};
        for (const MatchReport& r : reports) {
            for (const ai::ArenaSeatResult& s : r.result.seats) {
                const ai::ExpeditionResult& e = s.expedition;
                if (e.planned == 0 || s.spec.seat >= rows.size()) continue;
                Row& row = rows[s.spec.seat];
                ++row.matches;
                row.taken += e.swimmers_taken;
                row.given_up += e.given_up;
                row.bombs += e.planted;
                row.duds += e.duds;
                if (e.first_plant != 0) {
                    ++row.plants;
                    row.plant_sum += static_cast<double>(e.first_plant);
                }
                if (e.first_landing != 0) {
                    ++row.landings;
                    row.landing_sum += static_cast<double>(e.first_landing);
                }
                if (e.first_swimmer != 0) {
                    ++row.swimmers;
                    row.swimmer_sum += static_cast<double>(e.first_swimmer);
                }
            }
        }
        const auto mean = [](double sum, uint64_t n) { return n == 0 ? 0.0 : sum / static_cast<double>(n); };
        for (size_t seat = 0; seat < rows.size(); ++seat) {
            const Row& row = rows[seat];
            if (row.matches == 0) continue;
            std::fprintf(g_out, "bot_arena: expedition, seat %zu: %llu match(es), first bomb at tick %.0f (%llu), first landing %.0f (%llu), first Swimmer %.0f (%llu), %.2f Swimmers taken a match, %llu given up, %llu of %llu bombs duds\n", seat,
                         static_cast<unsigned long long>(row.matches), mean(row.plant_sum, row.plants), static_cast<unsigned long long>(row.plants), mean(row.landing_sum, row.landings),
                         static_cast<unsigned long long>(row.landings), mean(row.swimmer_sum, row.swimmers), static_cast<unsigned long long>(row.swimmers),
                         static_cast<double>(row.taken) / static_cast<double>(row.matches), static_cast<unsigned long long>(row.given_up), static_cast<unsigned long long>(row.duds),
                         static_cast<unsigned long long>(row.bombs));
        }
    }
    if (report.is_open()) {
        // written match by match, then flushed and CLOSED, and only then judged: a failure that comes at the flush (a full disk, a size limit) is a failure, not a truncated file
        // with exit code 0
        const bool written = write_report(report, o, maps, reports);
        report.close();
        if (!written || report.fail()) {
            std::fprintf(g_err, "bot_arena: cannot write %s completely\n", o.out.c_str());
            return 2;
        }
    }
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// --write-baselines
// ---------------------------------------------------------------------------------------------------------------------------------

// The text of tests/test_ai/baselines.inc: one row per shipped map and level (include/ants_ai/baselines.hpp is the procedure, the test that checks the table runs the same code)
bool baselines_text(const Options& o, std::string& text, std::string& err) {
    Options mo = o;
    mo.maps.assign(std::begin(kShippedMaps), std::end(kShippedMaps));
    const std::vector<LoadedMap> maps = load_maps(mo);
    for (const LoadedMap& m : maps) {
        if (!m.ok) { err = m.error; return false; }
    }
    const ai::Level levels[] = {ai::Level::Easy, ai::Level::Medium, ai::Level::Hard};
    const size_t count = maps.size() * 3;
    std::vector<ai::BaselineRow> rows(count);
    std::atomic<size_t> next{0};
    const auto work = [&]() {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= count) return;
            rows[i] = ai::measure_baseline(maps[i / 3].level, maps[i / 3].name, levels[i % 3]);
        }
    };
    const unsigned n = std::max(1u, std::min<unsigned>(o.threads, static_cast<unsigned>(count)));
    if (n == 1) {
        work();
    } else {
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < n; ++t) pool.emplace_back(work);
        for (std::thread& t : pool) t.join();
    }
    for (const ai::BaselineRow& r : rows) {
        if (!r.error.empty()) { err = r.map + ": " + r.error; return false; }
    }
    std::string seeds;
    for (const uint32_t s : ai::kBaselineSeeds) seeds += (seeds.empty() ? "" : ", ") + std::to_string(s);
    text = "// Generated by `bot_arena --write-baselines` (the procedure is include/ants_ai/baselines.hpp): do not edit by hand, regenerate on purpose.\n"
           "// The worker bot at three levels on the shipped maps; every number is the mean over the seeds " + seeds + ", the arena's sink latency is " + std::to_string(ai::kBaselineLatency) +
           " ticks, seats are rotated.\n"
           "//   columns: map, level, {alone against three idle bots, 2 minutes, seats 0..3}, {the same, whole match}, {four workers, whole match, seats 0..3}, sum of those four, reachable pot\n"
           "// ISLANDS: no hill walks to any food and the worker bot never crosses water: 0 = 0 (the standard bot's island play: docs/BOTS.md, \"Islands\").\n";
    for (const ai::BaselineRow& r : rows) text += ai::baseline_line(r) + "\n";
    return true;
}

int write_baselines(const Options& o) {
    std::string text;
    std::string err;
    if (!baselines_text(o, text, err)) {
        std::fprintf(stderr, "bot_arena: --write-baselines: %s\n", err.c_str());
        return 1;
    }
    std::fputs(text.c_str(), stdout);
    return 0;
}

}  // namespace ants::arena

using namespace ants::arena;

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    Options o;
    std::string err;
    if (!parse_args(args, o, err)) {
        std::fprintf(stderr, "bot_arena: %s\n", err.c_str());
        print_usage(stderr);
        return 2;
    }
    if (o.help) {
        print_usage(stdout);
        return 0;
    }
    if (o.selftest) return selftest();
    if (o.write_baselines) return write_baselines(o);
    return run_tool(o);
}
