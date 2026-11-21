# Ready up: the people of a room say that they are ready, and the host starts

A room of the front page ([Play in the browser](PLAY_IN_BROWSER.md#your-room)) does not start until every person in it who is not the host has said that they are ready. It is the lobby's rule (network protocol 27, [`NETWORK_PORT.md`](NETWORK_PORT.md#protocol-27-ready_upmd)), kept by the server: the page only shows it, and a START that the page sent too early is refused by the server all the same.

## The rules

| Who | Ready |
|---|---|
| A person in the room who is not the host | Says it with the **Ready up** switch on their own card, and takes it back with the same switch. Their mark shows on every screen as **Ready** or **Not ready**. |
| The host | Does not ready. Pressing START is the host's Ready, and the host's card shows **Ready** by itself. |
| A computer player | Is always ready (the mark says so, and nobody waits for it). |
| A person whose game is open already (a desktop player who joined the room) | Needs none: the game is open. |

- **START** of the host is off until every person who has to say it has. Under it the room says whose Ready it waits for ("Waiting for Wren to ready up", "Waiting for 3 players to ready up") and, when all have said it, "Everyone is ready." A guest sees START as "Waiting for the host" and, under it, "Waiting for you to ready up" until they have pressed theirs.
- **The marks stay when the host changes things.** A new map, another game type, a computer player in a color, the teams, a color moved or swapped (the mark goes with the person): nobody has to say it again.
- **The marks go** when a person's link ends (a person who drops is not ready, and says it again after coming back, also after reloading the page), when a person comes to lead the room (the next host starts with no mark: the host does not ready), and for a person who is removed or leaves. A START that waits for the games to open keeps them.
- A room with no person to wait for (the host alone, or with computer players) starts at once, as before.

## The network

- **The message `Ready`** (type 41, client to server): the type and one byte, 0 or 1. It is heard from a person of an open lobby room, who is a lobby page, who does not lead, while the room's START does not wait. Anything else is ignored and counted (the first 16 per connection are free, each one after those is a violation); a message that is not exactly two bytes, or says more than 1, is a violation. It has a budget of its own (a burst of 3, then one a second, like the visibility's: `include/ants_net/flood.hpp`) and counts as use of the room.
- **The Room message** ends with one more byte, the ready mask (bit *s*: the person of seat *s* has said Ready). The decoders refuse a mark for a seat that is not a lobby room's person who is a page and does not lead (a bot's, an empty one, the leader's, a game's), and any mark in a room that is not a lobby room.
- **The START** (`StartRequest` of the leader) is not taken while a person who must say Ready has not: the room tells the leader, and only the leader, "Priya is not ready yet." or "These players are not ready yet: Priya and Sam."
- The turns, the state hashes and the records are as they were. The protocol is 27 (the history is the comment of `kProtocolVersion`); a build of 26 and a build of 27 refuse each other's Hello.

## Where it lives

`HostLobby::everyone_ready`, `must_ready`, `apply_ready` and `ClientLobby::request_ready` (`src/ants_net/lobby.cpp`); the message and the mask in `include/ants_net/protocol.hpp` and `src/ants_net/protocol.cpp`; the page's codec and `LobbyClient.setReady` (`web/front/lobby_net.js`); the cards, the chips and the line under START (`web/front/lobby_rules.js`, `web/lobby.html`, `web/home/room.css`).

## Tests

`tests/test_net/test_lobby_ready.cpp` (the rules in a lobby room: who is heard, the START that waits, the host, the marks that stay and the ones that go, bots and games, the budget), `test_lockstep` N2.114 and N2.111 (the message and the Room's last byte, the golden bytes of `tests/data/lobby_messages.txt`), `tests/scripts/web_lobby_net_check.js` and `web_lobby_client_check.js` (the page's codec and pace), `tests/scripts/web_lobby_rules_check.js` (the cards and the line under START) and, in a real browser (opt-in), `tests/scripts/web_room_in_home_browser.js` (two pages and a real server: Ready up, START, the marks after a change). The rules are mutation-checked with `tests/data/ready_up_mutants.json`.
