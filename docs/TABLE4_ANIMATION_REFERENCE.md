# Table 4 Animation & Physics Reference (`ants.chd`)

This document is generated directly from ground-truth inspection of `ants.chd` Table 4.
Total animation sequences: **1344**.

## Action Prefixes & Meanings
- **Prefix Class**: `ag` (Worker), `ab` (Bomber), `af` (Fire), `ac` (Combat), `as` (Swimmer), `at` (Thief)
- **Action Code**:
  - `wg` / `ws`: Walking on Ground / Shoreline
  - `st`: Standing / Idle ready
  - `at`: Melee Attack strike
  - `gh`: Get Hit (combat flinch reaction slide)
  - `gb`: Ground Bounce (tumbling collision bounce flight)
  - `gf`: Grab Food (harvesting food bite)
  - `fa`: Food Action / Eat
  - `sb`: Set Bomb (plant bomb sequence)
  - `db`: Defuse Bomb / Demolish Bridge
  - `sf`: Set Fire (plant firewall)
  - `xf`: Extinguish Fire
  - `bb`: Build Bridge
  - `sw`: Swimming in water
  - `di`: Diving into water
  - `go`: Emerging / exiting water onto land
  - `dr`: Drowning sequence
  - `bu`: Bomb dud / smoke burn stagger
  - `cg`: Can't Go / blocked path reaction

---

## Core Action Timings, Subitems & Audio Triggers

| Entry | Action Name | Subitems | Total Duration | Audio Triggers (Subitem: Sound) | Motion (dx, dy) |
|:-----:|:------------|:--------:|:--------------:|:--------------------------------|:----------------|
|   56 | `battle` | 4 | 270 ms | Sub 0: `combatnetfairy.wav` (3) | Stationary |
|  133 | `bombex` | 10 | 680 ms | Sub 0: `bombexp.wav` (4) | Stationary |
|  670 | `afwg201` | 12 | 600 ms | None | s0:(3,3), s1:(3,3), s2:(3,3), s3:(3,3) |
|  675 | `afst201` | 6 | 600 ms | None | Stationary |
|  682 | `afat201` | 6 | 360 ms | Sub 4: `attack.wav` (57) | Stationary |
|  687 | `afcg301` | 9 | 660 ms | Sub 0: `cantgo.wav` (63) | Stationary |
|  688 | `afgh201` | 10 | 1000 ms | Sub 0: `flythumpa.wav` (64), Sub 3: `flythumpb.wav` (65) | s0:(-20,-20), s1:(-8,-8), s2:(-4,-4) |
|  693 | `afgb201` | 13 | 1100 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | s1:(-128,-128) |
|  698 | `afgf201` | 4 | 400 ms | Sub 1: `harvest.wav` (66) | Stationary |
|  703 | `afsf301` | 22 | 1760 ms | Sub 5: `firestarta.wav` (67), Sub 17: `firestartb.wav` (68) | Stationary |
|  706 | `afxf301` | 12 | 1200 ms | Sub 4: `fireextinguish.wav` (69) | Stationary |
|  710 | `afbu301` | 13 | 1330 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | Stationary |
|  755 | `afdr301` | 22 | 2370 ms | Sub 0: `splash.wav` (71), Sub 1: `antdrown.wav` (72) | Stationary |
|  757 | `abwg301` | 12 | 600 ms | None | s0:(0,4), s1:(0,4), s2:(0,4), s3:(0,4) |
|  762 | `abat201` | 6 | 360 ms | Sub 3: `attack.wav` (57) | Stationary |
|  767 | `abcg301` | 15 | 1380 ms | Sub 0: `cantgo.wav` (63) | Stationary |
|  773 | `abst201` | 8 | 800 ms | None | Stationary |
|  778 | `abgh301` | 9 | 1000 ms | Sub 0: `flythumpa.wav` (64), Sub 3: `flythumpb.wav` (65) | s0:(0,-16), s1:(0,-10), s2:(0,-6) |
|  783 | `abgb301` | 12 | 1060 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | s0:(0,-128) |
|  788 | `abbu301` | 16 | 1610 ms | Sub 0: `flythumpa.wav` (64), Sub 10: `flythumpb.wav` (65) | Stationary |
|  789 | `abdb301` | 12 | 1100 ms | Sub 3: `bombdrop.wav` (73), Sub 6: `bombmuffle.wav` (74) | Stationary |
|  792 | `abgf201` | 4 | 440 ms | Sub 1: `harvest.wav` (66) | Stationary |
|  804 | `abdr301` | 22 | 2380 ms | Sub 0: `splash.wav` (71), Sub 1: `antdrown.wav` (72) | Stationary |
|  811 | `agst301` | 12 | 1650 ms | None | Stationary |
|  816 | `agwg301` | 12 | 600 ms | None | s0:(0,4), s1:(0,4), s2:(0,4), s3:(0,4) |
|  821 | `agat301` | 6 | 420 ms | Sub 1: `attack_alt.wav` (75) | Stationary |
|  827 | `aggh301` | 9 | 800 ms | Sub 0: `flythumpa.wav` (64), Sub 3: `flythumpb.wav` (65) | s0:(0,-24), s1:(0,-8) |
|  847 | `aggb301` | 12 | 900 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | s0:(0,-128) |
|  852 | `aggf301` | 7 | 420 ms | Sub 4: `harvest_alt.wav` (77) | Stationary |
|  867 | `hgen301` | 17 | 1000 ms | None | Stationary |
|  884 | `agcg301` | 6 | 360 ms | Sub 0: `cantgo.wav` (63) | Stationary |
|  885 | `agbu301` | 11 | 1150 ms | Sub 0: `flythumpa.wav` (64), Sub 5: `flythumpb.wav` (65) | Stationary |
|  891 | `acwg201` | 12 | 600 ms | None | s0:(3,3), s1:(3,3), s2:(3,3), s3:(3,3) |
|  896 | `acst201` | 4 | 600 ms | None | Stationary |
|  901 | `acat201` | 6 | 540 ms | Sub 2: `attack2.wav` (78) | Stationary |
|  906 | `acgh201` | 9 | 880 ms | Sub 0: `flythumpa.wav` (64), Sub 3: `flythumpb.wav` (65) | s0:(-16,-16), s1:(-10,-10), s2:(-6,-6) |
|  911 | `acgf201` | 6 | 360 ms | Sub 0: `harvest_alt.wav` (77) | Stationary |
|  916 | `accg301` | 6 | 620 ms | Sub 0: `cantgo.wav` (63) | Stationary |
|  919 | `acgb201` | 12 | 980 ms | Sub 0: `flythumpa.wav` (64), Sub 7: `flythumpb.wav` (65) | s0:(-128,-128) |
|  936 | `acbu301` | 10 | 1165 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | Stationary |
|  941 | `acdr301` | 22 | 2370 ms | Sub 0: `splash.wav` (71), Sub 1: `antdrown.wav` (72) | Stationary |
|  942 | `aswg201` | 12 | 600 ms | None | s0:(3,3), s1:(3,3), s2:(3,3), s3:(3,3) |
|  962 | `asst201` | 6 | 900 ms | None | Stationary |
|  967 | `asgh201` | 9 | 1050 ms | Sub 0: `flythumpa.wav` (64), Sub 2: `flythumpb.wav` (65) | s0:(-15,-15), s1:(-12,-12), s2:(-5,-5) |
|  972 | `asgb201` | 12 | 1110 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | s0:(-128,-128) |
|  977 | `asat201` | 10 | 760 ms | Sub 5: `waterattack.wav` (79) | Stationary |
|  982 | `asgf301` | 4 | 320 ms | Sub 3: `harvest_alt.wav` (77) | Stationary |
|  985 | `asbu301` | 14 | 1050 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | Stationary |
| 1032 | `asdbl301` | 8 | 480 ms | Sub 4: `shovelgravel.wav` (81) | Stationary |
| 1041 | `asbbw301` | 8 | 500 ms | Sub 3: `shovelwater.wav` (82) | Stationary |
| 1048 | `ascg301` | 12 | 960 ms | Sub 0: `cantgo.wav` (63) | Stationary |
| 1054 | `atwg201` | 12 | 600 ms | None | s0:(3,3), s1:(3,3), s2:(3,3), s3:(3,3) |
| 1059 | `atat201` | 12 | 720 ms | Sub 4: `theifwhip.wav` (83) | Stationary |
| 1064 | `atgh201` | 9 | 880 ms | Sub 0: `flythumpa.wav` (64), Sub 3: `flythumpb.wav` (65) | s0:(-32,-32) |
| 1084 | `atst201` | 26 | 2520 ms | None | Stationary |
| 1093 | `atgb201` | 12 | 1000 ms | Sub 0: `flythumpa.wav` (64), Sub 6: `flythumpb.wav` (65) | s0:(-128,-128) |
| 1096 | `atgf301` | 5 | 340 ms | Sub 4: `harvest_alt.wav` (77) | Stationary |
| 1128 | `atcg301` | 11 | 890 ms | Sub 0: `cantgo.wav` (63) | Stationary |
| 1129 | `atbu301` | 11 | 1150 ms | Sub 0: `flythumpa.wav` (64), Sub 4: `flythumpb.wav` (65) | Stationary |
| 1130 | `atdr301` | 22 | 2370 ms | Sub 0: `splash.wav` (71), Sub 1: `antdrown.wav` (72) | Stationary |
| 1134 | `agdr301` | 22 | 2370 ms | Sub 0: `splash.wav` (71), Sub 1: `antdrown.wav` (72) | Stationary |
| 1317 | `absb301` | 17 | 1360 ms | Sub 9: `bombpick.wav` (90) | Stationary |

---

## Full Table 4 Data Access
The complete dataset with all frames, sprite indices, bounding boxes, and per-subitem durations is serialized in `docs/chd_table4_animations.json`.