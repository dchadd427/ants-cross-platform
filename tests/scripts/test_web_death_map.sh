#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the map "Where ants died" (docs/REPLAYS.md "The map of deaths") in a real browser: the card of the match page (Heat, Dots, the chips, the switch of the ants that drowned, the
# rings, the zoom, the tooltip, the sum of all the matches on the map, a phone), a match kept in squares on a 512 x 512 map, a match with no map of deaths (one quiet line, never two boxes), and the small card in the
# Stats panel of the replay page (a wide screen and a phone's row). It is the part `deaths` of tests/scripts/web_matches_check.py: it serves web/ itself with a fake door (the same one as
# test_web_matches.sh, plus GET /history/deaths) and a stand-in for the game, so what it shows is the pages' own part. A Chromium-based browser (CHROME=/path/to/it to name one; as root it needs --no-sandbox, so
# CHROME may be a script that adds it) and python3 are needed, nothing else; without them it says so and exits 0. Exit status: 0 every check passed (or the check could not be made); 1 a check failed.
#
#   tests/scripts/test_web_death_map.sh [--shots DIR]      (DIR gets the screenshots: deaths-heat-wide.png, deaths-dots-wide.png, deaths-heat-phone.png ...)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web death map] the card Where ants died, in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_matches_check.py" --only deaths "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
