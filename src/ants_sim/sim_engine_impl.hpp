#pragma once

// Private engine implementation state shared by the simulation translation units
// (sim_engine.cpp, movement_system.cpp). Not part of the public ants_sim API.

#include "ants_sim/sim_engine.hpp"
#include "ants_sim/pathfinding.hpp"
#include "ants_sim/path_planner.hpp"
#include "ants_sim/movement_tables.hpp"
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>

namespace ants::sim {

class SimulationEngineImpl {
public:
    PRNG prng_{1u};
    Grid grid_;
    MatchStatsManager stats_;
    PhysicsEngine physics_;
    MatchState match_state_{MatchState::NotStarted};

    uint64_t current_tick_{0};
    uint32_t match_time_remaining_ms_{0};
    bool warned_one_minute_{false};
    bool warned_thirty_seconds_{false};
    uint32_t last_countdown_second_{0};

    std::vector<std::unique_ptr<AntUnit>> ants_;
    std::unordered_map<uint32_t, std::unique_ptr<CombatAIController>> ai_controllers_;
    uint32_t next_ant_id_{1};

    std::vector<AudioEvent> audio_queue_;
    std::vector<NewsEvent>  news_queue_;
    std::vector<TileCoord>  reserved_queue_slots_;
    std::array<uint32_t, MAX_PLAYERS> invite_pending_ticks_{};
    uint32_t hatch_delay_ticks_{60};
    struct AnthillQueueState {
        std::vector<uint32_t> queue;
        uint32_t active_depositing_ant_id{0};
    };
    std::array<AnthillQueueState, MAX_PLAYERS> base_queues_{};
    std::vector<VisualEffect> active_effects_;

    // Fog of War State
    bool fog_of_war_enabled_{false};
    std::vector<uint8_t> fog_revealed_{};
    uint8_t viewing_player_id_{0};

    void reveal_fog_tile(int32_t x, int32_t y) {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= grid_.width() || static_cast<uint32_t>(y) >= grid_.height()) return;
        size_t idx = static_cast<size_t>(y) * grid_.width() + static_cast<size_t>(x);
        if (idx < fog_revealed_.size() && fog_revealed_[idx] == 0) {
            fog_revealed_[idx] = 1;
            world_state_dirty_ = true;
        }
    }

    void reveal_fog_box(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
        for (int32_t y = min_y; y <= max_y; ++y) {
            for (int32_t x = min_x; x <= max_x; ++x) {
                reveal_fog_tile(x, y);
            }
        }
    }

    void update_fog_of_war() {
        if (!fog_of_war_enabled_) return;
        size_t total_tiles = static_cast<size_t>(grid_.width() * grid_.height());
        if (fog_revealed_.size() != total_tiles) {
            fog_revealed_.assign(total_tiles, 0);
            world_state_dirty_ = true;
        }

        // Reveal viewing player's base (and allied bases)
        for (const auto& ah : grid_.anthills()) {
            if (ah.team_id == viewing_player_id_ || stats_.are_allies(viewing_player_id_, ah.team_id)) {
                reveal_fog_box(static_cast<int32_t>(ah.x) - 6, static_cast<int32_t>(ah.y) - 6,
                               static_cast<int32_t>(ah.x) + 9, static_cast<int32_t>(ah.y) + 9);
            }
        }

        // Reveal friendly units (and allied units)
        for (const auto& ant : ants_) {
            if (!ant || !ant->is_alive() || ant->underground) continue;
            if (ant->player_id == viewing_player_id_ || stats_.are_allies(viewing_player_id_, ant->player_id)) {
                reveal_fog_box(ant->pos.x - 6, ant->pos.y - 6, ant->pos.x + 6, ant->pos.y + 6);
            }
        }
    }

    struct FlowerDropper {
        TileCoord pos;
        TileCoord drop_pos;
        uint32_t interval_ticks{300};
        uint32_t timer_ticks{300};
        bool is_dropping{false};
        uint32_t drop_tick{0};
        uint8_t powerup_type{0}; // 0: Bomber, 1: Combat, 2: Thief, 3: Swimmer, 4: Fire
        std::array<double, 5> probabilities{0.2, 0.2, 0.2, 0.2, 0.2};
    };
    std::vector<FlowerDropper> flower_droppers_;

    mutable WorldState world_state_cache_;
    mutable bool       world_state_dirty_{true};
    void spawn_death_effect(int32_t px, int32_t py) {
        static const char* const death_anims[3] = { "death1", "death2", "death3" };
        uint32_t pick = prng_.rand() % 3;
        uint16_t max_frames = static_cast<uint16_t>(pick == 0 ? 11 : (pick == 1 ? 12 : 10));
        active_effects_.push_back(VisualEffect{death_anims[pick], px, py, 0, max_frames});
        world_state_dirty_ = true;
    }

    SimulationEngineImpl() = default;

    std::vector<AntUnit*> get_unit_pointers() {
        std::vector<AntUnit*> ptrs;
        ptrs.reserve(ants_.size());
        for (auto& a : ants_) {
            if (a) ptrs.push_back(a.get());
        }
        return ptrs;
    }

    AntUnit* find_unit(uint32_t id) {
        for (auto& a : ants_) {
            if (a && a->id == id) return a.get();
        }
        return nullptr;
    }

    const AntUnit* find_unit(uint32_t id) const {
        for (const auto& a : ants_) {
            if (a && a->id == id) return a.get();
        }
        return nullptr;
    }

    CombatAIController* get_or_create_ai(AntUnit& unit) {
        if (unit.type != AntType::Combat) return nullptr;
        auto it = ai_controllers_.find(unit.id);
        if (it != ai_controllers_.end()) {
            return it->second.get();
        }
        auto controller = std::make_unique<CombatAIController>(unit);
        CombatAIController* ptr = controller.get();
        ai_controllers_[unit.id] = std::move(controller);
        return ptr;
    }

    bool is_valid_powerup_drop_tile(TileCoord adj) const noexcept {
        if (!grid_.in_bounds(adj)) return false;
        if (grid_.is_solid_obstacle(adj.x, adj.y)) return false;
        const auto& cell = grid_.get_cell(adj);
        if (!cell.is_passable()) return false;
        if (cell.terrain_type == TERRAIN_WATER || cell.surface_type == SurfaceType::Water) return false;
        if (!cell.is_empty_overlay() || cell.has_powerup() || cell.has_bomb() || cell.has_fire() || cell.has_food() || cell.has_lunchbox()) return false;

        for (const auto& ah : grid_.anthills()) {
            if (adj.x >= static_cast<int32_t>(ah.x) && adj.x < static_cast<int32_t>(ah.x) + 4 &&
                adj.y >= static_cast<int32_t>(ah.y) && adj.y < static_cast<int32_t>(ah.y) + 4) {
                return false;
            }
        }

        for (const auto& other : ants_) {
            if (!other || !other->is_alive() || other->is_underground()) continue;
            if (other->pos == adj) return false;
            TileCoord other_tile{
                (other->pixel_x >= 0) ? (other->pixel_x / 32) : ((other->pixel_x - 31) / 32),
                (other->pixel_y >= 0) ? (other->pixel_y / 32) : ((other->pixel_y - 31) / 32)
            };
            if (other_tile == adj) return false;
        }

        return true;
    }

    bool has_living_ant_at(TileCoord target) const noexcept {
        for (const auto& ant : ants_) {
            if (ant && ant->is_alive() && !ant->underground) {
                if (ant->pos == target) return true;
                TileCoord cur_tile{
                    (ant->pixel_x >= 0) ? (ant->pixel_x / 32) : ((ant->pixel_x - 31) / 32),
                    (ant->pixel_y >= 0) ? (ant->pixel_y / 32) : ((ant->pixel_y - 31) / 32)
                };
                if (cur_tile == target) return true;
            }
        }
        return false;
    }

    bool has_other_living_ant_at(TileCoord target, uint32_t ignore_ant_id) const noexcept {
        for (const auto& ant : ants_) {
            if (ant && ant->is_alive() && !ant->underground && ant->id != ignore_ant_id) {
                if (ant->pos == target) return true;
                TileCoord cur_tile{
                    (ant->pixel_x >= 0) ? (ant->pixel_x / 32) : ((ant->pixel_x - 31) / 32),
                    (ant->pixel_y >= 0) ? (ant->pixel_y / 32) : ((ant->pixel_y - 31) / 32)
                };
                if (cur_tile == target) return true;
            }
        }
        return false;
    }

    // ================= Original movement system (implemented in movement_system.cpp) =================
    // Ports of the Ants.exe locomotion, order, path-manager and blocking code. Addresses in comments
    // refer to Original-Ants/Ants.exe; see docs/GAME_REVERSE_ENGINEERING.md "Movement ground truth".
    struct StepEvt {                 // FUN_0102b997 event block (ebp-0x18 .. ebp-0x4)
        int32_t  dx{0};
        int32_t  dy{0};
        uint16_t event{11111};       // next frame's event code (11111 = none)
        int32_t  status{0};          // 0 start step, 1 normal, 2 the consumed frame was the last one
    };
    struct OccCell {                 // occupancy grid cell (world+0x553c)
        int32_t ant{-1};             // registered ant id (-1 = empty)
        bool    multi{false};        // bit 8: more than one ant registered
    };

    SimulationEngine* engine_{nullptr};   // set while movement_tick() runs (bomb detonation callback)
    uint32_t anim_clock_ms_{0};      // animation clock ("timeGetTime()"), advances 50 ms per sim tick
    uint32_t now_ms_{0};             // clock value of the event being processed
    std::vector<OccCell> occ_;
    std::array<PathManager, MAX_PLAYERS> path_managers_{};          // one PATHMGR per player machine
    std::unordered_map<uint32_t, uint32_t> path_request_serial_;    // ant id -> move_serial at request
    bool loco_trace_enabled_{false};
    std::vector<LocoTraceEvent> loco_trace_;
    void trace_loco(LocoTraceEvent::Kind kind, const AntUnit& a, int32_t dx, int32_t dy);

    // lifecycle / per tick
    void movement_reset();
    void movement_tick(SimulationEngine& eng);
    void loco_sync(AntUnit& a);

    // occupancy (FUN_0100f17f / FUN_0100f2cd / FUN_0100f421 / FUN_0100f3ca)
    OccCell* occ_cell(TileCoord t) noexcept;
    void occ_move(AntUnit& a, TileCoord nt);
    void occ_refresh();
    AntUnit* occ_scan(TileCoord t, const AntUnit* exclude, uint16_t* count);
    AntUnit* occupant_at(TileCoord t);
    bool tile_occupied(TileCoord t);

    // animation stepping (FUN_0102c0db / FUN_0102b95f / FUN_0102b997 / FUN_0101ee84)
    void loco_play(AntUnit& a, const movement::MotionClip& clip, uint8_t dir);
    void loco_update(AntUnit& a, uint32_t now);
    int  loco_step(AntUnit& a, uint32_t now);
    void loco_on_step(AntUnit& a, StepEvt& e);

    // actions and walking (FUN_0101ad02 / FUN_0101b8cb / ARRIVE / FUN_0101ccaf / FUN_01021664)
    void set_action(AntUnit& a, uint8_t action, uint8_t dir, int16_t terr_a, int16_t terr_b, bool flag);
    void walk_step(AntUnit& a, StepEvt& e);
    void arrive(AntUnit& a, StepEvt& e, TileCoord cur);
    void path_complete(AntUnit& a, StepEvt& e);
    void stop_sync(AntUnit& a);
    void stop_at(AntUnit& a, TileCoord t);
    void start_pause(AntUnit& a);
    void cancel_pause(AntUnit& a);
    void pause_fire(AntUnit& a);
    void enter_cant_go(AntUnit& a);
    void loco_release(AntUnit& a);

    // tile entry and passability (FUN_0101c4f2 / FUN_0101f780 / FUN_01020951)
    int  try_enter_tile(AntUnit& a, TileCoord nt);
    bool can_enter(const AntUnit& a, TileCoord t, uint32_t flags);
    uint32_t step_cost(const AntUnit& a, TileCoord from, TileCoord to);

    // orders (FUN_0101fc50 / FUN_01020655 / FUN_010202e7 / FUN_0101ff5a / FUN_010287b5 / FUN_0100cba4)
    // allow_goal_bomb: remake flag (AntOrder::allow_friendly_bomb) that lets a player order end on an own bomb
    // (the original's player orders always may, FUN_010202e7 flag 0x20); default = the original behaviour.
    bool go_to(AntUnit& a, TileCoord t, bool user_cmd, bool special, bool allow_goal_bomb = true);
    void classify_order(AntUnit& a, TileCoord t, bool special, bool user_cmd);
    bool adjust_goal(AntUnit& a, TileCoord& t, bool user_cmd, bool allow_goal_bomb = true);
    bool can_take_user_order(const AntUnit& a) const noexcept;
    void deliver_path(uint32_t ant_id, const std::vector<TileCoord>& path);
    bool has_pending_path(uint32_t ant_id) const noexcept;

    // helpers
    uint8_t  orig_action_of(const AntUnit& a) const noexcept;
    static bool is_stationary_action(uint8_t action) noexcept;
    void set_position(AntUnit& a, int32_t x, int32_t y);
    void set_idle_label(AntUnit& a);
    void set_walking_label(AntUnit& a);
    int32_t food_object_at(TileCoord t) const noexcept;
    TileCoord team_entrance(uint8_t team) const noexcept;
    bool is_special_base_tile(TileCoord t) const noexcept;

    void handle_game_over() {
        match_state_ = MatchState::GameOver;
        match_time_remaining_ms_ = 0;
        MatchResult result = stats_.evaluate_victory();

        for (uint8_t winner : result.winning_players) {
            audio_queue_.push_back(AudioEvent{SoundID::VictoryFanfare, 0, 0, 2, winner});
        }
        for (uint8_t loser : result.losing_players) {
            audio_queue_.push_back(AudioEvent{SoundID::PlayerDefeat, 0, 0, 2, loser});
        }
    }
};

} // namespace ants::sim
