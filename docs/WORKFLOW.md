# Workflow

How a change travels from a commit to a release: which tests run when, how work is pushed, how the version moves, how the changelog is written. The rules for contributors and agents are in [`AGENTS.md`](../AGENTS.md) (rules 1 and 1b: branches, batch pushes and `STATUS.md`; rule 2: zero warnings and all tests passing; rule 6: the web image; rule 7: the version and the build id; rule 11: the tiers); this page is the short, practical version.

## The three test tiers

| Tier | When | What | How | Time |
|---|---|---|---|---|
| **Quick** | after every change, before every commit | the asset, simulation, network-core, bot and application MODEL suites that finish in seconds, plus the repository checks (the version / changelog consistency check and the python tests of `tests/scripts`) | `./run_tests.sh --fast` | about a minute on a 10-core Mac (it also builds what it needs) |
| **Full** | for every push: GitHub Actions runs it on its own machines | everything: all suites of `./run_tests.sh`, the E2E runner, the script suites (start script, server end to end), ctest on Linux (GCC), macOS (Apple clang) and Windows (MSVC 2022 and 2026), the web and the server image | push the branch, watch the run (`.github/workflows/ci.yml`); locally `./run_tests.sh` | CI about 12 minutes; the local full run about 6 minutes |
| **Deep** | only for changes to the network protocol, the simulation's rules or fairness (what a player can see or do), and now and then on a schedule | mutation checks (break the rule on purpose and show that a new test fails: `tools/mutate.py SPEC.json`, below), AddressSanitizer (`./run_tests.sh --asan`), soak runs (`map_sweep`, `bot_arena` tournaments, `test_lockstep`), the opt-in browser checks (`tests/scripts/test_web_aspect.sh`, `test_web_hidden.sh`), an independent review | by hand, as the change needs | as long as it takes |

`./run_tests.sh --fast` is the answer to "did I break something obvious"; it does not run the slow suites (the lock-step soak, the server, the worker bot, the network application, the E2E runner, the script suites): CI does, for every push, and `./run_tests.sh --sim` (or a single test program) runs the suites of one area when a change is in that area. `./run_tests.sh --list` shows what a choice of options runs, and every run prints the time of each suite and the slowest ones, so a suite that grows slow is seen. The other options are `--assets`, `--sim`, `--app`, `--e2e`, `--tools` (the repository checks), `--asan` and `--clean`; `--fast` combines with them (`--sim --fast`).

What a typical change can break decides what is in the quick tier: the rules and their golden cases, the command layer and the state hash, the room, TCP and WebSocket transports, the screens and the pictures (HUD, layout, zoom, fingerprints). What takes minutes and only a network, server or bot change can break stays in the full tier.

A new test must fail without the code it tests (change the code back and see it fail); a change to the network, the rules or fairness also gets the deep tier and an independent review. A change to a screen gets one combined review per batch.

**Mutation checks.** `tools/mutate.py SPEC.json` changes the code on purpose, one mutant at a time (`file`, the `old` text that must occur once, the `new` text, the CMake targets to `build`, the `test` command, an optional `expect`), builds, runs the test and puts the file back, always (also after an error, a timeout or Ctrl-C). A mutant is caught when the test fails and survived when it passes; a table and a JSON report (`SPEC.report.json`) say which. The unmutated tree is built and tested before the first mutant, every `--baseline-every N` mutants (default 10) and at the end: if that fails (a stale object of an earlier mutant, a Mac's make compares file times in whole seconds), the results are not reported. `--keep-going` goes on after a mutant that does not compile or times out, `--timeout` limits one mutant, `--check` only validates the file. The tool's own tests are `tests/scripts/test_mutate_tool.py`.

## Branches, pushes and releases

1. **Work on a branch**, commit often with a clear message. Run `./run_tests.sh --fast` before each commit.
2. **Push the branch.** CI runs for every branch and every pull request; a push to any branch other than `main` deploys nothing. A newer push cancels the older run of the same branch.
3. **Fast-forward `main` only when CI is green on the branch, and in batches** (one to three a day, not one per task): a push to `main` redeploys the beta site, which restarts it and ends the matches that are running. Before `main`, the local build and the web Docker build (`docker build -t ants-beta .`) and the server image (`docker build -f Dockerfile.server -t ants-server .`) have been checked.
4. A batch that changes what players see updates `README.md`, `CHANGELOG.md`, `STATUS.md` and, when the protocol or the rules changed, `docs/NETWORK_PORT.md`, in the same push. `STATUS.md` always carries the date and time of its last update (Pacific time).
5. `main` stays releasable: never push a state in which `./run_tests.sh --fast` or CI fails.

## Version policy

The version is the single line of the file [`VERSION`](../VERSION) (`MAJOR.MINOR.PATCH`). CMake generates the C++ header `ants_app/version.hpp` from it; nothing else writes the version by hand except the three places that name the current release for readers (the top release heading of `CHANGELOG.md`, "current release" in `STATUS.md`, the version line of `README.md`), which `tools/check_version_consistency.py` compares with the file (`./run_tests.sh --fast` and CI run it, and it names the file that disagrees).

- `VERSION` is bumped for a **batch or a milestone**, not for every push: **MINOR** for player-visible features, **PATCH** for a batch that only fixes.
- The **network protocol number** (`include/ants_net/protocol.hpp`) moves separately and by its own rule: whenever a game of the previous number could not play with the new one.
- Every **build** is identified by its build id, the short git commit, whatever the version says: `ants --version`, `ants_server --version`, the server's first log line, the footer of the web page ("Version v0.1.0 - build abc1234") and the head of the changelog pages show it. The corner plate shows the version only. A build outside a git checkout has the id `unknown`, or the one given with `-DANTS_BUILD_ID=<text>`.
- The Docker images take the id from the build argument `ANTS_BUILD_ID` (`docker build --build-arg ANTS_BUILD_ID=$(git rev-parse --short HEAD) .`; a compose file's `build.args`), else from the commit that the clone's `.git/HEAD` and `.git/refs` name (the build context holds those files and nothing else of `.git`, see `.dockerignore`), else from the UTC build time (`YYYYMMDD-HHMM`). The build log says which of the three it used (`docker/resolve_build_id.sh`).
- The test that guards the version checks its **format** (`vMAJOR.MINOR.PATCH`, the three numbers agree with it, the build id is a word), never its value.

## The changelog

[`CHANGELOG.md`](../CHANGELOG.md) is short, one entry per release, newest first, in a fixed template (it is also at the top of that file):

```text
## vX.Y.Z - YYYY-MM-DD - title

**For players:**
- 1 - 6 bullets: what a player or the owner of a server sees or can do now

**Rules / network:** only if the rules of the simulation or the network protocol changed: what, and the protocol number

**Fixes:**
- optional, one line each

**Details:** [commits](link to the commit range), [detailed notes](docs/CHANGELOG_ARCHIVE.md)
```

An entry is 5 - 15 lines, in plain words. No test counts, no mutation or review lists: those belong in commit messages and in the documents. Work that is merged but not yet released is collected under a section headed exactly `## Next` (the same template, no version, no date) above the newest release; `tools/release.py` turns it into the next entry. The detailed history of every release up to v0.1.0 is [`docs/CHANGELOG_ARCHIVE.md`](CHANGELOG_ARCHIVE.md) (frozen). The site builds both files into `changelog.html` (the short page, the default) and `changelog_archive.html` with `tools/changelog_to_html.py`, linked to each other and from the game page.

## Speed: ccache

When [ccache](https://ccache.dev) is installed, CMake uses it for every compile (`-DANTS_USE_CCACHE=OFF` switches it off; it is never used with MSVC, with a launcher you set yourself, as CI does, or for the web build). A fresh clone, another worktree or a clean build folder then compiles from the cache. Set `CCACHE_BASEDIR` (or `base_dir` in `ccache.conf`) to the folder that holds your checkouts so that several of them share hits, and keep the cache size modest on a small disk (`max_size`).
