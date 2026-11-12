#!/usr/bin/env bash
# Decides whether a pull request needs all of CI or only the light checks (the first job of .github/workflows/ci.yml, `changes`). Tested by tests/scripts/test_ci_light.py.
#
#   EVENT=pull_request BASE_REF=<base branch name> BASE_SHA=<commit> HEAD_SHA=<commit> tools/ci_light.sh
#
# The decision is `light` when EVERY file that the pull request changes (git diff against the merge base of its head and its base, renames taken apart so that both names count) is:
#   web/**                      the pages of the site, except the files named below
#   docs/**                     the documents
#   CHANGELOG.md, README.md
#   tests/data/*_mutants.json   the lists of mutants (a file in a folder below tests/data is not one)
#   tests/scripts/test_web_*, tests/scripts/web_*      the tests of the pages and their checks (a file in a folder below tests/scripts is not one), except the files named below
# Anything else makes it `full`: VERSION, CMakeLists.txt, cmake/, src/, include/, tools/, hub/, the Dockerfiles and compose files, .github/, third_party/, Community-Maps/, Original-Ants/,
# run_tests.*, the C++ tests, the other tests of tests/scripts (the macOS job runs most of them and the two shell suites), and every path not listed. Unknown is full. A run that is not a
# pull request (a push to main, a manual run), a commit that is not a full commit id, a history without a merge base with the base, a failing git and a pull request with no
# changed file are full too. So is every pull request whose base branch is main, whatever it changes, and one whose base branch is not named (BASE_REF empty or with a character that a branch
# name of this repository does not use): the full checks are the gate of main, and the light checks are for the branches that feed it.
#
# The base is the pull request's own: the commit that its base branch is at in the run (the workflow passes github.event.pull_request.base.sha), whatever that branch is (but main: see above).
# A pull request that gets another base branch is judged again: the workflow's `pull_request` trigger has the type `edited`, which a change of base is one of, and the run that starts has the new base commit.
#
# Light means: the macOS job and both Windows jobs have nothing to build (the Linux jobs and the web job run as always). Those jobs build the game and run the C++ tests (ctest, on all three
# systems), the tool self-tests, the E2E runner and, on macOS, the two shell suites; the files of the light kinds that THESE read or run are the exceptions in is_light below, so that no light
# file is read by a program or a shell suite of the skipped jobs (a test, tests/scripts/test_ci_light.py, finds the C++ tests and the shell suites that name a file of web/ or docs/ and fails when
# one is not in that list). What macOS also reads is checked by its python tests (the document, version and spec checks read every document, page and list of mutants; the page tests read the
# pages): Linux runs all of the python tests in a light run too, and the next full run (a pull request into main, a push to main) runs them on macOS (docs/WORKFLOW.md, "Light checks for web and document pull requests").
# The decision and every changed file are printed (the paths in a quoted form, so that no line of the log can begin with a workflow command), and `light=true` or `light=false` is appended to
# the file that GITHUB_OUTPUT names. The commit ids come from the environment (never from an argument or a command line built from text of the pull request); the exit status is 0 whatever
# the decision.
set -u

finish() {      # finish true|false REASON
    echo
    if [ "$1" = true ]; then
        echo "decision: LIGHT ($2)"
    else
        echo "decision: full checks ($2)"
    fi
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        echo "light=$1" >> "$GITHUB_OUTPUT"
    fi
    exit 0
}

is_light() {    # is_light PATH: 0 when the path is one of the light kinds and no program of the macOS and Windows jobs reads it
    case "$1" in
        # ---- of the light kinds, and read or run by the jobs that a light pull request skips ----
        # ctest, on all three systems: the alphabet of the room codes (test_start_menu_hosting) and the pictures of the six original maps
        # (test_map_list: they must exist)
        web/front/lobby_rules.js | web/front/preview_*.png) return 1 ;;
        # the server script of the macOS job (tests/scripts/test_ants_server.sh): it reads the two pages, web_fill_check.js runs their code, and web_lobby_server_check.js and
        # web_lobby_handoff_check.js run the lobby page against the real server and the native game, in the fake browser of web_page_harness.js (shapes.js; the scripts of front/ and home/ that the
        # harness loads: the seat-key reader, the codec of the protocol, the name of the person, the chat, the rematch strip, the game modes and types, the map picker, Home and the Games list)
        web/lobby.html | web/shell.html | web/shapes.js | web/rejoin_key.js) return 1 ;;
        web/front/lobby_net.js | web/front/lobby_chat.js | web/front/player_name.js | web/front/rematch.js | web/front/game_modes.js | web/front/game_type.js | web/front/map_picker.js) return 1 ;;
        web/home/home.js | web/home/games.js) return 1 ;;
        tests/scripts/web_fill_check.js | tests/scripts/web_lobby_server_check.js | tests/scripts/web_lobby_handoff_check.js | tests/scripts/web_page_harness.js) return 1 ;;
        # ---- the light kinds ----
        web/* | docs/*) return 0 ;;
        CHANGELOG.md | README.md) return 0 ;;
        tests/data/*/*) return 1 ;;
        tests/data/*_mutants.json) return 0 ;;
        tests/scripts/*/*) return 1 ;;
        tests/scripts/test_web_* | tests/scripts/web_*) return 0 ;;
    esac
    return 1
}

# The characters of the two patterns below are listed, not given as ranges: in the old bash of macOS (3.2) a range follows the collation of the locale, and in a CI run a commit id of
# capitals (A to E) and digits was taken for one.
is_commit_id() {    # a full lower case commit id: 40 hexadecimal digits (so that it can never be taken for an option)
    case "$1" in
        "" | *[!0123456789abcdef]*) return 1 ;;
    esac
    [ "${#1}" -eq 40 ]
}

check_changes() {   # check_changes LABEL MERGE_BASE: prints the files that the head changes against the merge base, and adds them to the totals
    local label="$1" at="$2" path light=0 full=0
    # --ignore-submodules=none: git diff obeys diff.ignoreSubmodules and `ignore = all` in .gitmodules, which would leave a bumped submodule out of the list (a document and a submodule bump came out light)
    git diff --name-only --no-renames --ignore-submodules=none -z "$at" "$head" -- > "$list" 2> /dev/null || finish false "git could not list the changed files"
    echo "$label: ${at:0:12} (merge base) .. ${head:0:12} (head)"
    while IFS= read -r -d '' path; do
        if is_light "$path"; then
            light=$((light + 1))
            [ "$light" -le 150 ] && printf '  %-5s %q\n' light "$path"
        else
            full=$((full + 1))
            [ "$full" -le 150 ] && printf '  %-5s %q\n' FULL "$path"
        fi
    done < "$list"
    [ "$light" -gt 150 ] && echo "  ... and $((light - 150)) more light files (not shown)"
    [ "$full" -gt 150 ] && echo "  ... and $((full - 150)) more files that need the full checks (not shown)"
    echo "changed files: $((light + full)) ($light light, $full that need the full checks)"
    total_light=$((total_light + light))
    total_full=$((total_full + full))
}

if [ "${EVENT:-}" != pull_request ]; then
    finish false "not a pull request: the light checks are for pull requests only"
fi
case "${BASE_REF:-}" in
    main) finish false "the pull request is into main: a pull request into main always gets the full checks" ;;
    "" | *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._/-]*) finish false "the base branch of the pull request is not named in a plain form" ;;
esac
base="${BASE_SHA:-}"
head="${HEAD_SHA:-}"
if ! is_commit_id "$base" || ! is_commit_id "$head"; then
    finish false "the base or the head of the pull request is not a full commit id"
fi
merge_base=$(git merge-base "$base" "$head" 2> /dev/null) || finish false "no merge base of the pull request and its base (is the history complete?)"
list=$(mktemp) || finish false "no temporary file"
trap 'rm -f "$list"' EXIT

total_light=0
total_full=0
check_changes "pull request" "$merge_base"

if [ $((total_light + total_full)) -eq 0 ]; then
    finish false "no changed file was found"
elif [ "$total_full" -gt 0 ]; then
    finish false "$total_full changed file(s) need the full checks"
fi
finish true "every changed file is a page, a document, a list of mutants or a page test that the skipped jobs do not read"
