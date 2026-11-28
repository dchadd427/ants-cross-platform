# The export lists

Which files go to the public repository, and the checks that keep every file on exactly one side. Nothing here changes how the game, the server or the site are built or run.

| File | What it is |
|---|---|
| `allowlist.txt` | the paths that the export copies to the public repository (the game, its protocol, bots, replays, assets, the game page and the Map Maker page, and the tests of those) |
| `private.txt` | the paths that stay here and are never exported (the game server and its libraries, the history service, the hub, the site pages, the Docker stack and deploy files, the tests that need those, and this folder) |
| `classify.py` | the check: every tracked file is on exactly one of the two lists. It also prints the two path lists (`--list public`, `--list private`) and the allow-list as `git filter-repo` path regexes (`--filter-repo-paths`) |
| `check_export.py` | the scan of words: which public text files still name something private. **Report only** for now |
| `split_mutants.py` | the split of a list of mutants into a public half and a private half (below); with no argument it checks every list and changes nothing |
| `overlay/` | public versions of files that must read differently in the public repository (`AGENTS.md`, `README.md`, `SECURITY.md`, the CI workflow): `overlay/<path>` is exported as `<path>`. It is empty today; `overlay/.gitkeep` only keeps the folder and is never exported |

`export/` itself is private (`private.txt`): the public repository does not describe this one. The lists are the one place that says what is public; no other file repeats them.

## The check

```
python3 export/classify.py             # exit 1 and a line per file when a file is on neither list or on both
python3 export/classify.py --dead      # also the patterns that match no file
python3 export/check_export.py         # the scan of words (exit 0 always); --list names the files, --tsv FILE writes them
```

`./run_tests.sh --fast` runs both as suite 5.3, and CI runs them in the Linux job. The check reads `git ls-files`, so a new file is checked once it is added to the index.
`tests/scripts/test_export_lists.py` runs the check on made-up trees (the pattern rules, the overlay, both failures) and on this repository.

## Which list a file goes on

A new file that is on neither list fails the check, and nobody guesses: put it on one list, with the other list left alone (a pattern may be a path, `dir/**`, or a glob with `*`; a line that starts with `!` takes away what an earlier line of the same list matched).

- **Public** when it is part of the game: the simulation, the network protocol and the client's side of it, the bots, replays, assets, the application, the game page and the Map Maker page and their scripts, the original's data, the players' maps, and a test or document of those that runs and reads without anything private.
- **Private** when it belongs to the running service (the game server, the history service, the database, the control door, the hub, the site pages, the Docker stack, the deploy files), is a test or a document of those, or needs one of them to run (a test that starts a real server or links its library).
- A file that must read differently in the public repository is two files: the private one where it is, and the public one under `overlay/`.
- A list of mutants (`tests/data/NAME_mutants.json`) holds only mutants that a tree without the private files can run: see "Lists of mutants" below.
- A script or a test that only names a private file in a comment is still public: the scan of words lists it, and the change that cuts the mention is made on its own.

## Lists of mutants

A mutant is public when everything it touches is public: the file it changes, every path in its `test` command (a program, a script, a page or a data file that the command is given) and every CMake target in its `build`. A list that has public and private mutants is split by `python3 export/split_mutants.py --write NAME` (the table says what it will do; `--why` says why each mutant is private; `--public-note NAME=TEXT` or `--notes FILE` gives the public half its own note, which must not name anything private):

| File | Holds | List |
|---|---|---|
| `tests/data/NAME_mutants.json` | the public mutants, the note, and the `defaults` and `baseline` that they need | public: its line goes on `allowlist.txt` (and comes off `private.txt`, or gets a `!` line there when a pattern of that list matches it) |
| `tests/data/NAME_server_mutants.json` | the private mutants, byte for byte as they were, and the whole of the old note | private, by the one pattern `tests/data/*_server_mutants.json` |

A list with no public mutant stays whole, as `NAME_mutants.json`, on `private.txt`. `tools/mutate.py` reads `NAME_mutants.json` together with `NAME_server_mutants.json` when it is there (so does `tests/scripts/mutant_specs.py` for the tests that run some mutants themselves), and a tree that has only the first half runs that. `tests/scripts/test_mutant_specs.py` checks that every mutant of both halves applies; `tests/scripts/test_export_split_mutants.py` fails when a list is mixed or on the wrong list, which is how a new list with a private mutant in it is found.

## The hooks: private files that the public files read when they are there

The exported tree has no server, so the build and the test runners of the public files never name one: each reads a private file if it exists, and a tree without the file is the game, its libraries, tools and tests.

| Private file | Read by | What it holds |
|---|---|---|
| `cmake/private.cmake` | `CMakeLists.txt` (`include(cmake/private.cmake OPTIONAL)`, at its end) | the game server's libraries, programs and tests: SQLite, the database layer, the control interface, the server, the history service, the picture maker, `tests/test_db`, `test_ctl`, `test_server`, and `test_app_server` (the application tests that run a real room manager) |
| `tests/suites_server.sh` | `run_tests.sh` (`define_suites`, before the E2E suite) | the `suite` lines of the server's tests and scripts; `--fast` and the tier options select them like any other |
| `tests/scripts/costs_private.py` | `tests/scripts/run_python_tests.py` (at import) | the `COSTS`, `SHARDS` and `LINUX_ONLY` entries of the python tests that the exported tree does not have |

`tests/scripts/test_runners_private.py` holds the checks of these two runners that name private suites or tests; `test_run_tests.py` and `test_run_python_tests.py` check the rest and the hooks themselves.

## The scan of words

`check_export.py` counts, in every public text file, the mentions of private headers and targets, private paths and documents, private web files and tools, the program `ants_server`, the web server and the stack, and private test targets (the list `KINDS` in the script; add a name when a new private name appears). It prints the number of files and hits, by kind and by area. It does not fail until the mentions have been cut from the public files; then `--strict` makes it the gate of the export.

## What is not here yet

The scripts that make the export itself (the first export with history, the export of a release, the full check of the exported tree) and the public overlay files are added by later changes.
