# The history service

`ants_history` is a program of its own that keeps the match history and gives out the recordings and the history pages. It reads the folder in which the game server keeps its recordings, counts every new recording into a database file of its own within a few seconds, and answers `GET /replays`, `GET /replays/<file>`, `GET /history`, `GET /history/<id>` and `GET /history/deaths` with the same JSON, the same limits and the same errors as the game server's public door ([`REPLAYS.md`](REPLAYS.md), "The match history"; [`SERVER.md`](SERVER.md)).

**Status:** built and tested, not deployed. The game server still counts the history, keeps its tables and serves the pages; nothing routes to the service yet. This page says what the program does.

## Why a program of its own

The game server runs on one thread. Counting a match means playing its recording again with no screen (0.3 to 0.6 s), which the server does only while no room runs a match, and the lists and the files of the public door are made on the same thread. A busy server counts late, and the counting shares its thread with the games. The service moves all of that to another process: the game server writes each recording when a match ends, as before, and the service does the counting and the serving.

## Starting it

```
ants_history --db FILE --recordings DIR --maps DIR --port N
             [--community-maps DIR] [--game-db FILE --submissions-dir DIR] [--any-interface]
             [--replays-days N] [--replays-max-mb N] [--settle-seconds N] [--look-ms N] [--pace-factor N]
```

| Option | Meaning |
|---|---|
| `--db FILE` | The service's own SQLite file, made when it is not there. It holds the match history and nothing else (the tables `history_matches`, `history_seats`, `history_markers`, `history_deaths`, `history_action`, `map_plays`, `replay_likes` and `meta`, made by the same SQL as the game server's; the plays and likes of replays are two columns of `history_matches`). |
| `--recordings DIR` | The folder of the game server's recordings (its `--replays-dir`). It is only read: the service deletes, writes and renames nothing in it. |
| `--maps DIR`, `--community-maps DIR` | The folders of the maps (the game server's `--maps` and `--community-maps`): a recording is played on its map. A folder that is named must be there. |
| `--game-db FILE`, `--submissions-dir DIR` | The game server's database (its `--db`) and its folder of submitted maps (its `--submissions-dir`). The database is opened read only. See "The first start" and "Maps of the players". |
| `--port N` | The port of the public pages (0: any free port, the log line says which). They listen on this machine only unless `--any-interface` is given (for a container; the host must restrict it). |
| `--replays-days N`, `--replays-max-mb N` | The game server's own numbers (default 30 and 100). `keep_days` in the lists is the first; the game server's purge does the deleting. |
| `--settle-seconds N` | A recording that changed less than this many seconds ago is not trusted yet (default 1; [`SERVER_REPLAYS.md`](SERVER_REPLAYS.md#replays), the store's `settle_s`). A new recording is counted this late at the least. |
| `--look-ms N` | How often the counter looks at the folder (default 250). |
| `--pace-factor N` | After a recording the counter waits this many times what it took (default 1), so that a backlog of old recordings does not take a whole core. |

`--version` and `--help` work as the game server's. The exit status is 0 after SIGINT or SIGTERM, 1 when the service cannot start (one message says why: a folder that is not there, a file that is no database or was made by a newer program, a game database that cannot be read), 2 for a wrong command line. `GET /healthz` is answered by the HTTP layer itself, as on the game server's doors.

## What it serves

The routes and their answers are the game server's public door's, made by the same code (`public_history_service_response` in `ants_server/control.cpp`): the lists, the one match, the map of deaths, the recordings, the errors and the other methods. Everything else is the door's own `404 {"error":"not found"}`; the game server's `/live`, `/lobbies`, `/busy` and `/maps` need its live memory and stay there. The limits are the same numbers: a list that runs past its time box answers 503 "too heavy", the visitor's budget of query time answers 503 "busy", the plain list is not held back by it, and a database that does not answer gives 503 `the match history is unavailable` with no word of SQLite's (each side's store looks again every 5 seconds, not the game server's minute, and says in the log when the database answers again). A request that is not a GET is answered 405 (`Allow: GET`) by the HTTP layer, as on the game server's public door.

## Two threads, nothing shared in memory

- The **counter thread** owns the one writer connection to the service's file, a replay store that looks at the recordings folder, the feeder (`HistoryFeeder`, the game server's own) and the list of approved maps. It looks at the folder every `--look-ms` (one stat of the folder's time when nothing changed), reads a file that is new when it has settled, plays it with the engine and puts the record into the database in one transaction. A recording appears in `/history` about one second after its file does with the default settings.
- The **HTTP thread** (the main thread) owns a read-only connection and a replay store of its own, and answers every request. Each `/history` answer is made inside one read transaction (`HistoryStore::ReadScope`), so a list, its `count` and its `total` are one state of the file, whatever the counter commits meanwhile; a look at `PRAGMA data_version` tells when another connection committed, and the counts and the sums of the maps of deaths are made again then.

The two meet in the database file (WAL: a reader never waits for the writer; a file system without WAL keeps the rollback journal, the log line says so, and a list may then wait a moment for a write) and in the log, which the service writes one line at a time. SQLite is built for one connection per thread (`third_party/sqlite/CMakeLists.txt`).

## The first start

A new database file has no history. With `--game-db` the service copies the game server's history into it once, at the first start: the matches, their seats, the markers of matches that the operator took out (so that a match whose recording is still there is not counted again), the maps of deaths, the actions of the matches, the play counts of the maps and the likes of the replays (the plays and likes' numbers are columns of the matches' rows), in **one transaction** together with a row `history_import` of `meta`. The game database is opened read only and read in one read transaction, so the game server may go on writing. What follows from that:

- A start again finds the row and copies nothing, whatever the game database holds now. The recordings that the game server counts after the copy are counted by the service from their files, once (the matches that were copied are known by their ids and are not counted again).
- A file of the service that already holds history of its own, and no such row, is left as it is: the play counts are sums and cannot be told apart from the game server's. The log says so; start with a new file to copy.
- A game database that cannot be read, has no history tables (a file of something else, or an old one), is the service's own file, or has a row that cannot be copied stops the start with a message that names it, and the service's file has no row of it.

The columns copied are listed in `src/ants_history/history_import.cpp`; a column that a later migration adds to the game server's tables is copied when it is added there and to `history_db_migrations()` together.

## Maps of the players

A match on a map of a player that the operator approved is counted like any other, the way the game server counts it: the file of the approved map is looked up by the name that the recording gives (in any letter case), in the game database's `map_submissions` (state `approved`) and the folder `--submissions-dir/files`, read again every minute and when a name is not known. Without `--game-db` such a recording is left out with one line in the log. A recording that cannot be counted for any reason (another simulation's rules, a map that is not here, a file that does not play out the same) is left out with one line, once, and tried again when the service starts. One that was left out only for want of its map (the list of approved maps could not be read, or did not hold the map yet) is tried again as soon as the list can be read or has changed: while such a recording waits, the list is read again every 5 seconds after a read that failed, and every minute otherwise.

## Files that are not whole

The store looks only at names the game server's store could have made. A file that is not a whole replay (cut, empty, not a replay) is neither counted nor listed, and is read again when the folder changes and its size is not the one that was read: a whole file put over it by a rename is counted at the next look. A `.tmp` of a write that was cut off, and every file with another name, are left alone, as are files older than the keep days.

## Recordings that go away

The service deletes no record, no recording and no marker. A marker (the row that keeps a match that the operator took out from being counted again while its recording is still kept) is deleted by the game server once the recording is gone, because only the game server knows that its folder is the right one; the service keeps the markers it has, a few bytes each. So a recordings folder that is empty, or is not the game server's, cannot make a deleted match public again. When the service starts on an empty folder while its history is not empty, the log says so in a line that names the folder (a quiet month looks the same, so it is a warning and not a refusal); every match is then shown as removed and nothing is counted until recordings appear.

## What it does not do yet

The game server keeps these until they move: the operator's `DELETE /history/<id>` and `DELETE /replays/<file>` (a deleted match leaves a marker so that its recording is not counted again), the hourly purge of recordings, the play counts that `/maps` reads (the service keeps its own `map_plays`, which `/maps` does not read), the plays and the likes of replays (its file has their columns and the table of the likes, lists and sorts by them, and copies them at the first start; the door that counts them and the pass that names a person stay on the game server, [`REPLAY_COUNTS.md`](REPLAY_COUNTS.md), so the route of `/history` must not move to the service before it takes them), the site statistics, and the database of the maps and accounts. The service has no secret and no write route.

## Tests

The cases are in [`TESTING.md`](TESTING.md), 2.19.20; the mutants of the service's rules are in `tests/data/history_service_mutants.json`.
