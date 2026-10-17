#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the map maker's page (docs/MAP_EDITOR.md) in a REAL browser: it starts with its core, makes a Classic and an Extended map (512 x 512 at the most), opens a
# file from the disk and refuses one that is no map, draws only what is on the screen, pans and zooms with a mouse, a wheel, two fingers and the keyboard, shows the checks of each kind, hands
# the map over as a download, refuses a map core of another interface version, and fits a phone held upright (390 x 844), a phone held sideways (844 x 390) and a desktop (1280 x 800).
# --shots DIR saves the pictures that the check looks at.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_mapmaker.sh [--only desktop|phone|older] [--shots DIR]
#
# The page is the web image's (tools/web_without_docker.py builds and serves it; or any server that has web/mapmaker.html, web/front/, web/lvl_core_api.js and the built lvl_core.js and
# lvl_core.wasm in one folder); the game server is not needed. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without
# ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the environment is not there); 1 a check failed, or the page or the browser broke down.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web map maker] the map maker's page in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to the web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_mapmaker_browser.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
