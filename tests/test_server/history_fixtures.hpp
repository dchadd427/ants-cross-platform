// Shared by the match history's tests that need real matches and a database (test_history_store.cpp, test_history_rooms.cpp): recordings made with the engine on the shipped TINY map, a temporary
// folder, a database of the server's schema in it, and the replay store, history store and feeder between them. Included once by each test program; everything is in an unnamed namespace.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ANTS_HISTORY_GETPID _getpid
#else
#include <unistd.h>
#define ANTS_HISTORY_GETPID getpid
#endif

#include "ants_ai/arena.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_db/database.hpp"
#include "ants_db/migrations.hpp"
#include "ants_net/netgame.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/recorder.hpp"
#include "ants_replay/replay.hpp"
#include "ants_server/control.hpp"
#include "ants_server/db_schema.hpp"
#include "ants_server/history_store.hpp"
#include "ants_server/map_store.hpp"
#include "ants_server/replay_store.hpp"
#include "ants_server/room_manager.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_sim/start_teams.hpp"
#include "ants_test_paths.hpp"

namespace {

namespace fs = std::filesystem;
using namespace ants;
using namespace ants::server;

constexpr int64_t kT0 = 1791469929;       // 2026-10-08 14:32:09 UTC

// A folder of the test's own; removed (and everything in it) when the object goes. A database in it must be closed first: declare the folder before the database, so that the database goes first.
struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        path = fs::temp_directory_path() / ("ants_test_history_" + std::to_string(ANTS_HISTORY_GETPID()) + "_" + std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

[[maybe_unused]] bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

[[maybe_unused]] size_t lines_with(const std::vector<std::string>& lines, const std::string& part) {
    size_t n = 0;
    for (const std::string& l : lines) n += contains(l, part) ? 1u : 0u;
    return n;
}

[[maybe_unused]] ctl::JsonValue parse_text(const std::string& text) {
    ctl::JsonValue v;
    std::string why;
    ctl::JsonLimits limits;
    limits.max_bytes = 1024u * 1024u;
    limits.max_elements = 16384;
    if (!ctl::parse_json(text, v, &why, limits)) return ctl::JsonValue::make_null();
    return v;
}

[[maybe_unused]] std::string maps_dir() { return std::string(ORIGINAL_ASSETS_DIR) + "/Maps"; }

// "ants-TINY-20261008-143209Z" (and "-2" for a second match of that map that ended in the same second)
[[maybe_unused]] std::string id_at(int64_t t, const std::string& map = "TINY", int n = 1) {
    return "ants-" + map + "-" + ReplayStore::time_stamp(t) + (n > 1 ? "-" + std::to_string(n) : std::string());
}

// A database of the server's schema in `path` ("" is a memory database); the object stays closed (and false is returned) when that is not possible
[[maybe_unused]] bool open_server_db(db::Database& database, const std::string& path, std::string* why = nullptr) {
    const db::Error e = path.empty() ? db::open_and_migrate(database, ":memory:", db_migrations(), db::OpenOptions()) : db::open_and_migrate(database, path, db_migrations(), db::OpenOptions());
    if (!e.ok() && why != nullptr) *why = e.message;
    return e.ok();
}

// ---- matches played with the engine (real recordings) ------------------------------------------------------------------------------

struct MatchSpec {
    uint32_t seed{7};
    uint32_t turns{1200};
    uint8_t roster{0x0F};
    sim::StartTeams teams;
    int quit_seat{-1};                                  // that seat quits after the last tick (a Quit that ends a match of two seats)
    std::array<std::string, sim::MAX_PLAYERS> names;
    uint16_t sim_rules{replay::kSimRules};
};

// A match on the shipped TINY map: the standard computer players gather food (the scores move), and on top of what they do every seat hatches an ant every 100 ticks and, from tick 1000, sends all its ants
// against an ant of the next seat every 200 ticks (so that ants are hatched, killed and lost), recorded as the server's rooms record one. Empty when the map is not there.
[[maybe_unused]] std::vector<uint8_t> record_match(const MatchSpec& spec) {
    const std::string path = maps_dir() + "/TINY.LVL";
    assets::LevelData level;
    uint64_t hash = 0;
    if (!level.load_from_file(path) || !net::hash_file(path, hash)) return std::vector<uint8_t>();
    ai::ArenaSpec arena;
    arena.level = &level;
    arena.seed = spec.seed;
    arena.max_ticks = spec.turns;
    arena.record = true;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (((spec.roster >> seat) & 1u) != 0) arena.bots.push_back(ai::BotSpec{seat, "standard", ai::Level::Hard, ai::Style::Random});
    }
    const ai::ArenaResult played = ai::play_match(arena);
    if (!played.error.empty()) return std::vector<uint8_t>();
    replay::Replay shell;
    shell.head.game_version = "v0.12.0-test";
    shell.head.build_id = "test";
    shell.head.venue = "game server";
    shell.head.map_name = "TINY.LVL";
    shell.head.map_hash = hash;
    shell.head.seed = spec.seed;
    shell.head.roster = spec.roster;
    shell.head.names = spec.names;
    shell.head.teams = spec.teams;
    shell.head.sim_rules = spec.sim_rules;
    sim::SimulationEngine engine;
    replay::begin_match(engine, shell, level, false);                // (as the player sets a match up)
    replay::Recorder rec(shell.head);
    size_t next = 0;
    for (uint64_t step = 0; step < played.steps; ++step) {
        std::vector<sim::Command> extra;
        if (step >= 200 && step % 100 == 50) {
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                if (((spec.roster >> seat) & 1u) == 0) continue;
                sim::Command hatch;
                hatch.type = sim::CommandType::Hatch;
                hatch.issuer = seat;
                extra.push_back(hatch);
            }
        }
        if (step >= 1000 && step % 200 == 100) {
            const sim::WorldState& world = engine.get_world_state();
            for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
                if (((spec.roster >> seat) & 1u) == 0) continue;
                sim::Command attack;
                attack.type = sim::CommandType::GroupAttack;
                attack.issuer = seat;
                for (const sim::AntSnapshot& a : world.ants) {
                    if (a.player_id == seat && a.hp > 0 && attack.ants.size() < sim::kMaxCommandAnts) attack.ants.push_back(a.id);
                    if (a.player_id != seat && a.hp > 0 && ((spec.roster >> a.player_id) & 1u) != 0 && attack.tile_x == 0) {
                        attack.tile_x = static_cast<int16_t>(a.tile_x);
                        attack.tile_y = static_cast<int16_t>(a.tile_y);
                    }
                }
                if (!attack.ants.empty() && attack.tile_x != 0) extra.push_back(attack);
            }
        }
        for (const sim::Command& c : extra) {
            engine.apply_command(c);
            rec.on_command(c);
        }
        for (; next < played.log.size() && played.log[next].step == step; ++next) {
            engine.apply_command(played.log[next].command);
            rec.on_command(played.log[next].command);
        }
        engine.tick();
        engine.clear_news_events();
        engine.clear_audio_events();
        rec.on_tick(engine);
    }
    for (; next < played.log.size(); ++next) {                       // (orders that the arena gave after its last tick: the recording holds them at the turn that the match ended in)
        engine.apply_command(played.log[next].command);
        rec.on_command(played.log[next].command);
    }
    if (spec.quit_seat >= 0) {
        sim::Command quit;
        quit.type = sim::CommandType::Quit;
        quit.issuer = static_cast<uint8_t>(spec.quit_seat);
        engine.apply_command(quit);
        rec.on_command(quit);
    }
    std::string error;
    return rec.finish(engine, error);
}

// Four recordings that the tests share (made once): 0 four people free for all, 1 two people, 2 two people where the second one quits, 3 four people in two teams
[[maybe_unused]] const std::vector<uint8_t>& match_file(int k) {
    static std::map<int, std::vector<uint8_t>> cache;
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    MatchSpec spec;
    switch (k) {
        case 0:
            spec.seed = 11;
            spec.turns = 0;                                         // (until the rules end it)
            spec.names = {"Ann", "", "Bot (Hard)", "Bot"};
            break;
        case 1:
            spec.seed = 12;
            spec.turns = 1000;
            spec.roster = 0x03;
            spec.names = {"Cy", "Di", "", ""};
            break;
        case 2:
            spec.seed = 13;
            spec.turns = 800;
            spec.roster = 0x03;
            spec.quit_seat = 1;
            spec.names = {"Ed", "Flo", "", ""};
            break;
        default:
            spec.seed = 14;
            spec.turns = 900;
            spec.teams = sim::StartTeams{true, 0, 1};
            spec.names = {"Gil", "Hob", "Ivy", "Jon"};
            break;
    }
    return cache.emplace(k, record_match(spec)).first->second;
}

// A recording as the feeder reads it: decoded and played once
struct Played {
    bool ok{false};
    replay::Replay rep;
    replay::Outcome outcome;
};

[[maybe_unused]] Played play_file(const std::vector<uint8_t>& bytes) {
    Played p;
    std::string why;
    if (!replay::decode(bytes.data(), bytes.size(), p.rep, why)) return p;
    assets::LevelData level;
    if (!replay::load_map(p.rep.head, maps_dir(), level, why)) return p;
    p.outcome = replay::play(p.rep, level);
    p.ok = p.outcome.ran && p.outcome.ok;
    return p;
}


}  // namespace
