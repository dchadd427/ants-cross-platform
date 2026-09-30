# Network Port

Status: **the core, the room and a native TCP transport are done and tested (v0.0.43 command layer and state hash, v0.0.44 protocol, sequencer, lock-step runner and sessions, v0.0.45 room and start
barrier and framed TCP).** The application is not connected to them yet (no host / join interface, no WebRTC / NAT traversal); a single-player game applies its commands at once.

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

## The lock-step core (`src/ants_net`, v0.0.44)

Everything below is pure logic over a `Connection` interface (message oriented, reliable, ordered, non-blocking; WebRTC data channels, WebSocket, framed TCP and the tests' in-memory link all fit) and is
driven from the game's main loop with a monotonic millisecond clock: no threads, no blocking calls.

* **Messages** (`protocol.hpp`, one datagram each: `u8 type`, payload, little endian; every decoder checks every length, count and range and rejects trailing bytes): `Hello` (version, name), `Welcome`
  (slot), `Reject`, `Command` (client -> host), `Turn` (turn number and the sealed commands in canonical order), `TurnAck`, `Hash` (state hash after a turn, all seven parts), `Desync`, `Chat` (100 printable
  characters), `Ping` / `Pong`. Messages are limited to 64 KB and turns to 512 commands.
* **Sequencer** (`sequencer.hpp`, host only): stamps the issuer from the connection (a client cannot speak for another player), refuses inactive slots and floods (64 commands per peer and turn), seals a
  turn every 100 ms with the queued commands in canonical order (by issuer, each issuer's commands in submission order), compares every peer's hash report with the host's own and names the peer and the
  subsystem that differs, and stops sealing while a peer's acknowledgement is more than 30 turns (3 s) behind (flow control; `laggard()` says who).
* **Runner** (`lockstep.hpp`, every machine): queues the turns (in order, no gaps), waits for a jitter buffer of two turns, then executes a tick every 50 ms of real time: a turn's commands are applied before
  its first tick, its second tick follows 50 ms later; it stalls at a turn boundary when the next turn has not arrived and runs at double speed while it is far behind. Every 10th turn it returns the state
  hash.
* **Sessions** (`session.hpp`): `HostSession` (sequencer, broadcast of turns, its own runner, relay of chat, violation counting: a client that sends garbage, host-only messages, oversized messages or floods is
  thrown out after 8 violations, dropping a client reports it once through `on_player_left`, a desync or `freeze()` stops sealing) and `ClientSession` (sends commands, executes turns, acknowledges, reports
  hashes, pings for the round trip, sees a `Desync`).
* **Tests** (`tests/test_net/test_lockstep.cpp`, 17 tests): every message round-trips and rejects any missing or extra byte, out-of-range fields, 400000 random and mutated messages; the loopback link is
  ordered and respects latency; sequencer stamping, canonical order, flood limit, flow control, hash comparison; the runner's buffering, tick cadence, stall and catch-up; whole matches of a host and three
  clients over 60 ms links with jitter for 90 s and over 40 / 150 / 300 ms links, all bit-identical at the end, ticks steady at 20 Hz; a client cannot issue commands as another player; a corrupted client is
  named within a second with the differing subsystem and the match freezes; a frozen peer stalls the game after 3 s and it goes on when the peer catches up; a cut connection reports the drop-out once; a
  hostile client is thrown out without anybody else noticing; chat is relayed with the sender stamped by the connection.

## The room and the start barrier (`lobby.hpp`, v0.0.45)

The original has no host / join interface (an external lobby starts every machine with its roster on the command line). Its rules are kept, the flow is new: a client sends `Hello` (protocol version,
name) and gets `Welcome` (its seat) or `Reject` (`Full`, `VersionMismatch`, `MatchRunning`, `Kicked`, `BadRequest`); the host broadcasts `Room` (seats and names, map file name, fog option, and the
receiver's own seat) on every change. `HostLobby::start` sends `Start` (seed, map file name and FNV-1a 64 hash of the file, fog, roster mask, names); every machine loads the map and answers `Loaded`
(a machine whose file differs answers not-ok); when the host and every guest are loaded the host sends `Begin` and hands the connections (seat -> connection) to the `HostSession`. A load failure,
a leaver, a host cancel or a 60 s load timeout sends `Cancel` and returns to the room; a guest that says nothing for 10 s, sends garbage first, or breaks the rules 8 times is closed. Map names travel
only as plain names of the maps folder (letters, digits, `_`, `-`, `.`, ending in `.LVL`; no path separators, no `..`). Seats go to guests in the order their `Hello` reaches the host. No late join,
no host migration (as the original).

## TCP for LAN and development (`tcp.hpp`, native builds, v0.0.45)

`TcpConnection` / `TcpListener`: non-blocking sockets (POSIX and Winsock), `u32` length + payload framing, a length above 64 KB fails the connection without allocating for it, a sender whose peer never
reads is cut off when its queue passes 512 KB, an orderly close still delivers what was sent before it, `TCP_NODELAY`. It implements the same `Connection` interface as the in-memory link, so the sessions,
lobby and tests run unchanged on real sockets (`tests/test_net/test_tcp.cpp`: message boundaries and order for every size up to the limit, 20000 small messages, hostile length prefixes, truncated frames, dead
ports, the never-reading peer, a 40 s match of a host and three clients over real sockets, and the room from `Hello` to `Begin`).

## State hash (`SimulationEngine::state_hash`)

FNV-1a 64 over the gameplay state in seven parts (engine, players, grid, food, ants, paths, droppers). Everything that can influence a later tick is in it (both PRNG states, clocks, every ant field, the
occupancy grid, every map cell, hills, food objects, statistics, eggs, alliances, invitations, queued path requests, flower droppers); what only presents the simulation (audio and news queues, effects, the
per-viewer fog, names) is not. `tests/test_sim/test_commands.cpp` changes every listed field in turn and demands a different hash, runs two engines with permuted command arrival for 600 ticks and three
seeds and demands the same hash every tick, and demands that an engine that played another map equals a fresh engine (`init()` used to leave stale cell flags behind).

## Milestones

1. **Command layer, issuer and ownership checks, `Stop` as a command, `init()` reset, state hash, removal of the alliance auto-accept, two-engine loopback test: shipped (v0.0.43).**
2. **Lock-step core: protocol, sequencer, runner, host and client sessions, in-memory network: shipped (v0.0.44).** (The plan's order changed: the turn logic needs no third-party code, so it was built and tested
   first, over a simulated network with latency and jitter.)
3. **Framed TCP for LAN and development, the room handshake and the start barrier: shipped (v0.0.45).**
4. The application: host / join interface, the runner instead of the wall-clock tick driver (`HUD::set_command_sink`, a predicted acknowledgement for the click feedback), "waiting for player" and desync
   messages, drop-out elimination stamped by the sequencer, native versus WebAssembly golden hashes.
5. WebRTC data channels with ICE / STUN / TURN (native via libdatachannel, browser via RTCPeerConnection) and the signaling service.
6. Docker / nginx / TURN deployment files (secrets from the environment).
7. Alliance dialogs (the ally attack confirmation, string 4; the invitation questions, strings 1 - 3), CHECKGO elimination rules (the match also ends when nobody is alive or one side leads alone).

Open for milestone 3 and later: a float-determinism audit across native and WebAssembly (the flower dropper's type draw uses doubles), native versus wasm golden hashes.
