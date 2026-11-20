// The rules number 13 of the original's game (docs/GAME_REVERSE_ENGINEERING.md 5.40, "A lunchbox on the anchor of an emptied record"): which object of the table an ant's bite takes its unit from.
// The original takes the FIRST object whose anchor is the ant's food tile (FUN_01008c63), whatever it holds, and never removes an object from the table, so a lunchbox that falls on a tile that is the anchor of an
// emptied pile or of an earlier lunchbox is a second object of that anchor behind an empty one: the ant is classified with the lunchbox, gets its points, and the bite is taken from the empty record, so the
// lunchbox stays and any number of ants can eat it. From the rules number 13 the bite takes the first object of the anchor that has a unit left. The setting is made by SimulationEngine::set_game_mode (back to the
// rule of a match played now) and by the player of an old recording (replay::begin_match), and is kept by init().
#include "sim_engine_impl.hpp"

namespace ants::sim {

void SimulationEngine::set_bite_skips_empty_record(bool on) {
    impl_->bite_skips_empty_record_ = on;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::bite_skips_empty_record() const noexcept {
    return impl_->bite_skips_empty_record_;
}

}  // namespace ants::sim
