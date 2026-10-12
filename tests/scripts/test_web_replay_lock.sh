#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the mouse in the fullscreen of a replay (docs/REPLAYS.md "Watching a replay") in a real browser with the REAL game: the click on Fullscreen takes the pointer
# lock as the game page's does and fullscreen's end lets it go; the game's cursor moves by the mouse and an edge scrolls the map; the bar stays clickable with the mouse held (Play, the
# timeline, a speed, the Picture window: the game's cursor is the pointer, an arrow is drawn over the bar); "Fullscreen mouse: Free" keeps the free pointer. The page answers for the recording
# itself (no game server is needed); the recording is the match of four computer players that tests/test_app/test_replay_app.cpp makes (ANTS_BOT_MATCH_OUT=FILE writes it).
#
#   ANTS_BOT_MATCH_OUT=/tmp/match.antsrep ANTS_TEST_FILTER=RA8.8 ./build/tests/test_app/test_replay_app
#   ANTS_CAMERA_REPLAY=/tmp/match.antsrep tools/web_without_docker.py -- tests/scripts/test_web_replay_lock.sh [--shots DIR]
#   (with Docker: ANTS_WEB_URL=http://127.0.0.1:19980/ ANTS_CAMERA_REPLAY=... tests/scripts/test_web_replay_lock.sh)
#
# A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, without ANTS_WEB_URL or without a recording it says so and exits 0. Exit status: 0
# every check passed (or the check could not be made because the environment is not there); 1 a check failed, or the page or the browser broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web replay lock] the mouse in the fullscreen of a replay in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_replay_lock_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
