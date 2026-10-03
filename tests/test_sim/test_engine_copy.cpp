// Tests of the value semantics of SimulationEngine: the deep copy on which the client-side prediction of one's own orders rests (docs/NETWORK_PORT.md, "Prediction of
// one's own orders"). A copy of an engine is a second engine in exactly the same state; the two share nothing, and given the same commands they stay identical.
//
// What "identical" means here is everything that anybody can observe, not only the gameplay state that state_hash() covers: the hash (all seven parts) at every tick, the
// answer of every command, the cues and news of every tick, and the whole WorldState (cells, ants, effects, score bubbles, droppers, statistics, fog, the match result).
// The hash does not cover the half-done path searches (only the queue of requests), the presentation or the fog, and the world state does not cover the A* open lists, so the
// tests look at all of them: a copy that lost an open list gives a different path some ticks later, and the lock-step runs below find it.
//
// "Independent" is measured against a TWIN: an engine that was never copied and that has played the same match from the start (the replay is deterministic). A copy or a
// source that has been worked on in every way must leave the other exactly as the twin is.
//
//   ./test_engine_copy                  the quick tests (seconds)
//   ./test_engine_copy --whole-matches  whole matches on the six shipped maps, a copy re-taken every 1000 ticks, the hash compared at every tick (about a minute)
//   ./test_engine_copy --all            both
//   ./test_engine_copy --bench          the cost of a copy, a tick, the hash and the world state on the six shipped maps (prints, asserts nothing)
//   ./test_engine_copy --census         what the generated whole matches contain: hatching, fights, scores, cues (prints, asserts nothing)
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/path_planner.hpp"
#include "ants_sim/sim_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace ants::sim;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

// An instrumented build (ASan / UBSan) runs five to ten times slower: the whole-match tier then plays the first part of each match only.
#if defined(__SANITIZE_ADDRESS__)
#define ANTS_COPY_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define ANTS_COPY_TEST_SANITIZED 1
#endif
#endif
#ifndef ANTS_COPY_TEST_SANITIZED
#define ANTS_COPY_TEST_SANITIZED 0
#endif

static int g_test_count = 0;
static int g_test_failures = 0;
static int g_assert_count = 0;

inline void run_test_case(const std::string& name, const std::function<void()>& fn) {
    ++g_test_count;
    std::cout << "  RUNNING: " << std::left << std::setw(112) << name << " ... " << std::flush;
    const int prev = g_test_failures;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        fn();
    } catch (const std::exception& e) {
        std::cout << "FAILED! Exception: " << e.what() << "\n";
        ++g_test_failures;
        return;
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (g_test_failures == prev) std::cout << "PASS (" << std::fixed << std::setprecision(1) << s << " s)\n";
}

#define TEST_CASE(name) run_test_case(name, [&]()
#define TEST_END() );
#define ASSERT_TRUE(cond) \
    do { \
        ++g_assert_count; \
        if (!(cond)) { \
            std::cout << "FAILED!\n    Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))
// A comparison function answers "" when the two engines agree and otherwise says what differs; the message is printed with the failure.
#define ASSERT_NO_DIFF(diff_expr) \
    do { \
        ++g_assert_count; \
        const std::string diff_ = (diff_expr); \
        if (!diff_.empty()) { \
            std::cout << "FAILED!\n    " << diff_ << "\n    at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_test_failures; \
            return; \
        } \
    } while (0)

namespace {

using Status = CommandResult::Status;
using Clock = std::chrono::steady_clock;

constexpr const char* kMaps[] = {"TINY.LVL", "SMALL.LVL", "MEDIUM.LVL", "ISLANDS.LVL", "GAUNTLET.LVL", "TREASURE.LVL"};

// A tiny deterministic generator (the tests must not depend on the library's PRNG)
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

const ants::assets::LevelData& level(const std::string& file) {
    static std::map<std::string, ants::assets::LevelData> cache;
    auto it = cache.find(file);
    if (it == cache.end()) {
        ants::assets::LevelData lvl;
        if (!lvl.load_lvl(std::string(ORIGINAL_ASSETS_DIR) + "/Maps/" + file)) {
            std::cout << "cannot load the map " << file << " from " << ORIGINAL_ASSETS_DIR << "\n";
            std::exit(2);
        }
        it = cache.emplace(file, std::move(lvl)).first;
    }
    return it->second;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Fingerprints of everything that can be observed
// ---------------------------------------------------------------------------------------------------------------------------------

class Fnv {
public:
    void u8(uint8_t v) noexcept {
        h_ ^= v;
        h_ *= 0x100000001b3ULL;
    }
    void b(bool v) noexcept { u8(v ? 1u : 0u); }
    void u16(uint16_t v) noexcept {
        u8(static_cast<uint8_t>(v & 0xFFu));
        u8(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
    void u32(uint32_t v) noexcept {
        for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void u64(uint64_t v) noexcept {
        for (int i = 0; i < 8; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void i16(int16_t v) noexcept { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) noexcept { u32(static_cast<uint32_t>(v)); }
    void str(const std::string& s) noexcept {
        u64(s.size());
        for (char c : s) u8(static_cast<uint8_t>(c));
    }
    uint64_t value() const noexcept { return h_; }

private:
    uint64_t h_{0xcbf29ce484222325ULL};
};

void fp_stats(Fnv& h, const PlayerMatchStats& s) {
    h.i32(s.score);
    h.u32(s.friendly_lost);
    h.u32(s.enemy_killed);
    h.u32(s.ants_hatched);
    h.u32(s.new_hatched);
    h.u32(s.food_deposited);
    h.u32(s.food_stolen);
    h.u32(s.food_lost);
    h.u32(s.bombs_planted);
    h.u32(s.bombs_defused);
    h.u32(s.fires_lit);
    h.u32(s.bridges_built);
}

// Every field of the WorldState, the presentation included (visual effects, score bubbles, the fog, the dust balls that are folded into the effects)
uint64_t fp_world(const WorldState& w) {
    Fnv h;
    h.u32(w.match_time_remaining_ms);
    h.u32(w.width);
    h.u32(w.height);
    h.u64(w.cells.size());
    for (const TileCell& c : w.cells) {
        h.u16(c.terrain_id);
        h.u8(c.terrain_type);
        h.u8(static_cast<uint8_t>(c.surface_type));
        h.u16(c.flags);
        h.u16(c.interactive_id);
        h.u8(c.interactive_owner);
        h.u32(c.timer_ticks);
        h.i16(c.anchor_x);
        h.i16(c.anchor_y);
        h.b(c.is_food);
        h.b(c.is_mud);
        h.b(c.is_powerup);
        h.u8(c.powerup_type);
        h.b(c.is_obstacle_overlay);
        h.b(c.is_thief_only);
        h.b(c.is_corridor_team_locked);
        h.b(c.is_base_hole);
        h.u8(c.base_owner_team);
        h.i32(c.occupant_ant_id);
        h.b(c.static_solid);
    }
    h.u64(w.ants.size());
    for (const AntSnapshot& a : w.ants) {
        h.u32(a.id);
        h.u8(a.player_id);
        h.u8(static_cast<uint8_t>(a.type));
        h.u8(static_cast<uint8_t>(a.raw_type));
        h.i32(a.px);
        h.i32(a.py);
        h.i32(a.tile_x);
        h.i32(a.tile_y);
        h.u8(a.facing);
        h.u16(a.hp);
        h.u16(a.max_hp);
        h.u16(a.anim_state);
        h.u16(a.anim_frame);
        h.b(a.is_holding);
        h.i32(a.carried_points);
        h.b(a.is_stunned);
        h.b(a.is_swimming);
        h.b(a.is_drowning);
        h.b(a.is_on_mud);
        h.i32(a.burn_elapsed_ms);
        h.b(a.frozen);
        h.u8(static_cast<uint8_t>(a.state));
        h.u8(a.action);
        h.u8(a.target_team_id);
        h.u16(a.loco_clip);
        h.u16(a.loco_frame);
        h.b(a.loco_mirrored);
        h.u16(a.loco_left_ms);
    }
    h.u64(w.effects.size());
    for (const VisualEffect& e : w.effects) {
        h.str(e.anim_name);
        h.i32(e.px);
        h.i32(e.py);
        h.u16(e.frame);
        h.u16(e.total_frames);
        h.u32(e.elapsed_ms);
        h.u32(e.duration_ms);
        h.i32(e.y_key);
        h.b(e.fog_gated);
        h.b(e.looping);
        h.u32(e.audio_owner);
    }
    h.u64(w.score_bubbles.size());
    for (const ScoreBubble& s : w.score_bubbles) {
        h.i32(s.x);
        h.i32(s.y);
        h.i32(s.amount);
        h.u32(s.elapsed_ms);
    }
    h.u64(w.flower_droppers.size());
    for (const FlowerDropperSnapshot& f : w.flower_droppers) {
        h.i32(f.x);
        h.i32(f.y);
        h.i32(f.drop_x);
        h.i32(f.drop_y);
        h.b(f.is_dropping);
        h.u32(f.drop_elapsed_ms);
        h.u8(f.powerup_type);
    }
    for (const PlayerMatchStats& s : w.player_stats) fp_stats(h, s);
    for (int32_t v : w.player_scores) h.i32(v);
    for (uint32_t v : w.player_eggs) h.u32(v);
    for (uint8_t v : w.player_alliances) h.u8(v);
    for (uint8_t v : w.pending_invite_from) h.u8(v);
    h.u64(w.anthills.size());
    for (const auto& a : w.anthills) {
        h.u16(a.tile_id);
        h.u16(a.y);
        h.u16(a.x);
        h.u8(a.team_id);
    }
    h.u64(w.plants.size());
    for (const MapPlant& p : w.plants) {
        h.u16(p.tile_id);
        h.u16(p.x);
        h.u16(p.y);
    }
    h.u8(w.dropped_mask);
    h.b(w.match_result.is_over);
    h.u64(w.match_result.winning_players.size());
    for (uint8_t v : w.match_result.winning_players) h.u8(v);
    h.u64(w.match_result.losing_players.size());
    for (uint8_t v : w.match_result.losing_players) h.u8(v);
    for (int32_t v : w.match_result.final_scores) h.i32(v);
    for (const PlayerMatchStats& s : w.match_result.stats) fp_stats(h, s);
    h.u8(w.match_result.present_mask);
    for (uint8_t v : w.match_result.ally) h.u8(v);
    h.u16(w.match_result.quitter);
    h.b(w.fog_of_war_enabled);
    h.u64(w.fog_revealed.size());
    for (uint8_t v : w.fog_revealed) h.u8(v);
    return h.value();
}

struct Events {
    uint64_t fp{0};
    size_t audio{0};
    size_t news{0};
};

// The cues and news that the engine has queued (this empties the queues)
Events poll_events(SimulationEngine& e) {
    Fnv h;
    Events out;
    const std::vector<AudioEvent> audio = e.poll_audio_events();
    h.u64(audio.size());
    for (const AudioEvent& a : audio) {
        h.u32(a.sound_id);
        h.i32(a.world_x);
        h.i32(a.world_y);
        h.u8(a.priority);
        h.u8(a.target_player);
        h.u32(a.owner);
        h.b(a.stop);
    }
    const std::vector<NewsEvent> news = e.poll_news_events();
    h.u64(news.size());
    for (const NewsEvent& n : news) {
        h.u8(n.target_player);
        h.str(n.message_text);
        h.u32(n.timestamp_ms);
        h.u16(n.string_id);
        h.b(n.blink);
        h.u8(static_cast<uint8_t>(n.channel));
    }
    out.fp = h.value();
    out.audio = audio.size();
    out.news = news.size();
    return out;
}

// The locomotion trace (empty unless a test switches it on)
uint64_t fp_trace(const SimulationEngine& e) {
    Fnv h;
    const std::vector<LocoTraceEvent>& t = e.locomotion_trace();
    h.u64(t.size());
    for (const LocoTraceEvent& x : t) {
        h.u8(static_cast<uint8_t>(x.kind));
        h.u32(x.time_ms);
        h.u32(x.ant_id);
        h.i32(x.px);
        h.i32(x.py);
        h.i32(x.dx);
        h.i32(x.dy);
        h.u16(x.clip);
        h.u16(x.frame);
        h.u8(x.action);
    }
    return h.value();
}

std::string describe_hash_difference(const StateHash& ha, const StateHash& hb, uint64_t tick_a, uint64_t tick_b) {
    std::string d;
    if (ha.engine != hb.engine) d += " engine";
    if (ha.players != hb.players) d += " players";
    if (ha.grid != hb.grid) d += " grid";
    if (ha.food != hb.food) d += " food";
    if (ha.ants != hb.ants) d += " ants";
    if (ha.paths != hb.paths) d += " paths";
    if (ha.droppers != hb.droppers) d += " droppers";
    if (d.empty()) d = " total";
    return "the state hashes differ (tick " + std::to_string(tick_a) + " vs " + std::to_string(tick_b) + ") in:" + d;
}

// "" when the gameplay state of the two engines is the same (all seven parts of the hash, the tick, the roster, the end of the match), else what differs
std::string diff_state(const SimulationEngine& a, const SimulationEngine& b) {
    const StateHash ha = a.state_hash();
    const StateHash hb = b.state_hash();
    if (ha.total != hb.total) return describe_hash_difference(ha, hb, a.current_tick(), b.current_tick());
    if (a.current_tick() != b.current_tick()) return "the ticks differ";
    if (a.roster_mask() != b.roster_mask()) return "the rosters differ";
    if (a.is_match_over() != b.is_match_over()) return "one match is over and the other is not";
    return {};
}

std::string diff_world(const SimulationEngine& a, const SimulationEngine& b) {
    if (fp_world(a.get_world_state()) != fp_world(b.get_world_state())) return "the world states differ (tick " + std::to_string(a.current_tick()) + ")";
    return {};
}

bool same_result(const CommandResult& a, const CommandResult& b) {
    return a.status == b.status && a.ack_ant == b.ack_ant && a.needing_order == b.needing_order && a.ants_ordered == b.ants_ordered && a.hatch_result == b.hatch_result;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The commands of a match: random, valid and invalid, from the state of the engine (deterministic)
// ---------------------------------------------------------------------------------------------------------------------------------

struct Mix {
    uint32_t percent_per_tick{10};   // the chance that something is ordered on a tick (a worker needs a hundred ticks for a trip to a pile and home: orders must leave it alone)
    bool alliances{true};
    bool hatch{true};
    bool quit{false};                // a Quit now and then (the team drops out, or the match ends)
};

std::vector<Command> gen_commands(const SimulationEngine& e, Lcg& rng, const Mix& mix) {
    std::vector<Command> out;
    if (e.is_match_over() || rng.below(100) >= mix.percent_per_tick) return out;
    const WorldState& w = e.get_world_state();
    const int32_t width = static_cast<int32_t>(w.width);
    const int32_t height = static_cast<int32_t>(w.height);
    // the places worth going to: food piles, power-ups (a worker that walks onto one takes it)
    std::vector<TileCoord> food;
    std::vector<TileCoord> powerups;
    for (size_t i = 0; i < w.cells.size(); ++i) {
        const TileCell& c = w.cells[i];
        if (!c.has_food() && !c.has_powerup()) continue;
        const TileCoord t{static_cast<int32_t>(i % w.width), static_cast<int32_t>(i / w.width)};
        (c.has_food() ? food : powerups).push_back(t);
    }
    const uint32_t n = 1 + rng.below(2);
    for (uint32_t k = 0; k < n; ++k) {
        uint8_t p = static_cast<uint8_t>(rng.below(MAX_PLAYERS));
        for (int tries = 0; tries < 6 && ((e.roster_mask() >> p) & 1u) == 0; ++tries) p = static_cast<uint8_t>(rng.below(MAX_PLAYERS));
        if (((e.roster_mask() >> p) & 1u) == 0) continue;
        std::vector<uint32_t> mine;
        std::vector<const AntSnapshot*> strangers;
        for (const AntSnapshot& a : w.ants) {
            if (a.player_id == p) mine.push_back(a.id);
            else strangers.push_back(&a);
        }

        Command c;
        c.issuer = p;
        // the selection: a random part of the player's ants, now and then with a stranger's ant or an id that does not exist (the engine must drop them)
        auto selection = [&]() {
            std::vector<uint32_t> sel;
            for (uint32_t id : mine) {
                if (rng.below(5) < 2) sel.push_back(id);
            }
            if (sel.empty() && !mine.empty()) sel.push_back(mine[rng.below(static_cast<uint32_t>(mine.size()))]);
            if (!strangers.empty() && rng.below(12) == 0) sel.push_back(strangers[rng.below(static_cast<uint32_t>(strangers.size()))]->id);
            if (rng.below(25) == 0 || sel.empty()) sel.push_back(90000u + rng.below(10));
            if (sel.size() > kMaxCommandAnts) sel.resize(kMaxCommandAnts);
            return sel;
        };
        // the target: anywhere, near one of the player's ants, near a hill, on a stranger's ant, on food or a power-up, or in the river of the busy world
        auto target = [&](CommandType type) {
            const uint32_t where = rng.below(100);
            int32_t x = static_cast<int32_t>(rng.below(static_cast<uint32_t>(width)));
            int32_t y = static_cast<int32_t>(rng.below(static_cast<uint32_t>(height)));
            auto around = [&](int32_t cx, int32_t cy, int32_t spread) {
                x = std::max(0, std::min(width - 1, cx + static_cast<int32_t>(rng.below(static_cast<uint32_t>(2 * spread + 1))) - spread));
                y = std::max(0, std::min(height - 1, cy + static_cast<int32_t>(rng.below(static_cast<uint32_t>(2 * spread + 1))) - spread));
            };
            if (where < 15 && !mine.empty()) {
                const uint32_t chosen = mine[rng.below(static_cast<uint32_t>(mine.size()))];
                for (const AntSnapshot& a : w.ants) {
                    if (a.id == chosen) {
                        around(a.tile_x, a.tile_y, 4);
                        break;
                    }
                }
            } else if (where < 25 && !w.anthills.empty()) {
                const auto& hill = w.anthills[rng.below(static_cast<uint32_t>(w.anthills.size()))];
                around(static_cast<int32_t>(hill.x) + 1, static_cast<int32_t>(hill.y) + 1, 2);
            } else if (where < 50 && !strangers.empty() && (type == CommandType::GroupAttack || type == CommandType::GroupMove)) {
                const AntSnapshot& s = *strangers[rng.below(static_cast<uint32_t>(strangers.size()))];
                x = s.tile_x;                                       // the ant's tile now: a click on it is an attack
                y = s.tile_y;
            } else if (where < 70 && !food.empty()) {
                const TileCoord t = food[rng.below(static_cast<uint32_t>(food.size()))];
                x = t.x;
                y = t.y;
            } else if (where < 78 && !powerups.empty()) {
                const TileCoord t = powerups[rng.below(static_cast<uint32_t>(powerups.size()))];
                x = t.x;
                y = t.y;
            } else if (type == CommandType::GroupSpecial && where < 90 && width > 30) {
                x = 30;
            }
            return std::make_pair(static_cast<int16_t>(x), static_cast<int16_t>(y));
        };

        const uint32_t kind = rng.below(100);
        if (kind < 34) {
            c.type = CommandType::GroupMove;
        } else if (kind < 52) {
            c.type = CommandType::GroupAttack;
        } else if (kind < 64) {
            c.type = CommandType::GroupSpecial;
        } else if (kind < 70) {
            c.type = CommandType::Stop;
        } else if (kind < 80) {
            c.type = mix.hatch ? CommandType::Hatch : CommandType::GroupMove;
        } else if (kind < 99) {
            if (!mix.alliances) continue;
            const uint8_t waiting = w.pending_invite_from[p];
            if (waiting != 255 && rng.below(10) < 6) {
                c.type = rng.below(2) == 0 ? CommandType::AllianceAccept : CommandType::AllianceDeny;
                c.other_player = waiting;
            } else {
                const uint32_t which = rng.below(4);
                c.type = which == 1 ? CommandType::AllianceWithdraw : which == 2 ? CommandType::AllianceBreak : CommandType::AllianceInvite;
                c.other_player = static_cast<uint8_t>((p + 1 + rng.below(3)) % MAX_PLAYERS);
            }
        } else {
            if (!mix.quit) continue;
            c.type = CommandType::Quit;
        }
        if (has_ant_list(c.type)) {
            c.ants = selection();
            const auto t = target(c.type);
            c.tile_x = t.first;
            c.tile_y = t.second;
        }
        out.push_back(std::move(c));
    }
    return out;
}

struct Run {
    uint32_t seed{1};
    Mix mix{};
    uint32_t score_bonus_every{250};   // a test hook (the points that make hatching possible): every this many ticks every team gets points; 0: never
};

// The matches of the quick tests: many orders, so that searches are in flight, ants fight and hatch within a few hundred ticks (the whole matches use the sparser default)
Run quick_run(uint32_t seed) {
    Run r;
    r.seed = seed;
    r.mix.percent_per_tick = 40;
    return r;
}

void give_points(SimulationEngine& e) {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) e.set_player_score(p, e.get_player_score(p) + 450);
}

// A match on one engine (deterministic: the same call twice gives two identical engines)
void play_ticks(SimulationEngine& e, Lcg& rng, const Run& run, uint32_t ticks) {
    for (uint32_t t = 0; t < ticks; ++t) {
        if (run.score_bonus_every != 0 && e.current_tick() % run.score_bonus_every == 0) give_points(e);
        for (const Command& c : gen_commands(e, rng, run.mix)) e.apply_command(c);
        e.tick();
    }
}

// The rig: an engine on a shipped map and the generator that plays it
struct Match {
    SimulationEngine engine;
    Lcg rng;
    Run run;
    explicit Match(const std::string& map, const Run& r, uint8_t roster = 0x0F) : rng(r.seed * 2654435761u + 17u), run(r) { engine.init(level(map), r.seed, roster); }
    void play(uint32_t ticks) { play_ticks(engine, rng, run, ticks); }
};

// One tick of the match `m` AND of every engine in `twins`: the same score hook and the same commands for all (the answers must agree), then everything observable
// compared with the match's engine. Returns what differs ("" when nothing does). The world states are compared when `with_world`.
std::string step_group(Match& m, const std::vector<SimulationEngine*>& twins, bool with_world) {
    SimulationEngine& e = m.engine;
    if (m.run.score_bonus_every != 0 && e.current_tick() % m.run.score_bonus_every == 0) {
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            const int32_t v = e.get_player_score(p) + 450;
            e.set_player_score(p, v);
            for (SimulationEngine* t : twins) t->set_player_score(p, v);
        }
    }
    const std::vector<Command> cmds = gen_commands(e, m.rng, m.run.mix);
    std::vector<CommandResult> answers;
    for (const Command& c : cmds) answers.push_back(e.apply_command(c));
    for (SimulationEngine* t : twins) {
        for (size_t i = 0; i < cmds.size(); ++i) {
            if (!same_result(t->apply_command(cmds[i]), answers[i])) return "an engine answers a command differently (tick " + std::to_string(e.current_tick()) + ")";
        }
    }
    e.tick();
    for (SimulationEngine* t : twins) t->tick();
    const StateHash he = e.state_hash();
    const Events ev = poll_events(e);
    for (SimulationEngine* t : twins) {
        const StateHash ht = t->state_hash();
        if (ht.total != he.total) return describe_hash_difference(he, ht, e.current_tick(), t->current_tick());
        if (t->current_tick() != e.current_tick() || t->is_match_over() != e.is_match_over()) return "the ticks or the end of the match differ";
        const Events et = poll_events(*t);
        if (et.fp != ev.fp) {
            return "the cues or the news of tick " + std::to_string(e.current_tick()) + " differ (" + std::to_string(ev.audio) + "+" + std::to_string(ev.news) + " vs " + std::to_string(et.audio) + "+" +
                   std::to_string(et.news) + ")";
        }
        if (with_world) {
            const std::string d = diff_world(e, *t);
            if (!d.empty()) return d;
        }
    }
    return {};
}

// `ticks` ticks of the match and of one twin; the world states are compared every `world_every` ticks (0: never)
std::string run_pair(Match& m, SimulationEngine& twin, uint32_t ticks, uint32_t world_every) {
    const std::vector<SimulationEngine*> twins = {&twin};
    for (uint32_t t = 0; t < ticks; ++t) {
        const std::string d = step_group(m, twins, world_every != 0 && t % world_every == 0);
        if (!d.empty()) return d;
    }
    return {};
}

bool has_effect(const SimulationEngine& e, const char* name) {
    for (const VisualEffect& f : e.get_world_state().effects) {
        if (f.anim_name == name) return true;
    }
    return false;
}

// Work on an engine in every way the commands allow, fast and wild (a stream that depends on `seed` only)
void wild_ticks(SimulationEngine& e, uint32_t seed, int ticks) {
    Lcg rng(seed);
    Mix wild;
    wild.percent_per_tick = 90;
    wild.quit = true;
    for (int i = 0; i < ticks; ++i) {
        if (i % 40 == 0) give_points(e);
        for (const Command& c : gen_commands(e, rng, wild)) e.apply_command(c);
        e.tick();
        if (i % 3 == 0) poll_events(e);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The scenario of the lock-step tests with everything going on: four teams of the six ant types, a river with a bridge, a lunch box, bombs
// ---------------------------------------------------------------------------------------------------------------------------------

void build_busy_world(SimulationEngine& sim, uint32_t seed) {
    sim.init_test_world(60, 60, seed, 720000);
    const TileCoord hills[MAX_PLAYERS] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) sim.grid_mut().set_anthill(p, hills[p]);
    for (int32_t y = 0; y < 60; ++y) sim.grid_mut().set_terrain(30, y, TERRAIN_WATER);      // a river down the middle
    for (int step = 0; step < 4; ++step) sim.grid_mut().advance_bridge(30, 30, 0);           // with one bridge
    sim.grid_mut().drop_lunchbox(20, 20, 25);
    sim.grid_mut().place_bomb(25, 25, 1);
    sim.grid_mut().place_bomb(34, 40, 2);
    const AntType types[6] = {AntType::Worker, AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        for (int i = 0; i < 6; ++i) {
            sim.spawn_unit(p, types[i], TileCoord{hills[p].x + 1 + i, hills[p].y + 6});
        }
    }
}

// The two lowest teams of a roster
std::pair<uint8_t, uint8_t> first_two(uint8_t roster) {
    uint8_t found[2] = {0, 1};
    int n = 0;
    for (uint8_t p = 0; p < MAX_PLAYERS && n < 2; ++p) {
        if (((roster >> p) & 1u) != 0) found[n++] = p;
    }
    return {found[0], found[1]};
}

}  // namespace

// =================================================================================================================================
// The tests
// =================================================================================================================================

namespace {

void run_api_tests() {
    TEST_CASE("R1.1 API: A Copy Is Constructed On Purpose (the constructor is explicit), Assigned And Moved; An Engine Cannot Be Copied By Accident") {
        static_assert(std::is_copy_constructible<SimulationEngine>::value, "an engine can be copied");
        static_assert(std::is_copy_assignable<SimulationEngine>::value, "an engine can be assigned");
        static_assert(std::is_move_constructible<SimulationEngine>::value && std::is_move_assignable<SimulationEngine>::value, "an engine can still be moved");
        static_assert(!std::is_convertible<const SimulationEngine&, SimulationEngine>::value, "no implicit copy: an engine passed by value would copy the whole map");
        static_assert(std::is_nothrow_move_constructible<SimulationEngine>::value, "moving an engine does not throw (a vector of engines moves its elements)");
        static_assert(std::is_copy_constructible<PathManager>::value && std::is_copy_assignable<PathManager>::value, "a path manager can be copied (a search points into the pool of its manager)");
        static_assert(!std::is_copy_constructible<PathSearch>::value, "a search is copied by its manager only (it must be attached to the new pool)");
        ASSERT_TRUE(true);
    } TEST_END();
}

void run_copy_equals_source_tests() {
    TEST_CASE("R1.2 A Copy Equals Its Source In Everything That Can Be Observed (constructor, assignment into a new engine, assignment over another map and over a test world): six maps, a 120 tick match") {
        for (const char* file : kMaps) {
            Run run = quick_run(11);
            Match m(file, run);
            m.play(120);
            // names are part of what a copy keeps (the alliance texts print them), and something is queued at the moment of the copy: the cues and news that nobody has polled
            // yet (an alliance offer posts both)
            m.engine.set_player_name(0, "Ann");
            m.engine.set_player_name(2, "Zed");
            m.engine.propose_alliance(0, 1);
            Events pending_in_source;
            {
                SimulationEngine probe(m.engine);
                pending_in_source = poll_events(probe);
            }
            ASSERT_TRUE(pending_in_source.audio > 0 && pending_in_source.news > 0);

            SimulationEngine by_constructor(m.engine);
            SimulationEngine by_assignment;
            by_assignment = m.engine;
            SimulationEngine over_another_map;
            over_another_map.init(level(std::string(file) == "TINY.LVL" ? "MEDIUM.LVL" : "TINY.LVL"), 5);
            for (int i = 0; i < 40; ++i) over_another_map.tick();
            over_another_map = m.engine;
            SimulationEngine over_a_test_world;
            build_busy_world(over_a_test_world, 3);
            for (int i = 0; i < 30; ++i) over_a_test_world.tick();
            over_a_test_world = m.engine;

            SimulationEngine* copies[] = {&by_constructor, &by_assignment, &over_another_map, &over_a_test_world};
            const uint64_t world_of_source = fp_world(m.engine.get_world_state());
            const uint64_t trace_of_source = fp_trace(m.engine);
            for (SimulationEngine* c : copies) {
                ASSERT_NO_DIFF(diff_state(m.engine, *c));
                ASSERT_EQ(fp_world(c->get_world_state()), world_of_source);
                ASSERT_EQ(fp_trace(*c), trace_of_source);
                ASSERT_EQ(c->current_tick(), m.engine.current_tick());
                ASSERT_EQ(c->roster_mask(), m.engine.roster_mask());
                ASSERT_EQ(c->get_player_name(0), std::string("Ann"));
                ASSERT_EQ(c->get_player_name(2), std::string("Zed"));
                const Events e = poll_events(*c);                            // the queued cues and news were copied
                ASSERT_EQ(e.fp, pending_in_source.fp);
                ASSERT_EQ(e.audio, pending_in_source.audio);
                ASSERT_EQ(e.news, pending_in_source.news);
            }
        }
    } TEST_END();

    TEST_CASE("R1.3 A Copy Is A Different Object In Every Respect: Its Own Cells, Ants, Waypoint Lists And World State (nothing is shared by address)") {
        Run run = quick_run(5);
        Match m("TREASURE.LVL", run);
        m.play(150);
        SimulationEngine c(m.engine);
        ASSERT_TRUE(&c.get_world_state() != &m.engine.get_world_state());
        ASSERT_TRUE(c.grid().cells().data() != m.engine.grid().cells().data());
        ASSERT_TRUE(c.grid().anthills().data() != m.engine.grid().anthills().data());
        size_t compared = 0;
        bool walking = false;
        for (const AntSnapshot& a : m.engine.get_world_state().ants) {
            AntUnit& original = m.engine.get_unit(a.id);
            AntUnit& copy = c.get_unit(a.id);
            ASSERT_TRUE(&copy != &original);                                 // the ant objects are two
            ASSERT_EQ(copy.pixel_x, original.pixel_x);
            ++compared;
            if (!original.waypoints.empty()) {
                walking = true;
                ASSERT_TRUE(original.waypoints.data() != copy.waypoints.data());   // and so are their waypoint lists
                ASSERT_TRUE(original.waypoints == copy.waypoints);
            }
        }
        ASSERT_TRUE(compared > 0);
        ASSERT_TRUE(walking);
    } TEST_END();
}

void run_scene_tests() {
    TEST_CASE("R1.4b A Copy Keeps What Only The Presentation Knows: removed ants (their entries stay in the table), a battle cloud, effects with sound owners, and the counter that numbers the next effect sprite") {
        SimulationEngine sim;
        sim.init_test_world(60, 60, 7, 60000);
        sim.set_viewing_player_id(0);
        // ants that are gone: the entry of a removed ant stays in the table (the state hash counts it)
        std::vector<uint32_t> gone;
        for (int i = 0; i < 4; ++i) gone.push_back(sim.spawn_unit(3, AntType::Worker, TileCoord{5 + 3 * i, 50}));
        sim.kill_unit(gone[0]);
        sim.kill_unit(gone[2]);
        ASSERT_TRUE(sim.get_unit(gone[0]).removed && sim.get_unit(gone[2]).removed && !sim.get_unit(gone[1]).removed);
        // a bomb that has gone off: an explosion effect whose sound has an owner; the owner ids come from a counter
        sim.grid_mut().place_bomb(10, 10, 1);
        const uint32_t sapper = sim.spawn_unit(2, AntType::Worker, TileCoord{10, 11});
        sim.trigger_bomb_detonation(sapper, TileCoord{10, 10});
        for (int i = 0; i < 3; ++i) sim.tick();
        ASSERT_TRUE(has_effect(sim, "bombex"));
        bool owned = false;
        for (const VisualEffect& f : sim.get_world_state().effects) owned = owned || f.audio_owner != 0;
        ASSERT_TRUE(owned);
        // a pile-up of foreign ants on one tile: the dust ball of the original (a looping "battle" effect), made in the tick of the copy
        sim.spawn_unit(1, AntType::Worker, TileCoord{30, 30});
        sim.spawn_unit(2, AntType::Worker, TileCoord{30, 30});
        sim.spawn_unit(2, AntType::Worker, TileCoord{30, 30});
        sim.blast_tile_for_test(TileCoord{30, 30});
        ASSERT_TRUE(has_effect(sim, "battle") && has_effect(sim, "bombex"));

        SimulationEngine by_constructor(sim);
        SimulationEngine by_assignment;
        by_assignment = sim;
        for (SimulationEngine* c : {&by_constructor, &by_assignment}) {
            ASSERT_NO_DIFF(diff_state(sim, *c));
            ASSERT_NO_DIFF(diff_world(sim, *c));
            ASSERT_TRUE(c->get_unit(gone[0]).removed && c->get_unit(gone[2]).removed && !c->get_unit(gone[1]).removed);
            ASSERT_TRUE(has_effect(*c, "battle") && has_effect(*c, "bombex"));
        }
        // a second explosion after the copy, the same on all three: the next sprite's owner id is the source's counter, not the copy's own
        for (SimulationEngine* e : {&sim, &by_constructor, &by_assignment}) {
            e->grid_mut().place_bomb(14, 14, 1);
            const uint32_t second = e->spawn_unit(2, AntType::Worker, TileCoord{14, 15});
            e->trigger_bomb_detonation(second, TileCoord{14, 14});
        }
        for (int t = 0; t < 120; ++t) {
            ASSERT_NO_DIFF(diff_state(sim, by_constructor));
            ASSERT_NO_DIFF(diff_state(sim, by_assignment));
            ASSERT_NO_DIFF(diff_world(sim, by_constructor));
            ASSERT_NO_DIFF(diff_world(sim, by_assignment));
            sim.tick();
            by_constructor.tick();
            by_assignment.tick();
            const Events source_events = poll_events(sim);
            ASSERT_EQ(poll_events(by_constructor).fp, source_events.fp);
            ASSERT_EQ(poll_events(by_assignment).fp, source_events.fp);
        }
    } TEST_END();
}

void run_lockstep_tests() {
    TEST_CASE("R1.4 A Copy Ticks Identically: six maps, copies taken at 0, 1, 20, 99, 100, 101 and 233 ticks (constructor and assignment in turn); the state hash, the answers, the cues and the world state compared at every tick") {
        struct Twin {
            SimulationEngine engine;
            uint32_t until;
            Twin(const SimulationEngine& source, uint32_t last) : engine(source), until(last) {}   // a copy constructed
            explicit Twin(uint32_t last) : engine(), until(last) {}                                   // an engine to assign into
        };
        const uint32_t starts[] = {0, 1, 20, 99, 100, 101, 233};
        for (const char* file : kMaps) {
            Run run = quick_run(29);
            Match m(file, run);
            std::vector<std::unique_ptr<Twin>> twins;
            size_t next_start = 0;
            size_t made = 0;
            for (uint32_t t = 0; t < 340; ++t) {
                while (next_start < sizeof(starts) / sizeof(starts[0]) && starts[next_start] == t) {
                    std::unique_ptr<Twin> twin;
                    if (made % 2 == 0) {
                        twin.reset(new Twin(m.engine, t + 60));
                    } else {
                        twin.reset(new Twin(t + 60));
                        twin->engine = m.engine;
                    }
                    ASSERT_NO_DIFF(diff_state(m.engine, twin->engine));
                    twins.push_back(std::move(twin));
                    ++next_start;
                    ++made;
                }
                std::vector<SimulationEngine*> active;
                bool ending = false;
                for (auto& tw : twins) {
                    active.push_back(&tw->engine);
                    if (tw->until == t + 1) ending = true;
                }
                ASSERT_NO_DIFF(step_group(m, active, t % 10 == 0 || ending));
                twins.erase(std::remove_if(twins.begin(), twins.end(), [t](const std::unique_ptr<Twin>& tw) { return tw->until <= t + 1; }), twins.end());
            }
            ASSERT_EQ(made, sizeof(starts) / sizeof(starts[0]));
        }
    } TEST_END();

    TEST_CASE("R1.5 A Copy Ticks Identically In A Match With Three Teams, With Fog Of War On, And With Locomotion Tracing On (and a copy looks from its own seat)") {
        Run run = quick_run(77);
        run.mix.alliances = false;                                                   // no team sees what another sees: the fog of each seat is its own
        Match m("TREASURE.LVL", run, 0x0B);
        m.engine.set_fog_of_war_enabled(true);
        m.engine.set_viewing_player_id(1);
        m.engine.set_locomotion_trace_enabled(true);
        m.play(80);
        SimulationEngine c(m.engine);
        ASSERT_NO_DIFF(diff_state(m.engine, c));
        ASSERT_NO_DIFF(diff_world(m.engine, c));
        ASSERT_TRUE(c.is_fog_of_war_enabled());
        ASSERT_EQ(fp_trace(c), fp_trace(m.engine));
        ASSERT_TRUE(!m.engine.locomotion_trace().empty());
        ASSERT_NO_DIFF(run_pair(m, c, 160, 5));
        ASSERT_EQ(fp_trace(c), fp_trace(m.engine));
        // the fog is the viewer's: looking from another seat on a copy changes that copy only (the fog is no part of the state hash)
        SimulationEngine other_viewer(m.engine);
        const uint64_t source_world = fp_world(m.engine.get_world_state());
        other_viewer.set_viewing_player_id(3);
        ASSERT_NO_DIFF(diff_state(m.engine, other_viewer));
        ASSERT_NE(fp_world(other_viewer.get_world_state()), source_world);
        ASSERT_EQ(fp_world(m.engine.get_world_state()), source_world);
        ASSERT_EQ(fp_world(c.get_world_state()), source_world);
    } TEST_END();

    TEST_CASE("R1.6 A Copy Ticks Identically In The Busy World (all six ant types, a river, a bridge, bombs, a lunch box): fire, bombs, bridges, drownings, raids, hatching, alliances") {
        for (uint32_t seed : {3u, 4u, 5u}) {
            Run run;
            run.seed = seed;
            run.mix.percent_per_tick = 55;
            Match m("TINY.LVL", run);                                        // (the match object is only the rig: its engine is replaced by the busy world)
            build_busy_world(m.engine, seed);
            m.rng = Lcg(seed * 977u);
            m.play(60);                                                      // the action starts
            SimulationEngine c;
            c = m.engine;                                                    // taken in the middle of the action, assigned into an engine that has never been used
            ASSERT_NO_DIFF(diff_state(m.engine, c));
            ASSERT_NO_DIFF(run_pair(m, c, 360, 4));
        }
    } TEST_END();
}

void run_independence_tests() {
    TEST_CASE("R1.7 Nothing Is Shared (the copy is changed, the source is not): every mutator and 150 wild ticks on the copy leave the source exactly as an engine that was never copied") {
        for (const char* file : {"TINY.LVL", "ISLANDS.LVL", "TREASURE.LVL"}) {
            Run run = quick_run(41);
            Match original(file, run);
            Match twin(file, run);
            original.play(200);
            twin.play(200);
            ASSERT_NO_DIFF(diff_state(original.engine, twin.engine));              // the replay is deterministic: the twin is the same match
            ASSERT_NO_DIFF(diff_world(original.engine, twin.engine));
            {
                SimulationEngine c(original.engine);
                // every way to change an engine through its public interface, on the COPY
                poll_events(c);                                                     // (drains the copy's queues, not the source's)
                wild_ticks(c, 999, 150);
                c.set_player_score(0, 12345);
                c.set_player_eggs(1, 77);
                c.set_player_name(0, "Copy");
                c.set_hatch_delay_ticks(3);
                c.set_fog_of_war_enabled(true);
                c.set_viewing_player_id(2);
                c.set_locomotion_trace_enabled(true);
                c.grid_mut().place_bomb(3, 3, 1);
                c.grid_mut().place_firewall(4, 4, 2);
                c.grid_mut().set_terrain(5, 5, TERRAIN_WATER);
                c.grid_mut().drop_lunchbox(6, 6, 40);
                c.set_match_time_remaining_ms(5000);
                for (const AntSnapshot& a : c.get_world_state().ants) {
                    c.get_unit(a.id).hp = 1;                                       // a direct write into the ant objects
                    c.get_unit(a.id).waypoints.clear();
                }
                ASSERT_TRUE(c.spawn_unit(0, AntType::Thief, TileCoord{2, 2}) > 0);
                c.drop_player(1);
                c.break_alliance(0);
                c.propose_alliance(0, 2);
                c.clear_audio_events();
                c.clear_news_events();
                for (int i = 0; i < 30; ++i) c.tick();
                c.init(level("SMALL.LVL"), 4);                                      // a complete re-initialisation of the copy
                for (int i = 0; i < 20; ++i) c.tick();
            }                                                                        // the copy dies here
            ASSERT_NO_DIFF(diff_state(original.engine, twin.engine));
            ASSERT_NO_DIFF(diff_world(original.engine, twin.engine));
            const Events source_events = poll_events(original.engine);              // the source's cues and news are all still there (the copy polled and cleared its own)
            const Events twin_events = poll_events(twin.engine);
            ASSERT_TRUE(source_events.audio > 0 && source_events.news > 0);
            ASSERT_EQ(source_events.fp, twin_events.fp);
            ASSERT_NO_DIFF(run_pair(original, twin.engine, 120, 10));
        }
    } TEST_END();

    TEST_CASE("R1.8 Nothing Is Shared (the source is changed, the copy is not): ticking, ordering, re-initialising and destroying the source leave the copy exactly as an engine that was never touched") {
        for (const char* file : {"MEDIUM.LVL", "GAUNTLET.LVL"}) {
            Run run = quick_run(43);
            Match twin(file, run);
            twin.play(180);
            std::unique_ptr<SimulationEngine> copy;
            {
                Match original(file, run);
                original.play(180);
                copy.reset(new SimulationEngine(original.engine));
                SimulationEngine assigned;
                assigned = original.engine;
                // the SOURCE is worked on
                original.run.mix.percent_per_tick = 90;
                original.play(200);
                original.engine.set_player_score(2, 999);
                original.engine.grid_mut().place_bomb(7, 7, 0);
                original.engine.set_fog_of_war_enabled(true);
                original.engine.set_viewing_player_id(3);
                original.engine.poll_audio_events();
                original.engine.poll_news_events();
                original.engine.init(level("TINY.LVL"), 8);
                for (int i = 0; i < 25; ++i) original.engine.tick();
                ASSERT_NO_DIFF(diff_state(assigned, twin.engine));                  // the engine that was assigned from it is untouched as well
                ASSERT_NO_DIFF(diff_world(assigned, twin.engine));
            }                                                                        // the source is destroyed here, its memory freed
            ASSERT_NO_DIFF(diff_state(*copy, twin.engine));
            ASSERT_NO_DIFF(diff_world(*copy, twin.engine));
            // and the copy is a complete, working engine that goes on as the never-copied one does
            ASSERT_NO_DIFF(run_pair(twin, *copy, 150, 10));
        }
    } TEST_END();

    TEST_CASE("R1.9 Nothing Is Shared (two copies of one source): the source and its two copies, each worked on in turn, each end exactly as an engine that was never copied and worked on in the same way") {
        Run run = quick_run(47);
        auto reference = [&](uint32_t wild_seed, int wild_count, int play_count) {
            Match r("SMALL.LVL", run);
            r.play(260);
            if (wild_count > 0) wild_ticks(r.engine, wild_seed, wild_count);
            r.play(static_cast<uint32_t>(play_count));
            return r;
        };
        Match source("SMALL.LVL", run);
        source.play(260);
        SimulationEngine a(source.engine);
        SimulationEngine b(source.engine);
        wild_ticks(a, 1, 100);                                                       // work on a ...
        source.play(100);                                                            // ... and on the source ...
        Match untouched = reference(0, 0, 0);
        ASSERT_NO_DIFF(diff_state(b, untouched.engine));                             // ... and b has not noticed
        ASSERT_NO_DIFF(diff_world(b, untouched.engine));
        wild_ticks(b, 2, 100);                                                       // now b
        Match a_reference = reference(1, 100, 0);
        ASSERT_NO_DIFF(diff_state(a, a_reference.engine));                           // a is as if it had never had a sibling
        ASSERT_NO_DIFF(diff_world(a, a_reference.engine));
        Match source_reference = reference(0, 0, 100);
        ASSERT_NO_DIFF(diff_state(source.engine, source_reference.engine));
        ASSERT_NO_DIFF(diff_world(source.engine, source_reference.engine));
        Match b_reference = reference(2, 100, 0);
        ASSERT_NO_DIFF(diff_state(b, b_reference.engine));
    } TEST_END();

    TEST_CASE("R1.10 Nothing Is Shared (a long match with copies taken and dropped all the time): 60 copies of a running match, each worked on and destroyed, leave the match exactly as an engine that was never copied") {
        Run run = quick_run(53);
        Match twin("SMALL.LVL", run);
        Match source("SMALL.LVL", run);
        for (int round = 0; round < 60; ++round) {
            twin.play(12);
            source.play(12);
            SimulationEngine c(source.engine);
            wild_ticks(c, static_cast<uint32_t>(round) + 100u, 6);
        }
        ASSERT_NO_DIFF(diff_state(source.engine, twin.engine));
        ASSERT_NO_DIFF(diff_world(source.engine, twin.engine));
    } TEST_END();
}

void run_assignment_tests() {
    TEST_CASE("R1.11 Assignment Over An Engine With Another History Leaves Nothing Of The Old One: other map and size, more or fewer ants, other roster, fog, names and trace") {
        struct Shape {
            const char* from_map;
            uint8_t from_roster;
            uint32_t from_ticks;
            const char* to_map;
            uint8_t to_roster;
            uint32_t to_ticks;
        };
        const Shape shapes[] = {
            {"TINY.LVL", 0x0F, 300, "MEDIUM.LVL", 0x0F, 90},      // a bigger map over a smaller
            {"MEDIUM.LVL", 0x0F, 300, "TINY.LVL", 0x0F, 90},      // and back
            {"ISLANDS.LVL", 0x0F, 400, "TINY.LVL", 0x03, 5},      // far fewer ants
            {"TINY.LVL", 0x03, 5, "ISLANDS.LVL", 0x0F, 400},      // far more
            {"TREASURE.LVL", 0x07, 120, "GAUNTLET.LVL", 0x0D, 120},
            {"SMALL.LVL", 0x0F, 0, "SMALL.LVL", 0x0F, 250},        // the same map: the memory of the target is reused
        };
        uint32_t seed = 60;
        for (const Shape& s : shapes) {
            Run run = quick_run(++seed);
            Match target(s.from_map, run, s.from_roster);
            target.engine.set_fog_of_war_enabled(true);
            target.engine.set_player_name(2, "Old name");
            target.engine.set_locomotion_trace_enabled(true);
            target.play(s.from_ticks);
            Match source(s.to_map, run, s.to_roster);
            source.play(s.to_ticks);
            const auto pair = first_two(s.to_roster);
            source.engine.propose_alliance(pair.first, pair.second);
            const uint64_t before = fp_world(source.engine.get_world_state());
            target.engine = source.engine;
            ASSERT_NO_DIFF(diff_state(source.engine, target.engine));
            ASSERT_EQ(fp_world(target.engine.get_world_state()), before);
            ASSERT_EQ(fp_trace(target.engine), fp_trace(source.engine));
            ASSERT_EQ(target.engine.get_player_name(2), source.engine.get_player_name(2));
            ASSERT_EQ(target.engine.is_fog_of_war_enabled(), source.engine.is_fog_of_war_enabled());
            ASSERT_EQ(poll_events(target.engine).fp, poll_events(source.engine).fp);
            ASSERT_NO_DIFF(run_pair(source, target.engine, 120, 6));
        }
    } TEST_END();

    TEST_CASE("R1.12 Assignment Of A Finished Match Over A Running One, And Of A Running One Over A Finished One (the end of the match, its result, nothing ticks after it)") {
        Run run = quick_run(71);
        Match running("SMALL.LVL", run);
        running.play(150);
        Match finished("SMALL.LVL", run);
        finished.play(60);
        finished.engine.set_match_time_remaining_ms(400);
        for (int i = 0; i < 30 && !finished.engine.is_match_over(); ++i) finished.engine.tick();
        ASSERT_TRUE(finished.engine.is_match_over());
        const uint64_t over_fp = fp_world(finished.engine.get_world_state());
        SimulationEngine copy_of_finished(finished.engine);
        ASSERT_TRUE(copy_of_finished.is_match_over());
        ASSERT_NO_DIFF(diff_state(finished.engine, copy_of_finished));
        ASSERT_EQ(fp_world(copy_of_finished.get_world_state()), over_fp);
        const uint64_t tick_at_end = copy_of_finished.current_tick();
        for (int i = 0; i < 10; ++i) copy_of_finished.tick();                      // nothing happens after the end
        ASSERT_EQ(copy_of_finished.current_tick(), tick_at_end);
        Command hatch;
        hatch.type = CommandType::Hatch;
        hatch.issuer = 0;
        ASSERT_EQ(copy_of_finished.apply_command(hatch).status, Status::Ignored);

        running.engine = finished.engine;                                           // over a running match
        ASSERT_TRUE(running.engine.is_match_over());
        ASSERT_NO_DIFF(diff_state(finished.engine, running.engine));
        ASSERT_EQ(fp_world(running.engine.get_world_state()), over_fp);

        Match alive("SMALL.LVL", run);
        alive.play(100);
        finished.engine = alive.engine;                                             // a running one over a finished one: it ticks again
        ASSERT_FALSE(finished.engine.is_match_over());
        ASSERT_NO_DIFF(run_pair(alive, finished.engine, 80, 10));
    } TEST_END();

    TEST_CASE("R1.13 Assignment Between Engines Of The Same Shape Keeps The Target's Memory (a rebuild every frame allocates next to nothing): the cells, the ants and their waypoint lists stay where they are") {
        Run run = quick_run(83);
        run.mix.hatch = false;                                                      // no new ants: the table of ants keeps its size, as it does between two frames
        run.score_bonus_every = 0;
        Match source("MEDIUM.LVL", run);
        source.play(220);
        SimulationEngine target(source.engine);
        source.play(40);                                                            // the source moves on; the target is rebuilt from it
        const TileCell* cells_before = target.grid().cells().data();
        std::vector<std::pair<uint32_t, const AntUnit*>> ants_before;
        for (const AntSnapshot& a : target.get_world_state().ants) ants_before.emplace_back(a.id, &target.get_unit(a.id));
        ASSERT_TRUE(!ants_before.empty());
        target = source.engine;
        ASSERT_TRUE(target.grid().cells().data() == cells_before);
        for (const auto& pa : ants_before) ASSERT_TRUE(&target.get_unit(pa.first) == pa.second);
        ASSERT_NO_DIFF(diff_state(source.engine, target));
        ASSERT_NO_DIFF(diff_world(source.engine, target));
        ASSERT_NO_DIFF(run_pair(source, target, 100, 10));
    } TEST_END();

    TEST_CASE("R1.14 The Rebuild Pattern Of The Prediction: a confirmed engine, a second engine assigned from it every tick and run three ticks ahead; it equals an engine that was never copied and ran the same ticks") {
        for (const char* file : {"SMALL.LVL", "ISLANDS.LVL"}) {
            Run run = quick_run(91);
            Match confirmed(file, run);
            Match never_copied(file, run);
            SimulationEngine predicted;
            for (uint32_t t = 0; t < 200; ++t) {
                confirmed.play(1);
                never_copied.play(1);
                predicted = confirmed.engine;                                       // the rebuild
                for (int k = 0; k < 3; ++k) predicted.tick();
                ASSERT_NO_DIFF(diff_state(confirmed.engine, never_copied.engine));  // reading from the confirmed engine never disturbs it
                if (t % 10 == 0) {
                    // what the prediction must show: the match three ticks on with no order in between, replayed from the start without a copy anywhere
                    Match replay(file, run);
                    replay.play(t + 1);
                    for (int k = 0; k < 3; ++k) replay.engine.tick();
                    ASSERT_NO_DIFF(diff_state(predicted, replay.engine));
                    ASSERT_NO_DIFF(diff_world(predicted, replay.engine));
                }
            }
        }
    } TEST_END();
}

void run_edge_tests() {
    TEST_CASE("R1.15 Self-Assignment Changes Nothing; Copying A Moved-From Engine Gives A Usable Empty Engine; Copying An Engine That Was Never Initialised Works") {
        Run run = quick_run(97);
        Match m("TINY.LVL", run);
        m.play(80);
        SimulationEngine twin(m.engine);
        SimulationEngine& alias = m.engine;
        m.engine = alias;                                                           // self-assignment
        ASSERT_NO_DIFF(diff_state(m.engine, twin));
        ASSERT_NO_DIFF(diff_world(m.engine, twin));
        ASSERT_NO_DIFF(run_pair(m, twin, 40, 5));

        SimulationEngine moved_to(std::move(twin));                                 // `twin` has no engine inside now
        ASSERT_NO_DIFF(diff_state(m.engine, moved_to));
        SimulationEngine copy_of_moved_from(twin);                                  // NOLINT: the point of the test
        copy_of_moved_from.init_test_world(10, 10, 1, 60000);
        copy_of_moved_from.spawn_unit(0, AntType::Worker, TileCoord{3, 3});
        for (int i = 0; i < 5; ++i) copy_of_moved_from.tick();
        ASSERT_EQ(copy_of_moved_from.current_tick(), 5u);
        SimulationEngine assigned_from_moved_from;
        assigned_from_moved_from.init_test_world(8, 8, 2, 60000);
        assigned_from_moved_from = twin;                                            // NOLINT: assigning a moved-from engine gives a fresh engine
        assigned_from_moved_from.init_test_world(8, 8, 2, 60000);
        assigned_from_moved_from.tick();
        ASSERT_EQ(assigned_from_moved_from.current_tick(), 1u);
        twin = m.engine;                                                            // a moved-from engine is assignable again
        ASSERT_NO_DIFF(diff_state(m.engine, twin));

        SimulationEngine fresh_a;
        SimulationEngine fresh_b(fresh_a);                                          // never initialised
        ASSERT_NO_DIFF(diff_state(fresh_a, fresh_b));
        fresh_b.init(level("TINY.LVL"), 3);
        fresh_a.init(level("TINY.LVL"), 3);
        ASSERT_NO_DIFF(diff_state(fresh_a, fresh_b));
    } TEST_END();

    TEST_CASE("R1.16 The Cached World State Is The Copy's Own: fresh or stale in the source, the copy answers for its own state (an assignment makes the target's cache stale)") {
        Run run = quick_run(101);
        Match m("TINY.LVL", run);
        m.play(100);
        // a copy of a source whose cache is fresh, and of one whose cache is stale (a tick after the last look)
        const uint64_t fp_before_tick = fp_world(m.engine.get_world_state());
        SimulationEngine from_fresh(m.engine);
        m.engine.tick();                                                            // the source's cache is stale now
        SimulationEngine from_stale(m.engine);
        ASSERT_EQ(fp_world(from_fresh.get_world_state()), fp_before_tick);          // the copy shows the state at ITS copy time, not the source's later state
        const uint64_t fp_after_tick = fp_world(m.engine.get_world_state());
        ASSERT_NE(fp_after_tick, fp_before_tick);
        ASSERT_EQ(fp_world(from_stale.get_world_state()), fp_after_tick);
        // a target whose cache is fresh and that is then assigned over: its next world state is the source's
        SimulationEngine target(m.engine);
        ASSERT_EQ(fp_world(target.get_world_state()), fp_after_tick);               // (fresh now)
        m.play(30);
        target = m.engine;
        ASSERT_EQ(fp_world(target.get_world_state()), fp_world(m.engine.get_world_state()));
        // the same into a target that is an older copy of the same match, with its cache fresh
        SimulationEngine older(from_fresh);
        older.get_world_state();
        older = m.engine;
        ASSERT_EQ(fp_world(older.get_world_state()), fp_world(m.engine.get_world_state()));
        // ticking the copy never changes the source's world state, and vice versa
        const uint64_t source_fp = fp_world(m.engine.get_world_state());
        for (int i = 0; i < 20; ++i) older.tick();
        ASSERT_EQ(fp_world(m.engine.get_world_state()), source_fp);
        const uint64_t older_fp = fp_world(older.get_world_state());
        m.play(10);
        ASSERT_EQ(fp_world(older.get_world_state()), older_fp);
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The path managers: the half-done searches are the part of the engine that holds raw pointers
// ---------------------------------------------------------------------------------------------------------------------------------

struct Field {
    int w{100};
    int h{60};
    std::vector<uint8_t> wall;
    Field() : wall(static_cast<size_t>(w * h), 0) {
        // two walls with a gap at the opposite ends: a path from the left to the right has to go round both, and the search has to look at most of the field first
        for (int y = 0; y < h - 4; ++y) wall[static_cast<size_t>(y * w + 33)] = 1;      // gap at the bottom
        for (int y = 4; y < h; ++y) wall[static_cast<size_t>(y * w + 66)] = 1;           // gap at the top
    }
    uint32_t cost(TileCoord from, TileCoord to) const {
        if (to.x < 0 || to.y < 0 || to.x >= w || to.y >= h) return 8000;
        if (wall[static_cast<size_t>(to.y * w + to.x)] != 0) return 8000;
        return (from.x != to.x && from.y != to.y) ? 28u : 20u;
    }
};

using DeliveryLog = std::vector<std::optional<PathManager::Delivery>>;

bool same_delivery(const std::optional<PathManager::Delivery>& a, const std::optional<PathManager::Delivery>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->ant_id == b->ant_id && a->path == b->path;
}

std::string queue_shape(const PathManager& m) {
    std::string s = std::to_string(m.pending()) + ":";
    m.for_each_request([&s](uint32_t ant, TileCoord start, TileCoord goal, bool has_grid, bool finished) {
        s += " " + std::to_string(ant) + "(" + std::to_string(start.x) + "," + std::to_string(start.y) + ")->(" + std::to_string(goal.x) + "," + std::to_string(goal.y) + ")" +
             (has_grid ? "g" : "-") + (finished ? "f" : "-");
    });
    return s;
}

PathManager::CostProvider cost_of(const Field& f) {
    return [&f](uint32_t, TileCoord from, TileCoord to) { return f.cost(from, to); };
}

// Runs the manager until its queue is empty (or `max_runs`), logging what every run delivered
void drain(PathManager& m, const Field& f, DeliveryLog& log, int max_runs) {
    const PathManager::CostProvider cost = cost_of(f);
    for (int run = 0; run < max_runs && m.pending() > 0; ++run) log.push_back(m.run(cost));
}

bool same_log(const DeliveryLog& a, const DeliveryLog& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!same_delivery(a[i], b[i])) return false;
    }
    return true;
}

void request_far_paths(PathManager& m, const Field& f, int count, uint32_t first_id) {
    for (int i = 0; i < count; ++i) {
        m.request(first_id + static_cast<uint32_t>(i), TileCoord{3 + i, 20 + 3 * i}, TileCoord{95 - i, 30 + 2 * i}, static_cast<uint16_t>(f.h), static_cast<uint16_t>(f.w));
    }
}

void run_path_manager_tests() {
    TEST_CASE("R1.17 PathManager Copy: The Copy Goes On With Every Half-Done Search Exactly Where The Original Is (the same paths in the same runs), At Every Point Of The Schedule") {
        const Field f;
        const PathManager::CostProvider cost = cost_of(f);
        // how long do these searches take? (the test needs searches that span many runs, and more of them than the four grids of the pool)
        PathManager probe;
        request_far_paths(probe, f, 7, 100);
        DeliveryLog full;
        drain(probe, f, full, 1000);
        size_t delivered = 0;
        for (const auto& e : full) {
            if (!e) continue;
            ++delivered;
            ASSERT_TRUE(e->path.size() > 100);                                       // real paths round both walls (a failed search would be empty)
        }
        ASSERT_EQ(delivered, 7u);
        ASSERT_TRUE(full.size() >= 14);                                              // seven searches, each of several slices
        const int total_runs = static_cast<int>(full.size());
        for (int cut = 0; cut <= total_runs; cut += 3) {
            PathManager original;
            request_far_paths(original, f, 7, 100);
            for (int r = 0; r < cut && original.pending() > 0; ++r) original.run(cost);
            PathManager copy(original);
            ASSERT_EQ(queue_shape(copy), queue_shape(original));
            // the original goes on, the copy goes on: the same deliveries in the same runs
            while (original.pending() > 0 || copy.pending() > 0) {
                const auto da = original.run(cost);
                const auto db = copy.run(cost);
                ASSERT_TRUE(same_delivery(da, db));
                ASSERT_EQ(queue_shape(copy), queue_shape(original));
            }
            ASSERT_EQ(original.pending(), 0u);
        }
    } TEST_END();

    TEST_CASE("R1.18 PathManager Copy Shares Nothing: the original runs on alone and is destroyed first, and the copy still delivers what an untouched manager would (and the other way round)") {
        const Field f;
        const PathManager::CostProvider cost = cost_of(f);
        PathManager reference;
        request_far_paths(reference, f, 6, 200);
        for (int r = 0; r < 5; ++r) reference.run(cost);
        std::unique_ptr<PathManager> original(new PathManager());
        request_far_paths(*original, f, 6, 200);
        for (int r = 0; r < 5; ++r) original->run(cost);
        PathManager copy(*original);
        // the original works on, which rewrites its own search cells and open lists, and then it is destroyed
        DeliveryLog from_original;
        drain(*original, f, from_original, 1000);
        original.reset();
        DeliveryLog from_copy;
        DeliveryLog from_reference;
        drain(copy, f, from_copy, 1000);
        drain(reference, f, from_reference, 1000);
        ASSERT_TRUE(!from_copy.empty());
        ASSERT_TRUE(same_log(from_copy, from_reference));
        ASSERT_TRUE(same_log(from_original, from_reference));
        // the other way round: the copy runs alone first, then the original
        PathManager a;
        request_far_paths(a, f, 6, 300);
        for (int r = 0; r < 7; ++r) a.run(cost);
        PathManager b(a);
        PathManager untouched(a);
        DeliveryLog log_a;
        DeliveryLog log_b;
        DeliveryLog log_untouched;
        drain(b, f, log_b, 1000);
        drain(a, f, log_a, 1000);
        drain(untouched, f, log_untouched, 1000);
        ASSERT_TRUE(!log_b.empty());
        ASSERT_TRUE(same_log(log_b, log_untouched));
        ASSERT_TRUE(same_log(log_a, log_untouched));
    } TEST_END();

    TEST_CASE("R1.19 PathManager Assignment: Over A Manager With Searches Of Its Own (their grids are released, the new ones taken), Over An Empty One, Over A Moved-From One, And To Itself") {
        const Field f;
        const PathManager::CostProvider cost = cost_of(f);
        PathManager source;
        request_far_paths(source, f, 7, 400);
        for (int r = 0; r < 4; ++r) source.run(cost);
        PathManager reference(source);

        PathManager busy;                                                            // all four grids held by searches that are not the source's
        request_far_paths(busy, f, 6, 900);
        for (int r = 0; r < 3; ++r) busy.run(cost);
        busy = source;
        ASSERT_EQ(queue_shape(busy), queue_shape(source));
        PathManager empty;
        empty = source;
        ASSERT_EQ(queue_shape(empty), queue_shape(source));
        PathManager moved_from_one;
        PathManager sink(std::move(moved_from_one));
        moved_from_one = source;                                                     // NOLINT: the target is moved-from (it has no pool)
        ASSERT_EQ(queue_shape(moved_from_one), queue_shape(source));
        PathManager& alias = source;
        source = alias;                                                              // self-assignment
        ASSERT_EQ(queue_shape(source), queue_shape(reference));

        DeliveryLog a;
        DeliveryLog b;
        DeliveryLog c;
        DeliveryLog d;
        DeliveryLog r;
        drain(source, f, a, 1000);
        drain(busy, f, b, 1000);
        drain(empty, f, c, 1000);
        drain(moved_from_one, f, d, 1000);
        drain(reference, f, r, 1000);
        ASSERT_TRUE(!r.empty());
        ASSERT_TRUE(same_log(a, r));
        ASSERT_TRUE(same_log(b, r));
        ASSERT_TRUE(same_log(c, r));
        ASSERT_TRUE(same_log(d, r));
        // a copy of an empty manager, and of a moved-from one
        PathManager none;
        PathManager copy_of_none(none);
        ASSERT_EQ(copy_of_none.pending(), 0u);
        PathManager gone_from;
        PathManager taker(std::move(gone_from));
        PathManager copy_of_moved_from(gone_from);                                   // NOLINT: the point of the test
        ASSERT_EQ(copy_of_moved_from.pending(), 0u);
        copy_of_moved_from.request(1, TileCoord{1, 1}, TileCoord{5, 5}, 60, 100);   // a manager that was copied from a moved-from one works
        DeliveryLog e;
        drain(copy_of_moved_from, f, e, 100);
        ASSERT_EQ(e.size(), 1u);
        ASSERT_TRUE(e[0] && e[0]->path.size() == 5u);
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Whole matches (--whole-matches)
// ---------------------------------------------------------------------------------------------------------------------------------

void run_whole_match_tests() {
    TEST_CASE("R1.20 Whole Matches On The Six Shipped Maps: a copy re-taken every 1000 ticks (constructor and assignment in turn) and run in lock-step to the end of the match, the state hash compared at EVERY tick") {
        const uint32_t cap = ANTS_COPY_TEST_SANITIZED ? 2500u : 0u;                 // 0: the whole match
        uint32_t seed = 200;
        for (const char* file : kMaps) {
            Run run;
            run.seed = ++seed;
            Match m(file, run);
            SimulationEngine twin;
            uint32_t copies = 0;
            uint32_t ticks = 0;
            uint32_t after_end = 0;
            while (after_end < 30) {
                if (ticks % 1000 == 0) {
                    if (copies % 2 == 0) twin = SimulationEngine(m.engine);         // (a copy constructed, then moved in)
                    else twin = m.engine;                                            // (assigned)
                    ++copies;
                    ASSERT_NO_DIFF(diff_state(m.engine, twin));
                    ASSERT_NO_DIFF(diff_world(m.engine, twin));
                }
                ASSERT_NO_DIFF(run_pair(m, twin, 1, ticks % 50 == 0 ? 1u : 0u));
                ++ticks;
                if (m.engine.is_match_over()) ++after_end;
                if (cap != 0 && ticks >= cap) break;
            }
            ASSERT_TRUE(ticks > 1000);
            ASSERT_TRUE(copies >= 2);
            std::cout << "\n        " << file << ": " << ticks << " ticks, " << copies << " copies, the match is " << (m.engine.is_match_over() ? "over" : "not over") << "   ... ";
            ASSERT_NO_DIFF(diff_world(m.engine, twin));
        }
    } TEST_END();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The cost (--bench): prints, asserts nothing
// ---------------------------------------------------------------------------------------------------------------------------------

double us_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// The bytes that the process has allocated from the heap and not freed (0 where the bench does not know how to ask): the memory of a copy is measured as the growth with 64
// copies held. (The resident memory would not do: freed pages are reused, so the growth of the resident set hides what a copy takes.)
size_t heap_in_use_bytes() {
#if defined(__APPLE__)
    malloc_statistics_t stats;
    malloc_zone_statistics(nullptr, &stats);
    return stats.size_in_use;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return static_cast<size_t>(mallinfo2().uordblks);
#else
    return 0;
#endif
}

// The number of heap blocks that are in use (where the platform tells): a copy of an engine makes this many allocations, which is what costs most in a build with a slow allocator (wasm)
size_t heap_blocks_in_use() {
#if defined(__APPLE__)
    malloc_statistics_t stats;
    malloc_zone_statistics(nullptr, &stats);
    return stats.blocks_in_use;
#else
    return 0;
#endif
}

void run_bench() {
    std::cout << "sizeof(AntUnit) = " << sizeof(AntUnit) << " bytes, sizeof(TileCell) = " << sizeof(TileCell) << " bytes\n";
    std::cout << "Microseconds per operation: the BEST of many runs (a busy machine only makes a run longer, so the best is the cost; the mean is shown for the copy in brackets).\n";
    std::cout << "copy-assign: into an engine that already holds the same map, the rebuild of a prediction. tick: the next tick of that state.\n";
    std::cout << std::left << std::setw(14) << "map" << std::right << std::setw(8) << "tick" << std::setw(8) << "ants" << std::setw(16) << "copy-ctor" << std::setw(16) << "copy-assign"
              << std::setw(10) << "tick" << std::setw(10) << "hash" << std::setw(14) << "world-state" << "\n";
    // the best of `reps` runs of `op`, and their mean
    auto best_of = [](int reps, const std::function<void()>& before, const std::function<void()>& op, double* mean) {
        double best = 1e18;
        double sum = 0;
        for (int i = 0; i < reps; ++i) {
            if (before) before();
            const auto t0 = Clock::now();
            op();
            const double us = us_since(t0);
            best = std::min(best, us);
            sum += us;
        }
        if (mean != nullptr) *mean = sum / reps;
        return best;
    };
    for (const char* file : kMaps) {
        Run run;
        run.seed = 777;
        run.mix.percent_per_tick = 50;
        Match m(file, run);
        uint32_t at = 0;
        for (uint32_t checkpoint : {0u, 100u, 400u, 1500u, 4000u}) {
            m.play(checkpoint - at);
            at = checkpoint;
            const int reps = 300;
            double ctor_mean = 0;
            double assign_mean = 0;
            const double ctor = best_of(reps, nullptr, [&]() { SimulationEngine c(m.engine); }, &ctor_mean);
            SimulationEngine target(m.engine);
            const double assign = best_of(reps, nullptr, [&]() { target = m.engine; }, &assign_mean);
            const double tick = best_of(reps, [&]() { target = m.engine; }, [&]() { target.tick(); }, nullptr);
            const double hash = best_of(60, nullptr, [&]() { volatile uint64_t sink = m.engine.state_hash().total; (void)sink; }, nullptr);
            const double world = best_of(60, [&]() { target = m.engine; target.tick(); }, [&]() { volatile size_t sink = target.get_world_state().ants.size(); (void)sink; }, nullptr);
            auto cell = [](double best, double mean) {
                std::ostringstream os;
                os << std::fixed << std::setprecision(1) << best << " (" << mean << ")";
                return os.str();
            };
            std::cout << std::left << std::setw(14) << file << std::right << std::setw(8) << at << std::setw(8) << m.engine.get_world_state().ants.size() << std::fixed << std::setprecision(1)
                      << std::setw(16) << cell(ctor, ctor_mean) << std::setw(16) << cell(assign, assign_mean) << std::setw(10) << tick << std::setw(10) << hash << std::setw(14) << world << "\n";
        }
    }
    // the largest grid that the engine hosts (256 x 256, a community map's size) with 200 ants walking about: the upper bound of the cost
    {
        SimulationEngine big;
        big.init_test_world(256, 256, 5, 720000);
        Lcg rng(9);
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            for (int i = 0; i < 50; ++i) big.spawn_unit(p, AntType::Worker, TileCoord{static_cast<int32_t>(20 + rng.below(200)), static_cast<int32_t>(20 + rng.below(200))});
        }
        for (int round = 0; round < 3; ++round) {
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                Command c;
                c.type = CommandType::GroupMove;
                c.issuer = p;
                c.tile_x = static_cast<int16_t>(rng.below(256));
                c.tile_y = static_cast<int16_t>(rng.below(256));
                for (const AntSnapshot& a : big.get_world_state().ants) {
                    if (a.player_id == p && c.ants.size() < kMaxCommandAnts) c.ants.push_back(a.id);
                }
                big.apply_command(c);
            }
            for (int t = 0; t < 40; ++t) big.tick();
        }
        const int reps = 100;
        double ctor = 1e18;
        for (int i = 0; i < reps; ++i) {
            const auto t0 = Clock::now();
            SimulationEngine c(big);
            ctor = std::min(ctor, us_since(t0));
        }
        SimulationEngine target(big);
        double assign = 1e18;
        for (int i = 0; i < reps; ++i) {
            const auto t0 = Clock::now();
            target = big;
            assign = std::min(assign, us_since(t0));
        }
        double tick = 1e18;
        for (int i = 0; i < reps; ++i) {
            target = big;
            const auto t0 = Clock::now();
            target.tick();
            tick = std::min(tick, us_since(t0));
        }
        double world = 1e18;
        for (int i = 0; i < 30; ++i) {
            target = big;
            target.tick();
            const auto t0 = Clock::now();
            volatile size_t sink = target.get_world_state().ants.size();
            (void)sink;
            world = std::min(world, us_since(t0));
        }
        std::cout << std::left << std::setw(14) << "256x256 (200)" << std::right << std::setw(8) << big.current_tick() << std::setw(8) << big.get_world_state().ants.size() << std::fixed << std::setprecision(1)
                  << std::setw(16) << ctor << std::setw(16) << assign << std::setw(10) << tick << std::setw(10) << "-" << std::setw(14) << world << "\n";
    }
}

// The cost of EVERY tick of a match, not of one: the tail is what a frame budget has to live with (a tick that runs a slice of the path manager costs a hundred times a quiet one)
void run_bench_tick_distribution() {
    std::cout << "\nCost of one tick over 4000 ticks of a match with many orders (microseconds), and the first world state built on a new copy:\n";
    std::cout << std::left << std::setw(14) << "map" << std::right << std::setw(10) << "mean" << std::setw(10) << "median" << std::setw(10) << "p90" << std::setw(10) << "p99" << std::setw(10) << "max"
              << std::setw(22) << "first world state" << "\n";
    for (const char* file : kMaps) {
        Run run = quick_run(4242);
        Match m(file, run);
        std::vector<double> us;
        for (int t = 0; t < 4000; ++t) {
            if (m.run.score_bonus_every != 0 && m.engine.current_tick() % m.run.score_bonus_every == 0) give_points(m.engine);
            for (const Command& c : gen_commands(m.engine, m.rng, m.run.mix)) m.engine.apply_command(c);
            const auto t0 = Clock::now();
            m.engine.tick();
            us.push_back(us_since(t0));
        }
        std::sort(us.begin(), us.end());
        double sum = 0;
        for (double v : us) sum += v;
        double first_world = 0;
        const int reps = 100;
        for (int i = 0; i < reps; ++i) {
            SimulationEngine c(m.engine);
            const auto t0 = Clock::now();
            volatile size_t sink = c.get_world_state().ants.size();
            (void)sink;
            first_world += us_since(t0);
        }
        std::cout << std::left << std::setw(14) << file << std::right << std::fixed << std::setprecision(1) << std::setw(10) << sum / static_cast<double>(us.size()) << std::setw(10)
                  << us[us.size() / 2] << std::setw(10) << us[us.size() * 9 / 10] << std::setw(10) << us[us.size() * 99 / 100] << std::setw(10) << us.back() << std::setw(22) << first_world / reps
                  << "\n";
    }
}

// (the memory table follows the timing table in run_bench: see run_bench_memory)
void run_bench_memory() {
    std::cout << "\nMemory of one copy (heap bytes in use with 64 copies held, minus without them, divided by 64; at tick 1500):\n";
    for (const char* file : kMaps) {
        Run run = quick_run(777);
        Match m(file, run);
        m.play(1500);
        const size_t cells = m.engine.grid().cells().size();
        const size_t ants = m.engine.get_world_state().ants.size();
        std::vector<std::unique_ptr<SimulationEngine>> held;
        const size_t before = heap_in_use_bytes();
        const size_t blocks_before = heap_blocks_in_use();
        for (int i = 0; i < 64; ++i) held.emplace_back(new SimulationEngine(m.engine));
        const size_t after = heap_in_use_bytes();
        const size_t blocks_after = heap_blocks_in_use();
        std::cout << "  " << std::left << std::setw(14) << file << std::right << std::setw(7) << cells << " cells, " << std::setw(4) << ants << " ants: " << std::setw(6)
                  << (after > before ? (after - before) / 64 / 1024 : 0) << " KiB and " << (blocks_after > blocks_before ? (blocks_after - blocks_before) / 64 : 0)
                  << " allocations per copy (the cells alone: " << cells * sizeof(TileCell) / 1024 << " KiB)\n";
    }
}

// What a whole match of the generated commands exercises (--census): the statistics and the distinct cues, so that "the copy ticks identically through whole matches" is known to
// mean matches with hatching, fighting, scoring and drop-outs in them
void run_census() {
    std::cout << std::left << std::setw(14) << "map" << std::right << std::setw(8) << "ticks" << std::setw(10) << "hatched" << std::setw(8) << "lost" << std::setw(8) << "killed"
              << std::setw(10) << "score(sum)" << std::setw(10) << "ants_ever" << std::setw(10) << "cues" << std::setw(10) << "distinct" << std::setw(8) << "news" << std::setw(10) << "alliance"
              << "\n";
    uint32_t seed = 200;
    for (const char* file : kMaps) {
        Run run;
        run.seed = ++seed;
        Match m(file, run);
        std::map<uint32_t, uint32_t> sounds;
        size_t cues = 0;
        size_t news = 0;
        uint32_t ticks = 0;
        uint32_t alliance_seen = 0;
        size_t ants_ever = 0;
        while (!m.engine.is_match_over() && ticks < 20000) {
            m.play(1);
            ++ticks;
            const std::vector<AudioEvent> audio = m.engine.poll_audio_events();
            cues += audio.size();
            for (const AudioEvent& a : audio) ++sounds[a.sound_id];
            news += m.engine.poll_news_events().size();
            const WorldState& w = m.engine.get_world_state();
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                if (w.player_alliances[p] != ALLIANCE_NONE) {
                    ++alliance_seen;
                    break;
                }
            }
            ants_ever = std::max<size_t>(ants_ever, w.ants.size());
        }
        uint32_t hatched = 0;
        uint32_t lost = 0;
        uint32_t killed = 0;
        int32_t score = 0;
        for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
            const PlayerMatchStats st = m.engine.get_player_stats(p);
            hatched += st.ants_hatched;
            lost += st.friendly_lost;
            killed += st.enemy_killed;
            score += st.score;
        }
        std::cout << std::left << std::setw(14) << file << std::right << std::setw(8) << ticks << std::setw(10) << hatched << std::setw(8) << lost << std::setw(8) << killed << std::setw(10) << score
                  << std::setw(10) << ants_ever << std::setw(10) << cues << std::setw(10) << sounds.size() << std::setw(8) << news << std::setw(10) << alliance_seen << "\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    bool quick = true;
    bool whole = false;
    bool bench = false;
    bool census = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--whole-matches") == 0) {
            quick = false;
            whole = true;
        } else if (std::strcmp(argv[i], "--all") == 0) {
            quick = true;
            whole = true;
        } else if (std::strcmp(argv[i], "--bench") == 0) {
            quick = false;
            bench = true;
        } else if (std::strcmp(argv[i], "--census") == 0) {
            quick = false;
            census = true;
        } else {
            std::cout << "usage: test_engine_copy [--whole-matches | --all | --bench | --census]\n";
            return 2;
        }
    }
    if (bench) {
        run_bench();
        run_bench_tick_distribution();
        run_bench_memory();
        return 0;
    }
    if (census) {
        run_census();
        return 0;
    }
    std::cout << "\n=======================================================\n [ENGINE COPY SUITE] value semantics of SimulationEngine (" << (quick ? "quick" : "") << (quick && whole ? " + " : "")
              << (whole ? "whole matches" : "") << ")\n=======================================================\n";
    if (quick) {
        run_api_tests();
        run_copy_equals_source_tests();
        run_scene_tests();
        run_lockstep_tests();
        run_independence_tests();
        run_assignment_tests();
        run_edge_tests();
        run_path_manager_tests();
    }
    if (whole) run_whole_match_tests();
    std::cout << "\n=======================================================\n"
              << " ENGINE COPY TEST SUMMARY\n=======================================================\n"
              << " Total Test Cases: " << g_test_count << "\n Total Assertions: " << g_assert_count << "\n Passed:           " << (g_test_count - g_test_failures) << "\n Failed:           "
              << g_test_failures << "\n=======================================================\n";
    return g_test_failures == 0 ? 0 : 1;
}
