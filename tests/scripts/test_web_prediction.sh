#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the prediction of one's own orders in a REAL browser, against a stack that is already running (docs/NETWORK_PORT.md, "Prediction of one's own orders").
# No automated test reaches the web build's side of it (the page's ?prediction=off, the game's frame function in wasm, the felt delay of an order in a browser's frames, a window that predicts against
# one that does not): a native test plays the application (suite 3.22), this plays a real browser. It starts a throwaway headless browser, plays two matches of a demo room for two players in
# two windows (the first window's player gives its orders with the mouse), with nothing said on the address (the prediction is on by default) and with ?prediction=off, and checks through the
# game's own `ants_probe` that the prediction is on and predicts, then off and predicts nothing, that the click is felt sooner than the confirmed engine applies it, that the game's frame function
# stays cheap, and, through the control interface and the games' own hash reports, that the room runs with both players and that the state hashes of the two windows are equal.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ ANTS_CTL_URL=http://127.0.0.1:4010 ANTS_CTL_SECRET=$(docker exec ants-server cat /results/control-secret) \
#       tests/scripts/test_web_prediction.sh [--orders N] [--rtt MS --jitter MS]
#
# --rtt and --jitter put a delay proxy of the script between the browser and the stack (every chunk of bytes, each way, waits MS / 2 plus a random 0 to JITTER ms; the order of the bytes of a
# connection is kept), to try a link of 100 or 180 ms; without them the stack's own latency, a few milliseconds, is all there is.
#
# The stack is docker-compose.stack.yml (the web image and the game server with demo rooms); a Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing
# else. Without them, or without the three variables, it says so and exits 0.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web prediction] the prediction of one's own orders in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ] || [ -z "$ANTS_CTL_URL" ] || [ -z "$ANTS_CTL_SECRET" ]; then
    echo "  SKIP: set ANTS_WEB_URL, ANTS_CTL_URL and ANTS_CTL_SECRET to a running stack (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_prediction_check.py" --web "$ANTS_WEB_URL" --ctl "$ANTS_CTL_URL" --secret "$ANTS_CTL_SECRET" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made: see above; nothing failed)"
    exit 0
fi
exit "$status"
