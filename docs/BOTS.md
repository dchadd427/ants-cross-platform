# Bots (computer players)

**Status: B2 done** (B1, the plumbing: the `ants_ai` library with the controller and the idle bot, `--bot` for local games and for the host of a room, bot seats in lobby, session and `NetGame`; B2, perception and the arena: the complete `BotView`, `MapInfo` (the static analysis of the map, with a conformance test for every rule it takes from the engine), the match runner and the headless `bot_arena`; every bot is still the idle bot until the worker bot of B3). B3 to B6 below are design (update this line with every milestone). The original 1998 game has no computer players: it was an internet game and every ant command came from a person. This document describes how computer players are added without touching the 1:1 core.

## What a bot is

A bot is a **virtual client**: a seat in a match whose commands are produced by a program instead of a person. It sits next to the simulation, never inside it. It reads the world through a read-only view, decides, and sends `ants::sim::Command`s through exactly the door a human uses.

## The rule

Project rule 8 ([`AGENTS.md`](../AGENTS.md)) allows bots as virtual clients, in the `ants_ai` library only:

- A bot uses the public command interface of a human: a `Command` (group move, special, attack, stop, hatch, the alliance commands) through the `CommandSink` of a local game or the sequencer of a room. `SimulationEngine::apply_command` validates every command, so a bot has a human's powers and cannot break a rule.
- A bot never changes the simulation, the original screens, the lock-step rules of the network protocol or any golden state hash. A normal game runs no bot code.
- **Off by default, always visible.** A bot exists only when asked for (command line, room specification). A bot seat is shown as a bot: its name is `Bot (Level)`, the room marks the seat as a bot, and a person cannot take a name that looks like a bot's (the lobby renames them: spaces and case do not matter), while a bot's name always carries the marker. (In a local game `--team-name` / `-N` may rename a seat; a room never does.)
- **Fair.** A bot knows nothing that a human of its team could not know and has a command budget per difficulty level (see below). Fog of War is rarely used; with fog off every player sees all ants, so a bot reads the whole world. **A bot together with fog is refused** until a per-team view exists.
- The only computer behaviour inside the simulation stays the original's own: an idle Combat Ant punches the first enemy within three tiles, then returns to where it stood (the auto-engage reflex).

## Fairness in detail

**What a bot sees (`BotView`).** Its own ants (type, tile, hit points, state, whether it carries food), every other team's ants (type, tile, state, whether they carry food), the numbers of the score boxes (a team's score plus its ally's, never below 0: not the individual scores), its own egg count and incubation, the food piles (remaining units, points per unit), the map, the clock, and an alliance invitation waiting for it. A bot does **not** see other teams' eggs or incubation, other teams' hit points or carried points, or any ant's order. It never reads the news or audio queues (they belong to the screen). (Complete since B2. The units of a pile and the exact egg stock are a harmless deviation: a person sees the pile's picture and an egg tray of at most nine eggs, and counts the bites; `BotView` is the one place to coarsen them if that is ever wanted. `has_pending_path` answers only for the seat's own ants and `predict_ack` only counts them, so neither says what another team's ants were told.)

**How fast a bot acts.** Three limits, all in simulation ticks (50 ms): it looks at the world only every `decision_interval` ticks; every command of one look is released after a `reaction_delay` (plus or minus 25 percent, drawn ONCE per look from the seat's own random generator, so the commands of a look leave in the order they were proposed and a later look never overtakes an earlier one); among commands that are due on the same tick the newer one wins for an ant they both name (like the later click of a person); and a token bucket limits commands per second (one token per command, whatever the number of ants). A command that cannot be paid waits and is dropped after a time-to-live because the world has moved on.

**What a bot may command.** Nothing a human could not click: at most 24 ants per command (a chosen conservative cap: the remake's HUD lets a person send up to 32, and the original's limit has not been established), special orders (bomb, defuse, fire, extinguish, bridge) and thief raids name exactly one ant, no `Quit`, no `Drop`, never an attack on an ally (a break of the alliance comes first and a bot never does that to attack), no command for another seat's ants.

**Honesty.** Bots are deterministic given the match seed and the seat (integer arithmetic, a private random generator, no clock), so a match can be reproduced and a recorded match replays from its commands alone, without any bot.

## What the game looks like to a bot (measured)

Numbers measured on the headless engine (release build) with a PROTOTYPE worker bot that is not in the repository yet (B3 builds the real one and pins the numbers in a test), see `docs/GAME_REVERSE_ENGINEERING.md` for the rules themselves.

| Fact | Value |
|---|---|
| What counts | The score: food deposited, plus loot stolen, minus loot lost, minus 200 per hatch. Kills, losses and ant counts are shown on the results screen but never ranked. |
| Food | Finite, shared, never respawns (the reachable points are pinned by test AI1.15). Points per map: TINY 4800, SMALL 4000 (3000 reachable on foot), MEDIUM 4900, GAUNTLET 1800 (1500 reachable), ISLANDS 5600 (none reachable on foot), TREASURE 10950 (8850 reachable). |
| Harvesting | One group move onto a pile starts a loop that runs by itself until the pile is empty: bite one unit, walk home, enter (22 ticks), score, walk back. The economy needs well under one command per second. |
| The hill gate | A hill banks about one deposit per 100 ticks (5 s) once three ants queue. Income is therefore capped near 11 deposits a minute and extra workers beyond 4 to 6 add nothing (more can lower it). |
| Hatching | Always a worker, costs 200 points and needs 200, 160 ticks, one egg at a time, eggs per team TINY 3, SMALL 2, MEDIUM 6, GAUNTLET 6, ISLANDS 4, TREASURE 9. It does not pay on any shipped map. |
| Typed ants | Only power-ups make Bomber, Fire, Thief, Combat and Swimmer ants: an ant walks onto the power-up and keeps the type for life. |
| Fighting | Every ant has 10 hit points; one blow per order (1 hp, a Combat Ant 2 hp); an ant at 1 hp walks home and heals. Fights are decided by who strikes first. |
| Thieves | A raid takes up to 50 points from a hill and cannot be interrupted at the raid tile; three fire walls (or bombs) on the tiles east of the hill stop it. |
| ISLANDS | No hill can walk to any food. A bomber plants a bomb inland of a shore tile and an ant that walks onto it is thrown over the channel. |
| Contested play | With four players the whole reachable pot is exhausted on every shipped map, so scores are shares of a pot. |

## Architecture

```
SimulationEngine --(const)--> BotView (+ MapInfo, built once) --> Bot::think(view, orders)
                                                   |
                          BotController: schedule, reaction delay, budget
                                                   |
        CommandSink, issuer = the bot's seat --> apply_command / the sequencer of a room
```

`ants_ai` (namespace `ants::ai`) depends only on `ants_sim`; it has no SDL, no sockets, no threads and no clock, so it builds for the web too. Adapters that talk to the network live in the application (`NetBotSink`) and the server.

**What B1 built** (include/ants_ai, src/ants_ai, tests/test_ai): `rng.hpp` (the splitmix64 generator of a seat and its seed), `bot.hpp` (levels and profiles, `BotSpec`, `parse_bot_spec`, `bot_display_name`, `check_setup`, `Priority` / `Intent` / `Orders`, `Bot`, `BotContext`, `make_bot`), `bot_view.hpp` (the minimal view: the clock, the score and alliance state of every team, the ants), `bot_controller.hpp` and `idle_bot.hpp`. `worker` and `standard` are accepted names that run the idle bot until B3 and B4.

**What B2 added**: the complete `bot_view.hpp` (own egg stock and incubator, the food piles, the borrowed grid, `predict_ack`, `has_pending_path`, `map()`), `map_info.hpp` / `map_info.cpp` (hills, walking-cost fields on the engine's own step weights, pile approach and click tile, walker components, power-ups, the trip model), `arena.hpp` / `arena.cpp` (one match with the real engine and the controller: pure, no threads, no clock, no files; the sink latency, recording, the replay without a bot), `tools/bot_arena.cpp` (the command line, the threads, the report; CMake target `bot_arena`, not part of the normal build), and `BotController::map()` / `BotContext::map` (the controller owns one `MapInfo`, built when it is created). Tests: `tests/test_ai` AI1.x and AI4.x, and `bot_arena --selftest` as suite 2.21.

- **`BotView`** is a copy of what the seat can see: the clock, the scores of the boxes, the alliance state, its own egg stock and incubator, its own ants (everything), the other teams' ants (what is on the screen), the food piles. Everything it keeps is a COPY (the engine's `get_world_state()` reference is a cache that is rewritten after any tick or command). Three things are BORROWED from the engine and valid only while the bot thinks: `grid()` (a const reference to the map: terrain, objects, food, power-ups, bombs, fire, bridges), `predict_ack(command, &needed)` (the engine's own prediction of which ant would acknowledge an order, which matched `apply_command` on 600 random orders in AI1.5) and `has_pending_path(ant)` (an order given a moment ago has not been answered by the path manager yet). A COPY of a view has none of them (an empty grid, helpers that answer "nothing"): the type cannot be used to read the engine later.
- **`MapInfo`** is the static analysis of one match, built once from the grid at the start: hill geometry (ramp, hole, queue tile, raid tile, waiting spot), one walking-cost field per hill on the engine's own step weights (`MapInfo::walkable`, `step_cost`, `cost_field`), per pile and team the approach cost and the click tile, walker components (which ants can reach which piles: an ant stranded on another island is never sent to a pile of the hill's island), the power-up list and the trip model (`trip_ticks`). It is a HINT, never a promise: it describes the map as it was at the start, and a bot still checks in the next view that an order did what it meant. `approach_now` asks the same question of the map as it is now (a pile's footprint shrinks as it is eaten, and the click tile of the start may no longer be food).
- **`Bot`** has `think(view, orders)` and `on_command(command, fate, tick)`. A bot never sees the engine's answer to a command (a lock-step room does not give one), so it checks the result in the next view.
- **`Orders`** is the only way out of a bot. Its named methods cannot express `Quit` or `Drop` (`push_unchecked` exists for the tests of the controller's filter, which refuses Quit, Drop and None whatever way they come).
- **`BotController`** owns the bots of one game and is called after every simulation tick: it runs each bot when due, applies the reaction delay and the budget, stamps the issuer, drops ants that died, filters forbidden commands and counts everything.
- **Tasks** (from B3): a bot's wishes become tasks that claim ants, emit orders, check progress from the view and end with success or failure. Everything a task knows can be rebuilt from the world, so a bot may be restarted at any moment.

Bot kinds: `idle` (stands still, for tests), `worker` (harvest only, a fixed yardstick for measuring other bots), `standard` (the bot of the three levels; before B4 it is the worker).

## Interfaces

```cpp
namespace ants::ai {
enum class Level : uint8_t { Easy, Medium, Hard };
struct Profile { /* decision_interval, reaction_delay, jitter_percent, rate_milli_cps, burst, max_ants_per_command,
                    intent_ttl, reissue_cooldown, value_aware_piles, max_ants_per_pile */ };
Profile profile_for(Level);
struct BotSpec { uint8_t seat; std::string kind{"standard"}; Level level{Level::Medium}; };
bool parse_bot_spec(std::string_view text, BotSpec& out, std::string& error);   // "2", "2:hard", "2:idle", "2:worker:easy"
std::string bot_display_name(const BotSpec&);                                   // "Bot (Medium)", "Bot (Idle)"
std::string check_setup(const SetupInfo&);                                      // "" or the reason a game with bots is refused

class Orders {   // move, attack (any number of ants: cut at 32 here, at 24 by the controller), special (ONE ant), stop, hatch,
};               // invite, accept, deny, withdraw, break_alliance: there is no quit() and no drop()
struct BotContext { uint8_t seat; Profile profile; uint64_t rng_seed; const MapInfo* map; };   // no engine: the BotView and the MapInfo are the only windows on the game
class Bot {
 public:
  virtual const char* kind() const noexcept = 0;
  virtual void start(const BotContext&) = 0;
  virtual void think(const BotView&, Orders&) = 0;
  enum class Fate : uint8_t { Sent, Expired, Pruned, Superseded };
  virtual void on_command(const sim::Command&, Fate, uint64_t tick) {}
};
std::unique_ptr<Bot> make_bot(const BotSpec&);                                  // idle; worker and standard are the idle bot until B3 / B4

class BotController {
 public:
  struct SeatStats { uint32_t decisions, intents, released, expired, pruned, superseded, filtered, rejected; };
  BotController(const sim::SimulationEngine&, uint64_t match_seed);
  bool add(const BotSpec&, sim::CommandSink& sink, std::string& error);          // refused: seat not in the match, twice, unknown kind, Fog of War
  bool add(const BotSpec&, std::unique_ptr<Bot>, sim::CommandSink&, std::string& error);   // a bot that is not in the registry (tests, later the arena)
  void on_tick(const sim::SimulationEngine&);                                     // after EVERY simulation tick; nothing once the match is over
  const SeatStats& stats(uint8_t seat) const noexcept;
  const MapInfo& map() const noexcept;                                           // the analysis of the match, made when the controller was created
};
// B2: what the bot reads (bot_view.hpp, map_info.hpp)
struct PileView { uint32_t index; sim::TileCoord anchor; uint16_t remaining, value; bool lunchbox; };   // piles with units left, by index
class BotView {
 public:
  static BotView build(const sim::SimulationEngine&, uint8_t seat, const MapInfo* = nullptr);
  uint64_t tick() const; uint32_t ticks_left() const; uint8_t seat() const; int32_t score() const;   // the number of the score box
  uint8_t ally() const; uint8_t invite_from() const; uint32_t eggs() const; bool hatching() const;   // eggs() and hatching(): the seat's own
  const std::array<TeamRow, 4>& rows() const;
  const std::vector<AntView>& mine() const; const std::vector<AntView>& others() const;            // by id; hit points, carried points: own ants only
  const std::vector<PileView>& piles() const; const MapInfo* map() const;
  bool has_grid() const; const sim::Grid& grid() const;                                            // BORROWED: valid only while the bot thinks
  uint32_t predict_ack(const sim::Command&, uint32_t* needed = nullptr) const;                      // BORROWED, own ants only
  bool has_pending_path(uint32_t ant) const;                                                        // BORROWED, own ants only
};
class MapInfo {
 public:
  explicit MapInfo(const sim::SimulationEngine&); explicit MapInfo(const sim::Grid&);
  static bool walkable(const sim::Grid&, uint8_t team, sim::TileCoord);                           // the engine's can_enter R1, R3, R4, R5 for a walking ant
  static uint32_t step_cost(const sim::Grid&, sim::TileCoord from, sim::TileCoord to);            // the engine's step cost: weights added, x1.4 truncated on a diagonal, halved
  static std::vector<int32_t> cost_field(const sim::Grid&, uint8_t team, sim::TileCoord source);
  const HillInfo& hill(uint8_t team) const;                                                        // origin, mouth, entrance, queue, raid, tile42
  const std::vector<PileInfo>& piles() const;                                                      // per team: Approach {cost, click}; cells; bite_index
  int32_t component(uint8_t team, sim::TileCoord) const; bool can_reach_pile(uint8_t team, sim::TileCoord from, uint32_t pile) const;
  int32_t trip_ticks(uint8_t team, uint32_t pile) const;                                           // 2 * cost * 2 / 5 + 70 ticks, -1 when not on foot
  Approach approach_now(const sim::Grid&, uint8_t team, uint32_t pile) const;                      // the same, for the map as it is now
  uint32_t reachable_points() const; uint32_t total_points() const; const std::vector<PowerUpInfo>& powerups() const;
};

// B2: one match (arena.hpp)
struct ArenaSpec { const assets::LevelData* level; uint32_t seed; std::vector<BotSpec> bots; uint64_t max_ticks /*0 = full*/; uint32_t latency_ticks /*3*/; bool record; /*factory*/ };
ArenaResult play_match(const ArenaSpec&);                                                          // scores, ticks, hash, hash at every 20th tick, the applied commands
ReplayResult replay_commands(const ArenaSpec&, const ArenaResult&);                                // the commands alone, into a fresh engine, without any bot
}
```

`SeatStats` counts commands after splitting: `intents` = queued, `released` = handed to the sink, `expired` = dropped after the time to live, `pruned` = commands that lost ants that died (one with no ant left is never released), `filtered` = refused when proposed, `superseded` = commands that were due together with a newer one and lost every ant to it, `rejected` = released and refused by the sink (0 when the sink is the engine or the sequencer and the bot keeps to what the filter lets through; AI6.7 and AI6.8 check it with a bot that acts, through both of the application's sinks). The controller refuses (counts `filtered`) a command that a person could not click: `Quit`, `Drop`, `None`, an empty list or more than 32 ants, a special order of other than one ant, a group order off the map, an attack on the tile of an ally, an alliance command that names no other seat.

Network additions, all additive, no protocol change (`SlotState::Bot` is in protocol 6): `HostLobby::add_bot` / `remove_bot` / `has_bot`, `HostSession::add_bot_seat` / `is_bot_seat` / `submit_bot`, `NetGame::add_bot` / `remove_bot` / `submit_bot`. A bot seat is a room slot in state `Bot`: it has no connection, never reports a hash, is never dropped for silence and counts as a player. A person whose name starts with "Bot (" is renamed by the lobby, so a name alone never makes a bot, and a bot is never taken for a person.

## Difficulty levels

The economy is nearly insensitive to reaction time and command rate (a harvesting bot needs a few hundredths of a command per second), so levels are defined by the **tactics they use**; delay and rate are fairness limits that bite when fighting.

| Level | Thinks every | Reaction | Commands per second (burst) | Economy | Tactics (from B4) |
|---|---|---|---|---|---|
| Easy | 5 s | 3 s | 0.4 (2) | nearest pile first, at most 4 ants per pile | one island hop on ISLANDS |
| Medium | 1 s | 1.2 s | 1.5 (6) | piles by points per trip | thief and combat power-ups, raids on the leader, guards, swimmer ferry, island hops |
| Hard | 0.2 s | 0.4 s | 3.0 (10) | as Medium | adds fire-wall and bomb defence of the hill, combat squads with a new order for every blow, shore kills, late raids, alliance invitations |

The numbers are proposals, tuned by tournaments in B5.

## Running bots

```
ants --map Original-Ants/Maps/TINY.LVL --bot 1:medium              # you (seat 0) against one bot
ants --player 2 --bot 0:hard --bot 1:easy --bot 3:easy             # three bots
ants --host --bot 2:medium                                         # a room on the local network whose seat 2 is a bot
bot_arena --map TINY,MEDIUM --seeds 1..8 --seat 0=standard:hard --seat 1=standard:easy --rotate --out report.json    # B2: cmake --build build --target bot_arena
```

- `--bot SEAT[:SPEC]` may be repeated. SEAT is 0 to 3 (green, red, blue, black), not the local player's own seat. SPEC is `easy`, `medium` (default), `hard`, `idle`, `worker` or `standard`, or `KIND:LEVEL`. Refused at startup with a message on stderr: a spec that does not parse, a bot on your own seat, two bots on one seat, `--join`. A game started from the setup screen checks again at START (Fog of War is chosen there): the refusal appears on the setup screen's prompt line.
- A game with bots plays with the seats that are taken (you and the bots); empty seats have no hill. The bots are called "Bot (Medium)" etc. (`-N` / `--team-name` names win) on the HUD labels, in the chat log and on the results screen.
- `--host --bot SEAT`: the room shows the bot as a player with the good thumb, a guest that asks for its seat gets the first free one, and the host's machine runs the bot from the moment the match begins.
- Until B3 and B4 every kind is the idle bot.
- Bots run only on the machine that owns them: a local game, a LAN host, or the dedicated server. A guest never runs bots. If a LAN host leaves, its bot seat leaves with it.
- (B6, design: the server has no bot support yet and ignores a `bots` key in a room specification) On the dedicated server a room specification names the bots: `"bots": [{"seat": 2, "bot": "medium"}]`; seats of bots count towards the room's players and at least one seat must be a person.
- (B6, design) The browser build can run bots in a local game (`ants_ai` has no sockets); rooms with bots need the server.
- `bot_arena` is the headless runner (B2): many matches without a window, seats rotated, reports as JSON. Options: `--map` (names, paths or `shipped`), `--seeds A..B`, `--seat N=KIND[:LEVEL]` (kinds idle, worker, standard: **worker and standard still run the idle bot until B3 / B4, and every report says so**), `--ticks full|N`, `--latency-ticks N` (default 3: commands reach the engine at a 100 ms turn boundary like in a room), `--rotate` (every distinct arrangement of the bots over the seats), `--repeat N` (every match N times, identical), `--replay-check` (the applied commands, fed into a fresh engine without any bot, give the same hash at every 20th tick and at the end), `--threads N`, `--out FILE` (the JSON has the map names only, no paths; `--no-wall-time` makes it bit-reproducible), `--selftest`. Exit code 0, 1 (a match that was not played or a check that failed) or 2 (usage). Every match is independent, so the report is the same for any number of threads; a full-length idle four-team match takes 23 ms (TINY) to 160 ms (TREASURE, ISLANDS) on one core of a release build.

## Testing

Built in B1 (suite 2.20, `tests/test_ai`, 22 tests, and eight more in suite 3.6): `--bot` text and names, `check_setup`, the controller (the budget in every window at the three levels, the reaction delay and its reproducibility, the schedule, time to live, ants that died, issuer stamping, the filter, splitting, special orders, allies, priorities, the cool-down, the end of the match, seating refusals), the idle controller leaving every state hash unchanged at every tick on four maps, bot seats in rooms (lobby, acknowledgement without a stall for 1,200 turns, `submit_bot`, a 60 s match with a bot seat on two machines, host migration dropping the bot, a LAN room over real sockets), and the application (command line, roster and names of a local game, results rows, the fog refusal, a room whose host runs a bot). Built in B2 (suite 2.20 grows to 46 tests, suite 2.21 `bot_arena --selftest`): the view (AI1.1 - AI1.7: own and other ants, hidden hit points, carried points and eggs, the piles, copies against borrows, `predict_ack` equal to `apply_command` on 600 random orders, `takes_orders()` against the engine over a harvest loop, a look costs in proportion to the ants), the map analysis with a conformance test for EVERY rule that `ants_ai` takes from the engine (AI1.8 hill geometry; **AI1.9 the flood fill against the engine's PATHMGR on 300 random pairs per shipped map with a fresh engine per pair: 0 disagreements**, and no engine path ever cheaper than MapInfo's optimum; AI1.10 the same around the hills; AI1.11 `walkable` against the engine's acceptance of a goal on every tile near a hill and at random; AI1.12 the step weights by hand and by walking time; AI1.13 and AI1.19 the path finder's limit of 8,000 on a snake of corridors; AI1.14 random terrain; AI1.15 reachable points per map pinned; AI1.16 components, stranded ants, power-ups; AI1.17 the click tile starts a harvest loop and the trip model against the measured solo cycle; AI1.18 approach of a pile that shrinks; AI1.20 two objects on one anchor), and the arena (AI4.1 bit-reproducible, AI4.2 a bot match is nothing but its commands, AI4.3 the length of every shipped match, AI4.4 refusals). A mutation check (each rule of `walkable` and `step_cost`, the diagonal factor, the cost limit, switched off in turn) is caught by at least one of these tests every time. The plan for the later milestones:

- Unit tests of the view on hand-made worlds (built: AI2.16, AI1.1 - AI1.7) and of the map analysis against the engine's own path finder (built in B2: AI1.8 - AI1.20).
- The controller's budget, reaction delay, issuer stamping, filtering and splitting.
- A match with a bot that only reads is **identical for everybody else** (state hash equal at every tick).
- The worker bot beats the idle bot on every shipped map by a margin (ISLANDS from B4a).
- Bot seats in rooms: counted, acknowledged every turn (no stall), commands in the turn stream, identical simulations on host and client, dropped with a host that leaves.
- `bot_arena` is bit-reproducible for a fixed seed; the commands of a bot match replay on a fresh engine **without** any bot and give the same state hash (built in B2: AI4.1 - AI4.4 and `bot_arena --selftest`).
- A sweep over the community maps (B5): no crash, no hang, no breach of the budget, idle where nobody can earn.

## Prototype measurements (Medium worker bot, seed 1, release build; not reproducible from the repository until B3: `bot_arena` exists since B2, but `worker` still runs the idle bot)

| Map | Alone vs idle bots, 2 min | Alone, full match | Four bots, full match (mean) |
|---|---|---|---|
| TINY | 615 | 1718 | 1200 |
| SMALL | 556 | 2025 | 750 |
| MEDIUM | 380 | 1809 | 1225 |
| GAUNTLET | 124 | 844 | 371 |
| TREASURE | 502 | 2934 | 2212 |
| ISLANDS | 0 | 0 | 0 |

Seat matters: alone on TINY the four seats score 1410 to 1980. With four bots the scores add up to the whole reachable pot on every map. A 12,000-tick match with four bots runs in about 0.2 s.

## Roadmap

Each step is a release with the full test suite.

- **B1 plumbing (done)**: `ants_ai` skeleton, controller, idle bot, `--bot`, bot seats in rooms.
- **B2 perception and the arena (done)**: `BotView` complete, `MapInfo`, the match runner and `bot_arena`; see [`docs/audit/B2_notes.md`](audit/B2_notes.md) for what was measured and where it differs from the design.
- **B3 economy**: the worker bot, pinned baselines.
- **B4a abilities that earn**: island hops (ISLANDS), the swimmer ferry (SMALL).
- **B4b tactics**: the standard bot with its three levels: power-ups, raids, defence, fights, alliances.
- **B5 tournaments**: tuning by self-play, regression ladder, community-map sweep.
- **B6 integration**: server room specification, results and replays, the match API, web.

## Known limits

- Host migration drops a LAN host's bot: the bot lives in the old host's process and has no endpoint, so the new host drops its seat in its first turn (the survivors continue identically); a dedicated server never migrates.
- Fog of War with bots is refused. A per-team view would need the engine to keep a reveal mask per seat.
- Until B4b a bot answers no alliance invitation (the idle bot says nothing: an invitation stays pending until the human withdraws it); from B4b it declines them; an alliance of all live teams with a positive combined score ends the match within a fifth of a second, so the rules for accepting are deliberate.
- Many community maps give walking ants no food at all; a bot idles gracefully there.
- The reference numbers depend on how the hill banks deposits; if that part of the simulation changes, the baselines are regenerated with `bot_arena`.
- `MapInfo` is the map as it was at the start of the match. Bombs, fire walls, bridges and eaten power-ups change the real map; a bot re-checks in the next view (`approach_now` asks again for one pile). On the six shipped maps the flood fill is exact (0 disagreements with the engine's path finder in 18,000 random pairs); on the 586 community maps it was measured at about 93 percent, so it is a hint and failure learning (an order that came back `Applied` and changed nothing) stays mandatory.
- The engine's path finder gives up when a path costs 8,000 or more (a long maze, mud all the way): `MapInfo` offers no approach to such a pile although a flood fill reaches it. It is also not exact: about 3 to 6 percent of the paths it finds cost a little more than the best (worst measured +11.8 percent), so costs are estimates.
- Two objects of the table on one anchor (two on TREASURE, neither reachable on foot): the last one owns the cells and sets the points of a bite, the first one loses the units. `PileView::remaining` lists both; `MapInfo` (`cells` empty, `bite_index`) says which can be clicked and which is bitten.
- The trip model is the cycle of ONE worker (there and back at 0.4 tick per unit of cost, plus 70 ticks): within -3 to +22 percent of the measured solo cycle for every (hill, pile) pair of the five maps with food on foot (the constant 70 is a little high for a lone worker: 45 fits within 8 percent). With several workers the hill gate takes over (about one deposit per 100 ticks), and the per-ant cycle is the number of ants times that.
