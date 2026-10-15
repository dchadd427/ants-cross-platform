#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the chat of the front page in REAL browsers (docs/PLAY_IN_BROWSER.md, "Chat in the lobby"): three people in one room (the lines they type, the colours, a name or a
# line that is markup, what the game cannot show, the pace, a line of 100 characters, the toast, START), and a phone (the box after the colours, a field that a finger reaches, the START bar that
# does not cover it when the keyboard is up, the same sideways). --shots DIR saves the pictures that the check looks at.
#
#   tools/web_without_docker.py -- tests/scripts/test_web_lobby_chat.sh [--only desktop|phone] [--shots DIR] [--server PATH-TO-ants_server]
#
# web_without_docker.py builds the web image's files, serves them with the real docker/nginx.conf and sets ANTS_WEB_URL (and CHROME). The check starts its own `ants_server` (build/src/ants_server/ants_server)
# and a front door that sends /ws to it. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0.
# Exit status: 0 every check passed (or the environment is not there); 1 a check failed, or a page, the browser or the server broke down.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web lobby chat] the chat of the front page in real browsers (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL (the web image's page; tools/web_without_docker.py does): see the header of this script"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_lobby_chat_browser.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
