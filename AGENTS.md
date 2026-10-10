# AGENTS.md

Ants is a C++17 remake of the 1998 RTS (SDL2; native on macOS, Linux and Windows, and in the browser through Emscripten) with a 20 Hz deterministic simulation, the original's assets decoded from its archive, lock-step multiplayer, a match server, bots, replays and a web front page. Beta: beta.playants.org.

## Commands
- Build: `cmake --build build -j8`. Run: `./start_game.sh`.
- Quick tier, before every commit: `./run_tests.sh --fast` (about 25 seconds). Full suite: `./run_tests.sh` (CI runs it for every pull request).
- Web page without Docker (cloud sessions): `tools/web_without_docker.py -- COMMAND`. Docker build: `docker build -t ants-beta .` (CI does it too).
- Branches, pull requests, releases, deploy and staging: `docs/WORKFLOW.md`.

## Rules that always apply
1. **Branch, CI, merge.** Work on a branch, never on `main`; commit as soon as the quick tier passes. `main` takes a change only through a pull request whose five checks (Linux GCC, macOS Apple clang, Windows MSVC 2022, Windows MSVC 2026, Web) are green, merged with a merge commit. A merge that changes the game or build redeploys the beta and waits for a moment with no match running. CI is the full gate; run the full suite, a Docker build or the sanitizers (`./run_tests.sh --asan`, CI has none) locally only for what CI cannot show.
2. **Quality.** No warnings (`-Wall -Wextra -Werror -Wsign-conversion`; the ASan build leaves `-Wsign-conversion` off). Never break, disable or weaken a test.
3. **Public repository.** Never commit credentials, tokens, private URLs, personal data, local paths, or anything read on the owner's servers (logs, addresses, ports, process lists, dashboards); describe a fix that follows from it in general terms. Plans, design notes and the owner's answers are kept outside this repository and are never named in a public file.
4. **The original game.** `Original-Ants/Ants.exe` and `docs/legacy/Ants.exe.c` are local copies, never committed, nor any copy of the program or its decompilation, function maps, string dumps or disassembly listings. The repository holds only `Original-Ants/ants.chd`, `Original-Ants/Maps/*.LVL`, the music, the decoded sprites and sounds (`asset_catalog/`, `docs/chd_table4_animations.json`), the front-page pictures (`web/front/`) and `src/ants_sim/movement_tables_data.inc`, the movement and animation tables that the simulation needs, which `tools/extract_movement_tables.py` generates from the owner's local copy of the program. Pinned digests in `tests/common/original_program_bytes.hpp` change only with the program present. When you change something the original defines (a rule, a timing, an animation), check it against those sources, never guess, and record what you find in `docs/GAME_REVERSE_ENGINEERING.md`; if you do not have them, leave it as it is.
5. **Bots are virtual clients** (library `ants_ai`). They use the public command interface of a human, read the world through a read-only view, see only what a human of their team could see (with Fog of War on, only their team's view; until that view exists, bots and fog are refused together), keep a command budget per level, exist only when asked for, are always shown as "Bot (Level)", and never change the simulation, the lock-step rules or any golden hash. The only computer behaviour inside the simulation is the original's own (the Combat Ant's auto-engage reflex).
6. **Web and version.** `.wasm`, `.data`, `.html`, `.js` and `.css` are served with `Cache-Control: no-cache, must-revalidate`. Keep the web build in step with every game, asset or simulation change; local web images are for testing only, never pushed or deployed. The file `VERSION` is the one source of the version; every build shows its build id; the on-screen version stays beside the FPS counter at the bottom right in every state.
7. **Screens.** Nothing on screen is built until the owner has approved a picture of it (fixes that change no look are exempt). Say "player", never "friend", in on-screen text.
8. **Times.** Every date and time written for the owner is Pacific time.
9. **One home, delete what you replace.** Every topic has one home page, listed in the README; edit it in place and never describe the same thing in a second place. A change deletes what it replaces (code, tests, documents, switches) in the same pull request. A switch that is off by default gets an expiry: retire it or remove it within two releases.
10. **Tests.** A test goes in the file of its topic; a test file over 2,500 lines is split.
11. **Threads and branches.** After your pull request merges, restart your branch from `main` (a push recreates it). A branch with no pull request for a week is reported by the coordinator and either gets a pull request or is archived.

## How much checking
- Every change: the quick tier. Every pull request: CI's five checks, and for code one independent review with every finding fixed.
- Deep checks (mutation-proven tests with `tools/mutate.py`, `./run_tests.sh --asan`, soak and browser runs, several reviews) only for changes to the network protocol, the simulation's rules, or fairness (what a player can see or do).

## Working style
- A step costs the whole conversation so far, so wait quietly: end your turn while you wait for CI or for the owner, report at milestones, and let a helper read big logs and diffs and report a short summary.
- In a project thread, at a merge point write a state note, put work that lives only in your container on GitHub or the shared folder, and stop; the coordinator starts a fresh thread from the note.
- Reversible work (a branch, a draft pull request) goes ahead without asking; production changes, deletions and messages outside the project wait for the owner's word.
- Documents and comments say what and why, briefly, and link to other documents instead of restating them.
- Every report to the owner is a TL;DR of at most six short lines, then multiple-choice questions asked with the question tool (never a numbered list in the text): the recommended option first, one line of why for each option. Detail goes in a linked document, not in the message.
