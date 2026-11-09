// The self-test of bot_arena (bot_arena --selftest): the checks of the tool itself (determinism, the replay check, the report, the threads, the tool end to end on a temporary folder).
// The tool is in bot_arena.cpp; what these checks call is declared in bot_arena_internal.hpp.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
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
#include "ants_assets/lvl_parser.hpp"
#include "ants_replay/player.hpp"
#include "ants_sim/sim_engine.hpp"
#include "bot_arena_internal.hpp"
#include "bot_arena_tune.hpp"
#include "ants_test_paths.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace fs = std::filesystem;
namespace ai = ants::ai;
namespace sim = ants::sim;
namespace assets = ants::assets;
namespace replay = ants::replay;

namespace ants::arena {
namespace {

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

// The whole report as one string (for the self-test: the same bytes write_report puts in a file)
std::string report_json(const Options& o, const std::vector<LoadedMap>& maps, const std::vector<MatchReport>& reports) {
    std::ostringstream text;
    write_report(text, o, maps, reports);
    return text.str();
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
            {"stalls", [](ai::ArenaResult& r) { ++r.seats[0].stalls; }},
            {"can't-go reactions", [](ai::ArenaResult& r) { ++r.seats[0].cantgo; }},
            {"first can't-go reactions", [](ai::ArenaResult& r) { ++r.seats[1].cantgo_began; }},
            {"orders", [](ai::ArenaResult& r) { ++r.seats[1].orders; }},
            {"refused orders", [](ai::ArenaResult& r) { ++r.seats[0].refused_orders; }},
            {"power-ups taken", [](ai::ArenaResult& r) { ++r.seats[1].took[2]; }},
            {"power-ups taken at a flower", [](ai::ArenaResult& r) { ++r.seats[0].took_at_flowers; }},
            {"expeditions planned", [](ai::ArenaResult& r) { ++r.seats[0].expedition.planned; }},
            {"expeditions given up", [](ai::ArenaResult& r) { ++r.seats[1].expedition.given_up; }},
            {"bombs planted", [](ai::ArenaResult& r) { ++r.seats[0].expedition.planted; }},
            {"hops", [](ai::ArenaResult& r) { ++r.seats[1].expedition.hops; }},
            {"landings of the expedition", [](ai::ArenaResult& r) { ++r.seats[0].expedition.landings; }},
            {"duds", [](ai::ArenaResult& r) { ++r.seats[1].expedition.duds; }},
            {"tokens taken", [](ai::ArenaResult& r) { ++r.seats[0].expedition.taken; }},
            {"Swimmers taken", [](ai::ArenaResult& r) { ++r.seats[1].expedition.swimmers_taken; }},
            {"tick of the first bomb", [](ai::ArenaResult& r) { ++r.seats[0].expedition.first_plant; }},
            {"tick of the first landing", [](ai::ArenaResult& r) { ++r.seats[1].expedition.first_landing; }},
            {"tick of the first Swimmer", [](ai::ArenaResult& r) { ++r.seats[0].expedition.first_swimmer; }},
            {"landings", [](ai::ArenaResult& r) { ++r.landings; }},
            {"the kinds that landed", [](ai::ArenaResult& r) { ++r.landed[5]; }},
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
        // --game-mode 187: the spec carries the mode, the match is another match than the original's, its commands still replay into a fresh engine, and the report names the mode (only a 187 report does)
        {
            Options m = o;
            m.game_mode = sim::GameMode::Kills187;
            m.replay_check = true;
            m.ticks = 300;
            t.check(spec_of(m, tiny, job, false).game_mode == sim::GameMode::Kills187 && spec_of(o, tiny, job, false).game_mode == sim::GameMode::HighestScore, "spec_of carries --game-mode");
            const MatchReport in187 = run_job(m, maps, job, selftest_factory);
            t.check(in187.ok() && in187.replay_checked && in187.replay.ok && in187.result.hash != checked.result.hash, "a 187 match is another match than the original's, and its commands replay into a fresh engine of the same mode");
            const std::vector<MatchReport> one187 = {in187};
            const std::vector<MatchReport> one_original = {checked};
            t.check(report_json(m, maps, one187).find("\"game_mode\": \"187\"") != std::string::npos && report_json(o, maps, one_original).find("game_mode") == std::string::npos,
                    "the report names the mode when it is 187 and says nothing of it for the original's game");
        }
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
    t.check(tree.get("tool") != nullptr && tree.get("tool")->text == "bot_arena" && tree.get("format") != nullptr && tree.get("format")->u64() == 5 && matches != nullptr && matches->items.size() == 1 &&
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
                    m.get("hash") != nullptr && m.get("hash")->text == hex64(played.hash) && m.get("seats") != nullptr && m.get("seats")->items.size() == played.seats.size() &&
                    m.get("landings") != nullptr && m.get("landings")->u64() == played.landings && m.get("landed") != nullptr && m.get("landed")->get("swimmer") != nullptr;
        for (size_t i = 0; fields_ok && i < played.seats.size(); ++i) {
            const JsonValue& s = m.get("seats")->items[i];
            const ai::ArenaSeatResult& r = played.seats[i];
            const auto num = [&s](const char* key) { return s.get(key) != nullptr ? s.get(key)->i64() : int64_t{-12345}; };
            fields_ok = num("seat") == r.spec.seat && num("score") == r.score && num("shown_score") == r.shown_score && num("ants") == r.ants && num("eggs") == r.eggs && num("hatched") == r.hatched &&
                        num("banked") == r.banked && num("raided") == r.raided && num("kills") == r.kills && num("losses") == r.losses && num("stalls") == r.stalls && num("decisions") == r.stats.decisions &&
                        num("orders") == r.orders && num("attack_orders") == r.attack_orders && num("bombs_planted") == r.bombs_planted && num("fires_lit") == r.fires_lit && num("refused_orders") == r.refused_orders && num("cantgo") == r.cantgo && num("cantgo_began") == r.cantgo_began &&
                        num("took_at_flowers") == r.took_at_flowers && s.get("took") != nullptr && s.get("took")->get("fire") != nullptr && s.get("took")->get("fire")->u64() == r.took[2] &&
                        num("released") == r.stats.released && num("intents") == r.stats.intents && num("expired") == r.stats.expired && num("rejected") == r.stats.rejected &&
                        s.get("bot") != nullptr && s.get("bot")->text == spec_text(r.spec) && s.get("runs") != nullptr && s.get("runs")->text == r.runs;
        }
        const JsonValue* replay = m.get("replay");
        fields_ok = fields_ok && replay != nullptr && replay->get("ok") != nullptr && replay->get("ok")->b && replay->get("commands") != nullptr && replay->get("commands")->u64() > 20;
    }
    t.check(fields_ok, "every field of the report equals the match it describes (scores, counts, hash, replay)");
    t.check(played.seats.size() == 3 && played.seats[0].score > 0 && played.seats[0].banked == static_cast<uint32_t>(played.seats[0].score) && played.seats[2].score == 0, "(and the harvesting seats really scored, so the fields above are not all zero)");
    // --save-replays: the match is kept as a replay of the server's kind (the site's list shows it), and the file plays back to the match's own hash
    {
        Options saving = o;
        saving.replay_check = false;
        saving.out = "";
        saving.save_replays = dir.file("kept");
        const ToolRun kept = run_tool_quietly(saving, dir, factory);
        t.check(kept.code == 0 && kept.out.find("1 replay(s) saved") != std::string::npos, "--save-replays: the run passes and says that one replay was saved");
        std::vector<std::string> files;
        std::error_code listing;
        for (const auto& entry : fs::directory_iterator(dir.path / "kept", listing)) files.push_back(entry.path().filename().string());
        t.check(files.size() == 1 && files[0].rfind("ants-TINY-", 0) == 0 && files[0].size() > 8 && files[0].compare(files[0].size() - 8, 8, ".antsrep") == 0,
                "... as one file named like the server's (ants-TINY-<date>-<time>Z.antsrep)");
        replay::Replay back;
        std::string why;
        const std::string bytes = files.size() == 1 ? read_file((dir.path / "kept" / files[0]).string()) : std::string();
        const bool decoded = !bytes.empty() && replay::decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), back, why);
        t.check(decoded && back.head.map_name == "TINY.LVL" && back.head.seed == 1 && back.head.roster == 7 && !back.head.fog && back.head.venue == "bot arena" && replay::plays_here(back.head),
                "... that reads back: the map, the seed, the three seats, no fog of war, and this build's rules");
        t.check(decoded && back.head.names[0] == ai::bot_display_name(o.seats[0]) && back.head.names[1] == ai::bot_display_name(o.seats[1]) && back.head.names[2] == ai::bot_display_name(o.seats[2]),
                "... with the names that the server's rooms show for bots (\"Bot (Hard)\")");
        assets::LevelData level;
        const bool mapped = decoded && replay::load_map(back.head, std::string(ORIGINAL_ASSETS_DIR) + "/Maps", level, why);
        const replay::Outcome outcome = mapped ? replay::play(back, level) : replay::Outcome{};
        t.check(mapped && outcome.ran && outcome.ok && outcome.complete && outcome.hash == played.hash && outcome.turns == played.steps,
                "... and plays back to the very hash and length of the match that the arena played (" + why + outcome.error + ")");
        // the arena's store deletes nothing: an old file of somebody else's (the server's folder) is still there afterwards
        {
            Options shared = saving;
            shared.save_replays = dir.file("shared");
            std::error_code made;
            fs::create_directories(dir.path / "shared", made);
            std::ofstream(dir.path / "shared" / "ants-TINY-20200101-000000Z.antsrep") << "an old file of the server's own";
            const ToolRun shared_run = run_tool_quietly(shared, dir, factory);
            t.check(shared_run.code == 0 && fs::exists(dir.path / "shared" / "ants-TINY-20200101-000000Z.antsrep", made), "a folder with an old file of the server's: the run does not delete it (the arena's store keeps everything)");
        }
        // a match under 30 seconds is not kept (the server keeps none, its list is of matches of 30 seconds or more)
        Options brief = saving;
        brief.ticks = 500;
        brief.save_replays = dir.file("brief");
        const ToolRun brief_run = run_tool_quietly(brief, dir, factory);
        size_t brief_files = 0;
        for (const auto& entry : fs::directory_iterator(dir.path / "brief", listing)) brief_files += entry.path().extension() == ".antsrep" ? size_t{1} : size_t{0};
        t.check(brief_run.code == 0 && brief_files == 0 && brief_run.out.find("0 replay(s) saved") != std::string::npos && brief_run.out.find("ran under 30 seconds") != std::string::npos,
                "a match of 500 turns is not saved (under 30 seconds), and the run says so");
        // a folder that cannot be used is found before the first match: exit 2
        Options blocked = saving;
        blocked.save_replays = dir.file("report.json");                  // (a file, not a folder)
        const ToolRun refused_dir = run_tool_quietly(blocked, dir, factory);
        t.check(refused_dir.code == 2 && refused_dir.err.find("--save-replays") != std::string::npos && refused_dir.out.find("match(es), ") == std::string::npos, "--save-replays on a file: exit 2 and nothing was played");
    }
    // a map with flowers: the landings are counted, and every kind is written under its own name (TINY has no flower, so every value above is 0)
    Options flowers = o;
    flowers.maps = {"SMALL"};
    flowers.ticks = 1500;
    flowers.replay_check = false;
    flowers.out = dir.file("flowers.json");
    const ToolRun flower_run = run_tool_quietly(flowers, dir, factory);
    JsonValue flower_tree;
    const std::string flower_text = read_file(flowers.out);
    bool flowers_ok = flower_run.code == 0 && JsonReader(flower_text).parse(flower_tree) && flower_tree.get("matches") != nullptr && flower_tree.get("matches")->items.size() == 1;
    if (flowers_ok) {
        const std::vector<LoadedMap> flower_maps = load_maps(flowers);
        ai::ArenaSpec flower_spec;
        flower_spec.level = &flower_maps[0].level;
        flower_spec.seed = 1;
        flower_spec.bots = flowers.seats;
        flower_spec.max_ticks = flowers.ticks;
        flower_spec.latency_ticks = flowers.latency;
        flower_spec.factory = factory;
        const ai::ArenaResult flower_played = ai::play_match(flower_spec);
        const JsonValue& m = flower_tree.get("matches")->items[0];
        const JsonValue* landed = m.get("landed");
        uint64_t sum = 0;
        for (size_t k = 1; k < flower_played.landed.size(); ++k) sum += flower_played.landed[k];
        flowers_ok = landed != nullptr && m.get("landings") != nullptr && m.get("landings")->u64() == flower_played.landings && flower_played.landings > 4 && sum == flower_played.landings &&
                     landed->get("bomber") != nullptr && landed->get("bomber")->u64() == flower_played.landed[1] && landed->get("fire") != nullptr && landed->get("fire")->u64() == flower_played.landed[2] &&
                     landed->get("thief") != nullptr && landed->get("thief")->u64() == flower_played.landed[3] && landed->get("combat") != nullptr && landed->get("combat")->u64() == flower_played.landed[4] &&
                     landed->get("swimmer") != nullptr && landed->get("swimmer")->u64() == flower_played.landed[5];
    }
    t.check(flowers_ok, "on SMALL the landings of the flowers are counted, and the report writes every kind under its own name (Bomber 1, Fire 2, Thief 3, Combat 4, Swimmer 5)");
    // a map with water: a Hard bot flies a crew to a row of Swimmers, and the report carries what its expedition did (TINY has none, so every value above is 0)
    Options islands = o;
    islands.maps = {"ISLANDS"};
    islands.seats = {spec_for(0, "standard", ai::Level::Hard), spec_for(2, "idle", ai::Level::Medium)};
    islands.ticks = 2400;
    islands.replay_check = false;
    islands.out = dir.file("islands.json");
    const ToolRun island_run = run_tool_quietly(islands, dir, factory);
    JsonValue island_tree;
    const std::string island_text = read_file(islands.out);
    bool islands_ok = island_run.code == 0 && JsonReader(island_text).parse(island_tree) && island_tree.get("matches") != nullptr && island_tree.get("matches")->items.size() == 1;
    if (islands_ok) {
        const std::vector<LoadedMap> island_maps = load_maps(islands);
        ai::ArenaSpec island_spec;
        island_spec.level = &island_maps[0].level;
        island_spec.seed = 1;
        island_spec.bots = islands.seats;
        island_spec.max_ticks = islands.ticks;
        island_spec.latency_ticks = islands.latency;
        island_spec.factory = factory;
        const ai::ArenaResult island_played = ai::play_match(island_spec);
        const JsonValue* seats = island_tree.get("matches")->items[0].get("seats");
        islands_ok = seats != nullptr && seats->items.size() == 2 && island_played.seats.size() == 2;
        for (size_t i = 0; islands_ok && i < 2; ++i) {
            const JsonValue* e = seats->items[i].get("expedition");
            const ai::ExpeditionResult& want = island_played.seats[i].expedition;
            const auto num = [e](const char* key) { return e != nullptr && e->get(key) != nullptr ? e->get(key)->u64() : uint64_t{12345678}; };
            islands_ok = e != nullptr && num("planned") == want.planned && num("given_up") == want.given_up && num("planted") == want.planted && num("hops") == want.hops && num("landings") == want.landings &&
                         num("duds") == want.duds && num("taken") == want.taken && num("swimmers_taken") == want.swimmers_taken && num("first_plant") == want.first_plant &&
                         num("first_landing") == want.first_landing && num("first_swimmer") == want.first_swimmer;
        }
        const ai::ExpeditionResult& flown = island_played.seats[0].expedition;
        islands_ok = islands_ok && flown.planned >= 1 && flown.planted >= 1 && flown.hops >= 1 && flown.landings >= 1 && flown.swimmers_taken >= 1 && flown.first_plant > 0 && flown.first_plant < flown.first_landing &&
                     flown.first_landing < flown.first_swimmer && island_played.seats[1].expedition == ai::ExpeditionResult{};
    }
    t.check(islands_ok, "on ISLANDS the report carries the expedition of a standard bot, every counter and tick as the match has it, and an idle seat has none");
    t.check(island_run.out.find("expedition, seat 0: 1 match(es), first bomb at tick") != std::string::npos && island_run.out.find("expedition, seat 2") == std::string::npos,
            "and the run's summary says what the expedition of seat 0 did, and nothing of the seat that made none");
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

}  // namespace

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
        Options gm;
        t.check(d.game_mode == sim::GameMode::HighestScore && parse_args({"--game-mode", "187"}, gm, err) && gm.game_mode == sim::GameMode::Kills187 &&
                    parse_args({"--game-mode", "highest-score"}, gm, err) && gm.game_mode == sim::GameMode::HighestScore,
                "--game-mode: the original's game by default, 187 and highest-score by their words");
        t.check(!parse_args({"--game-mode", "188"}, bad, err) && !parse_args({"--game-mode", "1"}, bad, err) && !parse_args({"--game-mode"}, bad, err), "--game-mode: no number, no other word, a value is needed");
        t.check(!parse_args({"--seat", "0=idle", "--seat", "0=hard"}, bad, err) && !parse_args({"--ticks", "0"}, bad, err) && !parse_args({"--wat"}, bad, err) &&
                    !parse_args({"--threads", "0"}, bad, err) && !parse_args({"--map"}, bad, err),
                "two bots on one seat, 0 ticks, an unknown option, 0 threads and a missing value are refused");
        Options out_given;
        t.check(parse_args({"--out", "report.json"}, out_given, err) && out_given.out == "report.json" && !parse_args({"--out", ""}, bad, err) && !parse_args({"--out"}, bad, err),
                "--out takes a file name; an empty one (an unset shell variable) is refused, not taken for 'no report'");
        ai::LevelPlan probe = ai::plan_for(ai::Level::Hard);
        std::string tune_err;
        t.check(apply_tune(probe, "stall", 500, tune_err) && probe.stall_ticks == 500 && apply_tune(probe, "repeat", 5, tune_err) && probe.repeat_limit == 5 && apply_tune(probe, "repwindow", 600, tune_err) &&
                    probe.repeat_window == 600 && apply_tune(probe, "fallback", 700, tune_err) && probe.fallback_ticks == 700 && apply_tune(probe, "gatefails", 4, tune_err) && probe.gate_user_fails == 4 &&
                    !apply_tune(probe, "stal", 1, tune_err),
                "the tuning keys of the stall detector and of the gate's pause (stall, repeat, repwindow, fallback, gatefails) set the plan; a misspelt key is refused");
        ai::LevelPlan cg = ai::plan_for(ai::Level::Hard);
        t.check(cg.cantgo_aware && apply_tune(cg, "cg", 0, tune_err) && !cg.cantgo_aware && apply_tune(cg, "cg", 1, tune_err) && cg.cantgo_aware, "the key cg switches the can't-go fixes of the plan (on by default)");
        ai::LevelPlan grp = ai::plan_for(ai::Level::Hard);
        ai::LevelPlan grp_medium = ai::plan_for(ai::Level::Medium);
        const int32_t grp_leash = grp.leash_tiles;
        t.check(!grp.group_fight && apply_tune(grp, "group", 1, tune_err) && grp.group_fight && grp.group_reach == 20 && apply_tune(grp, "group", 0, tune_err) && !grp.group_fight && apply_tune(grp, "greach", 16, tune_err) &&
                    grp.group_reach == 16 && grp.leash_tiles == grp_leash && !grp_medium.group_fight && apply_tune(grp_medium, "group", 1, tune_err) && grp_medium.group_fight && grp_medium.group_reach == 14 &&
                    !apply_tune(grp, "gkeep", 40, tune_err),
                "the keys group and greach switch the group batch of the fights (off at every level) and set the reach of its ants (20 tiles at Hard, 14 at Medium, which also needs defence=1; the leash stays); a misspelt key is refused");
        t.check(cg.raid_min_free == 1u && apply_tune(cg, "raidfree", 2, tune_err) && cg.raid_min_free == 2u && apply_tune(cg, "raidfree", 1, tune_err) && cg.raid_min_free == 1u &&
                    !apply_tune(cg, "raidfree", 0, tune_err) && !apply_tune(cg, "raidfree", 4, tune_err) && cg.raid_min_free == 1u,
                "the key raidfree sets the free tiles in front of a hole that a raid needs (1 by default, 1 to 3 allowed)");
        t.check(cg.gate_leaver_ticks == 60u && apply_tune(cg, "gatehold", 0, tune_err) && cg.gate_leaver_ticks == 0u && apply_tune(cg, "gatehold", 7, tune_err) && cg.gate_leaver_ticks == 7u &&
                    !apply_tune(cg, "gatehold", -1, tune_err) && !apply_tune(cg, "gatehold", 1001, tune_err) && cg.gate_leaver_ticks == 7u,
                "the key gatehold sets the ticks that the gate's click waits for an ant that leaves over the ramp (60 at Hard, 0: no wait, 0 to 1000 allowed)");
        t.check(cg.raid_unjam && apply_tune(cg, "unjam", 0, tune_err) && !cg.raid_unjam && apply_tune(cg, "unjam", 1, tune_err) && cg.raid_unjam,
                "the key unjam switches the step aside of a thief's own waiting thief of the plan (two thieves on one hole; on by default)");
        t.check(cg.island_fly_on && ai::plan_for(ai::Level::Medium).island_fly_on && !ai::plan_for(ai::Level::Easy).island_fly_on && apply_tune(cg, "ifly", 0, tune_err) && !cg.island_fly_on &&
                    apply_tune(cg, "ifly", 1, tune_err) && cg.island_fly_on,
                "the key ifly switches the flying on of the expedition's Bomber (on at Medium and Hard, off at Easy)");
        t.check(cg.island_timed_row && !ai::plan_for(ai::Level::Medium).island_timed_row && !ai::plan_for(ai::Level::Easy).island_timed_row && apply_tune(cg, "itimed", 0, tune_err) && !cg.island_timed_row &&
                    apply_tune(cg, "itimed", 1, tune_err) && cg.island_timed_row,
                "the key itimed switches the timed clicks through the hats of the rows of ISLANDS (on at Hard only)");
        ai::LevelPlan fl = ai::plan_for(ai::Level::Hard);
        const auto rules = [&fl]() {                                                                     // the five flower rules of the plan, as five digits: sides, fire, recover, recall, watch
            const bool on[5] = {fl.flower_sides, fl.flower_fire, fl.flower_recover, fl.flower_recall, fl.flower_watch};
            std::string digits;
            for (const bool b : on) digits += b ? '1' : '0';
            return digits;
        };
        const char* const flower_keys[5] = {"fside", "ffire", "frecover", "frecall", "fwatch"};
        const char* const flower_alone[5] = {"10000", "01000", "00100", "00010", "00001"};
        bool flower_ok = rules() == "11111" && apply_tune(fl, "flowers", 0, tune_err) && rules() == "00000";
        for (int k = 0; k < 5; ++k) flower_ok = flower_ok && apply_tune(fl, flower_keys[k], 1, tune_err) && rules() == flower_alone[k] && apply_tune(fl, flower_keys[k], 0, tune_err) && rules() == "00000";
        t.check(flower_ok && apply_tune(fl, "flowers", 1, tune_err) && rules() == "11111", "the key flowers switches the five flower rules of the plan together (Hard has all five) and fside, ffire, frecover, frecall and fwatch one each");
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

}  // namespace ants::arena
