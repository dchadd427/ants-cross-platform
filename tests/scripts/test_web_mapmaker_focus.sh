#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: typing in the map maker's boxes in a REAL browser (docs/MAP_EDITOR.md): the name, the note and the maker's name of Submit for approval, the properties, New map and
# Resize map, the search of Open a map and the name of a copy. Every key is a real key of the browser (the DevTools protocol), and after every key the box must be the same node, still in the page, with the
# focus, holding what was typed, with the caret where the typing left it; an input method (a composition) is not cut off. The page is a small one of its own that the script serves on a port of its own
# and that loads the real scripts of web/front/ with stand-ins for the map core and the server, so no built core, no web image and no server are needed.
#
#   tests/scripts/test_web_mapmaker_focus.sh [--browser PATH] [--front DIR]
#
# A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them it says so and exits 0. Exit status: 0 every check passed (or the environment is not there);
# 1 a check failed, or the browser broke down. The broken copies of tests/data/mapmaker_focus_browser_mutants.json must fail it (tools/mutate.py ... --keep-going --jobs 1).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web map maker focus] typing in the map maker's boxes in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_mapmaker_focus_browser.py" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
