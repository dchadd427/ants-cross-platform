#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the way back to a running match in REAL browsers (docs/NETWORK_PORT.md, "What the clients do in release B"): two players, each a browser of their own with their own
# storage, in a two-seat room of the real game server behind the real nginx of the web image: a reload (the page takes its seat again, with no "Get ready" dialog; the other screen says that the seat
# is missing and then "... is back: the match goes on in N"; the state hashes of the two games agree), a restart of the server (SIGTERM, the same folder and ports: both pages say "Connection lost.
# Reconnecting...", come back by themselves, the room is restored from its record), the front page's "Rejoin your match (CODE)" after a closed tab, and the front page without a usable entry.
# No automated test reaches this chain: the quick tier runs the front page's Rejoin block and the way back of the library and the application separately, never through a browser and nginx.
#
#   ANTS_WEB_URL=http://127.0.0.1:8080/ ANTS_WS_PORT=4012 tests/scripts/test_web_rejoin.sh [--only reload|restart|rejoin|none] [--shots DIR] [--server PATH-TO-ants_server]
#
# The site is the web image of this tree (`docker build -t ants-beta .`, run on a port) whose nginx passes /ws and /busy to the WebSocket port ANTS_WS_PORT of a game server that is NOT running: the
# check starts its own `ants_server` there (the program of this tree, built to build/src/ants_server/ants_server, with the options of docker-compose.stack.yml and no reconnect option) and stops it at
# the end. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without ANTS_WEB_URL and ANTS_WS_PORT, it says so and exits 0.
# Exit status: 0 every check passed (or the check could not be made because the environment is not there); 1 a check failed, or a page, the browser or the server broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web rejoin] the way back to a running match in real browsers (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ] || [ -z "$ANTS_WS_PORT" ]; then
    echo "  SKIP: set ANTS_WEB_URL (the web image's page) and ANTS_WS_PORT (the port that its /ws leads to): see the header of this script"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_rejoin_check.py" --web "$ANTS_WEB_URL" --ws-port "$ANTS_WS_PORT" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
