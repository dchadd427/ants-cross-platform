#pragma once

// Helpers that the tests of the standard bot (B4-1, test_ai_b41*.cpp) share: a hand-made world of four hills on a 60 x 60 field of grass, a bot and its controller by hand (Rig: no budget,
// no filter, the bot's intents leave after the reaction delay and are applied at once), small engine queries, and the world of the fire wall tests.
#include "ai_test.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "ants_ai/standard_bot.hpp"

namespace b41 {

using namespace ai_test;
using namespace ants;
using namespace ants::ai;

inline Command command_of(CommandType type, uint8_t issuer, const std::vector<uint32_t>& ants, int32_t x, int32_t y) {
    Command c;
    c.type = type;
    c.issuer = issuer;
    c.tile_x = static_cast<int16_t>(x);
    c.tile_y = static_cast<int16_t>(y);
    c.ants = ants;
    return c;
}

inline void tick_all(sim::SimulationEngine& sim, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        sim.tick();
        sim.clear_news_events();
        sim.clear_audio_events();
    }
}

inline constexpr TileCoord kFightHills[4] = {{4, 4}, {50, 4}, {4, 50}, {50, 50}};

// Four hills on a 60 x 60 field of grass and nothing else: the tests put the ants and the piles
inline void empty_field(sim::SimulationEngine& sim, uint32_t seed = 1, uint32_t ticks = 14400) {
    sim.init_test_world(60, 60, seed, ticks * sim::TICK_MS);
    for (uint8_t p = 0; p < 4; ++p) sim.grid_mut().set_anthill(p, kFightHills[p]);
}

// A bot and its controller by hand: no budget, no filter, what the bot proposes (`proposed`) leaves `delay` ticks after the look and is applied at once. The profile is the level's
// with a given look interval and delay and no jitter.
class Rig {
public:
    Rig(sim::SimulationEngine& sim, uint8_t seat, Level level, std::unique_ptr<Bot> bot, uint32_t interval = 0, uint32_t delay = 0)
        : sim_(sim), seat_(seat), profile_(profile_for(level)), map_(sim), bot_(std::move(bot)) {
        profile_.jitter_percent = 0;
        if (interval != 0) profile_.decision_interval = interval;
        if (delay != 0) profile_.reaction_delay = delay;
        bot_->start(BotContext{seat, profile_, 1, &map_});
    }
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
            sim_.apply_command(c);
            sent.emplace_back(now, c);
            bot_->on_command(c, Bot::Fate::Sent, now);
        }
        if (now >= next_look_) {
            next_look_ = now + std::max<uint32_t>(1u, profile_.decision_interval);
            const BotView view = BotView::build(sim_, seat_, &map_);
            Orders orders;
            bot_->think(view, orders);
            looks.push_back(now);
            for (const Intent& in : orders.intents()) {
                proposed.emplace_back(now, in.command);
                waiting_.push_back(Waiting{now + profile_.reaction_delay, in.command});
            }
        }
    }
    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) tick();
    }
    template <class T>
    T& as() {
        return static_cast<T&>(*bot_);
    }
    const MapInfo& map() const { return map_; }
    const Profile& profile() const { return profile_; }
    size_t proposed_count(CommandType t) const {
        size_t n = 0;
        for (const auto& e : proposed) n += e.second.type == t ? 1u : 0u;
        return n;
    }
    std::vector<std::pair<uint64_t, Command>> sent;        // (tick it left, command)
    std::vector<std::pair<uint64_t, Command>> proposed;    // (tick of the look, command): every intent of the bot
    std::vector<uint64_t> looks;

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
};

inline const sim::AntSnapshot* snapshot_of(const sim::SimulationEngine& sim, uint32_t id) {
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

inline bool alive(const sim::SimulationEngine& sim, uint32_t id) {
    const sim::AntSnapshot* a = snapshot_of(sim, id);
    return a != nullptr && a->hp > 0 && a->state != sim::UnitState::Dead && a->state != sim::UnitState::Drowning;
}

// An enemy that keeps attacking one ant: every 10 ticks, when it is idle and alive, it is ordered onto the tile of the ant `victim` (as long as that ant lives)
inline void keep_attacking(sim::SimulationEngine& sim, uint8_t team, uint32_t enemy, uint32_t victim) {
    if (sim.current_tick() % 10 != 0 || !alive(sim, enemy) || !alive(sim, victim)) return;
    const sim::AntSnapshot* e = snapshot_of(sim, enemy);
    const sim::AntSnapshot* v = snapshot_of(sim, victim);
    if (e == nullptr || v == nullptr || (e->state != sim::UnitState::Idle && e->state != sim::UnitState::GuardIdle)) return;
    sim.apply_command(command_of(CommandType::GroupAttack, team, {enemy}, v->tile_x, v->tile_y));
}

inline size_t count_type(const sim::SimulationEngine& sim, uint8_t team, sim::AntType type) {
    size_t n = 0;
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) n += a.player_id == team && a.raw_type == type && a.hp > 0 && a.state != sim::UnitState::Dead ? 1u : 0u;
    return n;
}

inline uint32_t first_of_type(const sim::SimulationEngine& sim, uint8_t team, sim::AntType type) {
    for (const sim::AntSnapshot& a : sim.get_world_state().ants) {
        if (a.player_id == team && a.raw_type == type && a.hp > 0 && a.state != sim::UnitState::Dead) return a.id;
    }
    return 0;
}

inline size_t walls_east(const sim::SimulationEngine& sim, TileCoord hill) {
    size_t n = 0;
    for (int dy = 1; dy <= 3; ++dy) n += sim.grid().has_fire_at(TileCoord{hill.x + 4, hill.y + dy}) ? 1u : 0u;
    return n;
}


// The world of the wall tests: team 0's hill at (4, 4), four workers at its side, a Fire power-up at (16, 12); the enemy (team 1) has a worker that WALKS (so that it "plays"), and
// as the case wants an enemy Thief ant in sight and a Thief power-up on the map (1: that its hill reaches, 2: shut in by rocks, which nobody reaches)
struct WallWorld {
    sim::SimulationEngine sim;
    uint32_t enemy_worker{0};
    uint32_t enemy_thief{0};
    void build(bool thief_ant, int thief_powerup, uint32_t ticks = 14400) {
        empty_field(sim, 11, ticks);
        sim.grid_mut().place_powerup(16, 12, 2);
        if (thief_powerup != 0) sim.grid_mut().place_powerup(40, 30, 3);
        if (thief_powerup == 2) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dy != 0) sim.set_terrain(40 + dx, 30 + dy, sim::TERRAIN_OBSTACLE);
                }
            }
        }
        for (int i = 0; i < 4; ++i) sim.spawn_unit(0, sim::AntType::Worker, TileCoord{10 + i, 10});
        enemy_worker = sim.spawn_unit(1, sim::AntType::Worker, TileCoord{44, 8});
        if (thief_ant) enemy_thief = sim.spawn_unit(1, sim::AntType::Thief, TileCoord{48, 12});
        sim.set_player_score(0, 200);
    }
    // after the bot's first looks: the enemy worker walks (the drawn state of an ant that walks is not idle)
    void let_enemy_play() { sim.apply_command(command_of(CommandType::GroupMove, 1, {enemy_worker}, 30, 20)); }
};


// A pile of `units` units of `value` points with the five stages of a cracker box
inline int32_t add_pile(sim::SimulationEngine& sim, int32_t col, int32_t row, uint16_t units, uint16_t value) {
    const uint16_t q = static_cast<uint16_t>(std::max<int>(1, units / 4));
    return place_pile(sim, col, row, units, value, {{units, 369}, {static_cast<uint16_t>(q * 3), 370}, {static_cast<uint16_t>(q * 2), 371}, {q, 372}, {0, kPileGone}});
}

// The pile (by the table's index) that a tile belongs to, as the analysis of the map sees the cells; -1 when none
inline int pile_of_tile(const MapInfo& map, size_t piles, sim::TileCoord tile) {
    for (size_t i = 0; i < piles; ++i) {
        const PileInfo* info = map.pile(static_cast<uint32_t>(i));
        if (info == nullptr) continue;
        for (const sim::TileCoord& c : info->cells) {
            if (c == tile) return static_cast<int>(i);
        }
    }
    return -1;
}

// The piles that the harvest orders of a rig name, in the order they were proposed (a move onto a power-up tile is a pick-up, not a harvest order)
inline std::vector<int> harvest_targets(const Rig& rig, const sim::SimulationEngine& sim, uint8_t seat, size_t piles) {
    std::vector<int> out;
    for (const auto& e : rig.proposed) {
        if (e.second.type != CommandType::GroupMove || e.second.issuer != seat) {
            if (e.second.type != CommandType::GroupMove) continue;
        }
        const int pile = pile_of_tile(rig.map(), piles, tc(e.second.tile_x, e.second.tile_y));
        if (pile < 0 || sim.grid().has_powerup_at(tc(e.second.tile_x, e.second.tile_y))) continue;
        if (std::find(out.begin(), out.end(), pile) == out.end()) out.push_back(pile);
    }
    return out;
}

}  // namespace b41
