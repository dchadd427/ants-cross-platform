#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: a match played in a real browser ends with a "Download replay" button that saves the match (docs/REPLAYS.md). The WebAssembly game hands the file of the match to
# the page, the button appears in the bar under the game with the size of the file, a click saves it, and `replay_tool` (build the target replay_tool; without it the saved file is not played) plays
# the file that the browser saved to the same state in which the web game's match ended: the hashes that the web engine wrote are the native engine's too.
# No automated test reaches the three together (the page's script is checked with a fake page by test_web_replay.py, the recording by the application's tests): this starts a throwaway headless browser
# (its own profile, port and download folder) against the web image, plays a short game against an easy bot on the TINY map, quits it and reads what the page and the saved file say.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_replay_game.sh [--shots DIR] [--play-seconds N] [--tool PATH] [--maps DIR]
#
# The page is the web image (`docker build -t ants-beta .`, run it on a port), or the pages that tools/web_without_docker.py builds: tools/web_without_docker.py -- tests/scripts/test_web_replay_game.sh.
# It needs no game server (the game is on this computer). A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed. Without them, or without ANTS_WEB_URL, it says so and
# exits 0. Exit status: 0 every check passed (or the check could not be made because the environment is not there); 1 a check failed, or the page or the browser broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web replay] a match in a real browser ends with a Download replay button that saves the match (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_replay_game_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
