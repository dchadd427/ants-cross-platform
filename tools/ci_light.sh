#!/usr/bin/env bash
# Decides whether a pull request needs all of CI or only the light checks (the first job of .github/workflows/ci.yml, `changes`). Tested by tests/scripts/test_ci_light.py.
#
#   EVENT=pull_request BASE_SHA=<commit> HEAD_SHA=<commit> tools/ci_light.sh
#
# The decision is `light` when EVERY file that the pull request changes (git diff against the merge base of its base and its head, renames taken apart so that both names count) is:
#   web/**                      the pages of the site, except the files named below
#   docs/**                     the documents
#   CHANGELOG.md, README.md
#   tests/data/*_mutants.json   the lists of mutants (a file in a folder below tests/data is not one)
#   tests/scripts/test_web_*, tests/scripts/web_*      the tests of the pages and their checks (a file in a folder below tests/scripts is not one), except the files named below
# Anything else makes it `full`: VERSION, CMakeLists.txt, cmake/, src/, include/, tools/, hub/, the Dockerfiles and compose files, .github/, third_party/, Community-Maps/, Original-Ants/,
# run_tests.*, the C++ tests, the other tests of tests/scripts (the macOS job runs most of them and the two shell suites), and every path not listed. Unknown is full. A run that is not a
# pull request (a push to main or staging, a manual run), a commit that is not a full commit id, a history without a merge base, a failing git and a pull request with no changed file are full too.
#
# Light means: the macOS job and both Windows jobs have nothing to build (the Linux jobs and the web job run as always). Those jobs build the game and run the C++ tests (ctest, on all three
# systems), the tool self-tests, the E2E runner and, on macOS, the two shell suites; the files of the light kinds that THESE read or run are the exceptions in is_light below, so that no light
# file is read by a program or a shell suite of the skipped jobs (a test, tests/scripts/test_ci_light.py, finds the C++ tests and the shell suites that name a file of web/ or docs/ and fails when
# one is not in that list). What macOS also reads is checked by its python tests (the document, version and spec checks read every document, page and list of mutants; the page tests read the
# pages): Linux runs all of the python tests in a light run too, and the next push to main runs them on macOS (docs/WORKFLOW.md, "Light checks for web and document pull requests").
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
        # ctest, on all three systems: the alphabet of the room codes (test_start_menu_hosting) and the twelve names of the lobby page (test_ai_names), and the pictures of the six original maps
        # (test_map_list: they must exist)
        web/front/lobby_rules.js | web/front/preview_*.png) return 1 ;;
        # the server script of the macOS job (tests/scripts/test_ants_server.sh): it reads the two pages, web_fill_check.js runs their code, and web_lobby_server_check.js and
        # web_lobby_handoff_check.js run the lobby page against the real server and the native game, in the fake browser of web_page_harness.js (shapes.js; the scripts of front/ and home/ that the
        # harness loads: the codec of the protocol, the chat, the rematch strip, the game modes and types, the map picker, Home and the Games list)
        web/lobby.html | web/shell.html | web/shapes.js) return 1 ;;
        web/front/lobby_net.js | web/front/lobby_chat.js | web/front/rematch.js | web/front/game_modes.js | web/front/game_type.js | web/front/map_picker.js) return 1 ;;
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

is_commit_id() {    # a full lower case commit id: 40 hexadecimal digits (so that it can never be taken for an option)
    case "$1" in
        "" | *[!0-9a-f]*) return 1 ;;
    esac
    [ "${#1}" -eq 40 ]
}

if [ "${EVENT:-}" != pull_request ]; then
    finish false "not a pull request: the light checks are for pull requests only"
fi
base="${BASE_SHA:-}"
head="${HEAD_SHA:-}"
if ! is_commit_id "$base" || ! is_commit_id "$head"; then
    finish false "the base or the head of the pull request is not a full commit id"
fi
merge_base=$(git merge-base "$base" "$head" 2> /dev/null) || finish false "no merge base of the pull request and its base (is the history complete?)"
list=$(mktemp) || finish false "no temporary file"
trap 'rm -f "$list"' EXIT
# --ignore-submodules=none: git diff obeys diff.ignoreSubmodules and `ignore = all` in .gitmodules, which would leave a bumped submodule out of the list (a document and a submodule bump came out light)
git diff --name-only --no-renames --ignore-submodules=none -z "$merge_base" "$head" -- > "$list" 2> /dev/null || finish false "git could not list the changed files"

echo "pull request: ${merge_base:0:12} (merge base) .. ${head:0:12} (head)"
light=0
full=0
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

if [ $((light + full)) -eq 0 ]; then
    finish false "no changed file was found"
elif [ "$full" -gt 0 ]; then
    finish false "$full changed file(s) need the full checks"
fi
finish true "every changed file is a page, a document, a list of mutants or a page test that the skipped jobs do not read"
