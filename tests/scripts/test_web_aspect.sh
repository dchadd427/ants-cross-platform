#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the web page's picture (16:9 by default, Classic 4:3, the selector, the resize / fullscreen bridge, the pointer) in a real browser, against a
# web page that is already running (docs/NETWORK_PORT.md, "The page's picture"). No automated test reaches the logic of web/shell.html: a native test cannot lay out a page.
# This starts a throwaway headless browser (its own profile and port), opens the page at desktop and phone sizes at device ratios 1, 2 and 3 and checks that the canvas the game
# makes has exactly the shape of its picture and fills the page's box, that the box fits the window, the address and the selector, resizing, fullscreen and the pointer.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_aspect.sh [--quick] [--shots DIR]
#
# The page is the web image (`docker build -t ants-beta .`, run it on a port; the game server is not needed); a Chromium-based browser (CHROME=/path/to/it to name one) and python3
# are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the check could not be made because the
# environment is not there: no browser, nothing answering at the address: it says so); 1 a check failed, or the page or the browser broke down while it was checked (a page
# that hangs or crashes is a failure, never a skip). The script's own exit statuses are checked first (--exit-codes of web_aspect_check.py: a closed port is a skip, a page
# that never starts a game is a failure); `--no-exit-codes` leaves that out.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web aspect] the web page's picture in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
ARGS=()
CODES=1
for a in "$@"; do
    if [ "$a" = "--no-exit-codes" ]; then CODES=0; else ARGS+=("$a"); fi
done
if [ "$CODES" -eq 1 ]; then
    python3 "$ROOT/tests/scripts/web_aspect_check.py" --web "$ANTS_WEB_URL" --exit-codes
    codes=$?
    if [ "$codes" -ne 0 ] && [ "$codes" -ne 3 ]; then
        echo "  FAIL: the exit statuses of the check itself are wrong"
        exit 1
    fi
fi
python3 "$ROOT/tests/scripts/web_aspect_check.py" --web "$ANTS_WEB_URL" "${ARGS[@]}"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
