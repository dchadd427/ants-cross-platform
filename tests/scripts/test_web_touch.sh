#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the touch controls of the game page in a real browser (docs/TOUCH.md): a tap, a hold, a drag, two fingers that pan and pinch the map, a touchcancel, the guards of the
# page (no page scroll or zoom under the game, the context menu and the browser's pinch cancelled over it) and the Fullscreen button, with a Pixel-like phone (Android Chrome: touch, mobile metrics,
# a device ratio of 2.625) and an iPhone-like one (the page's code paths where there is no Fullscreen API and no vibration; Chromium underneath, NOT WebKit). No automated test reaches the page's
# layout with a touch screen: a native test cannot lay out a page. This starts a throwaway headless browser (its own profile and port), starts a match in the game and drives it with the DevTools
# protocol's multi-touch events, reading what the GAME believes through its `ants_probe` (the view's origin, the zoom, the touch model's counters and its slop).
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_touch.sh [--profile pixel|iphone|all] [--only page|slop|scroll|taps|hold|drag|pan|pinch|gates|cancel|fullscreen] [--shots DIR]
#
# The page is the web image (`docker build -t ants-beta .`, run it on a port; the game server is not needed); a Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed,
# nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the check could not be made because the environment is not there: no browser,
# nothing answering at the address: it says so); 1 a check failed, or the page or the browser broke down while it was checked. What a headless browser cannot show (WebKit, a real finger, the
# system's own gestures, the sound's unlock) is listed in the header of web_touch_check.py.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web touch] the touch controls of the game page in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_touch_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
