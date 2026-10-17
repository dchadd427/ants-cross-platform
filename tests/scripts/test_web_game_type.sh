#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the lobby's Game type control in REAL browsers (docs/PLAY_IN_BROWSER.md, "Game type"): the leader's field and list, the plan that reaches the room, the note and the tag,
# a second browser that joins and sees the read-only line change, the keys, the phone's bottom sheet, the leader's last choice for the next room (and a word that no mode has, and a browser that refuses
# its storage), and a list with rows added as data. --shots DIR saves the pictures that the check looks at.
#
#   tools/web_without_docker.py -- tests/scripts/test_web_game_type.sh [--only leader|keys|phone|remember|rows] [--shots DIR] [--server PATH-TO-ants_server]
#
# web_without_docker.py builds the web image's files, serves them with the real docker/nginx.conf and sets ANTS_WEB_URL (and CHROME). The check starts its own `ants_server` (build/src/ants_server/ants_server)
# and a front door that sends /ws to it. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0.
# Exit status: 0 every check passed (or the environment is not there); 1 a check failed, or a page, the browser or the server broke down.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web game type] the Game type control in real browsers (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL (the web image's page; tools/web_without_docker.py does): see the header of this script"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_game_type_browser.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
