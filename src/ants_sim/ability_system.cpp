// Original ant abilities, part of the action system: the bomber's bombs, the fire ant's fire walls and the swimmer's
// bridges. Exact ports of the Ants.exe routines named in the comments (addresses refer to Original-Ants/Ants.exe,
// image base 0x01000000). Every ability is an action whose clip is played by the locomotion player
// (movement_system.cpp); the world changes when the old action is cleaned up by the next SetAction, so an interrupted
// ability has a different outcome than a finished one (the cflag of SetAction: a melee hit or a stun cancels, anything
// else - a blast, a death, the end of the clip - completes it). Only the ability's start puts an invisible, solid
// placeholder (tile 0xa0) on the target tile.
//
// Every ant of the remake's single authoritative simulation follows the owner ("IsLocal") code path, and the messages of
// the original (0xb ignite, 0xc extinguish, 0xd plant, 0xe defuse, 0x19 bridge build, 0x1a bridge demolish) are handled
// synchronously, as the local handler of FUN_0100d791 does before it broadcasts.

#include "sim_engine_impl.hpp"

namespace ants::sim {

namespace {

constexpr int32_t kTile = 32;

inline TileCoord no_order_tile() noexcept {
    return TileCoord{AntUnit::kNoOrderTileX, AntUnit::kNoOrderTileY};
}
inline TileCoord pixel_tile(const AntUnit& a) noexcept { return TileCoord{a.pixel_x / kTile, a.pixel_y / kTile}; }
inline TileCoord tile5a(const AntUnit& a) noexcept { return (a.occ_tile.x >= 0) ? a.occ_tile : pixel_tile(a); }
inline int32_t centre_x(TileCoord t) noexcept { return t.x * kTile + 16; }
inline int32_t centre_y(TileCoord t) noexcept { return t.y * kTile + 16; }

} // namespace

// ------------------------------------------------------------------------------------------------
// Arrival (FUN_0101ccaf cases 6..9, 0xd, 0xe)
// ------------------------------------------------------------------------------------------------

// The ant stands on the approach tile: the ability starts when its target is still valid (any ant on the target tile
// blocks a bomb or a fire wall now: the validators run with "any occupant"). Returns true when the ability started.
bool SimulationEngineImpl::ability_arrive(AntUnit& a, uint8_t order) {
    const TileCoord t = a.orig_special_tile;
    const TileCoord approach = tile5a(a);
    switch (order) {
        case AntUnit::kOrderIgnite:                                                      // case 6
            if (valid_ground(t, false)) { start_ignite(a, t, approach); return true; }
            break;
        case AntUnit::kOrderExtinguish:                                                  // case 7
            if (grid_.has_fire_at(t)) { start_extinguish(a, t, approach); return true; }
            break;
        case AntUnit::kOrderPlant:                                                       // case 8
            if (valid_ground(t, false)) { start_plant(a, t, approach); return true; }
            break;
        case AntUnit::kOrderDefuse:                                                      // case 9
            if (valid_bomb(t)) { start_defuse(a, t, approach); return true; }
            break;
        case AntUnit::kOrderBridgeBuild:                                                 // case 0xd
            if (valid_water(t, false)) { start_bridge_build(a, t, approach); return true; }
            break;
        case AntUnit::kOrderBridgeDemolish:                                              // case 0xe
            if (grid_.in_bounds(t) && grid_.get_cell(t).has_completed_bridge()) {
                start_bridge_demolish(a, t, approach);
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

// SetActionDefault(0) followed by SetPath(0): the tail of the ends of actions 4, 6, 7, 8, 9 and of the bridge actions
// (0x101f5a8): the ant is idle, its path and its order are gone.
void SimulationEngineImpl::end_walk_to_idle(AntUnit& a) {
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    a.orig_order = AntUnit::kOrderNone;
    a.orig_order_tile = no_order_tile();
    a.final_dest = TileCoord{-1, -1};
}

// Step callback of the actions 6, 7, 8 and 9 at the last frame (0x101f237, 0x101f277, 0x101f52b, 0x101f568): the ant is
// put on its tile centre and idles; the cleanup of the old action does the work of the ability.
void SimulationEngineImpl::ability_clip_end(AntUnit& a) {
    const TileCoord t = tile5a(a);
    set_position(a, centre_x(t), centre_y(t));
    end_walk_to_idle(a);
}

// ------------------------------------------------------------------------------------------------
// Bombs
// ------------------------------------------------------------------------------------------------

// Message 0xd handler FUN_01021915: the bomber faces the target, plays the plant clip (absb: 1360 ms N / S, 1400 ms E / W,
// cue 90 at 880 / 920 ms) and reserves the target tile with the invisible solid placeholder 0xa0.
void SimulationEngineImpl::start_plant(AntUnit& a, TileCoord target, TileCoord approach) {
    const uint8_t dir = dir_from_to(approach, target);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    set_action(a, AntUnit::kActionPlant, dir, -1, -1, false);
    set_position(a, centre_x(approach), centre_y(approach));
    a.orig_order = AntUnit::kOrderPlant;
    a.orig_special_tile = target;
    grid_.set_layer2(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), TILE_RESERVED, a.player_id);
    world_state_dirty_ = true;
}

// FUN_0101e433, cleanup of action 8: the placeholder becomes the team's bomb ("Bomb dropped."), unless the plant was
// cancelled (a melee hit or a stun): then the placeholder just disappears. A blast, a death or the end of the clip
// complete the plant at that very moment.
void SimulationEngineImpl::end_plant(AntUnit& a, bool cancel) {
    const TileCoord b0 = a.orig_special_tile;
    if (!grid_.in_bounds(b0)) return;
    const auto& cell = grid_.get_cell(b0);
    if (!cell.has_reserved() || cell.interactive_owner != a.player_id) return;
    if (cancel) {
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), TILE_EMPTY, 255);
        world_state_dirty_ = true;
        return;
    }
    grid_.place_bomb(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), a.player_id);
    stats_.get_player_stats_mut(a.player_id).bombs_planted++;
    post_news(a.player_id, "Bomb dropped.", 0x38);
    world_state_dirty_ = true;
}

// Message 0xe handler FUN_010219e8: the defuse clip (abdb: 1140 ms N / E, 1100 ms S, cues 73 at 220 and 74 at 620 ms);
// nothing changes on the tile until the clip ends.
void SimulationEngineImpl::start_defuse(AntUnit& a, TileCoord target, TileCoord approach) {
    const uint8_t dir = dir_from_to(approach, target);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    set_action(a, AntUnit::kActionDefuse, dir, -1, -1, false);
    set_position(a, centre_x(approach), centre_y(approach));
    a.orig_order = AntUnit::kOrderDefuse;
    a.orig_special_tile = target;
    world_state_dirty_ = true;
}

// FUN_0101e599, cleanup of action 9: the bomb is gone ("Bomb defused."), no explosion; when the bomb already went off
// there is nothing to do, and a cancelled defuse (melee, stun) leaves the bomb where it is.
void SimulationEngineImpl::end_defuse(AntUnit& a, bool cancel) {
    const TileCoord b0 = a.orig_special_tile;
    if (!valid_bomb(b0)) return;
    if (cancel) return;
    grid_.clear_bomb(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y));
    stats_.get_player_stats_mut(a.player_id).bombs_defused++;
    post_news(a.player_id, "Bomb defused.", 0x39);
    world_state_dirty_ = true;
}

// ------------------------------------------------------------------------------------------------
// Fire
// ------------------------------------------------------------------------------------------------

// Message 0xb handler FUN_010210fa: text "Starting a fire...", the ignite clip (afsf: 1760 ms S, 1810 ms N / E, cues 67
// at 500 and 68 at 1300 / 1350 ms) and the placeholder on the target tile.
void SimulationEngineImpl::start_ignite(AntUnit& a, TileCoord target, TileCoord approach) {
    const uint8_t dir = dir_from_to(approach, target);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    set_action(a, AntUnit::kActionIgnite, dir, -1, -1, false);
    post_news(a.player_id, "Starting a fire...", 0x41);
    set_position(a, centre_x(approach), centre_y(approach));
    a.orig_order = AntUnit::kOrderIgnite;
    a.orig_special_tile = target;
    grid_.set_layer2(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), TILE_RESERVED, a.player_id);
    world_state_dirty_ = true;
}

// FUN_0101e798, cleanup of action 6: the placeholder becomes a fire wall of the team, which burns out 180 s later (only
// when more than 180 s of the match remain); a cancelled ignite removes the placeholder.
void SimulationEngineImpl::end_ignite(AntUnit& a, bool cancel) {
    const TileCoord b0 = a.orig_special_tile;
    if (!grid_.in_bounds(b0)) return;
    const auto& cell = grid_.get_cell(b0);
    if (!cell.has_reserved() || cell.interactive_owner != a.player_id) return;
    if (cancel) {
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), TILE_EMPTY, 255);
        world_state_dirty_ = true;
        return;
    }
    grid_.place_firewall(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), a.player_id);
    arm_structure_lifetime(b0.x, b0.y);
    stats_.get_player_stats_mut(a.player_id).fires_lit++;
    world_state_dirty_ = true;
}

// Message 0xc handler FUN_010211f2: only for a fire wall; the extinguish clip (afxf: 1200 ms S, 1300 ms N / E, cue 69 at
// 400 ms).
void SimulationEngineImpl::start_extinguish(AntUnit& a, TileCoord target, TileCoord approach) {
    if (!grid_.has_fire_at(target)) return;
    const uint8_t dir = dir_from_to(approach, target);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    set_action(a, AntUnit::kActionExtinguish, dir, -1, -1, false);
    set_position(a, centre_x(approach), centre_y(approach));
    a.orig_order = AntUnit::kOrderExtinguish;
    a.orig_special_tile = target;
    world_state_dirty_ = true;
}

// FUN_0101e97b, cleanup of action 7: the fire wall is put out ("Fire put out.") with the sputter puff (830 ms, cue 5) and
// its burnout timer is cancelled; when the wall is already gone, or the extinguishing was cancelled, nothing happens.
void SimulationEngineImpl::end_extinguish(AntUnit& a, bool cancel) {
    const TileCoord b0 = a.orig_special_tile;
    if (!grid_.has_fire_at(b0)) return;
    if (cancel) return;
    grid_.clear_firewall(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y));
    spawn_tile_effect("sputter", b0.x, b0.y, effect_spec::kSputterMs);
    audio_queue_.push_back(AudioEvent{SoundID::FireBurnout, centre_x(b0), centre_y(b0), 1, 255});
    post_news(a.player_id, "Fire put out.", 0x40);
    world_state_dirty_ = true;
}

// ------------------------------------------------------------------------------------------------
// Bridges (swimmers)
// ------------------------------------------------------------------------------------------------

// Message 0x19 handler FUN_010212a3: the dig clip (bbw in water 500 ms per pass, bbl on land 480 ms, it loops) and the
// first bridge stage (0x22, walkable) on the target tile at once.
void SimulationEngineImpl::start_bridge_build(AntUnit& a, TileCoord target, TileCoord approach) {
    const uint8_t dir = dir_from_to(approach, target);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    set_action(a, AntUnit::kActionBridgeBuild, dir, -1, -1, false);
    a.orig_order = AntUnit::kOrderBridgeBuild;
    a.orig_special_tile = target;
    a.orig_b4 = TILE_BRIDGE1;
    grid_.set_layer2(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), TILE_BRIDGE1, a.player_id);
    world_state_dirty_ = true;
}

// Step callback of action 0x10 at the end of every pass (0x101f2b7): the bridge grows one stage when the tile is still
// the one this ant expects and belongs to its team; the last stage (0x25) ends the action, anything unexpected stops it.
void SimulationEngineImpl::bridge_build_pass_end(AntUnit& a) {
    const TileCoord b0 = a.orig_special_tile;
    const auto& cell = grid_.get_cell(b0);
    if (cell.interactive_id == a.orig_b4 && cell.interactive_owner == a.player_id) {
        const uint16_t next = static_cast<uint16_t>(cell.interactive_id + 1);
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), next, a.player_id);
        a.orig_b4 = next;
        world_state_dirty_ = true;
        if (next != TILE_BRIDGE4) return;                     // the clip loops on
    }
    const TileCoord t = tile5a(a);
    set_position(a, centre_x(t), centre_y(t));
    end_walk_to_idle(a);
}

// FUN_0101eaec, cleanup of action 0x10: a build that did not reach the completed bridge (or was cancelled by a hit or a
// stun) removes the bridge again; the finished bridge is (re)written and gets its 180 s collapse timer.
void SimulationEngineImpl::end_bridge_build(AntUnit& a, bool cancel) {
    const TileCoord b0 = a.orig_special_tile;
    if (!grid_.in_bounds(b0)) return;
    const auto& cell = grid_.get_cell(b0);
    if (cell.interactive_owner == a.player_id && (cancel || cell.interactive_id != TILE_BRIDGE4)) {
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), TILE_EMPTY, 255);
        world_state_dirty_ = true;
        return;
    }
    if (cell.interactive_id == a.orig_b4 && cell.interactive_owner == a.player_id) {
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), TILE_BRIDGE4, a.player_id);
        grid_.get_cell_mut(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y)).timer_ticks = LIFETIME_180S_TICKS;
        arm_structure_lifetime(b0.x, b0.y);
        world_state_dirty_ = true;
    }
}

// Message 0x1a handler FUN_0102137b: only a completed bridge; the dig clip (dbw / dbl, 4 passes) and the tile now belongs
// to the demolisher's team.
void SimulationEngineImpl::start_bridge_demolish(AntUnit& a, TileCoord target, TileCoord approach) {
    if (!grid_.in_bounds(target) || !grid_.get_cell(target).has_completed_bridge()) return;
    const uint8_t dir = dir_from_to(approach, target);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    set_action(a, AntUnit::kActionBridgeDemolish, dir, -1, -1, false);
    a.orig_order = AntUnit::kOrderBridgeDemolish;
    a.orig_special_tile = target;
    a.orig_b4 = TILE_BRIDGE4;
    auto& cell = grid_.get_cell_mut(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y));
    const uint32_t timer = cell.timer_ticks;
    grid_.set_layer2(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y), TILE_BRIDGE4, a.player_id);
    cell.timer_ticks = timer;                                  // the collapse timer keeps running until the tile is destroyed
    world_state_dirty_ = true;
}

// Step callback of action 0x11 at the end of every pass (0x101f401): one stage less when the tile is the expected one;
// the last stage (0x22) or an unexpected tile ends the action.
void SimulationEngineImpl::bridge_demolish_pass_end(AntUnit& a) {
    const TileCoord b0 = a.orig_special_tile;
    const auto& cell = grid_.get_cell(b0);
    if (cell.interactive_id != TILE_BRIDGE1 && cell.interactive_id == a.orig_b4 && cell.interactive_owner == a.player_id) {
        const uint16_t next = static_cast<uint16_t>(cell.interactive_id - 1);
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), next, a.player_id);
        a.orig_b4 = next;
        world_state_dirty_ = true;
        return;                                                // the clip loops on
    }
    const TileCoord t = tile5a(a);
    set_position(a, centre_x(t), centre_y(t));
    end_walk_to_idle(a);
}

// FUN_0101ecdf, cleanup of action 0x11: interrupted (or not at the last stage) the completed bridge is restored; at the
// end of the fourth pass the tile is destroyed for good (DestroyBridgeAt: the occupants drown or splash).
void SimulationEngineImpl::end_bridge_demolish(AntUnit& a, bool cancel) {
    const TileCoord b0 = a.orig_special_tile;
    if (!grid_.in_bounds(b0)) return;
    const auto& cell = grid_.get_cell(b0);
    const uint16_t id = cell.interactive_id;
    const uint8_t owner = cell.interactive_owner;
    if (owner != a.player_id) return;
    if (cancel || id != TILE_BRIDGE1) {
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), TILE_BRIDGE4, owner);
        world_state_dirty_ = true;
        return;
    }
    if (id == a.orig_b4) {
        grid_.set_layer2(static_cast<uint32_t>(b0.x), static_cast<uint32_t>(b0.y), TILE_EMPTY, 255);
        bridge_gone_scan(b0);
        world_state_dirty_ = true;
    }
}

} // namespace ants::sim
