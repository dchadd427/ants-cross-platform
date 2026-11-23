---
name: ants-map-layout
description: How to lay out a fair and fun Ants map for two to four colors - where to put hills and starting ants, how far apart, how to share food, where chokepoints and power-ups go, how many ants and eggs, how long a match should be - with numbers measured from the six original maps. Use it when you decide where things go, before you call put_hill, put_ants, put_food or place power-ups.
---

# Layout: fair, fun, and measured

Ants is a race for food and a fight at the meeting points. A good map gives every color the same chances, makes the colors meet at interesting places, and has something to gain at each meeting. The numbers below were **measured from the six maps that came with the game** (their files are in the Map Maker's library: `TINY`, `SMALL`, `MEDIUM`, `GAUNTLET`, `ISLANDS`, `TREASURE`). Use them as a guide, not as a law.

## The six original maps

<!-- generated:originals begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Map | Size | Minutes | Eggs | Ants for a team | Hill entrance to hill entrance, in cells (rows or columns, the larger) | Ground that matches its best mirror or turn | Ground |
|---|---|---|---|---|---|---|---|
| TINY | 31 x 31 | 6 | 3 | 3 | 19 | 100% | gravel 60%, mud 29%, dirt 7%, slate 4% |
| SMALL | 40 x 40 | 8 | 2 | 4 | 27 | 83% | gravel 75%, dirt 8%, slate 7%, water 7%, mud 3% |
| MEDIUM | 60 x 60 | 10 | 6 | 6 | 19 to 38 | 78% | gravel 65%, water 12%, mud 10%, dirt 9%, slate 5% |
| GAUNTLET | 60 x 60 | 10 | 6 | 6 | 9 to 23 | 95% | gravel 52%, water 23%, mud 13%, dirt 12% |
| ISLANDS | 60 x 60 | 12 | 4 | 8 | 13 to 14 | 81% | gravel 48%, water 33%, mud 15%, dirt 2%, slate 2% |
| TREASURE | 60 x 60 | 12 | 9 | 6 | 14 to 15 | 95% | gravel 89%, mud 7%, water 3% |

| Map | Food piles (all the points) | Nearest pile to a hill, in cells | Piles about as near a second hill as the nearest (within 4 cells) | Power-ups placed | Flower droppers |
|---|---|---|---|---|---|
| TINY | 14 (4,800 points) | 6 to 7 | 10 | 0 | 0 |
| SMALL | 5 (4,000 points) | 11 to 15 | 4 | 2 | 2 |
| MEDIUM | 4 (4,900 points) | 18 to 21 | 4 | 0 | 1 |
| GAUNTLET | 2 (1,800 points) | 32 to 36 | 2 | 10 | 1 |
| ISLANDS | 10 (5,600 points) | 10 to 11 | 10 | 40 | 2 |
| TREASURE | 18 (10,950 points) | 5 to 7 | 7 | 20 | 0 |
<!-- generated:originals end -->

"Hill entrance to hill entrance" is the larger of the row difference and the column difference between two hills' entrances. "Ground that matches its best mirror or turn" is the share of cells whose ground kind is the same as the kind at the mirrored (left-right, top-bottom, half-turn, or across the diagonal) cell.

## What the originals teach

- **Everything is equal for every color.** Each color starts with the same number of ants, and the file has one egg number for all four colors. A color's hill has the same kind of ground, space and food around it as the others'.
- **More space, more ants.** 3 ants a color on 31 x 31, 4 on 40 x 40, 6 on 60 x 60 (8 on the islands map). Eggs run from 2 to 9 and matches from 6 to 12 minutes: shorter for small maps.
- **The fair maps are nearly symmetric.** TINY is symmetric under every mirror and turn; TREASURE nearly. Symmetry is the easiest way to be fair. The races and the island maps are less symmetric but balance the distances.
- **Hills are far enough apart to need a walk.** The distance between neighboring hills is about 0.6 to 0.7 of the map's side on the corner maps (TINY, SMALL), and about a quarter of it on the crowded ones (ISLANDS, TREASURE). An ant takes about 0.4 seconds a tile on open ground and about 0.9 on mud, so 20 cells is 8 seconds.
- **Ants start a few cells from their hill.** The nearest starting ant is 2 to 5 cells from the entrance.
- **Food is the point.** The originals have 2 to 18 piles with 1,800 to 10,950 points in all, piles of 10 to 100 bites worth 10 to 50 points a bite. TINY and TREASURE put a pile only 5 to 7 cells from every hill, so the first minute is gathering. The maps with piles far from the hills (GAUNTLET: 32 to 36 cells) are races.
- **Contested food makes the fights.** Many piles are about as near a second hill as the nearest one (the second table). Put some piles between neighbors, the same distance from both.
- **Power-ups are scarce or in sets.** None on TINY, 2 on SMALL, 20 on TREASURE (four of each of the five kinds, placed symmetrically), 40 on ISLANDS (mostly along the borders). A map with droppers has one or two flowers.

## Choosing a plan

Pick one of these shapes, then adjust to what the person wants.

- **Four corners** (like TINY and SMALL). Hills near the four corners, a field in the middle, the contested food in the middle and along the edges. Long walks, big fights in the center.
- **Close quarters** (like TREASURE). Hills in a tight square near the center, food in a ring round them, rocks and plants making alleys. Fast, crowded games. Needs many piles.
- **Islands** (like ISLANDS). Each hill on its own island of ground, water between, bridges or swimmer power-ups to cross. Put the swimmer power-ups on the islands' shores.
- **Race** (like GAUNTLET). Hills in an arc, food and power-ups far away in one place; a long fight for one prize. Give it few piles and make them rich.
- **Diamond.** Hills on the four sides (top, left, right, bottom) instead of the corners, a lake in the middle with bridges.

Always give **all four colors** a hill and ants, even for two players.

## Placing hills

- The hill is 4 x 4 cells with its entrance in the second row and second column. Name the **entrance** in `put_hill`. It must not be on the first row or column or in the last two.
- **Mirroring a hill exactly.** On a map `size` cells across, a hill with its entrance at `e` is mirrored (left-right or top-bottom) by a hill with its entrance at `size - 2 - e`. TINY is built so (5 and 24 on 31). Use it for symmetric maps: the same formula for rows and columns.
- Keep a hill's four sides open: ground (not water or mud) for at least three cells round it, and exits on more than one side.
- Put hills on the same kind of ground. Do not put a hill beside a lake.

## Placing ants

`put_ants` with a cell about 3 to 5 cells from the entrance, on the side toward the middle of the map, with the count for the map's size (3 for 31 x 31, 4 for 40 x 40, 6 for 60 x 60). The ants go on that cell and the nearest free cells. One clump a color is the original's way; use the same count for every color.

## Placing food

1. **A safe pile for each color**, 5 to 7 cells from its hill (3 or 4 stages, about 20 to 30 bites, 25 points a bite). It lets each color start fast and equally.
2. **Contested piles**: equal distance from two neighbors. On a symmetric map put them on the mirror line between two hills.
3. **A prize**: one rich pile (5 or 6 stages, 60 to 100 bites) where all colors can come, or one for each pair of neighbors.
4. Totals: the more piles, the faster the game. A 60 x 60 map with 8 to 12 piles and 4,000 to 7,000 points in all is the middle of the originals.
5. Mirror every pile on a symmetric map (the same formula as for hills, applied to the pile's cell).
6. Never on water, never on a hill, never closed in by rocks. Check with the region text.

## Chokepoints and rocks

- Lakes and mud bands make **chokepoints** (see `ants-map-terrain`): two or three crossings between two areas is a good count. Mark the middle of each with food or a power-up so that something happens there.
- Rocks and bigger objects block ants. A line of rocks with two gaps makes an alley; **leave the gaps at least two cells wide** so that groups of ants can pass.
- Plants (clover, flowers) and grass tufts are scenery on open ground; use them to make the map look alive, but not as walls.
- Do not close a hill in. A hill with one narrow exit decides the game.

## Power-ups and droppers

- Put power-ups where a choice is made: at a crossing, beside a bridge, at the edge of the middle. Swimmer power-ups beside a lake make the lake crossable by whoever gets one; bomber ones belong at chokepoints.
- Place them in symmetric sets (one of each kind for each color, or two of each kind in the middle). Do not put one right beside a single hill.
- An ant takes a power-up by ending its walk on it, so a power-up on a one-cell path can be won by whoever is first.
- A flower that **drops** power-ups (`put_plant` with `dropper`, seconds between drops, 8 to 60 in the originals) makes a fight over the cell below it. One or two droppers is enough. The kinds that fall are chosen at random with equal chances.

## Before you call `check_map`

- Every color has a hill, the same number of ants, and a safe pile.
- The hills are far enough apart, and the walk to the food is about equal for every color.
- No hill, ant or pile is walled in. No one-cell strips of ground.
- Minutes and eggs match the size (see the table).
- `ants-map-check-and-fix` says how to check what the checker does not.

The worked call sequence for a four-corner map is in `reference/recipes.md`.
