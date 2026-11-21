// The rules number 14 of the original's game and 15 of 187 (docs/GAME_REVERSE_ENGINEERING.md 5.40, "The gap before the path manager's answer"): when the path manager's refusal is given.
// The original's PATHMGR is a scheduler task added with a delay of 0 and a period of 50 ms (`0x100e4ff`), so its runs fall on the boundaries of the 50 ms turns, and a click is taken between two runs, whenever it comes:
// an ant that has just been sent by a GoTo (a can't-go clip's end, a raid clip's end) is idle with no path until the next run, and an order in that gap is accepted. Until the rules number 13 the engine gave the
// answer of a run at once, at the end of the tick and before the next turn's commands, so that no command ever met an ant in the gap. From the rules number 14 the refusal ("no path") of a run that ends a tick is held and
// given at the start of the next one, after the turn's commands (pathmgr_answer in movement_system.cpp); an order in between replaces the request and drops the held refusal. A path that is found is given at once as
// before: the ant stands idle for its first 250 ms, so an order takes it before and after the answer alike. Nothing else moves: the searches run at the same instants and give the same paths. The setting is made by
// SimulationEngine::set_game_mode (back to the rule of a match played now) and by the player of an old recording (replay::begin_match), and is kept by init().
#include "sim_engine_impl.hpp"

namespace ants::sim {

void SimulationEngine::set_path_answers_after_commands(bool on) {
    impl_->path_answers_after_commands_ = on;
    impl_->world_state_dirty_ = true;
}

bool SimulationEngine::path_answers_after_commands() const noexcept {
    return impl_->path_answers_after_commands_;
}

}  // namespace ants::sim
