#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the pages of the match history (docs/REPLAYS.md "The match pages") in a real browser: the list of every match (the cards, the filters and the search with the requests
# that they make, the pager, a busy server, a phone), one match with its stats (a match of people, mode 187, a recording of an older version, one that is gone, one that is not counted yet), the replay in
# the frame of the match page (nothing of the game is asked for before "Show replay", then one frame in embed mode, taken away by "Hide replay" and by the game's own Quit), and the Stats of the replay
# page (a button, the panel, a phone's row, none for a live match, a frame or a match with no record).
# It needs no web image, no game and no server: it serves web/ itself with a fake door (GET /history, /history/<id>, /maps, /live, /replays/<file>) and a stand-in for the game (the one of
# tests/scripts/web_live_check.py), so what it shows is the pages' own part. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else; without them it says so
# and exits 0. Exit status: 0 every check passed (or the check could not be made); 1 a check failed.
#
#   tests/scripts/test_web_matches.sh [--only list|match|frame|stats] [--shots DIR]
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web matches] the pages of the match history, in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_matches_check.py" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
