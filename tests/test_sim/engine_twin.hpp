// What the tests of the simulation's lookups (test_engine_lookups, test_engine_claims) share: a thread switched to the scans alone (LinearOnly), the twin of two engines that play one
// match with their state hashes compared at every tick (Twin, play), the crowds of ants and the hazards that give them every kind of order (populate, add_hazards, crowd_script,
// hazard_script). Every function is inline, so that each test program has its own copy and none carries what it does not use.
#pragma once

#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/command.hpp"
#include "ants_sim/lookup_shadow.hpp"
#include "ants_sim/sim_engine.hpp"
#include "scripted_players.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifndef ANTS_SIM_LOOKUP_SHADOW
#error "engine_twin.hpp is for the programs that link ants_sim_shadow"
#endif

namespace ants::sim::twin_test {

// Switches the calling thread to the scans alone while it lives
struct LinearOnly {
    LinearOnly() { lookup_shadow_linear_only() = true; }
    ~LinearOnly() { lookup_shadow_linear_only() = false; }
    LinearOnly(const LinearOnly&) = delete;
    LinearOnly& operator=(const LinearOnly&) = delete;
};

// Counts the stale events that the two nets of the heap put right instead of stopping the program, while it lives (lookup_shadow.hpp): for the tests that write the fields of the
// events from outside on purpose
struct CountStale {
    CountStale() { lookup_shadow_count_stale_events() = true; }
    ~CountStale() { lookup_shadow_count_stale_events() = false; }
    CountStale(const CountStale&) = delete;
    CountStale& operator=(const CountStale&) = delete;
};

// ---------------------------------------------------------------------------------------------------------------------------------
// Two engines, one match
// ---------------------------------------------------------------------------------------------------------------------------------

struct Twin {
    SimulationEngine old_engine;   // the scans alone
    SimulationEngine engine;       // the lookups, checked against the scans at every call
    uint64_t ticks{0};
    uint64_t pending_seen{0};      // ants with a queued path request at the sweeps (the flags had something to say)
    std::string failure;

    std::string describe(const StateHash& a, const StateHash& b) const {
        std::string s = "tick " + std::to_string(ticks) + ": the state hashes differ, in";
        const char* names[] = {"engine", "players", "grid", "food", "ants", "paths", "droppers"};
        const uint64_t av[] = {a.engine, a.players, a.grid, a.food, a.ants, a.paths, a.droppers};
        const uint64_t bv[] = {b.engine, b.players, b.grid, b.food, b.ants, b.paths, b.droppers};
        for (int i = 0; i < 7; ++i) {
            if (av[i] != bv[i]) s += std::string(" ") + names[i];
        }
        return s;
    }

    // A change that is not a command (a test hook: fire, bomb, kill, hatch, ...): made in both engines, in the old engine by the scans alone
    void both(const std::function<void(SimulationEngine&)>& change) {
        {
            LinearOnly scans;
            change(old_engine);
        }
        change(engine);
    }

    bool apply(const Command& c) {
        CommandResult ro;
        {
            LinearOnly scans;
            ro = old_engine.apply_command(c);
        }
        const CommandResult rn = engine.apply_command(c);
        if (ro.status != rn.status || ro.ack_ant != rn.ack_ant || ro.needing_order != rn.needing_order || ro.ants_ordered != rn.ants_ordered) {
            failure = "tick " + std::to_string(ticks) + ": a command is answered differently";
            return false;
        }
        return true;
    }

    // Every id that was ever handed out and a few that were not: the ants and the path queues (the checks run inside, the engines are compared here)
    bool sweep() {
        const uint64_t fallbacks = lookup_shadow_fallbacks();   // (the ids that were never handed out fall back on purpose; they are not counted)
        const bool ok = sweep_ids();
        lookup_shadow_fallbacks() = fallbacks;
        return ok;
    }
    bool sweep_ids() {
        const uint32_t last = static_cast<uint32_t>(engine.get_world_state().ants.size()) + 3u;
        for (uint32_t id = 0; id <= last; ++id) {
            bool pending_old, base_old;
            {
                LinearOnly scans;
                pending_old = old_engine.has_pending_path(id);
                base_old = old_engine.is_ant_in_base_queue(id);
            }
            if (pending_old != engine.has_pending_path(id) || base_old != engine.is_ant_in_base_queue(id)) {
                failure = "tick " + std::to_string(ticks) + ": the engines answer differently for ant " + std::to_string(id);
                return false;
            }
            if (pending_old) ++pending_seen;
        }
        return true;
    }

    bool tick() {
        {
            LinearOnly scans;
            old_engine.tick();
        }
        engine.tick();
        ++ticks;
        const StateHash a = old_engine.state_hash();
        const StateHash b = engine.state_hash();
        if (a != b || a.engine != b.engine || a.players != b.players || a.grid != b.grid || a.food != b.food || a.ants != b.ants || a.paths != b.paths || a.droppers != b.droppers) {
            failure = describe(a, b);
            return false;
        }
        return true;
    }
};

// What a match is given besides the scripted players: the commands of a tick, from the match's own state
using Script = std::function<void(Twin&, uint32_t tick, std::vector<Command>& out)>;

// Plays `ticks` ticks. The scripted players of the map sweep (tests/common/scripted_players.hpp) give their orders once a second, `extra` adds orders of the test; the engine is
// copied in the middle (a copy-constructed one, then an assigned one) because a copy has to rebuild the flags of its path queue. Returns "" or what went wrong.
inline std::string play(Twin& t, const ants::assets::LevelData* level, uint8_t roster, uint32_t ticks, const Script& extra, uint64_t* checks_out = nullptr) {
    std::unique_ptr<ants_test::ScriptedPlayers> players;
    if (level != nullptr) players = std::make_unique<ants_test::ScriptedPlayers>(*level, roster, 0x5EEDu, true);
    const uint64_t checks0 = lookup_shadow_checks();
    const uint64_t fallbacks0 = lookup_shadow_fallbacks();
    std::vector<Command> cmds;
    for (uint32_t i = 0; i < ticks; ++i) {
        cmds.clear();
        if (players && i % 20 == 0) {
            players->orders(t.engine, cmds);                           // (the engines are equal, so one tells what both see; it tops up the scores of the engine it is given)
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) t.old_engine.set_player_score(p, t.engine.get_player_score(p));
        }
        if (extra) extra(t, i, cmds);
        for (const Command& c : cmds) {
            if (!t.apply(c)) return t.failure;
        }
        if (!t.tick()) return t.failure;
        if (i % 20 == 0 && !t.sweep()) return t.failure;
        if (i == ticks / 3) t.engine = SimulationEngine(t.engine);                 // copy construction, then move
        if (i == 2 * ticks / 3) {
            SimulationEngine other(t.engine);
            t.engine = other;                                                      // copy assignment
            t.old_engine = SimulationEngine(t.old_engine);
        }
    }
    if (checks_out) *checks_out = lookup_shadow_checks() - checks0;
    if (lookup_shadow_fallbacks() != fallbacks0) return "find_unit fell back to the scan " + std::to_string(lookup_shadow_fallbacks() - fallbacks0) + " times: the slot of the id should always answer";
    return "";
}

inline const ants::assets::LevelData& library_map(const std::string& dir, const char* file) {
    static std::vector<std::pair<std::string, ants::assets::LevelData>> cache;
    const std::string path = dir + "/" + std::string(file);
    for (auto& e : cache) {
        if (e.first == path) return e.second;
    }
    ants::assets::LevelData lvl;
    if (!ants::assets::LVLParser::load_from_file(path, lvl, nullptr, ants::assets::LevelLimits::for_extended_maps())) {
        std::cout << "cannot load the map " << path << "\n";
        std::exit(2);
    }
    cache.emplace_back(path, std::move(lvl));
    return cache.back().second;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Worlds with many ants, built here: `per_team` ants of each of the four teams
// ---------------------------------------------------------------------------------------------------------------------------------

enum class Layout { Packed, Spread, Piled };

struct Crowd {
    std::vector<std::vector<uint32_t>> ids{4};        // the ants of each team, in the order they were made
    std::vector<std::vector<uint32_t>> special{4};    // the typed ants that add_hazards made (bombers, fire ants, thieves, combat ants, swimmers), also in order
    std::vector<uint32_t> pit;                        // the ants beside the bombs (40, team i % 4), beside the power-ups (8) and the river (8 swimmers), then the combat ants and workers of the melees
    uint32_t side{0};
};

// Packed: a solid square of ants in each corner of a width x width world. Spread: the four teams on distinct tiles over the whole middle of the map, mixed together (the
// start markers of a big player's map are like this: every ant has enemies next door).
inline Crowd populate(SimulationEngine& e, uint32_t width, uint32_t per_team, Layout layout) {
    Crowd crowd;
    uint32_t s = 4242;
    auto below = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 8) % n; };
    if (layout == Layout::Packed) {
        uint32_t side = 1;
        while (side * side < per_team) ++side;
        crowd.side = side;
        for (uint8_t p = 0; p < 4; ++p) {
            const int32_t ox = (p & 1) != 0 ? static_cast<int32_t>(width - 2 - side) : 2;
            const int32_t oy = (p & 2) != 0 ? static_cast<int32_t>(width - 2 - side) : 2;
            uint32_t n = 0;
            for (uint32_t y = 0; y < side && n < per_team; ++y) {
                for (uint32_t x = 0; x < side && n < per_team; ++x, ++n) {
                    crowd.ids[p].push_back(e.spawn_unit(p, AntType::Worker, TileCoord{ox + static_cast<int32_t>(x), oy + static_cast<int32_t>(y)}, true));
                }
            }
        }
    } else if (layout == Layout::Piled) {
        // piles: six ants of every team on the same tile, the tiles two apart over the middle of the map (the ants of a pile depart one by one, each from a tile that holds several)
        const uint32_t lo = 12, cols = (width - 24) / 2;
        for (uint8_t p = 0; p < 4; ++p) {
            for (uint32_t i = 0; i < per_team; ++i) {
                const uint32_t k = i / 6;
                crowd.ids[p].push_back(e.spawn_unit(p, AntType::Worker, TileCoord{static_cast<int32_t>(lo + 2 * (k % cols)), static_cast<int32_t>(lo + 2 * (k / cols))}, true));
            }
        }
    } else {
        const uint32_t lo = 12, span = width - 24;
        std::vector<uint8_t> used(static_cast<size_t>(width) * width, 0);
        for (uint8_t p = 0; p < 4; ++p) {
            for (uint32_t i = 0; i < per_team; ++i) {
                for (;;) {
                    const uint32_t x = lo + below(span);
                    const uint32_t y = lo + below(span);
                    if (used[static_cast<size_t>(y) * width + x] != 0) continue;
                    used[static_cast<size_t>(y) * width + x] = 1;
                    crowd.ids[p].push_back(e.spawn_unit(p, AntType::Worker, TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)}, true));
                    break;
                }
            }
        }
    }
    return crowd;
}

// Orders of a crowd, in groups of 32 ants (the most one command takes): all ants of every team across the world at tick 3, the first half of every team stopped at tick 100 (a
// request that is queued is replaced by the stop or by the next order), all of them sent to another tile at tick 130, and attack orders at the middle of the map at tick 260
inline Script crowd_script(const Crowd& crowd, uint32_t width) {
    return [crowd, width](Twin&, uint32_t tick, std::vector<Command>& out) {
        const int16_t far = static_cast<int16_t>(width - 5);
        auto groups = [&](uint8_t p, size_t from, size_t to, CommandType type, int16_t x, int16_t y) {
            const std::vector<uint32_t>& ids = crowd.ids[p];
            for (size_t i = from; i < to && i < ids.size(); i += kMaxCommandAnts) {
                Command c;
                c.type = type;
                c.issuer = p;
                c.tile_x = x;
                c.tile_y = y;
                for (size_t j = i; j < std::min({ids.size(), to, i + kMaxCommandAnts}); ++j) c.ants.push_back(ids[j]);
                out.push_back(c);
            }
        };
        for (uint8_t p = 0; p < 4; ++p) {
            const size_t n = crowd.ids[p].size();
            const int16_t ox = (p & 1) != 0 ? 4 : far;
            const int16_t oy = (p & 2) != 0 ? 4 : far;
            if (tick == 3) groups(p, 0, n, CommandType::GroupMove, ox, oy);
            if (tick == 100) groups(p, 0, n / 2, CommandType::Stop, 0, 0);
            if (tick == 130) groups(p, 0, n, CommandType::GroupMove, static_cast<int16_t>(width / 2 + p), static_cast<int16_t>(width / 2 - p));
            if (tick == 260) groups(p, n / 4, n / 2, CommandType::GroupAttack, static_cast<int16_t>(width / 2), static_cast<int16_t>(width / 2));
        }
    };
}

// The hazards of a world with a crowd: a hill for each team (the hatching of the newborn, 40 ticks), a pond that ants fall into, and in the middle of the world, where the
// crowds cross, ten ants of each typed kind for each team (bombers plant, fire ants light, thieves raid, combat ants fight back, swimmers build bridges and swim).
// The same calls are made in both engines, in the same order.
inline void add_hazards(SimulationEngine& e, uint32_t width, Crowd& crowd) {
    const int32_t mid = static_cast<int32_t>(width / 2);
    e.set_hatch_delay_ticks(40);
    e.set_anthill(0, TileCoord{mid - 2, 6});
    e.set_anthill(1, TileCoord{mid - 2, static_cast<int32_t>(width) - 10});
    e.set_anthill(2, TileCoord{6, mid - 2});
    e.set_anthill(3, TileCoord{static_cast<int32_t>(width) - 10, mid - 2});
    for (int32_t y = mid + 6; y < mid + 12; ++y) {
        for (int32_t x = mid + 6; x < mid + 12; ++x) e.set_terrain(x, y, 2);   // water
    }
    for (int32_t y = mid - 12; y <= mid - 10; ++y) {
        for (int32_t x = mid - 10; x <= mid + 6; ++x) e.set_terrain(x, y, 2);  // a river that the swimmers bridge
    }
    for (int32_t i = 0; i < 8; ++i) e.grid_mut().place_powerup(mid - 16 + 3 * i, mid + 8, static_cast<uint8_t>(i % 5));   // power-ups on a row below the middle
    for (int32_t i = 0; i < 40; ++i) {                                          // a row of bombs, each with an ant two tiles below it: sent onto its bomb, 20 % of the bombs are duds (the ant burns)
        const uint8_t p = static_cast<uint8_t>(i % 4);
        e.grid_mut().place_bomb(static_cast<uint32_t>(mid - 20 + i), static_cast<uint32_t>(mid + 14), static_cast<uint8_t>((p + 1) % 4));
        crowd.pit.push_back(e.spawn_unit(p, AntType::Worker, TileCoord{mid - 20 + i, mid + 16}, false));
    }
    for (int32_t i = 0; i < 8; ++i) {                                           // workers two tiles from the power-ups, sent onto them
        crowd.pit.push_back(e.spawn_unit(static_cast<uint8_t>(i % 4), AntType::Worker, TileCoord{mid - 16 + 3 * i, mid + 10}, false));
    }
    for (int32_t i = 0; i < 8; ++i) {                                           // swimmers on the bank of the river, sent to dig into it (a bridge)
        crowd.pit.push_back(e.spawn_unit(static_cast<uint8_t>(i % 4), AntType::Swimmer, TileCoord{mid - 8 + 2 * i, mid - 9}, false));
    }
    for (int32_t k = 0; k < 4; ++k) {                                           // melees: a combat ant of team 0 beside a worker of team 1 (the combat ant strikes by its own reflex, the combat timer)
        crowd.pit.push_back(e.spawn_unit(0, AntType::Combat, TileCoord{mid - 30 + 4 * k, mid - 24}, false));
        crowd.pit.push_back(e.spawn_unit(1, AntType::Worker, TileCoord{mid - 28 + 4 * k, mid - 24}, false));
    }
    const AntType kinds[] = {AntType::Bomber, AntType::Fire, AntType::Thief, AntType::Combat, AntType::Swimmer};
    for (uint8_t p = 0; p < 4; ++p) {
        int32_t i = 0;
        for (const AntType kind : kinds) {
            for (int32_t k = 0; k < 10; ++k, ++i) {
                crowd.special[p].push_back(e.spawn_unit(p, kind, TileCoord{mid - 20 + i % 25, mid - 8 + 4 * p + i / 25}, false));
            }
        }
    }
}

// The loco actions that the typed ants and the ants of the pit were seen in (so that a test shows that power-ups were taken and bridges dug)
struct Seen {
    std::set<uint8_t> actions;
};
inline Seen g_seen;

// What happens besides the orders of crowd_script (the same calls in both engines): a fire wall across the middle of the world at tick 30 and bombs beside it at tick 31 (ants
// that walk or are thrown onto them, two ants of a team sent to each bomb at tick 33: some bombs are duds, the ant burns), the typed ants sent to the middle at tick 5, the
// swimmers ordered onto the river at ticks 25 and 125 (a bridge, then it is torn down) and, the typed ants of the crowd, at 60 and 160, the workers of the pit onto the power-ups at tick 20, special orders at tick 220, an
// egg hatched for each team every 45 ticks, a third of the ants of team 1 killed at tick 200 (a mass death) and team 2 leaving the match at tick 270 (all its ants die)
inline Script hazard_script(const Crowd& crowd, uint32_t width) {
    const Script orders = crowd_script(crowd, width);
    return [crowd, width, orders](Twin& t, uint32_t tick, std::vector<Command>& out) {
        orders(t, tick, out);
        const int32_t mid = static_cast<int32_t>(width / 2);
        std::vector<uint32_t> watched = crowd.pit;
        for (uint8_t p = 0; p < 4; ++p) watched.insert(watched.end(), crowd.special[p].begin(), crowd.special[p].end());
        for (const uint32_t id : watched) {
            const AntUnit& u = t.engine.get_unit(id);
            if (u.is_alive()) g_seen.actions.insert(u.loco_action);
        }
        auto some = [&](uint8_t p, const std::vector<uint32_t>& ids, size_t from, size_t count, CommandType type, int32_t x, int32_t y) {
            Command c;
            c.type = type;
            c.issuer = p;
            c.tile_x = static_cast<int16_t>(x);
            c.tile_y = static_cast<int16_t>(y);
            for (size_t j = from; j < from + count && j < ids.size(); ++j) c.ants.push_back(ids[j]);
            if (!c.ants.empty()) out.push_back(c);
        };
        if (tick == 15) {
            for (size_t i = 0; i < 40 && i < crowd.pit.size(); ++i) {
                some(static_cast<uint8_t>(i % 4), crowd.pit, i, 1, CommandType::GroupMove, mid - 20 + static_cast<int32_t>(i), mid + 14);
            }
        }
        if (tick == 20) {
            for (size_t i = 0; i < 8 && 40 + i < crowd.pit.size(); ++i) {
                some(static_cast<uint8_t>(i % 4), crowd.pit, 40 + i, 1, CommandType::GroupMove, mid - 16 + 3 * static_cast<int32_t>(i), mid + 8);
            }
        }
        if (tick == 33) {
            for (int32_t j = 0; j < 15; ++j) some(static_cast<uint8_t>(j % 4), crowd.ids[static_cast<size_t>(j % 4)], static_cast<size_t>(2 * (j / 4)), 2, CommandType::GroupMove, mid - 14 + 2 * j, mid - 4);
        }
        if (tick == 25 || tick == 125) {
            for (size_t i = 0; i < 8 && 48 + i < crowd.pit.size(); ++i) {
                some(static_cast<uint8_t>(i % 4), crowd.pit, 48 + i, 1, CommandType::GroupSpecial, mid - 8 + 2 * static_cast<int32_t>(i), mid - 10);
            }
        }
        if (tick == 60 || tick == 160) {
            for (uint8_t p = 0; p < 4; ++p) {
                const std::vector<uint32_t>& sp = crowd.special[p];
                if (sp.size() >= 50) some(p, sp, 40, 10, CommandType::GroupSpecial, mid - 8 + 4 * p, mid - 11);
            }
        }
        auto special_group = [&](uint8_t p, CommandType type, int32_t x, int32_t y) {
            const std::vector<uint32_t>& ids = crowd.special[p];
            for (size_t i = 0; i < ids.size(); i += kMaxCommandAnts) {
                Command c;
                c.type = type;
                c.issuer = p;
                c.tile_x = static_cast<int16_t>(x);
                c.tile_y = static_cast<int16_t>(y);
                for (size_t j = i; j < std::min(ids.size(), i + kMaxCommandAnts); ++j) c.ants.push_back(ids[j]);
                out.push_back(c);
            }
        };
        if (tick % 45 == 10) {
            t.both([](SimulationEngine& e) {
                for (uint8_t p = 0; p < 4; ++p) e.try_hatch(p, AntType::Worker, true);
            });
        }
        if (tick == 5) {
            for (uint8_t p = 0; p < 4; ++p) special_group(p, CommandType::GroupMove, mid - 6 + 4 * p, mid + 2);
        }
        if (tick == 30) {
            t.both([mid](SimulationEngine& e) {
                for (int32_t x = mid - 14; x <= mid + 14; ++x) e.set_fire_at(TileCoord{x, mid}, 2000);
            });
        }
        if (tick == 31) {
            t.both([mid](SimulationEngine& e) {
                for (int32_t x = mid - 14; x <= mid + 14; x += 2) e.grid_mut().place_bomb(static_cast<uint32_t>(x), static_cast<uint32_t>(mid - 4), 0);
            });
        }
        if (tick == 200) {
            t.both([&crowd](SimulationEngine& e) {
                for (size_t i = 0; i < crowd.ids[1].size(); i += 3) e.kill_unit(crowd.ids[1][i]);
            });
        }
        if (tick == 220) {
            for (uint8_t p = 0; p < 4; ++p) special_group(p, CommandType::GroupSpecial, mid - 3 + 2 * p, mid - 4);
        }
        if (tick == 270) t.both([](SimulationEngine& e) { e.drop_player(2); });
    };
}

inline std::string play_crowd(uint32_t width, uint32_t per_team, Layout layout, uint32_t ticks, uint64_t* checks, uint64_t* pending_seen) {
    g_seen = Seen{};
    Twin t;
    t.engine.init_test_world(width, width, 9);
    Crowd crowd = populate(t.engine, width, per_team, layout);
    add_hazards(t.engine, width, crowd);
    Crowd same;
    {
        LinearOnly scans;   // (the twin is set up by the scans alone too)
        t.old_engine.init_test_world(width, width, 9);
        same = populate(t.old_engine, width, per_team, layout);
        add_hazards(t.old_engine, width, same);
    }
    if (same.ids != crowd.ids || same.special != crowd.special || same.pit != crowd.pit) return "the two worlds hold different ants";
    const std::string why = play(t, nullptr, 0x0F, ticks, hazard_script(crowd, width), checks);
    *pending_seen = t.pending_seen;
    return why;
}

}  // namespace ants::sim::twin_test
