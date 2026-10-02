// Tests of the worker bot (B3, AI3.1 .. AI3.15): the economy of the computer players. Run as suite 2.22 (test_ai_worker): matches of the whole length, so they are kept apart from
// the quick suite of the plumbing (2.20).
//
//   AI3.1   margins over the idle bot after two minutes on the five maps with food on foot, every seat and level; the idle bots stay at 0; ISLANDS: nothing to walk to, nothing done
//   AI3.2   the economy needs well under one command per second; the first command of a level leaves no sooner than its reaction time
//   AI3.3   a pile is empty: the idle ants are sent on to the next (also the ants that stand where a big pile used to be)
//   AI3.4   a wall closes after the analysis was made: the bot learns from the tick the order LEFT, blacklists the pile for 900 ticks and uses the other
//   AI3.5   a carrier is never ordered onto a pile; a stuck one (idle, far from the hill) is sent home
//   AI3.6   never a Hatch
//   AI3.7   an invitation is declined, once
//   AI3.8   the endgame veto
//   AI3.9   the levels are not worse than each other
//   AI3.10  the budget is never exceeded over a whole TREASURE match
//   AI3.11  four workers share the whole pot, and do nothing that a person could not do
//   AI3.12  the pinned numbers (baselines.inc), within +-8 percent
//   AI3.13  the task model: the ledger, tasks that hold ants, orders that never left, stranded ants
//   AI3.14  determinism and replay: a worker match is bit-reproducible and nothing but its commands; standard is the worker until B4
//   AI3.15  what the level changes in the economy: Easy sends the nearest pile first and at most 4 ants to a pile, Medium and Hard rank by points per trip; the per-pile cap
//   AI3.16  whose fault a failed order is: an ant that is shut in is kept away from the pile, not the pile from everybody; a lone ant (nobody works anywhere) blames the pile; a
//           "can't go" mid-route and an ant that moved are no failure of the pile
//   AI3.17  the map as it is now: a pile that was shut off at the start (a wall that opens, food that plugs a corridor) and an ant that was shut in are asked again
//   AI3.18  packed ants: 24 workers on adjacent tiles do not jam the engine's path manager (ants per look), and a jam that comes anyway is repaired (the watchdog)
//   AI3.19  the deep queue: with many workers on one hill the rescue leaves the queue alone (no rescue of an ant that waits its turn)
//   AI3.20  the watchdog never stops a carrier on its way home
//   AI3.21  the watchdog halves the ants ordered at one look after a jam; the default task orders 8 at a look
//
// W_ONLY=AI3.4 (or AI3.4,AI3.7) runs only the tests with exactly that number, W_SKIP=AI3.9,AI3.12 leaves those out; a filter that leaves no test makes the program fail.
#include "ai_test.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "ants_ai/arena.hpp"
#include "ants_ai/baselines.hpp"
#include "ants_ai/idle_bot.hpp"
#include "ants_ai/tasks.hpp"
#include "ants_ai/worker_bot.hpp"

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

namespace {

// ---- the pinned table (tests/test_ai/baselines.inc: written by `bot_arena --write-baselines`) -------------------------------------------------------------------------------

struct PinnedRow {
    const char* map;
    Level level;
    int32_t solo_2min[4];
    int32_t solo_full[4];
    int32_t four_full[4];
    int32_t four_sum;
    int32_t pot;
};

const PinnedRow kPinned[] = {
#include "baselines.inc"
};

constexpr const char* kLandMaps[] = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE"};
constexpr const char* kShippedMaps[] = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"};
constexpr Level kLevels[] = {Level::Easy, Level::Medium, Level::Hard};

// The margin a worker must have over the idle bot after two minutes (the design's: about 80 percent of the worst seat of the Medium bot)
int32_t margin_of(const std::string& map) {
    if (map == "TINY") return 450;
    if (map == "SMALL") return 420;
    if (map == "MEDIUM") return 240;
    if (map == "GAUNTLET") return 80;
    if (map == "TREASURE") return 350;
    return 0;
}

// The reachable points of the six maps (pinned by AI1.15) and the eggs of a hill
int32_t pot_of(const std::string& map) {
    if (map == "TINY") return 4800;
    if (map == "SMALL") return 3000;
    if (map == "MEDIUM") return 4900;
    if (map == "GAUNTLET") return 1500;
    if (map == "TREASURE") return 8850;
    return 0;
}

uint32_t eggs_of(const std::string& map) {
    if (map == "TINY") return 3;
    if (map == "SMALL") return 2;
    if (map == "MEDIUM" || map == "GAUNTLET") return 6;
    if (map == "ISLANDS") return 4;
    return 9;
}

BotSpec spec_of(uint8_t seat, const char* kind, Level level) {
    BotSpec s;
    s.seat = seat;
    s.kind = kind;
    s.level = level;
    return s;
}

// A match of the arena: `kind` (the worker) of `level` on the seats of `workers` (bit s = seat s), idle bots on the others, all four seats in play
ArenaSpec match_of(const std::string& map, uint32_t seed, uint8_t workers, Level level, uint64_t ticks, uint32_t latency = 3, bool record = false, const char* kind = "worker") {
    ArenaSpec s;
    s.level = &level_of(map);
    s.seed = seed;
    s.max_ticks = ticks;
    s.latency_ticks = latency;
    s.record = record;
    for (uint8_t seat = 0; seat < 4; ++seat) s.bots.push_back(spec_of(seat, ((workers >> seat) & 1u) != 0 ? kind : "idle", level));
    return s;
}

// ---- a small world and a small driver ---------------------------------------------------------------------------------------------------------------------------------

constexpr TileCoord kHills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};

// Four hills on a 60 x 60 field of grass, `ants` workers in front of each (of seat 0 only with `only_seat0`); the match lasts `ticks`. Nobody but the test gives orders.
void world(sim::SimulationEngine& sim, uint32_t ticks = 14400, uint32_t seed = 1, uint32_t ants = 3, bool only_seat0 = false) {
    sim.init_test_world(60, 60, seed, ticks * sim::TICK_MS);
    for (uint8_t p = 0; p < 4; ++p) sim.set_anthill(p, kHills[p]);
    for (uint8_t p = 0; p < 4; ++p) {
        if (only_seat0 && p != 0) continue;
        const int32_t sx = kHills[p].x < 30 ? 1 : -1;
        const int32_t sy = kHills[p].y < 30 ? 1 : -1;
        for (uint32_t i = 0; i < ants; ++i) {
            sim.spawn_unit(p, sim::AntType::Worker, TileCoord{kHills[p].x + sx * (1 + static_cast<int32_t>(i % 8)), kHills[p].y + sy * (6 + static_cast<int32_t>(i / 8))});
        }
    }
}

// A pile of `units` units of `value` points: the 2 x 2 crackers picture (tile 369) that shrinks through the stages
int32_t pile(sim::SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value) {
    return place_pile(sim, col, row, units, value,
                      {{units, 369}, {static_cast<uint16_t>(units * 3 / 4), 370}, {static_cast<uint16_t>(units / 2), 371}, {static_cast<uint16_t>(units / 4), 372}, {0, kPileGone}});
}

// A pile that starts as a 4 x 4 picture (tile 366) and shrinks to the 2 x 2 crackers at its middle: the ants that work it end up on tiles that the analysis of the start took for
// solid and that have no free neighbour (the situation of TREASURE's biggest pile)
int32_t big_pile(sim::SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value) {
    return place_pile(sim, col, row, units, value, {{units, 366}, {static_cast<uint16_t>(units / 2), 369}, {0, kPileGone}});
}

void step_all(sim::SimulationEngine& sim, BotController& c, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        sim.tick();
        c.on_tick(sim);
        sim.clear_news_events();
        sim.clear_audio_events();
    }
}

const sim::AntSnapshot* snap(const sim::SimulationEngine& sim, uint32_t id) {
    for (const auto& a : sim.get_world_state().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

bool holds(const sim::SimulationEngine& sim, uint32_t id) {
    const sim::AntSnapshot* a = snap(sim, id);
    return a != nullptr && a->is_holding;
}

uint16_t units_left(const sim::SimulationEngine& sim, int32_t pile_index) { return sim.grid().food_objects()[static_cast<size_t>(pile_index)].remaining; }

int32_t score_of(const sim::SimulationEngine& sim, uint8_t seat) { return sim.get_player_score(seat); }

const WorkerBot* worker_of(BotController& c, uint8_t seat) { return dynamic_cast<const WorkerBot*>(c.bot(seat)); }

// The sink of a local game that looks at what it is given (the engine's state at the moment of the release) and applies it
class GuardSink final : public sim::CommandSink {
public:
    explicit GuardSink(sim::SimulationEngine& sim) : sim_(sim) {}
    sim::CommandResult submit(const Command& c) override {
        ++by_type[static_cast<size_t>(c.type)];
        log.emplace_back(sim_.current_tick(), c);
        if (c.type == CommandType::GroupMove) {
            const assets::AnthillSpawn* hill = sim_.grid().find_anthill(c.issuer);
            const bool to_hill = hill != nullptr && c.tile_x == hill->x + 1 && c.tile_y == hill->y + 1;
            for (const uint32_t id : c.ants) {
                const sim::AntSnapshot* a = snap(sim_, id);
                if (a == nullptr || !a->is_holding) continue;
                if (to_hill) ++carriers_sent_home;
                else ++carriers_sent_to_piles;
            }
        }
        return sim_.apply_command(c);
    }
    size_t count(CommandType t) const { return by_type[static_cast<size_t>(t)]; }
    std::array<size_t, 16> by_type{};
    std::vector<std::pair<uint64_t, Command>> log;
    size_t carriers_sent_home{0};
    size_t carriers_sent_to_piles{0};

private:
    sim::SimulationEngine& sim_;
};

// A whole match on a shipped map, played by hand with workers of `level` on the seats of `workers` and idle bots elsewhere, commands applied at once, to the end of the clock
struct HandMatch {
    sim::SimulationEngine sim;
    std::unique_ptr<GuardSink> sink;
    std::unique_ptr<BotController> ctl;
    uint64_t ticks{0};
    HandMatch(const std::string& map, uint32_t seed, uint8_t workers, Level level) {
        sim.init(level_of(map), seed, 0x0F);
        sink = std::make_unique<GuardSink>(sim);
        ctl = std::make_unique<BotController>(sim, seed);
        for (uint8_t seat = 0; seat < 4; ++seat) {
            std::string why;
            if (!ctl->add(spec_of(seat, ((workers >> seat) & 1u) != 0 ? "worker" : "idle", level), *sink, why)) std::cout << "    add failed: " << why << "\n";
        }
    }
    void play() {
        for (uint64_t limit = 0; limit < 20000 && !sim.is_match_over(); ++limit) {
            sim.tick();
            ++ticks;
            ctl->on_tick(sim);
            sim.clear_news_events();
            sim.clear_audio_events();
        }
    }
};

// A bot and its controller by hand: what the controller does between a bot and the engine, in the simplest form, so that a test can choose any reaction time and decide what
// becomes of every command (it leaves `delay` ticks after the look, or it is dropped as Expired). No budget, no filter: Orders and Bot only.
class Driver {
public:
    Driver(sim::SimulationEngine& sim, uint8_t seat, const Profile& profile, std::unique_ptr<Bot> bot) : sim_(sim), seat_(seat), profile_(profile), map_(sim), bot_(std::move(bot)) {
        bot_->start(BotContext{seat, profile, 1, &map_});
    }
    // one tick of the engine, then what is ready leaves, then (when due) a look
    void tick() {
        sim_.tick();
        sim_.clear_news_events();
        sim_.clear_audio_events();
        const uint64_t now = sim_.current_tick();
        for (size_t i = 0; i < waiting_.size();) {
            if (waiting_[i].release > now) {
                ++i;
                continue;
            }
            Command c = waiting_[i].command;
            waiting_.erase(waiting_.begin() + static_cast<std::ptrdiff_t>(i));
            c.issuer = seat_;
            if (drop_) {
                bot_->on_command(c, Bot::Fate::Expired, now);
            } else {
                sim_.apply_command(c);
                sent.emplace_back(now, c);
                bot_->on_command(c, Bot::Fate::Sent, now);
            }
        }
        if (now >= next_look_) {
            next_look_ = now + std::max<uint32_t>(1u, profile_.decision_interval);
            look();
        }
    }
    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) tick();
    }
    void look() {
        const BotView view = BotView::build(sim_, seat_, &map_);
        Orders orders;
        bot_->think(view, orders);
        decided.push_back(sim_.current_tick());
        for (const Intent& in : orders.intents()) waiting_.push_back(Waiting{sim_.current_tick() + profile_.reaction_delay, in.command});
    }
    /// A new bot takes the seat in the middle of the match: it knows nothing but the world
    void restart(std::unique_ptr<Bot> bot) {
        bot_ = std::move(bot);
        bot_->start(BotContext{seat_, profile_, 1, &map_});
    }
    Bot& bot() { return *bot_; }
    const WorkerBot* worker() const { return dynamic_cast<const WorkerBot*>(bot_.get()); }
    const MapInfo& map() const { return map_; }
    void set_drop(bool drop) { drop_ = drop; }
    std::vector<std::pair<uint64_t, Command>> sent;      // (tick it left, command)
    std::vector<uint64_t> decided;                       // the ticks of the looks

private:
    struct Waiting {
        uint64_t release;
        Command command;
    };
    sim::SimulationEngine& sim_;
    uint8_t seat_;
    Profile profile_;
    MapInfo map_;
    std::unique_ptr<Bot> bot_;
    std::vector<Waiting> waiting_;
    uint64_t next_look_{1};
    bool drop_{false};
};

Profile profile_with(Level level, uint32_t interval, uint32_t delay) {
    Profile p = profile_for(level);
    p.decision_interval = interval;
    p.reaction_delay = delay;
    p.jitter_percent = 0;
    return p;
}

size_t group_moves_to(const std::vector<std::pair<uint64_t, Command>>& log, TileCoord tile, uint64_t from = 0, uint64_t to = ~uint64_t{0}) {
    size_t n = 0;
    for (const auto& e : log) {
        if (e.second.type == CommandType::GroupMove && e.second.tile_x == tile.x && e.second.tile_y == tile.y && e.first >= from && e.first <= to) ++n;
    }
    return n;
}

bool names(const Command& c, uint32_t ant) { return std::find(c.ants.begin(), c.ants.end(), ant) != c.ants.end(); }

// ---- the pinned procedure for the 18 rows, played once for the whole suite on a few threads (every match is independent: the numbers do not depend on the number of threads) ----

const std::vector<BaselineRow>& measured_rows() {
    static std::vector<BaselineRow> rows;
    static std::once_flag once;
    std::call_once(once, [] {
        const size_t count = sizeof(kPinned) / sizeof(kPinned[0]);
        rows.resize(count);
        for (const PinnedRow& r : kPinned) (void)level_of(r.map);                // the maps are loaded before the threads start (the cache is not thread-safe)
        std::mutex lock;
        size_t next = 0;
        const auto work = [&]() {
            for (;;) {
                size_t i;
                {
                    std::lock_guard<std::mutex> g(lock);
                    if (next >= count) return;
                    i = next++;
                }
                rows[i] = measure_baseline(level_of(kPinned[i].map), kPinned[i].map, kPinned[i].level);
            }
        };
        const unsigned threads = std::max(1u, std::min(6u, std::thread::hardware_concurrency()));
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
        work();
        for (std::thread& t : pool) t.join();
    });
    return rows;
}

const BaselineRow* measured(const std::string& map, Level level) {
    const std::vector<BaselineRow>& rows = measured_rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (map == kPinned[i].map && level == kPinned[i].level) return &rows[i];
    }
    return nullptr;
}

// Within the band of a pinned number: 8 percent, and never less than one bite of 15 points (scores move in steps of 5 to 50); 0 stays 0 (ISLANDS is exempt: 0 = 0)
bool in_band(int32_t value, int32_t pinned_value) {
    if (pinned_value == 0) return value == 0;
    return std::abs(value - pinned_value) <= std::max(pinned_value * 8 / 100, 15);
}

int32_t mean4(const std::array<int32_t, 4>& v) { return (v[0] + v[1] + v[2] + v[3] + 2) / 4; }

// For the work on single tests: W_ONLY=AI3.4 (or AI3.4,AI3.7) runs the tests with exactly that number and no other (all of them without); W_SKIP=AI3.9,AI3.12 leaves those out (the
// sanitizer run of run_tests.sh does: the pinned table is 80 percent of the run time and checks numbers, not memory). The program fails when nothing ran.
bool listed(const char* variable, const std::string& id) {
    const char* value = std::getenv(variable);
    if (value == nullptr || *value == 0) return false;
    const std::string list = std::string(value) + ",";
    size_t from = 0;
    for (size_t comma = list.find(','); comma != std::string::npos; from = comma + 1, comma = list.find(',', from)) {
        if (list.substr(from, comma - from) == id) return true;
    }
    return false;
}

bool wanted(const char* name) {
    const std::string text(name);
    const std::string id = text.substr(0, text.find(' '));
    const char* only = std::getenv("W_ONLY");
    if (only != nullptr && *only != 0 && !listed("W_ONLY", id)) return false;
    return !listed("W_SKIP", id);
}

#define WORKER_TEST(name) if (wanted(name)) TEST_CASE(name)

// A match of four workers of `level` on a shipped map (seed 3: not one of the seeds of the pinned table), played once for the whole suite
const ArenaResult& four_workers(const std::string& map, Level level) {
    static std::map<std::pair<std::string, int>, ArenaResult> cache;
    const auto key = std::make_pair(map, static_cast<int>(level));
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, play_match(match_of(map, 3, 0x0F, level, 0, 3, true))).first;
    return it->second;
}

// The worker alone on seat 0 against three idle bots, the whole match, once for the whole suite
const ArenaResult& alone_full(const std::string& map, Level level) {
    static std::map<std::pair<std::string, int>, ArenaResult> cache;
    const auto key = std::make_pair(map, static_cast<int>(level));
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, play_match(match_of(map, 1, 0x01, level, 0, 3, true))).first;
    return it->second;
}

// How many ticks the budget of a level allows between the first and the last of the releases at `ticks` (the AI2.3 bound: the bucket, plus what the rate refills in the window)
bool within_budget(const std::vector<uint64_t>& ticks, const Profile& p) {
    for (size_t i = 0; i < ticks.size(); ++i) {
        for (size_t j = i; j < ticks.size(); ++j) {
            const uint64_t len = ticks[j] - ticks[i] + 1;
            const uint64_t n = j - i + 1;
            if (n * 1000u > static_cast<uint64_t>(p.burst) * 1000u + (len * p.rate_milli_cps + 19u) / 20u) return false;
        }
    }
    return true;
}

int32_t chebyshev(TileCoord a, TileCoord b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

// The economy with parameters of its own: what the tests of the guards need (a number of ants per look that is out of the ordinary, a short clock)
class ParamBot final : public Bot {
public:
    explicit ParamBot(const HarvestTask::Params& params) : task_(1, params) {}
    const char* kind() const noexcept override { return "param"; }
    void start(const BotContext& c) override {
        seat_ = c.seat;
        profile_ = c.profile;
        map_ = c.map;
    }
    void think(const BotView& v, Orders& o) override {
        const MapInfo* map = v.map() != nullptr ? v.map() : map_;
        if (map == nullptr) return;
        ledger_.forget_missing(v.mine());
        TaskContext context{v, o, ledger_, profile_, *map, seat_};
        task_.step(context);
    }
    void on_command(const Command& c, Fate fate, uint64_t tick) override { task_.on_command(c, fate, tick); }
    const HarvestTask& task() const noexcept { return task_; }

private:
    uint8_t seat_{0};
    Profile profile_{};
    const MapInfo* map_{nullptr};
    AntLedger ledger_;
    HarvestTask task_;
};

// The sink of a room: what the controller releases is applied `latency` ticks later, at the first turn boundary (an even tick), in canonical order; the test calls flush() after every tick
class LatencySink final : public sim::CommandSink {
public:
    LatencySink(sim::SimulationEngine& sim, uint32_t latency) : sim_(sim), latency_(latency) {}
    sim::CommandResult submit(const Command& c) override {
        log.emplace_back(sim_.current_tick(), c);
        waiting_.emplace_back(sim_.current_tick() + latency_, c);
        sim::CommandResult r;
        r.status = sim::CommandResult::Status::Applied;
        return r;
    }
    void flush() {
        const uint64_t now = sim_.current_tick();
        if (now % 2 != 0 || waiting_.empty()) return;
        std::vector<Command> due;
        std::vector<std::pair<uint64_t, Command>> later;
        for (auto& w : waiting_) {
            if (w.first <= now) due.push_back(w.second);
            else later.push_back(w);
        }
        waiting_ = std::move(later);
        sim::canonical_order(due);
        for (const Command& c : due) {
            const sim::CommandResult r = sim_.apply_command(c);
            if (r.status != sim::CommandResult::Status::Applied) ++not_applied;
        }
    }
    size_t count(CommandType t) const {
        size_t n = 0;
        for (const auto& e : log) n += e.second.type == t ? 1u : 0u;
        return n;
    }
    std::vector<std::pair<uint64_t, Command>> log;      // (tick of the release, command)
    size_t not_applied{0};

private:
    sim::SimulationEngine& sim_;
    uint32_t latency_;
    std::vector<std::pair<uint64_t, Command>> waiting_;
};

void step_late(sim::SimulationEngine& sim, BotController& c, LatencySink& sink, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        sim.tick();
        sink.flush();
        c.on_tick(sim);
        sim.clear_news_events();
        sim.clear_audio_events();
    }
}

// A ring of obstacles with a closed inside: a tile that nobody can walk to (an order to it ends in "Can't go there.") or a place that shuts ants in
void ring(sim::SimulationEngine& sim, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t door_x = -1, int32_t door_y = -1) {
    for (int32_t x = x0; x <= x1; ++x) {
        for (int32_t y = y0; y <= y1; ++y) {
            if ((x == x0 || x == x1 || y == y0 || y == y1) && !(x == door_x && y == door_y)) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
        }
    }
}

}  // namespace

void run_worker_tests() {
    for (const char* map : kShippedMaps) (void)level_of(map);                    // the cache of maps may not grow while references into it are in use

    WORKER_TEST("AI3.1 The Worker Beats The Idle Bot By A Margin: Two Minutes On The Five Maps With Food On Foot, Every Seat, Every Level; The Idle Opponents Stay At 0 And Send Nothing; ISLANDS: Nothing To Walk To, Nothing Is Done") {
        for (const char* map : kLandMaps) {
            for (const Level level : kLevels) {
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    const ArenaResult r = play_match(match_of(map, 1, static_cast<uint8_t>(1u << seat), level, 2400));
                    ASSERT_TRUE(r.error.empty() && r.ticks == 2400 && r.seats.size() == 4);
                    ASSERT_TRUE(r.seats[seat].score >= margin_of(map));
                    ASSERT_EQ(r.seats[seat].runs, "worker");
                    ASSERT_TRUE(r.seats[seat].stats.released >= 1u);
                    for (uint8_t other = 0; other < 4; ++other) {
                        if (other == seat) continue;
                        ASSERT_TRUE(r.seats[other].score == 0 && r.seats[other].food_deposited == 0 && r.seats[other].stats.released == 0 && r.seats[other].runs == "idle");
                    }
                }
            }
        }
        for (const Level level : kLevels) {
            for (uint8_t seat = 0; seat < 4; ++seat) {
                const ArenaResult r = play_match(match_of("ISLANDS", 1, static_cast<uint8_t>(1u << seat), level, 2400, 3, true));
                ASSERT_TRUE(r.error.empty() && r.ticks == 2400);
                ASSERT_TRUE(r.seats[seat].score == 0 && r.seats[seat].stats.released == 0 && r.seats[seat].stats.intents == 0 && r.log.empty());      // the exemption: no pile is on foot from any hill
                ASSERT_TRUE(r.seats[seat].stats.decisions > 0);                                                                                       // it did look
            }
        }
    } TEST_END();

    WORKER_TEST("AI3.2 The Economy Needs Well Under One Command Per Second At Every Level On Every Map (One Group Move Per Pile Starts A Loop That Runs By Itself); The First Command Leaves No Sooner Than The Reaction Time; Nothing Is Refused, Expired Or Lost") {
        for (const char* map : kLandMaps) {
            for (const Level level : kLevels) {
                const ArenaResult& r = alone_full(map, level);
                ASSERT_TRUE(r.error.empty() && r.match_over && !r.log.empty());
                const ArenaSeatResult& s = r.seats[0];
                const Profile p = profile_for(level);
                ASSERT_TRUE(s.stats.released >= 1u && s.stats.released == r.log.size());
                ASSERT_TRUE(s.milli_commands_per_second(r.ticks) < 150u);                      // the design measured 2 to 83 thousandths of a command per second; Easy's budget alone is 400
                ASSERT_TRUE(s.stats.rejected == 0 && s.stats.filtered == 0 && s.stats.expired == 0 && s.stats.superseded == 0);
                ASSERT_TRUE(r.log.front().tick >= p.reaction_delay * 3 / 4);                   // 75 percent of the reaction time is the least a command waits
                size_t early = 0;
                for (const RecordedCommand& c : r.log) early += c.tick <= 600 ? 1u : 0u;
                ASSERT_TRUE(early >= 1 && early <= 3);                                         // the first look sends the start ants to one or two piles
                ASSERT_EQ(s.hatched, 0u);
            }
        }
    } TEST_END();

    WORKER_TEST("AI3.3 A Pile Is Empty: Every Ant That Stands Idle At It Is Sent On To The Next Pile Within A Look And A Reaction Time, At Every Level; Also The Ants That Stand On The Tiles Where A Big Pile Used To Be, Which The Analysis Of The Start Took For Solid") {
        for (const bool footprint : {false, true}) {
            for (const Level level : kLevels) {
                sim::SimulationEngine sim;
                world(sim);
                const int32_t first = footprint ? big_pile(sim, 14, 14, 6, 50) : pile(sim, 12, 12, 4, 25);
                const int32_t next = pile(sim, 28, 28, 40, 25);
                RecordingSink sink(sim, true);
                BotController c(sim, 1);
                std::string why;
                ASSERT_TRUE(c.add(spec_of(0, "worker", level), sink, why));
                const TileCoord anchor = footprint ? TileCoord{14, 14} : TileCoord{12, 12};
                const Profile p = profile_for(level);
                const std::vector<uint32_t> mine = ants_of(sim, 0);
                ASSERT_EQ(mine.size(), 3u);
                uint64_t emptied = 0;
                bool on_unknown_tile = false;
                std::map<uint32_t, uint64_t> idle_at;
                for (uint64_t t = 1; t <= 3000; ++t) {
                    step_all(sim, c, 1);
                    if (emptied == 0 && units_left(sim, first) == 0) emptied = t;
                    if (emptied == 0) continue;
                    for (const uint32_t id : mine) {
                        const sim::AntSnapshot* a = snap(sim, id);
                        if (a == nullptr || a->state != sim::UnitState::Idle || a->is_holding || idle_at.count(id) != 0 || chebyshev(TileCoord{a->tile_x, a->tile_y}, anchor) > 4) continue;
                        idle_at[id] = t;
                        on_unknown_tile = on_unknown_tile || c.map().ant_component(0, TileCoord{a->tile_x, a->tile_y}) < 0;       // a tile that the analysis does not know as walkable, without a walkable neighbour
                    }
                }
                ASSERT_TRUE(emptied > 0);
                ASSERT_EQ(idle_at.size(), 3u);                                                   // all three ended up idle at the empty pile
                if (footprint) ASSERT_TRUE(on_unknown_tile);                                     // and (the point of the second run) some of them on tiles of the old footprint
                const TileCoord to_next = c.map().piles()[static_cast<size_t>(next)].approach[0].click;
                for (const auto& e : idle_at) {
                    bool found = false;
                    for (const auto& s : sink.log) {
                        if (s.first < e.second || s.second.type != CommandType::GroupMove || !names(s.second, e.first)) continue;
                        ASSERT_TRUE(chebyshev(TileCoord{s.second.tile_x, s.second.tile_y}, TileCoord{28, 28}) <= 3);            // onto the next pile, not the empty one
                        ASSERT_TRUE(s.first - e.second <= p.decision_interval + p.reaction_delay * 5 / 4 + 12);
                        found = true;
                        break;
                    }
                    ASSERT_TRUE(found);
                }
                ASSERT_TRUE(chebyshev(to_next, TileCoord{28, 28}) <= 3);                         // the analysis gave a cell of the next pile (the orders above went there)
                ASSERT_TRUE(units_left(sim, next) < 40 && score_of(sim, 0) >= 100);              // and the work goes on at the next pile
                ASSERT_EQ(worker_of(c, 0)->harvest().failures(), 0u);
            }
        }
        // a pile that has been eaten into: an ant that comes later is sent to a cell of the pile's footprint AS IT IS NOW (the cell of the start may not be food any more)
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t big = place_pile(sim, 14, 14, 12, 25, {{12, 366}, {6, 369}, {0, kPileGone}});            // a 4 x 4 picture, then the 2 x 2 crackers at its middle
            Driver d(sim, 0, profile_for(Level::Hard), std::make_unique<WorkerBot>());
            const TileCoord start_click = d.map().piles()[static_cast<size_t>(big)].approach[0].click;
            uint32_t newcomer = 0;
            size_t seen = 0;
            for (uint64_t t = 0; t < 4000 && seen == 0; ++t) {
                d.tick();
                if (newcomer == 0 && units_left(sim, big) <= 5 && units_left(sim, big) > 0) newcomer = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{6, 10});
                if (newcomer == 0) continue;
                for (const auto& e : d.sent) {
                    if (e.first != sim.current_tick() || !names(e.second, newcomer)) continue;
                    ++seen;
                    ASSERT_TRUE(sim.grid().food_object_at_cell(TileCoord{e.second.tile_x, e.second.tile_y}) == big);        // a cell of the pile that is there now ...
                    ASSERT_TRUE(!(e.second.tile_x == start_click.x && e.second.tile_y == start_click.y));                  // ... not the one the start analysis gave (it is gone from the picture)
                }
            }
            ASSERT_EQ(seen, 1u);
        }
    } TEST_END();

    WORKER_TEST("AI3.4 Failure Learning: A Wall Closes After The Analysis Of The Map Was Made; The Order Comes Back Applied And Does Nothing; The Bot Learns From The Tick The Order LEFT (Not The Tick It Was Decided), Blacklists The Pile For 900 Ticks, Uses The Other Pile, And Tries The First Again Afterwards") {
        sim::SimulationEngine sim;
        world(sim);
        const int32_t a = pile(sim, 14, 8, 30, 25);                        // near: the best pile by points per trip
        const int32_t b = pile(sim, 24, 14, 3, 25);                        // far and small: one trip for each of the three ants
        // a look every 4 ticks, but an order leaves only 60 ticks after the look: a timer that started at the look would run out long before the order is even on its way
        Driver d(sim, 0, profile_with(Level::Hard, 4, 60), std::make_unique<WorkerBot>());
        const TileCoord a_click = d.map().piles()[static_cast<size_t>(a)].approach[0].click;
        const TileCoord b_click = d.map().piles()[static_cast<size_t>(b)].approach[0].click;
        ASSERT_TRUE(d.map().piles()[static_cast<size_t>(a)].approach[0].reachable() && d.map().piles()[static_cast<size_t>(b)].approach[0].reachable());
        for (int32_t x = 11; x <= 17; ++x) {                                // the wall: the engine sees it, the analysis (built just now) does not
            for (int32_t y = 5; y <= 11; ++y) {
                if (x == 11 || x == 17 || y == 5 || y == 11) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
        }
        uint64_t failed_at = 0;
        uint32_t newcomer = 0;
        bool early = false;
        uint64_t first_sent = 0;
        for (uint64_t t = 1; t <= 4200; ++t) {
            d.tick();
            const WorkerBot* w = d.worker();
            if (first_sent == 0 && !d.sent.empty()) first_sent = d.sent.front().first;
            if (failed_at == 0 && w->harvest().failures() > 0) {
                failed_at = t;
                early = first_sent == 0;                                                                     // learned before the order had even left
            }
            if (failed_at != 0 && t == failed_at + 10) newcomer = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{7, 10});     // an ant that has failed nowhere: only the blacklist keeps it from the pile
            if (failed_at != 0 && t == failed_at + 890) {
                ASSERT_EQ(w->harvest().failures(), 1u);                                                      // once, not at every look
                ASSERT_TRUE(w->harvest().blacklisted(static_cast<uint32_t>(a), t));
            }
            if (failed_at != 0 && t == failed_at + 1100) {
                ASSERT_EQ(w->harvest().failures(), 2u);                                                      // after 900 ticks it was tried again (the wall may have come down), and failed again
                ASSERT_TRUE(w->harvest().blacklisted(static_cast<uint32_t>(a), t));
            }
        }
        ASSERT_TRUE(first_sent > 0 && failed_at > 0);
        ASSERT_FALSE(early);                                                                                  // nothing was blacklisted before the order left
        const Command& first = d.sent.front().second;
        ASSERT_TRUE(first.type == CommandType::GroupMove && first.ants.size() == 3 && first.tile_x == a_click.x && first.tile_y == a_click.y);       // the analysis said: go to the near pile
        ASSERT_TRUE(failed_at > first_sent);                                                                  // learned after the order left ...
        ASSERT_TRUE(failed_at >= first_sent + 8);                                                             // ... not before the "can't go" reaction can be told from a stale one (8 ticks after the release)
        ASSERT_TRUE(failed_at < first_sent + 24);                                                             // ... and from the "can't go" reaction, which a look every 4 ticks sees long before the 24 ticks of the idle-at-origin rule are over
        ASSERT_TRUE(group_moves_to(d.sent, a_click, 0, failed_at + 890) == 1);                                // the near pile is not ordered again while it is blacklisted
        {
            size_t newcomer_orders = 0;
            for (const auto& e : d.sent) {
                if (!names(e.second, newcomer) || e.first > failed_at + 890) continue;
                ++newcomer_orders;
                ASSERT_FALSE(e.second.tile_x == a_click.x && e.second.tile_y == a_click.y);                  // ... not even for an ant that did not fail itself (the ants that did are kept away by their own exclusion)
            }
            ASSERT_TRUE(newcomer_orders >= 1u);                                                              // it went to the other pile
        }
        ASSERT_TRUE(group_moves_to(d.sent, b_click, failed_at, failed_at + 70) >= 1);                         // the three ants are sent to the other pile at once
        ASSERT_EQ(units_left(sim, b), 0u);                                                                    // and they carried all of it home
        ASSERT_TRUE(score_of(sim, 0) >= 75);
        ASSERT_TRUE(group_moves_to(d.sent, a_click, failed_at + 900, failed_at + 900 + 60) >= 1);             // after 900 ticks it is tried again
        // a stale "can't go": the clip of an order that ANOTHER hand had refused is still playing (ticks 5 to 12) when the bot's order leaves (tick 9); the engine ignores the bot's order, the
        // ant stays where it is, and the reaction on the screen belongs to the other hand: it counts as nothing for the first 8 ticks after the release. The order did nothing all the same:
        // the idle-at-origin rule learns it 24 ticks after the release (tick 33), and not before.
        {
            sim::SimulationEngine sim3;
            world(sim3, 14400, 1, 1);
            pile(sim3, 14, 8, 60, 25);
            ring(sim3, 40, 20, 44, 24);
            Driver d3(sim3, 0, profile_with(Level::Hard, 4, 8), std::make_unique<WorkerBot>());
            const uint32_t ant = ants_of(sim3, 0)[0];
            bool saw_clip_after_release = false;
            for (uint64_t t = 1; t <= 45; ++t) {
                d3.tick();
                if (t == 1) sim3.issue_move_order(ant, TileCoord{42, 22});                                    // refused: nobody can walk into the ring
                saw_clip_after_release = saw_clip_after_release || (!d3.sent.empty() && snap(sim3, ant)->state == sim::UnitState::CantGo);
                if (t == 20) {
                    ASSERT_TRUE(saw_clip_after_release && d3.sent.size() == 1u && d3.sent[0].first == 9u);    // the premise: the clip played after the release, and the order left at tick 9
                    ASSERT_EQ(d3.worker()->harvest().failures(), 0u);                                         // ... and it is no evidence
                }
            }
            ASSERT_EQ(d3.worker()->harvest().failures(), 1u);                                                 // the order did nothing: learned from the idle ant at tick 33
        }
        // the window of the "can't go" clause: the reaction counts only in the first look after the 24 ticks (here: until tick 44). Later it can be the clip of ANOTHER hand's refusal while the
        // ant stands on its tile and is not idle (the bot did not look between: the idle-at-origin rule never saw the ant standing): a reaction to the other hand, not to this order
        {
            sim::SimulationEngine sim4;
            world(sim4, 14400, 1, 1);
            pile(sim4, 14, 8, 60, 25);
            ring(sim4, 40, 20, 44, 24);
            const MapInfo map4(sim4);
            AntLedger ledger4;
            const Profile profile4 = profile_for(Level::Medium);
            HarvestTask harvest4(1);
            const auto look4 = [&]() {
                const BotView v = BotView::build(sim4, 0, &map4);
                Orders o;
                TaskContext context{v, o, ledger4, profile4, map4, 0};
                harvest4.step(context);
                return o.intents().size();
            };
            ASSERT_EQ(look4(), 1u);
            const uint32_t ant4 = ants_of(sim4, 0)[0];
            Command sent_order;
            sent_order.type = CommandType::GroupMove;
            sent_order.ants = {ant4};
            harvest4.on_command(sent_order, Bot::Fate::Sent, 0);                                              // left at tick 0; nobody applies it
            for (int i = 0; i < 60; ++i) sim4.tick();                                                         // no look
            sim4.issue_move_order(ant4, TileCoord{42, 22});                                                   // another hand's refusal: the clip plays from about tick 64
            bool clip = false;
            for (int i = 0; i < 12 && !clip; ++i) {
                sim4.tick();
                clip = snap(sim4, ant4)->state == sim::UnitState::CantGo;
            }
            ASSERT_TRUE(clip && sim4.current_tick() > 24u + profile4.decision_interval);
            (void)look4();
            ASSERT_EQ(harvest4.failures(), 0u);
        }
        // the same learning through the real controller and its budget (Easy: the order waits 60 ticks for the reaction, looks are 100 ticks apart)
        sim::SimulationEngine sim2;
        world(sim2);
        const int32_t a2 = pile(sim2, 14, 8, 30, 25);
        pile(sim2, 24, 14, 30, 25);
        RecordingSink sink(sim2, true);
        BotController c(sim2, 1);
        std::string why;
        ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Easy), sink, why));
        for (int32_t x = 11; x <= 17; ++x) {
            for (int32_t y = 5; y <= 11; ++y) {
                if (x == 11 || x == 17 || y == 5 || y == 11) sim2.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
        }
        step_all(sim2, c, 1500);
        ASSERT_EQ(worker_of(c, 0)->harvest().failures(), 1u);
        ASSERT_TRUE(score_of(sim2, 0) >= 50);                                                                 // the design: "score 50 at tick 1500"
        const TileCoord a2_click = c.map().piles()[static_cast<size_t>(a2)].approach[0].click;
        size_t to_a2 = 0;
        for (const auto& e : sink.log) to_a2 += (e.second.type == CommandType::GroupMove && e.second.tile_x == a2_click.x && e.second.tile_y == a2_click.y) ? 1u : 0u;
        ASSERT_EQ(to_a2, 1u);
    } TEST_END();

    WORKER_TEST("AI3.5 A Carrier Is Never Ordered Onto A Pile (It Would Walk Home And Deliver With The Order Of Its Loop Replaced); A Carrier That Has Stood Idle With Its Food For Longer Than Any Legitimate Wait For The Gate Is Stuck And Is Sent Home, Wherever It Stands (Far From The Hill Or In Front Of The Gate); The Ants That Queue For The Gate Are Left Alone") {
        const HarvestTask::Params params;
        ASSERT_TRUE(params.rescue_after_ticks == 900u && params.rescue_far_ticks == 40u && params.rescue_max_carriers == 8u && params.rescue_cooldown_ticks == 200u && params.ring_tiles == 4);   // the constants that the census gave
        // whole matches: not one of the commands of the workers names a carrier onto a pile
        for (const char* map : {"TINY", "SMALL", "MEDIUM", "TREASURE"}) {
            HandMatch m(map, 2, 0x0F, Level::Medium);
            m.play();
            ASSERT_TRUE(m.sim.is_match_over() && m.sink->log.size() > 20);
            ASSERT_EQ(m.sink->carriers_sent_to_piles, 0u);
        }
        // a stuck carrier, idle with its food far from the hill, with nobody queueing at the gate: sent home after rescue_far_ticks, it delivers (and then goes to work like the others)
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t a = pile(sim, 14, 8, 30, 25);
            const uint32_t stuck = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
            sim.get_unit(stuck).pick_up_food(1, 25);
            Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
            const TileCoord a_click = d.map().piles()[static_cast<size_t>(a)].approach[0].click;
            const TileCoord entrance = d.map().hill(0).entrance;
            ASSERT_TRUE(entrance.x == 5 && entrance.y == 5);
            d.run(30);
            ASSERT_EQ(d.worker()->harvest().rescues(), 0u);                                                   // not yet: it may only just have stopped
            ASSERT_TRUE(holds(sim, stuck));
            uint64_t emptied = 0;
            for (uint64_t t = 0; t < 500; ++t) {
                d.tick();
                if (emptied == 0 && !holds(sim, stuck)) emptied = sim.current_tick();
            }
            ASSERT_EQ(d.worker()->harvest().rescues(), 1u);
            ASSERT_TRUE(emptied > 100 && emptied < 400);                                                      // it walked to the hill and delivered
            ASSERT_TRUE(score_of(sim, 0) >= 25);
            size_t to_hill = 0;
            for (const auto& e : d.sent) {
                if (!names(e.second, stuck) || e.first > emptied) continue;                                    // (once it is empty it works like the others)
                ASSERT_TRUE(e.second.type == CommandType::GroupMove && e.second.tile_x == entrance.x && e.second.tile_y == entrance.y && e.second.ants.size() == 1);   // while it holds food it is only ever sent home, alone
                ++to_hill;
            }
            ASSERT_EQ(to_hill, 1u);
            ASSERT_TRUE(group_moves_to(d.sent, a_click) >= 1);                                                // the others work the pile
        }
        // the queue: with carriers idle next to the mound the far one waits too (it may be in the queue), and none of them is touched for the whole legitimate wait; then each is sent home
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            const uint32_t far = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 20});
            const uint32_t waiting = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{3, 8});                // next to the mound: where the ants queue for the gate
            const uint32_t waiting_far = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{1, 9});            // 3 tiles from the mound: the farthest that a queueing ant was measured on the shipped maps
            for (const uint32_t id : {far, waiting, waiting_far}) sim.get_unit(id).pick_up_food(1, 25);
            Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
            d.run(params.rescue_after_ticks - 100);
            ASSERT_TRUE(d.sent.empty() && d.worker()->harvest().rescues() == 0u);                              // nobody has waited long enough
            ASSERT_TRUE(holds(sim, far) && holds(sim, waiting) && holds(sim, waiting_far));
            d.run(1500);
            ASSERT_EQ(d.worker()->harvest().rescues(), 3u);                                                   // the oldest after rescue_after_ticks, the next one gate_gap_ticks later, ...
            ASSERT_TRUE(!holds(sim, far) && !holds(sim, waiting) && !holds(sim, waiting_far));                // and all of them delivered
            ASSERT_EQ(score_of(sim, 0), 75);
            for (const auto& e : d.sent) ASSERT_TRUE(e.first >= params.rescue_after_ticks - 100 && e.second.type == CommandType::GroupMove && e.second.ants.size() <= 3);
        }
        // the ring: a carrier 4 tiles from the mound is within the ring of the queue (it waits for the clock), one 5 tiles from the mound is far (and alone: sent home at once)
        for (const int32_t tiles : {4, 5}) {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            const uint32_t id = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{7 + tiles, 5});
            sim.get_unit(id).pick_up_food(1, 25);
            Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
            d.run(300);
            if (tiles == 5) {
                ASSERT_EQ(d.worker()->harvest().rescues(), 1u);                                               // far: after rescue_far_ticks
            } else {
                ASSERT_EQ(d.worker()->harvest().rescues(), 0u);                                               // within the ring: it is waiting for the gate as far as anybody can tell ...
                d.run(params.rescue_after_ticks);
                ASSERT_EQ(d.worker()->harvest().rescues(), 1u);                                               // ... until the clock says it is not
            }
            d.run(400);
            ASSERT_TRUE(!holds(sim, id) && score_of(sim, 0) == 25);
        }
        // a carrier that a blow or a blast stopped on its way home, 1, 3 and 5 tiles from the mound: nobody walks it in, the clock does
        for (const int32_t tiles : {1, 3, 5}) {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 2);
            pile(sim, 14, 8, 20, 25);
            const uint32_t id = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{7 + tiles, 5});
            sim.get_unit(id).pick_up_food(1, 25);
            Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
            sim::Command home;
            home.type = CommandType::GroupMove;
            home.issuer = 0;
            home.tile_x = 5;
            home.tile_y = 5;
            home.ants = {id};
            sim.apply_command(home);
            d.run(12);
            sim.blast_tile_for_test(TileCoord{snap(sim, id)->tile_x, snap(sim, id)->tile_y});                 // the blast cancels the walk: the carrier stands idle with its food
            d.run(400);
            ASSERT_TRUE(holds(sim, id) && d.worker()->harvest().rescues() == 0u);                               // it stands there with its food: nobody walks it in
            bool delivered = false;
            for (uint64_t t = 0; t < params.rescue_after_ticks + 900; ++t) {
                d.tick();
                delivered = delivered || !holds(sim, id);
            }
            ASSERT_TRUE(delivered && d.worker()->harvest().rescues() >= 1u);                                  // the clock sent it home, and it delivered (and went back to work)
        }
        // a carrier that cannot be helped (it is shut into a ring: the order home ends in "Can't go there.") is not ordered at every look: its wait starts again with every order and two
        // orders are 200 ticks apart at the least (5 in the first 1,000 ticks: after 40, then every 200 ticks)
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            ring(sim, 24, 24, 28, 28);
            const uint32_t id = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{26, 26});
            sim.get_unit(id).pick_up_food(1, 25);
            Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
            d.run(1000);
            ASSERT_TRUE(d.worker()->harvest().rescues() >= 4u && d.worker()->harvest().rescues() <= 6u);
            ASSERT_TRUE(holds(sim, id));
            for (size_t i = 1; i < d.sent.size(); ++i) ASSERT_TRUE(d.sent[i].first >= d.sent[i - 1].first + params.rescue_cooldown_ticks);
        }
        // a rescued ant's wait starts again: its order is lost (nobody applies it) and a look 200 ticks later, when the cool-down is over, must not send it again (it has not stood for 900
        // ticks since the order), a look 900 ticks after the order does
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            const uint32_t id = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{11, 5});
            sim.get_unit(id).pick_up_food(1, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Medium);
            HarvestTask harvest(1);
            const auto look = [&]() {
                const BotView v = BotView::build(sim, 0, &map);
                Orders o;
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            look();                                                                                           // it is seen idle with food at tick 0
            while (sim.current_tick() < params.rescue_after_ticks + 5) sim.tick();
            look();
            ASSERT_EQ(harvest.rescues(), 1u);
            while (sim.current_tick() < params.rescue_after_ticks + 5 + params.rescue_cooldown_ticks + 10) sim.tick();
            look();
            ASSERT_EQ(harvest.rescues(), 1u);
            while (sim.current_tick() < 2 * params.rescue_after_ticks + 20) sim.tick();
            look();
            ASSERT_EQ(harvest.rescues(), 2u);
        }
        // a saturated gate: with 8 carriers idle at once (the queue of a big hill) nobody is helped, with 7 the oldest is
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            std::vector<uint32_t> ids;
            for (int32_t i = 0; i < 8; ++i) {
                ids.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i / 3, 5 + i % 3}));
                sim.get_unit(ids.back()).pick_up_food(1, 25);
            }
            Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
            d.run(params.rescue_after_ticks + 900);
            ASSERT_TRUE(d.worker()->harvest().rescues() == 0u && d.sent.empty());
            sim.kill_unit(ids.back());
            d.run(200);
            ASSERT_TRUE(d.worker()->harvest().rescues() >= 1u);
        }
        // whole matches, alone on a seat: TREASURE seed 1 (the carrier that stands on the tile in front of the gate after the walk home was refused: it waits for ever without the
        // clock) and TINY seed 2 on seat 3 (the carrier that stands at its pile with its food after a bite, 7 tiles from the hill)
        for (const bool treasure : {true, false}) {
            const char* map = treasure ? "TREASURE" : "TINY";
            const uint8_t seat = treasure ? 0 : 3;
            sim::SimulationEngine engine;
            engine.init(level_of(map), 2, 0x0F);
            const TileCoord entrance = MapInfo(engine).hill(seat).entrance;
            const ArenaResult r = play_match(match_of(map, treasure ? 1 : 2, static_cast<uint8_t>(1u << seat), Level::Medium, 0, 3, true));
            ASSERT_TRUE(r.error.empty() && r.match_over);
            uint64_t first_home = 0;
            for (const RecordedCommand& c : r.log) {
                if (c.command.type == CommandType::GroupMove && c.command.ants.size() == 1 && c.command.tile_x == entrance.x && c.command.tile_y == entrance.y && first_home == 0) first_home = c.tick;
            }
            if (treasure) {
                ASSERT_TRUE(first_home >= 900 && first_home <= 1600);                                         // after the clock (the carrier stood idle since tick 240)
                ASSERT_TRUE(r.seats[seat].score >= 2900);                                                     // 2750 when it was left standing for the whole match
            } else {
                ASSERT_TRUE(first_home >= 1700 && first_home <= 2000);                                        // a few looks after the bite at tick 1725
                ASSERT_TRUE(r.seats[seat].score >= 1700);                                                     // 1380 without
            }
        }
    } TEST_END();

    WORKER_TEST("AI3.6 The Worker Never Hatches, Whatever It Could Afford: Rich And With Eggs In A Hand-Made World, And Over Whole Matches On The Five Maps (The Eggs Stay As They Are, Nothing Is Hatched)") {
        sim::SimulationEngine sim;
        world(sim);
        pile(sim, 14, 8, 30, 25);
        sim.set_player_score(0, 1000);
        ASSERT_TRUE(sim.get_player_eggs(0) > 0);
        Driver d(sim, 0, profile_for(Level::Hard), std::make_unique<WorkerBot>());
        d.run(3000);
        for (const auto& e : d.sent) ASSERT_TRUE(e.second.type != CommandType::Hatch);
        ASSERT_TRUE(!d.sent.empty());
        ASSERT_EQ(sim.get_player_eggs(0), 10u);
        ASSERT_EQ(sim.get_player_hatched(0), 0u);
        for (const char* map : kLandMaps) {
            for (const Level level : kLevels) {
                const ArenaResult& r = alone_full(map, level);
                ASSERT_TRUE(r.seats[0].hatched == 0 && r.seats[0].eggs == eggs_of(map));
                for (const RecordedCommand& c : r.log) ASSERT_TRUE(c.command.type == CommandType::GroupMove);
            }
        }
    } TEST_END();

    WORKER_TEST("AI3.7 An Invitation To Team Up Is Declined With One Deny, At Every Level, Also The Next One; Nothing Else Is Said; The Controller Lets It Through (An Answer Needs The Invitation It Answers); Also Through The Latency Of A Room (A Hard Bot Looks Every 4 Ticks And Its Deny Takes 3 To 5 More To Be Applied), On Every Seat, From Every Inviter") {
        for (const Level level : kLevels) {
            sim::SimulationEngine sim;
            world(sim);
            pile(sim, 14, 8, 30, 25);
            RecordingSink sink(sim, true);
            BotController c(sim, 5);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", level), sink, why));
            step_all(sim, c, 10);
            Command invite;
            invite.type = CommandType::AllianceInvite;
            invite.issuer = 1;
            invite.other_player = 0;
            ASSERT_TRUE(sim.apply_command(invite).status == sim::CommandResult::Status::Applied);
            step_all(sim, c, 1);
            ASSERT_EQ(sim.get_world_state().pending_invite_from[0], 1);                                       // it is waiting for an answer
            step_all(sim, c, 300);
            const auto denies = [&]() { return std::count_if(sink.log.begin(), sink.log.end(), [](const auto& e) { return e.second.type == CommandType::AllianceDeny; }); };
            ASSERT_EQ(denies(), 1);                                                                           // one, not one at every look until it was applied
            ASSERT_EQ(sim.get_world_state().pending_invite_from[0], 255);                                     // answered
            ASSERT_TRUE(sim.get_ally_id(0) >= sim::MAX_PLAYERS);                                              // and no alliance
            for (const auto& e : sink.log) {
                if (e.second.type == CommandType::AllianceDeny) ASSERT_TRUE(e.second.issuer == 0 && e.second.other_player == 1);
                else ASSERT_TRUE(e.second.type == CommandType::GroupMove);
            }
            invite.issuer = 2;                                                                                // the next invitation, from another team
            ASSERT_TRUE(sim.apply_command(invite).status == sim::CommandResult::Status::Applied);
            step_all(sim, c, 300);
            ASSERT_EQ(denies(), 2);
            ASSERT_EQ(sim.get_world_state().pending_invite_from[0], 255);
            ASSERT_TRUE(c.stats(0).filtered == 0 && c.stats(0).rejected == 0);
            ASSERT_EQ(worker_of(c, 0)->denials(), 2u);
        }
        // a room: the Deny is applied 3 or 5 ticks after the release, so the invitation is still there at the next looks of a Hard bot; the bot must not send it again. Every seat, every inviter.
        for (const Level level : kLevels) {
            for (const uint32_t latency : {3u, 5u}) {
                for (uint8_t seat = 0; seat < 4; ++seat) {
                    for (uint8_t from = 0; from < 4; ++from) {
                        if (from == seat) continue;
                        sim::SimulationEngine sim;
                        world(sim);
                        LatencySink sink(sim, latency);
                        BotController c(sim, 5);
                        std::string why;
                        ASSERT_TRUE(c.add(spec_of(seat, "worker", level), sink, why));
                        step_late(sim, c, sink, 10);
                        Command invite;
                        invite.type = CommandType::AllianceInvite;
                        invite.issuer = from;
                        invite.other_player = seat;
                        ASSERT_TRUE(sim.apply_command(invite).status == sim::CommandResult::Status::Applied);
                        step_late(sim, c, sink, 300);
                        ASSERT_EQ(sink.count(CommandType::AllianceDeny), 1u);
                        ASSERT_EQ(sim.get_world_state().pending_invite_from[seat], 255);                      // answered once, and accepted by the engine
                        ASSERT_TRUE(sink.not_applied == 0u && c.stats(seat).rejected == 0u && c.stats(seat).filtered == 0u);
                    }
                }
            }
        }
    } TEST_END();

    WORKER_TEST("AI3.8 The Endgame Veto: No Order For A Trip That Cannot Be Finished Before The Clock Runs Out (Points Carried At 0:00 Are Lost), One When There Is Time; And In Whole Matches No Pile Is Ordered With Less Time Left Than Its Trip Takes") {
        // a far pile: the trip of one ant is long
        int32_t trip = 0;
        {
            sim::SimulationEngine probe;
            world(probe);
            const int32_t p = pile(probe, 40, 40, 30, 25);
            trip = MapInfo(probe).trip_ticks(0, static_cast<uint32_t>(p));
            ASSERT_TRUE(trip > 600);
        }
        const Profile profile = profile_for(Level::Medium);
        const int32_t delay = static_cast<int32_t>(profile.reaction_delay + profile.reaction_delay * profile.jitter_percent / 100u);
        const auto orders_in = [&](int32_t ticks_in_the_match) -> size_t {
            sim::SimulationEngine sim;
            world(sim, static_cast<uint32_t>(ticks_in_the_match));
            pile(sim, 40, 40, 30, 25);
            Driver d(sim, 0, profile, std::make_unique<WorkerBot>());
            d.run(static_cast<uint64_t>(std::min(ticks_in_the_match - 5, 400)));
            if (d.decided.size() < 3) return size_t{9999};                                                    // (it must have looked: an impossible answer says so)
            return d.sent.size();
        };
        ASSERT_EQ(orders_in(trip - 20), 0u);                                                                  // not even one trip fits
        ASSERT_EQ(orders_in(trip + 5), 0u);                                                                   // the trip fits but the last bite and the walk into the hill do not
        ASSERT_EQ(orders_in(trip + 9), 0u);
        ASSERT_TRUE(orders_in(trip + 10 + delay + static_cast<int32_t>(profile.decision_interval) + 10) >= 1u);                     // with the reaction time and a look to spare it goes
        ASSERT_TRUE(orders_in(trip + 400) >= 1u);
        // the margin itself: a profile with no jitter and a reaction time of 1 tick (Medium's 24 ticks of delay would hide any margin below 24): the veto is trip + margin + 1 ticks, a look
        // every 4 ticks finds the line to within 4 ticks
        {
            const HarvestTask::Params defaults;
            ASSERT_EQ(defaults.endgame_margin_ticks, 10u);
            const Profile quick = profile_with(Level::Hard, 4, 1);
            const auto quick_orders = [&](int32_t ticks_in_the_match) -> size_t {
                sim::SimulationEngine sim;
                world(sim, static_cast<uint32_t>(ticks_in_the_match));
                pile(sim, 40, 40, 30, 25);
                Driver d(sim, 0, quick, std::make_unique<WorkerBot>());
                d.run(40);
                if (d.decided.size() < 5) return size_t{9999};                                                // (it must have looked: an impossible answer says so)
                return d.sent.size();
            };
            const int32_t line = trip + static_cast<int32_t>(defaults.endgame_margin_ticks) + 1 + 1;         // (+ the tick of the first look)
            ASSERT_EQ(quick_orders(line - 5), 0u);                                                            // a margin of 5 or less lets it through here
            ASSERT_TRUE(quick_orders(line + 6) >= 1u);                                                        // a margin of 17 or more keeps it away from here
        }
        // whole matches: the veto is part of the bot, not of the test world
        for (const char* map : kLandMaps) {
            const ArenaResult& r = four_workers(map, Level::Medium);
            sim::SimulationEngine engine;
            engine.init(level_of(map), 1, 0x0F);
            const MapInfo info(engine);
            size_t orders = 0;
            for (const RecordedCommand& c : r.log) {
                if (c.command.type != CommandType::GroupMove) continue;
                const TileCoord to{c.command.tile_x, c.command.tile_y};
                const assets::AnthillSpawn* hill = engine.grid().find_anthill(c.command.issuer);
                if (hill != nullptr && to.x == hill->x + 1 && to.y == hill->y + 1) continue;                    // a carrier sent home
                int32_t best = 99;
                int32_t trip_of_it = -1;
                for (const PileInfo& p : info.piles()) {
                    const int32_t dist = chebyshev(p.anchor, to);
                    if (dist < best) {
                        best = dist;
                        trip_of_it = info.trip_ticks(c.command.issuer, p.index);
                    }
                }
                ASSERT_TRUE(best <= 4 && trip_of_it > 0);
                ASSERT_TRUE(static_cast<int64_t>(r.initial_ticks) - static_cast<int64_t>(c.tick) >= trip_of_it);   // the clock leaves time for the trip
                ++orders;
            }
            ASSERT_TRUE(orders >= 4);                                                                         // (GAUNTLET: one order for each of the four hills)
        }
    } TEST_END();

    WORKER_TEST("AI3.9 The Levels Are Not Worse Than Each Other On The Economy (It Is Nearly Insensitive To Reaction Time And Command Rate, So Not A Strict Ladder Before The Tactics Of B4): Medium At Least 97 Percent Of Easy, Hard At Least 97 Percent Of Medium, On Every Map, After Two Minutes And Over The Whole Match") {
        for (const char* map : kLandMaps) {
            const BaselineRow* easy = measured(map, Level::Easy);
            const BaselineRow* medium = measured(map, Level::Medium);
            const BaselineRow* hard = measured(map, Level::Hard);
            ASSERT_TRUE(easy != nullptr && medium != nullptr && hard != nullptr && easy->error.empty() && medium->error.empty() && hard->error.empty());
            ASSERT_TRUE(mean4(medium->solo_2min) * 100 >= mean4(easy->solo_2min) * 97);
            ASSERT_TRUE(mean4(hard->solo_2min) * 100 >= mean4(medium->solo_2min) * 97);
            ASSERT_TRUE(mean4(medium->solo_full) * 100 >= mean4(easy->solo_full) * 97);
            ASSERT_TRUE(mean4(hard->solo_full) * 100 >= mean4(medium->solo_full) * 97);
            ASSERT_TRUE(mean4(easy->solo_full) > 0 && mean4(easy->solo_2min) > 0);
        }
        for (const Level level : kLevels) {
            const BaselineRow* islands = measured("ISLANDS", level);
            ASSERT_TRUE(islands != nullptr && islands->error.empty());
            for (size_t seat = 0; seat < 4; ++seat) ASSERT_TRUE(islands->solo_2min[seat] == 0 && islands->solo_full[seat] == 0 && islands->four_full[seat] == 0);   // 0 = 0 until B4a
        }
    } TEST_END();

    WORKER_TEST("AI3.10 The Budget Is Never Exceeded Over A Whole TREASURE Match Of Four Workers At Any Level: No Window Of Releases Holds More Commands Than The Bucket And The Rate Allow, Nothing Is Refused") {
        for (const Level level : kLevels) {
            const ArenaResult r = play_match(match_of("TREASURE", 1, 0x0F, level, 0, 0, true));      // latency 0: the tick of a record is the tick of the release
            ASSERT_TRUE(r.error.empty() && r.match_over && r.ticks >= 14400);
            const Profile p = profile_for(level);
            size_t most = 0;
            for (uint8_t seat = 0; seat < 4; ++seat) {
                std::vector<uint64_t> ticks;
                for (const RecordedCommand& c : r.log) {
                    if (c.command.issuer == seat) ticks.push_back(c.tick);
                }
                ASSERT_TRUE(std::is_sorted(ticks.begin(), ticks.end()));
                ASSERT_TRUE(within_budget(ticks, p));
                ASSERT_EQ(ticks.size(), static_cast<size_t>(r.seats[seat].stats.released));
                ASSERT_TRUE(r.seats[seat].stats.rejected == 0 && r.seats[seat].stats.filtered == 0);
                ASSERT_TRUE(r.seats[seat].milli_commands_per_second(r.ticks) < 150u);
                most = std::max(most, ticks.size());
            }
            ASSERT_TRUE(most >= 20);                                                                  // (the windows were not empty)
        }
    } TEST_END();

    WORKER_TEST("AI3.11 Four Workers Share The Whole Pot On Every Shipped Map At Every Level (The Sum Of The Four Scores Is The Reachable Points, Less What Ants Carry At 0:00, A Few Units More Where Two Hills Bite The Same Last Unit); ISLANDS Stays At 0 = 0; Nothing That A Person Could Not Do") {
        for (const char* map : kLandMaps) {
            for (const Level level : kLevels) {
                const ArenaResult& r = four_workers(map, level);
                ASSERT_TRUE(r.error.empty() && r.match_over && r.seats.size() == 4);
                int32_t sum = 0;
                for (const ArenaSeatResult& s : r.seats) {
                    sum += s.score;
                    ASSERT_TRUE(s.score * 100 >= pot_of(map) * 5);                                    // everybody takes part
                    ASSERT_TRUE(s.stats.rejected == 0 && s.stats.filtered == 0 && s.hatched == 0 && s.eggs == eggs_of(map));
                    ASSERT_TRUE(s.milli_commands_per_second(r.ticks) < 150u);
                }
                ASSERT_EQ(r.reachable_units_left, 0u);                                                // the whole pot was taken: not a unit is left on a pile that a hill can walk to
                ASSERT_TRUE(sum * 100 >= pot_of(map) * 98 && sum * 100 <= pot_of(map) * 104);        // and banked, give or take: 600 matches (40 seeds, five maps, three levels) gave 100.00 to 102.50 percent of the pot (a double bite of the last unit adds up to 2.5)
                const int32_t width = static_cast<int32_t>(level_of(map).width);
                const int32_t height = static_cast<int32_t>(level_of(map).height);
                for (const RecordedCommand& c : r.log) {
                    ASSERT_TRUE(c.command.type == CommandType::GroupMove && c.command.issuer < 4);
                    ASSERT_TRUE(!c.command.ants.empty() && c.command.ants.size() <= kHudAntCap);
                    ASSERT_TRUE(c.command.tile_x >= 0 && c.command.tile_x < width && c.command.tile_y >= 0 && c.command.tile_y < height);
                }
            }
        }
        for (const Level level : kLevels) {
            const ArenaResult& r = four_workers("ISLANDS", level);
            ASSERT_TRUE(r.error.empty() && r.log.empty());
            for (const ArenaSeatResult& s : r.seats) ASSERT_TRUE(s.score == 0 && s.stats.released == 0);
        }
    } TEST_END();

    WORKER_TEST("AI3.12 The Pinned Numbers: The Worker Alone (Two Minutes, Whole Match) And Four Of Them, At Three Levels On The Six Shipped Maps, Within 8 Percent Of tests/test_ai/baselines.inc (Written By bot_arena --write-baselines; ISLANDS: 0 = 0)") {
        const size_t rows = sizeof(kPinned) / sizeof(kPinned[0]);
        ASSERT_EQ(rows, 18u);                                                                         // six maps, three levels
        std::string bad;
        for (const PinnedRow& row : kPinned) {
            // the table itself is consistent (a hand-edited number would show here)
            int32_t sum = 0;
            for (size_t seat = 0; seat < 4; ++seat) {
                ASSERT_TRUE(row.solo_full[seat] >= row.solo_2min[seat] && row.four_full[seat] <= row.pot);
                sum += row.four_full[seat];
            }
            ASSERT_EQ(row.pot, pot_of(row.map));
            ASSERT_TRUE(std::abs(sum - row.four_sum) <= 4);                                           // (rounded means)
            ASSERT_TRUE(row.four_sum * 100 >= row.pot * 98 && row.four_sum * 100 <= row.pot * 104);
            const BaselineRow* m = measured(row.map, row.level);
            ASSERT_TRUE(m != nullptr && m->error.empty());
            ASSERT_EQ(m->pot, row.pot);
            const auto check = [&](const char* what, size_t seat, int32_t value, int32_t was) {
                if (!in_band(value, was)) {
                    bad += std::string("    ") + row.map + " " + level_name(row.level) + " " + what + "[" + std::to_string(seat) + "]: measured " + std::to_string(value) + ", pinned " + std::to_string(was) + "\n";
                }
            };
            for (size_t seat = 0; seat < 4; ++seat) {
                check("alone, 2 minutes", seat, m->solo_2min[seat], row.solo_2min[seat]);
                check("alone, whole match", seat, m->solo_full[seat], row.solo_full[seat]);
                check("four workers", seat, m->four_full[seat], row.four_full[seat]);
            }
            check("sum of four", 4, m->four_sum, row.four_sum);
        }
        if (!bad.empty()) std::cout << "\n" << bad << "    (if the bot or the hill's banking was changed on purpose: bot_arena --write-baselines > tests/test_ai/baselines.inc)\n    ";
        ASSERT_TRUE(bad.empty());
    } TEST_END();

    WORKER_TEST("AI3.13 The Task Model: The Ledger; A Task Does Not Take An Ant That Another Holds; An Order That Never Left Is Forgotten; The Clock Of An Order Starts When It Left; Ants That Cannot Reach The Hill's Side Are Not Sent; A Restarted Bot Loses Nothing That The World Does Not Give Back") {
        // the ledger
        {
            AntLedger l;
            ASSERT_TRUE(l.claim(5, 1) && l.claim(5, 1));
            ASSERT_FALSE(l.claim(5, 2));
            ASSERT_FALSE(l.claim(6, kNoTask));
            ASSERT_EQ(l.owner(5), 1u);
            ASSERT_TRUE(l.is_free(9) && !l.is_free(5));
            ASSERT_TRUE(l.claim(7, 2) && l.claim(8, 1));
            ASSERT_EQ(l.count(1), 2u);
            ASSERT_EQ(l.count(2), 1u);
            ASSERT_TRUE((l.ants_of(1) == std::vector<uint32_t>{5, 8}));
            ASSERT_FALSE(l.release(5, 2));                                                            // only the owner lets go
            ASSERT_TRUE(l.release(5, 1) && l.is_free(5));
            ASSERT_FALSE(l.release(5, 1));
            std::vector<AntView> alive(1);
            alive[0].id = 7;
            l.forget_missing(alive);                                                                   // 8 is gone
            ASSERT_TRUE(l.owner(7) == 2u && l.is_free(8) && l.size() == 1u);
            ASSERT_TRUE(l.claim(10, 2) && l.claim(11, 1));
            ASSERT_EQ(l.release_all(2), 2u);
            ASSERT_TRUE(l.owner(11) == 1u && l.size() == 1u);
        }
        // a task does not take what another holds, and lets go of its own when it ends
        {
            sim::SimulationEngine sim;
            world(sim);
            pile(sim, 12, 12, 30, 25);
            const MapInfo map(sim);
            const BotView view = BotView::build(sim, 0, &map);
            AntLedger ledger;
            Orders orders;
            const Profile profile = profile_for(Level::Medium);
            TaskContext context{view, orders, ledger, profile, map, 0};
            const std::vector<uint32_t> mine = ants_of(sim, 0);
            ASSERT_EQ(mine.size(), 3u);
            ASSERT_TRUE(ledger.claim(mine[0], 7));                                                     // another task holds the first ant
            HarvestTask harvest(1);
            harvest.step(context);
            ASSERT_EQ(orders.intents().size(), 1u);
            ASSERT_TRUE((orders.intents()[0].command.ants == std::vector<uint32_t>{mine[1], mine[2]}));
            ASSERT_TRUE(ledger.owner(mine[0]) == 7u && ledger.owner(mine[1]) == 1u && ledger.owner(mine[2]) == 1u);
            ASSERT_EQ(harvest.working(), 2u);
            ASSERT_TRUE(harvest.state() == Task::State::Running && std::string(harvest.name()) == "harvest");
            harvest.finish(ledger);
            ASSERT_TRUE(ledger.owner(mine[0]) == 7u && ledger.is_free(mine[1]) && ledger.is_free(mine[2]));
        }
        // an order that never left (it expired, or its ants died) is forgotten: the ants are idle and empty-handed, so they are in the pool again; and the clock of an order
        // starts when it LEFT: an order decided at tick 0 that leaves at tick 30 is not a failure at tick 40, and is one 24 ticks after it left
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t a = pile(sim, 12, 12, 30, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Medium);
            HarvestTask harvest(1);
            const auto look = [&](Orders& o) {
                const BotView v = BotView::build(sim, 0, &map);
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            Orders first;
            look(first);
            ASSERT_EQ(first.intents().size(), 1u);
            const Command c1 = first.intents()[0].command;
            ASSERT_EQ(c1.ants.size(), 3u);
            ASSERT_EQ(harvest.working(), 3u);
            harvest.on_command(c1, Bot::Fate::Expired, 5);
            ASSERT_EQ(harvest.working(), 0u);
            Orders second;
            look(second);                                                                              // the same three ants, ordered again
            ASSERT_TRUE(second.intents().size() == 1 && second.intents()[0].command.ants == c1.ants);
            ASSERT_EQ(ledger.count(1), 3u);
            harvest.on_command(second.intents()[0].command, Bot::Fate::Pruned, 6);
            ASSERT_EQ(harvest.working(), 0u);
            Orders third;
            look(third);
            ASSERT_TRUE(third.intents().size() == 1 && third.intents()[0].command.ants == c1.ants);
            for (int i = 0; i < 30; ++i) sim.tick();
            harvest.on_command(third.intents()[0].command, Bot::Fate::Sent, 30);                      // decided at tick 0, left at tick 30; nobody applies it: the ants stay where they are
            for (int i = 0; i < 23; ++i) sim.tick();                                                  // tick 53: 23 ticks after it left
            Orders young;
            look(young);
            ASSERT_TRUE(young.intents().empty() && harvest.failures() == 0u);                         // (a timer from the look at tick 0 would have fired 29 ticks ago)
            sim.tick();                                                                                // tick 54: 24 ticks after it left
            Orders late;
            look(late);
            ASSERT_EQ(harvest.failures(), 1u);
            ASSERT_TRUE(harvest.blacklisted(static_cast<uint32_t>(a), 54) && late.intents().empty());  // the only pile is left alone ...
            ASSERT_TRUE(harvest.blacklisted(static_cast<uint32_t>(a), 54 + 899) && !harvest.blacklisted(static_cast<uint32_t>(a), 54 + 900));        // ... for 900 ticks
            ASSERT_EQ(harvest.working(), 0u);
            ASSERT_EQ(ledger.size(), 0u);
        }
        // the ledger and the records agree at every look: an order that never left (on_command Expired) took its ants' records, and the next look gives their claims back, also when no pile is left
        // that they could be sent to (a second task would otherwise find them held by this one for ever)
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t a = pile(sim, 12, 12, 30, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Medium);
            HarvestTask harvest(1);
            const auto look = [&](Orders& o) {
                const BotView v = BotView::build(sim, 0, &map);
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            Orders first;
            look(first);
            ASSERT_TRUE(first.intents().size() == 1 && ledger.count(1) == 3u);
            harvest.on_command(first.intents()[0].command, Bot::Fate::Expired, 5);
            bool changed = false;
            sim.grid_mut().take_food(a, 30, changed);                                                         // the pile is empty now: nothing to send the ants to
            Orders second;
            look(second);
            ASSERT_TRUE(second.intents().empty());
            ASSERT_EQ(ledger.count(1), 0u);                                                                   // the claims of the forgotten order are given back
            ASSERT_EQ(harvest.unplaced(), 3u);
        }
        // a record that no fate ever answered (a bot driven without the controller) is dropped after the longest an order can take to leave (delay + jitter + time to live + a look + 8): the ants
        // are in the pool again. At a look before that nothing is ordered twice; after it they are ordered again.
        {
            sim::SimulationEngine sim;
            world(sim);
            pile(sim, 12, 12, 30, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Medium);
            const uint64_t longest = profile.reaction_delay + profile.reaction_delay * profile.jitter_percent / 100u + profile.intent_ttl + profile.decision_interval + 8u;
            HarvestTask harvest(1);
            const auto look = [&](Orders& o) {
                const BotView v = BotView::build(sim, 0, &map);
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            for (int i = 0; i < 4; ++i) sim.tick();
            Orders first;
            look(first);                                                                                      // decided at tick 4
            ASSERT_EQ(first.intents().size(), 1u);
            while (sim.current_tick() < 4 + longest - 2) sim.tick();
            Orders young;
            look(young);
            ASSERT_TRUE(young.intents().empty() && harvest.working() == 3u);                                   // just before the line: still waiting for the fate
            while (sim.current_tick() < 4 + longest) sim.tick();
            Orders old;
            look(old);
            ASSERT_TRUE(old.intents().size() == 1 && old.intents()[0].command.ants.size() == 3u);              // at the line: forgotten, and sent again
        }
        // an ant that went somewhere else (another hand ordered it away) and stands idle there is no failure of the pile: it did something; it is idle again and goes to work
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t a = pile(sim, 12, 12, 30, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Medium);
            HarvestTask harvest(1);
            const auto look = [&](Orders& o) {
                const BotView v = BotView::build(sim, 0, &map);
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            Orders first;
            look(first);
            ASSERT_TRUE(first.intents().size() == 1 && first.intents()[0].command.ants.size() == 3);
            harvest.on_command(first.intents()[0].command, Bot::Fate::Sent, 0);
            for (const uint32_t ant : ants_of(sim, 0)) sim.issue_move_order(ant, TileCoord{22, 12});          // not the bot's order: the ants walk east instead and stop
            for (int i = 0; i < 300; ++i) sim.tick();
            for (const uint32_t ant : ants_of(sim, 0)) {
                const sim::AntSnapshot* gone = snap(sim, ant);
                ASSERT_TRUE(gone != nullptr && chebyshev(TileCoord{gone->tile_x, gone->tile_y}, TileCoord{5, 10}) > 8);                      // they are far from where they stood
            }
            Orders again;
            look(again);
            ASSERT_EQ(harvest.failures(), 0u);                                                         // they moved: that is not "the order did nothing"
            ASSERT_FALSE(harvest.blacklisted(static_cast<uint32_t>(a), sim.current_tick()));
            ASSERT_TRUE(again.intents().size() == 1 && again.intents()[0].command.ants.size() == 3);   // idle and empty-handed: sent to the pile again
        }
        // ants that cannot walk to the hill's side are not sent: one is shut into a ring of obstacles, the three on the grass go
        {
            sim::SimulationEngine sim;
            world(sim);
            pile(sim, 12, 12, 30, 25);
            for (int32_t x = 28; x <= 32; ++x) {
                for (int32_t y = 38; y <= 42; ++y) {
                    if (x == 28 || x == 32 || y == 38 || y == 42) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                }
            }
            const uint32_t stranded = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 40});
            const MapInfo map(sim);
            ASSERT_TRUE(map.ant_component(0, TileCoord{30, 40}) != map.hill_component(0));
            const BotView view = BotView::build(sim, 0, &map);
            AntLedger ledger;
            Orders orders;
            const Profile profile = profile_for(Level::Medium);
            TaskContext context{view, orders, ledger, profile, map, 0};
            HarvestTask harvest(1);
            harvest.step(context);
            ASSERT_TRUE(orders.intents().size() == 1 && orders.intents()[0].command.ants.size() == 3);
            ASSERT_FALSE(names(orders.intents()[0].command, stranded));
            ASSERT_EQ(harvest.unplaced(), 1u);
            ASSERT_TRUE(ledger.is_free(stranded));
        }
        // a bot may be restarted at any moment: a new worker in the middle of the match picks the work up from the world alone
        {
            const auto score_after = [&](bool restart) {
                sim::SimulationEngine sim;
                sim.init(level_of("TINY"), 1, 0x0F);
                Driver d(sim, 0, profile_for(Level::Medium), std::make_unique<WorkerBot>());
                if (!restart) {
                    d.run(3600);
                } else {
                    d.run(1500);
                    d.restart(std::make_unique<WorkerBot>());
                    d.run(2100);
                }
                return score_of(sim, 0);
            };
            const int32_t straight = score_after(false);
            const int32_t restarted = score_after(true);
            ASSERT_TRUE(straight >= 800);
            ASSERT_TRUE(std::abs(restarted - straight) * 100 <= straight * 8);
        }
    } TEST_END();

    WORKER_TEST("AI3.14 Determinism And Replay: A Match Of Workers Is Bit-Reproducible And Nothing But Its Commands (They Replay Into A Fresh Engine Without Any Bot); Another Seed Is Another Match; standard Is An Alias Of The Worker Until B4; The Registry") {
        for (const char* map : {"TINY", "TREASURE"}) {
            const ArenaSpec spec = match_of(map, 7, 0x0F, Level::Medium, 0, 3, true);
            const ArenaResult a = play_match(spec);
            const ArenaResult b = play_match(spec);
            ASSERT_TRUE(a.error.empty() && a.match_over && a.log.size() > 20);
            ASSERT_TRUE(a.hash == b.hash && a.checkpoints == b.checkpoints && a.log.size() == b.log.size() && a.ticks == b.ticks);
            for (size_t i = 0; i < a.seats.size(); ++i) ASSERT_TRUE(a.seats[i].score == b.seats[i].score && a.seats[i].stats.released == b.seats[i].stats.released);
            const ReplayResult replay = replay_commands(spec, a);
            ASSERT_TRUE(replay.ok && replay.hash == a.hash);                                          // the commands alone give the same match
            const ArenaResult alias = play_match(match_of(map, 7, 0x0F, Level::Medium, 0, 3, true, "standard"));
            ASSERT_TRUE(alias.error.empty() && alias.hash == a.hash && alias.log.size() == a.log.size());   // standard IS the worker (the same seat seed: the kind that plays is "worker")
            for (const ArenaSeatResult& s : alias.seats) ASSERT_TRUE(s.runs == "worker" && s.spec.kind == "standard");
            const ArenaResult other = play_match(match_of(map, 8, 0x0F, Level::Medium, 0, 3, true));
            ASSERT_TRUE(other.hash != a.hash);
        }
        BotSpec spec;
        spec.kind = "idle";
        ASSERT_EQ(std::string(make_bot(spec)->kind()), "idle");
        spec.kind = "worker";
        ASSERT_EQ(std::string(make_bot(spec)->kind()), "worker");
        spec.kind = "standard";
        ASSERT_EQ(std::string(make_bot(spec)->kind()), "worker");                                     // until the standard bot of B4
        spec.kind = "genius";
        ASSERT_TRUE(make_bot(spec) == nullptr);
    } TEST_END();

    WORKER_TEST("AI3.15 What The Level Changes In The Economy: Easy Sends The Ants To The Nearest Pile First And At Most 4 To A Pile; Medium And Hard Rank The Piles By Points Per Trip (A Far Pile Of 4 Times The Value Wins) And Fill A Pile Up To Its Cap Of Trip / 90 + 2 Ants Before The Next One") {
        const auto first_orders = [&](Level level, int32_t value_far, size_t ants, int32_t* trip_near) {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, static_cast<uint32_t>(ants));
            const int32_t near_pile = pile(sim, 12, 12, 40, 25);
            const int32_t far_pile = pile(sim, 24, 24, 40, static_cast<uint16_t>(value_far));
            const MapInfo map(sim);
            if (trip_near != nullptr) *trip_near = map.trip_ticks(0, static_cast<uint32_t>(near_pile));
            const BotView view = BotView::build(sim, 0, &map);
            AntLedger ledger;
            Orders orders;
            const Profile profile = profile_for(level);
            TaskContext context{view, orders, ledger, profile, map, 0};
            HarvestTask harvest(1);
            harvest.step(context);
            // (the click tile of the near pile first, then the far one's, with the number of ants of each order)
            std::vector<std::pair<int32_t, size_t>> out;
            for (const Intent& in : orders.intents()) {
                const bool near = chebyshev(TileCoord{in.command.tile_x, in.command.tile_y}, TileCoord{12, 12}) <= 3;
                out.emplace_back(near ? 0 : 1, in.command.ants.size());
            }
            (void)far_pile;
            return out;
        };
        // Easy: nearest first, at most 4 to a pile, whatever the far pile is worth
        for (const int32_t value_far : {25, 100}) {
            const auto easy = first_orders(Level::Easy, value_far, 6, nullptr);
            ASSERT_TRUE(easy.size() == 2 && easy[0].first == 0 && easy[0].second == 4 && easy[1].first == 1 && easy[1].second == 2);
        }
        // Medium and Hard: a far pile worth 4 times as much beats the near one by points per trip, and has room for all six ants; for the same value the near one wins
        for (const Level level : {Level::Medium, Level::Hard}) {
            const auto rich = first_orders(level, 100, 6, nullptr);
            ASSERT_TRUE(rich.size() == 1 && rich[0].first == 1 && rich[0].second == 6);
            int32_t trip = 0;
            const auto same = first_orders(level, 25, 7, &trip);
            const size_t cap = static_cast<size_t>(std::min<int32_t>(static_cast<int32_t>(profile_for(level).max_ants_per_pile), trip / 90 + 2));
            ASSERT_TRUE(cap >= 2 && cap < 7);
            ASSERT_TRUE(same.size() == 2 && same[0].first == 0 && same[0].second == cap && same[1].first == 1 && same[1].second == 7 - cap);       // the near pile is full at its cap, the rest go on
        }
    } TEST_END();

    WORKER_TEST("AI3.16 Whose Fault Is A Failed Order: An Ant That Is Shut In Is Kept Away From The Pile, Not The Pile From Everybody; A Lone Ant Of A Seat That Gets Nowhere Blames The Pile; A \"Can't Go\" On The Way Is No Failure Of The Pile; An Ant That Moved Is No Failure Of The Pile") {
        // the nook: one worker of seat 0 is shut into a walled square whose 1-wide door is blocked by a parked ant of an idle seat; two healthy workers outside; four piles of 14 units. Without the
        // attribution the shut-in ant fails on pile after pile and every pile it fails on is blacklisted for the whole seat: 22 failures, 900 points instead of the 1050 that the two healthy ants make.
        size_t nook_orders = 0;
        bool best_pile_blacklisted = false;
        const auto nook = [&](int32_t shut_in, uint32_t* learned, uint32_t* ant_blamed) {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            ring(sim, 18, 18, 24, 24, 21, 18);
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8, 12});
            sim.spawn_unit(0, sim::AntType::Worker, TileCoord{9, 12});
            for (int32_t i = 0; i < shut_in; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20 + 2 * i, 21});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 18});                                       // parked in the door
            const int32_t best = pile(sim, 14, 8, 14, 25);
            pile(sim, 24, 12, 14, 25);
            pile(sim, 12, 16, 14, 25);
            pile(sim, 30, 30, 14, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            std::string why;
            if (!c.add(spec_of(0, "worker", Level::Medium), sink, why) || !c.add(spec_of(1, "idle", Level::Medium), sink, why)) return -1;
            step_late(sim, c, sink, 130);                                                                     // the first order took the healthy ants and the shut-in ones to the best pile together
            best_pile_blacklisted = worker_of(c, 0)->harvest().blacklisted(static_cast<uint32_t>(best), sim.current_tick());
            if (shut_in > 0 && worker_of(c, 0)->harvest().ant_failures() < static_cast<uint32_t>(shut_in)) return -2;
            step_late(sim, c, sink, 7200 - 130);
            *learned = worker_of(c, 0)->harvest().failures();
            *ant_blamed = worker_of(c, 0)->harvest().ant_failures();
            nook_orders = sink.count(CommandType::GroupMove);
            return score_of(sim, 0);
        };
        uint32_t learned = 0;
        uint32_t blamed = 0;
        const int32_t without = nook(0, &learned, &blamed);
        ASSERT_TRUE(without >= 1000 && learned == 0u && blamed == 0u);                                        // the two healthy ants alone
        for (const int32_t shut_in : {1, 2}) {
            const int32_t with = nook(shut_in, &learned, &blamed);
            ASSERT_TRUE(with > 0);                                                                            // (-1: no bot, -2: the shut-in ants were not held responsible at the first look)
            ASSERT_FALSE(best_pile_blacklisted);                                                              // the pile that the healthy ants work is not given up on the word of the ants that are shut in
            ASSERT_TRUE(learned <= 1u);                                                                       // not 22: the shut-in ants do not blacklist the piles that the others work
            ASSERT_TRUE(blamed >= static_cast<uint32_t>(shut_in));                                            // they were held responsible
            ASSERT_TRUE(with >= without);                                                                     // and they cost the others nothing
            ASSERT_TRUE(nook_orders <= 60u);                                                                  // and they are kept away from the pile they failed on: a few orders in 900 ticks, not one at every look
        }
        // the FIRST failure of an ant that is shut in is on a pile that nobody works: Easy takes at most 4 ants to a pile, so of five ants at the first look the four healthy ones go to pile A
        // and the shut-in one (the last by id) to pile B; the seat is walking to A by the time the order is seen to have done nothing, and one ant alone is no witness against B: the ant is
        // kept away from B, B stays as it is
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            ring(sim, 18, 18, 24, 24, 21, 18);
            for (int32_t i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            const uint32_t shut = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 21});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 18});                                       // parked in the door
            const int32_t a = pile(sim, 14, 8, 200, 25);
            const int32_t b = pile(sim, 24, 12, 40, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Easy), sink, why) && c.add(spec_of(1, "idle", Level::Easy), sink, why));
            step_late(sim, c, sink, 300);
            const TileCoord b_click = c.map().piles()[static_cast<size_t>(b)].approach[0].click;
            ASSERT_TRUE(!sink.log.empty() && sink.log.size() >= 2u);
            ASSERT_TRUE(names(sink.log[1].second, shut) && sink.log[1].second.tile_x == b_click.x && sink.log[1].second.tile_y == b_click.y);          // the premise: the shut-in ant was sent to B, alone
            ASSERT_TRUE(units_left(sim, a) < 200u && units_left(sim, b) == 40u);
            ASSERT_EQ(worker_of(c, 0)->harvest().failures(), 0u);                                             // one ant is no witness: B is not blacklisted
            ASSERT_FALSE(worker_of(c, 0)->harvest().blacklisted(static_cast<uint32_t>(b), sim.current_tick()));
            ASSERT_TRUE(worker_of(c, 0)->harvest().ant_failures() >= 1u);
            ASSERT_TRUE(worker_of(c, 0)->harvest().excluded(shut, static_cast<uint32_t>(b), 300u));         // the ant is kept away from B
        }
        // Easy never sees the "can't go" clip (it looks every 100 ticks, the clip lasts 8): a shut-in ant that is sent to the pile that its three healthy mates work (they have taken units from it
        // when the 24 ticks are over) is found by the idle-at-origin rule alone, and held responsible: it is kept away from the pile and not ordered at every look
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            ring(sim, 18, 18, 24, 24, 21, 18);
            for (int32_t i = 0; i < 3; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            const uint32_t shut = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{20, 21});
            sim.spawn_unit(1, sim::AntType::Worker, TileCoord{21, 18});                                       // parked in the door
            const int32_t a = pile(sim, 14, 8, 200, 25);
            pile(sim, 24, 12, 200, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Easy), sink, why) && c.add(spec_of(1, "idle", Level::Easy), sink, why));
            step_late(sim, c, sink, 700);
            const TileCoord a_click = c.map().piles()[static_cast<size_t>(a)].approach[0].click;
            ASSERT_TRUE(names(sink.log[0].second, shut) && sink.log[0].second.tile_x == a_click.x && sink.log[0].second.tile_y == a_click.y);       // the premise: sent to A with the others
            size_t orders_of_it = 0;
            for (const auto& e : sink.log) orders_of_it += names(e.second, shut) ? 1u : 0u;
            ASSERT_TRUE(orders_of_it <= 4u);                                                                  // it was not sent at every look (7 looks in 700 ticks)
            ASSERT_TRUE(worker_of(c, 0)->harvest().excluded(shut, static_cast<uint32_t>(a), 300u));
            ASSERT_EQ(worker_of(c, 0)->harvest().failures(), 0u);                                             // and A was never given up
        }
        // somebody else takes units from the pile: four ants that were ordered to pile P stand on (nobody applied the order), another hand takes 3 units from P, and two more ants work pile Q
        // (their order was applied): none of the seat's ants works P, but P gives food, so the four are held responsible and P is not given up
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 6, true);
            const int32_t p = pile(sim, 12, 12, 40, 25);
            const int32_t q = pile(sim, 28, 28, 40, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Easy);                                                 // at most 4 ants to a pile: 4 to the nearer one, 2 to the other
            HarvestTask harvest(1);
            const auto look = [&](Orders& o) {
                const BotView v = BotView::build(sim, 0, &map);
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            Orders first;
            look(first);
            ASSERT_EQ(first.intents().size(), 2u);
            const TileCoord p_click = map.piles()[static_cast<size_t>(p)].approach[0].click;
            for (const Intent& in : first.intents()) {
                Command c = in.command;
                c.issuer = 0;
                const bool to_p = c.tile_x == p_click.x && c.tile_y == p_click.y;
                harvest.on_command(c, Bot::Fate::Sent, 0);
                if (!to_p) sim.apply_command(c);                                                              // the ants of Q walk, the four of P stand
            }
            ASSERT_TRUE(map.piles()[static_cast<size_t>(q)].approach[0].reachable());
            bool changed = false;
            sim.grid_mut().take_food(p, 3, changed);                                                          // a stranger bites at P
            for (int i = 0; i < 30; ++i) sim.tick();
            Orders second;
            look(second);
            ASSERT_EQ(harvest.failures(), 0u);                                                                // P is not blacklisted (two witnesses would have done it without the unit)
            ASSERT_TRUE(harvest.ant_failures() >= 2u);
            ASSERT_FALSE(harvest.blacklisted(static_cast<uint32_t>(p), sim.current_tick()));
        }
        // two late arrivals that cannot reach pile B (a wall that came up after the analysis of the map; the seat works at pile A, which is full): two ants that fail nowhere else both failed
        // on B, so it is the pile's fault: B is blacklisted for everybody, though the seat makes progress
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            for (int32_t i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{8 + i, 12});
            const int32_t a = pile(sim, 14, 8, 200, 25);
            const int32_t b = pile(sim, 24, 12, 40, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Easy), sink, why));
            ring(sim, 21, 9, 27, 15);                                                                         // closes B in after the analysis was made
            step_late(sim, c, sink, 300);
            std::vector<uint32_t> late;                                                                       // three arrive: the first takes the last place at A, the other two are sent to B in one order
            for (int32_t i = 0; i < 3; ++i) late.push_back(sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30 + 2 * i, 40}));
            step_late(sim, c, sink, 400);
            const TileCoord b_click = c.map().piles()[static_cast<size_t>(b)].approach[0].click;
            size_t two_to_b = 0;
            for (const auto& e : sink.log) {
                size_t named = 0;
                for (const uint32_t id : late) named += names(e.second, id) ? 1u : 0u;
                two_to_b += (named >= 2u && e.second.tile_x == b_click.x && e.second.tile_y == b_click.y) ? 1u : 0u;
            }
            ASSERT_TRUE(two_to_b >= 1u);                                                                      // the premise: two ants went to B in one order
            ASSERT_TRUE(units_left(sim, a) < 200u && units_left(sim, b) == 40u);                              // the seat works at A, nobody got to B
            ASSERT_EQ(worker_of(c, 0)->harvest().failures(), 1u);
            ASSERT_TRUE(worker_of(c, 0)->harvest().blacklisted(static_cast<uint32_t>(b), sim.current_tick()));
            // ... and an ant that has not failed anywhere is not sent there while it is blacklisted (the ants that failed are kept away by their own exclusion; this one only by the blacklist)
            const uint32_t fresh = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{34, 40});
            step_late(sim, c, sink, 300);
            size_t fresh_orders = 0;
            size_t fresh_to_b = 0;
            for (const auto& e : sink.log) {
                if (!names(e.second, fresh)) continue;
                ++fresh_orders;
                fresh_to_b += (e.second.tile_x == b_click.x && e.second.tile_y == b_click.y) ? 1u : 0u;
            }
            ASSERT_TRUE(fresh_orders >= 1u && fresh_to_b == 0u);
        }
        // a lone worker whose only near pile is walled in: nobody works anywhere, so the pile is the likelier cause: it is blacklisted at once and the ant goes to the other pile
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 1);
            const int32_t a = pile(sim, 14, 8, 30, 25);
            const int32_t b = pile(sim, 24, 14, 6, 25);
            RecordingSink sink(sim, true);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Hard), sink, why));
            ring(sim, 11, 5, 17, 11);                                                                         // the wall closes after the analysis was made
            step_all(sim, c, 200);
            const TileCoord b_click = c.map().piles()[static_cast<size_t>(b)].approach[0].click;
            ASSERT_EQ(worker_of(c, 0)->harvest().failures(), 1u);
            ASSERT_TRUE(worker_of(c, 0)->harvest().blacklisted(static_cast<uint32_t>(a), sim.current_tick()));
            ASSERT_EQ(worker_of(c, 0)->harvest().ant_failures(), 0u);
            size_t to_b = 0;
            for (const auto& e : sink.log) to_b += (e.second.type == CommandType::GroupMove && e.second.tile_x == b_click.x && e.second.tile_y == b_click.y) ? 1u : 0u;
            ASSERT_EQ(to_b, 1u);                                                                              // the same ant, at once, to the other pile
        }
        // a "can't go" in the middle of the walk is a traffic jam, not a failure of the pile: an ant that walks towards the pile (it is 2 tiles from where it stood, within the 24 ticks
        // after the release in which the reaction still counts) is refused by a hand that sends it to a tile that nobody can reach
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t a = pile(sim, 14, 8, 60, 25);
            ring(sim, 40, 20, 44, 24);                                                                        // the inside (42, 22) cannot be reached
            Driver d(sim, 0, profile_for(Level::Hard), std::make_unique<WorkerBot>());
            const uint32_t ant = ants_of(sim, 0)[0];
            bool refused = false;
            uint64_t refused_at = 0;
            for (uint64_t t = 0; t < 400 && !refused; ++t) {
                d.tick();
                const sim::AntSnapshot* s = snap(sim, ant);
                if (!d.sent.empty() && s != nullptr && chebyshev(TileCoord{s->tile_x, s->tile_y}, TileCoord{5, 10}) >= 2 && s->state == sim::UnitState::Walking && !holds(sim, ant)) {
                    sim.issue_move_order(ant, TileCoord{42, 22});
                    refused = true;
                    refused_at = sim.current_tick();
                }
            }
            ASSERT_TRUE(refused);
            bool saw_cant_go_in_time = false;
            for (uint64_t t = 0; t < 60; ++t) {
                d.tick();
                const sim::AntSnapshot* s = snap(sim, ant);
                const uint64_t sent = d.sent.front().first;
                saw_cant_go_in_time = saw_cant_go_in_time || (s != nullptr && s->state == sim::UnitState::CantGo && sim.current_tick() >= sent + 8 && sim.current_tick() <= sent + 24 && chebyshev(TileCoord{s->tile_x, s->tile_y}, TileCoord{5, 10}) >= 2);
            }
            ASSERT_TRUE(saw_cant_go_in_time && refused_at <= d.sent.front().first + 24);                       // the reaction did come while it counts, away from where the ant was ordered
            ASSERT_EQ(d.worker()->harvest().failures(), 0u);
            ASSERT_EQ(d.worker()->harvest().ant_failures(), 0u);
            ASSERT_FALSE(d.worker()->harvest().blacklisted(static_cast<uint32_t>(a), sim.current_tick()));
        }
        // idle where it was ordered, within one tile, is a failure (the order did nothing); two tiles away it is not (the ant did something: another hand moved it)
        for (const int32_t shift : {1, 2}) {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 1);
            const int32_t a = pile(sim, 12, 12, 30, 25);
            const MapInfo map(sim);
            AntLedger ledger;
            const Profile profile = profile_for(Level::Medium);
            HarvestTask harvest(1);
            const auto look = [&](Orders& o) {
                const BotView v = BotView::build(sim, 0, &map);
                TaskContext context{v, o, ledger, profile, map, 0};
                harvest.step(context);
            };
            Orders first;
            look(first);
            ASSERT_TRUE(first.intents().size() == 1 && first.intents()[0].command.ants.size() == 1);
            const uint32_t ant = first.intents()[0].command.ants[0];
            harvest.on_command(first.intents()[0].command, Bot::Fate::Sent, 0);                               // nobody applies it ...
            const sim::AntSnapshot* s0 = snap(sim, ant);
            ASSERT_TRUE(s0 != nullptr);
            const TileCoord origin{s0->tile_x, s0->tile_y};                                                    // (a snapshot is a view into a cache that every tick rewrites: copy what is needed)
            sim.issue_move_order(ant, TileCoord{origin.x + shift, origin.y});                                  // ... another hand moves the ant `shift` tiles
            for (int i = 0; i < 40; ++i) sim.tick();
            const sim::AntSnapshot* s1 = snap(sim, ant);
            ASSERT_TRUE(s1 != nullptr && s1->state == sim::UnitState::Idle && chebyshev(TileCoord{s1->tile_x, s1->tile_y}, origin) == shift);
            Orders later;
            look(later);
            ASSERT_EQ(harvest.failures(), shift == 1 ? 1u : 0u);
            ASSERT_EQ(harvest.blacklisted(static_cast<uint32_t>(a), sim.current_tick()), shift == 1);
        }
    } TEST_END();

    WORKER_TEST("AI3.17 The Map As It Is Now: A Pile That Was Shut Off At The Start Is Worked When The Wall Opens Or The Food That Plugged The Way Is Eaten; An Ant That The Start Analysis Took For Shut In Is Sent When It Is Free") {
        // a wall that opens: pile W stands in a closed ring (unreachable at the start), a small pile keeps the ants busy; at tick 500 a gap is opened
        {
            sim::SimulationEngine sim;
            world(sim);
            const int32_t w = pile(sim, 28, 28, 60, 25);
            const int32_t small = pile(sim, 12, 12, 4, 25);
            ring(sim, 25, 25, 31, 31);
            RecordingSink sink(sim, true);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Medium), sink, why));
            ASSERT_FALSE(c.map().piles()[static_cast<size_t>(w)].approach[0].reachable());
            step_all(sim, c, 500);
            ASSERT_EQ(units_left(sim, w), 60u);
            ASSERT_TRUE(units_left(sim, small) < 4u);
            sim.set_terrain(28, 25, sim::TERRAIN_WALKABLE);                                                   // the gap
            step_all(sim, c, 700);                                                                            // within the re-ask (200 ticks), a look and a reaction time ...
            ASSERT_TRUE(units_left(sim, w) < 60u);                                                            // ... the ants are at work on it
            size_t to_w = 0;
            for (const auto& e : sink.log) to_w += (e.second.type == CommandType::GroupMove && chebyshev(TileCoord{e.second.tile_x, e.second.tile_y}, TileCoord{28, 28}) <= 3) ? 1u : 0u;
            ASSERT_TRUE(to_w >= 1u);
            for (const auto& e : sink.log) ASSERT_TRUE(!(e.first < 500 && chebyshev(TileCoord{e.second.tile_x, e.second.tile_y}, TileCoord{28, 28}) <= 3));      // nothing was sent into the closed ring
            step_all(sim, c, 3000);
            ASSERT_TRUE(score_of(sim, 0) >= 300);
        }
        // food that plugs the way: a 4 x 4 pile (8 units) closes the mouth of a corridor, pile B (60 units) lies behind it; B is unreachable at the start and reachable when A is eaten
        {
            sim::SimulationEngine sim;
            world(sim);
            for (int32_t x = 18; x <= 34; ++x) {
                for (int32_t y = 15; y <= 19; ++y) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
                for (int32_t y = 23; y <= 27; ++y) sim.set_terrain(x, y, sim::TERRAIN_OBSTACLE);
            }
            for (int32_t y = 20; y <= 22; ++y) sim.set_terrain(34, y, sim::TERRAIN_OBSTACLE);
            const int32_t a = big_pile(sim, 22, 21, 8, 25);
            const int32_t b = pile(sim, 30, 21, 60, 25);
            pile(sim, 12, 12, 6, 25);
            RecordingSink sink(sim, true);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Medium), sink, why));
            ASSERT_TRUE(c.map().piles()[static_cast<size_t>(a)].approach[0].reachable() && !c.map().piles()[static_cast<size_t>(b)].approach[0].reachable());
            step_all(sim, c, 6000);
            ASSERT_EQ(units_left(sim, a), 0u);
            ASSERT_TRUE(units_left(sim, b) < 60u);                                                            // 60 units after 6,000 ticks when it was never asked again
            ASSERT_TRUE(score_of(sim, 0) >= 500);
        }
        // an ant that is shut in by food or a wall at the start (its component is not the hill's) is sent when the way is free: here the ring around it opens
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 0);
            pile(sim, 12, 12, 30, 25);
            ring(sim, 28, 38, 32, 42);
            const uint32_t shut = sim.spawn_unit(0, sim::AntType::Worker, TileCoord{30, 40});
            RecordingSink sink(sim, true);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Medium), sink, why));
            ASSERT_TRUE(c.map().ant_component(0, TileCoord{30, 40}) != c.map().hill_component(0));
            step_all(sim, c, 600);
            ASSERT_TRUE(sink.log.empty() && worker_of(c, 0)->harvest().unplaced() == 1u);                     // stranded: never sent
            sim.set_terrain(30, 38, sim::TERRAIN_WALKABLE);                                                   // the way out
            step_all(sim, c, 600);
            size_t ordered = 0;
            for (const auto& e : sink.log) ordered += names(e.second, shut) ? 1u : 0u;
            ASSERT_TRUE(ordered >= 1u);                                                                       // it is free: sent to the pile like any other
            ASSERT_TRUE(units_left(sim, 0) < 30u);                                                            // and the pile is being worked
        }
    } TEST_END();

    WORKER_TEST("AI3.18 Packed Ants: 24 Workers On Adjacent Tiles Do Not Jam The Engine's Path Manager (At Most 8 Ants Newly Ordered At One Look); A Jam That Comes Anyway Is Repaired By The Watchdog (A Stop, Then Smaller Groups)") {
        // the engine: one group order of 24 packed workers keeps 15 path requests pending for the rest of the match (no bot involved)
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 24, true);
            pile(sim, 14, 8, 200, 25);
            Command all;
            all.type = CommandType::GroupMove;
            all.issuer = 0;
            all.tile_x = 13;
            all.tile_y = 7;
            all.ants = ants_of(sim, 0);
            ASSERT_EQ(all.ants.size(), 24u);
            ASSERT_EQ(sim.apply_command(all).status, sim::CommandResult::Status::Applied);
            for (int i = 0; i < 600; ++i) sim.tick();
            size_t pending = 0;
            for (const uint32_t id : all.ants) pending += sim.has_pending_path(id) ? 1u : 0u;
            ASSERT_TRUE(pending >= 8u);                                                                       // the premise: the jam is the engine's
        }
        // the worker: groups of at most 8, and at most 8 ants newly ordered at one look: no jam, no stop, work from the start
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 24, true);
            pile(sim, 14, 8, 200, 25);
            pile(sim, 24, 14, 200, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Medium), sink, why));
            step_late(sim, c, sink, 7200);
            size_t most = 0;
            for (const auto& e : sink.log) most = std::max(most, e.second.ants.size());
            ASSERT_TRUE(most >= 2u && most <= 8u);
            size_t first_look = 0;                                                                            // the ants of the commands that left together with the first one: one look
            for (const auto& e : sink.log) first_look += e.first <= sink.log.front().first + 10 ? e.second.ants.size() : 0u;
            ASSERT_EQ(first_look, 8u);                                                                        // 8 of the 24, not 16 or 24: the rest of the pool waits for the next looks
            ASSERT_TRUE(worker_of(c, 0)->harvest().stalls() <= 3u);                                           // (an ant that a packed neighbour boxes in may stand for 300 ticks: the watchdog stops it; 24 of them is the jam)
            ASSERT_TRUE(score_of(sim, 0) >= 1000);                                                            // 0 for the whole match with one order of 24 ants
            size_t pending = 0;
            for (const uint32_t id : ants_of(sim, 0)) pending += sim.has_pending_path(id) ? 1u : 0u;
            ASSERT_TRUE(pending <= 2u);
        }
        // a bot that sends all 24 in one order (a limit of 24 ants per look) jams the engine; the watchdog finds the ants that walk on one tile for stall_ticks, stops them, and orders them
        // again in smaller groups: the economy comes back (0 points without it)
        {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, 24, true);
            pile(sim, 14, 8, 200, 25);
            pile(sim, 24, 14, 200, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            HarvestTask::Params params;
            params.max_ants_per_look = 24;
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Medium), std::make_unique<ParamBot>(params), sink, why));
            step_late(sim, c, sink, 7200);
            const ParamBot* bot = dynamic_cast<const ParamBot*>(c.bot(0));
            ASSERT_TRUE(bot != nullptr && bot->task().stalls() >= 8u);
            ASSERT_TRUE(score_of(sim, 0) >= 800);
            size_t most_after = 0;
            for (const auto& e : sink.log) {
                if (e.first > 1200) most_after = std::max(most_after, e.second.ants.size());
            }
            ASSERT_TRUE(most_after <= 12u);                                                                   // the groups after the jam are smaller than the first one (24)
            ASSERT_TRUE(sink.count(CommandType::Stop) >= 1u);
            uint64_t first_stop = 0;
            for (const auto& e : sink.log) {
                if (e.second.type == CommandType::Stop) {
                    first_stop = e.first;
                    break;
                }
            }
            size_t most_at_a_look = 0;                                                                        // ants ordered together (within 15 ticks) after the first Stop: halved, 12 at the most
            for (const auto& e : sink.log) {
                if (e.second.type != CommandType::GroupMove || e.first <= first_stop) continue;
                size_t together = 0;
                for (const auto& f : sink.log) together += (f.second.type == CommandType::GroupMove && f.first >= e.first && f.first < e.first + 15) ? f.second.ants.size() : 0u;
                most_at_a_look = std::max(most_at_a_look, together);
            }
            ASSERT_TRUE(first_stop > 0 && most_at_a_look >= 2u && most_at_a_look <= 12u);
        }
    } TEST_END();

    WORKER_TEST("AI3.20 A Carrier On Its Way Home Is Never Stopped By The Watchdog (It Would Stand With Its Food And Lose Its Place In The Queue)") {
        sim::SimulationEngine sim;
        world(sim, 14400, 1, 3, true);
        pile(sim, 30, 30, 60, 25);
        const MapInfo map(sim);
        AntLedger ledger;
        const Profile profile = profile_for(Level::Medium);
        HarvestTask::Params params;
        params.stall_ticks = 0;                                                                               // two looks on one tick find any walking ant standing: only the rule decides who is stopped
        HarvestTask harvest(1, params);
        const auto look = [&](Orders& o) {
            const BotView v = BotView::build(sim, 0, &map);
            TaskContext context{v, o, ledger, profile, map, 0};
            harvest.step(context);
        };
        Orders first;
        look(first);
        ASSERT_EQ(first.intents().size(), 1u);
        Command order = first.intents()[0].command;
        order.issuer = 0;
        harvest.on_command(order, Bot::Fate::Sent, 0);
        ASSERT_EQ(sim.apply_command(order).status, sim::CommandResult::Status::Applied);
        for (int i = 0; i < 30; ++i) sim.tick();
        const std::vector<uint32_t> ants = ants_of(sim, 0);
        ASSERT_EQ(ants.size(), 3u);
        sim.get_unit(ants[2]).pick_up_food(1, 25);                                                            // the third ant now carries food (a bite that the test hands it) while its record lives
        sim.tick();                                                                                           // (the world state that the view copies is rebuilt after a tick)
        ASSERT_TRUE(snap(sim, ants[2])->is_holding);
        for (const uint32_t id : ants) ASSERT_TRUE(snap(sim, id)->state == sim::UnitState::Walking);
        Orders a;
        look(a);                                                                                              // the watchdog notes the tiles
        Orders b;
        look(b);                                                                                              // and finds them unchanged
        ASSERT_EQ(b.intents().size(), 1u);
        ASSERT_TRUE(b.intents()[0].command.type == CommandType::Stop);
        ASSERT_TRUE(names(b.intents()[0].command, ants[0]) && names(b.intents()[0].command, ants[1]));
        ASSERT_FALSE(names(b.intents()[0].command, ants[2]));                                                 // the carrier is not among them
        ASSERT_EQ(harvest.stalls(), 2u);
    } TEST_END();

    WORKER_TEST("AI3.21 After A Jam The Watchdog Halves The Ants That Are Newly Ordered At One Look (8 Become 4), And The Default Task Orders 8 At A Look") {
        sim::SimulationEngine sim;
        world(sim, 14400, 1, 24, true);
        pile(sim, 30, 30, 200, 25);
        const MapInfo map(sim);
        AntLedger ledger;
        const Profile profile = profile_for(Level::Medium);
        HarvestTask::Params params;
        params.stall_ticks = 0;                                                                               // two looks on one tick find an ant that walks "stalled": the test decides when
        HarvestTask harvest(1, params);
        HarvestTask plain(2);                                                                                 // the constructor without parameters takes the defaults of Params too
        const auto look = [&](HarvestTask& task, AntLedger& l) {
            const BotView v = BotView::build(sim, 0, &map);
            Orders o;
            TaskContext context{v, o, l, profile, map, 0};
            task.step(context);
            size_t ordered = 0;
            for (const Intent& in : o.intents()) ordered += in.command.type == CommandType::GroupMove ? in.command.ants.size() : 0u;
            return std::make_pair(ordered, o);
        };
        AntLedger plain_ledger;
        ASSERT_EQ(look(plain, plain_ledger).first, static_cast<size_t>(HarvestTask::Params{}.max_ants_per_look));
        Orders first = look(harvest, ledger).second;
        ASSERT_EQ(first.intents().size(), 1u);
        Command order = first.intents()[0].command;
        order.issuer = 0;
        ASSERT_EQ(order.ants.size(), 8u);
        harvest.on_command(order, Bot::Fate::Sent, 0);
        ASSERT_EQ(sim.apply_command(order).status, sim::CommandResult::Status::Applied);
        for (int i = 0; i < 30; ++i) sim.tick();
        const size_t second = look(harvest, ledger).first;                                                    // the next 8 of the pool; the first 8 are noted at their tiles
        ASSERT_EQ(second, 8u);
        const auto third = look(harvest, ledger);                                                             // the first 8 have not moved: stopped, and the pace is halved for the pool
        ASSERT_EQ(harvest.stalls(), 8u);
        ASSERT_EQ(third.first, 4u);                                                                           // 4 newly ordered, not 8
    } TEST_END();

    WORKER_TEST("AI3.19 The Deep Queue: With 16, 20 Or 24 Workers On One Hill (The Queue Reaches Past The Ring Of 4 Tiles) The Rescue Leaves The Ants That Wait Their Turn Alone: No Rescue, The Whole Economy Works") {
        for (const uint32_t workers : {16u, 20u, 24u}) {
            sim::SimulationEngine sim;
            world(sim, 14400, 1, workers, true);
            pile(sim, 14, 8, 200, 25);
            pile(sim, 24, 14, 200, 25);
            LatencySink sink(sim, 3);
            BotController c(sim, 1);
            std::string why;
            ASSERT_TRUE(c.add(spec_of(0, "worker", Level::Medium), sink, why));
            step_late(sim, c, sink, 7200);
            ASSERT_EQ(worker_of(c, 0)->harvest().rescues(), 0u);                                              // 28 with 20 workers when a carrier more than 4 tiles from the mound counted as stuck
            ASSERT_TRUE(score_of(sim, 0) >= 1100);
            for (const auto& e : sink.log) ASSERT_TRUE(e.second.type == CommandType::GroupMove ? e.second.ants.size() <= 8u : e.second.type == CommandType::Stop);      // (a Stop: the watchdog found a jam)
        }
    } TEST_END();
}
