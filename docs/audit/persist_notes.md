# Restart records (phase 1, the server): what was built, how it was checked, what it costs

## State at handoff

Branch `persist` = phase 1 (the server side) on top of the v0.2.0 merge. The independent review found no critical or high issue, and **every finding below is now done**: each code fix has a test and a mutant check (`tools/mutate.py`; every mutant that the build accepted was caught, except the one named). Phase 2 (below the table) is the next feature. Test ids of this work: S3.82 - S3.112, N2.96 - N2.99, N4.21. The reviewer's drivers are not in the repository: the tests were written from their descriptions. Gates of each fix: `./run_tests.sh --fast`, `test_server`, `test_lockstep` (N2.96 - N2.99), the e2e parts `options` and `reconnect`, `-DANTS_WERROR=ON`; at the end AddressSanitizer and UBSan on the server and lock-step suites and a leak scan.

| Finding | Status | What was done, and the test that proves it |
|---|---|---|
| **M1** a hostile record costs about 2 GB at start | **done** | `kRestartMaxTurns` (1,800,000: 25 h of turns), judged from a TURNS frame's header before a turn is decoded; the replay streams the turns (`RestartRead::Streaming`, `for_each_restart_turn`: the record keeps its bytes, not its turns); keep time and run limit at most 24 h. **S3.103** (the 47 MiB record of empty turns is refused at start; the limit to the turn; both read modes); 10 of 10 mutants caught. The choice of the streaming read in `restore_rooms` changes only the memory used and has no test of its own. |
| **M2** the synchronous restore | **done** | `Room::replay` (engine, turns, hash checks) and `Room::begin_restored`; records newest first by time of last write, inside 30 s in all and 20 s a room (`RestartConfig`); the rest restored lazily by the first Hello for their code (`create_room` answers 409 for such a code); the stop flag, checked every 20 turns and between rooms, leaves the records on disk; the rooms begin together with a clock that starts then. **S3.106** (many copies of a record in a small budget, newest first, deferred records and the Hello that restores one, the room's cap, the fresh clock, the stop flag), **S3.107** (the real program told to stop in the middle of a restore); 19 of 20 mutants caught, the 20th is equivalent (whether the lazy restore comes before the demo room cannot be seen: `create_room` answers 409 for a deferred code anyway). |
| **M3** `/busy` counts orphaned restored rooms | **done** | `Room::busy(now_ms)`: a room counts in `matches` while a person is present or catching up, or for 5 minutes after its restore (`kRestoredBusyWindowMs`); `players` is the present people plus, inside that window, the held seats; the JSON keeps exactly `matches` and `players`. **S3.102** rewritten, with both sides of the window; 13 of 13. |
| **M4** gate on the protocol, not the game version | **done** | `RestartIdentity::same_rules_as` compares `protocol` only; the game version and build id are for the log; the refusal text names the protocols; the wording of this file and the README says so. S3.85, S3.91, S3.102; the mutant "the game version gates again" is caught by S3.85. |
| **M5** two servers on one folder | **done** | `RestartStore::prepare` takes `flock(LOCK_EX \| LOCK_NB)` on `restart/.lock` for the life of the store (Windows: `CreateFileW` with share mode 0, built by CI only); locked: `prepare` fails, `main.cpp` exits 1 for an explicit `--restart-dir` and logs that records are off for the default folder; `room-*.tmp` are cleaned up only after the lock is held. **S3.104** (two stores, across a killed process), **S3.105** (the real program); 10 of 10. |
| **L1** a failed delete | **done** | The store remembers the paths whose delete failed (stale); `RoomManager::update` retries every 10 s and `shutdown` once; the status says `record.stale` and never says `kept: false` while the file is there. **S3.109** (a folder that takes the file's name makes the delete fail, and works for root, which `chflags uchg` does not); 13 of 13. |
| **L2** refused records | **done** | A record that was read and refused moves to `restart/refused/` (mode 700) for 24 h; corrupt ones are deleted at once; the purge is at start and the folder stays under the budget. **S3.110**; 9 of 9. |
| **L3** the start message is written before it is sent | **done** | `HostLobby::set_before_start(fn)`, called in `HostLobby::start` between building the Start and its broadcast; the room opens its record there. **N4.21** (nothing has reached a guest when the hook runs), **S3.108** (the folder looked at when each Start leaves the server); 6 of 6. |
| **L4** flush cost of 256 rooms | **done** (docs) | NETWORK_PORT.md "Measured" and the Measured table below: the average times the rooms, and a flusher thread as phase 2 material. |
| **L5** writability probe | **done** | `prepare()` makes, writes and deletes a probe file; an explicit `--restart-dir` that fails exits 1, the default one logs that records are off. **S3.111** (a process that may not grow a file: `EFBIG`, and root can be tested); 5 of 5. |
| **L6** a won vote whose Drop is not sealed | **done** (docs) | Added to the limits in NETWORK_PORT.md. |
| **L7** age limit against long pauses | **done** (docs) | Added to the limits in NETWORK_PORT.md. |
| **L8** open without a race | **done** | `read_restart_record` opens once (POSIX `O_NOFOLLOW`, `O_NONBLOCK`, `O_CLOEXEC`, then `fstat` of the descriptor; Windows `CreateFileW` with `FILE_FLAG_OPEN_REPARSE_POINT`, then `GetFileInformationByHandle`, built by CI only: the reviewer's `windows.h` mock was not available). **S3.112** (a link, a folder and a pipe are refused, the pipe in a child with an alarm; the limit to the byte); 10 of 10. The swap itself cannot be made on purpose: the test proves each refusal, not the race. |
| **L9** README wording | **done** (docs) | "and that the others are not cut off" removed from the README paragraph. |
| **L10** budget and folder fsync wording | **done** (docs) | NETWORK_PORT.md and README: the 256 MiB budget covers the records of the running rooms (open files); the folder `fsync` is best effort. |

What is left is not a finding: the points of the section Open (the clients that rejoin, the restore that is bounded and not asynchronous, the pause totals) and the Windows branches that only CI builds (the lock, the open of a record, the writer).

**Phase 2 is the next feature** (the owner's real goal; nothing of it is started): the native and web clients rejoin by themselves (retry with a back-off for a few minutes, the overlay "Reconnecting... (the server is restarting)", the existing catch-up screen; the key kept per room and seat: native in a mode 600 file next to the settings, web in `localStorage` `ants.rejoin.<room>.<seat>`, forgotten at the match's end or after 24 hours; a BadRequest to a Hello that carries turns means "start from nothing"); `--reconnect` on in `docker-compose.stack.yml` (and `docker-compose.staging.yml`) and `kReconnectByDefault` true in `room_manager.hpp`; the start dialog yields to the pause notice and the vote buttons (test a seat lost 2 s into the 5 s pre-start); "Rejoin your match" in the start menu and `web/lobby.html`; end-to-end tests with the real clients (a restart, a page reload, a Wi-Fi blip, the countdown, the vote). The fixes above are in, so it can start (M2 and M3 matter most once clients rejoin).

Update for the implementation plan (that file is not part of the repository; the owner copies this text into it). A match that runs on the dedicated server survives a restart of the server: the server keeps a **restart record** of every running room that holds seats, and the server that starts again brings the matches back, paused, every seat held, until the players come back with their keys. This is the first half of the owner's request (the server side, with no change to the network protocol: it stays 12); the clients that rejoin by themselves are the second half and are not built yet. The reference text is [`docs/NETWORK_PORT.md`](../NETWORK_PORT.md), "Restart records"; this file has the decisions, the mutation check, the measurements and the open points.

## Built

| Item | Where |
|---|---|
| The record: a file of frames (`ANTSRST1`; HEAD, TURNS, CHECK; CRC-32), the strict reader (a torn tail is dropped, every other fault refuses the file, nothing is allocated beyond the input), the writer (the head made all at once, one `write()` a turn, `fsync`), the store (folder, disk budget, notices) | `include/ants_server/restart_record.hpp`, `src/ants_server/restart_record.cpp` |
| The room keeps its record from the start of its match to its end (`record_open` / `hook_up` / `flush_record` / `release_record` / `record_discard`), restores itself from one (`Room::replay`, then `Room::begin_restored`), and a refused record becomes a failed room (`Room::refused`) | `include/ants_server/room.hpp`, `src/ants_server/room.cpp` |
| The manager restores every record at start (`restore_rooms`), stops with the records kept (`shutdown`), checks the ranges of a restored room with the same function as `POST /rooms` (`spec_range_error`), carries the switch `kReconnectByDefault` (off) | `include/ants_server/room_manager.hpp`, `src/ants_server/room_manager.cpp` |
| The session: `set_on_seal` (a turn is written before it is sent), `set_on_referee_hash` (the checkpoints), `restore_turn`, `start_restored` | `include/ants_net/session.hpp`, `src/ants_net/session.cpp` |
| The attendance: `seat_restored` (every person's seat absent and **excused**, the vote after the restart's own wait, no loss for the flapping rule, nothing added to a seat's away time) | `include/ants_net/attendance.hpp`, `src/ants_net/attendance.cpp` |
| The program: `--restart-dir`, `--no-restart-records`, `--restart-vote-seconds`, `--restart-budget-mb`, the restore at start (the listeners are open, no connection is read yet), the stop that keeps the records, the log lines; the status JSON has `record` and `restored` | `src/ants_server/main.cpp`, `src/ants_server/control.cpp`, `include/ants_server/control.hpp` |
| The stack gives the server 15 s to stop | `docker-compose.stack.yml`, `docker-compose.server.yml` |
| Tests | `tests/test_net/test_lockstep.cpp` (N2.96 - N2.99), `tests/test_server/test_server.cpp` (S3.82 - S3.112: the real-process ones S3.95, S3.96, S3.105 and S3.107 behind `ANTS_HAS_SERVER_BINARY`, S3.101 against a real container, opt-in, S3.102 the `/busy` count of a restored room, S3.103 - S3.112 the findings of the review), `tests/test_net/test_lobby.cpp` (N4.21), `tests/scripts/test_ants_server.sh` (its `options` and `reconnect` parts), `tests/test_server/ants_server_binary.cpp.in` (where the program of the process tests is, generated like the paths of `ants_test_paths`) |

## Decisions, and why

* **A turn is written before it is sent, `fsync` is once a second.** The question was how much a restart may lose. A `write()` puts the bytes in the operating system, which keeps them when the process dies at any instant (a crash, `kill -9`, the container's death), so with the write placed between the turn log's append and the broadcast (`set_on_seal`) the record holds every turn that any player has run: **no turn that a player has seen is lost by the death of the server**. What `write()` does not survive is the death of the machine: that is what the once-a-second `fsync` is for, so a power cut loses at most the last second (20 turns) of the record's durability, and a player who ran turns that the disk lost is told `BadRequest` and starts the match from nothing again (its key is good). A flush per turn would be 20 a second per room on the one thread that every room shares; the measurement below shows what one costs.
* **The vote after a restart opens at 90 s, not 30 s.** A lost link is one player's trouble and the others vote after 30 s. A restart is nobody's fault and **every** player has to find out (a closed link at once, 10 s of silence otherwise), wait for the server (a deploy is a build and a start), reconnect (the clients try every 2 s) and catch up; a background tab wakes late. With 30 s the first player back could vote out a neighbour who is a few seconds behind. 90 s is three lost links' time and still short enough that a player who never comes back does not hold the others for long (the cap of 30 minutes is the bound anyway). The wait is `--restart-vote-seconds` (30 - 3600) and never less than the room's own vote time.
* **The absence is excused.** `Attendance::seat_restored` marks the seats; an excused absence adds nothing to the seat's away time when it is back and is no loss for the flapping rule (a restart must not make a player's next blip count as the second of three). The pause itself counts toward the cap, so a room whose players never come back ends "everybody left" by the existing rule.
* **The record is checked all through, the replay is checked against the old server's own hashes.** The reader refuses what an honest writer would not write; the replay compares the referee's state hash with the one stored at every 20th turn, so a rule that changed within a version, or a record that was altered with a valid checksum, stops the restore at the first checkpoint. A refused record becomes a **failed room** that names the reason (another network protocol, a map that is gone or changed, older than an hour, a replay that disagrees, a room this server would not make, too slow, too big, no room), and its record is kept for a day in `restart/refused/` (a corrupt one is deleted at once): a clear end instead of a match that never comes back.
* **Only the protocol decides, not the game version or the build id.** The version moves with every release and the protocol with every change of the rules: a deploy that changes a page or a log line (or only the version) must not end the matches; if it changed a rule without moving the protocol, the checkpoints catch it.
* **Nothing of the engine is stored.** The match is its start message and its turns; the engine is rebuilt by playing them (about 6,000 turns a second on the busiest map).
* **A record that cannot be completed is deleted at once.** A write that fails, a record past 48 MiB, a budget that is used up, a turn log that passed its limit: the file goes, the room plays on and says why. A record that stopped in the middle of a match would bring it back at the wrong tick, which is worse than none.
* **Only rooms that hold seats keep a record.** A room that does not hold seats cannot hold them across a restart; the switch that makes seats the default (`kReconnectByDefault`) is built and tested and **off**, because the game's own clients do not rejoin by themselves yet: turning it on now would make every lost player hold the others for the vote.
* **Waiting rooms are not kept.** They have no match; their players join again (a demo room is made again by the first Hello).
* **The record is for its owner only** (mode 600 in a folder of mode 700) because it holds the keys; no key is in any log line, status or result file (S3.91 looks for the keys' hex in every text the server makes).

## Checked

The deep tier: every new test must fail without the code it tests, so a fault was put into a scratch copy of the code for each behaviour (the file edited by exact text, its object deleted, ccache switched off, only the target rebuilt, the test run with a filter) and the test that fails is named. Two false survivors of the harness itself are worth knowing: `make` here decides by modification times in whole seconds, so a mutant written in the second in which its predecessor's object was built was not rebuilt, and ccache answered with the object of an older text; the harness now deletes the file's object before and after every mutant and runs with `CCACHE_DISABLE=1`.

**71 distinct faults were put in; 70 are caught by a test of this work and one (A7) is an equivalent mutant.** A = the attendance, H = the host session, R = the record file, M = the room and the manager, P = the real program (P6 is the same fault as H5, tried again at the level of the process). A7 is a change that no test can see because it cannot change a result: an excused seat and a seat that stops flapping never coincide, so the product keeps the line as it was before this work. The check found one weak assertion as well (R12: a name without the hash of the code survived because S3.82 compared names case-sensitively; it compares lower-cased names now).

### The attendance and the session (`test_lockstep`)

| Fault | Result |
|---|---|
| A1: excused never set | N2.96 |
| A2: restart wait = a lost link's | N2.96 |
| A3: excused absence added to away time | N2.96 |
| A4: restart counts as a loss (flapping) | N2.96 |
| A5: dropped seats come back Absent | N2.96 |
| A6: a lost link stays excused | N2.96 |
| A7: vote needs no restart wait in update() flap-close | equivalent mutant (see above) |
| H1: restore_turn skips the log | N2.97 |
| H2: start_restored: no resume of the sequencer | N2.97 |
| H3: start_restored: seats Present | N2.97 |
| H4: restore_turn: not queued for the runner | N2.97 |
| H5: on_seal after the broadcast | N2.98 |
| H6: on_seal before the log append | N2.98 |
| H7: on_seal only for hold_seats | N2.98 |
| H8: referee hash hook for a host with a seat | N2.98 |
| H9: referee hash hook never called | N2.98 |
| H10: restore_turn without hold_seats allowed | N2.99 |
| H11: restore_turn out of order allowed | N2.99 |
| H12: restore_turn after start allowed | N2.99 |
| H13: a seat without a key is not dropped | N2.99 |
| H14: a full log is ignored | N2.99 |

### The record, the room and the manager (`test_server`)

| Fault | Failing test |
|---|---|
| R1: crc polynomial | S3.82 |
| R2: a frame that does not fit is corruption | S3.82 |
| R3: last frame bad checksum is corruption | S3.83 |
| R4: keys need not be distinct | S3.83 |
| R5: a second head is fine | S3.83 |
| R6: turn numbers not checked | S3.83 |
| R7: checkpoint of an absent turn | S3.83 |
| R8: writer: turns out of order accepted | S3.82 |
| R9: record file mode 644 | S3.82 |
| R10: folder mode 755 | S3.82 |
| R11: reopen keeps the torn tail | S3.82 |
| R12: name without the hash | S3.82 |
| R13: every file with the extension is a record | S3.86 |
| R14: the head is written in place, not made all at once | S3.82 |
| R15: budget not given back by discard | S3.82 |
| M1: no hook: nothing is written | S3.84 |
| M2: no record at the start | S3.90 |
| M3: finished room keeps its record | S3.84 |
| M4: cancelled start keeps the record | S3.90 |
| M5: failed room keeps its record | S3.90 |
| M6: shutdown closes the rooms with records too | S3.90 |
| M7: checkpoints are not verified | S3.85 |
| M8: dropped seats come back absent | S3.92 |
| M9: the fill bots do not sit down | S3.93 |
| M10: no bot controller after a restore | S3.93 |
| M11: the run limit starts at the restart | S3.98 |
| M12: any version is restored | S3.85 |
| M13: any map file is restored | S3.85 |
| M14: age is not checked | S3.85 |
| M15: refused records stay on disk | S3.85 |
| M16: no failed room for a refused record | S3.85 |
| M17: rooms made by create_room keep no record | S3.84 |
| M18: the fill mask is not recorded | S3.93 |
| M19: the record keeps the clients' addresses | S3.99 |
| M20: a dead log does not end the record | S3.89 |
| M21: the restart's wait is a lost link's | S3.92 |
| M22: names are not restored | S3.84 |
| M23: joined counts the lobby | S3.84 |
| M24: rooms beyond max_rooms are restored | S3.85 |
| M25: a match that ended is not finished | S3.94 |
| M26: status JSON without record | S3.84 |
| M27: unreadable files are not deleted | S3.86 |
| M28: a bad range of the head is restored | S3.85 |
| M29: the log's budget refusal is ignored (replay) | S3.85 |
| M30: the replay time budget is ignored | S3.85 |

### The real program (`test_server`, the process tests)

| Fault | Result |
|---|---|
| P1: the stop closes every room (the old loop) | S3.95 |
| P2: no restore at the start | S3.95 |
| P3: the stop line is not logged | S3.95 |
| P4: records are never enabled | S3.95 |
| P5: the default folder is the results folder itself | S3.95 |
| P6: SIGKILL variant: a record per turn is not written before the send (the hook is after) | survives the process tests by construction (a kill between two passes cannot tell the order of two calls in one pass); it is the fault H5 above, caught by N2.98 |


## Measured (macOS, Apple silicon, Release build; S3.100)

The machine was shared with other work (its load average was about 40 on 10 cores), so these are upper bounds, not the best of a quiet machine; the figures are the spread of three runs.

| What | Measured |
|---|---|
| A record's size for a minute of play | **21.7 KB** idle (1,200 turns of 17 bytes and 60 checkpoints), **25.9 KB** with three players who each give an order every 0.7 s, **27.4 KB** with four |
| A whole match | TREASURE, four players, 11 minutes 40 s of play (13,999 turns, the longest match of the longest map): **350 KiB** (a full 12-minute match is 14,400 turns, about 360 KiB) |
| Writing a turn (one `write()`) | **2.1 - 2.6 microseconds** on macOS, 0.56 on Linux |
| A flush of a second's turns (`fsync`) | macOS **0.04 ms** on average, 0.5 - 0.6 ms at the worst (its `fsync` does not flush the drive's cache); Linux (Debian 12 in a docker VM, real `fsync`): **1.35 ms** on average, 6.6 ms at the worst. Every room flushes once a second on the server's one thread, so 256 rooms cost 256 times the average: macOS about 10 ms of every second, the Linux VM about 350 ms (a third of the thread); at 30 ms a flush it would be more than a second a second: not possible. A flusher thread is phase 2 material |
| Bringing a match back | TINY, three players, 5 minutes: **0.10 s**; **TREASURE, four players, 11 minutes 40 s (13,999 turns): 2.3 - 2.9 s** (4,800 - 6,000 turns a second); a full 12-minute match is about 3 s |
| Stopping | the real program exits **2 ms** after SIGTERM (macOS), 4 ms (Linux); `docker stop` of a container with a running match 0.18 - 0.23 s, exit code 0 |
| A restart seen by a match | the record held **154 or 155 turns when the machines had been sent 154** (the record may hold one turn more than any machine has seen, never fewer) |

`ANTS_PERSIST_MEASURE_LONG=1` makes S3.100 play the long matches (TINY for 5 minutes, TREASURE for 12 minutes less 20 s: 13,999 turns); without it the test plays three minutes of TINY.


## Gates

**With the fixes of the review** (the rows of the table "State at handoff"):

| Gate | Result |
|---|---|
| Release build with `-DANTS_WERROR=ON` | 0 warnings |
| `./run_tests.sh --fast` | every suite passes except two python tests that check that the children of a killed test are collected at once (they fail in the sandbox where this was done, which collects a killed child late; CI runs them) |
| `test_server` | 110 tests, 60,477 assertions, 0 failed |
| `test_lockstep` | 117 tests, 3,163,737 assertions, 0 failed |
| The server end-to-end script, parts `options` and `reconnect` | 113 checks, 0 failures |
| **AddressSanitizer + UBSan** (Debug, `-DENABLE_ASAN=ON`, built without `-Werror`: see below) | `test_server`: 110 tests, 60,455 assertions (its real-process tests run the sanitized server); `test_lobby`: 25 tests, 64,818 assertions; `test_lockstep`: N2.40 - N2.50 and N2.96 - N2.99; all pass, no finding. The rest of the lock-step suite was not run under the sanitizers (the machine was loaded, and the code of that suite is as it was: the one change in `ants_net` is the lobby's hook, which `test_lobby` covers) |
| Mutants (`tools/mutate.py`) | 96 faults put in the code of the fixes: 95 caught by a test of the fix, 1 equivalent (M2: whether the lazy restore comes before the demo room cannot be seen) |
| Leak scan of the diff and of the commit messages | no path, host, token or key |

The Debug build with `-Werror` stops at `-Wsign-conversion` for `(mask >> n) & 1u` (GCC 12 without optimisation) in `match_stats.hpp`, `bot_controller.cpp`, `session.cpp`, `room.cpp` and `restart_record.cpp`: code from before the fixes, and the Release build that CI and `run_tests.sh` use has none.

The work was done on the v0.2.0 candidate, then rebased onto the merge of v0.2.0 (the clock behind the start dialog, F1: a room discards a command sent before its first turn, the `/busy` count, the parallel runner, the split server script, the ants_test_paths scheme). What ran on which tree:

**On the final tree (after the rebase):**

| Gate | Result |
|---|---|
| Build with `-DANTS_WERROR=ON` (`-Wall -Wextra -Werror -Wsign-conversion`), Release | the whole tree builds with 0 warnings |
| **Full `./run_tests.sh`** (the parallel runner, detached into a log) | **62 suites, 0 failed, 180 s** (917 s of suite time added up) |
| `./run_tests.sh --fast`, before every commit | passed (48 suites, 50 s) |
| `test_server` | 100 tests, 60,086 assertions, 0 failed (the real-process SIGTERM and SIGKILL tests included) |
| `test_lockstep` | 117 tests, 3,153,417 assertions, 0 failed |
| The server end-to-end script, every part (`--list-parts`, side by side as the CI runs them) | 201 checks, 0 failures: options 56, rooms 53, secret 22, demo 13, reconnect 57 (the restart section is in the reconnect part: the record's mode, SIGTERM within 0.02 s with the record kept, the room restored with its two seats, a second restore after SIGKILL, `DELETE` removes the record, no key in the log) |
| The python tests of `tests/scripts` (the CI guards: the paths scheme `test_cmake_paths.py`, the runner and the parts `test_run_tests.py`, the CI against the runner `test_ci_workflow.py`, the configure with the Xcode generator `test_cmake_version.py`) | 302 tests pass |
| Leak scan of `git diff origin/main` and of the commit messages | no path, address (but 127.0.0.1), name, host, key or secret; the author lines are the repository's own; every commit ends with one trailer |

**On the tree before the rebase** (the code of the restart records is the same; the rebase changed the tests' ids, the way the process tests find the server, one test's tamper and added `S3.102`):

| Gate | Result |
|---|---|
| Debug build with AddressSanitizer and UBSan (`-DENABLE_ASAN=ON -DANTS_WERROR=ON`) | 0 warnings |
| **AddressSanitizer + UBSan** (`halt_on_error`; the logs are searched for `runtime error` as well) | `test_server` (97 tests, 60,007 assertions; its real-process tests run the ASan-built server), `test_lobby`, `test_tcp`, `test_ws`, `test_netgame`, `test_latency`, `test_jitter`, `test_ctl`, `test_network_app`, `test_start_menu_app`: all pass, no finding. `test_lockstep`: the 48 tests that touch the attendance, the hold of seats and the restart (N2.40 - N2.99, one process each, the four new ones among them) pass with no finding; the whole suite was started as well and had passed 68 of its 115 tests, with no finding, when it was stopped after an hour (the sanitized lock-step suite is about fifty times slower than the release one; the full run without sanitizers passes all 115). The build is Debug, `-DENABLE_ASAN=ON -DANTS_WERROR=ON`, 0 warnings |
| **Linux**: Debian 12, GCC 12.2, `-Werror` (a docker build of the worktree: `ants_server`, `test_lockstep`, `test_server`) | 0 warnings once a GCC 12 false positive (`-Warray-bounds` / `-Wstringop-overflow` for a `vector::insert` of 8 bytes into an empty vector) in one test helper was worked around. Both suites pass there: `test_lockstep` 115 tests, 3,153,356 assertions; `test_server` 97 tests, 60,024 assertions, with the real-process SIGTERM and SIGKILL tests and the disk-limit test with a real `EFBIG`. (The base image of that check was gone from the machine by the time of the rebase: the Linux build of the rebased tree is left to the CI.) |
| `docker build -f Dockerfile.server -t ants-server:ps .` and **a real container** | the image builds (GCC 12, 0 warnings). S3.101 (then S3.95) against a container with a named volume: `docker stop` 180 ms, exit code 0, the room back with 157 turns (the machines had been sent 156), both machines found it by themselves, the match ended in one state, the record on the volume was mode 600 in a folder of 700 and was gone at the end, no key in `docker logs`. The same with the stack's `read_only`, `cap_drop: ALL`, `no-new-privileges` and `pids_limit`: 225 ms, 158 / 157 turns. The stack's own command (no `--reconnect`): the log says that no record is kept, `docker stop` 0.19 s, exit code 0. The tag, the containers and the volumes were removed |
| The whole tree, full run | 57 suites, 0 failed, 682 s (the serial runner of then) |

## Open

- **Before phase 2 switches seats on** (today no room holds seats by default, so no record is kept and nothing is restored in production): the lazy restore of a deferred room replays on the server's one thread, up to 20 s, while every client takes 10 s of silence as a lost link. Make the replay incremental (a slice of turns per update, the room waiting in a "restoring" state, its first Hello answered when it is done); the per-room cap of 20 s can then go.

* **The clients do not rejoin by themselves yet** (the second half of the request). A player whose server restarts still reads "The connection to the other players was lost." until the native and the web clients keep their key, retry with a back-off, show "Reconnecting... (the server is restarting)" and rejoin, and the switch `kReconnectByDefault` is turned on. What the clients must do is in `docs/NETWORK_PORT.md`, "Restart records", the last limit.
* **The pause that was spent before a restart is not in the record.** Every seat starts the restart with 0 away and the match's pause cap starts again (a match that had spent 20 of its 30 minutes of pause has 30 again). The cap still bounds the wait for any one restart, which is what the players care about; recording the attendance's totals would add a frame type and a restore step for little gain.
* **The restore is bounded, not asynchronous.** The server answers nobody while it replays: 30 s at the most at the start (20 s a room), and a deferred room's replay (at most 20 s) runs inside the pass that reads its first Hello, so every other room waits for it; a client that waits longer than 10 s for a turn takes the server for lost. An incremental replay (a slice of turns every pass) is phase 2 material. The listeners are open meanwhile, so connections wait in the kernel's queue; the container's health check (a TCP connect) passes.
* **The machine's death** can take the last second of turns from a record (the players ahead start from nothing). macOS's `fsync` does not flush the drive's own cache (`F_FULLFSYNC` does); the server is a Linux container, where `fsync` is the real flush.
* **The Windows branches** of the file layer (the writer, `FlushFileBuffers`, the folder, the folder's lock, the open of a record) are written and are not run by any test on this machine; CI builds them. The records take the permissions of their folder there, as the control secret's file does.
* **A deploy that changes the network protocol ends the running matches** (their rooms are failed rooms with that reason). A deploy that wants to keep them waits for a quiet server; that is the deploy job of the CI's own work.
