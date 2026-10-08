# Bots: the contest batch and the island batch in one bot (what was joined, how it was checked, what is left)

Two finished branches of the bots were joined on v0.6.0: the contest batch (`docs/audit/bots_contest_notes.md`: the race for contested food, the hit points of every ant on the view, fights for the kill, a safe fire-in, the endgame; 7 commits) and the island batch (`docs/audit/B4_1_notes.md`, "B4a": the water analysis, bridges, the expedition by bomb flights, the Swimmer ferry; 5 commits). The simulation, the lock-step rules, the network protocol and every golden hash are unchanged (rule 5: only `ants_ai`, its tests, `bot_arena` and the documents change). The rules as they are, and the numbers, are in [`docs/BOTS.md`](../BOTS.md) ("Task ranks" and, in "Measurements", "The two batches together"); this file holds what is specific to the join.

## What conflicted

Only the kind that keeps both: the lists of test files and suites (`tests/test_ai/CMakeLists.txt`, `ai_test.hpp`, `test_ai_main.cpp`), the three per-level blocks of `plan_for` in `tactics.cpp` (the contest's catch-up and hunt numbers, then the island fields), and the documents (README, `docs/BOTS.md`: the level table, the testing paragraphs, the measurement sections, the known limits; one bullet was edited by both). `standard_bot.cpp`, `tasks.cpp`, `standard_tasks.cpp`, `tactics.hpp` and `bot_arena.cpp` merged by git: `think()` runs the island, expedition and ferry steps first and the contest's tasks as before; the harvest task has both the race and the pile limits of the bridges; the tune keys of both batches are in `apply_tune`.

The test ids clashed once: both batches had an `AI14`. The island batch keeps AI14 - AI19 (one family, used all through its documents), the contest's nine cases are AI20.1 - AI20.9; `AI2.22` and `AI1.1b` of the contest did not clash. The batch called itself "v0.6" in 47 places; it says "the contest batch" now and its note is `bots_contest_notes.md`.

## The one defect of the join, and its fix

The contest's controller refuses a special order onto a tile that any ant stands on (a click on an ant selects it). The builder of the island task (off in the level plans, on in the labs and with `--tune ibuild=1`) rests in the water beside its last tile and could rest on the tile of its next dig: the order was refused at every look for the rest of the match. `AI18.3` caught it (a builder with a ferry Swimmer scored 960 against 1,410 before the join). The builder now steps aside onto a free water tile that is not one of the tiles to dig and gives a tile up after 200 ticks if an ant stays on it; `AI15.16` and `AI18.3` pin it. No shipped bot was affected: with no builder in the level plans, no order of the island tasks was refused in any match played (the arena counts them: `filtered`; 96 whole matches of four bots on ISLANDS and SMALL at the three levels, 16 seeds each, had none filtered and none rejected). `AI21.3` keeps it that way for the shipped plans: four registry bots on ISLANDS and SMALL at every level for 6,000 ticks (the expedition's flights, the ferry and the fights of the contest), and not one command of any seat may be filtered; a refusal would be the sign of a join defect like the builder's. The mutant that plants an expedition bomb onto a tile that an ant holds is caught by it.

## The ranks of the tasks

Decided and pinned (`AI21.1`, `AI21.2`), the reasons are in "Task ranks": the island tasks at 3 with the power-up task, the ferry at 2 with the guard, the fights at 5 never taking an ant whose task has rank 3 or more.

## Checks (on the join, before the reviews)

- **Equal where nothing lies beyond water** (`bot_arena`, the merged bot against the contest batch's final binary): TINY, MEDIUM, GAUNTLET and TREASURE, four registry bots (seeds 1 - 12, three levels: 144 matches) and two new against two of the v0.5.0 strategy (`prev=1`, six arrangements, seeds 1 - 4, three levels: 288 matches): every score, hash and command count equal. SMALL and ISLANDS with `--tune islands=0`: 72 matches equal. The pinned table of the worker (`--write-baselines`) is byte for byte what it was: no baseline was re-pinned.
- **The measurements of the two batches stay in their own sections of `docs/BOTS.md`** ("The contest batch" and "The island play": each was measured without the other), and "The two batches together" measures the joined bot where they meet. **The numbers of both batches hold** ("The two batches together"): the contest's win rates on the four maps without water are reproduced to the digit (64 matches a cell), and the island play earns what it did (ISLANDS Easy and Medium to the digit, Hard within the noise; SMALL Easy and Medium to the digit, Hard 1,000 a seat against 998; two against two 935 / 1,008 / 1,108 against 784 / 798 / 839).
- **Tests**: `./run_tests.sh --fast` (53 suites, none failed, GCC 13, Release, `-Werror`), and the full tier's suites 2.22 (worker: 23 cases, 4,427 assertions) and 2.28 (`test_ai_islands`: 3 cases, 109 assertions); suite 2.20 (`test_ai`) holds the cases of both batches (AI14 - AI21), and `bot_arena --selftest` passes. AI18.3 failed on the first merged build (the defect above) and passes with the fix; no other test needed a new bound: the tick bounds of the island tests hold with the contest's rules present. `AI21.3` (the controller refuses nothing in whole island matches, see "The one defect of the join") was added after the checks above; its mutant is the last row of the table.
- **Mutants of the glue** (`tools/mutate.py`, one fault each; the specs are not kept): 9 faults, 9 caught, the unmutated tree built and tested before the first fault and after the last.

| Fault | Caught by |
|---|---|
| the builder ignores ants on its tile (the check removed) | AI15.16 (a refused order) |
| the builder never steps aside | AI18.3 (it never did) |
| a tile with an ant on it is never given up | AI15.16 |
| the expedition at the economy's rank | AI21.1 |
| the ferry at the rank of the power-ups | AI21.1 |
| the island task at the rank of the fights | AI21.1 |
| the fights take ants of rank 3 (the crew) | AI21.2 |
| the harvest task ignores the limit of a bridge pile (the island half of the merged `step`) | AI15.6 |
| the expedition plants a bomb on a tile that an ant holds (its guard removed) | AI21.3 (a refused order) |

- **Sanitizers**: GCC 13, `-fsanitize=address,undefined`, a Release build with `-DANTS_WERROR=OFF` (as in the contest note: GCC 13 gives known `-Wsign-conversion` false positives under UBSan in older files; the ordinary build with `-Werror` is clean). On the merged tree, with no failure and no sanitizer report: the whole suite 2.20 `test_ai` (209 cases, 568,376 assertions), suite 2.28 `test_ai_islands` (3 cases, 109 assertions), the worker suite without its two pinned tables (AI3.9, AI3.12, as `run_tests.sh --asan` leaves them out: 21 cases, 4,218 assertions) and `bot_arena --selftest` (140 checks).
- **Another compiler and standard library**: clang 18.1.3 with libc++ 18 (`-stdlib=libc++`, Debug, `-DANTS_WERROR=ON` with the project's warning flags; a stand-in for Apple clang, which CI runs). `ants_ai`, `test_ai`, `test_ai_islands`, `test_ai_worker` and `bot_arena` build without a warning and every suite passes: 2.20 (209 cases, 568,367 assertions), 2.28 (3 cases, 109 assertions), the whole worker suite with its pinned tables (23 cases, 4,427 assertions, as with GCC) and `bot_arena --selftest` (140 checks). The 2.20 count is 9 below GCC's 568,376 because of one older test, AI1.11 (`walkable()` against the engine): it draws the team and the tile of a random probe as arguments of one call, which the two compilers evaluate in another order, so it probes other tiles; it passes either way, and nothing in the join is involved. The arena plays the same matches with this build as with GCC 13 and libstdc++ (ISLANDS, SMALL, TREASURE and GAUNTLET, seeds 1 - 3, Easy and Hard: 24 matches, every score, state hash and command count equal), so no decision of a bot depends on the standard library.

## The two reviews of the joined branch and their fixes

Two independent read-only reviews of this branch, one of the contest batch and its join, one of the island batch, found no blocking defect and eight things to fix and eleven notes. All of them are fixed or answered; this is what each became.

| Finding | What was done | Pinned by |
|---|---|---|
| Contest 1: the escorts of the safe fire-in were called to an enemy gate whose ring had no open tile (the forward guard that the owner decided to leave out), the first wall waited for escorts that no walk gets to the entrance for ever, and the entrance was not tested | escorts are called only to light (an open tile and a Fire Ant to light it) and are held while a ring stands, whoever lit it (`ring_done_` is decided before the escorts are); an entrance that no ant can stand on or walk to is no fire-in; a wait for escorts ends after `sabotage_escort_wait` (2,400 ticks) and the team is left alone for `sabotage_giveup_ticks` | AI20.6 (f), (g), (g2), (h), (h2), (i), (i2), (i3) |
| Contest 2: `docs/BOTS.md` said that a pile with one competitor races, but the plans ship `race_one` off, and no test had it on | the document says that only a pile with two competitors races (`raceone=1` is the experiment); a test with `race_one` on tells a pile of one competitor from one that an enemy far nearer reaches first | AI20.1 (e) |
| Contest 3: a Thief that the raid task held stayed held on Easy when the catch-up pressure fell (nothing stepped the task any more) | `RaidTask::set_launching`: when the plan does not raid and the pressure is below the lift tier no raid is launched, the one under way is seen out and the Thief is the economy's again | AI20.9 (e2), (e3) |
| Contest N1: a dying ant (its clip of about 42 ticks) is in no list of the view, but the controller refuses a click on it; a raid at the entrance held the best target off for 900 ticks | `BotView::dying_at`; the tasks' `occupied()` looks there | AI1.1, AI13.5 |
| Contest N2: an offence fight whose defenders were all gone held the next hunt off for the rest of its 400 ticks; the comments said "advantage gone" | it ends when nobody is left and no free ant takes over; comments and the hunting paragraph of `docs/BOTS.md` say what the code does | AI20.5 (b3) |
| Contest N3: ring walls stayed lit for as long as an enemy Fire Ant stood near, and one that stands on a wall cannot be hunted | the hold ends `fire_defence_hold` (1,800) ticks after the first look at lit walls | AI20.7 (e), (e2) |
| Contest N4: the Medium and Hard rows of AI20.1 (a) were also met by the opening of v0.5.0 | the rows are pinned at 1, 0, 3 and 1 ants, and a check without the race shows that the opening alone gives fewer | AI20.1 (a) |
| Island 1: a Swimmer that the ferry sent and that then stood idle with its food was never sent home while its pile existed (the rescue ran only for a Swimmer with no record) | every idle carrier is rescued after `rescue_after` ticks; the record of one that was sent home stands while it walks home with the food and ends when it has banked it (else the empty Swimmer by the hill reads as an order that did nothing); the clock of the next rescue starts anew | AI18.8 |
| Island 2: the test that should have seen it (AI17.6) shared one counter between all Swimmers and tested the wrong state; nothing tested a failed order or a rescue | one counter per ant, `Idle` or `Swimming`; AI18.7 (an order lost in transit is learned from, the failure clock starts when it left) and AI18.8 | AI17.6, AI18.7, AI18.8 |
| Island 3: the expedition could choose a row that needs one ant, make a crew of two, refuse it (`crew_min` 3) and choose it again at every look | the crew is never smaller than `crew_min` | AI17.11 |
| Island 4: `docs/BOTS.md` said that the other tasks do not route over a bridge that will not last (only the power-up task does) | the document says so | - |
| Island 5: the ferry's water test accepted a tile with a rock or a reed in it (the engine lets no ant in) | open water only (`is_solid_object`); the test puts the rocks in after the map was analysed, because a map with the rocks from the start has no island for the ferry to serve (its analysis finds no water to cross) | AI18.9 |
| Island N1: the channel search scanned the map once for every component | one pass over the shores for all of them; the analysis is the same on every shipped map (the dump of every island, token group, channel and flight before and after is equal) | AI14.1 - AI14.10 |
| Island N2: AI15.11 had no premise (it passed when no stop was ever ordered) | it asserts that stops were seen | AI15.11 |
| Island N3: the sort of the tokens of a group tied when two touch one tile | `power` is the last key | no test can tell (see the mutants) |
| Island N4: the analysis counts the hills of teams that have dropped out, the engine does not | documented as the safe side (a few tiles are refused that the engine would accept) | - |
| Island N5: the unforced drowning counter of the fixture subtracts the ants that were fought | said in the fixture; AI17.3 and AI17.10 (one seat, no enemy) hold the expedition | - |
| Island N6: a fate of the ferry's order that came after a new search chose another cell was ignored | the order's cell is kept in its record | AI18.7 |
| Island N7: an expedition that was given up was tried again after 900 ticks for ever | the pause doubles with each attempt in a row (900, 1,800, 3,600, then 7,200); the rule is `ExpeditionTask::pause_after`, a function of its own (a match has too few attempts to show the cap: each takes tokens) | AI17.12, AI17.13 |

**Checked on the fixed branch** (GCC 13, Release, `-Werror`; then main's v0.8.1 and v0.8.2 merged in):

- **Whole matches before and after the fixes** (`bot_arena`, the joined bot of 5add5ce against the bot with the fixes, the same matches: four styled bots of one level on the six shipped maps, seeds 1 - 12, 72 matches a level). The mean score of a seat moves by +1.5 +- 1.2 points at Hard (31 of the 72 matches differ in their state hash), +0.3 +- 0.2 at Medium (24 differ) and -0.1 +- 0.2 at Easy (16 differ): inside the noise at every level. TINY, and at Medium and Easy also MEDIUM, GAUNTLET and TREASURE, play the same matches as before, hash for hash; the matches that differ are the island maps (the ferry's rescue and water test: ISLANDS and SMALL, all 12 matches at Hard and at Medium, 10 and 6 at Easy) and, at Hard, some on GAUNTLET (2 matches), MEDIUM (1) and TREASURE (4), where the contest's rules changed. Kills at Hard 61 before and 59 after, none at Medium and Easy.
- **The analysis of the islands is the same on every shipped map**: the dump of every island, token group, channel and flight, before and after the change of the shore search, is equal.
- **Tests**: `./run_tests.sh --fast` (55 suites, none failed); the whole of suite 2.20 `test_ai` (216 cases, 568,716 assertions), 2.28 `test_ai_islands` (3 cases, 109 assertions), the worker suite 2.22 (23 cases, 4,427 assertions, its pinned tables untouched: no baseline was re-pinned) and `bot_arena --selftest` (140 checks).
- **Sanitizers**: GCC 13, `-fsanitize=address,undefined`, Release with `-DANTS_WERROR=OFF` (as above), on the tree of the fixes (before the four assertions that AI20.6 (g2) gained at the end): `test_ai` (216 cases, 568,712 assertions), `test_ai_islands` (3 cases, 109 assertions), the worker suite without its two pinned tables (21 cases, 4,218 assertions) and `bot_arena --selftest` (140 checks): no failure and no sanitizer report.
- **Another compiler**: the macOS job of CI is Apple clang. On the head with the fixes it built everything and passed `test_ai`, `test_ai_islands` and `test_ai_worker` (the one red case of that run was `test_server` S3.32, a flood timing test over real sockets, which this branch does not touch). The clang 18 check above was made before the fixes.
- **Mutants of the fixes** (`tools/mutate.py`, one fault each, in two rounds; the specs are not kept; the unmutated tree was built and tested at the start and at the end of each run): 75 faults, 59 caught, 16 survive. The first round had 74 faults: 37 caught, 8 that did not compile (written again, all caught) and 29 that survived. Of those 29, 14 got sharper tests (AI18.8, AI18.9, AI20.6 (h2) and (i3), AI20.7 (e2), AI20.9 (e2) and (e3)) and 11 of them are caught now; 2 faults of the pause after a give-up (a cap that no match reaches) are caught since the rule is a function of its own with a test (AI17.13), and the second round added one more fault of that function; 13 look equivalent or like a boundary and were run again against the whole of `test_ai` (and `test_ai_islands` for the token order): no test sees them anywhere. The 3 that still survive of the 14 and the 13 are the 16 below.

| Faults of | Faults | Caught | By |
|---|---|---|---|
| dying ants: the view and the tasks' `occupied()` | 5 | 5 | AI1.1, AI13.5 |
| the end of a hunt | 3 | 1 | AI20.5 |
| fire defence: the hold and the ring | 9 | 9 | AI20.7 |
| raids and the launching flag | 6 | 6 | AI20.9 |
| the safe fire-in: the entrance, the escorts, the hold, the wait | 29 | 20 | AI20.6 |
| the ferry: rescue, record, water test | 9 | 6 | AI18.7, AI18.8, AI18.9 |
| the expedition: crew size, pauses | 9 | 8 | AI17.11 - AI17.13 |
| the shore search and the order of the tokens | 5 | 4 | AI14.1 - AI14.10 |

The 16 that survive, and why no test can tell them from the code:

| Fault | Why it makes no difference that a test can see |
|---|---|
| the hold of the escorts without "an escort is held" (`holding-without-escorts`) | with no escort in the set the hold has nothing to do: nobody is called without a wall to light, and the wait clock runs only while lighting |
| the entrance's power-up test removed (`station-powerup-ok`) | `MapInfo::walkable` refuses a tile with a power-up already (rule R4); AI20.6 (i2) puts one on the entrance and passes without the second test |
| the wait clock not cleared when no escort is wanted (`wait-clock-kept-when-unwanted`) | the clock is zero whenever no escort is held (`release_escorts` and the branch above clear it) |
| the tie key of the token sort dropped (`token-tie-key-dropped`) | only the order of two tokens with the same x could differ; the analysis dump of every shipped map is equal without it and no test builds such a pair |
| a timeout that keeps its escorts (`timeout-keeps-escorts`) | the next look gives them back (the give-up branch): the same state one look later |
| a timeout that keeps its Fire Ant (`timeout-keeps-fire-ant`) | the same: the next look gives it back |
| the ferry's clock kept when a Swimmer has banked (`ferry-bank-keeps-clock`) | the loop erases the clock of a free Swimmer at the next look |
| a hunt without hunters ends at the look it begins, not at the next (`hunt-end-at-once`) | one look earlier or later; nothing is ordered in between |
| the hold of the escorts one tick short (`holding-one-tick-short`) | a boundary of one tick: no test reads the tick |
| the wait for the escorts one tick long (`wait-one-tick-long`) | a boundary of one tick |
| a defence fight that nobody can join ends at once (`hunt-end-any-fight`) | it would end at each look and be begun again from the memory of the blow: no order differs, only the count of fights begun |
| the wait for the escorts also runs while the ring stands (`wait-without-lighting`) | needs escorts that have not reached the entrance when the ring already stands, for 2,400 ticks (the hold lasts 900): no scenario of the suite has it |
| the wait clock kept when the escorts are at the entrance (`wait-clock-kept-when-escorted`) | needs an escort that arrived and then leaves the entrance again while walls are still being lit, with the wait running out on the old clock |
| any move of a Swimmer is taken for the ferry's order (`ferry-any-move-is-the-order`) | needs a rescue order that is lost in transit while the record stands |
| only the x of the click is compared (`ferry-click-x-only`) | needs the click and the rescue order on one column |
| the count of give-ups in a row never reset (`giveups-never-reset`) | needs a finished expedition and then a give-up in one match |

## The owner's calls of the contest batch

Hunts, the safe fire-in, the forward guard and MEDIUM at Hard were approved as built on 2026-10-05 (`docs/audit/bots_contest_notes.md`, last section); the joined bot changes nothing for them.

## Left

- The builder is still off in the level plans (the ferry earns more, `docs/BOTS.md` "Islands"); with the contest's controller its bridges are tested only in the labs.
- Two owner decisions for later batches, not built here: Swimmers are wanted on TREASURE too (the ferry should harvest where a water route beats the land route), and the fire-in is the ring of eight round the queue row with more walls allowed when the enemy has no Fire Ant (the safe fire-in of the contest refuses where a Fire power-up lies; on SMALL it costs a Hard bot about 5 points of win rate).
- At Hard on SMALL some ants drown by blows (6 ants in 16 matches, none with the island batch alone): the fights of the contest reach the lake's shore and a punch flings an ant four tiles; the plan does not avoid it (the same for a person).
