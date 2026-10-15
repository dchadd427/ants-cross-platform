#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the countdown before a match (network protocol 21, docs/MATCH_COUNTDOWN.md) in REAL browsers: two players, each a browser of their own, in a two-seat
# room of the real game server behind the real nginx of the web image: the card with its count (30 s), the page's Fullscreen button over it (a real click makes the page fullscreen), a chat line that
# one player types during the count and the other's screen receives through the server, the first turn 30 s after the match began, and a match that then runs with equal state hashes. The quick
# tier runs every piece of it by itself (the host's rule, the message, the application's card, the page's script with fakes); no automated test reaches the chain.
#
#   ANTS_WEB_URL=http://127.0.0.1:8080/ ANTS_WS_PORT=4012 tests/scripts/test_web_countdown.sh [--shots DIR] [--server PATH-TO-ants_server]
#
# The site is the web image of this tree (`docker build -t ants-beta .`, run on a port; or tools/web_without_docker.py, which makes the same pages without Docker) whose nginx passes /ws and /busy to
# the WebSocket port ANTS_WS_PORT of a game server that is NOT running: the check starts its own `ants_server` there (the program of this tree, built to build/src/ants_server/ants_server, with the
# options of docker-compose.stack.yml) and stops it at the end. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without ANTS_WEB_URL
# and ANTS_WS_PORT, it says so and exits 0.
# Exit status: 0 every check passed (or the check could not be made because the environment is not there); 1 a check failed, or a page, the browser or the server broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web countdown] the 30 s countdown with the chat open in real browsers (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ] || [ -z "$ANTS_WS_PORT" ]; then
    echo "  SKIP: set ANTS_WEB_URL (the web image's page) and ANTS_WS_PORT (the port that its /ws leads to): see the header of this script"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_countdown_check.py" --web "$ANTS_WEB_URL" --ws-port "$ANTS_WS_PORT" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
