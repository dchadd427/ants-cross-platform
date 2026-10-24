#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: a replay that carries its map (the chunk tmap, docs/REPLAYS.md "The file") in a real browser with the REAL game: the page gives the game the map out of the file and asks
# the site for none; the same file without the chunk, and one whose chunk is damaged, take the old way (the map by its name: the check answers 404) and the card says that the map is not on the server.
# The page answers for the recording itself (no game server is needed); the recording is a match on an original map, which the check makes over (tests/test_app/test_replay_app.cpp writes one when
# ANTS_BOT_MATCH_OUT names a file).
#
#   ANTS_BOT_MATCH_OUT=/tmp/match.antsrep ANTS_TEST_FILTER=RA8.8 ./build/tests/test_app/test_replay_app
#   ANTS_CAMERA_REPLAY=/tmp/match.antsrep tools/web_without_docker.py -- tests/scripts/test_web_replay_map.sh
#   (with Docker: ANTS_WEB_URL=http://127.0.0.1:19980/ ANTS_CAMERA_REPLAY=... tests/scripts/test_web_replay_map.sh)
#
# A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, without ANTS_WEB_URL or without a recording it says so and exits 0. Exit status: 0
# every check passed (or the check could not be made because the environment is not there); 1 a check failed, or the page or the browser broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web replay map] a replay that carries its map in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_replay_map_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
