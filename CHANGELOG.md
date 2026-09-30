# Changelog

The running list of what changed in every version of the Ants remake, newest first. The version number lives in
`include/ants_app/version.hpp` and is shown on screen next to the FPS meter (bottom right of every screen).
This file is updated with every release, together with the [README](README.md); it is also published on the beta site at
[beta.playants.org/changelog.html](https://beta.playants.org/changelog.html).

How to read it: versions before v0.0.24 approximated the original 1998 game; from v0.0.24 on every system was re-derived from
the disassembly of `Ants.exe` and replaced (the "Original ..." series). "Rewritten tests" are old tests that encoded behaviour
the original does not have; they were changed to the verified behaviour, never deleted or disabled (each commit message lists them).
Ground truth for every entry is in [`docs/GAME_REVERSE_ENGINEERING.md`](docs/GAME_REVERSE_ENGINEERING.md); the network port is described in
[`docs/NETWORK_PORT.md`](docs/NETWORK_PORT.md).

## Unreleased

- **Audit of the whole game against the original (documents only, no behaviour change)**: [`docs/AUDIT_ONE_TO_ONE.md`](docs/AUDIT_ONE_TO_ONE.md) and one ledger per area in [`docs/audit/`](docs/audit/). Every finding of the
  earlier per-area audits was re-checked against today's code (13 independent passes with their own probes), plus two fresh audits (the original's scheduler tasks; food, economy and scoring). Result: the sprite, terrain,
  HUD, movement and path-finding layers are identical to the original in every pixel / step that could be compared; the differences are listed in the order in which they are proposed to be fixed (hill queue defects, the end-of-match
  rules, audio routing, the unbuilt results / options / start-up screens, the view origin, the minimap, fog reveal, chat, the walking-frame cadence). Nothing in that list has been changed yet.
- **Cleanup pass (no behaviour change, no version bump)**: dead code found with the linker's dead-strip map of an unoptimised build of every binary (nothing in any game or test binary reaches it), with a clang AST
  scan of every declaration in `include/` and `src/` that no translation unit of the game, the tools or the tests uses, and with a scan for members that are written but never read. The full suite gives identical
  results (188 integration tests with 6,536 assertions, every golden result and state hash unchanged, 506 E2E tests), the build is warning-free, the web image builds.
  - **The old A\* path finder** (`pathfinding.hpp` / `pathfinding.cpp`, 294 lines, `PathFinder::find_path` and `find_nearest_passable`): unused since the `PATHMGR` port of v0.0.24.
  - **Unused functions and accessors**: `SimulationEngine::reset`, `prng() const`, `get_unit() const`, `clear_locomotion_trace`, `has_other_living_ant_at` (and its twin in the implementation), the duplicate `retreat_home`
    (the live 1 hp retreat is `low_hp_check`), `spawn_death_effect`, `get_unit_pointers`, `HUD::has_friendly_selected` and eleven more HUD accessors, `NetGame::role` / `rtt_ms`, `started()` of the lock-step runner and both
    sessions, `last_heard_ms`, `LoopbackNetwork::now` / `messages`, `ByteReader::remaining`, `fnv1a64`, `PRNG::rand_range` / `roll_chance` / `set_state`, `MatchStatsManager::deduct_score`, `Grid::has_food_at` and seven more
    grid and coordinate helpers, the audio mixer's and MIDI player's unused getters, `animation_sound`, the asset archive's whole-vector accessors and its unused `Direction8` wrappers, the `WaveFormat` compatibility getters, and
    similar (about 90 in all).
  - **Unused constants and types**: the old layout constants of the HUD, the setup screen, the results modal and the renderer (the art carries its own positions), `InputMode`, `WorldCoord`, `WaypointPoint`, `PendingHatch`,
    `FixedPointMath`, `AntUnit::STANDARD_DAMAGE` / `COMBAT_DAMAGE`, `TICK_RATE_HZ`, `kTicksPerTurn`, the `Camera` / `Vec2i` / `LVLMap` / `WaveFormatEx` / `Animation` aliases, enumerators nothing names (`UnitState::Ability`,
    `DeathStatus::BombKilled` / `FireKilled`, `MatchState::Paused`, `ClientLobby::Phase::Cancelled`).
  - **State that was written but never read**: the setup screen's drop button hover / press flags and connection tick counter, the movement task's `engine_` pointer, the lobby's `hello_sent_`, the application's
    `last_tick_time_`, the HUD's `right_mouse_held_`, `NetGame::name_`, the `UIButton` sprite ids and `is_enabled`, snapshot fields no consumer reads (`tick_number`, `match_state`, `held_item_id`, `is_airborne`,
    `drop_frame`, `MatchResult::is_tie`, `PlayerEntry::is_allied`), `ApplicationConfig::vsync`, `MapSelectEntry::anthills_count` and the map scan that filled it, `LoopbackNetwork::messages_`.
  - **24 standard includes** that their file does not use.
  - **Kept on purpose**: the tables of the original's ids (`SoundID`, `StringID`, the game strings), the move constructors of the resource classes, `Application::run_frame` (the browser build's loop), helpers that a test
    pins (`effect_spec::dropper_frame_at`, the `Direction8` overloads of the mirroring helpers), and `ScorecardModal::set_on_replay` (a test and the application still set it although nothing ever calls it: a question for
    the results-screen stage).
  - **Found on the way, for the audit** (docs: implementation_plan.md section 18): `tests/test_sim/test_challenger_m2_it2_deep_stress.cpp` has not been part of the build since an early commit and no longer compiles
    (it uses the retired slot-queue API of the hill). (An earlier version of this line said that the death clips are no longer started by a separate effect: wrong, only the unused helper `spawn_death_effect` was removed; the effect path in `movement_system.cpp` is still in use, see the audit.)

## v0.0.69 - 2026-09-30 - The score boxes of the original

Batch 6 part 1 (`docs/audit/ledger_ui.md` NEW-1, NEW-2; re-read in `Ants.exe`: `0x100e1f0` - `0x100e222`, `0x1021e36` - `0x10220f4`, `0x101aa65`, the tables at `0x10021b8` and `0x1002218`):

- **An allied team's score box is split**: the left half in its own colour, the right half (from 26 px in) in its ally's colour, and the number is the sum of both scores, in the local team's box and in the ally's box alike (it used to be one colour and one score, so allies could not see what their team scored).
- **Teams that do not play, and teams that dropped out, have their box covered** with the original's `scorcovr` plate (it left an empty black box), and a dropped team's score is gone (the box used to keep it); its name label stays, as in the original.
- **The slot of a team follows its index**: the local team has the top bar's box, the other teams the three bottom slots in index order, and a team that is absent leaves its slot (covered) instead of the later teams moving up (a game of the teams 0 and 2 showed team 2 in the first slot).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.53, README (counts), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_hud_layout` `test_score_boxes` (20 checks: the slots for local team 0 and 1, absent teams covered at (left - 2, top - 1), a dropped team, the allied split and sum). Version assertions of 12.108.

## v0.0.68 - 2026-09-30 - The answers to orders and selections of the original

Batch 5 part 2 (`docs/audit/ledger_input.md` NEW-12, NEW-2, NEW-8; re-read in `Ants.exe`: `0x10287b5` - `0x1028a0e`, `0x1027f07`, `0x1027aae`, `0x1027530`, `0x1027940` - `0x1027950`, `0x1011281`):

- **Order feedback and voice follow the original's group order**: the pedestal click / pop-up happens whenever at least one selected ant needed the order (did not already carry out that very click), **even when every ant's path request is refused** (a worker sent onto an enemy hill: the pedestal flashes with its click, nobody answers, no text); nothing at all happens when every ant already carries it out. The voice comes only from the closest ant, and only when its order was accepted; a special order (bomb, fire, bridge, raid) speaks **only when exactly one ant needed it** (for several ants it is silent, not even the "go" voice), counting the ants that needed the order rather than the ants that were selected. The lock-step client's immediate feedback follows the same rules (`CommandResult::needing_order` is predicted exactly).
- **Shift selections post their text**: adding an ant with Shift (click or drag), or taking one out with Shift, rebuilds the panel and posts its text as every other selection does: "Ready!" for a group, the ant's type text when one is left ("BomberAnt selected."), and the line is cleared when nothing is left. They used to keep the old text (the remake had read the original's last argument of the panel function the wrong way round).
- **The quit dialog's and the team dialogs' buttons cancel for good when the pointer leaves them** (the button class): press, leave, come back and release no longer answers the question.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.42 / 5.44 (the group order's answers, the Shift texts), README (counts), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_commands` N1.20b (the count of the ants that needed the order, predicted and real, in a 2000-order scripted match too), `test_pointer_model` (a refused order still flashes the pedestal, a special click for two ants is silent, for one ant it speaks; the dialog button cancel), `test_status_messages` (the Shift paths). **Rewritten test**: `test_status_messages` "quiet paths" (a shift-add kept the old text). Version assertions of 12.108.

## v0.0.67 - 2026-09-30 - The view of the original

Batch 5 part 1 (`docs/audit/ledger_input.md` I-06 and NEW-1; re-read in `Ants.exe`: `0x100a32b`, `0x100dcbf`, `0x1030249`, `0x100e458` - `0x100e4b1`, `0x1027197`, `0x100cd00`, `0x100ecdf`):

- **The map view is at (16, 21) and 442 x 440**, as in the original: the remake put the world one pixel right and down (origin (17, 22), 441 x 439), so every click, every marker, the camera's limits (1479 / 1481 instead of 1478 / 1480 on a 60 x 60 map) and the listener of the sound law (origin + 220 / 219 instead of + 221 / 220) were a pixel off. What you see does not change: the UI shell covers the extra row and column with its black border.
- **The start view is the original's**: the fresh view scrolls just far enough to show the square 160 px up and left and 192 px down and right of the centre of your hill's anchor tile (clipped to the map). It no longer centres the hill: on GAUNTLET green starts at (726, 24) (it was centred at (772, 69)), red at (0, 728), blue at (406, 120); on TINY nothing scrolls. The same view is set at every match start and when the local team is set.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.44 (the map view and the start view), README (counts), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_input_model` `test_start_view` (golden origins), `test_pointer_model` (the pixels (16, 21) and (457, 460) are the camera's world pixel and 441 / 439 further), `test_hud_layout` (`static_assert` of the view rectangle), integration 2.4 (the listener). **Rewritten tests** (they encoded the old placement): integration 2.1, 2.2, 2.3 (the camera mapping and its limits), 7.6 (the viewport), 8.8 (the start views of the three teams), `test_pointer_model` (the marker pixels). Version assertions of 12.108.

## v0.0.66 - 2026-09-30 - The quick help and the chat input of the original

Batch 4 part 3 (`docs/audit/ledger_screens.md` R4.2d and NEW-4, the chat input of NEW-9; each re-read in `Ants.exe`: `0x10145d2`, `0x10147c2`, `0x1014802`, `0x100dbe2`, `0x100a37c`, `0x10119a8`):

- **The quick help closes only by its button or its keys**: the F1 / Help version used to close on a click anywhere. Now its Return button is the original's button class (the press captures it, the release on the button closes it, leaving it cancels for good) and a click elsewhere does nothing; the keys Enter, Esc, C and X (either case) close it, as before. The quick help at the start of the program has the same rules: **START!** is the button class (press captures, release starts, leaving cancels), and the keys are Enter, Esc, C and X (**Space is gone**: it was an invention). The button rectangles are the unions of their pictures.
- **The chat input box is the original's edit control**: text at (481, 424) in 12 px letters in the colour (7, 11, 15) (it was at (484, 423) in (20, 50, 40), cut to the last 25 characters), a caret that toggles every **150 ms** from the moment the screen is built (it was 750 ms), the end of a text that does not fit the box shows (box 139 px less the caret's width), and nothing is drawn while the chat option is off. It shares its drawing with the options' quick chat fields.
- Not built, recorded in `docs/GAME_REVERSE_ENGINEERING.md` 5.52: the **More Help** button and dialog (`M` / `m`): the dialog's text offers a trip to a web page that no longer exists (an owner decision).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.52, README (counts), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_pointer_model` (the Return button: capture, release, leaving, the union rectangle, a click elsewhere), integration 9.14 (the start-up quick help: button class, exact key set, the stored switch skips it), `test_hud_layout` (the chat input's text, colour, caret phases, tail and the hidden box). **Rewritten test**: the quick help block of `test_pointer_model` (a click no longer closes it). Version assertion of 12.108.

## v0.0.65 - 2026-09-30 - The options of the original

Batch 4, part 2 (`docs/audit/ledger_screens.md` S3, each rule re-read in `Ants.exe` first: `0x101487c`, `0x1011397` - `0x1011656`, `0x1015058` - `0x10150bc`, `0x10119a8` - `0x1011d86`, `0x1014f12` - `0x1015122`, `0x100c18f`, `0x102950f`, `0x100bbf2`, `0x100e7c7`):

- **The sliders are the original's slider class**: the thumb stands where `211 + 185 v / 99` puts it (the Sound Volume 100 of the start lies beyond the end of the track), the hit rectangle is the track from x 188 to 418 and 20 px down from its top (it was a rough box), and the value is what the thumb's position stands for, `(pos - 211) * 100 / 185`, 0 to 99. **A press changes nothing by itself** (it used to set the value at once); the thumb follows the pointer while the button is held and the release applies the value once (Sound Volume: the volume and the test voice; Music Volume: the volume and a new random piece; Scroll Speed: the speed). Like everything in the original, the release is preceded by a move to its position, so a click without any motion sets the value of the clicked position. The values are whole numbers as in the original (the sliders, the mixer and the edge scroll used fractions).
- **The switches (Participate In Chat, Show Quick Help at Startup) act at the release**, as the button class does: a press only captures (the down picture, then down + hover while the pointer moves inside), leaving the button cancels for good, and clicking the one that is already latched leaves it latched. They acted at the press.
- **The four quick chat fields are the original's edit fields**: 141 x 15 at (92 / 302, 370 / 402) in 12 px letters in the dialog's own colour (239, 231, 223), at most **100 characters** (the remake allowed 40), the F9 field has the focus when the screen opens, a press on a field focuses it and takes the focus from the others (a press on anything else takes it from all), the caret is an underscore right behind the text that shows for 150 ms and is gone for 150 ms (it was 750 ms), a text that does not fit shows its end while the field has the focus and its beginning otherwise, and every typed character or Backspace changes the quick chat at once. **Enter closes the screen whatever has the focus and Esc does nothing** (both used to only end the editing), and **a click outside the card no longer closes the screen** (that was an invention).
- **The options are remembered** (they were forgotten at every start): the original keeps them in its registry profile, the remake in a text file `settings.ini` of `name=value` lines (`Sound Volume`, `Music Volume`, `Scroll Speed`, `Participate In Chat`, `Show Quick Help at Startup`, `Quick Chat F9` ... `Quick Chat F12`: the original's names) in the per-user application folder, in the browser's local storage on the web, and in memory only in a headless run; `--settings FILE` names a file. Each callback writes its setting as the original's callbacks write the registry, and the next start reads them with the original's rule: a number counts only when it is a whole number from 0 up to 99 (the two switches: only 0, everything else is "on"), otherwise the default applies (Sound 100, Music 65, Scroll 50, chat and quick help on, quick chats = the texts of the original's strings 18 - 21); a stored quick chat is cut to 100 characters and stripped of anything but printable ASCII. The startup "quick help" now follows the stored switch.
- Not ported, recorded in the docs: the profile entries `Maps Folder`, `Pulse Interval` and `Pulse dropout` (the remake looks for its Maps folder next to the game, and its networking is its own).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.51 (the options screen, the control classes, the profile), README (settings, command line, test counts), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: the new suite `test_options` (slider placement for every value and the value for every pointer x against the original's formulas, hit edges, the latching button's pictures and capture rules, the edit field's rectangle, focus, maximum and caret phases, the store's validity rule, the file round trip, the options screen end to end), `test_hud_layout` (every picture of a switch in every state, the fields' text, colour, caret and overflow), integration 9.12 (every setting written and read back by the next start, through the whole application) and 9.13 (a headless run, invalid stored values). **Rewritten tests** (they encoded inventions of the remake): integration 9.4 (drags now expect the thumb's value: 58 and 69, not fractions), 9.10 (the music value 64), 12.58 and 12.59 (a switch acts at the release; the F9 field has the focus at open; Enter closes), the slider and switch blocks of `test_hud_layout`, the chat-off clicks of `test_status_messages` and `test_pointer_model` (press and release), `test_input_model` (the Scroll Speed as an integer). Version assertion of 12.108.

## v0.0.64 - 2026-09-30 - The setup screen of the original, and the community's maps

Batch 4, part 1 (`docs/audit/ledger_screens.md` R3.1 - R3.4, each re-read in `Ants.exe` first: `0x1012ce0`, `0x1013de7`, `0x1013fc9`, `0x1014076`, `0x10140c5`, `0x10133ef`, `0x1011206`, `0x1011281`), and three community maps that the owner asked to have checked (`POPcOrN.lvl`, `Bombz Away.lvl`, `OCEAN.LVL`; they are not part of the repository):

- **The map list is searched, not built in** (owner request): every `.lvl` file of the Maps folder is listed, sorted by the bytes of the file names (capitals first: `GAUNTLET`, `ISLANDS`, `MEDIUM`, ... for the shipped six); a map dropped into the folder appears at once; the description and the minutes come from the file's header. No map name is left in the program: the room's default map, the default map of a run without the setup screen (`--map`) and the list's fallback table are gone (a run without `--map` plays the first map of the list).
- **The setup screen's buttons are the original's button class**: the press captures (pressed picture, click sound) and the action happens at the **release**; moving off the button before the release cancels it for good. Up / Down step the list (they wrap), **Enter or S start, Q or X leave, and no other key does anything** (Esc used to leave; digits, Left / Right, Space, F and D used to act). The Fog of War pair is silent and starts on Off. START locks the screen. The map box, the description box, the thumbs and a "Drop" toggle were inventions and are gone.
- **Labels, portrait and thumb appear with the refresh, 500 ms after the screen is created**, in the original's colour (239, 231, 223) at x = 36; the prompt is wrapped in its label (36, 447) 293 px wide; the ant animation runs from the creation of the screen.
- **Community maps load** (they did not): the original's loader never looks at what follows the level's last word, but ours demanded that the file ends exactly there, so both `POPcOrN` and `Bombz Away` (which end with 206 bytes of the community editor's template) were refused. **`OCEAN.LVL` (81 rows of 100 columns) was read scrambled**: the level header lists the ROWS first and the columns second (the loader stores them at `+0xd0` / `+0xd2` and addresses `cells[y][x]`); the six shipped maps are square, so the remake had the order wrong all along. **The last word of the file is every team's egg stock as it stands**: the community template ends with `0x7FFE`, so these maps start every team with 32766 eggs (the remake used 10 when the word was 0). All three were then played for their whole clock by twin engines in 9 rosters, with and without fog and with scripted orders, hash-identical every 20 ticks, and rendered (headless screenshots).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 4.4 (what the loader does with a file) and 5.50 (the setup screen), README (setup controls, maps, community maps), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: integration 8.2 (the list is sorted, the exact key map), 8.3 (button class), 8.5 (fog pair, no invented click targets), 8.7 (the 500 ms refresh), 8.8 (a temporary folder with `POPcOrN.lvl`, `OCEAN.LVL`, `Bombz Away.lvl`, `zeta.Lvl`, a text file and a sub-folder), `test_assets` 6b.1 (rows first) and 6b.2 (filler, the final word), `test_sim_rules` 14.17 (egg stock), `ScreenButton` through the setup tests. **Rewritten tests** (they encoded inventions of the remake): integration 8.2 / 8.3 / 8.5 (digit keys, click targets, Drop, acting at the press, the built-in order), 12.75's setup block, the setup and room blocks of `test_hud_layout`, `test_network_app` N5.3 / N5.4 (press and release, map chosen by name), the two "trailing junk must be refused" tests of the asset suites (`test_adversarial` 6.4, `test_challenger_m1_2` 6.4), `test_lobby` N4.1 (an empty map name is "no map yet" in a Room) and the room helpers of `test_lobby`, `test_netgame` and `test_network_app` (a room has a map once its host chose one). Version assertion of 12.108.

## v0.0.63 - 2026-09-30 - The results screen of the original

Match-end batch, part 2 (`docs/audit/ledger_screens.md` R1.1 - R1.4b, `docs/AUDIT_ONE_TO_ONE.md` batch 3), each re-read in `Ants.exe` first (`0x10153a1`, `0x10155ac`, `0x1015136`, `0x1015b17`, `0x1015b47`, `0x10245ff`, `0x1021ba4`, `0x1021c1f`):

- **"Waiting for scores..." comes first**: the screen opens with the label (20 px, at (100, 350)) and nothing else: no rows, no portraits and **no Leave button**. After at least 250 ms (the original waits for the other machines' scores; here all numbers are known at once) the rows appear.
- **One row per team or alliance, in the original's order and places**: an alliance is one row "Alice & Bob" with the four columns added up, sorted by score (the quitter's row last, an equal score puts the local team's row first only when the local team made its row); the top row's labels start at y = 235, the others at 50 i + 273; the name at x = 100, the four numbers **left aligned** at x = 485, 534, 555 and 576 (they were centred); every text in the original's colour (239, 231, 223). Rows with nothing to show are no longer hidden, and no team is called "Green Team" any more: names come from the room / command line, a team without a name shows its colour word.
- **The ants are animated**: each row shows the standing-ant animation in the colour of its team (two ants for an alliance, at x = 45 and 75; one at x = 60), running from the moment the screen opened (they were one still picture per row).
- **One winner / loser cue, when the rows appear** (250 ms after the screen opens, not at the moment the match ends): the winner cue when the local team or its ally is the first team of the top row.
- **Keys**: **Enter, C, Q and X leave at any time** (even while it still waits); Esc does nothing (it sent the player back to the setup screen); every other key does nothing. The Leave button exists only once the rows do.
- Kept on purpose (owner decisions / forced by the substitute font): on the web "Leave" returns to the setup screen (the original exits the program); the three counter columns (19 - 21 px wide) stay on one line - the bundled Libre Franklin digits are wider than the original font's, so copying the original label's wrap would break every two-digit number; a browser game lists only the teams that have a score label.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.49 (the whole screen), README, `docs/AUDIT_ONE_TO_ONE.md` (progress; batch 3 is done).
- Tests: `test_hud_layout` `test_results_screen` (waiting phase, positions, colours, portraits and their animation, cue, Leave button, shown teams, dropped teams, names), integration 6.5 (keys) and 6.6 (the cue plays once, 250 ms after the screen opens). **Rewritten tests** (they showed the rows, the cue or the Leave button at the moment the screen opened): integration 6.1, 6.2, 6.3, 12.68, 12.75; `test_hud_layout`'s "every text of the screen is 18 px high". Version assertion of 12.108.

## v0.0.62 - 2026-09-30 - The match ends when it is decided

Match-end batch, part 1 (`docs/audit/ledger_screens.md` R1.4c, `docs/AUDIT_ONE_TO_ONE.md` batch 3), each re-read in `Ants.exe` first (`0x1024839` CHECKGO, `0x100d03b` drop-out, `0x100c5b1`, `0x101453f`, `0x1015136`):

- **A decided match no longer runs to the clock.** Besides the clock, the original's CHECKGO task ends the match when **no team has an egg, a hatch or an ant left**, and when **every team that has something left is one alliance whose combined score is strictly the best** (a tie is never a win, a score of 0 never wins, teams that are out or have dropped still count with their scores, a match of one team is never decided by score). Every machine of the original decides for its own team and the first to decide ends it for all; the shared simulation asks for each team that has not dropped.
- **A drop-out can decide it**: when a team leaves and only one team (or an allied pair) is left, the match ends at once (the original's win test after a drop, `0x100d172`). Before, the survivor of a two-player game played on alone until the clock ran out.
- **Quitting is a forfeit**: the quit dialog's Yes with exactly **one other side** left (`FUN_0100c5b1`: alliances count once, the quitter's own partner counts) ends the match for everybody and **names the quitter: its row goes last on every results screen**, whatever it scored. With more sides left it is a drop-out, as before. New player command `Quit` (wire type 11; `Drop` moved to 12, **network protocol version 4**: mixed versions cannot join one room).
- **The result carries what the original's results are built from** (`MatchResult`: the teams that have a row, the alliances that both sides confirm, the quitter; `rows(local)` is the original's row builder with its exchange sort: quitter last, then score, an equal score puts the local team's row first only when the local team made the row). The winner cue follows the same rule (the top row's first team is the local team or its ally). The results screen itself is rebuilt in the next release.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.47 (exact rules, the game phase, the quit flow, the departure list, the win test), `docs/NETWORK_PORT.md`, `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_sim_rules` Suite 14 (16 tests: elimination, alive by egg / hatch / dying ant, ties, dead and dropped teams' scores, one-team match, the drop-out win test in four shapes, sides left, quit with one or several sides, rows, sort and tie rules), `test_commands` N1.22 and the Quit / Drop wire values, `test_network_app` N5.11 (a guest quits a two-player match over the network), integration 6.4 (the quit dialog). **Rewritten tests** (they encoded playing on alone after the only other team dropped): `test_lockstep` N2.24, `test_netgame` N3.7, `test_network_app` N5.4. Version assertion of 12.108.

## v0.0.61 - 2026-09-30 - The music of the original

Audio batch, part 4 (`docs/audit/ledger_sound_texts.md` NEW-8, `ledger_screens.md` NEW-2 / R2.1d), each re-read in `Ants.exe` first (`0x100e8cc`, `0x100e6da`, `0x100e875`, `0x100e714`, `0x1022714`):

- **The intro plays once, then random pieces follow** - on the setup screen too: the original's intro ends after one play and starts a random in-game piece (`rand() % 3`, never the same piece twice in a row); every piece that ends starts the next one. The remake looped the intro until the match started.
- **Leaving the window closes the music, coming back starts a new random piece** (`WM_ACTIVATEAPP`): it kept playing in the background. The end of a match closes the music at once (it faded over a second) and nothing starts it again.
- **The music slider applies at the release and starts a new random piece** when music is playing (the Sound slider is unchanged: option and test voice, v0.0.59).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.24e (the music), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress; batch 2 is done).
- Tests: integration 9.8 (the intro plays once, the chain never repeats a piece, on the setup screen), 9.9 (deactivation / activation), 9.10 (slider release), 9.11 (match start, match end closes at once, nothing restarts). No existing test had to change. Version assertion of 12.108.

## v0.0.60 - 2026-09-30 - Tracked sounds are cut

Audio batch, part 3 (`docs/audit/ledger_sound_texts.md` NEW-4, `ledger_combat.md` V-K4), each re-read in `Ants.exe` first (`FUN_0102c0db`, `FUN_0102bdab`, `FUN_0102c245`, `FUN_01008871`):

- **A sprite's sounds stop with its clip**: in the original every sound-carrying animation has the "track" flag; the sprite remembers the buffers it started, and when its clip is replaced or it is removed they are stopped. The remake let every sound play to its end, so the tails of 109 frame sounds
  were heard that the original never plays: a bomb explosion (1144 ms) is cut after the 680 ms of its effect, a fire ant's attack (366 ms) after 120 ms, the grabs, the set-fire sound (460 of 879 ms), the swimmer's dive (420 of 993 ms), the defuse, the shovels, the thief's last sound.
  The battle cloud's sounds stop with the cloud.
- **A button's click is cut at its release**: the click of a pressed button belongs to the pressed picture, which the raised picture replaces at the release; a quick click now ends with the release instead of playing its full 277 ms.
- How: every sound has an owner (an ant, an effect sprite, the pressed button); the simulation emits a *stop* event, in order with the sounds, when an ant's clip is replaced or the ant is removed and when a bomb explosion or a battle cloud ends; the mixer cuts what the owner started.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.24d (tracked sounds), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_combat_actions` 7.2 (fire ant: the sound has its ant as owner, a stop follows the clip change), 7.3 (explosion sound stops with its 680 ms effect), 7.4 (a removed ant's sounds stop); integration 12.74 extended (the cloud's sounds stop), 4.3c (the mixer cuts only the owner's sounds, in order with the plays),
  9.6b (the button click is cut at the release; a cue is not). No existing test had to change. Version assertion of 12.108.

## v0.0.59 - 2026-09-30 - The original's sound law

Audio batch, part 2 (`docs/audit/ledger_sound_texts.md` NEW-5 / NEW-6, `ledger_screens.md` R2.1d), each re-read in `Ants.exe` first (`0x102e8e4`, `0x102d803`, `0x102f777`, `0x1015058`):

- **Distance, pan and volume follow the binary's integer law**: the listener is the centre of the view; the percent is `100 - trunc(max(|dx|, |dy|) * 100 / 2500)` (Chebyshev, radius 2500 px); the pan is `25 * trunc(dx * 100 / 2500)` hundredths of a dB applied to the far channel only;
  the volume is `25 * ((SV * pct / 100) - 100)` hundredths of a dB and DirectSound's `10^(att / 2000)`. The remake used `1 - distance / 800` (silent beyond 800 px), an equal-power pan that was hard-panned 221 px to one side, and ignored the map size: a bomb 1000 px away was silent, in the original it is heard at -10 dB.
- **The Sound Volume option is part of every sound**: -12.5 dB at 50 (x 0.237; the remake had x 0.5), cues and clicks included, and it follows the sounds that are already playing. A scrolling view re-attenuates the playing positional sounds as well.
- **The options sliders apply at the release**: the Sound slider sets the option when it is released and plays the test voice (`gantrdy`), the Music slider sets the music volume at the release; dragging only moves the thumb (before, every mouse move changed the volume).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.24c (the law), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: integration 4.3 (the integer law and golden gains: 221 px, 400 px, 800 px, 1000 px, the radius; mirrored sides), 4.3b (the option enters cues and positional sounds and follows playing sounds; a moving view; mute), `test_hud_layout` (slider applied once at the release, the voice after it, nothing while dragging).
  **Rewritten test**: integration 4.3 (it pinned the 800 px / equal-power law). Version assertion of 12.108.

## v0.0.58 - 2026-09-30 - One sting, global cues, the click of an order

Audio batch, part 1 (`docs/audit/ledger_sound_texts.md` NEW-1 / NEW-2 / NEW-3, `ledger_input.md` NEW-3, `ledger_effects_objects.md` C10, `ledger_food_economy.md` NEW-1 / NEW-2), each re-read in `Ants.exe` first:

- **The winner / loser sting plays once** (`0x1015a4a`): the simulation queued it for every team at the end tick and the results screen played its own on top, so the sting was heard twice (+6 dB, it could clip). The original plays ONE cue per machine when
  the results open: winner when the local team or its ally is the top row, else losers. The simulation no longer plays anything at the end of the match, and the music is closed at once (`0x1022714`) instead of fading over a second.
- **"Can't hatch" and the raid alarm are global cues** (`0x1010b97`, `0x10218f2`): plain volume wherever the view is. They were queued at the hill / raid tile and the mixer dropped them when the view was more than 800 px away (hatch click with fewer
  than 200 points while scrolled away: faint or silent; the raided player heard no alarm). `powerupd` (the sound of a power-up that finds no free tile) now reaches the owner's machine only, as in the original.
- **An order clicks**: after every accepted order the original flashes the pedestal of the order, and the pressed picture carries the click sound (89); the remake flashed silently. The click follows the order's voice; a latched pedestal that pops up stays silent.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.24b (who hears what) and the results-screen audio notes, README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_hill_actions` 2.6 (canthatch is global), 3.1b (raid alarm is global), `test_powerup_actions` 1.12 extended (powerupd owner-only), `test_pointer_model` (the click after the voice, silent pop-up), `test_status_messages` (voices counted apart from the click); the new checks
  fail on v0.0.57. **Rewritten tests** (they pinned the simulation's stings): integration 6.1 and 12.68, `test_challenger_m2_2` 5.2 and 5.3 (the result names winners and losers, the simulation plays nothing; the results screen's rule picks the sting). Version assertion of 12.108.

## v0.0.57 - 2026-09-30 - Small rules: level-start facing, where a power-up may land, the order of two effects

Audit batch 1, item 9 (the small items that could be verified; `docs/audit/ledger_hill.md` NEW-4, `ledger_abilities.md` NEW-5, `ledger_effects_objects.md` NEW-2), each re-read in `Ants.exe` first:

- **The ants a level starts with never face North**: SpawnAnt (`0x100ef18`) draws the direction of every ant it places as `rand() % 7 + 1`; the remake drew `rand() % 8` for the start ants (5 of the 24 start ants of GAUNTLET looked north).
- **A dying typed ant's power-up lands by the original's tile test** (`FUN_01020de7`): inside the map, no ant, not the layer-1 solid bit, not water, layer 2 empty, **not one of a live hill's special tiles** (the three tiles above the mound, the entrance, the raid tile).
  The remake only refused the 4x4 mound itself and used a different solid test, so a power-up could land on the tiles above a hill or on a solid tile.
- **When a bridge times out with a swimmer on it, the splash is created first and the `bsputter` puff afterwards** (`0x100f983`, `0x1024eb2`), so the puff is drawn on top (it was the other way round; 460 ms of the swimmer's splash).
- Not done in this release, on purpose: a path request delivered to a stunned ant (LM NEW-M3: code-only finding, needs the flight/path ownership of stage B), the team order of the occupant scan (LM NEW-M6, LK NEW-8: the mapping of the original's team slots to the remake's
  player numbers is not proven), the invented statistics counters (LB NEW-6, invisible, goes with the cleanup), and the burnout / bridge tasks as (tile, deadline) entries with the 2500 ms poll (LE NEW-4: a re-lit wall or rebuilt bridge keeps the old task, rare): all four stay on the list in `docs/AUDIT_ONE_TO_ONE.md`.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` (SpawnAnt facing, bridge timeout effects), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress and the remaining items).
- Tests: integration 12.146 (level-start facing, 20 seeds of GAUNTLET), `test_powerup_actions` 1.17 (drop tile test), `test_ability_actions` 3.9 extended (effect order); all three fail on v0.0.56. No existing test had to change. Version assertion of 12.108.

## v0.0.56 - 2026-09-30 - The flower droppers run on the original's poll

Audit batch 1, item 7 (`docs/audit/ledger_abilities.md` NEW-4, `ledger_effects_objects.md` R10, `ledger_scheduler.md` FDTASK), re-read in `Ants.exe` first (`0x100fc0d`, `0x1025063`, `0x100fdd8`, `0x100fe50`, `FUN_01009fd8`, `FUN_01008d2f`).
On the six droppers of the shipped maps (SMALL 2, MEDIUM 1, ISLANDS 2, GAUNTLET 1):

- **One poll, one stamp**: the original has a single task (FDTASK, every 3000 ms of the list scheduler) that visits every dropper. Its first visit only stamps the record; later visits post a drop when more than the record's interval
  (seconds) has passed since the stamp, and the record is stamped again AT THE POSTING. The remake counted the interval down per tick and restarted it at the landing, so every cycle was 0.8 s too long
  (SMALL 15.0 s then 30.8 s; MEDIUM 8.0 s then 16.8 s). Now SMALL posts at 15.005 s and every 15.005 s, MEDIUM at 9.003 s and every 9 s (a poll of 3001 ms: the period of a list scheduler task is never exactly its interval; an
  interval that is a multiple of 3 s would otherwise wait one more poll; a stopwatch on the original would settle the last millisecond).
- **The drop tile test** is the original's: nothing on layer 2 except a power-up (a bomb, a fire wall, a lunchbox or food refuse; an uncollected power-up is replaced) and no ant on the tile. A refusal keeps the stamp, so the next poll
  (3 s later) drops as soon as the tile is free.
- **The effect and the landing**: the drop effect runs 820 ms with its cue (powerdrip, 62) at 100 ms and the power-up tile is set at the very end without looking again, on the original's clock (before: 16 ticks = 800 ms and the cue at tick 2).
- **The power-up type** is drawn at the posting with the original's formula (`rand() % 10000` against the running totals of `trunc(p * 10000)`, `rand() % 5` when no type is selected); the remake used a scaled floating-point compare with an
  offset. The drop tile comes from the original's offset table by the plant's id (the flowers one row down, the clovers on the tile).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.29 and the two notes that called the timing approximate (the poll, the stamp and the landing replace the tick model), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_powerup_actions` 2.1 (SMALL cadence, landing), 2.2 (MEDIUM cadence), 2.3 (tile test, stamp kept, power-up replaced, ant refuses), 2.4 (the type formula), 2.5 (cue at 100 ms, landing at 820 ms); the state hash covers the new fields.
  **Rewritten tests** (they encoded the tick model): integration 12.33 (first drop after 301 ticks instead of 300, second after 285 more instead of 300, with comments), 12.126 (the dropper part waits for the next poll after the bomb is gone).
  Version assertion of 12.108.

## v0.0.55 - 2026-09-30 - Dropped teams follow the original; a blocked hatch looks again after a second

Audit batch 1, items 6 and 8 (`docs/audit/ledger_food_economy.md` NEW-5, `ledger_hill.md` NEW-5 / NEW-6, `ledger_ui.md` NEW-8, `ledger_combat.md` NEW-4, `ledger_scheduler.md` HATCHTSK), each re-read in `Ants.exe` first.
These only show after a player dropped out of a network match (quit, kicked, 60 s without a sign of life) or while a hatch waits for the entrance:

- **A raid on a dropped team's hill is refused** (`0x101d577`): the thief just stops; before, it took up to 50 points from a team that no longer exists.
- **The special tiles of a dropped team's hill are ordinary ground** (HillSpecial `FUN_0101d858` answers 0 for it, CanEnter skips it, `0x101f9a6`): bombs and fire walls may go on its entrance, raid tile and the three tiles above the mound, and ants
  of the other teams may walk on the tiles above it.
- **A dropped team's typed ants leave no power-ups** (Kill, `0x1020ff6`, drops only on the owner's machine): when a player with bomber, fire or swimmer ants left the match, all those ants used to litter the map with power-ups as they died.
- **The ally pedestal counts live teams** (`FUN_0100c58a`, `0x1028188`): it needs more than two teams that have not dropped out, and a local team that has not dropped out itself (before, it counted the hills on the map).
- **A blocked hatch looks again after 1000 ms** (HATCHTSK, `0x1025100`): when an own ant stands on the entrance the task writes 1000 into its interval and keeps running; the default (list) scheduler honours it. The remake re-checked every 8 ms
  (the `-newtask` wheel), so the newborn appeared the moment the entrance was free instead of up to a second later. (The earlier audit and the docs called the 1000 a dead store: that was wrong.)
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` (hatch, dropped hill), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_hill_actions` 2.5 (hatch retry, 1000 ms), 5.1 (raid refused), 5.2 (queue tiles ordinary); `test_ability_actions` 4.8 (placement on a dropped hill's special tiles); `test_combat_actions` 7.1 (no power-ups of a dropped team);
  `test_hud_layout` ally pedestal with live teams and with the local team dropped; all of them fail on v0.0.54. No existing test had to change. Version assertion of 12.108.

## v0.0.54 - 2026-09-30 - Bridges keep their timer, swimmers keep their footing, scores are never clamped

Audit batch 1, items 4 and 5 (`docs/audit/ledger_abilities.md` NEW-1 / NEW-2, `ledger_effects_objects.md` NEW-5, `ledger_food_economy.md` NEW-4, `ledger_sound_texts.md` NEW-9), each re-read in `Ants.exe` first:

- **An interrupted demolish no longer makes the bridge permanent** (`FUN_0101ecdf`, `0x101ed58`): the collapse timer of a bridge is a separate task in the original; stepping the stages of a demolish and restoring the
  completed bridge never touch it (only a finished build arms it, only a finished demolish cancels it). The remake zeroed the timer with every stage change, so a swimmer that lost a demolish fight left a bridge that never collapsed.
  The timeout (`BridgeTimeout`, `0x1024e66`) now acts only on a completed bridge (0x25) and is used up either way, as in the original.
- **A swimmer on a bridge that collapses goes on with its current action on the water** (`FUN_0100f8bf`, `0x100f99f`): the original starts the swimmer's current action again (`SetActionDefault`), so an idle swimmer
  shows the water idle clip and a walking one swims; before, it kept the land idle pose or the mud gait until its next action. An attacking swimmer loses its path and goes idle. The collapse is **silent**:
  the `splash.wav` that the remake played there was invented (the effect `dsplash` has no sound).
- **Scores are never clamped at zero** (`AddScore`, `0x1010cc9`; loot compare `0x101d57f`): two thieves that raid a 60-point hill together took 100 points from it and left it at 0 (points were created); the original leaves it at -40. The score box
  still draws 0 for a negative score (`FUN_01010452`), the results show the real number. The loot is a signed minimum as in the original: a thief that raids a hill below zero takes a negative loot (the victim gets the points
  back, the thief carries the debt home and its deposit lowers its team), and every non-zero carried amount is deposited.
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` (bridge timer, bridge collapse, raid loot and scores), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_ability_actions` 3.7 (the restored bridge collapses at its old deadline), 3.8 (a timeout on a half demolished bridge does nothing and is used up), 3.9 (swimmer on a collapsing bridge: water idle clip, no sound) and 3.5 extended
  (timer kept); `test_hill_actions` 3.4 (two thieves, victim at -40), 3.5 (raid on a victim below zero), 3.6 (real value in the stats); 3.7 and 3.9 fail on v0.0.53. **Rewritten tests** (they encoded the old rules): `test_sim_rules` 8.4
  (asserted the invented splash sound; now asserts the `dsplash` effect and silence), integration 12.57 (let a half built bridge time out; the original only collapses a completed one). Version assertion of 12.108.

## v0.0.53 - 2026-09-30 - Ability orders keep their identity, repeated clicks follow the original, no invented rule next to the hill

Three rule differences found by the audit (batch 1, items 2 and 3; `docs/audit/ledger_movement.md` NEW-M1 / NEW-M2, `docs/audit/ledger_abilities.md` NEW-3), each re-read in `Ants.exe` first:

- **A blocked approach tile no longer cancels the ability** (`FUN_0101c4f2` REPATH, `0x101ca65` / `0x101ca87` / `0x101caa9`): when another ant takes the tile a bomber, fire ant or swimmer was walking to (or stands on
  the way), the original orders the same ability again on the same target, which picks another side of the target (or refuses with "Can't do that..."). The remake turned the order into a plain walk to the taken tile, so the
  ant ended idle and planted, lit or built nothing. Groups sent to one target (they all pick the same side) benefit most.
- **Repeated clicks follow the original's skip rules** (`FUN_010287b5`, `0x1028874` / `0x10288b2`): clicking the target an ability order already works on again now does nothing (before: the ants snapped back to their tile and
  restarted their walk); a special click on the tile of a plain walk is an order (before: ignored, so "walk there, then plant there" did nothing); a plain click is not skipped by an ability order. The rules now live in one function that the order
  and the network layer's click prediction (`predict_order_ack`) both use, so that the predicted acknowledgement of a multiplayer click always equals the real one (an attack click now also skips an ant that is on its way home to the
  clicked own hill, as in the original).
- **Bombs and fire walls next to a hill**: the original refuses the entrance, the raid tile and the three tiles directly above the mound (row `by - 1`, columns `bx .. bx + 2`); the remake refused a second column of three tiles two
  tiles to the left of the mound (`bx - 2`, the same data read with rows and columns swapped). That extra rule is gone (and with it the duplicate `Grid::is_anthill_reserved_spot`); the three tiles above stay refused.
  The hill's tile list in `docs/GAME_REVERSE_ENGINEERING.md` section 18 was wrong and is corrected (re-derived from `0x100edb4 .. 0x100ee25`).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` (group-order skip rules, blocked approach tile, hill tiles), README (test table), `docs/AUDIT_ONE_TO_ONE.md` (progress).
- Tests: `test_ability_actions` 4.3 (bomber re-orders around a taken approach tile and plants), 4.4 (the same for a fire ant and a swimmer), 4.5 (second special click on the same target is skipped: no snap, no acknowledgement),
  4.6 (special click on a plain walk's tile is an order), 4.7 (plain click skip rules); 4.3 to 4.6 fail on v0.0.52 and pass now. `test_commands` N1.20 extended (prediction of the new skip rules).
  **Rewritten tests**: integration 12.112 (it asserted the invented left-column rule; it now asserts the three tiles above the mound and that the former left column is ordinary ground); pointer model, pedestal case (its second special click
  repeated the target the bomber already worked on and expected a flash; the original skips that click, so it now clicks a second bomb tile and checks that the repeated click gives no feedback). Version assertion of 12.108.
- Found while reading the original: the pedestal feedback follows "at least one ant needed an order" (the return value of `FUN_010287b5`), while the voice follows the closest ant's order being accepted (`0x10289b7`), and a special click
  speaks only for a group of exactly one ant; the remake ties both to the closest ant (audit LI NEW-12, now verified, small): listed for the input batch.

## v0.0.52 - 2026-09-30 - Hill queue: no ant is left behind on the waiting ring

- **The bug** (owner report on v0.0.50: "I command six ants to go to the base; the first three that arrived queued up and went in, the other three cancelled their queue"): an ant that is sent to a crowded hill walks to the waiting ring in
  front of the entrance and is queued there (`ANTHILLQ` lets one queued ant in at a time). The remake lost the queue flag of an ant whose walk was re-planned on the way, and never queued an ant that found its ring tile taken,
  so a part of every crowd stood on the ring for ever (8 carriers: 4 deposited). The original (`FUN_0101c4f2`, `0x101c935` / `0x101cad1` / `0x101caf2`, re-read for the audit) remembers the flag, gives it back after the new order, and queues a blocked ant whatever its order.
  Fixed to the original's logic; every carrier deposits now (verified with 6 and 8 carriers).
- **Dead ants no longer act on the living**: a queued ant that died kept `FIFO`-blocking the entrance for everybody, and the stale move order of a dead ant shifted the goal of later ants (30,30 became 29,29). Removed ants are skipped by the claim scans
  as in the original (`RemoveAnt` clears the slot).
- Docs: `docs/GAME_REVERSE_ENGINEERING.md` 5.35 (the flag across a re-plan), README (test table). Found by the audit of 2026-09-30 (`docs/audit/ledger_hill.md`, LH NEW-1 .. NEW-3).
- Tests: `test_hill_actions` 4.1 (re-planned ant keeps its flag and is queued), 4.2 (blocked ring tile queues the ant), 4.3 (6 and 8 carriers all deposit), 4.4 (a dead queued ant no longer blocks the entrance), 4.5 (a dead ant's order claims
  no tile); all fail on v0.0.51 and pass now. No test was changed. Version assertion of 12.108.

## v0.0.51 - 2026-09-30 - Only the original's keys

- **Every shortcut that the original did not have is gone** (owner request: "as close to a one-to-one copy of the game that works cross-platform as possible"): `Ctrl` / `Cmd` + `1` .. `4`, `Ctrl + Tab` and `Ctrl + C` (switching the
  controlled team), `Ctrl + T` / `F3` (tile grid), `Ctrl + M` (music mute), `Shift` / `Ctrl + F12` (screenshot), and the fullscreen keys (`F11`, `Alt + Enter`, `Cmd + F`). You play the team that `-pnum=` (or your network seat) gives you; in a
  local game the other teams stand idle, as the original has no computer players. `--fullscreen` still starts the game in fullscreen and `--show-grid`, `--screenshot FILE` and the other command-line options of the tests remain. The
  chat log's Page Up / Page Down / wheel scrolling stays until the original's scroll bar is built (audit batch 6).
- **Owner tweak (a deliberate difference from the original): the hit-point numbers above every ant are on by default** (the original starts with them off); `Ctrl + L` still toggles them. `test_pointer_model` was rewritten for the new default.
- Code removed with the shortcuts: `Application::toggle_fullscreen`, `toggle_tile_grid_visibility`, `set_tile_grid_visible`, the music-mute state.
- Tests: **rewritten** `8.6` (the tile grid is a command-line option only; Ctrl+T, Cmd+T and F3 do nothing), `8.8` (Ctrl+2 and Ctrl+Tab do not switch the team; the setter still does), `9.5` (no runtime grid toggle), `9.7` (Ctrl+T does not toggle the
  grid), comment of a `test_network_app` case. Version assertion of 12.108.

## v0.0.50 - 2026-09-29 - Teaming works: the three answer dialogs of the original

- **The problem**: the simulation could make and break teams (and the news, cues and chat lines were right) but nothing in the game could *answer* an offer: the original's three modal dialogs did not exist, so an
  invitation from another player could never be accepted, in a network match or with the local team switch, and "team chat" had nobody to talk to. Decoded from `Ants.exe` (`FUN_01015b65`, `FUN_010160e2`,
  `FUN_01016438`, their button callbacks and key handlers) and ported as they are:
  - **The invitation**: "Bob (Red) invites you to form a team.  Would you like to accept?" (or, when accepting ends your present team, "... This will remove you from the team you have with ...") with **Accept**
    and **Decline** (keys `A`; `D` / `Esc`), the allypro cue on the arrival. Accepting with another ally first breaks that team, then makes the new one.
  - **The waiting dialog** of the proposer: "Waiting for Bob (Red) to respond to your offer to team up." with **Withdraw** (keys `W` / `Esc`); it closes when the answer arrives ("Bob accepted teaming up" /
    "Bob rejected teaming up") or the offer is taken back (the invitee reads "... withdrew offer to team up").
  - **The confirmation** "Doing this will break your team with Bob (Red).  Continue?" with **Yes** / **No** (keys `Y`; `N` / `Esc`), which opens when a player who already has a team clicks the ally pedestal of
    another hill, and when an attack order is given on an ant or the hill of the own ally (Yes breaks the team and gives the order, No drops it; before, the ant just walked next to the ally).
  - The dialogs are the original's windows: the `std_dialg` art at (100, 100), the label at (30, 10) in 24 px, the buttons' three animation frames at their offsets; a dialog takes every key and click until it is
    answered.
- **Network**: the dialogs follow the shared simulation's state (`WorldState::pending_invite_from`), so every machine shows the same question at the same tick and it cannot be missed; the answers are
  commands through the turns. A player that drops out takes its offers with it. Team messages reach only the sender and the players whose ally the sender is, written in the sender's team colour; with the
  dialogs a team can now be formed and broken in a real match, so the chat rule is reachable (the audit of the chat colours, the team filter and the roster names found them matching the original: docs 5.42).
- **The refusal cue**: the player who declines an offer now hears the refusal cue (allynot) as well, not only the proposer: the original's answer message runs on both machines and plays it on each (`0x1023c53`,
  `0x100c4bc`).
- **Docs**: `docs/GAME_REVERSE_ENGINEERING.md` 5.42 (the dialogs: geometry, buttons, keys, callbacks, what opens them), 5.44 and 5.45 (the open items), 5.11 (the early paraphrase corrected), README, `docs/NETWORK_PORT.md`.
- **Known deviations** (recorded): one pending offer per invitee (the original queues several invitations); after the attack confirmation the whole selected group gets the order (the original re-issues the first ant's).
- Tests: `test_hud_layout` `test_alliance_dialog_layout` and `test_alliance_answers` (every button, key and text of the three dialogs, the state-driven open and close, the commands that the answers issue),
  `test_network_app` N5.9 (an offer from another machine reaches the application as the question, Accept makes the team on three machines, a team message reaches only the ally and in its colour) and N5.10
  (the waiting dialog, a refusal, Withdraw). **Rewritten tests**: `12.62` of `test_app_integration` (the old test expected the walk next to the ally that the remake did instead of asking; it now checks the confirmation:
  No keeps the team, Yes breaks it and the ant goes to attack). Version assertion of 12.108.

## v0.0.49 - 2026-09-29 - The Franklin Gothic look, and no placeholder labels in the browser build

- **The text face**: the original's labels are set in "Franklin Gothic Medium", a commercial font that the game never shipped. The game now bundles **Libre Franklin Medium** (a free interpretation of the same Franklin
  Gothic; SIL Open Font License 1.1, `Original-Ants/LibreFranklin-Medium.ttf` with its licence in `Original-Ants/LibreFranklin-OFL.txt`, 142 KB) and uses it everywhere, in the native game and in the web build (the
  bundle is packed from `Original-Ants/`). A real copy of the original font (`Original-Ants/framd.ttf` or the Windows fonts folder) still wins when it is found. The text sizes of v0.0.48 are unchanged (the cell height
  is measured from whichever face is loaded); the letters are narrower than Arial's, so the setup screen's prompt fits its box again.
- **Arial is no longer in the repository**: `Original-Ants/Arial.ttf` was committed here, which a commercial font must not be in a public repository; it is removed from the tree and from the web image (system
  copies of Arial remain the last fallback when the bundled font is missing). The earlier commits still contain it.
- **Score labels of a local game in the browser build**: the browser build has no multiplayer yet, so the labels "Red:", "Blue:", "Black:" next to the other teams' scores were only placeholders. A local game of
  the browser build now labels the local player and the teams that have a name (`-N<team><name>` / `--team-name`); the native game keeps its four labels, and in a network match every player's real name is shown
  (since v0.0.46). `ApplicationConfig::label_unnamed_teams` decides.
- **Docs**: `docs/GAME_REVERSE_ENGINEERING.md` 5.14 (the face), README (credit and licence of the font).
- Tests: `test_network_app` N5.7 and N5.8 (the label roster of a local game). Version assertion of 12.108.

## v0.0.48 - 2026-09-29 - Text sizes and the health number as in the original

- **Text sizes**: every text of the game now has the size that `Ants.exe` gives it. The original creates its labels with a GDI font whose cell height is set per label (`FUN_0102b05f`, the label
  constructors `FUN_010116cb` / `FUN_01011856`): 12 px for the status line and the chat log, 14 px for the score bar labels and the setup screen's prompt, 18 px for the setup screen's names and map, the results
  rows, 20 px for "Waiting for scores...", 24 px for the dialogs and 35 px for the start dialog's text. The remake used three tiers of 11 / 13 / 15 px for everything, so the start dialog's text was about a third of
  the original's size. `FontSize` is now the cell height (`Px12` ... `Px35`); the renderer opens its font at the point size whose cell height is exactly that (measured from the font itself), and reports it as the
  line distance.
- **The original's word wrap and label drawing** (`FUN_0102b0b5`, `FUN_0102b36a`): the start dialog and the quit dialog are wrapped at the label's width with the original's greedy algorithm and drawn centred, line
  by line, one cell height apart from the top of the label box. Labels that are too wide for their box (names) are cut at the box. The map description of the setup screen sits at the original's y = 380 and the
  players' names at the original's (415, 95 + 50 per seat).
- **The health number** (Ctrl+L) is drawn like the original's: white 8 x 15 pixel digits in a fixed cell (the stock fixed system font of Windows, `SYSTEM_FIXED_FONT`, drawn with `TextOut` at the ant's position), without
  antialiasing. The ten glyphs and the minus sign are built in (hand drawn in the style of that font: the Windows raster font itself is not available); it used to be small proportional text.
- **The face**: the original's font is "Franklin Gothic Medium" (a commercial font that the game never shipped). The renderer now prefers it when a copy is found next to the game (`Original-Ants/framd.ttf` or
  `Original-Ants/Franklin Gothic Medium.ttf`, both ignored by git and by the web image) or in the Windows fonts folder, and falls back to Arial. Franklin Gothic Medium is narrower than Arial, so with Arial some
  lines are wider than in the original.
- **Docs**: `docs/GAME_REVERSE_ENGINEERING.md` 5.14 rewritten (every label constructor call site with its box and font height, the setters of the text object, the wrap algorithm, the health number), README.
- Tests: `test_hud_layout` `test_text_sizes` and `test_label_wrap` (the size of every label of the HUD, the quit and start dialogs, the setup screen and the results screen; the wrap and draw algorithm on a
  fixed-width renderer), `test_render_parity` `test_text_sizes` and `test_fixed_digits` (the real renderer: cell heights, the ink of the letters, the fixed glyphs pixel by pixel). **Rewritten tests**: `12.54` and
  `12.62` of `test_app_integration` use the new size names (their Small / Large tiers no longer exist; 12.62 now uses the map name label's 18 px), the recording renderer of `test_hud_layout` reports the real cell
  height. Version assertion of 12.108.

## v0.0.47 - 2026-09-29 - Network port: host migration (the match goes on when the host leaves)

- **Host migration**: as in the original, where nobody is special once a match runs, the host may leave (or crash, or lose its network) and the others play on. Every machine already holds the whole simulation, so
  only the role of sealing turns moves. While the map loads every guest connects to the guests above its seat (it announces a port in its `Hello`, the host passes the addresses on in `Start`, an inbound link says
  `PeerHello{seat}` and is accepted only from a lower seat of the roster, once, from the address the host reported); every machine keeps the last 300 turns (30 s).
- **Detection and election**: the host's connection closes (everything it sent before is read first) or it is silent for 10 s. The guests elect the lowest seat they still see alive: it proposes `epoch + 1`, every
  guest accepts the lowest seat it sees alive (one candidate per election) or names the lower seat that lives, a guest that had not noticed yet that the host is gone is asked again after a second (three such
  refusals mean that only this machine lost the host: it stops), a guest that does not answer within 3 s is given up, an election that lasts 30 s ends. A link speaks only for its own seat and messages of another
  election are ignored.
- **Resync**: the winner takes the highest position that the accepting guests report, fetches the turns it lacks from the one that has them (a source that dies is given up), becomes the host with the same runner
  (`promote_to_host`), sends every follower the new host's `Resume` and the turns it lacks, and seals on from there. Its first turn drops the old host and every seat that did not follow, so all machines drop them at
  the same tick. Orders given in the last ~300 ms before the host went are lost (`NetGame::submit` reports them as ignored while there is no host, so no acknowledgement sounds).
- **In the game**: the overlay says "The host left. Choosing a new host..." while it happens and "Bob is the host now." (or "You are the host now.") for five seconds afterwards; the events `HostChanged` and
  `PlayerLeft` report it, `HostLeft` now only means that no new host could be agreed (the match returns to the setup screen with "The connection to the other players was lost."); a match with one machine left goes
  on for it alone; once the match is over (`freeze()`) a leaving host is no reason to elect anybody.
- **Protocol version 3**: `Hello` carries the guest's listen port, `Start` carries the guests' endpoints, six new messages (`Propose`, `Accept`, `Refuse`, `Resume`, `Request`, `PeerHello`), every decoder checks its
  ranges (seats, positions, addresses). `Sequencer::resume` starts a new host's sequencer at the resume turn with nobody active.
- **Fixed**: the setup screen's status line follows a state change at once (the notice of a map that could not be loaded appeared one frame late, which made a network test fail one time in six). The v0.0.46 commit
  was missing `tests/test_net/test_netgame.cpp`; it was added in the next commit and the pushed tree was checked with a clean clone.
- **Docs**: `docs/NETWORK_PORT.md` (host migration as built: messages, timings, limits), README (network section, roadmap, test table with the real counts).
- **Limits**: a host that dies in the first second of the match (before the links between guests exist) can split it; guests must reach each other on the addresses the host saw (a LAN, a VPN or forwarded ports;
  the WebRTC transport will remove this); commands in flight are lost; two groups that cannot reach each other each play on for themselves.
- Tests: `test_lockstep` N2.22 - N2.36 (the simulated network: the host dying abruptly and silently, two seats dying together, the successor or the only holder of the missing turns dying mid-election, guests behind or
  ahead of the new host, a silent guest, a partitioned old host, a guest whose own link broke, chat / leave / commands through the new host, three host changes in a row, 20000 garbage messages, the turn log and
  the sequencer's resume), `test_netgame` N3.9 - N3.12 (over real sockets: three- and four-player matches, no election after the match is over, strangers on a guest's port), `test_network_app` N5.6 (the application
  follows the new host). **Rewritten tests** (the behaviour they pinned, "the host leaving ends the match for the guests", is gone by design): `test_netgame` N3.7 (now: the only guest takes over and plays on),
  the last part of `test_network_app` N5.4 (same); extended: N2.1 - N2.3 (the new messages, unknown types start at 24, the fuzzer's type bytes cover them), the version assertion of 12.108; N4.6's forged `Start`
  is built field by field (a compiler warning).

## v0.0.46 - 2026-09-29 - Network port: the game meets the network

- **Host and join**: `--host [port]` opens a room (TCP, port 4001 like the original), `--join host[:port]` joins one, `--loopback` accepts only this machine. The setup screen is the room: a row per player with the
  portrait in the player's colour, the name and a thumb for the connection quality (`netgood` green thumbs up below a 1200 ms round trip, `netok` yellow sideways hand below 1800 ms, `netbad` red thumbs down,
  `netunk` orange question mark before the first measurement; the thresholds are the original's, `Ants.exe` 0x1013289). The host picks the map and the fog and presses START, which needs a second player and
  every thumb ("Press START when all players' thumbs have appeared."); a guest sees the host's choice and can only leave. The status line uses the original's texts ("Waiting for the host to start the game...",
  "Trying to connect to the host..." then "Having trouble connecting to host..." after 30 s and "Unable to connect to host, recommend you quit..." after 60 s, "Loading game...", "Waiting for others...").
- **Names from the command line**: `--name`, the original's `-N<team><name>`, `--team-name <team> <name>` and `-pnum=` reach the room, the HUD's score labels for every player, the results rows, the chat headers
  and the simulation's alliance and drop-out texts. A network game says "Player" unless `--name` is given (it never sends the user and machine name).
- **Matches over the network**: every machine loads the same map file (hash checked) with the same seed, roster and fog; the ticks come from the lock-step runner (no wall-clock ticking), the frame is drawn at the
  runner's sub-tick position; the HUD's orders go through `NetGame` with an immediate predicted acknowledgement (voice and pedestal feedback), chat goes through the host and back with the sender stamped by the
  connection, a waiting / out-of-sync overlay (remake text), no pause, no team switching in a network match, chat and `Leave` work as before.
- **Roster**: teams without a player do not exist (the original's team table holds NULL for them): no hill and no hill art, no start markers, no eggs, no score label, no voice (`SimulationEngine::init(level, seed,
  roster_mask)`, `LevelData::for_roster`).
- **Drop-out** (`FUN_0100d03b`): a player who leaves, is thrown out or is silent for 60 s is dropped by a host-only `Drop` command in the next turn, so every machine drops the team at the same tick: "%s dropped out
  of the game!", the cue, every ant of the team starts its death clip, its alliance ends, its egg in the incubator is lost and nothing hatches for it.
- **Fixed**: the ant that answers a group order is decided by GoTo's real result, as in the original; before, a refused order could still be answered because of a stale path request of the ant.
- **Protocol version 2**: the `Room` message carries every seat's measured round trip (the host pings every guest each second; up to eight pings in flight so slow links are measured too).
- **Build**: `ants_app` links `ants_net`; the test targets list the libraries in link order (no duplicate-library warnings).
- **Docs**: `docs/GAME_REVERSE_ENGINEERING.md` 5.47 (team table, drop-out, CHECKGO end rules) and 5.48 (the thumbs and the setup screen's status texts, decoded from `Ants.exe`), `docs/NETWORK_PORT.md` (the
  application, host migration design), README and the launcher banners, the changelog page on the beta site, hard-coded local paths removed.
- **Limits**: the host leaving ends the match for the guests (host migration is next), raw TCP only (no NAT traversal, no browser build yet), the alliance dialogs are not shown, the match ends by the clock only.
- Tests: `test_commands` N1.16 - N1.21 (rosters, drop-out, the predicted acknowledgement: 2000 of 2000 random orders identical), `test_lockstep` N2.18 - N2.21, `test_lobby` N4.9, new suite 2.14 `test_netgame` (10 tests,
  real sockets), the room screen in `test_hud_layout` (555 checks), new suite 3.6 `test_network_app` (5 tests: command line, a headless application as host and as guest, bit-identical matches). No existing test
  rewritten (only the version assertion of 12.108 and the Room codec assertions gained the round-trip values).

## v0.0.45 - 2026-09-29 - Network port: room / lobby protocol and framed TCP transport

- New `TcpConnection` / `TcpListener`: a non-blocking, message-oriented, reliable, ordered `Connection` over TCP (`u32` little-endian
  length frames, 64 KB message cap, 512 KB bounded send queue, bounded reads per pump, `TCP_NODELAY`, no `SIGPIPE`, Winsock support).
  Not built for the browser target.
- New room protocol: `Hello` / `Welcome` / `Reject`, `Room`, `Start` (seed, map name and FNV hash, fog, roster, names), `Loaded`,
  `Begin`, `Cancel`, `Leave`. `HostLobby` / `ClientLobby`: seats in arrival order, the host picks the map and the fog option, a start
  barrier (`Begin` only when every machine has loaded the same map file), plain `.LVL` map names only (no path separators, no hidden names).
- Sessions drop a leaving peer at once; the simulated network closes like TCP so that a final `Reject` is delivered.
- Tests: suites 2.12 (room, 8 tests) and 2.13 (TCP: framing, hostile frames, a real-socket match, 6 tests). No existing test rewritten.

## v0.0.44 - 2026-09-29 - Network port: the lock-step core

- New library `src/ants_net` (pure C++17, no threads, no blocking calls, driven by the main loop with a millisecond clock): the
  `Connection` interface, the wire protocol (bounds-checked decoders that reject every malformed or trailing byte), the host's turn
  `Sequencer` (issuer stamped from the connection, 64 commands per peer and turn, a 100 ms turn in canonical order, state-hash
  comparison, flow control at 30 turns), the `LockstepRunner` (jitter buffer of two turns, one tick every 50 ms, stall at a missing
  turn, double speed while far behind, a state hash every 10th turn), `HostSession` / `ClientSession` (acks, hashes, ping / pong, chat
  relay with the sender stamped by the host, 8 violations and out, drop-out reported once, a desync freezes the match) and a simulated
  network with latency and jitter.
- Tests: suite 2.11 (17 tests, 119,335 assertions): every message round-trips, 400,000 random and mutated messages never crash,
  a host and three clients over 60 ms links play 90 s bit-identically (two seeds) and over 40 / 150 / 300 ms links, a corrupted
  client is named within a second with the differing subsystem, hostile clients are thrown out unnoticed by the others.

## v0.0.43 - 2026-09-29 - Network port, milestone 1: command layer and state hash

- Every player action is a plain-data `Command` (group move / special / attack, Stop, hatch, alliance invite / accept / deny /
  withdraw / break) applied by `SimulationEngine::apply_command`: the issuer is stamped by the transport, a group order keeps only the
  issuer's own ants, tiles are range-checked, an answer needs the invitation it answers, nothing changes after the match. The HUD sends
  its orders through a `CommandSink`, so one code path serves single player and a network match. Byte-exact wire codec and a canonical
  application order (by issuer, stable).
- `SimulationEngine::state_hash()`: FNV-1a 64 over the whole gameplay state in seven named parts, in fixed-width little-endian order.
- Fixed: `Grid::init_from_level` resized instead of resetting the cells, so a second map inherited stale per-cell flags of the first.
- Removed: the alliance auto-accept (an invitation was accepted after 30 ticks by no player at all); an invitation now waits for the
  invitee's own answer (no bots).
- Tests: suite 2.10 (15 tests, 490,504 assertions). Rewritten tests: integration 10.5 (an invitation stays open, a bogus accept is refused).

## v0.0.42 - 2026-09-29 - Original keyboard, button class and always-active chat

- The keyboard is the original's (`FUN_0102609a`) and nothing more: a dialog takes every key; the chat box is always active and takes the
  printable keys; F1 quick help, F9 - F12 quick messages, Enter sends, Esc deselects (no quit dialog); Ctrl+A select all, Ctrl+H home
  hill (no hatching), Ctrl+L hit-point digits, Ctrl+N / Ctrl+P next / previous ant, Ctrl+O options, Ctrl+Q quit, Ctrl+S stop.
- Buttons follow the original's button class: a press captures (the click sound plays at the press), the action runs at the release
  while the pointer is still on the button, leaving cancels it.
- Removed as invented: chat focus by click or Enter, Esc opening the quit dialog, Space / H / A / N / P / M / C letter hotkeys, Ctrl+H hatching.
- Rewritten tests: integration 7.5, 9.7, 11.3, 12.58 and the status-message chat checks.

## v0.0.41 - 2026-09-29 - Original pointer model

- A click is decided by the cursor mode found at the release point: 1 deselect, 2 select, 3 / 7 move, 4 special, 5 attack. Ant hit boxes
  are the original's (3 x 3 tile scan, last box wins, no filters), the cursor table follows `FUN_01026aa3`, special targets follow
  `FUN_01026f91`.
- A left drag of at most 4 px is a click at the release point; a bigger one is the red 1 px rubber band that selects your ants by
  positive-area overlap (Shift adds in panels 3 and 4). The right button gives its order at the release, at the press point's tile.
- Pedestals: the Move and ability pedestals only latch (a visual state), Stop stops the ants, flashes, locks the mouse for 250 ms and
  deselects, hatch exists only while eggs remain, the ally pedestal only with more than two players.
- Removed as invented: the armed order mode and its hotkeys, 18 x 24 px ant boxes with filters, the "smart ability" right click.
- Tests: suite 3.5 (pointer model, 329 checks with the keyboard and button tests of v0.0.42); rubber-band drawing checks in the HUD layout suite.

## v0.0.40 - 2026-09-29 - Original input task and edge scrolling

- The view scrolls like the original's input task (every 50 ms): eight 12 px edge strips show the scroll arrows, only the 5 px inner
  strips scroll, the step is `scroll rate + 10` px around the target point of the pointer. Holding the left button on the minimap
  scrolls to the point under the pointer; nothing scrolls with the keyboard or the wheel.
- Removed as invented: the continuous 480 px/s pan, the 13 px band, edge panning while a dialog is open, the minimap jump.
- Tests: suite 3.4 (input model, 70 checks, 1,152 golden samples of the original's scroll code). Rewritten tests: integration 7.6, 7.7.

## v0.0.39 - 2026-09-29 - Original alliance texts, News Flash lines and the chat log

- Alliance protocol texts, sounds and channels as in the original (invitation question, "accepted / rejected / withdrew", "A team has
  been made.", "... are a team now!" / "... are no longer a team!", "... dropped out of the game!").
- The chat log keeps the original's entries: a header in the sender's team colour and a body of up to 100 characters, wrapped and
  indented, on a 12 px grid; chat needs the "Participate In Chat" option; F9 - F12 send the quick texts; a team message reaches only
  the sender and the sender's allies.
- Removed as invented: the "Alliance proposed / formed!" status messages, the 120-character input, the 50-line cap.
- Tests: the status-message suite grows to 255 checks. Rewritten tests: integration 9.7, 12.9, 12.54, 12.68.

## v0.0.38 - 2026-09-29 - Original status line, selection and order texts, voices, CHECKGO time warnings

- The status box under the unit card is one slot (`PostStatus`): a post replaces the text, it lives 5 s, six messages flash first; idle
  is empty. One table of the original's strings by id; every simulation message carries its id.
- Selection and order texts and voices follow the original (worker, combat ant, thief, bomber, fire ant, swimmer); only the closest ant
  of a group order answers.
- The match clock is checked every 200 ms like the original's CHECKGO task: warnings at 1:00, 0:30 and eleven countdown steps from 0:10;
  the match ends within 200 ms after 0:00.
- Tests: new suite 3.3 (status messages, 214 checks at the time). Rewritten tests: integration 6.1, 9.6, 12.31 and others.

## v0.0.37 - 2026-09-29 - Original food

- Map Block 2 is a table of food objects (anchor, units, points per unit, stage tiles), not respawn schedules; nothing respawns.
- Harvest: an ant is ordered onto the pile, plays the `?gf` grab clip (action 5); when the clip ends one unit is taken, the ant carries
  its points, "Got Food!" is posted and it heads home; an ant that already carries food answers "Can't - already have food.".
  A lunchbox is a food object of one unit. Food footprints are generated from `ants.chd` (`tools/gen_food_footprints.cpp`).
- Tests: new suite 2.9 (food actions, 21 golden cases). Rewritten tests: integration 9.1, 12.5, 12.47, 12.61, 12.63.

## v0.0.36 - 2026-09-29 - Original attack orders, pick rectangles and knock-back presentation

- A click on an enemy ant is the original group order with the attack flag: ants already attacking that tile are skipped, the nearest one
  answers. Before, every click restarted every ant, so spamming clicks kept the attacker from arriving.
- Ants are picked by the original sprite boxes; action clips are shown at the frame the original's real-time player shows; a thrown
  ant no longer vanishes at the screen edge; frozen (dud) ants are not drawn; the pile-up dust cloud is back; stun.wav is no longer
  heard after every hit.
- Tests: integration 12.142 - 12.145 and render / combat / movement checks. Rewritten tests: integration 12.74, 12.103, 12.28.

## v0.0.35 - 2026-09-29 - Original power-ups

- A power-up is taken only when a move or power-up order ends on its tile (action 4, the 770 ms `getpow` clip; the type changes at once,
  hit points stay). A "can't go" order given while the ant crosses into the tile cancels the pick-up: the ant stands on the power-up and
  cannot be attacked (power-ups are solid for paths, attacks and knock-backs). The old power-up is dropped on a free neighbour tile.
- Removed as invented: the 6-tick pick-up dwell, pick-up under any idle ant, the 15-tick transformation.
- Tests: new suite 2.8 (power-up actions, 16 golden cases). Rewritten tests: simulation 13.6 - 13.8, combat 6.4, integration 12.21 - 12.29.

## v0.0.34 - 2026-09-29 - Original abilities

- Plant, defuse, ignite, extinguish, bridge build and demolish are the original's action clips with their frame sounds; the world changes
  when the clip's old action is cleaned up; a melee hit or stun cancels an ability. A refused order plays the can't clip with
  "Can't do that...". Fire walls need grass, sand or dirt (never mud).
- Removed as invented: ability tick states and timings, the ability cooldown, the detonation of an ant standing on the target.
- Tests: new suite 2.7 (ability actions, 17 golden cases). Rewritten tests: integration 12.55, 12.56, simulation 5.3, 8.1, challenger 4.1, 5.1.

## v0.0.33 - 2026-09-29 - Original combat

- Melee follows the original's action model: contact is the attacker's step into the target tile (no range, no cooldown), hit points are
  lost at contact, the strike frame throws the victim (`gh` one tile, combat ant `gb` four tiles), landing blocks in the original order
  (bomb, pile-up dispersal, fire wall, water), stun only after bomb flights, deferred death with the death clips, the free hatch of a
  team's last ant. The combat ant's only AI is the original auto-engage; the guard-post AI is gone.
- Removed as invented: the physics engine, bounce and scuffle states, attack cooldown, pursuit, instant death, melee immunity flags.
- Tests: new suite 2.6 (combat actions, 24 golden cases). Dozens of old tests rewritten to the verified behaviour (listed in the commit).

## v0.0.32 - 2026-09-29 - Original hill actions

- Entering the own hill is the original's single clip; food scores and the ant heals when the clip ends; the waiting queue is the original's
  (`ANTHILLQ` every 200 ms, the ring, first come first served); hatching takes 8 s, costs `min(200, score)` and spawns a worker with the
  `aghatch` clip; a thief raid plays `atcr501` for 3.5 s and moves the loot at the end.
- Removed as invented: the underground flag and emergence invulnerability, the slot table queue, the hatch key.
- Tests: new suite 2.5 (hill actions, 13 golden cases, 321 assertions). Rewritten and retired tests are listed in the commit.

## v0.0.31 - 2026-09-29 - Original ant sprite rules

- Mirrored parts are drawn one pixel right of the plain flip, the held bomb is the neutral `2bomb.bmp`, selection and hill markers, the
  click marker and score bubbles are children of the view container (drawn over ants, foliage and fog). The invented ant shadow and hop
  are removed. The table generator now extracts the action clips of `SetAction` from `Ants.exe`.
- Tests: render parity 214 checks; movement-table counts grew with the generated table.

## v0.0.30 - 2026-09-29 - Original button states, click sounds, option defaults and setup screen layout

- Buttons are drawn from the original three-state animations (up, hover, pressed); click sounds follow the pressed animations (sound 0,
  sound 89 only for the pedestals, silent toggles); option defaults are the original's (sound 100, music 65, scroll 50, chat on, quick
  help on) and the quick-help option decides whether the startup help is shown; the setup screen takes its controls from the original animations.
- Tests: HUD layout 520 checks. Rewritten test: integration 12.75.

## v0.0.29 - 2026-09-29 - Original HUD shell, minimap painter and cursor rules

- The static HUD is the original composite animation `uishell`; the minimap follows the original painter (119 x 91 palette image,
  speckled class colours, object colour and size table, ants as one-cell dots); the map cursor only applies inside the view rectangle.

## v0.0.28 - 2026-09-29 - Original command-panel pedestals

- The left and right pedestal slots play the original animation chains in real time (rise, sink, icon swap, press), driven by the
  original's 56-row transition table (`pedestal.cpp`).

## v0.0.27 - 2026-09-29 - Original HUD digits, home panel and screens

- The match clock and all scores use the `dig0..dig9` sprites at the original positions; the home panel (egg tray, hatch / ally / stop
  pedestals) and the options, quick help, quit and match-start screens are the original composites; the match-start modal lasts 5 s.
- Tests: new suite 3.2 (HUD layout, 139 checks at the time).

## v0.0.26 - 2026-09-29 - Original effect sprites

- `bombex` for every detonation, burnout / collapse / splash puffs, the death clips `death1..death4`, floating score bubbles with the
  original layout; one stable y-sorted sprite list; effects follow real time.
- Removed as invented: the overhead health bar (the original shows health only through the selection ears).

## v0.0.25 - 2026-09-29 - Original sprite drawing rules

- Animation parts are drawn last stored part first; the colour rule (ants add the team colour offset, everything else uses the raw
  palette); terrain and objects share one template clock per animation id so that all cells of an id animate in lockstep.
- Tests: new suite 3.1 (render parity, 136 checks at the time: every multi-part frame, all six maps, 0 differing pixels).

## v0.0.24 - 2026-09-29 - Frame-exact 1998 ant movement

- Movement re-derived from the disassembly and ported 1:1: no speed constants (an ant moves by the current walk frame's displacement),
  the original SetAction / WalkStep / TryEnterTile / GoTo logic, the asynchronous `PATHMGR` A* (one 1000-expansion slice per 50 ms per
  team), the group order with distance sort and first-ant acknowledgement, blocking with 300 ms waits and the bump effect, the exact
  paces per terrain. Tables are generated from `Ants.exe` / `ants.chd` (`tools/extract_movement_tables.py`).
- Tests: new suites 1.1 (movement tables, parity with the executable), 2.3 (path planner) and 2.4 (movement golden, 22 golden timings).
  Existing tests updated to the verified behaviour (none deleted or disabled).

## v0.0.23 - 2026-09-16

- Power-up pick-up dwell, walk-on / walk-off and can't-go standing (approximation; re-derived from the binary in v0.0.35).

## v0.0.22 - 2026-09-16

- Enemy base targeting, knock-back base landing exclusion, queued click move orders and 1 HP auto-retreat invariants.

## v0.0.20 - 2026-09-14

- Stun animation pacing, bomb deflection, placement detonation, combat locomotion, punch and water drowning. (There was no v0.0.21.)

## v0.0.19 - 2026-09-13

- Bomb knock-back deflection, ant collision bumping, bridge demolition survival, the options scroll-speed slider, asset thumbnails on the web.

## v0.0.18 - 2026-09-13

- Bomb blast fly-back, sidebar pedestal buttons, glow fix, thief bottle-cap invariants, dud stability, base queuing.

## v0.0.17 - 2026-09-13

- Anthill mound bounds, 8-direction random bounces, bomb knock-back momentum.

## v0.0.16 - 2026-09-12

- Airborne knock-back trajectory clearance over ground ants; animated fire ricochet pacing.

## v0.0.15 - 2026-09-12

- Chain-bomb knock-back flight decoupling, fire wall landing recovery, 22-tick bomb defusal pacing.

## v0.0.14 - 2026-09-11

- Uninterruptible power-up transformation, silent rejection of orders during an action, no ability cooldown, friendly bomb path avoidance.

## v0.0.13 - 2026-09-11

- Fire extinguish animation timing, multi-direction ability queuing, bomb stun recovery.

## v0.0.12 - 2026-09-11

- Daisy dropper occupancy, fire ability pathing, 8-way bomb redirection, chain bombs, HUD bomb tile parity.

## v0.0.11 - 2026-09-11

- The 1998 loading screen composite and the Quick Help START button are back.

## v0.0.10 - 2026-09-11

- Bomb cursor, yellow pedestal glow, seamless bomb placement, knock-back facing and the ready modal; compiler warnings of the adversarial asset tests removed.

## v0.0.9 - 2026-09-10

- Web leave / quit reset, bomb dud and scorch, multi-ant food handling, base mound offset and right-flank infiltration.

## v0.0.8 - 2026-09-10

- Version bump with the compose label changed so that the beta container redeploys automatically.

## v0.0.7 - 2026-09-10

- 2 HP ant bounce cancellation fixed, 1 HP retreat timing restored, compiler warnings cleaned up.

## v0.0.6 - 2026-09-10

- 6 s non-dismissable match-start modal (5 s from v0.0.27), mutual friendly bumping, mud humping; a ballistic flight ends cleanly on a
  scuffle bounce; the anchor ant stays visible during a scuffle; `start_game.bat` rebuilds the release binary when code changes.

## v0.0.5 - 2026-09-10

- Action pedestals aligned, bottle-cap thief infiltration, scuffle recoil bounce, empty-steal idle; `c_cant` (animation 39) documented as
  an unused asset.

## v0.0.4 - 2026-09-10

- Windows: launch hang fixed (`midiOutSetVolume`), window raised on start, SDL2_ttf and the Arial font on Windows, Print Screen released.
- HUD buttons, combat guard AI (removed again in v0.0.33), thief infiltration and terrain feedback restored.

## v0.0.3 - 2026-09-10

- Blocked emergence delay from the hill, moving-ally A* passability; reserved anthill mound tiles clarified as blocked tiles.

## v0.0.2 - 2026-09-10

- Anthill geometry, occupied-tile bumping, mud cancel and power-up immunity; project rule 8 (no bot AI) added.

## v0.0.1 - 2026-09-10

- Semantic versioning starts (`v0.0.1`, pre-release); base concurrency and invulnerability parity documented; the web build sources the
  emsdk environment automatically and ignores its build artifacts.

## Before v0.0.1 - 2026-09-06 to 2026-09-09 (the "v0.01" era)

- The first commit is a complete cross-platform remake engine: asset pipeline for `ants.chd` and `Maps/*.LVL`, deterministic 20 Hz
  simulation, audio, map selection, unit health display and test suites.
- Game: setup screen, quit dialog, HUD (team palettes, scores, anthill selection brackets, hatch controls, chat), all six unit types,
  bomber / fire / swimmer / thief abilities, power-up lifecycle, flower droppers, anthill queuing, knock-back, drowning, collision
  scuffles, 8-connected diagonal paths, map durations read from the `.LVL` header, fog of war with the original dither, edge panning
  and cursors, TrueType text, frame-rate meter with a frame-time sparkline, the on-screen version meter (`v0.01`).
- Platforms: native Windows build (SDL2 and WinMM MIDI), WebAssembly port with an HTML shell, Docker + nginx deployment
  (`beta.playants.org`), in-engine MP3 streaming, mobile-friendly page.
- Asset catalog: an interactive web inspector for all sprites, sounds and animations (sortable views, live previews, zoom / pan,
  layer inspection, multiple audio triggers per animation).
- Reverse engineering: automated Table 4 animation extraction, binary struct mapping and the imported Ghidra decompilation of
  `Ants.exe` (`docs/legacy/Ants.exe.c`) as a navigation aid; project rules for Capstone-first verification.
