#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the web game in a really hidden browser tab, against a stack that is already running (docs/NETWORK_PORT.md, "Known gap").
# No automated test reaches the code of the web build (wasm_ws.cpp's wake-ups, ants_background_pump, the page's timer, sound and visibility logic): a native test plays
# the browser (suite 3.6, N5.32 - N5.44), this plays a real one. It starts a throwaway headless browser, opens the two seats of a demo room in two tabs, covers the first
# tab with the second so that it is hidden for real, and checks through the control interface and the games' own hash reports that the room runs at 20 ticks a second,
# that both state hashes agree, that nobody is dropped and that the hidden page starts no music.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ ANTS_CTL_URL=http://127.0.0.1:4010 ANTS_CTL_SECRET=$(docker exec ants-server cat /results/control-secret) \
#       tests/scripts/test_web_hidden.sh [seconds hidden, default 45]
#
# The stack is docker-compose.stack.yml (the web image and the game server with demo rooms); a Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed,
# nothing else. Without them, or without the three variables, it says so and exits 0.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web hidden] the web game in a really hidden tab (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ] || [ -z "$ANTS_CTL_URL" ] || [ -z "$ANTS_CTL_SECRET" ]; then
    echo "  SKIP: set ANTS_WEB_URL, ANTS_CTL_URL and ANTS_CTL_SECRET to a running stack (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_hidden_check.py" --web "$ANTS_WEB_URL" --ctl "$ANTS_CTL_URL" --secret "$ANTS_CTL_SECRET" --seconds "${1:-45}"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made: see above; nothing failed)"
    exit 0
fi
exit "$status"
