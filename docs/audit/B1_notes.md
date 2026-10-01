# Bot plumbing (B1): what was built

Update for section 50 of `implementation_plan.md` (that file is not part of the repository; the owner copies this text into it). Milestone B1 of the computer players, as designed in [`docs/BOTS.md`](../BOTS.md). Status: built, all suites pass, released as v0.0.85 (version.hpp, the version test, the README and the CHANGELOG entry carry it), after a review of its own that found 22 points, all fixed (see the CHANGELOG).

## Built

| Item | Where |
|---|---|
| `ants_ai` static library (depends on `ants_sim` only; no SDL, no sockets, no threads, no clock; listed in the root CMake before `ants_app`, also for Emscripten) | `include/ants_ai/`, `src/ants_ai/` |
| `BotRng` (splitmix64) and `seat_seed(match seed, seat, kind)` | `rng.hpp` |
| `Level`, `Profile` (Easy / Medium / Hard: look every 100 / 20 / 4 ticks, reaction 60 / 24 / 8 ticks, 0.4 / 1.5 / 3.0 commands per second, bursts 2 / 6 / 10, 12 / 24 / 24 ants, time to live 120 / 60 / 60 ticks, cool-down 10 ticks), `BotSpec`, `parse_bot_spec`, `bot_display_name`, `check_setup`, `Priority`, `Intent`, `Orders`, `Bot`, `BotContext`, `make_bot` | `bot.hpp`, `bot.cpp` |
| `BotView` (minimal: clock, scores, alliance state, rows, own and other ants) | `bot_view.hpp`, `bot_view.cpp` |
| `BotController` (schedule with per-seat phase, reaction delay +-25 % from the seat's generator, token bucket in thousandths of a command with an exact remainder, priorities, time to live, pruning of dead ants, splitting at 24 / 12 ants, HUD-parity filter, issuer stamping, anti-thrash, `SeatStats`) | `bot_controller.hpp`, `bot_controller.cpp` |
| `IdleBot` (header only); `worker` and `standard` are accepted names that run it until B3 / B4 | `idle_bot.hpp`, `make_bot` |
| Application: `ApplicationConfig::bots` and `startup_error`, `--bot SEAT[:SPEC]`, roster = local seat + bot seats (`load_match(roster)`), names from `bot_display_name` (an explicit `-N` / `--team-name` wins), `check_setup` at startup and again at START (fog, seat clash, `--join`), bots built after `sim_.init`, `LocalBotSink`, the controller hook as the last statement of `post_tick`, teardown in `return_to_map_select` / `net_end_session` / a new `start_game`, replay of a local game with bots | `application.hpp`, `application.cpp` |
| Rooms: `HostLobby::add_bot` / `remove_bot` / `has_bot` (round trip 0, `measured` true, fog and bot refuse each other in `set_fog` / `add_bot` / `start`, a person named "Bot (" is renamed, the host's own name too), `HostSession::add_bot_seat` / `is_bot_seat` / `submit_bot` and the acknowledgement of bot seats in `run_local` (also for a host without a seat), `NetGame::add_bot` / `remove_bot` / `submit_bot`, `--host --bot` through `NetBotSink`; no wire change | `lobby`, `session`, `netgame` |
| Tests | `tests/test_ai` (suite 2.20: AI2.1 - AI2.16, AI5.1 - AI5.6; 22 tests, 65,930 assertions) and AI6.1 - AI6.8 in `tests/test_app/test_network_app.cpp` (suite 3.6 now 26 tests, 431 assertions) |
| Documents | `docs/BOTS.md` (final design, "B1 done"), README (option, Bots section, suite rows, directory list, diagram), CHANGELOG (v0.0.85 entry), `docs/NETWORK_PORT.md` (bot seats), AGENTS.md rule 8 (no guard post patrol: the only computer behaviour is the original's auto-engage), three stale comments (`ant_unit.hpp` twice, `sim_engine.hpp`) |

## Decisions that the design left open

- `Orders` cuts a list of more than 32 ants into several intents (the wire limit) instead of truncating it; the controller then splits at its own cap. A raw command with more than 32 ants (only possible through the test-only `Orders::push_unchecked`) is filtered, not split.
- `SeatStats::pruned` counts commands that lost ants (partly or wholly); `intents` counts queued commands after splitting, so `intents = released + expired + wholly pruned + waiting` always holds (tested).
- The controller refuses a bot when Fog of War is on (a fourth layer next to `check_setup`, the lobby and `NetGame`), and falls silent for a seat whose team dropped out.
- `BotController::add` has a second overload that takes a ready bot (the tests' scripted bots; the arena of B2 will use it).
- On the host's setup screen a click on "Fog of War: On" with a bot in the room is refused by `NetGame::set_fog` (the status line says why) and the screen shows Off again.
- A local setup screen shows only the local player (the original's screen is unchanged); the bots appear from the HUD on.

## Verified

- A controller whose bots only read changes no state hash at any of 3,000 ticks on TINY, MEDIUM, TREASURE and ISLANDS (idle bots at Hard level, three seats, a person playing identically in both games).
- A host, a guest and a bot seat play 60 s with equal hashes on both engines and 40 or more bot commands in the turn stream with the bot's seat on both; a bot seat is acknowledged for 1,200 turns without a stall with and without a seat for the host (without the acknowledgement the sequencer stalls after 31 turns); a LAN room with a bot over real sockets stays bit-identical for 30 s; when the host leaves, the new host drops the bot seat in its first turn and the survivors stay identical.
- Every existing suite passes unchanged (golden suites and the state hash tests included); the build is clean with `-Wall -Wextra -Werror -Wsign-conversion` on top of the project's `-Wpedantic -Wconversion -Wshadow`.

## Open

- B2: the view has no map, eggs or food piles yet; `MapInfo` and the headless `bot_arena` are not built.
- B3 / B4: `worker` and `standard` run the idle bot, so a game with `--bot` has opponents that stand still.
- B6: the room specification of the dedicated server (`"bots": [...]`), the results rows' bot flag for a lobby, replays.
- Host migration drops the bot of a LAN host (documented, tested); a dedicated server never migrates.
- `src/ants_sim/sim_engine.cpp` (two comments) and `movement_system.cpp` (one) still mention a "guard AI" / "guard post"; the design listed only three comments, so these were left alone.
