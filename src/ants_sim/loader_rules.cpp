// The loader's rules number of the engine (docs/reverse_engineering/map_loader_fidelity.md, section 8; replay::kSimRulesFireWallFirst): whether the next init() reads a fire wall of the map file as the original does.
// The setting is made by SimulationEngine::set_game_mode (back to the rules of a match played now) and by the player of an old recording (replay::begin_match), and is kept by init().
#include "sim_engine_impl.hpp"

namespace ants::sim {

void SimulationEngine::set_fire_wall_as_original(bool on) {
    impl_->fire_wall_as_original_ = on;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::fire_wall_as_original() const noexcept {
    return impl_->fire_wall_as_original_;
}

}  // namespace ants::sim
