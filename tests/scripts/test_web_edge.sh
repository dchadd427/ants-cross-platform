#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the pointer of a FULLSCREEN game page in a real browser (docs/PLAY_IN_BROWSER.md, "The pointer in fullscreen") and the margin of a WINDOWED one ("The margin of a
# windowed page": about an inch beyond the game's box still scrolls the map): the black bars of a screen that is not
# the picture's shape (the pointer over a bar is at the picture's nearest edge: the map scrolls, corners included), and the pointer lock that fullscreen takes (the game's own cursor
# moves by the mouse's motion and stays on the picture; the Dock and the menu bar of a Mac have no edge to come up at), its release, the setting "Fullscreen mouse: Locked / Free".
# No automated test reaches the logic of web/shell.html with a layout and a browser's own fullscreen: a native test cannot lay out a page. This starts a throwaway headless browser (its
# own profile and port), starts a match in the game, enters the browser's own fullscreen at 1440 x 900 (and 2560 x 1080, and the page's own fullscreen) and reads what the GAME believes
# through its `ants_probe` (the pointer's position, whether it takes the pointer as gone, the map view's origin, whether a dialog is open, the zoom).
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_edge.sh [--only bars|windowed|margin|pillar|pseudo|lock|release|relock|setting|slow] [--shots DIR]
#
# The page is the web image (`docker build -t ants-beta .`, run it on a port; the game server is not needed); a Chromium-based browser (CHROME=/path/to/it to name one) and python3 are
# needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the check could not be made because the environment is not
# there: no browser, nothing answering at the address: it says so); 1 a check failed, or the page or the browser broke down while it was checked (a page that hangs or crashes is a
# failure, never a skip). What a headless browser cannot show (a real screen's Dock and menu bar, Esc and holding Esc, Safari and Firefox) is listed in the header of web_edge_check.py.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web edge] the pointer of a fullscreen page in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_edge_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
