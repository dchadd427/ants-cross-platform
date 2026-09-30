// Original ant action system, part 1: the hill (enter, deposit, heal, waiting ring, hatch) and the thief's raid.
// Exact ports of the Ants.exe routines named in the comments (addresses refer to Original-Ants/Ants.exe, image base
// 0x01000000). The locomotion machinery lives in movement_system.cpp; this file adds the actions that run on the same
// animation player: an action is a clip, its events fire from the clip's frames, and the world changes when the old
// action is cleaned up by the next SetAction.
//
// Every ant of the remake's single authoritative simulation follows the owner ("IsLocal") code path.

#include "sim_engine_impl.hpp"
#include "ants_sim/game_strings.hpp"

#include <algorithm>

namespace ants::sim {

namespace {

constexpr int32_t kTile = 32;
constexpr uint32_t kHatchRetryMs = 8;      // the scheduler wheel granularity: a blocked HATCHTSK re-runs in the next slot
constexpr uint32_t kRaidLootMax = 50;      // a thief steals min(score of the victim, 50)

inline TileCoord no_order_tile() noexcept {
    return TileCoord{AntUnit::kNoOrderTileX, AntUnit::kNoOrderTileY};
}
inline TileCoord pixel_tile(const AntUnit& a) noexcept { return TileCoord{a.pixel_x / kTile, a.pixel_y / kTile}; }
inline int32_t centre_x(TileCoord t) noexcept { return t.x * kTile + 16; }
inline int32_t centre_y(TileCoord t) noexcept { return t.y * kTile + 16; }

} // namespace

// ------------------------------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------------------------------

// Player +0x42: the tile 3 rows and 3 columns beyond the entrance (remake hill origin + (4, 4)).
TileCoord SimulationEngineImpl::team_tile42(uint8_t team) const noexcept {
    const auto* ah = grid_.find_anthill(team);
    if (!ah) return TileCoord{-1, -1};
    return TileCoord{static_cast<int32_t>(ah->x) + 4, static_cast<int32_t>(ah->y) + 4};
}

// Player +0x46: the alternative waiting tile, entrance + (2, -2) (remake hill origin + (-1, 3)).
TileCoord SimulationEngineImpl::team_ring_tile(uint8_t team) const noexcept {
    const auto* ah = grid_.find_anthill(team);
    if (!ah) return TileCoord{-1, -1};
    return TileCoord{static_cast<int32_t>(ah->x) - 1, static_cast<int32_t>(ah->y) + 3};
}

// FUN_01010cc9 AddScore: the bubble and the scoreup / scoredn cue are produced from the score change queue at the end
// of the tick (every change is shown for every player).
void SimulationEngineImpl::add_score(uint8_t player, int32_t amount) {
    if (amount == 0) return;
    stats_.add_score(player, amount);
}

void SimulationEngineImpl::post_news(uint8_t player, uint16_t string_id, const std::string& a, const std::string& b,
                                     const std::string& c, const std::string& d) {
    NewsEvent ev;
    ev.target_player = player;
    ev.message_text = strings::format(string_id, a, b, c, d);
    ev.timestamp_ms = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(match_limit_ms_) - match_clock_ms_));
    ev.string_id = string_id;
    ev.blink = strings::blinks(string_id);
    ev.channel = NewsChannel::Status;
    news_queue_.push_back(std::move(ev));
}

void SimulationEngineImpl::post_news_flash(uint16_t string_id, const std::string& a, const std::string& b,
                                           const std::string& c, const std::string& d) {
    NewsEvent ev;
    ev.target_player = 255;
    ev.message_text = strings::format(string_id, a, b, c, d);
    ev.timestamp_ms = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(match_limit_ms_) - match_clock_ms_));
    ev.string_id = string_id;
    ev.channel = NewsChannel::ChatLog;
    news_queue_.push_back(std::move(ev));
}

// ------------------------------------------------------------------------------------------------
// SetAction: cleanup of the OLD action (table 0x101b48f, dispatched at 0x101ade0)
// ------------------------------------------------------------------------------------------------

// Returns false when the new action must not be installed (the ant finished dying, 0x101b41c).
bool SimulationEngineImpl::action_cleanup(AntUnit& a, uint8_t old_action, uint8_t new_action) {
    // flag = (new == stun || engaged): an engaged ant cancels its abilities without any change of the world
    const bool flag = (new_action == AntUnit::kActionStun) || a.engaged;
    switch (old_action) {
        case AntUnit::kActionEnter:
        case AntUnit::kActionHatch:
            cleanup_enter(a);          // 0x101e165 (for a newborn it changes nothing: no food, full health)
            break;
        case AntUnit::kActionRaid:
            cleanup_raid(a);           // 0x101e27f
            break;
        case AntUnit::kActionHarvest:
            end_harvest(a);            // 0x101e342: whatever ends the bite (a hit too), the ant has taken its food
            break;
        case AntUnit::kActionIgnite:         end_ignite(a, flag); break;          // 0x101e798
        case AntUnit::kActionExtinguish:     end_extinguish(a, flag); break;      // 0x101e97b
        case AntUnit::kActionPlant:          end_plant(a, flag); break;           // 0x101e433
        case AntUnit::kActionDefuse:         end_defuse(a, flag); break;          // 0x101e599
        case AntUnit::kActionBridgeBuild:    end_bridge_build(a, flag); break;    // 0x101eaec
        case AntUnit::kActionBridgeDemolish: end_bridge_demolish(a, flag); break; // 0x101ecdf
        case AntUnit::kActionAttack: { // 0x101ecc4: a pending hit is delivered when its victim still waits for it
            AntUnit* v = find_unit(a.pending_victim);
            if (a.pending_victim != 0 && v && v->engaged) deliver_pending_hit(a);
            break;
        }
        case AntUnit::kActionDeath:    // 0x101e120: leaving the death clip finishes the death at once
            if (a.pending_victim != 0) deliver_pending_hit(a);
            finish_death(a);
            return false;
        case AntUnit::kActionDrown:    // 0x101e0c2
            if (new_action == AntUnit::kActionDrown) return false;
            if (a.pending_victim != 0) deliver_pending_hit(a);
            finish_death(a);
            return false;
        case AntUnit::kActionBlast:    // 0x101ade0: end of a flight or a hit
        case AntUnit::kActionHit:
        case AntUnit::kActionBlown:
            if (new_action == AntUnit::kActionDeath || new_action == AntUnit::kActionDrown ||
                new_action == AntUnit::kActionHit || new_action == AntUnit::kActionBlown ||
                new_action == AntUnit::kActionBlast) {
                break;                 // one flight replaces another without any ending
            }
            if (!flag) {
                const size_t sound_mark = audio_queue_.size();
                const bool r = stun_or_die(a);          // 0x101e68c -> FUN_0102151a(1)
                if (r && a.hp == 0) return false;       // the death started inside: the new action is dropped
                // The stun clip that this cleanup starts is replaced by the new action inside the same SetAction call, and
                // every ant clip tracks its sounds (flag 3): SetAnimation stops them at once, so stun.wav (70) is never heard
                // after a hit, a flight or a pile-up hop. Only a stun that stays (bomb flight, burn) is heard.
                if (!r && audio_queue_.size() > sound_mark) audio_queue_.resize(sound_mark);
            }
            break;
        default:
            break;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// Entering the own hill
// ------------------------------------------------------------------------------------------------

// Message 7 handler FUN_01021494 (constructed by FUN_01022b03 when an order-2 path ends at the entrance): the ant
// plays the enter clip on the entrance tile centre. Nothing else happens now: deposit and healing are done by the
// cleanup of action 2 when the clip's end handler orders the ant out again.
void SimulationEngineImpl::enter_hill(AntUnit& a) {
    if (a.hp == 0) a.hp = 1;                                   // burn survivor
    set_action(a, AntUnit::kActionEnter, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.orig_order = AntUnit::kOrderHome;                        // +0xa8 = 2
    a.waypoints.clear();                                       // ClearPath: +0xac stays the "no target" sentinel
    a.current_waypoint_idx = 0;
    a.orig_order_tile = no_order_tile();
    a.final_dest = TileCoord{-1, -1};
}

// Step callback, actions 2 and 0x14 at the last frame (0x101ef5c): a local ant gets a new order. A carrier of food
// goes back to the food source (+0xf4), everybody else (empty ants, thieves with loot, newborns) to tile42. The
// order's SetActionDefault(0) runs the cleanup of the old action first (deposit, heal).
void SimulationEngineImpl::enter_clip_end(AntUnit& a) {
    const bool to_food = a.is_holding() && !a.is_thief_steal;              // +0xe8 != 0 && +0xec == 0
    TileCoord dst = to_food ? a.harvest_origin : team_tile42(a.player_id);
    if (dst.x < 0 || !grid_.in_bounds(dst)) dst = team_tile42(a.player_id);
    if (dst.x < 0) {                                                        // no hill to go back to
        set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
        set_idle_label(a);
        return;
    }
    go_to(a, dst, false, false);
}

// FUN_0101e165: cleanup of action 2 (and 0x14): the carried food scores, the carrying state is cleared, and a
// wounded ant is healed to full health.
void SimulationEngineImpl::cleanup_enter(AntUnit& a) {
    const uint16_t missing = (a.hp < AntUnit::MAX_HP) ? static_cast<uint16_t>(AntUnit::MAX_HP - a.hp) : uint16_t{0};
    int32_t amount = static_cast<int32_t>(a.carried_points);
    if (amount == 0 && a.carried_food > 0) amount = static_cast<int32_t>(a.carried_food) * 25;
    if (amount > 0) {
        add_score(a.player_id, amount);
        post_news(a.player_id, strings::kScoreGoingUp);
    }
    a.clear_inventory();                                                    // SetCarrying(0, {0,0}): +0xe8, +0xec, +0xf0, +0xf4
    a.is_thief_steal = false;
    a.harvest_origin = TileCoord{-1, -1};
    if (missing > 0) a.hp = AntUnit::MAX_HP;                                // hp += 10 - hp; the marker is re-created
}

// ------------------------------------------------------------------------------------------------
// Waiting ring in front of the hill (ANTHILLQ) and the 1 hp retreat
// ------------------------------------------------------------------------------------------------

// ANTHILLQ (vtable 0x1002540, task added at 0x100e556 with period 200 ms; body FUN_0100ff1f): one queued ant is sent
// in when nobody is on or heading for the entrance. Queue order: the earliest arrival time at the ring tile; ants that
// were ordered by a click or a retreat have time 0 and go first (strictly smaller time wins, the first ant wins ties).
void SimulationEngineImpl::anthillq_run() {
    for (uint8_t team = 0; team < MAX_PLAYERS; ++team) {
        const TileCoord home = team_entrance(team);
        if (home.x < 0) continue;
        AntUnit* best = nullptr;
        bool blocked = false;
        for (auto& up : ants_) {
            AntUnit* a = up.get();
            if (!a || a->player_id != team || !a->is_alive()) continue;
            if ((a->orig_order == AntUnit::kOrderMove || a->orig_order == AntUnit::kOrderHome) && a->orig_order_tile == home) {
                blocked = true;                                             // somebody is heading for the entrance
                break;
            }
            if (pixel_tile(*a) == home) {
                blocked = true;                                             // somebody stands on it (entering, newborn)
                break;
            }
            const uint8_t act = orig_action_of(*a);
            if (act != AntUnit::kActionDeath && act != AntUnit::kActionDrown && a->home_state == 2 &&
                (!best || a->home_time_ms < best->home_time_ms)) {
                best = a;
            }
        }
        if (!blocked && best) {
            go_to(*best, home, false, false);                               // Order(best, &home, 0, 0, 0)
        }
    }
}


// ------------------------------------------------------------------------------------------------
// Hatching (FUN_01010aca / FUN_01010c14 / HATCHTSK 0x1025072)
// ------------------------------------------------------------------------------------------------

// HATCHTSK body, run 8000 ms after the click and then every scheduler slot while an own ant stands on the entrance
// tile: ants that are heading for the entrance are sent to the alternative waiting tile, and the newborn worker
// appears on the entrance tile centre playing the hatch clip.
void SimulationEngineImpl::hatch_run(uint8_t team) {
    HatchState& h = hatch_[team];
    const TileCoord ent = team_entrance(team);
    if (ent.x < 0) { h.active = false; return; }
    for (auto& up : ants_) {
        AntUnit* a = up.get();
        if (a && a->player_id == team && a->is_alive() && pixel_tile(*a) == ent) {
            h.due_ms += kHatchRetryMs;                                      // [task+0x1c]=1000 is a dead store: next slot
            return;
        }
    }
    for (auto& up : ants_) {
        AntUnit* a = up.get();
        if (!a || a->player_id != team || !a->is_alive()) continue;
        if ((a->orig_order == AntUnit::kOrderMove || a->orig_order == AntUnit::kOrderHome) && a->orig_order_tile == ent) {
            const TileCoord ring = team_ring_tile(team);
            if (ring.x >= 0) {
                go_to(*a, ring, false, false);                              // Order(a, &P.tile46)
                a->home_state = 1;
                a->home_priority = 0;
            }
        }
    }
    h.active = false;
    // SpawnAnt (0x100ef18): the newborn appears at the tile centre facing a random direction 1..7, then the hatch
    // clip plays with order 2 and the target set to the entrance (FUN_0101fc24).
    const uint32_t id = next_ant_id_++;
    auto unit = std::make_unique<AntUnit>(id, static_cast<TeamId>(team % 4), h.type, ent.x, ent.y);
    unit->player_id = team;
    AntUnit* n = unit.get();
    ants_.push_back(std::move(unit));
    set_position(*n, centre_x(ent), centre_y(ent));
    const uint8_t dir = static_cast<uint8_t>(prng_.rand() % 7u + 1u);
    set_action(*n, AntUnit::kActionIdle, dir, -1, -1, false);
    set_action(*n, AntUnit::kActionHatch, dir, -1, -1, false);
    n->orig_order = AntUnit::kOrderHome;
    n->orig_order_tile = ent;
    n->final_dest = TileCoord{-1, -1};
    audio_queue_.push_back(AudioEvent{SoundID::ExitHill, centre_x(ent), centre_y(ent), 1, team});
    post_news(team, strings::kHatched);
    world_state_dirty_ = true;
}

// ------------------------------------------------------------------------------------------------
// Thief raid (order 0xb, action 0xd)
// ------------------------------------------------------------------------------------------------

// FUN_0101ccaf case 0xb (0x101d51b) at the end of a raid path. Returns true when the raid started; a refusal has
// already stopped or redirected the ant.
bool SimulationEngineImpl::raid_arrive(AntUnit& a, StepEvt& /*e*/) {
    const uint8_t victim = a.orig_target_team;
    if (victim >= MAX_PLAYERS || victim == a.player_id || stats_.are_allies(a.player_id, victim) ||
        !grid_.find_anthill(victim)) {
        stop_sync(a);
        return false;
    }
    if (a.is_holding()) {                                                   // "Can't - already have food."
        post_news(a.player_id, strings::kAlreadyHaveFood);
        const TileCoord home = team_entrance(a.player_id);
        if (home.x >= 0) go_to(a, home, false, false); else stop_sync(a);
        return false;
    }
    const int32_t score = std::max<int32_t>(0, stats_.get_individual_score(victim));
    start_raid(a, victim, static_cast<uint32_t>(std::min<int32_t>(score, static_cast<int32_t>(kRaidLootMax))));
    return true;
}

// FUN_0102184e (message 0x12): the thief plays atcr501 on the raid tile centre; the loot is fixed now and moves at the
// end of the clip. The owner of the raided hill hears the anthill cue and reads the blinking warning.
void SimulationEngineImpl::start_raid(AntUnit& a, uint8_t victim, uint32_t amount) {
    const auto* hill = grid_.find_anthill(victim);
    if (!hill) { stop_sync(a); return; }
    const TileCoord tile{static_cast<int32_t>(hill->x) + 3, static_cast<int32_t>(hill->y) + 2};
    set_action(a, AntUnit::kActionRaid, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.orig_target_team = victim;                                            // +0xb0
    a.orig_order_tile = tile;                                               // +0xac
    a.orig_order = AntUnit::kOrderRaid;                                     // +0xa8 = 0xb
    a.raid_amount = amount;                                                 // +0xf8
    a.target_team_id = victim;
    a.waypoints.clear();                                                    // ClearPath
    a.current_waypoint_idx = 0;
    a.final_dest = TileCoord{-1, -1};
    set_position(a, centre_x(tile), centre_y(tile));
    audio_queue_.push_back(AudioEvent{SoundID::Anthill, centre_x(tile), centre_y(tile), 2, victim});
    post_news(victim, strings::kThiefAtHill);
}

// Step callback, actions 5 and 0xd at the last frame (0x101efbd): snap to the tile centre, go idle (the cleanup of
// action 0xd moves the loot), and a thief that got food starts for its own entrance.
void SimulationEngineImpl::raid_clip_end(AntUnit& a) {
    const TileCoord t5a = (a.occ_tile.x >= 0) ? a.occ_tile : pixel_tile(a);
    set_position(a, centre_x(t5a), centre_y(t5a));
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    set_idle_label(a);
    if (a.is_holding()) {
        const TileCoord home = team_entrance(a.player_id);
        if (home.x >= 0) go_to(a, home, false, false);
    }
}

// FUN_0101e27f: cleanup of action 0xd. The victim loses the loot, the thief carries it (the loot flag is set even for
// an amount of 0, which leaves the thief empty-handed and idle on the raid tile).
void SimulationEngineImpl::cleanup_raid(AntUnit& a) {
    const uint8_t victim = a.orig_target_team;
    const uint32_t amount = a.raid_amount;
    if (victim < MAX_PLAYERS && amount > 0) add_score(victim, -static_cast<int32_t>(amount));
    a.raid_amount = 0;
    if (amount > 0) a.steal_points(static_cast<uint16_t>(amount));
    a.is_thief_steal = true;                                                // +0xec = 1
    a.target_team_id = 255;
    post_news(a.player_id, strings::kFoodStolen);
}

// ------------------------------------------------------------------------------------------------
// Harvest (order 5, action 5)
// ------------------------------------------------------------------------------------------------

// FUN_0101ac8c SetHolding(amount, tile): the ant carries `amount` points taken at `tile` (or nothing for an amount of 0);
// a thief's loot flag (+0xec) is cleared.
void SimulationEngineImpl::set_holding(AntUnit& a, uint16_t amount, TileCoord tile) {
    a.is_thief_steal = false;
    if (amount != 0) {
        a.holding = 1;
        a.carried_food = 1;
        a.carried_points = amount;
        a.harvest_origin = tile;
    } else {
        a.holding = 0;
        a.carried_food = 0;
        a.carried_points = 0;
        a.harvest_origin = TileCoord{-1, -1};
    }
}

// Message 0xa handler FUN_0102178a: the ant, standing on its approach tile, faces the food's anchor and plays the grab clip
// (?gf: 320-460 ms, cue 66 or 77 at 120-240 ms). The order tile and +0xb4 are the anchor, +0xb0 is the FIRST object of the
// table with that anchor (FUN_01008c63; a later duplicate of the anchor is not the one that is harvested), +0xb8 the amount
// the arrival read from the object it had found by the clicked cell.
void SimulationEngineImpl::start_harvest(AntUnit& a, TileCoord food_tile, uint16_t amount) {
    const TileCoord approach = (a.occ_tile.x >= 0) ? a.occ_tile : TileCoord{a.pixel_x / 32, a.pixel_y / 32};
    const int32_t first = grid_.food_object_first_at(food_tile);
    const uint8_t dir = dir_from_to(approach, food_tile);
    set_action(a, AntUnit::kActionHarvest, dir, -1, -1, false);
    a.orig_order_tile = food_tile;                                // +0xac
    a.orig_food_tile = food_tile;                                 // +0xb4
    a.orig_order = AntUnit::kOrderHarvest;                        // +0xa8 = 5
    a.orig_food_id = first;                                       // +0xb0
    a.harvest_amount = amount;                                    // +0xb8
    a.waypoints.clear();                                          // FUN_0101ab56
    a.current_waypoint_idx = 0;
    set_position(a, approach.x * 32 + 16, approach.y * 32 + 16);
}

// FUN_0101e342, the cleanup of action 5 (every way out of the clip, the cflag is ignored: a hit in the middle of a bite still
// gives the food): one unit is taken from the object (no re-check: two ants that bite the last unit both get food), the ant
// carries the amount of the order (SetHolding), the owner reads "Got Food!" (text 0x3c), and a pile whose tile changed is
// redrawn (SetTile on the anchor: the new stage's cells, or nothing at all for a pile that is gone).
void SimulationEngineImpl::end_harvest(AntUnit& a) {
    const int32_t obj = a.orig_food_id;
    bool changed = false;
    if (obj >= 0) grid_.take_food(obj, 1, changed);
    uint16_t amount = a.harvest_amount;
    if (amount == 0 && obj >= 0 && static_cast<size_t>(obj) < grid_.food_objects().size()) {
        amount = grid_.food_objects()[static_cast<size_t>(obj)].value;
    }
    set_holding(a, amount, a.orig_food_tile);
    post_news(a.player_id, strings::kGotFood);
    if (changed && obj >= 0 && static_cast<size_t>(obj) < grid_.food_objects().size()) {
        const FoodObject& fo = grid_.food_objects()[static_cast<size_t>(obj)];
        grid_.set_food_tile(TileCoord{static_cast<int32_t>(fo.col), static_cast<int32_t>(fo.row)}, fo.stage_tile());
    }
    world_state_dirty_ = true;
}

// Step callback, action 5 at the last frame (0x101f06f): the ant is put on its tile centre and idles (the cleanup above
// gives it the food), its path is cleared, and an ant that now carries food goes home (Order to the entrance, no player flag).
void SimulationEngineImpl::harvest_clip_end(AntUnit& a) {
    const TileCoord t = (a.occ_tile.x >= 0) ? a.occ_tile : TileCoord{a.pixel_x / 32, a.pixel_y / 32};
    set_position(a, t.x * 32 + 16, t.y * 32 + 16);
    set_action(a, AntUnit::kActionIdle, static_cast<uint8_t>(a.facing), -1, -1, false);
    a.waypoints.clear();
    a.current_waypoint_idx = 0;
    if (a.is_holding()) {
        const TileCoord home = team_entrance(a.player_id);
        if (home.x >= 0) go_to(a, home, false, false);
    }
}

} // namespace ants::sim
