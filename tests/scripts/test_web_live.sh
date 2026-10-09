#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the pages that watch a match that is being played (docs/REPLAYS.md "Watching a replay") in a real browser: the "Live now" box of the list, the game page at
# play.html?live=<id> (the tag, Pause and "Jump to live", the file that grows, the feed that stops and comes back, a match that ends and was kept or was not), the cards of a link that cannot be
# followed, a plain replay as it was, and the REPLAY tag and the fullscreen bar of the player (a finger on the picture never calls the bar).
# It needs no web image, no game and no server: it serves web/ itself with a fake door (GET /live, /live/<id>, /replays) and a stand-in for the game's two controls
# (tests/scripts/web_live_stub.js), so what it shows is the page's own part. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else; without them it says so
# and exits 0. Exit status: 0 every check passed (or the check could not be made); 1 a check failed.
#
#   tests/scripts/test_web_live.sh [--only list|watch|ends|cards|replay|bar] [--shots DIR]
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web live] watching a match that is being played, in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_live_check.py" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
