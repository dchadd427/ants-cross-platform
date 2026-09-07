# Dispatch Assignment: worker_m2_1

**Identity**: worker_m2_1 (M2 Simulation Implementation Worker)  
**Role**: teamwork_preview_worker  
**Parent Conversation ID**: a28dfa55-5a82-453d-a21b-99459a66b340  
**Working Directory**: /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_worker_m2_1  

## Mandatory Reading
- /Users/dchadd/Desktop/Ants-Mac/ORIGINAL_REQUEST.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/orchestrator_1/PROJECT.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_spec_miner_survey_2/survey_sim.md
- /Users/dchadd/Desktop/Ants-Mac/GAME_REVERSE_ENGINEERING.md

## Authoritative Architectural Blueprints & Plans
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_1/sim_architecture_plan.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_2/combat_and_physics_plan.md
- /Users/dchadd/Desktop/Ants-Mac/.agents/teamwork_preview_explorer_m2_3/sim_rules_and_tests_plan.md

## File Ownership
You exclusively own and will modify/create:
- /Users/dchadd/Desktop/Ants-Mac/CMakeLists.txt
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/sim_engine.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/grid.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/prng.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/match_stats.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/ant_unit.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/combat_ai.hpp
- /Users/dchadd/Desktop/Ants-Mac/include/ants_sim/physics.hpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/CMakeLists.txt
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/sim_engine.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/grid.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/ant_unit.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/combat_ai.cpp
- /Users/dchadd/Desktop/Ants-Mac/src/ants_sim/physics.cpp
- /Users/dchadd/Desktop/Ants-Mac/tests/test_sim/CMakeLists.txt
- /Users/dchadd/Desktop/Ants-Mac/tests/test_sim/test_sim_rules.cpp
- /Users/dchadd/Desktop/Ants-Mac/run_tests.sh

## MANDATORY INTEGRITY WARNING
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Mission Objectives
1. Implement the complete, genuine, zero-dependency C++17 library `libants-sim` according to the blueprints and specifications:
   - Fixed 20 Hz tick engine, 32x32 integer tile math, deterministic MSVC LCG PRNG (`holdrand * 214013 + 2531011`, `latseed`).
   - 6 unit classes, starting & max 10 HP, 16.16 fixed point speeds, melee standard 1 HP.
   - Combat Ant 2 HP heavy punch (Sound 78) + 4-5 tile ballistic knockback + 12-tick stun.
   - Combat Ant Autonomous Guard AI (guard anchor post, 3-tile Chebyshev aggro perimeter, target filtering, intercept, return to anchor post).
   - Cardinal-only placement rules (|dx|+|dy|==1, dx==0 || dy==0, diagonal rejection, ground flags 0x02 bomb, 0x04 fire).
   - Bombs planting (Sound 90), detonation (2 HP, 2-3 tile knockback, Sound 4), Bomber-only body squash defusal (Sounds 73+74, 0 HP damage).
   - Fire creation (Sounds 67+68, 180s timer), non-fire ant obstacle, ricochet physics (+1 fire damage, non-occupancy bounce, multi-fire chains, never extinguish by landing), Fire Ant walk & extinguish (Sound 69).
   - Bridge 4-stage construction (Sound 82), universal traversal (any friendly or hostile ant), 180s collapse with instant drowning for non-swimmers (`death_status = 0xF`, Sounds 71+72, 22-subitem drowning sequence), Swimmer survival.
   - Anthill base queuing (concentric Chebyshev rings), 17-frame `hgen301` sequence (Frame 4 food deposit Sound 87, Frame 8 100% full heal to 10 HP Sound 36, Frame 16 emergence ready), 200pt egg hatching.
   - Thief infiltration 33-frame `atcr501`, Sound 58 alarm siren on victim client, `min(50, score)` stolen, Sound 88 score drop, physical lunchbox drop on carrier death, universal pickup.
   - Dynamic alliances (FFA default, Sound 51 propose, Sounds 53+50 accept, Sound 52 deny, Sound 49 break, combined HUD score display while preserving discrete individual stats).
   - 4-stat scorecard tracking (Score, Friendly Lost, Enemy Killed, New Hatched).
   - Match countdown to 0:00, simulation freeze, winner Sound 56 vs loser Sound 41.
2. Implement automated test suite `tests/test_sim/test_sim_rules.cpp` covering 12 test suites and 50+ test cases.
3. Update `run_tests.sh` to include `test_sim_rules` as part of `./run_tests.sh --all` and support `./run_tests.sh --sim`.
4. Build using CMake and verify `./run_tests.sh --all` and `./run_tests.sh --asan` run cleanly with 100% passes and 0 leaks.
5. Deliver handoff report to `handoff.md` and send completion message to parent.
