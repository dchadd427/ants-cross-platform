#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the settings menu (Esc, or a Settings button; docs/GAME_MENU.md) in a real browser with the REAL game: Esc's rule, the panel in the stage and over
# the whole window, the keys that the game does not take, the focus, the settings that are the game's and remembered, the picture's shape, fullscreen with the panel inside it and the pointer lock let go and asked
# for again, a dialog that closes it, Leave game and its question (a forfeit, the end of a game for one), a phone at 390 x 844 and on its side, a recording, and the frame of a match page.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_game_menu.sh [--profile desktop|phone|replay] [--replay FILE] [--shots DIR]
#   tools/web_without_docker.py -- tests/scripts/test_web_game_menu.sh [--shots DIR]
#
# The page is the web image (`docker build -t ants-beta .`, or tools/web_without_docker.py); the game server is not needed. The parts that watch a recording need one (--replay FILE, or ANTS_CAMERA_REPLAY, as for
# tests/scripts/test_web_camera.sh: ANTS_BOT_MATCH_OUT=/tmp/match.antsrep ANTS_TEST_FILTER=RA8.8 ./build/tests/test_app/test_replay_app writes one). A Chromium-based browser (CHROME=/path/to/it to name one) and python3
# are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the check could not be made because the environment is not there); 1 a check
# failed, or the page or the browser broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web game menu] the settings menu in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_game_menu_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
