# The picture, the view and the HUD

What you see in a match and how the game draws it: the 16:9 picture, the map view with its edge scrolling and zoom, the minimap, the score boxes, the status line and the chat log, the results screen, and the text and sound that go with them. Where the original program's own numbers matter, the section names the part of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) that holds them (for example 5.43). The rules of the game are in [`GAMEPLAY.md`](GAMEPLAY.md), the mouse, keys and touch gestures in [`CONTROLS.md`](CONTROLS.md), and the web page in [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md).

**Wording.** On-screen text says "player", never "friend" (`tests/scripts/test_web_lobby.py` checks the lobby page).

## 16:9 by default: a wider view with more of the map (desktop and web builds)

The game opens in a 16:9 picture, a fixed canvas of 960 x 540. The original's own 640 x 480 picture is `--aspect 4:3` on the command line, `aspect=4:3` in the settings file, or, in the browser, `?aspect=4:3` on the page's address or the page's selector. Two more wide shapes are offered for other screens: `16:10` (960 x 600, laptops and 16:10 monitors) and `21:9` (1260 x 540, ultrawide monitors). The desktop window is created in the picture's shape from the first moment.

| | `--aspect 4:3` | 16:10 | 16:9 (the default) | 21:9 |
|---|---|---|---|---|
| Canvas | 640 x 480 | 960 x 600 | 960 x 540 | 1260 x 540 |
| Map view, at (16, 21) | 442 x 440 pixels (13.8 x 13.75 tiles) | 762 x 560 pixels (23.8 x 17.5 tiles) | 762 x 500 pixels (23.8 x 15.6 tiles) | 1062 x 500 pixels (33.2 x 15.6 tiles) |
| Right panel and minimap | the original's places (minimap at (480, 35)) | pinned to the right edge, 320 px further right (minimap at (800, 35)) | the same as 16:10 | pinned to the right edge, 620 px further right (minimap at (1100, 35)) |
| Chat log | 138 x 101 at (482, 299) | 138 x 221 (120 px taller) at (802, 299) | 138 x 161 (60 px taller) at (802, 299) | 138 x 161 at (1102, 299) |
| The three bottom score boxes, x | 105, 254, 402 | 213, 468, 722 (60 px lower than 16:9) | 213, 468, 722 | 313, 668, 1022 |

The shape can be changed **while a match runs**: in the browser the selector under the game (Classic 4:3, 16:10, 16:9, 21:9) switches at once, with no reload, no new connection and nothing asked; the simulation is not touched (a switched player's state hash after every tick is the one of a match that was never switched: `test_aspect_switch`). `Application::set_aspect` is the call. The world point in the middle of the view stays in the middle, the zoom that the player chose is kept and comes back where the new view offers it, and the pointer, the HUD and the screens follow. A native window keeps its size (the picture is fitted into it with bars, as for any window); nothing in the desktop game calls it yet.

- The view shows about twice the area of the world at once. At the zoom 1 everybody who plays the same aspect sees the same area. The zoom is each player's own choice (see Mouse-wheel zoom below).
- The frame is the original's own art, grown by repeating one line of its pieces: the top bar, the bottom strip and the left strip, and, in the right panel, the strip beside the map, the chat box and the right edge strip at one row (357, where all three are plain between their ant decorations). The clock box, the score boxes and the decorations are never stretched (`include/ants_app/shell_layout.hpp` lists the cuts). The bottom strip is widened at three plain cuts of its art, so that the three score boxes are spread evenly over it (they are laid out by slot).
- A map that is smaller than the view (a 16 x 16 map is 512 px wide) is centred in it with black around it, and the camera stays fixed on that axis. A click on the black does nothing at all.
- The edge strips of the scroll run along the edges of the whole picture. The minimap's view frame, the start view of a match and the sound listener follow the bigger view.
- Every screen outside a match is composed for the whole canvas from the original's own art, on the same clay and wide frame as the setup screen:
  - the loading screen and the quick help at the start: the page is centred in the frame, and START! is in the bottom right corner, where it keeps its place relative to the setup screen's START;
  - the results screen (see Results Screen below);
  - the desktop start menu: its controls untouched, the title at the top, the buttons centred, the hint and the server line at the bottom;
  - the setup screen and the room have their own 16:9 version with a map preview (see [`CONTROLS.md`](CONTROLS.md)).
  - in the 16:10 and 21:9 canvases these pages are the 960 x 540 page, **centred in the canvas with black around it** (`CanvasLayout::page()`; the frame-rate plate and the version stand in the page's corner); the match itself fills the whole canvas. Pages composed for the other shapes are future work.
- The windows that open during a match (the options window, the quick help, the quit and alliance dialogs, "get ready") are centred over the map view, with the frame around them. The options window dims everything outside its card with the checker dither of its art: the whole picture, the frame and the panel included.
- The web page's own handling of the picture (its selector, its fullscreen) is in [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md).

## Edge Scrolling & Minimap

The map view is the original's: 442 x 440 pixels at the screen pixel (16, 21), and 762 x 500 in the 16:9 picture. A match starts as in the original: the view scrolls just far enough to show the square around your hill. It does not centre it ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.44, the bullet "The map view and the start view").

The view scrolls like the original's input task, every 50 ms ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.43):

- The eight 12 px edge strips show the scroll arrows, and only the 5 px inner strips scroll. A strip that cannot move shows no arrow. Dialogs or a captured button stop the scroll.
- The view moves just far enough to show a square that reaches `scroll rate + 10` px each way around the target point of the pointer, so the step depends on the exact pointer pixel. The scroll rate is the options window's Map Scroll Rate (the settings key `Scroll Speed`, 0 - 99, default 50). The steps are the same in both pictures:

  | Scroll rate | Step on the pushed axis, per 50 ms tick | Speed |
  |---|---|---|
  | 0 (slowest) | 5 - 10 px | 100 - 200 px/s |
  | 50 (default) | 55 - 60 px | 1100 - 1200 px/s |
  | 99 (fastest) | 104 - 109 px | 2080 - 2180 px/s |

- Holding the left button on the minimap centres the view on the point under the pointer.
- Nothing scrolls with the keyboard except Ctrl+N / Ctrl+P, which select the next / previous ant of yours and bring it into view (just far enough to show the ant with a margin of 128 px). The wheel zooms (see below).
- In a window of another shape than the picture (fullscreen on a 21:9 or 16:10 monitor with the 16:9 picture, a wide window with the 4:3 one), the black bars beside the picture count as its nearest edge pixel. Pushing the mouse into a bar scrolls like the edge. (The original's exclusive 640 x 480 mode gave its one screen no bars.)
- A button released outside the window, or a finger lifted from a touch screen, leaves no pointer behind that keeps scrolling.
- The web page does the same for the bars of its fullscreen: it hands the game the pointer at the picture's nearest edge pixel, because the browser sends the game's canvas nothing over a bar.

## Mouse-wheel zoom

The wheel over the map view zooms through many levels, one a notch, towards the pointer: the world point under it stays under it as far as the map's edges allow. The wheel rolled away zooms in, the wheel rolled towards you zooms out, and the middle button goes back to 1. The full design is in the header of `include/ants_app/view_zoom.hpp`.

### Levels and the limit

- The levels are the series 2^(k/4), four to a doubling: **2** (every world pixel a crisp 2 x 2 square), 1.68, 1.41, 1.19 and **1** (the original's picture, exactly), then 0.84, 0.71, 0.59, **0.5**, 0.42 ... down to the map's own limit. The exact limit is the last level. A level closer than a quarter step above it is left out.
- **The limit is `max(view width / map width, view height / map height)`.** The map's width or its height just fills the view, so the view never shows anything outside the map. A map that the view already covers at 1 has no zoom-out.
- **The limit depends on the picture's shape.** On TREASURE (1920 x 1920 world pixels):

  | | 4:3 view (442 x 440) | 16:9 view (762 x 500) |
  |---|---|---|
  | Limit | 0.230 | 0.397 |
  | World pixels shown at the limit | 1920 x 1911, nearly the whole map | 1920 x 1260, two thirds of it |

  So in one match a player in the 4:3 picture can zoom out to nearly the whole of TREASURE, while a player in the 16:9 picture sees at most two thirds of it (at the zoom 1 the 16:9 picture already shows about twice the area of the 4:3 one). Every player can choose either shape.

### The wheel, the middle button and where they act

- On the desktop the system's natural scrolling is undone (`SDL_MOUSEWHEEL_FLIPPED`), so the wheel rolled away zooms in whatever the setting is. In the browser the system's own setting applies (see [`CONTROLS.md`](CONTROLS.md)).
- A mouse notch is one step: an event worth a step or more moves the nearest whole number of steps and keeps no fraction (the web page's 120 at 100 a step is 1.2, so one step). A trackpad's small deltas add up. A pause of half a second forgets a fraction, a change of direction starts again, and a wheel event that is not a number is ignored.
- The wheel and the middle button act only over the map view (not over the panels). They do nothing during a rubber band, a pressed button, the minimap or the chat log's drag, nor with a dialog or the results open (options, quick help, quit, alliance, "get ready").
- On the setup screens they do nothing, the 16:9 screens of a room with their chat box included, with the chat input closed or open. Typing in the match's chat goes on through every zoom step.
- In the browser the wheel and a trackpad's pinch (ctrl + wheel; Safari's gesture events) over the game canvas zoom the game and never scroll or zoom the page. The middle button over the canvas never starts the browser's autoscroll: the page cancels its press and its click, over the canvas only.

### The zoom is the player's own view

- **The zoom is the player's own view and nothing else.** The simulation, the network, the bots and every state hash never see it.
- **Every kind of match has the same levels**: a local game, a game with bots, a server's room, a LAN game (host or guest), and a match that the leader's START filled with bots.
- **With Fog of War on, every level draws exactly what the zoom 1 draws.** One world pass serves every level, with the same culling and the same gates: a tile that is not explored is dithered and hides the enemy ants, the food, the bombs, fire walls and power-ups, and the effects on it. The minimap respects the fog too (enemy ants, food, power-ups and fire walls only on explored cells). A test pins it at every level (`tests/test_app/test_zoom_view.cpp`).
- **What the zoom-out adds is breadth, not a new kind of information**: the types and states of the ants, planted bombs, effects and the hit point digits of every ant that is visible, over a wider area at once. Where those ants are was already on the minimap.
- The level is remembered: `zoom` in the settings file, `--zoom N` for any number from 0.05 to 2. A match starts at the nearest level that its map offers, and the remembered number is kept, so a small map's limit does not change what the next, larger map starts with.
- The map preview of the setup screens is always the zoom 1 picture, also on a room's screens when the game was started with `--zoom 0.5` or `--zoom 2`.

### How it is drawn

- At the zoom 1 the world is drawn straight into the view. At another zoom it is drawn at one texel per world pixel into an offscreen target that covers the world that the view shows, and copied into the view: nearest at 2, the linear filter between 1 and 2, the exact average of 2 x 2 world pixels at every halving below 0.5, and a last linear step. So 2, 1, 0.5 and 0.25 are exact pictures (nearest, or the average of 2 x 2 or 4 x 4 world pixels), and the levels in between are smoothed (2 stays crisp).
- The origin of the view lies on a grid of one screen pixel at the zoom, so a scroll moves the picture and never makes it shimmer.
- Everything else on the screen is drawn afterwards at its own size: the frame, the cursor, the rubber band and the hit point digits of Ctrl+L (one size at every zoom).
- These follow the world that is seen: the edge scroll moves the same distance on the screen at every zoom (in screen pixels), the arrow cursors follow what the view can still do, the minimap's frame is the world that is seen and a press on the minimap centres it, and so do Ctrl+N / Ctrl+P, the start view (the hill stays in view at every zoom) and the sound listener.
- **The zoom drawn is always the zoom the game says.** Every frame holds the camera inside the levels that the map offers (never below its limit). When the renderer cannot make the offscreen target, the zoom drops to 1 at the next frame and only 1 is offered. One line on stderr says so, once, when the failure begins. The levels come back when the target can be made again.
- The API that other input uses (a touch screen's pinch calls it): `Application::set_zoom(level, anchor)`, `step_zoom(direction, anchor)`, `zoom()`, `zoom_levels()` and `view_zoom_allowed(x, y)`.

## Touch controls (phones and tablets)

A touch screen plays the whole game with the mouse's own commands (a tap is the left click, a hold the right click, a drag the rubber band, two fingers move the map and a pinch zooms it through the API above): the gestures and their numbers are in [`TOUCH.md`](TOUCH.md), and the controls table is in [`CONTROLS.md`](CONTROLS.md).

## The HUD

- The frame and the right panel are the original's art, one animation (`uishell`, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.34).
- Besides the clock, the score boxes, the minimap and the chat, the HUD shows the card under the minimap with the two command pedestals (see [`CONTROLS.md`](CONTROLS.md)).
- On your own hill the egg count is an egg tray, `egg1` to `egg9` (ten or more eggs show nine).
- Every selected ant wears health-coloured selection ears: `dogears` at 9 or more hit points, `yelears` at 3 to 8, `redears` at 2 or fewer.
- The recessed status box (`wstatus.bmp`) sits under the card. The chat log and the chat box are below it.

## Minimap

The 119 x 91 image is painted the way the original's painter does it ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.55):

- Every pixel shows the object of the cell under it, in the colour of the original's table. A tile that is not in the table gets palette colour 0.
- Bombs show the terrain. In fog only power-ups, food and fire walls fall back to the fog colour, while rocks, toys, bridges and every colony's hill keep theirs. Fire walls show as yellow. There are no dots per object cell.
- The dots are rectangles on top of the image: flowers and clovers (3 x 3 on a 60 x 60 map) and the ants, each at its pixel position (2 x 2 on a 60 x 60 map, 3 x 2 on 31 x 31). Every ant has its dot until it is removed, dying and drowning ants too. In fog your own ants and your ally's show, enemies only on explored ground.
- The view frame is (251, 251, 255). Its size is the world that the view shows (442 x 440 in the 4:3 picture, 762 x 500 in the 16:9 one, divided by the zoom) divided by the scale (world pixels per image pixel), plus one pixel. It stands at the view's origin divided by the scale and is kept inside the image. On an axis where the view is bigger than the map, it is the whole image.
- The original's flash of the dot of an own or allied ant that was hit in the last 5 seconds is not ported.

## Score Boxes

The four score boxes are the original's ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.53):

- The local team's box is in the top bar. The others are in the three bottom slots, in team-index order.
- An allied team's box is half its colour and half its ally's colour, and shows the two scores added.
- The box of a team that does not play, or has dropped out, is covered with the original's `scorcovr` plate.

## Status Line & Messages

The one-line status box under the unit card is a single slot (the original's `PostStatus`, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.41). A new message replaces the old one and lives 5 s. Six messages flash: they flicker for 500 ms first.

- Selecting one ant of yours posts the text of its type: "Ready!" (worker), "BomberAnt selected.", "Where to?" (fire ant), "Thief here", "Yessir!" (combat ant) or "SwimmerAnt selected." Selecting several posts "Ready!" (string 12 of the original's table).
- A power-up taken by a selected ant of yours posts the selection text again, every time, also when the type does not change. The panel is rebuilt, which posts the text of the new type (string 12 for a group) and pops up both command pedestals.
- Orders answer with the ant's voice and a text: "On my way." (worker, bomber, fire ant, swimmer), "Movin' out." (combat ant) or "Here I go..." (thief) for a move, "Attack!" for an attack, "My pleasure..." (thief) or "Burn..." (fire ant) for a special order that one ant takes. Stop posts "Stopping."
- Nothing is shown while idle.
- The six flashing messages are "Can't - already have food.", "1 minute left in the game.", "30 seconds left in the game.", "10 seconds and counting...", "A ThiefAnt is at your anthill!" and "A team has been made."

The match clock is checked every 200 ms, like the original's CHECKGO task. The warnings come at 1:00, at 0:30 and in eleven countdown steps from 0:10, and the match ends within 200 ms after 0:00 (the rules of the end are in [`GAMEPLAY.md`](GAMEPLAY.md)).

## Alliance Texts & Chat Log

Teaming up follows the original's protocol ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.42). Three modal dialogs ask the questions:

| Dialog | Shown to | Buttons and keys |
|---|---|---|
| The question "... Would you like to accept?", or the variant that says it ends the invitee's present team (the allypro cue plays) | The invitee | Accept (`A`), Decline (`D` or `Esc`) |
| A waiting dialog | The proposer | Withdraw (`W` or `Esc`) |
| Whether to break the team | A player who already has a team and asks another team (the ally pedestal), or attacks an ant or the hill of the own ally | Yes (`Y`), No (`N` or `Esc`) |

- The proposer reads "%s accepted teaming up" or "%s rejected teaming up". The invitee reads "%s withdrew offer to team up" when the proposer takes the offer back.
- A team that is made flashes "A team has been made." and writes the News Flash "%s (%s) and %s (%s) are a team now!" into the chat log. Breaking it writes "... are no longer a team!". A drop-out writes "%s dropped out of the game!".
- The chat log keeps the original's entries: a header in the sender's team colour ("Name:" or "Name (To Teammate):", "[m:ss] News Flash:" for news) and a body of up to 100 characters, wrapped by pixels and set 10 px in (see Chat Log Window).
- Chat needs the "Participate In Chat" option. F9 - F12 chat the quick-chat texts to everybody. A team message reaches only the sender and the sender's allies.

## Chat Log Window

The log box is the original's window ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.56): 138 x 101 px at (482, 299), and in the 16:9 picture 138 x 161 at (802, 299).

- Entries are stacked with one pixel between them. The header is at the left edge and the body 10 px in, wrapped at 126 px by the font's real widths. A header wider than the box shows its end, and a half line at the edge is cut in pixels.
- The window follows the newest entry smoothly (5 px every 50 ms).
- **Holding the left button inside the log and moving the pointer scrolls it like a sheet. A pointer held outside scrolls 15 px every 100 ms. Releasing the button snaps back to the newest entries.** The original has no scroll bar, wheel or PageUp / PageDown, so the remake has none either.
- When the program ends after a match, the chat log is written to `chat.txt` ("date @ time", then "header body" per entry) in the per-user application folder (where `settings.ini` normally lies; a `--settings FILE` does not move it, see [`CONTROLS.md`](CONTROLS.md)). The original wrote it into the folder it ran in. The web build and headless runs write nothing.

## Results Screen

The results screen is the original's ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.49):

- "Waiting for scores..." shows for at least 250 ms. Then there is one row per team or alliance ("Alice & Bob", the columns added up), in the original's order and with the animated ant of each team. The order is by score, the quitter last, and the local team first on a tie that it made.
- The winner or loser cue plays once when the rows appear. The Leave button appears with the rows.
- Enter, C, Q and X leave at any time. Esc does nothing.
- In the 4:3 picture the top row is at y = 235, the others at 50 i + 273, and the numbers are left aligned at x = 485 / 534 / 555 / 576.
- The 16:9 page is recomposed for the whole canvas from the original's own pieces. The numbers and the Leave button move 320 px right (numbers at x = 805 / 854 / 875 / 896), the middle of the page moves 30 px down (top row at y = 265, the others at 50 i + 303), and both black boxes are 320 columns wider. The numbers are drawn in the original's 8 px digits.

## Modern Audio & Presentation

The application library (`ants_app`, `src/ants_app/`) draws the picture and plays the sound.

### Rendering and text

- The renderer is SDL2's hardware-accelerated one (SDL's software renderer when none can be made). The picture is SDL's logical canvas: 960 x 540 (16:9, the default) or the original's 640 x 480 (`--aspect 4:3`). It is scaled into the window by the largest scale that fits, centred, with nearest-neighbour filtering and black bars where the window's shape differs. The scale is fractional unless the window is a multiple of the canvas. There is no integer-scaling mode: a 1920 x 1080 screen shows the 16:9 picture at 2x and the 4:3 one at 2.25x.
- Every number of the picture's geometry (the map view, the minimap, the right panel, the edge strips of the scroll, the pointer's limits, the corner of the frame-rate plate) is a value of a `ScreenLayout` (`include/ants_app/screen_layout.hpp`) that the renderer, the HUD, the edge scroll and the application read. The 4:3 layout is the original's 640 x 480 screen. The 16:9 one moves the panel and the bottom strip out and grows the map view to 762 x 500.
- Text is TrueType, drawn at the cell height that the original gives each label ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.14):

  | Cell height | Used for |
  |---|---|
  | 12 px | the status line and the chat |
  | 14 px | the score labels and the setup screen's prompt |
  | 18 px | the setup screen's names and map name, the results rows |
  | 20 px | "Waiting for scores..." |
  | 24 px | dialogs |
  | 35 px | the start dialog's text |

- The face is the original's "Franklin Gothic Medium" when a copy of that font is found next to the game or in the Windows fonts folder. Otherwise it is the bundled Libre Franklin Medium (a free interpretation of the same Franklin Gothic, SIL Open Font License).
- The health numbers (Ctrl+L) are white digits in an 8 x 15 pixel cell, like the original's fixed system font.

### Sound and music

- A 32-channel software mixer plays the sound clips with the original's sound law ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.24c): Chebyshev distance over 2500 px, a pan that quiets only the far channel, DirectSound hundredths of a dB, and the Sound Volume option inside every sound.
- Music is MP3 renders of the original's four MIDI pieces (`INTRO.mp3`, `ANTS2A.mp3`, `ANTS2B.mp3`, `ANTSFUN3.mp3`), played by the same mixer on every platform ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.24e). In the browser (the WebAssembly build) the sound and the music go through SDL's Web Audio context; the web build has no HTML5 audio element. The original's `.MID` files are loaded but never played in a real game, so only the MP3s sound. Playback by the sources' AudioToolbox MIDI player (macOS) runs only in that player's own unit test; in `--headless` runs the player is a silent stand-in.

### Match Audio Cues

| Cue | File |
|---|---|
| 1-minute alert | `1min.wav` |
| 30-second warning | `30sec.wav` |
| 10-second countdown | `countdwn.wav` |
| Winner or defeat sting, one per machine, when the results' rows appear (250 ms after the screen opens) | `winner.wav` / `losers.wav` |
| A player drops out | `playerout.wav` |

"Can't hatch" and the raid alarm are global cues, and an accepted order clicks ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.24b).
