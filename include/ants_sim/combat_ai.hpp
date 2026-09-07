#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "ants_sim/ant_unit.hpp"

namespace ants::sim {

class Grid;
class MatchStatsManager;
struct AudioEvent;

enum class CombatGuardState : uint8_t {
    Idle         = 0, // Guarding anchor post; scanning 3-tile Chebyshev radius
    Intercepting = 1, // Autonomously pathfinding towards detected intruder
    Striking     = 2, // Melee contact reached; delivering heavy punch
    Returning    = 3  // Disengaged; pathfinding back to guard_anchor
};

class CombatAIController {
public:
    static constexpr int32_t AGGRO_RADIUS_CHEBYSHEV   = 3; // 7x7 square scan perimeter
    static constexpr int32_t PURSUIT_DISENGAGE_RADIUS = 5;
    static constexpr uint16_t ATTACK_ANIM_TICKS       = 6;
    static constexpr uint16_t STRIKE_IMPACT_FRAME     = 2;

    explicit CombatAIController(AntUnit& owner);

    CombatGuardState guard_state() const noexcept { return guard_state_; }
    int32_t          anchor_tile_x() const noexcept { return anchor_tx_; }
    int32_t          anchor_tile_y() const noexcept { return anchor_ty_; }
    std::optional<uint32_t> target_id() const noexcept { return target_unit_id_; }

    void commit_guard_anchor() noexcept;
    void set_guard_anchor(int32_t tx, int32_t ty) noexcept;

    void update(const std::vector<AntUnit*>& all_units,
                const Grid& grid,
                const MatchStatsManager& stats,
                std::vector<AudioEvent>& audio_out,
                uint32_t random_seed);

    bool is_valid_target(const AntUnit& candidate,
                         const AntUnit& owner,
                         const MatchStatsManager& stats) const noexcept;

    void on_user_command_issued() noexcept;

private:
    void update_guard_idle(const std::vector<AntUnit*>& all_units,
                           const Grid& grid,
                           const MatchStatsManager& stats,
                           std::vector<AudioEvent>& audio_out,
                           uint32_t random_seed);

    void update_intercepting(const std::vector<AntUnit*>& all_units,
                             const Grid& grid,
                             const MatchStatsManager& stats,
                             std::vector<AudioEvent>& audio_out,
                             uint32_t random_seed);

    void update_striking(const std::vector<AntUnit*>& all_units,
                         const Grid& grid,
                         std::vector<AudioEvent>& audio_out,
                         uint32_t random_seed);

    void update_returning(const Grid& grid);

    AntUnit* find_best_target(const std::vector<AntUnit*>& all_units,
                              const MatchStatsManager& stats) const;

private:
    AntUnit&                owner_;
    CombatGuardState        guard_state_{CombatGuardState::Idle};

    int32_t                 anchor_tx_{0};
    int32_t                 anchor_ty_{0};

    std::optional<uint32_t> target_unit_id_{std::nullopt};
    uint16_t                strike_timer_{0};
};

} // namespace ants::sim
