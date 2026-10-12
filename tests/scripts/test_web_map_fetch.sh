#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the players' maps in REAL browsers (docs/PLAY_IN_BROWSER.md, "Players' maps in the browser"): a room of the game server on a map of the community, two browsers that
# must fetch the file from the site before the match loads (and play the same match), a third that watches the match live (a recording of such a map), the map that is not on the site (a message, no
# match, then the match once it is) and the map whose copy on the server is not the site's (a message, no match).
#
#   tools/web_without_docker.py -- tests/scripts/test_web_map_fetch.sh [--only match|missing|differs] [--server PATH-TO-ants_server]
#
# web_without_docker.py builds the web image's files, serves them with the real docker/nginx.conf and sets ANTS_WEB_URL (and CHROME); the site's map files are in its work folder
# (ANTS_MAP_STORE, default scratch/web_without_docker/fs/srv/community-maps/files of this tree). The check starts its own `ants_server` (build/src/ants_server/ants_server) and a front door that sends
# /ws and the server's public routes to it. A Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without ANTS_WEB_URL, it says so and exits 0.
# Exit status: 0 every check passed (or the check could not be made because the environment is not there); 1 a check failed, or a page, the browser or the server broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web map fetch] the players' maps in real browsers (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL (the web image's page; tools/web_without_docker.py does): see the header of this script"
    exit 0
fi
STORE="${ANTS_MAP_STORE:-$ROOT/scratch/web_without_docker/fs/srv/community-maps/files}"
python3 "$ROOT/tests/scripts/web_map_fetch_check.py" --web "$ANTS_WEB_URL" --store "$STORE" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
