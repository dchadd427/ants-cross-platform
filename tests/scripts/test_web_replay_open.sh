#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: opening a replay FILE of your own on the website (docs/REPLAYS.md "A file of your own") in a real browser (Chromium through Playwright): the strip of the Matches page
# (the chooser, a dropped file, a refused file and its card, the page with the site down, a phone), the game page at play.html?local=<id> (the bytes that the game gets are the file's, a reload, a record that is
# gone or too old, the header, no Copy link, Stats or Download) and every card (an older or newer version, a map that the site has not, a file that cannot be read, odd names).
# It needs no web image, no game and no server: it serves web/ itself behind a stand-in for the site (GET /history, /live, /maps; POST and PUT are refused and every request is counted: none carries a body)
# and puts a stand-in for the game's program (tests/scripts/web_live_stub.js) in the place of index.js, so what it shows is the pages' own part. With ANTS_WEB_URL (a running site: the web image, or
# python3 tools/web_without_docker.py, which sets it) the part REAL also runs the same pages with the REAL game against that site: the game plays a file, says an older or newer version, reads a map that the file
# carries, and meets nine damaged maps. It needs node and Playwright (npm install -g playwright; the browsers of its own, or CHROME=/path/to/a/Chromium); without them it says so and exits 0. --shots DIR
# draws the pictures (built_*.png) of the approved screens. Exit status: 0 every check passed (or the check could not be made); 1 a check failed.
#
#   tests/scripts/test_web_replay_open.sh [--only strip|hand|cards|real] [--shots DIR] [--web URL]
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web replay open] a replay file of your own, in a real browser (opt-in: see the header of this script)"
if ! command -v node > /dev/null 2>&1; then
    echo "  SKIP: node is not installed"
    exit 0
fi
node "$ROOT/tests/scripts/web_replay_open_browser.js" "$@"
exit $?
