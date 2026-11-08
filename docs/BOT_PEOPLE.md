# Bots against people: the yardstick

A computer player is judged against people by what people do, not only by who wins. The win rate and the margin of `bot_arena` stay the check that a change did not cost strength ([BOTS.md](BOTS.md), "Running bots"); they say nothing about whether a player answers a blow, helps its own ants or brings a hurt ant home. This page holds what measures that: the **people numbers** of `bot_watch`, a bench bot named `gang` that plays like a small group of people, five scenes in which the standard bot is held to a rule against an opponent that a test scripts, and the numbers of the bot before and after the first change that was measured with them ([Step 1](#step-1-everyone-hits-back)). The yardstick itself changes no bot.

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

Hard now answers a blow the way a group of people does: every ant that can strike answers, the ant that was struck first of all, as many as the enemy has near the target and one more, and the fight is kept up through the target's hit clips for as long as the force is the stronger. Medium has the same rules built and **off** (`defence=1` turns them on); Easy is as it was. The rules are in `include/ants_ai/defence.hpp` and `src/ants_ai/defence.cpp`, and `FightTask` uses them for the fights that answer a blow, and only for those: the hunts of a kill, the skirmishes, the assault, the hunt of an enemy Fire Ant and of a thief are as they were, and in the game type 187 the brawl owns the fight and nothing changes. With the batch taken out a bot plays exactly as before: the 48 matches of the Hard against `gang:hard` run, played again with `standard+styled=1,defence=0`, end on the same state hashes and scores as the matches of the "before" run.

**The rules**

| | rule | plan field (tune key) |
|---|---|---|
| 1 | *Who answers.* Any ant that takes orders, holds no food or loot, has at least 4 hit points and does not stand on a power-up: a Thief, a Fire Ant, a Bomber and a Swimmer too. Never the Fire Ant that keeps the walls while a thief threatens, a Thief in its raid clip or one of the raid task within 6 tiles of the raid tile of an enemy hill, the crew and diggers of the island play, or an ant of a task that the fights cannot take from (another fight, a gate goal, the rush). The ants of a pick-up trip, of walls that no thief threatens, of the mines, the bombs, the sabotage, the strike and the harassment may be taken; they go back to their task when the fight ends. | `defend_all_types` (`dall`) |
| 2 | *The struck ant first.* The ant whose hit points fell is the first defender of the fight, if it still passes rule 1. It may still be in its hit clip; it takes its order as soon as it can. | `victim_answers` (`dvictim`) |
| 3 | *How many.* The level's defenders, or as many as the enemy has ants within `defend_radius` tiles of the target (a carrier is no fighter) and `defend_extra` more, at most `defend_max`. They come from within `defend_reach` tiles of the target and never from beyond the leash, a Combat Ant first and then the nearest; the ants that a job of rank 3 or 4 holds come only after the free ones. | `defend_by_strength` (`dstr`), `defend_radius` (`dradius`), `defend_extra` (`dextra`), `defend_max` (`dmax`), `defend_reach` (`dreach`) |
| 4 | *How long.* The fight waits through the target's hit clip (it ended at the first blow unless a kill was in reach) while the strength of the defenders (the sum of their hit points times the sum of their damage, as in the hunts) is at least `defend_keep_percent` of the strength of the strongest enemy ants near the target, the target is within the leash of the first blow, and a blow on an own ant or on the target fell within five times the level's linger time. When the force is the weaker the fight is called off at the look, before an ant is taken or ordered. | `defend_by_strength`, `defend_keep_percent` (`dkeep`) |
| 5 | *Focus.* All defenders strike the fight's target until it dies, runs (rule 4 fails) or leaves the leash. The escape rule (one hit point, walking home, a lead of two tiles) and the can't-go rule are as they were. | |
| 6 | *The last ants* stay out of every fight (the reserve is unchanged), and a defender that falls below 3 hit points (4 beside an enemy Combat Ant) leaves, as before. | |

Hard: the three flags on, `defend_keep_percent` 100, `defend_radius` 5, `defend_extra` 1, `defend_max` 5, `defend_reach` 10. Medium: the flags off, 120, 5, 1, 3, 8. Easy: the flags off. (The radius is 5 where the first draft of the plan had 4: with 4 the hit-back rate of item 1 below was 59.8%, 54.8% and 56.7% on three sets of 24 seeds, with 5 it was 61.9%, 55.6% and 58.7%, and the score was higher on all three.) A plan made by hand has none of it. `defence=0` takes the batch out of a plan and `defence=1` puts the three rules on with the level's numbers (`bot_arena --tune K=V,...`, or `standard+K=V` for one seat; add `styled=1` to a tuned seat that is to keep the style and the variations of the registry's bot). `without_defence_batch` in `tactics.hpp` does the same for the tests.

**What the engine allows.** An ant that is struck cannot take an order while the striker's clip runs and during its own hit clip: the engine refuses orders to an ant that is engaged or in flight, about 25 ticks of every blow. So the struck ant strikes back when the striker waits a moment or the others have been at it; most of the answer comes from the ants near it.

**The numbers after Step 1**, the same seeds and commands as before (the report files are called `after_...`):

| # | measure: Hard against `gang:hard`, TREASURE, seeds 1 to 24, both seats | before | after | bar |
|---|---|---|---|---|
| 1 | hit back / struck first | 115 / 444 = 25.9% | 125 / 202 = 61.9% | at least 60% |
| 2 | ticks from the first blow to the first blow of a helper, median (p90) | 129 (366) | 100 (246) | median at most 120 |
| 3 | ants lost / killed in the fights | 37 / 25 | 22 / 44 | at most 29.6 / at least 37.5 |
| 4 | mean score of the seat | 3308 | 3391 | at least the before |
| 5 | self-play win rate, all three maps: Hard / Medium / Easy | 0.638 / 0.149 / 0.053 | 0.653 / 0.142 / 0.051 | Hard at most 3 points down, Medium and Easy within 1 point |

Item 1 is the ratio of two numbers that both moved: the bot is struck first in 202 fights instead of 444, probably because its answers kill more of the ants that strike. Most of the fights that are left are ones in which nobody could answer: the struck ant carries food, and the ants near it carry food too, are held by another fight, or are further away than `defend_reach`. On the other seeds (not part of the acceptance) the rate is lower: 55.6% on seeds 25 to 48 (the bot before: 30.9%) and 58.7% on seeds 49 to 72, so the bar of item 1 is not reliably cleared: it is about 55 to 62 percent.

**Fights in which the seat was struck first**

| run | seat | seats | struck first | hit back | helped (median, p90 ticks) | strikers own / enemy | at the fight own / enemy | lost / killed in fights |
|---|---|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 202 | 125 (62%) | 119 (100, 246) | 1.59 / 1.81 | 3.17 / 2.23 | 22 / 44 |
| hard vs gang:hard | gang:hard | 48 | 132 | 75 (57%) | 54 (124, 450) | 1.20 / 2.03 | 2.21 / 3.36 | 50 / 4 |
| hard vs gang:medium | standard:hard | 48 | 162 | 104 (64%) | 94 (69, 307) | 1.61 / 1.54 | 3.29 / 2.07 | 6 / 22 |
| hard vs gang:medium | gang:medium | 48 | 278 | 115 (41%) | 83 (158, 468) | 0.82 / 2.03 | 2.06 / 3.24 | 76 / 1 |
| four hard (mirror) | standard:hard | 32 | 333 | 84 (25%) | 72 (106, 318) | 0.46 / 1.29 | 2.10 / 2.17 | 26 / 6 |

**Deaths, heals, Fire Ants, mines, blows**

| run | seat | deaths | could walk home / far / no warning | heals | Fire Ants (died) | walls at enemy gates | home mines by 3,000 / 6,000 / 12,000 | away mines (hits) | blows struck | focus |
|---|---|---|---|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 44 | 26 / 5 / 13 | 473 | 115 (23) | 456 | 0 / 0 / 0 | 0 (0) | 1710 | 62% |
| hard vs gang:hard | gang:hard | 172 | 83 / 45 / 44 | 177 | 46 (38) | 0 | 0 / 0 / 0 | 0 (0) | 1468 | 59% |
| hard vs gang:medium | standard:hard | 25 | 8 / 4 / 13 | 450 | 117 (14) | 542 | 0 / 0 / 0 | 0 (0) | 2161 | 67% |
| hard vs gang:medium | gang:medium | 197 | 87 / 44 / 66 | 276 | 52 (47) | 0 | 0 / 0 / 0 | 0 (0) | 915 | 44% |
| four hard (mirror) | standard:hard | 61 | 21 / 20 / 20 | 378 | 36 (7) | 7 | 0 / 0 / 2 | 305 (144) | 916 | 59% |

**Points and ants** (the engine's own counts, per seat and match)

| run | seat | seats | score | ants killed | ants lost | ants left |
|---|---|---|---|---|---|---|
| hard vs gang:hard | standard:hard | 48 | 3391 | 3.58 | 0.92 | 5.1 |
| hard vs gang:hard | gang:hard | 48 | 325 | 0.90 | 3.58 | 2.4 |
| hard vs gang:medium | standard:hard | 48 | 3497 | 4.08 | 0.52 | 5.5 |
| hard vs gang:medium | gang:medium | 48 | 189 | 0.52 | 4.08 | 2.0 |
| four hard (mirror) | standard:hard | 32 | 1953 | 1.81 | 1.81 | 4.1 |

**Self-play**, the same 1,080 matches as above, before next to after (win rate and margin as defined there):

| maps | bot | seats | win rate before | after | margin before | after |
|---|---|---|---|---|---|---|
| all three | standard:hard | 1080 | 0.638 | 0.653 | +213 | +219 |
| all three | standard:medium | 2160 | 0.149 | 0.142 | -436 | -439 |
| all three | standard:easy | 1080 | 0.053 | 0.051 | -784 | -765 |
| TREASURE | standard:hard | 360 | 0.847 | 0.875 | +445 | +468 |
| TREASURE | standard:medium | 720 | 0.074 | 0.057 | -739 | -765 |
| TREASURE | standard:easy | 360 | 0.000 | 0.006 | -1601 | -1532 |
| SMALL | standard:hard | 360 | 0.575 | 0.581 | +43 | +31 |
| SMALL | standard:medium | 720 | 0.176 | 0.168 | -221 | -213 |
| SMALL | standard:easy | 360 | 0.058 | 0.058 | -286 | -278 |
| MEDIUM | standard:hard | 360 | 0.492 | 0.503 | +150 | +159 |
| MEDIUM | standard:medium | 720 | 0.197 | 0.201 | -348 | -340 |
| MEDIUM | standard:easy | 360 | 0.100 | 0.089 | -465 | -485 |

The rest of the self-play check: the Hard mirror (four Hard bots, seeds 1 to 8): win rate 0.250 per seat (a fair share is .250; the seats are alike), mean score 1953 against 1869.

What the numbers say: against the gang at Hard the bot now answers in about 6 of 10 fights (a quarter before), more of its ants strike (1.6 against 0.4) and the first helper strikes after 100 ticks instead of 129; it loses 22 ants in these fights instead of 37 and kills 44 instead of 25, and the engine counts 3.6 ants killed and 0.9 lost per match against 2.0 and 1.7. Against the milder gang it answers more often as well (64 percent, 62 before) and loses and kills more in the fights (6 and 22, 2 and 24 before), and its mean score is lower (3,497 against 3,682), most likely the food of the ants that fight. In the mirror the bot hits back less often (25 percent, 38 before), mostly because it calls off a fight in which the other side is the stronger instead of sending three ants into it, and it loses more ants in those fights than it kills (26 and 6, 21 and 2 before); its mean score is higher (1,953 against 1,869).

**Tune keys that move the numbers** (Hard against `gang:hard`, the same 48 matches, one key at a time on top of the shipped plan; hit back, ticks to the first helper, ants lost and killed in the fights, mean score): the shipped plan 61.9%, 100, 22 and 44, 3,391; `dradius=4` 59.8%, 112, 25 and 44, 3,361; `dradius=6` 59.6%, 102, 20 and 49, 3,335; `dkeep=80` 57.9%, 109, 25 and 44, 3,312; `dkeep=120` 60.5%, 104, 22 and 41, 3,380; `dextra=0` 56.2%, 96, 22 and 40, 3,380; `dreach=14` 58.3%, 97, 29 and 43, 3,280; `dstr=0` (rules 1 and 2 only) 67.8%, 132, 23 and 34, 3,224; `dmax=4` 61.4%, 100, 22 and 43, 3,392; `dmax=7` is the shipped plan (no fight in these matches asked for more than 5). The rate of item 1 moves by a few points with any of them: it is within the noise of 48 matches.

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
```

The numbers of Step 1 were taken with the same four commands (`after_gang_hard.json`, `after_gang_medium.json`, `after_mirror_hard.json`, `after_arena.json`). The mutants of the batch are `tests/data/bot_defence_mutants.json` (`python3 tools/mutate.py tests/data/bot_defence_mutants.json --keep-going --jobs 1 --timeout 900`).

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

- **The numbers depend on the bench bot's details.** The same Hard bot hit back in 26 percent of the fights against a gang that forms at tick 300 (seeds 1 to 24; 31 percent on seeds 25 to 48) and in 34 and 36 percent against one that forms at tick 600 (both arrangements of the seats). The table is a yardstick for comparing a bot with its next version against the same `gang`, not a measure of people.
- **48 matches are a few hundred fights.** A difference of a few points of a rate is within the noise of a run; compare runs over the same seeds.
- **Ants lost and killed are counted two ways.** In the fights where the seat was struck first (above) the standard bot loses more ants than it kills; over the whole match the engine's counts are close to even against `gang` at Hard, because about half of the standard bot's kills are ants that a blow threw into water: they drown, and the fights count only the blow that takes the last hit point.
- **One map.** TREASURE is where the numbers were taken; the other maps change them.
- **The hit-back share is a ratio of two moving numbers.** A bot that answers well is struck first in fewer fights, and the fights that are left are the ones that nobody could answer. Read it together with the ants lost and killed in the fights and with the engine's own counts.
- The people numbers of a recording (`bot_watch --replay`) are worked out the same way; no recording is part of the repository.
