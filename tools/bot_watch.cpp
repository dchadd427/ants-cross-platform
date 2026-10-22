// bot_watch: plays matches of computer players like bot_arena and WATCHES them (docs/BOT_FIGHT.md). It counts, per seat and per minute of game time, what the bots do badly:
// ants that stand idle or stuck, kills that were within reach and were not taken (above all an enemy on a tile next to water that a blow would throw in), the time that goes to
// fire-wall chores and other tasks, and the plain losses (drowned, blown up, lone ants in a crowd). The observer only reads the engine after each tick (ArenaSpec::on_tick), so it
// changes nothing: a match under watch has the same state hash as the same match in bot_arena.
//
// Usage:
//   bot_watch [--map NAMES] [--maps-dir DIR] [--seeds A..B] --seat N=SPEC ... [--rotate] [--threads N] [--ticks full|N] [--latency-ticks N] [--out FILE] [--quiet]
//   bot_watch --mapinfo NAMES [--maps-dir DIR]     (and --ascii NAMES: the map as letters)
//   bot_watch --replay FILE... [--maps-dir DIR] [--out FILE]     the same counters over recordings (a person's game against bots too: the bots' ants have no task names there)
// SPEC is as in bot_arena (KIND:LEVEL[:STYLE]); KIND may be standard+K=V,K=V (the keys of apply_tune, tools/bot_arena_tune.cpp), `styled=1` draws the style like the registry's bot.
// --rotate plays every distinct arrangement of the specs over the seats. The report is JSON (format 1): per match, per seat, the counters, the per-minute rows and the worst scenes.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ants_ai/arena.hpp"
#include "ants_ai/bot.hpp"
#include "ants_ai/island_info.hpp"
#include "ants_ai/knock.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/standard_bot.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_replay/player.hpp"
#include "ants_replay/replay.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"
#include "bot_arena_tune.hpp"
#include "library_guard.hpp"
#include <cstdarg>
#include <cctype>

namespace fs = std::filesystem;
namespace ai = ants::ai;
namespace sim = ants::sim;
namespace assets = ants::assets;

namespace {

constexpr uint64_t kIdleTicks = 100;       // an ant that stands with no order for this long (5 s) is idle
constexpr uint64_t kStuckTicks = 200;      // an ant with an order that stays on at most two tiles for this long (10 s) is stuck
constexpr uint64_t kEpisodeMin = 20;       // a chance must last this long (1 s) to count: a bot looks every 4 ticks and its orders leave 8 ticks later
constexpr uint64_t kEpisodeGap = 12;       // a chance that is gone for this long is over
constexpr uint64_t kSamplePeriod = 2;      // the chances are looked for every 2nd tick
constexpr size_t kMinutes = 31;            // 0 .. 30 minutes of game time (a minute is 1,200 ticks)
constexpr int32_t kNear = 3;               // "within reach": the nearest free ant is at most this many tiles from the tile the blow comes from
constexpr int32_t kFar = 8;

std::string fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    return buf;
}

std::string json_quote(const std::string& s) {
    std::string o = "\"";
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (u < 0x20 || u >= 0x7F) o += fmt("\\u%04x", u);
        else o += c;
    }
    return o + "\"";
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool parse_uint(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    out = v;
    return true;
}

const char* kTaskNames[32] = {"free", "harvest", "fight", "aid", "walls", "powerups", "raids", "guard", "strike", "bombs", "hatch", "gate", "harass", "sabotage", "islands",
                              "expedition", "ferry", "mines", "rush", "gate_goals", "field", "brawl", "opportunity", "t23", "t24", "t25", "t26", "t27", "t28", "t29", "t30", "other"};

enum Kind : uint8_t { kWater = 0, kBomb = 1, kFire = 2, kWeak = 3, kCarrier = 4, kWater1 = 5, kKinds = 6 };      // (kWater1: the water chance of a plain blow, the enemy stands next to the water; kWater also has the Combat Ant's punch of four tiles)
const char* kKindNames[kKinds] = {"water", "bomb", "fire", "weak", "carrier", "water1"};

// ---- the counters of one seat ---------------------------------------------------------------------------------------------------------------------------------------------------------
struct Counters {
    uint64_t ant_ticks{0};                    // living own ants, summed over the ticks
    uint64_t idle_ticks{0};                   // of them, idle (no order) for at least kIdleTicks
    uint64_t idle_food_ticks{0};              // ... while food lay on the map
    uint64_t idle_reach_ticks{0};             // ... while food that this seat's hill can walk to lay on the map (the analysis of the start, as the bot has it)
    uint64_t idle_enemy_ticks{0};             // ... while an enemy ant that can be attacked stood within 20 tiles and a walk of the ant's component reaches it (something to do that the bot did not do)
    uint64_t idle_enemy_unreach_ticks{0};     // ... while one stood within 20 tiles and no walk reaches it (the attack would be refused)
    uint64_t idle_free_ticks{0};              // ... held by no task (the bot had nothing for them)
    uint64_t idle_held_ticks{0};              // ... held by a task (a wait that the bot meant, or a task that forgot them)
    uint64_t carry_idle_ticks{0};             // carrying food and idle for kIdleTicks
    uint64_t stuck_ticks{0};                  // an order and the same two tiles for kStuckTicks
    uint64_t stuck_ants{0};                   // ants that were ever stuck
    uint64_t cantgo_ticks{0};                 // ant ticks in the can't-go clip
    uint64_t contact_idle_ticks{0};           // a free ant stands next to an enemy ant and does nothing about it
    uint64_t fire_chore_ticks{0};             // ant ticks with a fire-wall order (walk and work)
    uint64_t fire_ant_ticks{0};               // ant ticks of Fire Ants
    uint64_t fire_ant_idle_ticks{0};
    uint64_t walls_started{0};                // fire walls begun (home: within 6 tiles of the own hill, enemy: within 6 of an enemy hill)
    uint64_t walls_home{0};
    uint64_t walls_enemy{0};
    uint64_t walls_pre_thief{0};              // ... of the home ones, begun before any enemy Thief was seen anywhere
    uint64_t outs_started{0};                 // fires put out
    uint64_t outs_home{0};
    uint64_t drown_kills{0};                  // enemy ants that drowned with this seat's blow the last damage
    uint64_t drown_own{0};                    // own ants that drowned
    uint64_t bomb_victims{0};                 // own ants blown up
    uint64_t fire_hits{0};                    // own ants that lost a hit point on a fire wall
    uint64_t ant_deaths{0};
    uint64_t carrier_deaths{0};
    uint64_t lone_deaths{0};                  // died with no own ant within 4 tiles and two or more enemy ants there
    uint64_t outnumbered_deaths{0};           // died with at least twice as many enemy ants within 4 tiles as own
    std::array<uint64_t, kKinds> ep{};        // chances of at least kEpisodeMin ticks (a free ant within kFar tiles)
    std::array<uint64_t, kKinds> ep_near{};   // ... with the nearest free ant within kNear
    std::array<uint64_t, kKinds> ep_att{};    // ... in which the seat ordered an attack on that ant
    std::array<uint64_t, kKinds> ep_near_att{};
    std::array<uint64_t, kKinds> ep_still{};  // ... the enemy stood still
    uint64_t water_conv{0};                   // water chances that ended with the enemy drowned by this seat
    std::array<std::array<uint64_t, 32>, kKinds> epm{};   // every chance by its flags: bit 0 the enemy stood still, 1 the nearest free ant within kNear, 2 that ant was held by no task or by the economy, the guard, the ferry or a fight (the fight task may take it), 3 an attack was ordered, 4 that ant stood idle
    std::array<uint64_t, 32> task_ticks{};    // ant ticks by the task that holds the ant (0: none)
    std::array<uint64_t, 32> task_idle_ticks{};
    std::array<uint64_t, 32> task_stuck_ticks{};    // stuck ant ticks by task
    std::array<uint64_t, 32> task_cantgo_ticks{};   // can't-go ant ticks by task
    std::array<uint64_t, kMinutes> m_ants{};
    std::array<uint64_t, kMinutes> m_idle{};
    std::array<uint64_t, kMinutes> m_stuck{};
    std::array<uint64_t, kMinutes> m_chore{};
    std::array<uint64_t, kMinutes> m_water_ep{};
    std::array<uint64_t, kMinutes> m_water_att{};
    std::array<uint64_t, kMinutes> m_kills_missed{};
};

struct Ep {
    uint64_t start{0};
    uint64_t last{0};
    bool attacked{false};
    bool conv{false};
    int32_t best{99};                        // the nearest free ant to the stand tile
    uint32_t samples{0};
    uint32_t still{0};
    bool scene_done{false};
    std::string scene;
    uint8_t seat{0};
    uint8_t kind{0};
    uint32_t enemy{0};
    uint8_t enemy_type{0};
    uint8_t enemy_hp{0};
    sim::TileCoord enemy_tile{};
    sim::TileCoord stand{};
    size_t owner{0};
    bool owner_idle{false};
};

struct TraceSpec {          // --trace SEAT:FROM:TO: a line for every ant of the seat every 20 ticks from tick FROM to TO (standard output; for looking at one match)
    int seat{-1};
    uint64_t from{0};
    uint64_t to{0};
};
TraceSpec g_trace;

struct Event {
    std::string text;
    uint64_t score{0};
};

struct AntMemo {
    bool seen{false};
    uint64_t idle_since{0};
    bool idle_counted{false};
    std::array<sim::TileCoord, 20> ring{};
    uint32_t ring_n{0};
    uint64_t last_push{0};
    bool was_stuck{false};
    sim::UnitState state{sim::UnitState::Idle};
    uint8_t action{0};
    uint16_t hp{10};
    bool dying{false};
    bool drowned{false};
};

struct SeatObs {
    bool present{false};
    Counters c;
    ai::StandardBot* bot{nullptr};
    uint64_t thief_seen{0};                  // the tick an enemy Thief was first on the field (0: not yet)
};

class Observer {
public:
    Observer(const assets::LevelData& level) : level_(level) {}
    std::array<SeatObs, sim::MAX_PLAYERS> seats;
    std::vector<Event> events;
    std::map<uint64_t, Ep> open;
    std::vector<AntMemo> memo;
    std::vector<uint64_t> drowned_tick;       // by ant id
    std::vector<uint8_t> drowned_by;
    sim::AntType default_type{sim::AntType::Worker};
    bool default_known{false};
    uint64_t closed_eps{0};

    void tick(const sim::SimulationEngine& engine);
    void finish();

private:
    struct Unit {
        const sim::AntSnapshot* s{nullptr};
        const sim::AntUnit* u{nullptr};
        size_t owner{0};
        bool eligible{false};
        bool idle{false};
    };
    const assets::LevelData& level_;
    std::unique_ptr<ai::MapInfo> map_;
    std::vector<uint8_t> bomb_owner_;        // by tile index: the owner of a bomb seen at the last scan (255: none)
    uint32_t bomb_w_{0};

    static bool idle_state(sim::UnitState st) { return st == sim::UnitState::Idle || st == sim::UnitState::GuardIdle || st == sim::UnitState::Swimming; }
    static bool takes_orders(sim::UnitState st) {
        return idle_state(st) || st == sim::UnitState::Walking || st == sim::UnitState::DivingInWater || st == sim::UnitState::ExitingWater;
    }
    void sample_chances(const sim::SimulationEngine& engine, uint64_t now, const std::vector<Unit>& units);
    void touch(uint8_t seat, uint8_t kind, const sim::AntSnapshot& e, uint64_t now, int32_t dist, sim::TileCoord stand, const Unit* nearest, bool attacked, const sim::SimulationEngine& engine,
               const std::vector<Unit>& units);
    void close(Ep& ep, uint64_t now);
    std::string scene_of(const sim::SimulationEngine& engine, const std::vector<Unit>& units, sim::TileCoord centre, sim::TileCoord stand);
};

// The engine's knock rule, from the bot library (the same function the OpportunityTask plans with; AI31.1 holds it against the engine): the tile where the victim lands, or none when cornered
bool knock_landing(const sim::Grid& grid, sim::TileCoord victim, sim::TileCoord attacker, int range, sim::TileCoord& land) {
    const ai::KnockFlight flight = ai::knock_flight(grid, victim, attacker, range);
    land = flight.land;
    return flight.flies;
}

int32_t cheb(sim::TileCoord a, sim::TileCoord b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

void Observer::tick(const sim::SimulationEngine& engine) {
    const uint64_t now = engine.current_tick();
    const sim::WorldState& ws = engine.get_world_state();
    sim::SimulationEngine& writable = const_cast<sim::SimulationEngine&>(engine);     // (get_unit has no const form; the observer only reads)
    const sim::Grid& grid = engine.grid();
    const size_t minute = std::min<size_t>(static_cast<size_t>(now / 1200u), kMinutes - 1);
    if (!default_known) {
        for (const sim::AntSnapshot& s : ws.ants) {
            if (s.raw_type == sim::AntType::Worker) {
                default_type = s.type;
                default_known = true;
                break;
            }
        }
    }
    uint64_t food_left = 0;
    for (const sim::FoodObject& f : grid.food_objects()) food_left += f.remaining;
    if (map_ == nullptr) map_ = std::make_unique<ai::MapInfo>(engine);
    std::array<bool, sim::MAX_PLAYERS> reach_food{};
    {
        const std::vector<sim::FoodObject>& foods = grid.food_objects();
        for (size_t i = 0; i < foods.size(); ++i) {
            if (foods[i].remaining == 0) continue;
            const ai::PileInfo* info = map_->pile(static_cast<uint32_t>(i));
            if (info == nullptr) continue;
            for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) reach_food[t] = reach_food[t] || info->approach[t].reachable();
        }
    }
    // bombs seen now, with their owners (a bomb that goes off is gone by the time its victim shows)
    if (now % 4 == 0) {
        bomb_w_ = grid.width();
        bomb_owner_.assign(static_cast<size_t>(grid.width()) * grid.height(), 255);
        for (uint32_t y = 0; y < grid.height(); ++y) {
            for (uint32_t x = 0; x < grid.width(); ++x) {
                if (grid.get_cell(x, y).has_bomb()) bomb_owner_[static_cast<size_t>(y) * grid.width() + x] = grid.get_cell(x, y).interactive_owner;
            }
        }
    }
    std::vector<Unit> units;
    units.reserve(ws.ants.size());
    uint32_t max_id = 0;
    for (const sim::AntSnapshot& s : ws.ants) max_id = std::max(max_id, s.id);
    if (memo.size() <= max_id) {
        memo.resize(max_id + 64);
        drowned_tick.resize(max_id + 64, 0);
        drowned_by.resize(max_id + 64, 7);
    }
    for (const sim::AntSnapshot& s : ws.ants) {
        Unit un;
        un.s = &s;
        try {
            un.u = &writable.get_unit(s.id);
        } catch (...) {
            continue;
        }
        units.push_back(un);
    }
    // enemy Thief first seen
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (!seats[seat].present || seats[seat].thief_seen != 0) continue;
        for (const Unit& un : units) {
            if (un.s->player_id != seat && un.s->type == sim::AntType::Thief && ws.player_alliances[un.s->player_id] != seat) {
                seats[seat].thief_seen = now;
                break;
            }
        }
    }
    for (Unit& un : units) {
        const sim::AntSnapshot& s = *un.s;
        const sim::AntUnit& u = *un.u;
        AntMemo& m = memo[s.id];
        const uint8_t seat = s.player_id;
        const bool mine = seat < sim::MAX_PLAYERS && seats[seat].present;
        un.owner = 0;
        if (mine && seats[seat].bot != nullptr) {
            const uint32_t o = seats[seat].bot->ledger().owner(s.id);
            un.owner = o >= 32 ? 31u : static_cast<size_t>(o);
        }
        const bool no_order = u.orig_order == sim::AntUnit::kOrderNone && u.home_state == 0 && u.waypoints.empty();
        un.idle = idle_state(s.state) && no_order;
        un.eligible = un.idle || (takes_orders(s.state) && u.orig_order != sim::AntUnit::kOrderAttack);
        un.eligible = un.eligible && !s.is_holding && s.carried_points <= 0 && s.hp >= 4 && (s.type == default_type || s.type == sim::AntType::Combat) && s.state != sim::UnitState::Stunned;
        // drowning
        if (s.state == sim::UnitState::Drowning || u.loco_action == sim::AntUnit::kActionDrown) {
            if (!m.drowned) {
                m.drowned = true;
                drowned_tick[s.id] = now;
                drowned_by[s.id] = u.killer_team;
                if (seat < sim::MAX_PLAYERS && seats[seat].present) ++seats[seat].c.drown_own;
                if (u.killer_team < sim::MAX_PLAYERS && u.killer_team != seat && seats[u.killer_team].present) ++seats[u.killer_team].c.drown_kills;
            }
        }
        if (!mine) {
            m.state = s.state;
            m.hp = s.hp;
            continue;
        }
        Counters& c = seats[seat].c;
        ++c.ant_ticks;
        ++c.m_ants[minute];
        ++c.task_ticks[un.owner];
        // idle
        if (un.idle && !s.is_holding) {
            if (m.idle_since == 0) m.idle_since = now ? now : 1;
        } else if (un.idle && s.is_holding) {
            if (m.idle_since == 0) m.idle_since = now ? now : 1;
        } else {
            m.idle_since = 0;
        }
        if (m.idle_since != 0 && now - m.idle_since >= kIdleTicks) {
            if (s.is_holding) {
                ++c.carry_idle_ticks;
            } else {
                ++c.idle_ticks;
                ++c.m_idle[minute];
                ++c.task_idle_ticks[un.owner];
                if (food_left > 0) ++c.idle_food_ticks;
                if (reach_food[seat]) ++c.idle_reach_ticks;
                {
                    const sim::TileCoord me{s.tile_x, s.tile_y};
                    const int32_t my_comp = map_->ant_component(seat, me);
                    bool near_enemy = false, reachable = false;
                    for (const Unit& o : units) {
                        if (o.s->player_id == seat || ws.player_alliances[seat] == o.s->player_id || o.s->hp == 0 || o.s->state == sim::UnitState::EnteringBase) continue;
                        const sim::TileCoord ot{o.s->tile_x, o.s->tile_y};
                        if (cheb(ot, me) > 20) continue;
                        near_enemy = true;
                        for (int dy = -1; dy <= 1 && !reachable; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                                if (my_comp >= 0 && map_->component(seat, sim::TileCoord{ot.x + dx, ot.y + dy}) == my_comp) {
                                    reachable = true;
                                    break;
                                }
                        if (reachable) break;
                    }
                    if (reachable) ++c.idle_enemy_ticks;
                    else if (near_enemy) ++c.idle_enemy_unreach_ticks;
                }
                if (un.owner == 0) ++c.idle_free_ticks;
                else ++c.idle_held_ticks;
            }
        }
        // can't go
        if (s.state == sim::UnitState::CantGo) {
            ++c.cantgo_ticks;
            ++c.task_cantgo_ticks[un.owner];
        }
        // stuck
        if (now >= m.last_push + 10) {
            m.last_push = now;
            m.ring[m.ring_n % m.ring.size()] = s.state == sim::UnitState::Dead ? sim::TileCoord{-1, -1} : sim::TileCoord{s.tile_x, s.tile_y};
            ++m.ring_n;
        }
        bool stuck = false;
        if (m.ring_n >= m.ring.size() && (u.orig_order != sim::AntUnit::kOrderNone || s.state == sim::UnitState::Walking || s.state == sim::UnitState::CantGo) && u.home_state == 0 &&
            (s.state == sim::UnitState::Walking || s.state == sim::UnitState::CantGo || (s.state == sim::UnitState::Idle && u.orig_order != sim::AntUnit::kOrderNone))) {
            std::set<std::pair<int32_t, int32_t>> distinct;
            for (const sim::TileCoord& t : m.ring) distinct.insert({t.x, t.y});
            stuck = distinct.size() <= 2;
        }
        if (stuck) {
            ++c.stuck_ticks;
            ++c.task_stuck_ticks[un.owner];
            ++c.m_stuck[minute];
            if (!m.was_stuck) ++c.stuck_ants;
        }
        m.was_stuck = stuck;
        // the fire-wall chores
        const bool fire_order = u.orig_order == sim::AntUnit::kOrderIgnite || u.orig_order == sim::AntUnit::kOrderExtinguish || s.state == sim::UnitState::PlacingFire ||
                                s.state == sim::UnitState::ExtinguishingFire;
        if (fire_order) {
            ++c.fire_chore_ticks;
            ++c.m_chore[minute];
        }
        if (s.type == sim::AntType::Fire) {
            ++c.fire_ant_ticks;
            if (un.idle) ++c.fire_ant_idle_ticks;
        }
        const sim::TileCoord here{s.tile_x, s.tile_y};
        if (s.state == sim::UnitState::PlacingFire && m.state != sim::UnitState::PlacingFire) {
            ++c.walls_started;
            bool home = false, enemy = false;
            for (const assets::AnthillSpawn& h : grid.anthills()) {
                const sim::TileCoord o{h.x, h.y};
                if (cheb(here, o) <= 6) (h.team_id == seat ? home : enemy) = true;
            }
            if (home) {
                ++c.walls_home;
                if (seats[seat].thief_seen == 0) ++c.walls_pre_thief;
            }
            if (enemy) ++c.walls_enemy;
        }
        if (s.state == sim::UnitState::ExtinguishingFire && m.state != sim::UnitState::ExtinguishingFire) {
            ++c.outs_started;
            for (const assets::AnthillSpawn& h : grid.anthills()) {
                if (h.team_id == seat && cheb(here, sim::TileCoord{h.x, h.y}) <= 6) {
                    ++c.outs_home;
                    break;
                }
            }
        }
        // bomb victims and fire hits
        if (u.loco_action == sim::AntUnit::kActionBlast && m.action != sim::AntUnit::kActionBlast) ++c.bomb_victims;
        if (s.hp < m.hp && grid.has_fire_at(here) && s.type != sim::AntType::Fire) ++c.fire_hits;
        // deaths
        if ((s.hp == 0 || u.loco_action == sim::AntUnit::kActionDeath || u.loco_action == sim::AntUnit::kActionDrown) && !m.dying) {
            m.dying = true;
            ++c.ant_deaths;
            if (s.is_holding) ++c.carrier_deaths;
            int own_near = 0, enemy_near = 0;
            for (const Unit& o : units) {
                if (o.s->id == s.id || o.s->hp == 0) continue;
                if (cheb(sim::TileCoord{o.s->tile_x, o.s->tile_y}, here) > 4) continue;
                if (o.s->player_id == seat) ++own_near;
                else if (ws.player_alliances[seat] != o.s->player_id) ++enemy_near;
            }
            if (s.state != sim::UnitState::Drowning && u.loco_action != sim::AntUnit::kActionDrown) {
                if (own_near == 0 && enemy_near >= 2) ++c.lone_deaths;
                if (enemy_near >= 2 * (own_near + 1)) ++c.outnumbered_deaths;
            }
        }
        m.state = s.state;
        m.action = u.loco_action;
        m.hp = s.hp;
        // a free ant that stands next to an enemy and does nothing
        if (un.idle && !s.is_holding) {
            for (const Unit& o : units) {
                if (o.s->player_id == seat || ws.player_alliances[seat] == o.s->player_id || o.s->hp == 0) continue;
                if (cheb(sim::TileCoord{o.s->tile_x, o.s->tile_y}, here) <= 1) {
                    ++c.contact_idle_ticks;
                    break;
                }
            }
        }
    }
    if (g_trace.seat >= 0 && now >= g_trace.from && now <= g_trace.to && now % 20 == 0) {
        for (const Unit& un : units) {
            if (un.s->player_id != g_trace.seat) continue;
            std::printf("T%llu ant %u type %d (%d,%d) hp %u state %d order %d task %s%s hold %d\n", static_cast<unsigned long long>(now), un.s->id, int(un.s->type), un.s->tile_x, un.s->tile_y, un.s->hp, int(un.s->state),
                        int(un.u->orig_order), kTaskNames[un.owner], un.idle ? " IDLE" : "", un.s->is_holding ? 1 : 0);
        }
    }
    if (now % kSamplePeriod == 0) sample_chances(engine, now, units);
    // close the chances that are over
    for (auto it = open.begin(); it != open.end();) {
        if (now > it->second.last + kEpisodeGap) {
            close(it->second, now);
            it = open.erase(it);
        } else {
            ++it;
        }
    }
}

void Observer::sample_chances(const sim::SimulationEngine& engine, uint64_t now, const std::vector<Unit>& units) {
    const sim::Grid& grid = engine.grid();
    const sim::WorldState& ws = engine.get_world_state();
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (!seats[seat].present) continue;
        std::vector<const Unit*> free_ants;
        std::set<uint32_t> attacked;                                  // the enemy ants that an own ant is ordered at
        for (const Unit& un : units) {
            if (un.s->player_id != seat) continue;
            if (un.u->orig_order == sim::AntUnit::kOrderAttack && un.u->orig_target_ant != 0) attacked.insert(un.u->orig_target_ant);
            if (un.eligible) free_ants.push_back(&un);
        }
        for (const Unit& eu : units) {
            const sim::AntSnapshot& e = *eu.s;
            if (e.player_id == seat || ws.player_alliances[seat] == e.player_id || e.hp == 0) continue;
            const sim::TileCoord et{e.tile_x, e.tile_y};
            if (!grid.in_bounds(et) || grid.terrain_class_at(et) == sim::movement::kTerrainWater || grid.has_powerup_at(et)) continue;
            switch (e.state) {
                case sim::UnitState::EnteringBase: case sim::UnitState::Flinch: case sim::UnitState::Knockback: case sim::UnitState::Burn: case sim::UnitState::Infiltrating:
                case sim::UnitState::Drowning: case sim::UnitState::Dead:
                    continue;
                default:
                    break;
            }
            if (eu.u->engaged || e.type == sim::AntType::Swimmer) {
                if (e.type == sim::AntType::Swimmer) continue;
            }
            bool on_hill = false;
            for (const assets::AnthillSpawn& h : grid.anthills()) {
                if (et.x >= h.x && et.x < h.x + 4 && et.y >= h.y && et.y < h.y + 4) on_hill = true;
            }
            if (on_hill) continue;
            const bool was_attacked = attacked.count(e.id) != 0;
            // (1) a thrown ant that lands in water drowns; on a bomb it loses 2 hit points and on a fire wall 1 more (the blow itself takes 1, a Combat Ant's punch 2)
            int32_t best[4] = {99, 99, 99, 99};                // water, bomb, fire, and (index 3) water by a plain blow
            sim::TileCoord best_stand[4]{};
            const Unit* best_unit[4] = {nullptr, nullptr, nullptr, nullptr};
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const sim::TileCoord t{et.x + dx, et.y + dy};
                    if (!grid.in_bounds(t) || !sim::movement::terrain_walkable(grid.terrain_class_at(t)) || grid.is_solid_object(t) || grid.has_fire_at(t) || grid.has_bomb_at(t)) continue;
                    bool occupied = false;
                    for (const Unit& o : units) {
                        if (o.s->tile_x == t.x && o.s->tile_y == t.y && o.s->id != e.id) occupied = true;
                    }
                    for (const int range : {1, 4}) {
                        sim::TileCoord land;
                        if (!knock_landing(grid, et, t, range, land)) continue;
                        const bool water = grid.terrain_class_at(land) == sim::movement::kTerrainWater;
                        const int dmg = range == 4 ? 2 : 1;
                        const bool bomb = grid.has_bomb_at(land) && e.hp <= dmg + 2;
                        const bool fire = grid.has_fire_at(land) && e.hp <= dmg + 1;
                        const int k = water ? 0 : bomb ? 1 : fire ? 2 : -1;
                        if (k < 0) continue;
                        for (const Unit* a : free_ants) {
                            const bool combat = a->s->type == sim::AntType::Combat;
                            if (combat != (range == 4)) continue;
                            const sim::TileCoord at{a->s->tile_x, a->s->tile_y};
                            const int32_t d = at == t ? 0 : cheb(at, t) + (occupied ? 1 : 0);
                            if (d < best[k]) {
                                best[k] = d;
                                best_stand[k] = t;
                                best_unit[k] = a;
                            }
                            if (k == 0 && range == 1 && d < best[3]) {
                                best[3] = d;
                                best_stand[3] = t;
                                best_unit[3] = a;
                            }
                        }
                    }
                }
            }
            for (int k = 0; k < 4; ++k) {
                if (best[k] <= kFar) touch(seat, static_cast<uint8_t>(k == 3 ? kWater1 : k), e, now, best[k], best_stand[k], best_unit[k], was_attacked, engine, units);
            }
            // (2) a blow that finishes it
            {
                int32_t bw = 99;
                const Unit* bu = nullptr;
                for (const Unit* a : free_ants) {
                    const int dmg = a->s->type == sim::AntType::Combat ? 2 : 1;
                    if (e.hp > dmg) continue;
                    const int32_t d = cheb(sim::TileCoord{a->s->tile_x, a->s->tile_y}, et) - 1;
                    if (d < bw) {
                        bw = d;
                        bu = a;
                    }
                }
                if (bu != nullptr && bw <= kFar) touch(seat, kWeak, e, now, bw, et, bu, was_attacked, engine, units);
            }
            // (3) a carrier on its way past
            if (e.is_holding || e.type == sim::AntType::Thief) {
                int32_t bw = 99;
                const Unit* bu = nullptr;
                for (const Unit* a : free_ants) {
                    const int32_t d = cheb(sim::TileCoord{a->s->tile_x, a->s->tile_y}, et) - 1;
                    if (d < bw) {
                        bw = d;
                        bu = a;
                    }
                }
                if (bu != nullptr && bw <= kNear) touch(seat, kCarrier, e, now, bw, et, bu, was_attacked, engine, units);
            }
        }
    }
}

void Observer::touch(uint8_t seat, uint8_t kind, const sim::AntSnapshot& e, uint64_t now, int32_t dist, sim::TileCoord stand, const Unit* nearest, bool attacked,
                     const sim::SimulationEngine& engine, const std::vector<Unit>& units) {
    const uint64_t key = (static_cast<uint64_t>(seat) << 56) | (static_cast<uint64_t>(kind) << 48) | e.id;
    auto it = open.find(key);
    if (it == open.end()) {
        Ep ep;
        ep.start = now;
        ep.seat = seat;
        ep.kind = kind;
        ep.enemy = e.id;
        it = open.emplace(key, ep).first;
    }
    Ep& ep = it->second;
    ep.last = now;
    ++ep.samples;
    if (e.state != sim::UnitState::Walking) ++ep.still;
    ep.attacked = ep.attacked || attacked;
    if (dist < ep.best) {
        ep.best = dist;
        ep.stand = stand;
        ep.enemy_type = static_cast<uint8_t>(e.type);
        ep.enemy_hp = static_cast<uint8_t>(e.hp);
        ep.enemy_tile = sim::TileCoord{e.tile_x, e.tile_y};
        ep.owner = nearest != nullptr ? nearest->owner : 0;
        ep.owner_idle = nearest != nullptr && nearest->idle;
    }
    if (!ep.scene_done && now - ep.start >= 30 && dist <= kNear && !ep.attacked) {
        ep.scene_done = true;
        ep.scene = scene_of(engine, units, sim::TileCoord{e.tile_x, e.tile_y}, stand);
    }
}

void Observer::close(Ep& ep, uint64_t now) {
    (void)now;
    const uint64_t dur = ep.last - ep.start + kSamplePeriod;
    if (dur < kEpisodeMin) return;
    Counters& c = seats[ep.seat].c;
    ++closed_eps;
    const uint8_t k = ep.kind;
    ++c.ep[k];
    const bool near = ep.best <= kNear;
    if (near) ++c.ep_near[k];
    if (ep.attacked) {
        ++c.ep_att[k];
        if (near) ++c.ep_near_att[k];
    }
    if (ep.still * 2 >= ep.samples) ++c.ep_still[k];
    {
        const bool avail = ep.owner == 0 || ep.owner == 1 || ep.owner == 2 || ep.owner == 7 || ep.owner == 16;
        const unsigned flags = (ep.still * 2 >= ep.samples ? 1u : 0u) | (near ? 2u : 0u) | (avail ? 4u : 0u) | (ep.attacked ? 8u : 0u) | (ep.owner_idle ? 16u : 0u);
        ++c.epm[k][flags];
    }
    const size_t minute = std::min<size_t>(static_cast<size_t>(ep.start / 1200u), kMinutes - 1);
    if (k == kWater) {
        ++c.m_water_ep[minute];
        if (ep.attacked) ++c.m_water_att[minute];
        else ++c.m_kills_missed[minute];
        if (ep.enemy < drowned_tick.size() && drowned_tick[ep.enemy] != 0 && drowned_tick[ep.enemy] + 40 >= ep.start && drowned_tick[ep.enemy] <= ep.last + 120 &&
            drowned_by[ep.enemy] == ep.seat) {
            ++c.water_conv;
            ep.conv = true;
        }
    }
    if (near && !ep.attacked && dur >= 40 && events.size() < 400) {
        Event ev;
        ev.score = dur * (k == kWater ? 4u : 1u);
        std::ostringstream t;
        t << "{\"seat\":" << int(ep.seat) << ",\"kind\":\"" << kKindNames[k] << "\",\"tick\":" << ep.start << ",\"ticks\":" << dur << ",\"enemy_type\":" << int(ep.enemy_type)
          << ",\"enemy_hp\":" << int(ep.enemy_hp) << ",\"enemy\":[" << ep.enemy_tile.x << "," << ep.enemy_tile.y << "],\"stand\":[" << ep.stand.x << "," << ep.stand.y << "],\"nearest\":" << ep.best
          << ",\"owner\":\"" << kTaskNames[ep.owner] << "\",\"owner_idle\":" << (ep.owner_idle ? "true" : "false") << ",\"still\":" << (ep.still * 2 >= ep.samples ? "true" : "false")
          << ",\"conv\":" << (ep.conv ? "true" : "false") << ",\"scene\":" << (ep.scene.empty() ? std::string("null") : ep.scene) << "}";
        ev.text = t.str();
        events.push_back(std::move(ev));
    }
}

// The window of 15 x 15 tiles round `centre`: terrain class, flags and the ants in it (for a picture of the worst moments)
std::string Observer::scene_of(const sim::SimulationEngine& engine, const std::vector<Unit>& units, sim::TileCoord centre, sim::TileCoord stand) {
    const sim::Grid& grid = engine.grid();
    std::ostringstream o;
    o << "{\"tick\":" << engine.current_tick() << ",\"x0\":" << centre.x - 7 << ",\"y0\":" << centre.y - 7 << ",\"stand\":[" << stand.x << "," << stand.y << "],\"tiles\":[";
    for (int dy = -7; dy <= 7; ++dy) {
        o << (dy == -7 ? "" : ",") << "\"";
        for (int dx = -7; dx <= 7; ++dx) {
            const sim::TileCoord t{centre.x + dx, centre.y + dy};
            char ch = '#';
            if (grid.in_bounds(t)) {
                const uint8_t tc = grid.terrain_class_at(t);
                ch = tc == sim::movement::kTerrainWater ? '~' : tc == sim::movement::kTerrainMud ? 'm' : tc == sim::movement::kTerrainSand ? 's' : tc == sim::movement::kTerrainDirt ? 'd' : '.';
                if (grid.is_solid_object(t) && !grid.has_fire_at(t)) ch = '#';
                if (grid.has_fire_at(t)) ch = 'F';
                if (grid.has_bomb_at(t)) ch = 'B';
                for (const assets::AnthillSpawn& h : grid.anthills()) {
                    if (t.x >= h.x && t.x < h.x + 4 && t.y >= h.y && t.y < h.y + 4) ch = 'H';
                }
            }
            o << ch;
        }
        o << "\"";
    }
    o << "],\"ants\":[";
    bool first = true;
    for (const Unit& un : units) {
        const sim::TileCoord t{un.s->tile_x, un.s->tile_y};
        if (cheb(t, centre) > 7) continue;
        o << (first ? "" : ",") << "{\"id\":" << un.s->id << ",\"team\":" << int(un.s->player_id) << ",\"type\":" << int(un.s->type) << ",\"hp\":" << un.s->hp << ",\"x\":" << t.x << ",\"y\":" << t.y
          << ",\"state\":" << int(un.s->state) << ",\"order\":" << int(un.u->orig_order) << ",\"hold\":" << (un.s->is_holding ? 1 : 0) << ",\"task\":\"" << kTaskNames[un.owner] << "\"}";
        first = false;
    }
    o << "]}";
    return o.str();
}

void Observer::finish() {
    for (auto& kv : open) close(kv.second, 0);
    open.clear();
}

// ---- the command line -----------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Options {
    std::vector<std::string> maps;
    std::string maps_dir;
    std::vector<uint32_t> seeds{1};
    std::vector<ai::BotSpec> seats;
    uint64_t ticks{0};
    uint32_t latency{3};
    bool rotate{false};
    unsigned threads{1};
    std::string out;
    bool quiet{false};
    bool mapinfo{false};
    bool ascii{false};
    std::vector<std::string> replays;
    bool help{false};
};

bool parse_seeds(const std::string& text, std::vector<uint32_t>& out) {
    out.clear();
    for (const std::string& part : split(text, ',')) {
        const size_t dots = part.find("..");
        uint64_t a = 0, b = 0;
        if (dots == std::string::npos) {
            if (!parse_uint(part, a)) return false;
            b = a;
        } else if (!parse_uint(part.substr(0, dots), a) || !parse_uint(part.substr(dots + 2), b) || b < a || b - a > 100000) {
            return false;
        }
        for (uint64_t s = a; s <= b; ++s) out.push_back(static_cast<uint32_t>(s));
    }
    return !out.empty();
}

// "N=KIND[+K=V,..][:LEVEL[:STYLE]]"
bool parse_seat(const std::string& text, ai::BotSpec& out, std::string& err) {
    std::string spec = text;
    const size_t eq = spec.find('=');
    if (eq == std::string::npos || eq != 1) {
        err = "a seat is N=SPEC";
        return false;
    }
    const std::string seat = spec.substr(0, 1);
    spec = spec.substr(2);
    std::vector<std::string> parts = split(spec, ':');
    std::string kind = parts[0];
    std::string tuned;
    if (kind.rfind("standard+", 0) == 0) {
        tuned = kind;
        parts[0] = "standard";
    }
    ai::Style pinned = ai::Style::Random;
    if (!tuned.empty() && parts.size() == 3) {                                 // KIND:LEVEL:STYLE of a tuned bot
        if (!ai::parse_style(parts[2], pinned)) { err = "unknown style"; return false; }
        parts.pop_back();
    }
    std::string joined = seat;
    for (const std::string& p : parts) joined += ":" + p;
    if (!ai::parse_bot_spec(joined, out, err)) return false;
    if (!tuned.empty()) {
        out.kind = tuned;
        out.style = pinned;
    }
    return true;
}

bool parse_args(const std::vector<std::string>& a, Options& o, std::string& err) {
    for (size_t i = 0; i < a.size(); ++i) {
        const std::string& k = a[i];
        const auto value = [&]() -> const std::string* { return i + 1 < a.size() ? &a[++i] : nullptr; };
        if (k == "--help" || k == "-h") o.help = true;
        else if (k == "--map" || k == "--mapinfo" || k == "--ascii") {
            const std::string* v = value();
            if (v == nullptr) { err = k + " needs a value"; return false; }
            o.maps = split(*v, ',');
            o.mapinfo = k == "--mapinfo";
            o.ascii = k == "--ascii";
        } else if (k == "--trace") {
            const std::string* v = value();
            const std::vector<std::string> p = v != nullptr ? split(*v, ':') : std::vector<std::string>{};
            uint64_t seat = 0, from = 0, to = 0;
            if (p.size() != 3 || !parse_uint(p[0], seat) || !parse_uint(p[1], from) || !parse_uint(p[2], to) || seat > 3) { err = "--trace SEAT:FROM:TO"; return false; }
            g_trace = TraceSpec{static_cast<int>(seat), from, to};
        } else if (k == "--replay") {
            while (i + 1 < a.size() && a[i + 1].rfind("--", 0) != 0) o.replays.push_back(a[++i]);
            if (o.replays.empty()) { err = "--replay needs files"; return false; }
        } else if (k == "--maps-dir") {
            const std::string* v = value();
            if (v == nullptr) { err = k + " needs a value"; return false; }
            o.maps_dir = *v;
        } else if (k == "--seeds") {
            const std::string* v = value();
            if (v == nullptr || !parse_seeds(*v, o.seeds)) { err = "bad --seeds"; return false; }
        } else if (k == "--seat") {
            const std::string* v = value();
            ai::BotSpec s;
            if (v == nullptr || !parse_seat(*v, s, err)) { if (err.empty()) err = "bad --seat"; return false; }
            o.seats.push_back(s);
        } else if (k == "--ticks") {
            const std::string* v = value();
            uint64_t n = 0;
            if (v == nullptr || (*v != "full" && !parse_uint(*v, n))) { err = "bad --ticks"; return false; }
            o.ticks = n;
        } else if (k == "--latency-ticks") {
            const std::string* v = value();
            uint64_t n = 0;
            if (v == nullptr || !parse_uint(*v, n) || n > 20) { err = "bad --latency-ticks"; return false; }
            o.latency = static_cast<uint32_t>(n);
        } else if (k == "--threads") {
            const std::string* v = value();
            uint64_t n = 0;
            if (v == nullptr || !parse_uint(*v, n) || n == 0 || n > 64) { err = "bad --threads"; return false; }
            o.threads = static_cast<unsigned>(n);
        } else if (k == "--out") {
            const std::string* v = value();
            if (v == nullptr) { err = "--out needs a file"; return false; }
            o.out = *v;
        } else if (k == "--rotate") o.rotate = true;
        else if (k == "--quiet") o.quiet = true;
        else { err = "unknown option " + k; return false; }
    }
    return true;
}

struct LoadedMap {
    std::string name;
    assets::LevelData level;
    bool ok{false};
};

std::vector<LoadedMap> load_maps(const Options& o, std::string& err, bool skip_bad = false) {
    std::vector<LoadedMap> maps;
    for (const std::string& what : o.maps) {
        LoadedMap m;
        fs::path p;
        const bool path = what.find('/') != std::string::npos || what.find('.') != std::string::npos;
        if (path) {
            p = what;
        } else {
            const fs::path base = o.maps_dir.empty() ? fs::path(ORIGINAL_ASSETS_DIR) / "Maps" : fs::path(o.maps_dir);
            std::error_code ec;
            for (const std::string& stem : {upper(what), what}) {
                for (const char* ext : {".LVL", ".lvl"}) {
                    if (fs::is_regular_file(base / (stem + ext), ec)) p = base / (stem + ext);
                }
            }
        }
        std::string stem = p.stem().string();
        m.name = path ? stem : upper(what);
        if (p.empty() || !m.level.load_from_file(p.string(), nullptr, assets::LevelLimits::for_play_extended())) {
            if (skip_bad) {
                std::fprintf(stderr, "bot_watch: map '%s' does not load\n", what.c_str());
                continue;
            }
            err = "map '" + what + "' does not load";
            return {};
        }
        m.ok = true;
        maps.push_back(std::move(m));
    }
    return maps;
}

std::string spec_text(const ai::BotSpec& s) { return s.kind + ":" + ai::level_name(s.level) + (s.style != ai::Style::Random ? std::string(":") + ai::style_name(s.style) : std::string()); }

std::vector<std::vector<ai::BotSpec>> arrangements(const std::vector<ai::BotSpec>& given, bool rotate) {
    std::vector<std::vector<ai::BotSpec>> out;
    out.push_back(given);
    if (!rotate || given.size() < 2) return out;
    std::vector<uint8_t> seats;
    std::vector<std::string> texts;
    std::map<std::string, ai::BotSpec> by_text;
    for (const ai::BotSpec& s : given) {
        seats.push_back(s.seat);
        texts.push_back(spec_text(s));
        by_text[spec_text(s)] = s;
    }
    std::sort(texts.begin(), texts.end());
    std::set<std::vector<std::string>> seen;
    seen.insert([&] { std::vector<std::string> v; for (const ai::BotSpec& s : given) v.push_back(spec_text(s)); return v; }());
    do {
        if (!seen.insert(texts).second) continue;
        std::vector<ai::BotSpec> arr;
        for (size_t i = 0; i < texts.size(); ++i) {
            ai::BotSpec s = by_text[texts[i]];
            s.seat = seats[i];
            arr.push_back(s);
        }
        out.push_back(arr);
    } while (std::next_permutation(texts.begin(), texts.end()));
    return out;
}

struct MatchOut {
    std::string map;
    uint32_t seed{0};
    std::string arrangement;
    std::string json;
    std::string line;
    bool ok{true};
};

// A seat's bot, made like bot_arena's: the registry's bot for `standard`, a tuned one for `standard+K=V,K=V`
std::unique_ptr<ai::Bot> make_seat_bot(const ai::BotSpec& spec, Observer& obs) {
    std::unique_ptr<ai::Bot> made;
    if (spec.kind.rfind("standard+", 0) == 0) {
        std::vector<std::pair<std::string, int64_t>> keys;
        bool styled = false;
        for (const std::string& kv : split(spec.kind.substr(9), ',')) {
            const size_t eq = kv.find('=');
            uint64_t num = 0;
            if (eq == std::string::npos || !parse_uint(kv.substr(eq + 1), num)) return nullptr;
            ai::LevelPlan probe;
            std::string err;
            if (!apply_tune(probe, kv.substr(0, eq), static_cast<int64_t>(num), err)) return nullptr;
            keys.emplace_back(kv.substr(0, eq), static_cast<int64_t>(num));
            if (kv.substr(0, eq) == "styled" && num != 0) styled = true;
        }
        const auto tune = [keys](ai::LevelPlan& p) {
            for (const auto& t : keys) {
                std::string err;
                apply_tune(p, t.first, t.second, err);
            }
        };
        if (spec.style == ai::Style::Random && !styled) {
            ai::LevelPlan plan = ai::plan_for(spec.level);
            tune(plan);
            made = std::make_unique<ai::StandardBot>(plan);
        } else {
            made = std::make_unique<ai::StandardBot>(spec.level, spec.style, tune);
        }
    } else {
        made = ai::make_bot(spec);
    }
    if (made != nullptr && std::strcmp(made->kind(), "standard") == 0 && spec.seat < sim::MAX_PLAYERS) obs.seats[spec.seat].bot = static_cast<ai::StandardBot*>(made.get());
    return made;
}

void write_counters(std::ostringstream& o, const Counters& c) {
    const auto arr = [&](const char* name, const auto& a, size_t n) {
        o << ",\"" << name << "\":[";
        for (size_t i = 0; i < n; ++i) o << (i ? "," : "") << a[i];
        o << "]";
    };
    o << "\"ant_ticks\":" << c.ant_ticks << ",\"idle_ticks\":" << c.idle_ticks << ",\"idle_food_ticks\":" << c.idle_food_ticks << ",\"idle_reach_ticks\":" << c.idle_reach_ticks << ",\"idle_enemy_ticks\":" << c.idle_enemy_ticks << ",\"idle_enemy_unreach_ticks\":" << c.idle_enemy_unreach_ticks << ",\"idle_free_ticks\":" << c.idle_free_ticks
      << ",\"idle_held_ticks\":" << c.idle_held_ticks << ",\"carry_idle_ticks\":" << c.carry_idle_ticks << ",\"stuck_ticks\":" << c.stuck_ticks << ",\"stuck_ants\":" << c.stuck_ants
      << ",\"cantgo_ticks\":" << c.cantgo_ticks << ",\"contact_idle_ticks\":" << c.contact_idle_ticks << ",\"fire_chore_ticks\":" << c.fire_chore_ticks << ",\"fire_ant_ticks\":" << c.fire_ant_ticks
      << ",\"fire_ant_idle_ticks\":" << c.fire_ant_idle_ticks << ",\"walls_started\":" << c.walls_started << ",\"walls_home\":" << c.walls_home << ",\"walls_enemy\":" << c.walls_enemy
      << ",\"walls_pre_thief\":" << c.walls_pre_thief << ",\"outs_started\":" << c.outs_started << ",\"outs_home\":" << c.outs_home << ",\"drown_kills\":" << c.drown_kills
      << ",\"drown_own\":" << c.drown_own << ",\"bomb_victims\":" << c.bomb_victims << ",\"fire_hits\":" << c.fire_hits << ",\"ant_deaths\":" << c.ant_deaths << ",\"carrier_deaths\":" << c.carrier_deaths
      << ",\"lone_deaths\":" << c.lone_deaths << ",\"outnumbered_deaths\":" << c.outnumbered_deaths << ",\"water_conv\":" << c.water_conv;
    arr("ep", c.ep, kKinds);
    arr("ep_near", c.ep_near, kKinds);
    arr("ep_att", c.ep_att, kKinds);
    arr("ep_near_att", c.ep_near_att, kKinds);
    arr("ep_still", c.ep_still, kKinds);
    o << ",\"epm\":[";
    for (size_t k = 0; k < kKinds; ++k) {
        o << (k ? "," : "") << "[";
        for (size_t i = 0; i < 32; ++i) o << (i ? "," : "") << c.epm[k][i];
        o << "]";
    }
    o << "]";
    o << ",\"tasks\":{";
    bool first = true;
    for (size_t i = 0; i < 32; ++i) {
        if (c.task_ticks[i] == 0) continue;
        o << (first ? "" : ",") << "\"" << kTaskNames[i] << "\":[" << c.task_ticks[i] << "," << c.task_idle_ticks[i] << "," << c.task_stuck_ticks[i] << "," << c.task_cantgo_ticks[i] << "]";
        first = false;
    }
    o << "}";
    arr("m_ants", c.m_ants, kMinutes);
    arr("m_idle", c.m_idle, kMinutes);
    arr("m_stuck", c.m_stuck, kMinutes);
    arr("m_chore", c.m_chore, kMinutes);
    arr("m_water_ep", c.m_water_ep, kMinutes);
    arr("m_water_att", c.m_water_att, kMinutes);
}

MatchOut play_one(const Options& o, const LoadedMap& m, uint32_t seed, const std::vector<ai::BotSpec>& arrangement) {
    MatchOut out;
    out.map = m.name;
    out.seed = seed;
    for (const ai::BotSpec& b : arrangement) out.arrangement += (out.arrangement.empty() ? "" : " ") + std::to_string(b.seat) + "=" + spec_text(b);
    Observer obs(m.level);
    for (const ai::BotSpec& b : arrangement) {
        if (b.seat < sim::MAX_PLAYERS) obs.seats[b.seat].present = true;
    }
    ai::ArenaSpec spec;
    spec.level = &m.level;
    spec.seed = seed;
    spec.bots = arrangement;
    spec.max_ticks = o.ticks;
    spec.latency_ticks = o.latency;
    for (const ai::BotSpec& b : arrangement) {
        if (b.kind.rfind("standard+", 0) == 0) spec.extra_kinds.push_back(b.kind);
    }
    spec.factory = [&](const ai::BotSpec& b) { return make_seat_bot(b, obs); };
    spec.on_tick = [&](const sim::SimulationEngine& e) { obs.tick(e); };
    spec.inspect = [&](const sim::SimulationEngine&) { obs.finish(); };
    const ai::ArenaResult r = ai::play_match(spec);
    if (!r.error.empty()) {
        out.ok = false;
        out.line = out.map + " seed " + std::to_string(seed) + ": " + r.error;
        return out;
    }
    std::ostringstream j;
    j << "{\"map\":" << json_quote(m.name) << ",\"seed\":" << seed << ",\"arrangement\":" << json_quote(out.arrangement) << ",\"ticks\":" << r.ticks << ",\"over\":" << (r.match_over ? "true" : "false")
      << ",\"hash\":\"" << fmt("%016llx", static_cast<unsigned long long>(r.hash)) << "\",\"reachable_left\":" << r.reachable_units_left << ",\"seats\":[";
    for (size_t i = 0; i < r.seats.size(); ++i) {
        const ai::ArenaSeatResult& s = r.seats[i];
        const uint8_t seat = s.spec.seat;
        j << (i ? "," : "") << "{\"seat\":" << int(seat) << ",\"spec\":" << json_quote(spec_text(s.spec)) << ",\"style\":" << json_quote(s.style) << ",\"score\":" << s.score << ",\"banked\":" << s.banked
          << ",\"raided\":" << s.raided << ",\"kills\":" << s.kills << ",\"losses\":" << s.losses << ",\"ants\":" << s.ants << ",\"hatched\":" << s.hatched << ",\"cantgo\":" << s.cantgo
          << ",\"refused\":" << s.refused_orders << ",\"orders\":" << s.orders << ",\"attack_orders\":" << s.attack_orders << ",\"stalls\":" << s.stalls << ",\"fires_lit\":" << s.fires_lit
          << ",\"bombs_planted\":" << s.bombs_planted << ",\"obs\":{";
        write_counters(j, obs.seats[seat].c);
        j << "}}";
    }
    j << "],\"events\":[";
    std::sort(obs.events.begin(), obs.events.end(), [](const Event& a, const Event& b) { return a.score > b.score; });
    for (size_t i = 0; i < obs.events.size() && i < 8; ++i) j << (i ? "," : "") << obs.events[i].text;
    j << "]}";
    out.json = j.str();
    std::string scores;
    for (const ai::ArenaSeatResult& s : r.seats) scores += fmt(" [%d %s %d]", int(s.spec.seat), spec_text(s.spec).c_str(), s.score);
    out.line = out.map + " seed " + std::to_string(seed) + ":" + scores + fmt("  ticks %llu  hash %016llx", static_cast<unsigned long long>(r.ticks), static_cast<unsigned long long>(r.hash));
    return out;
}

// --ascii: the map as letters (. grass, s sand, d dirt, m mud, ~ water, # solid, F fire, B bomb, b f t c w power-up (bomber fire thief combat swimmer), 1-4 hill of a seat, $ food)
int ascii_maps(const Options& o) {
    std::string err;
    const std::vector<LoadedMap> maps = load_maps(o, err, true);
    for (const LoadedMap& m : maps) {
        sim::SimulationEngine engine;
        engine.init(m.level, 1, 15);
        const sim::Grid& g = engine.grid();
        std::printf("%s (%ux%u)\n", m.name.c_str(), g.width(), g.height());
        for (uint32_t y = 0; y < g.height(); ++y) {
            std::string row;
            for (uint32_t x = 0; x < g.width(); ++x) {
                const sim::TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                const uint8_t tc = g.terrain_class_at(t);
                char ch = tc == sim::movement::kTerrainWater ? '~' : tc == sim::movement::kTerrainMud ? 'm' : tc == sim::movement::kTerrainSand ? 's' : tc == sim::movement::kTerrainDirt ? 'd' : '.';
                if (g.is_solid_object(t)) ch = '#';
                if (g.get_cell(t).has_food()) ch = '$';
                if (g.has_fire_at(t)) ch = 'F';
                if (g.has_bomb_at(t)) ch = 'B';
                if (g.has_powerup_at(t)) ch = "?bftcw"[std::min<unsigned>(g.get_powerup_type(t), 5u)];
                for (const assets::AnthillSpawn& h : g.anthills()) {
                    if (x >= h.x && x < h.x + 4u && y >= h.y && y < h.y + 4u) ch = static_cast<char>('1' + h.team_id);
                }
                row += ch;
            }
            std::printf("%s\n", row.c_str());
        }
    }
    return 0;
}

// A recording played into a fresh engine with the observer watching after every tick (nothing is changed: the hashes of the file are checked all the same)
std::string watch_replay(const Options& o, const std::string& file, std::string& line, bool& ok) {
    ok = false;
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ants::replay::Replay replay;
    std::string why;
    if (bytes.empty() || !ants::replay::decode(bytes.data(), bytes.size(), replay, why)) {
        line = file + ": refused: " + why;
        return "";
    }
    assets::LevelData level;
    const std::vector<std::string> dirs = {o.maps_dir.empty() ? std::string(ORIGINAL_ASSETS_DIR) + "/Maps" : o.maps_dir, std::string(ANTS_SOURCE_DIR) + "/Community-Maps"};
    if (!ants::replay::load_map(replay.head, dirs, level, why)) {
        line = file + ": " + why;
        return "";
    }
    Observer obs(level);
    std::array<std::string, sim::MAX_PLAYERS> label{};
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (((replay.head.roster >> seat) & 1u) == 0) continue;
        obs.seats[seat].present = true;
        label[seat] = replay.head.names[seat];
    }
    ants::replay::Hooks hooks;
    hooks.after_tick = [&](uint32_t, sim::SimulationEngine& engine) { obs.tick(engine); };
    const ants::replay::Outcome out = ants::replay::play(replay, level, hooks);
    obs.finish();
    ok = out.ok;
    std::ostringstream j;
    j << "{\"replay\":" << json_quote(fs::path(file).filename().string()) << ",\"map\":" << json_quote(replay.head.map_name) << ",\"game\":" << json_quote(replay.head.game_version) << ",\"ticks\":" << out.turns
      << ",\"ok\":" << (out.ok ? "true" : "false") << ",\"seats\":[";
    bool first = true;
    for (uint8_t seat = 0; seat < sim::MAX_PLAYERS; ++seat) {
        if (!obs.seats[seat].present) continue;
        const std::string& name = label[seat];
        const size_t bot = name.find("(bot, ");
        std::string spec = bot == std::string::npos ? "human" : "bot-" + name.substr(bot + 6, name.find(')', bot) - bot - 6);
        j << (first ? "" : ",") << "{\"seat\":" << int(seat) << ",\"name\":" << json_quote(name) << ",\"spec\":" << json_quote(spec) << ",\"score\":" << out.scores[seat] << ",\"obs\":{";
        write_counters(j, obs.seats[seat].c);
        j << "}}";
        first = false;
    }
    j << "],\"events\":[";
    std::sort(obs.events.begin(), obs.events.end(), [](const Event& a, const Event& b) { return a.score > b.score; });
    for (size_t i = 0; i < obs.events.size() && i < 8; ++i) j << (i ? "," : "") << obs.events[i].text;
    j << "]}";
    line = fs::path(file).filename().string() + (out.ok ? ": played, every hash matches" : ": " + out.error);
    return j.str();
}

int mapinfo(const Options& o) {
    std::string err;
    const std::vector<LoadedMap> maps = load_maps(o, err, true);
    for (const LoadedMap& m : maps) {
        sim::SimulationEngine engine;
        engine.init(m.level, 1, 15);
        const sim::Grid& g = engine.grid();
        int water = 0, fire = 0, bomb = 0, solid = 0, mud = 0, pu = 0;
        for (uint32_t y = 0; y < g.height(); ++y) {
            for (uint32_t x = 0; x < g.width(); ++x) {
                const sim::TileCoord t{static_cast<int32_t>(x), static_cast<int32_t>(y)};
                if (g.terrain_class_at(t) == sim::movement::kTerrainWater) ++water;
                if (g.terrain_class_at(t) == sim::movement::kTerrainMud) ++mud;
                if (g.has_fire_at(t)) ++fire;
                if (g.has_bomb_at(t)) ++bomb;
                if (g.has_powerup_at(t)) ++pu;
                if (g.is_solid_object(t)) ++solid;
            }
        }
        int food = 0;
        for (const sim::FoodObject& f : g.food_objects()) food += f.remaining * f.value;
        std::printf("%-40s %3ux%-3u water %4d (%2d%%) mud %4d fire %3d bombs %3d powerups %2d solid %4d hills %zu piles %zu food %d minutes %d\n", m.name.c_str(), g.width(), g.height(), water,
                    water * 100 / static_cast<int>(g.width() * g.height()), mud, fire, bomb, pu, solid, g.anthills().size(), g.food_objects().size(), food,
                    static_cast<int>(engine.get_match_time_remaining_ms() / 60000u));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    std::string err;
    if (!parse_args(std::vector<std::string>(argv + 1, argv + argc), o, err)) {
        std::fprintf(stderr, "bot_watch: %s\n", err.c_str());
        return 2;
    }
    if (o.help || (argc == 1)) {
        std::printf("usage: bot_watch --map NAMES [--maps-dir DIR] [--seeds A..B] --seat N=SPEC... [--rotate] [--threads N] [--ticks full|N] [--out FILE] [--quiet]\n       bot_watch --mapinfo NAMES [--maps-dir DIR]\n");
        return o.help ? 0 : 2;
    }
    if (!o.replays.empty()) {
        std::ostringstream report;
        report << "{\"format\":1,\"tool\":\"bot_watch\",\"replays\":[";
        bool first = true, all = true;
        if (!o.out.empty() && library_guard::refused("bot_watch", "--out", o.out, o.replays, stderr)) return 2;
        for (const std::string& f : o.replays) {
            std::string line;
            bool ok = false;
            const std::string js = watch_replay(o, f, line, ok);
            all = all && ok;
            if (!o.quiet) std::printf("%s\n", line.c_str());
            if (js.empty()) continue;
            report << (first ? "" : ",\n") << js;
            first = false;
        }
        report << "]}\n";
        if (!o.out.empty()) {
            std::ofstream out(o.out, std::ios::binary);
            out << report.str();
        }
        return all ? 0 : 1;
    }
    if (o.mapinfo) return mapinfo(o);
    if (o.ascii) return ascii_maps(o);
    if (o.maps.empty() || o.seats.empty()) {
        std::fprintf(stderr, "bot_watch: --map and at least one --seat are needed\n");
        return 2;
    }
    std::set<uint8_t> used;
    for (const ai::BotSpec& s : o.seats) {
        if (!used.insert(s.seat).second) {
            std::fprintf(stderr, "bot_watch: seat %u twice\n", s.seat);
            return 2;
        }
    }
    if (!o.out.empty() && library_guard::refused("bot_watch", "--out", o.out, {}, stderr)) return 2;
    const std::vector<LoadedMap> maps = load_maps(o, err);
    if (!err.empty()) {
        std::fprintf(stderr, "bot_watch: %s\n", err.c_str());
        return 2;
    }
    struct Job { size_t map; uint32_t seed; size_t arr; };
    const std::vector<std::vector<ai::BotSpec>> arrs = arrangements(o.seats, o.rotate);
    std::vector<Job> jobs;
    for (size_t mi = 0; mi < maps.size(); ++mi)
        for (const uint32_t seed : o.seeds)
            for (size_t ai_ = 0; ai_ < arrs.size(); ++ai_) jobs.push_back({mi, seed, ai_});
    std::vector<MatchOut> results(jobs.size());
    std::atomic<size_t> next{0};
    std::mutex print_lock;
    const auto worker = [&]() {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= jobs.size()) return;
            results[i] = play_one(o, maps[jobs[i].map], jobs[i].seed, arrs[jobs[i].arr]);
            if (!o.quiet) {
                std::lock_guard<std::mutex> guard(print_lock);
                std::printf("%s\n", results[i].line.c_str());
                std::fflush(stdout);
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < o.threads; ++t) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();
    bool all_ok = true;
    std::ostringstream report;
    report << "{\"format\":1,\"tool\":\"bot_watch\",\"matches\":[";
    bool first = true;
    for (const MatchOut& r : results) {
        all_ok = all_ok && r.ok;
        if (!r.ok) continue;
        report << (first ? "" : ",\n") << r.json;
        first = false;
    }
    report << "]}\n";
    if (!o.out.empty()) {
        std::ofstream f(o.out, std::ios::binary);
        f << report.str();
        if (!f) {
            std::fprintf(stderr, "bot_watch: could not write %s\n", o.out.c_str());
            return 2;
        }
    }
    return all_ok ? 0 : 1;
}
