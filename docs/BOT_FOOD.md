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
| `Hopeless` | an enemy is there first and at most one competes (in the denial order: an enemy is there first, whoever else competes), or a contested pile that an enemy **army** holds | the Cheerios of another hill |

An enemy army holds a pile when the enemy's Combat Ants (8 each) and ants drawn attacking (4 each) within four tiles of it weigh at least 8, and the own ants within six tiles (Combat Ants 8, others 4) weigh less than one and a half times as much.

## Medium and Hard: the denial order

The rule (`HarvestTask::Params::denial_order`, on in the plans of Medium and Hard; the Economic and Defensive styles keep the plain order, their style is no contest of the middle): **food is finite, and what the seat leaves at a pile that other teams reach as soon is eaten by them; what it leaves at its reserve waits.** So the points per trip of a pile are weighed by who else wants it:

- a `Multi` pile counts **300 percent** of its points per trip (`deny_multi_percent`), a `One` pile **150 percent** (`deny_one_percent`); every other pile counts as it is;
- a reserve pile (`Safe`) with an enemy ant within **6 tiles** of its anchor (`reserve_threat_tiles`) counts 300 percent as well: it is being taken now (a weight too, so that a threatened pile of few points does not go before a rich one);
- the weights fade as the match runs out, in step with the share of the match that is left (`deny_fade`): at half time a contested pile counts 200 and 125 percent, at the end as it is;
- the race (below) is off; the caps of ants per pile are the same as in the plain order.

It is a weight and not a rank. A pile that several enemies want goes first while its points per trip are more than a third of the reserve's: on TREASURE with four seats the centre (25 points a unit, a trip of 260 to 321 ticks) goes before the Cheerios (30 points, 162 to 265 ticks) at every seat. The reserve goes first when the contested food lies across the field, and a pile that another team reaches far sooner is that team's (it eats the pile before the ants arrive). When the contested food is gone the reserve is next, and as the weights fade the reserve comes before contested food that is far away, so it is not left at the end (with four bots of the order on TREASURE, a hill's reserve of 15 points a unit kept 35 of its 60 units at the end without the fade, 17 with it; a clock that put the whole reserve first instead cost about 3 points of share in duels). A pile that only one enemy wants counts less than one that several want: with two teams the food rarely runs out (four worker bots eat all of TREASURE in a match, one eats a third of it), so what the seat leaves there is less often lost.

Measured against the bots without the order (`deny=0`: the bots of v0.12.53 to the state hash; Hard unless said, in the same matches with every seating; 50 percent of the share is a tie; the tables are in the pull request that brought the order): on TREASURE one bot with the order against three without it scores 50.7 percent, two against two 50.4, one against one 54.9, and one bot without it against three with it 50.8; on the 24 maps of the design half with four hills where every seat has a pile that two enemies reach as soon and a reserve, 50.9 with four seats and 52.0 in duels, and the bot without the order against three with it 50.7; on the 24 such maps of the held-out half 49.4, 51.9 and 50.3; Medium 50.4 on TREASURE with four seats and 50.0 in duels, 50.5 and 50.2 with four seats on the contested maps of the two halves. Four bots with the order score 2.6 percent less per seat than four without it on the design maps (more of their ants meet at the contested piles) and as much on the held-out ones, and on TREASURE they leave as much food uneaten (of the 415 units in reach, 68 are left at the end without the order and 68 with it, over 48 seeds). So the order is no stronger than the plain one against bots that do not contest, and no weaker against bots that do; what it changes is the play: with four teams the contested food goes first and the food at home is kept for later.

## Easy: the race

Easy keeps the race of the contest batch (a report after play, 2026-10-05: the bots ate their food in a weird order, and the centre food on TREASURE is contested on all four sides, so that is usually the food to start with). The gate banks about one deposit per 100 ticks whatever the number of ants, so only the ants that the gate cannot use are free to go. The ants at work are counted by their trips; the ants beyond what fills the gate (`race_gap_ticks` 100, `race_slack_percent` 100) take the `Multi` piles, each by points per trip, before the plain order. The race lasts the first 1,200 ticks and needs six ants at the start (TINY has three, SMALL four). It does not run into a pile that an enemy army holds. A pile with one competitor is no race in the shipped plans (`LevelPlan::race_one`: `bot_arena --tune raceone=1` makes it one, and even then not where an enemy is far nearer). The Economic and Defensive styles do not race. `bot_arena --tune race=0` brings back the opening of v0.5.0 (`contest_opening_ants`: Medium one ant, Hard two, Easy none, until tick 1,200), and `deny=0` gives Medium and Hard the race again.

## Tried and left out

Measured with one bot of the variant against three of v0.12.52 on TREASURE (24 seeds, every seating; 50 percent is a tie):

- **The strict order** (every contested pile before the reserve, the reserve only when the contested food is gone or a clock says so): 43.8 to 46.5 percent of the share. The bot walked across the field to piles that every other team reached sooner (a pile that two enemies reach counted as contested even when a third was far nearer), lost two thirds of its Cheerios to the others while it was away (a reserve of 30 points a unit is worth a trip to everybody), and left up to 32 of its 75 units of reserve uneaten at the end. The `contest_aware` order of v0.3.0 was a strict order too, and every standard bot with it scored 11 percent less ([BOTS.md](BOTS.md#strategy), point 8).
- **The reserve's clock** (the whole reserve first when the rest of the match only just eats it): no change in four-player games, about 3 points of share less in duels (with two teams every far pile that nobody else wants is a reserve, so the clock made the walk across the field urgent).
- **Hatching for the economy** (an egg whenever the food in reach is enough for one more ant): 35.8 to 40.7 percent with the strict order, 39.2 with the weight (an egg costs 200 points and the food is finite).
- **The nearest ants only** (at most three or five ants on a contested pile): 51.3 and 50.1 percent against 50.3 without a cap, within the noise.

## Limits

- The classes come from the start analysis (the walking costs of every hill at the start, as for the race): a wall that comes up later does not change who competes for a pile.
- The threat to the reserve is any enemy ant within six tiles of it, also one that only passes by on its way to something else.
- An enemy that is there first owns the pile only by the marks of 70 and 130 percent; a pile that an enemy reaches a little sooner is still contested.
