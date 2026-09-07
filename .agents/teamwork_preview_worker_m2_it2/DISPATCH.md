## 2026-09-06T23:34:36Z
You are the M2 Remediation Worker for the Microsoft Ants remake project.
Your identity: worker_m2_it2
Your working directory: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m2_it2
Parent conversation ID: a28dfa55-5a82-453d-a21b-99459a66b340

MANDATORY: Read the original user request before starting work:
/Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md

Also read:
- Master Project Document:
  /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- Remediation Blueprints from Explorers:
  1. /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_1/base_lifecycle_remediation.md
  2. /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_2/thief_alliances_ai_remediation.md
  3. /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_3/physics_and_integration_remediation.md

File Ownership:
You exclusively own and will modify:
- include/ants_sim/sim_engine.hpp
- include/ants_sim/ant_unit.hpp
- include/ants_sim/combat_ai.hpp
- include/ants_sim/match_stats.hpp
- src/ants_sim/sim_engine.cpp
- src/ants_sim/combat_ai.cpp
- src/ants_sim/physics.cpp
- tests/test_sim/CMakeLists.txt
- tests/test_sim/test_sim_rules.cpp
- tests/test_sim/test_challenger_m2_1.cpp
- tests/test_sim/test_challenger_m2_2.cpp
- run_tests.sh

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Mission:
Apply all verified remediation diffs and implementations across libants-sim, tests/test_sim, and run_tests.sh:
1. Base Lifecycle & Queuing:
   - Implement concentric Chebyshev rings slot allocation (R=1..5) in assign_queue_slot with reserved slots tracking (release_queue_slot, clear_reserved_queue_slots, is_queue_slot_reserved).
   - Ensure Frame 8 heal sound (Sound 36 / powerupc.wav) occurs strictly once (target_frame == 8).
   - Prevent post-match egg hatching mutations (is_match_over() || impl_->match_state_ == MatchState::GameOver).
2. Thief Routing, Dynamic Alliances, and Combat AI Boundaries:
   - Add target_team_id on AntUnit; store target_team_id in start_thief_infiltration; route Sound 58 and News String 53 to u->target_team_id.
   - In MatchStatsManager::set_alliance, reset former partners to ALLIANCE_NONE for both p1 and p2 before setting the new alliance.
   - In CombatAIController::update_striking, raycast step-by-step along knockback vector checking in_bounds and !is_solid_obstacle, and clamp to map boundaries.
3. Physics Engine & Simulation Tick Integration:
   - In PhysicsEngine::resolve_fire_contact, update incoming_dx and incoming_dy across reflections/deflections.
   - Remove synthetic simulate_ballistic_flight; replace with apply_knockback and resolve_fire_contact delegating directly to PhysicsEngine. Update test 7.5 and test 3.3 to use genuine physics.
   - In SimulationEngine::issue_order, handle OrderType::ReturnToBase (path to friendly anthill) and OrderType::InfiltrateAnthill.
   - In SimulationEngine::tick(), step autonomous base entry / heal / food deposit and thief infiltration.
4. Test Integration & Runner:
   - Update tests/test_sim/CMakeLists.txt to register test_challenger_m2_1 and test_challenger_m2_2 as executables and CTest targets.
   - Update run_tests.sh to execute test_sim_rules, test_challenger_m2_1, and test_challenger_m2_2 under ./run_tests.sh --sim and ./run_tests.sh --all.
5. Verification:
   - Build with CMake.
   - Run ./run_tests.sh --sim
   - Run ./run_tests.sh --all
   - Run ./run_tests.sh --clean --asan
   - Verify 100% test passes with 0 memory leaks, 0 undefined behavior, and 0 failures.
6. Deliver handoff report to:
   /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m2_it2/handoff.md
   and send a completion message to your parent.
