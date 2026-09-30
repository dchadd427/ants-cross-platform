# Network Port

Status: **milestone 1 of 5 shipped (v0.0.43): the command layer and the state hash.** Nothing crosses a network yet; a single-player game applies its commands at once.

## What the original does

See `docs/GAME_REVERSE_ENGINEERING.md` 5.46: an external lobby starts the exe with the roster on its command line, WinSock 1.1 blocking TCP in a full mesh, and every machine simulates only its own team
and broadcasts the results (the game is not lock-step). Its lobby flow, texts and rules are kept; its transport, trust model and sync model are not.

## The remake's design

* **Sync model: deterministic lock-step of player commands** (the Age of Empires / StarCraft / Warcraft III lineage). Every machine simulates everything with the same shared PRNG; only intent crosses the wire.
  A turn is 2 ticks (100 ms) and commands are applied two turns after they were given (the input delay adapts to the round trip, capped); the local player gets immediate click feedback (voice, marker,
  pedestal flash) from a prediction on its own state. The commands of a turn are applied in canonical order (by issuer, stable). Every 20 ticks every peer hashes the gameplay state; a mismatch freezes the
  match and dumps the per-subsystem hashes.
* **Commands** (`include/ants_sim/command.hpp`, milestone 1): `GroupMove`, `GroupSpecial`, `GroupAttack`, `Stop`, `Hatch`, `AllianceInvite`, `AllianceAccept`, `AllianceDeny`, `AllianceWithdraw`, `AllianceBreak`
  (`Leave` and the drop-out stamped by the sequencer come with milestone 3). `SimulationEngine::apply_command` is the only way a player changes the simulation; the HUD sends its commands through a
  `CommandSink` (`HUD::set_command_sink`; null means the engine itself).

  | field | bytes | meaning |
  |---|---|---|
  | type | 1 | `CommandType` (1 .. 10) |
  | issuer | 1 | the player; stamped by the transport, never trusted from a payload |
  | other_player | 1 | alliance commands: the other side |
  | tile_x, tile_y | 2 + 2 | group orders: the clicked tile (little endian, signed) |
  | count | 1 | ants: 1 .. 32 for group orders and Stop, 0 for every other type |
  | ants | 4 * count | ant ids (little endian), in selection order |

  Validation (`command_system.cpp`): the issuer is a player; a group order keeps only the issuer's own ants (each once, order kept); the tile is on the map; an answer to an invitation needs that invitation;
  nothing changes once the match is over. Rejections are reported (`CommandResult::Status`), never trusted, never fatal.
* **Topology and NAT traversal** (milestone 2): a star with the room owner as sequencer; one WebRTC data channel per player (ICE with STUN and a TURN relay as the fallback, DTLS-encrypted, reliable ordered
  channel for commands and one for keep-alive and hashes) so the same protocol runs natively (libdatachannel) and in the browser (built-in RTCPeerConnection); a small WebSocket signaling service for rooms
  and the SDP / ICE hand-shake. TURN credentials are issued per session (the standard TURN REST scheme); the shared secret, host names and STUN / TURN URLs are configuration, never committed.
* **Kept from the original**: the lobby flow and strings (latency thumbs, host picks map and fog and presses START, LOADED and READY barriers, "Get ready" for at least 5 s), a 60 s drop-out timeout, kick,
  quit, alliances, chat (100 characters, team-only filter), no late join, no pause, no bots (the alliance auto-accept of the old simulation is removed).

## State hash (`SimulationEngine::state_hash`)

FNV-1a 64 over the gameplay state in seven parts (engine, players, grid, food, ants, paths, droppers). Everything that can influence a later tick is in it (both PRNG states, clocks, every ant field, the
occupancy grid, every map cell, hills, food objects, statistics, eggs, alliances, invitations, queued path requests, flower droppers); what only presents the simulation (audio and news queues, effects, the
per-viewer fog, names) is not. `tests/test_sim/test_commands.cpp` changes every listed field in turn and demands a different hash, runs two engines with permuted command arrival for 600 ticks and three
seeds and demands the same hash every tick, and demands that an engine that played another map equals a fresh engine (`init()` used to leave stale cell flags behind).

## Milestones

1. **Command layer, issuer and ownership checks, `Stop` as a command, `init()` reset, state hash, removal of the alliance auto-accept, two-engine loopback test: shipped (v0.0.43).**
2. Transport: WebRTC data channels with ICE / STUN / TURN behind one `Transport` interface (native and web), signaling service, loopback and simulated-NAT tests, room and lobby screens.
3. Turn manager, input delay, drop-out stamped by the sequencer, desync handling, the fixed-step lock-step driver (the current tick driver is wall-clock with pause and hot seat).
4. Docker / nginx / TURN deployment files (secrets from the environment).
5. Alliance dialogs (the ally attack confirmation, string 4; the invitation questions, strings 1 - 3), chat transport, CHECKGO elimination rules (the match also ends when nobody is alive or one side leads alone).

Open for milestone 3 and later: a float-determinism audit across native and WebAssembly (the flower dropper's type draw uses doubles), native versus wasm golden hashes.
