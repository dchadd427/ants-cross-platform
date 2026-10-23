#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the Map Maker's "Start from a game map" (docs/MAP_EDITOR.md) in a REAL browser: the six original maps come from the site byte for byte, TREASURE opens as a copy
# under a free name with the original's size, and a map that is not there (404), a site that answers badly (500) and a request that fails each get their own words.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_mapmaker_open.sh
#
# The page is the web image's (tools/web_without_docker.py builds and serves it); the game server is not needed. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed,
# nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the environment is not there); 1 a check failed, or the page or the browser broke down.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web map maker open] Start from a game map in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to the web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_mapmaker_open_browser.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
