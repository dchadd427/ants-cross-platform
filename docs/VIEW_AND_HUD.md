# The picture, the view and the HUD

What you see in a match and how the game draws it: the 16:9 picture, the map view with its edge scrolling and zoom, the minimap, the score boxes, the status line and the chat log, the results screen, and the text and sound that go with them. Where the original program's own numbers matter, the section names the part of [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) that holds them (for example 5.43). The rules of the game are in [`GAMEPLAY.md`](GAMEPLAY.md), the mouse, keys and touch gestures in [`CONTROLS.md`](CONTROLS.md), and the web page in [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md).

**Wording.** On-screen text says "player", never "friend" (`tests/scripts/test_web_lobby.py` checks the lobby page).

## 16:9 by default: a wider view with more of the map (desktop and web builds)

The game opens in a 16:9 picture, a fixed canvas of 960 x 540. The original's own 640 x 480 picture is `--aspect 4:3` on the command line, `aspect=4:3` in the settings file, or, in the browser, `?aspect=4:3` on the page's address or the page's Picture dropdown. Five more wide shapes are offered for other screens: `3:2` (960 x 640, tablets and Surface-style laptops), `16:10` (960 x 600, laptops and 16:10 monitors), `20:9` (1200 x 540, phones held sideways), `21:9` (1260 x 540, ultrawide monitors) and `32:9` (1920 x 540, super-ultrawide monitors). Every canvas but the classic one is at least 960 x 540, because the pages (setup, room, loading, quick help, results) exist only for 640 x 480 and for 960 x 540. The desktop window is created in the picture's shape from the first moment. In the browser, a first visit with nothing stored starts in the shape nearest the player's screen instead ([`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md), "Which shape").

| Shape | Canvas | Map view, at (16, 21) | Tiles at the zoom 1 | Area of 16:9's | Minimap at | Chat log | Bottom score boxes, x |
|---|---|---|---|---|---|---|---|
| `4:3` | 640 x 480 | 442 x 440 | 13.8 x 13.8 | 0.51 | (480, 35) | 138 x 101 at (482, 299) | 105, 254, 402 |
| `3:2` | 960 x 640 | 762 x 600 | 23.8 x 18.8 | 1.20 | (800, 35) | 138 x 261 at (802, 299) | 213, 468, 722 |
| `16:10` | 960 x 600 | 762 x 560 | 23.8 x 17.5 | 1.12 | (800, 35) | 138 x 221 at (802, 299) | 213, 468, 722 |
| `16:9` (the default) | 960 x 540 | 762 x 500 | 23.8 x 15.6 | 1 | (800, 35) | 138 x 161 at (802, 299) | 213, 468, 722 |
| `20:9` | 1200 x 540 | 1002 x 500 | 31.3 x 15.6 | 1.31 | (1040, 35) | 138 x 161 at (1042, 299) | 293, 628, 962 |
| `21:9` | 1260 x 540 | 1062 x 500 | 33.2 x 15.6 | 1.39 | (1100, 35) | 138 x 161 at (1102, 299) | 313, 668, 1022 |
| `32:9` | 1920 x 540 | 1722 x 500 | 53.8 x 15.6 | 2.26 | (1760, 35) | 138 x 161 at (1762, 299) | 533, 1108, 1682 |

The right panel and the minimap are pinned to the right edge (the canvas's extra width further right), the bottom strip and the chat log take the extra height, and the three score boxes are spread by the bottom strip's three cuts. `tests/test_app/test_screen_layout.cpp` pins every number above at every canvas (and that nothing leaves the screen), `test_wide_hud_frame.cpp` the frame pixel for pixel at each of them.

The shape can be changed **while a match runs**: in the browser the Picture dropdown under the game (and the one in the settings menu) switches at once, with no reload, no new connection and nothing asked; the simulation is not touched (a switched player's state hash after every tick is the one of a match that was never switched: `test_aspect_switch`). `Application::set_aspect` is the call. The world point in the middle of the view stays in the middle, the zoom that the player chose is kept and comes back where the new view offers it, and the pointer, the HUD and the screens follow. A native window keeps its size (the picture is fitted into it with bars, as for any window); nothing in the desktop game calls it yet.

- The view shows about twice the area of the world at once. At the zoom 1 everybody who plays the same aspect sees the same area. The zoom is each player's own choice (see Mouse-wheel zoom below).
- The frame is the original's own art, grown by repeating one line of its pieces: the top bar, the bottom strip and the left strip, and, in the right panel, the strip beside the map, the chat box and the right edge strip at one row (357, where all three are plain between their ant decorations). The clock box, the score boxes and the decorations are never stretched (`include/ants_app/shell_layout.hpp` lists the cuts). The bottom strip is widened at three plain cuts of its art, so that the three score boxes are spread evenly over it (they are laid out by slot).
- A map that is smaller than the view (a 16 x 16 map is 512 px wide) is centred in it with black around it, and the camera stays fixed on that axis. A click on the black does nothing at all.
- The edge strips of the scroll run along the edges of the whole picture. The minimap's view frame, the start view of a match and the sound listener follow the bigger view.
- Every screen outside a match is composed for the whole canvas from the original's own art, on the same clay and wide frame as the setup screen:
  - the loading screen and the quick help at the start: the page is centred in the frame, and START! is in the bottom right corner, where it keeps its place relative to the setup screen's START;
  - the results screen (see Results Screen below);
  - the desktop start menu: its controls untouched, the title at the top, the buttons centred, the hint and the server line at the bottom;
  - the setup screen and the room have their own 16:9 version with a map preview (see [`CONTROLS.md`](CONTROLS.md)).
  - in every canvas bigger than 960 x 540 (3:2, 16:10, 20:9, 21:9, 32:9) these pages are the 960 x 540 page, **centred in the canvas with black around it** (`CanvasLayout::page()`; the frame-rate plate and the version stand in the page's corner); the match itself fills the whole canvas. Pages composed for the other shapes are future work.
- The windows that open during a match (the options window, the quick help, the quit and alliance dialogs, "get ready") are centred over the map view, with the frame around them. The options window dims everything outside its card with the checker dither of its art: the whole picture, the frame and the panel included.
- The web page's own handling of the picture (its dropdown, its fullscreen) is in [`PLAY_IN_BROWSER.md`](PLAY_IN_BROWSER.md).

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
- **The limit depends on the picture's shape.** On TREASURE (1920 x 1920 world pixels), fully zoomed out:

  | Shape | Limit | World pixels shown at the limit | Share of the map |
  |---|---|---|---|
  | 4:3 (442 x 440) | 0.230 | 1920 x 1911 | nearly all |
  | 3:2 (762 x 600) | 0.397 | 1920 x 1512 | 79% |
  | 16:10 (762 x 560) | 0.397 | 1920 x 1411 | 73% |
  | 16:9 (762 x 500) | 0.397 | 1920 x 1260 | 66% |
  | 20:9 (1002 x 500) | 0.522 | 1920 x 958 | 50% |
  | 21:9 (1062 x 500) | 0.553 | 1920 x 904 | 47% |
  | 32:9 (1722 x 500) | 0.897 | 1920 x 557 | 29% |

  So a wide picture shows more at the zoom 1 and less at the limit: a player in the 4:3 picture can zoom out to nearly the whole of TREASURE, one in the 32:9 picture sees 29% of it at most (and 2.26 times the area of 16:9 at the zoom 1). Every player can choose any shape, and the zoom-out is free.

### The wheel, the middle button and where they act

- On the desktop the system's natural scrolling is undone (`SDL_MOUSEWHEEL_FLIPPED`), so the wheel rolled away zooms in whatever the setting is. In the browser the system's own setting applies (see [`CONTROLS.md`](CONTROLS.md)).
- A mouse notch is one step: an event worth a step or more moves the nearest whole number of steps and keeps no fraction (the web page's 120 at 100 a step is 1.2, so one step). A trackpad's small deltas add up. A pause of half a second forgets a fraction, a change of direction starts again, and a wheel event that is not a number is ignored.
- The wheel and the middle button act only over the map view (not over the panels). They do nothing during a rubber band, a pressed button, the minimap or the chat log's drag, nor with a dialog or the results open (options, quick help, quit, alliance, "get ready").
- On the setup screens they do nothing, the 16:9 screens of a room with their chat box included, with the chat input closed or open.
- In the browser the wheel and a trackpad's pinch (ctrl + wheel; Safari's gesture events) over the game canvas zoom the game and never scroll or zoom the page. The middle button over the canvas never starts the browser's autoscroll: the page cancels its press and its click, over the canvas only.
- **Typing in the match's chat goes on through the zoom**: the characters of a line (`+`, `-`, `=`) are text and never a zoom key; the wheel and the middle button change the zoom in the middle of a line and leave it as typed, and Enter sends it whole to the room (N5.70).

### The zoom is the player's own view

- **The picture's shape is the player's own view too, and nothing else.** No rule caps the view beyond the table of shapes: the widest picture there is, 32:9, shows at most 2.26 times the area of 16:9 at the zoom 1, and the zoom-out is free for everybody, so a wide picture sees more at the zoom 1 and less at the limit (above). The shape is in no recording and in no message of the network (the game server and its rooms never receive it: `--aspect`, `ants_set_aspect` and the settings key are read by the application only), and the simulation, the lock-step and every state hash never see it: the hash after every tick is the one of a match that was never switched (`tests/test_app/test_aspect_switch.cpp`, with bots too), and the golden hash tests do not change. **Which tiles a player can see is the simulation's fog of war, not the view's size** (a tile that is not explored is dithered and hides the enemy things whatever the shape); `tests/test_app/test_aspect_switch.cpp` also pins the numbers of the table above (tiles at the zoom 1, the share at the limit).
- **The zoom is the player's own view and nothing else.** The simulation, the network, the bots and every state hash never see it.
- **Every kind of match has the same levels**: a local game, a game with bots, a server's room, a LAN game (host or guest), and a match that the leader's START filled with bots.
- **With Fog of War on, every level draws exactly what the zoom 1 draws.** One world pass serves every level, with the same culling and the same gates: a tile that is not explored is dithered and hides the enemy ants, the food, the bombs, fire walls and power-ups, and the effects on it. The minimap respects the fog too (enemy ants, food, power-ups and fire walls only on explored cells). A test pins it at every level (`tests/test_app/test_zoom_view_renderer.cpp`).
- **What the zoom-out adds is breadth, not a new kind of information**: the types and states of the ants, planted bombs, effects and the health marks of every ant that is visible, over a wider area at once. Where those ants are was already on the minimap.
- The level is remembered: `zoom` in the settings file, `--zoom N` for any number from 0.05 to 2. A match starts at the nearest level that its map offers, and the remembered number is kept, so a small map's limit does not change what the next, larger map starts with.
- The map preview of the setup screens is always the zoom 1 picture, also on a room's screens when the game was started with `--zoom 0.5` or `--zoom 2`.

### How it is drawn

- At the zoom 1 the world is drawn straight into the view. At another zoom it is drawn at one texel per world pixel into an offscreen target that covers the world that the view shows, and copied into the view: on a hardware window, every zoom-out level (`zoom < 1`) blits the `1x` world texture straight to the window viewport with nearest-neighbor sampling (`SDL_ScaleModeNearest`) so fullscreen / high-DPI windows keep full native resolution and crisp pixel art; on the software renderer (and from 1 up to 2) the target is copied into the lattice of whole screen pixels (nearest from 1 to 2, the exact average of 2 x 2 world pixels at each halving below 1 on the software renderer).
- The origin of the view lies on a grid of one screen pixel at the zoom, so a scroll moves the picture and never makes it shimmer.
- Everything else on the screen is drawn afterwards at its own size: the frame, the cursor, the rubber band and the health marks (a bar scales with the zoom, a number is one size at every zoom, as in the original: [Ant health marks](#ant-health-marks)).
- These follow the world that is seen: the edge scroll moves the same distance on the screen at every zoom (in screen pixels), the arrow cursors follow what the view can still do, the minimap's frame is the world that is seen and a press on the minimap centres it, and so do Ctrl+N / Ctrl+P, the start view (the hill stays in view at every zoom) and the sound listener.
- **The zoom drawn is always the zoom the game says.** Every frame holds the camera inside the levels that the map offers (never below its limit). When the renderer cannot make the offscreen target, the zoom drops to 1 at the next frame and only 1 is offered. One line on stderr says so, once, when the failure begins. The levels come back when the target can be made again.
- The API that other input uses (a touch screen's pinch calls it): `Application::set_zoom(level, anchor)`, `step_zoom(direction, anchor)`, `zoom()`, `zoom_levels()` and `view_zoom_allowed(x, y)`.

## Touch controls (phones and tablets)

The gestures and their numbers are in [`TOUCH.md`](TOUCH.md#the-gestures-and-why-these-numbers), and the controls table is in [`CONTROLS.md`](CONTROLS.md#touch-controls).

## The HUD

- The frame and the right panel are the original's art, one animation (`uishell`, [`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.34).
- Besides the clock, the score boxes, the minimap and the chat, the HUD shows the card under the minimap with the two command pedestals (see [`CONTROLS.md`](CONTROLS.md)).
- On your own hill the egg count is an egg tray, `egg1` to `egg9` (ten or more eggs show nine).
- Every selected ant wears health-colored selection ears: `dogears` at 9 or more hit points, `yelears` at 3 to 8, `redears` at 2 or fewer. The health bars over the ants use the same colors.
- The recessed status box (`wstatus.bmp`) sits under the card. The chat log and the chat box are below it.

## Ant health marks

A bar over an ant, or a number at its lower right, says how many of its hit points are left. Every ant has 10 at most (a power-up changes the type, not the hit points), so a hurt ant is one with fewer than 10. The number is the original's own: its **Ctrl+L** prints every ant's hit points as white numbers (and starts with them off). The bar is an addition, a drawing aid that changes nothing of the game. It is set in the [settings menu](GAME_MENU.md) (Video: **Ant health**, Hurt ants (the default), All ants or Off; **Show health as**, Bar (the default) or Number) and **Ctrl+L** flips Off and the last choice that was on ([`CONTROLS.md`](CONTROLS.md)). Everybody has it, in a match, a recording and a live watch alike (the one renderer).

- **Who:** every ant that is drawn: own, allied and enemy, as the numbers always were. With Fog of War an enemy ant on unexplored ground is not drawn and has no mark. A frozen ant has none (the display loop skips it). Hurt ants is the ants with fewer than their own most hit points; All ants is every ant. A dying ant has no bar (no hit points are left) but keeps its **0** as a number, as in the original ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.14).
- **Number (the original's):** `sprintf("%d", hp)` in white, in the fixed system font of the original (the raster "Fixedsys": a cell of 8 x 15 pixels per character, glyphs without antialiasing, no outline, no color), with the top left of the text at the ant's sprite position (the one its ears follow), so the digits stand at the lower right of that point. It is drawn right after its ant, in the ant's place in the draw order (a nearer ant can cover it). The original has no zoom: here the number keeps its one size at every zoom, and at a zoom it is drawn after the copy at the screen position of the sprite, as it always was before v0.12.134. On a canvas that is shown small (picture pixels for a CSS pixel, as for the thick bars: 1.25 or more, which is a phone, and a tablet at the edge) the digits are drawn `ceil(0.8 x picture pixels per CSS pixel)` times as big (x3 on a phone held upright, x2 on its side, glyph about 8 CSS pixels tall or more), still white and with their top left at the sprite position, plus a one-pixel black outline (scaled with them). Below 1.25, a desktop window included (the density is the window's size in points, so a HiDPI screen at 200 percent is still 1.0), nothing changes: the original's digits, no outline, pixel for pixel.
- **Bar:** 22 world pixels wide and 3 high at the zoom 1, in a black frame, its lower edge 31 pixels over the ant; the colored part is the hit points out of 10 of a dark track. It follows the zoom (5 high from the zoom 1.5, 2 and without a frame below 0.6, 22 times the zoom wide) and is one and a half times as thick on a canvas that the page shows at less than half its size (a phone held upright).
- **Bar colors:** the selection ears': 9 and 10 hit points green, 3 to 8 amber, 1 and 2 red.
- The bars are drawn once the world is on the screen (after the copy at a zoom), over everything of the world, for the ants in view only. The code is `health_marks.hpp` (the rule and the bar's sizes, no SDL) and `renderer_health.cpp`; they read the hit points that every client already has, so the simulation, the recordings, the hashes and the network are untouched.

## Minimap

The 119 x 91 image is painted the way the original's painter does it ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.55):

- Square maps (`cols == rows`) fill the full 119 x 91 slot at `(480 + dx, 35)`, identical to 1998. Non-square maps (`cols != rows`, `ScreenLayout::minimap_for_map`) adapt to the map's aspect ratio inside the slot: wide maps (`cols > rows`) use a top-aligned `119 x mh` rect (`mh = clamp(round(119 * rows / cols), 1, 91)`), tall maps (`cols < rows`) use a horizontally centered `mw x 91` rect (`mw = clamp(round(91 * cols / rows), 1, 119)`), and the surrounding `uishell` recessed bevel (`x458y22.bmp`, `x458y35.bmp`, `x599y35.bmp`, `x480y126.bmp`) hugs the resized minimap with HUD panel texture filling the unused strip.
- Every pixel shows the object of the cell under it, in the color of the original's table. A tile that is not in the table gets palette color 0.
- Bombs show the terrain. In fog only power-ups, food and fire walls fall back to the fog color, while rocks, toys, bridges and every colony's hill keep theirs. Fire walls show as yellow. There are no dots per object cell.
- The dots are rectangles on top of the image: flowers and clovers (3 x 3 on a 60 x 60 map) and the ants, each at its pixel position (2 x 2 on a 60 x 60 map, 3 x 2 on 31 x 31). Every ant has its dot until it is removed, dying and drowning ants too. In fog your own ants and your ally's show, enemies only on explored ground.
- The view frame is (251, 251, 255). Its size is the world that the view shows (442 x 440 in the 4:3 picture, 762 x 500 in the 16:9 one, divided by the zoom) divided by the scale (world pixels per image pixel), plus one pixel. It stands at the view's origin divided by the scale and is kept inside the image. On an axis where the view is bigger than the map, it is the whole image.
- The original's flash of the dot of an own or allied ant that was hit in the last 5 seconds is not ported.

## Score Boxes

The four score boxes are the original's ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.53):

- The local team's box is in the top bar. The others are in the three bottom slots, in team-index order.
- An allied team's box is half its color and half its ally's color, and shows the two scores added.
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
- The chat log keeps the original's entries: a header in the sender's team color ("Name:" or "Name (To Teammate):", "[m:ss] News Flash:" for news) and a body of up to 100 characters, wrapped by pixels and set 10 px in (see Chat Log Window).
- Chat needs the "Participate In Chat" option. F9 - F12 chat the quick-chat texts to everybody. A team message reaches only the sender and the sender's allies.

## The countdown card

A match of a server's room with two or more people holds its first turn for 30 s (network protocol 21, [`MATCH_COUNTDOWN.md`](MATCH_COUNTDOWN.md)), and the "Get ready to play!" dialog is a card of the same look for that long: the original's clay orange inside its thin green frame, in the middle of the map view (`include/ants_app/countdown_card.hpp`, 400 x 346 px, in the picture's own pixels whatever its shape).

- **From the top:** `Get ready to play!` (35 px) and `You are the Blue Ants.` (20 px), a black box with the count in 96 px gold digits (the seconds left, rounded up; `0` and "starting..." until the first turn arrives) and "seconds to the start", the four colors in two rows of two in the order of the seats (a dot in the color, the name, `in` for a person, `bot` for a computer player named like "Pip (bot, Hard)"; your own is "(you)"), the line "Everybody is in. Say hello in the chat." ("The match starts soon." when Participate In Chat is off), and the Fullscreen button (the native game draws it, the web page lays its own over the place, `HUD::countdown_button_rect`).
- **The chat is open.** The HUD's chat box, its line and the All / Team buttons work: the typed characters (text input; the key events of printable keys are left out, or every letter would be typed twice) and Backspace edit the line, Enter sends it (to the team when you have an ally, else to everybody), F9 - F12 send the quick chat texts, and the lines of the others come into the log. The status line says "Match starts in N s" every second, above the chat box.
- **Everything else stays out of reach**, as in the 5 s dialog: a click on the map or the pedestals, the keys of the match (F1, Esc, Ctrl+A and the others) and the zoom do nothing. The Quit button and Ctrl+Q ask to leave (the quit dialog is drawn in place of the card; N keeps you here).
- The card closes with the first executed turn, or for good when a seat is lost and the match is held (the pause overlay and the vote take its place). The 5 s dialog of a match with one person, a game on the local network and a game on this computer is as it was.

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
- The 16:9 page is recomposed for the whole canvas from the original's own pieces. The Leave button moves 320 px right, the middle of the page moves 30 px down (top row at y = 265, the others at 50 i + 303), both black boxes are 320 columns wider, and the three narrow columns of `newstats.bmp` shift left by 84 / 56 / 28 px into the open area so every column fits 5 digits (numbers at x = 721 / 770 / 819 / 868, widths 49 / 47 / 49 / 48; 187 mode keeps its three headings at x = 805 / 854 / 896). The numbers are drawn in the original's 8 px digits.

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
- The health numbers are digits of the original's fixed system font (an 8 x 15 pixel cell), white, as the original prints them ([Ant health marks](#ant-health-marks)).

### Sound and music

- A 32-channel software mixer plays the sound clips with the original's sound law ([`GAME_REVERSE_ENGINEERING.md`](GAME_REVERSE_ENGINEERING.md) 5.24c): Chebyshev distance over 2500 px, a pan that quiets only the far channel, DirectSound hundredths of a dB, and the Sound Volume option inside every sound. The listener is the middle of the picture, so a zoom or a scroll moves it: the gains of the sounds that play then glide to the new law in 12 ms (`AUDIO_GAIN_RAMP_MS`) instead of stepping, which is a click. Which sounds start, and when, never depends on the view (`test_replay_app` RA9).
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
