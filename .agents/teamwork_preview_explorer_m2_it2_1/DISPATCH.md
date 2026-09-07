# Dispatch Assignment: explorer_m2_it2_1

**Identity**: explorer_m2_it2_1 (M2 It2 Base Lifecycle & Queuing Remediation Explorer)  
**Role**: teamwork_preview_explorer  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_1  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m2_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_2/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/sim_engine.cpp
- /Users/dchadd/Desktop/Ants-Mac/tests/test_sim/test_sim_rules.cpp
- /Users/dchadd/Desktop/Ants-Mac/tests/test_sim/test_challenger_m2_2.cpp

## Objectives
Formulate exact C++ remediation diffs and implementation code for:
1. Concentric Chebyshev Anthill Queuing (`src/ants_sim/sim_engine.cpp` & `sim_engine.hpp`):
   - Replace dummy `assign_queue_slot` that always returned `{bx + 1, by}`.
   - Implement authentic concentric Chebyshev ring slot allocation ($R=1, 2, \dots$):
     - For ring $R$ from 1 to 5: check perimeter cells satisfying $\max(|x - b_x|, |y - b_y|) == R$.
     - Verify cell validity: `in_bounds`, `is_passable`, unoccupied by existing queued units.
     - Among candidate slots in the lowest available ring, pick the slot closest to `from_pos` (Manhattan distance), mark/reserve it, and return it.
2. Frame 8 Full Heal Audio Event Multiplication (`src/ants_sim/sim_engine.cpp:730`):
   - Ensure Sound 36 (`powerupc.wav`) and `heal_full()` occur strictly ONCE when reaching Frame 8, not 9 times on every frame $\ge 8$.
3. Post-Match Game Freeze on Egg Hatching (`src/ants_sim/sim_engine.cpp:289`):
   - In `SimulationEngine::hatch_ant`, check `if (is_match_over() || impl_->match_state_ == MatchState::GameOver) return false;` to prevent unit hatching and score mutation during game over freeze.
4. Document exact C++ replacement code and diffs in `base_lifecycle_remediation.md`.
5. Deliver handoff report to `handoff.md` and send message to parent.
