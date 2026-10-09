#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the front page, which is the lobby, and the way into and out of a game in a real browser (docs/NETWORK_PORT.md, "The front page"): opening "/" makes a ROOM on the
# game server and seats the visitor in it (a first visit: Treasure, You at Green, the other three colours open; a code of six letters and numbers and its link; the line of numbers; Screen with 16:9 the
# default; 23 widths from 320 to 1600 px; the contrast of all text), the lobby with several people (one host and guests, each a browser of its own: the link and its name card, drag and swap, Team 1 and
# Team 2, a bot, the pencil, Remove with its question, a typed code, a reload, a phone, and START, which hands everybody into the real game), START in THIS tab against bots (no new tab; the game's
# arguments; the match starts by itself; the bots' scores rise: their ants move; the Menu link goes back), START alone (a game for one on this computer: the map and the name, no room, no bot; the match runs
# with its one colony), Leave game (the results' button and the quit dialog's Yes go back to the front page, in each shape of the picture and in a room, where the seat is dropped), two people in a match
# (the host's room, a friend through the link, the match starts by itself, the state hashes agree), the old addresses (/?join=..., /?embed=1, /four.html?room=..., /play.html, /?map=...&players=1), an
# address that hosts a room (the test room's panel), and the game page in the front page's look (part game: the loading screen and its failure card, 19 widths from 320 to 1600 px, the header with
# "More", the logo that goes back like Menu, the pairs under the game, the name step, the contrast of every text).
# No automated test reaches the logic of web/lobby.html and web/shell.html with a layout, nginx's routes and the real game: this starts throwaway headless browsers (their own profile and port)
# against the web image and reads what the page and the GAME believe (the address, the game's arguments, `ants_probe`, the pixels of the scores, the game server's own status of the room).
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ ANTS_WS_PORT=4002 tests/scripts/test_web_home.sh [--only front|lobby|play|solo|leave|friend|old|room|game] [--shots DIR] [--bot-seconds N] [--server PATH] [--maps DIR]
#
# The page is the web image (`docker build -t ants-beta .`, run it on a port). Opening "/" makes a room, so the parts front, lobby, play, solo, leave and friend need that site's /ws and /busy to lead to
# a game server: ANTS_WS_PORT is the port they lead to, and the check starts the native `ants_server` of this tree there (build the target ants_server; --server names another program), as
# web_rejoin_check.py does. Without ANTS_WS_PORT those parts are left out (leave still checks the games on this computer; old, room and game do not need a server). A Chromium-based browser (CHROME=/path/to/it
# to name one) and python3 are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the check could not be made because the
# environment is not there); 1 a check failed, or the page or the browser broke down while it was checked (a page that hangs or crashes is a failure, never a skip).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web home] the front page (the lobby) and the way into and out of a game in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
ports=()
if [ -n "$ANTS_WS_PORT" ]; then
    ports=(--ws-port "$ANTS_WS_PORT")
fi
python3 "$ROOT/tests/scripts/web_home_check.py" --web "$ANTS_WEB_URL" "${ports[@]}" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
