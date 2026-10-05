# Bots: the contest batch and the island batch in one bot (what was joined, how it was checked, what is left)

Two finished branches of the bots were joined on v0.6.0: the contest batch (`docs/audit/bots_contest_notes.md`: the race for contested food, the hit points of every ant on the view, fights for the kill, a safe fire-in, the endgame; 7 commits) and the island batch (`docs/audit/B4_1_notes.md`, "B4a": the water analysis, bridges, the expedition by bomb flights, the Swimmer ferry; 5 commits). The simulation, the lock-step rules, the network protocol and every golden hash are unchanged (rule 8: only `ants_ai`, its tests, `bot_arena` and the documents change). The rules as they are, and the numbers, are in [`docs/BOTS.md`](../BOTS.md) ("Task ranks" and, in "Measurements", "The two batches together"); this file holds what is specific to the join.

## What conflicted

Only the kind that keeps both: the lists of test files and suites (`tests/test_ai/CMakeLists.txt`, `ai_test.hpp`, `test_ai_main.cpp`), the three per-level blocks of `plan_for` in `tactics.cpp` (the contest's catch-up and hunt numbers, then the island fields), and the documents (README, `docs/BOTS.md`: the level table, the testing paragraphs, the measurement sections, the known limits; one bullet was edited by both). `standard_bot.cpp`, `tasks.cpp`, `standard_tasks.cpp`, `tactics.hpp` and `bot_arena.cpp` merged by git: `think()` runs the island, expedition and ferry steps first and the contest's tasks as before; the harvest task has both the race and the pile limits of the bridges; the tune keys of both batches are in `apply_tune`.

The test ids clashed once: both batches had an `AI14`. The island batch keeps AI14 - AI19 (one family, used all through its documents), the contest's nine cases are AI20.1 - AI20.9; `AI2.22` and `AI1.1b` of the contest did not clash. The batch called itself "v0.6" in 47 places; it says "the contest batch" now and its note is `bots_contest_notes.md`.

## The one defect of the join, and its fix

The contest's controller refuses a special order onto a tile that any ant stands on (a click on an ant selects it). The builder of the island task (off in the level plans, on in the labs and with `--tune ibuild=1`) rests in the water beside its last tile and could rest on the tile of its next dig: the order was refused at every look for the rest of the match. `AI18.3` caught it (a builder with a ferry Swimmer scored 960 against 1,410 before the join). The builder now steps aside onto a free water tile that is not one of the tiles to dig and gives a tile up after 200 ticks if an ant stays on it; `AI15.16` and `AI18.3` pin it. No shipped bot was affected: with no builder in the level plans, no order of the island tasks was refused in any match played (the arena counts them: `filtered`).

## The ranks of the tasks

Decided and pinned (`AI21.1`, `AI21.2`), the reasons are in "Task ranks": the island tasks at 3 with the power-up task, the ferry at 2 with the guard, the fights at 5 never taking an ant whose task has rank 3 or more.

## Checks

- **Equal where nothing lies beyond water** (`bot_arena`, the merged bot against the contest batch's final binary): TINY, MEDIUM, GAUNTLET and TREASURE, four registry bots (seeds 1 - 12, three levels: 144 matches) and two new against two of the v0.5.0 strategy (`prev=1`, six arrangements, seeds 1 - 4, three levels: 288 matches): every score, hash and command count equal. SMALL and ISLANDS with `--tune islands=0`: 72 matches equal. The pinned table of the worker (`--write-baselines`) is byte for byte what it was: no baseline was re-pinned.
- **The numbers of both batches hold** ("The two batches together" in `docs/BOTS.md`): the contest's win rates on the four maps without water are reproduced to the digit (64 matches a cell), and the island play earns what it did (ISLANDS Easy and Medium to the digit, Hard within the noise; SMALL Easy and Medium to the digit, Hard 1,000 a seat against 998; two against two 935 / 1,008 / 1,108 against 784 / 798 / 839).
- **Tests**: `./run_tests.sh --fast` (53 suites), the whole of suite 2.20 (`test_ai`: the cases of both batches, AI14 - AI21), suite 2.28 (`test_ai_islands`), the worker suite 2.22 and `bot_arena --selftest`. AI18.3 failed on the first merged build (the defect above) and passes with the fix; no other test needed a new bound: the tick bounds of the island tests hold with the contest's rules present.
- **Mutants of the glue** (`tools/mutate.py`, one fault each; the specs are not kept): 7 faults, 7 caught.

| Fault | Caught by |
|---|---|
| the builder ignores ants on its tile (the check removed) | AI15.16 (a refused order) |
| the builder never steps aside | AI18.3 (it never did) |
| a tile with an ant on it is never given up | AI15.16 |
| the expedition at the economy's rank | AI21.1 |
| the ferry at the rank of the power-ups | AI21.1 |
| the island task at the rank of the fights | AI21.1 |
| the fights take ants of rank 3 (the crew) | AI21.2 |
- **Sanitizers**: TBD_ASAN

## Left

- The builder is still off in the level plans (the ferry earns more, `docs/BOTS.md` "Islands"); with the contest's controller its bridges are tested only in the labs.
- Two owner decisions for later batches, not built here: Swimmers are wanted on TREASURE too (the ferry should harvest where a water route beats the land route), and the fire-in is the ring of eight round the queue row with more walls allowed when the enemy has no Fire Ant (the safe fire-in of the contest refuses where a Fire power-up lies; on SMALL it costs a Hard bot about 5 points of win rate).
- At Hard on SMALL some ants drown by blows (6 ants in 16 matches, none with the island batch alone): the fights of the contest reach the lake's shore and a punch flings an ant four tiles; the plan does not avoid it (the same for a person).
