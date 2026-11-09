#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: a short random-mutation run of the native replay reader and map reader on files that someone could hand to the page that opens a replay FILE of your own
# (docs/REPLAYS.md "A file of your own"): maps carried in a recording (the chunk tmap, at most 1 MiB) with bytes, sizes and counts changed, damaged heads and orders, chunks cut, doubled and dropped. Every
# checksum and the head's map hash are made again, so the reader and not the checksums decides. Each file goes to `replay_tool info` and `replay_tool verify` with a limit of memory and of time; a crash
# (a signal), a hang or a sanitizer report fails the run, an abort (an exception, an assertion) is listed.
#
#   cmake --build build --target replay_tool && tests/scripts/web_replay_open_fuzz.sh [--runs 300] [--seed 1] [--keep DIR] [--tool PATH/replay_tool]
#
# python3 and a built replay_tool are needed; without them it says so and exits 0. Exit status: 0 nothing crashed or hung (or the check could not be made); 1 something did.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web replay open fuzz] the replay reader and the map reader on files of your own (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_replay_open_fuzz.py" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
