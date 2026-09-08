#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include "ants_assets/mirroring.hpp"
#include "ants_sim/grid.hpp"

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
    HarvestingFood    = 26  // Harvesting / grabbing food sequence (Sound 66, 6 ticks)
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

    // Movement Speeds (16.16 fixed-point pixels per tick at 20 Hz)
    static constexpr int32_t SPEED_STANDARD_FX = 262144; // 4.0 px/tick (2.5 tiles/sec)
    static constexpr int32_t SPEED_THIEF_FX    = 367001; // 5.6 px/tick (3.5 tiles/sec)
    static constexpr int32_t SPEED_AQUATIC_FX  = 209715; // 3.2 px/tick (2.0 tiles/sec)
    static constexpr int32_t DIAG_SCALE_FX     = 46341;  // 1 / sqrt(2) in 16.16

    static constexpr uint16_t FLINCH_TICKS     = 4;
    static constexpr uint16_t STUN_TICKS       = 12;

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
    uint16_t    blocked_ticks{0};
    uint16_t    base_dwell_ticks{0};
    bool        is_on_mud{false};
    bool        was_in_water{false};
    bool        in_water{false};

    std::vector<TileCoord> waypoints;
    size_t      current_waypoint_idx{0};
    TileCoord   final_dest{-1, -1};
    TileCoord   harvest_origin{-1, -1};
    bool        is_thief_steal{false};
    bool        had_food_at_base_entry{false};
    bool        underground_visited{false};
    bool        is_newborn{false};
    OrderType   pending_ability{static_cast<OrderType>(0)};
    TileCoord   ability_target{-1, -1};
    uint16_t    attack_cooldown_ticks{0};
    uint32_t    attack_target_id{0};
    bool        allow_friendly_bomb{false};
    uint8_t     pending_powerup_type{255};
    AntType     previous_type{AntType::Worker};
    bool        transformation_interrupted{false};
    bool        on_powerup{false};
    TileCoord   dropped_powerup_pos{-1, -1};

    AntUnit(uint32_t unit_id, TeamId team_in, AntType type_in, int32_t start_tx, int32_t start_ty);

    bool is_alive() const noexcept {
        return hp > 0 && death_status == DeathStatus::Alive;
    }
    bool is_stunned() const noexcept {
        return state == UnitState::Stunned || stun_ticks_remaining > 0;
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

    std::string get_sprite_prefix() const;

    void set_destination(int32_t target_tx, int32_t target_ty);
    void set_path(std::vector<TileCoord> path);
    void clear_path() noexcept {
        waypoints.clear();
        final_dest = TileCoord{-1, -1};
        current_waypoint_idx = 0;
        anim_tick = 0;
        anim_subitem = 0;
    }

    void start_flinch() noexcept {
        state = UnitState::Flinch;
        state_timer = FLINCH_TICKS;
    }

    void start_stun(uint16_t ticks = STUN_TICKS) noexcept {
        state = UnitState::Stunned;
        stun_ticks_remaining = ticks;
    }

    void start_drowning() noexcept {
        state = UnitState::Drowning;
        death_status = DeathStatus::Drowned;
        facing = Direction::South;
        hp = 0;
        anim_subitem = 0;
        anim_tick = 0;
    }

    void tick_movement(bool is_swimming, SurfaceType surface = SurfaceType::Grass);
    void tick_timers() noexcept;
};

} // namespace ants::sim
