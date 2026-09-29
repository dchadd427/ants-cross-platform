# Original Ant Movement: Reverse-Engineering Reports

Verified findings behind `docs/GAME_REVERSE_ENGINEERING.md` §5.32 ("Movement Ground Truth") and the remake's
movement code (`src/ants_sim/movement_system.cpp`, `src/ants_sim/path_planner.cpp`,
`src/ants_sim/movement_tables_data.inc`).

Method: Capstone disassembly of `Original-Ants/Ants.exe` (image base `0x01000000`) is the primary source; the Ghidra
output `docs/legacy/Ants.exe.c` was used only to navigate. Each report was followed by an independent adversarial
pass that re-derived every claim from the instructions; its verdicts are appended to the report.

| Report | Covers |
|---|---|
| [01_timing_model_and_stepper.md](01_timing_model_and_stepper.md) | Animation stepper `FUN_0102b95f` / `FUN_0102b997`, millisecond clock, start step, re-entrancy quirk |
| [02_animation_tables_and_setaction.md](02_animation_tables_and_setaction.md) | Static animation tables, SetAction `FUN_0101ad02`, terrain classes, per-frame dx / dy / duration |
| [03_walk_step_arrival_and_stop.md](03_walk_step_arrival_and_stop.md) | WalkStep `FUN_0101b8cb`, ARRIVE, PathComplete `FUN_0101ccaf`, StopSync / StopAt, ANTPAUSE |
| [04_occupancy_passability_and_blocking.md](04_occupancy_passability_and_blocking.md) | Occupancy grid, CanEnter `FUN_0101f780`, TryEnterTile `FUN_0101c4f2`, solid bits |
| [05_path_manager_and_astar.md](05_path_manager_and_astar.md) | PATHMGR task, PathRequest A*, heap, step cost `FUN_01020951`, delivery |
| [06_orders_goto_and_group_dispatch.md](06_orders_goto_and_group_dispatch.md) | Group order `FUN_010287b5`, accept predicate, GoTo `FUN_0101fc50`, classification, goal ring scan |

Regenerate and audit the numbers:

```bash
python3 tools/extract_movement_tables.py --check      # generated tables still match Ants.exe / ants.chd
python3 tools/movement_reference_model.py             # golden timings of tests/test_sim/test_movement_golden.cpp
```
