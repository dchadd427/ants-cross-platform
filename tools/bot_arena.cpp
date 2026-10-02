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
//   --seat N=SPEC     a bot on seat N (0 green, 1 red, 2 blue, 3 black); SPEC is KIND, KIND:LEVEL or LEVEL: KIND idle, worker, standard; LEVEL easy, medium, hard.
//                     Repeat it for every seat that plays. Default: four standard bots at medium level. A seat that is not named has no hill and no ants.
//                     idle stands still; worker harvests (B3); standard is an ALIAS of worker until the standard bot with its tactics arrives with B4 (every report says so).
//   --ticks full|N    play until the match is over (the map's own length, default) or at most N ticks (50 ms each)
//   --latency-ticks N the sink latency: 0 applies a command the moment a bot releases it; N > 0 plays like a lock-step room (applied at the first 100 ms turn
//                     boundary at least N ticks later, in canonical order); default 3
//   --rotate          play every distinct arrangement of the given bots over the given seats (up to 24 for four different bots): seats are not symmetric on a map,
//                     so a comparison of bots must rotate them
//   --repeat N        play every match N times and require identical results (finds any nondeterminism)
//   --replay-check    re-feed the commands that were applied into a FRESH engine with no bot at all and require the same state hash at every 20th tick and at the end
//   --threads N       matches played at the same time (default 1; every match is independent, the report is sorted by map, seed and arrangement)
//   --out FILE        write the JSON report (fixed key order; the maps' NAMES only, no paths)
//   --no-wall-time    leave the wall clock time of each match out of the report, so that the file is bit-reproducible
//   --maps-dir DIR    where map names are looked for (default: the shipped maps)
//   --selftest        check the tool itself (determinism, replay check, report, threads)
//   --write-baselines print the table that tests/test_ai/baselines.inc pins (the worker bot at the three levels on the six shipped maps, a fixed seed set, seats rotated:
//                     include/ants_ai/baselines.hpp) to stdout; the other options (but --threads and --maps-dir) are ignored. Regenerate it ON PURPOSE, when the bot or the
//                     hill's banking changed.
//
// Exit code: 0 when every match was played and every check passed, 1 on a finding (a match that could not be played, a replay or repeat that differs), 2 on bad usage.
#include <algorithm>
#include <atomic>
#include <chrono>
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
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ants_ai/arena.hpp"
#include "ants_ai/baselines.hpp"
#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/rng.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
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
    "idle stands still; worker harvests (B3); standard is an alias of worker until the standard bot with its tactics arrives with B4";

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

// Pretty printed, keys in the order they are written
class JsonWriter {
public:
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
        "  --seat N=SPEC      a bot on seat N (0 to 3); SPEC is KIND, KIND:LEVEL or LEVEL (kinds idle worker standard; levels easy medium hard).\n"
        "                     Default: four standard bots at medium level. %s.\n"
        "  --ticks full|N     until the match is over (default) or at most N ticks\n"
        "  --latency-ticks N  sink latency in ticks (default 3; 0 = commands applied at once)\n"
        "  --rotate           every distinct arrangement of the bots over the seats\n"
        "  --repeat N         play every match N times and require identical results\n"
        "  --replay-check     replay the applied commands into a fresh engine without any bot: same hash at every 20th tick and at the end\n"
        "  --threads N        matches at the same time (default 1)\n"
        "  --out FILE         write the JSON report\n"
        "  --quiet            no line per match\n"
        "  --no-wall-time     leave wall times out of the report (the file is then bit-reproducible)\n"
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

// "N=KIND:LEVEL" (also "N:KIND:LEVEL"): the spec of ants::ai::parse_bot_spec with the seat in front
bool parse_seat(const std::string& text, ai::BotSpec& out, std::string& err) {
    std::string spec = text;
    const size_t eq = spec.find('=');
    if (eq != std::string::npos) spec[eq] = ':';
    return ai::parse_bot_spec(spec, out, err);
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

// A name is looked for in the maps folder (NAME.LVL or NAME.lvl, any case of the extension); anything with a folder separator or an extension is a path
bool resolve_map(const std::string& what, const std::string& dir, fs::path& out) {
    std::error_code ec;
    const bool is_path = what.find('/') != std::string::npos || what.find('\\') != std::string::npos || what.find('.') != std::string::npos;
    if (is_path) {
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
            m.name = o.maps[i];
            m.error = "map '" + o.maps[i] + "' not found";
            continue;
        }
        const std::string stem = p.stem().string();
        m.name = o.maps[i].find('.') == std::string::npos && o.maps[i].find('/') == std::string::npos ? upper(stem) : stem;
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

std::string spec_text(const ai::BotSpec& s) { return s.kind + ":" + ai::level_name(s.level); }

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
    ai::ArenaResult result;
    bool replay_checked{false};
    ai::ReplayResult replay;
    uint32_t plays{1};
    bool repeat_ok{true};
    std::string repeat_note;
    double wall_ms{0.0};
    bool ok() const { return result.error.empty() && (!replay_checked || replay.ok) && repeat_ok; }
};

bool same_match(const ai::ArenaResult& a, const ai::ArenaResult& b) {
    if (a.ticks != b.ticks || a.hash != b.hash || a.match_over != b.match_over || a.checkpoints != b.checkpoints || a.seats.size() != b.seats.size()) return false;
    for (size_t i = 0; i < a.seats.size(); ++i) {
        const ai::ArenaSeatResult& x = a.seats[i];
        const ai::ArenaSeatResult& y = b.seats[i];
        if (x.score != y.score || x.ants != y.ants || x.food_deposited != y.food_deposited || x.stats.decisions != y.stats.decisions || x.stats.released != y.stats.released ||
            x.stats.intents != y.stats.intents || x.stats.expired != y.stats.expired || x.stats.rejected != y.stats.rejected) {
            return false;
        }
    }
    return true;
}

ai::ArenaSpec spec_of(const Options& o, const LoadedMap& m, const Job& j, bool record) {
    ai::ArenaSpec s;
    s.level = &m.level;
    s.seed = j.seed;
    s.bots = j.bots;
    s.max_ticks = o.ticks;
    s.latency_ticks = o.latency;
    s.record = record;
    return s;
}

MatchReport run_job(const Options& o, const std::vector<LoadedMap>& maps, const Job& job, std::function<std::unique_ptr<ai::Bot>(const ai::BotSpec&)> factory) {
    MatchReport r;
    r.job = job;
    r.plays = o.repeat;
    const LoadedMap& m = maps[job.map];
    if (!m.ok) {
        r.result.error = m.error;
        return r;
    }
    const Clock::time_point started = Clock::now();
    ai::ArenaSpec spec = spec_of(o, m, job, o.replay_check);
    spec.factory = factory;
    r.result = ai::play_match(spec);
    if (r.result.error.empty() && o.replay_check) {
        r.replay_checked = true;
        r.replay = ai::replay_commands(spec, r.result);
    }
    for (uint32_t again = 1; again < o.repeat && r.result.error.empty(); ++again) {
        ai::ArenaSpec s2 = spec_of(o, m, job, false);
        s2.factory = factory;
        const ai::ArenaResult second = ai::play_match(s2);
        if (!same_match(r.result, second)) {
            r.repeat_ok = false;
            r.repeat_note = fmt("play %u differs: ticks %llu / %llu, hash %s / %s", again + 1, static_cast<unsigned long long>(r.result.ticks), static_cast<unsigned long long>(second.ticks),
                                hex64(r.result.hash).c_str(), hex64(second.hash).c_str());
        }
    }
    r.wall_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return r;
}

std::vector<Job> make_jobs(const Options& o, size_t map_count) {
    std::vector<Job> jobs;
    const std::vector<std::vector<ai::BotSpec>> arr = arrangements(o.seats, o.rotate);
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
    return jobs;
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
        while (j + 1 < seeds.size() && seeds[j + 1] == seeds[j] + 1) ++j;
        if (!out.empty()) out += ",";
        out += j > i ? fmt("%u..%u", seeds[i], seeds[j]) : fmt("%u", seeds[i]);
        i = j + 1;
    }
    return out;
}

std::string report_json(const Options& o, const std::vector<LoadedMap>& maps, const std::vector<MatchReport>& reports) {
    JsonWriter j;
    j.begin_object();
    j.field("tool", "bot_arena");
    j.field("format", uint64_t{1});
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
            j.field_signed("score", s.score);
            j.field_signed("shown_score", s.shown_score);
            j.field("ants", uint64_t{s.ants});
            j.field("eggs", uint64_t{s.eggs});
            j.field("hatched", uint64_t{s.hatched});
            j.field("food_deposited", uint64_t{s.food_deposited});
            j.field("food_stolen", uint64_t{s.food_stolen});
            j.field("food_lost", uint64_t{s.food_lost});
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
            j.field("commands", uint64_t{r.result.log.size()});
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
    }
    j.end_array();
    j.key("summary");
    j.begin_object();
    j.field("matches", uint64_t{reports.size()});
    j.field("failures", uint64_t{failures});
    j.field("ticks", ticks);
    j.end_object();
    j.end_object();
    return j.text() + "\n";
}

std::string seats_line(const MatchReport& r) {
    std::string s;
    for (const ai::ArenaSeatResult& seat : r.result.seats) {
        s += fmt(" [%u %s %d]", static_cast<unsigned>(seat.spec.seat), spec_text(seat.spec).c_str(), seat.score);
    }
    return s;
}

void print_match(const std::vector<LoadedMap>& maps, const MatchReport& r) {
    if (!r.result.error.empty()) {
        std::printf("%-9s seed %u #%u: NOT PLAYED: %s\n", maps[r.job.map].name.c_str(), r.job.seed, r.job.rotation, r.result.error.c_str());
        return;
    }
    std::printf("%-9s seed %u #%u:%s  ticks %llu%s  hash %s  %s%s\n", maps[r.job.map].name.c_str(), r.job.seed, r.job.rotation, seats_line(r).c_str(),
                static_cast<unsigned long long>(r.result.ticks), r.result.match_over ? " (over)" : "", hex64(r.result.hash).c_str(),
                r.replay_checked ? (r.replay.ok ? "replay ok" : "REPLAY DIFFERS") : "", r.repeat_ok ? "" : "  REPEAT DIFFERS");
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The tool
// ---------------------------------------------------------------------------------------------------------------------------------

int run_tool(const Options& o) {
    std::vector<LoadedMap> maps = load_maps(o);
    const std::vector<Job> jobs = make_jobs(o, maps.size());
    if (jobs.size() * o.repeat > kMaxMatches) {
        std::fprintf(stderr, "bot_arena: %zu matches are too many (at most %zu)\n", jobs.size() * o.repeat, kMaxMatches);
        return 2;
    }
    std::string kinds;
    bool placeholder = false;
    for (const ai::BotSpec& s : o.seats) {
        kinds += (kinds.empty() ? "" : ", ") + std::to_string(static_cast<unsigned>(s.seat)) + "=" + spec_text(s);
        placeholder = placeholder || s.kind == "standard";
    }
    std::printf("bot_arena: %zu match(es): maps %zu, seeds %s, %zu arrangement(s) of [%s], ticks %s, latency %u%s%s\n", jobs.size(), maps.size(), seeds_text(o.seeds).c_str(),
                arrangements(o.seats, o.rotate).size(), kinds.c_str(), o.ticks == 0 ? "full" : std::to_string(o.ticks).c_str(), o.latency, o.replay_check ? ", replay check" : "",
                o.repeat > 1 ? fmt(", every match %u times", o.repeat).c_str() : "");
    if (placeholder) std::printf("NOTE: %s\n", kKindsNote);
    const Clock::time_point started = Clock::now();
    const std::vector<MatchReport> reports = run_jobs(o, maps, jobs, nullptr, jobs.size() > 100);
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
    std::printf("bot_arena: %zu match(es), %llu ticks, %.2f s%s\n", reports.size(), static_cast<unsigned long long>(ticks), seconds, failures == 0 ? ", every check passed" : "");
    if (failures != 0) std::printf("bot_arena: %zu match(es) FAILED (not played, replay or repeat differs)\n", failures);
    if (!o.out.empty()) {
        std::ofstream f(o.out, std::ios::binary | std::ios::trunc);
        const std::string json = report_json(o, maps, reports);
        f << json;
        if (!f) {
            std::fprintf(stderr, "bot_arena: cannot write %s\n", o.out.c_str());
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
    void section(const char* name) { std::printf("  %s\n", name); }
};

ai::BotSpec spec_for(uint8_t seat, const char* kind, ai::Level level) {
    ai::BotSpec s;
    s.seat = seat;
    s.kind = kind;
    s.level = level;
    return s;
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
        ai::ArenaResult dropped = a;
        dropped.log.erase(dropped.log.begin() + static_cast<std::ptrdiff_t>(dropped.log.size() / 2));
        t.check(!ai::replay_commands(s, dropped).ok, "a replay with one command missing is noticed");
        ai::ArenaResult moved = a;
        moved.log[moved.log.size() / 2].command.tile_x = static_cast<int16_t>(moved.log[moved.log.size() / 2].command.tile_x + 5);
        t.check(!ai::replay_commands(s, moved).ok, "a replay with one command changed is noticed");
        ai::ArenaResult late = a;
        late.log[late.log.size() / 2].step += 7;
        t.check(!ai::replay_commands(s, late).ok, "a replay with one command at another step is noticed");
        ai::ArenaSpec direct = s;
        direct.latency_ticks = 0;
        const ai::ArenaResult d = ai::play_match(direct);
        t.check(d.error.empty() && ai::replay_commands(direct, d).ok, "latency 0: the commands are applied at once, and replay as well");
        t.check(d.hash != a.hash, "the sink latency changes the match (so it is part of the arguments)");
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
        const std::vector<Job> jobs = make_jobs(o, maps.size());
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
            replays = replays && r.replay_checked && r.replay.ok && !r.result.log.empty();
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
        t.check(json1.find(kKindsNote) != std::string::npos, "the report says what the kinds are (standard is an alias of worker until B4)");
        t.check(json1.find("\"wall_ms\"") == std::string::npos, "no wall time with --no-wall-time");
        o.wall_time = true;
        t.check(report_json(o, maps, one).find("\"wall_ms\"") != std::string::npos, "wall times with the default");
        // a replay that fails is reported as a failed match
        t.check(json1.find("\"failures\": 4") != std::string::npos, "the summary counts the 4 matches of the missing map as failures");
        std::vector<MatchReport> broken = one;
        broken[0].replay.ok = false;
        t.check(!broken[0].ok() && report_json(o, maps, broken).find("\"failures\": 5") != std::string::npos, "a replay that differs is one more failure");
    }

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
