# When the food is gone, and the idle swap

The food of a match is finite and never comes back, so every match has a moment after which no ant can bank anything by eating. A computer player of the standard bot ([BOTS.md](BOTS.md#the-standard-bot)) used to stand from then on: its economy had nothing to send its ants to, and the fighting tasks were built around the economy (the last ants stay out of every fight, an assault asks for the surplus of the economy, a raid waits for a big box). This page is the rule set that gets the ants moving again, the numbers of the three levels, what was measured and what is not built. Source: `include/ants_ai/tactics.hpp` (`LevelPlan::gone_*`, `idle_swap*`, `Tactics::gone`), `src/ants_ai/standard_bot.cpp`, `standard_tasks.cpp`, `standard_tasks_raid.cpp`, `standard_tasks_powerup.cpp`, `heal_task.cpp`; tests `AI56` (`tests/test_ai/test_ai_gone.cpp`), mutants `tests/data/bot_gone_mutants.json`. `bot_arena --tune gone=0` is the bot as it was before (the same match, hash for hash, as the bot without this change).

## What the idle ants were doing

Studied with `bot_watch` over a recording of a computer-only match on POPcOrN (4 bots, two teams of two, 14 minutes), with the arena's idle tally ([below](#measuring-it)) over many matches of the rotation's maps, and by replaying the recording tick by tick.

- **The size of it.** On POPcOrN a team starts with three ants, dealt across both halves of the map ([BOT_GATES.md](BOT_GATES.md#popcorn-in-a-match)), and is down to one to three ants after the first minutes: about half of all ant time is idle (58 percent for the seat of the recorded match that banked nothing, 10 to 32 percent for the others). After the last food has been taken, 85 to 97 percent of the ant time is idle on POPcOrN and ISLANDS, 60 to 75 percent on SMALL. In the recorded match all four teams ended with one ant each, standing, with two Fire power-ups and eight Thief power-ups lying on the map.
- **Typed ants stood beside power-ups they could take.** `PowerUpTask` sent plain ants only, and a bot whose ants are all typed (on POPcOrN nearly every ant of a team is, after the first minute) fetched nothing for the rest of the match. An ant that takes a power-up while it has a type keeps the new one and leaves its old one on a free tile beside it ([GAMEPLAY.md](GAMEPLAY.md), "Typed ants"; the bots' own tasks do it on POPcOrN, a Bomber becoming a Swimmer and then a Fire Ant). In the recording a Bomber stood more than two minutes on the left island, on the tile next to a Fire power-up, and nothing sent it: no bridge was missing, the power-up was one step away. A bridge to a power-up that a Swimmer could dig was not the cause here (an earlier trial of the bridge planner of the island play on POPcOrN, 20 seeds of four standard bots, found no job there, and its switch was taken out again; it was not repeated for this page).
- **After the last food nothing was left to do.** The idle ants after the last food are Swimmers that ferried food, Thieves with no hill that has points to take, Bombers and Fire Ants whose task is done. A hurt ant stands where it was hurt, because a fight needs four hit points and nothing sent it home. The fighting tasks did not take a lone ant at all: `FightTask` kept the last ants (two, or one with `idle_hunt`) out of every fight, started an assault only with at least two ants of the economy's surplus, and took only workers and Combat Ants.

## The rules

The batch begins for a bot when it **has nothing to do** and has had nothing for `gone_wait` ticks, with more than 300 ticks left, in the original's game (the game type 187 has no food by its rules; its brawl is its own, [BOT_187.md](BOT_187.md)). It has nothing to do when no pile that its hill walks to has units left (a lunchbox that no walk joins to the hill is no pile), and either no pile is left at all, or the island, gate or field work has banked nothing for `gone_stall` ticks after the first 3 minutes (a map whose food is behind a wall of fire or beyond water keeps its work as long as it banks). The bot's own stall detector and the tasks of the gate goals and the island play are not changed; they run as before.

While it is on, and only then:

1. **Steal.** A Thief raids a hill for `gone_raid_min` points instead of the level's usual loot (a steal is a swing of twice the loot), Easy too, and the highest box is still the first target. The team wants a Thief while an enemy box shows that much and the own hill's ants walk to that enemy hill (`gone_thief`; a Thief that no walk leads to a hill is of no use), and the trip to a Thief power-up may be as long as `gone_trip_ticks`. An idle Swimmer that no task holds (the ferry is over) may be the ant that goes and becomes the Thief (`gone_swap`; Medium and Hard); a Fire Ant is the keeper of the walls against the thieves and a Bomber lays the mines, so neither is taken.
2. **Fight only the fights that are won.** An assault (the free ants go for an enemy ant) needs no wait and no surplus (two free ants are enough, as in the level's own assault; a lone ant sent at a target met more than it counted, measured on TREASURE at Hard: 23 percent more ants lost for 15 percent fewer kills than the bot as it was, rule 3 for the last ants) but asks for `gone_odds_percent` of the strength near the target, not the level's usual odds: with the food gone a fight changes no score, and a lost one costs ants, and a team's last ant costs a forced hatch. A team that shows at most `gone_wipe_ants` ants and a box of `gone_wipe_box` points is the first target, at the lower `gone_wipe_odds_percent`: killing its last ant forces a hatch of min(box, 200) points.
3. **The last ant.** The last ants of a bot take part only in an assault on a team that has more to lose than the bot (min(its box, 200) at least `gone_margin` points above min(the own box, 200): the price of a wipe-out) or that is ahead of it by twice that margin (a team that is behind loses no place by losing a fight). A bot that is ahead stands: a lost fight costs it and a won one changes no place. Easy never risks its last ant.
4. **Heal.** An idle ant with at most `gone_heal_hp` hit points walks into its hill from wherever a walk leads (`HealTask`, which is otherwise off): healthy ants are what the assault and the raids take.

**The idle swap** (any time of the match, Medium and Hard, `idle_swap`): an own Bomber or Swimmer that no task holds and that has stood idle with empty hands for `idle_swap_ticks` takes a Fire power-up that lies within a trip while the bot has no Fire Ant (the order of value of the pick-up trips, `PowerUpTask`: Fire, Bomber, Thief, Combat, Swimmer). The ant leaves its old power-up beside it, so the Bomber's hat can be taken again by a plain ant.

| | Easy | Medium | Hard |
|---|---|---|---|
| begins after nothing to do for (`gone_wait`) | 1,600 ticks (80 s) | 800 (40 s) | 400 (20 s) |
| work that banks nothing is lost after (`gone_stall`, never before 3 minutes) | 3,000 | 1,800 | 1,200 |
| raids for (`gone_raid_min`) | 30 | 15 | 10 |
| trip to a Thief (`gone_trip_ticks`) | 500 | 700 | 900 |
| an idle Swimmer becomes the Thief (`gone_swap`) | no | yes | yes |
| assault with free ants (`gone_assault_min`) | 2 | 2 | 2 |
| strength asked for (`gone_odds_percent`) | 250 | 200 | 200 |
| against a team that a wipe-out forces to hatch (`gone_wipe_odds_percent`) | 250 | 100 | 80 |
| last ants, margin (`gone_margin`) | never | 100 | 50 |
| first target: a team of at most (`gone_wipe_ants`) | none | 1 ant | 2 ants |
| heals at or below (`gone_heal_hp`) | 5 | 6 | 7 |
| idle swap after standing idle for (`idle_swap_ticks`) | no | 600 ticks (30 s) | 300 (15 s) |

The weaker the level, the later it begins and the more it asks before it risks anything: the same reasoning as the rest of the plans ([BOTS.md](BOTS.md#difficulty-levels)). Everything it sends is an ordinary command of a person (a special order onto a hill, an attack, a move onto the hill's entrance or onto a power-up), through the controller's budget and reaction delay, from what the bot's team can see ([BOTS.md](BOTS.md#fairness-in-detail)); the simulation, the lock-step rules and every state hash are untouched. The tuning keys of `bot_arena --tune`: `gone` (0: off, and the idle swap with it), `gonewait`, `gonestall`, `goneraid`, `gonethief`, `goneswap`, `gonetrip`, `gonemin`, `goneodds`, `gonewipeodds`, `gonemargin`, `gonewipe`, `goneheal`, `iswap`, `iswapticks`.

## Measuring it

`bot_arena` counts, per seat and match, from the engine's own state every 4th tick (`IdleTally`, report format 7): the ants and the idle ants in ticks, the same two from the last food taken to the end (the last look at which the units on any pile fell; the whole match when none were taken), and the raids that the seat's Thieves began with the points they took (`idle` in the seat's record, `last_food` in the match's). An ant is idle when it stands in an idle clip with no order and nothing in its hands for 5 seconds. `--teams A+B` starts a match with seats A and B as a team and the other two as the other team, as a room and the game do. The kills, losses, hatched eggs, power-ups taken, banked and raided points were in the report already.

@@MEASURED@@

## Tried and left out

- **Idle typed ants in the fights** (built, measured, taken out). A Swimmer, Bomber or Fire Ant that stands idle joined an assault or a hunt like a worker. On POPcOrN, where a team has one to three ants and every walk crosses mines and fire, it cost ants and forced hatches and took nothing: in 80 matches against the bot as it was (same matches, Medium seats with it against the seats without the batch) 518 points a seat against 564 and 118 forced hatches against 88; the same plan without it, 577 against 538 and 88 against 85.
- **The Thief from any idle typed ant, for any want of the plan.** An idle Swimmer, Bomber or Fire Ant became the Thief whenever the plan wanted one once the batch had begun. On POPcOrN (400 matches a level, same matches) the other team's Thieves then took 58,000 points against 38,300 at Hard and 32,100 against 19,600 at Medium: the Fire Ant that keeps the walls against thieves and the Bomber that lays the mines had gone. Now only an idle Swimmer goes, and only for the batch's own want.
- **The ordinary assault odds** (Hard 60 percent, Medium 100, Easy 200) with the food gone: the fights are even then and change no score; the stronger odds above keep the ants.
- **A bridge to a power-up on a map without island play.** The planner of "Bridges for ants" was run on POPcOrN behind a switch and found no job in 20 seeds (no other map without island play has Swimmers to dig); it was taken out again. A bot still digs such bridges on ISLANDS (Hard).

## Not built, and not verified

- **ISLANDS.** No hill walks to another there, so nothing can be stolen on foot once the food is gone; the batch begins and its fights are the ring's (more ants lost, no score changed: the table).
- **A stranded ant.** A hurt Bomber stood five minutes on the middle island of POPcOrN beside the pile, with no walk home: it would carry food only as a Swimmer, and Swimmer power-ups lay on that island at times. The idle swap takes a Fire power-up only; a swap to the Swimmer for the ferry is not built.
- **A seat that banks nothing for a whole match** (the bottom seats of POPcOrN: [BOT_GATES.md](BOT_GATES.md#popcorn-in-a-match), "Next"): the batch begins for it after the first 3 minutes plus `gone_stall`, and has nothing to send where no walk leads to a hill or an enemy ant. What starts its breach is the gate goals' work, which this change does not touch.
- **Guard and help.** Fire walls at the thief hole and the answer to a blow on an ally are the older rules ([BOTS.md](BOTS.md#defence), [Alliances](BOTS.md#alliances)); the batch adds nothing to them.
- **Not measured:** matches with a person against the bots, Fog of War (a bot with fog is refused), the 187 game type (no batch there), the 540 maps of the players beyond the three of the rotation's library.
