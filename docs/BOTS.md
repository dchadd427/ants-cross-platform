# Bots (computer players)

**Status: design only. No bot code exists yet.** The original 1998 game has no computer players; this document describes how they will be added without touching the 1:1 core.

## The rule

Project rule 8 ([`AGENTS.md`](../AGENTS.md)) allows bots as **virtual clients, in the `ants_ai` library only**:

- A bot uses exactly the public command interface of a human: an `ants::sim::Command` (group move, special, attack, stop, hatch, the alliance commands) through the `CommandSink` of a local game or the sequencer of a room. The simulation validates every command (`SimulationEngine::apply_command`), so a bot has a human's powers and cannot break a rule.
- A bot never changes the simulation, the original screens, the lock-step rules of the network protocol or any golden state hash. Nothing in a normal game needs bot code.
- **Off by default, always visible**: a bot exists only when asked for (command line, room specification); a bot seat is shown as a bot (name, results rows).
- **Fair**: a bot knows nothing that a human of its team could not know, and has a command budget (commands per second, reaction delay) per difficulty level. Fog of War is rarely used: with fog off every player sees all ants, so a bot reads the whole world; **a bot together with fog is refused** until a per-team view exists.
- The Combat Ant guard post patrol stays the only computer behaviour inside the simulation.

## Architecture

A bot is **not simulation code**. In lock-step a bot never has to be deterministic: it runs on ONE machine (a local game, a LAN host, or the dedicated server) and its commands travel as data, like those of a remote player.

```
SimulationEngine --(read-only)--> BotView --> Bot::think(view, tick) --> vector<Command>
                                                       |                         |
                                                BotController (budget, timing)   +--> CommandSink / sequencer, issuer = the bot's seat
```

- `BotView`: the world as a player sees it (own ants by type and state, eggs and points, hills, food objects, enemies, the map grid), built from `get_world_state()` and the grid.
- `Bot`: the interface (`think`); first implementations `IdleBot` (for plumbing tests) and `WorkerBot` (harvest food and hatch).
- `BotController`: owns the bots of a game, calls them at the right tick, enforces the command budget and reaction delay.
- A bot seat in a room (`SlotState::Bot`, network protocol 6) has no connection: it never reports a hash, never times out and has no thumb.

## Difficulty

Three levels (easy, medium, hard) are parameter sets: reaction delay, commands per second, hatch policy, worker targets, aggression, the abilities used (power-ups, bombs, fire walls, bridges, thieves, raids), whether it attacks hills, how it treats alliances. They are tuned with headless tournaments (a 12-minute match runs in a few seconds without a window).

## How bots will be asked for

- Local game: `--bot SEAT[:LEVEL]` (the original's setup screens have no "add bot" button and stay as they are).
- Rooms and the dedicated server: the room specification (`{"seat": 2, "bot": "medium"}`).

## Tests

Unit tests of the view on hand-made worlds; the controller's budget and timing; a match with an idle bot is bit-identical for the other players; the worker bot beats the idle bot on every shipped map; a headless `bot_arena` runner plays bot against bot over many seeds and maps and must be reproducible with a fixed seed; replays of bot matches.

## Roadmap

B1 plumbing (bot seats, `--bot`, `IdleBot`) - B2 perception (`BotView`) - B3 economy (harvest, hatch) - B4 military and power-ups - B5 tournaments and difficulty levels - B6 integration (room specification, results screen, documents). Each step is a release with the full test suite.
