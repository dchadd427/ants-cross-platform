---
name: ants-map-vocabulary
description: The words and numbers of Ants maps: the four colors, the five kinds of ground and their tile ids, objects, bigger objects, plants, power-ups, food items, ant types, the rules word, and the limits of Classic and Extended maps. Use it when you need a tile id, a name, or a size limit, or to understand what a tile in a map is.
---

# The vocabulary of an Ants map

Every number below is read from the program's own source when this skill is made, so it is the same as the Map Maker's. `list_tiles` answers the same questions from the open page (use its `group` argument to ask for one part).

## How a map is built

A map is a grid of `rows` x `columns` cells.

- **Ground** (layer 1): every cell has a ground tile. There are five kinds of ground and nothing else: gravel, dirt, slate, mud and water (no grass and no sand). A cell can also be **solid**: ants cannot walk through it. Objects that are solid make the cell solid by themselves.
- **Objects** (layer 2): a cell may hold one object: a rock, a power-up, a bridge piece, a bomb, a fire wall, or one cell of a bigger object, of a hill or of a food pile. Most cells hold none.
- **Hills**: each color has one hill, 4 x 4 cells. Its entrance is the cell in its second row and second column; that is the cell you name in `put_hill`.
- **Starting ants**: a record for each ant, with the color's start marker. A color may have many clumps; the original game starts one ant for each record.
- **Food piles**: a cell, a number of **bites**, the **points** a bite is worth, and **stages** (what the pile looks like as it is eaten; the last stage is empty, so the pile vanishes when it is gone).
- **Plants**: flowers and clovers on top of the ground. A flower can be a **dropper**: every so many seconds it lets a power-up fall on the cell below it.
- **Properties**: a description, the minutes of a match, the eggs each color starts with, and a default ant type.

A **tile id** is a number from 0 to 1,343. Each id has a fixed game name (for example `g01a` is plain gravel). The page puts the names a map uses into the map itself.

## The four colors

<!-- generated:teams begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Team (the `team` argument) | Hill tile | Start marker tile |
|---|---|---|
| `green` | 248 | 154 |
| `red` | 247 | 155 |
| `blue` | 246 | 153 |
| `black` | 245 | 152 |

| Ant type (the `type` and `default_ant` argument) | Power-up tile that makes an ant this type |
|---|---|
| `none` (None) | none |
| `combat` (Combat ant) | 62 |
| `thief` (Thief ant) | 63 |
| `bomber` (Bomber ant) | 64 |
| `swimmer` (Swimmer ant) | 65 |
| `fire` (Fire ant) | 66 |
<!-- generated:teams end -->

The game's own order of seats is green, red, blue, black. A Classic map works in the original game only when all four colors have a hill and starting ants.

## Ants: types and power-ups

There are six types of ant. A plain ant is a **worker**. An ant becomes another type when it walks onto a **power-up** tile (the tile ids are in the table above). The types add: combat (a stronger blow), thief (raids an enemy hill), bomber (plants and defuses bombs), swimmer (swims, and builds and demolishes bridges), fire (lights and puts out fire walls and walks through them). A map may set a **default ant type**: then every ant on the map, the ones that start and the ones that hatch, is of that type. On Extended maps each clump of starting ants may have its own type (`type` of `put_ants`).

## Ground

<!-- generated:ground begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Kind | Letter of its names | Plain tile | Game name | Has edge pieces toward |
|---|---|---|---|---|
| Gravel | g | 485 | g01a | dirt, mud, slate |
| Dirt | d | 487 | d01a | gravel, mud, slate |
| Slate | s | 571 | s01a | dirt, gravel, mud |
| Mud | m or M | 439 | M01a | dirt, gravel, slate |
| Water | w | 476 | w01a | mud |
<!-- generated:ground end -->

An edge piece's name is `<own kind><other kind><shape><variant>`: `gd03a` is a gravel cell that blends toward dirt with the dirt on its north side. Shapes 02 to 09 are the north-west corner, north edge, north-east corner, west edge, east edge, south-west corner, south edge and south-east corner. The game does **not** blend by itself: a boundary between two kinds painted with plain tiles is a hard square edge until the edge pieces are put in (`fix_edges`, or `auto_edges` on `paint`). See `ants-map-terrain`.

## Objects you can paint

These stand on one cell. Paint them with `paint` (`layer: "objects"`) or put one with `put_object`.

<!-- generated:objects begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Tile | Name | Game name | What it is |
|---|---|---|---|
| 302 | Rock 1 | smlrock1 | rock |
| 303 | Rock 2 | smlrock2 | rock |
| 304 | Rock 3 | smlrock3 | rock |
| 305 | Rock 4 | smlrock4 | rock |
| 307 | Rock 5 | smlrock6 | rock |
| 308 | Rock 6 | smlrock7 | rock |
| 62 | Combat ant power-up | pu_comb | power-up |
| 63 | Thief ant power-up | pu_thief | power-up |
| 64 | Bomber ant power-up | pu_bomb | power-up |
| 65 | Swimmer ant power-up | pu_swim | power-up |
| 66 | Fire ant power-up | pu_mason | power-up |
| 134 | Fire wall | wallup04 | fire wall |
| 37 | Bridge | bridge4 | finished bridge piece |
| 131 | Black bomb | blackbomb | bomb |
| 132 | Blue bomb | bluebomb | bomb |
| 130 | Green bomb | greenbomb | bomb |
| 129 | Red bomb | redbomb | bomb |
<!-- generated:objects end -->

Power-ups work only when an ant ends its walk on the tile. Put bombs and fire walls on plain ground, where the game's own abilities would put them (never on mud, water, a bridge or a hill). A finished bridge piece on water is a permanent crossing in the map file (it has no timer); it walks like mud.

## Bigger objects

An object that covers several cells (grass tufts, leaves, toys, rocks). Put one with `put_object`: the cell you name is its **anchor**, and the object covers the cells around it as the footprint says. A hill, starting ants or a food pile in the way refuse it.

<!-- generated:bigger begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Tile | Game name | Cells it covers | Columns x rows |
|---|---|---|---|
| 1323 | grass4 | 4 | 2 x 2 |
| 1324 | grassmed1 | 10 | 5 x 2 |
| 1321 | grass2 | 6 | 2 x 3 |
| 1322 | grass3 | 4 | 2 x 2 |
| 1320 | grass1 | 6 | 2 x 3 |
| 1326 | grassmed2 | 8 | 2 x 4 |
| 301 | medrock9 | 2 | 2 x 1 |
| 237 | bobber | 4 | 2 x 2 |
| 1329 | grassmed4 | 10 | 4 x 4 |
| 1327 | grassmed3 | 12 | 4 x 4 |
| 242 | slinky | 12 | 4 x 3 |
| 346 | bearbod | 30 | 6 x 6 |
| 329 | mush2a | 16 | 4 x 4 |
| 236 | glasses | 19 | 7 x 3 |
| 238 | can1 | 22 | 5 x 5 |
| 350 | sling | 6 | 3 x 3 |
| 326 | leaf4 | 7 | 2 x 4 |
| 418 | ivylft | 6 | 2 x 3 |
| 417 | ivyrt | 6 | 3 x 2 |
| 289 | bigrock1 | 8 | 4 x 2 |
| 325 | leaf3 | 12 | 4 x 4 |
| 409 | plant1 | 14 | 4 x 4 |
| 324 | leaf2 | 22 | 5 x 5 |
| 349 | ducky2 | 12 | 4 x 4 |
| 315 | pin3 | 2 | 1 x 2 |
| 345 | bearhed | 15 | 4 x 4 |
| 292 | bigrock4 | 4 | 2 x 2 |
| 354 | yelcray | 4 | 2 x 3 |
| 352 | pinkcray | 3 | 1 x 3 |
<!-- generated:bigger end -->

## Plants

Put one with `put_plant` (`dropper` makes it a flower that drops power-ups every so many seconds).

<!-- generated:plants begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Tile | Name | Game name |
|---|---|---|
| 404 | Clover 1 | clover1 |
| 405 | Clover 2 | clover2 |
| 406 | Clover 3 | clover3 |
| 407 | Clover 4 | clover4 |
| 408 | Clover patch | clovers |
| 410 | Flower 1a | flower1a |
| 411 | Flower 1b | flower1b |
| 412 | Flower 2a | flower2a |
| 413 | Flower 2b | flower2b |
| 414 | Flower 3a | flower3a |
| 415 | Flower 3b | flower3b |
| 416 | Flower 3c | flower3c |
| 420 | Daisy flower | dflower1 |
| 421 | Flower 1 | flower1 |
<!-- generated:plants end -->

## Food

<!-- generated:food begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| Food id (the `food` argument) | Item | Game name | Bites | Points a bite | Stages |
|---|---|---|---|---|---|
| 253 | Burger | fdburgr1 | 30 | 30 | 4 |
| 256 | Lollipop | fdsuckr1 | 40 | 25 | 5 |
| 260 | Chocolate bar | fdchoc1 | 40 | 30 | 5 |
| 264 | Pretzel | fdpretz1 | 30 | 25 | 4 |
| 267 | Skittles | fdskit1 | 80 | 20 | 6 |
| 272 | Gummy worms | fdgumw1 | 40 | 25 | 5 |
| 275 | Single gummy worm | fdgumw4 | 10 | 25 | 2 |
| 276 | Cola cap | fdcola1 | 30 | 25 | 5 |
| 280 | Froot Loop (yellow) | fdfrl1 | 10 | 30 | 2 |
| 281 | Froot Loop (blue) | fdfrl2 | 10 | 30 | 2 |
| 282 | Froot Loop (orange) | fdfrl3 | 10 | 30 | 2 |
| 283 | Froot Loop (purple) | fdfrl4 | 10 | 30 | 2 |
| 284 | Froot Loop (green) | fdfrl5 | 10 | 30 | 2 |
| 285 | Froot Loop (pink) | fdfrl6 | 10 | 30 | 2 |
| 286 | Froot Loop (red) | fdfrl7 | 10 | 30 | 2 |
| 340 | PEZ candy (pink) | fdpezp1 | 10 | 30 | 2 |
| 341 | PEZ candy (yellow) | fdpezb1 | 10 | 30 | 2 |
| 342 | PEZ candy (orange) | fdpez2p1 | 10 | 30 | 2 |
| 343 | PEZ candy (purple) | fdpez2b1 | 10 | 30 | 2 |
| 356 | Lunchbox | lunchbox | 60 | 30 | 6 |
| 357 | Potted meat | fdpmeat1 | 40 | 25 | 5 |
| 361 | Corn dog | fdcornd1 | 50 | 30 | 6 |
| 366 | Jell-O | fdjelo1 | 30 | 25 | 4 |
| 369 | Round crackers | fdcrakr1 | 30 | 25 | 5 |
| 373 | Square crackers | fdcraks1 | 30 | 25 | 5 |
| 378 | Steak | fdsteak1 | 50 | 35 | 5 |
| 382 | Twinkie | fdtwink1 | 30 | 30 | 4 |
| 385 | Fried eggs | fdeggs1 | 40 | 30 | 5 |
| 390 | Pizza slice | fdpizz1 | 40 | 30 | 5 |
| 394 | Fish | fdfish1 | 50 | 30 | 6 |
| 399 | Chicken leg | fdchick1 | 50 | 30 | 6 |
| 1331 | Cereal | fdcer1 | 30 | 25 | 4 |

| Stages (the `stages` argument) | A plain pile of that size has |
|---|---|
| 2 | 10 bites, 30 points a bite |
| 3 | 20 bites, 25 points a bite |
| 4 | 30 bites, 25 points a bite |
| 5 | 30 bites, 25 points a bite |
| 6 | 80 bites, 20 points a bite |
<!-- generated:food end -->

A pile is eaten bite by bite; its look changes by stage. More bites and more points make a richer prize: the six original maps have piles of 10 to 100 bites worth 10 to 50 points a bite.

## The rules word

A Classic map is a version 8 file and plays by the original's rules. An Extended map is a version 9 file with a **rules word**: 0 is the original's rules, 1 has no wall at row 90 for tall maps, 2 also searches paths farther (orders of 450 cells and more). The page sets the word itself when a map is made or resized; you cannot set it, and you do not need to.

## Limits

<!-- generated:limits begin (tools/make_ai_skills.py makes this block from the program's source: change the source, not these lines) -->

| What | The limit |
|---|---|
| A new Classic map: rows, columns, cells | at most 90 rows, 130 columns and 10,000 cells |
| An Extended map: sides, cells | at most 1,024 on a side and 262,144 cells (for example 512 x 512) |
| Description | a Classic map at most 29 bytes (UTF-8 bytes, not letters), an Extended map at most 255 bytes |
| Minutes of a match | 1 to 120 |
| Eggs each team starts with | 0 to 255 |
| A hill | 4 x 4 cells; its entrance is the cell in its second row and second column |
| A clump of starting ants | 1 to 250 ants (a team may have many clumps; a map holds at most 65,535 records of ants and plants together) |
| A food pile | 2 to 6 stages; bites and points a bite 5 to 200 |
| Different tiles in one map | at most 1,344 (a tile id is 0 to 1343) |
| An online room plays | a map of at most 65,536 cells and 1,000 starting ants (a game on your own computer has no such limit; above 2,400 starting ants a map is called heavy) |
| One tool call changes | at most 5,000 cells (at most 2,000 listed one by one) |
| get_region | a window of at most 128 x 128 cells |
| get_picture | at most 1,000 pixels on a side |
| note, a map's name or a draft's name | a note 200 characters, a name 40 characters |
<!-- generated:limits end -->

A Classic map with more than 90 rows would have an invisible wall in the original game (its ants cannot cross the 91st row), so a taller map must be an Extended map.
