#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "ants_sim/ant_unit.hpp"
#include "ants_sim/prng.hpp"

#include <functional>

namespace ants::sim {

class Grid;
struct AudioEvent;

/**
 * @brief Active ballistic projectile / knockback state for an ant in flight.
 */
struct BallisticFlight {
    uint32_t unit_id{0};
    int32_t  start_px{0};
    int32_t  start_py{0};
    int32_t  target_px{0};
    int32_t  target_py{0};
    int32_t  total_distance_px{0};

    uint16_t current_tick{0};
    uint16_t total_ticks{10}; // 10 ticks = 500 ms

    int32_t  apex_height_px{36};

    Direction flight_dir{Direction::South};
    DamageSource origin_source{DamageSource::CombatPunch};
    bool destination_resolved{false};
};

/**
 * @brief Physics and physical interaction simulation subsystem.
 */
class PhysicsEngine {
public:
    static constexpr uint32_t SOUND_BOMB_EXP      = 4;  // bombexp.wav
    static constexpr uint32_t SOUND_FLY_THUMP_A   = 64; // flythumpa.wav
    static constexpr uint32_t SOUND_FLY_THUMP_B   = 65; // flythumpb.wav
    static constexpr uint32_t SOUND_STUN          = 70; // stun.wav
    static constexpr uint32_t SOUND_SPLASH        = 71; // splash.wav
    static constexpr uint32_t SOUND_DROWN         = 72; // antdrown.wav
    static constexpr uint32_t SOUND_ATTACK_COMBAT = 78; // attack2.wav

    static constexpr int32_t COMBAT_PUNCH_MIN_TILES = 4;
    static constexpr int32_t COMBAT_PUNCH_MAX_TILES = 4;
    static constexpr int32_t BOMB_BLAST_MIN_TILES   = 4;
    static constexpr int32_t BOMB_BLAST_MAX_TILES   = 4;

    static constexpr uint16_t STUN_RECOVERY_TICKS   = 50;

public:
    PhysicsEngine() = default;

    void apply_knockback(AntUnit& victim,
                         int32_t from_px,
                         int32_t from_py,
                         int32_t min_tiles,
                         int32_t max_tiles,
                         DamageSource source,
                         std::vector<AudioEvent>& audio_out,
                         uint16_t random_val,
                         const Grid* grid = nullptr);

    void tick(std::vector<AntUnit*>& all_units,
              Grid& grid,
              std::vector<AudioEvent>& audio_out,
              PRNG& prng,
              std::function<void(AntUnit&, TileCoord)> on_bomb_land = nullptr);

    void resolve_landing(AntUnit& unit,
                         Grid& grid,
                         std::vector<AudioEvent>& audio_out,
                         PRNG& prng,
                         int32_t incoming_dx = 0,
                         int32_t incoming_dy = 0,
                         std::function<void(AntUnit&, TileCoord)> on_bomb_land = nullptr,
                         DamageSource source = DamageSource::CombatPunch);

    void resolve_water_entry(AntUnit& unit,
                             std::vector<AudioEvent>& audio_out);

    void resolve_fire_contact(AntUnit& unit,
                              Grid& grid,
                              std::vector<AudioEvent>& audio_out,
                              PRNG& prng,
                              int32_t incoming_dx = 0,
                              int32_t incoming_dy = 0,
                              DamageSource source = DamageSource::CombatPunch);

    const BallisticFlight* get_active_flight(uint32_t unit_id) const noexcept;
    void cancel_flight(uint32_t unit_id) noexcept;

private:
    std::vector<BallisticFlight> active_flights_;
};

} // namespace ants::sim
