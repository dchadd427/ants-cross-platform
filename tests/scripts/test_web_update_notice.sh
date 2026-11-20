#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the update notice and the "What's new" window (docs/SITE_NAV.md "The update notice") in a real browser: the strip on every page at 1000 x 700 and on a touch phone, Reload
# and Later, the room's note, the game page (a recording, a live match, a match that runs), and the window (it fits, folds the older releases, opens one at a press, closes with Close and Escape).
# It needs no web image, no game and no server: it serves web/ itself with a made-up data file and the stand-in for the game of tests/scripts/web_live_check.py. A Chromium-based browser (CHROME=/path/to/it
# to name one) and python3 are needed, nothing else; without them it says so and exits 0. The browser is closed at the end. Exit status: 0 every check passed (or the check could not be made); 1 a check failed.
#
#   tests/scripts/test_web_update_notice.sh [--only strip|window|game] [--shots DIR]
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web update notice] the strip and the window, in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_update_notice_browser.py" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
