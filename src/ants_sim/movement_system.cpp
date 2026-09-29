// Original ant movement system: exact ports of the Ants.exe locomotion, order, path-manager delivery
// and tile-blocking code. Function addresses refer to Original-Ants/Ants.exe (image base 0x01000000);
// see docs/GAME_REVERSE_ENGINEERING.md, "Movement ground truth", for the verified pseudocode.
//
// Mapping of the original multiplayer model onto the remake: in Ants.exe each machine makes decisions
// only for its own team's ants ("IsLocal") and broadcasts the results. The remake has one authoritative
// simulation, so every ant follows the owner code paths and the "local team" is the ant's own team.

#include "sim_engine_impl.hpp"

#include <cstdlib>
#include <limits>

namespace ants::sim {

namespace {

constexpr int32_t kTile = 32;
constexpr uint32_t kBlockedCost = 8000;          // impassable edge / tile (FUN_01020951)
constexpr uint32_t kPauseMs = 300;
constexpr uint32_t kAnthillqPeriodMs = 200;     // ANTHILLQ task period (0x100e556)               // ANTPAUSE wait (FUN_0101cc1e -> FUN_0103057b(task, 0, 300))

// CanEnter flag bits (FUN_0101f780).
constexpr uint32_t kFinalTile      = 0x001;
constexpr uint32_t kCheckClaims    = 0x002;
constexpr uint32_t kOwnHillOk      = 0x004;
constexpr uint32_t kPowerUpOk      = 0x008;
constexpr uint32_t kQueueCount     = 0x010;
constexpr uint32_t kIgnoreBombs    = 0x020;
constexpr uint32_t kIgnoreOccupant = 0x040;
constexpr uint32_t kMovingMateOk   = 0x080;
constexpr uint32_t kSkipQueueRules = 0x100;

inline TileCoord no_order_tile() noexcept {
    return TileCoord{AntUnit::kNoOrderTileX, AntUnit::kNoOrderTileY};
}

inline TileCoord pixel_tile(const AntUnit& a) noexcept {       // FUN_0100ccc0 (idiv truncation)
    return TileCoord{a.pixel_x / kTile, a.pixel_y / kTile};
}

inline int32_t centre_x(TileCoord t) noexcept { return t.x * kTile + 16; }   // FUN_0100cd00
inline int32_t centre_y(TileCoord t) noexcept { return t.y * kTile + 16; }

inline uint8_t dir_between(TileCoord from, TileCoord to) noexcept {          // FUN_01017531
    int drow = to.y - from.y;
    int dcol = to.x - from.x;
    // The original indexes its 3x3 table with raw deltas; deltas larger than one tile only occur after
    // a knock-back resumes an old path (deferred system) and would read neighbouring memory there.
    drow = (drow > 0) ? 1 : ((drow < 0) ? -1 : 0);
    dcol = (dcol > 0) ? 1 : ((dcol < 0) ? -1 : 0);
    return static_cast<uint8_t>(movement::dir_from_delta(drow, dcol));
}

inline bool is_walking_label(UnitState s) noexcept {
    return s == UnitState::Walking || s == UnitState::Intercepting || s == UnitState::ReturningToPost ||
           s == UnitState::DivingInWater || s == UnitState::ExitingWater;
}

inline bool is_idle_label(UnitState s) noexcept {
    return s == UnitState::Idle || s == UnitState::GuardIdle || s == UnitState::Swimming;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------

void SimulationEngineImpl::movement_reset() {
    anim_clock_ms_ = 0;
    now_ms_ = 0;
    occ_.assign(static_cast<size_t>(grid_.width()) * grid_.height(), OccCell{});
    for (auto& pm : path_managers_) pm.clear();
    path_request_serial_.clear();
    anthillq_next_ms_ = 200;
    hatch_ = {};
}

// ------------------------------------------------------------------------------------------------
// Occupancy grid (world+0x553c)
// ------------------------------------------------------------------------------------------------

SimulationEngineImpl::OccCell* SimulationEngineImpl::occ_cell(TileCoord t) noexcept {
    if (!grid_.in_bounds(t)) return nullptr;
    const size_t idx = static_cast<size_t>(t.y) * grid_.width() + static_cast<size_t>(t.x);
    if (idx >= occ_.size()) return nullptr;
    return &occ_[idx];
}

// FUN_0100f2cd: every ant registered on t (team slots 0..3, then team index order); the first one found
// is the default occupant, and a later ant replaces it when it has no path and a stationary action.
AntUnit* SimulationEngineImpl::occ_scan(TileCoord t, const AntUnit* exclude, uint16_t* count) {
    uint16_t n = 0;
    AntUnit* best = nullptr;
    for (uint8_t team = 0; team < MAX_PLAYERS; ++team) {
        for (auto& up : ants_) {
            AntUnit* a = up.get();
            if (!a || a->player_id != team || a == exclude) continue;
            if (a->occ_tile != t) continue;
            ++n;
            if (!best) {
                best = a;
            } else if (a->waypoints.empty() && is_stationary_action(orig_action_of(*a))) {
                best = a;
            }
        }
    }
    if (count) *count = n;
    return best;
}

// FUN_0100f17f: move the ant's registration from its old tile to nt.
void SimulationEngineImpl::occ_move(AntUnit& a, TileCoord nt) {
    const TileCoord old = a.occ_tile;
    if (old.x >= 0) {
        if (nt == old) return;
        if (OccCell* c = occ_cell(old)) {
            if (c->multi) {
                uint16_t n = 0;
                AntUnit* one = occ_scan(old, &a, &n);
                c->multi = (n > 1);
                if (n == 1 && one) c->ant = static_cast<int32_t>(one->id);
                // n == 0: the original only clears the multi bit and leaves the departing ant's id.
            } else {
                c->ant = -1;
            }
        }
    }
    a.occ_tile = nt;
    if (nt.x < 0) return;
    if (OccCell* c = occ_cell(nt)) {
        if (c->ant < 0) c->ant = static_cast<int32_t>(a.id);
        else c->multi = true;
    }
}

// Remake: re-register ants whose pixel tile changed outside the movement system (teleports, knock-backs,
// spawns, test set-ups) and drop dead or underground ones, before occupancy is consulted by an order.
void SimulationEngineImpl::occ_refresh() {
    const size_t cells = static_cast<size_t>(grid_.width()) * grid_.height();
    if (occ_.size() != cells) {
        occ_.assign(cells, OccCell{});
        for (auto& up : ants_) {
            if (up) up->occ_tile = TileCoord{-1, -1};
        }
    }
    for (auto& up : ants_) {
        if (!up) continue;
        AntUnit& a = *up;
        if (!a.is_alive()) {
            if (a.occ_tile.x >= 0) occ_move(a, TileCoord{-1, -1});
        } else if (a.occ_tile != pixel_tile(a)) {
            occ_move(a, pixel_tile(a));
        }
    }
}

// FUN_0100f421 (TileInfo mask 1): the tile's occupant, or nullptr.
AntUnit* SimulationEngineImpl::occupant_at(TileCoord t) {
    OccCell* c = occ_cell(t);
    if (!c) return nullptr;
    AntUnit* a = nullptr;
    if (c->multi) a = occ_scan(t, nullptr, nullptr);
    else if (c->ant >= 0) a = find_unit(static_cast<uint32_t>(c->ant));
    // Remake: ants that died or went underground since the last movement tick are unregistered lazily.
    if (a && (!a->is_alive())) return nullptr;
    return a;
}

// FUN_0100f3ca: any ant registered on the tile.
bool SimulationEngineImpl::tile_occupied(TileCoord t) {
    return occupant_at(t) != nullptr;
}

// ------------------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------------------

bool SimulationEngineImpl::is_stationary_action(uint8_t action) noexcept {
    return action == 0 || (action >= 3 && action <= 9) || action == 0x0B;
}

// The original "action" (+0xe4) of an ant whose state is driven by other remake systems.
uint8_t SimulationEngineImpl::orig_action_of(const AntUnit& a) const noexcept {
    if (a.loco_action != AntUnit::kActionNone) return a.loco_action;
    if (a.is_transforming()) return 0x04;                      // getpow
    switch (a.state) {
        case UnitState::Attacking:          return 0x12;
        case UnitState::Flinch:             return 0x0E;
        case UnitState::Knockback:
        case UnitState::Bounce:             return 0x13;
        case UnitState::Stunned:            return 0x03;
        case UnitState::Burn:               return 0x0A;
        case UnitState::EnteringBase:       return 0x02;
        case UnitState::Drowning:           return 0x0F;
        case UnitState::Dead:               return 0x0C;
        case UnitState::Infiltrating:       return 0x0D;
        case UnitState::BuildingBridge:     return 0x10;
        case UnitState::DemolishingBridge:  return 0x11;
        case UnitState::PlantingBomb:       return 0x08;
        case UnitState::DefusingBomb:       return 0x09;
        case UnitState::PlacingFire:        return 0x06;
        case UnitState::ExtinguishingFire:  return 0x07;
        case UnitState::HarvestingFood:     return 0x05;
        case UnitState::CantGo:             return 0x0B;
        case UnitState::Walking:
        case UnitState::Intercepting:
        case UnitState::ReturningToPost:
        case UnitState::DivingInWater:
        case UnitState::ExitingWater:       return 0x01;
        default:                            return 0x00;
    }
}

// Ant vtable+0x28 (FUN_0101a93a): set the pixel position and re-register the occupancy tile.
void SimulationEngineImpl::set_position(AntUnit& a, int32_t x, int32_t y) {
    a.set_pixel_pos(x, y);
    occ_move(a, pixel_tile(a));
    const uint8_t terr = grid_.terrain_class_at(pixel_tile(a));
    a.in_water = (terr == movement::kTerrainWater);
    a.is_on_mud = (terr == movement::kTerrainMud);
}

void SimulationEngineImpl::set_idle_label(AntUnit& a) {
    if (a.type == AntType::Swimmer && grid_.terrain_class_at(pixel_tile(a)) == movement::kTerrainWater) {
        a.state = UnitState::Swimming;
    } else {
        a.state = (a.type == AntType::Combat) ? UnitState::GuardIdle : UnitState::Idle;
    }
}

void SimulationEngineImpl::set_walking_label(AntUnit& a) {
    if (a.dive_flag) {
        a.state = a.in_water ? UnitState::ExitingWater : UnitState::DivingInWater;
        return;
    }
    if (a.state == UnitState::DivingInWater || a.state == UnitState::ExitingWater || !is_walking_label(a.state)) {
        a.state = UnitState::Walking;
    }
}

// Food object identity for the harvest-order exceptions (+0xb0 compared with TileInfo +0x38).
int32_t SimulationEngineImpl::food_object_at(TileCoord t) const noexcept {
    if (!grid_.in_bounds(t)) return -1;
    const auto& cell = grid_.get_cell(t);
    if (cell.has_lunchbox()) {
        return 1000000 + t.y * static_cast<int32_t>(grid_.width()) + t.x;
    }
    if (!cell.has_food()) return -1;
    const auto& fs = grid_.food_schedules();
    for (size_t i = 0; i < fs.size(); ++i) {
        if (!fs[i].active) continue;
        for (const auto& c : fs[i].footprint) {
            if (c == t) return static_cast<int32_t>(i);
        }
    }
    return 2000000 + t.y * static_cast<int32_t>(grid_.width()) + t.x;
}

// Player object +0x2e: the hill entrance. Remake anthill origin (bx, by) is the top-left of the 4x4
// mound; the original's entrance is (bx + 1, by + 1).
TileCoord SimulationEngineImpl::team_entrance(uint8_t team) const noexcept {
    const auto* ah = grid_.find_anthill(team);
    if (!ah) return TileCoord{-1, -1};
    return TileCoord{static_cast<int32_t>(ah->x) + 1, static_cast<int32_t>(ah->y) + 1};
}

// FUN_0101d822: raid tile (+0x32), queue tiles (+0x36/+0x3a/+0x3e) or entrance (+0x2e) of a live team.
bool SimulationEngineImpl::is_special_base_tile(TileCoord t) const noexcept {
    for (const auto& ah : grid_.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (t == TileCoord{bx + 3, by + 2} || t == TileCoord{bx + 1, by + 1}) return true;
        if (t.y == by - 1 && t.x >= bx && t.x <= bx + 2) return true;
    }
    return false;
}

bool SimulationEngineImpl::has_pending_path(uint32_t ant_id) const noexcept {
    for (const auto& pm : path_managers_) {
        if (pm.has_request_for(ant_id)) return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// Animation stepping
// ------------------------------------------------------------------------------------------------

// FUN_0102c0db + FUN_0102c1fc: play a clip; ants are on the display list (running bit set), so the
// start step runs immediately.
void SimulationEngineImpl::loco_play(AntUnit& a, const movement::MotionClip& clip, uint8_t dir, uint16_t evt5_ms) {
    a.loco.clip = clip;
    a.loco.dir = dir;
    a.loco.evt5_ms = evt5_ms;
    a.loco.cursor = 0;
    a.loco.next_ms = now_ms_;
    ++a.loco.serial;
    if (!clip.valid()) return;
    loco_update(a, now_ms_);
}

// FUN_0102b95f: catch-up loop with a single "now".
void SimulationEngineImpl::loco_update(AntUnit& a, uint32_t now) {
    while (loco_step(a, now) != 0) {
    }
}

// FUN_0102b997: one animation step. The displacement of the frame whose time just expired is applied
// when it ends; if the step callback starts a new clip (which runs its own start step), the new first
// frame's duration is booked a second time (re-entrancy quirk of the original).
int SimulationEngineImpl::loco_step(AntUnit& a, uint32_t now) {
    auto& p = a.loco;
    if (!p.clip.valid()) return 0;
    StepEvt e{};
    uint16_t next_cursor = 1;
    if (p.cursor == 0) {
        e.status = 0;
        e.event = p.clip.event(0);
        next_cursor = 1;
    } else {
        if (p.clip.count <= 1) return 0;               // empty or single-frame list never advances
        if (now < p.next_ms) return 0;
        const uint16_t i = static_cast<uint16_t>(p.cursor - 1);
        e.dx = p.clip.dx(i);
        e.dy = p.clip.dy(i);
        const bool at_tail = (p.cursor == p.clip.count);
        e.status = at_tail ? 2 : 1;
        next_cursor = at_tail ? static_cast<uint16_t>(1) : static_cast<uint16_t>(p.cursor + 1);
        e.event = p.clip.event(static_cast<uint16_t>(next_cursor - 1));
    }
    const uint32_t serial_before = p.serial;
    loco_on_step(a, e);                                 // ant vtable+0x38 (FUN_0101ee84)
    if (p.serial != serial_before) {
        if (!p.clip.valid()) return 0;
        next_cursor = (p.cursor != 0) ? p.cursor : static_cast<uint16_t>(1);
    }
    set_position(a, a.pixel_x + e.dx, a.pixel_y + e.dy); // vtable+0x28 with x,y read after the callback
    p.cursor = next_cursor;
    const uint16_t cur = static_cast<uint16_t>(next_cursor - 1);
    // The enter clip's heal frame (event 5) lasts (10 - hp) * 200 ms instead of its native 40 ms (FUN_0101e20d)
    p.next_ms += (p.evt5_ms != 0 && p.clip.event(cur) == 5) ? p.evt5_ms : p.clip.duration(cur);
    if (loco_trace_enabled_) trace_loco(LocoTraceEvent::Kind::Step, a, e.dx, e.dy);
    const int16_t snd = p.clip.sound(cur);               // FUN_0102bac8 (loco clips have no sound-once flag)
    if (snd >= 0) {
        audio_queue_.push_back(AudioEvent{static_cast<uint32_t>(snd), a.pixel_x, a.pixel_y, 1, 255});
    }
    return (e.status != 0) ? 1 : 0;
}

// FUN_0101ee84 (locomotion actions).
void SimulationEngineImpl::loco_on_step(AntUnit& a, StepEvt& e) {
    if (a.pause_active) return;                          // waiting: no processing, no tail
    switch (a.loco_action) {
        case AntUnit::kActionIdle:
        case AntUnit::kActionWalk:
            walk_step(a, e);
            break;
        case AntUnit::kActionEnter:                      // 0x101ef5c
        case AntUnit::kActionHatch:
            if (e.status == 2) enter_clip_end(a);
            break;
        case AntUnit::kActionRaid:                       // 0x101efbd
            if (e.status == 2) raid_clip_end(a);
            break;
        case AntUnit::kActionCantGo:
            if (e.status == 2) {                         // end of the *cg animation (case 0xb)
                set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
                a.waypoints.clear();
                a.current_waypoint_idx = 0;
                a.orig_order = AntUnit::kOrderNone;
                a.orig_order_tile = no_order_tile();
                set_idle_label(a);
                if (is_special_base_tile(pixel_tile(a))) {
                    const auto* ah = grid_.find_anthill(a.player_id);
                    if (ah) {
                        // Player +0x42 = entrance + (3, 3)
                        go_to(a, TileCoord{static_cast<int32_t>(ah->x) + 4, static_cast<int32_t>(ah->y) + 4}, false, false);
                    }
                } else {
                    stop_sync(a);
                }
            }
            break;
        default:
            break;
    }
    // tail: register on (pos + d) / 32
    occ_move(a, TileCoord{(a.pixel_x + e.dx) / kTile, (a.pixel_y + e.dy) / kTile});
}

// ------------------------------------------------------------------------------------------------
// Actions (FUN_0101ad02, locomotion subset)
// ------------------------------------------------------------------------------------------------

void SimulationEngineImpl::set_action(AntUnit& a, uint8_t action, uint8_t dir, int16_t terr_a, int16_t terr_b, bool flag) {
    // The old action is cleaned up first (Ants.exe 0x101ade0): its world effects happen here, whatever ends it.
    if (a.loco_action != AntUnit::kActionNone) action_cleanup(a, a.loco_action, action);
    const bool same_action = (a.loco_action == action);
    const bool same_dir = (static_cast<uint8_t>(a.facing) == dir);
    a.facing = static_cast<Direction>(dir & 7);
    a.loco_action = action;
    if (action != AntUnit::kActionIdle) cancel_pause(a);            // FUN_0101cc1e(0)
    bool supplied = true;
    if (terr_a == -1) {
        supplied = false;
        terr_a = static_cast<int16_t>(grid_.terrain_class_at(pixel_tile(a)));
    }
    const uint8_t type = static_cast<uint8_t>(a.type);
    const bool carrying = a.is_holding();
    switch (action) {
        case AntUnit::kActionIdle: {                                // always restarts
            const movement::MotionClip clip = (terr_a == movement::kTerrainWater)
                ? movement::idle_water_clip() : movement::idle_clip(type, dir, carrying);
            a.dive_flag = false;
            loco_play(a, clip, dir);
            return;
        }
        case AntUnit::kActionWalk: {
            if (flag && a.type == AntType::Swimmer) {
                const bool dive = (terr_a != movement::kTerrainWater && terr_b == movement::kTerrainWater);
                const bool climb = (terr_a == movement::kTerrainWater && terr_b != movement::kTerrainWater);
                if (dive || climb) {
                    a.dive_flag = true;
                    loco_play(a, dive ? movement::dive_clip(dir) : movement::climb_clip(dir), dir);
                    return;
                }
            }
            int16_t tr = !supplied ? terr_a : (flag ? terr_a : terr_b);
            if (tr == movement::kTerrainWater && a.type != AntType::Swimmer) tr = movement::kTerrainMud;
            movement::MotionClip clip{};
            if (tr == movement::kTerrainWater) clip = movement::swim_clip(dir);
            else if (tr >= 0 && tr <= 4) clip = movement::walk_clip(type, static_cast<uint8_t>(tr), dir, carrying);
            bool restart;
            if (!same_action) restart = true;
            else if (!supplied) restart = true;
            else if (a.dive_flag) { a.dive_flag = false; restart = true; }
            else if (flag) restart = !same_dir;
            else restart = (terr_a != terr_b);
            if (restart) loco_play(a, clip, dir);
            return;
        }
        case AntUnit::kActionCantGo:
            a.orig_order_tile = no_order_tile();
            loco_play(a, movement::cant_go_clip(type, carrying), dir);
            return;
        case AntUnit::kActionEnter: {                                // 0x101b1c1
            // The clip is chosen by "holding" (+0xe8); the heal frame (event 5) is stretched to (10 - hp) * 200 ms
            const uint16_t missing = (a.hp < AntUnit::MAX_HP) ? static_cast<uint16_t>(AntUnit::MAX_HP - a.hp) : uint16_t{0};
            loco_play(a, movement::action_clip(movement::ActionClip::Enter, type, 0, carrying), dir,
                      static_cast<uint16_t>(missing * 200u));
            a.state = UnitState::EnteringBase;
            return;
        }
        case AntUnit::kActionHatch:                                  // newborn: aghatch (the newborn is a worker)
            loco_play(a, movement::action_clip(movement::ActionClip::Hatch, type, 0, false), dir);
            a.state = UnitState::EnteringBase;
            return;
        case AntUnit::kActionRaid:                                   // atcr501
            loco_play(a, movement::action_clip(movement::ActionClip::Infiltrate, type, 0, false), dir);
            a.state = UnitState::Infiltrating;
            return;
        default:
            return;
    }
}

void SimulationEngineImpl::enter_cant_go(AntUnit& a) {
    set_action(a, AntUnit::kActionCantGo, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.state = UnitState::CantGo;
    a.anim_tick = 0;
    a.anim_subitem = 0;
}

// ------------------------------------------------------------------------------------------------
// Walking (FUN_0101b8cb) and arrival
// ------------------------------------------------------------------------------------------------

void SimulationEngineImpl::walk_step(AntUnit& a, StepEvt& e) {
    const TileCoord cur = (a.occ_tile.x >= 0) ? a.occ_tile : pixel_tile(a);     // +0x5a/+0x5c
    const int32_t cx = centre_x(cur);
    const int32_t cy = centre_y(cur);
    if (e.status == 0) return;
    if (a.dive_flag && e.status == 2) {                   // dive/climb finished: snap and arrive
        e.dx = cx - a.pixel_x;
        e.dy = cy - a.pixel_y;
        arrive(a, e, cur);
        return;
    }
    // B. end of an idle (or other stationary) animation loop without a path on a bomb tile: the bomb
    //    order ends the (empty) path through ARRIVE, which sets the bomb off (0x101baef-0x101bb8d).
    if (e.status == 2 && a.waypoints.empty() && is_stationary_action(a.loco_action) && grid_.has_bomb_at(cur)) {
        a.orig_order = AntUnit::kOrderBomb;
        a.orig_order_tile = cur;
        e.dx = 0;
        e.dy = 0;
        arrive(a, e, cur);
        return;
    }
    if (a.loco_action != AntUnit::kActionIdle && a.loco_action != AntUnit::kActionWalk) return;
    if (a.waypoints.empty()) return;
    if (a.loco_action == AntUnit::kActionIdle) {          // idle with a path: start walking
        arrive(a, e, cur);
        return;
    }
    if (a.current_waypoint_idx >= a.waypoints.size()) return;

    int32_t nx = a.pixel_x + e.dx;
    int32_t ny = a.pixel_y + e.dy;
    TileCoord nt{nx / kTile, ny / kTile};
    const bool new_tile = (nt != cur);
    const TileCoord wp = a.waypoints[a.current_waypoint_idx];
    const bool off_path = !new_tile && (nt != wp);
    if (a.orig_order == AntUnit::kOrderHarvest && a.pixel_x == cx && a.pixel_y == cy) {
        const int32_t food = food_object_at(wp);
        if (food >= 0 && food == a.orig_food_id) {          // next waypoint is part of the target food
            e.dx = 0;
            e.dy = 0;
            a.current_waypoint_idx = a.waypoints.size();
            arrive(a, e, cur);
            return;
        }
    }
    if (off_path) return;                                  // departure half of the stride
    if (new_tile) {
        if (nt != wp) {
            const int32_t sx = (e.dx > 0) ? 1 : -1;
            const int32_t sy = (e.dy > 0) ? 1 : -1;
            e.dx += sx; nx += sx;
            e.dy += sy; ny += sy;
            nt = TileCoord{nx / kTile, ny / kTile};
        }
        if (try_enter_tile(a, nt) == 0) {
            e.dx = cx - a.pixel_x;
            e.dy = cy - a.pixel_y;
            return;
        }
        if (a.pause_active) {
            e.dx = 0;
            e.dy = 0;
            return;
        }
    }
    if (a.dive_flag) return;
    const int32_t ncx = centre_x(nt);
    const int32_t ncy = centre_y(nt);
    if (static_cast<uint16_t>(std::abs(nx - ncx)) <= 2 && static_cast<uint16_t>(std::abs(ny - ncy)) <= 2) {
        e.dx = ncx - a.pixel_x;
        e.dy = ncy - a.pixel_y;
        arrive(a, e, nt);
        return;
    }
    if (new_tile) {
        set_action(a, AntUnit::kActionWalk, static_cast<uint8_t>(a.facing),
                   static_cast<int16_t>(grid_.terrain_class_at(cur)), static_cast<int16_t>(grid_.terrain_class_at(nt)), false);
    }
}

// ARRIVE (0x101b938)
void SimulationEngineImpl::arrive(AntUnit& a, StepEvt& e, TileCoord cur) {
    // Bomb test on the registered tile (0x101b960): a bomb there turns the order into a bomb order and ends
    // the path without the centre snap. (The combat ant auto-engage test that follows in the original is
    // part of the combat-ant system, which stays with the remake's guard AI for now.)
    const TileCoord t5a = (a.occ_tile.x >= 0) ? a.occ_tile : pixel_tile(a);
    if (grid_.has_bomb_at(t5a)) {
        a.orig_order = AntUnit::kOrderBomb;
        a.orig_order_tile = t5a;
        e.dx = 0;
        e.dy = 0;
        a.current_waypoint_idx = a.waypoints.size();
    }
    ++a.current_waypoint_idx;
    if (a.current_waypoint_idx >= a.waypoints.size()) {
        a.waypoints.clear();                               // ClearPath (FUN_0101ab56)
        a.current_waypoint_idx = 0;
        path_complete(a, e);
        return;
    }
    const TileCoord next = a.waypoints[a.current_waypoint_idx];
    const uint8_t dir = dir_between(cur, next);
    set_action(a, AntUnit::kActionWalk, dir,
               static_cast<int16_t>(grid_.terrain_class_at(cur)), static_cast<int16_t>(grid_.terrain_class_at(next)), true);
    set_walking_label(a);
    if (a.dive_flag) {
        a.state = (grid_.terrain_class_at(next) == movement::kTerrainWater) ? UnitState::DivingInWater : UnitState::ExitingWater;
    }
}

// FUN_0101ccaf: path completion. The follow-up behaviour of the other order kinds (harvest, power-up,
// base entry, raid, attack chase) stays with the existing remake systems, which trigger on arrival;
// every order therefore takes the "not handled" branch: stop at the tile (StopSync -> StopAt).
void SimulationEngineImpl::path_complete(AntUnit& a, StepEvt& e) {
    const TileCoord old_target = a.orig_order_tile;
    const uint8_t order = a.orig_order;
    const bool was_home = (a.home_state == 1);                 // heading for the waiting ring in front of the hill
    a.orig_order_tile = no_order_tile();
    a.arrived_this_tick = true;
    if (order == AntUnit::kOrderHome) {
        // case 2 (0x101cdc5): message 7 makes the ant play the enter clip on the entrance tile; the step's snap delta
        // stays (handled: no stop).
        enter_hill(a);
        return;
    }
    if (order == AntUnit::kOrderRaid) {
        // case 0xb (0x101d51b): a thief that reached the raid tile starts the raid; refusals stop or send it home
        const bool started = raid_arrive(a, e);
        if (!started) { e.dx = 0; e.dy = 0; }
        return;
    }
    if ((order == AntUnit::kOrderMove || order == AntUnit::kOrderPowerUp) && was_home) {
        // cases 1 / 4 (0x101cd1d): arrival at the ring tile: queued, first come first served (click orders first)
        a.home_state = 2;
        a.home_time_ms = a.home_priority ? 0u : now_ms_;
    }
    if (a.orig_order == AntUnit::kOrderBomb && grid_.has_bomb_at(old_target)) {
        // case 0xa: message 0x0f sets the bomb off at once (handled: no stop, the frame delta stays 0).
        // The blast itself is the remake's bomb/knock-back code; the ant flies in its walking direction.
        static constexpr int32_t kDirDx[8] = { 0,  1, 1, 1, 0, -1, -1, -1 };
        static constexpr int32_t kDirDy[8] = {-1, -1, 0, 1, 1,  1,  0, -1 };
        const int32_t f = static_cast<int32_t>(a.facing) & 7;
        const bool walked = (a.loco_action == AntUnit::kActionWalk);
        a.orig_order = AntUnit::kOrderNone;
        a.final_dest = TileCoord{-1, -1};
        a.state = UnitState::Idle;   // hands the ant over to the blast code
        loco_release(a);
        if (engine_) {
            engine_->trigger_bomb_detonation(a.id, old_target, walked ? kDirDx[f] : 0, walked ? kDirDy[f] : 0);
        }
        if (a.is_alive() && (a.state == UnitState::Idle || a.state == UnitState::GuardIdle)) {
            set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
            set_idle_label(a);
        }
        return;
    }
    // Remake guard AI: a combat ant that finished a player move guards the tile it stopped on.
    if (a.type == AntType::Combat && a.state == UnitState::Walking) a.guard_anchor = pixel_tile(a);
    // Harvest orders (case 5) stop next to the food; the remake's harvest and lunchbox code picks the food up
    // from there and needs to know which food was ordered.
    const bool harvest = (a.orig_order == AntUnit::kOrderHarvest);
    const TileCoord remake_dest = a.final_dest;
    stop_sync(a);
    if (harvest) a.final_dest = remake_dest;
    e.dx = 0;
    e.dy = 0;
}

void SimulationEngineImpl::loco_release(AntUnit& a) {
    a.loco_action = AntUnit::kActionNone;
    a.pause_active = false;
    a.dive_flag = false;
}

// FUN_010214d9 (message 0x13, handled synchronously) -> FUN_01021664
void SimulationEngineImpl::stop_sync(AntUnit& a) {
    stop_at(a, pixel_tile(a));
}

void SimulationEngineImpl::stop_at(AntUnit& a, TileCoord t) {
    set_position(a, centre_x(t), centre_y(t));
    cancel_pause(a);
    const uint8_t act = orig_action_of(a);
    const bool already_idle = a.waypoints.empty() && a.orig_order == AntUnit::kOrderNone &&
                              (act == 0 || (act > 2 && (act <= 9 || act == 0x0B)));
    if (already_idle) return;
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();                                   // SetPath(0, ...)
    a.current_waypoint_idx = 0;
    a.orig_order = AntUnit::kOrderNone;
    a.orig_order_tile = no_order_tile();
    a.final_dest = TileCoord{-1, -1};
    set_idle_label(a);
}

// ------------------------------------------------------------------------------------------------
// ANTPAUSE (FUN_0101cc1e / FUN_01024cf7 / 0x1024d2b / FUN_0101cbcc)
// ------------------------------------------------------------------------------------------------

void SimulationEngineImpl::start_pause(AntUnit& a) {
    if (a.pause_active) return;                             // already waiting: timer not restarted
    a.pause_active = true;
    a.pause_fire_ms = now_ms_ + kPauseMs;
    a.pause_saved_action = a.loco_action;
    a.pause_saved_dir = static_cast<uint8_t>(a.facing);
}

void SimulationEngineImpl::cancel_pause(AntUnit& a) {
    a.pause_active = false;
}

void SimulationEngineImpl::pause_fire(AntUnit& a) {
    a.pause_active = false;
    const uint8_t saved_action = a.pause_saved_action;
    const uint8_t saved_dir = a.pause_saved_dir;
    const bool attack_type = (a.orig_order == AntUnit::kOrderRaid || a.orig_order == AntUnit::kOrderAttack || a.orig_order == 0x0F);
    if (attack_type && !a.waypoints.empty() && a.current_waypoint_idx + 1 == a.waypoints.size() &&
        !can_enter(a, a.waypoints[a.current_waypoint_idx], 0)) {
        start_pause(a);                                     // re-wait with the original saved action/dir
        a.pause_saved_action = saved_action;
        a.pause_saved_dir = saved_dir;
        return;
    }
    set_action(a, saved_action, saved_dir, -1, -1, false);
    if (saved_action == AntUnit::kActionWalk) set_walking_label(a);
}

// ------------------------------------------------------------------------------------------------
// Tile entry (FUN_0101c4f2) and passability (FUN_0101f780 / FUN_01020951)
// ------------------------------------------------------------------------------------------------

int SimulationEngineImpl::try_enter_tile(AntUnit& a, TileCoord nt) {
    const TileCoord own = pixel_tile(a);
    const bool was_home = (a.home_state == 1);
    AntUnit* occ = occupant_at(nt);

    // A. contact with the attack target: melee resolution stays with the remake's combat code.
    if ((a.orig_order == AntUnit::kOrderAttack || a.orig_order == 0x0F) && occ &&
        occ->player_id == a.orig_target_team && occ->id == a.orig_target_ant) {
        const uint32_t target_id = occ->id;
        stop_sync(a);
        a.attack_target_id = target_id;
        return 0;
    }

    // B. passability
    uint32_t flags = 0;
    if (a.orig_order == AntUnit::kOrderPowerUp && nt == a.orig_order_tile) flags = kPowerUpOk;
    if (a.orig_order == AntUnit::kOrderBomb && nt == a.orig_order_tile) flags |= kIgnoreBombs;
    const TileCoord ent = team_entrance(a.player_id);
    if (a.orig_order == AntUnit::kOrderHome || own == ent || (ent.x >= 0 && own == TileCoord{ent.x, ent.y - 1})) {
        flags |= kOwnHillOk;
    }
    if (can_enter(a, nt, flags)) return 1;

    // C. blocked
    const TileCoord final_tile = a.waypoints.empty() ? nt : a.waypoints.back();
    bool is_final = (nt == final_tile);
    if (!is_final) {
        AntUnit* o2 = occupant_at(nt);
        if (o2 && !is_stationary_action(orig_action_of(*o2))) {       // moving occupant: wait 300 ms
            start_pause(a);
            set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
            return 1;
        }
    } else if (a.orig_order == AntUnit::kOrderRaid) {
        start_pause(a);
        set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
        return 1;
    }
    // REPATH
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    a.orig_order_tile = no_order_tile();
    const uint8_t order = a.orig_order;
    if (is_final && (order == 6 || order == 7 || order == 8 || order == 9 || order == 0x0D || order == 0x0E ||
                     order == AntUnit::kOrderAttack)) {
        is_final = false;
    }
    if (is_final) {
        if (order == AntUnit::kOrderHome && was_home) a.home_state = 2;   // (+0x70 is not written on this path)
        stop_sync(a);
        return 0;
    }
    switch (order) {
        case AntUnit::kOrderHarvest:
            go_to(a, a.orig_food_tile.x >= 0 ? a.orig_food_tile : final_tile, false, false);
            break;
        case AntUnit::kOrderPowerUp:
            go_to(a, final_tile, true, false);
            break;
        case AntUnit::kOrderAttack: {
            AntUnit* tgt = find_unit(a.orig_target_ant);
            go_to(a, (tgt && tgt->is_alive()) ? pixel_tile(*tgt) : final_tile, true, false);
            break;
        }
        default:
            go_to(a, final_tile, false, false);
            break;
    }
    // FUN_010100e5: positional "bump" effect (CHD anim 0xDC, sound 47) at the blocked tile
    active_effects_.push_back(VisualEffect{"bump", centre_x(nt), centre_y(nt), 0, 10});
    audio_queue_.push_back(AudioEvent{SoundID::Bump, centre_x(nt), centre_y(nt), 1, 255});
    return 0;
}

bool SimulationEngineImpl::can_enter(const AntUnit& a, TileCoord t, uint32_t flags) {
    if (!grid_.in_bounds(t)) return false;
    const auto& cell = grid_.get_cell(t);
    // R1 terrain (0x10049b8): water only for swimmers
    const uint8_t terr = grid_.terrain_class_at(t);
    if (!movement::terrain_walkable(terr)) {
        if (terr != movement::kTerrainWater || a.type != AntType::Swimmer) return false;
    }
    // R2 occupant
    AntUnit* occ = occupant_at(t);
    if (occ && !(flags & kIgnoreOccupant) && occ != &a) {
        if (occ->player_id == a.player_id) {
            if (!(flags & kMovingMateOk)) return false;
            if (occ->waypoints.empty() && is_stationary_action(orig_action_of(*occ))) return false;
        } else if (!(a.orig_order == AntUnit::kOrderAttack && a.orig_target_team == occ->player_id &&
                     a.orig_target_ant == occ->id)) {
            return false;
        }
    }
    // R3 anthill cells (4x4 mound of a live team): entrance, raid tile, own-hill rules
    for (const auto& ah : grid_.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (t.x < bx || t.x > bx + 3 || t.y < by || t.y > by + 3) continue;
        const bool raid = (a.orig_order == AntUnit::kOrderRaid && a.orig_target_team == ah.team_id &&
                           t == TileCoord{bx + 3, by + 2});
        if (!raid) {
            if (ah.team_id != a.player_id) return false;
            if (!(flags & kOwnHillOk)) return false;
            if ((flags & kFinalTile) && t != TileCoord{bx + 1, by + 1}) return false;
        }
        goto claims;
    }
    // R4 solid layer-1 object
    if (grid_.is_solid_object(t)) {
        if ((flags & kPowerUpOk) && cell.has_powerup()) return true;
        if (cell.has_fire() && a.type == AntType::Fire) return true;
        const int32_t food = food_object_at(t);
        if (food >= 0 && a.orig_order == AntUnit::kOrderHarvest && a.orig_food_id == food) return true;
        return false;
    }
    // R5 anthill queue tiles Q1..Q3 (bx..bx+2, by-1)
    if (!(flags & kSkipQueueRules)) {
        for (const auto& ah : grid_.anthills()) {
            const int32_t bx = static_cast<int32_t>(ah.x);
            const int32_t by = static_cast<int32_t>(ah.y);
            if (t.y != by - 1 || t.x < bx || t.x > bx + 2) continue;
            if (ah.team_id != a.player_id) return false;
            if (!(flags & kFinalTile) || !(flags & kQueueCount)) continue;
            uint16_t sum = 0;
            for (int32_t qx = bx; qx <= bx + 2; ++qx) {
                const TileCoord q{qx, by - 1};
                if (tile_occupied(q)) ++sum;
                for (const auto& up : ants_) {
                    const AntUnit* x = up.get();
                    if (!x || x == &a || x->player_id != a.player_id || x->orig_order != AntUnit::kOrderMove) continue;
                    if (x->orig_order_tile == q) ++sum;
                }
            }
            if (sum == 2) return false;                        // exactly 2 (3 passes)
        }
    }
claims:
    // R6 tiles claimed by team-mates on move / home orders
    if (flags & kCheckClaims) {
        // first come first served at the entrance: an ant that is not queued may not take it while another one waits
        const TileCoord home = team_entrance(a.player_id);
        if (home.x >= 0 && t == home && a.home_state != 2) {
            for (const auto& up : ants_) {
                const AntUnit* x = up.get();
                if (x && x != &a && x->player_id == a.player_id && x->home_state == 2) return false;
            }
        }
        for (const auto& up : ants_) {
            const AntUnit* x = up.get();
            if (!x || x == &a || x->player_id != a.player_id) continue;
            if ((x->orig_order == AntUnit::kOrderMove || x->orig_order == AntUnit::kOrderHome) && x->orig_order_tile == t) {
                return false;
            }
        }
    }
    // R7 own / allied bombs
    if (flags & kIgnoreBombs) return true;
    if (!cell.has_bomb()) return true;
    const uint8_t owner = cell.interactive_owner;
    if (owner == a.player_id) return false;
    if (stats_.get_alliance(a.player_id) == owner) return false;
    return true;
}

uint32_t SimulationEngineImpl::step_cost(const AntUnit& a, TileCoord from, TileCoord to) {
    if (!grid_.in_bounds(to) || !grid_.in_bounds(from)) return kBlockedCost;
    const auto& cell = grid_.get_cell(to);
    const uint8_t lt = a.player_id;

    // Anthill mound cells and the raid shortcut
    int hill_team = -1;
    TileCoord hill_entrance{-1, -1};
    for (const auto& ah : grid_.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (to.x >= bx && to.x <= bx + 3 && to.y >= by && to.y <= by + 3) {
            hill_team = ah.team_id;
            hill_entrance = TileCoord{bx + 1, by + 1};
            if (a.orig_order == AntUnit::kOrderRaid && a.orig_target_team == ah.team_id && to == TileCoord{bx + 3, by + 2}) {
                goto terrain;
            }
            break;
        }
    }
    {
        AntUnit* occ = occupant_at(to);
        if (occ) {
            if (occ->player_id == lt) {
                if (occ->pause_active) return kBlockedCost;
                if (occ->waypoints.empty() && is_stationary_action(orig_action_of(*occ)) && !occ->has_order_tile()) {
                    return kBlockedCost;
                }
            } else if (!(a.orig_order == AntUnit::kOrderAttack && a.orig_target_team == occ->player_id &&
                         a.orig_target_ant == occ->id)) {
                return kBlockedCost;
            }
        }
        if (hill_team >= 0) {
            if (hill_team != lt) return kBlockedCost;
            if (a.orig_order != AntUnit::kOrderHome && from != team_entrance(a.player_id)) return kBlockedCost;
            (void)hill_entrance;
        }
        // Special tiles: another team's queue tiles
        if (!(a.orig_order == AntUnit::kOrderAttack && a.orig_order_tile == to)) {
            for (const auto& ah : grid_.anthills()) {
                const int32_t bx = static_cast<int32_t>(ah.x);
                const int32_t by = static_cast<int32_t>(ah.y);
                if (to.y == by - 1 && to.x >= bx && to.x <= bx + 2) {
                    if (ah.team_id != lt) return kBlockedCost;
                    break;
                }
            }
        }
        if (grid_.is_solid_object(to)) {
            const int32_t food = food_object_at(to);
            const bool ok = (a.orig_order_tile == to && cell.has_powerup() && a.orig_order == AntUnit::kOrderPowerUp) ||
                            (food >= 0 && a.orig_order == AntUnit::kOrderHarvest && a.orig_food_id == food) ||
                            (a.orig_order_tile == to && a.orig_order == AntUnit::kOrderRaid) ||
                            (cell.has_fire() && a.type == AntType::Fire);
            if (!ok) return kBlockedCost;
        }
        if (!(a.orig_order == AntUnit::kOrderBomb && a.orig_order_tile == to) && cell.has_bomb()) {
            if (cell.interactive_owner == a.player_id) return kBlockedCost;
            if (stats_.get_alliance(lt) == cell.interactive_owner) return kBlockedCost;
        }
    }
terrain: {
        const bool swimmer = (a.type == AntType::Swimmer);
        const uint32_t ca = movement::terrain_step_weight(grid_.terrain_class_at(from), swimmer);
        const uint32_t cb = movement::terrain_step_weight(grid_.terrain_class_at(to), swimmer);
        if (ca == kBlockedCost || cb == kBlockedCost) return kBlockedCost;
        uint32_t s = ca + cb;
        if (from.x != to.x && from.y != to.y) {
            // fild / fmul qword [0x10049e0] (1.4) / __ftol: an IEEE double multiply, truncated
            s = static_cast<uint32_t>(static_cast<double>(s) * 1.4);
        }
        return s >> 1;
    }
}

// ------------------------------------------------------------------------------------------------
// Orders
// ------------------------------------------------------------------------------------------------

// FUN_0101ff5a
bool SimulationEngineImpl::can_take_user_order(const AntUnit& a) const noexcept {
    if (!a.is_alive()) return false;
    const uint8_t act = orig_action_of(a);
    return act == 0 || act == 1 || act == 3;
}

// FUN_01020655
void SimulationEngineImpl::classify_order(AntUnit& a, TileCoord t, bool special, bool user_cmd) {
    int hill_team = -1;
    for (const auto& ah : grid_.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (t.x >= bx && t.x <= bx + 3 && t.y >= by && t.y <= by + 3) { hill_team = ah.team_id; break; }
    }
    if (hill_team >= 0) {
        if (hill_team == a.player_id) {
            a.orig_order = AntUnit::kOrderHome;
        } else {
            a.orig_order = AntUnit::kOrderRaid;
            a.orig_target_team = static_cast<uint8_t>(hill_team);
        }
    } else if (special) {
        // Bomber/fire/swimmer special orders are driven by the remake's ability system; worker, thief
        // and combat ants keep order 0 (the original leaves +0xa8 unchanged after SetPath(0)).
    } else if (tile_occupied(t)) {
        AntUnit* occ = occupant_at(t);
        // An ally's ant opens the "attack your ally?" dialog in the original (FUN_0101ffab); the remake has
        // no dialog and treats the click as a move next to the ally.
        if (!occ || occ->player_id == a.player_id || !user_cmd || stats_.are_allies(a.player_id, occ->player_id)) {
            a.orig_order = AntUnit::kOrderMove;
        } else {
            a.orig_order = AntUnit::kOrderAttack;
            a.orig_target_team = occ->player_id;
            a.orig_target_ant = occ->id;
        }
    } else if (food_object_at(t) >= 0) {
        a.orig_order = AntUnit::kOrderHarvest;
        a.orig_food_id = food_object_at(t);
        a.orig_food_tile = t;
    } else if (grid_.in_bounds(t) && grid_.get_cell(t).has_powerup() && user_cmd) {
        a.orig_order = AntUnit::kOrderPowerUp;
    } else if (grid_.has_bomb_at(t)) {
        a.orig_order = AntUnit::kOrderBomb;
    } else {
        a.orig_order = AntUnit::kOrderMove;
    }
    a.orig_order_tile = t;
}

// FUN_010202e7(t, userCmd, 1, 1, 5)
bool SimulationEngineImpl::adjust_goal(AntUnit& a, TileCoord& t, bool user_cmd, bool allow_goal_bomb) {
    const TileCoord home = team_entrance(a.player_id);
    uint32_t flags = (user_cmd ? ((allow_goal_bomb ? kIgnoreBombs : 0u) | kPowerUpOk | kQueueCount) : 0u) |
                     kFinalTile | kCheckClaims;
    if (t == home) flags |= kOwnHillOk;
    if (a.orig_order == AntUnit::kOrderAttack && a.orig_order_tile == t) flags |= kSkipQueueRules;
    if (can_enter(a, t, flags)) return true;
    if (home.x >= 0 && t == home) {
        const TileCoord save = t;
        t = TileCoord{home.x - 2, home.y + 2};                // player +0x46: entrance + (2, -2)
        if (adjust_goal(a, t, user_cmd, allow_goal_bomb)) return true;   // (+0x68 is set by the order, see go_to)
        t = save;
        return false;
    }
    flags &= 0xffd3u;                                          // drop 0x20, 0x08, 0x04 for the ring scan
    const int32_t r0 = t.y;
    const int32_t c0 = t.x;
    const int32_t rows = static_cast<int32_t>(grid_.height());
    const int32_t cols = static_cast<int32_t>(grid_.width());
    auto try_tile = [&](int32_t r, int32_t c) -> bool {
        if (static_cast<uint16_t>(r) < static_cast<uint16_t>(rows) && static_cast<uint16_t>(c) < static_cast<uint16_t>(cols) &&
            can_enter(a, TileCoord{c, r}, flags)) {
            t = TileCoord{c, r};
            return true;
        }
        return false;
    };
    for (int32_t d = 1; d < 5; ++d) {
        if (c0 - d >= 0) {
            for (int32_t row = std::max(0, r0 - d); row <= r0 + d; ++row) if (try_tile(row, c0 - d)) return true;
        }
        for (int32_t row = std::max(0, r0 - d); row <= r0 + d; ++row) if (try_tile(row, c0 + d)) return true;
        if (r0 - d >= 0) {
            for (int32_t col = std::max(0, c0 - d + 1); col <= c0 + d - 1; ++col) if (try_tile(r0 - d, col)) return true;
        }
        for (int32_t col = std::max(0, c0 - d + 1); col <= c0 + d - 1; ++col) if (try_tile(r0 + d, col)) return true;
    }
    return false;
}

// FUN_0101fc50: snap, idle, classify, adjust the goal and queue an asynchronous path request.
bool SimulationEngineImpl::go_to(AntUnit& a, TileCoord t, bool user_cmd, bool special, bool allow_goal_bomb) {
    if (user_cmd && !can_take_user_order(a)) return false;
    occ_refresh();
    // Anthill clicked: own hill -> entrance; enemy hill -> raid tile for thieves, otherwise stop.
    for (const auto& ah : grid_.anthills()) {
        const int32_t bx = static_cast<int32_t>(ah.x);
        const int32_t by = static_cast<int32_t>(ah.y);
        if (t.x < bx || t.x > bx + 3 || t.y < by || t.y > by + 3) continue;
        if (ah.team_id == a.player_id) {
            t = TileCoord{bx + 1, by + 1};
        } else if (a.type == AntType::Thief) {
            t = TileCoord{bx + 3, by + 2};
        } else {
            stop_sync(a);
            return false;
        }
        break;
    }
    const TileCoord cur = pixel_tile(a);
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);   // FUN_0101ace3(0)
    set_position(a, centre_x(cur), centre_y(cur));                                          // snap (vtbl+0x18)
    cancel_pause(a);
    a.waypoints.clear();                                                                    // SetPath(0, ...)
    a.current_waypoint_idx = 0;
    a.orig_order = AntUnit::kOrderNone;
    a.orig_order_tile = no_order_tile();
    const TileCoord requested = t;
    classify_order(a, t, special, user_cmd);
    bool ok = true;
    if (a.orig_order != AntUnit::kOrderRaid) {
        // +0x68 keeps its old value during the goal check: the queued ant that ANTHILLQ sends in (+0x68 == 2) passes the
        // first-come-first-served rule of the entrance
        ok = adjust_goal(a, t, user_cmd, allow_goal_bomb);
        if (t != requested) classify_order(a, t, false, user_cmd);
    }
    // 0x101fed2: the entrance replaced by the waiting tile sets +0x68 = 1 and +0x6c = player, anything else clears +0x68
    const TileCoord own_entrance = team_entrance(a.player_id);
    if (t != requested && own_entrance.x >= 0 && requested == own_entrance) {
        a.home_state = 1;
        a.home_priority = user_cmd ? 1 : 0;
    } else {
        a.home_state = 0;
    }
    if (!ok) {
        stop_sync(a);
        return false;
    }
    path_managers_[a.player_id < MAX_PLAYERS ? a.player_id : 0].request(
        a.id, cur, t, static_cast<uint16_t>(grid_.height()), static_cast<uint16_t>(grid_.width()));
    path_request_serial_[a.id] = a.move_serial;
    // Remake bookkeeping: player orders report the resolved destination; orders from remake systems keep the
    // tile they asked for, so that repeating the same order is recognised as such.
    a.final_dest = user_cmd ? t : requested;
    set_walking_label(a);
    return true;
}

// FUN_0100cba4 + message-6 handler (0x10229b7)
void SimulationEngineImpl::deliver_path(uint32_t ant_id, const std::vector<TileCoord>& path) {
    AntUnit* a = find_unit(ant_id);
    if (!a || !a->is_alive()) return;
    auto it = path_request_serial_.find(ant_id);
    const bool stale = (it == path_request_serial_.end() || it->second != a->move_serial);
    if (it != path_request_serial_.end()) path_request_serial_.erase(it);
    if (stale) return;                                     // cancelled by a remake clear_path()
    if (path.empty()) {                                    // "Can't go there."
        if (loco_trace_enabled_) trace_loco(LocoTraceEvent::Kind::PathFailed, *a, 0, 0);
        stop_sync(*a);
        enter_cant_go(*a);
        news_queue_.push_back(NewsEvent{a->player_id, "Can't go there.", 0, 0x3A});
        // Remake systems waiting for this walk give up with it (attack chase, ability approach, harvest
        // return, hill entry slot).
        a->final_dest = pixel_tile(*a);
        a->attack_target_id = 0;
        a->pending_ability = OrderType::None;
        a->pending_ability_target = TileCoord{-1, -1};
        a->ability_target = TileCoord{-1, -1};
        a->harvest_origin = TileCoord{-1, -1};
        a->is_food_order = false;
        return;
    }
    if (a->loco_action != AntUnit::kActionIdle || a->pause_active || pixel_tile(*a) != path.front()) {
        if (is_walking_label(a->state) && a->waypoints.empty()) set_idle_label(*a);   // dropped: stale order
        return;
    }
    cancel_pause(*a);
    set_position(*a, centre_x(path.front()), centre_y(path.front()));
    set_action(*a, a->loco_action, static_cast<uint8_t>(a->facing), -1, -1, false);  // idle restarts
    a->waypoints = path;                                   // FUN_0101ab87: [start .. goal], index 0
    a->current_waypoint_idx = 0;
    a->orig_order_tile = path.back();
    if (loco_trace_enabled_) trace_loco(LocoTraceEvent::Kind::PathDelivered, *a, 0, 0);
}

void SimulationEngineImpl::trace_loco(LocoTraceEvent::Kind kind, const AntUnit& a, int32_t dx, int32_t dy) {
    LocoTraceEvent ev;
    ev.kind = kind;
    ev.time_ms = now_ms_;
    ev.ant_id = a.id;
    ev.px = a.pixel_x;
    ev.py = a.pixel_y;
    ev.dx = dx;
    ev.dy = dy;
    ev.clip = a.loco.clip.valid() ? a.loco.clip.chd_index : movement::kNoAnimation;
    ev.frame = (a.loco.cursor > 0) ? static_cast<uint16_t>(a.loco.cursor - 1) : 0;
    ev.action = a.loco_action;
    loco_trace_.push_back(ev);
}

// ------------------------------------------------------------------------------------------------
// Per-tick driver
// ------------------------------------------------------------------------------------------------

// Reconcile remake-driven state changes with the locomotion model (lazy start of idle / can't-go clips,
// occupancy registration after teleports, deactivation for states owned by other systems).
void SimulationEngineImpl::loco_sync(AntUnit& a) {
    if (!a.is_alive()) {
        if (a.occ_tile.x >= 0) occ_move(a, TileCoord{-1, -1});
        a.loco_action = AntUnit::kActionNone;
        a.pause_active = false;
        return;
    }
    if (a.occ_tile != pixel_tile(a)) occ_move(a, pixel_tile(a));
    if (a.loco_action == AntUnit::kActionEnter || a.loco_action == AntUnit::kActionHatch ||
        a.loco_action == AntUnit::kActionRaid) {
        return;                                            // running action clips: owned by the action system
    }
    const UnitState s = a.state;
    if (s == UnitState::CantGo) {
        if (a.loco_action != AntUnit::kActionCantGo) {
            set_action(a, AntUnit::kActionCantGo, static_cast<uint8_t>(a.facing), -1, -1, false);
        }
        return;
    }
    if (is_walking_label(s)) {
        if (a.loco_action == AntUnit::kActionNone ||
            (a.waypoints.empty() && !has_pending_path(a.id) && a.loco_action == AntUnit::kActionIdle && !a.pause_active)) {
            // A walking label without a path or pending request: nothing to walk.
            set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
            if (a.waypoints.empty() && !has_pending_path(a.id)) set_idle_label(a);
        }
        return;
    }
    if (is_idle_label(s)) {
        if (a.loco_action == AntUnit::kActionWalk || (a.loco_action == AntUnit::kActionIdle && a.pause_active)) {
            // Another system stopped a walking ant: drop the rest of the path and the order (SetPath(0)).
            a.waypoints.clear();
            a.current_waypoint_idx = 0;
            a.orig_order = AntUnit::kOrderNone;
            a.orig_order_tile = no_order_tile();
            a.pause_active = false;
            set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
        } else if (a.loco_action != AntUnit::kActionIdle) {
            set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
        }
        return;
    }
    if (a.loco_action != AntUnit::kActionNone) {           // state owned by another remake system
        loco_release(a);
        // The original keeps the path through hit / flight actions and resumes it after landing; those
        // actions are still the remake's knock-back code, which ends the walk and its order as before.
        a.waypoints.clear();
        a.current_waypoint_idx = 0;
        a.orig_order = AntUnit::kOrderNone;
        a.orig_order_tile = no_order_tile();
    }
}

void SimulationEngineImpl::movement_tick(SimulationEngine& eng) {
    engine_ = &eng;
    occ_refresh();
    const uint32_t t_begin = anim_clock_ms_;
    const uint32_t t_end = t_begin + TICK_MS;
    now_ms_ = t_begin;
    for (auto& up : ants_) {
        if (!up) continue;
        up->arrived_this_tick = false;
        loco_sync(*up);
    }
    // Animation steps and pause timers in exact due-time order (ties: ant id = display-list order).
    for (;;) {
        AntUnit* best = nullptr;
        uint32_t best_t = std::numeric_limits<uint32_t>::max();
        bool best_is_pause = false;
        int task = 0;                                       // 1 ANTHILLQ, 2..5 HATCHTSK of team 0..3
        for (auto& up : ants_) {
            AntUnit* a = up.get();
            if (!a || a->loco_action == AntUnit::kActionNone) continue;
            const auto& p = a->loco;
            if (p.clip.valid() && p.cursor != 0 && p.clip.count > 1 && p.next_ms <= t_end && p.next_ms < best_t) {
                best = a;
                best_t = p.next_ms;
                best_is_pause = false;
            }
            if (a->pause_active && a->pause_fire_ms <= t_end && a->pause_fire_ms < best_t) {
                best = a;
                best_t = a->pause_fire_ms;
                best_is_pause = true;
            }
        }
        if (anthillq_next_ms_ <= t_end && anthillq_next_ms_ < best_t) {
            best = nullptr;
            best_t = anthillq_next_ms_;
            task = 1;
        }
        for (uint8_t team = 0; team < MAX_PLAYERS; ++team) {
            if (hatch_[team].active && hatch_[team].due_ms <= t_end && hatch_[team].due_ms < best_t) {
                best = nullptr;
                best_t = hatch_[team].due_ms;
                task = 2 + team;
            }
        }
        if (!best && task == 0) break;
        now_ms_ = best_t;
        if (task == 1) {
            anthillq_next_ms_ += kAnthillqPeriodMs;
            anthillq_run();
        } else if (task >= 2) {
            hatch_run(static_cast<uint8_t>(task - 2));
        } else if (best_is_pause) {
            pause_fire(*best);
        } else {
            loco_step(*best, now_ms_);
        }
    }
    now_ms_ = t_end;
    anim_clock_ms_ = t_end;
    // PATHMGR: one run per player every 50 ms; at most one path delivered per run.
    for (uint8_t team = 0; team < MAX_PLAYERS; ++team) {
        auto delivery = path_managers_[team].run([this](uint32_t ant_id, TileCoord from, TileCoord to) -> uint32_t {
            const AntUnit* u = find_unit(ant_id);
            if (!u) return kBlockedCost;
            return step_cost(*u, from, to);
        });
        if (delivery) deliver_path(delivery->ant_id, delivery->path);
    }
    engine_ = nullptr;
}

} // namespace ants::sim
