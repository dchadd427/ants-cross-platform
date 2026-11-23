---
name: ants-map-terrain
description: How to shape the ground of an Ants map so that it looks right and plays right - the five kinds of ground, hard edges and edge pieces (fix_edges, auto_edges), the ring of mud beside water, strips that must be two cells wide, bridges, and natural shorelines. Use it before you paint lakes, mud, paths or any change of ground, and when the edges look wrong.
---

# Terrain: ground that looks right and plays right

## The five kinds

Gravel, dirt, slate, mud and water. Gravel is the brown-gray ground that covers most maps; dirt is cream; slate is blue-gray stone; mud is dark gray; water is blue. The plain tile of each kind and the kinds that have edge pieces toward each other are in `ants-map-vocabulary` (also from `list_tiles` with `group: "ground"`). Paint a kind by its plain tile.

How they play: **mud is slow** (a tile takes about 900 ms; the other ground 320 to 480 ms, in the game's own table of paces); **water cannot be walked on** by ordinary ants, only swimmer ants cross it. A bridge piece on water gives everyone a way over, at the pace of mud. Solid objects (rocks and many bigger objects) block ants as water does. So a lake, a mud band or a wall of rocks is a **chokepoint**: it decides where ants meet.

## The game does not blend

Every cell is drawn by its own tile. Two kinds painted side by side with their plain tiles meet in a hard, square edge. The game has **edge pieces** (tiles that fade one kind into another along one side or around a corner) and the six original maps use them everywhere. Two ways to get them:

- `paint` with `auto_edges: true`: each stroke sets the edge pieces round what it painted. For a lake it also puts the ring of mud (below).
- `fix_edges` (whole map, or a `rect`): picks the pieces for every boundary in the area, as the Map Maker's own Fix edges button does. Call it after you finish painting an area. It is safe to call again: a map that is fixed is left as it is, and it never removes a piece that someone placed by hand.

Do not paint edge pieces yourself unless you know exactly which one you need: `fix_edges` knows the rules.

## What `fix_edges` knows

- At every boundary **only one of the two cells wears the piece**, as in the original maps. The kind that wears it is the higher of the pair in this order: water, gravel, slate, dirt, mud.
- Only pairs that the game has pieces for get one. **Water has edge pieces toward mud only.** Gravel, dirt and slate have none toward water. So water straight against gravel, dirt or slate stays a hard cut.
- Where the higher kind cannot wear a piece (a strip of water one cell wide in mud, a lone cell, a notch with the other kind on three sides), the **lower** kind beside it wears the pieces instead.
- It looks at the four sides of a cell and at corners where two sides meet. A cell with the other kind on **opposite** sides has no piece in the game.

## The ring of mud beside water

The original maps never put water against gravel, dirt or slate: there is mud between, one cell of it at about 85 boundaries in 100 (in the six original maps; two or more cells make a bank). With `auto_edges: true`, a stroke of water turns the gravel, dirt and slate cells that touch the new water into plain mud and then sets the pieces. One `undo` takes the lake and its ring away together.

If you paint water without `auto_edges`, add the mud yourself: a ring one cell wide round the water (`paint` mud with a `rect` slightly larger than the lake, then the lake on top), and call `fix_edges`.

## Strips must be two cells wide

The game has no edge piece for a cell that has the other kind on opposite sides. A strip **one cell wide** beside another one-cell strip (strip, gap, strip) cannot be drawn with pieces and ends in a straight cut. Make strips, paths and rivers **at least two cells wide**. This holds for rivers of water in mud, for paths of dirt across gravel, and for gaps between lakes. The Map Maker marks such places and says "No piece in the game for a strip one cell wide: make it two wide".

## Natural shorelines

A rectangle of water looks like a swimming pool. Real shores wander.

1. Paint the lake as one rectangle first, with `auto_edges`.
2. Then add and take away blobs along the edge: a second, offset `rect` of water on one side, a notch of mud on another. Offset by one to three cells; vary the sizes.
3. Round the corners: a staircase of two or three steps is better than a right angle. Work in a few `rect` calls, not in a hundred single cells.
4. Call `fix_edges` on the area when you are done and look at it with `get_picture`.

Keep lakes at least about four cells across, and keep the ground between two lakes at least two cells wide, or ants will not be able to use it.

## Bridges

A finished bridge piece is the object tile in `ants-map-vocabulary` ("finished bridge piece"). Put a **line of them across water**, one on each water cell, with `paint` (`layer: "objects"`, `cells` or a `line`). A bridge piece on land does nothing useful. In a map file a bridge has no timer: it stays all match. Swimmer ants can also build and demolish bridges in play, so a bridge on a map is a fair, permanent crossing but not the only one.

Make a bridge two cells wide if you can: it gives two ants room to pass, and the strip rule above applies to the water on both sides.

## Paths, mud and chokepoints

- Use **mud** to slow a route (a swamp in front of a hill; a muddy ford). Use **dirt** or **slate** to mark a road or a place: they read as a different part of the map.
- Keep every hill connected to the rest of the map by open ground at least two cells wide, on more than one side. A hill with one narrow exit decides the game.
- Water in the middle with mud banks makes the classic "everyone goes round" map; add one or two bridges to give the choice.
- Do not wall off food. A pile that ants cannot reach is not a prize. See `ants-map-check-and-fix` for how to check that.

## Order of work

1. The big shapes: lakes, a mud band, the main roads (`rect` and `line`).
2. Break the shapes: offsets and notches.
3. `fix_edges` on the area.
4. Bridges and rocks last, so that you can see where the crossings go.
5. `get_picture` (or `get_region`) and look for one-cell strips and hard edges.
