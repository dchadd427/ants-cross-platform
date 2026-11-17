#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the screen that stays on while a recording plays (docs/PLAY_IN_BROWSER.md "The screen stays on") in a real browser: the game page at play.html?replay=<file>
# asks Chromium's navigator.wakeLock for the screen lock while the recording plays, lets it go on pause and at the end, asks again on play and on "Watch again"; a match that is watched live does the
# same; a browser that refuses the lock, or has none, shows nothing and plays on.
# It needs no web image, no game and no server: it serves web/ itself with a fake door and a stand-in for the game's two controls (tests/scripts/web_live_stub.js, the same as test_web_live.sh), so
# what it shows is the page's own part. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else; without them it says so and exits 0.
# Exit status: 0 every check passed (or the check could not be made); 1 a check failed.
#
#   tests/scripts/test_web_wake_lock.sh [--only replay|live|hidden|refused|none]
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web wake lock] the screen that stays on, in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_wake_lock_check.py" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
