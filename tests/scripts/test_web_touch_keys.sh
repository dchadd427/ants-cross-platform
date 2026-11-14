#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the touch buttons for the game's hotkeys (docs/TOUCH.md "The hotkey buttons") against a real game in a phone-sized browser: All ants, Stop, Home, Next and None
# against what the game says is picked, a tap on a button that is no tap on the map, the row under the picture and the rail beside it (in the page and in fullscreen), turning the phone, the settings panel
# that hides them and the row that turns them off. It drives Playwright's Chromium (TK_ENGINES=chromium,webkit,firefox adds the others when Playwright has them) with a phone's touch and reads the game's probes.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_touch_keys.sh [--shots DIR]
#
# The page is the web image (`tools/web_without_docker.py -- tests/scripts/test_web_touch_keys.sh`, or `docker build -t ants-beta .` and run it on a port; no game server is needed); node and a Playwright
# that it can require (PLAYWRIGHT_MODULE=/path/to/playwright to name one) are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or
# the environment is not there); 1 a check failed.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web touch keys] the hotkey buttons against a real game in a phone-sized browser (opt-in: see the header of this script)"
if ! command -v node > /dev/null 2>&1; then
    echo "  SKIP: node is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
node "$ROOT/tests/scripts/web_touch_keys_browser_check.js" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
