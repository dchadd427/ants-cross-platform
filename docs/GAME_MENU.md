# The settings menu

**Esc**, or a **Settings** button, opens a panel over the running game: Sound, Video, Controls and Game, with **Resume** and **Leave game**. The game keeps running behind it (the panel says so). This page is the one home of the menu: what it does, the one table of rows that every look of it is drawn from, and how the browser's panel is built. The keys it lists are in [`CONTROLS.md`](CONTROLS.md), the browser page around it in [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md), the touch screen in [`TOUCH.md`](TOUCH.md).

## Opening and closing

- **Esc closes or cancels what is open first, and opens the menu only when there is nothing to cancel.** A dialog (a team-up offer, the quit question, the quick help) answers its own Esc as ever; the original's options window keeps its Esc (it does nothing) and the menu never opens over it; with something selected (ants, the hill) or a pedestal latched, Esc deselects as the original does, and the **next** Esc opens the menu. That is the rule of the HUD (`HUD::handle_key_down`): the original's Esc is unchanged, the menu is what an Esc that would have done nothing does.
- **A held Esc does not open or close it**: only a first press (not a key repeat) counts, so that holding Esc to leave fullscreen in Chrome and Edge does not flicker it.
- **The Settings button** is for what has no Esc: in the page's header next to Menu (from 1140 px wide), under the picture (a window narrower than that, and every phone), and in the bar of a recording. **Nothing is laid over the game's picture, fullscreen or not** (the owner's word): in fullscreen, real or the page's own, there is no Settings button on any device. A computer opens the menu with Esc; in Safari and Firefox, where Esc belongs to the browser in fullscreen, Esc leaves fullscreen first. **A phone in fullscreen has no way to open the menu** (no Esc, no button) until it leaves fullscreen; if that is wanted back, the button over the picture at the top is the one to restore (`git log -S"settings-btn-fs"`), or a button in the game's own bar. It is off (and says "Not now") while a dialog needs its answer.
- **Resume**, **Esc** and the cross (a phone has no Esc) close it. A click on the dimmed area does not: a stray touch cannot close it.
- **A dialog that needs an answer closes it** and it does not open over one: a team-up offer, a vote (F2 / F3), a lost link or the catch-up screen, the results, the quick help and the options window that the panel itself opens. The answer is more urgent than a setting, and a dialog drawn on the canvas would sit hidden under the panel.
- Where it is not: the frame of a match page (`play.html?replay=...&embed=1`, "Show replay") has no panel, no button and loads no script; Esc is what it was. The loading, the room and the results have none.

## What the game does while it is open

The panel is a child of the stage (`#game-stage`), the element that goes fullscreen, and fixed over the whole window, so it is there in fullscreen too, and its dimmed area covers the whole screen. While it is up:

- the game takes **no key, text, click, wheel or finger** (`Application::game_menu_takes`, told by `ants_menu_open`) and the map does not scroll at the edges; what a drag or a held button began before it opened is let go; the simulation, the sound and the network run on and nothing is sent;
- closing takes effect after the events that were queued, so the Esc that closes it is not a second Esc for the game;
- the page behind it is `inert`, Tab stays inside, the focus starts on Resume and goes back to the canvas when it closes, and the dialog is named (`role="dialog"`, `aria-modal`, the tabs a `tablist`, the sliders real `input type=range`, the Picture shape a native `select`);
- the **pointer lock is let go** (a locked pointer has no cursor) and asked for again when it closes, if it was held or the page is fullscreen; a lock that the page is given while the panel is up (fullscreen asks for one) is let go at once: the panel hears it on the window in the capture phase, because the page's own handler there stops the event before the document's, and the 250 ms poll catches the rest.
- only the keys of the panel's own controls (Tab, Enter, Space, the arrows, Home, End and Esc) stop at the panel; every other key goes on to the page, which keeps the browser's reactions to Ctrl+S, F1 and the like off, and the game takes none while the panel is up. The focus goes back to the canvas when it closes, or to the Settings button when a key opened it. **The Picture shape dropdown** (a native `select`, in the panel's focus order, so Tab wraps around it) takes the arrows, Home and End itself. On Windows and Linux a closed dropdown changes its entry on an arrow, so each arrow changes the picture of the running game at once and the panel stays open; on macOS an arrow opens the list and Enter commits the entry (the page does nothing different there). Esc on the closed dropdown closes the panel. Esc with the list OPEN belongs to the browser's own popup and has not been verified in any browser (a headless browser cannot show the popup); it is to be checked by hand in Chrome, Firefox and Safari.

## The rows

The rows are **one table in C++** (`include/ants_app/game_menu.hpp`, `src/ants_app/game_menu.cpp`): id, name, section, kind, range, default, which screens show it, and every word. The page gets it as one JSON text from the game (`Module._ants_menu_describe`) and draws what it says; a skin of the desktop game can read the same table. `tests/data/game_menu_match.json` and `game_menu_watch.json` are what the game says, kept equal to the table by `tests/test_app/test_game_menu.cpp` (`test_game_menu --write-fixtures` writes them again), and the page's check draws from them.

| Section | Row | Where its value lives | Applies |
|---|---|---|---|
| Sound | **Master volume** (new) | `master=` in the settings file; default 100, so nothing changes for anybody | while dragged (`AudioMixer::set_output_gain`, a 12 ms ramp: no click) |
| Sound | Music volume | the original's `Music Volume` | at the release, as the options window does |
| Sound | Sound effects volume | the original's `Sound Volume` | at the release, with the test voice |
| Sound | **Mute everything** (new) | `mute=on` / `off` | at once; every slider stays where it is and a muted sound goes on playing |
| Video | Fullscreen | the page | the page's own enter / leave (a real click: the browser's permission) |
| Video | Picture shape | the page (`ants.aspect.v2`, the address) | a native dropdown that the panel fills from the page's table (`web/shapes.js`: the shape and its canvas, a quiet line under it names the shape that fills the screen); a choice calls the page's one function (the address and the stored choice follow); the row's range and words (their number) are made from the game's shape table (`kAspectTable`) |
| Video | Fullscreen mouse | the page (`ants.pointerlock`) | Locked / Free; not on a touch screen |
| Video | **Hit point numbers** (new: remembered) | `hp_numbers=` (Ctrl+L flips the same flag) | at once |
| Video | Map scroll speed | the original's `Scroll Speed` | at the release |
| Controls | the mouse (or the fingers) and the keys | none: a list to read, in `web/game_menu.js` | |
| Game | Participate in chat, Show quick help at startup | the original's names | at once |
| Game | Quick help (F1), Original options window (Ctrl+O) | none | close the panel and open the original's window |
| Game | Version | none | |

Master volume and the three that are the original's use the same numbers as the original's options window (0 to 99 on the slider; the stored 100 reads as 99), and the panel and the window show the same state, so they always agree. The values are the player's own: **Clear Cache & Update** erases the settings file and they come back to their defaults. The original's options window, Ctrl+O and the top bar's buttons are unchanged.

## Leave game

It asks first, in the words of what will happen (`game_menu::leave_kind`, the rules of the quit question's Yes, `Application::confirm_quit`; the default is **Stay in the game** and Esc means stay):

| Case | Question | Button |
|---|---|---|
| exactly one opponent left in a running match | "Forfeit and leave?" It ends now for everybody and counts as a loss for you. | Forfeit |
| two or more opponents in a network match | "Leave the game?" You will drop out; your ants are lost, the others play on, you cannot come back. | Leave game |
| a game at home against several, or alone | "Leave the game?" This match ends. Your game is not saved. | Leave game |
| the match is held (a seat missing, the countdown, a lost link) | "Leave the game?" The match is held. You leave it for good and cannot come back to this match. | Leave game |

A recording asks nothing (below). Only a click leaves: a question that is up stays when the match becomes held (a seat's link dropped) or the number of opponents changes under it (it is drawn again in the new words), and never turns into a leave by itself. Which kinds ask is the table's word (`asks`), not a rule of the page.

There is no separate Surrender: the forfeit is the quit that the game always had. Leaving goes through `confirm_quit`, so the `Quit` command, a drop out, the recording and the state hashes are what they were. A game on this computer leaves for the front page (a game in the lobby's frame, for that page's setup screen), as Quit always did.

## A recording or a live match that is watched

The same panel, without what a seat needs: no Game section, a tag ("Watching a replay", "Watching live"), the note "The replay keeps playing. Pause it with the bar.", and **Back to the matches** for Leave game, which asks nothing (nothing is lost) and goes to the list. Esc, Resume and the dimmed area behave as in a match. Its Settings button is in the replay's bar.

## How the browser's panel is built

- **The game's side** (`src/ants_app/application_game_menu.cpp`): the exports `ants_menu_describe`, `ants_menu_get`, `ants_menu_set` (a third number says whether to write the value now: a slider that is dragged is heard at once and written at the release), `ants_menu_do` and `ants_menu_open`; the HUD's Esc hook (`HUD::set_on_menu_request`) that calls `window.antsGameMenuAsk` of the page (a page without the script, or a frame, has none and Esc stays what it was); the three new settings (`game_menu::MenuSettings`).
- **The page's side** (`web/game_menu.js`, `web/game_menu.css`, beside `web/shell.html` like `countdown_button.js`): fetched once the game runs (`antsLoadGameMenu` in the page; retried once, then said in the toast, and Esc keeps its old meaning), never in an embedded frame and not in the first load. It makes its own Settings buttons and panel (every text with `textContent`), fetches its own stylesheet, and asks the game for everything that is the game's. ES5, no module system.
- **Size**: the two files together are held to 41,000 bytes (12,000 gzipped; about 40,000 and 11,600 now) by `tests/scripts/test_web_front.py`, apart from the `web/front` folder's cap.
- **Tests**: `tests/test_app/test_game_menu.cpp` (the table, Leave game's kinds, the settings, Esc's rule, the input the game does not take, the acts; one case in `test_replay_app.cpp` for a recording, one in `test_app_integration_basics.cpp` for the mixer's gain), `tests/scripts/test_web_game_menu.py` with `web_game_menu_check.js` (the page's script on a strict fake of the DOM, and a look that the style and the script have nothing over the picture, in the quick tier) and the opt-in `tests/scripts/test_web_game_menu.sh` (a real browser: 1280 x 800 with a pointer lock that comes while the panel is up, a phone at 390 x 844 with a finger held on the map as the panel opens, and on its side, a recording, a frame). Its mutants are `tests/data/game_menu_mutants.json`.

## Not yet

The desktop game's own drawn panel (the same table, the keyboard and the mouse), Leave game going back to the start menu from a single-player game there, and the Controls list drawn from the same table as [`CONTROLS.md`](CONTROLS.md) are later changes; until then the desktop game's Esc is what it was.
