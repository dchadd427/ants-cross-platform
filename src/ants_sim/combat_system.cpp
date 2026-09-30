// Original ant combat, part of the action system: contact, the strike frame, the hit and blown flights, landings,
// the blast dispersal, bomb victims, stun, drowning, death and the removal of an ant.
// Exact ports of the Ants.exe routines named in the comments (addresses refer to Original-Ants/Ants.exe, image base
// 0x01000000). Every action is a clip played by the locomotion player (movement_system.cpp); its events fire from the
// clip frames and the world changes at the frames the original changes it, not when the action starts.
//
// Every ant of the remake's single authoritative simulation follows the owner ("IsLocal") code path, and the messages
// of the original (msg 8 melee, 0xF bomb, 0x14 blast, 0x15 drown, 0x16 death, 0x11 power-up drop, 0x25 food drop) are
// handled synchronously, exactly as the original's local handler does before it broadcasts.

#include "sim_engine_impl.hpp"
#include "ants_sim/game_strings.hpp"

#include <algorithm>

namespace ants::sim {

namespace {

constexpr int32_t kTile = 32;
constexpr uint8_t kNoKiller = 7;
constexpr uint32_t kAlarmCueGapMs = 10000;   // OnAttacked plays the alarm at most once per 10 s per team

inline TileCoord no_order_tile() noexcept {
    return TileCoord{AntUnit::kNoOrderTileX, AntUnit::kNoOrderTileY};
}
inline TileCoord pixel_tile(const AntUnit& a) noexcept { return TileCoord{a.pixel_x / kTile, a.pixel_y / kTile}; }
inline int32_t centre_x(TileCoord t) noexcept { return t.x * kTile + 16; }
inline int32_t centre_y(TileCoord t) noexcept { return t.y * kTile + 16; }

// Power-up tile ids of the killed typed ants (0x10210c1): bomber 0x40, fire 0x42, thief 0x3f, combat 0x3e, swimmer 0x41.
inline uint8_t powerup_type_of(AntType t) noexcept { return static_cast<uint8_t>(t); }

} // namespace

// ------------------------------------------------------------------------------------------------
// Direction, tile and flight helpers
// ------------------------------------------------------------------------------------------------

// FUN_01017531 / FUN_01017560: direction of a step by the signs of the row and column difference.
uint8_t SimulationEngineImpl::dir_from_to(TileCoord from, TileCoord to) noexcept {
    return static_cast<uint8_t>(movement::dir_from_delta(to.y - from.y, to.x - from.x));
}

// FUN_0101d9f7: the tile `r` steps from `t` in direction `dir` (dir 8 = no direction, no movement).
TileCoord SimulationEngineImpl::step_toward(TileCoord t, uint8_t dir, int32_t r) noexcept {
    if (dir >= movement::kDirectionCount) return t;
    const movement::TileDelta d = movement::dir_delta(dir);
    return TileCoord{t.x + d.dcol * r, t.y + d.drow * r};
}

// The tile test of KnockDir (0x101d936..0x101d9a0) and PickLanding (0x101e00d..): inside the map, not solid (a fire
// wall counts as free although it is solid), not a hill special tile and not a tile of an anthill cell. Water, ants
// and bombs on the tile are no reasons to refuse it.
bool SimulationEngineImpl::landing_ok(TileCoord k) const {
    if (!grid_.in_bounds(k)) return false;
    if (!grid_.has_fire_at(k) && grid_.is_solid_object(k)) return false;
    if (is_special_base_tile(k)) return false;
    for (const auto& ah : grid_.anthills()) {
        if (k.x >= static_cast<int32_t>(ah.x) && k.x < static_cast<int32_t>(ah.x) + 4 &&
            k.y >= static_cast<int32_t>(ah.y) && k.y < static_cast<int32_t>(ah.y) + 4) {
            return false;
        }
    }
    return true;
}

// FUN_0101d8ed: the direction a victim is thrown, from the attacker's tile through the victim's tile; the directions
// dir, +1, -1, +2, -2 are tried at the same range, 8 = cornered (no flight).
uint8_t SimulationEngineImpl::knock_dir(TileCoord target, TileCoord attacker, int32_t range) const {
    const uint8_t d0 = dir_from_to(attacker, target);
    static constexpr int kOffsets[5] = {0, 1, -1, 2, -2};
    for (int attempt = 0; attempt < 5; ++attempt) {
        const uint8_t d = static_cast<uint8_t>((d0 + kOffsets[attempt] + 8) % 8);
        if (landing_ok(step_toward(target, d, range))) return d;
    }
    return 8;
}

// FUN_0101df5d: a random (or, without `use_rand`, the direction opposite the ant's facing) start direction, then up to
// 8 (4 for `cardinal`) directions in order; `excl` marks directions that are already used.
TileCoord SimulationEngineImpl::pick_landing(const AntUnit& self, TileCoord from, int32_t range, bool cardinal,
                                             bool* excl, bool use_rand) {
    uint32_t d = use_rand ? static_cast<uint32_t>(prng_.rand()) : static_cast<uint32_t>(self.facing) + 4u;
    d %= 8u;
    if (cardinal) {
        if (d % 2u != 0u) ++d;
        d %= 8u;
    }
    const int tries = cardinal ? 4 : 8;
    const uint32_t step = cardinal ? 2u : 1u;
    for (int i = 0; i < tries; ++i) {
        if (!(excl && excl[d])) {
            const TileCoord k = step_toward(from, static_cast<uint8_t>(d), range);
            if (landing_ok(k)) {
                if (excl) excl[d] = true;
                return k;
            }
        }
        d = (d + step) % 8u;
    }
    return from;
}

// ------------------------------------------------------------------------------------------------
// Hits
// ------------------------------------------------------------------------------------------------

// FUN_01021627 TakeHit: one hit point is lost (never below 0) and the team of the last damage is remembered.
void SimulationEngineImpl::take_hit(AntUnit& t, uint8_t source_team) {
    if (t.hp != 0) {
        --t.hp;
        if (source_team != kNoKiller) t.killer_team = source_team;
    }
    world_state_dirty_ = true;
}

// FUN_0101cb0c CanBeAttackedFrom: refused against an ant that is engaged, one action away from the hill, in a hit or
// a flight, drowning or dying, when the ants are not adjacent, or when either tile is water.
bool SimulationEngineImpl::can_be_attacked_from(const AntUnit& t, TileCoord attacker_tile) const {
    if (t.engaged) return false;
    const TileCoord tt = t.occ_tile.x >= 0 ? t.occ_tile : pixel_tile(t);
    if (std::abs(attacker_tile.y - tt.y) > 1 || std::abs(attacker_tile.x - tt.x) > 1) return false;
    const uint8_t act = orig_action_of(t);
    if (act == AntUnit::kActionHatch || act == AntUnit::kActionEnter || act == AntUnit::kActionBlast ||
        act == AntUnit::kActionHit || act == AntUnit::kActionDrown || act == AntUnit::kActionDeath) {
        return false;
    }
    if (grid_.terrain_class_at(attacker_tile) == movement::kTerrainWater) return false;
    if (grid_.terrain_class_at(tt) == movement::kTerrainWater) return false;
    return true;
}

// FUN_01010a03 OnAttacked: the victim's team reads "Ouch!" (its allies too), and hears the alarm unless it was
// attacked in the last 10 s.
void SimulationEngineImpl::on_attacked(uint8_t team) {
    if (team >= MAX_PLAYERS) return;
    const int64_t now = static_cast<int64_t>(anim_clock_ms_);
    const int64_t delta = now - last_attacked_ms_[team];
    last_attacked_ms_[team] = now;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        if (p != team && !stats_.are_allies(p, team)) continue;
        if (delta > static_cast<int64_t>(kAlarmCueGapMs)) {
            audio_queue_.push_back(AudioEvent{SoundID::BaseAlarmSiren, 0, 0, 1, p});
        }
        post_news(p, strings::kOuch);
    }
}

// FUN_01020c70 StartEngaged: the victim is hit at contact and stands idle facing the attacker until the strike frame.
// The cleanup of its old action runs with flag 1 (abilities are cancelled without any change of the world).
void SimulationEngineImpl::start_engaged(AntUnit& t, uint8_t dir) {
    t.engaged = true;
    set_action(t, AntUnit::kActionIdle, dir, -1, -1, false);
    t.waypoints.clear();                                   // ClearPath: the order and its target stay
    t.current_waypoint_idx = 0;
    ++t.move_serial;                                       // and no path request that is still queued may arrive
    if (t.type == AntType::Combat) {
        cancel_combat_timer(t);
        t.auto_engage = false;
    }
    on_attacked(t.player_id);
}

// FUN_01010245 StartMelee: the attacker faces the victim and plays its attack clip, the pending hit waits for the
// strike frame (event 4) or for the end of the clip.
void SimulationEngineImpl::start_melee(AntUnit& t, AntUnit& a, TileCoord t_tile, uint8_t kdir, uint8_t range) {
    const uint8_t face_ta = dir_from_to(pixel_tile(t), pixel_tile(a));
    set_action(a, AntUnit::kActionAttack, static_cast<uint8_t>((face_ta + 4) % 8), -1, -1, false);
    start_engaged(t, face_ta);
    if (orig_action_of(a) != AntUnit::kActionAttack) {     // the attacker could not start the clip: the hit is at once
        hit_frame(t, t_tile, kdir, range);
        return;
    }
    a.pending_range = range;
    a.pending_victim = t.id;
    a.pending_tile = t_tile;
    a.pending_dir = kdir;
    set_position(a, centre_x(pixel_tile(a)), centre_y(pixel_tile(a)));
}

// FUN_01010335 HitFrame: the flight of the victim (or, when cornered or no longer engaged, the stun / the death).
void SimulationEngineImpl::hit_frame(AntUnit& t, TileCoord tile, uint8_t kdir, uint8_t range) {
    if (!t.engaged || kdir == 8) {
        stun_or_die(t);
    } else {
        start_knock_flight(t, kdir, range, tile);
    }
    t.engaged = false;
}

// FUN_0101c1e2 DeliverPendingHit (also from the cleanup of old action 0x12, 0xC, 0xF and RemoveAnt).
void SimulationEngineImpl::deliver_pending_hit(AntUnit& a) {
    AntUnit* v = find_unit(a.pending_victim);
    const TileCoord tile = a.pending_tile;
    const uint8_t dir = a.pending_dir;
    const uint8_t range = a.pending_range;
    a.pending_victim = 0;
    if (v) hit_frame(*v, tile, dir, range);
}

// FUN_0101de7e StartKnockFlight: the victim is put on the contact tile and plays gh (range 1) or gb (range 4) facing the
// attacker while the clip carries it to the landing tile (the landing is not checked again).
void SimulationEngineImpl::start_knock_flight(AntUnit& t, uint8_t kdir, uint8_t range, TileCoord tile) {
    const uint8_t face = static_cast<uint8_t>((kdir + 4) % 8);
    set_position(t, centre_x(tile), centre_y(tile));
    const TileCoord land = step_toward(tile, kdir, range);
    if (range == 1) {
        set_action(t, AntUnit::kActionHit, face, -1, -1, false);
    } else if (range == 4) {
        set_action(t, AntUnit::kActionBlown, face, -1, -1, false);
    }
    t.orig_order_tile = land;
    t.flight_tile = land;
    t.orig_order = 0x0C;
    t.knock_flag = (kdir == 8);
}

// msg 8 handler FUN_01022ca1, run once at contact with the contact-time tiles.
void SimulationEngineImpl::handle_melee_message(AntUnit& t, AntUnit& a, TileCoord t_tile, uint8_t kdir, bool combat) {
    const uint8_t t_act = orig_action_of(t);
    if (t_act == AntUnit::kActionDrown || t_act == AntUnit::kActionDeath) return;
    const uint8_t a_act = orig_action_of(a);
    if (a_act == AntUnit::kActionDeath || a_act == AntUnit::kActionDrown) {        // the attacker is gone: no wind-up
        const uint8_t face = (kdir == 8) ? static_cast<uint8_t>(t.facing) : static_cast<uint8_t>((kdir + 4) % 8);
        take_hit(t, a.player_id);
        if (combat) take_hit(t, a.player_id);
        start_engaged(t, face);
        hit_frame(t, t_tile, kdir, combat ? 4 : 1);
        return;
    }
    take_hit(t, a.player_id);
    if (combat) take_hit(t, a.player_id);
    cancel_pause(t);
    cancel_pause(a);
    start_melee(t, a, t_tile, kdir, combat ? uint8_t{4} : uint8_t{1});
}

// TryEnterTile branch A (0x101c53f-0x101c671): the attacker's step crosses into the target's tile. Returns true when
// the melee started.
bool SimulationEngineImpl::melee_contact(AntUnit& a, AntUnit& t) {
    const TileCoord a_tile = a.occ_tile.x >= 0 ? a.occ_tile : pixel_tile(a);
    const TileCoord t_tile = t.occ_tile.x >= 0 ? t.occ_tile : pixel_tile(t);
    const bool water = grid_.terrain_class_at(t_tile) == movement::kTerrainWater ||
                       grid_.terrain_class_at(a_tile) == movement::kTerrainWater;
    if (water || !can_be_attacked_from(t, a_tile) || stats_.are_allies(a.player_id, t.player_id)) {
        stop_sync(a);
        enter_cant_go(a);
        post_news(a.player_id, water ? strings::kCantGoThere : strings::kCantDoThat);
        a.waypoints.clear();
        a.current_waypoint_idx = 0;
        if (a.type == AntType::Combat) resume_after_auto_engage(a);
        return false;
    }
    const bool combat = (a.type == AntType::Combat);
    const uint8_t kdir = knock_dir(t_tile, a_tile, combat ? 4 : 1);
    handle_melee_message(t, a, t_tile, kdir, combat);
    return true;
}

// ------------------------------------------------------------------------------------------------
// Stun, retreat and death (FUN_0102151a / FUN_0101dded)
// ------------------------------------------------------------------------------------------------

// FUN_0101dded LowHpCheck with flag 1: a 1 hp survivor goes home ahead of the queue, a victim without hit points
// starts dying. Returns true when it did one of them.
bool SimulationEngineImpl::low_hp_check(AntUnit& a) {
    if (a.hp == 1) {
        a.waypoints.clear();
        a.current_waypoint_idx = 0;
        ++a.move_serial;
        a.loco_action = AntUnit::kActionIdle;                                // raw write of action 0
        const TileCoord home = team_entrance(a.player_id);
        if (home.x >= 0) {
            go_to(a, home, false, false);                                     // Order(&home, 0, 0, 0)
            if (a.home_state == 1) a.home_priority = 1;                       // +0x6c = 1
        }
        return true;
    }
    if (a.hp == 0) {
        const uint8_t act = orig_action_of(a);
        if (act != AntUnit::kActionDeath && act != AntUnit::kActionDrown) {
            set_action(a, AntUnit::kActionDeath, static_cast<uint8_t>(a.facing), -1, -1, false);
        }
        return true;
    }
    return false;
}

// FUN_0102151a with flag 1 (the end of a hit, a flight or a burn): retreat or death, otherwise the stun clip.
// Returns true when the ant retreats or dies.
bool SimulationEngineImpl::stun_or_die(AntUnit& a) {
    if (low_hp_check(a)) {
        if (a.hp > 0) set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
        return true;
    }
    set_action(a, AntUnit::kActionStun, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.orig_order = AntUnit::kOrderNone;
    a.orig_order_tile = no_order_tile();
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    const TileCoord t = pixel_tile(a);
    set_position(a, centre_x(t), centre_y(t));
    return false;
}

// FUN_0102151a with flag 0 (the end of the stun clip): idle again, then the retreat or death check.
bool SimulationEngineImpl::stun_end(AntUnit& a) {
    if (a.loco_action != AntUnit::kActionStun) return false;
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    return low_hp_check(a);
}

// ------------------------------------------------------------------------------------------------
// Landings, blasts and bombs
// ------------------------------------------------------------------------------------------------

// FUN_0101c221 BlastHit (message 0x14): the ant is thrown one tile to `dest` from `at`, replacing whatever it did.
void SimulationEngineImpl::blast_hit(AntUnit& a, TileCoord at, TileCoord dest, uint8_t dmg, uint8_t src) {
    const uint8_t act = orig_action_of(a);
    if (act == AntUnit::kActionDeath || act == AntUnit::kActionDrown) return;
    const uint8_t d = static_cast<uint8_t>((dir_from_to(at, dest) + 4) % 8);
    if (dmg) take_hit(a, src);
    set_position(a, centre_x(at), centre_y(at));
    set_action(a, AntUnit::kActionHit, d, -1, -1, false);
    a.orig_order = 0x0C;
    a.orig_order_tile = dest;
    a.flight_tile = dest;
    a.knock_flag = (at == dest);
}

// FUN_0101c34c Blast: every ant on the tile (every team, the blasted ant included) is thrown to its own free
// neighbour in distinct directions; `dmg` hit points are lost by each (0 for a pile-up, 1 for a fire wall).
void SimulationEngineImpl::blast(AntUnit& a, uint8_t dmg, uint8_t src) {
    const TileCoord at = a.occ_tile.x >= 0 ? a.occ_tile : pixel_tile(a);
    // 0x101c449: a blast of an ant that is not the viewer's own (its owner's machine decides in the original) shows the dust
    // ball; the remake's single simulation still disperses the ants at once.
    if (a.player_id != viewing_player_id_) spawn_battle_cloud(at);
    bool excl[8] = {false, false, false, false, false, false, false, false};
    std::vector<AntUnit*> on_tile;
    for (auto& up : ants_) {
        AntUnit* x = up.get();
        if (x && !x->removed && x->occ_tile == at) on_tile.push_back(x);
    }
    for (AntUnit* x : on_tile) {
        const TileCoord dest = pick_landing(a, pixel_tile(*x), 1, false, excl, true);
        blast_hit(*x, pixel_tile(*x), dest, dmg, src);
    }
}

// FUN_0101a2aa (Cloud54): the dust ball is created at the tile centre, its clock starts now; its clip loops and plays sound 3
// (combatnetfairy) at every loop start.
void SimulationEngineImpl::spawn_battle_cloud(TileCoord tile) {
    BattleCloud c;
    c.tile = tile;
    c.created_ms = anim_clock_ms_;
    c.next_sound_ms = anim_clock_ms_;
    battle_clouds_.push_back(c);
    world_state_dirty_ = true;
}

// The cloud's step callback (0x101a329): gone after 3000 ms, when the tile holds no ant, or when the crowd (the tile's "more
// than one ant" bit) has been gone for more than 1000 ms; a returning crowd restarts that second.
void SimulationEngineImpl::tick_battle_clouds() {
    for (auto it = battle_clouds_.begin(); it != battle_clouds_.end();) {
        BattleCloud& c = *it;
        bool remove = (anim_clock_ms_ - c.created_ms) > effect_spec::kBattleCloudMaxMs;
        if (!remove) {
            const OccCell* oc = occ_cell(c.tile);
            if (!oc || oc->ant < 0) {
                remove = true;
            } else if (oc->multi) {
                c.clear_since_ms = 0;
            } else if (c.clear_since_ms == 0) {
                c.clear_since_ms = anim_clock_ms_;
            } else if (anim_clock_ms_ - c.clear_since_ms > effect_spec::kBattleCloudClearMs) {
                remove = true;
            }
        }
        if (remove) {
            stop_audio_owner(c.audio_owner);                                  // the cloud sprite is removed: the sounds of its loops stop with it
            it = battle_clouds_.erase(it);
            world_state_dirty_ = true;
            continue;
        }
        while (c.next_sound_ms <= anim_clock_ms_) {
            if (c.audio_owner == 0) c.audio_owner = new_audio_owner();
            audio_queue_.push_back(AudioEvent{effect_spec::kBattleCloudSound, centre_x(c.tile), centre_y(c.tile), 1, 255, c.audio_owner});
            c.next_sound_ms += effect_spec::kBattleMs;
        }
        ++it;
    }
    if (!battle_clouds_.empty()) world_state_dirty_ = true;
}

// FUN_0101c2e2 Drown (message 0x15): the ant plays the drowning clip on the tile; its hit points do not change.
void SimulationEngineImpl::drown(AntUnit& a, TileCoord t) {
    set_position(a, centre_x(t), centre_y(t));
    set_action(a, AntUnit::kActionDrown, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.orig_order = AntUnit::kOrderNone;
    a.orig_order_tile = t;
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
}

// FUN_0101e6b3: an ant that lands in (or is put into) water. A swimmer splashes and is stunned, every other ant drowns.
void SimulationEngineImpl::water_landing(AntUnit& a, TileCoord t) {
    if (a.type != AntType::Swimmer) {
        drown(a, t);
        return;
    }
    spawn_tile_effect("dsplash", t.x, t.y, effect_spec::kDsplashMs);
    stun_or_die(a);
    set_position(a, centre_x(t), centre_y(t));
}

// FUN_0100f8bf DestroyBridgeAt (after the tile was set empty): every ant on the tile is checked. A non-swimmer drowns
// (message 0x15), a swimmer only splashes (effect 0x28 = dsplash, which has no sound) and its current action is started
// again (SetActionDefault(current action), 0x100f99f) so that the clip is chosen for the water it now stands in: the water
// idle clip, the swim gait; an attacking swimmer loses its path and goes idle (current action 0x12: SetPath(0), action 0).
void SimulationEngineImpl::bridge_gone_scan(TileCoord t) {
    for (auto& up : ants_) {
        AntUnit* a = up.get();
        if (!a || a->removed) continue;
        const TileCoord at = a->occ_tile.x >= 0 ? a->occ_tile : pixel_tile(*a);
        if (at != t) continue;
        if (a->type == AntType::Swimmer) {
            a->in_water = true;
            a->was_in_water = true;
            spawn_tile_effect("dsplash", t.x, t.y, effect_spec::kDsplashMs);
            uint8_t act = orig_action_of(*a);
            if (act == AntUnit::kActionAttack) {
                a->clear_path();                                                    // 0x101ab56
                act = AntUnit::kActionIdle;
            }
            // (A swimmer in its death or drowning clip is left alone: restarting those clips would start the death effect a second time,
            // which the original's single clip does not do.)
            if (act != AntUnit::kActionDeath && act != AntUnit::kActionDrown) {
                set_action(*a, act, static_cast<uint8_t>(a->facing), -1, -1, false);     // 0x101ace3
                if (act == AntUnit::kActionIdle) set_idle_label(*a);
            }
        } else {
            const uint8_t act = orig_action_of(*a);
            if (act != AntUnit::kActionDrown && act != AntUnit::kActionDeath) drown(*a, t);
        }
    }
}

// path completion, case 0xA (0x101d44f): an ant that ends on a bomb tile sets the bomb off; 20 % of the bombs are
// duds (the ant burns on the spot), the others throw it 4 tiles.
void SimulationEngineImpl::bomb_trigger(AntUnit& a, TileCoord at) {
    if (!grid_.has_bomb_at(at)) return;
    const bool dud = (prng_.rand() % 100u) < 20u;
    const TileCoord to = dud ? at : pick_landing(a, at, 4, false, nullptr, false);
    bomb_victim(a, at, to);
}

// FUN_01021a6f BombVictim (message 0xF): the bomb is removed, the ant loses 2 hit points (credit: the bomb's owner) and
// is thrown to `to` (gb), or - a dud - stands frozen under the burn overlay; bombex plays on the tile.
void SimulationEngineImpl::bomb_victim(AntUnit& a, TileCoord at, TileCoord to) {
    const uint8_t d = static_cast<uint8_t>((dir_from_to(at, to) + 4) % 8);
    const uint8_t owner = grid_.get_cell(at).interactive_owner;
    grid_.clear_bomb(static_cast<uint32_t>(at.x), static_cast<uint32_t>(at.y));
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    take_hit(a, owner);
    take_hit(a, owner);
    const uint8_t act = orig_action_of(a);
    if (act != AntUnit::kActionBlast && act != AntUnit::kActionHit && act != AntUnit::kActionBlown) {
        set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    }
    set_position(a, centre_x(at), centre_y(at));
    a.flight_tile = to;
    a.knock_flag = (at == to);
    a.orig_order = AntUnit::kOrderBomb;
    set_action(a, AntUnit::kActionBlast, d, -1, -1, false);
    const uint32_t boom_owner = new_audio_owner();                            // the explosion sprite owns bombexp.wav (1144 ms) and is removed after 680 ms
    spawn_tile_effect("bombex", at.x, at.y, effect_spec::kBombexMs, boom_owner);
    audio_queue_.push_back(AudioEvent{SoundID::BombDetonate, centre_x(at), centre_y(at), 2, 255, boom_owner});
    if (a.knock_flag) burn_overlay_start(a);
}

// FUN_01021c68 BurnOverlay: the frozen ant is covered by the ?bu clip; when it ends the ant is unfrozen and stunned.
void SimulationEngineImpl::burn_overlay_start(AntUnit& a) {
    a.frozen = true;
    const movement::MotionClip clip = movement::action_clip(movement::ActionClip::Burn, static_cast<uint8_t>(a.type), 0, false);
    a.burn_end_ms = anim_clock_ms_ + clip.total_duration_ms();
    world_state_dirty_ = true;
}

// Callback 0x1021dda of the burn overlay's last frame: Unfreeze, remove the overlay, F102151a(1).
void SimulationEngineImpl::burn_overlay_end(AntUnit& a) {
    a.frozen = false;
    a.burn_end_ms = 0;
    stun_or_die(a);
}

// ------------------------------------------------------------------------------------------------
// Death
// ------------------------------------------------------------------------------------------------

// FUN_01020f89 FinishDeath (message 0x16) -> Kill (0x1020ff6): a typed ant leaves its power-up on a free neighbour tile, but only
// on its owner's machine (IsLocal); the owner of a team that dropped out is gone, so nobody drops anything for it.
void SimulationEngineImpl::finish_death(AntUnit& a) {
    if (a.removed) return;
    if (a.type != AntType::Worker && !team_dropped(a.player_id)) drop_powerup(a, pixel_tile(a), powerup_type_of(a.type));
    remove_ant(a);
}

// FUN_01020e6e DropPowerup: the 8 neighbours (never the own tile) in a cyclic order from a random row and column
// offset; the first free tile gets the power-up. Nothing free: only the powerupd cue (sound 40, audible, the owner hears it).
void SimulationEngineImpl::drop_powerup(AntUnit& a, TileCoord tile, uint8_t type) {
    int32_t r0 = static_cast<int32_t>(prng_.rand() % 3u);
    int32_t c0 = static_cast<int32_t>(prng_.rand() % 3u);
    int32_t r = r0;
    for (int i = 0; i < 3; ++i) {
        int32_t c = c0;
        for (int j = 0; j < 3; ++j) {
            const int32_t drow = 1 - r;
            const int32_t dcol = 1 - c;
            if (drow != 0 || dcol != 0) {
                const TileCoord cand{tile.x + dcol, tile.y + drow};
                if (is_valid_powerup_drop_tile(cand)) {
                    grid_.place_powerup(cand.x, cand.y, type);
                    world_state_dirty_ = true;
                    return;
                }
            }
            c = (c + 1) % 3;
        }
        r = (r + 1) % 3;
    }
    // Nothing free: the powerupd effect (anim 0xd5, sound 40, audible) plays at the tile, but FUN_01020e6e runs on the owner's machine only (0x1020e7a)
    audio_queue_.push_back(AudioEvent{40, centre_x(tile), centre_y(tile), 1, a.player_id});
}

// FUN_0100cd9f RemoveAnt: the text, the scorecard counters, the food drop, the release of the ant.
void SimulationEngineImpl::remove_ant(AntUnit& a) {
    const uint8_t killer = (a.killer_team < 4) ? a.killer_team : kNoKiller;
    const bool drowned = (orig_action_of(a) == AntUnit::kActionDrown);
    post_news(255, drowned ? strings::kAntDrowned : strings::kAntDead);
    if (a.player_id < MAX_PLAYERS) {
        stats_.get_player_stats_mut(a.player_id).friendly_lost++;
        if (killer != kNoKiller && killer != a.player_id) stats_.get_player_stats_mut(killer).enemy_killed++;
    }
    const TileCoord tile = pixel_tile(a);
    occ_move(a, TileCoord{-1, -1});
    if (a.is_holding() && grid_.in_bounds(tile)) {
        const auto& cell = grid_.get_cell(tile);
        if (cell.is_empty_overlay() && grid_.terrain_class_at(tile) != movement::kTerrainWater && !grid_.is_solid_object(tile)) {
            grid_.drop_lunchbox(static_cast<uint32_t>(tile.x), static_cast<uint32_t>(tile.y),
                                static_cast<uint32_t>(std::max<int32_t>(0, a.carried_signed())));   // (a thief's debt drops an empty lunchbox: what the original's lunchbox does with a negative amount is not known)
        }
    }
    a.clear_inventory();
    if (a.audio_tracked) {                                    // Map::RemoveSprite with [map + 0x68] set: the sprite's tracked sounds stop
        stop_audio_owner(a.id);
        a.audio_tracked = false;
    }
    if (a.pending_victim != 0) deliver_pending_hit(a);
    a.removed = true;
    a.hp = 0;
    a.state = UnitState::Dead;
    a.death_status = (drowned ? DeathStatus::Drowned : DeathStatus::CombatKilled);
    a.loco_action = AntUnit::kActionNone;
    a.waypoints.clear();
    ++a.move_serial;
    world_state_dirty_ = true;
    // CheckNoAnts (0x100cf48): a team without an ant that still has eggs and no egg incubating hatches one for free
    if (a.player_id < MAX_PLAYERS) {
        bool any = false;
        for (const auto& up : ants_) {
            if (up && !up->removed && up->player_id == a.player_id) { any = true; break; }
        }
        if (!any && stats_.get_egg_count(a.player_id) > 0 && !hatch_[a.player_id].active && (dropped_mask_ & (1u << a.player_id)) == 0) {
            hatch_request(a.player_id, AntType::Worker, true);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Combat ant auto-engage (the original's only "AI": no guard post, no pursuit range)
// ------------------------------------------------------------------------------------------------

// FUN_0101c152 CancelCombatTimer.
void SimulationEngineImpl::cancel_combat_timer(AntUnit& a) {
    a.combevt_due_ms = 0;
}

// FUN_0101c184 StartCombatTimer: the COMBEVT task fires once, after `ms`.
void SimulationEngineImpl::start_combat_timer(AntUnit& a, uint32_t ms) {
    cancel_combat_timer(a);
    a.combevt_due_ms = now_ms_ + ms;
}

// FUN_0101c0d5 CanAutoEngage: a combat ant that got no order for 2 s, is neither stunned, dying, drowning, engaged nor
// frozen and does not already chase a target.
bool SimulationEngineImpl::can_auto_engage(const AntUnit& a) const {
    if (a.type != AntType::Combat || a.removed) return false;
    if (static_cast<uint32_t>(now_ms_ - a.last_order_ms) <= 2000u) return false;
    const uint8_t act = orig_action_of(a);
    if (act == AntUnit::kActionStun || act == AntUnit::kActionDeath || act == AntUnit::kActionDrown) return false;
    if (a.hp == 0 || a.frozen || a.engaged) return false;
    return a.orig_order != AntUnit::kOrderAttack && a.orig_order != 0x0F;
}

// FUN_0101dbec FindEnemy: the rings r = 1 .. radius-1 around the ant's tile; per ring the left column, the right
// column, the top row and the bottom row; the first tile that holds an enemy wins (not the nearest one).
bool SimulationEngineImpl::find_enemy(const AntUnit& a, int32_t radius, TileCoord& tile) {
    const TileCoord o = tile;
    auto test = [&](TileCoord t) -> bool {                                     // FUN_0101db55
        if (!grid_.in_bounds(t)) return false;
        AntUnit* occ = occupant_at(t);
        if (!occ || occ->player_id == a.player_id || stats_.are_allies(a.player_id, occ->player_id)) return false;
        const uint8_t act = orig_action_of(*occ);
        if (act == 0x0C || act == 0x0F || act == 0x0A || act == 0x02 || act == 0x13 || act == 0x14 || act == 0x0E) return false;
        return !a.frozen;
    };
    for (int32_t r = 1; r < radius; ++r) {
        if (o.x - r >= 0) {
            for (int32_t row = std::max(o.y - r, 0); row <= o.y + r; ++row) {
                if (test(TileCoord{o.x - r, row})) { tile = TileCoord{o.x - r, row}; return true; }
            }
        }
        for (int32_t row = std::max(o.y - r, 0); row <= o.y + r; ++row) {
            if (test(TileCoord{o.x + r, row})) { tile = TileCoord{o.x + r, row}; return true; }
        }
        if (o.y - r >= 0) {
            for (int32_t col = std::max(o.x - r + 1, 0); col <= o.x + r - 1; ++col) {
                if (test(TileCoord{col, o.y - r})) { tile = TileCoord{col, o.y - r}; return true; }
            }
        }
        for (int32_t col = std::max(o.x - r + 1, 0); col <= o.x + r - 1; ++col) {
            if (test(TileCoord{col, o.y + r})) { tile = TileCoord{col, o.y + r}; return true; }
        }
    }
    return false;
}

// FUN_0101da6f AttackTile: one step towards the enemy's tile by a two tile path {here, next} (the step onto the enemy's
// own tile is the contact); when the step is not possible the ant resumes its old order.
void SimulationEngineImpl::attack_tile(AntUnit& a, TileCoord target) {
    const TileCoord me = pixel_tile(a);
    const uint8_t dir = dir_from_to(me, target);
    AntUnit* victim = occupant_at(target);
    a.auto_engage = true;
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    const TileCoord next = step_toward(me, dir, 1);
    if (!victim || !can_enter_attack_step(a, next, next == target)) {
        resume_after_auto_engage(a);
        return;
    }
    a.orig_order = 0x0F;
    a.orig_order_tile = next;
    a.orig_target_team = victim->player_id;
    a.orig_target_ant = victim->id;
    apply_path(a, std::vector<TileCoord>{me, next});
    set_walking_label(a);
}

// FUN_0101dd6f ResumeAfterAutoEngage: the order that was saved when the auto-engage began is given again (the
// player flag set unless it was a plain move). Returns true when it did so.
bool SimulationEngineImpl::resume_after_auto_engage(AntUnit& a) {
    cancel_combat_timer(a);
    const uint8_t act = orig_action_of(a);
    if (!a.auto_engage || !(act == AntUnit::kActionIdle || act == AntUnit::kActionWalk)) return false;
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    ++a.move_serial;
    const bool player = !(a.ae_order == AntUnit::kOrderNone || a.ae_order == AntUnit::kOrderMove);
    go_to(a, a.ae_target, player, false);
    a.home_state = a.ae_home_state;
    a.auto_engage = false;
    return true;
}

// The two hooks of the walk step (0x101b9a8 at every arrival on a tile, radius 3; 0x101bd67 for an idle ant
// without a path, radius 4): a combat ant that can auto-engage and sees an enemy saves its order and attacks.
bool SimulationEngineImpl::auto_engage_check(AntUnit& a, int32_t radius, bool at_arrival) {
    if (!can_auto_engage(a)) return false;
    const uint8_t act = orig_action_of(a);
    if (!(act == AntUnit::kActionIdle || act == AntUnit::kActionWalk || (!at_arrival && act == AntUnit::kActionStun))) return false;
    TileCoord tile = pixel_tile(a);
    if (!find_enemy(a, radius, tile)) return false;
    a.ae_order = a.orig_order;
    a.ae_target = at_arrival ? a.orig_order_tile : pixel_tile(a);
    a.ae_home_state = a.home_state;
    attack_tile(a, tile);
    start_combat_timer(a, at_arrival ? 2000u : 3000u);
    return true;
}

} // namespace ants::sim
