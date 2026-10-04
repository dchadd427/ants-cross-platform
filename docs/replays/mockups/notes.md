# Replays and Watch bots: the mock-ups (notes)

Scratch only: nothing in the repository was changed or committed. Every picture is 960 x 540 (the web page 1280 wide), made the way the approved wide-pages pictures were: the original's art from `ants.chd`, the game's TrueType font (its own glyph bitmaps), nothing scaled, and for the match screen the game's OWN screenshot of a real match. The overview is one column at full size (960 wide: a phone shows it at 40 percent, pinch to zoom).

## 0. Review order on a phone

`overview.png`, then `compare_menu_main.png`, `compare_c.png`, `compare_d.png` (today above, proposal below), then `alternatives/`.

## 1. Files

| File | What |
|---|---|
| `overview.png` | every picture, one column, numbered 0, A .. E |
| `menu_main_new.png`, `menu_main_today.png`, `compare_menu_main.png` | **0** the first panel of the start menu with Watch bots and Replays; today's approved four-button panel |
| `a_watch_bots.png`, `a_watch_bots_1v1.png`, `a_watch_bots_2v2.png` | **(a)** the Watch bots panel: 1v1v1v1 (Blue is "Random" level and style, to show the choice), 1v1 (two seats "Not playing"), 2v2 (Sides "Green + Black", Team A / B) |
| `b_replays.png`, `b_replays_empty.png`, `b_replays_delete.png` | **(b)** the Replays list (six sample rows, the second selected), nothing saved, the delete question |
| `c_watch_live.png`, `c_replay.png`, `c_today_player_hud.png`, `compare_c.png` | **(c)** the spectator HUD in a live watch (4 bots, 3:30 of 12:00) and in a replay (a 2v2 played online by Anna and Ben, recorded by a player's own game, 7:30 of 12:00 at 4x); today's player HUD of the same match |
| `d_end_of_watch.png`, `d_end_of_replay_2v2.png`, `d_today_results.png`, `compare_d.png` | **(d)** the end of a watched match and of a replay; today's approved results page |
| `e_web_page.png` | **(e)** the web page with the Watch bots / Replay bar under the picture (the 960 x 540 picture is (c)'s live one) |
| `alternatives/c_alt_seek_bar_in_top_bar.png` | the same HUD with the seek bar long, in the free middle of the top bar (x 142 .. 562) instead of in the panel: finer seeking (1.7 s a pixel against 5 s) but a 14 px target in a 21 px bar, which a finger cannot hit |
| `alternatives/c_alt_live_rewound.png` | a live watch that has been taken back to 1:50 (the match waits at 3:30, the frontier: the dark green part of the bar); the heading says REWOUND and the jump says Live |
| `alternatives/c_alt_replay_paused.png` | a paused replay: the pause plate is lit and shows the triangle |
| `alternatives/c_alt_grey_frame.png` | the frame in the black team's grey as a "not a player" colour: tried, not proposed (it IS the black team's colour, the first score box then belongs to Black, and the green plates do not suit it) |
| `rects/` | every rectangle: `menu_main_new.txt`, `a_watch_bots.txt`, `b_replays.txt` (page and canvas coordinates), `c_spectator_controls.txt` (canvas), `d_end_of_watch.txt` (with the seams of the info box) |
| `game_screenshots/` | the game's own screenshots that (c) is built on, and `settings_chat_off.cfg` (the one setting they were made with) |
| `web/` | the static copy of `web/shell.html` with the bar (`page_mock.html`), `shoot.sh` (headless Chrome, throwaway profile), the raw screenshot |
| `*.py`, `data/` | the generators (`build_all.py` makes everything) and the dump of `ants.chd` (+ the cache of the game's text widths and glyphs) that they read |

## 2. How they were made

* **Start menu panels (0, a, b).** `watch_menu.py` on the approved compositor (`menu.py`: the same fills, bevels, cyclers, fields and text as `start_menu_view.cpp`; its first panel reproduces the approved picture with 0 differing pixels): the wide tile clay and frame of the setup screen, the controls untouched in size and look, title at the top, the middle group centred (+160, +30), the hint at the bottom (+60). The new panels fit the original's 640 x 480 page (the classic picture shows them centred). The first panel's buttons keep their size, the pitch goes from 66 to 60 and the first button from y 118 to 94 (six buttons).
* **The match screen (c).** `tools/shot_driver.cpp` starts the real game headless on TREASURE (seed 7; seats 1 - 3 by the game's own `--bot`, seat 0 by an outside `BotController` over a direct sink, which is what a watch will do for every seat), steps it one tick at a time and takes the game's `--screenshot` after 4,201 ticks (3:30 elapsed, "8:29" left on the clock); zoom 0.5 and the camera on the middle of the map so that all four hills show; the chat is switched off with the original's own option, which puts the original's cover (`chatcovr`) over the input and hides "Send to"; the pointer is "outside the window" so that no cursor is drawn. The replay picture is the same with the alliances formed at turn 0 by the four ordinary commands (the chat log shows the game's own "... are a team now!" lines), names typed as "Anna" / "Ben", 9,001 ticks. Then `spectator.py` draws the controls on the free card of the right panel (where today's Move pedestal and Stop stand; see `compare_c.png`) with the start menu's plates and a seek bar in the style of the clock's black box; the status line's text is erased.
* **The end page (d).** `end_screen.py`: the approved wide results page (`results.py`) without the "YOUR SCORE" art; an info box in its place, built with the same box construction as the other boxes (`pieces.efram_box_100`: one jump in each side strip, between identical lines: listed in `rects/d_end_of_watch.txt`); plates left of the original's Leave Game button.
* **The web page (e).** A static copy of `web/shell.html` (scripts removed, the picture in place of the canvas, one new row built from the page's own `.view-bar` / `.seg` classes), rendered by a headless Chrome with a throwaway profile; the gap that the page's script closes in the real page was cut out.

## 3. What is real and what is a sample

* **Real:** the match screens (the game's renderer, HUD, map, ants, clock, score numbers and boxes, the chat lines, the alliances); the final scores of (d)'s first match (2,475 / 2,270 / 2,085 / 2,020: the same match as (c), played to its end); the game's text; the art.
* **Sample:** the six rows of the Replays list (dates, winners, venues); the names Anna and Ben; the three counters of the results (friendly lost, enemy killed, new hatched: today's bots do not fight, the real ones are all 0 or 1; the samples add up: lost = killed); the date in the replay's heading; the scores of (d2).
* **Not drawn:** hover and pressed states, the Seeking label, the hint texts (the status line is empty in the pictures), the first panel when the replays folder cannot be read, other maps.

## 4. Rebuilding

`python3 build_all.py` (needs Pillow; `game_screenshots/` and `data/` are inputs). The screenshots: `tools/shot_driver.cpp` against the libraries of a build of the game (`LABEL`, `CAM`, `NOPTR`, `ALLY`, `LOCAL`, `EXTRA`, `SELECT_OWN` are its environment options: `live_base.png` = `LABEL="Bot (Hard)" NOPTR=1 CAM=28,27 EXTRA="--zoom|0.5" shot_driver live_base.png 4201 7 Original-Ants/Maps/TREASURE.LVL 16:9 standard:hard standard:medium standard:medium standard:easy` with the chat-off setting). `web/shoot.sh` renders the page.
