#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include "ants_assets/mirroring.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/movement_tables.hpp"

namespace ants::sim {

enum class OrderType : uint8_t;

/**
 * @brief Discrete ant unit types matching original binary type IDs.
 */
enum class AntType : uint8_t {
    Worker  = 0, // General worker ant (ag) - harvesting, general labor
    Bomber  = 1, // Bomber ant (ab) - landmine planting & squash defusal
    Fire    = 2, // Fire ant (af) - magnifying glass ignition & extinguishing
    Thief   = 3, // Thief ant (at) - infiltration, fast raiding, score theft
    Combat  = 4, // Combat ant (ac) - heavy punch (2 HP + knockback), guard AI
    Swimmer = 5  // Swimmer ant (as) - deep water navigation, bridge building
};

/**
 * @brief Primary behavioral state machine states for ant units.
 */
enum class UnitState : uint8_t {
    Idle           = 0,  // Stationary, standing ready (*st*, Action 7)
    Walking        = 1,  // Traversing terrain towards waypoint (*wg* / *ws*, Action 2)
    Attacking      = 2,  // Melee attack wind-up and strike (*at*, Action 1)
    Ability        = 3,  // Executing unit ability
    Flinch         = 4,  // Hit reaction flinch (*gh*, Action 10)
    Knockback      = 5,  // Ballistic airborne flight (*gf*, Action 14)
    Bounce         = 6,  // Ground impact skid and roll (*gb*, Action 19)
    Stunned        = 7,  // Immobilized recovery state (Action 12, 12 ticks)
    EnteringBase   = 8,  // 17-frame base entry sequence (hgen301 / Anim 867)
    QueuingBase    = 9,  // Queued in Chebyshev ring around anthill
    Drowning       = 10, // 22-subitem drowning sequence (*dr301)
    Dead           = 11, // Unit eliminated
    GuardIdle      = 12, // Combat Ant idle at guard post
    Intercepting   = 13, // Combat Ant intercepting detected intruder
    ReturningToPost= 14, // Combat Ant returning to guard post
    Swimming       = 15, // Swimmer Ant actively swimming in water
    Infiltrating   = 16, // Thief Ant diving into enemy base
    DivingInWater  = 17, // Swimmer Ant diving into water (asdi*, Sound 71)
    ExitingWater   = 18, // Swimmer Ant emerging from water onto land (asgo*, Sound 71)
    BuildingBridge = 19, // Swimmer Ant digging / building bridge (asbb*, Action 13)
    DemolishingBridge = 20, // Swimmer Ant digging / demolishing bridge (asdb*, Action 14)
    PlantingBomb      = 21, // Bomber Ant planting bomb (*sb*, Action 8, 17 ticks)
    DefusingBomb      = 22, // Bomber Ant defusing bomb (*db*, Action 9, 12 ticks)
    PlacingFire       = 23, // Fire Ant placing firewall (*sf*, Action 6, 22 ticks)
    ExtinguishingFire = 24, // Fire Ant extinguishing fire (*xf*, Action 7, 12 ticks)
    CantGo            = 25, // Blocked path / impossible order reaction (*cg*, Action 20, Sound 63)
    HarvestingFood    = 26, // Harvesting / grabbing food sequence (Sound 66, 6 ticks)
    Burn              = 27  // Dud burn / bomb smoke blast stagger (*bu301, 11 subitems)
};

/**
 * @brief Returns the duration in simulation ticks of the *cg301 Can't Go animation.
 */
inline constexpr uint16_t get_cant_go_duration(AntType type) noexcept {
    switch (type) {
        case AntType::Worker:  return 6;
        case AntType::Bomber:  return 15;
        case AntType::Fire:    return 9;
        case AntType::Thief:   return 11;
        case AntType::Combat:  return 6;
        case AntType::Swimmer: return 12;
    }
    return 8;
}

/**
 * @brief Unit death classification status.
 * Value 0x0F is the reverse-engineered code for drowning in deep water.
 */
enum class DeathStatus : uint8_t {
    Alive        = 0x00,
    CombatKilled = 0x01,
    BombKilled   = 0x02,
    FireKilled   = 0x03,
    Drowned      = 0x0F  // Authentic code 0x0F from Ants.exe disassembly 0x101b86e
};

/**
 * @brief Damage event source identifier for damage matrix accounting.
 */
enum class DamageSource : uint8_t {
    MeleeStandard = 1, // Standard 1 HP attack
    CombatPunch   = 2, // Combat Ant 2 HP heavy punch
    BombBlast     = 3, // Landmine detonation (2 HP)
    FireBurn      = 7  // Fire wall contact (+1 HP, Disasm 0x01021627)
};

/**
 * @brief Player team faction identifiers.
 */
enum class TeamId : uint8_t {
    Black = 0,
    Blue  = 1,
    Red   = 2,
    Green = 3,
    None  = 4
};

using Direction = ants::assets::Direction;

/**
 * @brief Fixed-point arithmetic constants for 20 Hz movement determinism.
 * 16.16 fixed point: 16 bits integer, 16 bits fractional.
 */
struct FixedPointMath {
    static constexpr int32_t SHIFT = 16;
    static constexpr int32_t ONE   = 1 << SHIFT;
    static constexpr int32_t HALF  = ONE >> 1;

    static constexpr int32_t from_int(int32_t v) noexcept { return v << SHIFT; }
    static constexpr int32_t to_int(int32_t v)   noexcept { return v >> SHIFT; }
    static constexpr int32_t mul(int32_t a, int32_t b) noexcept {
        return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> SHIFT);
    }
    static constexpr int32_t div(int32_t a, int32_t b) noexcept {
        return static_cast<int32_t>((static_cast<int64_t>(a) << SHIFT) / b);
    }
};

/**
 * @brief AntUnit entity representing an active unit on the game grid.
 */
class AntUnit {
public:
    static constexpr uint16_t MAX_HP          = 10;
    static constexpr uint16_t STARTING_HP     = 10;
    static constexpr uint16_t STANDARD_DAMAGE = 1;
    static constexpr uint16_t COMBAT_DAMAGE   = 2;

    // Locomotion has no speed constants: in Ants.exe an ant moves only when a walk-animation frame
    // ends, by that frame's dx/dy from ants.chd Table 4 (see movement_tables.hpp and
    // docs/GAME_REVERSE_ENGINEERING.md, "Movement ground truth").

    // Original-engine "action" ids (CAntUnit +0xe4) for locomotion-managed states.
    static constexpr uint8_t kActionIdle   = 0x00;  // *st* idle animation
    static constexpr uint8_t kActionWalk   = 0x01;  // *w{g,s,m,d}* walk / assw swim / asdi dive / asgo climb
    static constexpr uint8_t kActionCantGo = 0x0B;  // *cg301 "can't" animation (sound 63 on frame 0)
    static constexpr uint8_t kActionNone   = 0xFF;  // state driven by other remake systems (no locomotion clip)

    // Order codes (CAntUnit +0xa8) and the "no target" sentinel tile of +0xac (row 0x5a, col 0x78).
    static constexpr uint8_t kOrderNone    = 0x00;
    static constexpr uint8_t kOrderMove    = 0x01;
    static constexpr uint8_t kOrderHome    = 0x02;
    static constexpr uint8_t kOrderAttack  = 0x03;
    static constexpr uint8_t kOrderPowerUp = 0x04;
    static constexpr uint8_t kOrderHarvest = 0x05;
    static constexpr uint8_t kOrderBomb    = 0x0A;
    static constexpr uint8_t kOrderRaid    = 0x0B;
    static constexpr int32_t kNoOrderTileX = 0x78;
    static constexpr int32_t kNoOrderTileY = 0x5A;

    static constexpr uint16_t FLINCH_TICKS     = 14;
    static constexpr uint16_t STUN_TICKS       = 50; // Authentic 2.5s (50 ticks @ 20Hz, 3-4 star rotations)

    // Entity Public State Fields (directly inspectable & testable)
    uint32_t    id{0};
    uint8_t     player_id{0};
    TeamId      team{TeamId::Black};
    uint8_t     target_team_id{4};
    AntType     type{AntType::Worker};
    UnitState   state{UnitState::Idle};
    DeathStatus death_status{DeathStatus::Alive};

    uint16_t    hp{STARTING_HP};
    uint16_t    max_hp{MAX_HP};

    TileCoord   pos{0, 0};
    TileCoord   guard_anchor{0, 0};
    uint32_t    stun_ticks_remaining{0};

    int32_t     pixel_x{0};
    int32_t     pixel_y{0};
    int32_t     altitude_z{0};
    Direction   facing{Direction::South};

    int32_t     fx_x{0};
    int32_t     fx_y{0};

    uint8_t     holding{0};
    uint16_t    carried_food{0};
    uint16_t    carried_points{0};
    bool        underground{false};

    uint16_t    anim_subitem{0};
    uint16_t    anim_tick{0};
    uint16_t    state_timer{0};
    uint16_t    transform_timer{0};
    uint16_t    base_dwell_ticks{0};
    bool        is_on_mud{false};
    bool        was_in_water{false};
    bool        in_water{false};

    int32_t     push_start_px{0};
    int32_t     push_start_py{0};
    int32_t     push_dest_px{0};
    int32_t     push_dest_py{0};
    uint8_t     push_ticks_total{0};
    uint8_t     push_tick_current{0};
    bool        is_in_scuffle{false};
    uint16_t    scuffle_ticks{0};
    bool        post_bounce_stun{false};
    bool        is_friendly_bump{false};

    std::vector<TileCoord> waypoints;
    size_t      current_waypoint_idx{0};
    TileCoord   final_dest{-1, -1};
    TileCoord   harvest_origin{-1, -1};
    bool        is_thief_steal{false};
    bool        had_food_at_base_entry{false};
    bool        completed_base_deposit{false};
    bool        underground_visited{false};
    bool        is_newborn{false};
    OrderType   pending_ability{static_cast<OrderType>(0)};
    TileCoord   pending_ability_target{-1, -1};
    TileCoord   ability_target{-1, -1};
    uint16_t    attack_cooldown_ticks{0};
    uint16_t    ability_cooldown_ticks{0};
    uint32_t    attack_target_id{0};
    bool        allow_friendly_bomb{false};
    bool        is_food_order{false};
    uint8_t     pending_powerup_type{255};
    AntType     previous_type{AntType::Worker};
    bool        transformation_interrupted{false};
    bool        on_powerup{false};
    uint16_t    powerup_dwell_timer{0};
    bool        cantgo_standing_on_powerup{false};
    TileCoord   dropped_powerup_pos{-1, -1};
    uint16_t    invulnerable_ticks{0};

    // ---- Original-engine locomotion state (Ants.exe sprite animation + CAntUnit fields) ----
    // Waypoints follow the original convention: waypoints[0] is the start tile, waypoints.back() the
    // goal, and current_waypoint_idx starts at 0 (the first ARRIVE advances it to 1).
    struct LocoPlayer {
        movement::MotionClip clip{};   // running animation (frame list, sprite +0x28)
        uint16_t cursor{0};            // 0 = start step pending; otherwise 1-based index of the current frame (+0x2c)
        uint32_t next_ms{0};           // animation-clock time at which the current frame ends (+0x10)
        uint8_t  dir{4};               // direction the clip was chosen for (renderer uses it for mirroring)
        uint32_t serial{0};            // incremented on every play (detects a clip change inside a step callback)
    };
    LocoPlayer  loco{};
    uint8_t     loco_action{kActionNone};   // CAntUnit +0xe4 while locomotion-managed
    bool        dive_flag{false};           // +0x88: swimmer dive-in / climb-out animation running
    bool        pause_active{false};        // +0x60: ANTPAUSE wait (blocked by a moving ant)
    uint32_t    pause_fire_ms{0};
    uint8_t     pause_saved_action{0};
    uint8_t     pause_saved_dir{0};
    uint8_t     orig_order{kOrderNone};     // +0xa8
    TileCoord   orig_order_tile{kNoOrderTileX, kNoOrderTileY}; // +0xac
    uint8_t     orig_target_team{255};      // +0xb0 (attack order)
    uint32_t    orig_target_ant{0};         // +0xb2 (attack order)
    int32_t     orig_food_id{-1};           // +0xb0 (harvest order): food object identity
    TileCoord   orig_food_tile{-1, -1};     // +0xb4 (harvest order): food anchor tile
    uint32_t    move_serial{0};             // bumped by clear_path() to invalidate pending path requests
    TileCoord   occ_tile{-1, -1};           // +0x5a/+0x5c: tile this ant is registered on in the occupancy grid
    bool        arrived_this_tick{false};   // set when the path completed during the current tick

    AntUnit(uint32_t unit_id, TeamId team_in, AntType type_in, int32_t start_tx, int32_t start_ty);

    bool is_alive() const noexcept {
        return hp > 0 && death_status == DeathStatus::Alive;
    }
    bool is_stunned() const noexcept {
        return state == UnitState::Stunned || state == UnitState::Knockback || state == UnitState::Burn || stun_ticks_remaining > 0;
    }
    bool is_holding() const noexcept {
        return holding != 0;
    }
    bool is_underground() const noexcept {
        return underground;
    }
    bool is_transforming() const noexcept {
        return transform_timer > 0;
    }
    bool is_invulnerable() const noexcept {
        return invulnerable_ticks > 0;
    }
    bool is_orderable() const noexcept {
        return is_alive() && state != UnitState::Knockback && state != UnitState::Drowning &&
               state != UnitState::EnteringBase && state != UnitState::Infiltrating && !is_transforming();
    }

    void heal_full() noexcept {
        hp = max_hp;
    }

    bool take_damage(uint16_t amount, DamageSource source, uint32_t attacker_id) noexcept;

    void set_tile_pos(int32_t tx, int32_t ty) noexcept {
        pos.x = tx;
        pos.y = ty;
        pixel_x = tx * 32 + 16;
        pixel_y = ty * 32 + 16;
        fx_x = pixel_x << 16;
        fx_y = pixel_y << 16;
        push_start_px = pixel_x;
        push_start_py = pixel_y;
        push_dest_px = pixel_x;
        push_dest_py = pixel_y;
        push_ticks_total = 0;
        push_tick_current = 0;
    }

    void set_pixel_pos(int32_t px, int32_t py) noexcept {
        pixel_x = px;
        pixel_y = py;
        pos.x = (px >= 0) ? (px / 32) : ((px - 31) / 32);
        pos.y = (py >= 0) ? (py / 32) : ((py - 31) / 32);
        fx_x = px << 16;
        fx_y = py << 16;
    }

    void sync_pixel_from_fx() noexcept {
        pixel_x = fx_x >> 16;
        pixel_y = fx_y >> 16;
        pos.x = (pixel_x >= 0) ? (pixel_x / 32) : ((pixel_x - 31) / 32);
        pos.y = (pixel_y >= 0) ? (pixel_y / 32) : ((pixel_y - 31) / 32);
    }

    void pick_up_food(uint16_t units = 1, uint16_t points = 25) noexcept {
        holding = 1;
        carried_food += units;
        carried_points += points;
    }

    void steal_points(uint16_t points) noexcept {
        holding = 1;
        carried_points = points;
    }

    std::pair<uint16_t, uint16_t> deposit_food() noexcept {
        std::pair<uint16_t, uint16_t> res = {carried_food, carried_points};
        holding = 0;
        carried_food = 0;
        carried_points = 0;
        return res;
    }

    void clear_inventory() noexcept {
        holding = 0;
        carried_food = 0;
        carried_points = 0;
    }
    // Ends the current walk and order like the original SetPath(0) (FUN_0101ab87): no waypoints, order 0,
    // no target tile, and any path request still queued for the ant is dropped when it arrives.
    void clear_path() noexcept {
        waypoints.clear();
        final_dest = TileCoord{-1, -1};
        current_waypoint_idx = 0;
        anim_tick = 0;
        anim_subitem = 0;
        orig_order = kOrderNone;
        orig_order_tile = TileCoord{kNoOrderTileX, kNoOrderTileY};
        ++move_serial;
    }
    bool has_order_tile() const noexcept {
        return static_cast<uint16_t>(orig_order_tile.y) < static_cast<uint16_t>(kNoOrderTileY);
    }

    void start_flinch(uint16_t ticks = FLINCH_TICKS) noexcept {
        state = UnitState::Flinch;
        state_timer = ticks;
        anim_tick = 0;
        anim_subitem = 0;
    }

    void start_stun(uint16_t ticks = STUN_TICKS) noexcept {
        state = UnitState::Stunned;
        stun_ticks_remaining = ticks;
        anim_tick = 0;
        anim_subitem = 0;
    }

    void start_drowning() noexcept {
        state = UnitState::Drowning;
        death_status = DeathStatus::Drowned;
        facing = Direction::South;
        hp = 0;
        anim_subitem = 0;
        anim_tick = 0;
    }

    void tick_timers() noexcept;
};

} // namespace ants::sim
