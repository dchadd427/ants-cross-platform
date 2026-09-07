#include "ants_sim/sim_engine.hpp"
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

    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        impl_->stats_.set_egg_count(p, 10);
    }

    for (const auto& a : level.anthill_spawns) {
        if (a.team_id < MAX_PLAYERS) {
            int32_t sx = a.x;
            int32_t sy = a.y;
            if (!impl_->grid_.in_bounds(sx, sy) || !impl_->grid_.get_cell(sx, sy).is_passable()) {
                static const int offsets[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
                for (const auto& off : offsets) {
                    int32_t nx = sx + off[0];
                    int32_t ny = sy + off[1];
                    if (impl_->grid_.in_bounds(nx, ny) && impl_->grid_.get_cell(nx, ny).is_passable()) {
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
                bool lethal = ant_ptr->take_damage(2, DamageSource::BombBlast, bomb_owner);
                if (lethal) {
                    impl_->stats_.get_player_stats_mut(ant_ptr->player_id).friendly_lost++;
                    if (bomb_owner < MAX_PLAYERS) {
                        impl_->stats_.get_player_stats_mut(bomb_owner).enemy_killed++;
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

    // 5. Step Unit Movement & Timers
    for (auto& ant_ptr : impl_->ants_) {
        if (!ant_ptr || !ant_ptr->is_alive()) continue;
        ant_ptr->tick_timers();
        bool in_water = (impl_->grid_.in_bounds(ant_ptr->pos) &&
                         impl_->grid_.get_cell(ant_ptr->pos).terrain_type == TERRAIN_WATER &&
                         !impl_->grid_.get_cell(ant_ptr->pos).has_completed_bridge());
        ant_ptr->tick_movement(in_water);

        // Universal lunchbox pickup
        if (ant_ptr->is_alive() && !ant_ptr->is_holding() && impl_->grid_.has_lunchbox_at(ant_ptr->pos)) {
            uint32_t pts = impl_->grid_.get_lunchbox_points(ant_ptr->pos);
            impl_->grid_.clear_lunchbox(static_cast<uint32_t>(ant_ptr->pos.x), static_cast<uint32_t>(ant_ptr->pos.y));
            ant_ptr->pick_up_food(1, static_cast<uint16_t>(pts > 0 ? pts : 25));
        }

        // Static Layer 2 food harvest
        if (ant_ptr->is_alive() && !ant_ptr->is_holding() && impl_->grid_.in_bounds(ant_ptr->pos)) {
            auto& cell = impl_->grid_.get_cell_mut(ant_ptr->pos);
            if (cell.has_food()) {
                cell.interactive_id = TILE_EMPTY;
                cell.is_food = false;
                ant_ptr->pick_up_food(1, 25);
            }
        }

        // Autonomous Base Entry progression
        if (ant_ptr->state == UnitState::EnteringBase) {
            ant_ptr->anim_subitem++;
            if (ant_ptr->anim_subitem == 4 && ant_ptr->is_holding()) {
                auto [food, pts] = ant_ptr->deposit_food();
                uint32_t deposit_pts = (pts > 0) ? pts : (food * 25);
                impl_->stats_.add_score(ant_ptr->player_id, static_cast<int32_t>(deposit_pts));
                impl_->audio_queue_.push_back(AudioEvent{SoundID::BaseScoreUp, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, ant_ptr->player_id});
            } else if (ant_ptr->anim_subitem == 8) {
                ant_ptr->heal_full();
                ant_ptr->underground = true;
                impl_->audio_queue_.push_back(AudioEvent{SoundID::PowerUpHeal, ant_ptr->pixel_x, ant_ptr->pixel_y, 1, ant_ptr->player_id});
            } else if (ant_ptr->anim_subitem >= 16) {
                ant_ptr->underground = false;
                ant_ptr->state = (ant_ptr->type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
                ant_ptr->anim_subitem = 0;
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
                ant_ptr->state = UnitState::Idle;
                ant_ptr->anim_subitem = 0;
                const auto* home = impl_->grid_.find_anthill(ant_ptr->player_id);
                if (home) {
                    issue_move_order(ant_ptr->id, TileCoord{home->x, home->y});
                }
            }
            continue;
        }

        // Check arrival at friendly anthill with food or needing healing
        const auto* friendly_base = impl_->grid_.find_anthill(ant_ptr->player_id);
        if (friendly_base && ant_ptr->pos.x == friendly_base->x && ant_ptr->pos.y == friendly_base->y) {
            if (ant_ptr->is_holding() || ant_ptr->hp < ant_ptr->max_hp) {
                ant_ptr->state = UnitState::EnteringBase;
                ant_ptr->anim_subitem = 0;
                ant_ptr->clear_path();
            }
        }

        // Check thief arrival at enemy anthill
        if (ant_ptr->type == AntType::Thief) {
            for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                if (p != ant_ptr->player_id && !impl_->stats_.are_allies(ant_ptr->player_id, p)) {
                    const auto* enemy_base = impl_->grid_.find_anthill(p);
                    if (enemy_base && ant_ptr->pos.x == enemy_base->x && ant_ptr->pos.y == enemy_base->y) {
                        ant_ptr->target_team_id = p;
                        ant_ptr->state = UnitState::Infiltrating;
                        ant_ptr->anim_subitem = 0;
                        ant_ptr->clear_path();
                        break;
                    }
                }
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

    switch (order.type) {
        case OrderType::Move:
            issue_move_order(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::ReturnToBase: {
            const auto* anthill = impl_->grid_.find_anthill(unit->player_id);
            if (anthill) {
                issue_move_order(order.ant_id, TileCoord{anthill->x, anthill->y});
            }
            break;
        }
        case OrderType::Attack:
            if (order.target_entity_id >= 0) {
                execute_melee_attack(order.ant_id, static_cast<uint32_t>(order.target_entity_id));
            }
            break;
        case OrderType::PlantBomb:
            plant_bomb(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::DefuseBomb:
            defuse_bomb(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::IgniteFire:
            ignite_fire(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::ExtinguishFire:
            extinguish_fire(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::BuildBridge:
            build_bridge_step(order.ant_id, TileCoord{order.target_x, order.target_y});
            break;
        case OrderType::InfiltrateAnthill: {
            uint8_t target_team = (order.target_entity_id >= 0) ? static_cast<uint8_t>(order.target_entity_id) : 255;
            int32_t tx = order.target_x;
            int32_t ty = order.target_y;
            if (tx == 0 && ty == 0 && target_team < MAX_PLAYERS) {
                const auto* ah = impl_->grid_.find_anthill(target_team);
                if (ah) { tx = ah->x; ty = ah->y; }
            }
            if (target_team == 255) {
                for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                    const auto* ah = impl_->grid_.find_anthill(p);
                    if (ah && ah->x == tx && ah->y == ty) {
                        target_team = p;
                        break;
                    }
                }
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
        spawn_pos = TileCoord{a->x, a->y};
    }

    spawn_unit(player_id, type, spawn_pos);
    return true;
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
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceBreak, 0, 0, 1, 255});
    impl_->news_queue_.push_back(NewsEvent{255, "Alliance broken!", impl_->match_time_remaining_ms_, StringID::AllianceBrokenBroadcast});
}

void SimulationEngine::break_alliance(uint8_t p1, uint8_t p2) {
    if (p1 >= MAX_PLAYERS || p2 >= MAX_PLAYERS) return;
    impl_->stats_.break_alliance(p1, p2);
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceBreak, 0, 0, 1, 255});
    impl_->news_queue_.push_back(NewsEvent{255, "Alliance broken!", impl_->match_time_remaining_ms_, StringID::AllianceBrokenBroadcast});
}

void SimulationEngine::form_alliance(uint8_t p1, uint8_t p2) {
    if (p1 < MAX_PLAYERS && p2 < MAX_PLAYERS) {
        impl_->stats_.set_alliance(p1, p2);
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
            s.is_swimming = (a->state == UnitState::Swimming);
            s.is_underground = a->underground;
            s.is_drowning = (a->state == UnitState::Drowning);
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

    if (attacker->type == AntType::Combat) {
        // Combat Ant: 2 HP heavy punch, Sound 78, 4-5 tile knockback, 12-tick stun
        impl_->audio_queue_.push_back(AudioEvent{SoundID::HeavyPunch, attacker->pixel_x, attacker->pixel_y, 1, 255});
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

        target->set_tile_pos(land_pos.x, land_pos.y);
        target->start_stun(AntUnit::STUN_TICKS);
        impl_->audio_queue_.push_back(AudioEvent{SoundID::HeavyPunch, attacker->pixel_x, attacker->pixel_y, 1, 255});
        impl_->audio_queue_.push_back(AudioEvent{SoundID::StunRecover, target->pixel_x, target->pixel_y, 0, 255});

        // Fire collision check on landing / contact
        if (impl_->grid_.has_fire_at(target->pos)) {
            impl_->physics_.resolve_fire_contact(*target, impl_->grid_, impl_->audio_queue_, impl_->prng_, dx, dy);
        }
    } else {
        // Standard Ant: 1 HP melee strike, Sound 57
        impl_->audio_queue_.push_back(AudioEvent{SoundID::MeleeAttack, attacker->pixel_x, attacker->pixel_y, 1, 255});
        bool lethal = target->take_damage(1, DamageSource::MeleeStandard, attacker->id);
        if (lethal) {
            impl_->stats_.get_player_stats_mut(target->player_id).friendly_lost++;
            impl_->stats_.get_player_stats_mut(attacker->player_id).enemy_killed++;
        }
    }
}

void SimulationEngine::issue_move_order(uint32_t ant_id, TileCoord dest) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit || !unit->is_alive() || unit->is_stunned()) return;
    unit->set_destination(dest.x, dest.y);
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
