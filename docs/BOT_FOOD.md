# Which food the computer players eat first

The standard bot ([BOTS.md](BOTS.md#the-standard-bot)) sends its idle workers to food by `HarvestTask` (`include/ants_ai/tasks.hpp`): a pile is worth its points per tick of the trip from the hill (value × 100,000 / trip), the best first, with at most so many ants on a pile as the hill's gate can use. This page is about the ORDER of the piles when other teams want the same food: which piles count as contested, what Medium and Hard do about it (the denial order), what Easy does (the race), and what was measured and left out. The rules of the game it relies on: food is finite (a pile that is eaten up is gone), the hill banks about one bite per 100 ticks whatever the number of ants (the gate), and nobody can take food from a pile faster by standing at it.

## Who else wants a pile: the classes

A team **competes** for a pile when its hill reaches it at 70 to 130 percent of the bot's own walking cost (the start analysis' costs of every team's hill, `PileInfo::approach`; each bot moves the two marks by up to 10 percent of its own); a team below 70 percent is **there first**. `HarvestTask::tier_for` gives every pile a class from that, made again at every look from the view's alliance state:

| Class | When | Example (TREASURE, four seats) |
|---|---|---|
| `Multi` | two or more live enemy teams compete (an enemy that is there first does not stop it) | the pile in the centre, for every seat |
| `One` | exactly one enemy competes, and no enemy is there first | a pile between two neighbouring hills |
| `Safe` | no enemy competes and none is there first | the Cheerios next to the own hill: the seat's **reserve** |
| `Shared` | only the ally competes or is as near | the ally's side |
| `Hopeless` | an enemy is there first and at most one competes, or (contested piles only) an enemy army holds it | the Cheerios of another hill |

A pile that an enemy **army** holds (within four tiles of it an enemy Combat Ant counts 8 and an ant drawn attacking 4, against the own ants within six tiles at 150 percent) reads as `Hopeless`.

## Medium and Hard: the denial order

The rule (`HarvestTask::Params::reserve_order`, on in the plans of Medium and Hard; the Economic and Defensive styles keep the plain order, their style is no contest of the middle): **food is finite, and what the seat leaves at a pile that other teams reach as soon is eaten by them; what it leaves at its reserve waits.** So the points per trip of a pile are weighed by who else wants it:

- a `Multi` pile counts **300 percent** of its points per trip (`deny_multi_percent`), a `One` pile **<<ONE>> percent** (`deny_one_percent`); every other pile counts as it is;
- a reserve pile (`Safe`) with an enemy ant within **6 tiles** of its anchor (`reserve_threat_tiles`) goes before everything else: it is being taken now;
- the race (below) is off; the caps of ants per pile are the same as in the plain order.

It is a weight and not a rank. A contested pile goes first while its trip is up to three times the reserve's (TREASURE: the centre, 25 points a unit and a trip of 260 to 321 ticks, before the Cheerios, 30 points and 162 to 265 ticks, at every seat), and the reserve goes first when the contested food lies across the field (a trip of four times the reserve's, or a contested pile that the other teams reach far sooner). When the contested food is gone, the reserve is next, so nothing needs a clock to keep it from being left at the end.

<<MEASURED>>

## Easy: the race

Easy keeps the race of the contest batch (a report after play, 2026-10-05: the bots ate their food in a weird order, and the centre food on TREASURE is contested on all four sides, so that is usually the food to start with). The gate banks about one deposit per 100 ticks whatever the number of ants, so only the ants that the gate cannot use are free to go. The ants at work are counted by their trips; the ants beyond what fills the gate (`race_gap_ticks` 100, `race_slack_percent` 100) take the `Multi` piles, each by points per trip, before the plain order. The race lasts the first 1,200 ticks and needs six ants at the start (TINY has three, SMALL four). It does not run into a pile that an enemy army holds. A pile with one competitor is no race in the shipped plans (`LevelPlan::race_one`: `bot_arena --tune raceone=1` makes it one, and even then not where an enemy is far nearer). The Economic and Defensive styles do not race. `bot_arena --tune race=0` brings back the opening of v0.5.0 (`contest_opening_ants`: Medium one ant, Hard two, Easy none, until tick 1,200), and `reserve=0` gives Medium and Hard the race again.

## Tried and left out

<<LEFTOUT>>

## Limits

<<LIMITS>>
