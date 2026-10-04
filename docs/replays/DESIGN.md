# Replays and Watch mode: design

The owner: "I want replays anyway and I want to be able to see the bots play 1v1v1v1, 1v1 and 2v2."

Scratch design; nothing in the repository was changed. Evidence (what the code has, what the original has, every measurement) is in `FACTS.md`; the pictures are in `mockups/` (`mockups/overview.png` first); the build plan is `PLAN.md`. Written against main `b8d2603` (v0.1.2) and the branches in flight. Paths are repository-relative.

**The idea in three lines.** A lock-step match IS its start data plus its commands, so a replay is that and a few checks. One recorder, fed from the two places that every mode passes (the lock-step runner's executed turns; a local game's one command sink), writes it. One viewer, the normal match screen with no local team, plays it; a watch match is the same viewer over bots that are being recorded.

## 1. The replay file (`.antsrep`)

A small chunked container, like PNG: a reader skips what it does not know, a file that was cut still plays up to the cut, no library is needed. (The original has a recorder and an observer playback of its own, `-O<file>` and `-I<file>`, but of the network messages of a game that is not lock-step: such a file cannot be rebuilt from commands, so the remake's file is its own: FACTS section 2.)

```
file  := magic chunk*              magic = 89 41 52 50 4C 0D 0A 1A
chunk := tag[4] u32 length payload u32 crc32(tag + payload)       little endian; first letter UPPER = critical (refused if unknown), lower = skippable
order := HEAD meta? mapd? CMDS+ hash? rslt? ENDS
```

| Chunk | Holds |
|---|---|
| `HEAD` | `u16 format_version` (1), `u16 engine_rules` (= `net::kProtocolVersion` of the recorder: by the repository's own rule the number moves with every change of a golden hash, so it IS the rules version), then fields `varint id, varint len, bytes` (unknown ids skipped): game version, build id, **map file name and hash** (FNV-1a 64 as `StartMsg`), **seed**, **roster**, fog, **names[4]** (printable ASCII, at most 32), **per seat person / bot and for a bot its kind, level and a free `style` text**, the starting alliances (a hint: the commands are in `CMDS`), the format label ("1v1v1v1", "1v1", "2v2", or empty for a match of people), the planned length in turns, hash period (100), tick length (50 ms) |
| `meta` | **where it was played**: venue (local game, watch, LAN, online, arena), who recorded it (application, LAN host / guest, server referee, online guest, arena tool) and its seat, start time (UTC), an optional server label that only an operator sets. Never an address, room code, token, seat key, chat or the operating system's name |
| `mapd` | optional: the map file itself (19 - 59 KB); off in v1 (the six shipped maps are in every build; a missing map is refused by name) |
| `CMDS` | one chunk per 30 s of game time or 4,096 commands (a crash or power cut leaves a file that plays to the last full chunk) |
| `hash` | a `u32` (low half of `state_hash().total`) after every 100th turn: 144 of them (576 bytes) for 12 minutes |
| `rslt` | final turn count, whether the rules ended it, final hash (u64), per team score / lost / killed / hatched, present mask, allies, quitter, winners: what the Replays list shows without playing |
| `ENDS` | offset of `rslt`, total turns, final hash: the list reads the first 1 KiB and the last 300 bytes of a file |

**Commands.** Each record: `varint gap_turns` (0 = same turn), `u8 head` (type 4 bits, issuer 2 bits, form 2 bits), then for the compact form the tile as two zigzag varints and the ants as a count, the first id and zigzag deltas (the ORDER is kept: the engine's group order depends on it); `u8 other_player` for the four alliance commands; nothing for Hatch, Break, Quit, Drop. Form 1 is the protocol's own wire form (`sim::encode`, 8 + 4 n bytes) for any command that is not canonical (issuer above 3, a stray other_player or tile): every command that `sim::decode` accepts round-trips, the engine's rejected commands are kept as given (a replay is what the engine was told), the order inside a turn is the order of application.

*Why not the protocol's bytes for everything:* measured on real streams (FACTS section 3), wire form with gaps is 2.2 - 3.3 times the compact form (Hard bots: 19.3 KB against 8.9 KB; heavy human play 284 against 95 KB) and the TurnLog packing is 5 to 60 times (two bytes for each of the 14,405 turns, empty or not). The protocol's codec stays the fallback and the test reference. If the owner prefers no new coding at all, dropping form 0 is a one-line change: files double, still tiny.

**Size, measured:** a 12-minute 4-bot Treasure match is **0.5 KB** (worker bots), **0.6 / 1.5 / 8.9 KB** (B4's standard bots Easy / Medium / Hard) of commands; a synthetic human match is 31 KB (2 people, 2 commands a second) to 95 KB (4 people, 3 a second), 183 KB for a rate nobody plays. With the header (about 300 bytes), hashes and results: **1 - 10 KB for bots, 30 - 190 KB for people.** The recorder stops at **900 KiB** and marks the file incomplete (five times the heaviest row; it keeps every stored file below the control interface's 1 MiB response).

**Compression: none in v1.** zlib / xz take another 25 - 40 percent off, need a library on every platform (a download on Windows, a flag on the web, a package in the server image) and make the reader a bomb target. `HEAD` reserves a codec number (0 = none).

**Verification.** The viewer compares the `hash` chunk as it plays and stops at the first difference with a message ("this replay does not reproduce here: it differs at 3:25"); `replay_tool verify` checks all of it at full speed (0.25 - 0.4 s a 12-minute match). 32 bits per check catch engine differences; it is not a signature (section 5).

**Versions: refused, and what else is possible.**

1. `format_version` above the reader's: "made by a newer game (format 2): update". Older formats stay readable (a golden file of each is in the tests).
2. `engine_rules` unequal: refused with the facts: "Recorded by v0.1.2 (network protocol 11); this game, v0.4.0, plays protocol 12. Open it with the game that made it." Header, list entry and `replay_tool info` still work for any number.
3. Possible: (a) a build may declare **playable older rule numbers** (`kReplayCompatibleRules`) after CI replayed the golden files of those numbers on this build and got their hashes: a verified list, never a guess; (b) **golden replays in the repository** (one small file per protocol number) turn the rule "a change of a golden hash raises the protocol number" into a test that fails when a rules change forgets to bump it; (c) the web site may keep old builds on their own paths and the message can name the one to open (operations, not code); (d) the engine is not carried in the file.
4. Another map file under the same name: refused with both hashes. A map that is not installed: refused by name.
5. Damage (a bad CRC, a length past the file, a count past its limit, 33 ants, a byte after `ENDS`, an unknown critical chunk): refused with the chunk named, never a crash (bounded like the protocol's decoders; fuzzed under ASan). No `ENDS`: "incomplete", plays to the last full chunk: "The recording ends here (7:12)".

## 2. Recording, in every mode

In memory while the match runs; written when it ends. On the desktop each full chunk is appended to `<name>.antsrep.part` and the file is renamed at the end. The recorder is observation only: it never changes a hash.

| Mode | Tap | Written by / where |
|---|---|---|
| Local game (single player, with bots), watch | `RecordingSink`: the one sink that the HUD (`set_command_sink`), `LocalBotSink` and `confirm_quit` use; `turn` = ticks run so far | the application at the match end or when the player leaves (kept if it ran a minute or holds a command): desktop `replays/` of the per-user folder (newest 100 / 64 MB, `replays_keep`; `replays=off`, `--no-replay`); web: memory + a Download button |
| LAN host and guests | new tap on `LockstepRunner`, called in `update()` AND `fast_forward()` after each executed turn; it travels with the runner (a host migration keeps it) | each machine writes its copy (same commands and hashes; `recorded_by` differs). The tap is on the CONFIRMED engine: after rollback R2 a predicted engine exists and is never recorded |
| Server room | the same tap on the referee's runner (not `TurnLog`: it exists only with reconnect) | the server at the end of a non-demo room to `<results-dir>/replays/`; rooms still running at SIGTERM write an incomplete file |
| Online guest (desktop, web) | the same tap on its own runner: every client already receives every turn; a rejoiner records what it catches up | the client: desktop folder, web download. **This is how "the web client downloads its own match": no server route, nothing the player did not have** |
| Bot arena | `ArenaSpec::record` converted (`turn = step`, hashes every 5th of its 20-tick ones) | `bot_arena --save-replays DIR [--save-which first\|all\|check-failed\|seeds=LIST] [--save-max N]` (default `first` of each map x arrangement, at most 50; a failed `--replay-check` is always saved); file `TREASURE_s7_hard-medium-medium-easy.antsrep` |

**The server.** Flags `--no-replays`, `--replays-keep N` (200), `--replays-max-mb M` (256), `--replays-days D` (30), `--replay-demo` (off; demo rooms are not recorded: a peer chooses their codes and "nothing may pile up", the rule the result files follow); a room's specification may say `"record": false`. Names are `<room code>.antsrep` (the code's charset is already filename-safe; a repeat gets `-2`), written under a temporary name and renamed; retention after each write and at start: older than D days goes, then the oldest until N files and M MB hold, never the file just written. **The control interface** (bearer secret, unchanged): `GET /replays` (newest first: file, bytes, created, map, format, players with seat / name / bot, turns, complete), `GET /replays/<file>` (octet-stream), `DELETE /replays/<file>`; names must match `^[A-Za-z0-9_-]{1,40}(-[0-9]+)?\.antsrep$` so no path ever comes from a request; `GET /rooms/<code>` and the result JSON gain `"replay": {file, bytes, complete}`. The owner's lobby lists and serves them to its members and may run `replay_tool verify` itself: **statistics from a re-simulation, not from memory scanning.** Nothing is public: no listing, no unauthenticated download.

## 3. The viewer

It runs the same `load_match` as every match (so the engine starts exactly as it did), feeds the recorded commands and wears a spectator HUD (pictures C, C2; today's player HUD is C0, `mockups/compare_c.png`).

* **HUD.** It keeps a *view seat* (the lowest seat that plays) for the frame colour (green as today; a grey frame, `alternatives/c_alt_grey_frame.png`, is not proposed: grey is the black team's), the top-bar score slot and the three bottom ones: **all four score boxes show, with names and the allies' two-tone boxes**; 1v1 shows two and covers the rest as the original does. No pedestals, Stop, own-ant selection or egg tray. **The controls take the pedestals' place** (the card under the minimap): a heading plate ("Watching" with a red LIVE, or "Replay" with its date), pause / play, speeds 1x 2x 4x 8x, the seek bar with "3:30 / 12:00", a jump to the end ("Live" when a live watch looks at the past). The chat input is covered by the original's own chat-off cover (`chatcovr`), "Send to" is gone, the status line shows only a hint under the pointer ("Speed 4x (4)"). Kept: minimap (click or drag), Help, Options, Quit (the original's dialog; for a spectator Yes LEAVES and sends no command), the game's clock, the news in the chat log, the version and frame-rate plate.
* **Camera.** Free: edge scrolling, minimap, wheel zoom 0.5 / 1 / 2 (a spectator is not in a network match, so the zoom-out is offered; 0.5 shows all of TREASURE; a watch starts at 0.5).
* **Keys.** Space pause; `1 2 4 8` speed; Left / Right 10 s; Shift + Left / Right 1 minute; Home start; End end (or live); Ctrl+Q quit as in the original. Nothing else.
* **Input never sends a command.** Three layers, each tested: (1) the spectator's `CommandSink` is a null sink that answers "ignored"; (2) the HUD's order paths are gated by `spectator_`; (3) `confirm_quit` leaves without a Quit command. A test drives 2,000 frames of random clicks and keys and demands zero recorded commands and the same hash as a plain replay.
* **Speeds, sound.** The tick accumulator runs at `dt x speed`, at most 16 ticks a frame; pause stops it (`is_paused_` exists); the music goes on (it is not the simulation's). 1x and 2x play every sound effect (`ingest_simulation_events(events, 255)`: cues aimed at one team are dropped, broadcast ones play); 4x and 8x play none; pause and seek are silent.
* **Seeking.** `ReplayPlayer` takes an engine copy (R1) every **400 ticks** (20 s) while running forward and keeps at most 64 (the spacing doubles past that): 37 for 12 minutes, 7.4 MB on TREASURE (a copy is about 200 KB). Seek to T: restore the nearest copy at or before T (copy assignment keeps the target's memory), re-simulate the recorded commands to T without drawing or the HUD (at most 400 ticks); past the farthest point known, simulate in slices of at most 8 ms a frame ("Seeking" shown) and take copies on the way. **Measured with R1's real copy** (`tools/seek_probe.cpp`, a recorded 12-minute Hard match, 2,000 random seeks forward and back): **0 hash mismatches**; with a copy every 400 turns 2.6 - 3.2 ms on average, 9 ms at p99, 14 - 20 ms at most; every 200 turns 1.1 - 1.5 ms and 14.6 MB; every 1000 turns 5 - 8 ms and 3.0 MB; **no copies at all (restart from turn 0) 80 - 114 ms on average, 264 ms at most**: copies make a dragged bar smooth and are a speed-up, not a requirement (a heavy human match costs more a tick, which is why 400 and not 1000). Afterwards the HUD's chat log is rebuilt from the viewer's own news log (tick + text, filled by the first pass), the engine's cue and news queues are drained and the renderer's transient effects cleared; effects, score bubbles and fog are engine state and come with the copy.
* **Live watch = a replay being recorded.** `WatchSession` owns the live engine and its bots, which run only at the *frontier* (the farthest point simulated). The cursor may go back: the view becomes a replay engine (copy + recorded commands, no bots), the match waits at the frontier, the plate says REWOUND and the jump says "Live" (`alternatives/c_alt_live_rewound.png`); back at the frontier the live engine goes on, deterministically. A click beyond the frontier simulates ahead with the bots at full speed in slices (a Hard match in about 1 s native, 1.3 s wasm) and records as it goes. One bar and one set of controls serve both.
* **The end.** At the end `check_match_over` opens the results page with `rows(255)` (ties by seat order) and the winner fanfare (the defeat sting is wrong for nobody). The page is the approved wide results page with ONE change: the "YOUR SCORE" art becomes an info box built from the same pieces as the other boxes ("Watched match" / "Replay", map, format, length, "Saved to your Replays list"), and the viewer's buttons stand left of the original's Leave Game: **Watch again** (from turn 0) and, for a watch only, **New match** (same setup, new seed). Pictures D, D2, `mockups/compare_d.png`.

## 4. Watch mode: bots only

A local match whose seats are all bots, played by the unchanged `BotController` over a `RecordingSink`, the viewer on top. To the engine it is a normal match: nothing in the simulation, lock-step rules, protocol or a golden hash changes. Rule 8 holds (public command interface, off unless asked for, shown as bots "Bot (Hard)", Fog of War refused as ever); the one new thing is `SetupInfo::allow_all_bots` ("only the arena" today) also set by the watch start, with the reason in the comment.

* **Formats.** `1v1v1v1`: roster 0x0F. `1v1`: two seats (default Green vs Black, the top row of TREASURE, 15 tiles apart); the others have no hill and covered boxes. `2v2`: four seats; at turn 0 the four ordinary commands `invite(a -> b)`, `accept(b <- a)`, `invite(c -> d)`, `accept(d <- c)` go through the recording sink with the seats as issuers (verified), so the replay holds them; default Green + Black against Red + Blue. **Sides** offers what the format allows (six pairs for 1v1, Green's three possible partners for 2v2, Random).
* **Seats.** Level Easy / Medium / Hard / Random (default Medium). **Style** = the bot kind from the registry: Standard, Worker (harvest only, the frozen yardstick), Random; a style B4 adds appears by itself; `idle` is not offered (question 2). Random comes from the match seed by a generator of its own, so a seed repeats a match and the file records the concrete choice.
* **Map.** Treasure by default; the six maps of the Host panel; a map the roster cannot play (`validate(roster)`) is refused on the panel with the reason. **Seed:** fresh for every start (`--seed N` repeats). **Start:** no "Get ready" dialog (nobody can click); the bots' hold follows the product path (`start-clock` makes it "ends when the simulation starts"; the build waits for that).

| Where | How |
|---|---|
| Desktop start menu (pictures 0, A, A2, A3) | two new first-panel buttons **Watch bots** and **Replays** (same size, pitch 60 instead of 66); the panel remembers format, map, sides, levels and styles in the settings (`watch_*`, like `bots`) |
| Replays panel (B, B2, B3) | newest first, two lines a row: date, map, format, length, winner / players with their colours and names; Watch, Delete (asks), Open folder, Back; Up / Down, Enter, Delete, Esc; 8 rows and a scroll bar |
| Command line | `ants --watch 1v1v1v1\|1v1\|2v2 [--map NAME] [--bot SEAT:KIND:LEVEL ...] [--sides SPEC] [--seed N] [--speed 1\|2\|4\|8] [--no-replay \| --save-replay FILE]`; `ants --replay FILE [--speed N] [--at MM:SS]`; `ants --verify-replay FILE` (headless, exit code); `--bot` is the grammar that exists (a seat not named gets `random`); `-I<file>` / `-O<file>` accepted as the original's names for `--replay` / `--save-replay`; `--screenshot` works with all |
| Web (E) | the web game has no start menu; its page gets a **Watch bots** segment (1v1v1v1, 1v1, 2v2) and a **Replay** pair (Open a file..., Download this match) under the picture; addresses `?watch=1v1v1v1&map=treasure&levels=hard,medium,random,easy&styles=standard,standard,random,worker&sides=gb&speed=1` and `?replay=/path/file.antsrep`; every value is whitelisted by a pure function of the page (`ANTS_PAGE.watchArguments`, like `?fill=`, tested by a table of addresses) and passed as `--watch` / `--bot` / `--replay`; a replay address is a same-origin path (`^/[A-Za-z0-9._~/-]{1,200}\.antsrep$`, no `..`, no `//`) that the page fetches (at most 1 MiB) into the game's file system; a picked file goes there the same way and an exported `ants_open_replay` opens it (after "Leave the match to watch the replay?" if a match runs); the game writes `/replays/last.antsrep` when a recording exists and tells the page to enable Download |

## 5. Fairness, security, privacy

* **Who sees what, when.** A replay holds every command, a full record, which every client of a match already receives (lock-step sends every turn to everybody): the file leaks nothing a player did not have. What must not happen is looking at a RUNNING match: the viewer opens files only, a client's recording is unreadable by its own game until the match ends, the server serves a replay only after the room has ended, and nothing offers live spectating of a server room (a protocol change and a fog question: question 5). A spectator sees the whole map without fog, for finished matches only.
* **A replay is not proof.** Anyone can write a file; the engine plays what it is given and the final hash is self-reported. For the lobby only files that THE SERVER wrote count; it may re-run `verify`. A signature (an HMAC with the server's secret in `meta`) is the later step if ranked results ever need a file that left the server.
* **Reader safety.** Every length, count and offset is checked before use (the style of `protocol.cpp`); a file is at most 1 MiB; no compression, so no bomb; the map is looked up by `valid_map_name` in the maps folder only; fuzzed under ASan.
* **Server disk.** At most 900 KiB a file, 200 files, 256 MB, 30 days; atomic writes; demo rooms off; the sequencer's 64 commands a turn bound what a hostile client can add and the cap catches the rest; the control interface's response limit exceeds the file limit.
* **Names** are the only personal data, the names the players typed (every player in the room sees them). A local game's implicit `USER@host` is written as `Player` (a name from `--name` or the settings is kept: the person chose it); file names carry no name; the server holds 30 days, an operator can delete one or switch recording off, a room can opt out; `replay_tool anonymize IN OUT` replaces names by colour words and drops `meta` for sharing. **Chat is not recorded** (team chat is private; chat is not simulation). The desktop keeps the newest 100 files / 64 MB, recording can be switched off, the game uploads nothing. One paragraph on the web page and the server's README says so.

## 6. The pictures (order of `mockups/overview.png`; how they were made and what is real: `mockups/notes.md`)

| | File | Shows |
|---|---|---|
| 0 | `compare_menu_main.png`, `menu_main_new.png` | first panel with Watch bots and Replays, against the approved four-button panel |
| A, A2, A3 | `a_watch_bots.png`, `a_watch_bots_1v1.png`, `a_watch_bots_2v2.png` | Watch bots in the three formats: format, map, sides, level and style per seat, Team A / B, "Not playing", Watch |
| B, B2, B3 | `b_replays.png`, `b_replays_empty.png`, `b_replays_delete.png` | the Replays list, nothing saved, the delete question |
| C, C2, C0 | `c_watch_live.png`, `c_replay.png`, `compare_c.png` | the spectator HUD live and a replay (a 2v2 played online, two-tone boxes), against today's player HUD |
| D, D2 | `d_end_of_watch.png`, `d_end_of_replay_2v2.png`, `compare_d.png` | the end of a watched match and of a replay, against today's results page |
| E | `e_web_page.png` | the web page's Watch bots / Replay bar |
| | `alternatives/` | seek bar in the top bar, a rewound live watch, a paused replay, a grey frame |

## 7. For the owner

1. **Approve or mark up `mockups/overview.png`** (the rule: nothing visible is built before).
2. **"Style"**: read as the bot's kind (Standard, Worker; later kinds appear by themselves). If you meant playing styles (rusher, economy, double-thief ...) they are new bots in B4; the panel needs no change.
3. **Server recording by default** (30 days, names only, demo rooms off, the lobby decides per room): confirm the defaults.
4. **The web bar** under the picture, or should watching live on the front page (`lobby.html`)?
5. **Live spectating of a server room** (with a delay): not designed here; say if it is the next step after replays.
6. **Old replays are refused after a rules change** (the message names the game that made them): fine, or keep old builds online?
7. **Compact command coding** (2 - 3 times smaller than the protocol's bytes, about 150 lines) or the protocol's bytes with gaps: recommended compact.
