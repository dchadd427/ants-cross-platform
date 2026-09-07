# Milestone 2: Units, Combat AI & Physics Specification and Header Design

**Document:** `combat_and_physics_plan.md`  
**Author:** M2 Explorer 2 (`explorer_m2_2`)  
**Target Milestone:** M2 (`ants-sim`)  
**Scope:** Complete C++ Header Designs and Algorithmic Specifications for Ant Units, Combat Ant Guard AI, and Ballistic Physics Engine  
**Authoritative References:**  
- `ORIGINAL_REQUEST.md` (2026-09-06T22:30:38Z, Clarifications 22:34:55Z, 22:37:34Z)
- `GAME_REVERSE_ENGINEERING.md` (Sections 2.3, 5.1, 5.2, 5.7, 5.8, 5.10, 5.12)
- `survey_sim.md` (Sections 1, 2, 3, 6, 7)
- `PROJECT.md` (Interface Contracts and Code Layout)

---

## Table of Contents
1. [Executive Summary & Architectural Overview](#1-executive-summary--architectural-overview)
2. [C++ Public Header: `include/ants_sim/ant_unit.hpp`](#2-c-public-header-includeants_simant_unithpp)
3. [C++ Public Header: `include/ants_sim/combat_ai.hpp`](#3-c-public-header-includeants_simcombat_aihpp)
4. [C++ Public Header: `include/ants_sim/physics.hpp`](#4-c-public-header-includeants_simphysicshpp)
5. [Exact Algorithms & Mathematical Models](#5-exact-algorithms--mathematical-models)
   - 5.1 [Fixed-Point 20 Hz Discrete Movement & Speeds](#51-fixed-point-20-hz-discrete-movement--speeds)
   - 5.2 [Combat Ant Autonomous Guard AI State Machine](#52-combat-ant-autonomous-guard-ai-state-machine)
   - 5.3 [Ballistic Parabolic Knockback & Obstacle Raycasting](#53-ballistic-parabolic-knockback--obstacle-raycasting)
   - 5.4 [Deep Water Landing & 22-Subitem Drowning Sequence](#54-deep-water-landing--22-subitem-drowning-sequence)
   - 5.5 [Fire Contact Ricochet Dynamics & Multi-Fire Chains](#55-fire-contact-ricochet-dynamics--multi-fire-chains)
   - 5.6 [12-Tick Stun Recovery State Machine](#56-12-tick-stun-recovery-state-machine)
6. [Data Structures & Enum Reference](#6-data-structures--enum-reference)
7. [Integration & Verification Strategy](#7-integration--verification-strategy)

---

## 1. Executive Summary & Architectural Overview

Milestone 2 (`ants-sim`) implements the headless, deterministic, tick-based simulation core of Ants. This document establishes the complete, production-ready C++17 design for three critical subsystems:
1. **Unit Entity System (`ant_unit.hpp`)**: Encapsulates the 6 ant classes (`Worker`, `Bomber`, `Fire`, `Thief`, `Combat`, `Swimmer`), universal 10 HP tracking, 20 Hz movement state machine with integer/fixed-point sub-pixel spatial coordinates, 8-directional facings, and carried food/points holding state suites (`ht*` / `h*`).
2. **Combat Ant Autonomous Guard AI (`combat_ai.hpp`)**: Implements the **sole autonomous unit behavior** in Ants: anchor post retention `(x_g, y_g)`, continuous 3-tile Chebyshev aggro scanning (`max(|dx|, |dy|) <= 3`), ally/underground target filtering, autonomous pursuit, 2 HP heavy punch delivery with 4–5 tile ballistic knockback, and automatic return-to-post disengagement.
3. **Ballistic Physics & Collision Engine (`physics.hpp`)**: Implements deterministic 3D parabolic trajectory arcs for heavy punch (4–5 tiles) and bomb detonation (2–3 tiles), obstacle raycast collisions terminating flight, water landing detection causing instant drowning (`death_status = 0xF`, HP = 0, Sound 71/72, rising bubbles) for non-swimmers while Swimmers survive unharmed, +1 fire contact ricochet physics with reflection and chaining, and 12-tick (600 ms) stun recovery.

All math is strictly integer and fixed-point (zero floating-point operations in the simulation step), guaranteeing 100% deterministic bitwise execution across macOS, Linux, and Windows.

---

## 2. C++ Public Header: `include/ants_sim/ant_unit.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include "ants_assets/mirroring.hpp"

namespace ants::sim {

/**
 * @brief Discrete ant unit types matching original binary type IDs.
 * Reference: survey_sim.md Section 2.1, GAME_REVERSE_ENGINEERING.md Section 2.3
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
    Idle          = 0, // Stationary, standing ready (*st*, Action 7)
    Walking       = 1, // Traversing terrain towards waypoint (*wg* / *ws*, Action 2)
    Attacking     = 2, // Melee attack wind-up and strike (*at*, Action 1)
    Ability       = 3, // Executing unit ability (plant, defuse, fire, extinguish, bridge, dive)
    Flinch        = 4, // Hit reaction flinch (*gh*, Action 10, 4 ticks)
    Knockback     = 5, // Ballistic airborne flight (*gf*, Action 14)
    Bounce        = 6, // Ground impact skid and roll (*gb*, Action 19)
    Stunned       = 7, // Immobilized recovery state (Action 12, 12 ticks)
    EnteringBase  = 8, // 17-frame base entry sequence (hgen301 / Anim 867)
    QueuingBase   = 9, // Queued in Chebyshev ring around anthill
    Drowning      = 10,// 22-subitem drowning sequence (*dr301)
    Dead          = 11 // Unit eliminated; marked for removal or corpse
};

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

// Aliasing Direction from ants_assets for direct cross-tier compatibility
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
    // Core Health Metrics
    static constexpr uint16_t MAX_HP          = 10; // Universal 10 HP cap
    static constexpr uint16_t STARTING_HP     = 10; // Universal spawn HP
    static constexpr uint16_t STANDARD_DAMAGE = 1;  // Universal 1 HP standard melee
    static constexpr uint16_t COMBAT_DAMAGE   = 2;  // Combat Ant 2 HP heavy strike

    // Standard Movement Speeds at 20 Hz (pixels per tick in 16.16 fixed point)
    // 1 tile = 32 pixels. 20 ticks = 1 second.
    // Standard: 2.5 tiles/sec = 80 px/sec = 4.0 px/tick -> 4 * 65536 = 262144
    // Thief:    3.5 tiles/sec = 112 px/sec = 5.6 px/tick -> 5.6 * 65536 = 367001
    // Swimmer:  2.0 tiles/sec = 64 px/sec = 3.2 px/tick -> 3.2 * 65536 = 209715
    static constexpr int32_t SPEED_STANDARD_FX = 262144; // 4.0 px/tick
    static constexpr int32_t SPEED_THIEF_FX    = 367001; // 5.6 px/tick
    static constexpr int32_t SPEED_AQUATIC_FX  = 209715; // 3.2 px/tick
    
    // Diagonal speed attenuation factor (1 / sqrt(2) ≈ 0.70710678)
    // In 16.16 fixed-point: 0.70710678 * 65536 = 46341
    static constexpr int32_t DIAG_SCALE_FX     = 46341;

    // Timers (in 20 Hz ticks)
    static constexpr uint16_t FLINCH_TICKS     = 4;  // 200 ms flinch stagger
    static constexpr uint16_t STUN_TICKS       = 12; // 600 ms stun immobilization

public:
    AntUnit(uint32_t unit_id, TeamId team, AntType type, int32_t start_tile_x, int32_t start_tile_y);

    // Getters & Identity
    uint32_t    id() const noexcept        { return m_id; }
    TeamId      team() const noexcept      { return m_team; }
    AntType     type() const noexcept      { return m_type; }
    UnitState   state() const noexcept     { return m_state; }
    DeathStatus death_status() const noexcept { return m_death_status; }
    bool        is_alive() const noexcept  { return m_hp > 0 && m_death_status == DeathStatus::Alive; }
    bool        is_stunned() const noexcept{ return m_state == UnitState::Stunned || m_stun_timer > 0; }
    bool        is_underground() const noexcept { return m_underground; }

    // Health
    uint16_t    hp() const noexcept        { return m_hp; }
    void        heal_full() noexcept;
    bool        take_damage(uint16_t amount, DamageSource source, uint32_t attacker_id) noexcept;

    // Spatial Position (Grid & World Pixels)
    int32_t     tile_x() const noexcept    { return m_pixel_x / 32; }
    int32_t     tile_y() const noexcept    { return m_pixel_y / 32; }
    int32_t     pixel_x() const noexcept   { return m_pixel_x; }
    int32_t     pixel_y() const noexcept   { return m_pixel_y; }
    int32_t     altitude_z() const noexcept{ return m_altitude_z; }
    Direction   facing() const noexcept    { return m_facing; }

    void        set_tile_pos(int32_t tx, int32_t ty) noexcept;
    void        set_pixel_pos(int32_t px, int32_t py) noexcept;
    void        set_altitude_z(int32_t z) noexcept { m_altitude_z = z; }
    void        set_facing(Direction dir) noexcept { m_facing = dir; }

    // Carried Items & Inventory
    bool        is_holding() const noexcept        { return m_holding; }
    uint16_t    carried_food() const noexcept      { return m_carried_food; }
    uint16_t    carried_points() const noexcept    { return m_carried_points; }
    void        pick_up_food(uint16_t units = 1) noexcept;
    void        steal_points(uint16_t points) noexcept;
    std::pair<uint16_t, uint16_t> deposit_food() noexcept;
    void        clear_inventory() noexcept;

    // Visual Animation Prefix Suite Resolver
    // Returns 2-character prefix: "ag", "ab", "af", "at", "ac", "as" (empty-handed)
    // or "hg", "hb", "hf", "ht", "hc", "hs" (holding lunchbox)
    std::string get_sprite_prefix() const;

    // Movement & Waypoints
    void        set_destination(int32_t target_tx, int32_t target_ty);
    void        clear_path() noexcept;
    bool        has_path() const noexcept  { return !m_waypoints.empty(); }
    void        set_waypoints(std::vector<std::pair<int32_t, int32_t>> wps);

    // State Transitions
    void        transition_to(UnitState new_state) noexcept;
    void        start_flinch() noexcept;
    void        start_stun(uint16_t ticks = STUN_TICKS) noexcept;
    void        start_drowning() noexcept;
    void        set_underground(bool underground) noexcept { m_underground = underground; }

    // Simulation Tick Update
    void        tick_movement(bool is_swimming);
    void        tick_timers() noexcept;

    // Animation Subitem Tracking
    uint16_t    anim_subitem() const noexcept { return m_anim_subitem; }
    uint16_t    anim_tick() const noexcept    { return m_anim_tick; }
    void        advance_anim() noexcept;
    void        reset_anim() noexcept;

private:
    uint32_t    m_id;
    TeamId      m_team;
    AntType     m_type;
    UnitState   m_state{UnitState::Idle};
    DeathStatus m_death_status{DeathStatus::Alive};

    uint16_t    m_hp{STARTING_HP};

    // 16.16 Fixed-Point Coordinates
    int32_t     m_fx_x{0};
    int32_t     m_fx_y{0};

    // Integer Pixel Coordinates (cached from fixed-point)
    int32_t     m_pixel_x{0};
    int32_t     m_pixel_y{0};
    int32_t     m_altitude_z{0}; // Height above terrain (pixels)
    Direction   m_facing{Direction::South};

    // Inventory & Holding Suite
    bool        m_holding{false};
    uint16_t    m_carried_food{0};
    uint16_t    m_carried_points{0};
    bool        m_underground{false};

    // Timers
    uint16_t    m_state_timer{0};
    uint16_t    m_stun_timer{0};

    // Animation
    uint16_t    m_anim_subitem{0};
    uint16_t    m_anim_tick{0};

    // Navigation Waypoints (tile coordinates)
    std::vector<std::pair<int32_t, int32_t>> m_waypoints;
    size_t      m_current_waypoint_idx{0};
};

} // namespace ants::sim
```

---

## 3. C++ Public Header: `include/ants_sim/combat_ai.hpp`

```cpp
#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "ant_unit.hpp"

namespace ants::sim {

// Forward declarations
class Grid;
struct AudioEvent;

/**
 * @brief Autonomous Guard AI state machine states for Combat Ants.
 * Combat Ants are the SOLE unit type with autonomous AI in Ants.
 * Reference: survey_sim.md Section 3, GAME_REVERSE_ENGINEERING.md Section 5.10
 */
enum class CombatGuardState : uint8_t {
    Idle         = 0, // Guarding anchor post (x_g, y_g); scanning 3-tile Chebyshev radius
    Intercepting = 1, // Autonomously pathfinding towards detected intruder
    Striking     = 2, // Melee contact reached; delivering heavy punch (Sound 78, 2 HP)
    Returning    = 3  // Disengaged; pathfinding back to guard_anchor (x_g, y_g)
};

/**
 * @brief Combat Ant Autonomous Guard Controller.
 * Manages guard anchor, scan perimeter, target selection, intercept, and disengagement.
 */
class CombatAIController {
public:
    // Autonomous Guard Constants
    static constexpr int32_t AGGRO_RADIUS_CHEBYSHEV = 3; // 7x7 square scan perimeter
    static constexpr int32_t PURSUIT_DISENGAGE_RADIUS = 5; // Disengage if enemy flees beyond 5 tiles
    static constexpr uint16_t ATTACK_ANIM_TICKS = 6;    // 6-frame punch animation (acat301)
    static constexpr uint16_t STRIKE_IMPACT_FRAME = 2;  // Frame 2 triggers punch impact & knockback

public:
    explicit CombatAIController(AntUnit& owner);

    // AI Cycle State
    CombatGuardState guard_state() const noexcept { return m_guard_state; }
    int32_t          anchor_tile_x() const noexcept { return m_anchor_tx; }
    int32_t          anchor_tile_y() const noexcept { return m_anchor_ty; }
    std::optional<uint32_t> target_id() const noexcept { return m_target_unit_id; }

    // Anchor Management
    void commit_guard_anchor() noexcept;
    void set_guard_anchor(int32_t tx, int32_t ty) noexcept;

    // Autonomous Execution Tick
    // Evaluates every 20 Hz simulation tick when owner ant is not performing user-ordered actions.
    void update(const std::vector<AntUnit*>& all_units,
                const Grid& grid,
                std::vector<AudioEvent>& audio_out,
                uint32_t random_seed);

    // Target Filtering
    // Returns true if candidate is a valid target (enemy, not allied, alive, not underground)
    bool is_valid_target(const AntUnit& candidate, const AntUnit& owner) const noexcept;

    // Reset / Order Interruption
    void on_user_command_issued() noexcept;

private:
    // Internal State Machine Handlers
    void update_guard_idle(const std::vector<AntUnit*>& all_units, const Grid& grid);
    void update_intercepting(const std::vector<AntUnit*>& all_units,
                             const Grid& grid,
                             std::vector<AudioEvent>& audio_out,
                             uint32_t random_seed);
    void update_striking(const std::vector<AntUnit*>& all_units,
                         std::vector<AudioEvent>& audio_out,
                         uint32_t random_seed);
    void update_returning(const Grid& grid);

    AntUnit* find_best_target(const std::vector<AntUnit*>& all_units) const;

private:
    AntUnit&                m_owner;
    CombatGuardState        m_guard_state{CombatGuardState::Idle};

    // Guard Post Anchor Coordinates (Tile)
    int32_t                 m_anchor_tx{0};
    int32_t                 m_anchor_ty{0};

    // Current Pursuit Target
    std::optional<uint32_t> m_target_unit_id{std::nullopt};
    uint16_t                m_strike_timer{0};
};

} // namespace ants::sim
```

---

## 4. C++ Public Header: `include/ants_sim/physics.hpp`

```cpp
#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "ant_unit.hpp"

namespace ants::sim {

// Forward declarations
class Grid;
struct AudioEvent;

/**
 * @brief Active ballistic projectile / knockback state for an ant in flight.
 */
struct BallisticFlight {
    uint32_t unit_id;
    int32_t  start_px;
    int32_t  start_py;
    int32_t  target_px;
    int32_t  target_py;
    int32_t  total_distance_px;
    
    // Discrete Time Steps
    uint16_t current_tick{0};
    uint16_t total_ticks{10}; // Typically 8-10 ticks for 4-5 tiles (400-500 ms)
    
    // Parabolic Height Trajectory Peak (pixels)
    int32_t  apex_height_px{36};

    // Heading of Knockback
    Direction flight_dir{Direction::South};
    DamageSource origin_source{DamageSource::CombatPunch};
};

/**
 * @brief Physics and Physical Interaction Simulation Subsystem.
 * Manages ballistic knockback arcs, obstacle collisions, water drowning, fire ricochets, and stun.
 * Reference: survey_sim.md Sections 2.3, 6, 7; GAME_REVERSE_ENGINEERING.md Sections 5.7, 5.12
 */
class PhysicsEngine {
public:
    // Sound FX Identifiers from ants.chd Table 2
    static constexpr uint32_t SOUND_BOMB_EXP     = 4;  // bombexp.wav (Landmine blast)
    static constexpr uint32_t SOUND_FLY_THUMP_A  = 64; // flythumpa.wav (Flight launch/impact)
    static constexpr uint32_t SOUND_FLY_THUMP_B  = 65; // flythumpb.wav (Flight secondary impact)
    static constexpr uint32_t SOUND_STUN         = 70; // stun.wav (Stun state entry)
    static constexpr uint32_t SOUND_SPLASH       = 71; // splash.wav (Water plunge)
    static constexpr uint32_t SOUND_DROWN        = 72; // antdrown.wav (Drowning scream)
    static constexpr uint32_t SOUND_ATTACK_COMBAT= 78; // attack2.wav (Combat heavy punch)

    // Ballistic Distance Ranges (in tiles)
    static constexpr int32_t COMBAT_PUNCH_MIN_TILES = 4;
    static constexpr int32_t COMBAT_PUNCH_MAX_TILES = 5;
    static constexpr int32_t BOMB_BLAST_MIN_TILES   = 2;
    static constexpr int32_t BOMB_BLAST_MAX_TILES   = 3;

    // Stun Duration
    static constexpr uint16_t STUN_RECOVERY_TICKS   = 12; // 12 ticks = 600 ms

public:
    PhysicsEngine() = default;

    /**
     * @brief Initiates ballistic knockback on a target ant.
     * @param victim Target unit to launch.
     * @param from_px Origin pixel X (attacker or bomb center).
     * @param from_py Origin pixel Y (attacker or bomb center).
     * @param min_tiles Minimum knockback distance in tiles.
     * @param max_tiles Maximum knockback distance in tiles.
     * @param source Cause of knockback (CombatPunch or BombBlast).
     * @param audio_out Vector receiving audio trigger events.
     * @param random_val Deterministic PRNG value for distance selection.
     */
    void apply_knockback(AntUnit& victim,
                         int32_t from_px,
                         int32_t from_py,
                         int32_t min_tiles,
                         int32_t max_tiles,
                         DamageSource source,
                         std::vector<AudioEvent>& audio_out,
                         uint16_t random_val);

    /**
     * @brief Steps all active ballistic trajectories and environmental interactions by 1 tick (50 ms).
     */
    void tick(std::vector<AntUnit*>& all_units,
              Grid& grid,
              std::vector<AudioEvent>& audio_out,
              uint16_t (*prng_func)());

    /**
     * @brief Handles landing resolution when flight terminates.
     * Checks for water drowning, fire ricochet, obstacle rebound, or normal ground bounce & stun.
     */
    void resolve_landing(AntUnit& unit,
                         Grid& grid,
                         std::vector<AudioEvent>& audio_out,
                         uint16_t (*prng_func)());

    /**
     * @brief Resolves water landing or bridge collapse for a unit.
     * Non-swimmers drown instantly (HP=0, death_status=0xF, Sound 71/72).
     * Swimmer ants plunge unharmed into swimming state (Sound 71).
     */
    void resolve_water_entry(AntUnit& unit,
                             std::vector<AudioEvent>& audio_out);

    /**
     * @brief Resolves contact with an active firewall tile (tile 134 / wallup04).
     * Non-fire ants take +1 fire damage and ricochet away; fire is never extinguished.
     */
    void resolve_fire_contact(AntUnit& unit,
                              Grid& grid,
                              std::vector<AudioEvent>& audio_out,
                              uint16_t (*prng_func)());

    /**
     * @brief Returns active flight record if unit is currently airborne.
     */
    const BallisticFlight* get_active_flight(uint32_t unit_id) const noexcept;

private:
    std::vector<BallisticFlight> m_active_flights;
};

} // namespace ants::sim
```

---

## 5. Exact Algorithms & Mathematical Models

### 5.1 Fixed-Point 20 Hz Discrete Movement & Speeds

All position calculations occur in **16.16 signed fixed-point arithmetic**:
$$\text{px} = \text{fx\_x} \gg 16, \quad \text{py} = \text{fx\_y} \gg 16$$
$$\text{tx} = \text{px} / 32, \quad \text{ty} = \text{py} / 32$$

#### Speed Lookup Table (20 Hz Tick Step)

| Ant Type | Condition | Tiles/sec | Pixels/sec | Pixels/tick (Fixed 16.16) | Hex Value |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Worker / Bomber / Fire / Combat** | Ground | 2.5 | 80.0 | 4.0 (`4 << 16`) | `0x00040000` |
| **Thief Ant** | Scout Boost | 3.5 | 112.0 | 5.6 (`5.6 * 65536`) | `0x0005999A` |
| **Swimmer Ant** | Deep Water | 2.0 | 64.0 | 3.2 (`3.2 * 65536`) | `0x00033333` |
| **Swimmer Ant** | Ground | 2.5 | 80.0 | 4.0 (`4 << 16`) | `0x00040000` |

#### Isotropic Diagonal Scaling
When moving along a diagonal vector ($\Delta x \ne 0 \land \Delta y \ne 0$), linear step size must be scaled by $1/\sqrt{2}$ to prevent the classic "diagonal speed boost" bug:
$$\text{step\_diag} = \frac{\text{speed\_fx} \times 46341}{65536}$$
- Cardinal tick step: $(\Delta x, 0) \implies |\vec{v}| = 4.0\text{ px}$.
- Diagonal tick step: $(\Delta x, \Delta y) \implies \Delta x = \Delta y = 2.8284\text{ px} \implies |\vec{v}| = \sqrt{2.8284^2 + 2.8284^2} = 4.0\text{ px}$.

#### Discrete Waypoint Stepping Algorithm
```cpp
void AntUnit::tick_movement(bool is_swimming) {
    if (m_state != UnitState::Walking || m_waypoints.empty()) return;
    if (m_current_waypoint_idx >= m_waypoints.size()) {
        transition_to(UnitState::Idle);
        m_waypoints.clear();
        return;
    }

    auto [dest_tx, dest_ty] = m_waypoints[m_current_waypoint_idx];
    int32_t dest_px = dest_tx * 32 + 16; // Target cell center
    int32_t dest_py = dest_ty * 32 + 16;

    int32_t cur_px = m_fx_x >> 16;
    int32_t cur_py = m_fx_y >> 16;

    int32_t dx = dest_px - cur_px;
    int32_t dy = dest_py - cur_py;

    // Check waypoint arrival threshold (within 2 pixels)
    if (std::abs(dx) <= 2 && std::abs(dy) <= 2) {
        m_fx_x = dest_px << 16;
        m_fx_y = dest_py << 16;
        m_pixel_x = dest_px;
        m_pixel_y = dest_py;
        m_current_waypoint_idx++;
        if (m_current_waypoint_idx >= m_waypoints.size()) {
            transition_to(UnitState::Idle);
            m_waypoints.clear();
        }
        return;
    }

    // Determine 8-way direction
    m_facing = ants::assets::vector_to_direction(dx, dy);

    // Select speed
    int32_t speed_fx = SPEED_STANDARD_FX;
    if (m_type == AntType::Thief) {
        speed_fx = SPEED_THIEF_FX;
    } else if (m_type == AntType::Swimmer && is_swimming) {
        speed_fx = SPEED_AQUATIC_FX;
    }

    // Apply diagonal scaling if moving diagonally
    bool is_diagonal = (dx != 0 && dy != 0);
    int32_t step_fx = is_diagonal ? FixedPointMath::mul(speed_fx, DIAG_SCALE_FX) : speed_fx;

    // Advance along X
    if (dx > 0) m_fx_x += std::min(step_fx, dx << 16);
    else if (dx < 0) m_fx_x -= std::min(step_fx, (-dx) << 16);

    // Advance along Y
    if (dy > 0) m_fx_y += std::min(step_fx, dy << 16);
    else if (dy < 0) m_fx_y -= std::min(step_fx, (-dy) << 16);

    m_pixel_x = m_fx_x >> 16;
    m_pixel_y = m_fx_y >> 16;
}
```

---

### 5.2 Combat Ant Autonomous Guard AI State Machine

Combat Ants (`ac`) follow an autonomous 4-state lifecycle cycle:

```
                  +-----------------------------------+
                  |                                   |
                  v                                   |
           [ GuardIdle ] <---+ (Return reached)       |
            (Anchor post)    |                        |
                  |          |                        |
       Intruder   |          |                        |
       in 3-tile  v          |                        |
         [ Intercepting ]    |                        |
         (Chase target)      |                        |
                  |          |                        |
       Melee dist |          | Target escapes/dies    |
         <= 1     v          |                        |
            [ Striking ] ----+                        |
         (2 HP + Knock)      |                        |
                  |          |                        |
            Punch |          |                        |
            done  v          |                        |
           [ Returning ] ----+------------------------+
         (Path to post)
```

#### 1. Chebyshev Metric & Proximity Scan
The Chebyshev metric evaluates square tile distance:
$$D_{\text{Chebyshev}} = \max(|x_e - x_g|, |y_e - y_g|)$$
An intruder is within the aggro perimeter if:
$$D_{\text{Chebyshev}} \le 3 \implies 7 \times 7 \text{ tile area centered at } (x_g, y_g)$$

#### 2. Target Filtering Predicate
```cpp
bool CombatAIController::is_valid_target(const AntUnit& candidate, const AntUnit& owner) const noexcept {
    // 1. Must not be self
    if (candidate.id() == owner.id()) return false;

    // 2. Must be alive and not already dying
    if (!candidate.is_alive() || candidate.death_status() != DeathStatus::Alive) return false;

    // 3. Must be an enemy faction (not friendly team, not allied)
    if (candidate.team() == owner.team()) return false;
    // (Alliance check: if candidate.team() == owner.ally_team(), return false)

    // 4. Must not be submerged underground (e.g. Thief digging or unit healing)
    if (candidate.is_underground() || candidate.state() == UnitState::EnteringBase) return false;

    // 5. Must not be airborne in knockback flight
    if (candidate.state() == UnitState::Knockback) return false;

    return true;
}
```

#### 3. Intercept and Melee Strike Delivery
- While in `CombatGuardState::Intercepting`:
  - If target distance $D_{\text{Chebyshev}} \le 1$:
    - Halt movement.
    - Set facing direction towards target.
    - Transition to `CombatGuardState::Striking`.
    - Owner ant sets state to `UnitState::Attacking`.
    - Start 6-tick strike timer (`ATTACK_ANIM_TICKS = 6`).
- At `m_strike_timer == STRIKE_IMPACT_FRAME` (Subitem 2 of `acat301`):
  - Dispatch **Sound 78 (`attack2.wav`)**.
  - Deal **2 HP damage** to target:
    ```cpp
    target->take_damage(2, DamageSource::CombatPunch, owner.id());
    ```
  - Roll knockback distance:
    $$\text{knockback\_tiles} = 4 + (\text{random\_seed} \ \& \ 1) \quad (4 \text{ or } 5 \text{ tiles})$$
  - Initiate ballistic knockback via `PhysicsEngine::apply_knockback`.
- Upon punch completion (`m_strike_timer == 0`):
  - Transition to `CombatGuardState::Returning`.
  - Set navigation path back to `(m_anchor_tx, m_anchor_ty)`.

---

### 5.3 Ballistic Parabolic Knockback & Obstacle Raycasting

#### 1. Parabolic Flight Trajectory Formula
During knockback, horizontal position interpolates linearly while vertical altitude $z(t)$ follows a pure integer parabolic arc over $T$ ticks ($T = 10$ ticks = 500 ms):
$$t \in [0, T]$$
$$x(t) = x_{\text{start}} + \frac{(x_{\text{target}} - x_{\text{start}}) \times t}{T}$$
$$y(t) = y_{\text{start}} + \frac{(y_{\text{target}} - y_{\text{start}}) \times t}{T}$$
$$z(t) = \frac{4 \times H_{\text{apex}} \times t \times (T - t)}{T^2}$$
where $H_{\text{apex}} = 36$ pixels peak altitude.

At $t = 0 \implies z = 0$.  
At $t = T/2 = 5 \implies z = \frac{4 \times 36 \times 5 \times 5}{100} = 36\text{ pixels}$.  
At $t = T = 10 \implies z = 0\text{ (impact)}$.

#### 2. Obstacle Raycasting & Collision Termination
On each flight tick, the discrete cell at $(x(t), y(t))$ is evaluated:
```cpp
int32_t check_tx = (cur_px) / 32;
int32_t check_ty = (cur_py) / 32;

if (!grid.is_in_bounds(check_tx, check_ty) || grid.is_solid_obstacle(check_tx, check_ty)) {
    // Collision! Flight terminates immediately at impact point
    flight.current_tick = flight.total_ticks; // End flight
    victim.set_altitude_z(0);
    audio_out.push_back({SOUND_FLY_THUMP_A, cur_px, cur_py, 1});
    resolve_landing(victim, grid, audio_out, prng_func);
    return;
}
```

---

### 5.4 Deep Water Landing & 22-Subitem Drowning Sequence

When ballistic flight concludes on a deep water tile (or when a 180-second bridge collapses under an ant):

#### Case A: Non-Swimmer Ant (`Worker`, `Bomber`, `Fire`, `Thief`, `Combat`)
- **Instant Fatal Drowning:**
  - `death_status = DeathStatus::Drowned` (`0x0F`).
  - `hp = 0`.
  - State becomes `UnitState::Drowning`.
- **Audio Sequence:**
  - Subitem 0: Sound 71 (`splash.wav`) triggered.
  - Subitem 1: Sound 72 (`antdrown.wav`) drowning scream triggered.
- **Visual Animation Sequences (22 Subitems):**
  - Worker: `agdr301` (Animation 1134)
  - Fire: `afdr301` (Animation 755)
  - Bomber: `abdr301` (Animation 804)
  - Combat: `acdr301` (Animation 941)
  - Thief: `atdr301` (Animation 1130)
- **Subitem Breakdown:**
  - `Subitem 0`: Initial plunge plume (`9splas04.bmp`, Sprite 121), Sound 71.
  - `Subitem 1`: Flailing in splash ring (`9splas05.bmp`, Sprite 122), Sound 72.
  - `Subitems 2–5`: Ant pulled underwater (`9splas06..09.bmp`).
  - `Subitems 6–21`: Rising air bubbles & surface foam (`9bub1..3b.bmp`, Sprites 1223..1227).
- **Deallocation:**
  - At Subitem 21, the entity is removed from active grid tracking.
  - If carrying food/lunchbox, the lunchbox is dropped into water and destroyed.

#### Case B: Swimmer Ant (`AntType::Swimmer`)
- Complete aquatic immunity:
  - Takes **0 damage**.
  - Triggers Sound 71 (`splash.wav`).
  - Smoothly transitions to swimming mode (`UnitState::Walking` with aquatic animation `assw*`).

---

### 5.5 Fire Contact Ricochet Dynamics & Multi-Fire Chains

Contact with an active firewall (`wallup04`, tile 134) is governed by 4 strict rules:

1. **Fire Ant Immunity:** Fire Ants (`af`) are 100% immune to fire. They land or walk through fire unharmed.
2. **Cumulative Damage (+1 Fire Damage):** Non-fire ants taking fire contact immediately suffer **1 HP fire damage** (`DamageSource::FireBurn`, source 7):
   $$\text{HP} \gets \text{HP} - 1$$
   If $\text{HP} == 0$, the ant dies immediately of fire burns.
3. **Non-Occupancy & Reflection Trajectory:**
   Non-fire ants cannot occupy fire cells. They deflect backwards:
   $$\text{bounce\_dir} = (\text{incoming\_dir} + 4 + (\text{sim\_rand}() \pmod 3 - 1) + 8) \pmod 8$$
   The unit is propelled 1 to 2 tiles in `bounce_dir`.
4. **Multi-Fire Chain Reactions:**
   If the deflection trajectory lands on another fire tile, the ant immediately takes **another 1 point of fire damage** and rebounds again! Bounces continue chaining until the ant lands on open ground, deep water, or dies.
5. **Never Extinguish Rule:** Ants **never** extinguish fire by landing on it or bouncing off it. Fire persists until its 180s timer expires or a Fire Ant explicitly uses `afxf301`.

---

### 5.6 12-Tick Stun Recovery State Machine

Following a ground landing from ballistic knockback or fire bounce:
- Impact audio fires: Sound 64 (`flythumpa.wav`), Sound 65 (`flythumpb.wav`), Sound 70 (`stun.wav`).
- Unit transitions to `UnitState::Bounce` (`*gb*`, 12 subitems).
- Unit enters `UnitState::Stunned` (Action 12).
- `m_stun_timer` is set to **12 simulation ticks (600 ms)**.
- While `m_stun_timer > 0`:
  - The ant cannot walk, attack, or perform abilities.
  - Player click orders on this ant are ignored or queued.
  - On every tick: `m_stun_timer--`.
- When `m_stun_timer == 0`:
  - Unit transitions to `UnitState::Idle`.
  - If unit is a Combat Ant: commits current landing tile as its new `guard_anchor` and re-arms its 3-tile aggro scan.

---

## 6. Data Structures & Enum Reference

### 6.1 Entity State Field Offsets (`CAntUnit`)
Aligned with reverse-engineered disassembly at `0x1004be0`:

| Field Offset | C++ Member | Type | Description |
| :--- | :--- | :--- | :--- |
| `+0x00` | VTable | `uint32_t` | Entity virtual method table (`0x1004be0`) |
| `+0x08..0x38` | `m_pixel_x, m_pixel_y` | `int32_t` | Spatial coordinates and collision envelope |
| `+0x44` | `m_active` | `uint8_t` | Unit active in simulation flag |
| `+0x56` | `m_team` | `uint16_t` | Team ID (0=Black, 1=Blue, 2=Red, 3=Green) |
| `+0x58` | `m_type` | `uint16_t` | Ant Class ID (0=Worker, 1=Bomber, 2=Fire, 3=Thief, 4=Combat, 5=Swimmer) |
| `+0x74` | `m_hp` | `uint16_t` | Current Health (0..10 HP) |
| `+0x76` | `m_anim_state` | `uint16_t` | State index (Idle=7, Walking=2, Attacking=3, Ability=4) |
| `+0x80` | `m_target_id` | `uint32_t` | Target entity ID for attacks or interactions |
| `+0x9C` | `m_action_timestamp` | `uint32_t` | Tick timestamp of action initiation |
| `+0xA8` | `m_state` | `uint8_t` | UnitState enum value |
| `+0xE4` | `m_death_status` | `uint8_t` | Death status (`0x0F` = Drowned in water, `0x01` = Combat) |
| `+0xE8` | `m_holding` | `uint8_t` | 1 if carrying food/lunchbox, 0 if empty-handed |

### 6.2 Visual Suite Prefixes (`a*` vs `h*`)

| Unit Type | Empty-Handed Prefix (`a*`) | Carrying Food / Stolen Points (`h*`) |
| :--- | :---: | :---: |
| **Worker Ant** | `ag` (`agwg`, `agst`, `agat`) | `hg` (`hgwg`, `hgst`, `hgat`) |
| **Bomber Ant** | `ab` (`abwg`, `abst`, `abat`) | `hb` (`hbwg`, `hbst`, `hbat`) |
| **Fire Ant** | `af` (`afwg`, `afst`, `afat`) | `hf` (`hfwg`, `hfst`, `hfat`) |
| **Thief Ant** | `at` (`atwg`, `atst`, `atat`) | `ht` (`htwg`, `htst`, `htat`) |
| **Combat Ant** | `ac` (`acwg`, `acst`, `acat`) | `hc` (`hcwg`, `hcst`, `hcat`) |
| **Swimmer Ant** | `as` (`aswg`, `asst`, `asat`) | `hs` (`hswg`, `hsst`, `hsat`) |

---

## 7. Integration & Verification Strategy

### 7.1 Cross-Module Header Dependencies
- `ant_unit.hpp` consumes `ants::assets::Direction` from `include/ants_assets/mirroring.hpp`.
- `combat_ai.hpp` consumes `AntUnit` from `ant_unit.hpp` and forward-declares `Grid` and `AudioEvent`.
- `physics.hpp` consumes `AntUnit` from `ant_unit.hpp` and interacts with `Grid`.
- `sim_engine.hpp` (designed by Explorer 1) aggregates `AntUnit`, `CombatAIController`, and `PhysicsEngine`.

### 7.2 Headless Test Coverage Matrix for M2 Implementers
The implementers and test writers (Explorer 3 / `test_sim_rules.cpp`) can directly verify each component against the following test scenarios:

1. **Damage Matrix Verification:**
   - Worker, Bomber, Fire, Thief, Swimmer inflict strictly 1 HP damage per hit.
   - Combat Ant inflicts strictly 2 HP damage per hit.
   - Units die at HP == 0.
2. **Combat Ant Autonomous Guard Verification:**
   - Enemy unit enters 3-tile Chebyshev radius $\implies$ Combat Ant breaks idle and intercepts.
   - Friendly or allied ant enters 3-tile radius $\implies$ Combat Ant remains idle.
   - Underground unit (e.g. Thief in anthill) enters 3-tile radius $\implies$ Combat Ant ignores.
   - Combat Ant punches target, target is knocked back 4–5 tiles, Combat Ant returns to anchor post.
3. **Ballistic Knockback & Obstacle Verification:**
   - Punch knockback pushes target 4–5 tiles; bomb knockback pushes target 2–3 tiles.
   - Knockback into a stone boulder stops immediately upon collision.
   - Unit enters 12-tick stun state (`stun_timer == 12`) and rejects orders.
4. **Water Landing & Drowning Verification:**
   - Non-swimmer ant knocked into deep water $\implies$ `death_status = 0x0F`, `hp = 0`, Sound 71 and 72 dispatched.
   - Swimmer ant knocked into deep water $\implies$ takes 0 damage, survives into swimming mode.
5. **Fire Ricochet Verification:**
   - Ant knocked into fire takes 1 damage (`source = 7`) and deflects.
   - Ant bouncing between two fire tiles takes 2 damage and deflects.
   - Fire tile is never extinguished by bouncing ants.

---
*End of Blueprint — Ready for M2 Workers to Implement.*
