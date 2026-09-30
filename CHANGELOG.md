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

- (nothing yet)

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
