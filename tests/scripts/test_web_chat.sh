#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: a phone's keyboard for the match's chat box in a real browser (docs/TOUCH.md, "The chat box and the on-screen keyboard"): a tap on the box focuses the page's
# invisible text field, what is typed is the game's chat line, Enter sends, Esc and a tap on the map close it, a dialog closes it, and a desktop's mouse and keyboard are as they were. It
# starts a throwaway headless browser (its own profile and port) as a Pixel-like phone, an iPhone-like one (Chromium underneath, NOT WebKit) and a desktop, starts a match in the game and
# drives it with the DevTools protocol's touch and key events, reading what the GAME holds through its exports (ants_chat_probe, ants_probe).
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_chat.sh [--profile pixel|iphone|desktop|all]
#
# The page is the web image (`docker build -t ants-beta .`, or tools/web_without_docker.py; the game server is not needed); a Chromium-based browser (CHROME=/path/to/it to name one) and python3
# are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the environment is not there: it says so); 1 a check failed, or
# the page or the browser broke down while it was checked. What a headless browser cannot show (the operating system's keyboard itself) is listed in the header of web_chat_check.py.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web chat] a phone's keyboard for the chat box in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_chat_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
