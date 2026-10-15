#!/usr/bin/env bash
# OPT-IN, not part of ./run_tests.sh: the changelog page's pages in a real browser (docs/WORKFLOW.md, "The changelog"): ten releases to a page (five on a phone), tabs named by the versions they hold,
# the address of a release (#v0.12.30 or #v0-12-30) opening its page and showing the release, a click on a tab and on Older, the focus staying in the bar, the browser's Back and Forward, no horizontal
# scroll on a phone, the old list of versions behind "Jump to a version", and the whole list without JavaScript. It starts a throwaway headless browser (its own profile and port) as a desktop and as a
# phone (Chromium underneath) and reads what the page does; the script's own rules are run by node in the quick tier (tests/scripts/test_changelog_to_html.py), this is the real browser.
#
#   ANTS_WEB_URL=http://127.0.0.1:19980/ tests/scripts/test_web_changelog.sh [--profile desktop|phone|all]
#
# The page is the web image (`docker build -t ants-beta .`, or tools/web_without_docker.py; no game server is needed), or any folder that serves the generated changelog.html with /front/ beside it
# (python3 tools/changelog_to_html.py CHANGELOG.md changelog.html); a Chromium-based browser (CHROME=/path/to/it to name one) and python3 are needed, nothing else. Without them, or without
# ANTS_WEB_URL, it says so and exits 0. Exit status: 0 every check passed (or the environment is not there: it says so); 1 a check failed, or the page or the browser broke down while it was checked.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "[web changelog] the changelog page's pages in a real browser (opt-in: see the header of this script)"
if ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP: python3 is not installed"
    exit 0
fi
if [ -z "$ANTS_WEB_URL" ]; then
    echo "  SKIP: set ANTS_WEB_URL to a running web page (see the header of this script)"
    exit 0
fi
python3 "$ROOT/tests/scripts/web_changelog_check.py" --web "$ANTS_WEB_URL" "$@"
status=$?
if [ "$status" -eq 3 ]; then
    echo "  (the check could not be made because the environment is not there: see above; nothing failed)"
    exit 0
fi
exit "$status"
