#include "ants_sim/sim_engine.hpp"
#include "sim_engine_impl.hpp"
#include "ants_sim/game_strings.hpp"
#include <memory>
#include <algorithm>
#include <cmath>

namespace ants::sim {

namespace {

// Shared body of move orders: remake bookkeeping, then the original GoTo (Ants.exe FUN_0101fc50), which
// snaps the ant to the centre of its tile, idles it and queues an asynchronous PATHMGR path request.
// user_cmd = order clicked by the player (FUN_0101fc50 arg "player" = 1); remake systems (guard AI, base
// queue, ability approach, harvest return) issue internal orders with user_cmd = false.
// Returns what GoTo returns: true when the order was accepted and a path was requested (the group order acknowledges with it).
bool route_move_order(SimulationEngineImpl& impl, AntUnit& unit, TileCoord dest, bool user_cmd, bool special = false) {
    if (!unit.is_alive()) return false;

    // Special abilities and the power-up pick-up cannot be interrupted and silently ignore move orders
    if (unit.state == UnitState::PoweringUp ||
        unit.state == UnitState::PlacingFire || unit.state == UnitState::ExtinguishingFire ||
        unit.state == UnitState::PlantingBomb || unit.state == UnitState::DefusingBomb ||
        unit.state == UnitState::BuildingBridge || unit.state == UnitState::DemolishingBridge) {
        return false;
    }

    // Accept predicate for player orders (FUN_0101ff5a): action idle, walk or stunned only, not engaged, not frozen.
    // Orders of the remake's own systems need an idle, walking or stunned ant as well.
    if (user_cmd) {
        if (!impl.can_take_user_order(unit)) return false;
    } else {
        const uint8_t act = impl.orig_action_of(unit);
        if (unit.engaged || unit.frozen || !(act == 0 || act == 1 || act == 3)) return false;
    }

    // A remake system repeating the order it already gave (same destination, request still queued) keeps
    // the request's place in the PATHMGR queue instead of restarting the search.
    if (!user_cmd && unit.final_dest == dest && impl.has_pending_path(unit.id)) return true;
    // A remake system sending a standing ant to the tile it stands on (hill queue slot, guard post) just
    // leaves it there; player orders always go through GoTo (a one-tile path, as in the original).
    if (!user_cmd && unit.pos == dest && unit.waypoints.empty() && !impl.has_pending_path(unit.id) &&
        unit.pixel_x == dest.x * 32 + 16 && unit.pixel_y == dest.y * 32 + 16 &&
        unit.loco_action != AntUnit::kActionWalk && !unit.pause_active) {
        unit.final_dest = dest;
        if (unit.state == UnitState::Walking) {
            impl.set_idle_label(unit);
        }
        return true;
    }

    return impl.go_to(unit, dest, user_cmd, special);
}

} // anonymous namespace

SimulationEngine::SimulationEngine()
    : impl_(std::make_unique<SimulationEngineImpl>()) {}

SimulationEngine::~SimulationEngine() = default;
SimulationEngine::SimulationEngine(SimulationEngine&&) noexcept = default;
SimulationEngine& SimulationEngine::operator=(SimulationEngine&&) noexcept = default;

void SimulationEngine::init(const ants::assets::LevelData& level, uint32_t random_seed) {
    init(level, random_seed, 0x0Fu);
}

uint8_t SimulationEngine::roster_mask() const noexcept {
    return impl_->roster_mask_;
}

void SimulationEngine::init(const ants::assets::LevelData& level_in, uint32_t random_seed, uint8_t roster_mask) {
    roster_mask &= 0x0Fu;
    // A team without a player has no hill and no start markers (so no starting ants): the level is played without them
    ants::assets::LevelData roster_level;
    const bool everybody = roster_mask == 0x0Fu;
    if (!everybody) roster_level = level_in.for_roster(roster_mask);
    const ants::assets::LevelData& level = everybody ? level_in : roster_level;
    impl_->roster_mask_ = roster_mask;
    impl_->dropped_mask_ = 0;
    impl_->quitter_ = NO_QUITTER;
    impl_->prng_.srand(random_seed);
    impl_->cosmetic_prng_.srand(random_seed ^ 0x5EEDu);
    impl_->active_effects_.clear();
    impl_->battle_clouds_.clear();
    impl_->score_bubbles_.clear();
    impl_->grid_.init_from_level(level);
    impl_->stats_.reset();
    impl_->last_attacked_ms_.fill(-1000000);          // player +0x4c: no team has been attacked yet
    uint32_t match_minutes = (level.default_minutes > 0) ? level.default_minutes : 12;
    impl_->set_match_clock(static_cast<int64_t>(match_minutes) * 60 * 1000);
    impl_->match_limit_ms_ = match_minutes * 60 * 1000;
    impl_->checkgo_stage_ = 0;
    impl_->checkgo_threshold_ms_ = 61000;
    impl_->match_state_ = MatchState::Running;
    impl_->current_tick_ = 0;
    impl_->ants_.clear();
    impl_->audio_queue_.clear();
    impl_->news_queue_.clear();
    impl_->next_ant_id_ = 1;
    impl_->world_state_dirty_ = true;
    impl_->movement_reset();

    impl_->flower_droppers_.clear();
    // The flower dropper task (FDTASK, Ants.exe 0x100fc0d): it walks the plants of Block 1 (the world objects of 0x100e3b0 - 0x100e436), and a plant whose TILE ID has the plant
    // flag 0x10 (FUN_01007227 at 0x100fc78: the clovers 404 .. 408, the flowers 410 .. 416, 420, 421, whatever the dictionary calls them) looks up the FIRST Block 4 record at its
    // cell (FUN_01008d79: the record's row and column are the plant's) and drops when that record's flag dword is not 0 (0x100fcc5). A record that two plants share is one dropper:
    // the stamp (+0x18) is the record's, so the plant that comes first in the list posts and renews it before the second one is due.
    std::vector<bool> record_taken(level.waypoints.size(), false);
    for (const auto& sp : level.anthill_spawns) {
        if (!movement::is_dropper_plant_tile(sp.tile_id)) continue;
        for (size_t w = 0; w < level.waypoints.size(); ++w) {
            const auto& wp = level.waypoints[w];
            if (wp.x != sp.x || wp.y != sp.y) continue;
            if (wp.flag != 0 && !record_taken[w]) {
                record_taken[w] = true;
                SimulationEngineImpl::FlowerDropper fd;
                fd.pos = TileCoord{static_cast<int32_t>(wp.x), static_cast<int32_t>(wp.y)};
                // The drop tile: the object's tile plus the offset of its id (table 0x1001af8, FUN_01008d2f): the flowers 410 .. 416, 420 and 421
                // drop one row below the stem, the clovers 404 .. 408 on the tile itself
                const bool one_row_down = (sp.tile_id >= 410 && sp.tile_id <= 416) || sp.tile_id == 420 || sp.tile_id == 421;
                fd.drop_pos = TileCoord{fd.pos.x, fd.pos.y + (one_row_down ? 1 : 0)};
                fd.interval_s = wp.param;                           // +0x10, in seconds (a record without one does not drop: +0xc is tested, the interval is not defaulted)
                fd.stamped = false;
                fd.is_dropping = false;
                fd.powerup_type = 0;
                fd.probabilities = wp.probabilities;
                impl_->flower_droppers_.push_back(fd);
            }
            break;                                                  // the first record at the cell is the one the lookup returns, its flag decides
        }
    }
    // The droppers are exactly the plants of Block 1 whose cell holds a Block 4 record with a trigger (above), the obstacle cells are those of Grid::init_from_level.
    // The original has no further source: a map without such a pair has no dropper, whatever its size (the remake once added the droppers of SMALL.LVL to every
    // 40 x 40 map without one; SMALL.LVL itself never ran that block, its droppers come from its waypoints).

    uint32_t starting_eggs = level.boundary_param;                          // the level's last word, as the original copies it to every team (+0x4a, FUN_0100dc94)
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        impl_->stats_.set_egg_count(p, (roster_mask & (1u << p)) != 0 ? starting_eggs : 0u);
    }

    for (const auto& a : level.anthill_spawns) {
        if (a.team_id < MAX_PLAYERS) {
            int32_t sx = a.x;
            int32_t sy = a.y;
            // A start marker outside the grid has no behaviour to copy: the original (0x100ef18 -> 0x100f17f) indexes its row table with the record's row and
            // column without a check. LevelData::validate refuses such a map for a roster that contains the team; an engine that is handed it anyway (a tool, a test)
            // places no ant for the marker rather than one outside the map (whose fixed-point position would overflow, and whose occupancy cell does not exist).
            if (!impl_->grid_.in_bounds(sx, sy)) continue;
            if (!impl_->grid_.get_cell(TileCoord{sx, sy}).is_passable()) {
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
            spawn_unit(a.team_id, AntType::Worker, TileCoord{static_cast<uint16_t>(sx), static_cast<uint16_t>(sy)}, true);
        }
    }

    impl_->fog_revealed_.assign(static_cast<size_t>(impl_->grid_.width() * impl_->grid_.height()), 0);
    if (impl_->fog_of_war_enabled_) {
        impl_->update_fog_of_war();
    }
}

void SimulationEngine::init_test_world(uint32_t width, uint32_t height, uint32_t random_seed, uint32_t match_time_ms) {
    impl_->roster_mask_ = 0x0Fu;
    impl_->dropped_mask_ = 0;
    impl_->quitter_ = NO_QUITTER;
    impl_->prng_.srand(random_seed);
    impl_->cosmetic_prng_.srand(random_seed ^ 0x5EEDu);
    impl_->active_effects_.clear();
    impl_->battle_clouds_.clear();
    impl_->score_bubbles_.clear();
    impl_->grid_.init_empty(width, height);
    impl_->stats_.reset();
    impl_->last_attacked_ms_.fill(-1000000);          // player +0x4c: no team has been attacked yet
    impl_->set_match_clock(static_cast<int64_t>(match_time_ms));
    impl_->match_limit_ms_ = match_time_ms;
    impl_->checkgo_stage_ = 0;
    impl_->checkgo_threshold_ms_ = 61000;
    impl_->match_state_ = MatchState::Running;
    impl_->current_tick_ = 0;
    impl_->ants_.clear();
    impl_->audio_queue_.clear();
    impl_->news_queue_.clear();
    impl_->next_ant_id_ = 1;
    impl_->world_state_dirty_ = true;
    impl_->movement_reset();

    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        impl_->stats_.set_egg_count(p, 10);
    }

    impl_->fog_revealed_.assign(static_cast<size_t>(impl_->grid_.width() * impl_->grid_.height()), 0);
    if (impl_->fog_of_war_enabled_) {
        impl_->update_fog_of_war();
    }
}


void SimulationEngine::tick() {
    if (impl_->match_state_ == MatchState::GameOver) {
        return;
    }

    // 1. CHECKGO (Ants.exe 0x1024839, period 200 ms, first run at once): the time warnings and the end of the match
    if (impl_->match_clock_ms_ <= impl_->checkgo_next_ms_) {
        impl_->checkgo_poll();
        impl_->checkgo_next_ms_ = impl_->match_clock_ms_ - 200;
        if (impl_->match_state_ == MatchState::GameOver) return;
    }

    // 2. The match clock runs on (GetClock: limit - time played); it may go below 0 until CHECKGO notices
    impl_->set_match_clock_running(impl_->match_clock_ms_ - 50);
    impl_->current_tick_++;
    impl_->world_state_dirty_ = true;

    // Step Active Visual Effects (e.g. bomb explosion)
    for (auto it = impl_->active_effects_.begin(); it != impl_->active_effects_.end();) {
        it->elapsed_ms += 50;
        it->frame = static_cast<uint16_t>(it->elapsed_ms / 50);
        const bool finished = (it->duration_ms > 0) ? (it->elapsed_ms >= it->duration_ms)
                                                     : (it->frame >= it->total_frames);
        if (finished) {
            impl_->stop_audio_owner(it->audio_owner);                     // the effect sprite is removed: its tracked sounds stop (Map::RemoveSprite)
            it = impl_->active_effects_.erase(it);
        } else {
            ++it;
        }
    }

    // Step score bubbles (20 steps of 20 ms, gone after 400 ms)
    for (auto it = impl_->score_bubbles_.begin(); it != impl_->score_bubbles_.end();) {
        it->elapsed_ms += 50;
        if (it->elapsed_ms >= 400) it = impl_->score_bubbles_.erase(it); else ++it;
    }

    // 2. Step Structure Timers (Firewall burnout & Bridge collapse)
    for (uint32_t y = 0; y < impl_->grid_.height(); ++y) {
        for (uint32_t x = 0; x < impl_->grid_.width(); ++x) {
            auto& cell = impl_->grid_.get_cell_mut(x, y);
            if (cell.has_fire() && cell.timer_ticks > 0) {
                cell.timer_ticks--;
                if (cell.timer_ticks == 0) {
                    impl_->grid_.clear_firewall(x, y);
                    impl_->audio_queue_.push_back(AudioEvent{SoundID::FireBurnout, static_cast<int32_t>(x * 32 + 16), static_cast<int32_t>(y * 32 + 16), 1, 255});
                    // Fire wall lifetime task (Ants.exe 0x1024de7): smoke puff at the tile top-left
                    impl_->spawn_tile_effect("sputter", static_cast<int32_t>(x), static_cast<int32_t>(y), effect_spec::kSputterMs);
                }
            } else if (cell.has_any_bridge() && cell.timer_ticks > 0) {
                cell.timer_ticks--;
                // BridgeTimeout (0x1024e66) acts only when the tile is the completed bridge 0x25 at that moment; the task is used up either way
                if (cell.timer_ticks == 0 && cell.interactive_id == TILE_BRIDGE4) {
                    impl_->grid_.collapse_bridge(x, y);
                    // Bridge lifetime task (Ants.exe 0x1024e66): DestroyBridgeAt first (0x100f8bf: the drowning scan, a swimmer's dsplash, 0x100f983) ...
                    impl_->bridge_gone_scan(TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)});
                    // ... then the bsputter puff at the tile top-left (0x1024eb2), so it is drawn over the splash
                    impl_->spawn_tile_effect("bsputter", static_cast<int32_t>(x), static_cast<int32_t>(y), effect_spec::kBsputterMs);
                }
            }
        }
    }

    // 3.5 Step Water State & Swimmer Ant Idle Animation (Snorkel Bobbing & Movement)
    for (auto& ant_ptr : impl_->ants_) {
        if (!ant_ptr || !ant_ptr->is_alive()) continue;
        if (impl_->grid_.in_bounds(ant_ptr->pos)) {
            const auto& cell = impl_->grid_.get_cell(ant_ptr->pos);
            if (cell.terrain_type == TERRAIN_WATER && !cell.has_any_bridge()) {
                if (impl_->type_of(*ant_ptr) == AntType::Swimmer) {
                    ant_ptr->in_water = true;
                    if (ant_ptr->state == UnitState::Idle) {
                        ant_ptr->state = UnitState::Swimming;
                    }
                }
            }
        }
        if (impl_->type_of(*ant_ptr) == AntType::Swimmer && ant_ptr->state == UnitState::Swimming) {
            ant_ptr->anim_tick++;
            ant_ptr->anim_subitem = ant_ptr->anim_tick;
        }
    }

    // 4.9 Original locomotion (Ants.exe): animation-driven walking, tile blocking with ANTPAUSE waits and
    // the PATHMGR path task, processed in exact millisecond order across all ants.
    impl_->movement_tick();
    impl_->tick_battle_clouds();

    // 5. Step unit timers (food is harvested by the ants' own action 5 clip, see harvest_system in action_system.cpp)
    for (auto& ant_ptr : impl_->ants_) {
        if (!ant_ptr || ant_ptr->removed) continue;
        ant_ptr->tick_timers();

    }

    // 7. The flower droppers (FDTASK poll, drop effect cue and landing) run in movement_tick() on the original's own clock.

    if (impl_->fog_of_war_enabled_) {
        impl_->update_fog_of_war();
    }

    // Score changes of this tick become floating bubbles at the players' home tiles (hill exit tile, the tile where
    // hatched ants appear); every change is shown, for every player (Ants.exe FUN_01010cc9).
    for (const ScoreChange& change : impl_->stats_.take_score_changes()) {
        const auto* hill = impl_->grid_.find_anthill(change.player);
        if (!hill) continue;
        ScoreBubble b;
        b.x = (static_cast<int32_t>(hill->x) + 1) * 32;
        b.y = (static_cast<int32_t>(hill->y) + 1) * 32;
        b.amount = change.delta;
        impl_->score_bubbles_.push_back(b);
        // scoreup / scoredn are shared cues heard by every player (Ants.exe FUN_01010560)
        impl_->audio_queue_.push_back(AudioEvent{change.delta > 0 ? SoundID::BaseScoreUp : SoundID::BaseScoreDn,
                                                 b.x, b.y, 1, 255});
    }
    impl_->world_state_dirty_ = true;
}

void SimulationEngineImpl::set_match_clock(int64_t ms) {
    match_clock_ms_ = ms;
    match_time_remaining_ms_ = static_cast<uint32_t>(std::max<int64_t>(0, ms));
    checkgo_next_ms_ = ms;
}

void SimulationEngineImpl::set_match_clock_running(int64_t ms) {
    match_clock_ms_ = ms;
    match_time_remaining_ms_ = static_cast<uint32_t>(std::max<int64_t>(0, ms));
}

// CHECKGO (vtable 0x1004e38, Run 0x1024839): when the clock is below the task's threshold (unsigned compare: a negative clock is
// never below it) the next warning is given: stage 0 the one-minute cue and text 49 (threshold becomes 31000), stage 1 the
// thirty-second cue and text 50 (11000), then eleven countdown steps (cue and text 59, the threshold falls by 1000 each time:
// clock 10800, 9800 ... 800 on the 200 ms grid). Every text is posted with the flash flag. The match ends at the first run that
// finds the clock below 0. (The elimination rules of the same task belong to the network stage.)
void SimulationEngineImpl::checkgo_poll() {
    const int64_t remaining = match_clock_ms_;
    if (remaining >= 0 && remaining < static_cast<int64_t>(checkgo_threshold_ms_)) {
        if (checkgo_stage_ == 0) {
            audio_queue_.push_back(AudioEvent{SoundID::OneMinute, 0, 0, 1, 255});
            post_news(255, strings::kOneMinute);
            checkgo_threshold_ms_ = 31000;
        } else if (checkgo_stage_ == 1) {
            audio_queue_.push_back(AudioEvent{SoundID::ThirtySeconds, 0, 0, 1, 255});
            post_news(255, strings::kThirtySeconds);
            checkgo_threshold_ms_ = 11000;
        } else {
            audio_queue_.push_back(AudioEvent{SoundID::Countdown, 0, 0, 1, 255});
            post_news(255, strings::kTenSeconds);
            checkgo_threshold_ms_ = checkgo_threshold_ms_ >= 1000 ? checkgo_threshold_ms_ - 1000 : 0;
        }
        ++checkgo_stage_;
    }
    // The end: the clock is below 0 (the wrapped unsigned clock is above the limit), or the elimination rules hold for some team (0x10248de - 0x1024a8f)
    if (remaining < 0 || checkgo_end_rules()) handle_game_over();
}

bool SimulationEngineImpl::team_alive(uint8_t team) const {
    if (team >= MAX_PLAYERS) return false;
    if (stats_.get_egg_count(team) > 0 || hatch_[team].active) return true;
    for (const auto& a : ants_) {
        if (a != nullptr && !a->removed && a->player_id == team) return true;
    }
    return false;
}

bool SimulationEngineImpl::checkgo_ends_for(uint8_t local) const {
    bool any_alive = false;
    bool all_allied = true;
    uint32_t present = 0;
    for (uint8_t k = 0; k < MAX_PLAYERS; ++k) {
        if ((roster_mask_ & (1u << k)) == 0) continue;              // a team without a player is NULL in the table
        ++present;
        if (team_dropped(k) || !team_alive(k)) continue;
        any_alive = true;
        if (k != local && stats_.get_alliance(local) != k) all_allied = false;
    }
    if (!any_alive) return true;                                     // nobody has anything left
    if (!all_allied || present <= 1) return false;                   // a live enemy, or the only team of the match (no score to compare)
    // The best combined score over the teams that are not the local team's ally: equal scores keep the old best unless it is the local team,
    // so a tie for the top is never a win for the local team; a score of 0 never becomes the best
    int32_t best_score = 0;
    int32_t best = -1;
    for (uint8_t j = 0; j < MAX_PLAYERS; ++j) {
        if ((roster_mask_ & (1u << j)) == 0 || stats_.get_alliance(local) == j) continue;
        int32_t combined = stats_.get_individual_score(j);
        const uint8_t ally = stats_.get_alliance(j);
        if (ally != ALLIANCE_NONE && ally < MAX_PLAYERS) combined += stats_.get_individual_score(ally);
        if (combined > best_score || (combined == best_score && best == static_cast<int32_t>(local))) {
            best_score = combined;
            best = j;
        }
    }
    return best == static_cast<int32_t>(local);
}

bool SimulationEngineImpl::checkgo_end_rules() const {
    for (uint8_t local = 0; local < MAX_PLAYERS; ++local) {
        if ((roster_mask_ & (1u << local)) == 0 || team_dropped(local)) continue;     // a dropped team's machine is gone
        if (checkgo_ends_for(local)) return true;
    }
    return false;
}

bool SimulationEngineImpl::drop_leaves_winner() const {
    for (uint8_t local = 0; local < MAX_PLAYERS; ++local) {
        if ((roster_mask_ & (1u << local)) == 0 || team_dropped(local)) continue;
        bool enemy = false;
        for (uint8_t k = 0; k < MAX_PLAYERS && !enemy; ++k) {
            if ((roster_mask_ & (1u << k)) == 0 || team_dropped(k) || k == local) continue;
            enemy = stats_.get_alliance(local) != k;
        }
        if (!enemy) return true;
    }
    return false;
}

uint32_t SimulationEngineImpl::other_sides(uint8_t team) const {
    uint32_t count = 0;
    for (uint8_t si = 0; si < MAX_PLAYERS; ++si) {
        if ((roster_mask_ & (1u << si)) == 0 || team_dropped(si) || si == team) continue;
        const uint8_t ally = stats_.get_alliance(si);
        if (ally == ALLIANCE_NONE || ally == team || ally < si) ++count;
    }
    return count;
}

MatchResult SimulationEngineImpl::make_match_result() const {
    return stats_.evaluate_victory(static_cast<uint8_t>(roster_mask_ & ~dropped_mask_ & 0x0Fu), quitter_);
}

bool SimulationEngine::issue_order(const AntOrder& order) {
    AntUnit* unit = impl_->find_unit(order.ant_id);
    if (!unit || !unit->is_alive() || unit->engaged || unit->frozen) return false;
    {
        // Accept predicate of player orders (FUN_0101ff5a): idle, walking or stunned ants only.
        const uint8_t act = impl_->orig_action_of(*unit);
        if (order.type != OrderType::Cancel && !(act == 0 || act == 1 || act == 3)) return false;
    }

    // Active action states (placing fire, planting bomb, building bridge, extinguishing, defusing, picking up a power-up)
    // strictly and silently disallow all incoming orders (Ants.exe 0x101ff5a / Ants.exe.c line 22938 -> LAB_0101fef5).
    // The unit finishes its action uninterrupted without playing CantGo.
    if (unit->state == UnitState::PoweringUp ||
        unit->state == UnitState::PlacingFire || unit->state == UnitState::ExtinguishingFire ||
        unit->state == UnitState::PlantingBomb || unit->state == UnitState::DefusingBomb ||
        unit->state == UnitState::BuildingBridge || unit->state == UnitState::DemolishingBridge) {
        return false;
    }

    // Player move orders pass the original accept predicate (FUN_0101ff5a) before anything else: an ant
    // that is attacking, harvesting, entering the hill or playing its "can't go" animation ignores them.
    if (order.type == OrderType::Move || order.type == OrderType::InfiltrateAnthill) {
        if (!impl_->can_take_user_order(*unit)) return false;
    }

    if (order.type != OrderType::ReturnToBase) {
        leave_base_queue(order.ant_id);
    }

    switch (order.type) {
        case OrderType::Move:
            unit->ability_target = TileCoord{-1, -1};
            return route_move_order(*impl_, *unit, TileCoord{order.target_x, order.target_y}, true, order.special);
        case OrderType::ReturnToBase: {
            unit->ability_target = TileCoord{-1, -1};
            join_base_queue(order.ant_id);
            return true;
        }
        case OrderType::Attack:
            unit->ability_target = TileCoord{-1, -1};
            {
                // The armed attack mode clicks a tile, not an ant: the occupant of the tile is the target (FUN_01020655 does
                // the same for every click on a tile that another team's ant stands on).
                AntUnit* target = (order.target_entity_id >= 0)
                    ? impl_->find_unit(static_cast<uint32_t>(order.target_entity_id))
                    : impl_->occupant_at(TileCoord{order.target_x, order.target_y});
                if (target && !target->removed) {
                    if (target->player_id == unit->player_id || impl_->stats_.are_allies(unit->player_id, target->player_id)) {
                        return false; // Ants cannot attack friendly teammates or allies (the original asks for a confirmation)
                    }
                    // FUN_010287b5: a click on an enemy ant is a move order onto its tile with the player flag; the
                    // classification (FUN_01020655) turns it into the attack order 3 and the path ends in contact.
                    return route_move_order(*impl_, *unit, TileCoord{target->pixel_x / 32, target->pixel_y / 32}, true);
                }
            }
            return false;
        case OrderType::PlantBomb:
        case OrderType::DefuseBomb:
        case OrderType::IgniteFire:
        case OrderType::ExtinguishFire:
        case OrderType::BuildBridge:
        case OrderType::DemolishBridge: {
            // FUN_010287b5 with the special flag: the classification of the ant's type (plant / defuse, ignite / extinguish,
            // bridge build / demolish) and the neighbour tile it works from are decided by GoTo (FUN_0101fc50)
            return impl_->go_to(*unit, TileCoord{order.target_x, order.target_y}, true, true);
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
                        if (unit->type == AntType::Thief) {                  // the ant's OWN type, as GoTo reads it (0x101fcb7)
                            tx = ah->x + 3;
                            ty = ah->y + 2;
                        } else {
                            tx = ah->x + 1;
                            ty = ah->y + 1;
                        }
                        break;
                    }
                }
            } else if (unit->type == AntType::Thief && target_team < MAX_PLAYERS) {     // (0x101fcb7)
                const auto* ah = impl_->grid_.find_anthill(target_team);
                if (ah) {
                    tx = ah->x + 3;
                    ty = ah->y + 2;
                }
            }
            unit->target_team_id = target_team;
            const auto* target_ah = impl_->grid_.find_anthill(target_team);
            bool at_enemy_base = (target_ah && unit->pos.x == target_ah->x + 3 && unit->pos.y == target_ah->y + 2);
            if (at_enemy_base || (unit->pos.x == tx && unit->pos.y == ty)) {
                start_thief_infiltration(order.ant_id, target_team);
            } else {
                issue_internal_move_order(order.ant_id, TileCoord{tx, ty});
            }
            return true;
        }
        case OrderType::Cancel:
            unit->ability_target = TileCoord{-1, -1};
            unit->clear_path();
            if (impl_->type_of(*unit) == AntType::Swimmer && unit->in_water) {
                unit->state = UnitState::Swimming;
            } else {
                unit->state = (impl_->type_of(*unit) == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
            }
            return true;
        default:
            return false;
    }
}

// Body of the Stop button loop (FUN_01028a60) for one selected ant. The ant must accept player orders (FUN_0101ff5a) and
// not stand on the hill entrance or the tile above it; its +0x68 is cleared; an ant without a path is skipped while it is
// idle or in one of the actions 3..9 and 0xb; only an ant that has a target tile is sent to its own tile with GoTo
// (FUN_0101fc50, player flag 0).
bool SimulationEngine::stop_ant(uint32_t ant_id) {
    AntUnit* a = impl_->find_unit(ant_id);
    if (!a || !a->is_alive() || !impl_->can_take_user_order(*a)) return false;
    const TileCoord t{a->pixel_x / 32, a->pixel_y / 32};
    const TileCoord entrance = impl_->team_entrance(a->player_id);
    if (entrance.x >= 0 && (t == entrance || t == TileCoord{entrance.x, entrance.y - 1})) return false;
    a->home_state = 0;
    if (a->waypoints.empty()) {
        const uint8_t act = impl_->orig_action_of(*a);
        if (act == 0 || (act > 2 && act <= 9) || act == 0x0B) return false;
    }
    if (!a->has_order_tile()) return false;
    return impl_->go_to(*a, t, false, false);
}

// FUN_01010aca: the hatch pedestal. The checks come in the original's order; an accepted click costs
// min(score, 200), uses one egg and starts an 8000 ms incubation during which no ant exists (only one at a time).
SimulationEngine::HatchResult SimulationEngineImpl::hatch_request(uint8_t player_id, AntType type, bool force) {
    using HatchResult = SimulationEngine::HatchResult;
    if (match_state_ == MatchState::GameOver || match_time_remaining_ms_ == 0 || player_id >= MAX_PLAYERS ||
        (roster_mask_ & (1u << player_id)) == 0 || (dropped_mask_ & (1u << player_id)) != 0) {
        return HatchResult::NotAvailable;
    }
    if (stats_.get_egg_count(player_id) < 1) {
        post_news(player_id, strings::kNoEggs);
        return HatchResult::NoEggs;
    }
    if (hatch_[player_id].active) {
        post_news(player_id, strings::kAlreadyHatching);
        return HatchResult::AlreadyHatching;
    }
    const int32_t score = stats_.get_individual_score(player_id);
    if (!force && score < static_cast<int32_t>(HATCH_COST_POINTS)) {
        post_news(player_id, strings::kNeed200Points);
        // canthatch (61, 0x1010b97) is a global cue of the clicking machine: plain volume, heard wherever the view is (not positional)
        audio_queue_.push_back(AudioEvent{SoundID::AntStop, 0, 0, 1, player_id});
        return HatchResult::NotEnoughPoints;
    }
    post_news(player_id, strings::kHatching);
    const int32_t cost = std::min<int32_t>(static_cast<int32_t>(HATCH_COST_POINTS), std::max<int32_t>(0, score));
    add_score(player_id, -cost);                                                        // "-N" bubble and scoredn
    stats_.set_egg_count(player_id, stats_.get_egg_count(player_id) - 1);
    stats_.get_player_stats_mut(player_id).ants_hatched++;
    stats_.get_player_stats_mut(player_id).new_hatched++;
    auto& h = hatch_[player_id];
    h.active = true;
    h.type = type;
    h.due_ms = anim_clock_ms_ + hatch_delay_ticks_ * 50u;
    world_state_dirty_ = true;
    return HatchResult::Started;
}

SimulationEngine::HatchResult SimulationEngine::try_hatch(uint8_t player_id, AntType type, bool force) {
    return impl_->hatch_request(player_id, type, force);
}

bool SimulationEngine::hatch_ant(uint8_t player_id, AntType type) {
    return try_hatch(player_id, type, false) == HatchResult::Started;
}

size_t SimulationEngine::get_pending_hatch_count(uint8_t player_id) const {
    return (player_id < MAX_PLAYERS && impl_->hatch_[player_id].active) ? 1u : 0u;
}

void SimulationEngine::set_hatch_delay_ticks(uint32_t ticks) {
    impl_->hatch_delay_ticks_ = ticks;
}

std::string SimulationEngineImpl::player_colour_name(uint8_t p) const {
    return strings::colour_name(static_cast<uint8_t>(3u - (p & 3u)));
}

std::string SimulationEngineImpl::player_display_name(uint8_t p) const {
    if (p < MAX_PLAYERS && !player_names_[p].empty()) return player_names_[p];
    return player_colour_name(p);
}

void SimulationEngine::set_player_name(uint8_t player_id, const std::string& name) {
    if (player_id < MAX_PLAYERS) impl_->player_names_[player_id] = name;
}

std::string SimulationEngine::get_player_name(uint8_t player_id) const {
    return player_id < MAX_PLAYERS ? impl_->player_display_name(player_id) : std::string();
}

// The invitation reaches the invitee as a modal question (strings 1 / 2) with the allypro cue (51); the proposer waits (string 3).
void SimulationEngine::propose_alliance(uint8_t from_player, uint8_t to_player) {
    if (from_player >= MAX_PLAYERS || to_player >= MAX_PLAYERS || from_player == to_player) return;
    impl_->stats_.set_pending_invite(to_player, from_player, static_cast<uint32_t>(impl_->current_tick_ + 200));
    impl_->world_state_dirty_ = true;
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AlliancePro, 0, 0, 1, to_player});
    const uint8_t old_ally = impl_->stats_.get_alliance(to_player);
    NewsEvent ev;
    ev.target_player = to_player;
    ev.channel = NewsChannel::Dialog;
    ev.timestamp_ms = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(impl_->match_limit_ms_) - impl_->match_clock_ms_));
    if (old_ally < MAX_PLAYERS && old_ally != to_player) {
        ev.string_id = strings::kInviteBreakDialog;
        ev.message_text = strings::format(ev.string_id, impl_->player_display_name(from_player), impl_->player_colour_name(from_player),
                                          impl_->player_display_name(old_ally), impl_->player_colour_name(old_ally));
    } else {
        ev.string_id = strings::kInviteDialog;
        ev.message_text = strings::format(ev.string_id, impl_->player_display_name(from_player), impl_->player_colour_name(from_player));
    }
    impl_->news_queue_.push_back(std::move(ev));
}

void SimulationEngine::withdraw_alliance_offer(uint8_t from_player, uint8_t to_player) {
    if (from_player >= MAX_PLAYERS || to_player >= MAX_PLAYERS) return;
    const auto& invite = impl_->stats_.get_pending_invite(to_player);
    if (!invite.active || invite.from_player != from_player) return;
    impl_->stats_.clear_pending_invite(to_player);
    impl_->world_state_dirty_ = true;
    impl_->post_news(to_player, strings::kTeamWithdrawn, impl_->player_display_name(from_player));       // FUN_0100c5fa, 0x100c73a
}

// FUN_0100c36b (the proposer hears the answer: 81 "%s accepted teaming up") and the team message 0x1d kind 1 (FUN_01023c87, every
// client): the allyon cue, the News Flash of string 39 (proposer first) and the blinking status 75. The allyyes cue is the
// answering player's own click sound.
void SimulationEngine::accept_alliance(uint8_t responding_player, uint8_t proposing_player) {
    if (responding_player >= MAX_PLAYERS || proposing_player >= MAX_PLAYERS) return;
    impl_->stats_.set_alliance(responding_player, proposing_player);
    impl_->stats_.clear_pending_invite(responding_player);
    impl_->world_state_dirty_ = true;

    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceYes, 0, 0, 1, responding_player});
    impl_->post_news(proposing_player, strings::kTeamAccepted, impl_->player_display_name(responding_player));
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceOn, 0, 0, 1, 255});
    impl_->post_news_flash(strings::kTeamNow, impl_->player_display_name(proposing_player), impl_->player_colour_name(proposing_player),
                           impl_->player_display_name(responding_player), impl_->player_colour_name(responding_player));
    impl_->post_news(255, strings::kTeamMade);
}

// FUN_0100c36b, the refusal: the proposer reads 80 "%s rejected teaming up" and hears allynot (0x100c4bc); the decliner hears it as well (the answer
// message 0x1c is executed on its machine too, 0x1023c53).
void SimulationEngine::deny_alliance(uint8_t responding_player, uint8_t proposing_player) {
    if (responding_player >= MAX_PLAYERS || proposing_player >= MAX_PLAYERS) return;
    impl_->stats_.clear_pending_invite(responding_player);
    impl_->world_state_dirty_ = true;
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceNot, 0, 0, 1, proposing_player});
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceNot, 0, 0, 1, responding_player});           // 0x1023c53: the decliner's own machine plays the same cue
    impl_->post_news(proposing_player, strings::kTeamRejected, impl_->player_display_name(responding_player));
}

// Team message 0x1d kind 2 (FUN_01023c87, every client): the allyoff cue always plays, the News Flash of string 40 (the breaker and
// its old ally) only when the breaker had an ally.
void SimulationEngine::break_alliance(uint8_t player_id) {
    if (player_id >= MAX_PLAYERS) return;
    const uint8_t old_ally = impl_->stats_.get_alliance(player_id);
    impl_->stats_.break_alliance(player_id);
    impl_->world_state_dirty_ = true;
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceBreak, 0, 0, 1, 255});
    if (old_ally < MAX_PLAYERS && old_ally != player_id) {
        impl_->post_news_flash(strings::kTeamNoMore, impl_->player_display_name(player_id), impl_->player_colour_name(player_id),
                               impl_->player_display_name(old_ally), impl_->player_colour_name(old_ally));
    }
}

void SimulationEngine::break_alliance(uint8_t p1, uint8_t p2) {
    if (p1 >= MAX_PLAYERS || p2 >= MAX_PLAYERS) return;
    const bool were_allied = impl_->stats_.are_allies(p1, p2) && p1 != p2;
    impl_->stats_.break_alliance(p1, p2);
    impl_->world_state_dirty_ = true;
    impl_->audio_queue_.push_back(AudioEvent{SoundID::AllianceBreak, 0, 0, 1, 255});
    if (were_allied) {
        impl_->post_news_flash(strings::kTeamNoMore, impl_->player_display_name(p1), impl_->player_colour_name(p1),
                               impl_->player_display_name(p2), impl_->player_colour_name(p2));
    }
}

void SimulationEngine::form_alliance(uint8_t p1, uint8_t p2) {
    if (p1 < MAX_PLAYERS && p2 < MAX_PLAYERS) {
        impl_->stats_.set_alliance(p1, p2);
        impl_->world_state_dirty_ = true;
    }
}

const WorldState& SimulationEngine::get_world_state() const {
    if (impl_->world_state_dirty_) {
        impl_->world_state_cache_.match_time_remaining_ms = impl_->match_time_remaining_ms_;
        impl_->world_state_cache_.width = impl_->grid_.width();
        impl_->world_state_cache_.height = impl_->grid_.height();
        impl_->world_state_cache_.cells = impl_->grid_.cells();

        impl_->world_state_cache_.ants.clear();
        for (const auto& a : impl_->ants_) {
            if (!a) continue;
            // A removed ant is gone (RemoveAnt); an ant without hit points still flies, drowns or dies on its clip
            if (a->removed) continue;
            AntSnapshot s{};
            s.id = a->id;
            s.player_id = a->player_id;
            s.type = impl_->type_of(*a);                      // what the ant IS (the getter): the sprite, the voice, the panel, the pedestal, the selection's type
            s.raw_type = a->type;                              // the ant's own type field (+0x54): the pick box of a combat ant is read from it (0x1026a3d)
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
            s.carried_points = a->carried_signed();
            s.is_stunned = a->is_stunned();
            s.is_swimming = (a->state == UnitState::Swimming || a->in_water);
            s.is_drowning = (a->state == UnitState::Drowning);
            s.is_on_mud = a->is_on_mud;
            s.state = a->state;
            s.frozen = a->frozen;
            if (a->burn_end_ms != 0) {                       // the dud burn overlay (?bu) that covers the frozen ant
                const uint32_t total = movement::action_clip(movement::ActionClip::Burn, static_cast<uint8_t>(impl_->type_of(*a)), 0, false).total_duration_ms();
                const uint32_t remaining = (a->burn_end_ms > impl_->anim_clock_ms_) ? a->burn_end_ms - impl_->anim_clock_ms_ : 0u;
                s.burn_elapsed_ms = static_cast<int32_t>(total - std::min(total, remaining));
            }
            s.target_team_id = a->target_team_id;
            if (a->loco_action != AntUnit::kActionNone && a->loco.clip.valid()) {
                s.loco_clip = a->loco.clip.chd_index;
                s.loco_frame = (a->loco.cursor > 0) ? static_cast<uint16_t>(a->loco.cursor - 1) : 0;
                s.loco_mirrored = a->loco.clip.mirrored;
                s.loco_left_ms = (a->loco.next_ms > impl_->anim_clock_ms_)
                    ? static_cast<uint16_t>(std::min<uint32_t>(a->loco.next_ms - impl_->anim_clock_ms_, 0xFFFFu)) : uint16_t{0};
            }
            impl_->world_state_cache_.ants.push_back(s);
        }

        impl_->world_state_cache_.effects = impl_->active_effects_;
        for (const auto& bc : impl_->battle_clouds_) {          // the dust balls of foreign pile-ups (looping "battle" clip)
            VisualEffect e;
            e.anim_name = "battle";
            e.px = bc.tile.x * 32 + 16;
            e.py = bc.tile.y * 32 + 16;
            e.y_key = e.py;
            e.elapsed_ms = impl_->anim_clock_ms_ - bc.created_ms;
            e.fog_gated = true;
            e.looping = true;
            impl_->world_state_cache_.effects.push_back(std::move(e));
        }
        impl_->world_state_cache_.score_bubbles = impl_->score_bubbles_;

        impl_->world_state_cache_.flower_droppers.clear();
        for (const auto& fd : impl_->flower_droppers_) {
            FlowerDropperSnapshot s{};
            s.x = fd.pos.x;
            s.y = fd.pos.y;
            s.drop_x = fd.drop_pos.x;
            s.drop_y = fd.drop_pos.y;
            s.is_dropping = fd.is_dropping;
            s.drop_elapsed_ms = fd.is_dropping ? std::min<uint32_t>(impl_->anim_clock_ms_ - fd.drop_start_ms, effect_spec::kDropperMs) : 0u;
            s.powerup_type = fd.powerup_type;
            impl_->world_state_cache_.flower_droppers.push_back(s);
        }

        for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
            impl_->world_state_cache_.player_stats[i] = impl_->stats_.get_player_stats(i);
            impl_->world_state_cache_.player_scores[i] = impl_->stats_.get_display_score(i);
            impl_->world_state_cache_.player_eggs[i] = impl_->stats_.get_egg_count(i);
            impl_->world_state_cache_.player_alliances[i] = impl_->stats_.get_alliance(i);
            const AllianceInvite& invite = impl_->stats_.get_pending_invite(i);
            impl_->world_state_cache_.pending_invite_from[i] = invite.active && invite.from_player < MAX_PLAYERS ? invite.from_player : uint8_t{255};
        }

        impl_->world_state_cache_.anthills = impl_->grid_.anthills();
        impl_->world_state_cache_.plants = impl_->grid_.plants();
        impl_->world_state_cache_.dropped_mask = impl_->dropped_mask_;
        impl_->world_state_cache_.match_result = impl_->make_match_result();
        impl_->world_state_cache_.fog_of_war_enabled = impl_->fog_of_war_enabled_;
        impl_->world_state_cache_.fog_revealed = impl_->fog_revealed_;
        impl_->world_state_dirty_ = false;
    }
    return impl_->world_state_cache_;
}

uint32_t SimulationEngine::get_match_time_remaining_ms() const {
    return impl_->match_time_remaining_ms_;
}

void SimulationEngine::set_match_time_remaining_ms(uint32_t ms) {
    // Test hook: the clock jumps; CHECKGO looks at it at its next run (the stage and threshold of the warnings stay)
    impl_->set_match_clock(static_cast<int64_t>(ms));
    impl_->checkgo_next_ms_ = impl_->match_clock_ms_;
}

void SimulationEngine::set_fog_of_war_enabled(bool enabled) {
    impl_->fog_of_war_enabled_ = enabled;
    if (enabled) {
        impl_->update_fog_of_war();
    }
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::is_fog_of_war_enabled() const {
    return impl_->fog_of_war_enabled_;
}

void SimulationEngine::set_viewing_player_id(uint8_t player_id) {
    impl_->viewing_player_id_ = player_id;
    for (auto& ant : impl_->ants_) {                       // a new viewer looks at its ants afresh (as a program that starts as that team would)
        if (ant) ant->fog_tile_x = ant->fog_tile_y = -1;
    }
    if (impl_->fog_of_war_enabled_) {
        impl_->update_fog_of_war();
    }
    impl_->world_state_dirty_ = true;
}

// FUN_0100d03b: "%s dropped out of the game!" as a News Flash of the chat log, and playerout.wav unless the game is over.
void SimulationEngine::trigger_player_dropout(uint8_t player_id, const std::string& player_name) {
    if (impl_->match_state_ != MatchState::GameOver) {
        impl_->audio_queue_.push_back(AudioEvent{SoundID::PlayerDropOut, 0, 0, 1, 255});
    }
    const std::string name = !player_name.empty() ? player_name
                             : (player_id < MAX_PLAYERS ? impl_->player_display_name(player_id) : ("Player " + std::to_string(player_id)));
    impl_->post_news_flash(strings::kDroppedOut, name);
}

bool SimulationEngine::is_match_over() const {
    return impl_->match_state_ == MatchState::GameOver;
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

uint32_t SimulationEngine::spawn_unit(uint8_t player_id, AntType type, TileCoord pos, bool level_start) {
    uint32_t id = impl_->next_ant_id_++;
    auto unit = std::make_unique<AntUnit>(id, static_cast<TeamId>(player_id % 4), type, pos.x, pos.y);
    unit->player_id = player_id;
    unit->facing = static_cast<Direction>(level_start ? (impl_->prng_.rand() % 7 + 1) : (impl_->prng_.rand() % 8));
    AntUnit* unit_ptr = unit.get();
    // The ant is born with the own type `type` (the original's ants all start with 0, the constructor 0x101a77a writes it at 0x101a815); it IS the type of the level's
    // default when that is Worker (the getter, 0x100f9cb)
    const AntType is_type = impl_->effective_type(type);
    if (is_type != type && is_type == AntType::Combat) unit_ptr->state = UnitState::GuardIdle;
    if (impl_->grid_.in_bounds(pos)) {
        const auto& cell = impl_->grid_.get_cell(pos);
        if (cell.terrain_type == TERRAIN_WATER && !cell.has_any_bridge()) {
            unit_ptr->in_water = true;
            unit_ptr->was_in_water = true;
            if (is_type == AntType::Swimmer) {
                unit_ptr->state = UnitState::Swimming;
            }
        }
    }
    impl_->ants_.push_back(std::move(unit));
    if (unit_ptr->is_alive()) {
        // Register on the occupancy grid at once (the original registers every ant it places).
        impl_->occ_refresh();
        if (unit_ptr->in_water && is_type != AntType::Swimmer) impl_->drown(*unit_ptr, pos);   // a placed ant that cannot swim
    }
    impl_->world_state_dirty_ = true;
    return id;
}

AntType SimulationEngine::ant_type(const AntUnit& ant) const noexcept {
    return impl_->type_of(ant);
}

AntUnit& SimulationEngine::get_unit(uint32_t ant_id) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u) {
        throw std::runtime_error("Unit not found");
    }
    return *u;
}


// Test hook: the ant is removed at once (Kill 0x1020ff6 without the death clip).
void SimulationEngine::kill_unit(uint32_t ant_id) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u || u->removed) return;
    u->hp = 0;
    impl_->finish_death(*u);
}

// A melee contact between two adjacent ants happens now (TryEnterTile branch A and the msg-8 handler of the original):
// the victim loses 1 hp (a combat ant: 2), the attacker plays its attack clip, and the victim is thrown at the strike
// frame of that clip. Ants of the same team or allies never fight.
void SimulationEngine::execute_melee_attack(uint32_t attacker_id, uint32_t target_id) {
    AntUnit* attacker = impl_->find_unit(attacker_id);
    AntUnit* target = impl_->find_unit(target_id);
    if (!attacker || !target || attacker->removed || target->removed) return;
    if (attacker->player_id == target->player_id || impl_->stats_.are_allies(attacker->player_id, target->player_id)) return;
    // Test hook: the contact itself never looks at the tile. An ant standing on a power-up is immune only because no path
    // can end on the solid tile (FUN_01020951, FUN_0101f780), so no attacker ever makes the step that is the contact.
    impl_->melee_contact(*attacker, *target);
}

void SimulationEngine::issue_move_order(uint32_t ant_id, TileCoord dest) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit) return;
    route_move_order(*impl_, *unit, dest, true);
}

void SimulationEngine::issue_internal_move_order(uint32_t ant_id, TileCoord dest) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit) return;
    route_move_order(*impl_, *unit, dest, false);
}

uint32_t SimulationEngine::issue_group_move_order(const std::vector<uint32_t>& ant_ids, TileCoord target, uint32_t* needed) {
    return group_order(ant_ids, target, false, needed);
}

uint32_t SimulationEngine::issue_group_special_order(const std::vector<uint32_t>& ant_ids, TileCoord target, uint32_t* needed) {
    return group_order(ant_ids, target, true, needed);
}

uint32_t SimulationEngine::group_order(const std::vector<uint32_t>& ant_ids, TileCoord target, bool special, uint32_t* needed) {
    struct Entry {
        uint32_t id;
        uint32_t d;
    };
    std::vector<Entry> e;
    for (uint32_t id : ant_ids) {
        AntUnit* a = impl_->find_unit(id);
        if (!a || !impl_->can_take_user_order(*a)) continue;
        if (impl_->group_click_skips(*a, target, special, false)) continue;           // already carries out this click (0x102881a..0x10288ff)
        const TileCoord at{a->pixel_x / 32, a->pixel_y / 32};
        const int32_t dr = std::abs(at.y - target.y);
        const int32_t dc = std::abs(at.x - target.x);
        e.push_back(Entry{id, static_cast<uint32_t>(std::max(dr, dc)) << 4});   // FUN_01020911
    }
    if (needed != nullptr) *needed = static_cast<uint32_t>(e.size());         // FUN_010287b5 returns 1 when there is an entry, 0 (0x102892f) when every ant skipped
    // Exchange sort by distance, strict '>' swap (0x102893e..0x1028994).
    for (size_t p = 0; p < e.size(); ++p) {
        for (size_t j = p + 1; j < e.size(); ++j) {
            if (e[p].d > e[j].d) std::swap(e[p], e[j]);
        }
    }
    uint32_t ack = 0;
    for (size_t k = 0; k < e.size(); ++k) {
        AntOrder order;
        order.ant_id = e[k].id;
        order.type = OrderType::Move;
        order.target_x = target.x;
        order.target_y = target.y;
        order.special = special;
        const bool accepted = issue_order(order);                          // GoTo (FUN_0101fc50) returned true
        if (k == 0 && accepted) ack = e[k].id;                             // acknowledgement: closest ant only (0x10289b7 .. 0x10289c0)
    }
    return ack;
}

// FUN_01026f91 (the cursor's "special target" test), by the homogeneous type of the selection (FUN_010282e0):
//  bomber  - a bomb tile (FUN_0101d7f9) for both flags; ground that takes a bomb (FUN_0101d762 with the strict occupancy rule) with the
//            pedestal latched only;
//  fire    - the pedestal latched only: a fire wall (layer 2 id 0x86) or plantable ground;
//  thief   - any hill of another colour, both flags;
//  swimmer - the pedestal latched only: a completed bridge (0x25) or water that takes a bridge (FUN_0101d6d6);
//  worker, combat ant and mixed selections never.
bool SimulationEngine::is_special_target_valid(AntType type, TileCoord tile, bool auto_flag, uint8_t own_team) const {
    SimulationEngineImpl& impl = *impl_;
    if (!impl.grid_.in_bounds(tile)) return false;
    switch (type) {
        case AntType::Bomber:
            if (impl.valid_bomb(tile)) return true;
            return !auto_flag && impl.valid_ground(tile, false);
        case AntType::Fire:
            if (auto_flag) return false;
            return impl.grid_.get_cell(tile).has_fire() || impl.valid_ground(tile, false);
        case AntType::Swimmer:
            if (auto_flag) return false;
            return impl.grid_.get_cell(tile).has_completed_bridge() || impl.valid_water(tile, false);
        case AntType::Thief:
            for (const auto& ah : impl.grid_.anthills()) {
                if (tile.x >= static_cast<int32_t>(ah.x) && tile.x <= static_cast<int32_t>(ah.x) + 3 &&
                    tile.y >= static_cast<int32_t>(ah.y) && tile.y <= static_cast<int32_t>(ah.y) + 3) {
                    return ah.team_id != own_team;
                }
            }
            return false;
        default:
            return false;
    }
}

// FUN_010287b5 with the attack flag: the skip rule is "already order 3 with +0xac == the clicked tile" (0x1028820, then 0x102887e);
// orders 1, 4 and 5 are NOT skipped by an attack click (see group_click_skips). The target comes from the occupant of the clicked
// tile (FUN_01020655).
uint32_t SimulationEngine::issue_group_attack_order(const std::vector<uint32_t>& ant_ids, TileCoord target, uint32_t* needed) {
    struct Entry {
        uint32_t id;
        uint32_t d;
    };
    std::vector<Entry> e;
    for (uint32_t id : ant_ids) {
        AntUnit* a = impl_->find_unit(id);
        if (!a || !impl_->can_take_user_order(*a)) continue;
        if (impl_->group_click_skips(*a, target, false, true)) continue;
        const TileCoord at{a->pixel_x / 32, a->pixel_y / 32};
        const int32_t dr = std::abs(at.y - target.y);
        const int32_t dc = std::abs(at.x - target.x);
        e.push_back(Entry{id, static_cast<uint32_t>(std::max(dr, dc)) << 4});   // FUN_01020911
    }
    if (needed != nullptr) *needed = static_cast<uint32_t>(e.size());
    for (size_t p = 0; p < e.size(); ++p) {                                    // exchange sort, strict '>' (0x102893e..0x1028994)
        for (size_t j = p + 1; j < e.size(); ++j) {
            if (e[p].d > e[j].d) std::swap(e[p], e[j]);
        }
    }
    uint32_t ack = 0;
    for (size_t k = 0; k < e.size(); ++k) {
        AntUnit* a = impl_->find_unit(e[k].id);
        if (!a) continue;
        leave_base_queue(e[k].id);
        a->ability_target = TileCoord{-1, -1};
        const bool accepted = route_move_order(*impl_, *a, target, true);
        if (k == 0 && accepted) ack = e[k].id;                                 // acknowledgement: closest ant only (GoTo returned true)
    }
    return ack;
}

bool SimulationEngine::has_pending_path(uint32_t ant_id) const {
    return impl_->has_pending_path(ant_id);
}

void SimulationEngine::set_locomotion_trace_enabled(bool enabled) {
    impl_->loco_trace_enabled_ = enabled;
}

const std::vector<LocoTraceEvent>& SimulationEngine::locomotion_trace() const {
    return impl_->loco_trace_;
}


bool SimulationEngine::validate_cardinal_placement(TileCoord from, TileCoord to) const {
    int32_t dx = to.x - from.x;
    int32_t dy = to.y - from.y;
    if (dx != 0 && dy != 0) return false;
    return (std::abs(dx) + std::abs(dy) == 1);
}

// The ability entry points of the tests and of the HUD's direct calls. With `instant` the effect happens at once (a fixture
// that puts a bomb, a fire wall or a bridge stage on the tile); without it the ant starts the original's action from the tile
// it stands on (which must be a cardinal neighbour of the target): the clip plays and the world changes when it ends.
namespace {
bool ability_ant_ready(const SimulationEngineImpl& impl, const AntUnit* ant, AntType type) {
    if (!ant || !ant->is_alive() || impl.type_of(*ant) != type || ant->engaged || ant->frozen) return false;
    const uint8_t act = impl.orig_action_of(*ant);
    return act == 0 || act == 1 || act == 3;
}
} // namespace

bool SimulationEngine::plant_bomb(uint32_t ant_id, TileCoord target, bool instant) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || impl_->type_of(*ant) != AntType::Bomber) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->valid_ground(target, false)) return false;
    if (instant) {
        impl_->grid_.place_bomb(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), ant->player_id);
        impl_->audio_queue_.push_back(AudioEvent{SoundID::BombPick, ant->pixel_x, ant->pixel_y, 1, 255});
        impl_->stats_.get_player_stats_mut(ant->player_id).bombs_planted++;
        impl_->world_state_dirty_ = true;
        return true;
    }
    if (!ability_ant_ready(*impl_, ant, AntType::Bomber)) return false;
    impl_->start_plant(*ant, target, TileCoord{ant->pixel_x / 32, ant->pixel_y / 32});
    return true;
}

bool SimulationEngine::defuse_bomb(uint32_t ant_id, TileCoord target, bool instant) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || impl_->type_of(*ant) != AntType::Bomber) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->valid_bomb(target)) return false;
    if (instant) {
        impl_->grid_.clear_bomb(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y));
        impl_->audio_queue_.push_back(AudioEvent{SoundID::BombDefuseGrab, ant->pixel_x, ant->pixel_y, 1, 255});
        impl_->audio_queue_.push_back(AudioEvent{SoundID::BombBodySquash, ant->pixel_x, ant->pixel_y, 1, 255});
        impl_->stats_.get_player_stats_mut(ant->player_id).bombs_defused++;
        impl_->world_state_dirty_ = true;
        return true;
    }
    if (!ability_ant_ready(*impl_, ant, AntType::Bomber)) return false;
    impl_->start_defuse(*ant, target, TileCoord{ant->pixel_x / 32, ant->pixel_y / 32});
    return true;
}

bool SimulationEngine::ignite_fire(uint32_t ant_id, TileCoord target, bool instant) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || impl_->type_of(*ant) != AntType::Fire) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->valid_ground(target, false)) return false;
    if (instant) {
        impl_->grid_.place_firewall(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), ant->player_id);
        impl_->arm_structure_lifetime(target.x, target.y);
        impl_->audio_queue_.push_back(AudioEvent{SoundID::FireBeam, ant->pixel_x, ant->pixel_y, 1, 255});
        impl_->audio_queue_.push_back(AudioEvent{SoundID::FireErupt, ant->pixel_x, ant->pixel_y, 1, 255});
        impl_->stats_.get_player_stats_mut(ant->player_id).fires_lit++;
        impl_->world_state_dirty_ = true;
        return true;
    }
    if (!ability_ant_ready(*impl_, ant, AntType::Fire)) return false;
    impl_->start_ignite(*ant, target, TileCoord{ant->pixel_x / 32, ant->pixel_y / 32});
    return true;
}

bool SimulationEngine::extinguish_fire(uint32_t ant_id, TileCoord target, bool instant) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || impl_->type_of(*ant) != AntType::Fire) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.has_fire_at(target)) return false;
    if (instant) {
        impl_->grid_.clear_firewall(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y));
        impl_->audio_queue_.push_back(AudioEvent{SoundID::FireExtinguish, ant->pixel_x, ant->pixel_y, 1, 255});
        impl_->world_state_dirty_ = true;
        return true;
    }
    if (!ability_ant_ready(*impl_, ant, AntType::Fire)) return false;
    impl_->start_extinguish(*ant, target, TileCoord{ant->pixel_x / 32, ant->pixel_y / 32});
    return true;
}

bool SimulationEngine::build_bridge_step(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || impl_->type_of(*ant) != AntType::Swimmer) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->valid_water(target, false)) return false;
    if (!ability_ant_ready(*impl_, ant, AntType::Swimmer)) return false;
    impl_->start_bridge_build(*ant, target, TileCoord{ant->pixel_x / 32, ant->pixel_y / 32});
    return true;
}

bool SimulationEngine::demolish_bridge_step(uint32_t ant_id, TileCoord target) {
    AntUnit* ant = impl_->find_unit(ant_id);
    if (!ant || !ant->is_alive() || impl_->type_of(*ant) != AntType::Swimmer) return false;
    if (!validate_cardinal_placement(ant->pos, target)) return false;
    if (!impl_->grid_.in_bounds(target) || !impl_->grid_.get_cell(target).has_completed_bridge()) return false;
    if (!ability_ant_ready(*impl_, ant, AntType::Swimmer)) return false;
    impl_->start_bridge_demolish(*ant, target, TileCoord{ant->pixel_x / 32, ant->pixel_y / 32});
    return true;
}

bool SimulationEngine::can_unit_traverse(AntType type, TileCoord pos, uint8_t ant_team, bool is_entering_or_leaving) const {
    if (!impl_->grid_.in_bounds(pos)) return false;
    const auto& cell = impl_->grid_.get_cell(pos);
    return cell.is_passable(type == AntType::Swimmer, type == AntType::Fire, type == AntType::Thief, ant_team, is_entering_or_leaving);
}

bool SimulationEngine::has_bomb_at(TileCoord pos) const {
    return impl_->grid_.has_bomb_at(pos);
}

bool SimulationEngine::has_fire_at(TileCoord pos) const {
    return impl_->grid_.has_fire_at(pos);
}

bool SimulationEngine::has_living_ant_at(TileCoord pos) const {
    return impl_->has_living_ant_at(pos);
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

// ------------------------------------------------------------------------------------------------
// Hill waiting ring (ANTHILLQ, Ants.exe 0x10247f9). An ant that wants to enter is ordered to the entrance; when the
// entrance is not free it waits on the ring tiles in front of the hill, one queued ant is sent in every 200 ms.
// ------------------------------------------------------------------------------------------------

void SimulationEngine::join_base_queue(uint32_t ant_id) {
    AntUnit* unit = impl_->find_unit(ant_id);
    if (!unit || !unit->is_alive() || unit->player_id >= MAX_PLAYERS) return;
    const TileCoord home = impl_->team_entrance(unit->player_id);
    if (home.x < 0) return;
    const uint8_t act = impl_->orig_action_of(*unit);
    if (act != AntUnit::kActionIdle && act != AntUnit::kActionWalk && act != AntUnit::kActionStun) return;
    impl_->go_to(*unit, home, false, false);                                  // Order(&home, 0, 0, 0)
}

void SimulationEngine::leave_base_queue(uint32_t ant_id) {
    if (AntUnit* u = impl_->find_unit(ant_id)) u->home_state = 0;
}

bool SimulationEngine::is_ant_in_base_queue(uint32_t ant_id) const {
    const AntUnit* u = impl_->find_unit(ant_id);
    return u && u->home_state != 0;
}

uint32_t SimulationEngine::get_active_depositing_ant(uint8_t player_id) const {
    for (const auto& a : impl_->ants_) {
        if (a && a->player_id == player_id && a->loco_action == AntUnit::kActionEnter) return a->id;
    }
    return 0;
}

size_t SimulationEngine::get_base_queue_size(uint8_t player_id) const {
    size_t n = 0;
    for (const auto& a : impl_->ants_) {
        if (a && a->is_alive() && a->player_id == player_id && a->home_state == 2) ++n;
    }
    return n;
}

// The raid starts now (test hook): the loot is min(victim score, 50), fixed at the start.
void SimulationEngine::start_thief_infiltration(uint32_t ant_id, uint8_t target_team_id) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u || !u->is_alive() || target_team_id >= MAX_PLAYERS) return;
    const int32_t score = impl_->stats_.get_individual_score(target_team_id);
    impl_->start_raid(*u, target_team_id, std::min<int32_t>(score, static_cast<int32_t>(MAX_THIEF_STEAL)));
}

bool SimulationEngine::has_lunchbox_at(TileCoord pos) const {
    return impl_->grid_.has_lunchbox_at(pos);
}

uint32_t SimulationEngine::get_lunchbox_points(TileCoord pos) const {
    return impl_->grid_.get_lunchbox_points(pos);
}

// Test hook: the ant is thrown away from the pixel position (from_px, from_py) like the victim of a punch (range 4)
// or of an ordinary hit (range 1), with the original's direction rule and no damage.
void SimulationEngine::apply_knockback(uint32_t ant_id, int32_t from_px, int32_t from_py, int32_t min_tiles, int32_t max_tiles) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u || u->removed) return;
    const int32_t range = (std::max(min_tiles, max_tiles) >= 4) ? 4 : 1;
    const TileCoord from{from_px / 32, from_py / 32};
    const TileCoord at{u->pixel_x / 32, u->pixel_y / 32};
    const uint8_t kdir = impl_->knock_dir(at, from, range);
    u->engaged = true;
    impl_->hit_frame(*u, at, kdir, static_cast<uint8_t>(range));
}

void SimulationEngine::resolve_fire_contact(uint32_t ant_id, int32_t /*incoming_dx*/, int32_t /*incoming_dy*/) {
    AntUnit* u = impl_->find_unit(ant_id);
    if (!u || u->removed) return;
    const TileCoord at{u->pixel_x / 32, u->pixel_y / 32};
    impl_->blast(*u, 1, u->player_id < MAX_PLAYERS ? u->player_id : uint8_t{7}, false);
    (void)at;
}

void SimulationEngine::blast_tile_for_test(TileCoord tile) {
    for (auto& up : impl_->ants_) {
        AntUnit* u = up.get();
        if (u && !u->removed && u->occ_tile == tile) {
            impl_->blast(*u, 0, 7, true);
            return;
        }
    }
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


// Test hook: the ant sets off the bomb on `bomb_pos` (path completion case 0xA / message 0xF of the original).
void SimulationEngine::trigger_bomb_detonation(uint32_t ant_id, TileCoord bomb_pos, int32_t /*incoming_dx*/, int32_t /*incoming_dy*/) {
    AntUnit* ant_ptr = impl_->find_unit(ant_id);
    if (!ant_ptr || ant_ptr->removed || !impl_->grid_.in_bounds(bomb_pos)) return;
    impl_->bomb_trigger(*ant_ptr, bomb_pos);
}

} // namespace ants::sim
