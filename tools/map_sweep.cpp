// map_sweep: plays every map of a folder headless with the real simulation engine, so that a server knows it can host the whole library before it
// offers a map to anybody.
//
// Why: the community's maps are the content of the game, and nothing of them ever went through this engine. A map that does not load, crashes the
// engine, hangs it, grows without bound or makes two machines of a lock-step match disagree must be found here, not by the players. For every
// *.lvl / *.LVL file of the folder (in name order) the sweep records
//   1. whether the NAME can travel in the network protocol (ants::net::valid_map_name) and, if not, why and which characters offend;
//   2. what the file is: size, FNV-1a 64 hash (ants::net::hash_file, the identity of the start barrier), header version, size of the grid, minutes,
//      description, and, when it loads (LevelData::load_from_file), the start markers, hills, plants, food objects, waypoints and the egg stock;
//      when it does not load, why (the loader names it: LevelValidation, the findings of ants::assets::LVLParser, the same answer that a server gets from
//      LVLParser::check_file), and every finding about a file that it does load (the original reads it only as far as it goes, tiles outside the
//      dictionary, ...); a roster with a team whose start marker lies outside the grid is REFUSED, not played (the original has no behaviour for it);
//   3. a headless match of N ticks (default 2400 = two minutes of game time) with a scripted player per team: every second each team gives one
//      order with its own ants (group move, attack, special, stop, hatch) from a fixed-seed generator, aimed at food, hills, enemy ants or random
//      tiles, so that ants harvest, hatch, fight and walk (the teams are given the 200 points of a hatch whenever they have less, as a player who has
//      harvested would have them: otherwise a short play would hardly ever hatch, and the hatching code of the map would not be played). The tick time (mean and maximum, around SimulationEngine::tick() only), the peak number of
//      ants, the memory at the end and the state hash after the last tick are measured. Every map is played TWICE with the same seed in two fresh
//      processes, and a third time in the first process on the same engine object (init() must reset everything); the three hashes must be equal.
//      A difference is a determinism bug (lock-step peers would disagree): the sweep then replays the runs with a hash every second and every tick
//      and names the first tick and the subsystems (engine, players, grid, food, ants, paths, droppers) that differ.
//   The default rosters are all four teams, and teams 0 and 3 alone when the map has a start marker or hill for both.
//
// Every play runs in a CHILD PROCESS (fork + exec of this same binary in the hidden `--child` mode) with a wall clock limit: a crash by signal, an
// uncaught exception, a hang or a runaway allocation is recorded as that map's failure and never takes the sweep down. Windows has no fork: there
// the matches run in the sweep process itself (one map at a time, the wall clock limit is checked between ticks only, a crash ends the sweep).
//
// Usage:
//   map_sweep <maps-dir> [--ticks N] [--roster MASK] [--jobs J] [--timeout-seconds S] [--out report.json] [--only text] [--skip-run]
//   map_sweep --selftest
// Exit code: 0 only when every map loads, plays every roster, runs without a crash, hang or error and is deterministic. Bad names do not fail it (they are counted
// apart: whether the protocol should accept them is a decision of the protocol). 1: findings, 2: bad usage or an unreadable folder.
//
// The JSON report has one object per map in the order of the folder's names, with a fixed key order, no time stamps and no paths: file NAMES only.
//
// Hidden options (the child protocol, and the hooks that `--selftest` uses to prove that the sweep notices what it must notice; --no-topup is public):
//   --child FILE --repeat K --no-run --trace-every E --trace-from F --trace-to T --seed S --memory-mb M
//   --fault abort@K | segv@K | hang@K | abortload | exit3 | garbage | leak@K | flip@K
// A child prints one line per message, "TAG key=value ..." with percent-escaped values: INFO (the structure of the map), BEGIN / PROG (progress, so
// that a crash can be placed within 20 ticks), RESULT (one run) and TR (the state hash at a tick, for the search for the first difference).
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ants_assets/lvl_parser.hpp"
#include "ants_net/netgame.hpp"
#include "ants_net/protocol.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/movement_tables.hpp"
#include "ants_sim/sim_engine.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SWEEP_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define SWEEP_PRINTF(fmt_index, first_arg)
#endif

namespace fs = std::filesystem;
namespace assets = ants::assets;
namespace net = ants::net;
namespace sim = ants::sim;

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kDefaultTicks = 2400;           // two minutes of game time (a tick is 50 ms)
constexpr uint32_t kTicksPerSecond = 20;           // the scripted players act once per game second
constexpr uint32_t kSweepSeed = 20240229u;         // the engine's seed of every play (the same for the repeated plays of a map)
constexpr uint32_t kScriptSeed = 0x5EEDu;          // the scripted players' generator
constexpr uint32_t kAllTeams = 0x0Fu;
constexpr uint32_t kGreenAndBlack = 0x09u;         // teams 0 and 3: the two-team roster
constexpr uint32_t kTraceCoarse = 20;              // the search for a difference: a hash every game second, then every tick
constexpr size_t kMaxChildOutput = 8u * 1024u * 1024u;

// ---------------------------------------------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------------------------------------------

SWEEP_PRINTF(1, 2) std::string fmt(const char* format, ...) {
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

bool parse_uint(const std::string& s, uint64_t& out, int base = 10) {
    if (s.empty() || s[0] == '-' || s[0] == '+' || s[0] == ' ') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(s.c_str(), &end, base);
    if (errno != 0 || end == nullptr || *end != '\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
}

unsigned long current_pid() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

std::string lower_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// The length of the valid UTF-8 sequence that starts at s[i]; 0 when the bytes there are not valid UTF-8
size_t utf8_sequence_length(const std::string& s, size_t i) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    if (c < 0x80) return 1;
    size_t n = 0;
    uint32_t cp = 0;
    if (c >= 0xC2 && c <= 0xDF) { n = 2; cp = c & 0x1Fu; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 3; cp = c & 0x0Fu; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 4; cp = c & 0x07u; }
    else return 0;
    if (i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k) {
        const uint8_t cc = static_cast<uint8_t>(s[i + k]);
        if ((cc & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800u) || (n == 4 && (cp < 0x10000u || cp > 0x10FFFFu)) || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0;
    return n;
}

// A file name for the terminal: control bytes become '?', at most `width` code points, padded to `width`
std::string column_text(const std::string& s, size_t width) {
    std::string out;
    size_t points = 0;
    for (size_t i = 0; i < s.size() && points < width;) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < 0x20 || c == 0x7F) { out += '?'; ++i; ++points; continue; }
        const size_t n = utf8_sequence_length(s, i);
        if (n == 0) { out += '?'; ++i; ++points; continue; }
        out.append(s, i, n);
        i += n;
        ++points;
    }
    if (points == width && out.size() < s.size()) {
        // cut: mark it with the last visible position
        size_t last = out.size();
        while (last > 0 && (static_cast<uint8_t>(out[last - 1]) & 0xC0u) == 0x80u) --last;
        if (last > 0) out.erase(last - 1);
        out += '~';
    }
    while (points < width) { out += ' '; ++points; }
    return out;
}

// Keeps of every word that holds a path separator only what follows the last one: a diagnostic of a sanitizer or an assertion names source files with
// the paths of the machine that built them, and a report must not carry them
std::string scrub_paths(const std::string& text) {
    std::string out;
    std::string word;
    auto flush = [&]() {
        const size_t cut = word.find_last_of("/\\");
        out += (cut == std::string::npos) ? word : word.substr(cut + 1);
        word.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            flush();
            out += c;
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

// A file name for a terminal line: control bytes become '?'
std::string safe_text(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '?';
    }
    return out;
}

// The first `limit` characters of a diagnostic as one line without paths (line ends become " | "). The paths go first, from the whole text: a cut
// in the middle of a path would leave the directory that precedes the cut (the machine's user name and folders) in the report.
std::string one_line_diagnostic(const std::string& text, size_t limit) {
    const std::string s = scrub_paths(text);
    std::string out;
    for (char c : s) {
        if (c == '\n') {
            if (!out.empty() && out.compare(out.size() - 1, 1, "|") != 0) out += " | ";
        } else if (c == ' ' || c == '\t' || c == '\r') {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '|')) out.pop_back();
    if (out.size() > limit) out.resize(limit);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The lines that a child prints: "TAG key=value key=value", values percent-escaped
// ---------------------------------------------------------------------------------------------------------------------------------

bool kv_plain(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == ',' || c == ':' || c == '-' || c == '_';
}

std::string kv_escape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (kv_plain(c)) out += ch;
        else out += fmt("%%%02X", static_cast<unsigned>(c));
    }
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string kv_unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex_digit(s[i + 1]) >= 0 && hex_digit(s[i + 2]) >= 0) {
            out += static_cast<char>(hex_digit(s[i + 1]) * 16 + hex_digit(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

using KvMap = std::map<std::string, std::string>;

class Kv {
public:
    Kv& add(const char* key, const std::string& value) { items_.emplace_back(key, kv_escape(value)); return *this; }
    Kv& add(const char* key, const char* value) { return add(key, std::string(value)); }
    Kv& add(const char* key, uint64_t value) { items_.emplace_back(key, std::to_string(value)); return *this; }
    Kv& add_hex(const char* key, uint64_t value) { items_.emplace_back(key, hex64(value)); return *this; }
    std::string line(const char* tag) const {
        std::string s = tag;
        for (const auto& kv : items_) s += " " + kv.first + "=" + kv.second;
        return s;
    }

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

// "TAG k=v k=v": false when the line does not start with the tag
bool kv_parse(const std::string& line, const char* tag, KvMap& out) {
    const size_t tag_len = std::strlen(tag);
    if (line.size() <= tag_len || line.compare(0, tag_len, tag) != 0 || line[tag_len] != ' ') return false;
    out.clear();
    size_t i = tag_len + 1;
    while (i < line.size()) {
        size_t end = line.find(' ', i);
        if (end == std::string::npos) end = line.size();
        const std::string item = line.substr(i, end - i);
        const size_t eq = item.find('=');
        if (eq != std::string::npos) out[item.substr(0, eq)] = kv_unescape(item.substr(eq + 1));
        i = end + 1;
    }
    return true;
}

std::string kv_get(const KvMap& m, const char* key) {
    const auto it = m.find(key);
    return it == m.end() ? std::string() : it->second;
}
uint64_t kv_uint(const KvMap& m, const char* key, uint64_t fallback = 0) {
    uint64_t v = 0;
    const auto it = m.find(key);
    return (it != m.end() && parse_uint(it->second, v)) ? v : fallback;
}
uint64_t kv_hex(const KvMap& m, const char* key) {
    uint64_t v = 0;
    const auto it = m.find(key);
    return (it != m.end() && parse_uint(it->second, v, 16)) ? v : 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------------------------------------------------------------

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        if (c == '"') { out += "\\\""; ++i; }
        else if (c == '\\') { out += "\\\\"; ++i; }
        else if (c < 0x20 || c == 0x7F) { out += fmt("\\u%04x", static_cast<unsigned>(c)); ++i; }
        else if (c < 0x80) { out += static_cast<char>(c); ++i; }
        else {
            const size_t n = utf8_sequence_length(s, i);
            if (n == 0) { out += fmt("\\u%04x", static_cast<unsigned>(c)); ++i; }   // not UTF-8: the byte as a Latin-1 character
            else { out.append(s, i, n); i += n; }
        }
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
    void str(const std::string& v) { prefix(); out_ += json_quote(v); }
    void num(uint64_t v) { prefix(); out_ += std::to_string(v); }
    void num(double v, int decimals) { prefix(); out_ += fmt("%.*f", decimals, v); }
    void boolean(bool v) { prefix(); out_ += v ? "true" : "false"; }
    void field(const std::string& k, const std::string& v) { key(k); str(v); }
    void field(const std::string& k, const char* v) { key(k); str(v); }
    void field(const std::string& k, uint64_t v) { key(k); num(v); }
    void field(const std::string& k, double v, int decimals) { key(k); num(v, decimals); }
    void field_bool(const std::string& k, bool v) { key(k); boolean(v); }
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

// ---------------------------------------------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------------------------------------------

// A fault that a test asks a child to commit (see the header comment): kind "" is none
struct Fault {
    std::string kind;
    uint32_t tick{0};
    bool crashes_in_run() const { return kind == "abort" || kind == "segv" || kind == "hang"; }
};

struct Options {
    std::string maps_dir;
    uint32_t ticks{kDefaultTicks};
    int roster{-1};                 // -1: the default rosters; else the mask of the one roster to play
    unsigned jobs{0};               // 0: the cores minus two (at least one)
    uint32_t timeout_s{120};
    uint32_t memory_mb{3072};
    uint32_t slack_s{10};           // what a child may take beyond its plays' limits (loading the map, starting up)
    std::string out;
    std::string only;
    bool skip_run{false};
    bool topup{true};               // the scripted players are given the points of a hatch whenever they have less (see ScriptedPlayers)
    bool selftest{false};
    bool help{false};
    // the child protocol and the test hooks
    bool child{false};
    std::string child_file;
    uint32_t repeat{1};
    bool no_run{false};
    std::string fault_text;
    Fault fault;
    uint32_t trace_every{0};
    uint32_t trace_from{0};
    uint32_t trace_to{0};
    uint32_t seed{kSweepSeed};
};

bool parse_fault(const std::string& text, Fault& out) {
    if (text.empty()) { out = Fault{}; return true; }
    const size_t at = text.find('@');
    const std::string kind = text.substr(0, at);
    uint64_t tick = 0;
    if (at != std::string::npos && !parse_uint(text.substr(at + 1), tick)) return false;
    static const char* const kKinds[] = {"abort", "segv", "hang", "abortload", "exit3", "garbage", "leak", "flip"};
    for (const char* k : kKinds) {
        if (kind == k) {
            out.kind = kind;
            out.tick = static_cast<uint32_t>(std::min<uint64_t>(tick, 0xFFFFFFFFu));
            return true;
        }
    }
    return false;
}

void print_usage(std::FILE* to) {
    std::fprintf(to,
        "Usage: map_sweep <maps-dir> [options]\n"
        "       map_sweep --selftest\n"
        "Plays every .lvl / .LVL map of the folder headless with the real engine and reports what loads, what the protocol accepts as a name,\n"
        "how fast and how big each map plays, and whether two plays of the same map agree.\n"
        "  --ticks N             ticks per play (default %u = two minutes of game time)\n"
        "  --roster MASK         play only this roster (bit t = team t; 15 = all four teams, 9 = teams 0 and 3); default: all four teams, and\n"
        "                        teams 0 and 3 alone when the map has a start marker or hill for both\n"
        "  --jobs J              maps played at the same time (default: the cores minus two)\n"
        "  --timeout-seconds S   wall clock limit of one play (default 120); a play that uses it up is a hang\n"
        "  --out FILE            write the JSON report\n"
        "  --only TEXT           only the maps whose file name contains TEXT (any case)\n"
        "  --skip-run            load and check the maps only, play nothing\n"
        "  --no-topup            do not give the scripted players the points of a hatch (200) whenever they have less: by default they have them,\n"
        "                        as a player who has harvested would, so that Hatch orders hatch and the hatching code of every map is played\n"
        "  --selftest            check the tool on the six shipped maps (and on faults it must notice)\n",
        kDefaultTicks);
}

bool need_uint(const std::vector<std::string>& a, size_t& i, const char* name, uint64_t lo, uint64_t hi, uint64_t& out, std::string& err) {
    if (i + 1 >= a.size()) { err = std::string(name) + " needs a value"; return false; }
    uint64_t v = 0;
    if (!parse_uint(a[i + 1], v, 0) || v < lo || v > hi) {
        err = std::string(name) + " needs a number from " + std::to_string(lo) + " to " + std::to_string(hi) + ", not '" + a[i + 1] + "'";
        return false;
    }
    out = v;
    ++i;
    return true;
}

bool parse_args(const std::vector<std::string>& a, Options& o, std::string& err) {
    bool have_dir = false;
    for (size_t i = 0; i < a.size(); ++i) {
        const std::string& s = a[i];
        uint64_t v = 0;
        if (s == "--help" || s == "-h") o.help = true;
        else if (s == "--selftest") o.selftest = true;
        else if (s == "--skip-run") o.skip_run = true;
        else if (s == "--no-run") o.no_run = true;
        else if (s == "--no-topup") o.topup = false;
        else if (s == "--ticks") { if (!need_uint(a, i, "--ticks", 0, 100000000u, v, err)) return false; o.ticks = static_cast<uint32_t>(v); }
        else if (s == "--roster") { if (!need_uint(a, i, "--roster", 1, 15, v, err)) return false; o.roster = static_cast<int>(v); }
        else if (s == "--jobs") { if (!need_uint(a, i, "--jobs", 1, 256, v, err)) return false; o.jobs = static_cast<unsigned>(v); }
        else if (s == "--timeout-seconds") { if (!need_uint(a, i, "--timeout-seconds", 1, 86400, v, err)) return false; o.timeout_s = static_cast<uint32_t>(v); }
        else if (s == "--memory-mb") { if (!need_uint(a, i, "--memory-mb", 16, 1048576, v, err)) return false; o.memory_mb = static_cast<uint32_t>(v); }
        else if (s == "--repeat") { if (!need_uint(a, i, "--repeat", 1, 16, v, err)) return false; o.repeat = static_cast<uint32_t>(v); }
        else if (s == "--seed") { if (!need_uint(a, i, "--seed", 0, 0xFFFFFFFFu, v, err)) return false; o.seed = static_cast<uint32_t>(v); }
        else if (s == "--trace-every") { if (!need_uint(a, i, "--trace-every", 0, 100000000u, v, err)) return false; o.trace_every = static_cast<uint32_t>(v); }
        else if (s == "--trace-from") { if (!need_uint(a, i, "--trace-from", 0, 100000000u, v, err)) return false; o.trace_from = static_cast<uint32_t>(v); }
        else if (s == "--trace-to") { if (!need_uint(a, i, "--trace-to", 0, 100000000u, v, err)) return false; o.trace_to = static_cast<uint32_t>(v); }
        else if (s == "--out" || s == "--only" || s == "--child" || s == "--fault") {
            if (i + 1 >= a.size()) { err = s + " needs a value"; return false; }
            const std::string& value = a[++i];
            if (s == "--out") o.out = value;
            else if (s == "--only") o.only = value;
            else if (s == "--child") { o.child = true; o.child_file = value; }
            else {
                o.fault_text = value;
                if (!parse_fault(value, o.fault)) { err = "unknown fault '" + value + "'"; return false; }
            }
        }
        else if (!s.empty() && s[0] == '-' && s.size() > 1) { err = "unknown option " + s; return false; }
        else if (!have_dir) { o.maps_dir = s; have_dir = true; }
        else { err = "more than one folder given: " + s; return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The name of a map file against the protocol
// ---------------------------------------------------------------------------------------------------------------------------------

struct NameCheck {
    bool ok{true};
    size_t length{0};                       // bytes, extension included (the protocol limit counts bytes)
    bool too_long{false};
    std::vector<std::string> bad_chars;     // the distinct offending characters in order of appearance; non-ASCII bytes as \xNN
    std::string issue;                      // why the protocol refuses the name ("" when it accepts it)
};

NameCheck check_name(const std::string& name) {
    NameCheck r;
    r.length = name.size();
    r.ok = net::valid_map_name(name);
    r.too_long = name.size() > net::kMaxMapNameChars;
    if (r.ok) return r;
    // A byte offends when a name made of it and plain letters is refused: the protocol itself decides, so the report follows it when its rule changes
    std::vector<bool> seen(256, false);
    for (char ch : name) {
        const uint8_t c = static_cast<uint8_t>(ch);
        if (seen[c]) continue;
        seen[c] = true;
        const std::string probe = std::string("a") + ch + "a.lvl";
        if (net::valid_map_name(probe)) continue;
        if (c > 0x20 && c < 0x7F) r.bad_chars.push_back(std::string(1, ch));
        else if (c == ' ') r.bad_chars.push_back("space");
        else r.bad_chars.push_back(fmt("\\x%02X", static_cast<unsigned>(c)));
    }
    std::string why;
    if (r.too_long) why = fmt("%zu bytes, the limit is %zu", name.size(), net::kMaxMapNameChars);
    if (!r.bad_chars.empty()) {
        if (!why.empty()) why += "; ";
        why += "characters not accepted:";
        for (const std::string& c : r.bad_chars) why += " [" + c + "]";
    }
    if (why.empty()) why = "refused for another reason (extension, a leading '.' or too short)";
    r.issue = why;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// What a map file is: its header, a diagnosis of a load failure, its structure
// ---------------------------------------------------------------------------------------------------------------------------------

struct MapInfo {
    bool loads{false};
    std::string load_error;
    bool have_header{false};
    uint32_t version{0};
    uint32_t game_mode{0};
    uint32_t minutes{0};
    std::string description;
    uint32_t tile_types{0};
    bool have_dims{false};
    uint32_t width{0};              // columns
    uint32_t height{0};             // rows
    uint32_t spawns{0};
    std::array<uint32_t, 4> starts{};   // Block 1 records of each team's start marker
    std::array<uint32_t, 4> hills{};    // layer 2 cells that hold the team's hill tile
    uint32_t plants{0};
    uint32_t food{0};
    uint32_t waypoints{0};
    uint32_t eggs{0};
    bool playable{false};                       // LevelData::validate(all four teams): no Fatal finding
    uint32_t blocked_teams{0};                  // bit t: team t has a start marker outside the grid, so a match with it is refused (not played)
    std::string blocked_reason;                 // why (the first such finding)
    std::vector<assets::LevelProblem> problems; // every finding about the file (LevelValidation::problems, all four teams)
    bool has_team(size_t t) const { return t < 4 && (starts[t] > 0 || hills[t] > 0); }
};

uint32_t le16(const std::vector<uint8_t>& d, size_t p) { return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8); }
uint32_t le32(const std::vector<uint8_t>& d, size_t p) { return le16(d, p) | (le16(d, p + 2) << 16); }

// The header facts that can be read whatever follows (the layout of lvl_parser.cpp: version, mode, minutes, 30 bytes of text, tile types, the
// dictionary of 11 byte names, rows, columns)
void read_header(const std::vector<uint8_t>& d, MapInfo& info) {
    if (d.size() < 42) return;
    info.have_header = true;
    info.version = le32(d, 0);
    info.game_mode = le32(d, 4);
    info.minutes = le16(d, 8);
    size_t len = 0;
    while (len < 30 && d[10 + len] != 0) ++len;
    info.description.assign(reinterpret_cast<const char*>(d.data()) + 10, len);
    info.tile_types = le16(d, 40);
    const size_t dims_at = 42 + (static_cast<size_t>(info.tile_types) + 1) * 11;
    if (d.size() >= dims_at + 8) {
        info.have_dims = true;
        info.height = le32(d, dims_at);      // the first dword is the number of rows
        info.width = le32(d, dims_at + 4);
    }
}

// The team whose hill a layer-2 tile is (GREENHILL 0, REDHILL 1, BLUEHILL 2, BLACKHILL 3), -1 for any other tile (the numbering of lvl_parser.cpp)
int hill_team(const std::string& tile_name) {
    const std::string low = lower_ascii(tile_name);
    if (low == "greenhill") return 0;
    if (low == "redhill") return 1;
    if (low == "bluehill") return 2;
    if (low == "blackhill") return 3;
    return -1;
}

// Reads the file, fills what can be known about it and loads the level (`level` is only valid when info.loads)
void analyze_map(const std::string& path, MapInfo& info, assets::LevelData& level) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        info.load_error = "the file cannot be opened";
        return;
    }
    const std::streamoff size = f.tellg();
    std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
    f.seekg(0);
    if (!bytes.empty() && !f.read(reinterpret_cast<char*>(bytes.data()), size)) {
        info.load_error = "the file cannot be read";
        return;
    }
    read_header(bytes, info);
    // LevelData::load_from_file reads the file and calls load_from_memory: the bytes read here serve the header and the loader alike. The loader names why
    // it refuses a file (LevelValidation), and lists every finding about one that it reads (the original's partial reads, tiles outside the dictionary, ...).
    assets::LevelValidation verdict;
    info.loads = level.load_from_memory(bytes.data(), bytes.size(), &verdict);
    info.problems = verdict.problems;
    info.playable = verdict.playable;
    if (!info.loads) {
        info.load_error = verdict.reason();
        if (info.load_error.empty()) info.load_error = "the loader refused the file without naming a reason";
        return;
    }
    for (const assets::LevelProblem& p : verdict.problems) {
        if (p.kind == assets::LevelProblemKind::StartMarkerOutsideGrid && p.severity == assets::LevelProblemSeverity::Fatal && p.team >= 0 && p.team < 4) {
            info.blocked_teams |= 1u << p.team;
            if (info.blocked_reason.empty()) info.blocked_reason = p.message;
        }
    }
    info.width = level.width.val;
    info.height = level.height.val;
    info.have_dims = true;
    info.spawns = static_cast<uint32_t>(level.anthill_spawns.size());
    for (const assets::AnthillSpawn& sp : level.anthill_spawns) {
        if (sp.team_id < 4) ++info.starts[sp.team_id];
        if ((sim::movement::tile_flags_of(sp.tile_id) & 0x30u) != 0) ++info.plants;     // the plants of Grid::init_from_level
    }
    for (const assets::MapCell& cell : level.layer2_interactive) {
        if (cell.tile_index >= level.tile_dictionary.size()) continue;
        const int team = hill_team(level.tile_dictionary[cell.tile_index]);
        if (team >= 0) ++info.hills[static_cast<size_t>(team)];
    }
    info.food = static_cast<uint32_t>(level.food_schedules.size());
    info.waypoints = static_cast<uint32_t>(level.waypoints.size());
    info.eggs = level.boundary_param;
}

std::string join_counts(const std::array<uint32_t, 4>& a) {
    return std::to_string(a[0]) + "," + std::to_string(a[1]) + "," + std::to_string(a[2]) + "," + std::to_string(a[3]);
}
bool split_counts(const std::string& s, std::array<uint32_t, 4>& out) {
    std::stringstream ss(s);
    std::string part;
    for (size_t i = 0; i < 4; ++i) {
        uint64_t v = 0;
        if (!std::getline(ss, part, ',') || !parse_uint(part, v)) return false;
        out[i] = static_cast<uint32_t>(v);
    }
    return true;
}

// The findings of a map through the pipe: one line each, "severity TAB kind TAB team TAB count TAB message"
std::string encode_problems(const std::vector<assets::LevelProblem>& problems) {
    std::string out;
    for (const assets::LevelProblem& p : problems) {
        out += std::to_string(static_cast<unsigned>(p.severity)) + "\t" + std::to_string(static_cast<unsigned>(p.kind)) + "\t" + std::to_string(static_cast<int>(p.team)) + "\t" +
               std::to_string(p.count) + "\t" + p.message + "\n";
    }
    return out;
}

std::vector<assets::LevelProblem> decode_problems(const std::string& text) {
    std::vector<assets::LevelProblem> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        std::vector<std::string> fields;
        size_t from = 0;
        for (int k = 0; k < 4; ++k) {
            const size_t tab = line.find('\t', from);
            if (tab == std::string::npos) break;
            fields.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        if (fields.size() != 4) continue;
        assets::LevelProblem p;
        p.severity = static_cast<assets::LevelProblemSeverity>(std::strtoul(fields[0].c_str(), nullptr, 10));
        p.kind = static_cast<assets::LevelProblemKind>(std::strtoul(fields[1].c_str(), nullptr, 10));
        p.team = static_cast<int8_t>(std::strtol(fields[2].c_str(), nullptr, 10));
        p.count = static_cast<uint32_t>(std::strtoul(fields[3].c_str(), nullptr, 10));
        p.message = line.substr(from);
        out.push_back(std::move(p));
    }
    return out;
}

std::string info_line(const MapInfo& i) {
    Kv kv;
    kv.add("loads", i.loads ? 1u : 0u).add("load_error", i.load_error).add("header", i.have_header ? 1u : 0u).add("version", i.version);
    kv.add("game_mode", i.game_mode).add("minutes", i.minutes).add("description", i.description).add("tile_types", i.tile_types);
    kv.add("dims", i.have_dims ? 1u : 0u).add("width", i.width).add("height", i.height).add("spawns", i.spawns);
    kv.add("starts", join_counts(i.starts)).add("hills", join_counts(i.hills)).add("plants", i.plants).add("food", i.food);
    kv.add("waypoints", i.waypoints).add("eggs", i.eggs);
    kv.add("playable", i.playable ? 1u : 0u).add("blocked_teams", i.blocked_teams).add("blocked_reason", i.blocked_reason).add("problems", encode_problems(i.problems));
    return kv.line("INFO");
}

MapInfo info_from_kv(const KvMap& m) {
    MapInfo i;
    i.loads = kv_uint(m, "loads") != 0;
    i.load_error = kv_get(m, "load_error");
    i.have_header = kv_uint(m, "header") != 0;
    i.version = static_cast<uint32_t>(kv_uint(m, "version"));
    i.game_mode = static_cast<uint32_t>(kv_uint(m, "game_mode"));
    i.minutes = static_cast<uint32_t>(kv_uint(m, "minutes"));
    i.description = kv_get(m, "description");
    i.tile_types = static_cast<uint32_t>(kv_uint(m, "tile_types"));
    i.have_dims = kv_uint(m, "dims") != 0;
    i.width = static_cast<uint32_t>(kv_uint(m, "width"));
    i.height = static_cast<uint32_t>(kv_uint(m, "height"));
    i.spawns = static_cast<uint32_t>(kv_uint(m, "spawns"));
    split_counts(kv_get(m, "starts"), i.starts);
    split_counts(kv_get(m, "hills"), i.hills);
    i.plants = static_cast<uint32_t>(kv_uint(m, "plants"));
    i.food = static_cast<uint32_t>(kv_uint(m, "food"));
    i.waypoints = static_cast<uint32_t>(kv_uint(m, "waypoints"));
    i.eggs = static_cast<uint32_t>(kv_uint(m, "eggs"));
    i.playable = kv_uint(m, "playable") != 0;
    i.blocked_teams = static_cast<uint32_t>(kv_uint(m, "blocked_teams"));
    i.blocked_reason = kv_get(m, "blocked_reason");
    i.problems = decode_problems(kv_get(m, "problems"));
    return i;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The scripted players
// ---------------------------------------------------------------------------------------------------------------------------------

// A tiny deterministic generator (the sweep must not depend on the library's own PRNG; the same as the tests of the command layer)
struct Lcg {
    uint32_t s{1};
    Lcg() = default;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// What a plausible player does with its ants, once a second per team: hatch, stop, or send a handful of ants to food (harvest, and the points hatch
// more ants), an enemy hill (raid), an enemy ant (attack), its own hill or a random tile (special orders: plant, light, build). Every order is
// issued through SimulationEngine::apply_command like the lock-step host does.
class ScriptedPlayers {
public:
    ScriptedPlayers(const assets::LevelData& level, uint8_t roster, uint32_t seed, bool top_up)
        : roster_(roster), top_up_(top_up), width_(static_cast<int32_t>(level.width.val)), height_(static_cast<int32_t>(level.height.val)) {
        for (const assets::FoodSchedule& f : level.food_schedules) food_.push_back(clamp(f.x, f.y));
        for (const assets::AnthillSpawn& sp : level.anthill_spawns) {
            if (sp.team_id < 4) hills_[sp.team_id].push_back(clamp(sp.x, sp.y));
        }
        for (uint32_t p = 0; p < 4; ++p) rng_[p] = Lcg(seed + p * 7919u);
    }

    /// Appends this second's commands; returns the number of ants alive now (every team)
    uint32_t orders(sim::SimulationEngine& engine, std::vector<sim::Command>& out) {
        if (top_up_) {      // a hatch costs 200 points, which a real player earns by harvesting: without them most plays of a short sweep would never hatch
            for (uint8_t p = 0; p < 4; ++p) {
                if ((roster_ & (1u << p)) != 0 && engine.get_player_score(p) < static_cast<int32_t>(sim::HATCH_COST_POINTS)) {
                    engine.set_player_score(p, static_cast<int32_t>(sim::HATCH_COST_POINTS));
                }
            }
        }
        std::array<std::vector<const sim::AntSnapshot*>, 4> by_team;
        uint32_t alive = 0;
        for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
            if (a.hp == 0 || a.player_id >= 4) continue;
            ++alive;
            by_team[a.player_id].push_back(&a);
        }
        for (uint8_t p = 0; p < 4; ++p) {
            if ((roster_ & (1u << p)) == 0) continue;
            Lcg& rng = rng_[p];
            const std::vector<const sim::AntSnapshot*>& mine = by_team[p];
            const uint32_t roll = rng.below(100);
            sim::Command c;
            c.issuer = p;
            if (roll < 20) {
                c.type = sim::CommandType::Hatch;
                out.push_back(c);
                continue;
            }
            if (mine.empty()) continue;
            for (const sim::AntSnapshot* a : mine) {
                if (c.ants.size() < sim::kMaxCommandAnts && rng.below(2) == 0) c.ants.push_back(a->id);
            }
            if (c.ants.empty()) c.ants.push_back(mine[rng.below(static_cast<uint32_t>(mine.size()))]->id);
            if (roll < 30) {
                c.type = sim::CommandType::Stop;
            } else if (roll < 45) {
                c.type = sim::CommandType::GroupSpecial;
                aim(c, target(p, 2, by_team, rng));
            } else if (roll < 65) {
                c.type = sim::CommandType::GroupAttack;
                aim(c, target(p, 1, by_team, rng));
            } else {
                c.type = sim::CommandType::GroupMove;
                aim(c, target(p, 0, by_team, rng));
            }
            out.push_back(c);
        }
        return alive;
    }

private:
    struct Tile {
        int16_t x{0};
        int16_t y{0};
    };
    Tile clamp(int32_t x, int32_t y) const {
        Tile t;
        t.x = static_cast<int16_t>(std::min(std::max(x, 0), width_ - 1));
        t.y = static_cast<int16_t>(std::min(std::max(y, 0), height_ - 1));
        return t;
    }
    static void aim(sim::Command& c, const Tile& t) {
        c.tile_x = t.x;
        c.tile_y = t.y;
    }
    Tile random_tile(Lcg& rng) const {
        return clamp(static_cast<int32_t>(rng.below(static_cast<uint32_t>(width_))), static_cast<int32_t>(rng.below(static_cast<uint32_t>(height_))));
    }
    // kind: 0 move, 1 attack, 2 special
    Tile target(uint8_t p, int kind, const std::array<std::vector<const sim::AntSnapshot*>, 4>& by_team, Lcg& rng) {
        const uint32_t r = rng.below(100);
        std::vector<const sim::AntSnapshot*> enemies;
        for (uint8_t t = 0; t < 4; ++t) {
            if (t == p || (roster_ & (1u << t)) == 0) continue;
            enemies.insert(enemies.end(), by_team[t].begin(), by_team[t].end());
        }
        std::vector<Tile> enemy_hills;
        for (uint8_t t = 0; t < 4; ++t) {
            if (t == p || (roster_ & (1u << t)) == 0) continue;
            enemy_hills.insert(enemy_hills.end(), hills_[t].begin(), hills_[t].end());
        }
        auto enemy_ant = [&]() { const sim::AntSnapshot* a = enemies[rng.below(static_cast<uint32_t>(enemies.size()))]; return clamp(a->tile_x, a->tile_y); };
        auto enemy_hill = [&]() { return enemy_hills[rng.below(static_cast<uint32_t>(enemy_hills.size()))]; };
        if (kind == 1) {
            if (r < 70 && !enemies.empty()) return enemy_ant();
            if (!enemy_hills.empty()) return enemy_hill();
        } else if (kind == 0) {
            if (r < 35 && !food_.empty()) return food_[rng.below(static_cast<uint32_t>(food_.size()))];
            if (r < 50 && !enemy_hills.empty()) return enemy_hill();
            if (r < 65 && !hills_[p].empty()) return hills_[p][rng.below(static_cast<uint32_t>(hills_[p].size()))];
        } else if (r < 50 && !by_team[p].empty()) {
            const sim::AntSnapshot* a = by_team[p][rng.below(static_cast<uint32_t>(by_team[p].size()))];
            return clamp(a->tile_x + static_cast<int32_t>(rng.below(5)) - 2, a->tile_y + static_cast<int32_t>(rng.below(5)) - 2);
        }
        return random_tile(rng);
    }

    uint8_t roster_;
    bool top_up_;
    int32_t width_;
    int32_t height_;
    std::vector<Tile> food_;
    std::array<std::vector<Tile>, 4> hills_;
    std::array<Lcg, 4> rng_;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// One play
// ---------------------------------------------------------------------------------------------------------------------------------

struct RunResult {
    uint32_t run{0};
    uint32_t roster{0};
    std::string status{"ok"};       // ok; timeout (the wall clock budget of a play is used up); memory (the limit is reached)
    std::string detail;
    uint32_t ticks_done{0};
    uint32_t ended_at{0};           // the tick at which the match was over (0: it was not)
    uint64_t total_ns{0};           // the time of the ticks, tick() only
    uint64_t max_ns{0};
    uint64_t timed_ticks{0};
    uint32_t peak_ants{0};          // the most ants alive at a sample: once per game second (when the orders are given) and after the last tick
    uint32_t final_ants{0};
    uint32_t hatched{0};
    uint32_t orders{0};
    uint32_t orders_applied{0};
    uint64_t memory_kb{0};
    sim::StateHash hash;            // the state after the last tick
    uint64_t digest{0};             // FNV-1a 64 over the hash at tick 0 and after every 20th tick (what lock-step peers compare), and the last

    double mean_us() const { return timed_ticks == 0 ? 0.0 : static_cast<double>(total_ns) / 1000.0 / static_cast<double>(timed_ticks); }
    double max_us() const { return static_cast<double>(max_ns) / 1000.0; }

    std::string line() const {
        Kv kv;
        kv.add("run", run).add("roster", roster).add("status", status).add("detail", detail).add("ticks_done", ticks_done).add("ended_at", ended_at);
        kv.add("total_ns", total_ns).add("max_ns", max_ns).add("timed_ticks", timed_ticks).add("peak_ants", peak_ants).add("final_ants", final_ants);
        kv.add("hatched", hatched).add("orders", orders).add("orders_applied", orders_applied).add("memory_kb", memory_kb);
        kv.add_hex("total", hash.total).add_hex("engine", hash.engine).add_hex("players", hash.players).add_hex("grid", hash.grid);
        kv.add_hex("food", hash.food).add_hex("ants", hash.ants).add_hex("paths", hash.paths).add_hex("droppers", hash.droppers).add_hex("digest", digest);
        return kv.line("RESULT");
    }
    static RunResult from_kv(const KvMap& m) {
        RunResult r;
        r.run = static_cast<uint32_t>(kv_uint(m, "run"));
        r.roster = static_cast<uint32_t>(kv_uint(m, "roster"));
        r.status = kv_get(m, "status");
        r.detail = kv_get(m, "detail");
        r.ticks_done = static_cast<uint32_t>(kv_uint(m, "ticks_done"));
        r.ended_at = static_cast<uint32_t>(kv_uint(m, "ended_at"));
        r.total_ns = kv_uint(m, "total_ns");
        r.max_ns = kv_uint(m, "max_ns");
        r.timed_ticks = kv_uint(m, "timed_ticks");
        r.peak_ants = static_cast<uint32_t>(kv_uint(m, "peak_ants"));
        r.final_ants = static_cast<uint32_t>(kv_uint(m, "final_ants"));
        r.hatched = static_cast<uint32_t>(kv_uint(m, "hatched"));
        r.orders = static_cast<uint32_t>(kv_uint(m, "orders"));
        r.orders_applied = static_cast<uint32_t>(kv_uint(m, "orders_applied"));
        r.memory_kb = kv_uint(m, "memory_kb");
        r.hash = hash_from_kv(m);
        r.digest = kv_hex(m, "digest");
        return r;
    }
    static sim::StateHash hash_from_kv(const KvMap& m) {
        sim::StateHash h;
        h.total = kv_hex(m, "total");
        h.engine = kv_hex(m, "engine");
        h.players = kv_hex(m, "players");
        h.grid = kv_hex(m, "grid");
        h.food = kv_hex(m, "food");
        h.ants = kv_hex(m, "ants");
        h.paths = kv_hex(m, "paths");
        h.droppers = kv_hex(m, "droppers");
        return h;
    }
};

struct PlayParams {
    uint32_t ticks{0};
    uint32_t seed{kSweepSeed};
    uint32_t roster{kAllTeams};
    bool topup{true};
    uint32_t timeout_s{120};
    uint32_t memory_mb{3072};
    uint32_t trace_every{0};
    uint32_t trace_from{0};
    uint32_t trace_to{0};
    Fault fault;
};

// The resident memory high-water mark of this process in KiB (macOS reports bytes, Linux KiB; 0 where it is not measured)
uint64_t resident_kb() {
#ifdef _WIN32
    return 0;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0 || ru.ru_maxrss < 0) return 0;
#ifdef __APPLE__
    return static_cast<uint64_t>(ru.ru_maxrss) / 1024u;
#else
    return static_cast<uint64_t>(ru.ru_maxrss);
#endif
#endif
}

uint64_t g_runs_started = 0;    // plays begun in this process (the hook "leak" makes the hash depend on it, like a static that init() forgets to reset)

// The hash the sweep reports at `tick`: the engine's, perturbed by a test hook when one is asked for
sim::StateHash observed_hash(const sim::SimulationEngine& engine, uint32_t tick, const PlayParams& pp, uint64_t run_no) {
    sim::StateHash h = engine.state_hash();
    if (pp.fault.kind == "flip" && tick >= pp.fault.tick) {          // differs from process to process
        const uint64_t x = 0x9E3779B97F4A7C15ull * (current_pid() + 1u);
        h.total ^= x;
        h.ants ^= x;
    } else if (pp.fault.kind == "leak" && tick >= pp.fault.tick) {   // differs from play to play inside one process
        const uint64_t x = 0xC2B2AE3D27D4EB4Full * run_no;
        h.total ^= x;
        h.paths ^= x;
    }
    return h;
}

void sleep_forever() {
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}

void commit_fault(const Fault& f) {
    if (f.kind == "abort") std::abort();
#ifndef _WIN32
    if (f.kind == "segv") std::raise(SIGSEGV);
#else
    if (f.kind == "segv") std::abort();
#endif
    if (f.kind == "hang") sleep_forever();
}

// Plays `pp.ticks` ticks of the level on `engine` (re-initialised: the same engine object plays the repeated plays of a child)
RunResult play_match(sim::SimulationEngine& engine, const assets::LevelData& level, const PlayParams& pp, uint32_t run_index, std::ostream& out) {
    RunResult r;
    r.run = run_index;
    r.roster = pp.roster;
    const uint64_t run_no = g_runs_started++;
    engine.init(level, pp.seed, static_cast<uint8_t>(pp.roster));
    ScriptedPlayers script(level, static_cast<uint8_t>(pp.roster), kScriptSeed, pp.topup);
    const Clock::time_point deadline = Clock::now() + std::chrono::seconds(pp.timeout_s);
    const uint64_t memory_limit_kb = static_cast<uint64_t>(pp.memory_mb) * 1024u;

    auto trace = [&](uint32_t tick) {
        if (pp.trace_every == 0 || tick < pp.trace_from || tick > pp.trace_to || tick % pp.trace_every != 0) return;
        const sim::StateHash h = observed_hash(engine, tick, pp, run_no);
        Kv kv;
        kv.add("run", run_index).add("tick", tick);
        kv.add_hex("total", h.total).add_hex("engine", h.engine).add_hex("players", h.players).add_hex("grid", h.grid);
        kv.add_hex("food", h.food).add_hex("ants", h.ants).add_hex("paths", h.paths).add_hex("droppers", h.droppers);
        out << kv.line("TR") << '\n' << std::flush;
    };

    // The digest folds the hash at every checkpoint, so a difference that heals itself (a state init() leaves from an earlier play, overwritten later)
    // is seen as well as one that lasts: lock-step peers compare their hashes at these ticks
    uint64_t digest = 0xcbf29ce484222325ull;
    auto fold = [&digest](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            digest ^= (v >> (8 * i)) & 0xFFu;
            digest *= 0x100000001b3ull;
        }
    };
    auto checkpoint = [&](uint32_t tick) {
        fold(tick);
        fold(observed_hash(engine, tick, pp, run_no).total);
    };
    trace(0);
    checkpoint(0);
    std::vector<sim::Command> commands;
    for (uint32_t t = 0; t < pp.ticks; ++t) {
        if (t % kTicksPerSecond == 0) {
            out << "PROG run=" << run_index << " tick=" << t << '\n' << std::flush;
            if (Clock::now() > deadline) {
                r.status = "timeout";
                r.detail = fmt("the wall clock limit of %u s was used up at tick %u", pp.timeout_s, t);
                break;
            }
            if (resident_kb() > memory_limit_kb) {
                r.status = "memory";
                r.detail = fmt("more than %u MB resident at tick %u", pp.memory_mb, t);
                break;
            }
            commands.clear();
            r.peak_ants = std::max(r.peak_ants, script.orders(engine, commands));
            for (const sim::Command& c : commands) {
                ++r.orders;
                if (engine.apply_command(c).accepted()) ++r.orders_applied;
            }
        }
        if (pp.fault.crashes_in_run() && run_index == 0 && t == pp.fault.tick) commit_fault(pp.fault);
        if (engine.is_match_over()) {
            r.ended_at = t;
            break;
        }
        const Clock::time_point begin = Clock::now();
        engine.tick();
        const uint64_t ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
        r.total_ns += ns;
        r.max_ns = std::max(r.max_ns, ns);
        ++r.timed_ticks;
        r.ticks_done = t + 1;
        trace(t + 1);
        if ((t + 1) % kTraceCoarse == 0) checkpoint(t + 1);
    }
    r.hash = observed_hash(engine, r.ticks_done, pp, run_no);
    fold(r.ticks_done);
    fold(r.hash.total);
    r.digest = digest;
    uint32_t final_ants = 0;
    for (const sim::AntSnapshot& a : engine.get_world_state().ants) {
        if (a.hp > 0) ++final_ants;
    }
    r.final_ants = final_ants;
    r.peak_ants = std::max(r.peak_ants, final_ants);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) r.hatched += engine.get_player_hatched(p);
    r.memory_kb = resident_kb();
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The child: prints the map's structure, then plays it `repeat` times on one engine
// ---------------------------------------------------------------------------------------------------------------------------------

int child_main(const Options& o, std::ostream& out) {
    if (o.fault.kind == "exit3") return 3;
    if (o.fault.kind == "garbage") {
        out << "this is not a message of the sweep\n" << std::flush;
        return 0;
    }
    if (o.fault.kind == "abortload") std::abort();
    MapInfo info;
    assets::LevelData level;
    analyze_map(o.child_file, info, level);
    out << info_line(info) << '\n' << std::flush;
    if (!info.loads || o.no_run) return 0;
    // a match of a team whose start marker lies outside the grid is refused, not played (the parent records the refusal)
    if ((info.blocked_teams & (o.roster > 0 ? static_cast<uint32_t>(o.roster) : kAllTeams)) != 0) return 0;

    PlayParams pp;
    pp.ticks = o.ticks;
    pp.seed = o.seed;
    pp.roster = o.roster > 0 ? static_cast<uint32_t>(o.roster) : kAllTeams;
    pp.topup = o.topup;
    pp.timeout_s = o.timeout_s;
    pp.memory_mb = o.memory_mb;
    pp.trace_every = o.trace_every;
    pp.trace_from = o.trace_from;
    pp.trace_to = o.trace_to;
    pp.fault = o.fault;
    sim::SimulationEngine engine;
    for (uint32_t run = 0; run < o.repeat; ++run) {
        out << "BEGIN run=" << run << '\n' << std::flush;
        RunResult r;
        try {
            r = play_match(engine, level, pp, run, out);
        } catch (const std::exception& e) {
            r.run = run;
            r.roster = pp.roster;
            r.status = "error";
            r.detail = std::string("exception: ") + e.what();
        } catch (...) {
            r.run = run;
            r.roster = pp.roster;
            r.status = "error";
            r.detail = "an exception that is not a std::exception";
        }
        out << r.line() << '\n' << std::flush;
        if (r.status != "ok") break;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Starting a child and reading what it says
// ---------------------------------------------------------------------------------------------------------------------------------

struct ChildOutcome {
    enum class Kind { Exited, Signaled, TimedOut, SpawnFailed };
    Kind kind{Kind::SpawnFailed};
    int code{0};            // the exit status or the signal
    std::string output;     // everything the child wrote (stdout and stderr together)
};

std::string signal_name(int sig) {
#ifdef _WIN32
    return "signal " + std::to_string(sig);
#else
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        case SIGTRAP: return "SIGTRAP";
        case SIGKILL: return "SIGKILL";
        case SIGTERM: return "SIGTERM";
        case SIGPIPE: return "SIGPIPE";
        default: return "signal " + std::to_string(sig);
    }
#endif
}

#ifndef _WIN32
std::mutex g_spawn_mutex;   // one fork at a time, with the pipe's write end closed again before the next: no child holds another child's pipe

ChildOutcome spawn_child(const std::string& exe, const std::vector<std::string>& args, uint32_t timeout_s) {
    ChildOutcome res;
    std::vector<std::string> words;
    words.push_back(exe);
    words.insert(words.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& w : words) argv.push_back(&w[0]);
    argv.push_back(nullptr);

    int fds[2] = {-1, -1};
    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> lock(g_spawn_mutex);
        if (pipe(fds) != 0) {
            res.output = std::string("pipe failed: ") + std::strerror(errno);
            return res;
        }
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        pid = fork();
        if (pid == 0) {
            // the child: only calls that are safe between fork and exec (everything it needs was built before the fork)
            const int null_in = open("/dev/null", O_RDONLY | O_CLOEXEC);
            if (null_in >= 0) dup2(null_in, 0);
            dup2(fds[1], 1);
            dup2(fds[1], 2);
            execv(argv[0], argv.data());
            _exit(127);
        }
        close(fds[1]);
        if (pid < 0) {
            close(fds[0]);
            res.output = std::string("fork failed: ") + std::strerror(errno);
            return res;
        }
    }

    const Clock::time_point deadline = Clock::now() + std::chrono::seconds(timeout_s);
    bool timed_out = false;
    bool eof = false;
    char buf[4096];
    while (!eof) {
        const Clock::time_point now = Clock::now();
        if (now >= deadline) { timed_out = true; break; }
        const long long left_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        struct pollfd pfd;
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int pr = poll(&pfd, 1, static_cast<int>(std::min<long long>(left_ms + 1, 1000)));
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;
        const ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            if (res.output.size() < kMaxChildOutput) res.output.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            eof = true;
        } else if (errno != EINTR && errno != EAGAIN) {
            eof = true;
        }
    }
    close(fds[0]);

    int status = 0;
    if (!timed_out) {
        for (;;) {      // the child has closed its output: it is leaving; give it what is left of the time
            const pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) break;
            if ((w < 0 && errno != EINTR) || Clock::now() >= deadline) {
                timed_out = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        res.kind = ChildOutcome::Kind::TimedOut;
        return res;
    }
    if (WIFSIGNALED(status)) {
        res.kind = ChildOutcome::Kind::Signaled;
        res.code = WTERMSIG(status);
    } else {
        res.kind = ChildOutcome::Kind::Exited;
        res.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return res;
}
#else
// Windows has no fork: the child runs in this process (no crash isolation; the limit is checked between ticks by the play itself)
ChildOutcome spawn_child(const std::string&, const std::vector<std::string>& args, uint32_t) {
    ChildOutcome res;
    Options o;
    std::string err;
    if (!parse_args(args, o, err)) {
        res.output = err;
        return res;
    }
    std::ostringstream captured;
    try {
        res.code = child_main(o, captured);
        res.kind = ChildOutcome::Kind::Exited;
    } catch (const std::exception& e) {
        captured << "uncaught exception: " << e.what() << "\n";
        res.kind = ChildOutcome::Kind::Exited;
        res.code = 1;
    }
    res.output = captured.str();
    return res;
}
#endif

std::string self_exe(const char* argv0) {
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        const fs::path p = fs::canonical(buf, ec);
        if (!ec) return p.string();
        return buf;
    }
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) return std::string(buf, static_cast<size_t>(n));
#endif
    std::error_code ec;
    const fs::path p = fs::absolute(argv0, ec);
    return ec ? std::string(argv0) : p.string();
}

struct TracePoint {
    uint32_t tick{0};
    sim::StateHash hash;
};
using Trace = std::vector<TracePoint>;

struct ParsedOutput {
    bool have_info{false};
    MapInfo info;
    std::vector<RunResult> results;
    std::vector<Trace> traces;      // by run
    int began{0};                   // BEGIN lines seen
    uint32_t last_tick{0};          // the last progress report
    std::string diagnostics;        // every other line (what the sanitizers and the C library write to stderr)
};

ParsedOutput parse_output(const std::string& text) {
    ParsedOutput po;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find('\n', i);
        const bool whole = end != std::string::npos;
        if (!whole) end = text.size();
        std::string line = text.substr(i, end - i);
        i = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        KvMap m;
        if (kv_parse(line, "INFO", m)) {
            po.have_info = true;
            po.info = info_from_kv(m);
        } else if (kv_parse(line, "RESULT", m)) {
            po.results.push_back(RunResult::from_kv(m));
        } else if (kv_parse(line, "BEGIN", m)) {
            ++po.began;
        } else if (kv_parse(line, "PROG", m)) {
            po.last_tick = static_cast<uint32_t>(kv_uint(m, "tick"));
        } else if (kv_parse(line, "TR", m)) {
            const size_t run = static_cast<size_t>(kv_uint(m, "run"));
            if (run < 16) {
                if (po.traces.size() <= run) po.traces.resize(run + 1);
                TracePoint tp;
                tp.tick = static_cast<uint32_t>(kv_uint(m, "tick"));
                tp.hash = RunResult::hash_from_kv(m);
                po.traces[run].push_back(tp);
            }
        } else if (!line.empty() && po.diagnostics.size() < 65536) {
            po.diagnostics += line + "\n";
        }
    }
    return po;
}

// What happened to one child process
struct Attempt {
    ChildOutcome outcome;
    ParsedOutput parsed;
    std::string failure;        // "" (the child did its job), "crash", "hang" or "error"
    std::string detail;
};

bool sanitizer_reported(const std::string& diagnostics, std::string& summary) {
    static const char* const kMarkers[] = {"ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "ThreadSanitizer", "Assertion failed", "assertion failed"};
    for (const char* m : kMarkers) {
        const size_t at = diagnostics.find(m);
        if (at != std::string::npos) {
            summary = one_line_diagnostic(diagnostics.substr(at), 400);
            return true;
        }
    }
    return false;
}

std::string progress_text(const ParsedOutput& po) {
    if (po.began == 0) return "before the first tick (loading or starting the match)";
    return fmt("in the %s, within 20 ticks after tick %u", po.began == 1 ? "first play" : "second play (the same engine played again in this process)", static_cast<unsigned>(po.last_tick));
}

Attempt classify(ChildOutcome outcome, uint32_t expected_runs, bool wants_run, uint32_t timeout_s) {
    Attempt a;
    a.parsed = parse_output(outcome.output);
    a.outcome = std::move(outcome);
    const ParsedOutput& po = a.parsed;
    std::string sanitizer;
    switch (a.outcome.kind) {
        case ChildOutcome::Kind::TimedOut:
            a.failure = "hang";
            a.detail = fmt("no end after %u s, killed; %s", timeout_s, progress_text(po).c_str());
            return a;
        case ChildOutcome::Kind::Signaled:
            a.failure = "crash";
            a.detail = fmt("%s %s", signal_name(a.outcome.code).c_str(), progress_text(po).c_str());
            return a;
        case ChildOutcome::Kind::SpawnFailed:
            a.failure = "error";
            a.detail = "the child process could not be started: " + one_line_diagnostic(a.outcome.output, 200);
            return a;
        case ChildOutcome::Kind::Exited:
            break;
    }
    if (a.outcome.code != 0) {
        if (sanitizer_reported(po.diagnostics, sanitizer)) {
            a.failure = "crash";
            a.detail = fmt("%s (exit code %d) %s", sanitizer.c_str(), a.outcome.code, progress_text(po).c_str());
        } else {
            a.failure = "error";
            a.detail = fmt("exit code %d %s %s", a.outcome.code, progress_text(po).c_str(), one_line_diagnostic(po.diagnostics, 200).c_str());
        }
        return a;
    }
    if (!po.have_info) {
        a.failure = "error";
        a.detail = "the child printed no description of the map: " + one_line_diagnostic(po.diagnostics, 200);
        return a;
    }
    for (const RunResult& r : po.results) {
        if (r.status != "ok") {
            a.failure = r.status == "timeout" ? "hang" : "error";
            a.detail = r.detail;
            return a;
        }
    }
    if (wants_run && po.info.loads && po.results.size() < expected_runs) {
        a.failure = "error";
        a.detail = "the child ended without reporting every play";
    }
    return a;
}

std::vector<std::string> child_args(const Options& o, const std::string& path, uint32_t roster, uint32_t repeat, bool no_run) {
    std::vector<std::string> a = {"--child", path, "--roster", std::to_string(roster), "--ticks", std::to_string(o.ticks), "--repeat", std::to_string(repeat),
                                  "--timeout-seconds", std::to_string(o.timeout_s), "--memory-mb", std::to_string(o.memory_mb), "--seed", std::to_string(o.seed)};
    if (no_run) a.push_back("--no-run");
    if (!o.topup) a.push_back("--no-topup");
    if (!o.fault_text.empty()) {
        a.push_back("--fault");
        a.push_back(o.fault_text);
    }
    return a;
}

Attempt run_attempt(const Options& o, const std::string& exe, const std::string& path, uint32_t roster, uint32_t repeat, bool no_run) {
    const uint32_t budget = no_run ? 60u : o.timeout_s * repeat + o.slack_s;
    return classify(spawn_child(exe, child_args(o, path, roster, repeat, no_run), budget), repeat, !no_run, budget);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The search for the first difference between two plays that were supposed to be equal
// ---------------------------------------------------------------------------------------------------------------------------------

std::string differing_parts(const sim::StateHash& a, const sim::StateHash& b) {
    std::string s;
    auto add = [&](bool differs, const char* name) {
        if (!differs) return;
        if (!s.empty()) s += ", ";
        s += name;
    };
    add(a.engine != b.engine, "engine");
    add(a.players != b.players, "players");
    add(a.grid != b.grid, "grid");
    add(a.food != b.food, "food");
    add(a.ants != b.ants, "ants");
    add(a.paths != b.paths, "paths");
    add(a.droppers != b.droppers, "droppers");
    return s.empty() ? "only the total" : s;
}

// The index of the first point at which the traces differ, or npos
size_t first_difference(const Trace& a, const Trace& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        if (a[i].tick != b[i].tick || a[i].hash.total != b[i].hash.total) return i;
    }
    return a.size() == b.size() ? std::string::npos : n;
}

// Replays the two plays with a hash every game second, then every tick of the second that differs. `cross_process`: two fresh processes; else the
// second play of one process against its first.
std::string localize_difference(const Options& o, const std::string& exe, const std::string& path, uint32_t roster, bool cross_process) {
    auto fetch = [&](uint32_t from, uint32_t to, uint32_t every, Trace& x, Trace& y) {
        auto args = [&](uint32_t repeat) {
            std::vector<std::string> a = child_args(o, path, roster, repeat, false);
            a.insert(a.end(), {"--trace-every", std::to_string(every), "--trace-from", std::to_string(from), "--trace-to", std::to_string(to)});
            return a;
        };
        if (cross_process) {
            const uint32_t budget = o.timeout_s + o.slack_s;
            const Attempt a = classify(spawn_child(exe, args(1), budget), 1, true, budget);
            const Attempt b = classify(spawn_child(exe, args(1), budget), 1, true, budget);
            if (a.parsed.traces.empty() || b.parsed.traces.empty()) return false;
            x = a.parsed.traces[0];
            y = b.parsed.traces[0];
        } else {
            const uint32_t budget = 2u * o.timeout_s + o.slack_s;
            const Attempt a = classify(spawn_child(exe, args(2), budget), 2, true, budget);
            if (a.parsed.traces.size() < 2) return false;
            x = a.parsed.traces[0];
            y = a.parsed.traces[1];
        }
        return true;
    };
    Trace x, y;
    if (!fetch(0, o.ticks, kTraceCoarse, x, y)) return "the replay with hashes failed";
    const size_t i = first_difference(x, y);
    if (i == std::string::npos) return "the plays agreed when replayed with hashes (the difference does not repeat every time)";
    if (i >= x.size() || i >= y.size()) return fmt("the plays ran for different numbers of ticks (%zu and %zu checkpoints)", x.size(), y.size());
    const uint32_t hi = x[i].tick;
    const uint32_t lo = (i > 0) ? x[i - 1].tick + 1 : 0;
    auto where = [](uint32_t tick) { return tick == 0 ? std::string("right after init() (tick 0)") : fmt("after tick %u", static_cast<unsigned>(tick)); };
    if (hi > lo) {
        Trace fx, fy;
        if (fetch(lo, hi, 1, fx, fy)) {
            const size_t j = first_difference(fx, fy);
            if (j != std::string::npos && j < fx.size() && j < fy.size()) {
                return fmt("first difference %s in: %s", where(fx[j].tick).c_str(), differing_parts(fx[j].hash, fy[j].hash).c_str());
            }
        }
        return fmt("first difference between ticks %u and %u in: %s", static_cast<unsigned>(lo), static_cast<unsigned>(hi), differing_parts(x[i].hash, y[i].hash).c_str());
    }
    return fmt("first difference %s in: %s", where(hi).c_str(), differing_parts(x[i].hash, y[i].hash).c_str());
}

// ---------------------------------------------------------------------------------------------------------------------------------
// One map
// ---------------------------------------------------------------------------------------------------------------------------------

struct RunReport {
    uint32_t roster{0};
    std::string status{"skipped"};      // ok, crash, hang, error, skipped
    std::string detail;
    RunResult first;                    // the first play (its numbers are the report's, the timings are the lower of the two processes')
    bool have_first{false};
    double mean_tick_us{0};
    double max_tick_us{0};
    uint64_t memory_kb{0};
    bool have_second{false};            // the play of the second process
    uint64_t second_hash{0};
    uint64_t second_digest{0};
    bool have_rerun{false};             // the second play of the first process (the same engine object)
    uint64_t rerun_hash{0};
    uint64_t rerun_digest{0};
    bool deterministic{true};
    std::string divergence;
    std::string diagnostic;             // what a sanitizer or the C library wrote to stderr, scrubbed of paths
};

struct MapReport {
    std::string name;
    uint64_t size{0};
    bool hashed{false};
    uint64_t hash{0};
    NameCheck name_check;
    MapInfo info;
    std::string load_child_failure;     // "", or how the child died before it could describe the file: crash, hang or error
    std::vector<RunReport> runs;
};

int severity(const std::string& status) {
    if (status == "crash") return 3;
    if (status == "hang") return 2;
    if (status == "error") return 1;
    return 0;
}

void add_detail(std::string& to, const std::string& what) {
    if (what.empty()) return;
    if (!to.empty()) to += "; ";
    to += what;
}

RunReport play_roster(const Options& o, const std::string& exe, const std::string& path, uint32_t roster, const Attempt& first_attempt) {
    RunReport rr;
    rr.roster = roster;
    const Attempt& a = first_attempt;
    const Attempt b = (a.failure == "hang") ? Attempt() : run_attempt(o, exe, path, roster, 1, false);   // a hang has cost its time once already
    rr.status = "ok";
    if (severity(a.failure) >= severity(b.failure)) {
        rr.status = a.failure.empty() ? "ok" : a.failure;
    } else {
        rr.status = b.failure;
    }
    if (!a.failure.empty()) add_detail(rr.detail, "first process: " + a.detail);
    if (!b.failure.empty()) add_detail(rr.detail, "second process: " + b.detail);

    const RunResult* r0 = (a.parsed.results.size() >= 1 && a.parsed.results[0].status == "ok") ? &a.parsed.results[0] : nullptr;
    const RunResult* r1 = (a.parsed.results.size() >= 2 && a.parsed.results[1].status == "ok") ? &a.parsed.results[1] : nullptr;
    const RunResult* rb = (b.parsed.results.size() >= 1 && b.parsed.results[0].status == "ok") ? &b.parsed.results[0] : nullptr;
    if (r0 != nullptr) {
        rr.first = *r0;
        rr.have_first = true;
        rr.mean_tick_us = r0->mean_us();
        rr.max_tick_us = r0->max_us();
        rr.memory_kb = r0->memory_kb;
    } else if (rb != nullptr) {
        rr.first = *rb;
        rr.have_first = true;
        rr.mean_tick_us = rb->mean_us();
        rr.max_tick_us = rb->max_us();
        rr.memory_kb = rb->memory_kb;
    }
    if (r0 != nullptr && rb != nullptr) {      // the lower of the two is the steadier estimate (the machine is busy with other plays)
        rr.mean_tick_us = std::min(r0->mean_us(), rb->mean_us());
        rr.max_tick_us = std::min(r0->max_us(), rb->max_us());
        rr.memory_kb = std::max(r0->memory_kb, rb->memory_kb);
    }
    if (rb != nullptr) {
        rr.have_second = true;
        rr.second_hash = rb->hash.total;
        rr.second_digest = rb->digest;
    }
    if (r1 != nullptr) {
        rr.have_rerun = true;
        rr.rerun_hash = r1->hash.total;
        rr.rerun_digest = r1->digest;
    }
    std::string diagnostics = a.parsed.diagnostics + b.parsed.diagnostics;
    if (!diagnostics.empty()) rr.diagnostic = one_line_diagnostic(diagnostics, 1200);

    const bool cross = r0 != nullptr && rb != nullptr && (r0->hash.total != rb->hash.total || r0->digest != rb->digest);
    const bool rerun = r0 != nullptr && r1 != nullptr && (r0->hash.total != r1->hash.total || r0->digest != r1->digest);
    if (cross || rerun) {
        rr.deterministic = false;
        if (cross) add_detail(rr.divergence, "two processes differ: " + localize_difference(o, exe, path, roster, true));
        if (rerun) add_detail(rr.divergence, "the second play of one process differs from its first: " + localize_difference(o, exe, path, roster, false));
    }
    // the same input with a crash in one process and not in the other is not deterministic either (a hang may only be a busy machine)
    if ((a.failure == "crash") != (b.failure == "crash") && a.failure != "hang" && b.failure != "hang") {
        rr.deterministic = false;
        add_detail(rr.divergence, "one of two identical plays crashed and the other did not");
    }
    return rr;
}

MapReport sweep_map(const Options& o, const std::string& exe, const fs::path& file) {
    MapReport r;
    r.name = file.filename().string();
    std::error_code ec;
    const uintmax_t size = fs::file_size(file, ec);
    r.size = ec ? 0 : static_cast<uint64_t>(size);
    r.hashed = net::hash_file(file.string(), r.hash);
    r.name_check = check_name(r.name);

    const uint32_t first_roster = o.roster > 0 ? static_cast<uint32_t>(o.roster) : kAllTeams;
    const Attempt a = run_attempt(o, exe, file.string(), first_roster, o.skip_run ? 1u : 2u, o.skip_run);
    if (a.parsed.have_info) {
        r.info = a.parsed.info;
    } else {
        r.load_child_failure = a.failure;
        r.info.load_error = "the child could not describe the file: " + a.detail;
    }
    if (!r.info.loads || o.skip_run) return r;

    // A roster with a team whose start marker lies outside the grid is not played: LevelData::validate refuses it (the original has no behaviour for it)
    auto refused = [&](uint32_t roster) {
        RunReport rr;
        rr.roster = roster;
        rr.status = "refused";
        rr.detail = r.info.blocked_reason;
        return rr;
    };
    if ((r.info.blocked_teams & first_roster) != 0) {
        r.runs.push_back(refused(first_roster));
    } else {
        r.runs.push_back(play_roster(o, exe, file.string(), first_roster, a));
    }
    if (o.roster < 0 && r.info.has_team(0) && r.info.has_team(3)) {
        if ((r.info.blocked_teams & kGreenAndBlack) != 0) {
            r.runs.push_back(refused(kGreenAndBlack));
        } else {
            const Attempt two = run_attempt(o, exe, file.string(), kGreenAndBlack, 2, false);
            r.runs.push_back(play_roster(o, exe, file.string(), kGreenAndBlack, two));
        }
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------------------------------------------------------------

std::string run_verdict(const RunReport& r) {
    if (r.status == "crash") return "CRASH";
    if (r.status == "hang") return "HANG";
    if (r.status == "error") return "ERROR";
    if (r.status == "refused") return "REFUSED";
    if (!r.deterministic) return "NONDET";
    return r.status == "ok" ? "ok" : "-";
}

bool map_failed(const MapReport& m) {
    if (!m.info.loads) return true;
    for (const RunReport& r : m.runs) {
        if (r.status == "crash" || r.status == "hang" || r.status == "error" || r.status == "refused" || !r.deterministic) return true;
    }
    return false;
}

struct Summary {
    size_t maps{0}, loads{0}, load_failures{0}, bad_names{0}, names_over_limit{0};
    size_t crashes{0}, hangs{0}, errors{0}, nondeterministic{0}, runs{0}, with_diagnostics{0};
    size_t refused{0};              // rosters that were not played because a team of them has a start marker outside the grid
    size_t partial{0};              // maps that load but are read only as far as the original reads them (a finding of severity Warning)
    double mean_of_means{0}, median{0}, p95{0}, maximum{0};
    uint32_t peak_ants{0};
    struct Entry {
        std::string name;
        uint32_t roster;
        double value;
    };
    std::vector<Entry> slowest;
    std::vector<Entry> biggest;
};

Summary summarize(const std::vector<MapReport>& maps) {
    Summary s;
    s.maps = maps.size();
    std::vector<double> means;
    std::vector<Summary::Entry> speeds, sizes;
    for (const MapReport& m : maps) {
        if (m.info.loads) ++s.loads; else ++s.load_failures;
        if (!m.name_check.ok) ++s.bad_names;
        if (m.name_check.too_long) ++s.names_over_limit;
        bool crashed = m.load_child_failure == "crash";
        bool hung = m.load_child_failure == "hang";
        bool errored = m.load_child_failure == "error";
        bool nondet = false, diag = false;
        if (m.info.loads) {
            for (const assets::LevelProblem& p : m.info.problems) {
                if (p.severity == assets::LevelProblemSeverity::Warning) {
                    ++s.partial;
                    break;
                }
            }
        }
        for (const RunReport& r : m.runs) {
            if (r.status == "refused") {
                ++s.refused;
                continue;
            }
            ++s.runs;
            crashed = crashed || r.status == "crash";
            hung = hung || r.status == "hang";
            errored = errored || r.status == "error";
            nondet = nondet || !r.deterministic;
            diag = diag || !r.diagnostic.empty();
            if (r.have_first) {
                means.push_back(r.mean_tick_us);
                s.peak_ants = std::max(s.peak_ants, r.first.peak_ants);
                speeds.push_back({m.name, r.roster, r.mean_tick_us});
                sizes.push_back({m.name, r.roster, static_cast<double>(r.memory_kb)});
            }
        }
        if (crashed) ++s.crashes;
        if (hung) ++s.hangs;
        if (errored) ++s.errors;
        if (nondet) ++s.nondeterministic;
        if (diag) ++s.with_diagnostics;
    }
    if (!means.empty()) {
        std::sort(means.begin(), means.end());
        double sum = 0;
        for (double v : means) sum += v;
        s.mean_of_means = sum / static_cast<double>(means.size());
        s.median = means[means.size() / 2];
        s.p95 = means[std::min(means.size() - 1, means.size() * 95 / 100)];
        s.maximum = means.back();
    }
    auto by_value = [](const Summary::Entry& a, const Summary::Entry& b) { return a.value > b.value; };
    std::stable_sort(speeds.begin(), speeds.end(), by_value);
    std::stable_sort(sizes.begin(), sizes.end(), by_value);
    speeds.resize(std::min<size_t>(speeds.size(), 10));
    sizes.resize(std::min<size_t>(sizes.size(), 10));
    s.slowest = speeds;
    s.biggest = sizes;
    return s;
}

void print_table(const std::vector<MapReport>& maps, std::FILE* to) {
    const size_t kNameWidth = 40;
    std::fprintf(to, "%s %9s %8s %-4s %-5s %-4s %9s %6s %8s %-16s %s\n", column_text("map", kNameWidth).c_str(), "bytes", "w x h", "name", "loads", "team", "ticks/s",
                 "ants", "MB", "hash", "result");
    for (const MapReport& m : maps) {
        const std::string name = column_text(m.name, kNameWidth);
        const std::string dims = m.info.have_dims ? std::to_string(m.info.width) + "x" + std::to_string(m.info.height) : "-";
        auto head = [&]() {
            std::fprintf(to, "%s %9llu %8s %-4s %-5s ", name.c_str(), static_cast<unsigned long long>(m.size), dims.c_str(), m.name_check.ok ? "ok" : "BAD",
                         m.info.loads ? "yes" : "NO");
        };
        if (m.runs.empty()) {
            head();
            std::fprintf(to, "%-4s %9s %6s %8s %-16s %s\n", "-", "-", "-", "-", m.hashed ? hex64(m.hash).c_str() : "-", m.info.loads ? "-" : (m.load_child_failure.empty() ? "LOADFAIL" : ("load " + m.load_child_failure).c_str()));
            continue;
        }
        for (const RunReport& r : m.runs) {
            head();
            const std::string team = "R" + std::to_string(r.roster);
            if (r.have_first) {
                std::fprintf(to, "%-4s %9.0f %6u %8.1f %-16s %s\n", team.c_str(), r.mean_tick_us > 0 ? 1e6 / r.mean_tick_us : 0.0, static_cast<unsigned>(r.first.peak_ants),
                             static_cast<double>(r.memory_kb) / 1024.0, hex64(r.first.hash.total).c_str(), run_verdict(r).c_str());
            } else {
                std::fprintf(to, "%-4s %9s %6s %8s %-16s %s\n", team.c_str(), "-", "-", "-", "-", run_verdict(r).c_str());
            }
        }
    }
}

void print_findings(const std::vector<MapReport>& maps, std::FILE* to) {
    bool any = false;
    for (const MapReport& m : maps) {
        if (!m.info.loads) {
            if (!any) std::fprintf(to, "\nFindings\n");
            any = true;
            std::fprintf(to, "  %s: does not load: %s\n", safe_text(m.name).c_str(), m.info.load_error.c_str());
        }
        for (const RunReport& r : m.runs) {
            if (r.status != "ok" && r.status != "skipped") {
                if (!any) std::fprintf(to, "\nFindings\n");
                any = true;
                std::fprintf(to, "  %s [R%u]: %s: %s\n", safe_text(m.name).c_str(), r.roster, r.status.c_str(), r.detail.c_str());
            }
            if (!r.deterministic) {
                if (!any) std::fprintf(to, "\nFindings\n");
                any = true;
                std::fprintf(to, "  %s [R%u]: NOT DETERMINISTIC: %s\n", safe_text(m.name).c_str(), r.roster, r.divergence.c_str());
            }
        }
    }
}

void print_summary(const Summary& s, double seconds, std::FILE* to) {
    std::fprintf(to, "\nSummary\n");
    std::fprintf(to, "  maps                 %zu\n", s.maps);
    std::fprintf(to, "  load                 %zu\n", s.loads);
    std::fprintf(to, "  load failures        %zu\n", s.load_failures);
    std::fprintf(to, "  load with warnings   %zu (the original reads the file only as far as it goes, or its tables are indeterminate)\n", s.partial);
    std::fprintf(to, "  refused rosters      %zu (a team of the roster has a start marker outside the grid)\n", s.refused);
    std::fprintf(to, "  bad names            %zu (%zu longer than %zu bytes)\n", s.bad_names, s.names_over_limit, net::kMaxMapNameChars);
    std::fprintf(to, "  crashes              %zu\n", s.crashes);
    std::fprintf(to, "  hangs                %zu\n", s.hangs);
    std::fprintf(to, "  errors               %zu\n", s.errors);
    std::fprintf(to, "  not deterministic    %zu\n", s.nondeterministic);
    std::fprintf(to, "  plays                %zu (%zu with sanitizer or library diagnostics)\n", s.runs, s.with_diagnostics);
    if (s.runs > 0) {
        std::fprintf(to, "  tick time (mean of the plays' means) %.1f us, median %.1f us, 95th percentile %.1f us, worst %.1f us; peak ants %u\n", s.mean_of_means, s.median, s.p95,
                     s.maximum, static_cast<unsigned>(s.peak_ants));
        std::fprintf(to, "  slowest by mean tick:\n");
        for (const Summary::Entry& e : s.slowest) std::fprintf(to, "    %9.1f us  %s [R%u]\n", e.value, safe_text(e.name).c_str(), e.roster);
        std::fprintf(to, "  biggest by memory:\n");
        for (const Summary::Entry& e : s.biggest) std::fprintf(to, "    %9.1f MB  %s [R%u]\n", e.value / 1024.0, safe_text(e.name).c_str(), e.roster);
    }
    std::fprintf(to, "  the sweep took %.1f s\n", seconds);
}

void write_run_json(JsonWriter& j, const RunReport& r) {
    j.begin_object();
    j.field("roster", static_cast<uint64_t>(r.roster));
    j.field("status", r.status);
    j.field("detail", r.detail);
    j.field("ticks_done", static_cast<uint64_t>(r.have_first ? r.first.ticks_done : 0));
    j.field("ended_at", static_cast<uint64_t>(r.have_first ? r.first.ended_at : 0));
    j.field("mean_tick_us", r.mean_tick_us, 1);
    j.field("max_tick_us", r.max_tick_us, 1);
    j.field("peak_ants", static_cast<uint64_t>(r.have_first ? r.first.peak_ants : 0));
    j.field("final_ants", static_cast<uint64_t>(r.have_first ? r.first.final_ants : 0));
    j.field("hatched", static_cast<uint64_t>(r.have_first ? r.first.hatched : 0));
    j.field("orders", static_cast<uint64_t>(r.have_first ? r.first.orders : 0));
    j.field("orders_applied", static_cast<uint64_t>(r.have_first ? r.first.orders_applied : 0));
    j.field("memory_kb", r.memory_kb);
    j.field("hash", r.have_first ? hex64(r.first.hash.total) : std::string());
    j.field("hash_second_process", r.have_second ? hex64(r.second_hash) : std::string());
    j.field("hash_rerun_same_process", r.have_rerun ? hex64(r.rerun_hash) : std::string());
    j.field("digest", r.have_first ? hex64(r.first.digest) : std::string());
    j.field("digest_second_process", r.have_second ? hex64(r.second_digest) : std::string());
    j.field("digest_rerun_same_process", r.have_rerun ? hex64(r.rerun_digest) : std::string());
    j.field_bool("deterministic", r.deterministic);
    j.field("divergence", r.divergence);
    j.field("diagnostic", r.diagnostic);
    j.end_object();
}

void write_counts(JsonWriter& j, const char* key, const std::array<uint32_t, 4>& a) {
    j.key(key);
    j.begin_array();
    for (uint32_t v : a) j.num(static_cast<uint64_t>(v));
    j.end_array();
}

std::string report_json(const Options& o, const std::vector<MapReport>& maps, const Summary& s) {
    JsonWriter j;
    j.begin_object();
    j.field("tool", "map_sweep");
    j.field("format", static_cast<uint64_t>(1));
    j.key("settings");
    j.begin_object();
    j.field("ticks", static_cast<uint64_t>(o.ticks));
    j.field("roster", o.roster > 0 ? std::to_string(o.roster) : std::string("default"));
    j.field("timeout_seconds", static_cast<uint64_t>(o.timeout_s));
    j.field("memory_limit_mb", static_cast<uint64_t>(o.memory_mb));
    j.field("seed", static_cast<uint64_t>(o.seed));
    j.field("skip_run", o.skip_run ? "yes" : "no");
    j.field("name_limit_bytes", static_cast<uint64_t>(net::kMaxMapNameChars));
    j.end_object();
    j.key("summary");
    j.begin_object();
    j.field("maps", static_cast<uint64_t>(s.maps));
    j.field("load", static_cast<uint64_t>(s.loads));
    j.field("load_failures", static_cast<uint64_t>(s.load_failures));
    j.field("load_with_warnings", static_cast<uint64_t>(s.partial));
    j.field("refused_rosters", static_cast<uint64_t>(s.refused));
    j.field("bad_names", static_cast<uint64_t>(s.bad_names));
    j.field("names_over_limit", static_cast<uint64_t>(s.names_over_limit));
    j.field("crashes", static_cast<uint64_t>(s.crashes));
    j.field("hangs", static_cast<uint64_t>(s.hangs));
    j.field("errors", static_cast<uint64_t>(s.errors));
    j.field("nondeterministic", static_cast<uint64_t>(s.nondeterministic));
    j.field("plays", static_cast<uint64_t>(s.runs));
    j.field("plays_with_diagnostics", static_cast<uint64_t>(s.with_diagnostics));
    j.field("mean_tick_us_average", s.mean_of_means, 1);
    j.field("mean_tick_us_median", s.median, 1);
    j.field("mean_tick_us_p95", s.p95, 1);
    j.field("mean_tick_us_worst", s.maximum, 1);
    j.field("peak_ants", static_cast<uint64_t>(s.peak_ants));
    j.key("slowest");
    j.begin_array();
    for (const Summary::Entry& e : s.slowest) {
        j.begin_object();
        j.field("name", e.name);
        j.field("roster", static_cast<uint64_t>(e.roster));
        j.field("mean_tick_us", e.value, 1);
        j.end_object();
    }
    j.end_array();
    j.key("biggest");
    j.begin_array();
    for (const Summary::Entry& e : s.biggest) {
        j.begin_object();
        j.field("name", e.name);
        j.field("roster", static_cast<uint64_t>(e.roster));
        j.field("memory_kb", static_cast<uint64_t>(e.value));
        j.end_object();
    }
    j.end_array();
    j.end_object();
    j.key("maps");
    j.begin_array();
    for (const MapReport& m : maps) {
        j.begin_object();
        j.field("name", m.name);
        j.field("size", m.size);
        j.field("hash", m.hashed ? hex64(m.hash) : std::string());
        j.field_bool("name_ok", m.name_check.ok);
        j.field("name_bytes", static_cast<uint64_t>(m.name_check.length));
        j.field_bool("name_too_long", m.name_check.too_long);
        j.key("name_bad_chars");
        j.begin_array();
        for (const std::string& c : m.name_check.bad_chars) j.str(c);
        j.end_array();
        j.field("name_issue", m.name_check.issue);
        j.field_bool("loads", m.info.loads);
        j.field("load_error", m.info.load_error);
        j.field("load_child_failure", m.load_child_failure);
        j.field("version", static_cast<uint64_t>(m.info.version));
        j.field("game_mode", static_cast<uint64_t>(m.info.game_mode));
        j.field("minutes", static_cast<uint64_t>(m.info.minutes));
        j.field("width", static_cast<uint64_t>(m.info.width));
        j.field("height", static_cast<uint64_t>(m.info.height));
        j.field("description", m.info.description);
        j.field("tile_types", static_cast<uint64_t>(m.info.tile_types));
        j.field("spawn_records", static_cast<uint64_t>(m.info.spawns));
        write_counts(j, "start_markers", m.info.starts);
        write_counts(j, "hill_cells", m.info.hills);
        j.field("plants", static_cast<uint64_t>(m.info.plants));
        j.field("food_objects", static_cast<uint64_t>(m.info.food));
        j.field("waypoints", static_cast<uint64_t>(m.info.waypoints));
        j.field("eggs", static_cast<uint64_t>(m.info.eggs));
        j.field_bool("playable", m.info.loads && m.info.playable);
        j.key("problems");
        j.begin_array();
        for (const assets::LevelProblem& p : m.info.problems) {
            j.begin_object();
            j.field("severity", assets::to_string(p.severity));
            j.field("kind", assets::to_string(p.kind));
            j.field("team", static_cast<uint64_t>(p.team < 0 ? 255 : p.team));            // 255 = no team (the numbering of AnthillSpawn::team_id)
            j.field("count", static_cast<uint64_t>(p.count));
            j.field("message", p.message);
            j.end_object();
        }
        j.end_array();
        j.key("runs");
        j.begin_array();
        for (const RunReport& r : m.runs) write_run_json(j, r);
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.end_object();
    return j.text() + "\n";
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The sweep
// ---------------------------------------------------------------------------------------------------------------------------------

bool is_map_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    return lower_ascii(p.extension().string()) == ".lvl";
}

bool list_maps(const std::string& dir, const std::string& only, std::vector<fs::path>& out, std::string& err) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        err = "not a folder: " + dir;
        return false;
    }
    const std::string needle = lower_ascii(only);
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!is_map_file(it->path())) continue;
        if (!needle.empty() && lower_ascii(it->path().filename().string()).find(needle) == std::string::npos) continue;
        out.push_back(it->path());
    }
    if (ec) {
        err = "cannot read the folder: " + ec.message();
        return false;
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
    return true;
}

std::vector<MapReport> sweep_files(const Options& o, const std::string& exe, const std::vector<fs::path>& files, bool progress) {
    std::vector<MapReport> reports(files.size());
    unsigned jobs = o.jobs;
    if (jobs == 0) {
        const unsigned cores = std::thread::hardware_concurrency();
        jobs = cores > 3 ? cores - 2 : 1;
    }
#ifdef _WIN32
    jobs = 1;       // the engine runs in this process: one map at a time
#endif
    jobs = static_cast<unsigned>(std::min<size_t>(jobs, std::max<size_t>(files.size(), 1)));
    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    std::mutex print_mutex;
    auto worker = [&]() {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= files.size()) return;
            try {
                reports[i] = sweep_map(o, exe, files[i]);
            } catch (const std::exception& e) {
                reports[i].name = files[i].filename().string();
                reports[i].info.load_error = std::string("the sweep failed on this file: ") + e.what();
            }
            const size_t n = ++done;
            if (progress) {
                std::lock_guard<std::mutex> lock(print_mutex);
                if (map_failed(reports[i])) std::fprintf(stderr, "[%zu/%zu] finding: %s\n", n, files.size(), safe_text(reports[i].name).c_str());
                else if (n % 25 == 0 || n == files.size()) std::fprintf(stderr, "[%zu/%zu]\n", n, files.size());
            }
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 1; t < jobs; ++t) threads.emplace_back(worker);
    worker();
    for (std::thread& t : threads) t.join();
    return reports;
}

bool all_good(const std::vector<MapReport>& maps) {
    for (const MapReport& m : maps) {
        if (map_failed(m)) return false;
    }
    return true;
}

int run_sweep(const Options& o, const std::string& exe) {
    std::vector<fs::path> files;
    std::string err;
    if (!list_maps(o.maps_dir, o.only, files, err)) {
        std::fprintf(stderr, "map_sweep: %s\n", err.c_str());
        return 2;
    }
    if (files.empty()) {
        std::fprintf(stderr, "map_sweep: no .lvl files%s in that folder\n", o.only.empty() ? "" : " with that text in the name");
        return 2;
    }
    const Clock::time_point started = Clock::now();
    const std::vector<MapReport> maps = sweep_files(o, exe, files, true);
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    const Summary s = summarize(maps);
    print_table(maps, stdout);
    print_findings(maps, stdout);
    print_summary(s, seconds, stdout);
    if (!o.out.empty()) {
        std::ofstream f(o.out, std::ios::binary | std::ios::trunc);
        f << report_json(o, maps, s);
        if (!f.good()) {
            std::fprintf(stderr, "map_sweep: cannot write %s\n", o.out.c_str());
            return 2;
        }
    }
    return all_good(maps) ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// --selftest
// ---------------------------------------------------------------------------------------------------------------------------------

// A small JSON syntax checker (RFC 8259 without the number grammar's finer points): the report must parse
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
            if (c < 0x20) return false;
            if (c == '\\') {
                ++i_;
                if (i_ >= s_.size()) return false;
                if (s_[i_] == 'u') {
                    for (int k = 1; k <= 4; ++k) {
                        if (i_ + static_cast<size_t>(k) >= s_.size() || hex_digit(s_[i_ + static_cast<size_t>(k)]) < 0) return false;
                    }
                    i_ += 4;
                } else if (std::strchr("\"\\/bfnrt", s_[i_]) == nullptr) {
                    return false;
                }
                ++i_;
            } else if (c >= 0x80) {
                const size_t n = utf8_sequence_length(s_, i_);
                if (n == 0) return false;
                i_ += n;
            } else {
                ++i_;
            }
        }
        return false;
    }
    bool number() {
        const size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-')) ++i_;
        return i_ > start && s_[start] != '.';
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

bool read_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const std::streamoff size = f.tellg();
    out.assign(static_cast<size_t>(size), 0);
    f.seekg(0);
    return size == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size));
}

void write_file(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

const RunReport* find_run(const MapReport& m, uint32_t roster) {
    for (const RunReport& r : m.runs) {
        if (r.roster == roster) return &r;
    }
    return nullptr;
}

int selftest(const std::string& exe) {
    SelfTest t;
    const Clock::time_point started = Clock::now();
    fs::path maps = fs::path(ORIGINAL_ASSETS_DIR) / "Maps";
    std::error_code ec;
    if (!fs::is_directory(maps, ec)) maps = fs::path("Original-Ants") / "Maps";
    std::printf("map_sweep selftest (maps of %s)\n", fs::is_directory(maps, ec) ? "the shipped game" : "nowhere: not found");
    if (!fs::is_directory(maps, ec)) {
        std::printf("  the shipped maps are missing\n");
        return 1;
    }

    t.section("text helpers");
    t.check(json_quote(std::string("a\"b\\c\n\x01", 7)) == "\"a\\\"b\\\\c\\u000a\\u0001\"", "json_quote escapes quotes, backslashes and control characters");
    t.check(json_quote("caf\xC3\xA9") == "\"caf\xC3\xA9\"", "json_quote keeps valid UTF-8");
    t.check(json_quote("a\xFFz") == "\"a\\u00ffz\"", "json_quote turns a byte that is not UTF-8 into a character");
    t.check(JsonChecker(json_quote(std::string("x\0y\xC0\x80z\xED\xA0\x80", 9))).valid(), "json_quote output is valid for hostile bytes");
    t.check(scrub_paths("ERROR at /Users/someone/proj/src/x.cpp:12:3 in f(int) (C:\\dev\\lib.dll+0x1)") == "ERROR at x.cpp:12:3 in f(int) lib.dll+0x1)", "scrub_paths keeps file names and drops directories");
    {
        // a diagnostic longer than the limit must not leave a piece of a path where the text was cut, whatever the alignment of the cut
        const std::string word = "/home/someone/project/include/ants_sim/ant_unit.hpp:257:24:";
        bool clean = true;
        for (size_t pad = 0; pad < 70 && clean; ++pad) {
            std::string text(pad, 'z');
            for (int i = 0; i < 14; ++i) text += " " + word;
            std::istringstream tokens(one_line_diagnostic(text, 400));
            std::string token;
            while (tokens >> token) {
                if (token != "ant_unit.hpp:257:24:" && token != std::string(pad, 'z')) clean = false;
            }
        }
        t.check(clean, "a diagnostic cut at the limit keeps no piece of a path");
    }
    t.check(kv_unescape(kv_escape("a b%c\n=\xFF")) == "a b%c\n=\xFF", "key=value escaping round-trips every byte");
    {
        KvMap m;
        t.check(kv_parse(Kv().add("a", "x y").add("n", static_cast<uint64_t>(7)).line("TAG"), "TAG", m) && m["a"] == "x y" && kv_uint(m, "n") == 7, "a message parses back");
        t.check(!kv_parse("TAGX a=1", "TAG", m) && !kv_parse("TAG", "TAG", m), "a line of another tag is not a message");
    }
    {
        Options o;
        std::string err;
        t.check(parse_args({"dir", "--ticks", "100", "--roster", "9", "--jobs", "3", "--only", "x"}, o, err) && o.ticks == 100 && o.roster == 9 && o.jobs == 3 && o.maps_dir == "dir", "options parse");
        Options bad;
        t.check(!parse_args({"--ticks", "-5"}, bad, err) && !parse_args({"--roster", "16"}, bad, err) && !parse_args({"--wat"}, bad, err) && !parse_args({"a", "b"}, bad, err) &&
                    !parse_args({"--fault", "wat"}, bad, err), "bad options are refused");
    }

    t.section("names against the protocol");
    {
        const NameCheck good = check_name("TREASURE.LVL");
        t.check(good.ok && good.issue.empty() && !good.too_long && good.length == 12, "a plain name is accepted");
        const NameCheck spaced = check_name("My Map!!.lvl");                       // protocol 6: spaces and '!' travel
        t.check(spaced.ok && spaced.bad_chars.empty() && spaced.issue.empty(), "a space and '!' are accepted (protocol 6)");
        const NameCheck forbidden = check_name("My:Map**.lvl");
        t.check(!forbidden.ok && forbidden.bad_chars.size() == 2 && forbidden.bad_chars[0] == ":" && forbidden.bad_chars[1] == "*", "a ':' and a '*' are named once each");
        const NameCheck longer = check_name(std::string(70, 'a') + ".LVL");
        t.check(!longer.ok && longer.too_long && longer.bad_chars.empty() && longer.issue.find("74 bytes") != std::string::npos, "a name over the limit says so");
        const NameCheck utf = check_name("caf\xC3\xA9.lvl");
        t.check(!utf.ok && utf.bad_chars.size() == 2 && utf.bad_chars[0] == "\\xC3", "a non-ASCII name lists its bytes");
        t.check(!check_name(".hidden.lvl").ok && check_name("a..b.lvl").ok && !check_name("MAP.Lvl").ok && check_name("MAP.lvl").ok, "a leading dot and a mixed case extension are refused, '..' inside a name is accepted");
        t.check(!check_name("MAP.Lvl").issue.empty() && !check_name(".hidden.lvl").issue.empty(), "a refusal always has a reason");
    }

    t.section("the six shipped maps load and their structure is read");
    std::vector<fs::path> files;
    std::string err;
    t.check(list_maps(maps.string(), "", files, err) && files.size() == 6, "six maps are listed in name order");
    t.check(files.size() == 6 && files[0].filename().string() == "GAUNTLET.LVL" && files[5].filename().string() == "TREASURE.LVL", "the order is the order of the names");
    {
        MapInfo info;
        assets::LevelData level;
        analyze_map((maps / "TINY.LVL").string(), info, level);
        t.check(info.loads && info.version == 8 && info.game_mode == 1 && info.width == 31 && info.height == 31, "TINY.LVL is a version 8 map of 31 x 31");
        t.check(info.has_team(0) && info.has_team(1) && info.has_team(2) && info.has_team(3), "TINY.LVL has a start marker or hill for each team");
        MapInfo copy = info_from_kv([&]() { KvMap m; kv_parse(info_line(info), "INFO", m); return m; }());
        t.check(copy.loads == info.loads && copy.width == info.width && copy.description == info.description && copy.starts == info.starts && copy.hills == info.hills &&
                    copy.plants == info.plants && copy.eggs == info.eggs, "the child's description of a map survives the pipe");
    }

    t.section("loader failures are diagnosed");
    {
        std::vector<uint8_t> good;
        t.check(read_file(maps / "TINY.LVL", good), "TINY.LVL is read");
        auto diagnosis = [&](std::vector<uint8_t> bytes) {
            assets::LevelData level;
            assets::LevelValidation verdict;
            const bool loads = level.load_from_memory(bytes.data(), bytes.size(), &verdict);
            return std::make_pair(loads, verdict.reason());
        };
        t.check(diagnosis(good) == std::make_pair(true, std::string()), "a good file has no diagnosis");
        auto cut = [&](size_t n) { return std::vector<uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n)); };
        const auto tiny = diagnosis(cut(20));
        t.check(!tiny.first && tiny.second.find("inside the header") != std::string::npos, "20 bytes: the header is cut");
        std::vector<uint8_t> v = good;
        v[0] = 9;
        const auto ver = diagnosis(v);
        t.check(!ver.first && ver.second.find("version 9") != std::string::npos, "version 9 is named");
        v = good;
        v[4] = 2;
        {
            // the original stores the mode dword and never reads it again: any value loads (the community editor leaves garbage there), the finding says so
            assets::LevelData level;
            assets::LevelValidation verdict;
            t.check(level.load_from_memory(v.data(), v.size(), &verdict) && level.game_mode == 2 && verdict.playable && verdict.has(assets::LevelProblemKind::ModeIgnored) &&
                        !verdict.has(assets::LevelProblemKind::WrongVersion), "game mode 2 loads (the original ignores the mode) and the finding names it");
        }
        const size_t dims_at = 42 + (static_cast<size_t>(good[40]) | (static_cast<size_t>(good[41]) << 8)) * 11 + 11;
        v = good;
        v[dims_at] = 0;
        v[dims_at + 1] = 0;
        v[dims_at + 2] = 0;
        v[dims_at + 3] = 0;
        const auto zero = diagnosis(v);
        t.check(!zero.first && zero.second.find("0 rows") != std::string::npos, "a grid of 0 rows is named");
        const auto layers = diagnosis(cut(dims_at + 8 + 1000));
        t.check(!layers.first && layers.second.find("layers") != std::string::npos, "layers cut short are named");
        const auto dict = diagnosis(cut(100));
        t.check(!dict.first && dict.second.find("tile dictionary") != std::string::npos, "a cut dictionary is named");
        size_t needed = good.size();      // the shortest prefix that still loads (a file may carry filler after its final word)
        while (needed > 0 && diagnosis(cut(needed - 1)).first) --needed;
        bool every_cut_explained = needed > 42;
        for (size_t n = 0; n < needed && every_cut_explained; n += (n < 200 || n + 400 > needed) ? 1 : 397) {
            const auto d = diagnosis(cut(n));
            if (d.first || d.second.empty()) every_cut_explained = false;
        }
        t.check(every_cut_explained && needed < good.size(), "every truncation before block 4 of a good file fails to load and is explained; a cut inside block 4 or the final word loads, as in the original");
        std::vector<uint8_t> longer = good;
        longer.insert(longer.end(), 206, 0);
        t.check(diagnosis(longer).first, "bytes after the final word are no error (the community editor's filler)");

        // a start marker of a team outside the grid: the file loads, the roster with that team is refused, the others are not
        const size_t rows = static_cast<size_t>(good[dims_at]) | (static_cast<size_t>(good[dims_at + 1]) << 8);
        const size_t columns = static_cast<size_t>(good[dims_at + 4]) | (static_cast<size_t>(good[dims_at + 5]) << 8);
        const size_t records_at = dims_at + 8 + rows * columns * 12 + 2;
        const size_t record_count = static_cast<size_t>(good[records_at - 2]) | (static_cast<size_t>(good[records_at - 1]) << 8);
        size_t red = 0;
        for (size_t i = 0; i < record_count && red == 0; ++i) {
            const size_t at = records_at + i * 6;
            if ((static_cast<size_t>(good[at]) | (static_cast<size_t>(good[at + 1]) << 8)) == 155) red = at;      // RSTART
        }
        t.check(red != 0, "TINY.LVL has a start marker of the red team");
        if (red != 0) {
            v = good;
            v[red + 2] = 200;                    // its row
            v[red + 3] = 0;
            MapInfo info;
            assets::LevelData level;
            std::vector<uint8_t> copy = v;
            const fs::path file = fs::temp_directory_path() / ("ants_map_sweep_marker_" + std::to_string(current_pid()) + ".lvl");
            write_file(file, copy);
            analyze_map(file.string(), info, level);
            std::error_code ignore;
            fs::remove(file, ignore);
            t.check(info.loads && !info.playable && info.blocked_teams == 2u && !info.blocked_reason.empty(), "a red start marker outside the grid: the file loads, a match with the red team is not playable");
            t.check(level.validate(0x09).playable && !level.validate(0x0F).playable && level.validate(0x0F).first_fatal() != nullptr, "the other teams are not affected by it");
        }
    }

    t.section("the sweep of the shipped maps (400 ticks, three plays each)");
    {
        Options o;
        o.maps_dir = maps.string();
        o.ticks = 400;
        o.jobs = 4;
        const std::vector<MapReport> reports = sweep_files(o, exe, files, false);
        t.check(reports.size() == 6 && all_good(reports), "every shipped map loads, runs and is deterministic");
        bool every_run_sound = !reports.empty();
        bool two_team_runs = true;
        for (const MapReport& m : reports) {
            const RunReport* all = find_run(m, kAllTeams);
            const RunReport* two = find_run(m, kGreenAndBlack);
            if (all == nullptr || !all->have_first || !all->have_second || !all->have_rerun || all->first.hash.total != all->second_hash || all->first.hash.total != all->rerun_hash ||
                all->first.ticks_done != 400 || all->mean_tick_us <= 0.0 || all->first.peak_ants < 2 || all->first.orders == 0 || all->first.orders_applied == 0) {
                every_run_sound = false;
                std::printf("    problem with %s\n", m.name.c_str());
            }
            if (m.info.has_team(0) && m.info.has_team(3) && (two == nullptr || two->status != "ok")) two_team_runs = false;
            if (two != nullptr && two->first.peak_ants < 1) two_team_runs = false;
            if (!m.name_check.ok || !m.hashed || m.size == 0) every_run_sound = false;
        }
        t.check(every_run_sound, "each map has three equal hashes, 400 ticks, a tick time, ants and orders that the engine took");
        t.check(two_team_runs, "the two-team roster is played when the map has both hills");
        const Summary s = summarize(reports);
        const std::string json = report_json(o, reports, s);
        t.check(JsonChecker(json).valid(), "the JSON report is valid JSON");
        t.check(json.find(maps.string()) == std::string::npos && json.find("/Users/") == std::string::npos && json.find("\\\\") == std::string::npos, "the JSON report holds no path");
        t.check(s.maps == 6 && s.loads == 6 && s.crashes == 0 && s.nondeterministic == 0 && s.slowest.size() <= 10 && !s.slowest.empty(), "the summary counts what happened");
        const std::vector<MapReport> again = sweep_files(o, exe, files, false);
        bool same = again.size() == reports.size();
        for (size_t i = 0; same && i < again.size(); ++i) {
            const RunReport* a = find_run(again[i], kAllTeams);
            const RunReport* b = find_run(reports[i], kAllTeams);
            same = a != nullptr && b != nullptr && a->first.hash.total == b->first.hash.total && again[i].hash == reports[i].hash;
        }
        t.check(same, "a second sweep gives the same hashes");
    }

    t.section("faults that the sweep must notice");
    {
        const std::vector<fs::path> one = {maps / "TINY.LVL"};
        Options base;
        base.maps_dir = maps.string();
        base.ticks = 200;
        base.jobs = 1;
        base.timeout_s = 4;
        base.slack_s = 2;
        auto first_run = [&](const Options& o) {
            const std::vector<MapReport> r = sweep_files(o, exe, one, false);
            return r.empty() ? MapReport() : r[0];
        };
#ifndef _WIN32
        Options o = base;
        o.fault_text = "abort@100";
        parse_fault(o.fault_text, o.fault);
        MapReport m = first_run(o);
        const RunReport* r = find_run(m, kAllTeams);
        t.check(r != nullptr && r->status == "crash" && r->detail.find("SIGABRT") != std::string::npos && r->detail.find("after tick 100") != std::string::npos, "an abort at tick 100 is a crash by SIGABRT placed at tick 100");
        t.check(map_failed(m) && !all_good({m}), "a crash fails the sweep");
        o.fault_text = "segv@40";
        parse_fault(o.fault_text, o.fault);
        m = first_run(o);
        r = find_run(m, kAllTeams);
        t.check(r != nullptr && r->status == "crash" && r->detail.find("SIGSEGV") != std::string::npos && r->detail.find("after tick 40") != std::string::npos, "a segmentation fault at tick 40 is a crash by SIGSEGV");
        o = base;
        o.timeout_s = 1;
        o.fault_text = "hang@60";
        parse_fault(o.fault_text, o.fault);
        const Clock::time_point t0 = Clock::now();
        m = first_run(o);
        const double waited = std::chrono::duration<double>(Clock::now() - t0).count();
        r = find_run(m, kAllTeams);
        t.check(r != nullptr && r->status == "hang" && r->detail.find("killed") != std::string::npos, "a hang is killed and recorded");
        t.check(waited < 20.0, "the hang costs the timeout and not more");
        o = base;
        o.fault_text = "abortload";
        parse_fault(o.fault_text, o.fault);
        m = first_run(o);
        t.check(!m.info.loads && m.load_child_failure == "crash" && m.runs.empty() && map_failed(m), "a crash while loading is a failure of the map, not of the sweep");
        o.fault_text = "exit3";
        parse_fault(o.fault_text, o.fault);
        m = first_run(o);
        t.check(!m.info.loads && m.info.load_error.find("exit code 3") != std::string::npos, "an exit code is an error");
        o.fault_text = "garbage";
        parse_fault(o.fault_text, o.fault);
        m = first_run(o);
        t.check(!m.info.loads && m.info.load_error.find("no description") != std::string::npos, "output that is not the protocol is an error");
        o = base;
        o.fault_text = "flip@130";
        parse_fault(o.fault_text, o.fault);
        m = first_run(o);
        r = find_run(m, kAllTeams);
        t.check(r != nullptr && !r->deterministic && r->divergence.find("after tick 130") != std::string::npos && r->divergence.find("ants") != std::string::npos, "two processes that differ from tick 130 are found at tick 130 in the ants");
        t.check(r != nullptr && r->status == "ok" && map_failed(m), "a difference fails the sweep though nothing crashed");
        o.fault_text = "leak@57";
        parse_fault(o.fault_text, o.fault);
        m = first_run(o);
        r = find_run(m, kAllTeams);
        t.check(r != nullptr && !r->deterministic && r->have_rerun && r->first.hash.total == r->second_hash && r->first.digest == r->second_digest && r->first.hash.total != r->rerun_hash &&
                    r->divergence.find("after tick 57") != std::string::npos && r->divergence.find("paths") != std::string::npos, "a second play of one process that differs from tick 57 is found at tick 57 in the paths");
#else
        (void)base;
        (void)first_run;
        std::printf("    (the fault checks need child processes: skipped on Windows)\n");
#endif
    }

    t.section("a folder with broken files");
    {
        fs::path dir = fs::temp_directory_path(ec) / ("ants_map_sweep_selftest_" + std::to_string(current_pid()));
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        std::vector<uint8_t> good;
        read_file(maps / "TINY.LVL", good);
        write_file(dir / "good.lvl", good);
        write_file(dir / "too*short.LVL", std::vector<uint8_t>(good.begin(), good.begin() + 1000));       // ('*' cannot travel in a name)
        write_file(dir / "empty.lvl", {});
        write_file(dir / "garbage.lvl", std::vector<uint8_t>(5000, 0xAB));
        write_file(dir / "notamap.txt", good);
        std::vector<uint8_t> wrong_version = good;
        wrong_version[0] = 7;
        write_file(dir / (std::string(70, 'L') + ".lvl"), wrong_version);                                  // a name over the limit
        Options o;
        o.maps_dir = dir.string();
        o.ticks = 100;
        o.jobs = 2;
        std::vector<fs::path> listed;
        t.check(list_maps(o.maps_dir, "", listed, err) && listed.size() == 5, "only the .lvl files are listed (five of six files)");
        const std::vector<MapReport> reports = sweep_files(o, exe, listed, false);
        const Summary s = summarize(reports);
        t.check(s.maps == 5 && s.loads == 1 && s.load_failures == 4 && s.bad_names == 2 && s.names_over_limit == 1, "one map loads, four do not, two names are bad and one of them too long");
        t.check(s.crashes == 0 && s.hangs == 0 && s.nondeterministic == 0, "broken files do not crash, hang or differ");
        t.check(!all_good(reports), "broken files fail the sweep");
        bool explained = true;
        for (const MapReport& m : reports) {
            if (!m.info.loads && m.info.load_error.empty()) explained = false;
        }
        t.check(explained, "every load failure has a reason");
        const std::string json = report_json(o, reports, s);
        t.check(JsonChecker(json).valid() && json.find(dir.string()) == std::string::npos, "the JSON of a broken folder is valid and holds no path");
        std::vector<fs::path> only;
        t.check(list_maps(o.maps_dir, "GOOD", only, err) && only.size() == 1 && only[0].filename() == "good.lvl", "--only filters by name in any case");
        Options skip = o;
        skip.skip_run = true;
        const std::vector<MapReport> loaded = sweep_files(skip, exe, listed, false);
        bool no_runs = true;
        for (const MapReport& m : loaded) no_runs = no_runs && m.runs.empty();
        t.check(no_runs && summarize(loaded).loads == 1, "--skip-run loads and plays nothing");
        fs::remove_all(dir, ec);
    }

    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    std::printf("map_sweep selftest: %d checks, %d failures, %.1f s\n", t.checks, t.failures, seconds);
    return t.failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    Options o;
    std::string err;
    if (!parse_args(args, o, err)) {
        std::fprintf(stderr, "map_sweep: %s\n", err.c_str());
        print_usage(stderr);
        return 2;
    }
    if (o.help) {
        print_usage(stdout);
        return 0;
    }
    if (o.child) return child_main(o, std::cout);
    const std::string exe = self_exe(argv[0]);
    if (o.selftest) return selftest(exe);
    if (o.maps_dir.empty()) {
        print_usage(stderr);
        return 2;
    }
    return run_sweep(o, exe);
}
