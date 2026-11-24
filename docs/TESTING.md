# Testing and CI

How the game is tested and what CI runs for every pull request. `./run_tests.sh` runs every suite (decoders, simulation, network, server, bots, the application's screens, view fingerprints, an opaque-box E2E runner and the repository checks) and prints each result and time; CI runs the same. Every suite keeps a 100% pass rate ([`AGENTS.md`](../AGENTS.md), rule 2); the counts of tests and assertions are printed by `./run_tests.sh`, not kept in documents.

Prerequisites and building: [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md).

## Parts

| Section | Page |
|---|---|
| [What the Suites Cover](TESTING_SUITES.md#what-the-suites-cover) | [What the test suites cover](TESTING_SUITES.md) |
| [The python tests (suite 5.2)](TESTING_PYTHON.md#the-python-tests-suite-52) | [The python tests](TESTING_PYTHON.md) |

## The three tiers

Quick (`./run_tests.sh --fast`, after every change), full (CI, for every pull request) and deep (mutation checks, AddressSanitizer, soak and browser runs, for changes to the network, the rules or fairness): what each one is, when it runs and what it costs is in [`WORKFLOW.md`](WORKFLOW.md) ("The three test tiers"). `./run_tests.sh --fast --list` names the suites of the quick tier. The quick tier's test time on a 10-core Mac is about 25 seconds in parallel (100 serial), plus the build.

## Master Test Suite

To run all test suites:

```bash
./run_tests.sh
```

It runs the same suites as CI's Linux and macOS jobs. CI also builds on Windows and builds the web and server images ([Continuous Integration](#continuous-integration)).

On Windows, `run_tests.bat` runs only some of the suites (1, 1.2 - 1.5, 2, 2.1, 2.2, 3 and 4). The other test programs are built by the same project, and `ctest -C Release --output-on-failure` in the build folder runs them all.

## Running Specific Suites

```bash
./run_tests.sh --all      # Everything (the default)
./run_tests.sh --assets   # Asset decoders, movement-table parity with the original program (from a local copy of Ants.exe when there is one; its bytes are always checked against pinned digests) and the asset challengers (suites 1, 1.1 - 1.5)
./run_tests.sh --sim      # Simulation rules, golden action suites, movement differential, command layer, the network (lock-step, room, TCP, LAN, WebSocket), dedicated server, computer players (suites 2.x)
./run_tests.sh --app      # Application integration, render, HUD, status, input, pointer, options, start menu, zoom, touch and rejoin suites, and the script suites (suites 3.x)
./run_tests.sh --e2e      # Opaque-box E2E test runner (runs against its own model, see below)
./run_tests.sh --tools    # The repository checks: version / changelog consistency and the python tests of tests/scripts (suites 5.x)
./run_tests.sh --fast     # The quick tier (above); combines with the others: --sim --fast runs the quick simulation suites only
./run_tests.sh --list     # Name the suites the other options select (with their tier), and run nothing
./run_tests.sh --jobs 4   # At most 4 suites at a time (the default is the number of cores); also -j 4 and --jobs=4
./run_tests.sh --serial   # One suite at a time, the output live (the same as --jobs 1)
./run_tests.sh --asan     # Build in build_asan and run with AddressSanitizer and UBSan
./run_tests.sh --clean    # Remove the build directories and rebuild first (also --rebuild)
./run_tests.sh -v         # The E2E runner also prints a [PASS] line for every test that passes (also --verbose)
./run_tests.sh -h         # The usage (also --help)
```

A test program that reads `ANTS_TEST_FILTER` runs only the cases whose title contains its text, for work on one case (`ANTS_TEST_FILTER="N3.1 Room" ./build/tests/test_net/test_netgame`); `test_ai_worker` also reads `W_ONLY` and `W_SKIP`, comma-separated case numbers (`W_ONLY=AI3.4,AI3.7`). `./run_tests.sh` removes these variables for its suites, so a filter left in the shell never reaches a master run.

A big suite is one binary of several sources, so no file passes the hygiene check's line limit (below): the `main()` and the order of the groups of cases are in the file of the binary's name (`test_server.cpp`), the cases in the files beside it (`test_server_core.cpp`, `test_server_bots.cpp`, ...), and what the parts share (the harness, the worlds and clients, the helpers) in a header of the folder (`server_fixtures.hpp`), all inline or in an anonymous namespace. The same layout holds for `test_lockstep`, `test_netgame`, `test_rejoin`, `test_ctl`, `test_network_app`, `test_lobby`, `test_app_integration`, `test_view_fingerprint`, `test_hud_layout`, `test_start_menu`, `test_start_menu_app`, `test_zoom_view` and `test_wide_hud` (a long `main()` or long function is cut into functions at case boundaries, which `main()` calls in order; the golden table of `test_view_fingerprint` stays with its `main()`). A case moves between the parts of its suite, but a group keeps its place in `main()`: the order of the cases is the order of the output.

## Rules for tests and mutants

These rules bind every new or changed test and mutant spec. They keep the suite on what the game, the pages and the servers do, so that an edit which changes no behavior does not break the build.

**What a test may pin.** A test runs the code or the page and looks at what it does. It reads a source file as text only for: a number or table that two programs must agree on; a security or privacy setting that has nothing to run (an nginx header, a hardening line, the legal sentences); the style contract of the web pages (one test file). It never pins a line of code, a selector, a sentence, a comment, or a count that an edit elsewhere can move. A test that reads text says first which defect it catches and why no run can. A new test must also fail without the code it tests.

**When a mutant is worth having.** One per rule of the simulation, the state hash, the map files, the protocol, the server's and the hub's limits, a page's logic, and a tool that guards the maps or the merge gate, each caught by a test that runs the code. None of a document, a comment, markup, styling or a test, and none that only a text check catches. Its `old` text is the shortest unique piece that occurs once. A mutant that survives is a defect: fix the test, or remove the mutant when it guards no rule. How a spec is written and run: "Mutation checks" in [`WORKFLOW.md`](WORKFLOW.md).

**What is never weakened or removed to make a change pass:** the C++ suites, the end-to-end runner and the shared test code (`tests/e2e`, `tests/common`); golden files, ledgers and recordings; the map library and the tests of the tools that guard it; the tests of the chat hub, above all those of its security and privacy rules; the nginx tests; the legal sentences; numbers that a page and the server share; the download budgets.

## Standalone E2E Test Runner

The E2E suite is a CMake project of its own (`tests/e2e/`) and links none of the game's code (no `ants_sim`, `ants_assets` or `ants_app`). It runs against its own model of the rules (`tests/e2e/e2e_model.hpp`), which was written from the early, paraphrased rules of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) and was never brought in line with the audit of the original program: its melee, knock-back, stun and "guard AI" rules, and its bomb knock-back and fire ricochet, are the model's, not the engine's (the engine's combat rules are section 5.36 of the specification, checked by the golden, integration and differential suites above). A pass here says that the model agrees with the documents, not that the engine matches the 1998 game.

The tests are written opaque-box: they assert only on observable inputs, outputs, states and events (decoded structures, simulation tick outputs, state transitions, audio events, scorecard numbers), never on internals; the expected values come from the specification and never from fitting to what passes; each test sets up its own state, runs deterministically and depends on no other test or on the order. Four tiers:

1. **Feature coverage** (category-partition): the happy paths and the modes of every feature.
2. **Boundaries and corner cases**: coordinates at the edges of the grid, exact tick boundaries (a bridge at 179,950, 180,000 and 180,050 ms; the match clock at 0:01 and 0:00), numeric limits (food theft with a score of 0, 25, 50 and 75; hit points 0 to 10), invalid input refused.
3. **Cross-feature pairs**: knock-back with fire walls and ricochets, bridges with the collapse and drowning, the thief's dive with the alarm and the dropped lunchbox, alliances with scores.
4. **Whole matches** on the shipped maps at 20 Hz to 0:00: the freeze, the winner's and the losers' sound, the four statistics of the scorecard.

```bash
# Build standalone E2E runner
cmake -S tests/e2e -B build_e2e
cmake --build build_e2e

# Run all tests
./build_e2e/e2e_runner --all

# Run specific tiers
./build_e2e/e2e_runner --tier 1   # Tier 1: Feature Coverage
./build_e2e/e2e_runner --tier 2   # Tier 2: Boundary & Corner Cases
./build_e2e/e2e_runner --tier 3   # Tier 3: Cross-Feature Pairwise
./build_e2e/e2e_runner --tier 4   # Tier 4: Real-World Workloads (full matches)
```

`--list` names the registered tests, and `-v` also prints a `[PASS]` line with its time for every test that passes (a test that fails is always printed). The exit code is 0 when every executed test passed and otherwise the number of failures; the output has the test names, their times, the subtotals of the tiers, and the file and line of every failure.

## Continuous Integration

Every pull request, every push to `main`, and every manual run is built and tested by GitHub Actions (`.github/workflows/ci.yml`). A newer push to a branch or pull request cancels the run of the older one.

The five jobs of the table below are the checks that `main` requires, as [`WORKFLOW.md`](WORKFLOW.md#branches-pull-requests-and-releases) describes; CI does not deploy (see [`WORKFLOW.md`](WORKFLOW.md#deploy)). A sixth job, "Image files unchanged by a Dockerfile change", is not required: for a pull request that changes a Dockerfile it builds the images from the base and from this tree and compares what the final images hold and what the link reads (`tools/image_files_diff.sh`). A pull request that adds a `COPY` line (a new page file) or an input of the link changes the images on purpose, and the job reports that as a note and passes (what is still red, and the rest of the rule, is in [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md)). It stands behind none of the five names (not behind "Web (Emscripten, Docker image)" either): a name can say "skipped is fine" only for all the jobs behind it together, and whether a change of the images was meant is for the person who merges to say. It runs when a Dockerfile, `docker/emsdk_warm.cpp` or one of the check's own tools changes.

The jobs run side by side, and the slowest Windows leg decides how long a run takes. On Linux and macOS the time is in the tests, not in the build (the compiler caches make a build a minute), so their work is shared between jobs that run at the same time, the legs (`Linux tests (...)`, `macOS tests (...)`; a Windows compiler has two, which both build everything, as a build takes 13 to 22 minutes there), and each required name is a small job that fails when a leg behind it failed, was cancelled or did not run, or when the first job failed. The first job, "Which checks does this change need?", decides on the light checks ([`WORKFLOW.md`](WORKFLOW.md#light-checks-for-web-and-document-pull-requests)): a pull request of documents, web pages (not the lobby page) and their tests only skips the macOS and Windows legs (their names still report).

| Job | Runner and compiler | SDL2 | What runs |
|---|---|---|---|
| Linux (GCC) | `ubuntu-24.04`, GCC 13, Ninja, Release, `-DANTS_WERROR=ON` | apt: `libsdl2-dev`, `libsdl2-ttf-dev` | seven legs, each of which builds everything: `ctest` in three parts (`test_engine_lookups` alone; `test_ai`, `test_lockstep`, `test_ai_worker` and `test_engine_copy`; every other test: together every registered test program, the same ones as `./run_tests.sh`); the `map_sweep`, `bot_arena`, `replay_tool` and `lvl_tool` self-tests, a sweep of `Community-Maps` checked by `tools/community_maps.py verify`, the E2E tests and the script suites (`start_game.sh --dry-run`, the six parts of the `ants_server` end-to-end script, five at a time); the repository checks (version / changelog consistency) and the python tests of `tests/scripts` in three slices of equal cost (`run_python_tests.py --shard K/3`), all of them together |
| macOS (Apple clang) | `macos-latest` (Apple silicon), Apple clang, Ninja, Release, `-DANTS_WERROR=ON` | SDL2 2.32.10 and SDL2_ttf 2.24.0 built from the release sources (cached) | two legs: `ctest` (all of it); the self-tests, the sweep, the E2E tests, the script suites and the python tests that depend on the system, which are all but the `LINUX_ONLY` groups of `run_python_tests.py` (node checks of the site's pages that start no server and assert no time, most of them the broken-copy checks; Linux runs them): servers, sockets, HTTP, timers, shells, file names and paths and compilers, the things the macOS runners taught the tests, stay |
| Windows (MSVC 2022) | `windows-2022`, MSVC 19.44, Ninja under the Visual C++ environment, Release, `/W4 /WX` (`-DANTS_WERROR=ON`), sccache (its folder cached) | the prebuilt Visual C++ packages that `CMakeLists.txt` downloads | two legs that each build everything: `ctest` for the eight tests of `CTEST_WINDOWS_LONG` (`ci.yml`); the other `ctest` tests, the `map_sweep`, `bot_arena`, `replay_tool` and `lvl_tool` self-tests, the E2E tests |
| Windows (MSVC 2026) | `windows-latest`, MSVC 19.5x, the same flags and cache | the same | the same, for every run |
| Web (Emscripten, Docker image) | `ubuntu-latest`, two legs side by side: the web image (`docker buildx build`, the pinned emsdk, layers cached) and the server and hub images | the Emscripten port | the beta site's image builds; `nginx -t` accepts its configuration; the game's files are in it and the page names the version and the build; the site's routes are right (the front page at `/`, the game page's addresses, the redirect of the old Play online address); the game server's image builds and prints its `--version`; the stack file renders (`docker compose config`) |

The compiler versions come from GitHub's runner images and move with them, apart from the Linux jobs, which stay on `ubuntu-24.04` (GCC 13), so that the compiler under the required check changes only with `ci.yml`: the default image, Ubuntu 26.04, has GCC 15, and the build is warning-free with it too. The macOS job builds SDL2 itself because Homebrew's `sdl2` is now sdl2-compat (the SDL2 interface on top of SDL3), under which eight test programs failed in the job's first run (pointer, window and canvas checks, pixel fingerprints, one crash).

### The zero-warning standard

The project builds without warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor`. `cmake -B build -DANTS_WERROR=ON` turns them into errors, and `./run_tests.sh` configures its build folder that way. MSVC uses `/W4 /permissive-` and, with `ANTS_WERROR`, `/WX` (the warnings C4127, C4244, C4100 and C4189 are off). CI builds every pull request that changes code that way with GCC 13, Apple clang and MSVC 2022 and 2026.

The rule covers the game, the server and every test program, with three exceptions. The E2E runner, a CMake project of its own, builds with `-Wall -Wextra -Wpedantic`. One test file (`tests/test_ai/test_ai_worker.cpp`) turns off GCC 13's `-Wdangling-reference`, which has a known false positive there. GCC's sanitizer build (`./run_tests.sh --asan`) leaves `-Wsign-conversion` off, because GCC 13 reports `(mask >> n) & 1u` under `-fsanitize=shift` as a sign change when there is none (`CMakeLists.txt`, under `ENABLE_ASAN`); every other build, CI's too, has it on.

### What CI does not cover

All tests run headless (SDL's dummy video and audio drivers). CI does not run:

- the opt-in browser checks (`tests/scripts/test_web_*.sh`: a real browser against a running page). A Python check starts its browser through `tests/scripts/browser_process.py` (its own process and group, stopped by its PID on the end, a failure, SIGTERM, SIGHUP and Ctrl-C; tested by `test_browser_process.py`), a node check through Playwright, which stops the browser it started when the program ends or is signalled; nothing stops a browser by its name;
- what a page shows, as opposed to what its markup says: CI has no browser, and no node check computes the style of a page. So "a link to the front page is shown, and first in the Tab order" (every bar, the game page's recording view, the Map maker's two screens) and the first row of the front page's bar are checked only by the browser checks that need Playwright (`web_site_nav_browser.js`, `web_home_topbar_browser.js`, `web_picture_in_settings_check.js`; the last also runs from `test_web_picture_in_settings.py` on a machine that has Playwright, and is skipped in CI). The tests that CI runs read the markup and the fake-DOM code only: a rule that hides a link would pass them;
- the script suites on Windows (they are bash and python);
- a sanitizer job (AddressSanitizer and UBSan: `./run_tests.sh --asan`, locally);
- the 32-bit and ARM Windows builds.

`ctest -N` in a build folder lists the test programs; `ctest -C Release --output-on-failure` runs them on Windows. A failed run keeps `Testing/Temporary` as an artifact.

Two rules keep a CI run short (its test step takes about as long as all the test programs together, divided by the three or four cores of a runner). `ctest --parallel 4` starts the programs with the highest `COST` first (a number of seconds of a CI run, set with `set_tests_properties` beside the `add_test` of the program; in a fresh build folder one without a `COST` counts as 0 and starts after them, in the order of its number, and a folder that has run the tests before gives it the average time that it took), so a new test program that takes a minute or more gets one. And the pump loops of the network tests (a step of game time, then a moment for the kernel to deliver the loopback bytes) pause with `ants_test::short_pause()` (tests/common/ants_test_pause.hpp), not with `sleep_for(300 us)`: on Windows that lasts a whole timer tick, and one test program then took five minutes instead of fifteen seconds. A step is short in real time, so a wait that the sockets decide ends with `ants_test::real_time_tail` (the same header): a late kernel gets seconds of real time with the game clock standing still.

One rule follows from the machines that CI uses: a pinned pixel fingerprint must mask every TrueType text, because SDL's alpha blit rounds differently on x86-64 and on ARM.
