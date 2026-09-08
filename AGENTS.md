# AGENTS.md — Ants macOS Port & Remake

## Project Overview
Native C++17 macOS port and remake of the 1998 classic RTS game *Ants*, featuring SDL2 rendering, 32-channel spatial audio mixer, AudioToolbox MIDI playback, 20Hz deterministic simulation engine, custom 1998 CHD asset decoding, and full multiplayer & AI logic.

## Key Commands
- **Build**: `cmake --build build -j8`
- **Master Test Suite**: `./run_tests.sh` (Runs all asset, simulation, integration, and 506 E2E tests)
- **Integration Tests**: `./build/tests/test_app/test_app_integration`
- **E2E Test Runner**: `./build_e2e/e2e_runner`
- **Run Game**: `./start_game.sh` or `./build/src/ants_app/ants`

## Mandatory Operational Rules

### 1. Frequent Git Commits & Pushes
- **ALWAYS commit and push promptly**: As soon as a task, bug fix, or feature is implemented and verified by automated tests, stage, commit with a clear and descriptive commit message, and immediately push to `origin main`.
- **Never accumulate uncommitted changes**: Do not allow tested, working code to linger unstaged or uncommitted across multiple user requests.

### 2. Code Quality & Standards
- **Compiler Flags**: Code must compile cleanly with `-Wall -Wextra -Werror -Wsign-conversion`. Zero warnings allowed.
- **Test Integrity**: Never break or disable existing tests. All 77 integration tests and 506 E2E tests must maintain a 100% pass rate.
- **Documentation**: Maintain code comments and existing documentation.

### 3. Forbidden Terms
- Strictly **NEVER** write or mention the forbidden word ("M-i-c-r-o-s-o-f-t") anywhere in code, comments, commit messages, or documentation.

### 4. Mandatory Capstone Reverse Engineering & Original Logic Parity
- **Authentic Fidelity Verification**: Always compare and verify our logic, timings, animation sequencing, sprite layering, and behavioral mechanics against the original 1998 executable (`Original-Ants/Ants.exe`) and asset archive (`Original-Ants/ants.chd`) using Capstone reverse engineering disassembly and binary inspection.
- **Match Original Behavior**: The goal is to make the remake as completely faithful to the original 1998 game as possible. Never guess or approximate when the ground-truth logic and constants can be extracted directly via disassembly and reverse engineering.

### 5. Reverse Engineering Documentation Invariant & Living Memory
- **Maintain Up-to-Date RE Docs**: Continually document and maintain all reverse engineering findings, disassembly addresses, opcode traces, and verified asset IDs in `docs/GAME_REVERSE_ENGINEERING.md`.
- **Assume Existing Docs Potentially Outdated**: Always treat pre-existing text in `docs/GAME_REVERSE_ENGINEERING.md` as potentially unverified or outdated until explicitly validated against the original game binary (`Original-Ants/Ants.exe`) and asset archive (`Original-Ants/ants.chd`).
- **Primary Source First**: Always attempt to reverse-engineer directly from the original game binary using Capstone disassembly, using `docs/GAME_REVERSE_ENGINEERING.md` as a living guide and proactively updating it whenever new ground-truth logic is discovered.
