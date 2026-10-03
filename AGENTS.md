# AGENTS.md — Ants macOS Port & Remake

## Project Overview
Native C++17 macOS port and remake of the 1998 classic RTS game *Ants*, featuring SDL2 rendering, 32-channel spatial audio mixer, AudioToolbox MIDI playback, 20Hz deterministic simulation engine, custom 1998 CHD asset decoding, and full multiplayer & AI logic.

## Key Commands
- **Build Local**: `cmake --build build -j8`
- **Build Web (Docker / beta.playants.org)**: `wsl docker build -t ants-beta .`
- **Quick Tests (every change)**: `./run_tests.sh --fast` (the quick tier, about a minute; see rule 11)
- **Master Test Suite**: `./run_tests.sh` (Runs all asset, simulation, integration, E2E and repository-check suites; prints each suite's time and the totals)
- **Integration Tests**: `./build/tests/test_app/test_app_integration`
- **E2E Test Runner**: `./build_e2e/e2e_runner`
- **Run Game**: `./start_game.sh` or `./build/src/ants_app/ants`

## Mandatory Operational Rules

### 1. Frequent Git Commits on a Branch, Batch Pushes to Main + Dual Local & Web Docker Builds
- **ALWAYS commit promptly, on a branch**: As soon as a task, bug fix, or feature is implemented and verified by the quick tier (`./run_tests.sh --fast`), stage and commit with a clear and descriptive commit message on a branch (not on `main`), and push the branch to `origin` so that GitHub Actions (CI) runs on it. A push to a branch deploys nothing.
- **Never accumulate uncommitted changes**: Do not allow tested, working code to linger unstaged or uncommitted across multiple user requests.
- **Push to `origin main` in batches (one to three a day), not per task**: A push to `main` redeploys `beta.playants.org` and ends the matches that are running. When a batch is complete, its checks have passed (the full `./run_tests.sh` once, at 100% pass rate) and CI is green on the batch's branch, fast-forward `main` to that tested commit and push.
- **Mandatory Dual Local + Docker Web Builds**: Before a batch goes to `main`, ALWAYS build the final tree for BOTH: (1) Local machine (`cmake --build build -j8`), and (2) Web (`beta.playants.org`) Docker image (`wsl docker build -t ants-beta .`; the game server's image `Dockerfile.server` with it). Verify that both build targets succeed and all test suites maintain 100% pass rate before pushing to `main`.

### 1b. STATUS.md Is Always Current
- **`STATUS.md`** (repo root) is the owner's one-glance view: In progress, Next, Recently done. Update it with every batch push (in the batch's own commits) and whenever work starts or stops (a task starts, finishes or changes), with the date and time of the update in Pacific time at the top (e.g. `2026-10-02 08:26 PDT`, from `TZ=America/Los_Angeles date '+%Y-%m-%d %H:%M %Z'`). Its "current release" must name the release in the file `VERSION` (`./run_tests.sh --fast` and CI check it). Keep it very brief: one line per item, no details (those go to CHANGELOG.md and `implementation_plan.md`), nothing private.

### 2. Code Quality & Standards
- **Compiler Flags**: Code must compile cleanly with `-Wall -Wextra -Werror -Wsign-conversion`. Zero warnings allowed.
- **Test Integrity**: Never break or disable existing tests. All tests (every suite of `./run_tests.sh`, every job of CI) must maintain a 100% pass rate. Counts of tests and assertions are printed by `./run_tests.sh` and are not kept in documents.
- **Documentation**: Maintain code comments and existing documentation.

### 4. Mandatory Capstone Reverse Engineering & Original Logic Parity
- **Dual Verification with Capstone & Ants.exe.c**: Always compare and verify our logic, timings, animation sequencing, sprite layering, and behavioral mechanics against the original 1998 executable (`Original-Ants/Ants.exe`), the complete C decompilation (`docs/legacy/Ants.exe.c`), and asset archive (`Original-Ants/ants.chd`).
- **Synergy of Capstone Disassembly + Decompiled C**: Use Capstone disassembly (e.g. `tools/analyze_binary.py`) for exact assembly instruction sequences, opcode timings, register allocations, and jump tables, coupled with `docs/legacy/Ants.exe.c` for high-level C logic flow, variable naming, struct definitions, state machines, sound triggers, and exact numeric constants.
- **Match Original Behavior**: The goal is to make the remake as completely faithful to the original 1998 game as possible. Never guess or approximate when the ground-truth logic and constants can be extracted directly via disassembly and reverse engineering.
- **Local Copies, Never Committed**: `Original-Ants/Ants.exe` and `docs/legacy/Ants.exe.c` are local copies that are NOT in the public repository (`.gitignore` lists them, with the other local files of an installation of the original: cnc-ddraw, its shaders, `chat.txt`). Keep your own copy at those paths and never commit them, nor any other byte copy or dump of the original program (extracted tables, string dumps, raw disassembly listings). From the original game the repository holds only `Original-Ants/ants.chd`, `Original-Ants/Maps/*.LVL` and the music (`*.mp3`, `*.MID`). The tests that compare the remake with the program's static tables run without it (pinned SHA-256 digests in `tests/common/original_program_bytes.hpp`); change a pinned digest only with the program present.

### 5. Reverse Engineering Documentation Invariant & Living Memory
- **Maintain Up-to-Date RE Docs**: Continually document and maintain all reverse engineering findings, disassembly addresses, opcode traces, and verified asset IDs in `docs/GAME_REVERSE_ENGINEERING.md`.
- **Assume Existing Docs Potentially Outdated**: Always treat pre-existing text in `docs/GAME_REVERSE_ENGINEERING.md` as potentially unverified or outdated until explicitly validated against the original game binary (`Original-Ants/Ants.exe`), `docs/legacy/Ants.exe.c` (both local copies that are not in the public repository and must never be committed, see rule 4), and asset archive (`Original-Ants/ants.chd`).
- **Primary Source First**: Always cross-reference directly with the primary sources: inspect the C decompilation (`docs/legacy/Ants.exe.c`) and reverse-engineer from the original game binary using Capstone disassembly (your local copies of both, see rule 4), using `docs/GAME_REVERSE_ENGINEERING.md` as a living guide and proactively updating it whenever new ground-truth logic is discovered.

### 6. WebAssembly / Beta Deployment Synchronization & Cache Invariant
- **Synchronize Web Builds**: Ensure any game logic, asset, or simulation engine changes remain continuously synchronized with the WebAssembly / Emscripten build and deployment pipeline (`docker/nginx.conf`, `web/`).
- **Mandatory Docker Web Build**: Before a batch is pushed to `main`, always execute `wsl docker build -t ants-beta .` to ensure the beta container for `beta.playants.org` builds and packages cleanly alongside local native builds.
- **Cache Invalidation**: Web builds served on beta (e.g. `beta.playants.org`) must enforce strict revalidation headers (`Cache-Control: "no-cache, must-revalidate"`) for `.wasm`, `.data`, `.html`, `.js`, and `.css` so clients immediately execute updated game binaries without stale browser caching.

### 7. Version Tracking & Build Identification Invariant
- **One source, a milestone**: The version is the single line of the file `VERSION` (`MAJOR.MINOR.PATCH`, starting at `0.0.1`); CMake generates `ants_app/version.hpp` from it, and nothing else writes it by hand except the three places that name the current release for readers (the top release heading of `CHANGELOG.md`, "current release" in `STATUS.md`, the version line of `README.md`), which `./run_tests.sh --fast` and CI compare with it. It is bumped for a batch or a milestone, not for every push: MINOR for player-visible features, PATCH for fix-only batches. The network protocol number moves separately, by its own rule.
- **Every build is identified**: A build id (the short git commit; the Docker images take a build argument, else the commit of the clone's git files, else the UTC build time) identifies every build: `--version`, the server's start-up line, the web page's footer and the changelog pages show it.
- **On-Screen Version Meter**: The current version string must always be rendered on screen adjacent to the FPS counter / sparkline in the bottom-right corner of the viewport across all game states.

### 8. Bots: Virtual Clients Only Invariant (amended 2026-09-30 at the owner's request; it used to forbid bot AI)
- **Bot players are allowed as virtual clients, in the `ants_ai` library only.** A bot uses exactly the public command interface of a human (a `Command` through the `CommandSink` of a local game or the sequencer of a room), reads the world through a read-only view, and **never changes the simulation, the original screens, the lock-step rules of the network protocol or any golden state hash.** No bot code may be needed to start, host or join a normal game.
- **Off by default, always visible**: a bot exists only when asked for (command line, room specification); a bot seat is shown as a bot: a room always names it "Bot (Level)" (a person cannot take such a name; in a local game the local player may rename a seat with `--team-name`); nothing pretends that a bot is a person.
- **Fair**: a bot knows nothing that a human of its team could not know (with Fog of War on it may only use its own team's view; until that view exists a bot together with fog is refused, never silently allowed to see through it) and has a command budget (commands per second, reaction delay) per difficulty level; no extra commands, no rule bending.
- **The 1:1 core stays pure**: the original's mechanics, screens and keys are unchanged; the only computer behaviour inside the simulation is the original's own: the Combat Ant's auto-engage reflex (an idle Combat Ant punches the first enemy within three tiles and returns to where it stood; the remake's guard post AI was removed in v0.0.33, there is no guard post). Bots are tested headless against each other (tournaments) and a bot test never replaces, disables or weakens an existing test.

### 9. Implementation Plan & Explicit User Approval Invariant
- **Log in Implementation Plan**: Whenever the user provides tasks, features, or bug reports, thoroughly document all findings, reverse engineering references, planned code modifications, and test verifications into `implementation_plan.md`.
- **Wait for Explicit Approval**: Always present the plan and explicitly ask the user for permission to proceed. STOP and WAIT for the user to say to proceed, go, or continue before making changes or executing execution steps.

### 10. Open Source Information Security & Leak Prevention Invariant
- **Zero Information Leakage to GitHub**: This repository is a public open-source project. Strictly NEVER write, commit, or leak any private credentials, API keys, personal access tokens, secret URLs, personal identifiable information (PII), proprietary source code, internal hostnames, or local system paths to GitHub in any code, comments, commit messages, or documentation.

### 11. Process Tiers: Which Checks Run When
- **Quick tier, per change**: `./run_tests.sh --fast` (about a minute: the sim, network-core and application model suites and the repository checks) before every commit. Agents run focused tests and the quick tier while working, not the full gates.
- **Full tier, per push and per batch**: CI's full matrix (Windows, macOS, Linux, the web and server images) for every push of a branch, and one full local `./run_tests.sh` plus the local and Docker builds per batch before `main` (rule 1).
- **Deep tier, only where it pays**: Mutation-proven tests (a new test must fail without the code it tests; show it), AddressSanitizer, soak runs, browser checks and an independent review are required for changes to the network protocol, the simulation's rules and fairness (what a player can see or do), and may run on a schedule. A change to a screen gets one combined quick review per batch.
- `docs/WORKFLOW.md` describes the tiers, the branch and release procedure, the version policy and the changelog template.
