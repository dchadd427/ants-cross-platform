#include "ants_sim/sim_engine.hpp"
#include "ants_sim/pathfinding.hpp"
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cmath>
#include <iostream>

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

    mutable WorldState world_state_cache_;
    mutable bool       world_state_dirty_{true};

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

namespace {

bool is_tile_free_and_passable(const Grid& grid,
                               const std::vector<std::unique_ptr<AntUnit>>& ants,
                               int32_t tx, int32_t ty,
                               uint32_t ignore_id,
                               AntType unit_type) {
    if (!grid.in_bounds(tx, ty)) return false;
    const auto& cell = grid.get_cell(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
    if (!cell.is_passable()) return false;
    if (unit_type != AntType::Swimmer && cell.terrain_type == TERRAIN_WATER && !cell.has_completed_bridge()) return false;
    if (unit_type != AntType::Fire && cell.has_fire()) return false;

    for (const auto& other : ants) {
        if (other && other->is_alive() && !other->underground && other->id != ignore_id) {
            if (other->pos.x == tx && other->pos.y == ty) {
                return false;
            }
        }
    }
    return true;
}

void bounce_unit_cascade(SimulationEngineImpl& impl,
                         AntUnit& unit,
                         int32_t from_x, int32_t from_y,
                         int depth = 0) {
    if (depth > 10) return;

    unit.state = UnitState::Bounce;
    unit.state_timer = 5;
    unit.anim_tick = 0;
    unit.anim_subitem = 0;
    unit.clear_path();

    impl.audio_queue_.push_back(AudioEvent{SoundID::FlingThumpA, unit.pixel_x, unit.pixel_y, 1, 255});

    int32_t bdx = unit.pos.x - from_x;
    int32_t bdy = unit.pos.y - from_y;
    if (bdx == 0 && bdy == 0) {
        bdx = (unit.id % 2 == 0) ? 1 : -1;
        bdy = (unit.id % 3 == 0) ? 1 : -1;
    }

    static const int base_adj[8][2] = {
        {1, 0}, {0, 1}, {-1, 0}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    };
    std::vector<std::pair<int32_t, int32_t>> candidates;
    candidates.reserve(8);
    for (const auto& off : base_adj) {
        candidates.push_back({unit.pos.x + off[0], unit.pos.y + off[1]});
    }

    // Sort candidates by alignment with (bdx, bdy)
    std::sort(candidates.begin(), candidates.end(), [&](const std::pair<int32_t, int32_t>& c1, const std::pair<int32_t, int32_t>& c2) {
        int32_t dot1 = (c1.first - unit.pos.x) * bdx + (c1.second - unit.pos.y) * bdy;
        int32_t dot2 = (c2.first - unit.pos.x) * bdx + (c2.second - unit.pos.y) * bdy;
        return dot1 > dot2;
    });

    // 1. Look for an immediately available (unoccupied and passable) tile
    for (const auto& cand : candidates) {
        if (is_tile_free_and_passable(impl.grid_, impl.ants_, cand.first, cand.second, unit.id, unit.type)) {
            unit.set_tile_pos(cand.first, cand.second);
            unit.final_dest = unit.pos;
            return;
        }
    }

    // 2. If no immediate tile is free, bounce into the best passable candidate tile and bounce that ant away!
    for (const auto& cand : candidates) {
        if (!impl.grid_.in_bounds(cand.first, cand.second)) continue;
        const auto& cell = impl.grid_.get_cell(static_cast<uint32_t>(cand.first), static_cast<uint32_t>(cand.second));
        if (!cell.is_passable()) continue;

        AntUnit* occupying = nullptr;
        for (const auto& other : impl.ants_) {
            if (other && other->is_alive() && !other->underground && other->id != unit.id) {
                if (other->pos.x == cand.first && other->pos.y == cand.second) {
                    occupying = other.get();
                    break;
                }
            }
        }

        if (occupying) {
            // Cascade bounce the occupying ant!
            bounce_unit_cascade(impl, *occupying, unit.pos.x, unit.pos.y, depth + 1);
            unit.set_tile_pos(cand.first, cand.second);
            unit.final_dest = unit.pos;
            return;
        } else {
            unit.set_tile_pos(cand.first, cand.second);
            unit.final_dest = unit.pos;
            return;
        }
    }

    // Fallback: strictly snapped to tile center
    unit.set_tile_pos(unit.pos.x, unit.pos.y);
    unit.final_dest = unit.pos;
}

} // anonymous namespace

SimulationEngine::SimulationEngine()
    : impl_(std::make_unique<SimulationEngineImpl>()) {}

SimulationEngine::~SimulationEngine() = default;
SimulationEngine::SimulationEngine(SimulationEngine&&) noexcept = default;
SimulationEngine& SimulationEngine::operator=(SimulationEngine&&) noexcept = default;

void SimulationEngine::init(const ants::assets::LevelData& level, uint32_t random_seed) {
    impl_->prng_.srand(random_seed);
    impl_->grid_.init_from_level(level);
    impl_->stats_.reset();
    impl_->match_time_remaining_ms_ = 12 * 60 * 1000;
    impl_->match_state_ = MatchState::Running;
    impl_->current_tick_ = 0;
    impl_->ants_.clear();
    impl_->ai_controllers_.clear();
    impl_->audio_queue_.clear();
    impl_->news_queue_.clear();
    impl_->next_ant_id_ = 1;
    impl_->world_state_dirty_ = true;
    impl_->reserved_queue_slots_.clear();
    for (auto& bq : impl_->base_queues_) {
        bq.queue.clear();
        bq.active_depositing_ant_id = 0;
    }

    impl_->invite_pending_ticks_.fill(0);
    uint32_t starting_eggs = (level.boundary_param > 0) ? level.boundary_param : 10;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        impl_->stats_.set_egg_count(p, starting_eggs);
    }

    for (const auto& a : level.anthill_spawns) {
        if (a.team_id < MAX_PLAYERS) {
            int32_t sx = a.x;
            int32_t sy = a.y;
            if (!impl_->grid_.in_bounds(sx, sy) || !impl_->grid_.get_cell(TileCoord{sx, sy}).is_passable()) {
                static const int offsets[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
                for (const auto& off : offsets) {
                    int32_t nx = sx + off[0];
                    int32_t ny = sy + off[1];
                    if (impl_->grid_.in_bounds(nx, ny) && impl_->grid_.get_cell(TileCoord{nx, ny}).is_passable()) {
                        sx = nx;
                        sy = ny;
                        break;
                    }
                }
            }
            spawn_unit(a.team_id, AntType::Worker, TileCoord{static_cast<uint16_t>(sx), static_cast<uint16_t>(sy)});
        }
    }
}

void SimulationEngine::init_test_world(uint32_t width, uint32_t height, uint32_t random_seed, uint32_t match_time_ms) {
    impl_->prng_.srand(random_seed);
    impl_->grid_.init_empty(width, height);
    impl_->stats_.reset();
    impl_->match_time_remaining_ms_ = match_time_ms;
    impl_->match_state_ = MatchState::Running;
    impl_->current_tick_ = 0;
    impl_->ants_.clear();
    impl_->ai_controllers_.clear();
    impl_->audio_queue_.clear();
    impl_->news_queue_.clear();
    impl_->next_ant_id_ = 1;
    impl_->world_state_dirty_ = true;
    impl_->reserved_queue_slots_.clear();
    for (auto& bq : impl_->base_queues_) {
        bq.queue.clear();
        bq.active_depositing_ant_id = 0;
    }

    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        impl_->stats_.set_egg_count(p, 10);
    }
}

void SimulationEngine::reset() {
    impl_->match_state_ = MatchState::NotStarted;
    impl_->current_tick_ = 0;
    impl_->match_time_remaining_ms_ = 0;
    impl_->ants_.clear();
    impl_->ai_controllers_.clear();
    impl_->audio_queue_.clear();
    impl_->news_queue_.clear();
    impl_->next_ant_id_ = 1;
    impl_->stats_.reset();
    impl_->world_state_dirty_ = true;
    impl_->reserved_queue_slots_.clear();
    for (auto& bq : impl_->base_queues_) {
        bq.queue.clear();
        bq.active_depositing_ant_id = 0;
    }
    impl_->invite_pending_ticks_.fill(0);
}

void SimulationEngine::tick() {
    if (impl_->match_state_ == MatchState::GameOver) {
        return;
    }

    // 1. Step Match Timer
    if (impl_->match_time_remaining_ms_ <= 50) {
        impl_->handle_game_over();
        return;
    }
    impl_->match_time_remaining_ms_ -= 50;
    impl_->current_tick_++;
    impl_->world_state_dirty_ = true;

    // 2. Step Structure Timers (Firewall burnout & Bridge collapse)
    for (uint32_t y = 0; y < impl_->grid_.height(); ++y) {
        for (uint32_t x = 0; x < impl_->grid_.width(); ++x) {
            auto& cell = impl_->grid_.get_cell_mut(x, y);
            if (cell.has_fire() && cell.timer_ticks > 0) {
                cell.timer_ticks--;
                if (cell.timer_ticks == 0) {
                    impl_->grid_.clear_firewall(x, y);
                    impl_->audio_queue_.push_back(AudioEvent{SoundID::FireBurnout, static_cast<int32_t>(x * 32 + 16), static_cast<int32_t>(y * 32 + 16), 1, 255});
                }
            } else if (cell.has_completed_bridge() && cell.timer_ticks > 0) {
                cell.timer_ticks--;
                if (cell.timer_ticks == 0) {
                    impl_->grid_.collapse_bridge(x, y);
                    // Occupancy Drowning Scan
                    for (auto& ant_ptr : impl_->ants_) {
                        if (ant_ptr && ant_ptr->is_alive() && ant_ptr->pos.x == static_cast<int32_t>(x) && ant_ptr->pos.y == static_cast<int32_t>(y)) {
                            if (ant_ptr->type == AntType::Swimmer) {
                                ant_ptr->state = UnitState::Swimming;
                                ant_ptr->was_in_water = true;
                                ant_ptr->in_water = true;
                                impl_->audio_queue_.push_back(AudioEvent{SoundID::WaterSplash, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, 255});
                            } else {
                                ant_ptr->start_drowning();
                                impl_->stats_.get_player_stats_mut(ant_ptr->player_id).friendly_lost++;
                                impl_->audio_queue_.push_back(AudioEvent{SoundID::WaterSplash, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, 255});
                                impl_->audio_queue_.push_back(AudioEvent{SoundID::AntDrown, ant_ptr->pixel_x, ant_ptr->pixel_y, 2, 255});
                                ant_ptr->clear_inventory();
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Step Bomb Proximity Detonation
    for (auto& ant_ptr : impl_->ants_) {
        if (!ant_ptr || !ant_ptr->is_alive()) continue;
        if (impl_->grid_.has_bomb_at(ant_ptr->pos)) {
            const auto& cell = impl_->grid_.get_cell(ant_ptr->pos);
            uint8_t bomb_owner = cell.interactive_owner;
            if (bomb_owner != ant_ptr->player_id && !impl_->stats_.are_allies(bomb_owner, ant_ptr->player_id)) {
                impl_->grid_.clear_bomb(static_cast<uint32_t>(ant_ptr->pos.x), static_cast<uint32_t>(ant_ptr->pos.y));
                impl_->audio_queue_.push_back(AudioEvent{SoundID::BombDetonate, ant_ptr->pixel_x, ant_ptr->pixel_y, 2, 255});
                if (is_ant_in_base_queue(ant_ptr->id)) {
                    leave_base_queue(ant_ptr->id);
                }
                bool lethal = ant_ptr->take_damage(2, DamageSource::BombBlast, bomb_owner);
                if (lethal) {
                    impl_->stats_.get_player_stats_mut(ant_ptr->player_id).friendly_lost++;
                    if (bomb_owner < MAX_PLAYERS) {
                        impl_->stats_.get_player_stats_mut(bomb_owner).enemy_killed++;
                    }
                } else if (ant_ptr->hp == 1 && ant_ptr->state != UnitState::EnteringBase && !ant_ptr->underground) {
                    const auto* home = impl_->grid_.find_anthill(ant_ptr->player_id);
                    if (home) {
                        join_base_queue(ant_ptr->id);
                    }
                }
                impl_->physics_.apply_knockback(*ant_ptr, ant_ptr->pixel_x, ant_ptr->pixel_y, 2, 3, DamageSource::BombBlast, impl_->audio_queue_, impl_->prng_.rand());
            }
        }
    }

    // 4. Step Combat Ant Autonomous Guard AI
    auto ptrs = impl_->get_unit_pointers();
    for (auto& ant_ptr : impl_->ants_) {
        if (ant_ptr && ant_ptr->type == AntType::Combat && ant_ptr->is_alive()) {
            auto* ai = impl_->get_or_create_ai(*ant_ptr);
            if (ai) {
                ai->update(ptrs, impl_->grid_, impl_->stats_, impl_->audio_queue_, impl_->prng_.rand());
            }
        }
    }

    // AI Diplomacy: Process pending alliance invitations
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        const auto& invite = impl_->stats_.get_pending_invite(p);
        if (invite.active && invite.from_player < MAX_PLAYERS) {
            impl_->invite_pending_ticks_[p]++;
            if (impl_->invite_pending_ticks_[p] >= 30) {
                accept_alliance(p, invite.from_player);
                impl_->invite_pending_ticks_[p] = 0;
            }
        } else {
            impl_->invite_pending_ticks_[p] = 0;
        }
    }

    // 4.5 Process Anthill Queues & Priority Base Entry
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        auto& bq = impl_->base_queues_[p];
        const auto* base = impl_->grid_.find_anthill(p);
        if (!base) continue;

        // Clean up dead / invalid / re-assigned ants from queue
        auto q_it = bq.queue.begin();
        while (q_it != bq.queue.end()) {
            AntUnit* u = impl_->find_unit(*q_it);
            if (!u || !u->is_alive() || u->player_id != p) {
                if (bq.active_depositing_ant_id == *q_it) {
                    bq.active_depositing_ant_id = 0;
                }
                q_it = bq.queue.erase(q_it);
            } else {
                ++q_it;
            }
        }

        // Check if active depositing ant has completed deposit or was interrupted
        if (bq.active_depositing_ant_id != 0) {
            AntUnit* dep_ant = impl_->find_unit(bq.active_depositing_ant_id);
            bool interrupted = !dep_ant || !dep_ant->is_alive() || dep_ant->player_id != p ||
                               dep_ant->is_stunned() || dep_ant->state == UnitState::Knockback ||
                               dep_ant->state == UnitState::Flinch || dep_ant->state == UnitState::Drowning;
            if (interrupted) {
                uint32_t int_id = bq.active_depositing_ant_id;
                leave_base_queue(int_id);
            } else if ((dep_ant->state == UnitState::EnteringBase && dep_ant->anim_subitem >= 8) ||
                       (dep_ant->state != UnitState::EnteringBase && dep_ant->state != UnitState::Walking && !dep_ant->is_holding())) {
                auto pos = std::find(bq.queue.begin(), bq.queue.end(), bq.active_depositing_ant_id);
                if (pos != bq.queue.end()) {
                    bq.queue.erase(pos);
                }
                bq.active_depositing_ant_id = 0;
                dispatch_next_base_queue(p);
            }
        }

        // If base entry path is free, grant priority to queue head across the map
        if (bq.active_depositing_ant_id == 0 && !bq.queue.empty()) {
            dispatch_next_base_queue(p);
        }

        // Update all other queued ants to stand off to the side in their assigned queue slots
        for (size_t k = 0; k < bq.queue.size(); ++k) {
            uint32_t q_id = bq.queue[k];
            if (q_id == bq.active_depositing_ant_id) continue;
            AntUnit* q_ant = impl_->find_unit(q_id);
            if (!q_ant || !q_ant->is_alive()) continue;

            TileCoord target_slot = get_base_queue_slot(p, k);
            if (q_ant->pos == target_slot) {
                if (q_ant->state != UnitState::QueuingBase) {
                    q_ant->clear_path();
                    q_ant->state = UnitState::QueuingBase;
                    q_ant->facing = Direction::East;
                }
            } else {
                if (q_ant->state != UnitState::Walking || q_ant->final_dest != target_slot) {
                    issue_move_order(q_id, target_slot);
                    q_ant->final_dest = target_slot;
                }
            }
        }
    }

    // 5. Step Unit Movement & Timers
    for (auto& ant_ptr : impl_->ants_) {
        if (!ant_ptr || !ant_ptr->is_alive()) continue;
        ant_ptr->tick_timers();
        SurfaceType surf = SurfaceType::Grass;
        bool in_water = false;
        if (impl_->grid_.in_bounds(ant_ptr->pos)) {
            const auto& cell = impl_->grid_.get_cell(ant_ptr->pos);
            surf = cell.surface_type;
            in_water = (cell.terrain_type == TERRAIN_WATER && !cell.has_completed_bridge());
        }
        ant_ptr->tick_movement(in_water, surf);

        // Water splash audio trigger on swimmer dive or exit
        if (ant_ptr->type == AntType::Swimmer) {
            if ((ant_ptr->state == UnitState::DivingInWater && ant_ptr->anim_tick == 1) ||
                (ant_ptr->state == UnitState::ExitingWater && ant_ptr->anim_tick == 1)) {
                impl_->audio_queue_.push_back(AudioEvent{SoundID::WaterSplash, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, 255});
            }
        }

        // Universal lunchbox pickup
        TileCoord lb_target{-1, -1};
        if (ant_ptr->is_alive() && !ant_ptr->is_holding()) {
            if (impl_->grid_.has_lunchbox_at(ant_ptr->pos)) {
                lb_target = ant_ptr->pos;
            } else if (impl_->grid_.in_bounds(ant_ptr->final_dest) &&
                       impl_->grid_.has_lunchbox_at(ant_ptr->final_dest) &&
                       ant_ptr->pos.chebyshev_dist(ant_ptr->final_dest) <= 1) {
                lb_target = ant_ptr->final_dest;
            }
        }
        if (lb_target.x >= 0) {
            uint32_t pts = impl_->grid_.get_lunchbox_points(lb_target);
            impl_->grid_.clear_lunchbox(static_cast<uint32_t>(lb_target.x), static_cast<uint32_t>(lb_target.y));
            ant_ptr->pick_up_food(1, static_cast<uint16_t>(pts > 0 ? pts : 25));
            ant_ptr->final_dest = TileCoord{-1, -1};
        }

        // Power-up pickup, transformation & swap
        TileCoord pu_target{-1, -1};
        if (ant_ptr->is_alive()) {
            if (impl_->grid_.in_bounds(ant_ptr->pos) && impl_->grid_.has_powerup_at(ant_ptr->pos)) {
                pu_target = ant_ptr->pos;
            } else if (impl_->grid_.in_bounds(ant_ptr->final_dest) &&
                       impl_->grid_.has_powerup_at(ant_ptr->final_dest) &&
                       ant_ptr->pos.chebyshev_dist(ant_ptr->final_dest) <= 1) {
                pu_target = ant_ptr->final_dest;
            }
        }
        if (pu_target.x >= 0) {
            AntType old_type = ant_ptr->type;
            uint8_t new_type_id = impl_->grid_.get_powerup_type(pu_target);
            AntType new_type = static_cast<AntType>(new_type_id);
            impl_->grid_.clear_powerup(pu_target.x, pu_target.y);

            // If ant already possessed a power-up, drop previous power-up onto an adjacent free tile
            if (old_type != AntType::Worker) {
                bool dropped = false;
                for (int32_t dy = -1; dy <= 1 && !dropped; ++dy) {
                    for (int32_t dx = -1; dx <= 1 && !dropped; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        TileCoord adj{ant_ptr->pos.x + dx, ant_ptr->pos.y + dy};
                        if (impl_->grid_.in_bounds(adj) &&
                            impl_->grid_.get_cell(adj).is_passable() &&
                            impl_->grid_.get_cell(adj).is_empty_overlay()) {
                            impl_->grid_.place_powerup(adj.x, adj.y, static_cast<uint8_t>(old_type));
                            dropped = true;
                        }
                    }
                }
            }

            // Transform ant
            ant_ptr->type = new_type;
            if (new_type == AntType::Combat) ant_ptr->max_hp = 12;
            else ant_ptr->max_hp = 10;
            ant_ptr->hp = ant_ptr->max_hp;
            ant_ptr->transform_timer = 11;
            ant_ptr->final_dest = TileCoord{-1, -1};
            impl_->audio_queue_.push_back(AudioEvent{SoundID::PowerUpHeal, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, ant_ptr->player_id});
            impl_->audio_queue_.push_back(AudioEvent{SoundID::PowerUpChime, ant_ptr->pixel_x, ant_ptr->pixel_y, 2, ant_ptr->player_id});
        }

        // Multi-Stage & Schedule-Driven Food Harvest
        TileCoord food_target{-1, -1};
        if (ant_ptr->is_alive() && !ant_ptr->is_holding() && impl_->grid_.in_bounds(ant_ptr->pos)) {
            if (impl_->grid_.get_cell(ant_ptr->pos).has_food()) {
                food_target = ant_ptr->pos;
            } else if (impl_->grid_.in_bounds(ant_ptr->final_dest) &&
                       impl_->grid_.get_cell(ant_ptr->final_dest).has_food() &&
                       ant_ptr->pos.chebyshev_dist(ant_ptr->final_dest) <= 1) {
                food_target = ant_ptr->final_dest;
            } else if (ant_ptr->state == UnitState::Idle || ant_ptr->waypoints.empty()) {
                for (int32_t dy = -1; dy <= 1 && food_target.x < 0; ++dy) {
                    for (int32_t dx = -1; dx <= 1 && food_target.x < 0; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        TileCoord adj{ant_ptr->pos.x + dx, ant_ptr->pos.y + dy};
                        if (impl_->grid_.in_bounds(adj) && impl_->grid_.get_cell(adj).has_food()) {
                            if (adj == ant_ptr->final_dest || ant_ptr->final_dest.x < 0) {
                                food_target = adj;
                            }
                        }
                    }
                }
            }
        }
        if (food_target.x >= 0) {
            auto& cell = impl_->grid_.get_cell_mut(food_target);
            ActiveFoodSchedule* matched_fs = nullptr;
            for (auto& afs : impl_->grid_.food_schedules_mut()) {
                if (!afs.active) continue;
                for (const auto& c : afs.footprint) {
                    if (c.x == food_target.x && c.y == food_target.y) {
                        matched_fs = &afs;
                        break;
                    }
                }
                if (matched_fs) break;
            }

            // Play authentic Sound 66 (harvest.wav)
            impl_->audio_queue_.push_back(AudioEvent{SoundID::FoodHarvest, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, ant_ptr->player_id});
            ant_ptr->pick_up_food(1, 25);
            ant_ptr->harvest_origin = food_target;
            ant_ptr->is_thief_steal = false;
            ant_ptr->final_dest = TileCoord{-1, -1};

                if (matched_fs) {
                    matched_fs->remaining_bites--;
                    uint16_t next_tile = matched_fs->variants[0].tile_id;
                    for (size_t vi = 0; vi < matched_fs->variants.size(); ++vi) {
                        if (matched_fs->remaining_bites <= matched_fs->variants[vi].weight) {
                            next_tile = matched_fs->variants[vi].tile_id;
                        }
                    }
                    if (next_tile == ants::assets::LVL_EMPTY_TILE || next_tile == 32766 || matched_fs->remaining_bites <= 0) {
                        matched_fs->active = false;
                        matched_fs->countdown_ticks = matched_fs->respawn_interval_ticks;
                        for (const auto& c : matched_fs->footprint) {
                            if (impl_->grid_.in_bounds(c)) {
                                auto& fc = impl_->grid_.get_cell_mut(c);
                                fc.interactive_id = TILE_EMPTY;
                                fc.is_food = false;
                            }
                        }
                    } else {
                        matched_fs->current_tile_id = next_tile;
                        for (const auto& c : matched_fs->footprint) {
                            if (impl_->grid_.in_bounds(c)) {
                                auto& fc = impl_->grid_.get_cell_mut(c);
                                fc.interactive_id = next_tile;
                                fc.is_food = true;
                            }
                        }
                    }
                } else {
                    // Tuna can (can1 -> can2) or single-stage morsel
                    if (cell.interactive_id == 238) { // can1 opens into can2
                        cell.interactive_id = 239; // can2
                        cell.is_food = true;
                    } else {
                        cell.interactive_id = TILE_EMPTY;
                        cell.is_food = false;
                    }
                }

                // Worker automatically returns to base queue upon collecting food
                join_base_queue(ant_ptr->id);
            }

        // Autonomous Pending Ability Execution (Bomb, Fire, Bridge, etc.)
        if (ant_ptr->is_alive() && ant_ptr->pending_ability != OrderType::None && ant_ptr->ability_target.x >= 0) {
            bool is_cardinal_adj = validate_cardinal_placement(ant_ptr->pos, ant_ptr->ability_target);
            bool is_at_dest = (ant_ptr->waypoints.empty() || ant_ptr->state == UnitState::Idle);
            if (is_cardinal_adj || (is_at_dest && ant_ptr->pos.chebyshev_dist(ant_ptr->ability_target) <= 1)) {
                OrderType ability = ant_ptr->pending_ability;
                TileCoord target = ant_ptr->ability_target;
                ant_ptr->pending_ability = OrderType::None;
                ant_ptr->ability_target = TileCoord{-1, -1};
                ant_ptr->clear_path();
                ant_ptr->state = (ant_ptr->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                ant_ptr->facing = ants::assets::vector_to_direction(target.x - ant_ptr->pos.x, target.y - ant_ptr->pos.y);

                switch (ability) {
                    case OrderType::PlantBomb:
                        plant_bomb(ant_ptr->id, target);
                        break;
                    case OrderType::DefuseBomb:
                        defuse_bomb(ant_ptr->id, target);
                        break;
                    case OrderType::IgniteFire:
                        ignite_fire(ant_ptr->id, target);
                        break;
                    case OrderType::ExtinguishFire:
                        extinguish_fire(ant_ptr->id, target);
                        break;
                    case OrderType::BuildBridge:
                        build_bridge_step(ant_ptr->id, target);
                        break;
                    default:
                        break;
                }
            }
        }

        // Autonomous Base Entry progression
        if (ant_ptr->state == UnitState::EnteringBase) {
            if (ant_ptr->is_newborn && ant_ptr->underground) {
                if (ant_ptr->state_timer > 0) {
                    ant_ptr->state_timer--;
                    continue;
                }
                // Incubation delay complete: emerge onto surface at the hole
                ant_ptr->underground = false;
                ant_ptr->anim_subitem = 8;
                ant_ptr->facing = Direction::South;
                continue;
            }

            ant_ptr->anim_subitem++;
            if (ant_ptr->anim_subitem == 8) {
                // At subitem 8 (underground): deposit food, score, full heal, play authentic Sound 87 (scoreup.wav)
                if (ant_ptr->is_holding()) {
                    auto [food, pts] = ant_ptr->deposit_food();
                    uint32_t deposit_pts = (pts > 0) ? pts : (food * 25);
                    impl_->stats_.add_score(ant_ptr->player_id, static_cast<int32_t>(deposit_pts));
                    impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseScoreUp, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, ant_ptr->player_id});
                } else {
                    impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseEnter, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, ant_ptr->player_id});
                }
                ant_ptr->heal_full();
                ant_ptr->underground = true;

                // Depositing complete: clear active depositing status so next ant can be granted priority
                auto& bq = impl_->base_queues_[ant_ptr->player_id];
                if (bq.active_depositing_ant_id == ant_ptr->id) {
                    auto pos = std::find(bq.queue.begin(), bq.queue.end(), ant_ptr->id);
                    if (pos != bq.queue.end()) {
                        bq.queue.erase(pos);
                    }
                    bq.active_depositing_ant_id = 0;
                    dispatch_next_base_queue(ant_ptr->player_id);
                }
            } else if (ant_ptr->anim_subitem >= 16) {
                // Emerge from base
                ant_ptr->underground = false;
                ant_ptr->anim_subitem = 0;
                ant_ptr->state = (ant_ptr->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                const auto* friendly_base = impl_->grid_.find_anthill(ant_ptr->player_id);
                if (friendly_base) {
                    int32_t idle_x = friendly_base->x + 3;
                    int32_t idle_y = friendly_base->y + 3;

                    if (!ant_ptr->is_newborn && ant_ptr->had_food_at_base_entry && !ant_ptr->is_thief_steal &&
                        ant_ptr->harvest_origin.x >= 0 && ant_ptr->harvest_origin.y >= 0) {
                        // Route back to origin food harvest location
                        issue_move_order(ant_ptr->id, ant_ptr->harvest_origin);
                    } else {
                        // Newborn ant, had no food, or was thief ant: route to IDLE spot (bx+3, by+3)
                        issue_move_order(ant_ptr->id, TileCoord{idle_x, idle_y});
                    }
                }
                ant_ptr->is_newborn = false;
            }
            continue;
        }

        // Autonomous Thief Infiltration progression
        if (ant_ptr->state == UnitState::Infiltrating) {
            ant_ptr->anim_subitem++;
            if (ant_ptr->anim_subitem == 19) {
                uint8_t victim = (ant_ptr->target_team_id < MAX_PLAYERS) ? ant_ptr->target_team_id : ((ant_ptr->player_id == 0) ? 1 : 0);
                impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseAlarmSiren, ant_ptr->pixel_x, ant_ptr->pixel_y, 2, victim});
                impl_->news_queue_.push_back(NewsEvent{victim, "A ThiefAnt is at your anthill!", impl_->match_time_remaining_ms_, StringID::ThiefAlarmWarning});
            } else if (ant_ptr->anim_subitem >= 32) {
                uint8_t victim = (ant_ptr->target_team_id < MAX_PLAYERS) ? ant_ptr->target_team_id : ((ant_ptr->player_id == 0) ? 1 : 0);
                execute_thief_loot(ant_ptr->id, victim);
                ant_ptr->is_thief_steal = true;
                ant_ptr->state = UnitState::Idle;
                ant_ptr->anim_subitem = 0;
                join_base_queue(ant_ptr->id);
            }
            continue;
        }

        // Check arrival at friendly anthill with food or needing healing
        const auto* friendly_base = impl_->grid_.find_anthill(ant_ptr->player_id);
        if (friendly_base) {
            int32_t bx = friendly_base->x;
            int32_t by = friendly_base->y;
            int32_t ent_x = bx + 1;
            int32_t ent_y = by + 1;

            // If ant reached the top entrance hole, start entering base
            if (ant_ptr->pos.x == ent_x && ant_ptr->pos.y == ent_y) {
                if (ant_ptr->is_holding() || ant_ptr->hp < ant_ptr->max_hp || ant_ptr->is_newborn) {
                    if (ant_ptr->state != UnitState::EnteringBase) {
                        ant_ptr->state = UnitState::EnteringBase;
                        ant_ptr->anim_subitem = 0;
                        ant_ptr->had_food_at_base_entry = ant_ptr->is_holding();
                        ant_ptr->clear_path();
                    }
                }
            } else if ((ant_ptr->pos == TileCoord{bx - 1, by + 3} || ant_ptr->pos == TileCoord{bx, by + 3}) &&
                       (ant_ptr->is_holding() || ant_ptr->hp < ant_ptr->max_hp)) {
                if (!is_ant_in_base_queue(ant_ptr->id)) {
                    join_base_queue(ant_ptr->id);
                }
            }
        }

        // Check thief arrival at enemy anthill
        if (ant_ptr->type == AntType::Thief && ant_ptr->state != UnitState::Infiltrating) {
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                if (p != ant_ptr->player_id && !impl_->stats_.are_allies(ant_ptr->player_id, p)) {
                    const auto* enemy_base = impl_->grid_.find_anthill(p);
                    if (enemy_base) {
                        int32_t ent_x = enemy_base->x + 1;
                        int32_t ent_y = enemy_base->y + 1;
                        if ((ant_ptr->pos.x == ent_x && ant_ptr->pos.y == ent_y) ||
                            (ant_ptr->pos.chebyshev_dist(TileCoord{ent_x, ent_y}) <= 1)) {
                            if (impl_->stats_.get_individual_score(p) > 0) {
                                ant_ptr->target_team_id = p;
                                ant_ptr->state = UnitState::Infiltrating;
                                ant_ptr->anim_subitem = 0;
                                ant_ptr->clear_path();
                            } else {
                                // Base has 0 food, cannot steal!
                                ant_ptr->clear_path();
                                ant_ptr->state = UnitState::Idle;
                                join_base_queue(ant_ptr->id);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    // 5.5 Step Ant-Ant Elastic Collision & Tile Occupancy Separation
    for (size_t i = 0; i < impl_->ants_.size(); ++i) {
        auto& a1 = impl_->ants_[i];
        if (!a1 || !a1->is_alive() || a1->underground || a1->state == UnitState::EnteringBase || a1->state == UnitState::Infiltrating || a1->state == UnitState::QueuingBase) continue;
        for (size_t j = i + 1; j < impl_->ants_.size(); ++j) {
            auto& a2 = impl_->ants_[j];
            if (!a2 || !a2->is_alive() || a2->underground || a2->state == UnitState::EnteringBase || a2->state == UnitState::Infiltrating || a2->state == UnitState::QueuingBase) continue;

            int32_t dx = a1->pixel_x - a2->pixel_x;
            int32_t dy = a1->pixel_y - a2->pixel_y;
            int32_t dist_sq = dx * dx + dy * dy;
            // Collision radius threshold: 22 pixels (each tile is 32x32)
            if (dist_sq < 22 * 22) {
                float dist = std::sqrt(static_cast<float>(dist_sq));
                float nx = 1.0f, ny = 0.0f;
                if (dist > 0.01f) {
                    nx = static_cast<float>(dx) / dist;
                    ny = static_cast<float>(dy) / dist;
                } else {
                    dist = 0.01f;
                }

                float overlap = 22.0f - dist;

                bool are_enemies = (a1->player_id != a2->player_id && !impl_->stats_.are_allies(a1->player_id, a2->player_id));
                if (are_enemies) {
                    // Hostile units must never push each other out of their tiles!
                    bool a1_moving = (a1->state == UnitState::Walking);
                    bool a2_moving = (a2->state == UnitState::Walking);
                    if (a1_moving && !a2_moving) {
                        int32_t push_x = static_cast<int32_t>(nx * overlap + (nx >= 0 ? 0.5f : -0.5f));
                        int32_t push_y = static_cast<int32_t>(ny * overlap + (ny >= 0 ? 0.5f : -0.5f));
                        a1->pixel_x += push_x;
                        a1->pixel_y += push_y;
                        a1->fx_x = a1->pixel_x << 16;
                        a1->fx_y = a1->pixel_y << 16;
                        a1->pos.x = a1->pixel_x / 32;
                        a1->pos.y = a1->pixel_y / 32;
                    } else if (!a1_moving && a2_moving) {
                        int32_t push_x = static_cast<int32_t>(nx * overlap + (nx >= 0 ? 0.5f : -0.5f));
                        int32_t push_y = static_cast<int32_t>(ny * overlap + (ny >= 0 ? 0.5f : -0.5f));
                        a2->pixel_x -= push_x;
                        a2->pixel_y -= push_y;
                        a2->fx_x = a2->pixel_x << 16;
                        a2->fx_y = a2->pixel_y << 16;
                        a2->pos.x = a2->pixel_x / 32;
                        a2->pos.y = a2->pixel_y / 32;
                    } else {
                        int32_t push_x = static_cast<int32_t>(nx * (overlap * 0.5f) + (nx >= 0 ? 0.5f : -0.5f));
                        int32_t push_y = static_cast<int32_t>(ny * (overlap * 0.5f) + (ny >= 0 ? 0.5f : -0.5f));
                        a1->pixel_x += push_x;
                        a1->pixel_y += push_y;
                        a1->fx_x = a1->pixel_x << 16;
                        a1->fx_y = a1->pixel_y << 16;
                        a2->pixel_x -= push_x;
                        a2->pixel_y -= push_y;
                        a2->fx_x = a2->pixel_x << 16;
                        a2->fx_y = a2->pixel_y << 16;
                    }
                    continue;
                }

                bool a1_moving = (a1->state == UnitState::Walking);
                bool a2_moving = (a2->state == UnitState::Walking);

                if (a1_moving && !a2_moving) {
                    // a1 is moving, a2 is stationary friendly:
                    // a2 stands still and MUST NOT be pushed! a1 is pushed away from a2:
                    int32_t push_x = static_cast<int32_t>(nx * overlap + (nx >= 0 ? 0.5f : -0.5f));
                    int32_t push_y = static_cast<int32_t>(ny * overlap + (ny >= 0 ? 0.5f : -0.5f));
                    int32_t cand1_x = (a1->pixel_x + push_x) / 32;
                    int32_t cand1_y = (a1->pixel_y + push_y) / 32;
                    if (impl_->grid_.in_bounds(cand1_x, cand1_y) && impl_->grid_.get_cell(static_cast<uint32_t>(cand1_x), static_cast<uint32_t>(cand1_y)).is_passable()) {
                        a1->set_pixel_pos(a1->pixel_x + push_x, a1->pixel_y + push_y);
                    } else {
                        // Deflect perpendicular if blocked
                        int32_t tang_x = -push_y;
                        int32_t tang_y = push_x;
                        int32_t t1_x = (a1->pixel_x + tang_x) / 32;
                        int32_t t1_y = (a1->pixel_y + tang_y) / 32;
                        if (impl_->grid_.in_bounds(t1_x, t1_y) && impl_->grid_.get_cell(static_cast<uint32_t>(t1_x), static_cast<uint32_t>(t1_y)).is_passable()) {
                            a1->set_pixel_pos(a1->pixel_x + tang_x, a1->pixel_y + tang_y);
                        }
                    }

                    // If a1 collides with stationary friendly a2
                    TileCoord dest = (a1->final_dest.x >= 0) ? a1->final_dest : (!a1->waypoints.empty() ? a1->waypoints.back() : a1->pos);
                    if (dest == a2->pos) {
                        // a1 has arrived as close as possible to a2: stop a1 cleanly!
                        a1->clear_path();
                        a1->state = (a1->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                        a1->set_tile_pos(a1->pos.x, a1->pos.y);
                        a1->final_dest = a1->pos;
                    } else {
                        // Repath around stationary a2
                        bool needs_repath = false;
                        for (size_t wi = a1->current_waypoint_idx; wi < a1->waypoints.size(); ++wi) {
                            if (a1->waypoints[wi] == a2->pos) {
                                needs_repath = true;
                                break;
                            }
                        }
                        if (!needs_repath && a1->waypoints.size() <= 1) {
                            needs_repath = true;
                        }
                        if (needs_repath && dest != a1->pos) {
                            issue_move_order(a1->id, dest);
                        }
                    }
                } else if (!a1_moving && a2_moving) {
                    // a2 is moving, a1 is stationary friendly:
                    // a1 stands still and MUST NOT be pushed! a2 is pushed away from a1:
                    int32_t push_x = static_cast<int32_t>(-nx * overlap + (-nx >= 0 ? 0.5f : -0.5f));
                    int32_t push_y = static_cast<int32_t>(-ny * overlap + (-ny >= 0 ? 0.5f : -0.5f));
                    int32_t cand2_x = (a2->pixel_x + push_x) / 32;
                    int32_t cand2_y = (a2->pixel_y + push_y) / 32;
                    if (impl_->grid_.in_bounds(cand2_x, cand2_y) && impl_->grid_.get_cell(static_cast<uint32_t>(cand2_x), static_cast<uint32_t>(cand2_y)).is_passable()) {
                        a2->set_pixel_pos(a2->pixel_x + push_x, a2->pixel_y + push_y);
                    } else {
                        // Deflect perpendicular if blocked
                        int32_t tang_x = -push_y;
                        int32_t tang_y = push_x;
                        int32_t t2_x = (a2->pixel_x + tang_x) / 32;
                        int32_t t2_y = (a2->pixel_y + tang_y) / 32;
                        if (impl_->grid_.in_bounds(t2_x, t2_y) && impl_->grid_.get_cell(static_cast<uint32_t>(t2_x), static_cast<uint32_t>(t2_y)).is_passable()) {
                            a2->set_pixel_pos(a2->pixel_x + tang_x, a2->pixel_y + tang_y);
                        }
                    }

                    // If a2 collides with stationary friendly a1
                    TileCoord dest = (a2->final_dest.x >= 0) ? a2->final_dest : (!a2->waypoints.empty() ? a2->waypoints.back() : a2->pos);
                    if (dest == a1->pos) {
                        // a2 has arrived as close as possible to a1: stop a2 cleanly!
                        a2->clear_path();
                        a2->state = (a2->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                        a2->set_tile_pos(a2->pos.x, a2->pos.y);
                        a2->final_dest = a2->pos;
                    } else {
                        // Repath around stationary a1
                        bool needs_repath = false;
                        for (size_t wi = a2->current_waypoint_idx; wi < a2->waypoints.size(); ++wi) {
                            if (a2->waypoints[wi] == a1->pos) {
                                needs_repath = true;
                                break;
                            }
                        }
                        if (!needs_repath && a2->waypoints.size() <= 1) {
                            needs_repath = true;
                        }
                        if (needs_repath && dest != a2->pos) {
                            issue_move_order(a2->id, dest);
                        }
                    }
                } else {
                    // Both moving or both stationary: mutual separation
                    int32_t sep_x = static_cast<int32_t>(nx * (overlap * 0.5f) + (nx >= 0 ? 0.5f : -0.5f));
                    int32_t sep_y = static_cast<int32_t>(ny * (overlap * 0.5f) + (ny >= 0 ? 0.5f : -0.5f));

                    int32_t cand1_x = (a1->pixel_x + sep_x) / 32;
                    int32_t cand1_y = (a1->pixel_y + sep_y) / 32;
                    if (impl_->grid_.in_bounds(cand1_x, cand1_y) && impl_->grid_.get_cell(static_cast<uint32_t>(cand1_x), static_cast<uint32_t>(cand1_y)).is_passable()) {
                        a1->set_pixel_pos(a1->pixel_x + sep_x, a1->pixel_y + sep_y);
                    }

                    int32_t cand2_x = (a2->pixel_x - sep_x) / 32;
                    int32_t cand2_y = (a2->pixel_y - sep_y) / 32;
                    if (impl_->grid_.in_bounds(cand2_x, cand2_y) && impl_->grid_.get_cell(static_cast<uint32_t>(cand2_x), static_cast<uint32_t>(cand2_y)).is_passable()) {
                        a2->set_pixel_pos(a2->pixel_x - sep_x, a2->pixel_y - sep_y);
                    }
                }

                // If both are still on exact same tile, bounce the moving/colliding one to an available tile (cascading until each finds an available tile)
                if (a1->pos.x == a2->pos.x && a1->pos.y == a2->pos.y) {
                    AntUnit* to_displace = (a1_moving && !a2_moving) ? a1.get() : ((!a1_moving && a2_moving) ? a2.get() : (a1->id > a2->id ? a1.get() : a2.get()));
                    AntUnit* anchor_ant = (to_displace == a1.get()) ? a2.get() : a1.get();
                    bounce_unit_cascade(*impl_, *to_displace, anchor_ant->pos.x, anchor_ant->pos.y);
                }
            }
        }
    }

    // Discrete tile center guarantee: Ants must NEVER settle halfway between tiles
    for (auto& u : impl_->ants_) {
        if (!u || !u->is_alive() || u->underground) continue;
        if (u->state == UnitState::Idle || u->state == UnitState::GuardIdle ||
            u->state == UnitState::Bounce || u->state == UnitState::QueuingBase) {
            if (u->pixel_x != u->pos.x * 32 + 16 || u->pixel_y != u->pos.y * 32 + 16) {
                u->set_tile_pos(u->pos.x, u->pos.y);
            }
        }
    }

    // 6. Step Ballistic Physics
    impl_->physics_.tick(ptrs, impl_->grid_, impl_->audio_queue_, impl_->prng_);
}

void SimulationEngine::issue_order(const AntOrder& order) {
    AntUnit* unit = impl_->find_unit(order.ant_id);
    if (!unit || !unit->is_alive() || unit->is_stunned()) return;

    if (unit->type == AntType::Combat) {
        auto* ai = impl_->get_or_create_ai(*unit);
        if (ai) ai->on_user_command_issued();
    }

    if (order.type != OrderType::ReturnToBase) {
        leave_base_queue(order.ant_id);
    }

    switch (order.type) {
        case OrderType::Move:
            unit->pending_ability = OrderType::None;
            unit->ability_target = TileCoord{-1, -1};
            issue_move_order(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::ReturnToBase: {
            unit->pending_ability = OrderType::None;
            unit->ability_target = TileCoord{-1, -1};
            join_base_queue(order.ant_id);
            break;
        }
        case OrderType::Attack:
            unit->pending_ability = OrderType::None;
            unit->ability_target = TileCoord{-1, -1};
            if (order.target_entity_id >= 0) {
                uint32_t target_id = static_cast<uint32_t>(order.target_entity_id);
                AntUnit* target = impl_->find_unit(target_id);
                if (target && target->is_alive()) {
                    int32_t dist = unit->pos.chebyshev_dist(target->pos);
                    if (dist <= 1) {
                        execute_melee_attack(order.ant_id, target_id);
                    } else {
                        TileCoord best_neighbor = target->pos;
                        int32_t best_dist = 999999;
                        for (int32_t dy = -1; dy <= 1; ++dy) {
                            for (int32_t dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                TileCoord cand{target->pos.x + dx, target->pos.y + dy};
                                if (impl_->grid_.in_bounds(cand) && impl_->grid_.get_cell(cand).is_passable()) {
                                    int32_t d = unit->pos.chebyshev_dist(cand);
                                    if (d < best_dist) {
                                        best_dist = d;
                                        best_neighbor = cand;
                                    }
                                }
                            }
                        }
                        issue_move_order(order.ant_id, best_neighbor);
                    }
                }
            }
            break;
        case OrderType::PlantBomb:
        case OrderType::DefuseBomb:
        case OrderType::IgniteFire:
        case OrderType::ExtinguishFire:
        case OrderType::BuildBridge: {
            TileCoord target{order.target_x, order.target_y};
            if (validate_cardinal_placement(unit->pos, target)) {
                unit->pending_ability = OrderType::None;
                unit->ability_target = TileCoord{-1, -1};
                unit->facing = ants::assets::vector_to_direction(target.x - unit->pos.x, target.y - unit->pos.y);
                if (order.type == OrderType::PlantBomb) plant_bomb(order.ant_id, target);
                else if (order.type == OrderType::DefuseBomb) defuse_bomb(order.ant_id, target);
                else if (order.type == OrderType::IgniteFire) ignite_fire(order.ant_id, target);
                else if (order.type == OrderType::ExtinguishFire) extinguish_fire(order.ant_id, target);
                else if (order.type == OrderType::BuildBridge) build_bridge_step(order.ant_id, target);
            } else {
                // Find closest traversable cardinal neighbor to target
                static const int offsets[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
                TileCoord best_cand{-1, -1};
                int32_t best_dist = 999999;
                for (const auto& off : offsets) {
                    TileCoord cand{target.x + off[0], target.y + off[1]};
                    if (can_unit_traverse(unit->type, cand)) {
                        int32_t d = std::abs(cand.x - unit->pos.x) + std::abs(cand.y - unit->pos.y);
                        if (d < best_dist) {
                            best_dist = d;
                            best_cand = cand;
                        }
                    }
                }
                if (best_cand.x >= 0) {
                    unit->pending_ability = order.type;
                    unit->ability_target = target;
                    issue_move_order(order.ant_id, best_cand);
                    unit->final_dest = target;
                } else if (can_unit_traverse(unit->type, target)) {
                    unit->pending_ability = order.type;
                    unit->ability_target = target;
                    issue_move_order(order.ant_id, target);
                    unit->final_dest = target;
                }
            }
            break;
        }
        case OrderType::InfiltrateAnthill: {
            uint8_t target_team = (order.target_entity_id >= 0) ? static_cast<uint8_t>(order.target_entity_id) : 255;
            int32_t tx = order.target_x;
            int32_t ty = order.target_y;
            if (tx == 0 && ty == 0 && target_team < MAX_PLAYERS) {
                const auto* ah = impl_->grid_.find_anthill(target_team);
                if (ah) { tx = ah->x + 1; ty = ah->y + 1; }
            }
            if (target_team == 255) {
                for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                    const auto* ah = impl_->grid_.find_anthill(p);
                    if (ah && tx >= ah->x && tx < ah->x + 4 && ty >= ah->y && ty < ah->y + 4) {
                        target_team = p;
                        tx = ah->x + 1;
                        ty = ah->y + 1;
                        break;
                    }
                }
            }
            if (target_team < MAX_PLAYERS && impl_->stats_.get_individual_score(target_team) <= 0) {
                impl_->news_queue_.push_back(NewsEvent{unit->player_id, "Enemy anthill has no food!", impl_->match_time_remaining_ms_, 0});
                break;
            }
            unit->target_team_id = target_team;
            if (unit->pos.x == tx && unit->pos.y == ty) {
                start_thief_infiltration(order.ant_id, target_team);
            } else {
                issue_move_order(order.ant_id, TileCoord{tx, ty});
            }
            break;
        }
        case OrderType::Cancel:
            unit->pending_ability = OrderType::None;
            unit->ability_target = TileCoord{-1, -1};
            unit->clear_path();
            unit->state = (unit->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            break;
        default:
            break;
    }
}

bool SimulationEngine::hatch_ant(uint8_t player_id, AntType type) {
    if (is_match_over() || impl_->match_state_ == MatchState::GameOver) return false;
    if (player_id >= MAX_PLAYERS) return false;
    if (impl_->stats_.get_individual_score(player_id) < static_cast<int32_t>(HATCH_COST_POINTS)) return false;
    if (impl_->stats_.get_egg_count(player_id) < 1) return false;

    impl_->stats_.deduct_score(player_id, HATCH_COST_POINTS);
    impl_->stats_.set_egg_count(player_id, impl_->stats_.get_egg_count(player_id) - 1);
    impl_->stats_.get_player_stats_mut(player_id).ants_hatched++;
    impl_->stats_.get_player_stats_mut(player_id).new_hatched++;

    TileCoord spawn_pos{10, 10};
    const auto* a = impl_->grid_.find_anthill(player_id);
    if (a) {
        spawn_pos = TileCoord{static_cast<int32_t>(a->x + 1), static_cast<int32_t>(a->y + 1)};
    }

    uint32_t uid = spawn_unit(player_id, type, spawn_pos);
    AntUnit* unit = impl_->find_unit(uid);
    if (unit) {
        unit->state = UnitState::EnteringBase;
        unit->is_newborn = true;
        unit->had_food_at_base_entry = false;
        unit->facing = Direction::South;
        if (impl_->hatch_delay_ticks_ > 0) {
            unit->underground = true;
            unit->state_timer = static_cast<uint16_t>(impl_->hatch_delay_ticks_);
            unit->anim_subitem = 0;
        } else {
            unit->underground = false;
            unit->state_timer = 0;
            unit->anim_subitem = 8;
        }
    }
    return true;
}

size_t SimulationEngine::get_pending_hatch_count(uint8_t player_id) const {
    size_t count = 0;
    for (const auto& a : impl_->ants_) {
        if (a && a->player_id == player_id && a->is_newborn && a->underground) {
            count++;
        }
    }
    return count;
}

void SimulationEngine::set_hatch_delay_ticks(uint32_t ticks) {
    impl_->hatch_delay_ticks_ = ticks;
}

void SimulationEngine::propose_alliance(uint8_t from_player, uint8_t to_player) {
    if (from_player >= MAX_PLAYERS || to_player >= MAX_PLAYERS || from_player == to_player) return;
    impl_->stats_.set_pending_invite(to_player, from_player, static_cast<uint32_t>(impl_->current_tick_ + 200));
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AlliancePro, 0, 0, 1, to_player});
    impl_->news_queue_.push_back(NewsEvent{to_player, "Alliance proposed", impl_->match_time_remaining_ms_, StringID::AllianceInvitePrompt});
}

void SimulationEngine::respond_alliance(uint8_t responding_player, uint8_t proposing_player, bool accept) {
    if (accept) {
        accept_alliance(responding_player, proposing_player);
    } else {
        deny_alliance(responding_player, proposing_player);
    }
}

void SimulationEngine::accept_alliance(uint8_t responding_player, uint8_t proposing_player) {
    if (responding_player >= MAX_PLAYERS || proposing_player >= MAX_PLAYERS) return;
    impl_->stats_.set_alliance(responding_player, proposing_player);
    impl_->stats_.clear_pending_invite(responding_player);
    impl_->world_state_dirty_ = true;

    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceYes, 0, 0, 1, 255});
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceOn, 0, 0, 1, 255});
    impl_->news_queue_.push_back(NewsEvent{255, "Alliance formed!", impl_->match_time_remaining_ms_, StringID::AllianceFormedBroadcast});
}

void SimulationEngine::deny_alliance(uint8_t responding_player, uint8_t proposing_player) {
    if (responding_player >= MAX_PLAYERS || proposing_player >= MAX_PLAYERS) return;
    impl_->stats_.clear_pending_invite(responding_player);
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceNot, 0, 0, 1, proposing_player});
    impl_->news_queue_.push_back(NewsEvent{proposing_player, "Alliance declined", impl_->match_time_remaining_ms_, StringID::AllianceDeclined});
}

void SimulationEngine::break_alliance(uint8_t player_id) {
    if (player_id >= MAX_PLAYERS) return;
    impl_->stats_.break_alliance(player_id);
    impl_->world_state_dirty_ = true;
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceBreak, 0, 0, 1, 255});
    impl_->news_queue_.push_back(NewsEvent{255, "Alliance broken!", impl_->match_time_remaining_ms_, StringID::AllianceBrokenBroadcast});
}

void SimulationEngine::break_alliance(uint8_t p1, uint8_t p2) {
    if (p1 >= MAX_PLAYERS || p2 >= MAX_PLAYERS) return;
    impl_->stats_.break_alliance(p1, p2);
    impl_->world_state_dirty_ = true;
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceBreak, 0, 0, 1, 255});
    impl_->news_queue_.push_back(NewsEvent{255, "Alliance broken!", impl_->match_time_remaining_ms_, StringID::AllianceBrokenBroadcast});
}

void SimulationEngine::form_alliance(uint8_t p1, uint8_t p2) {
    if (p1 < MAX_PLAYERS && p2 < MAX_PLAYERS) {
        impl_->stats_.set_alliance(p1, p2);
        impl_->world_state_dirty_ = true;
    }
}

const WorldState& SimulationEngine::get_world_state() const {
    if (impl_->world_state_dirty_) {
        impl_->world_state_cache_.tick_number = impl_->current_tick_;
        impl_->world_state_cache_.match_time_remaining_ms = impl_->match_time_remaining_ms_;
        impl_->world_state_cache_.match_state = impl_->match_state_;
        impl_->world_state_cache_.width = impl_->grid_.width();
        impl_->world_state_cache_.height = impl_->grid_.height();
        impl_->world_state_cache_.cells = impl_->grid_.cells();

        impl_->world_state_cache_.ants.clear();
        for (const auto& a : impl_->ants_) {
            if (!a) continue;
            // Eliminated units with 0 HP disappear from active world state (unless drowning animation active)
            if (a->hp == 0 && a->state != UnitState::Drowning) continue;
            AntSnapshot s{};
            s.id = a->id;
            s.player_id = a->player_id;
            s.type = a->type;
            s.px = a->pixel_x;
            s.py = a->pixel_y;
            s.tile_x = a->pos.x;
            s.tile_y = a->pos.y;
            s.facing = static_cast<uint8_t>(a->facing);
            s.hp = a->hp;
            s.max_hp = a->max_hp;
            s.anim_state = static_cast<uint16_t>(a->state);
            s.anim_frame = a->anim_subitem;
            s.is_holding = a->is_holding();
            s.carried_points = a->carried_points;
            s.is_airborne = (a->state == UnitState::Knockback);
            s.is_stunned = a->is_stunned();
            s.is_swimming = (a->state == UnitState::Swimming || a->in_water);
            s.is_underground = a->underground;
            s.is_drowning = (a->state == UnitState::Drowning);
            s.is_on_mud = a->is_on_mud;
            s.is_transforming = a->is_transforming();
            s.transform_anim_frame = (a->transform_timer > 0) ? static_cast<uint16_t>(11 - a->transform_timer) : 0;
            impl_->world_state_cache_.ants.push_back(s);
        }

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            impl_->world_state_cache_.player_stats[i] = impl_->stats_.get_player_stats(i);
            impl_->world_state_cache_.player_scores[i] = impl_->stats_.get_display_score(i);
            impl_->world_state_cache_.player_eggs[i] = impl_->stats_.get_egg_count(i);
            impl_->world_state_cache_.player_alliances[i] = impl_->stats_.get_alliance(i);
        }

        impl_->world_state_cache_.anthills = impl_->grid_.anthills();
        impl_->world_state_cache_.match_result = impl_->stats_.evaluate_victory();
        impl_->world_state_dirty_ = false;
    }
    return impl_->world_state_cache_;
}

uint32_t SimulationEngine::get_match_time_remaining_ms() const {
    return impl_->match_time_remaining_ms_;
}

void SimulationEngine::set_match_time_remaining_ms(uint32_t ms) {
    impl_->match_time_remaining_ms_ = ms;
    if (ms == 0 && impl_->match_state_ == MatchState::Running) {
        impl_->handle_game_over();
    }
}

bool SimulationEngine::is_match_over() const {
    return impl_->match_state_ == MatchState::GameOver || impl_->match_time_remaining_ms_ == 0;
}

PlayerMatchStats SimulationEngine::get_player_stats(uint8_t player_id) const {
    return impl_->stats_.get_player_stats(player_id);
}

std::vector<AudioEvent> SimulationEngine::poll_audio_events() {
    std::vector<AudioEvent> res = std::move(impl_->audio_queue_);
    impl_->audio_queue_.clear();
    return res;
}

std::vector<NewsEvent> SimulationEngine::poll_news_events() {
    std::vector<NewsEvent> res = std::move(impl_->news_queue_);
    impl_->news_queue_.clear();
    return res;
}

uint64_t SimulationEngine::current_tick() const noexcept {
    return impl_->current_tick_;
}

const Grid& SimulationEngine::grid() const {
    return impl_->grid_;
}
Grid& SimulationEngine::grid_mut() {
    return impl_->grid_;
}

const PRNG& SimulationEngine::prng() const {
    return impl_->prng_;
}
PRNG& SimulationEngine::prng_mut() {
    return impl_->prng_;
}

const MatchStatsManager& SimulationEngine::stats_manager() const {
    return impl_->stats_;
}
MatchStatsManager& SimulationEngine::stats_manager_mut() {
    return impl_->stats_;
}

bool SimulationEngine::has_audio_event(uint32_t sound_id) const {
    for (const auto& e : impl_->audio_queue_) {
        if (e.sound_id == sound_id) return true;
    }
    return false;
}

bool SimulationEngine::has_targeted_audio_event(uint8_t player_id, uint32_t sound_id) const {
    for (const auto& e : impl_->audio_queue_) {
        if (e.sound_id == sound_id && (e.target_player == player_id || e.target_player == 255)) {
            return true;
        }
    }
    return false;
}

bool SimulationEngine::has_news_event(uint8_t player_id, uint16_t string_id) const {
    for (const auto& n : impl_->news_queue_) {
        if (n.string_id == string_id && (n.target_player == player_id || n.target_player == 255)) {
            return true;
        }
    }
    return false;
}

void SimulationEngine::clear_audio_events() {
    impl_->audio_queue_.clear();
}

void SimulationEngine::clear_news_events() {
    impl_->news_queue_.clear();
}

uint32_t SimulationEngine::spawn_unit(uint8_t player_id, AntType type, TileCoord pos) {
    uint32_t id = impl_->next_ant_id_++;
    auto unit = std::make_unique<AntUnit>(id, static_cast<TeamId>(player_id % 4), type, pos.x, pos.y);
    unit->player_id = player_id;
    unit->facing = static_cast<Direction>(impl_->prng_.rand() % 8);
    AntUnit* unit_ptr = unit.get();
    impl_->ants_.push_back(std::move(unit));
    if (type == AntType::Combat) {
        impl_->get_or_create_ai(*unit_ptr);
    }
    impl_->world_state_dirty_ = true;
    return id;
}

AntUnit& SimulationEngine::get_unit(uint32_t ant_id) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) {
        throw std::runtime_error("Unit not found");
    }
    return *u;
}

const AntUnit& SimulationEngine::get_unit(uint32_t ant_id) const {
    const AntUnit* u = impl_->find_unit(ant_id);
    if (!u) {
        throw std::runtime_error("Unit not found");
    }
    return *u;
}

void SimulationEngine::kill_unit(uint32_t ant_id) {
    leave_base_queue(ant_id);
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;
    u->hp = 0;
    u->state = UnitState::Dead;
    u->death_status = DeathStatus::CombatKilled;
    if (u->is_holding()) {
        impl_->grid_.drop_lunchbox(static_cast<uint32_t>(u->pos.x), static_cast<uint32_t>(u->pos.y), u->carried_points);
        u->clear_inventory();
    }
}

void SimulationEngine::execute_melee_attack(uint32_t attacker_id, uint32_t target_id) {
    AntUnit* attacker = impl_->find_unit(attacker_id);
    AntUnit* target = impl_->find_unit(target_id);
    if (!attacker || !target || !attacker->is_alive() || !target->is_alive()) return;

    if (is_ant_in_base_queue(target_id)) {
        leave_base_queue(target_id);
    }

    if (attacker->type == AntType::Combat) {
        // Combat Ant: 2 HP heavy punch, Sound 78, 4-5 tile knockback, 12-tick stun
        bool lethal = target->take_damage(2, DamageSource::CombatPunch, attacker->id);
        if (lethal) {
            impl_->stats_.get_player_stats_mut(target->player_id).friendly_lost++;
            impl_->stats_.get_player_stats_mut(attacker->player_id).enemy_killed++;
        }

        int32_t dx = target->pos.x - attacker->pos.x;
        int32_t dy = target->pos.y - attacker->pos.y;
        if (dx == 0 && dy == 0) dx = 1;
        int32_t dist = 4 + (impl_->prng_.rand() & 1);

        TileCoord land_pos = target->pos;
        for (int32_t s = 1; s <= dist; ++s) {
            TileCoord next_pos{target->pos.x + dx * s, target->pos.y + dy * s};
            if (!impl_->grid_.in_bounds(next_pos) || impl_->grid_.is_solid_obstacle(next_pos.x, next_pos.y)) {
                break;
            }
            land_pos = next_pos;
            if (impl_->grid_.has_fire_at(next_pos)) {
                break; // Flight interrupted by fire contact!
            }
        }

        target->pos = land_pos;
        target->pixel_x = land_pos.x * 32 + 16;
        target->pixel_y = land_pos.y * 32 + 16;
        target->fx_x = target->pixel_x << 16;
        target->fx_y = target->pixel_y << 16;
        target->clear_path();
        target->state = UnitState::Knockback;
        target->start_stun(AntUnit::STUN_TICKS);
        impl_->audio_queue_.push_back(AudioEvent{SoundID::HeavyPunch, attacker->pixel_x, attacker->pixel_y, 1, 255});
        impl_->audio_queue_.push_back(AudioEvent{SoundID::StunRecover, target->pixel_x, target->pixel_y, 0, 255});

        // Fire collision check on landing / contact
        if (impl_->grid_.has_fire_at(target->pos)) {
            impl_->physics_.resolve_fire_contact(*target, impl_->grid_, impl_->audio_queue_, impl_->prng_, dx, dy);
        }

        if (!lethal && target->hp == 1 && target->state != UnitState::EnteringBase && !target->underground) {
            const auto* home = impl_->grid_.find_anthill(target->player_id);
            if (home) {
                join_base_queue(target->id);
            }
        }
    } else {
        // Standard Ant: 1 HP melee strike, Sound 57
        impl_->audio_queue_.push_back(AudioEvent{SoundID::MeleeAttack, attacker->pixel_x, attacker->pixel_y, 1, 255});
        bool lethal = target->take_damage(1, DamageSource::MeleeStandard, attacker->id);
        if (lethal) {
            impl_->stats_.get_player_stats_mut(target->player_id).friendly_lost++;
            impl_->stats_.get_player_stats_mut(attacker->player_id).enemy_killed++;
        } else {
            target->start_flinch();

            if (target->hp == 1 && target->state != UnitState::EnteringBase && !target->underground) {
                const auto* home = impl_->grid_.find_anthill(target->player_id);
                if (home) {
                    join_base_queue(target->id);
                }
            }
        }
    }
}

void SimulationEngine::issue_move_order(uint32_t ant_id, TileCoord dest) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit || !unit->is_alive() || unit->is_stunned()) return;

    // Authentic mud "humping" animation cancel:
    // Reissuing a move order while traversing mud resets the struggle cycle and gives a micro-step
    if (unit->is_on_mud && unit->state == UnitState::Walking) {
        unit->anim_tick = 0;
        unit->anim_subitem = 0;
        if (unit->current_waypoint_idx < unit->waypoints.size()) {
            const TileCoord wpt = unit->waypoints[unit->current_waypoint_idx];
            int32_t target_px = wpt.x * 32 + 16;
            int32_t target_py = wpt.y * 32 + 16;
            int32_t cur_px = unit->fx_x >> 16;
            int32_t cur_py = unit->fx_y >> 16;
            int32_t dx = target_px - cur_px;
            int32_t dy = target_py - cur_py;
            if (dx > 0) unit->fx_x += (2 << 16);
            else if (dx < 0) unit->fx_x -= (2 << 16);
            if (dy > 0) unit->fx_y += (2 << 16);
            else if (dy < 0) unit->fx_y -= (2 << 16);
            unit->sync_pixel_from_fx();
        }
    }

    bool is_swimmer = (unit->type == AntType::Swimmer);
    bool is_fire_ant = (unit->type == AntType::Fire);

    if (unit->pos == dest) {
        unit->clear_path();
        unit->state = (unit->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
        unit->final_dest = unit->pos;
        return;
    }

    // Check if dest is occupied by a stationary friendly ant
    const AntUnit* occ_ant = nullptr;
    for (const auto& other : impl_->ants_) {
        if (other && other->is_alive() && !other->underground && other->id != unit->id && other->pos == dest) {
            bool is_enemy = (other->player_id != unit->player_id && !impl_->stats_.are_allies(unit->player_id, other->player_id));
            if (!is_enemy && other->state != UnitState::Walking) {
                occ_ant = other.get();
                break;
            }
        }
    }

    if (occ_ant) {
        // If already adjacent to dest, the ant has reached as close as it can get
        int32_t cdx = std::abs(unit->pos.x - dest.x);
        int32_t cdy = std::abs(unit->pos.y - dest.y);
        if (cdx <= 1 && cdy <= 1) {
            unit->clear_path();
            unit->state = (unit->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            unit->final_dest = unit->pos;
            return;
        }

        // Find nearest unoccupied, passable neighbor to dest (closest to unit->pos)
        TileCoord best_neighbor = dest;
        int32_t best_dist_sq = INT32_MAX;

        for (int r = 1; r <= 2; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int32_t nx = dest.x + dx;
                    int32_t ny = dest.y + dy;
                    if (!impl_->grid_.in_bounds(nx, ny) || !impl_->grid_.get_cell(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)).is_passable(is_swimmer, is_fire_ant)) {
                        continue;
                    }
                    TileCoord cand{nx, ny};
                    bool occupied = false;
                    for (const auto& other : impl_->ants_) {
                        if (other && other->is_alive() && !other->underground && other->id != unit->id && other->pos == cand) {
                            occupied = true;
                            break;
                        }
                    }
                    if (occupied) continue;

                    int32_t dist_sq = (unit->pos.x - nx) * (unit->pos.x - nx) + (unit->pos.y - ny) * (unit->pos.y - ny);
                    if (dist_sq < best_dist_sq) {
                        best_dist_sq = dist_sq;
                        best_neighbor = cand;
                    }
                }
            }
            if (best_neighbor != dest) break;
        }

        if (best_neighbor != dest) {
            dest = best_neighbor;
            if (unit->pos == dest) {
                unit->clear_path();
                unit->state = (unit->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                unit->final_dest = unit->pos;
                return;
            }
        }
    }

    std::vector<TileCoord> dynamic_obstacles;
    for (const auto& other : impl_->ants_) {
        if (other && other->is_alive() && !other->underground && other->id != unit->id) {
            bool is_enemy = (other->player_id != unit->player_id && !impl_->stats_.are_allies(unit->player_id, other->player_id));
            bool is_stationary_friendly = (!is_enemy && other->state != UnitState::Walking);
            if (is_enemy || is_stationary_friendly) {
                if (other->pos != unit->pos && other->pos != dest) {
                    dynamic_obstacles.push_back(other->pos);
                }
            }
        }
    }

    auto path = PathFinder::find_path(impl_->grid_, unit->pos, dest, is_swimmer, is_fire_ant, 4000, dynamic_obstacles);
    if (path.empty() && !dynamic_obstacles.empty()) {
        path = PathFinder::find_path(impl_->grid_, unit->pos, dest, is_swimmer, is_fire_ant, 4000);
    }
    if (!path.empty()) {
        unit->set_path(std::move(path));
        unit->final_dest = dest;
    } else {
        unit->set_destination(dest.x, dest.y);
    }
}

bool SimulationEngine::validate_cardinal_placement(TileCoord from, TileCoord to) const {
    int32_t dx = to.x - from.x;
    int32_t dy = to.y - from.y;
    if (dx != 0 && dy != 0) return false;
    return (std::abs(dx) + std::abs(dy) == 1);
}

bool SimulationEngine::plant_bomb(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || ant->type != AntType::Bomber) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.in_bounds(target) || !impl_->grid_.get_cell(target).can_place_bomb()) return false;

    impl_->grid_.place_bomb(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), ant->player_id);
    impl_->audio_queue_.push_back(AudioEvent{SoundID::BombPick, ant->pixel_x, ant->pixel_y, 1, 255});
    impl_->stats_.get_player_stats_mut(ant->player_id).bombs_planted++;
    return true;
}

bool SimulationEngine::defuse_bomb(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || ant->type != AntType::Bomber) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.has_bomb_at(target)) return false;

    impl_->grid_.clear_bomb(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y));
    impl_->audio_queue_.push_back(AudioEvent{SoundID::BombDefuseGrab, ant->pixel_x, ant->pixel_y, 1, 255});
    impl_->audio_queue_.push_back(AudioEvent{SoundID::BombBodySquash, ant->pixel_x, ant->pixel_y, 1, 255});
    impl_->stats_.get_player_stats_mut(ant->player_id).bombs_defused++;
    return true;
}

bool SimulationEngine::ignite_fire(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || ant->type != AntType::Fire) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.in_bounds(target) || !impl_->grid_.get_cell(target).can_place_fire()) return false;

    impl_->grid_.place_firewall(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), ant->player_id);
    impl_->audio_queue_.push_back(AudioEvent{SoundID::FireBeam, ant->pixel_x, ant->pixel_y, 1, 255});
    impl_->audio_queue_.push_back(AudioEvent{SoundID::FireErupt, ant->pixel_x, ant->pixel_y, 1, 255});
    impl_->stats_.get_player_stats_mut(ant->player_id).fires_lit++;
    return true;
}

bool SimulationEngine::extinguish_fire(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || ant->type != AntType::Fire) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.has_fire_at(target)) return false;

    impl_->grid_.clear_firewall(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y));
    impl_->audio_queue_.push_back(AudioEvent{SoundID::FireExtinguish, ant->pixel_x, ant->pixel_y, 1, 255});
    return true;
}

bool SimulationEngine::build_bridge_step(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || ant->type != AntType::Swimmer) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.in_bounds(target)) return false;
    if (impl_->grid_.get_cell(target).terrain_type != TERRAIN_WATER) return false;

    impl_->grid_.advance_bridge(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), ant->player_id);
    impl_->audio_queue_.push_back(AudioEvent{SoundID::ShovelWater, ant->pixel_x, ant->pixel_y, 1, 255});
    return true;
}

bool SimulationEngine::can_unit_traverse(AntType type, TileCoord pos) const {
    if (!impl_->grid_.in_bounds(pos)) return false;
    const auto& cell = impl_->grid_.get_cell(pos);
    return cell.is_passable(type == AntType::Swimmer, type == AntType::Fire);
}

bool SimulationEngine::has_bomb_at(TileCoord pos) const {
    return impl_->grid_.has_bomb_at(pos);
}

bool SimulationEngine::has_fire_at(TileCoord pos) const {
    return impl_->grid_.has_fire_at(pos);
}

uint32_t SimulationEngine::get_fire_timer(TileCoord pos) const {
    return impl_->grid_.get_fire_timer(pos);
}

void SimulationEngine::set_fire_at(TileCoord pos, uint32_t timer_ticks) {
    impl_->grid_.set_fire_at(pos, timer_ticks);
}

bool SimulationEngine::has_bridge_at(TileCoord pos) const {
    return impl_->grid_.has_bridge_at(pos);
}

int SimulationEngine::get_bridge_stage(TileCoord pos) const {
    return impl_->grid_.get_bridge_stage(pos);
}

void SimulationEngine::set_bridge_at(TileCoord pos, int stage, uint32_t timer_ticks) {
    impl_->grid_.set_bridge_at(pos, stage, timer_ticks);
}

void SimulationEngine::set_terrain(int32_t x, int32_t y, uint8_t terrain_type) {
    impl_->grid_.set_terrain(x, y, terrain_type);
}

void SimulationEngine::set_tile_flags(int32_t x, int32_t y, uint16_t flags) {
    impl_->grid_.set_tile_flags(x, y, flags);
}

void SimulationEngine::set_anthill(uint8_t team_id, TileCoord pos) {
    impl_->grid_.set_anthill(team_id, pos);
}

TileCoord SimulationEngine::assign_queue_slot(uint8_t team_id, TileCoord from_pos) {
    const auto* a = impl_->grid_.find_anthill(team_id);
    int32_t bx = a ? a->x : 30;
    int32_t by = a ? a->y : 30;

    for (int32_t r = 1; r <= 5; ++r) {
        std::vector<TileCoord> candidates;
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int32_t x = bx + dx;
                int32_t y = by + dy;

                if (!impl_->grid_.in_bounds(x, y)) continue;

                const auto& cell = impl_->grid_.get_cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                if (!cell.is_passable(false, false)) continue;

                TileCoord cand{x, y};

                bool is_reserved = std::any_of(
                    impl_->reserved_queue_slots_.begin(),
                    impl_->reserved_queue_slots_.end(),
                    [&cand](const TileCoord& slot) { return slot == cand; }
                );
                if (is_reserved) continue;

                bool is_ant_queued = false;
                for (const auto& ant : impl_->ants_) {
                    if (ant && ant->is_alive() &&
                        (ant->state == UnitState::QueuingBase || ant->state == UnitState::EnteringBase) &&
                        ant->pos == cand) {
                        is_ant_queued = true;
                        break;
                    }
                }
                if (is_ant_queued) continue;

                candidates.push_back(cand);
            }
        }

        if (!candidates.empty()) {
            auto best_it = std::min_element(
                candidates.begin(),
                candidates.end(),
                [&from_pos](const TileCoord& c1, const TileCoord& c2) {
                    int32_t m1 = c1.manhattan_dist(from_pos);
                    int32_t m2 = c2.manhattan_dist(from_pos);
                    if (m1 != m2) return m1 < m2;

                    int64_t edx1 = c1.x - from_pos.x;
                    int64_t edy1 = c1.y - from_pos.y;
                    int64_t edx2 = c2.x - from_pos.x;
                    int64_t edy2 = c2.y - from_pos.y;
                    int64_t e1 = edx1 * edx1 + edy1 * edy1;
                    int64_t e2 = edx2 * edx2 + edy2 * edy2;
                    if (e1 != e2) return e1 < e2;

                    if (c1.y != c2.y) return c1.y < c2.y;
                    return c1.x < c2.x;
                }
            );

            TileCoord best_slot = *best_it;
            impl_->reserved_queue_slots_.push_back(best_slot);
            return best_slot;
        }
    }

    return TileCoord{bx + 1, by};
}

void SimulationEngine::release_queue_slot(TileCoord slot) {
    auto it = std::find(impl_->reserved_queue_slots_.begin(), impl_->reserved_queue_slots_.end(), slot);
    if (it != impl_->reserved_queue_slots_.end()) {
        impl_->reserved_queue_slots_.erase(it);
    }
}

void SimulationEngine::clear_reserved_queue_slots() {
    impl_->reserved_queue_slots_.clear();
}

bool SimulationEngine::is_queue_slot_reserved(TileCoord slot) const {
    return std::find(impl_->reserved_queue_slots_.begin(), impl_->reserved_queue_slots_.end(), slot) != impl_->reserved_queue_slots_.end();
}

void SimulationEngine::send_ant_straight_into_base(uint32_t ant_id) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit || !unit->is_alive() || unit->player_id >= MAX_PLAYERS) return;

    const auto* base = impl_->grid_.find_anthill(unit->player_id);
    if (!base) return;

    int32_t bx = base->x;
    int32_t by = base->y;
    const std::array<TileCoord, 6> ramp = {{
        {bx, by + 3},
        {bx, by + 2},
        {bx, by + 1},
        {bx, by},
        {bx + 1, by},
        {bx + 1, by + 1}
    }};

    std::vector<TileCoord> waypoints;
    int32_t ramp_idx = -1;
    for (size_t i = 0; i < ramp.size(); ++i) {
        if (unit->pos == ramp[i]) {
            ramp_idx = static_cast<int32_t>(i);
            break;
        }
    }

    if (ramp_idx >= 0) {
        for (size_t i = static_cast<size_t>(ramp_idx + 1); i < ramp.size(); ++i) {
            waypoints.push_back(ramp[i]);
        }
    } else {
        bool is_swimmer = (unit->type == AntType::Swimmer);
        bool is_fire_ant = (unit->type == AntType::Fire);
        std::vector<TileCoord> dynamic_obstacles;
        for (const auto& other : impl_->ants_) {
            if (other && other->is_alive() && !other->underground && other->id != unit->id) {
                bool is_enemy = (other->player_id != unit->player_id && !impl_->stats_.are_allies(unit->player_id, other->player_id));
                bool is_stationary_friendly = (!is_enemy && other->state != UnitState::Walking);
                if (is_enemy || is_stationary_friendly) {
                    if (other->pos != unit->pos && other->pos != ramp[0]) {
                        dynamic_obstacles.push_back(other->pos);
                    }
                }
            }
        }

        waypoints = PathFinder::find_path(impl_->grid_, unit->pos, ramp[0], is_swimmer, is_fire_ant, 4000, dynamic_obstacles);
        if (waypoints.empty() && !dynamic_obstacles.empty()) {
            waypoints = PathFinder::find_path(impl_->grid_, unit->pos, ramp[0], is_swimmer, is_fire_ant, 4000);
        }

        if (!waypoints.empty() && waypoints.back() == ramp[0]) {
            for (size_t i = 1; i < ramp.size(); ++i) {
                waypoints.push_back(ramp[i]);
            }
        } else if (unit->pos == ramp[0]) {
            for (size_t i = 1; i < ramp.size(); ++i) {
                waypoints.push_back(ramp[i]);
            }
        } else if (!waypoints.empty()) {
            waypoints.push_back(ramp[0]);
            for (size_t i = 1; i < ramp.size(); ++i) {
                waypoints.push_back(ramp[i]);
            }
        } else {
            for (size_t i = 0; i < ramp.size(); ++i) {
                waypoints.push_back(ramp[i]);
            }
        }
    }

    if (!waypoints.empty()) {
        unit->set_path(std::move(waypoints));
        unit->final_dest = TileCoord{bx + 1, by + 1};
        unit->state = UnitState::Walking;
    } else if (unit->pos == TileCoord{bx + 1, by + 1}) {
        unit->clear_path();
        unit->final_dest = TileCoord{bx + 1, by + 1};
    }
}

void SimulationEngine::dispatch_next_base_queue(uint8_t player_id) {
    if (player_id >= MAX_PLAYERS) return;
    auto& bq = impl_->base_queues_[player_id];

    auto q_it = bq.queue.begin();
    while (q_it != bq.queue.end()) {
        AntUnit* u = impl_->find_unit(*q_it);
        if (!u || !u->is_alive() || u->player_id != player_id) {
            if (bq.active_depositing_ant_id == *q_it) {
                bq.active_depositing_ant_id = 0;
            }
            q_it = bq.queue.erase(q_it);
        } else {
            ++q_it;
        }
    }

    if (bq.active_depositing_ant_id == 0 && !bq.queue.empty()) {
        uint32_t head_id = bq.queue.front();
        bq.active_depositing_ant_id = head_id;
        send_ant_straight_into_base(head_id);
    }
}

void SimulationEngine::join_base_queue(uint32_t ant_id) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit || !unit->is_alive() || unit->player_id >= MAX_PLAYERS) return;

    auto& bq = impl_->base_queues_[unit->player_id];
    auto it = std::find(bq.queue.begin(), bq.queue.end(), ant_id);
    if (it == bq.queue.end()) {
        bq.queue.push_back(ant_id);
    }

    if (bq.active_depositing_ant_id == 0 || bq.active_depositing_ant_id == ant_id) {
        bq.active_depositing_ant_id = ant_id;
        send_ant_straight_into_base(ant_id);
    } else {
        size_t idx = static_cast<size_t>(std::distance(bq.queue.begin(), std::find(bq.queue.begin(), bq.queue.end(), ant_id)));
        TileCoord target_slot = get_base_queue_slot(unit->player_id, idx);
        issue_move_order(ant_id, target_slot);
        unit->final_dest = target_slot;
    }
}

void SimulationEngine::leave_base_queue(uint32_t ant_id) {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        auto& bq = impl_->base_queues_[p];
        bool was_active = (bq.active_depositing_ant_id == ant_id);
        if (was_active) {
            bq.active_depositing_ant_id = 0;
        }
        auto it = std::find(bq.queue.begin(), bq.queue.end(), ant_id);
        if (it != bq.queue.end()) {
            bq.queue.erase(it);
        }
        if (was_active) {
            dispatch_next_base_queue(p);
        }
    }
}

bool SimulationEngine::is_ant_in_base_queue(uint32_t ant_id) const {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        const auto& bq = impl_->base_queues_[p];
        if (bq.active_depositing_ant_id == ant_id) return true;
        if (std::find(bq.queue.begin(), bq.queue.end(), ant_id) != bq.queue.end()) return true;
    }
    return false;
}

uint32_t SimulationEngine::get_active_depositing_ant(uint8_t player_id) const {
    if (player_id >= MAX_PLAYERS) return 0;
    return impl_->base_queues_[player_id].active_depositing_ant_id;
}

size_t SimulationEngine::get_base_queue_size(uint8_t player_id) const {
    if (player_id >= MAX_PLAYERS) return 0;
    return impl_->base_queues_[player_id].queue.size();
}

TileCoord SimulationEngine::get_base_queue_slot(uint8_t player_id, size_t index) const {
    const auto* base = impl_->grid_.find_anthill(player_id);
    if (!base) return TileCoord{0, 0};
    int32_t bx = base->x;
    int32_t by = base->y;

    // Queue slots line up strictly to the left of the base:
    // Slot 0: (bx - 1, by + 3)
    // Slot 1: (bx - 1, by + 2)
    // Slot 2: (bx - 1, by + 1)
    // Slot 3: (bx - 1, by)
    // Slot 4: (bx - 2, by + 3), etc.
    size_t col = index / 4;
    size_t row = index % 4;
    int32_t qx = bx - 1 - static_cast<int32_t>(col);
    int32_t qy = by + 3 - static_cast<int32_t>(row);

    if (impl_->grid_.in_bounds(qx, qy) && impl_->grid_.get_cell(TileCoord{qx, qy}).is_passable(false, false)) {
        return TileCoord{qx, qy};
    }

    // Fallback: search adjacent passable cells strictly to the left of base mound
    for (int32_t r = 1; r <= 4; ++r) {
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                int32_t cx = (bx - 1) + dx;
                int32_t cy = (by + 3) + dy;
                if (cx < bx && impl_->grid_.in_bounds(cx, cy) &&
                    impl_->grid_.get_cell(TileCoord{cx, cy}).is_passable(false, false)) {
                    return TileCoord{cx, cy};
                }
            }
        }
    }

    return TileCoord{std::max(0, bx - 1), by + 3};
}

void SimulationEngine::step_base_entry_animation(uint32_t ant_id, uint16_t target_frame) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;
    u->anim_subitem = target_frame;

    if (target_frame >= 4 && u->is_holding()) {
        auto [food, pts] = u->deposit_food();
        uint32_t deposit_pts = (pts > 0) ? pts : (food * 25);
        impl_->stats_.add_score(u->player_id, static_cast<int32_t>(deposit_pts));
        impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseScoreUp, u->pixel_x, u->pixel_y, 1, u->player_id});
    }

    if (target_frame == 8) {
        u->heal_full();
        impl_->audio_queue_.push_back(AudioEvent{SoundID::PowerUpHeal, u->pixel_x, u->pixel_y, 1, u->player_id});
    }

    if (target_frame >= 16) {
        u->state = UnitState::Idle;
        u->anim_subitem = 0;
    }
}

void SimulationEngine::start_thief_infiltration(uint32_t ant_id, uint8_t target_team_id) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;
    u->state = UnitState::Infiltrating;
    u->target_team_id = target_team_id;
    u->anim_subitem = 0;
}

void SimulationEngine::step_thief_animation(uint32_t ant_id, uint16_t target_frame) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;
    u->anim_subitem = target_frame;

    if (target_frame >= 19) {
        uint8_t victim = u->target_team_id;
        if (victim >= 4) {
            for (const auto& a : impl_->grid_.anthills()) {
                if (a.team_id != u->player_id && a.x == u->pos.x && a.y == u->pos.y) {
                    victim = a.team_id;
                    break;
                }
            }
            if (victim >= 4) {
                victim = (u->player_id == 0) ? 1 : 0;
            }
        }
        impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseAlarmSiren, u->pixel_x, u->pixel_y, 2, victim});
        impl_->news_queue_.push_back(NewsEvent{victim, "A ThiefAnt is at your anthill!", impl_->match_time_remaining_ms_, StringID::ThiefAlarmWarning});
    }
}

void SimulationEngine::execute_thief_loot(uint32_t ant_id, uint8_t target_team_id) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;

    int32_t victim_score = impl_->stats_.get_individual_score(target_team_id);
    if (victim_score <= 0) return;
    int32_t stolen = std::min(MAX_THIEF_STEAL, victim_score);
    impl_->stats_.deduct_score(target_team_id, stolen);
    u->steal_points(static_cast<uint16_t>(stolen));

    impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseScoreDn, u->pixel_x, u->pixel_y, 1, target_team_id});
    impl_->news_queue_.push_back(NewsEvent{target_team_id, "Food stolen...", impl_->match_time_remaining_ms_, StringID::FoodStolenStatus});
}

bool SimulationEngine::has_lunchbox_at(TileCoord pos) const {
    return impl_->grid_.has_lunchbox_at(pos);
}

uint32_t SimulationEngine::get_lunchbox_points(TileCoord pos) const {
    return impl_->grid_.get_lunchbox_points(pos);
}

void SimulationEngine::apply_knockback(uint32_t ant_id, int32_t from_px, int32_t from_py, int32_t min_tiles, int32_t max_tiles) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;
    impl_->physics_.apply_knockback(*u, from_px, from_py, min_tiles, max_tiles,
                                    DamageSource::CombatPunch, impl_->audio_queue_, impl_->prng_.rand());
}

void SimulationEngine::resolve_fire_contact(uint32_t ant_id, int32_t incoming_dx, int32_t incoming_dy) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) return;
    impl_->physics_.resolve_fire_contact(*u, impl_->grid_, impl_->audio_queue_, impl_->prng_, incoming_dx, incoming_dy);
}

int32_t SimulationEngine::get_display_score(uint8_t player_id) const {
    return impl_->stats_.get_display_score(player_id);
}

int32_t SimulationEngine::get_player_score(uint8_t player_id) const {
    return impl_->stats_.get_individual_score(player_id);
}

void SimulationEngine::set_player_score(uint8_t player_id, int32_t score) {
    impl_->stats_.set_individual_score(player_id, score);
}

uint32_t SimulationEngine::get_player_eggs(uint8_t player_id) const {
    return impl_->stats_.get_egg_count(player_id);
}

void SimulationEngine::set_player_eggs(uint8_t player_id, uint32_t eggs) {
    impl_->stats_.set_egg_count(player_id, eggs);
}

uint32_t SimulationEngine::get_player_hatched(uint8_t player_id) const {
    return impl_->stats_.get_player_stats(player_id).ants_hatched;
}

uint8_t SimulationEngine::get_ally_id(uint8_t player_id) const {
    return impl_->stats_.get_alliance(player_id);
}

void SimulationEngine::record_player_stat(uint8_t player_id, StatType stat, uint32_t value) {
    impl_->stats_.record_stat(player_id, stat, value);
}

} // namespace ants::sim
