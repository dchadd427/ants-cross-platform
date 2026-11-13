"""The size numbers of documents, code files and web downloads, in one place. Data only: tools/check_hygiene.py and the tests that need a limit import it.

Documents and code files have a soft policy. A base size, and three fractions of it:
  target  (75%)   what a file aims at; only `check_hygiene.py --list` shows the files over it, and documents get a warning from here
  split   (90%)   a warning on the pull request: split the file by topic now
  cap     (125%)  the safety cap: the only fraction that fails the check
A document is measured in bytes, a code file in lines. Nothing is a ledger: no file has a number of its own, and a file is never trimmed or joined to fit (AGENTS.md, rule 10).
"""

TARGET_PERCENT = 75
SPLIT_PERCENT = 90
CAP_PERCENT = 125

# why 100 KiB: a page that a person reads in one sitting and an agent reads in one file read (about 25,000 tokens of this prose)
DOC_BASE_BYTES = 100 * 1024
# why 1,500 and 2,500: a file that one person can hold in mind; a test file is longer because it is a list of small cases
PRODUCTION_BASE_LINES = 1500
TEST_BASE_LINES = 2500
# code files warn from the split line only (from the target, about 50 files would be listed); documents warn from the target
CODE_WARNS_FROM_PERCENT = SPLIT_PERCENT

# GitHub shows at most 10 warning annotations of one step: the hygiene check prints at most this many (the worst files first)
ANNOTATION_LIMIT = 10

# why 2,000: agent readers may cut a line longer than this; a long line is a warning (reflow it in a change that moves no text)
DOC_LINE_WARN_CHARACTERS = 2000

# Files over the safety cap that wait for their split. Names only: no number. They warn instead of failing; a name fails when its file is gone or no longer over the cap,
# and each split removes its name. The set and the code that reads it are deleted when it is empty.
SPLIT_PENDING = frozenset({
    "docs/GAME_REVERSE_ENGINEERING.md",
    "docs/NETWORK_PORT.md",
    "docs/BOTS.md",
    "web/shell.html",
    "tools/map_sweep.cpp",
})

# Web byte budgets (tests/scripts/test_web_*). A budget is a promise about what a player downloads, so it stays an exact failure: a file over it fails its test.
# The rule: the measured size plus about 10%, rounded up (to 50,000 from 1,000,000, to 5,000 from 100,000, to 500 from 10,000, else to 100). A raise is a one-number edit with
# its reason in the commit message; no story is kept here.
# the whole of web/front/ (pictures, font, scripts, style) and the map maker's own scripts and style, apart from the pictures
FRONT_BUDGET = 1_650_000
EDITOR_BUDGET, EDITOR_GZIP_BUDGET = 430_000, 125_000
# scripts that load on first use (raw bytes, gzipped; both files of a pair together unless a script is named alone)
LAZY_BUDGET, LAZY_GZIP_BUDGET = 66_000, 21_500          # the three lazy map maker scripts together
SUBMIT_BUDGET, SUBMIT_GZIP_BUDGET = 35_000, 11_000      # mapmaker_submit.js
ANTS_BUDGET, ANTS_GZIP_BUDGET = 46_500, 14_000          # the Ants tool
CONVERT_BUDGET, CONVERT_GZIP_BUDGET = 33_500, 10_500    # Copy, convert and Crop
PICKUP_BUDGET, PICKUP_GZIP_BUDGET = 83_000, 23_500      # the Pick up tool
PLAY_BUDGET, PLAY_GZIP_BUDGET = 36_500, 10_500          # Play this map
# the game page
GAME_BUDGET, GAME_GZIP_BUDGET = 12_000, 4_600           # the game page's side of Play this map
GAME_MENU_BUDGET, GAME_MENU_GZIP_BUDGET = 45_000, 13_000  # the settings menu
SHAPES_BUDGET, SHAPES_GZIP_BUDGET = 8_100, 3_100        # the one table of the picture's shapes
REJOIN_KEY_BUDGET, REJOIN_KEY_GZIP_BUDGET = 5_900, 2_400    # the seat-key reader (web/rejoin_key.js), shared by the front page and the game page
PICKER_BUDGET, PICKER_GZIP_BUDGET = 15_500, 5_000       # the picture's dropdown
# the front page (web/home/, web/front/player_name.js)
HOME_BUDGET, HOME_GZIP_BUDGET = 44_000, 13_500
GAMES_BUDGET, GAMES_GZIP_BUDGET = 98_000, 28_000
PLAYERS_BUDGET, PLAYERS_GZIP_BUDGET = 33_000, 10_500
CHAT_BUDGET, CHAT_GZIP_BUDGET = 56_500, 18_000
TALK_BUDGET, TALK_GZIP_BUDGET = 28_500, 9_800
SCRIPT_BUDGET, SCRIPT_GZIP_BUDGET = 13_000, 4_500       # player_name.js
# the installable web app (web/app/, web/manifest.webmanifest): the four icons together (measured 66,850 bytes), and the manifest (measured 1,166, revalidated on every visit)
APP_ICONS_BUDGET = 73_600
APP_MANIFEST_BUDGET = 1_300
# the bar at the top of every page (web/site_nav.css, web/site_nav.js)
NAV_CSS_BUDGET, NAV_CSS_GZIP_BUDGET = 5_900, 1_900
NAV_JS_BUDGET, NAV_JS_GZIP_BUDGET = 4_400, 1_800
