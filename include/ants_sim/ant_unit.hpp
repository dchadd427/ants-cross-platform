#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
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
    Combat  = 4, // Combat ant (ac) - heavy punch (2 HP + knockback), the original's auto-engage reflex (its only AI: no guard post)
    Swimmer = 5  // Swimmer ant (as) - deep water navigation, bridge building
};

/**
 * @brief Primary behavioral state machine states for ant units.
 */
enum class UnitState : uint8_t {
    Idle           = 0,  // Stationary, standing ready (*st*, Action 7)
    Walking        = 1,  // Traversing terrain towards waypoint (*wg* / *ws*, Action 2)
    Attacking      = 2,  // Melee attack wind-up and strike (*at*, Action 1)
    Flinch         = 4,  // Hit reaction flinch (*gh*, Action 10)
    Knockback      = 5,  // Ballistic airborne flight (*gf*, Action 14)
    Stunned        = 7,  // Immobilized recovery state (Action 12, 12 ticks)
    EnteringBase   = 8,  // Enter (action 2) or hatch (action 0x14) clip on the hill entrance (?h0 / ?hatch)
    Drowning       = 10, // 22-subitem drowning sequence (*dr301)
    Dead           = 11, // Unit eliminated
    GuardIdle      = 12, // Combat Ant at rest (the name is a leftover of the guard post AI that was removed in v0.0.33: there is no guard post)
    Swimming       = 15, // Swimmer Ant actively swimming in water
    Infiltrating   = 16, // Thief raid clip (action 0xD, atcr501) on the raid tile
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
    Burn              = 27, // Dud burn / bomb smoke blast stagger (*bu301, 11 subitems)
    PoweringUp        = 28  // Power-up pick-up clip (action 4, getpow: the type has already changed)
};


/**
 * @brief Unit death classification status.
 * Value 0x0F is the reverse-engineered code for drowning in deep water.
 */
enum class DeathStatus : uint8_t {
    Alive        = 0x00,
    CombatKilled = 0x01,
    Drowned      = 0x0F  // Authentic code 0x0F from Ants.exe disassembly 0x101b86e
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
 * @brief AntUnit entity representing an active unit on the game grid.
 */
class AntUnit {
public:
    static constexpr uint16_t MAX_HP          = 10;
    static constexpr uint16_t STARTING_HP     = 10;

    // Locomotion has no speed constants: in Ants.exe an ant moves only when a walk-animation frame
    // ends, by that frame's dx/dy from ants.chd Table 4 (see movement_tables.hpp and
    // docs/GAME_REVERSE_ENGINEERING.md, "Movement ground truth").

    // Original-engine "action" ids (CAntUnit +0xe4) for locomotion-managed states.
    static constexpr uint8_t kActionIdle   = 0x00;  // *st* idle animation
    static constexpr uint8_t kActionWalk   = 0x01;  // *w{g,s,m,d}* walk / assw swim / asdi dive / asgo climb
    static constexpr uint8_t kActionEnter  = 0x02;  // ?h0 / h?h0: enter the own hill (deposit and heal at the end)
    static constexpr uint8_t kActionStun   = 0x03;  // ?sd301 / h?sd301
    static constexpr uint8_t kActionGetPow = 0x04;  // getpow: power-up pickup
    static constexpr uint8_t kActionHarvest = 0x05; // ?gf: grab food
    static constexpr uint8_t kActionIgnite = 0x06;  // afsf: fire ant places a fire wall
    static constexpr uint8_t kActionExtinguish = 0x07; // afxf
    static constexpr uint8_t kActionPlant  = 0x08;  // absb: bomber plants a bomb
    static constexpr uint8_t kActionDefuse = 0x09;  // abdb
    static constexpr uint8_t kActionBlast  = 0x0A;  // gb / burn overlay: bomb victim
    static constexpr uint8_t kActionCantGo = 0x0B;  // *cg301 "can't" animation (sound 63 on frame 0)
    static constexpr uint8_t kActionDeath  = 0x0C;  // death1..4
    static constexpr uint8_t kActionRaid   = 0x0D;  // atcr501: thief raids an enemy hill
    static constexpr uint8_t kActionHit    = 0x0E;  // ?gh: get hit
    static constexpr uint8_t kActionDrown  = 0x0F;  // ?dr301
    static constexpr uint8_t kActionBridgeBuild = 0x10;    // ?bbl / ?bbw
    static constexpr uint8_t kActionBridgeDemolish = 0x11; // ?dbl / ?dbw
    static constexpr uint8_t kActionAttack = 0x12;  // ?at: melee attack
    static constexpr uint8_t kActionBlown  = 0x13;  // ?gb: blown away by a punch
    static constexpr uint8_t kActionHatch  = 0x14;  // ?hatch: newborn emerges
    static constexpr uint8_t kActionNone   = 0xFF;  // state driven by other remake systems (no locomotion clip)

    // Order codes (CAntUnit +0xa8) and the "no target" sentinel tile of +0xac (row 0x5a, col 0x78).
    static constexpr uint8_t kOrderNone    = 0x00;
    static constexpr uint8_t kOrderMove    = 0x01;
    static constexpr uint8_t kOrderHome    = 0x02;
    static constexpr uint8_t kOrderAttack  = 0x03;
    static constexpr uint8_t kOrderPowerUp = 0x04;
    static constexpr uint8_t kOrderHarvest = 0x05;
    static constexpr uint8_t kOrderIgnite  = 0x06;   // fire ant: place a fire wall on the target tile
    static constexpr uint8_t kOrderExtinguish = 0x07; // fire ant: put the fire wall on the target tile out
    static constexpr uint8_t kOrderPlant   = 0x08;   // bomber: plant a bomb on the target tile
    static constexpr uint8_t kOrderDefuse  = 0x09;   // bomber: defuse the bomb on the target tile
    static constexpr uint8_t kOrderBomb    = 0x0A;
    static constexpr uint8_t kOrderRaid    = 0x0B;
    static constexpr uint8_t kOrderBridgeBuild = 0x0D;     // swimmer: build a bridge piece on the target water tile
    static constexpr uint8_t kOrderBridgeDemolish = 0x0E;  // swimmer: demolish the bridge on the target tile
    static constexpr int32_t kNoOrderTileX = 0x78;
    static constexpr int32_t kNoOrderTileY = 0x5A;

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

    int32_t     pixel_x{0};
    int32_t     pixel_y{0};
    /// The pixel tile at which the fog of war last looked at this ant (the original reveals from an ant's position update, FUN_0101a93a -> FUN_01006af4); (-1, -1) = not yet.
    /// A view-side memory: it is neither part of the simulation's state nor hashed.
    int32_t     fog_tile_x{-1};
    int32_t     fog_tile_y{-1};
    Direction   facing{Direction::South};

    int32_t     fx_x{0};
    int32_t     fx_y{0};

    uint8_t     holding{0};
    uint16_t    carried_food{0};
    uint16_t    carried_points{0};

    uint16_t    anim_subitem{0};
    uint16_t    anim_tick{0};
    bool        is_on_mud{false};
    bool        was_in_water{false};
    bool        in_water{false};


    std::vector<TileCoord> waypoints;
    size_t      current_waypoint_idx{0};
    TileCoord   final_dest{-1, -1};
    TileCoord   harvest_origin{-1, -1};
    bool        is_thief_steal{false};
    TileCoord   ability_target{-1, -1};

    // ---- Combat state (Ants.exe CAntUnit) ----
    bool        engaged{false};              // +0x84: hit at contact, waits for the strike frame of the attacker's clip
    bool        frozen{false};               // +0xfc: the step callback does nothing (dud burn overlay)
    uint32_t    pending_victim{0};           // +0x8c: victim of the pending hit of an attack clip (ant id, 0 = none)
    uint8_t     pending_range{1};            // +0x90: flight range in tiles (1, or 4 for a combat ant)
    uint8_t     pending_dir{0};              // +0x92: flight direction (8 = cornered, no flight)
    TileCoord   pending_tile{-1, -1};        // +0x94: contact tile of the victim
    uint8_t     killer_team{7};              // +0x76: team of the last damage (7 = none)
    uint32_t    last_order_ms{0xFFFF0000u};  // +0x98: time of the last Order (auto-engage waits 2 s; the original's clock is large)
    bool        knock_flag{false};           // +0xb4: dud (bomb) or cornered (knock) flag of the flight order 0xA / 0xC
    TileCoord   flight_tile{-1, -1};         // +0xb0 of a flight: the landing tile (the ant is put on its centre at the end)
    bool        removed{false};              // the ant object is gone (RemoveAnt); the entry stays for its id
    bool        audio_tracked{false};        // audio only (not hashed): the ant has started a clip sound that a clip change or the removal must stop (tracked sounds)
    uint32_t    burn_end_ms{0};              // end time of the dud burn overlay that freezes the ant (0 = none)
    // combat ant auto-engage (+0xbc, +0xc0, +0xc4, +0xc8, COMBEVT task +0x80)
    bool        auto_engage{false};          // +0xbc
    uint8_t     ae_order{0};                 // +0xc0: saved order
    TileCoord   ae_target{-1, -1};           // +0xc4: saved target tile
    uint8_t     ae_home_state{0};            // +0xc8: saved +0x68
    uint32_t    combevt_due_ms{0};           // COMBEVT task fire time (0 = no task)

    // ---- Original-engine locomotion state (Ants.exe sprite animation + CAntUnit fields) ----
    // Waypoints follow the original convention: waypoints[0] is the start tile, waypoints.back() the
    // goal, and current_waypoint_idx starts at 0 (the first ARRIVE advances it to 1).
    struct LocoPlayer {
        movement::MotionClip clip{};   // running animation (frame list, sprite +0x28)
        uint16_t cursor{0};            // 0 = start step pending; otherwise 1-based index of the current frame (+0x2c)
        uint32_t next_ms{0};           // animation-clock time at which the current frame ends (+0x10)
        uint8_t  dir{4};               // direction the clip was chosen for (renderer uses it for mirroring)
        uint32_t serial{0};            // incremented on every play (detects a clip change inside a step callback)
        uint16_t evt5_ms{0};           // stretched duration of the heal frame (event 5) of the enter clip, 0 = native
        uint32_t sound_mask{0};        // +0x30: frames whose sound already played (only clips with the once flag use it)
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
    uint8_t     home_state{0};              // +0x68: 0 none, 1 heading to the waiting ring, 2 queued at the ring
    uint8_t     home_priority{0};           // +0x6c: 1 = ordered by a click or a retreat, queued first
    uint32_t    home_time_ms{0};            // +0x70: arrival time at the ring tile (0 for priority ants)
    int32_t     raid_amount{0};             // +0xf8: loot fixed when a thief's raid starts (signed: min(victim score, 50), below zero for a victim below zero)
    uint8_t     orig_target_team{255};      // +0xb0 (attack order)
    uint32_t    orig_target_ant{0};         // +0xb2 (attack order)
    TileCoord   orig_special_tile{-1, -1};  // +0xb0 of the ability orders 6..9, 0xd, 0xe: the tile the ability works on
    uint16_t    orig_b4{0};                 // +0xb4 of the bridge actions: the tile id (0x22..0x25) this ant expects to find
    int32_t     orig_food_id{-1};           // +0xb0 (harvest order): food object identity (index in the grid's object table)
    TileCoord   orig_food_tile{-1, -1};     // +0xb4 (harvest order): food anchor tile
    uint16_t    harvest_amount{0};          // +0xb8 (harvest order): points of the bite, 0 = the object's value
    uint32_t    move_serial{0};             // bumped by clear_path() to invalidate pending path requests
    TileCoord   occ_tile{-1, -1};           // +0x5a/+0x5c: tile this ant is registered on in the occupancy grid
    bool        arrived_this_tick{false};   // set when the path completed during the current tick

    AntUnit(uint32_t unit_id, TeamId team_in, AntType type_in, int32_t start_tx, int32_t start_ty);

    // An ant object exists until RemoveAnt: a victim that lost its last hp still flies and plays its death clip.
    bool is_alive() const noexcept {
        return !removed;
    }
    bool is_stunned() const noexcept {
        return state == UnitState::Stunned || state == UnitState::Knockback || state == UnitState::Burn;
    }
    bool is_holding() const noexcept {
        return holding != 0;
    }
    /// Playing the enter (2) or hatch (0x14) clip: melee cannot start against such an ant (its refusal list).
    bool in_hill_action() const noexcept {
        return loco_action == kActionEnter || loco_action == kActionHatch || state == UnitState::EnteringBase;
    }


    /// A pixel coordinate as 16.16 fixed point. The multiplication is done on the unsigned value: for every coordinate that fits (|px| < 32768) the result is the
    /// shift's, a coordinate that does not fit wraps instead of being undefined behaviour (a left shift of a negative or overflowing int).
    static constexpr int32_t to_fixed(int32_t px) noexcept {
        return static_cast<int32_t>(static_cast<uint32_t>(px) * 65536u);
    }

    void set_tile_pos(int32_t tx, int32_t ty) noexcept {
        pos.x = tx;
        pos.y = ty;
        pixel_x = tx * 32 + 16;
        pixel_y = ty * 32 + 16;
        fx_x = to_fixed(pixel_x);
        fx_y = to_fixed(pixel_y);
    }

    void set_pixel_pos(int32_t px, int32_t py) noexcept {
        pixel_x = px;
        pixel_y = py;
        pos.x = (px >= 0) ? (px / 32) : ((px - 31) / 32);
        pos.y = (py >= 0) ? (py / 32) : ((py - 31) / 32);
        fx_x = to_fixed(px);
        fx_y = to_fixed(py);
    }


    void pick_up_food(uint16_t units = 1, uint16_t points = 25) noexcept {
        holding = 1;
        carried_food += units;
        carried_points += points;
    }

    // SetHolding of a thief's loot (0x101ac64): the loot word is sign-extended (movsx) into the carried amount, so a negative loot is a
    // debt that the thief deposits; carried_points keeps the 16 bits, carried_signed() gives the value back.
    void steal_points(int32_t points) noexcept {
        holding = 1;
        carried_points = static_cast<uint16_t>(points);
    }
    int32_t carried_signed() const noexcept { return static_cast<int16_t>(carried_points); }


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

    void tick_timers() noexcept;
};

} // namespace ants::sim
