# Hints and direct channels between players (protocol 26)

The server stays what it is: the sealer of the turns, the referee and the recorder of every match. What this page adds is a way for a player's machine to learn another player's order EARLIER than its sealed turn, so that the other player's ants are corrected less on the screen. Two kinds of sender, one kind of message:

- **A player's browser** sends each order it gives to the other players' browsers over a WebRTC data channel, at once, before the server has sealed it.
- **The server** sends a computer player's order to every player the moment the bot gives it, over the connection that every player already has, before the turn that will carry it is sealed.

Both are a **hint**. A hint is a guess and the sealed turn is the truth: the confirmed engine, the turns, the state hashes, the replay and everything that the server records never see a hint, and a hint that the sealed turn does not confirm is dropped. A channel that is lost, slow or wrong costs nothing, because the server's stream is the only stream that the match is made of. Where the prediction of one's own orders is described, see [`NETWORK_PORT.md`](NETWORK_PORT.md#prediction-of-ones-own-orders-predictionhpp-cue_routerhpp-netgame-applicationview_sim); the server is described in [`SERVER.md`](SERVER.md), which also has the relay and what the operator must set up ([The relay for the players' channels](SERVER.md#the-relay-for-the-players-channels)).

## Who may see whose address

A direct connection between two browsers shows each its peer's public IP address. So:

- **Every player is on the relay by default.** The browser's ICE transport policy is `relay`: it gathers, sends and accepts only relay candidates, so that no player's address ever reaches another player. The relay is the operator's own coturn.
- **A direct connection is a choice of each player**: a switch in the room (the Room of the front page, [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md#your-room)), off by default, remembered by the browser, with a plain explanation the first time it is turned on ("Players you connect to directly can see your IP address"). A pair of players is connected directly only when **both** turned it on; every other pair stays on the relay.
- **The rule is kept in three places.** The sending page never emits a candidate, or an SDP that holds an address, to a peer that did not allow direct; the receiving page never accepts one from it, and closes a channel whose chosen path is not the relay's for such a pair (`web/p2p.js`); and the server does not pass one on between two players who did not both allow it, and counts the sender's violation (`signal_allowed`, `include/ants_net/p2p.hpp`). A page that breaks the rule is thrown out by the server's violation count; it never reaches a player who did not opt in.
- **If the relay cannot be reached the game falls back silently to the server's stream**, which always runs. It never falls back to a direct connection for a player who did not opt in.
- Nothing about addresses is shown on any screen. The room's rows carry one small word for each other player once a channel to that player is up: `relay` or `direct` (see below).

The relay's own log holds addresses and the random labels of credentials; it is kept short by the stack (three files of 10 MB). The Legal page says what is shared and with whom ([`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md#the-legal-page)).

## The messages

Types 36 to 40 (network protocol 26: a game of protocol 25 cannot play with it, because a server that knows no such message counts it as an offence). Every decoder checks every length, count and range and refuses trailing bytes ([`include/ants_net/p2p.hpp`](../include/ants_net/p2p.hpp)).

| Message | Layout | Direction |
|---|---|---|
| `Hint` | `[36][seq u32][tick u32][a Command]` | server -> player (a computer player's order); player <-> player over a channel. Only the kinds that the prediction predicts: group moves, special orders, attacks and Stop |
| `P2pConfig` | `[37][direct u8]` | player -> server, once the match has begun: "I can open channels" and whether this player allows direct ones |
| `IceConfig` | `[38][expires u32][username][credential][n u8][url]*n` | server -> that player: the relay's short-lived credential and addresses; `n = 0`: this server has no relay, and the player opens nothing |
| `P2pPeers` | `[39][ready u8][direct u8]` | server -> every player that sent `P2pConfig`, whenever they change: who is here and ready, and who allows direct |
| `Signal` | `[40][peer u8][kind u8][len u16][text]` | player -> server for seat `peer`; server -> player from seat `peer` (stamped by the connection). Offer and Answer carry SDP text, Candidate carries `index|candidate line`, Bye nothing |

The server relays a `Signal` only between two players who are both ready; a `P2pConfig` after the sixteenth, a `Signal` after the five hundred and twelfth, a `Signal` to oneself, or one that shows an address that the other player may not see, is a violation (the server throws a connection out after eight).

## What a machine does with a hint

`Prediction::hint` (`include/ants_net/prediction.hpp`, "HINTS") puts the order into the predicted engine at the tick it names, and the sealed turn matches or drops it. The rule, in one place:

- **Where it goes.** At the tick the hint names (`tick`; see the next point), but never behind a turn that is already in hand (such a turn was sealed without it): there the order moves to the first turn that is not known. A hint whose tick is past the display tick makes the engine rebuild with it; one at the display tick is applied at once; one ahead of it waits there. A tick far ahead of any screen is held to one lead beyond the display tick.
- **The tick a hint names.** The turn in which the sender expects the server to seal the order (`Prediction::last_order_hint_tick`, `SealPhase` in `include/ants_net/seal_phase.hpp`), not the tick at which the sender's own screen applied it: the two differ in roughly half of the orders (the lead is the shortest lag of the last orders, the seal comes at its own phase), and a walking ant that a hint puts one tick off from the sealed tick is moved a whole tile (32 px) when the turn corrects it. The sender knows the rhythm of the server's seals from the arrival times of its own turns (turn `k` arrives at about `phase + 50 ms * k`: the phase is the median of the last 24 arrivals, each less 50 ms times its number) and the way an order takes from the orders that came back (an order given at `t` and sealed in turn `n` says that the delay, the whole round trip, is between `50 (n - 1) - (t - phase)` and `50 n - (t - phase)`; the delay that fits most of the last 16 orders, newer ones counting more, is used). The turn of a new order is then `ceil((t + delay - phase) / 50)`, never a turn that has arrived and never more than 3 ticks beyond or 2 before the display tick (then the phase is wrong, not the order). With no clock, fewer than 12 turns, arrivals that keep no rhythm (the middle half of them wider than 60 ms), fewer than 3 orders back, or no delay that fits half of the orders, the hint names the tick of the sender's own screen, as before. The computer players' hints name the very turn that will carry the order, the server knows it.
- **How a sealed command finds its hint.** A sealed command of another seat takes the OLDEST hint of that seat that is equal to it (same kind, tile and ants). The hints of that seat before it were not sealed (the server refused them: flood control): they are dropped. A command with no hint is learned from its turn, as it always was.
- **When they disagree, the sealed one wins.** The hint stood at the sealed tick: nothing happens, no rebuild. It stood at another tick: one correction, the order is where the turn has it. The sealed turn of the hint's tick comes without it: the hint moves once to the next tick (an order is most often sealed one tick off, as one's own orders are), and when that turn comes without it too, it is dropped. One that no turn carries for 100 ticks (5 s) is dropped, as an own order is.
- **Copies and lateness.** A sender's number for each order (`seq`, from 1) is the same on every copy: a copy that was seen is ignored (a channel sends each hint three times; see below). A hint that comes after the sealed turn that carries its order (the direct path was slower than the server's) is recognized by that turn's commands and ignored: the order is never applied twice. A hint more than 12 ticks behind the next turn that is expected is ignored too.
- **Only a seat's own orders.** The seat of a hint from a channel is the channel's, never the payload's: a hint whose command names another seat is refused. A group order keeps only the issuer's own ants, so a forged hint can show false motion of its sender's ants for at most 300 ms, until the sealed turn corrects it; nothing reaches the simulation, the hashes or the record. At most 64 hints wait at a time, and the work that hints cause is counted by the prediction's budget and cool-down like any other.
- **No hint without a prediction.** A machine whose prediction is off (a pause, a catch-up, a hidden tab, `--no-prediction`) takes no hints and sends none (the tick of a hint comes from the prediction's phase and its display tick).

**Fairness and Fog of War.** A hint carries only the sender's own orders, which every receiver would get from the server moments later in the sealed turn: sealed turns carry every player's orders to every machine today, fog or not, and nothing hidden is added. The explored ground that a screen shows is the confirmed engine's and is copied into the predicted engine after every tick, so an ant of another team is never drawn where the confirmed engine has not explored, whatever a hint did to its path; the tests hold the predicted engine's explored ground to the confirmed engine's at every frame under hints. The server's hints of a computer player are no earlier than the seal wait (up to one turn, 50 ms): the bot's order is sealed into the very next turn, exactly the tick the hint names, and its time of effect in the simulation is untouched.

## The channels (`web/p2p.js`)

- **Where.** The game page (the wasm game, `src/ants_net/wasm_rtc.cpp`, calls `web/p2p.js`). The Room only holds the switch; the connections are made on the game page once the match has begun, and the countdown card (30 s in a room of two or more people) is the time they have to come up. A connection takes 0.5 to 3 s through the relay.
- **One connection to each other player**, started by the lower seat. One data channel, negotiated (`id 1`), **unordered and without retransmits**; each hint is sent **three times** at once (an unordered channel with three copies lost 0.09 % of the turns at 3 to 5 % loss, where TCP and a reliable channel stalled for 150 ms or more many times a minute: the measurement is in the design notes of the change). A one-byte heartbeat every second keeps the mark honest; the game never sees it.
- **The credential** is made by the game server (`P2pConfig` -> `IceConfig`): coturn's shared-secret form, `<expiry>:<random label>` and base64(HMAC-SHA1(secret, username)), good for three hours (a match is capped at two), a new label for every player and every ask; the label is random bytes, never anything that names a person, a seat or a room. The secret is `ANTS_TURN_SECRET` of the stack's environment; it lives in a closure of the server and is printed nowhere and kept in no record.
- **Fallback.** A channel that does not come up within 12 s, or fails, is let go of and tried again by the lower seat up to three times (1, 2, 4 s). The server's stream is not affected at any point. A browser without WebRTC, a page without `p2p.js`, a server without a relay (`IceConfig` with no address) and `?p2p=off` (`--no-p2p`, which also makes the game ignore the server's hints) all mean: no channel.
- **The mark.** `NetGame::peer_link(seat)` says nothing, connecting, relay or direct. The countdown card's row of each other person shows `relay` or `direct` where it said `in` once a channel to that person is up. The mark follows the browser's own statistics (the kind of the local candidate of the chosen path), not a guess.

## Command line and page

`--direct on|off` (default off: this player allows direct connections) and `--no-p2p` (no channel at all), see [`COMMAND_LINE.md`](COMMAND_LINE.md). The page passes `--direct on` when the Room's switch was on (localStorage `ants.direct`) and `--no-p2p` for `?p2p=off`. The server: `--turn-host HOST[:PORT]` (or `ANTS_TURN_HOST`) and `ANTS_TURN_SECRET`, see [`SERVER.md`](SERVER.md). Without both, protocol 26 changes nothing about a match except the computer players' hints.

## Measured effect (a rig on virtual time, not real networks)

What was run: real sealing server session, real client sessions and predictions, links with a latency and a jitter, 240 s of play and six seeds for each line. A "correction" is one ant of ANOTHER seat that a rebuild moved by 8 px or more on a player's screen (a tile is 32 px); the same unit as the study of the bots' rubber banding. The two players of the peers rig give an order to their colony about every 1.3 s each (a busy match), so only the comparison of the columns means something, not the counts.

**Between players** (per minute and per player; round trips in ms: player A to the server / player B to the server / A to B; the relay's path is half the sum of the two players' round trips to it, which sits with the server). Whole-tile hops are corrections of 28 px or more, which the engine makes when an ant that walks is re-ordered a tick away from where the order was meant to land.

| link (A / B / A-B) | no hints: 8 px+ (tile hops) | relay: 8 px+ (tile hops) | direct: 8 px+ (tile hops) |
|---|---|---|---|
| 80 / 80 / 15: neighbors far from the server | 106 (0.0) | 28 (0.5) | 2.7 (0.5) |
| 120 / 120 / 10: both far, close to each other | 105 (1.4) | 97 (2.0) | 5.9 (2.0) |
| 40 / 150 / 110: one overseas | 106 (2.4) | 61 (0.8) | 63 (0.9) |
| 40 / 40 / 80: server in the middle | 107 (0.0) | 17 (0.0) | 28 (0.0) |
| 20 / 20 / 40: both near the server | 107 (0.0) | 2.5 (0.0) | 2.6 (0.0) |
| 2 / 2 / 2: lab floor | 107 (0.0) | 2.5 (0.0) | 2.5 (0.0) |

Every hint was confirmed by its turn in these runs (no loss, no late hint). Reading: a hint cuts the number of corrections a lot where it comes well before its turn (pairs near the server, or near each other), and less for the player who is far from the other and from the server (one overseas). The tick that the hint names decides the whole-tile hops: when it was the tick of the sender's own screen, which is the sealed tick in 37 to 74 % of the orders (the rest are a tick off), the same links had 11.0, 6.3, 7.3 and 1.1 hops a minute through the relay in the first four rows (11.2, 6.3, 5.2 and 0.3 direct), and the overseas player's own screen 11.9 (relay) and 8.7 (direct). Named from the phase of the seals, the hint is the sealed tick in 97 % of the orders at 80 ms, 99 % at 40 ms, 89 % at 120 ms and 86 % at 150 ms (the links jitter by a quarter of their round trip: a tick is 50 ms), and the overseas player's screen has 1.0 (relay) and 1.3 (direct) hops a minute. The hints that are still a tick off come from the jitter of the links: an order that happens to take longer to reach the server than the others of its link is sealed a tick later than the phase said.

**A computer player's orders from the server** (a Hard bot on SMALL, one person who does nothing, corrections of the bot's ants per minute, 6 matches of 240 s for each line):

| link (round trip, ms) | without: 8 px+ / 16 px+ / p90 size (px) | with the server's hints: 8 px+ / 16 px+ / p90 size (px) |
|---|---|---|
| 2 | 12.3 / 1.4 / 16 | 0.0 / 0.0 / 0 |
| 30 | 12.3 / 1.4 / 16 | 0.0 / 0.0 / 0 |
| 80 | 12.4 / 2.6 / 18 | 0.7 / 0.0 / 12 |
| 150 | 18.0 / 10.5 / 27 | 15.5 / 8.0 / 26 |
| 250 | 23.9 / 15.3 / 31 | 21.7 / 14.2 / 31 |

Every hint of the server (about 1,090 in six matches, each line) was confirmed on the tick it named. The hint is ahead of its turn by one turn (50 ms: the bot decides right after the server runs a turn, and the order is sealed in the next one), so it removes the corrections where a player's picture is no more than a turn or two behind the server (a round trip up to about 80 ms) and helps little beyond that (9 to 14 % fewer at 150 and 250 ms). Holding the bot's order back so that the hint comes a whole lead ahead is a different thing and was not built: it changes who wins a duel.

**Two real browsers** (headless Chrome, the wasm game, a real `ants_server` and a real coturn in a container): the relay (switch off on both), both on (direct), one on and one off (relay), the relay not there with both off (no channel, the match runs on the server's stream, no host or server-reflexive candidate is ever gathered or sent) and the relay not there with both on (direct). In the relay and direct runs each player gave five orders with the mouse: each was sent as a hint (five sent, fifteen copies received), the prediction took five, and the sealed turns confirmed them; the page was served with the exact content security policy and isolation headers of the site.


## Tests

`test_prediction_hints` (the matching rule and its properties on the prediction's rig, and the tick that a hint names: the phase of the seals on a model of the link and on the rig), `test_lockstep` P25.x (the wire, the credential against RFC 2202's HMAC vectors, the rule of the address, the server's session, the bot hints, nothing changed in turns or hashes), `test_netgame` N25.x (the game's side with a transport that the test plays), `test_web_p2p` and `test_web_lobby_direct` (the page's channels and the Room's switch, node, with mutants in `tests/data/p2p_mutants.json`, `tests/data/lobby_direct_mutants.json`, and `tests/data/p2p_core_mutants.json` for the C++ rules). The rig that measured the effect and the two-browser run are not part of the repository (they are described above).

## Limits

- Real NATs and real routes were not measured here: how often a direct path fails, and how many pairs are nearer to each other than to the server, will show only in use. The relay costs the operator about 3 kB/s for each relayed pair (hints and a heartbeat).
- A hint cannot help a player whose server path is already as short as the direct one. The phase that names its tick is learned from the player's own orders: until three have come back, for about five orders after the delay to the server changes, and while the turns arrive scattered over more than 60 ms (a link in trouble) the hints name the tick of the sender's own screen, as before; a stall of the server or a late order is a hint a tick off, which the receiver corrects once.
- A hidden tab, a phone that sleeps, a machine that cannot afford the second engine: no prediction, so no hints.
