# Dispatch Assignment: explorer_m2_it2_3

**Identity**: explorer_m2_it2_3 (M2 It2 Physics Ricochets, Autonomous Orders & Test Integration Explorer)  
**Role**: teamwork_preview_explorer  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_it2_3  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_reviewer_m2_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_1/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_challenger_m2_2/handoff.md
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/physics.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/sim_engine.cpp
- /Users/dchadd/Desktop/Ants-Mac/tests/test_sim/CMakeLists.txt
- /Users/dchadd/Desktop/Ants-Mac/run_tests.sh

## Objectives
Formulate exact C++ remediation diffs and implementation code for:
1. Physics Engine Multi-Fire Ricochet Updates:
   - In `src/ants_sim/physics.cpp:173` (`resolve_fire_contact`), update `incoming_dx` and `incoming_dy` upon each reflection so consecutive bounces properly calculate reflection angles.
   - Remove any synthetic shortcut methods (`simulate_ballistic_flight`) in `sim_engine.cpp`/`.hpp` and ensure tests and engine use genuine physics ticks.
2. Autonomous Orders & Simulation Tick Integration:
   - In `src/ants_sim/sim_engine.cpp`:
     - Handle `OrderType::ReturnToBase` in `issue_order`: find friendly anthill and issue move order to anthill coordinates.
     - In `SimulationEngine::tick()`: check when an ant carrying food reaches friendly anthill and transition it through base entry / heal / deposit.
     - In `SimulationEngine::tick()`: check when an infiltrating thief ant reaches enemy anthill and transition it through infiltration / theft / score deduction.
3. Test Suite Integration:
   - Update `tests/test_sim/CMakeLists.txt` to register both challenger suites (`test_challenger_m2_1` and `test_challenger_m2_2`).
   - Update `run_tests.sh` so `./run_tests.sh --sim` and `./run_tests.sh --all` execute all simulation tests and challenger suites.
4. Document exact C++ replacement code and diffs in `physics_and_integration_remediation.md`.
5. Deliver handoff report to `handoff.md` and send message to parent.
