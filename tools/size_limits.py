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
FRONT_BUDGET = 1_900_000
EDITOR_BUDGET, EDITOR_GZIP_BUDGET = 430_000, 125_000
# scripts that load on first use (raw bytes, gzipped; both files of a pair together unless a script is named alone)
LAZY_BUDGET, LAZY_GZIP_BUDGET = 80_000, 25_500          # the three lazy map maker scripts together
SUBMIT_BUDGET, SUBMIT_GZIP_BUDGET = 35_000, 11_000      # mapmaker_submit.js
ANTS_BUDGET, ANTS_GZIP_BUDGET = 46_500, 14_000          # the Ants tool
CONVERT_BUDGET, CONVERT_GZIP_BUDGET = 33_500, 10_500    # Copy, convert and Crop
PICKUP_BUDGET, PICKUP_GZIP_BUDGET = 83_000, 23_500      # the Pick up tool
PLAY_BUDGET, PLAY_GZIP_BUDGET = 36_500, 10_500          # Play this map
AI_BUDGET, AI_GZIP_BUDGET = 84_000, 24_000              # Connect an AI: the sheet, the strip and the socket (mapmaker_ai.js), the tools (mapmaker_ai_tools.js) and the style
# the game page
GAME_BUDGET, GAME_GZIP_BUDGET = 12_000, 4_600           # the game page's side of Play this map
GAME_MENU_BUDGET, GAME_MENU_GZIP_BUDGET = 53_000, 15_500  # the settings menu
SHAPES_BUDGET, SHAPES_GZIP_BUDGET = 12_000, 4_400       # the one table of the picture's shapes
REJOIN_KEY_BUDGET, REJOIN_KEY_GZIP_BUDGET = 5_900, 2_400    # the seat-key reader (web/rejoin_key.js), shared by the front page and the game page
PICKER_BUDGET, PICKER_GZIP_BUDGET = 17_000, 5_500      # the picture's dropdown
WAKE_LOCK_BUDGET, WAKE_LOCK_GZIP_BUDGET = 8_100, 2_900   # the screen lock of a match and a replay (web/wake_lock.js)
P2P_BUDGET, P2P_GZIP_BUDGET = 17_500, 5_600             # the data channels to the other players (web/p2p.js)
TOUCH_KEYS_BUDGET, TOUCH_KEYS_GZIP_BUDGET = 35_700, 11_200   # the hotkey buttons of a touch screen, with the Shift button (web/touch_keys.js and web/touch_keys.css together)
# the map pages (docs/MAP_PAGES.md): the style and the script of the index, and what tools/map_pages.py makes (the list that the index reads, one page of a map, one page of the index)
MAP_PAGES_BUDGET, MAP_PAGES_GZIP_BUDGET = 4_200, 1_600         # web/map_pages.css
MAP_LIBRARY_BUDGET, MAP_LIBRARY_GZIP_BUDGET = 9_000, 3_300     # web/map_library.js
MAP_PLAYS_BUDGET, MAP_PLAYS_GZIP_BUDGET = 2_800, 1_300        # web/map_plays.js
MAP_DATA_BUDGET, MAP_DATA_GZIP_BUDGET = 66_000, 18_500         # map_library_data.js: raise with the library, one number
MAP_PAGE_BUDGET, MAP_INDEX_PAGE_BUDGET = 10_500, 25_500        # one map's page, one page of the index (each is also what a search engine reads)
# the front page (web/home/, web/front/player_name.js)
HOME_BUDGET, HOME_GZIP_BUDGET = 53_600, 15_900
GAMES_BUDGET, GAMES_GZIP_BUDGET = 105_000, 29_800
PLAYERS_BUDGET, PLAYERS_GZIP_BUDGET = 61_000, 18_500       # players.js and players.css: raised from 46,500 and 14,400 for the Everyone and Following tabs, the star and the people who are not here (docs/HUB_PEOPLE.md)
VP_BUDGET, VP_GZIP_BUDGET = 4_200, 1_900            # vp.js: the screen numbers of /?vp (loaded for that address only)
CHAT_BUDGET, CHAT_GZIP_BUDGET = 125_000, 37_500     # chat.js and chat.css: raised from 112,000 and 33,800 for the frames of follows and private messages that the dock hands on and the answers of the sheet's own commands (docs/HUB_PEOPLE.md)
IMAGES_BUDGET, IMAGES_GZIP_BUDGET = 46_000, 15_800    # pics.js, pics.css, post.js and post.css: the thumbnails, the viewer and the poster of the chat's images (loaded when the first image is drawn; post.* for a person who may post only); raised from 41,500 and 14,500 for the viewer's Share, Copy link and Save in the installed app
TALK_BUDGET, TALK_GZIP_BUDGET = 80_600, 28_900
MOD_BUDGET, MOD_GZIP_BUDGET = 86_500, 25_000        # mod.js and mod.css: the sheet of a person, with Message, Follow, Ignore and Report for anybody who is logged in, the invitation for anybody in a room and the moderation for staff; raised from 72,600 and 21,000 for the four rows
CMDS_BUDGET, CMDS_GZIP_BUDGET = 64_500, 19_500       # cmds.js and cmds.css: the helper of the chat's commands (and of the names after /invite and an @), the /bots builder and the private answers (loaded for an account that has commands only); raised from 57,500 and 17,500 for the words of /follow, /unfollow and /msg
SPLIT_BUDGET, SPLIT_GZIP_BUDGET = 16_000, 5_200     # split.js and split.css: the drag handles of Home
ROOM_BUDGET, ROOM_GZIP_BUDGET = 38_000, 11_200       # room.css, roomchat.js and roomchat.css: the room inside Home and the room's tab of the chat box
ALERTS_BUDGET, ALERTS_GZIP_BUDGET = 54_500, 16_500   # alerts.js and alerts.css: the bell's list of alerts and its settings, the invitation, room, message and mention toasts, the join alert of Home and the entry of the match that is over; raised from 48,500 and 14,500 for the room and message rows
ALERT_CORE_BUDGET, ALERT_CORE_GZIP_BUDGET = 13_000, 4_400   # web/alert_core.js: the switches, the tab alert and the sound table that the front page and the game page share
MESSAGES_BUDGET, MESSAGES_GZIP_BUDGET = 47_000, 14_000   # messages.js and messages.css: the Messages button of the chat's head, the panel (a sheet on a phone), the conversations, the report card (loaded for a person who is logged in only; docs/HUB_MESSAGES.md)
SCRIPT_BUDGET, SCRIPT_GZIP_BUDGET = 13_000, 4_500       # player_name.js
# the installable web app (web/app/, web/manifest.webmanifest): the four icons together (measured 5,922 bytes: the original game's icon, 12 colors and fine detail), and the manifest (measured 1,166, revalidated on every visit)
APP_ICONS_BUDGET = 6_500
# the browser-tab icon (web/favicon.png 527 bytes + web/favicon.ico 1,530 bytes: the original game's icon at 32 and 64 px)
FAVICON_BUDGET = 2_300
APP_MANIFEST_BUDGET = 1_300
# the game page's part of the bar (web/game_bar.js and web/game_bar.css together)
GAME_BAR_BUDGET, GAME_BAR_GZIP_BUDGET = 12_800, 4_250
# the bar at the top of every page (web/site_nav.css, web/site_nav.js)
NAV_CSS_BUDGET, NAV_CSS_GZIP_BUDGET = 4_900, 1_700
NAV_JS_BUDGET, NAV_JS_GZIP_BUDGET = 8_200, 3_150
# the update notice (web/update_notice.js and web/update_notice.css together: the strip and the "What's new" window that every page with the version stamped loads)
UPDATE_NOTICE_BUDGET, UPDATE_NOTICE_GZIP_BUDGET = 24_000, 7_900
