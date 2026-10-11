# The map loader as the original's: what v0.12.10 changed

Where the remake's level loader (`Grid::init_from_level`, the level reader, the renderer's copy of the hill search) decided by a dictionary NAME, by a guess or by its own rule, it now does what the original does, read from the program (the tile-info tables, the loader `FUN_01006349` and its helpers, `SetTile` `FUN_01007352`). The original was read, never run. Each fix is its own commit with its own test (`tests/test_sim/test_loader_fidelity.cpp`, suite 2.9.4) and was swept over the 558 community maps (`tools/map_sweep`, 2,400 ticks, two rosters, three runs each: no crash, no hang, every run deterministic).

## Numbers

Every fix below changes how a level is read, so by the rule next to `kProtocolVersion` the network protocol is 19 (nothing on the wire changed) and the replay rules numbers are 5 (the original's game) and 6 (game mode 187); recordings of rules 1 to 4 are an earlier version's ([REPLAYS.md](../REPLAYS.md#the-file)). One bump covers all the fixes.

## 1. Power-ups, food, fire walls and lunchboxes are solid when the file's solid bit says so

**The original.** A cell blocks by its layer-1 solid bit (`FUN_0100cf0f`), which the loader copies from the file (`FUN_010069d8`). A power-up, a food pile, a fire wall or a lunchbox is not solid by being one: `SetTile` (`FUN_01007352` -> `FUN_01007a22`) copies the tile flag 0x01 of the id to the bit when it places the object, and clearing the cell (`FUN_0100744f` -> `FUN_0100660c(row, col, 0)`) clears it again, row 0 never. A power-up that a file leaves on a cell without the bit is ordinary ground: paths cross it, ants stand on it, nothing happens (a power-up is taken only by the arrival of a move or power-up order whose tile it is). The exceptions of `CanEnter` and of the path step cost (the power-up order onto that tile, a harvest, a fire ant on fire) are evaluated only inside the `solid != 0` branch.

**The remake did.** `Grid::is_solid_object` counted every power-up, pile, lunchbox, fire wall and placeholder as solid by itself, and `init_from_level` cleared their file bits. `POPcOrN.lvl` (all 32 power-ups without the bit) therefore had its corridors plugged.

**Now.** The file's bit is kept and is the only source for a level read from a file. What the game places sets it and what it removes clears it, as `SetTile` does: `place_powerup`, `place_firewall`, `set_fire_at`, the flower dropper's power-up, `set_food_tile` (the footprint of every stage tile, so also the lunchbox), `set_layer2` (the placeholder 160, bridge stages, empty) and `place_bomb` (replacing the placeholder). A world made by hand (`init_empty`: tests and tools) has no file bits and keeps counting these objects as solid themselves. The cells under a pile's footprint also stop being power-ups (they kept the flag while holding the pile's tile).

**Maps.** Power-ups without the bit: 3,132 cells in 94 maps; food: 183 cells in 36; fire walls: 439 in 2 (`LF1.6`). Sweep of the 558 maps (loaded state, and 2,400 ticks with the sweep's scripted players, rosters 15 and 9): the walkability of the map at load changes in 106 maps; the play changes in 101 (the state hash of the other 451 only because it now holds the bit that was cleared before; the six shipped maps play exactly as before, `LF1.7`); no crash, no hang, every run deterministic.
