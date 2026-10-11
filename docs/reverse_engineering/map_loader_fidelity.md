# The map loader as the original's: what v0.12.10 changed

Where the remake's level loader (`Grid::init_from_level`, the level reader, the renderer's copy of the hill search) decided by a dictionary NAME, by a guess or by its own rule, it now does what the original does, read from the program (the tile-info tables, the loader `FUN_01006349` and its helpers, `SetTile` `FUN_01007352`). The original was read, never run. Each fix is its own commit with its own test (`tests/test_sim/test_loader_fidelity.cpp`, suite 2.9.4) and was swept over the 558 community maps (`tools/map_sweep`, 2,400 ticks, two rosters, three runs each: no crash, no hang, every run deterministic).

## Numbers

Every fix below changes how a level is read, so by the rule next to `kProtocolVersion` the network protocol is 19 (nothing on the wire changed) and the replay rules numbers are 5 (the original's game) and 6 (game mode 187); recordings of rules 1 to 4 are an earlier version's ([REPLAYS.md](../REPLAYS.md#the-file)). One bump covers all the fixes.
