# AGENTS.md — Ants macOS Port & Remake

## Project Overview
Native C++17 macOS port and remake of the 1998 classic RTS game *Ants*, featuring SDL2 rendering, 32-channel spatial audio mixer, AudioToolbox MIDI playback, 20Hz deterministic simulation engine, custom 1998 CHD asset decoding, and full multiplayer & AI logic.

## Key Commands
- **Build Local**: `cmake --build build -j8`
- **Build Web (Docker / beta.playants.org)**: `wsl docker build -t ants-beta .`
- **Master Test Suite**: `./run_tests.sh` (Runs all asset, simulation, integration, and 506 E2E tests)
- **Integration Tests**: `./build/tests/test_app/test_app_integration`
- **E2E Test Runner**: `./build_e2e/e2e_runner`
- **Run Game**: `./start_game.sh` or `./build/src/ants_app/ants`

## Mandatory Operational Rules

### 1. Frequent Git Commits & Pushes + Dual Local & Web Docker Builds
- **ALWAYS commit and push promptly**: As soon as a task, bug fix, or feature is implemented and verified by automated tests, stage, commit with a clear and descriptive commit message, and immediately push to `origin main`.
- **Never accumulate uncommitted changes**: Do not allow tested, working code to linger unstaged or uncommitted across multiple user requests.
- **Mandatory Dual Local + Docker Web Builds**: Whenever making changes, committing, and pushing, ALWAYS build a new version for BOTH: (1) Local machine (`cmake --build build -j8`), and (2) Web (`beta.playants.org`) Docker image (`wsl docker build -t ants-beta .`). Verify that both build targets succeed and all test suites maintain 100% pass rate before committing and pushing.

### 1b. STATUS.md Is Always Current
- **`STATUS.md`** (repo root) is the owner's one-glance view: In progress, Next, Recently done. Update it whenever a task starts, finishes, changes or a release ships (with the release commit). Keep it very brief: one line per item, no details (those go to CHANGELOG.md and `implementation_plan.md`), nothing private.

### 2. Code Quality & Standards
- **Compiler Flags**: Code must compile cleanly with `-Wall -Wextra -Werror -Wsign-conversion`. Zero warnings allowed.
- **Test Integrity**: Never break or disable existing tests. All 77 integration tests and 506 E2E tests must maintain a 100% pass rate.
- **Documentation**: Maintain code comments and existing documentation.

### 4. Mandatory Capstone Reverse Engineering & Original Logic Parity
- **Dual Verification with Capstone & Ants.exe.c**: Always compare and verify our logic, timings, animation sequencing, sprite layering, and behavioral mechanics against the original 1998 executable (`Original-Ants/Ants.exe`), the complete C decompilation (`docs/legacy/Ants.exe.c`), and asset archive (`Original-Ants/ants.chd`).
- **Synergy of Capstone Disassembly + Decompiled C**: Use Capstone disassembly (e.g. `tools/analyze_binary.py`) for exact assembly instruction sequences, opcode timings, register allocations, and jump tables, coupled with `docs/legacy/Ants.exe.c` for high-level C logic flow, variable naming, struct definitions, state machines, sound triggers, and exact numeric constants.
- **Match Original Behavior**: The goal is to make the remake as completely faithful to the original 1998 game as possible. Never guess or approximate when the ground-truth logic and constants can be extracted directly via disassembly and reverse engineering.

### 5. Reverse Engineering Documentation Invariant & Living Memory
- **Maintain Up-to-Date RE Docs**: Continually document and maintain all reverse engineering findings, disassembly addresses, opcode traces, and verified asset IDs in `docs/GAME_REVERSE_ENGINEERING.md`.
- **Assume Existing Docs Potentially Outdated**: Always treat pre-existing text in `docs/GAME_REVERSE_ENGINEERING.md` as potentially unverified or outdated until explicitly validated against the original game binary (`Original-Ants/Ants.exe`), `docs/legacy/Ants.exe.c`, and asset archive (`Original-Ants/ants.chd`).
- **Primary Source First**: Always cross-reference directly with the primary sources: inspect the C decompilation (`docs/legacy/Ants.exe.c`) and reverse-engineer from the original game binary using Capstone disassembly, using `docs/GAME_REVERSE_ENGINEERING.md` as a living guide and proactively updating it whenever new ground-truth logic is discovered.

### 6. WebAssembly / Beta Deployment Synchronization & Cache Invariant
- **Synchronize Web Builds**: Ensure any game logic, asset, or simulation engine changes remain continuously synchronized with the WebAssembly / Emscripten build and deployment pipeline (`docker/nginx.conf`, `web/`).
- **Mandatory Docker Web Build**: Before committing and pushing changes, always execute `wsl docker build -t ants-beta .` to ensure the beta container for `beta.playants.org` builds and packages cleanly alongside local native builds.
- **Cache Invalidation**: Web builds served on beta (e.g. `beta.playants.org`) must enforce strict revalidation headers (`Cache-Control: "no-cache, must-revalidate"`) for `.wasm`, `.data`, `.html`, `.js`, and `.css` so clients immediately execute updated game binaries without stale browser caching.

### 7. Version Tracking & Continuous Increment Invariant
- **Continuous Version Tracking**: Starting at `v0.0.1` (pre-release), track and increment semantic version numbers in `include/ants_app/version.hpp` on every release/feature cycle.
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



