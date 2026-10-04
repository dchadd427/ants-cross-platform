// bot_arena: plays one or many matches of computer players headless, with the real engine and the BotController (docs/BOTS.md). The tool every later bot is measured
// with: the same arguments always give the same matches, bit for bit, on any machine, whatever the number of threads.
//
// Usage:
//   bot_arena [--map NAMES] [--seeds A..B] [--seat N=KIND[:LEVEL]]... [--ticks full|N] [--latency-ticks N] [--rotate] [--repeat N] [--replay-check]
//             [--threads N] [--out report.json] [--quiet] [--no-wall-time] [--maps-dir DIR]
//   bot_arena --selftest
//   bot_arena --write-baselines [--threads N] [--maps-dir DIR] > tests/test_ai/baselines.inc
//
//   --map NAMES       a comma list of shipped maps by name (TINY, SMALL, MEDIUM, GAUNTLET, TREASURE, ISLANDS, or "shipped" for all six) or paths of .LVL files (default TINY)
//   --seeds A..B      the engine and controller seeds: "3", "1..8", "1,4,9..12" (default 1)
//   --seat N=SPEC     a bot on seat N (0 green, 1 red, 2 blue, 3 black); SPEC is KIND, KIND:LEVEL or LEVEL: KIND idle, worker, standard (or aggressor, the bench bot of
//                     tools/bench_aggressor.hpp: raids and harasses, never harvests, not a bot of the game); LEVEL easy, medium, hard.
//                     Repeat it for every seat that plays. Default: four standard bots at medium level. A seat that is not named has no hill and no ants.
//                     idle stands still; worker harvests (B3, the frozen yardstick); standard is the standard bot (B4-1: the worker's economy plus the tactics of its level).
//   --ticks full|N    play until the match is over (the map's own length, default) or at most N ticks (50 ms each)
//   --latency-ticks N the sink latency: 0 applies a command the moment a bot releases it; N > 0 plays like a lock-step room (applied at the first 100 ms turn
//                     boundary at least N ticks later, in canonical order); default 3
//   --rotate          play every distinct arrangement of the given bots over the given seats (up to 24 for four different bots): seats are not symmetric on a map,
//                     so a comparison of bots must rotate them
//   --repeat N        play every match N times and require identical results (finds any nondeterminism)
//   --replay-check    re-feed the commands that were applied into a FRESH engine with no bot at all and require the same state hash at every 20th tick and at the end
//   --threads N       matches played at the same time (default 1; every match is independent, the report is sorted by map, seed and arrangement)
//   --out FILE        write the JSON report (fixed key order; the maps' NAMES only, no paths). The file is opened BEFORE the first match (an unwritable path costs nothing) and
//                     checked after the last: a report that could not be written completely is exit code 2, never a silent success
//   --no-wall-time    leave the wall clock time of each match out of the report, so that the file is bit-reproducible
//   --maps-dir DIR    where map names are looked for (default: the shipped maps)
//   --selftest        check the tool itself (determinism, replay check, report, threads, the tool end to end on a temporary folder)
//   --write-baselines print the table that tests/test_ai/baselines.inc pins (the worker bot at the three levels on the six shipped maps, a fixed seed set, seats rotated:
//                     include/ants_ai/baselines.hpp) to stdout; the other options (but --threads and --maps-dir) are ignored. Regenerate it ON PURPOSE, when the bot or the
//                     hill's banking changed.
//
// Exit code: 0 when every match was played and every check passed, 1 on a finding (a match that could not be played, a replay or repeat that differs), 2 on bad usage or when the
// report (or the match list: too many matches) cannot be handled.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <ostream>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ants_ai/arena.hpp"
#include "ants_ai/baselines.hpp"
#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/rng.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "bench_aggressor.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ARENA_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define ARENA_PRINTF(fmt_index, first_arg)
#endif

namespace fs = std::filesystem;
namespace ai = ants::ai;
namespace sim = ants::sim;
namespace assets = ants::assets;

namespace {

using Clock = std::chrono::steady_clock;

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

// A small JSON syntax checker (RFC 8259, ASCII text): the report must parse
class JsonChecker {
public:
    explicit JsonChecker(const std::string& s) : s_(s) {}
    bool valid() {
        skip();
        if (!value()) return false;
        skip();
        return i_ == s_.size();
    }

private:
    void skip() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\t' || s_[i_] == '\r')) ++i_; }
    bool literal(const char* w) {
        const size_t n = std::strlen(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    bool string() {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            const uint8_t c = static_cast<uint8_t>(s_[i_]);
            if (c == '"') { ++i_; return true; }
            if (c < 0x20 || c >= 0x80) return false;
            if (c == '\\') {
                ++i_;
                if (i_ >= s_.size()) return false;
                if (s_[i_] == 'u') {
                    for (size_t k = 1; k <= 4; ++k) {
                        if (i_ + k >= s_.size() || !std::isxdigit(static_cast<unsigned char>(s_[i_ + k]))) return false;
                    }
                    i_ += 4;
                } else if (std::strchr("\"\\/bfnrt", s_[i_]) == nullptr) {
                    return false;
                }
            }
            ++i_;
        }
        return false;
    }
    bool number() {
        const size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        const size_t digits = i_;
        while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-')) ++i_;
        return i_ > start && i_ > digits && s_[digits] != '.';
    }
    bool value() {
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            skip();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            for (;;) {
                skip();
                if (!string()) return false;
                skip();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                skip();
                if (!value()) return false;
                skip();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++i_;
            skip();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            for (;;) {
                skip();
                if (!value()) return false;
                skip();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') return string();
        if (c == 't') return literal("true");
        if (c == 'f') return literal("false");
        if (c == 'n') return literal("null");
        return number();
    }
    const std::string& s_;
    size_t i_{0};
};

// A report read back (the self-test reads the file that run_tool wrote and compares its numbers): the values of a JSON text as a tree. Numbers are doubles (every counter of the
// report is far below 2^53), strings are the ASCII text of the file with the escapes undone.
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind{Kind::Null};
    bool b{false};
    double number{0.0};
    std::string text;
    std::vector<JsonValue> items;                  // an array's elements
    std::vector<std::string> keys;                 // an object's keys, in file order, and the values that belong to them
    std::vector<JsonValue> values;

    const JsonValue* get(const std::string& key) const {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return &values[i];
        }
        return nullptr;
    }
    uint64_t u64() const { return static_cast<uint64_t>(number); }
    int64_t i64() const { return static_cast<int64_t>(number); }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& s) : s_(s) {}
    bool parse(JsonValue& out) {
        skip();
        if (!value(out)) return false;
        skip();
        return i_ == s_.size();
    }

private:
    void skip() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\t' || s_[i_] == '\r')) ++i_; }
    bool string(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '"') { ++i_; return true; }
            if (c == '\\') {
                ++i_;
                if (i_ >= s_.size()) return false;
                const char e = s_[i_];
                if (e == 'u') {
                    if (i_ + 4 >= s_.size()) return false;
                    out += static_cast<char>(std::strtol(s_.substr(i_ + 1, 4).c_str(), nullptr, 16));
                    i_ += 4;
                } else if (e == 'n') out += '\n';
                else if (e == 't') out += '\t';
                else if (e == 'r') out += '\r';
                else if (e == 'b') out += '\b';
                else if (e == 'f') out += '\f';
                else out += e;
            } else {
                out += c;
            }
            ++i_;
        }
        return false;
    }
    bool value(JsonValue& v) {
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            v.kind = JsonValue::Kind::Object;
            ++i_;
            skip();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            for (;;) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                skip();
                JsonValue member;
                if (!value(member)) return false;
                v.keys.push_back(std::move(key));
                v.values.push_back(std::move(member));
                skip();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.kind = JsonValue::Kind::Array;
            ++i_;
            skip();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            for (;;) {
                skip();
                JsonValue item;
                if (!value(item)) return false;
                v.items.push_back(std::move(item));
                skip();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') { v.kind = JsonValue::Kind::String; return string(v.text); }
        if (s_.compare(i_, 4, "true") == 0) { v.kind = JsonValue::Kind::Bool; v.b = true; i_ += 4; return true; }
        if (s_.compare(i_, 5, "false") == 0) { v.kind = JsonValue::Kind::Bool; v.b = false; i_ += 5; return true; }
        if (s_.compare(i_, 4, "null") == 0) { v.kind = JsonValue::Kind::Null; i_ += 4; return true; }
        char* end = nullptr;
        v.number = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        v.kind = JsonValue::Kind::Number;
        i_ = static_cast<size_t>(end - s_.c_str());
        return true;
    }
    const std::string& s_;
    size_t i_{0};
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------------------------------------------

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

void print_usage(std::FILE* to) {
    std::fprintf(to,
        "Usage: bot_arena [--map NAMES] [--seeds A..B] [--seat N=KIND[:LEVEL]]... [--ticks full|N] [--latency-ticks N] [--rotate] [--repeat N]\n"
        "                 [--replay-check] [--threads N] [--out report.json] [--quiet] [--no-wall-time] [--maps-dir DIR]\n"
        "       bot_arena --selftest\n"
        "       bot_arena --write-baselines [--threads N] [--maps-dir DIR] > tests/test_ai/baselines.inc\n"
        "Plays matches of computer players headless with the real engine. See the top of tools/bot_arena.cpp and docs/BOTS.md.\n"
        "  --map NAMES        shipped maps by name (TINY SMALL MEDIUM GAUNTLET TREASURE ISLANDS, or 'shipped') or .LVL paths, comma separated (default TINY)\n"
        "  --seeds A..B       \"3\", \"1..8\" or \"1,4,9..12\" (default 1)\n"
        "  --seat N=SPEC      a bot on seat N (0 to 3); SPEC is KIND, KIND:LEVEL or LEVEL (kinds idle worker standard aggressor aggressor2 saboteur rusher; levels easy medium hard).\n"
        "                     Default: four standard bots at medium level. %s.\n"
        "  --ticks full|N     until the match is over (default) or at most N ticks\n"
        "  --latency-ticks N  sink latency in ticks (default 3; 0 = commands applied at once)\n"
        "  --rotate           every distinct arrangement of the bots over the seats\n"
        "  --ally-standard    the first two standard bots of a match team up: the lower seat invites at its first look, the other accepts by its accept rule (test-only: no bot of the game invites)\n"
        "  --ally-pairs       every two seats that have the same standard spec team up (2 + 2 for [A, A, B, B]; the lower seat of a pair invites; test-only)\n"
        "  --repeat N         play every match N times and require identical results\n"
        "  --replay-check     replay the applied commands into a fresh engine without any bot: same hash at every 20th tick and at the end\n"
        "  --threads N        matches at the same time (default 1)\n"
        "  --out FILE         write the JSON report\n"
        "  --quiet            no line per match\n"
        "  --no-wall-time     leave wall times out of the report (the file is then bit-reproducible)\n"
        "  --tune K=V,...     ablations of the standard bot's plan, for the tournaments (the keys are those of apply_tune in this file, listed in docs/audit/B4_1_notes.md)
"
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
        if (upper(parts[i]) == "AGGRESSOR" || upper(parts[i]) == "AGGRESSOR2" || upper(parts[i]) == "SABOTEUR" || upper(parts[i]) == "RUSHER" ||
            (upper(parts[i]).rfind("AGGR", 0) == 0 && upper(parts[i]).size() == 5 && upper(parts[i])[4] >= '1' && upper(parts[i])[4] <= '9')) {
            aggressor = true;
            bench_kind = upper(parts[i]) == "SABOTEUR" ? "saboteur" : upper(parts[i]) == "AGGRESSOR2" ? "aggressor2" : upper(parts[i]) == "RUSHER" ? "rusher" : upper(parts[i]) == "AGGRESSOR" ? "aggressor" : "aggr" + parts[i].substr(4);
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

// --tune KEY=VALUE,...: the ablations of the standard bot's plan (docs/audit/B4_1_notes.md): every standard bot of the run gets the plan of its level with these values put over it.
// Set while the options are parsed, before any match (and thread) starts, and only read afterwards.
std::vector<std::pair<std::string, int64_t>> g_tune;

bool apply_tune(ai::LevelPlan& p, const std::string& key, int64_t v, std::string& err) {
    const auto flag = [&](bool& f) { f = v != 0; return true; };
    if (key == "defenders") { p.defenders = static_cast<uint32_t>(v); return true; }
    if (key == "leash") { p.leash_tiles = static_cast<int32_t>(v); return true; }
    if (key == "linger") { p.fight_linger_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "aid") return flag(p.carrier_aid);
    if (key == "contest") return flag(p.contest_aware);
    if (key == "clow") { p.contest_low = static_cast<uint32_t>(v); return true; }
    if (key == "chigh") { p.contest_high = static_cast<uint32_t>(v); return true; }
    if (key == "rankrem") return flag(p.rank_by_remaining);
    if (key == "cone") return flag(p.contest_one_first);
    if (key == "creact") return flag(p.contest_reactive);
    if (key == "copen") { p.contest_opening_ants = static_cast<uint32_t>(v); return true; }
    if (key == "typedh") return flag(p.typed_harvest);
    if (key == "firew") return flag(p.fire_aware);
    if (key == "secure") return flag(p.secure_side);
    if (key == "securek") { p.secure_kinds = static_cast<uint8_t>(v); return true; }
    if (key == "counters") return flag(p.counters);
    if (key == "bhit") return flag(p.bomb_hit);
    if (key == "chv") return flag(p.combat_harvests);
    if (key == "walls") { p.wall_trigger = v == 0 ? ai::WallTrigger::Never : v == 1 ? ai::WallTrigger::ThiefSeen : v == 2 ? ai::WallTrigger::ThiefPossible : ai::WallTrigger::Early; return true; }
    if (key == "renew") { p.renew_lead_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "combat") { p.takes_combat = v > 0; p.max_combat = static_cast<uint32_t>(v); return true; }
    if (key == "steals") return flag(p.steals);
    if (key == "fiststrict") return flag(p.fists_strict);
    if (key == "ambush") return flag(p.ambush);
    if (key == "ambushticks") { p.ambush_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "ambushpause") { p.ambush_pause = static_cast<uint32_t>(v); return true; }
    if (key == "ambushdist") { p.ambush_distance = static_cast<int32_t>(v); return true; }
    if (key == "ambushn") { p.ambush_thieves = static_cast<uint32_t>(v); return true; }
    if (key == "raidmin") { p.raid_min_loot = static_cast<uint32_t>(v); return true; }
    if (key == "raidblack") { p.raid_black_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "sabotage") return flag(p.sabotage);
    if (key == "fireextra") { p.fire_extra = static_cast<uint32_t>(v); return true; }
    if (key == "sabkeeper") return flag(p.sabotage_spare_keeper);
    if (key == "sabscore") { p.sabotage_min_score = static_cast<uint32_t>(v); return true; }
    if (key == "sabafter") { p.sabotage_after = static_cast<uint32_t>(v); return true; }
    if (key == "harass") return flag(p.harass);
    if (key == "hatchsq") return flag(p.hatch_for_squad);
    if (key == "hatchextra") { p.hatch_extra = static_cast<uint32_t>(v); return true; }
    if (key == "harassw") { p.harass_workers = static_cast<uint32_t>(v); return true; }
    if (key == "harassres") { p.harass_reserve = static_cast<uint32_t>(v); return true; }
    if (key == "harasshp") { p.harass_min_hp = static_cast<uint32_t>(v); return true; }
    if (key == "harassodds") { p.harass_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "harassstrong") { p.harass_strong_defence = static_cast<uint32_t>(v); return true; }
    if (key == "harassretreat") { p.harass_retreat_hp = static_cast<uint32_t>(v); return true; }
    if (key == "harasspause") { p.harass_pause_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "harassidlew") { p.harass_idle_weight = static_cast<uint32_t>(v); return true; }
    if (key == "harassnear") { p.harass_near = static_cast<int32_t>(v); return true; }
    if (key == "harassleader") { p.harass_leader_bonus = static_cast<uint32_t>(v); return true; }
    if (key == "harassidle") { p.harass_idle_bonus = static_cast<uint32_t>(v); return true; }
    if (key == "harassstick") { p.harass_stick = static_cast<uint32_t>(v); return true; }
    if (key == "harassdist") { p.harass_dist_cost = static_cast<uint32_t>(v); return true; }
    if (key == "harassfar") { p.harass_far_bonus = static_cast<uint32_t>(v); return true; }
    if (key == "harassstation") return flag(p.harass_station);
    if (key == "harassrange") { p.harass_range = static_cast<int32_t>(v); return true; }
    if (key == "harassrel") { p.harass_idle_release = static_cast<uint32_t>(v); return true; }
    if (key == "combatx") { p.combat_extra = static_cast<uint32_t>(v); return true; }
    if (key == "thief") { p.takes_thief = v > 0; p.max_thief = static_cast<uint32_t>(v); return true; }
    if (key == "intercept") return flag(p.intercepts);
    if (key == "combat_early") { p.combat_when_attacked = v == 0; return true; }
    if (key == "combat_idle") return flag(p.combat_when_idle);
    if (key == "guard") return flag(p.guards);
    if (key == "raid") return flag(p.raids);
    if (key == "strike") return flag(p.strikes);
    if (key == "strikew") return flag(p.strike_workers);
    if (key == "strikef") { p.strike_force = static_cast<uint32_t>(v); return true; }
    if (key == "strikeres") { p.strike_reserve = static_cast<uint32_t>(v); return true; }
    if (key == "strikeodds") { p.strike_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "wipe") return flag(p.wipe_focus);
    if (key == "hatch") return flag(p.hatches);
    if (key == "allyhelp") return flag(p.ally_help);
    if (key == "gate") return flag(p.gate);
    if (key == "gatepred") return flag(p.gate_predictive);
    if (key == "gatelat") { p.gate_latency = static_cast<uint32_t>(v); return true; }
    if (key == "gatestaged") { p.gate_max_staged = static_cast<uint32_t>(v); return true; }
    if (key == "gategap") { p.gate_gap_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "idle") { p.bench_idle_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "avoid") return flag(p.avoids_guarded_hills);
    if (key == "agg") {                                                               // the aggressive plan (Hard's from the start): the squad, Combat Ants in the opening, no guard post at home
        if (v == 0) return true;
        p.harass = true;
        p.combat_when_attacked = false;
        p.guards = false;
        return true;
    }
    if (key == "old") {                                                               // the conflict tactics as they were shipped before the win-rate measurements: all off, one Thief
        if (v == 0) return true;
        p.contest_aware = false;
        p.contest_reactive = false;
        p.strikes = false;
        p.hatches = false;
        p.wipe_focus = false;
        p.ally_help = false;
        if (p.max_thief > 1) p.max_thief = 1;
        return true;
    }
    if (key == "allon") {                                                             // ALL the conflict tactics on (the mirror of the conflict-rich tournaments): the strict contest order, strikes, hatching for fights, wipe-out focus, the help of the ally, two Thieves
        if (v == 0) return true;
        p.contest_aware = true;
        p.strikes = true;
        p.hatches = true;
        p.wipe_focus = true;
        p.ally_help = true;
        if (p.takes_thief && p.max_thief < 2) p.max_thief = 2;
        return true;
    }
    err = "unknown tuning key '" + key + "' (the keys are those of apply_tune in tools/bot_arena.cpp, listed in docs/audit/B4_1_notes.md)";
    return false;
}

// BOT_DIAG=1: the standard bots print what their tasks did when the match is over (experiments only)
std::unique_ptr<ai::Bot> diag_wrap(std::unique_ptr<ai::StandardBot> bot, const ai::BotSpec& spec) {
    if (std::getenv("BOT_DIAG") == nullptr) return bot;
    return std::make_unique<ai::bench::DiagBot>(std::move(bot), spec.kind + ":" + ai::level_name(spec.level) + (spec.style != ai::Style::Random ? std::string(":") + ai::style_name(spec.style) : std::string()));
}

std::unique_ptr<ai::Bot> count_wrap(std::unique_ptr<ai::Bot> bot, const ai::BotSpec& spec) {
    if (std::getenv("BOT_DIAG") == nullptr) return bot;
    return std::make_unique<ai::bench::CountBot>(std::move(bot), spec.kind + ":" + ai::level_name(spec.level));
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
        if (spec.style == ai::Style::Random) {                                       // no style named: the level's neutral plan, no variations (the ablations of the tournaments)
            ai::LevelPlan plan = ai::plan_for(spec.level);
            apply_tuning(plan, keys);
            return diag_wrap(std::make_unique<ai::StandardBot>(plan), spec);
        }
        return diag_wrap(std::make_unique<ai::StandardBot>(spec.level, spec.style, [keys](ai::LevelPlan& p) { apply_tuning(p, keys); }), spec);
    }
    if (spec.kind == "aggressor") return count_wrap(std::make_unique<ai::bench::AggressorBot>(), spec);
    if (spec.kind.size() == 5 && spec.kind.rfind("aggr", 0) == 0 && spec.kind[4] >= '1' && spec.kind[4] <= '9') return count_wrap(std::make_unique<ai::bench::AggressorBot>(1, static_cast<size_t>(spec.kind[4] - '0')), spec);
    if (spec.kind == "aggressor2") return std::make_unique<ai::bench::AggressorBot>(2);
    if (spec.kind == "rusher") {                                                      // the centre-rusher of the bench: the economy of the standard bot with the contest order and no tactics
        ai::LevelPlan plan = ai::plan_for(spec.level);
        plan.contest_aware = true;
        plan.secure_side = false;
        plan.secure_kinds = 0;
        plan.wall_trigger = ai::WallTrigger::Never;
        plan.takes_combat = false;
        plan.takes_thief = false;
        plan.guards = false;
        plan.raids = false;
        plan.counters = false;
        return std::make_unique<ai::StandardBot>(plan);
    }
    if (spec.kind == "saboteur") return std::make_unique<ai::bench::SaboteurBot>();
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

struct LoadedMap {
    std::string name;                // what the report calls it: the file's name without folder and extension, in capitals for a shipped map
    assets::LevelData level;
    bool ok{false};
    std::string error;
};

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
        if (!m.level.load_from_file(p.string())) {
            m.error = "map '" + stem + "' does not load";
            continue;
        }
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

// Two plays of one match are the same match: every number of the result is equal (the log is not compared: only the first play records one)
bool same_match(const ai::ArenaResult& a, const ai::ArenaResult& b) {
    if (a.error != b.error || a.ticks != b.ticks || a.steps != b.steps || a.hash != b.hash || a.match_over != b.match_over || a.initial_ticks != b.initial_ticks ||
        a.checkpoints != b.checkpoints || a.seats.size() != b.seats.size() || a.news_events != b.news_events || a.audio_events != b.audio_events || a.peak_queue != b.peak_queue ||
        a.reachable_units_left != b.reachable_units_left) {
        return false;
    }
    for (size_t i = 0; i < a.seats.size(); ++i) {
        const ai::ArenaSeatResult& x = a.seats[i];
        const ai::ArenaSeatResult& y = b.seats[i];
        if (x.spec.seat != y.spec.seat || x.runs != y.runs || x.score != y.score || x.shown_score != y.shown_score || x.ants != y.ants || x.eggs != y.eggs || x.hatched != y.hatched ||
            x.banked != y.banked || x.raided != y.raided || x.kills != y.kills || x.losses != y.losses || x.stats.decisions != y.stats.decisions || x.stats.intents != y.stats.intents ||
            x.stats.released != y.stats.released || x.stats.expired != y.stats.expired || x.stats.pruned != y.stats.pruned || x.stats.superseded != y.stats.superseded ||
            x.stats.filtered != y.stats.filtered || x.stats.rejected != y.stats.rejected) {
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
    s.record = record;
    s.extra_kinds = {"aggressor", "aggressor2", "saboteur", "rusher", "aggr1", "aggr2", "aggr3", "aggr4", "aggr5", "aggr6", "aggr7", "aggr8", "aggr9"};
    for (const ai::BotSpec& b : j.bots) {
        if (b.kind.rfind("standard+", 0) == 0) s.extra_kinds.push_back(b.kind);
    }
    return s;
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
    ai::ArenaSpec spec = spec_of(o, m, job, o.replay_check);
    spec.factory = factory ? factory : o.ally_pairs ? ally_pairs_factory(job) : o.ally_standard ? ally_factory(job) : std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>(arena_factory);
    r.result = ai::play_match(spec);
    r.plays = 1;
    if (r.result.error.empty() && o.replay_check) {
        r.replay_checked = true;
        r.replay = ai::replay_commands(spec, r.result);
    }
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

// Writes the report to `out` one match at a time (nothing is built as one big string); false when the stream is not good afterwards
bool write_report(std::ostream& out, const Options& o, const std::vector<LoadedMap>& maps, const std::vector<MatchReport>& reports) {
    JsonWriter j(&out);
    j.begin_object();
    j.field("tool", "bot_arena");
    j.field("format", uint64_t{2});                                   // 2: banked and raided replace the engine's food_deposited, food_stolen and food_lost (never fed, always 0)
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

// The whole report as one string (for the self-test: the same bytes write_report puts in a file)
std::string report_json(const Options& o, const std::vector<LoadedMap>& maps, const std::vector<MatchReport>& reports) {
    std::ostringstream text;
    write_report(text, o, maps, reports);
    return text.str();
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
           "// ISLANDS: no hill walks to any food: 0 = 0 until the island hops of B4a.\n";
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

// ---------------------------------------------------------------------------------------------------------------------------------
// --selftest
// ---------------------------------------------------------------------------------------------------------------------------------

// A scripted bot that only the self-test uses (the real worker bot gives a handful of commands a minute, a replay check wants many): at every look it sends one of its ants to a tile near it, so that matches hold commands for the
// replay check to check. Like every bot it talks through Orders only.
class SelftestWalker final : public ai::Bot {
public:
    const char* kind() const noexcept override { return "selftest-walker"; }
    void start(const ai::BotContext& context) override { rng_ = ai::BotRng(context.rng_seed); }
    void think(const ai::BotView& view, ai::Orders& orders) override {
        if (view.mine().empty()) return;
        const ai::AntView& a = view.mine()[rng_.below(static_cast<uint32_t>(view.mine().size()))];
        const int32_t w = static_cast<int32_t>(view.grid().width());
        const int32_t h = static_cast<int32_t>(view.grid().height());
        const int32_t x = std::clamp(a.tile.x + static_cast<int32_t>(rng_.below(15)) - 7, 0, std::max(0, w - 1));
        const int32_t y = std::clamp(a.tile.y + static_cast<int32_t>(rng_.below(15)) - 7, 0, std::max(0, h - 1));
        orders.move({a.id}, sim::TileCoord{x, y});
    }

private:
    ai::BotRng rng_;
};

std::unique_ptr<ai::Bot> selftest_factory(const ai::BotSpec& spec) {
    if (spec.kind == "worker") return std::make_unique<SelftestWalker>();
    return ai::make_bot(spec);
}

struct SelfTest {
    int checks{0};
    int failures{0};
    void check(bool ok, const std::string& what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("    FAILED: %s\n", what.c_str());
        }
    }
    void section(const char* name) {
        std::printf("  %s\n", name);
        std::fflush(stdout);
    }
};

ai::BotSpec spec_for(uint8_t seat, const char* kind, ai::Level level) {
    ai::BotSpec s;
    s.seat = seat;
    s.kind = kind;
    s.level = level;
    return s;
}

// A bot that earns points (the self-test's fields must be read back with numbers that are not 0): idle ants go to the nearest pile of MapInfo that still has units
class SelftestHarvester final : public ai::Bot {
public:
    const char* kind() const noexcept override { return "selftest-harvester"; }
    void start(const ai::BotContext&) override {}
    void think(const ai::BotView& v, ai::Orders& o) override {
        const ai::MapInfo* map = v.map();
        if (map == nullptr) return;
        std::vector<uint32_t> idle;
        for (const ai::AntView& a : v.mine()) {
            if (a.idle()) idle.push_back(a.id);
        }
        if (idle.empty()) return;
        const ai::PileInfo* best = nullptr;
        for (const ai::PileView& p : v.piles()) {
            const ai::PileInfo* info = map->pile(p.index);
            if (p.lunchbox || info == nullptr || !info->approach[v.seat()].reachable()) continue;
            if (best == nullptr || info->approach[v.seat()].cost < best->approach[v.seat()].cost) best = info;
        }
        if (best != nullptr) o.move(idle, best->approach[v.seat()].click);
    }
};

// A bot that is NOT reproducible on purpose: where it sends its ant depends on a counter that outlives the match (a repeat of a match must notice)
class SelftestDrifter final : public ai::Bot {
public:
    explicit SelftestDrifter(uint32_t offset) : offset_(offset) {}
    const char* kind() const noexcept override { return "selftest-drifter"; }
    void start(const ai::BotContext&) override {}
    void think(const ai::BotView& view, ai::Orders& orders) override {
        if (view.mine().empty()) return;
        const ai::AntView& a = view.mine().front();
        orders.move({a.id}, sim::TileCoord{a.tile.x + 2 + static_cast<int32_t>(offset_ % 5), a.tile.y + 2});
    }

private:
    uint32_t offset_;
};

// A scratch folder (the tool may write files; ants_ai may not)
struct TempDir {
    fs::path path;
    TempDir() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("bot_arena_selftest_" + std::to_string(static_cast<unsigned long long>(Clock::now().time_since_epoch().count())));
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    bool ok() const {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }
    std::string file(const char* name) const { return (path / name).string(); }
};

std::string read_file(const std::string& name) {
    std::ifstream f(name, std::ios::binary);
    std::ostringstream text;
    text << f.rdbuf();
    return text.str();
}

// A stream buffer that refuses every byte after `limit` (a full disk, a size limit) and remembers how it was written to
class LimitedBuf final : public std::streambuf {
public:
    explicit LimitedBuf(size_t limit) : limit_(limit) {}
    size_t bytes{0};
    size_t writes{0};
    size_t biggest{0};

protected:
    std::streamsize xsputn(const char*, std::streamsize n) override {
        ++writes;
        biggest = std::max(biggest, static_cast<size_t>(n));
        if (bytes + static_cast<size_t>(n) > limit_) return 0;
        bytes += static_cast<size_t>(n);
        return n;
    }
    int_type overflow(int_type c) override {
        if (bytes + 1 > limit_) return traits_type::eof();
        ++bytes;
        return traits_type::not_eof(c);
    }

private:
    size_t limit_;
};

// What run_tool printed (g_out and g_err) while it ran, and its exit code
struct ToolRun {
    int code{-1};
    std::string out;
    std::string err;
};

ToolRun run_tool_quietly(const Options& o, const TempDir& dir, const std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)>& factory) {
    ToolRun r;
    const std::string out_name = dir.file("stdout.txt");
    const std::string err_name = dir.file("stderr.txt");
    std::FILE* out = std::fopen(out_name.c_str(), "wb");
    std::FILE* err = std::fopen(err_name.c_str(), "wb");
    if (out == nullptr || err == nullptr) {
        if (out != nullptr) std::fclose(out);
        if (err != nullptr) std::fclose(err);
        return r;
    }
    std::FILE* keep_out = g_out;
    std::FILE* keep_err = g_err;
    g_out = out;
    g_err = err;
    r.code = run_tool(o, factory);
    g_out = keep_out;
    g_err = keep_err;
    std::fclose(out);
    std::fclose(err);
    r.out = read_file(out_name);
    r.err = read_file(err_name);
    return r;
}

// The checks of the tool's own wiring: each of them kills a one-line change that used to leave the self-test green
void selftest_wiring(SelfTest& t, const LoadedMap& tiny) {
    t.section("the wiring: options reach the arena, equal results are noticed, unequal ones are not missed");
    {
        // --ticks and --latency-ticks reach the match
        Options o;
        o.maps = {"TINY"};
        o.seats = {spec_for(0, "worker", ai::Level::Hard), spec_for(1, "worker", ai::Level::Hard)};
        o.seats_given = true;
        o.ticks = 300;
        o.latency = 3;
        o.wall_time = false;
        Job job;
        job.seed = 5;
        job.bots = o.seats;
        const ai::ArenaSpec spec = spec_of(o, tiny, job, true);
        t.check(spec.max_ticks == 300 && spec.latency_ticks == 3 && spec.seed == 5 && spec.record && spec.bots.size() == 2 && spec.level == &tiny.level, "spec_of carries --ticks, --latency-ticks, the seed and the seats");
        const std::vector<LoadedMap> maps = [&] {
            std::vector<LoadedMap> v(1);
            v[0].name = tiny.name;
            v[0].ok = true;
            v[0].level = tiny.level;
            return v;
        }();
        const MatchReport three = run_job(o, maps, job, selftest_factory);
        o.latency = 0;
        const MatchReport zero = run_job(o, maps, job, selftest_factory);
        t.check(three.result.error.empty() && three.result.ticks == 300 && !three.result.match_over, "a match of --ticks 300 plays 300 ticks");
        t.check(zero.result.ticks == 300 && zero.result.hash != three.result.hash, "--latency-ticks 0 and 3 are different matches");
        o.ticks = 0;
        o.latency = 3;
        t.check(spec_of(o, tiny, job, false).max_ticks == 0, "--ticks full means no limit");
        // a result that is not the same is not the same: every number is compared
        const ai::ArenaResult base = three.result;
        t.check(same_match(base, base), "a result equals itself");
        const std::vector<std::pair<const char*, std::function<void(ai::ArenaResult&)>>> changes = {
            {"hash", [](ai::ArenaResult& r) { r.hash ^= 1; }},
            {"ticks", [](ai::ArenaResult& r) { ++r.ticks; }},
            {"steps", [](ai::ArenaResult& r) { ++r.steps; }},
            {"checkpoints", [](ai::ArenaResult& r) { r.checkpoints.push_back(7); }},        // (run_job dropped the checkpoints of `base`: one more is a difference)
            {"score", [](ai::ArenaResult& r) { ++r.seats[1].score; }},
            {"shown score", [](ai::ArenaResult& r) { ++r.seats[0].shown_score; }},
            {"ants", [](ai::ArenaResult& r) { ++r.seats[0].ants; }},
            {"banked", [](ai::ArenaResult& r) { ++r.seats[1].banked; }},
            {"kills", [](ai::ArenaResult& r) { ++r.seats[1].kills; }},
            {"released", [](ai::ArenaResult& r) { ++r.seats[0].stats.released; }},
            {"decisions", [](ai::ArenaResult& r) { ++r.seats[0].stats.decisions; }},
            {"rejected", [](ai::ArenaResult& r) { ++r.seats[1].stats.rejected; }},
            {"audio events", [](ai::ArenaResult& r) { ++r.audio_events; }},
            {"units left on reachable piles", [](ai::ArenaResult& r) { ++r.reachable_units_left; }},
            {"seat count", [](ai::ArenaResult& r) { r.seats.pop_back(); }},
        };
        for (const auto& c : changes) {
            ai::ArenaResult other = base;
            c.second(other);
            t.check(!same_match(base, other), std::string("a result that differs in its ") + c.first + " is not the same match");
        }
        // --repeat: a match that is not reproducible is found, one that is reproducible passes, and the plays are counted as they are played
        o.repeat = 3;
        const MatchReport good = run_job(o, maps, job, selftest_factory);
        t.check(good.repeat_ok && good.ok() && good.plays == 3, "a reproducible match is played 3 times and passes");
        std::atomic<uint32_t> counter{0};
        const auto drifting = [&counter](const ai::BotSpec&) { return std::unique_ptr<ai::Bot>(new SelftestDrifter(counter.fetch_add(1))); };
        o.ticks = 300;
        const MatchReport drift = run_job(o, maps, job, drifting);
        t.check(!drift.repeat_ok && !drift.ok() && drift.plays == 3 && !drift.repeat_note.empty(), "a match that is not reproducible is found by --repeat and fails");
        const std::vector<MatchReport> as_one = {drift};
        t.check(report_json(o, maps, as_one).find("\"identical\": false") != std::string::npos, "and the report says so");
        // the report keeps the number of commands and drops the log and the checkpoints
        o.repeat = 1;
        o.replay_check = true;
        const MatchReport checked = run_job(o, maps, job, selftest_factory);
        t.check(checked.replay_checked && checked.replay.ok && checked.commands > 20 && checked.result.log.empty() && checked.result.checkpoints.empty() && checked.result.ticks == 300,
                "a match that was replay-checked keeps the number of its commands, not the commands");
        // a replay that fails is reported as such, with its first bad tick
        std::vector<MatchReport> broken = {checked};
        broken[0].replay.ok = false;
        broken[0].replay.first_bad_tick = 140;
        const JsonValue* replay_of = nullptr;
        JsonValue tree;
        const std::string text = report_json(o, maps, broken);
        t.check(JsonReader(text).parse(tree) && tree.get("matches") != nullptr && !tree.get("matches")->items.empty(), "the report of a failed replay can be read back");
        if (tree.get("matches") != nullptr && !tree.get("matches")->items.empty()) replay_of = tree.get("matches")->items[0].get("replay");
        t.check(replay_of != nullptr && replay_of->get("ok") != nullptr && !replay_of->get("ok")->b && replay_of->get("first_bad_tick") != nullptr && replay_of->get("first_bad_tick")->u64() == 140 &&
                    replay_of->get("commands") != nullptr && replay_of->get("commands")->u64() == checked.commands,
                "the replay block of the report says: failed, the first bad tick, the commands");
    }

    t.section("text: JSON quoting, seeds, map names that are paths");
    {
        t.check(json_quote("a\"b\\c\nd") == "\"a\\\"b\\\\c\\u000ad\"", "json_quote escapes the quote, the backslash and a newline");
        JsonValue v;
        t.check(JsonReader(json_quote("say \"hi\" \\ there")).parse(v) && v.kind == JsonValue::Kind::String && v.text == "say \"hi\" \\ there", "a quoted string reads back as it was");
        t.check(seeds_text({4294967295u, 0u}) == "4294967295,0", "the seeds 4294967295 and 0 are two seeds, not a range");
        t.check(seeds_text({4294967294u, 4294967295u}) == "4294967294..4294967295", "but 4294967294 and 4294967295 are a range");
        t.check(seeds_text({1, 2, 3, 7, 9, 10}) == "1..3,7,9..10", "ranges and singles");
        t.check(stem_of_path("/a/b/NOPE.LVL") == "NOPE" && stem_of_path("C:\\Users\\x\\Maps\\NOPE.lvl") == "NOPE" && stem_of_path("NOPE.LVL") == "NOPE" && stem_of_path("a.b/c") == "c" && stem_of_path("c/") == "",
                "the name of a map given as a path is its file name without folders and extension, for either kind of folder separator");
        TempDir dir;
        t.check(dir.ok(), "a scratch folder");
        if (dir.ok()) {
            // a garbage file that is no map
            {
                std::ofstream f(dir.file("BROKEN.LVL"), std::ios::binary);
                f << "this is not a level";
            }
            Options o;
            o.maps = {fs::path(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/TINY.LVL").string(), dir.file("NOPE_A.LVL"), "C:\\Users\\someone\\private\\NOPE_B.LVL", dir.file("BROKEN.LVL"), "NOSUCHNAME"};
            const std::vector<LoadedMap> maps = load_maps(o);
            t.check(maps.size() == 5 && maps[0].ok && maps[0].name == "TINY", "a map given as a path that exists loads and is named by its file");
            t.check(!maps[1].ok && maps[1].name == "NOPE_A" && maps[1].error == "map 'NOPE_A' not found", "a path that does not exist is named by its file name only");
            t.check(!maps[2].ok && maps[2].name == "NOPE_B" && maps[2].error == "map 'NOPE_B' not found", "also a Windows-style path");
            t.check(!maps[3].ok && maps[3].name == "BROKEN" && maps[3].error == "map 'BROKEN' does not load", "a file that is no map: does not load, named by its file name");
            t.check(!maps[4].ok && maps[4].name == "NOSUCHNAME", "a name that is no map");
            o.seats = {spec_for(0, "idle", ai::Level::Medium)};
            o.seats_given = true;
            o.seeds = {1};
            o.ticks = 20;
            o.wall_time = false;
            std::vector<Job> jobs;
            std::string why;
            t.check(make_jobs(o, maps.size(), jobs, why), "the jobs of the five maps");
            const std::vector<MatchReport> reports = run_jobs(o, maps, jobs, selftest_factory, false);
            const std::string json = report_json(o, maps, reports);
            const std::string folder = dir.path.string();
            t.check(JsonChecker(json).valid(), "the report of maps given as paths is valid JSON");
            t.check(json.find(folder) == std::string::npos && json.find("Users") == std::string::npos && json.find("someone") == std::string::npos && json.find("private") == std::string::npos &&
                        json.find(ORIGINAL_ASSETS_DIR) == std::string::npos && json.find('\\') == std::string::npos && json.find('/') == std::string::npos && json.find(".LVL") == std::string::npos,
                    "no folder of any path is in the report, found or not");
            std::string lines;
            for (const MatchReport& r : reports) lines += r.result.error;
            t.check(lines.find(folder) == std::string::npos && lines.find("someone") == std::string::npos, "nor in the errors");
        }
    }

    t.section("the tool's limits: too many matches are refused before any is built, the pool really runs threads");
    {
        Options o;
        o.maps = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"};
        o.seats = {spec_for(0, "idle", ai::Level::Easy), spec_for(1, "worker", ai::Level::Medium), spec_for(2, "standard", ai::Level::Hard), spec_for(3, "standard", ai::Level::Medium)};
        o.seats_given = true;
        o.rotate = true;                                                        // 24 arrangements of four different bots
        std::string err;
        t.check(parse_seeds("1..199999", o.seeds, err), "a seed range just under the limit");
        std::vector<Job> jobs;
        std::string why;
        t.check(!make_jobs(o, o.maps.size(), jobs, why) && jobs.empty() && jobs.capacity() == 0 && why.find("too many") != std::string::npos,
                "6 maps x 199,999 seeds x 24 arrangements (28.8 million matches) is refused with nothing allocated");
        o.rotate = false;
        o.repeat = 16;
        t.check(!make_jobs(o, o.maps.size(), jobs, why) && jobs.capacity() == 0, "a repeat that takes it over the limit is counted too (6 x 199,999 x 1 x 16)");
        o.seeds = {1, 2};
        o.maps = {"TINY"};
        o.repeat = 1;
        t.check(make_jobs(o, o.maps.size(), jobs, why) && jobs.size() == 2, "a small run is made");
        o.maps.assign(10, "TINY");
        o.seeds.assign(20000, 1u);
        o.repeat = 1;
        t.check(make_jobs(o, o.maps.size(), jobs, why) && jobs.size() == 200000, "exactly the limit is allowed");
        o.seeds.push_back(1);
        t.check(!make_jobs(o, o.maps.size(), jobs, why) && jobs.empty(), "one more is not");
        // the pool: with 4 threads the matches are played by at least 2 threads at the same time (a bot made for the match waits until a second thread has also arrived)
        struct Rendezvous {
            std::mutex m;
            std::condition_variable cv;
            std::set<std::thread::id> ids;
            bool gave_up{false};
        } rv;
        const auto meeting = [&rv](const ai::BotSpec& spec) {
            std::unique_lock<std::mutex> lock(rv.m);
            rv.ids.insert(std::this_thread::get_id());
            rv.cv.notify_all();
            if (!rv.gave_up && !rv.cv.wait_for(lock, std::chrono::seconds(10), [&rv] { return rv.ids.size() >= 2; })) rv.gave_up = true;
            lock.unlock();
            return selftest_factory(spec);
        };
        Options p;
        p.maps = {"TINY"};
        p.seats = {spec_for(0, "worker", ai::Level::Hard)};
        p.seats_given = true;
        p.seeds = {1, 2, 3, 4, 5, 6};
        p.ticks = 40;
        p.threads = 4;
        std::vector<LoadedMap> one_map(1);
        one_map[0].name = tiny.name;
        one_map[0].ok = true;
        one_map[0].level = tiny.level;
        std::vector<Job> pool_jobs;
        make_jobs(p, 1, pool_jobs, why);
        const std::vector<MatchReport> played = run_jobs(p, one_map, pool_jobs, meeting, false);
        t.check(played.size() == 6 && rv.ids.size() >= 2 && !rv.gave_up, "four threads were asked for and at least two ran matches at the same time");
        size_t good = 0;
        for (const MatchReport& r : played) good += r.ok() ? 1u : 0u;
        t.check(good == 6, "and all six matches were played");
    }
}

// The tool end to end on a scratch folder: the exit codes, the report that is written and read back, what happens to a report that cannot be written
void selftest_tool(SelfTest& t) {
    t.section("the tool end to end: exit codes, the report written and read back, a report that cannot be written");
    TempDir dir;
    t.check(dir.ok(), "a scratch folder");
    if (!dir.ok()) return;
    const auto factory = [](const ai::BotSpec& spec) -> std::unique_ptr<ai::Bot> {
        if (spec.kind == "worker") return std::make_unique<SelftestHarvester>();
        return ai::make_bot(spec);
    };
    Options o;
    o.maps = {"TINY"};
    o.seats = {spec_for(0, "worker", ai::Level::Hard), spec_for(1, "worker", ai::Level::Hard), spec_for(2, "idle", ai::Level::Medium)};
    o.seats_given = true;
    o.seeds = {1};
    o.ticks = 2400;
    o.quiet = true;
    o.wall_time = false;
    o.replay_check = true;
    o.out = dir.file("report.json");
    const ToolRun ok = run_tool_quietly(o, dir, factory);
    t.check(ok.code == 0 && ok.out.find("every check passed") != std::string::npos, "a run that passes exits 0 and says so");
    const std::string text = read_file(o.out);
    JsonValue tree;
    t.check(!text.empty() && JsonChecker(text).valid() && JsonReader(text).parse(tree), "the report file is there, valid JSON, and reads back");
    const JsonValue* matches = tree.get("matches");
    const JsonValue* summary = tree.get("summary");
    t.check(tree.get("tool") != nullptr && tree.get("tool")->text == "bot_arena" && tree.get("format") != nullptr && tree.get("format")->u64() == 2 && matches != nullptr && matches->items.size() == 1 &&
                summary != nullptr && summary->get("matches") != nullptr && summary->get("matches")->u64() == 1 && summary->get("failures") != nullptr && summary->get("failures")->u64() == 0,
            "the tool, the format and the summary");
    // the numbers of the report are the numbers of the match: play the same match in-process and compare every field
    const std::vector<LoadedMap> loaded = load_maps(o);
    ai::ArenaSpec spec;
    spec.level = &loaded[0].level;
    spec.seed = 1;
    spec.bots = o.seats;
    spec.max_ticks = o.ticks;
    spec.latency_ticks = o.latency;
    spec.factory = factory;
    const ai::ArenaResult played = ai::play_match(spec);
    bool fields_ok = matches != nullptr && matches->items.size() == 1;
    if (fields_ok) {
        const JsonValue& m = matches->items[0];
        fields_ok = m.get("map") != nullptr && m.get("map")->text == "TINY" && m.get("ok") != nullptr && m.get("ok")->b && m.get("ticks") != nullptr && m.get("ticks")->u64() == played.ticks &&
                    m.get("hash") != nullptr && m.get("hash")->text == hex64(played.hash) && m.get("seats") != nullptr && m.get("seats")->items.size() == played.seats.size();
        for (size_t i = 0; fields_ok && i < played.seats.size(); ++i) {
            const JsonValue& s = m.get("seats")->items[i];
            const ai::ArenaSeatResult& r = played.seats[i];
            const auto num = [&s](const char* key) { return s.get(key) != nullptr ? s.get(key)->i64() : int64_t{-12345}; };
            fields_ok = num("seat") == r.spec.seat && num("score") == r.score && num("shown_score") == r.shown_score && num("ants") == r.ants && num("eggs") == r.eggs && num("hatched") == r.hatched &&
                        num("banked") == r.banked && num("raided") == r.raided && num("kills") == r.kills && num("losses") == r.losses && num("decisions") == r.stats.decisions &&
                        num("released") == r.stats.released && num("intents") == r.stats.intents && num("expired") == r.stats.expired && num("rejected") == r.stats.rejected &&
                        s.get("bot") != nullptr && s.get("bot")->text == spec_text(r.spec) && s.get("runs") != nullptr && s.get("runs")->text == r.runs;
        }
        const JsonValue* replay = m.get("replay");
        fields_ok = fields_ok && replay != nullptr && replay->get("ok") != nullptr && replay->get("ok")->b && replay->get("commands") != nullptr && replay->get("commands")->u64() > 20;
    }
    t.check(fields_ok, "every field of the report equals the match it describes (scores, counts, hash, replay)");
    t.check(played.seats.size() == 3 && played.seats[0].score > 0 && played.seats[0].banked == static_cast<uint32_t>(played.seats[0].score) && played.seats[2].score == 0, "(and the harvesting seats really scored, so the fields above are not all zero)");
    // the defaults of the command line mean four standard bots at medium level
    Options d;
    std::string err;
    bool defaults = parse_args({}, d, err) && d.seats.size() == 4;
    for (size_t i = 0; defaults && i < 4; ++i) defaults = d.seats[i].seat == i && d.seats[i].kind == "standard" && d.seats[i].level == ai::Level::Medium;
    t.check(defaults, "the default seats are four standard bots of medium level");
    // a map that is not there: exit 1, the finding in the report, no folder in it
    Options missing = o;
    missing.maps = {dir.file("NOT_THERE.LVL")};
    missing.out = dir.file("missing.json");
    missing.replay_check = false;
    const ToolRun lost = run_tool_quietly(missing, dir, factory);
    JsonValue lost_tree;
    t.check(lost.code == 1 && lost.out.find("FAILED") != std::string::npos, "a map that is not there is a finding: exit 1");
    t.check(JsonReader(read_file(missing.out)).parse(lost_tree) && lost_tree.get("matches") != nullptr && lost_tree.get("matches")->items.size() == 1 &&
                lost_tree.get("matches")->items[0].get("ok") != nullptr && !lost_tree.get("matches")->items[0].get("ok")->b && read_file(missing.out).find(dir.path.string()) == std::string::npos,
            "and it is in the report as a match that was not played, without the folder");
    // a report that cannot be opened is found BEFORE the first match
    Options unwritable = o;
    unwritable.out = (dir.path / "no_such_folder" / "report.json").string();
    const ToolRun refused = run_tool_quietly(unwritable, dir, factory);
    t.check(refused.code == 2 && refused.err.find("cannot write") != std::string::npos && refused.out.find("match(es)") == std::string::npos, "an unwritable --out is exit 2 and no match was played");
#if defined(__unix__) || defined(__APPLE__)
    // a report that breaks while it is being written (a size limit does what a full disk does) is exit 2, not a truncated file with exit 0
    {
        Options cut_options = o;
        cut_options.seats = {spec_for(0, "worker", ai::Level::Hard), spec_for(1, "idle", ai::Level::Medium)};
        cut_options.ticks = 40;
        cut_options.replay_check = false;
        cut_options.out = dir.file("cut.json");
        const ToolRun whole = run_tool_quietly(cut_options, dir, factory);                  // without the limit: a good report of more than 1,500 bytes
        const std::string whole_text = read_file(cut_options.out);
        rlimit old_limit{};
        const bool have_limit = getrlimit(RLIMIT_FSIZE, &old_limit) == 0 && old_limit.rlim_max >= 700;
        t.check(whole.code == 0 && whole_text.size() > 1500 && have_limit, "(a report that fits: exit 0, more than 1,500 bytes)");
        if (have_limit) {
            void (*old_handler)(int) = std::signal(SIGXFSZ, SIG_IGN);
            rlimit small = old_limit;
            small.rlim_cur = 700;
            setrlimit(RLIMIT_FSIZE, &small);
            const ToolRun cut = run_tool_quietly(cut_options, dir, factory);
            setrlimit(RLIMIT_FSIZE, &old_limit);
            std::signal(SIGXFSZ, old_handler);
            t.check(cut.code == 2 && cut.err.find("completely") != std::string::npos && read_file(cut_options.out).size() <= 700, "a report that breaks after 700 bytes is exit 2 and says so");
        }
    }
#endif
    // too many matches: exit 2 before anything is built or played
    Options many = o;
    many.maps.assign(10, "TINY");
    many.out = "";
    many.seeds.assign(30000, 1u);
    const ToolRun refused_many = run_tool_quietly(many, dir, factory);
    t.check(refused_many.code == 2 && refused_many.err.find("too many") != std::string::npos && refused_many.out.find("match(es)") == std::string::npos, "300,000 matches: exit 2 and none was played");
    // a report that cannot be written to the end: the stream says so, whenever it breaks
    {
        std::vector<LoadedMap> maps = load_maps(o);
        std::vector<Job> jobs;
        std::string why;
        o.seeds = {1, 2, 3, 4, 5};
        o.ticks = 40;
        o.replay_check = false;
        make_jobs(o, maps.size(), jobs, why);
        const std::vector<MatchReport> reports = run_jobs(o, maps, jobs, factory, false);
        LimitedBuf unlimited(1u << 30);
        std::ostream all(&unlimited);
        t.check(write_report(all, o, maps, reports), "a report written to a good stream is a success");
        const std::string whole = report_json(o, maps, reports);
        t.check(unlimited.bytes == whole.size(), "and it is the same bytes as the report as text");
        t.check(unlimited.writes >= 4 && unlimited.biggest * 2 < whole.size(), "it is written match by match, not as one string");
        for (const size_t limit : {size_t{0}, size_t{10}, whole.size() / 2, whole.size() - 2}) {
            LimitedBuf small(limit);
            std::ostream cut(&small);
            t.check(!write_report(cut, o, maps, reports), "a stream that breaks after " + std::to_string(limit) + " bytes is a failure");
        }
    }
}

int selftest() {
    SelfTest t;
    const Clock::time_point started = Clock::now();
    std::printf("bot_arena selftest\n");
    Options base;
    base.maps = {"TINY"};
    LoadedMap tiny;
    {
        std::vector<LoadedMap> m = load_maps(base);
        if (m.empty() || !m[0].ok) {
            std::printf("  the shipped map TINY is missing or does not load\n");
            return 1;
        }
        tiny.name = m[0].name;
        tiny.ok = true;
        tiny.level = std::move(m[0].level);
    }

    t.section("the command line");
    {
        std::vector<uint32_t> seeds;
        std::string err;
        t.check(parse_seeds("1..3", seeds, err) && seeds == std::vector<uint32_t>({1, 2, 3}), "seeds 1..3");
        t.check(parse_seeds("1,4,9..10", seeds, err) && seeds == std::vector<uint32_t>({1, 4, 9, 10}) && seeds_text(seeds) == "1,4,9..10", "seeds 1,4,9..10 and their text");
        t.check(!parse_seeds("3..1", seeds, err) && !parse_seeds("x", seeds, err) && !parse_seeds("", seeds, err) && !parse_seeds("1..", seeds, err), "bad seeds are refused");
        ai::BotSpec s;
        t.check(parse_seat("2=worker:easy", s, err) && s.seat == 2 && s.kind == "worker" && s.level == ai::Level::Easy, "--seat 2=worker:easy");
        t.check(parse_seat("0=hard", s, err) && s.seat == 0 && s.kind == "standard" && s.level == ai::Level::Hard, "--seat 0=hard");
        t.check(!parse_seat("4=idle", s, err) && !parse_seat("1=bogus", s, err), "a seat out of range and an unknown kind are refused");
        Options o;
        t.check(parse_args({"--map", "TINY,SMALL", "--seeds", "1..2", "--seat", "1=idle", "--seat", "0=worker:hard", "--ticks", "300", "--latency-ticks", "0", "--rotate", "--repeat", "2",
                            "--replay-check", "--threads", "3", "--no-wall-time"}, o, err), "a full command line parses");
        t.check(o.maps.size() == 2 && o.seats.size() == 2 && o.seats[0].seat == 0 && o.ticks == 300 && o.latency == 0 && o.rotate && o.repeat == 2 && o.threads == 3 && !o.wall_time, "and means what it says");
        Options d;
        t.check(parse_args({}, d, err) && d.maps == std::vector<std::string>({"TINY"}) && d.seats.size() == 4 && d.latency == 3 && d.ticks == 0 && d.threads == 1, "the defaults: TINY, four standard bots, latency 3, full length");
        Options wb;
        t.check(parse_args({"--write-baselines", "--threads", "2"}, wb, err) && wb.write_baselines && wb.threads == 2 && !d.write_baselines, "--write-baselines is an option of its own (off by default)");
        Options bad;
        t.check(!parse_args({"--seat", "0=idle", "--seat", "0=hard"}, bad, err) && !parse_args({"--ticks", "0"}, bad, err) && !parse_args({"--wat"}, bad, err) &&
                    !parse_args({"--threads", "0"}, bad, err) && !parse_args({"--map"}, bad, err),
                "two bots on one seat, 0 ticks, an unknown option, 0 threads and a missing value are refused");
        Options out_given;
        t.check(parse_args({"--out", "report.json"}, out_given, err) && out_given.out == "report.json" && !parse_args({"--out", ""}, bad, err) && !parse_args({"--out"}, bad, err),
                "--out takes a file name; an empty one (an unset shell variable) is refused, not taken for 'no report'");
    }

    t.section("the table of baselines (tests/test_ai/baselines.inc)");
    {
        ai::BaselineRow row;
        row.map = "TINY";
        row.level = ai::Level::Hard;
        row.solo_2min = {600, 705, 600, 615};
        row.solo_full = {1725, 1980, 1785, 1800};
        row.four_full = {1130, 1350, 1140, 1195};
        row.four_sum = 4815;
        row.pot = 4800;
        t.check(ai::baseline_line(row) == "{\"TINY\", Level::Hard, {600, 705, 600, 615}, {1725, 1980, 1785, 1800}, {1130, 1350, 1140, 1195}, 4815, 4800},", "a row is written the way the test includes it");
        ai::BaselineRow islands;
        islands.map = "ISLANDS";
        islands.level = ai::Level::Easy;
        t.check(ai::baseline_line(islands) == "{\"ISLANDS\", Level::Easy, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0},", "ISLANDS is a row of zeros");
        t.check(ai::kBaselineLatency == 3 && ai::kBaselineShortTicks == 2400 && sizeof(ai::kBaselineSeeds) / sizeof(ai::kBaselineSeeds[0]) == 2 && ai::kBaselineSeeds[0] == 1 && ai::kBaselineSeeds[1] == 2,
                "the procedure: latency 3, two minutes, the seeds 1 and 2");
    }

    t.section("arrangements of the seats");
    {
        const auto bots = [](std::initializer_list<const char*> kinds) {
            std::vector<ai::BotSpec> v;
            uint8_t seat = 0;
            for (const char* k : kinds) v.push_back(spec_for(seat++, "standard", std::string(k) == "e" ? ai::Level::Easy : std::string(k) == "m" ? ai::Level::Medium : ai::Level::Hard));
            return v;
        };
        t.check(arrangements(bots({"e", "m", "h", "e"}), false).size() == 1, "without --rotate one arrangement");
        t.check(arrangements(bots({"e", "m"}), true).size() == 2, "two different bots: 2 arrangements");
        t.check(arrangements(bots({"e", "m", "h"}), true).size() == 6, "three different bots: 6");
        t.check(arrangements(bots({"e", "m", "h", "e"}), true).size() == 12, "four bots, two equal: 12");
        t.check(arrangements(bots({"e", "m", "h", "h"}), true).size() == 12, "four bots, two equal (another pair): 12");
        t.check(arrangements(bots({"e", "e", "m", "m"}), true).size() == 6, "two pairs: 6");
        t.check(arrangements(bots({"e", "e", "e", "e"}), true).size() == 1, "four equal bots: 1");
        std::vector<ai::BotSpec> distinct = bots({"e", "m", "h"});
        distinct.push_back(spec_for(3, "idle", ai::Level::Medium));
        t.check(arrangements(distinct, true).size() == 24, "four different bots: 24");
        const auto four = bots({"e", "m", "h", "h"});
        const auto all = arrangements(four, true);
        t.check(all.front()[0].level == ai::Level::Easy && all.front()[1].level == ai::Level::Medium, "the given arrangement comes first");
        bool seats_ok = true;
        for (const auto& a : all) {
            for (size_t i = 0; i < a.size(); ++i) seats_ok = seats_ok && a[i].seat == four[i].seat;
        }
        t.check(seats_ok, "every arrangement uses the given seats");
    }

    t.section("one match: determinism, the replay without a bot, and a replay that notices");
    {
        ai::ArenaSpec s;
        s.level = &tiny.level;
        s.seed = 3;
        s.bots = {spec_for(0, "worker", ai::Level::Hard), spec_for(1, "worker", ai::Level::Hard), spec_for(2, "idle", ai::Level::Medium), spec_for(3, "idle", ai::Level::Medium)};
        s.max_ticks = 1500;
        s.latency_ticks = 3;
        s.record = true;
        s.factory = selftest_factory;
        const ai::ArenaResult a = ai::play_match(s);
        const ai::ArenaResult b = ai::play_match(s);
        t.check(a.error.empty() && a.ticks == 1500 && !a.match_over && a.initial_ticks == 7200, "TINY plays 1500 of its 7200 ticks");
        t.check(a.log.size() >= 40 && a.checkpoints.size() == 75, "the walkers gave commands and the hash was sampled 75 times");
        t.check(a.hash == b.hash && a.checkpoints == b.checkpoints && a.log.size() == b.log.size(), "the same arguments give the same match");
        const ai::ReplayResult rp = ai::replay_commands(s, a);
        t.check(rp.ok && rp.hash == a.hash, "the commands alone, without any bot, replay the match to the same hash at every 20th tick and at the end");
        ai::ArenaSpec other = s;
        other.seed = 4;
        const ai::ArenaResult c = ai::play_match(other);
        t.check(c.hash != a.hash, "another seed gives another match");
        if (a.log.size() >= 40 && a.checkpoints.size() == 75) {                       // (without the precondition the checks below would index an empty log: report, do not crash)
            ai::ArenaResult dropped = a;
            dropped.log.erase(dropped.log.begin() + static_cast<std::ptrdiff_t>(dropped.log.size() / 2));
            t.check(!ai::replay_commands(s, dropped).ok, "a replay with one command missing is noticed");
            ai::ArenaResult moved = a;
            moved.log[moved.log.size() / 2].command.tile_x = static_cast<int16_t>(moved.log[moved.log.size() / 2].command.tile_x + 5);
            t.check(!ai::replay_commands(s, moved).ok, "a replay with one command changed is noticed");
            ai::ArenaResult late = a;
            late.log[late.log.size() / 2].step += 7;
            t.check(!ai::replay_commands(s, late).ok, "a replay with one command at another step is noticed");
            ai::ArenaResult added = a;
            ai::RecordedCommand more = a.log.front();                                   // the issuer and the ant of one command belong together, so the added command reaches the engine
            more.command.tile_x = 2;
            more.command.tile_y = 2;
            more.step = 40;
            more.tick = 40;
            added.log.insert(added.log.begin(), more);
            std::stable_sort(added.log.begin(), added.log.end(), [](const ai::RecordedCommand& x, const ai::RecordedCommand& y) { return x.step < y.step; });
            t.check(!ai::replay_commands(s, added).ok, "a replay with one command added is noticed");
            ai::ArenaResult wrong_hash = a;
            wrong_hash.hash ^= 1;
            t.check(!ai::replay_commands(s, wrong_hash).ok, "a replay whose last hash is wrong is noticed");
            ai::ArenaResult wrong_checkpoint = a;
            wrong_checkpoint.checkpoints[30] ^= 1;
            const ai::ReplayResult bad = ai::replay_commands(s, wrong_checkpoint);
            t.check(!bad.ok && bad.first_bad_tick == 31 * ai::kArenaHashPeriod, "a replay with a wrong checkpoint is noticed, and names its tick");
            ai::ArenaResult extra_checkpoint = a;
            extra_checkpoint.checkpoints.push_back(a.hash);
            t.check(!ai::replay_commands(s, extra_checkpoint).ok, "a replay that was promised one more checkpoint than it can make is noticed");
            ai::ArenaResult after_end = a;
            ai::RecordedCommand last = a.log.back();
            last.step = a.steps + 3;
            after_end.log.push_back(last);
            t.check(!ai::replay_commands(s, after_end).ok, "a command that was recorded after the last step is noticed");
        }
        ai::ArenaSpec direct = s;
        direct.latency_ticks = 0;
        const ai::ArenaResult d = ai::play_match(direct);
        t.check(d.error.empty() && ai::replay_commands(direct, d).ok, "latency 0: the commands are applied at once, and replay as well");
        t.check(d.hash != a.hash, "the sink latency changes the match (so it is part of the arguments)");
        // the counters of the controller reach the result, and latency 0 applies every released command at once (nothing waits at the end), latency 3 all but the last few
        bool counted = true;
        bool direct_all = true;
        bool room_most = true;
        for (const ai::ArenaSeatResult& seat : a.seats) {
            size_t applied = 0;
            for (const ai::RecordedCommand& rc : a.log) applied += rc.command.issuer == seat.spec.seat ? 1u : 0u;
            if (seat.spec.kind == "worker") counted = counted && seat.stats.released > 10 && seat.stats.decisions > 100;
            room_most = room_most && applied <= seat.stats.released && applied + 4 >= seat.stats.released;
        }
        for (const ai::ArenaSeatResult& seat : d.seats) {
            size_t applied = 0;
            for (const ai::RecordedCommand& rc : d.log) applied += rc.command.issuer == seat.spec.seat ? 1u : 0u;
            direct_all = direct_all && applied == seat.stats.released;
        }
        t.check(counted, "the walkers' counters (decisions, commands released) are in the result");
        t.check(direct_all, "latency 0: every released command is applied, none waits");
        t.check(room_most, "latency 3: every command but the last few is applied");
        // the whole match, ended by the engine's clock: the call that ends it does not advance the tick count
        ai::ArenaSpec full;
        full.level = &tiny.level;
        full.seed = 1;
        full.bots = {spec_for(0, "worker", ai::Level::Medium), spec_for(1, "idle", ai::Level::Medium)};
        full.record = true;
        full.factory = selftest_factory;
        const ai::ArenaResult f = ai::play_match(full);
        t.check(f.error.empty() && f.match_over && f.steps == f.ticks + 1 && f.ticks >= f.initial_ticks, "a full match ends by the clock");
        t.check(ai::replay_commands(full, f).ok, "and its replay, including the call that ends it, is identical");
    }

    t.section("refusals");
    {
        ai::ArenaSpec s;
        s.level = &tiny.level;
        t.check(!ai::play_match(s).error.empty(), "no bot: refused");
        s.bots = {spec_for(0, "idle", ai::Level::Medium), spec_for(0, "idle", ai::Level::Medium)};
        t.check(!ai::play_match(s).error.empty(), "two bots on one seat: refused");
        s.bots = {spec_for(0, "nonsense", ai::Level::Medium)};
        t.check(!ai::play_match(s).error.empty(), "an unknown kind: refused");
        s.level = nullptr;
        s.bots = {spec_for(0, "idle", ai::Level::Medium)};
        t.check(!ai::play_match(s).error.empty(), "no map: refused");
        // a seat that does not exist, and a map that the seated teams cannot play (a start marker outside the grid)
        s.level = &tiny.level;
        s.bots = {spec_for(0, "idle", ai::Level::Medium), spec_for(32, "idle", ai::Level::Medium)};
        t.check(!ai::play_match(s).error.empty(), "a seat 32: refused");
        assets::LevelData broken = tiny.level;
        for (assets::AnthillSpawn& sp : broken.anthill_spawns) {
            if (sp.team_id < sim::MAX_PLAYERS) {
                sp.x = 999;
                break;
            }
        }
        s.level = &broken;
        s.max_ticks = 30;
        s.bots = {spec_for(0, "idle", ai::Level::Medium), spec_for(1, "idle", ai::Level::Medium), spec_for(2, "idle", ai::Level::Medium), spec_for(3, "idle", ai::Level::Medium)};
        const ai::ArenaResult unplayable = ai::play_match(s);
        t.check(unplayable.error.find("cannot be played") != std::string::npos && unplayable.ticks == 0, "a map that the seated teams cannot play: refused with the reason");
    }

    t.section("the tool: matches in order, threads, repeat, report");
    {
        Options o;
        o.maps = {"TINY", "SMALL", "NOSUCHMAP"};
        o.seeds = {1, 2};
        o.seats = {spec_for(0, "worker", ai::Level::Hard), spec_for(1, "idle", ai::Level::Medium)};
        o.seats_given = true;
        o.ticks = 900;
        o.latency = 3;
        o.rotate = true;
        o.repeat = 2;
        o.replay_check = true;
        o.wall_time = false;
        const std::vector<LoadedMap> maps = load_maps(o);
        t.check(maps.size() == 3 && maps[0].ok && maps[1].ok && !maps[2].ok, "TINY and SMALL load, a name that is no map does not");
        std::vector<Job> jobs;
        std::string jobs_error;
        t.check(make_jobs(o, maps.size(), jobs, jobs_error), "the jobs of a small run are made");
        t.check(jobs.size() == 3u * 2u * 2u, "3 maps x 2 seeds x 2 arrangements = 12 matches");
        o.threads = 1;
        const std::vector<MatchReport> one = run_jobs(o, maps, jobs, selftest_factory, false);
        o.threads = 4;
        const std::vector<MatchReport> four = run_jobs(o, maps, jobs, selftest_factory, false);
        const std::string json1 = report_json(o, maps, one);
        const std::string json4 = report_json(o, maps, four);
        t.check(json1 == json4, "one thread and four threads give the same report, byte for byte");
        t.check(one.size() == 12 && one[0].job.map == 0 && one[0].job.seed == 1 && one[0].job.rotation == 0 && one[1].job.rotation == 1 && one[2].job.seed == 2 && one[4].job.map == 1, "the reports are in the order map, seed, arrangement");
        size_t played = 0;
        size_t good = 0;
        bool replays = true;
        bool repeats = true;
        for (const MatchReport& r : one) {
            if (!r.result.error.empty()) continue;
            ++played;
            good += r.ok() ? 1u : 0u;
            replays = replays && r.replay_checked && r.replay.ok && r.commands > 0;
            repeats = repeats && r.repeat_ok && r.plays == 2;
        }
        t.check(played == 8 && good == 8, "the 8 matches on real maps were played and passed every check");
        t.check(replays, "each one was replay-checked, and had commands");
        t.check(repeats, "each one was played twice with the same result");
        t.check(one[8].result.error.find("not found") != std::string::npos && !one[8].ok(), "the missing map is a finding, not a crash");
        t.check(one[0].result.hash != one[1].result.hash, "the two arrangements of the same seed are different matches");
        t.check(JsonChecker(json1).valid(), "the JSON report is valid JSON");
        t.check(json1.find("/Users/") == std::string::npos && json1.find(ORIGINAL_ASSETS_DIR) == std::string::npos && json1.find('\\') == std::string::npos && json1.find(".LVL") == std::string::npos,
                "the JSON report holds no path");
        t.check(json1.find(kKindsNote) != std::string::npos, "the report says what the kinds are (worker is the yardstick, standard the standard bot)");
        t.check(json1.find("\"wall_ms\"") == std::string::npos, "no wall time with --no-wall-time");
        o.wall_time = true;
        t.check(report_json(o, maps, one).find("\"wall_ms\"") != std::string::npos, "wall times with the default");
        // a replay that fails is reported as a failed match
        t.check(json1.find("\"failures\": 4") != std::string::npos, "the summary counts the 4 matches of the missing map as failures");
        std::vector<MatchReport> broken = one;
        broken[0].replay.ok = false;
        t.check(!broken[0].ok() && report_json(o, maps, broken).find("\"failures\": 5") != std::string::npos, "a replay that differs is one more failure");
    }

    selftest_wiring(t, tiny);
    selftest_tool(t);

    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    std::printf("bot_arena selftest: %d checks, %d failures, %.1f s\n", t.checks, t.failures, seconds);
    return t.failures == 0 ? 0 : 1;
}

}  // namespace

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
