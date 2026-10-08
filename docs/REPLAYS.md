# Replays

A match of this game is its starting data (the map, the seed, who plays, the teams) plus the orders that the players gave, turn by turn: the engine is deterministic, so the same data and the same orders give the same match on every machine. A replay file holds exactly that, and a few checks that say whether a machine plays it out the same way. `replay_tool` reads a file, plays the match again with no window, and lists every order with its time, its place and the types of the ants that got it.

**Built:** recording in the desktop game; the file; `replay_tool`. **Not built yet:** the game server's recording of the matches that its rooms play (the browser's matches), watching a replay inside the game, and watching computer players play each other ([`replays/DESIGN.md`](replays/DESIGN.md) is the design for those; this page says what exists).

## Recording a match

Every match that the desktop game plays is recorded while it is played: a game on your computer (alone or against computer players) and a match of a room, as its host or as a guest. Nothing is switched on, and recording changes nothing in the game: it only watches the orders that the engine is given and the ticks that it runs. The browser game records nothing: a page has nowhere to keep a file, and what it plays in a room is for the game server to record.

- **The file** is named after the map and the date and time of your computer, for example `ants-TREASURE-20261006-143209.antsrep`, and is written when the match is over, into the folder `replays` beside the settings file: `~/Library/Application Support/Ants/Ants/replays/` on macOS, `~/.local/share/Ants/Ants/replays/` on Linux, `%APPDATA%\Ants\Ants\replays\` on Windows (with `--settings FILE`, the folder of that file; [`CONTROLS.md`](CONTROLS.md#settings)). The console says `Replay saved: <path>`. A file that is there is never replaced: a match that ends in the same second as another gets `-2` (then `-3`, and so on). The game never deletes anything from this folder, so it grows with every match that you play: clear it out when you like. A headless run without a settings file keeps nothing on disk.
- **A match that you leave** before it is over is kept as well, when something happened in it: you gave an order (the quit is one) or a minute went by. What the computer players or the other players did does not count, so a match that you walk away from in the first minute leaves nothing, however busy the others were. The file says that the match was not over. When a recording cannot be a file (a turn of a network match never arrived, or the file would pass 1 MiB), nothing is kept and the console says why.
- **Names:** in a game on your computer the file holds the names that were typed or given (a computer player's "Bot (Medium)", `--name`, `--team-name`, the name typed in the start menu); the name that the program makes of your system user stays out, because a file may be sent to someone. In a match of a room the names are the ones that the room showed everybody. A name in the file is plain ASCII, the letters from the space to the tilde: any other character of it becomes a `?`, and a file whose texts hold anything else is refused when it is read.

To show someone a match, send the file. They need a build with the same rules number (below) and the same map; `replay_tool info FILE` says which.

## The file

A `.antsrep` file is a row of chunks, like a PNG: a reader skips a chunk that it does not know (unless its first letter is upper case: then the file is refused), every chunk has a checksum, and a file that stopped in the middle still plays up to its last whole chunk. The numbers are little endian. [`include/ants_replay/replay.hpp`](../include/ants_replay/replay.hpp) has the exact layout; in words:

| Chunk | Holds |
|---|---|
| `HEAD` | the format number; the rules number; the game's version and build; "local game" or "network game"; the map's file name and its hash; the seed; the seats that play; Fog of War; the names; the teams that the match started with; the seat of the machine that wrote the file |
| `CMDS` | the orders in the order in which the engine was given them, each with its turn; one chunk for every 30 seconds of game time or 4,096 orders |
| `hash` | the low half of the engine's state hash after every 100th turn (every 5 seconds of play) |
| `ENDS` | the number of turns, whether the rules ended the match, and the state hash at the end. A file without it is incomplete: a copy that was cut short (a download that stopped, a file that was truncated), because the game writes a file whole, when the match is over or left |

An order is the network's own wire form (the one a room sends, [`NETWORK_PORT.md`](NETWORK_PORT.md)): its type, who gave it, the tile, the ants that it names by number. **Turn t** is the t-th tick of the engine: the orders of turn t are given first, in the order they stand, and then the tick runs (a turn is 50 ms). Orders at the last turn's number come after the last tick: a Quit that ends the match is one.

**The rules number** is the network protocol's number (`replay_tool info` shows it), because that number moves whenever what the engine computes changes. A file is played only by a build with the same number; another build still reads its header, and the message names both numbers and the game that made the file. **The map** is found by its file name in the folder of the shipped maps (`--maps-dir` names another) and must be the same file: another file under that name is refused with both hashes.

**Size:** a match of computer players is a few KB (4 KB for two and a half minutes of four of them on the smallest map), a long match of people tens of KB. A recording that would pass 1 MiB is not kept (the console says so).

## The tool

```bash
cmake --build build --target replay_tool
./build/replay_tool info    match.antsrep      # what the file says: map, seed, seats, teams, length, orders (no map needed)
./build/replay_tool verify  match.antsrep      # plays it and compares every hash in the file and the final state
./build/replay_tool orders  match.antsrep      # plays it and lists every order
./build/replay_tool orders  match.antsrep --seat 1 --type bomber     # only the orders of Red that name a bomber
./build/replay_tool orders  match.antsrep --csv > orders.csv         # a table for a spreadsheet
```

(`build\replay_tool.exe` on Windows.) `--seat N` is 0 Green, 1 Red, 2 Blue, 3 Black; `--type` is worker, bomber, fire, thief, combat or swimmer; `--maps-dir DIR` says where the maps are. The exit status is 0 when the file was read (and, for `verify` and `orders`, played to the end with every hash right), 1 when the file is refused, its map is missing or the match does not play out the same here, and 2 for a wrong command line or a file that cannot be read. A file that stops early plays as far as it goes and says so; if it holds no hash either, `verify` has nothing to compare the match with and says UNVERIFIED (exit 1) instead of OK.

`orders` prints one line for each order:

```text
TINY.LVL, seed 5: Green, Red, Blue, Black; 3000 turns (2:30.0)
  time  turn  seat   order  place    ants      from     result
0:00.6    12  Green  move   (12,12)  3 Worker  (9,8)    ok
0:00.7    14  Red    move   (18,18)  3 Worker  (21,21)  ok
...
291 orders: Green 57, Red 92, Blue 65, Black 77
```

- **time** is the game time of the order (minutes:seconds.tenths since the first tick), **turn** the turn number, **seat** the colour, with the name when the seat has one ("Red (Bot (Hard))").
- **order** is `move`, `special`, `attack`, `stop`, `hatch`, `team offer` / `accept` / `refuse` / `withdraw` / `leave`, `quit` or `dropped out`; **place** the tile of a move, special or attack, or the other side of a team order.
- **ants** are the ants that the order names, by type ("3 Bomber, 1 Worker"), read off the engine as it stood when the order was given: an order names its ants by number, and only the engine knows what an ant is. Ants that were gone, never were, or belong to another seat are counted as "not there" (the engine leaves them out). **from** is the tile where the first of those ants stood.
- **result** is what the engine said: `ok`, `ignored` (nothing for it to do, for example every ant named was gone) or `refused`.

With `--csv` the same lines come as a table (`time,turn,seat,name,order,x,y,ants,from_x,from_y,result`; the text cells are in quotes). A text that begins with `=`, `+`, `-` or `@` (a name in someone else's file could) gets a `'` in front, so that a spreadsheet shows it as text and never takes it for a formula.

A **special** order is what the ant's type does with it ([`CONTROLS.md`](CONTROLS.md#mouse-controls), [`GAMEPLAY.md`](GAMEPLAY.md#special-abilities)): a Bomber plants a bomb at the place or defuses one, a Fire ant lights a fire wall or puts out a blaze, a Swimmer digs a bridge on water or demolishes one, a Thief raids a hill. For a group of workers, combat ants or mixed ants it is a plain move. After the table the tool counts the special orders by the type of ant that got them: "special orders by ant type: Bomber 12, Swimmer 3" is how to see at once whether a player bombed with bombers and dug bridges with swimmers.
