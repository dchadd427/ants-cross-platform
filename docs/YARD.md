# The Yard (the game as a picture of the map alone)

The Yard is the shared meadow that Home will show under its top bar ([`HOME_SCREEN.md`](HOME_SCREEN.md)): visitors steer a few ants over one big map with the game's own look and controls. This page describes the **client**: the game program run as the Yard. The server that sends the ants and the wire that carries them are not part of it yet; until they are, a batch of ants comes in through `Application::yard_apply` (the tests make theirs by hand). Nothing of the Yard is reachable from a page of the site, and the game that is played is the one it always was.

## What it is

The real game (HUD, renderer, pointer, touch and order code, run as they are) with three changes, each behind one switch:

- **A picture of the map alone.** `ScreenLayout::map_only(w, h)` (`screen_layout.hpp`) is a layout in which the whole canvas, from 320 x 240 to 4,096 on a side (a phone held upright is 390 x 600), is the map view and the minimap is a small slot in the top right corner, 14 px from the edges. There is no panel, no top bar, no score, no status line, no chat and no frame-rate plate. Only that function makes the flag `ScreenLayout::yard`; every other layout has it false and gives what it always gave.
- **A world that is never ticked.** The engine is loaded with the Yard's map and never runs: its ants are a pool of ordinary units whose fields a batch of the server rewrites (`YardPuppet`). The picture, the picking and the cursor read the engine's world state as they do in a game.
- **Orders that leave as intents.** The HUD gives its commands to a sink (`YardSink`) that turns them into intents for the server.

The Yard has no hill of its own: the hills are the places with an open game (up to eight, the game's own hill art, each with its own number so that a click puts the game's brackets on that one).

## Running it

`--yard` (`ApplicationConfig::yard`) starts the game as the Yard at once: `--map FILE` is the Yard's map (it needs no start marker and no hill; the hills, markers and ants that a map of the game has are ignored), `--window-size WxH` the canvas (960 x 540 without it). It cannot be combined with `--host`, `--join`, `--join-url`, `--bot`, `--alone`, `--play`, `--replay` or `--game-mode`. A running game is put into the Yard by `Application::start_yard(w, h, scale)`, and `set_yard_size(w, h, scale)` follows the page's box while it runs. The window is the canvas times the whole number `scale` (1 to 3): SDL then scales the canvas by a whole number, and no seam or bar shows between the tiles.

In the Yard the game opens no audio device and starts no music, shows no start dialog, has the chat option off (the option is not written: the Yard never changes the game's stored settings, not the zoom either) and starts at the normal scale whatever the game remembers.

For the page (web build only), three exports with numbers only: `ants_yard_set_size(w, h, scale)`, `ants_yard_probe(what)` (the canvas, the scale, the ants shown, the orders waiting, the zoom, the middle of the view; `Application::yard_value`) and `ants_yard_show_own()` (the view goes to the middle of the visitor's own ants). The batches, the places and the orders pass through `yard_apply`, `yard_set_hills` and `yard_take_intents` of the `Application`; their wire is the codec's chapter.

## The HUD in the Yard

The places in the HUD that know the Yard are four, each one line behind `layout_.yard` (`hud.cpp`), and the rest is in `hud_yard.cpp`:

| Where | What the Yard does |
|---|---|
| `apply_layout` | the buttons of the top bar, the pedestals, the chat and the quit dialog are given no size: nothing that is not drawn can be hit |
| `render` | the minimap and, while the left button drags, the rubber band; nothing else |
| `handle_key_down` | the keys are Esc (deselect) and Ctrl (or Cmd) with A (all mine), N and P (next, previous ant), S (stop); every other key is nothing: no chat, no help, no options, no quit, no home hill |
| `input_tick` | the view moves at the edge of the picture only after the pointer has stayed where it scrolls for 250 ms (the Yard is part of a page, and the pointer passes its edge on its way to the page's own controls); the minimap's drag has no wait |

Click, drag box, right click (move, attack), the order ring, the cursors, the brackets, the wheel and middle button, and the fingers (tap, one-finger box, hold as a right click, two-finger pan and pinch) are the game's own code. The zoom stops one notch out of the normal scale, at 0.84 (the level of the game's series nearest to 0.8); the server sends at most 120 other ants in a batch on that assumption (`ants_yard/yard_types.hpp`). The game's own zoom is unchanged.

## The world

`include/ants_yard/yard_types.hpp` holds the data that the client and the server both read: `AntRecord` (the server's id, color 0 for the visitor's own and 1 to 3 for the others, the clip with its mirror flag, the frame, the milliseconds to the end of the frame, the next clip, the position, flags), `Hill`, `Batch` (a tick and its records) and `Intent`.

`YardPuppet` keeps 160 slots, the ids of units that are removed until a batch uses them. An ant that is in a batch keeps its slot, a new one takes the next free slot (the search goes round the pool, so a slot that was freed is the last to be used again), the others are removed. What the server sends is checked: a record with no id, a color that is none, a clip that is not one of the ants' clips of the movement tables, or an id that has come already is left out; a place off the map is the map's edge; a frame past the end of a clip is its last frame; more ants than slots push out the others', never the visitor's own. An idle clip is shown as the idle state (the renderer repeats it between ticks) and every other clip as not; every ant is shown whole (the client does not know a health). Only public calls of the engine are used (`spawn_unit`, `get_unit`, `grid_mut`, `set_fog_of_war_enabled` as the nudge that marks the world state stale); `src/ants_sim` and `include/ants_sim` are not changed.

The HUD refers to ants by slot. A slot that the HUD's selection still names is never given to another ant, so a selection never turns into a stranger when an ant dies or goes out of view and another takes its place; the application hands the selection to the puppet with every batch.

The world is drawn as the last batch left it. Moving the ants between two batches is the clip player's work, which comes with the codec.

## The orders

`YardSink` takes the HUD's commands. A group move, a group attack and a stop of the visitor's own ants become intents with the server's ids; a command of any other kind (a special, a hatch, an alliance, a quit) is refused. The sink leaves out every ant that is not the visitor's own and any ant that is named twice, keeps at most six (the HUD sends the nearest first), refuses a tile off the map, and sends an attack only when another visitor's ant still stands on the tile, naming that ant. It answers as the engine would, so the voice, the status text and the order ring still happen. At most 64 intents wait; the oldest is dropped when more come.

## The normal game stays as it is

`src/ants_sim` and `include/ants_sim` have no change. The pinned suites that were there before (the screen layout, the HUD's layout, the view fingerprints, the wide HUD, the pointer model, the zoom, the touch and replay applications, the render parity, the movement golden, the engine copy, the map round trip) run unchanged, and a frame of the game with its HUD, a drag box and a right click is the same bytes before and after the change. `test_yard_isolation` pins what the four places above do in the original's picture and in the 16:9 one, as numbers that were made before the Yard came in (76 of them: `HUD::render`'s draw calls in twelve states, the rectangles of `apply_layout`, the HUD and its frame after each of fifteen keys, and `input_tick` at every edge, in the middle and on the minimap).

## Tests

- `test_yard` (suite 3.33): the picture's geometry; the puppet (the fields of a batch, the slots, a slot that is pointed at, what a server may send, the pool's limits, the places); the sink; the HUD in the Yard's layout (the buttons, every key, what is drawn, the edge dwell, the selection, the pointer model); the application (the flag and what it refuses, the start, five shapes of box and the window's scale, a frame that is the map and nothing else, the mouse, the keys and the fingers through the real event loop, the zoom, the edge scroll, the page's calls, the settings that stay as they were, and a game that is not the Yard refusing every call).
- `test_yard_isolation` (suite 3.32): above.
- `tests/data/yard_mutants.json` (`tools/mutate.py tests/data/yard_mutants.json --keep-going --jobs 1`): each gate removed or turned on for the game's layouts, and the puppet's and the sink's rules broken one by one; every mutant must be caught.
