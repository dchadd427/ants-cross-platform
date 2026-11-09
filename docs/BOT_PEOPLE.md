# Bots against people: the yardstick

A computer player is judged against people by what people do, not only by who wins. The win rate and the margin of `bot_arena` stay the check that a change did not cost strength ([BOTS.md](BOTS.md), "Running bots"); they say nothing about whether a player answers a blow, helps its own ants or brings a hurt ant home. This page holds what measures that: the **people numbers** of `bot_watch`, a bench bot named `gang` that plays like a small group of people (and a stronger one, `gang2`), five scenes in which the standard bot is held to a rule against an opponent that a test scripts, and the numbers of the bot before and after the first two changes that were measured with them ([Step 1](#step-1-everyone-hits-back), [Step 2](#step-2-hurt-ants-heal)). The yardstick itself changes no bot.

## The people numbers

`bot_watch` ([BOT_FIGHT.md](BOT_FIGHT.md)) reads the engine after every tick, as it always did, and now also counts these numbers for every seat. The code is `tools/bot_watch_people.cpp`; its core takes plain snapshots (the ants, bombs and fire walls of one tick), so the tests feed it by hand. It changes nothing: a match under watch ends on the state hash of the same match in `bot_arena` (AI34.10).

- **A blow.** An ant's hit points fall and an enemy ant in its strike clip (`UnitState::Attacking`) stands within 2 tiles of it. That ant struck the blow; the nearest wins, the lower number for a tie. A fall with no such ant (a bomb, a fire wall) is no blow. Allied seats are one side.
- **A fight.** A run of blows between two sides with gaps of at most 300 ticks and 10 tiles (a blow joins the open fight of the same two sides whose last blow is nearest).

For the fights in which the seat was **struck first** (the first blow fell on its ant):

| Number | Meaning |
|---|---|
| `struck_first` | how many such fights |
| `hit_back` | in how many of them the seat's side struck a blow |
| `helped` | ... an ant other than the first victim struck |
| `help_ticks_median`, `help_ticks_p90` | the ticks from the first blow of the fight to the first blow of such a helper (nearest rank) |
| `strikers_own_mean`, `strikers_enemy_mean` | the ants that struck, per fight, on the seat's side and on the other |
| `local_own_max_mean`, `local_enemy_max_mean` | the most ants of each side within 4 tiles of the fight's last blow, sampled every 20 ticks while blows fall, averaged over the fights |
| `lost_in_fights`, `killed_in_fights` | the ants of the seat that a blow of these fights killed, and the enemy ants that a blow of the seat's side killed |

The rest, for every seat:

| Number | Meaning |
|---|---|
| `deaths` | the seat's ants that died (a drowning counts) |
| `could_walk_home` | a death of an ant that, at the first look at 4 hit points or fewer (at most 1,200 ticks before it died, and not before its last heal), stood nearer to its hill's entrance than the match's walkers would have covered in the ticks it still lived: the Chebyshev distance divided by the mean speed of the ants in the walking state (sampled every 20 ticks, all seats), against the ticks to its death |
| `died_far` | a death after such a look that it could not have walked home |
| `died_without_warning` | a death of an ant that never had 4 or fewer |
| `heals` | how many times an own ant's hit points rose |
| `fire_ants`, `fire_ants_died` | ants that became Fire Ants, and those that died |
| `walls_at_enemy_gates` | fire walls of the seat lit within 4 tiles of an enemy hill's entrance (the owner is the cell's owner) |
| `fire_ants_lost_per_wall` | `fire_ants_died` over the larger of `walls_at_enemy_gates` and 1 |
| `home_mines_3000`, `_6000`, `_12000` | the seat's bombs that appeared within 10 tiles of its own hill's entrance by those ticks |
| `away_mines`, `mine_hits_enemy` | its bombs that appeared farther away; its bombs that vanished with an enemy ant losing hit points within a tile in the same tick or the next two |
| `blows_struck`, `focus_pct` | the seat's blows; of its consecutive blows less than 200 ticks apart, the percent on the same victim |

In the game type 187 the hill is closed, so no ant can walk home: `could_walk_home` and `died_far` are `null` (not applicable) there.

**Where they appear.** Every match of `bot_watch` prints one line for each seat and its JSON report has them under `"people"` in the seat's object, in the arena mode and with `--replay` alike. A run over several matches also prints, and writes under `"people_totals"`, the sums by seat spec (the means are over all the fights of the run). `--no-people` leaves all of it out. On four Hard bots on TREASURE (seeds 1 to 8) the observer costs about 5 percent of the run.

## The bench bot `gang`

`gang` is a test-only bot (`tools/bench_gang.hpp`, never in the registry: `make_bot` does not know it, no game or room can seat one). `bot_arena` and `bot_watch` seat it as `--seat N=gang:LEVEL`; it is a client like every bot (the view, the orders, the controller, its level's profile). In the order of its looks:

1. **The economy.** The standard bot's own harvest task (every type of ant harvests when it has nothing else to do) and power-up task: Thief power-ups first (up to three, those of the other sides too), then one Combat Ant, then one Fire Ant. It has none of the standard bot's fight tasks, or the bench would measure the bot against itself.
2. **Raids.** Every Thief raids the hill with the highest score box (30 points or more) that is not shut, again and again, and harvests between raids.
3. **The gang.** From tick 300 it keeps a gang of four ants (any type but a Thief, never a carrier). The target is the enemy ant that carries nothing within 8 tiles of the gang's centre, Thieves, Fire Ants and Bombers first, then the one with the fewest hit points. One strike order names every gang ant that is idle after its blow, again each time, until the target is dead or more than 10 tiles from the centre. A gang ant with 3 hit points or fewer goes to its hill and rejoins when healed. With no target the gang waits at the pile that is nearest the hill of the victim (the enemy with the highest score box).
4. **The hunter.** One Combat Ant (the Fire Ant when there is none) stands 4 tiles from the victim's hill entrance on the way to its nearest pile, strikes any enemy ant within 3 tiles that is not a Combat Ant, again after every blow, and goes home at 3 hit points or fewer.
5. It lights no fire walls and lays no mines.

It is deliberately simple: the opponent that the standard bot is measured against, not a model of a good player. How the numbers move with its details is in "Limits".

## The AI34 cases

`tests/test_ai/test_ai_people.cpp` (suite 2.20, [TESTING.md](TESTING.md)). AI34.1 to AI34.5 are scenes on a 60 by 60 field of grass with four hills: a Hard bot at seat 0 plays through the real controller (its budget and reaction time), an opponent that the test scripts gives its orders through `apply_command`, and the bot gets 40 ticks before the script's first blow. Every scene is played for both styles of a Hard bot and the rule has to hold for both. The people numbers of the observer do the counting. A **known failure** is a scene that the shipped bot fails and a later change has to make pass: it runs, prints `KNOWN FAIL AI34.n (Step k)` and what it measured, and is green; the day it passes it turns red (`NOW PASSES`) and becomes a required case. AI34.1 and AI34.4 became required in Step 1.

| ID | Scene | Rule | Today |
|---|---|---|---|
| AI34.1 | four ants of the bot (two Thieves, a Fire Ant, a worker, at full health and empty) stand together 12 tiles from their hill; three enemy workers strike a Thief again after every blow | within 600 ticks at least 6 blows by the bot and at most 1 ant lost | required since Step 1 (before: 5 blows, 1 ant lost, one worker was the only ant that the fight task took; now 9 blows, none lost) |
| AI34.2 | the bot has two ants (one at home, one hatched); an enemy Fire Ant stands 4 tiles from its hill entrance and strikes any ant within 3 tiles | within 1,200 ticks no ant of the bot dies alone to it, or two of them strike it | known failure: 1 ant lost, the camper never struck |
| AI34.3 | the bot's only ant is sent for a Fire power-up 10 tiles away; an enemy Combat Ant stands 2 tiles from the power-up, from the start and from tick 30 (see below) | the ant does not stand within a tile of the Combat Ant with no second ant of the bot within 2 tiles | known failure from tick 30 (the ant walks up to it); from the start the bot does not send the ant |
| AI34.4 | the bot's Bomber (empty, 6 hit points) is struck by an enemy worker that strikes again at the first tick that 20 divides at which it is idle; three workers of the bot stand nearby (see below) | the Bomber strikes the worker itself within 200 ticks of the first blow on it | required since Step 1 (after 118 and 130 ticks in the two styles; before: never) |
| AI34.5 | an enemy Thief raids the hill of the bot (four workers, no Fire Ant) again and again: its own hill is 46 tiles away, so about three raids in 2,400 ticks | before the fourth raid the three tiles east of the raid tile are shut, or the Thief has been struck | known failure: three raids, the hole open, the Thief never struck |

AI34.3 reads "a lone Fire Ant is sent": only a worker is ever sent for a power-up (a typed ant is not), so the ant is a worker on its way to become one. When the Combat Ant stands there from the start, the standard bot's race check gives the trip up, which is why the other variant has it arrive at tick 30, while the ant is on its way.

AI34.4 as Step 0 wrote it (a lone Bomber, a striker that orders on the very tick, "strikes back within 40 ticks or walks home") cannot be met by any bot. The bot's last ants never fight (the reserve, unchanged in Step 1), and the engine takes no order for an ant that is engaged or in its hit clip, about 25 ticks of every blow: a striker that orders again on the first tick leaves its victim 3 free ticks in 28, which a look every 4 ticks and a reaction of 8 cannot use. The scene has three workers of the bot nearby, the striker orders on the first tick that 20 divides, and the rule is the Bomber's own blow.

AI34.6 to AI34.9 are unit tests of the numbers on hand-made streams of snapshots (a blow and the nearest striker, fights and their gaps, hitting back, helping and the crowd; heals and Fire Ants; deaths that could have walked home; mines); AI34.10 plays a match with and without the observer and compares the state hashes; AI34.11 checks that `gang` gives only orders that a person could give, is not in the registry and plays a match twice alike. The mutants of the numbers are `tests/data/bot_people_mutants.json` (`python3 tools/mutate.py tests/data/bot_people_mutants.json --keep-going --jobs 1 --timeout 900`).

## The numbers before Step 1

TREASURE, Release build, the arena's sink latency of 3 ticks. `bot_watch` runs: Hard against `gang` at Hard and at Medium, seeds 1 to 24, both seats of each arrangement (48 matches each); four Hard bots, seeds 1 to 8 (32 seats). A seat counts once for every match it plays.

**Fights in which the seat was struck first**

| run | seat | seats | struck first | hit back | helped (median, p90 ticks) | strikers own / enemy | at the fight own / enemy | lost / killed in fights |
|---|---|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 444 | 115 (26%) | 90 (129, 366) | 0.36 / 1.80 | 2.43 / 2.36 | 37 / 25 |
| hard vs gang:hard | gang:hard | 48 | 132 | 67 (51%) | 54 (184, 450) | 1.27 / 1.36 | 2.36 / 2.80 | 28 / 19 |
| hard vs gang:medium | standard:hard | 48 | 210 | 130 (62%) | 109 (98, 376) | 0.88 / 1.59 | 3.17 / 2.08 | 2 / 24 |
| hard vs gang:medium | gang:medium | 48 | 241 | 112 (46%) | 88 (195, 584) | 0.90 / 1.46 | 2.22 / 2.98 | 53 / 1 |
| four hard (mirror) | standard:hard | 32 | 324 | 122 (38%) | 104 (126, 368) | 0.57 / 1.33 | 2.06 / 2.08 | 21 / 2 |

**Deaths, heals, Fire Ants, mines, blows**

| run | seat | deaths | could walk home / far / no warning | heals | Fire Ants (died) | walls at enemy gates | home mines by 3,000 / 6,000 / 12,000 | away mines (hits) | blows struck | focus |
|---|---|---|---|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 81 | 57 / 7 / 17 | 769 | 116 (42) | 390 | 0 / 0 / 0 | 0 (0) | 913 | 59% |
| hard vs gang:hard | gang:hard | 96 | 42 / 28 / 26 | 155 | 47 (23) | 0 | 0 / 0 / 0 | 0 (0) | 2311 | 64% |
| hard vs gang:medium | standard:hard | 18 | 4 / 2 / 12 | 537 | 118 (10) | 538 | 0 / 0 / 0 | 0 (0) | 1471 | 58% |
| hard vs gang:medium | gang:medium | 185 | 70 / 54 / 61 | 224 | 47 (42) | 0 | 0 / 0 / 0 | 0 (0) | 1042 | 45% |
| four hard (mirror) | standard:hard | 62 | 23 / 11 / 28 | 391 | 32 (1) | 3 | 0 / 0 / 0 | 428 (214) | 908 | 56% |

**Points and ants** (the engine's own counts, per seat and match; the ants killed include those that drowned after a blow)

| run | seat | seats | score | ants killed | ants lost | ants left |
|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 3308 | 2.00 | 1.67 | 4.3 |
| hard vs gang:hard | gang:hard | 48 | 243 | 1.67 | 2.00 | 4.0 |
| hard vs gang:medium | standard:hard | 48 | 3682 | 3.85 | 0.38 | 5.6 |
| hard vs gang:medium | gang:medium | 48 | 107 | 0.38 | 3.85 | 2.2 |
| four hard (mirror) | standard:hard | 32 | 1869 | 1.94 | 1.94 | 4.1 |

The self-play baseline, `bot_arena` on TREASURE, SMALL and MEDIUM, seeds 1 to 30, one Hard, two Medium and one Easy bot in every arrangement of the seats (1,080 matches). Win rate: the share of seats whose score is strictly above the best of the others; margin: the seat's score minus the best other score, on average. A change to a bot may cost at most 3 points of win rate at any level against these (self-play is the check that nothing broke, not the measure).

| maps | bot | seats | win rate | margin |
|---|---|---|---|---|
| all three | standard:hard | 1080 | 0.638 | +213 |
| all three | standard:medium | 2160 | 0.149 | -436 |
| all three | standard:easy | 1080 | 0.053 | -784 |
| TREASURE | standard:hard | 360 | 0.847 | +445 |
| TREASURE | standard:medium | 720 | 0.074 | -739 |
| TREASURE | standard:easy | 360 | 0.000 | -1601 |
| SMALL | standard:hard | 360 | 0.575 | +43 |
| SMALL | standard:medium | 720 | 0.176 | -221 |
| SMALL | standard:easy | 360 | 0.058 | -286 |
| MEDIUM | standard:hard | 360 | 0.492 | +150 |
| MEDIUM | standard:medium | 720 | 0.197 | -348 |
| MEDIUM | standard:easy | 360 | 0.100 | -465 |

What the numbers said of the standard bot before Step 1: against a gang at Hard it hit back in about a quarter of the fights in which it is struck first, with 0.4 of its ants striking for 1.8 of the other side, and lost more ants in those fights than it killed; seven of ten of its deaths happened to ants that had a warning and could have walked home; it laid no mines near its own hill (home mines 0 at every tick, though it laid them elsewhere); and in the mirror it hit back in 38 percent of the fights. Against the milder gang at Medium it answered in most fights.

## Step 1: everyone hits back

Hard now answers a blow the way a group of people does: every ant that can strike answers, the ant that was struck first of all, as many as the enemy has near the target and one more, and the fight is kept up through the target's hit clips for as long as its force is at least 60 percent as strong as the enemy's near the target. Medium has the same rules built and **off** (`defence=1` turns them on); Easy is as it was. The rules are in `include/ants_ai/defence.hpp` and `src/ants_ai/defence.cpp`, and `FightTask` uses them for the fights that answer a blow, and only for those: the hunts of a kill, the skirmishes, the assault, the hunt of an enemy Fire Ant and of a thief are as they were, and in the game type 187 the brawl owns the fight and nothing changes. With the batch taken out a bot plays exactly as before: the 48 matches of the Hard against `gang:hard` run, played again with `standard+styled=1,defence=0`, end on the same state hashes and scores as the matches of the "before" run.

**The rules**

| | rule | plan field (tune key) |
|---|---|---|
| 1 | *Who answers.* Any ant that takes orders, holds no food or loot, has at least 4 hit points and does not stand on a power-up: a Thief, a Fire Ant, a Bomber and a Swimmer too. Never the Fire Ant that keeps the walls while a thief threatens, a Thief in its raid clip or one of the raid task within 6 tiles of the raid tile of an enemy hill, the crew and diggers of the island play, or an ant of a task that the fights cannot take from (another fight, a gate goal, the rush). The ants of a pick-up trip, of walls that no thief threatens, of the mines, the bombs, the sabotage, the strike and the harassment may be taken; they go back to their task when the fight ends. | `defend_all_types` (`dall`) |
| 2 | *The struck ant first.* The ant whose hit points fell is the first defender of the fight, if it still passes rule 1. It may still be in its hit clip; it takes its order as soon as it can. | `victim_answers` (`dvictim`) |
| 3 | *How many.* The level's defenders, or as many as the enemy has ants within `defend_radius` tiles of the target (a carrier is no fighter) and `defend_extra` more, at most `defend_max`. They come from within `defend_reach` tiles of the target and never from beyond the leash, a Combat Ant first and then the nearest; the ants that a job of rank 3 or 4 holds come only after the free ones. | `defend_by_strength` (`dstr`), `defend_radius` (`dradius`), `defend_extra` (`dextra`), `defend_max` (`dmax`), `defend_reach` (`dreach`) |
| 4 | *How long.* The fight waits through the target's hit clip (it ended at the first blow unless a kill was in reach) while the strength of the defenders (the sum of their hit points times the sum of their damage, as in the hunts) is at least `defend_keep_percent` of the strength of the strongest enemy ants near the target, the target is within the leash of the first blow, and a blow on an own ant or on the target fell within five times the level's linger time. When the force is below that share the fight is called off at the look, before an ant is taken or ordered, and the struck ant gets no help from it (see "Limits"). | `defend_by_strength`, `defend_keep_percent` (`dkeep`) |
| 5 | *Focus.* All defenders strike the fight's target until it dies, runs (rule 4 fails) or leaves the leash. The escape rule (one hit point, walking home, a lead of two tiles) and the can't-go rule are as they were. | |
| 6 | *The last ants* stay out of every fight (the reserve is unchanged), and a defender that falls below 3 hit points (4 beside an enemy Combat Ant) leaves, as before. | |

Hard: the three flags on, `defend_keep_percent` 60, `defend_radius` 5, `defend_extra` 1, `defend_max` 5, `defend_reach` 10. Medium: the flags off, 120, 5, 1, 3, 8. Easy: the flags off. (The radius is 5 where the first draft of the plan had 4: over the five sets of seeds below, the hit-back rate is 62.5% with 5 and 55.5% with 4 (higher with 5 on every set), and the mean score 3,239 against 3,214.) (The share is 60 where the first draft of the plan had 100, which called a fight off whenever the enemy's group was even a little the stronger: with 100 Hard hit back in 61.9% of the fights against `gang:hard` on seeds 1 to 24 and 49.4% on seeds 73 to 96, with 60 it hits back in 61.1% and 57.3%, with ants lost and killed in the fights 20 / 44 and 22 / 37 against 22 / 44 and 25 / 36; in the Hard mirror (seeds 1 to 24) the rate is about the same, 26.2% against 24.8%.) A plan made by hand has none of it. `defence=0` takes the batch out of a plan and `defence=1` puts the three rules on with the level's numbers (`bot_arena --tune K=V,...`, or `standard+K=V` for one seat; add `styled=1` to a tuned seat that is to keep the style and the variations of the registry's bot). `without_defence_batch` in `tactics.hpp` does the same for the tests.

**What the engine allows.** An ant that is struck cannot take an order while the striker's clip runs and during its own hit clip: the engine refuses orders to an ant that is engaged or in flight, about 25 ticks of every blow. So the struck ant strikes back when the striker waits a moment or the others have been at it; most of the answer comes from the ants near it.

**The numbers after Step 1**, the same seeds and commands as before (the report files are called `after_...`):

| # | measure: Hard against `gang:hard`, TREASURE, seeds 1 to 24, both seats | before | after | bar | on these seeds |
|---|---|---|---|---|---|
| 1 | hit back / struck first | 115 / 444 = 25.9% | 129 / 211 = 61.1% | at least 60% | met |
| 2 | ticks from the first blow to the first blow of a helper, median (p90) | 129 (366) | 111 (278) | median at most 120 | met |
| 3 | ants lost / killed in the fights | 37 / 25 | 20 / 44 | at most 29.6 / at least 37.5 | met |
| 4 | mean score of the seat | 3308 | 3298 | at least the before | 10 short (noise, see below) |
| 5 | self-play win rate, all three maps: Hard / Medium / Easy | 0.638 / 0.149 / 0.053 | 0.640 / 0.145 / 0.057 | Hard at most 3 points down, Medium and Easy within 1 point | met |

On the acceptance seeds items 1, 2, 3 and 5 are met and item 4 is 10 points short, which is noise (see the five sets below). Item 1 is the ratio of two numbers that both moved: the bot is struck first in 211 fights instead of 444, probably because its answers kill more of the ants that strike. Most of the fights that are left are ones in which nobody could answer: the struck ant carries food, and the ants near it carry food too, are held by another fight or are further away than `defend_reach`, or the force that could answer is below 60 percent of the enemy's near the target.

**Five sets of 24 seeds** (the acceptance seeds and four others, 48 matches each, Hard against `gang:hard` on TREASURE; the bot before Step 1 on the left of each arrow, the bot now on the right):

| seeds | hit back | helper after, median (p90) ticks | ants lost / killed in the fights | mean score |
|---|---|---|---|---|
| 1 to 24 | 25.9% -> 61.1% | 129 (366) -> 111 (278) | 37 / 25 -> 20 / 44 | 3308 -> 3298 |
| 25 to 48 | 30.9% -> 59.1% | 158 (580) -> 91 (324) | 51 / 15 -> 21 / 45 | 3228 -> 3263 |
| 49 to 72 | 37.5% -> 68.2% | 137 (376) -> 124 (398) | 29 / 37 -> 18 / 47 | 3355 -> 3213 |
| 73 to 96 | 31.3% -> 57.3% | 172 (380) -> 99 (319) | 37 / 27 -> 22 / 37 | 3170 -> 3125 |
| 97 to 120 | 30.4% -> 68.8% | 128 (398) -> 56 (257) | 32 / 23 -> 16 / 42 | 3277 -> 3297 |
| all 5 sets | 31.0% -> 62.5% | | 186 / 127 -> 97 / 215 | 3267 -> 3239 |

Item 1 is 57 to 69 percent per set (pooled 31.0 to 62.5), so the bar of 60 percent is met on 3 of the 5 sets and not on all of them; the median of item 2 is met on 4 of 5 (124 ticks on seeds 49 to 72), the bar of killed ants of item 3 on 3 of 5 and the bar of lost ants on all 5. The score is unchanged: pooled over the 240 matches the change is -28 points with a standard error of 44 (a paired difference per match, 95 percent interval -115 to +59), and a single set moves by -142 to +35. Read the hit-back share together with the ants lost and killed in the fights and with the engine's own counts (below).

**Fights in which the seat was struck first**

| run | seat | seats | struck first | hit back | helped (median, p90 ticks) | strikers own / enemy | at the fight own / enemy | lost / killed in fights |
|---|---|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 211 | 129 (61%) | 121 (111, 278) | 1.60 / 1.81 | 3.18 / 2.22 | 20 / 44 |
| hard vs gang:hard | gang:hard | 48 | 136 | 76 (56%) | 49 (185, 533) | 1.08 / 2.09 | 2.12 / 3.43 | 54 / 3 |
| hard vs gang:medium | standard:hard | 48 | 161 | 117 (73%) | 106 (61, 295) | 1.89 / 1.62 | 3.43 / 2.08 | 7 / 30 |
| hard vs gang:medium | gang:medium | 48 | 264 | 103 (39%) | 76 (170, 559) | 0.79 / 1.98 | 2.09 / 3.21 | 74 / 0 |
| four hard (mirror) | standard:hard | 32 | 294 | 79 (27%) | 66 (98, 295) | 0.52 / 1.34 | 1.91 / 2.23 | 27 / 9 |

**Deaths, heals, Fire Ants, mines, blows**

| run | seat | deaths | could walk home / far / no warning | heals | Fire Ants (died) | walls at enemy gates | home mines by 3,000 / 6,000 / 12,000 | away mines (hits) | blows struck | focus |
|---|---|---|---|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 25 / 7 / 16 | 478 | 115 (22) | 496 | 0 / 0 / 0 | 0 (0) | 1817 | 65% |
| hard vs gang:hard | gang:hard | 173 | 82 / 50 / 41 | 193 | 46 (39) | 0 | 0 / 0 / 0 | 0 (0) | 1487 | 58% |
| hard vs gang:medium | standard:hard | 24 | 8 / 2 / 14 | 450 | 118 (11) | 539 | 0 / 0 / 0 | 0 (0) | 2235 | 66% |
| hard vs gang:medium | gang:medium | 199 | 90 / 41 / 68 | 278 | 48 (43) | 0 | 0 / 0 / 0 | 0 (0) | 947 | 45% |
| four hard (mirror) | standard:hard | 73 | 31 / 20 / 22 | 344 | 32 (3) | 5 | 0 / 0 / 2 | 327 (197) | 946 | 65% |

**Points and ants** (the engine's own counts, per seat and match)

| run | seat | seats | score | ants killed | ants lost | ants left |
|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 3298 | 3.60 | 1.00 | 5.0 |
| hard vs gang:hard | gang:hard | 48 | 391 | 0.98 | 3.60 | 2.4 |
| hard vs gang:medium | standard:hard | 48 | 3490 | 4.15 | 0.50 | 5.5 |
| hard vs gang:medium | gang:medium | 48 | 196 | 0.50 | 4.15 | 2.0 |
| four hard (mirror) | standard:hard | 32 | 1960 | 2.25 | 2.25 | 3.7 |

**Self-play**, the same 1,080 matches as above, before next to after (win rate and margin as defined there):

| maps | bot | seats | win rate before | after | margin before | after |
|---|---|---|---|---|---|---|
| all three | standard:hard | 1080 | 0.638 | 0.640 | +213 | +221 |
| all three | standard:medium | 2160 | 0.149 | 0.145 | -436 | -435 |
| all three | standard:easy | 1080 | 0.053 | 0.057 | -784 | -756 |
| TREASURE | standard:hard | 360 | 0.847 | 0.869 | +445 | +480 |
| TREASURE | standard:medium | 720 | 0.074 | 0.058 | -739 | -754 |
| TREASURE | standard:easy | 360 | 0.000 | 0.008 | -1601 | -1523 |
| SMALL | standard:hard | 360 | 0.575 | 0.544 | +43 | +28 |
| SMALL | standard:medium | 720 | 0.176 | 0.186 | -221 | -209 |
| SMALL | standard:easy | 360 | 0.058 | 0.058 | -286 | -271 |
| MEDIUM | standard:hard | 360 | 0.492 | 0.506 | +150 | +154 |
| MEDIUM | standard:medium | 720 | 0.197 | 0.190 | -348 | -344 |
| MEDIUM | standard:easy | 360 | 0.100 | 0.106 | -465 | -473 |

The rest of the self-play check: the Hard mirror (four Hard bots, seeds 1 to 8): win rate 0.250 per seat (a fair share is .250; the seats are alike), mean score 1960 against 1869.

What the numbers say: against the gang at Hard the bot now answers in about 6 of 10 fights (a quarter before), more of its ants strike (1.6 against 0.4) and the first helper strikes after 111 ticks instead of 129; it loses 20 ants in these fights instead of 37 and kills 44 instead of 25, and the engine counts 3.6 ants killed and 1.0 lost per match against 2.0 and 1.7. Against the milder gang it answers more often as well (73 percent, 62 before) and loses and kills more in the fights (7 and 30, 2 and 24 before), and its mean score is lower (3490 against 3,682 on these seeds; over four sets of 24 seeds -74 points with a standard error of 35), most likely the food of the ants that fight. In the mirror the bot hits back less often (27 percent, 38 before), mostly because it calls off a fight in which its force would be below 60 percent of the enemy's instead of sending ants into it, and it loses more ants in those fights than it kills (27 and 9, 21 and 2 before); its mean score is higher (1960 against 1,869).

**Tune keys that move the numbers** (Hard against `gang:hard`, the same 48 matches, one key at a time on top of the shipped plan; hit back, ticks to the first helper, ants lost and killed in the fights, mean score): the shipped plan 61.1%, 111, 20 and 44, 3,298; `dkeep=100` (the first draft of the plan) 61.9%, 100, 22 and 44, 3,391; `dkeep=0` (a fight is never called off for weakness) 80.5%, 96, 18 and 59, 3,283; `dkeep=40` 71.8%, 120, 22 and 46, 3,264; `dkeep=80` 57.9%, 109, 25 and 44, 3,312; `dkeep=120` 60.5%, 104, 22 and 41, 3,380; `dradius=4` 59.4%, 104, 20 and 42, 3,297; `dradius=6` 54.8%, 106, 22 and 37, 3,277; `dextra=0` 59.1%, 111, 21 and 43, 3,278; `dreach=14` 58.2%, 102, 29 and 41, 3,177; `dstr=0` (rules 1 and 2 only) 67.8%, 132, 23 and 34, 3,224; `dmax=4` is the shipped plan (no fight in these matches asked for more than 4). The rate of item 1 moves by a few points with any of them and not in step with the key: it is within the noise of 48 matches.

## Step 2: hurt ants heal

A task that sends a hurt ant into its own hill when it is in danger. The hill heals every ant that enters it (the heal frame of the enter clip lasts (10 - hit points) x 200 ms: under two seconds from 1 hit point, [GAMEPLAY.md](GAMEPLAY.md), "Anthill Enter, Heal, Hatch & Raid"), and the engine walks an ant home by itself only at 1 hit point; without the task an ant with 2 to 4 hit points goes on with its job and is struck again. The task is **built and switched off**: `heal` is off in the shipped plan of every level, so every bot plays as it did before this step (the 48 matches of Hard against `gang:hard` on seeds 1 to 24 end on the same state hashes as the bot of Step 1, and so do 144 matches of Medium and Easy and 96 of the game type 187). `bot_arena --tune heal=1` (or `standard+styled=1,heal=1` for one seat) turns it on with the level's numbers below; in the game type 187 the hill is closed and the task does nothing even then (the brawl keeps its own retreat). The code is `HealTask` (`include/ants_ai/heal_task.hpp`, `src/ants_ai/heal_task.cpp`), a task of its own in the standard bot's step order, after the fights.

**Why it is off.** It was built to send hurt ants home before they die, and the numbers say that it does not save them: the bars that the step was measured against (the deaths of ants that could have walked home down to 0.4 of Step 1, three times the heals, a fifth fewer ants lost) are not met on any of five sets of seeds (below). It takes about two ants a match; 361 of the 484 ants that it took over 240 matches against `gang:hard` were healed, and 118 were killed on the way: the enemy that hurt an ant follows it, and a blow during the walk stops it. The score does not change, and the other numbers move within the noise. The task stays for a later change that gives the hurt ant an escort or clears the way.

**The rules**

| | rule | plan field (tune key) |
|---|---|---|
| 1 | *Who goes.* An own ant that takes orders and is not stunned, with at most `heal_hp` hit points, when an enemy ant that can strike (it carries nothing, is no ally's, any type) is within `heal_danger_tiles` tiles of it, or it lost hit points within `heal_recent_ticks` ticks, or (no danger) it has at most `heal_calm_hp` hit points and its walk home is at most 6 tiles; and its walk home (the hill's walking cost field, in tiles: 20 cost units, a diagonal step 28) is at most `heal_walk_tiles`. | `heal` (`heal`), `heal_hp` (`healhp`), `heal_danger_tiles` (`healdanger`), `heal_recent_ticks` (`healrecent`), `heal_walk_tiles` (`healwalk`), `heal_calm_hp` (`healcalm`) |
| 2 | *Never.* A Thief in its raid clip, or one of the raid task within 6 tiles of the raid tile of an enemy hill (it is about to raid); the Fire Ant that keeps the walls while an enemy Thief is within 4 tiles of the raid tile of the own hill; an ant of the island play (the crew, the expedition); an ant of a task that this one cannot take from (a fight, the rush, the kill, a gate goal: rank 5 and above); an ant that stands on a power-up; an ant on the hill's mound; an ant that the engine already walks home (1 hit point, walking, nearer than at the last look); a hill that is closed or not there; a hill that a ring of fire walls shuts (no click into it would be carried out; `cantgo_aware`). | |
| 3 | *Carriers.* A carrier goes too: its click onto the entrance banks the food and heals. It is the task's order alone: the gate task (Hard) and the carrier aid leave an ant that the task holds to it, so that one order is made for it at a look and not two. | |
| 4 | *The order.* One group move onto the entrance, Urgent, for every ant that is due at a look. An ant that walks nearer (its walk fell since the last look, which is not older than 40 ticks) is held and not ordered again; an ant that stands idle or whose walk grew is ordered again after 40 ticks, at most 8 times in all. After 600 ticks the ant is given back and left alone for 600 more (an ant in its enter clip is waited for). | |
| 5 | *Done.* The ant has all its hit points again (it entered and healed) or is gone: it is given back, and its task asks for it again. | |
| 6 | *The other tasks.* The task has the rank of a fight (5, `StandardBot::kHeal`): it takes ants from the ranks 1 to 4 and never one from a fight, a rush, the kill or a gate goal; a fight never takes an ant that it holds, and an ant that a fight lets go (it fell below 3 hit points) is taken at the same look, since the task steps after the fights. The unjam of the ramp (Hard's gate) leaves a healing ant where it is: it moves only ants that nobody above the economy holds. | |

`heal` is off at every level. The numbers that the task has when it is turned on: Hard `heal_hp` 4, `heal_danger_tiles` 3, `heal_recent_ticks` 60, `heal_walk_tiles` 16, `heal_calm_hp` 2; Medium 3, 3, 60, 12, 0 (no top-up); Easy the plan's own. A plan made by hand has none of it. `heal=1` puts the task on with the level's numbers and `heal=0` takes it out again (`bot_arena --tune K=V,...`, or `standard+K=V` for one seat; add `styled=1` to a tuned seat that is to keep the style and the variations of the registry's bot). `without_heal` in `tactics.hpp` does the same for the tests.

**Ranks of the tasks** (the Step 2 line: the full list is in [BOTS.md](BOTS.md), "Task ranks"): economy 1, guard and ferry 2, power-ups, raids, sabotage, island task and expedition 3, walls, bombs, mines, strike and squad 4, **fights and the healing task 5**, gate goal and field breach 6. A higher rank takes from a lower one; ranks that are equal take nothing from each other.

**The numbers of Step 2, with the task turned on** (`heal=1`; the shipped plan has it off and plays as the bot of Step 1), the same commands as before, on five sets of 24 seeds and two of 30 (the bot before is the bot of Step 1: a build of its merge; the report files are called `after_...`). The first acceptance set is seeds 1 to 24, Hard against `gang:hard`, both seats:

| # | measure | before Step 1 | Step 1 (task off) | task on | bar | on these seeds |
|---|---|---|---|---|---|---|
| 1 | deaths of ants that could have walked home | 57 | 25 | 27 | at most 10 (0.4 of Step 1; 0.3 of "before Step 1" is 17) | **not met** |
| 2 | heals (hit points that rose) | 769 | 478 | 457 | at least 2,307 (3 times "before Step 1") | **not met** |
| 3 | ants lost / killed in the fights | 37 / 25 | 20 / 44 | 19 / 45 | lost no more than Step 1; all ants lost at most 0.8 of Step 1 (1.00 a match) | fights met; all ants lost 1.00: **not met** |
| 4 | mean score of the seat | 3308 | 3298 | 3370 | at least Step 1 minus 2 percent | met |
| 5 | self-play win rate, Hard / Medium / Easy (1,080 matches, seeds 1 to 30) | .638 / .149 / .053 | 0.640 / 0.145 / 0.057 | 0.649 / 0.142 / 0.054 | Hard at most 3 points down, Medium and Easy within 1 point | met (and on seeds 31 to 60: 0.638 / 0.151 / 0.047 -> 0.632 / 0.152 / 0.050) |
| 6 | can't-go reactions and the orders followed by one, Hard seats in the arena (360 seats a map, 1,080 in all) | | 59976 / 2229 | 58653 / 2310 | not above 5 percent more | met (-2.2 and +3.6 percent); filtered and rejected stay 0 |

Items 1, 2 and the second half of item 3 are **not met** on these seeds, and not on the other four sets either (below). The numbers of the "before Step 1" column are those of the table "The numbers before Step 1" above; the Step 1 column is the bot of Step 1 measured again for this step.

**Hard against `gang:hard`, TREASURE, both seats of each arrangement (48 matches a set)**

| seeds | hit back | ants lost / killed in fights | deaths | could walk home / far / no warning | heals | mean score |
|---|---|---|---|---|---|---|
| 1 to 24 | 61.1% -> 58.4% | 20 / 44 -> 19 / 45 | 48 -> 48 | 25 / 7 / 16 -> 27 / 10 / 11 | 478 -> 457 | 3298 -> 3370 |
| 25 to 48 | 59.1% -> 56.9% | 21 / 45 -> 14 / 49 | 55 -> 54 | 33 / 6 / 16 -> 27 / 6 / 21 | 457 -> 433 | 3263 -> 3248 |
| 49 to 72 | 68.2% -> 63.4% | 18 / 47 -> 14 / 59 | 55 -> 48 | 28 / 10 / 17 -> 20 / 9 / 19 | 525 -> 508 | 3213 -> 3241 |
| 73 to 96 | 57.3% -> 56.7% | 22 / 37 -> 19 / 42 | 57 -> 51 | 30 / 8 / 19 -> 28 / 8 / 15 | 501 -> 458 | 3125 -> 3211 |
| 97 to 120 | 68.8% -> 54.8% | 16 / 42 -> 23 / 44 | 50 -> 60 | 27 / 5 / 18 -> 32 / 7 / 21 | 420 -> 477 | 3297 -> 3308 |
| all 5 sets | 62.5% -> 58.1% | 97 / 215 -> 89 / 239 | 265 -> 261 | 143 / 36 / 86 -> 134 / 40 / 87 | 2381 -> 2333 | 3239 -> 3276 |

Engine counts per seat and match, before -> after: ants killed 3.95 -> 3.89, ants lost 1.10 -> 1.09, ants left 4.9 -> 4.9. Mean score change over the 240 matches: +36 (standard error 32).
Commands: orders per match 493 -> 494, can't-go reactions 1601 -> 1631, orders followed by one 237 -> 326.

**Hard against `gang:medium`**

| seeds | hit back | ants lost / killed in fights | deaths | could walk home / far / no warning | heals | mean score |
|---|---|---|---|---|---|---|
| 1 to 24 | 72.7% -> 69.7% | 7 / 30 -> 3 / 30 | 24 -> 24 | 8 / 2 / 14 -> 4 / 4 / 16 | 450 -> 446 | 3490 -> 3531 |
| 25 to 48 | 62.3% -> 64.1% | 2 / 40 -> 5 / 34 | 16 -> 20 | 3 / 3 / 10 -> 6 / 2 / 12 | 421 -> 444 | 3566 -> 3552 |
| 49 to 72 | 67.4% -> 68.0% | 4 / 39 -> 1 / 36 | 23 -> 19 | 4 / 3 / 16 -> 4 / 4 / 11 | 412 -> 414 | 3500 -> 3517 |
| 73 to 96 | 75.2% -> 74.1% | 0 / 42 -> 1 / 44 | 22 -> 19 | 3 / 4 / 15 -> 3 / 2 / 14 | 402 -> 451 | 3499 -> 3439 |
| 97 to 120 | 80.2% -> 75.4% | 2 / 28 -> 5 / 29 | 15 -> 17 | 4 / 1 / 10 -> 5 / 1 / 11 | 394 -> 431 | 3584 -> 3544 |
| all 5 sets | 71.4% -> 70.2% | 15 / 179 -> 15 / 173 | 100 -> 99 | 22 / 13 / 65 -> 22 / 13 / 64 | 2079 -> 2186 | 3528 -> 3517 |

Engine counts per seat and match, before -> after: ants killed 4.28 -> 4.35, ants lost 0.42 -> 0.41, ants left 5.6 -> 5.6. Mean score change over the 240 matches: -11 (standard error 15).
Commands: orders per match 522 -> 525, can't-go reactions 1875 -> 1862, orders followed by one 278 -> 276.

**Self-play, seeds 1 to 30** (win rate and margin as defined above; before -> after):

| maps | bot | seats | win rate | margin | can't-go reactions | orders followed by one |
|---|---|---|---|---|---|---|
| all three | standard:hard | 1080 | 0.640 -> 0.649 | +221 -> +219 | 59976 -> 58653 | 2229 -> 2310 |
| all three | standard:medium | 2160 | 0.145 -> 0.142 | -435 -> -436 | 83686 -> 82919 | 3069 -> 3091 |
| all three | standard:easy | 1080 | 0.057 -> 0.054 | -756 -> -746 | 15742 -> 15301 | 775 -> 645 |
| TREASURE | standard:hard | 360 | 0.869 -> 0.881 | +480 -> +474 | 52401 -> 52469 | 948 -> 973 |
| TREASURE | standard:medium | 720 | 0.058 -> 0.056 | -754 -> -758 | 70719 -> 70077 | 1627 -> 1628 |
| TREASURE | standard:easy | 360 | 0.008 -> 0.003 | -1523 -> -1501 | 2842 -> 2628 | 299 -> 282 |
| SMALL | standard:hard | 360 | 0.544 -> 0.564 | +28 -> +31 | 5179 -> 3780 | 998 -> 1050 |
| SMALL | standard:medium | 720 | 0.186 -> 0.178 | -209 -> -208 | 9863 -> 9719 | 957 -> 970 |
| SMALL | standard:easy | 360 | 0.058 -> 0.053 | -271 -> -268 | 10763 -> 10331 | 415 -> 306 |
| MEDIUM | standard:hard | 360 | 0.506 -> 0.503 | +154 -> +153 | 2396 -> 2404 | 283 -> 287 |
| MEDIUM | standard:medium | 720 | 0.190 -> 0.193 | -344 -> -344 | 3104 -> 3123 | 485 -> 493 |
| MEDIUM | standard:easy | 360 | 0.106 -> 0.106 | -473 -> -469 | 2137 -> 2342 | 61 -> 57 |

**Self-play, seeds 31 to 60** (win rate and margin as defined above; before -> after):

| maps | bot | seats | win rate | margin | can't-go reactions | orders followed by one |
|---|---|---|---|---|---|---|
| all three | standard:hard | 1080 | 0.638 -> 0.632 | +225 -> +221 | 60380 -> 57682 | 2339 -> 2377 |
| all three | standard:medium | 2160 | 0.151 -> 0.152 | -452 -> -450 | 82419 -> 85350 | 2908 -> 2854 |
| all three | standard:easy | 1080 | 0.047 -> 0.050 | -777 -> -773 | 17764 -> 19368 | 931 -> 992 |
| TREASURE | standard:hard | 360 | 0.867 -> 0.856 | +493 -> +481 | 51685 -> 49446 | 969 -> 970 |
| TREASURE | standard:medium | 720 | 0.064 -> 0.069 | -808 -> -799 | 68652 -> 70815 | 1656 -> 1607 |
| TREASURE | standard:easy | 360 | 0.003 -> 0.000 | -1574 -> -1568 | 4007 -> 5096 | 333 -> 363 |
| SMALL | standard:hard | 360 | 0.503 -> 0.508 | +5 -> +8 | 5276 -> 5082 | 1067 -> 1100 |
| SMALL | standard:medium | 720 | 0.212 -> 0.207 | -196 -> -197 | 11109 -> 11832 | 991 -> 978 |
| SMALL | standard:easy | 360 | 0.044 -> 0.053 | -267 -> -263 | 12797 -> 13154 | 541 -> 572 |
| MEDIUM | standard:hard | 360 | 0.544 -> 0.533 | +177 -> +176 | 3419 -> 3154 | 303 -> 307 |
| MEDIUM | standard:medium | 720 | 0.176 -> 0.181 | -353 -> -353 | 2658 -> 2703 | 261 -> 269 |
| MEDIUM | standard:easy | 360 | 0.094 -> 0.097 | -490 -> -489 | 960 -> 1118 | 57 -> 57 |

**What the numbers say.** (Before: the task off, the shipped plan; after: the task on.) The task takes few ants, about two a match, and for those it does what it is meant to do. In a counting build of the task (before its can't-go rule; the counters are not part of the repository), over the 240 matches against `gang:hard` of the table above it took 484 ants; 361 walked into the hill and came out healed, and 118 were killed on the way. A hurt ant that walks home is followed by the enemy that hurt it, and the gang strikes the ant that is hurt first. So the deaths did not fall: over the five sets against `gang:hard` the ants that died were 265 before and 261 now, those that "could have walked home" 143 before and 134 now (the bar was a fall to 0.4 of that), the heals 2381 before and 2333 now (most heals in a match are an ant of 9 or 8 hit points that banks its food), the ants killed in the fights 215 before and 239 now and the ants lost in them 97 and 89, and the mean score 3239 and 3276 (a paired difference of +36 points with a standard error of 32: no change). The hit-back share fell from 62.5 to 58.1 percent: a hurt ant leaves the fight that it was answering. Against the milder gang there is no change either way (a paired score difference of -11, standard error 15). In the Hard mirror (four Hard bots, 96 seats on seeds 1 to 24) the ants that died fall from 200 to 177, and the ants lost in the fights are 72 and 71; the mean score is 1918 and 1872.

**Tune keys that move the numbers** (Hard against `gang:hard`, the five sets, 240 matches, one set of keys at a time on top of the task turned on; deaths, deaths that could have walked home, heals, ants lost and killed in the fights, mean score): the task off (the shipped plan, the bot of Step 1) 265, 143, 2381, 97 and 215, 3239; the task on with the level's numbers (`heal=1`) 261, 134, 2333, 89 and 239, 3276. `heal=1,healhp=5` 252, 125, 2434, 88 and 252, 3266; `heal=1,healcalm=4` 280, 142, 2371, 94 and 233, 3235; `heal=1,healwalk=24` 267, 143, 2397, 94 and 236, 3249; `heal=1,healhp=6` 279, 144, 2489, 95 and 250, 3225; `heal=1,healdanger=5` 268, 137, 2382, 90 and 236, 3252; `heal=1,healhp=5,healdanger=5,healcalm=4` 272, 136, 2430, 99 and 250, 3231. None of them comes near the bars of items 1 and 2: the fewest deaths that could have walked home is 125 against 143 without the task (the noise of 240 matches is about 12).


## The bench bot `gang2`

`gang` (above) is far weaker than the strong players whose games it stands for. The reference figures from games against people, round numbers, say what a strong player does against the standard bot on TREASURE: two to four of the four Thief power-ups in the first half minute; Thieves that harvest between raids and raid about 15 to 35 times in a game; an economy as good as the bot's with about 65 orders a minute (the bot gives about 20); all six ants, Thieves and the Fire Ant too, in one group from the first minute on, told again after every blow (two to three ants to an order), and two thirds of the consecutive blows on the same ant; hurt ants that go home and come back healed; one hunter near the other side's door; few walls and no bombs. The Hard bots against such a player got no points, lost about 15 ants and killed none, and all of their Fire Ants died. `gang` does not do that: Hard keeps 3,308 points against its 243 (the bot before Step 1), the Hard Fire Ant dies in 42 of 116 cases and all the raids of `gang` together take about 20 points a match from Hard.

`gang2` is a second test-only bench bot (`tools/bench_gang2.hpp`; `gang` and every shipped bot play exactly as before, see "The identity of the shipped bots" below). `bot_arena` and `bot_watch` seat it as `--seat N=gang2:LEVEL`. It is a client like every bot: the view, the orders, the controller and its level's profile; it reads nothing that a player cannot see, and at Hard it gives about 63 orders a minute (the reference is about 65; `gang` gives 23, the standard bot at Hard 38). In the order of its looks:

1. **The economy.** The standard bot's own harvest and power-up tasks, with none of its fight tasks, walls, mines or eggs. Thief power-ups first (three, those of the other sides too), then two Combat Ants, then one Fire Ant.
2. **Raids.** A Thief with empty hands raids the hill with the highest score box (30 points or more) whose thief hole is open, the moment it is, and harvests between raids. A Thief of the gang leaves it for a raid at once.
3. **One gang.** From tick 700 every free ant, healthy and with empty hands: the Thieves while no raid is open, the Fire Ant and the Combat Ants too. The target is the enemy ant that strikes one of the gang now, else a Thief or Fire Ant, else the nearest (the weakest of equal ones); never an ant that carries food or loot, stands on a power-up or on a hill. One strike order names every gang ant that is idle after its blow and, every 24 ticks, every one that is walking to the target (it has moved), until the target is dead or more than 10 tiles from the gang. A gang ant with 3 hit points or fewer goes to its hill and rejoins at 8. With no target the gang stands 3 tiles in front of the victim's thief hole (the victim is the enemy with the highest score box): while the hole is shut by fire walls, nobody renews them with a Thief and a gang there.
4. **The hunter.** The first Combat Ant is not of the gang: it stands 4 tiles from the victim's hill entrance on the way to its nearest pile, strikes any enemy ant within 3 tiles that is not a Combat Ant and does not stand in a group of three or more of the other side, again after every blow, and goes home at 3 hit points or fewer.
5. No fire walls of its own, no mines, no eggs.

**What the engine taught.** A blow takes 1 hit point; a Combat Ant's blow takes 2 and throws the victim up to 4 tiles back (into water it drowns). An ant that is left with 1 hit point walks home by itself and is healed inside, so a worker's blow cannot finish an ant that has 2 left, and blows on an ant that is in its hit clip are not possible: more ants keep a victim from walking away but do not make the blows come faster. That is why the Combat Ants matter (two power-ups, the second one is of the gang) and why the order that is given again to the ants that are still walking matters more than the size of the gang. Drowning is two in five of the kills of the bench bot (1.7 of 4.1 a match) and half of its losses (0.7 of 1.2) on TREASURE.

**The targets**, against the bot before Step 1 (the first two columns) and the bot now (the last two); the figures are per seat-match of the Hard bot (the bench bot's own figures for raids and score), 48 of them for seeds 1 to 24 and 240 for the five sets of seeds 1 to 120.

| measure | reference figure (target) | seeds 1 to 24 | five sets | now: seeds 1 to 24 | now: five sets | met against the bot before Step 1 |
|---|---|---|---|---|---|---|
| Hard Fire Ants that die | all (at least 70%) | 93 of 115 (81%) | 449 of 572 (78%) | 75 of 115 (65%) | 412 of 583 (71%) | yes |
| raids of `gang2` a match | about 15 to 35 (at least 10) | 13.7 | 11.3 | 8.5 | 9.3 | yes |
| ants Hard lost a match | about 15 in a game (at least 6) | 4.40 | 4.12 | 3.96 | 3.85 | no |
| ants Hard killed a match | none (at most 1.5) | 0.75 | 1.23 | 1.81 | 1.99 | yes |
| Hard's score | none (at most 1,654, half of 3,308) | 1,561 | 1,769 | 1,867 | 1,816 | on seeds 1 to 24 only |
| `gang2`'s score | (at least 2,000) | 421 | 323 | 241 | 272 | no |
| orders a minute of `gang2` | about 65 | 63.5 | 63.4 | 61.6 | 60.5 | |

Two targets are not reached and a third only on seeds 1 to 24; the bench bot was not given what a person lacks to reach them. **Hard loses 4 ants a match, not 6**: an ant that is left with 1 hit point walks home by itself and is healed, and only the Combat Ants' 2-point blow and the water finish an ant that has 2 left. **Hard's score** is 1,561 on seeds 1 to 24 and 1,769 on the five sets (the standard error of the mean of 240 matches is about 54); against `gang` it is 3,308. **`gang2` scores 240 to 420, not 2,000**: it banks about 900 a match (the standard bot at Hard against an idle seat scores 3,598 on this map, seeds 1 to 8) because the ants of its group do not harvest, and the Hard Thieves take about 550 from it, since it lights no wall at its own hole. A bench bot that harvests in the pauses of the fight and shuts its own hole would score more, and would be a different opponent from the one that the reference figures describe; the reference figures describe players whose raids met bots with little left to defend with.

**Hard against `gang2`, seeds 1 to 24** (both seats of each seed, TREASURE, Release build, the arena's sink latency of 3 ticks; "before Step 1" is `standard+styled=1,defence=0:hard`, see below; `gang2` at Hard unless the row says Medium; Medium and Easy are seeds 1 to 8). The same three tables as above, with the points the seat banked, the points raids took from it and its orders a minute added:

**Fights in which the seat was struck first**

| run | seat | seats | struck first | hit back | helped (median, p90 ticks) | strikers own / enemy | at the fight own / enemy | lost / killed in fights |
|---|---|---|---|---|---|---|---|---|
| Hard before Step 1 vs gang2:hard | standard+styled=1,defence=0:hard | 48 | 716 | 129 (18%) | 107 (15%) (211, 540) | 0.27 / 2.34 | 2.09 / 3.40 | 108 / 10 |
| Hard before Step 1 vs gang2:hard | gang2:hard | 48 | 59 | 39 (66%) | 32 (54%) (42, 383) | 2.17 / 1.34 | 3.29 / 3.00 | 9 / 15 |
| Hard now vs gang2:hard | standard:hard | 48 | 635 | 167 (26%) | 155 (24%) (160, 511) | 0.55 / 2.38 | 2.13 / 3.34 | 103 / 33 |
| Hard now vs gang2:hard | gang2:hard | 48 | 118 | 75 (64%) | 71 (60%) (53, 384) | 2.04 / 1.66 | 3.14 / 2.89 | 13 / 17 |
| Medium vs gang2:hard | standard:medium | 16 | 213 | 36 (17%) | 32 (15%) (254, 410) | 0.31 / 2.56 | 1.77 / 3.56 | 63 / 1 |
| Medium vs gang2:hard | gang2:hard | 16 | 22 | 14 (64%) | 14 (64%) (72, 217) | 2.18 / 1.27 | 3.45 / 3.09 | 0 / 5 |
| Easy vs gang2:hard | standard:easy | 16 | 232 | 16 (7%) | 16 (7%) (450, 1377) | 0.11 / 3.16 | 1.49 / 4.28 | 115 / 0 |
| Easy vs gang2:hard | gang2:hard | 16 | 2 | 2 (100%) | 2 (100%) (159, 215) | 2.00 / 1.00 | 3.50 / 3.00 | 0 / 0 |
| Hard now vs gang2:medium | standard:hard | 48 | 378 | 186 (49%) | 171 (45%) (96, 328) | 1.17 / 2.04 | 3.02 / 3.19 | 19 / 25 |
| Hard now vs gang2:medium | gang2:medium | 48 | 314 | 171 (54%) | 143 (46%) (108, 377) | 1.29 / 1.74 | 2.73 / 3.05 | 43 / 8 |

**Deaths, heals, Fire Ants, mines, blows**

| run | seat | deaths | could walk home / far / no warning | heals | Fire Ants (died) | walls at enemy gates | home mines by 3,000 / 6,000 / 12,000 | away mines (hits) | blows struck | focus |
|---|---|---|---|---|---|---|---|---|---|---|
| Hard before Step 1 vs gang2:hard | standard+styled=1,defence=0:hard | 212 | 117 / 45 / 50 | 1076 | 115 (93) | 120 | 0 / 0 / 0 | 21 (2) | 786 | 45% |
| Hard before Step 1 vs gang2:hard | gang2:hard | 36 | 18 / 8 / 10 | 176 | 55 (13) | 0 | 0 / 0 / 0 | 0 (0) | 3661 | 67% |
| Hard now vs gang2:hard | standard:hard | 191 | 109 / 46 / 36 | 1053 | 115 (75) | 124 | 0 / 0 / 0 | 1 (1) | 1439 | 67% |
| Hard now vs gang2:hard | gang2:hard | 88 | 25 / 35 / 28 | 235 | 53 (19) | 0 | 0 / 0 / 0 | 0 (0) | 3814 | 69% |
| Medium vs gang2:hard | standard:medium | 97 | 64 / 17 / 16 | 339 | 24 (24) | 0 | 0 / 0 / 0 | 10 (7) | 266 | 25% |
| Medium vs gang2:hard | gang2:hard | 7 | 1 / 0 / 6 | 71 | 16 (1) | 0 | 0 / 0 / 0 | 0 (0) | 1519 | 69% |
| Easy vs gang2:hard | standard:easy | 163 | 102 / 26 / 35 | 248 | 18 (17) | 0 | 0 / 0 / 0 | 0 (0) | 31 | 0% |
| Easy vs gang2:hard | gang2:hard | 0 | 0 / 0 / 0 | 8 | 16 (0) | 0 | 0 / 0 / 0 | 0 (0) | 1715 | 68% |
| Hard now vs gang2:medium | standard:hard | 57 | 25 / 8 / 24 | 999 | 109 (27) | 173 | 0 / 0 / 0 | 0 (0) | 2615 | 61% |
| Hard now vs gang2:medium | gang2:medium | 170 | 48 / 59 / 63 | 409 | 56 (30) | 0 | 0 / 0 / 0 | 0 (0) | 2349 | 49% |

**Points and ants**

| run | seat | seats | score | ants killed | ants lost | ants left | banked | raided | orders a minute | raids a match (raid orders) |
|---|---|---|---|---|---|---|---|---|---|---|
| Hard before Step 1 vs gang2:hard | standard+styled=1,defence=0:hard | 48 | 1561 | 0.75 | 4.40 | 2.0 | 2320 | 680 | 35.4 | - |
| Hard before Step 1 vs gang2:hard | gang2:hard | 48 | 421 | 4.40 | 0.75 | 5.3 | 1015 | 594 | 63.5 | 13.7 (16.4) |
| Hard now vs gang2:hard | standard:hard | 48 | 1867 | 1.81 | 3.96 | 2.5 | 2341 | 409 | 37.5 | - |
| Hard now vs gang2:hard | gang2:hard | 48 | 241 | 3.96 | 1.83 | 4.3 | 745 | 497 | 61.6 | 8.5 (11.4) |
| Medium vs gang2:hard | standard:medium | 16 | 604 | 0.44 | 6.00 | 1.2 | 1730 | 962 | 12.0 | - |
| Medium vs gang2:hard | gang2:hard | 16 | 823 | 6.00 | 0.44 | 5.6 | 1295 | 471 | 55.8 | 20.7 (23.4) |
| Easy vs gang2:hard | standard:easy | 16 | 8 | 0.00 | 10.12 | 0.7 | 1049 | 979 | 6.0 | - |
| Easy vs gang2:hard | gang2:hard | 16 | 1400 | 10.12 | 0.00 | 6.0 | 1406 | 6 | 48.4 | 22.8 (23.7) |
| Hard now vs gang2:medium | standard:hard | 48 | 2893 | 3.50 | 1.17 | 4.8 | 2990 | 97 | 45.5 | - |
| Hard now vs gang2:medium | gang2:medium | 48 | 2 | 1.17 | 3.50 | 2.8 | 474 | 472 | 43.2 | 1.9 (5.3) |

The five sets of seeds 1 to 120 (240 seat-matches each):

**Points and ants**

| run | seat | seats | score | ants killed | ants lost | ants left | banked | raided | orders a minute | raids a match (raid orders) |
|---|---|---|---|---|---|---|---|---|---|---|
| five sets, Hard before Step 1 | standard+styled=1,defence=0:hard | 240 | 1769 | 1.23 | 4.12 | 2.1 | 2386 | 560 | 36.8 | - |
| five sets, Hard before Step 1 | gang2:hard | 240 | 323 | 4.10 | 1.23 | 4.9 | 892 | 569 | 63.4 | 11.3 (14.2) |
| five sets, Hard now | standard:hard | 240 | 1816 | 1.99 | 3.85 | 2.5 | 2333 | 456 | 36.8 | - |
| five sets, Hard now | gang2:hard | 240 | 272 | 3.84 | 2.00 | 4.2 | 788 | 511 | 60.5 | 9.3 (12.2) |

**What was tried** (Hard before Step 1, TREASURE, both seats of every seed, the sets of seeds 25 to 96 unless the matches are fewer; one change at a time on top of the row above it for the first six rows, then each on top of the third row unless it says otherwise; Hard's score, ants Hard lost / killed, Hard Fire Ants that died, raids a match of the bench bot, the bench bot's score). A difference of about 150 points of Hard's score is within the noise of 144 matches (the standard error of the mean is about 70).

| change | matches | Hard score | Hard lost / killed | Fire Ants died | raids | bench score |
|---|---|---|---|---|---|---|
| `gang` (the bench bot of Step 0) | 48 | 3228 | 1.88 / 2.08 | 38% | 0.0 | 306 |
| first `gang2`: one permanent group of every free ant, waiting at the victim's nearest pile | 48 | 2421 | 3.15 / 1.90 | 62% | 3.5 | 104 |
| ... waiting 3 tiles in front of the victim's thief hole, the ant that strikes first, Thieves raid while the group fights, one Combat Ant | 144 | 2476 | 2.62 / 2.53 | 58% | 7.4 | 164 |
| ... + the walking ants told again every 40 ticks | 144 | 2000 | 3.40 / 1.47 | 68% | 9.5 | 231 |
| ... + two Combat power-ups, every 20 ticks | 144 | 1715 | 4.24 / 1.12 | 80% | 11.0 | 345 |
| **the final bot**: two Combat power-ups, every 24 ticks | 144 | 1775 | 4.09 / 1.24 | 78% | 11.3 | 318 |

Changes that did not help (each on top of the third row of the table above unless it says otherwise; the rows of 48 matches are seeds 25 to 48):

| change | matches | Hard score | Hard lost / killed | Fire Ants died | raids | bench score |
|---|---|---|---|---|---|---|
| a group of at most 4 ants | 48 | 2778 | 2.38 / 2.67 | 42% | 2.6 | 146 |
| Thieves never of the group | 144 | 3488 | 1.03 / 3.17 | 26% | 0.6 | 209 |
| two Thief power-ups instead of three | 144 | 2623 | 2.47 / 2.31 | 53% | 5.3 | 110 |
| four Thief power-ups | 144 | 2517 | 2.58 / 2.46 | 57% | 6.9 | 163 |
| group from tick 300 | 144 | 2488 | 2.62 / 2.61 | 57% | 7.1 | 158 |
| blow gap 8 ticks | 144 | 2512 | 2.68 / 2.51 | 58% | 6.2 | 153 |
| home at 4 hit points | 144 | 2484 | 2.63 / 2.10 | 58% | 7.0 | 146 |
| reach and leash from the group's centre | 144 | 2547 | 2.52 / 2.38 | 54% | 6.5 | 135 |
| waiting at the victim's pile (with centre) | 144 | 2561 | 2.52 / 2.42 | 52% | 5.8 | 133 |
| re-order every 12 ticks (85 orders a minute) | 144 | 1835 | 4.04 / 1.15 | 75% | 9.8 | 294 |
| no hunter, Combat 2, every 20 | 144 | 1856 | 4.10 / 1.05 | 81% | 11.0 | 319 |
| no hunter, three Combat power-ups, every 20 | 144 | 1823 | 4.26 / 0.93 | 84% | 10.6 | 293 |
| the group gathers before it strikes | 48 | 2605 | 2.56 / 2.85 | 50% | 3.9 | 105 |
| carriers are targets | 48 | 2753 | 1.98 / 4.58 | 42% | 2.0 | 76 |
| the group forms on demand | 48 | 3928 | 0.42 / 3.23 | 10% | 0.3 | 313 |
| a Fire Ant puts out one hole wall | 48 | 2673 | 1.98 / 3.44 | 46% | 5.5 | 92 |
| Combat Ants finish (parity of the hit points), no stop | 144 | 2641 | 2.26 / 3.24 | 44% | 6.1 | 82 |
| ... with a stop for the others | 144 | 2765 | 2.12 / 3.15 | 38% | 4.7 | 55 |
| an ant at 1 hit point is not chased | 144 | 1868 | 4.02 / 1.31 | 73% | 9.6 | 304 |
| targets that the water kills first, on the final bot | 144 | 1747 | 4.13 / 1.31 | 77% | 11.6 | 337 |

What did not help or hurt: a group of at most four ants, Thieves kept out of the group (Hard keeps 3,488: the three Thieves are half of the ants), a different number of Thief power-ups, forming the group at tick 300 or only on demand (the ants that harvest are too far to come), a shorter gap between two blows, going home at 4 hit points, measuring reach and leash from the group's centre, waiting at the victim's pile instead of in front of its hole, making the group gather before it strikes, carriers as targets (the fights are with the ants that carry food and they hit back), a Fire Ant that puts out a wall of the hole (the wall keeper lights it again within about 100 ticks, before a Thief is there), preferring targets that the water would kill, letting only Combat Ants strike at an even number of hit points (to finish with the 2-point blow, with or without stopping the others: it halves the strikes), not chasing an ant that walks home at 1 hit point, a third Combat power-up, and keeping the hunter out of the group (the last two are the same within the noise).

**The tests.** `tests/test_ai/test_ai_gang2.cpp` (suite 2.20, [TESTING.md](TESTING.md)), AI37.1 to AI37.12, on the 60 by 60 field of grass with four hills and the Hard profile (a look every 4 ticks, a reaction of 8): the kind is known to the bench factory and not to `make_bot`, no order is filtered or refused, a match is played twice alike, and the bot takes Thief and Combat power-ups in the opening (AI37.1); four workers strike the nearer of two idle enemy workers, one order names them all, the walking ones are told again, and no blow falls on the other one until the first is down to 1 hit point (AI37.2); a Thief or a Fire Ant is struck before a worker that is nearer, and the ant that strikes one of the gang before a Thief that is as near (AI37.3); a gang ant with 3 hit points is sent home at once, is healed and strikes again (AI37.4); a Fire Ant and, while no raid is open, a Thief strike beside the workers (AI37.5); a Thief raids a hill whose hole is open and takes loot, and does not raid one whose three tiles are shut (AI37.6); an enemy ant that carries food is left alone (AI37.7); the first Combat Ant walks to its post alone and the second one is of the gang (AI37.8); the nearer of two enemy ants is struck first, whichever is older (AI37.9); at equal distances the one with fewer hit points, and an ant that comes nearer later does not take the blows from the victim (AI37.10); with no enemy ant in reach the gang stands in front of the thief hole of the richest enemy hill (AI37.11); a hurt gang ant is in no strike order, a gang ant that takes food leaves the gang, an invitation to team up is refused, the hunter goes home at 3 hit points, and a Thief of the gang leaves it for a raid when a hole opens (AI37.12). Not covered: the hunter's own strike rules (a Combat Ant strikes an enemy in reach by reflex before the bot looks, so no scene shows what the bot orders). The 41 mutants are `tests/data/bot_gang2_mutants.json` (`python3 tools/mutate.py tests/data/bot_gang2_mutants.json --keep-going --jobs 1 --timeout 900`).

**The identity of the shipped bots.** Nothing under `src/ants_ai` or `include/ants_ai` changed. `bot_arena --selftest` passes (182 checks); `bot_arena --write-baselines` is byte for byte `tests/test_ai/baselines.inc`; 24 seeds of Hard against `gang:hard` (both seats, with `--replay-check`), 8 matches of Medium against Easy and the mixed arena matches give reports that `cmp` finds identical for a build of the base and a build with `gang2`; and `test_ai` in full has the cases and assertions of the base plus those of AI37.

**The bot before Step 1.** The numbers "before Step 1" in this page are those of a build of the bot before the defence rules. Here the bot before Step 1 is the current build with `defence=0` (`--seat 0=standard+styled=1,defence=0:hard`): Step 1 says that this plays exactly as before, and the 48 matches of seeds 1 to 24 against `gang:hard` end on the same state hashes and scores as the report of that build (48 of 48).

## Step 3: fight as a group

A Hard bot that is struck answers with the ants near the target (the defence batch of Step 1: the ants within 10 tiles, as many as the enemy has there and one more, a fight kept up from 60 percent of the enemy's strength). The group batch calls the ants within **20 tiles** of the target instead (`group_reach`; the leash no longer caps the reach). Nothing else of the fight changes: every defender strikes the same target, the struck ant first, a Combat Ant before the nearest ant, the ants that a job of rank 3 or 4 holds after the free ones, no carrier, nobody under 4 hit points, and the walk check across water. The batch is **built and switched off**: `group_fight` is off in the shipped plan of every level, so every bot plays as it did before this step (below: the same state hashes in 480 matches against the bench bots, and the same reports and baselines). `bot_arena --tune group=1` (or `--seat 0=standard+styled=1,group=1:hard` for one seat) turns it on with the level's reach (Hard 20, Medium 14, Easy the plan's own 20; Medium also needs `defence=1`: its defence batch is off, and the reach belongs to the fights that the defence batch sizes by strength), `greach=N` sets the reach (the leash stays) and `group=0` takes it out again. The code is `recruit_reach` (`include/ants_ai/group_fight.hpp`, `src/ants_ai/group_fight.cpp`), which `FightTask::take_defenders` (`src/ants_ai/defence.cpp`) asks. It reaches the fights that are sized by strength (`defend_by_strength`); the game type 187 plays the brawl and has nothing for it to change.

**Why it is off.** The numbers do not support turning it on: against `gang2:hard` it adds 0.2 to 0.4 kills a match (0.40 on 720 seat-matches, 0.19 on 240 more that were not used to choose the reach) and takes 0.03 to 0.1 fewer ants, which is under the bars that the step was measured against (0.5 ants lost fewer or 0.5 kills more), and against `gang:hard` it costs up to 2 percent of the score (0 on one set of seeds; below). The table of what answering more does, and the counts that set the ceiling of any rule of this kind, follow. The maintainer decides: leave it off, turn it on for Hard (`group_fight` in `plan_for`: one line of the plan, with the pins that say "off" (AI9.5, AI11.6, AI38.1, AI38.5 and the check of the tune keys in the self-test of `bot_arena`) and two scenes that react to Hard's play (AI25.8's bank bound and AI37.1's Combat power-up in the opening) changed with it), or drop it. The cases AI34.2 and AI34.3 (marked for this step) and AI34.5 (marked for Step 5) print as known failures before and after, with the batch on or off: the group batch does not make them pass. (AI34.2 asks for two ants against a camper to strike it; the reserve rule, which keeps the last ants out of fights and which this batch does not change, is the likely reason; not tested.)

**What a rule of this kind can reach.** Hard against `gang2:hard`, TREASURE, seeds 1 to 120, both seats (240 Hard seat-matches), counted with an observer of the engine that is not part of the repository (it reads every ant after every tick, as `bot_watch` does). Per seat-match:

| | ants |
|---|---|
| ants lost | 3.88 |
| ... drowned (a blow threw the ant into water) | 1.54 |
| ... by a blow, with an enemy ant within 3 tiles | 2.33 |
| ... ... with no own ant within 6 tiles (alone) | 0.98 |
| ... ... outnumbered (the ants of the seat that could strike within 10 tiles and the victim, fewer than the enemy ants within 5) | 1.69 |
| ... ... with an own ant that could strike within 10 tiles and had not struck for 100 ticks | 1.32 |
| ... ... ... and those ants and the victim outweigh the enemy within 5 tiles (a group rule could have won this fight) | **0.28** |
| ... ... ... and they are as many as the enemy | **0.47** |
| ... ... of an ant on a trip for a power-up, a wall, a mine, a bomb or a fire-in, into a place where an own ant was struck or lost in the last 1,200 ticks | 0.13 |
| ... ... of an ant hatched in the last 1,500 ticks | 0.07 |
| ... ... by a blow within 6 tiles of the own hill, the ant carrying nothing | 0.79 |

A perfect rule that saved every death of the three kinds that a group, a memory of dangerous places and the hatching could reach, each death counted once, would save 0.47 ants a match (0.65 with "as many"). Of the 2,971 fights that the seat was struck first in (12.4 a match), 72 percent were never answered; in 580 of them (2.4 a match) the victim and the ants near outweighed the enemy at the first blow: 257 were answered within 100 ticks (0.26 kills and 0.14 ants lost a fight) and 323 were not (0.10 and 0.17); answering all of them as well as the first 257 would bring 0.22 kills a match and save 0.04 ants, and the fights with as many ants on both sides would bring 0.27 kills and cost 0.15 ants. Against `gang:hard` the ceiling is 0.2 ants a match (1.10 lost, half of them drowned). Two of the rules that were considered are not built:

- *A door.* A hunter (an enemy that struck an own ant in the last 600 ticks) stands within 6 tiles of Hard's hill in 46 percent of the ticks of a match, in every one of the 240, and 43 percent of the points that Hard banks (998 of 2,333 a match) come in those ticks. Keeping every ant in while one stands there would save at most the 0.79 ants a match that die at the door with nothing in their hands, and give up up to a third of the score; answering it with the ants near when they outweigh it is the group rule above (0.10 ants a match at the door).
- *A memory of dangerous places and hatching only into an answer.* 0.13 and 0.07 ants a match, with the lone trips of the economy (533 of the 931 ants lost, more than half) outside both.

**What answering more does.** The same seeds, paired against the shipped Hard (the mean difference per seat-match and its standard error; the bot is `standard+styled=1,K=V:hard`, which plays like `standard:hard`). `dkeep=0` is the plain answer (no fight is ever called off for weakness):

| Hard seat | seat-matches | score | kills | ants lost | hit back | helped |
|---|---|---|---|---|---|---|
| as shipped | 720 | 1833 | 2.06 | 3.86 | 28% | 25% |
| `dkeep=0` (every blow answered) | 480 | -275 (SE 57) | +1.70 (SE 0.15) | +0.32 (SE 0.15) | 42% | 37% |
| `dkeep=100`, `150`, `200` (answer only a stronger force) | 240 | -29, +1, +11 (SE 62 to 73) | -0.18, -0.23, -0.33 (SE 0.16 to 0.17) | +0.20, +0.05, +0.06 (SE 0.15 to 0.17) | 25%, 24%, 24% | 22%, 20%, 20% |
| `dmax=8,dextra=3` (more defenders) | 240 | +18 (SE 44) | +0.14 (SE 0.14) | 0.00 (SE 0.10) | 30% | 27% |
| **`group=1`** (reach 20) | 720 | -10 (SE 35) | **+0.40** (SE 0.10) | **-0.12** (SE 0.09) | 33% | 30% |
| `group=1,dkeep=50` (reach 20, fights kept from 50 percent) | 720 | -60 (SE 40) | +0.55 (SE 0.11) | -0.03 (SE 0.10) | 36% | 33% |
| `group=1`, other seeds (401 to 520, measured separately; shipped 1855, 2.26, 3.74) | 240 | -11 (SE 59) | +0.19 (SE 0.18) | -0.03 (SE 0.14) | 28% -> 32% | 25% -> 29% |

Plain answering buys 1.7 kills a match and pays for it with 0.3 ants and 275 points (15 percent of the score): the ants that fight do not bring food. A rule that answers only a stronger force answers less and loses no fewer ants. The reach is the one lever that moved the kills without costing the score: `dreach=16`, `20`, `24` (the reach with the leash out of it) gave +0.42, +0.62 and +0.57 kills a match on the first 240 seat-matches; the leash alone (`leash=20`) gave nothing, and a reach and a leash of 30 tiles cost score again (-50, SE 67). The numbers of the last two rows are the pooled ones of 15 sets of 24 seeds (seeds 1 to 360, both seats). The reach was chosen on the first five sets; the next five confirmed it and the last five were measured after the choice was made, and the gain is smaller there: for `group=1` the three blocks give kills +0.62, +0.18 and +0.39, ants lost -0.31, +0.07 and -0.12, score +25, -60 and +5; for `dkeep=50` kills +0.80, +0.50 and +0.35, ants lost -0.20, -0.02 and +0.14, score 0, -48 and -132. The standard errors are those of the paired means (0.17 to 0.20 kills and 0.15 to 0.17 ants a block of 240 seat-matches); a run of 24 seeds shows differences of a few points of a rate that are noise.

**Hard against `gang:hard`, 15 sets of 24 seeds (720 seat-matches), paired against the shipped Hard** (`group=1`): score -66 (SE 25, 2 percent), ants killed 0.00, ants lost +0.04, hit back 60% -> 64%. On the other seeds (401 to 520, 240 seat-matches, measured separately) the same comparison gave score -0.3 (SE 35), kills +0.17 (SE 0.10), ants lost -0.15 (SE 0.08) and hit back 57% -> 66%, so the cost is up to 2 percent of the score and 0 on one set of seeds. Where there is a cost it is the economy's: the seat banks 60 points less, and the ants that come from 20 tiles away are food that is not carried.

**The numbers against the bars of the step, `group=1`** (against `gang2:hard`, 720 seat-matches, paired):

| bar | measured | |
|---|---|---|
| ants lost down by at least 0.5 a seat-match, or kills up by at least 0.5 with the ratio killed / lost better | kills +0.40, lost -0.12; ratio 0.53 -> 0.66 (seeds 401 to 520: +0.19, -0.03) | **not met** (kills +0.55 with `dkeep=50`: met, with the score below the bar) |
| hit back and helped up | 28% -> 33%, 25% -> 30% | met |
| deaths with no own ant within 6 tiles down | not measured with the batch on: the batch calls ants, it does not keep any off a trip | not applicable |
| score not below the shipped Hard minus 2 percent | -10 (SE 35): -0.5% | met (`dkeep=50`: -60, -3.3%: not met) |
| `gang:hard`: score and ants lost no worse | -66 (SE 25), ants lost +0.04 (seeds 401 to 520: -0.3 (SE 35), ants lost -0.15) | **not met** on the 720 (score, 2.6 standard errors); about 0 on the other seeds |
| self-play: Hard at most 3 points of win rate down, Medium and Easy within 1 point | seeds 1 to 30: Hard 0.640 -> 0.611, Medium 0.145 -> 0.159, Easy 0.057 -> 0.059; seeds 31 to 60: Hard 0.638 -> 0.599, Medium 0.151 -> 0.170, Easy 0.047 -> 0.050 (the Hard seat plays `group=1`, the others as shipped) | **not met** on seeds 31 to 60 (Hard -3.9 points, Medium +1.9) |
| the shipped bots play as before | the same state hashes and scores in 480 matches of Hard against `gang` and `gang2`; the same reports in 7 arena sets and the same baselines | met |

**The identity of the shipped bots.** `bot_arena --selftest` passes (183 checks: the 182 of the base and one for the tune keys `group` and `greach`); `bot_arena --write-baselines` is byte for byte `tests/test_ai/baselines.inc`; the JSON reports of a build of the base (the merge of the practice opponent) and of this build are byte for byte the same for 24 seeds of Hard against `gang:hard` and against `gang2:hard` (both seats, with `--replay-check`), 16 matches of Medium against Easy, 8 of Hard against Hard in the game type 187 and 24 more of the game type 187 with a Hard, a Medium and an Easy bot, 72 mixed arena matches (one Hard, two Medium and one Easy bot, TREASURE and SMALL, seeds 1 to 3, every arrangement, `--replay-check`) and 8 Hard mirror matches; the 480 matches of Hard against `gang` and `gang2` on seeds 1 to 120 end on the same state hashes and scores. Self-play, seeds 1 to 30 and 31 to 60 (TREASURE, SMALL and MEDIUM, one Hard, two Medium and one Easy bot in every arrangement, 1,080 matches each): the reports of the base and of this build are byte for byte the same, so the win rates are those of the table above (0.640 / 0.145 / 0.057 and 0.638 / 0.151 / 0.047). The cost of a look (AI13.1, Hard on TREASURE, microseconds, three alternating runs on a shared machine): 19.0, 22.0 and 26.7 before, 21.8, 18.1 and 17.5 now; AI13.2 passes.

**The tests.** `tests/test_ai/test_ai_group.cpp` (suite 2.20, [TESTING.md](TESTING.md)), AI38.1 to AI38.7, on the 60 by 60 field of grass with four hills and the profiles of the levels (a look every 4 ticks, a reaction of 4): the reach with the batch off (the defence batch's, capped by the leash) and on (the group's, which nothing caps), and the shipped plans of every level pin the batch off (AI38.1, AI9.5, AI11.6); an ant 20 tiles from the target joins a Hard bot's fight and gets a strike order, one 21 tiles away does not, in a straight line and on the diagonal, and with the batch off the line is at 10 and 11 tiles; Medium with the defence rules on by hand calls from 14 tiles (not 15) with the batch and from 8 (not 9) without (AI38.2); the share of the enemy's strength that keeps a fight up is the defence batch's with the batch on or off (three ants against six enemy workers: kept up to 24 hit points between them at 60 percent and called off at 25, kept up to 29 at 50 and called off at 30) (AI38.3); three enemy workers strike one ant, the two workers 15 tiles away are called and the fight stands, and without the batch it is called off at the look and nobody is ordered (AI38.4); the shipped bots of every level play as before on a fixed scene (the same counters, commands and state hash as the plan with the batch taken out, whatever the reach says), Hard with the batch turned on does not, and the game type 187 gives the same commands and hash with the batch on or off (AI38.5); a channel of water between the far ant and the target still keeps it out, a carrier is called from no distance, the struck ant answers first, and with `defend_by_strength` off an ant 30 tiles away is still called (AI38.6); an ant with 3 hit points is not called from 15 tiles, an ant that a job holds is called from 15 tiles when it is the only one, and a free ant 19 tiles away is called before an ant 3 tiles away that a job holds (AI38.7). The 21 mutants are `tests/data/bot_group_mutants.json` (`python3 tools/mutate.py tests/data/bot_group_mutants.json --keep-going --jobs 1 --timeout 900`): all 21 are caught by the case named for them, none survives (125 seconds; the unmutated tree passes before, in the middle and after). The last four were added after an independent check of the first 17: a far ant with 3 hit points, a far ant that a job holds, the tune key `group` forcing the reach to 20 and `greach` setting the leash; two more mutants of that check (the carrier rule without the `holding` test, and without the `carried_points` test) are equivalent, since a carrier has both set, and are left out.

## Commands

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DANTS_BUILD_APP=OFF
cmake --build build --target bot_arena bot_watch test_ai
build/bot_watch --map TREASURE --seeds 1..24 --seat 0=standard:hard --seat 1=gang:hard --rotate --out before_gang_hard.json
build/bot_watch --map TREASURE --seeds 1..24 --seat 0=standard:hard --seat 1=gang:medium --rotate --out before_gang_medium.json
build/bot_watch --map TREASURE --seeds 1..8 --seat 0=standard:hard --seat 1=standard:hard --seat 2=standard:hard --seat 3=standard:hard --out before_mirror_hard.json
build/bot_arena --map TREASURE,SMALL,MEDIUM --seeds 1..30 --seat 0=standard:hard --seat 1=standard:medium --seat 2=standard:medium --seat 3=standard:easy --rotate --threads 2 --quiet --out before_arena.json
ANTS_TEST_FILTER='AI34.' build/tests/test_ai/test_ai
ANTS_TEST_FILTER='AI35.' build/tests/test_ai/test_ai
ANTS_TEST_FILTER='AI36.' build/tests/test_ai/test_ai
ANTS_TEST_FILTER='AI37.' build/tests/test_ai/test_ai
ANTS_TEST_FILTER='AI38.' build/tests/test_ai/test_ai
build/bot_watch --map TREASURE --seeds 1..24 --seat 0=standard+styled=1,defence=0:hard --seat 1=gang2:hard --rotate --out gang2_before_step1.json
build/bot_watch --map TREASURE --seeds 1..24 --seat 0=standard:hard --seat 1=gang2:hard --rotate --out gang2_now.json
BOT_DIAG=1 build/bot_watch --map TREASURE --seeds 1..24 --seat 0=standard:hard --seat 1=gang2:hard --rotate --quiet 2>&1 | grep GANG2
```

The last line prints, for every seat that plays `gang2`, the raids that went through (a Thief seen in its raid clip), the raid orders, the strike orders and the ants they named, and the orders home.

The numbers of Step 3 were taken with `bot_watch` on five sets of 24 seeds three times over (`--seeds 1..24` to `337..360`), against `gang2:hard` and `gang:hard`, the Hard seat `standard:hard` and, with the batch on, `standard+styled=1,group=1:hard`; the other keys of the tables with a seat such as `--seat 0=standard+styled=1,group=1,dkeep=50:hard`, and the arena with the Hard seat tuned the same way (`--seat 0=standard+styled=1,group=1:hard`). The differences are paired by seed and seat (the mean of the per-seat-match difference and its standard error). The shipped plan plays as the bot of Step 2.

The numbers of Step 2 were taken with the same commands on the bot of Step 1 (a build of its merge, the report files `before_...`) and with the task turned on (`standard+styled=1,heal=1`, the report files `after_...`; the shipped plan has it off), on five sets of seeds against both `gang` levels (`--seeds 1..24`, `25..48`, `49..72`, `73..96`, `97..120`), the mirror on `1..8` and `9..24`, and the arena on `1..30` and `31..60`; the tune keys with a seat such as `--seat 0=standard+styled=1,heal=1,healhp=5:hard`. The shipped plan plays as the bot of Step 1: the 48 matches of seeds 1 to 24 against `gang:hard` end on the same state hashes, and so do 144 matches of Medium and Easy bots, 144 of one Hard, two Medium and one Easy bot and 96 of the game type 187. The mutants of the task are `tests/data/bot_heal_mutants.json` (`python3 tools/mutate.py tests/data/bot_heal_mutants.json --keep-going --jobs 1 --timeout 900`).

The numbers of Step 1 were taken with the same four commands (the report files are called `after_...`); the other sets of seeds with `--seeds 25..48` and so on (the bot before Step 1 is a build of the base branch), the keys of the table with a seat such as `--seat 0=standard+styled=1,dkeep=80:hard`. The mutants of the batch are `tests/data/bot_defence_mutants.json` (`python3 tools/mutate.py tests/data/bot_defence_mutants.json --keep-going --jobs 1 --timeout 900`).

The people lines of a run are the last lines that `bot_watch` prints without `--quiet` (one for each seat spec) and `"people_totals"` at the end of its report. Win rate and margin from the arena's report:

```
python3 - before_arena.json <<'EOF'
import json, sys
total = {}
for m in json.load(open(sys.argv[1]))["matches"]:
    for s in m["seats"]:
        best = max(o["score"] for o in m["seats"] if o is not s)
        for key in ((m["map"], s["bot"]), ("all", s["bot"])):
            t = total.setdefault(key, [0, 0, 0])
            t[0] += 1
            t[1] += s["score"] > best
            t[2] += s["score"] - best
for (where, bot), (n, wins, gap) in sorted(total.items()):
    print("%-9s %-16s %5d seats  win rate %.3f  margin %+.0f" % (where, bot, n, wins / n, gap / n))
EOF
```

## Limits

- **The numbers depend on the bench bot's details.** Before Step 1 the same Hard bot hit back in 26 percent of the fights against a gang that forms at tick 300 (seeds 1 to 24; 31 percent on seeds 25 to 48) and in 34 and 36 percent against one that forms at tick 600 (both arrangements of the seats). The table is a yardstick for comparing a bot with its next version against the same `gang`, not a measure of people.
- **A weaker force gets no help.** When the strength of the ants that would answer is below 60 percent of the strength of the strongest enemy ants near the target, the fight is called off at the look and the struck ant gets no help from it: a lone worker beside a group of four is not answered with three more workers. That keeps the bot from feeding ants to a stronger group. With `dkeep=0` (never called off for weakness) Hard against `gang:hard` hits back in 80.5% of the fights on seeds 1 to 24 and 68.1% on seeds 73 to 96 (61.1% and 57.3% as shipped), with ants lost and killed in the fights 18 / 59 and 23 / 54 (20 / 44 and 22 / 37) and the same score within the noise (3283 and 3206 against 3298 and 3125); in the Hard mirror (seeds 9 to 24) it hits back in 45.8% of the fights instead of 25.9% and the mean score falls from 1898 to 1653.
- **Against a weaker group the plan costs a little score.** Hard against `gang:medium`, four sets of 24 seeds (192 matches): the mean score is 3,609 before Step 1 and 3,535 now (-74 points, standard error 35, about 2 percent), probably because the ants that fight do not bring food meanwhile. Against `gang:hard` the score is unchanged within the noise (-28 points, standard error 44, five sets).
- **48 matches are a few hundred fights.** A difference of a few points of a rate is within the noise of a run; compare runs over the same seeds.
- **Ants lost and killed are counted two ways.** In the fights where the seat was struck first (above) the standard bot loses more ants than it kills; over the whole match the engine's counts are close to even against `gang` at Hard, because about half of the standard bot's kills are ants that a blow threw into water: they drown, and the fights count only the blow that takes the last hit point.
- **One map.** TREASURE is where the numbers were taken; the other maps change them.
- **The hit-back share is a ratio of two moving numbers.** A bot that answers well is struck first in fewer fights, and the fights that are left are the ones that nobody could answer. Read it together with the ants lost and killed in the fights and with the engine's own counts.
- **The "could have walked home" number does not measure what the task changes.** It asks whether the walk home was shorter than the time that the ant still lived. An ant that is hurt in the middle of a fight, with the enemy beside it, has the time and not the way: the enemy follows it, and a blow that lands during its walk stops it for a hit clip, so the walk home takes about as long as the ant has left. The gang does exactly this to the ant that it hurt (it strikes the same victim until it is dead or far away).
- **The task orders an ant again after a blow, and the engine takes no order for an ant that is in its hit clip or engaged.** The ant stands idle for some ticks after a blow (about 12 for the order to be given and carried out, more for a second blow); gaps of 40, 20, 12 and 8 ticks between two orders were tried in a build with the gap as a switch (not part of the repository): the five sets gave 136, 122, 134 and 127 deaths that could have walked home against 143 without the task, which is within the noise of 240 matches. The gap that ships is 40.
- **Orders followed by a can't-go reaction.** An ant that a blow throws into a tile that no walk joins to the hill is not ordered again, and two can't-go reactions end the job. One match of the 576 seat-matches of the tables above shows 98 orders followed by a reaction for one Fire Ant that stood in such a tile: they are the economy's harvest orders to that ant, which it repeats every 20 ticks or so (the ant cannot go anywhere); the task gave the ant up after the second reaction. The harvest task's loop is a separate matter and is not part of this step.
- The people numbers of a recording (`bot_watch --replay`) are worked out the same way; no recording is part of the repository.
