# The export lists

Which files go to the public repository, and the checks that keep every file on exactly one side. Nothing here changes how the game, the server or the site are built or run.

| File | What it is |
|---|---|
| `allowlist.txt` | the paths that the export copies to the public repository (the game, its protocol, bots, replays, assets, the game page and the Map Maker page, and the tests of those) |
| `private.txt` | the paths that stay here and are never exported (the game server and its libraries, the history service, the hub, the site pages, the Docker stack and deploy files, the tests that need those, and this folder) |
| `classify.py` | the check: every tracked file is on exactly one of the two lists. It also prints the two path lists (`--list public`, `--list private`) and the allow-list as `git filter-repo` path regexes (`--filter-repo-paths`) |
| `check_export.py` | the scan of words: which public text files still name something private. **Report only** for now |
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
- A file of mutants (`tests/data/*_mutants.json`) is public when no mutant of it changes a private file (a private test may still be the one that catches some of them: the file's note and the command lines of those mutants are reworded later); it is private when any mutant changes a private file.
- A script or a test that only names a private file in a comment is still public: the scan of words lists it, and the change that cuts the mention is made on its own.

## The scan of words

`check_export.py` counts, in every public text file, the mentions of private headers and targets, private paths and documents, private web files and tools, the program `ants_server`, the web server and the stack, and private test targets (the list `KINDS` in the script; add a name when a new private name appears). It prints the number of files and hits, by kind and by area. It does not fail until the mentions have been cut from the public files; then `--strict` makes it the gate of the export.

## What is not here yet

The scripts that make the export itself (the first export with history, the export of a release, the full check of the exported tree) and the public overlay files are added by later changes.
