# Network Port

Status: **the core, the room, a native TCP transport, the application integration and host migration are done and tested (v0.0.43 command layer and state hash, v0.0.44 protocol, sequencer, lock-step
runner and sessions, v0.0.45 room and start barrier and framed TCP, v0.0.46 host / join in the game, v0.0.47 host migration).** Open: WebRTC / NAT traversal (raw TCP works on a LAN, a VPN or with a
forwarded port); a single-player game applies its commands at once.

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
* **Topology and NAT traversal** (milestone 2): a star with the room owner as sequencer (with a mesh of links behind it for host migration, below); one WebRTC data channel per player (ICE with STUN and a TURN relay as the fallback, DTLS-encrypted, reliable ordered
  channel for commands and one for keep-alive and hashes) so the same protocol runs natively (libdatachannel) and in the browser (built-in RTCPeerConnection); a small WebSocket signaling service for rooms
  and the SDP / ICE hand-shake. TURN credentials are issued per session (the standard TURN REST scheme); the shared secret, host names and STUN / TURN URLs are configuration, never committed.
* **Kept from the original**: the lobby flow and strings (latency thumbs, host picks map and fog and presses START, LOADED and READY barriers, "Get ready" for at least 5 s), a drop-out timeout (60 s in
  the original), kick, quit, alliances, chat (100 characters, team-only filter), no late join, no pause, no bots (the alliance auto-accept of the old simulation is removed), and **the match goes on when
  the host leaves**: the original is a mesh in which nobody is special once the match runs, so the remake moves the sequencer role to another machine (host migration, below).

## Host migration (milestone 4b, shipped in v0.0.47)

In the original the machine that picked the map can drop out while the others play on. The remake keeps that: the host is a role (the sequencer), not the place where the state lives, because every machine
already runs the whole simulation and holds the same state. What moves is sealing turns, the reference hash, the chat relay and the drop decisions. Code: `include/ants_net/session.hpp` /
`src/ants_net/session.cpp` (`ClientSession` election state machine, `HostSession::resume`, `promote_to_host`), `protocol.hpp` (version 3), `netgame.cpp` (the TCP mesh and the events).

1. **Peer links.** While the map loads every guest connects to the guests above its own seat (one link per pair; the ones below connect to it). A guest listens on an ephemeral port that it announces in
   `Hello.listen_port`; the host passes what it saw (the guest's address and that port) to everybody in `Start.endpoints`. An inbound link starts with `PeerHello{seat}` and is accepted only from a lower seat of the
   roster, once per seat, and only from the address the host reported for that seat; everything else is closed. The links carry a ping every second (`peer_silence_ms` = 5 s tells "alive") and, when the host goes,
   the election. The sessions only need a `Connection*` per seat (`ClientSession::set_peer`), so the WebRTC mesh will plug in the same way.
2. **Turn log.** Every machine keeps the last 300 turns (30 s) it received (`LockstepRunner::logged_turn`; the host those it sealed), commands included.
3. **Trigger.** The host link is closed (everything the host sent before is read first) or the host has been silent for `host_silence_ms` (10 s; the host answers a guest's ping every second and seals a turn
   every 100 ms, so silence means gone). A guest that receives a proposal while it has not noticed yet re-reads its host link first, and believes "the host is alive" only if it heard the host within
   `host_alive_ms` (2 s). After `NetGame::freeze()` (the match is over) a leaving host is no reason to elect anybody.
4. **Election** (bully algorithm with epochs; `Propose`, `Accept`, `Refuse`, `Resume`, `Request`, `PeerHello`). The candidate is the lowest seat that a survivor still sees alive on its peer links; it proposes
   `epoch + 1` to every live peer. A peer accepts the proposal of the lowest seat it sees alive and only one candidate per election (a second one is refused with the name of the first); a candidate that
   receives "a lower seat lives" waits for that seat's proposal (and asks anyway after `elect_timeout_ms`); one that receives "the host lives" (the peer had not noticed the loss) asks again after a second and
   gives up after three such refusals (`Lost`: it is this machine that lost the host); a peer that does not answer within `accept_timeout_ms` (3 s) is given up. Every message carries the epoch and one of
   another election is ignored, a link speaks only for its own seat, and an election that takes longer than `election_limit_ms` (30 s) ends as `Lost`.
5. **Resync.** A survivor stops reading its old host at the moment the election begins, so the receive position it reports in `Accept` (`next_receive`, `next_execute`) is the history it offers. The candidate takes
   `resume_turn` = the highest position among the accepts and its own, fetching the turns it lacks from the peer that has them (`Request`; a source that dies is given up and the plan is made again without it),
   becomes the host (`promote_to_host`: the same runner, so the log and the presentation hooks stay), sends every follower `Resume{epoch, host, resume_turn}` followed by the turns it lacks, and seals
   `resume_turn` next. The first turn holds the `Drop` of the old host and of every seat of the roster that did not follow (a seat that already dropped stays as it is), so every machine drops them at the
   same tick. Nobody can be ahead of `resume_turn` by construction, so nobody diverges; followers adopt the link to the new host as their host link (`Resume` is honoured only from the candidate they accepted).
6. **What is lost.** Commands given in the last ~300 ms that were not yet in a sealed turn (the player gives the order again; while there is no host `NetGame::submit` reports the command as ignored, so no
   acknowledgement sounds). The game stalls for the detection time plus a round trip (the overlay says "The host left. Choosing a new host..."), then goes on and says "Bob is the host now." for five seconds.
   A second failure during the election is handled by the same rule; a match with one machine left goes on for it alone (as in the original, where the last team standing plays on).
7. **Limits.** A host that dies in the first second of the match, before the links between guests are made, can split the match (each guest then plays on alone). Two guests that cannot reach each other while both
   reach the host, or a network partition, split the match into groups that each play on consistently (as in the original's mesh). A guest that is on the same machine as the host is reachable for other guests
   only under the address the host saw (127.0.0.1 in that case); NAT needs the WebRTC transport.
8. **Tests** (`tests/test_net/test_lockstep.cpp` N2.22 - N2.36 on the simulated network, `test_netgame.cpp` N3.7 - N3.12 and `test_network_app.cpp` N5.4, N5.6 over real sockets): the host dying abruptly and
   silently, the host and the next seat dying together, the successor dying while it is being accepted, guests that are behind or ahead of the new host, the only holder of the missing turns dying while they are
   fetched, a silent guest, a partitioned old host that keeps playing alone, a guest whose own link broke while the host lives for the others (it is lost, nobody takes over), chat / leave / commands through the new
   host, three host changes in a row, forged, stale and garbage messages between guests (20000 of them), strangers on a guest's port, the runner's turn log, the sequencer's resume; the states of all survivors are equal
   to the end, twice with the same result.

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
only as plain names of the maps folder (letters, digits, `_`, `-`, `.`, ending in `.LVL`; no path separators, no `..`). Seats go to guests in the order their `Hello` reaches the host. No late join
(as the original); the host leaving is handled by host migration (below).

**Connection quality (v0.0.46, protocol version 2).** The host pings every seated guest once a second (`Ping` / `Pong` with the send time echoed, up to eight pings in flight, so a link slower than
the interval is still measured) and puts the measured round trip of every seat in the `Room` message (`RoomMsg::Slot::rtt_ms`, 0 for the host, `0xFFFF` before the first answer). `link_quality()` turns it
into the thumb of the setup screen with the original's thresholds (`Ants.exe` 0x1013289): below 1200 ms good (`netgood`), below 1800 ms ok (`netok`), more bad (`netbad`), not measured `netunk`, the question
mark. The host re-broadcasts the room only when a seat's tier changes. START needs a second player and every guest measured ("Press START when all players' thumbs have appeared"), which `NetGame::can_start()`
checks.

## TCP for LAN and development (`tcp.hpp`, native builds, v0.0.45)

`TcpConnection` / `TcpListener`: non-blocking sockets (POSIX and Winsock), `u32` length + payload framing, a length above 64 KB fails the connection without allocating for it, a sender whose peer never
reads is cut off when its queue passes 512 KB, an orderly close still delivers what was sent before it, `TCP_NODELAY`. It implements the same `Connection` interface as the in-memory link, so the sessions,
lobby and tests run unchanged on real sockets (`tests/test_net/test_tcp.cpp`: message boundaries and order for every size up to the limit, 20000 small messages, hostile length prefixes, truncated frames, dead
ports, the never-reading peer, a 40 s match of a host and three clients over real sockets, and the room from `Hello` to `Begin`).

## The application (`netgame.hpp`, `Application`, v0.0.46)

* **`NetGame`** (`src/ants_net/netgame.cpp`, no SDL, no threads): owns the listener and the connections and runs the room and then the session behind one small interface: `host(port, name)` /
  `join(address, port, name)` / `leave()`, `update(now_ms)` every frame, `take_events()` (`RoomChanged`, `StartRequested`, `Begun`, `Cancelled`, `PlayerLeft`, `HostLeft`, `Desync`, `Failed`, `HostChanged`), the host's
  `set_map` / `set_fog` / `start_match(seed, map_hash)`, `report_loaded(ok)`, and in the match `submit()` (the HUD's `CommandSink`), `chat()`, `freeze()`, `stalled_ms()`, `laggard()`, `sub_tick_ms()`.
  `NetGame::status_text()` is the setup screen's status line, in the original's words (`docs/GAME_REVERSE_ENGINEERING.md` 5.48). A WebAssembly build has no TCP, so `host()` / `join()` return false there.
* **Commands from the HUD**: `NetGame::submit` stamps the local seat, predicts the acknowledging ant with `SimulationEngine::predict_order_ack` (the click's voice and pedestal feedback are immediate although the
  order is applied a few turns later; the prediction equals the engine's answer for 2000 of 2000 random orders in `test_commands` N1.21) and queues the command for the next turn.
* **The setup screen is the room** (`MapSelectScreen::RoomView`): a row per occupied seat (portrait in the seat's colour, name, thumb), the host's map / fog / START controls (a guest's clicks on them do nothing
  and its map and fog follow the host's), LEAVE for everybody.
* **The match**: the ticks come from the `LockstepRunner` (`Application::pump_network` runs `NetGame::update`, every tick calls `Application::post_tick`: HUD, events, audio, the end of the match); the frame is
  drawn at the runner's sub-tick position; a machine that waits for a turn says so after one second (remake text) and a desync stops the match and says so. There is no pause and no team switching.
* **Roster and drop-out**: `Start` carries the roster mask; a team without a player has no hill (`LevelData::for_roster` also removes its hill art for the renderer), no start markers and no eggs. A player that
  leaves, is thrown out or is silent for 60 s is dropped by a host-only `Drop` command in the next turn (`SimulationEngine::drop_player`, `FUN_0100d03b`), so every machine drops the team at the same tick.
* **Names**: `--name`, the original's `-N<team><name>` and `--team-name` reach the room (the `Hello`), the HUD's score labels, the results rows, the chat headers and the simulation's alliance and drop-out
  texts. A network game says "Player" unless `--name` is given: it never sends the user and machine name by default.
* **Command line**: `--host [port]`, `--join host[:port]`, `--port`, `--name`, `-N<team><name>`, `--team-name <team> <name>`, `-pnum=<team>`, `--loopback` (accept only this machine); port 4001 by default (the original's).
* **Group order acknowledgement**: the ant that answers a group order is now decided by GoTo's real result (`issue_order` returns it), as in the original (`0x10289b2 .. 0x10289c0`); before, a stale path request
  of a refused order could still make an ant answer.
* **Teaming (v0.0.50)**: the three modal dialogs of the original (the invitation question, the proposer's waiting dialog and the confirmation before a team is broken, `docs/GAME_REVERSE_ENGINEERING.md` 5.42)
  are driven by the shared simulation's state, not by an event: `WorldState::pending_invite_from[invitee]` says who has an offer for whom, so every machine that holds the invitee or the proposer shows the dialog
  at the same tick and a dialog cannot be missed. The answers are commands of the answering seat (`AllianceAccept / Deny / Withdraw / Break`, and `AllianceInvite` or the attack order after the confirmation) that
  travel through the turns like every other order; while an answer is on its way the question does not come back. A team message (`Chat` with the team flag) goes to everybody through the host and every machine
  shows it only when it is the sender or an ally of the sender by its own alliance table (`HUD::receive_chat_message`), in the sender's team colour. `drop_player` also clears the offers to and from the dropped team.
  Tests: `N5.9` / `N5.10` of `test_network_app` (an offer from another machine reaches the application as the question, Accept makes the team on all three machines, a team message reaches only the ally, a
  refusal closes the waiting dialog, Withdraw takes the offer back everywhere, equal state hashes at the end).
* **Limits of this release**: no NAT traversal (raw TCP: a LAN, a VPN or a forwarded port), a machine returns to the local setup screen when a network match ends, a player has one pending team offer at a time
  (the original queues several), and the match ends by the clock only (the elimination rules are not ported yet). Since v0.0.47 the host may leave and the match goes on (see Host migration: `HostChanged` reports it, `HostLeft` only says that no
  new host could be agreed).

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
4. **The application: host / join, the room with names and thumbs, the runner instead of the wall-clock tick driver, predicted click feedback, waiting and desync messages, roster and drop-out through the
   turn stream, names on the command line: shipped (v0.0.46).** Still open from the original plan: native versus WebAssembly golden hashes.
4b. **Host migration** (the match continues when the host leaves, as in the original): peer links between the guests (a TCP address directory for LAN), the turn log, election with epochs, resync, the sequencer
   role moving to a surviving machine, the events and messages in the game, tests with the host dying at many points, double failures and a partitioned old host: **shipped (v0.0.47).**
5. WebRTC data channels with ICE / STUN / TURN (native via libdatachannel, browser via RTCPeerConnection) and the signaling service (it also brokers the mesh links).
6. Docker / nginx / TURN deployment files (secrets from the environment).
7. **Alliance dialogs (the ally attack confirmation, string 4; the invitation questions, strings 1 - 3): shipped (v0.0.50).** CHECKGO elimination rules (the match also ends when nobody is alive or one side leads alone): open.

Open for milestone 3 and later: a float-determinism audit across native and WebAssembly (the flower dropper's type draw uses doubles), native versus wasm golden hashes.
