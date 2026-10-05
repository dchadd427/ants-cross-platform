#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the front page and the way into and out of a game in a real browser (docs/NETWORK_PORT.md, "The front page"): the lobby at "/" (a first visit: the two
# cards, Treasure, Medium opponents, 2 players on the Host card; the line of numbers; 15 widths from 320 to 1600 px; the contrast of all text), the level buttons' keys, START! with Medium
# bots in THIS tab (no new tab; the game's arguments; the quick help closes with Enter and the match starts at once; the bots'
# scores rise: their ants move), START! with no opponents, the Menu link of the game page (it asks while a match runs and goes back to the front page in the same tab), the old addresses
# (/?join=..., /?embed=1, /four.html?room=..., /play.html with nothing), the host's "Play in this tab", and the game page in the front page's look (part game: the loading screen and its failure card,
# 19 widths from 320 to 1600 px, the header with "More", the logo that goes back like Menu, the pairs under the game, the name step, the contrast of every text).
# No automated test reaches the logic of web/lobby.html and web/shell.html with a layout, nginx's routes and the real game: this starts a throwaway headless browser (its own profile and port)
# against the web image and reads what the page and the GAME believe (the address, the game's arguments, `ants_probe`, the pixels of the scores).
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_home.sh [--only front|levels|play|alone|menu|old|host|game] [--shots DIR] [--bot-seconds N]
#
# The page is the web image (`docker build -t ants-beta .`, run it on a port; the game server is not needed); a Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed,
# nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the check could not be made because the environment is not there); 1 a
# check failed, or the page or the browser broke down while it was checked (a page that hangs or crashes is a failure, never a skip).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web home] the front page and the way into and out of a game in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_home_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
