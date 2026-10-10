#pragma once

// What the tasks of the standard bot (standard_tasks.hpp) share: the switches and numbers of a level (LevelPlan), the geometry of a hill's thief hole (the three tiles in front of
// it), and the bot's soft MEMORY of the last looks (who was hit, which enemy ants moved, which thieves were seen).
//
// Everything in here is something a person of the seat could know: the memory is built from the BotView alone (the hit points, the tile, type and drawn state of every ant,
// the grid), and the geometry is the hill's. Nothing here reads another team's carried points, orders, eggs or timers (the hit points of every ant are on the view by the owner's decision,
// BotView), and nothing is a fact that the next look could not give back: a bot that is started in the middle of a match has an empty memory and plays on (a hit that it did not see is a hit that it does not answer).

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "ants_ai/bot.hpp"
#include "ants_ai/bot_view.hpp"
#include "ants_ai/map_info.hpp"
#include "ants_ai/rng.hpp"
#include "ants_sim/grid.hpp"

namespace ants::ai {

// ---- what a level does ---------------------------------------------------------------------------------------------------------------------------------

/// When a level builds the three fire walls in front of its thief hole (and takes a Fire power-up for it)
enum class WallTrigger : uint8_t {
    Never,           // no walls (only for the tournaments' ablations)
    ThiefSeen,       // Easy: an enemy Thief ant has been seen (and for a while after)
    ThiefPossible,   // Medium: an enemy Thief ant has been seen, or an enemy that plays can reach a Thief power-up that lies on the map
    Early            // Hard: an enemy Thief ant has been seen, or any enemy can reach a Thief power-up that lies on the map (pre-emptively: without waiting for the enemy to show
                     // that it plays), and the walls are renewed before they burn out
};

/// The switches and numbers of a level's tactics. The profile (look interval, reaction delay, command budget) is a fairness limit and is NOT here: it stays as profile_for() says.
/// The numbers were decided by tournaments (docs/BOTS.md, "The standard bot").
struct LevelPlan {
    Level level{Level::Medium};
    Style style{Style::Random};          // the style that this plan was made for (Random: a plan made by hand, the tournaments' ablations and the tests)
    // fighting back (every level)
    uint32_t defenders{2};               // the nearest healthy ants that answer a blow: Easy 1, Medium 2, Hard 3
    int32_t leash_tiles{10};             // a fight is never carried further than this from the place of the blow (or from the hill, for a thief)
    uint32_t fight_linger_ticks{100};    // a fight ends this long after the last blow that was seen
    bool carrier_aid{true};              // a carrier that was hit stands idle with its food (the blow cleared its walk): it is sent home at once, as a person would click it
    bool cantgo_aware{true};             // no order that the engine is bound to refuse for a reason that the view shows (a ring of fire shuts the hill, an ant holds a hole or the ramp): docs/BOTS.md,
                                         // "The can't-go loop"; false (the ablation cg=0): the bot as it was before
    // the economy
    bool contest_aware{false};           // the contest-aware pile order: the piles that several enemies reach first, then those that one reaches, the safe ones, those of the ally, the hopeless (HarvestTask::Params::contest_aware)
    uint32_t contest_low{70};            // an enemy whose cost to a pile is below this percentage of the own cost is there first
    uint32_t contest_high{130};          // ... and above this percentage it is no competitor
    bool rank_by_remaining{false};       // within a class: the points that a pile still holds per tick of the trip (Medium, Hard)
    bool contest_one_first{true};        // the piles that one enemy competes for come before the safe ones
    bool contest_reactive{false};        // the tournaments' experiment: a contested pile first only while an enemy ant is at it
    uint32_t contest_opening_ants{0};    // the opening: this many ants go to the contested centre of the map at the start (Medium 1, Hard 2), the rest harvest by value per trip
                                         // (strong players contest the centre first; a whole-match duel against the plain order is a tie, docs/BOTS.md); 0: none (Easy)
    // the race: contested food first for the ants that the gate cannot use (HarvestTask::Params::race)
    bool race{false};
    uint32_t race_gap_ticks{100};
    uint32_t race_slack_percent{100};    // the ants that the gate can use (about one deposit per race_gap_ticks) harvest in the plain order, the ants beyond that race (0: every ant races: measured worse)
    uint32_t race_floor{0};              // at least this many ants of the pool go to the piles where a race is open, whatever the gate can use (the centre is the starting food)
    bool race_one{false};
    uint32_t race_ants{0};
    uint32_t race_ticks{1200};           // the first minute of the match
    uint32_t race_army_weight{8};
    uint32_t race_army_percent{150};
    uint32_t contest_opening_ticks{1200};  // the opening of v0.5.0 (race off) lasts this long
    uint32_t contest_opening_min_ants{6};  // ... only with at least this many ants at the start (TINY has 3, SMALL 4: there the contest of the middle costs and loses; MEDIUM, GAUNTLET and TREASURE have 6 or more)
    bool fire_aware{true};               // a pile with a fire wall near it is asked again with the map as it is now: no ant is sent into fire (HarvestTask::Params::fire_aware)
    bool gate{false};                    // (Hard) every carrier is guided at the hill's gate by hand (GateTask: "guiding for eating"): 55 to 65 ticks per deposit instead of 93 to 116
    uint32_t gate_latency{9};            // ticks between a decision and the order's arrival (the profile's reaction delay less its jitter, and the sink)
    uint32_t gate_max_staged{8};         // carriers brought to the doorstep at a time
    bool gate_predictive{true};          // the entrance click is given before the gate is seen free (from the clip that the look showed first)
    uint32_t gate_leaver_ticks{0};       // the entrance click waits at most this long while an own ant without food stands on the ramp (mostly one that has banked and leaves; 0: it does not wait)
    uint32_t gate_gap_ticks{47};         // with the gate guided the hill banks a deposit per 47 ticks per pile slot: the economy's cap of ants per pile follows (trip / gap + 1)
    bool typed_harvest{true};            // Fire and Bomber ants harvest between their jobs (HarvestTask::Params::extra_types)
    bool combat_harvests{false};         // Combat Ants harvest too (and punch what comes within two tiles of their way) instead of standing on a guard post
    // protecting the thief hole (every level)
    WallTrigger wall_trigger{WallTrigger::ThiefPossible};
    uint32_t wall_latch_ticks{3600};     // a thief threat lasts this long after the last time it was seen (a walled hill needs its walls renewed while it lasts)
    uint32_t renew_lead_ticks{0};        // a wall is put out and lit again this long before it would burn out (0: it is lit again after it burned out, while the threat lasts)
    // the side's power-ups and the counters of the enemy's fire walls and bombs
    bool secure_side{false};             // (Medium, Hard) the power-ups of secure_kinds on the own side of the map are taken early, so that nobody steals them (docs/BOTS.md)
    uint8_t secure_kinds{0};             // bit t = AntType t, in the order of value: Fire, Bomber, then Thief, Combat, Swimmer (the first moves go to power-ups, not food)
    bool counters{true};                 // enemy fire walls near the hill or a pile are put out by an own Fire Ant, enemy bombs defused by an own Bomber or, without one, set off by a healthy idle worker
    bool ally_help{false};               // the counters and the strike-back reach the ally's hill and piles too, and a blow on an ant of the ally that an own ant is near is answered
    int32_t ally_help_radius{10};        // ... an own ant within this many tiles of the blow answers it (nobody walks across the map)
    bool bomb_hit{true};                 // ... the worker's way (without a Bomber): an ant that steps on a bomb loses 2 hit points and is thrown, and the bomb is gone
    int32_t counter_hill_radius{10};     // a hazard within this many tiles of the own hill (or within counter_pile_radius of a pile that the hill reaches) is harmful
    int32_t counter_pile_radius{3};
    // power-ups, raids, guards (Medium and Hard)
    bool combat_when_attacked{true};     // Combat Ants are taken only once the enemy fights (a blow, an enemy Combat Ant or Thief in sight), not at the start of the match ...
    bool combat_when_idle{true};         // ... or when the economy has workers that stand idle with nothing to harvest (they cost nothing)
    bool fists_strict{false};            // "the enemy fights" means: an own ant that is not a Thief was hit with another team's ant (not a Thief) next to it lately; an enemy Thief or Combat Ant in sight is no reason (the walls answer thieves)
    bool takes_combat{false};
    bool takes_thief{false};
    bool intercepts{false};              // an enemy Thief that stands near the own hill is attacked (measured: it does not pay, see docs/BOTS.md; off at every level)
    bool guards{false};                  // a Combat Ant is parked where its reflex covers the hill
    bool raids{false};                   // a Thief raids the hill of the leading team
    uint32_t max_combat{0};              // Combat Ants at a time (including those that are on their way to a power-up)
    uint32_t combat_extra{0};            // ... and this many more while the bot is being attacked (a non-thief ant of it was hit with another team's ant next to it, in the last 2,400 ticks): the
                                         // adaptive part of the Combat policy (a Combat Ant that harvests costs nothing, a second one is a theft at the neighbour's side: it only pays when there is a fight)
    uint32_t max_thief{0};
    bool steals{false};                  // (Hard) a pick-up trip may go to a power-up on ANOTHER team's side (never the ally's), when no ant of the owner is quicker there: a theft; the other levels take their own side's power-ups only
    // forced fights and hatching (Medium and Hard)
    bool strikes{false};                 // clearly behind the leader (by the score boxes) and a fight looks winnable: a strike force hunts the leader's carriers
    uint32_t strike_margin{150};         // clearly behind: the leader's box is this many points above the own ...
    uint32_t strike_margin_percent{12};  // ... and at least this percentage of the leader's own
    uint32_t strike_min_leader{300};     // the leader holds at least this many points (below that a fight is not worth the trip)
    uint32_t strike_force{3};            // ants of the force (Combat Ants first), never so many that fewer than strike_reserve stay at home
    bool strike_workers{false};          // workers join the force too (an ant less at the piles for the whole fight); false: Combat Ants only (they do not harvest anyway)
    uint32_t strike_reserve{3};
    uint32_t strike_min_force{2};
    uint32_t strike_odds_percent{150};   // the own strength (a Combat Ant counts 8, any other ant 4) against the enemy's near its hill (fighters 8 or 4, other ants 1), in percent, to start a strike; below 100 it is called off
    int32_t strike_radius{14};           // carriers within this many tiles of the target hill's queue are hunted, and the enemy ants within this many are counted
    uint32_t strike_stop_ticks{900};     // no strike in the last ticks of the match
    bool wipe_focus{false};              // (Hard) an enemy team with very few ants left is hunted down by a force that is far stronger: its last ant forces a hatch (-200) or ends it
    uint32_t wipe_max_ants{3};
    bool hatches{false};                 // (Medium, Hard) an egg is hatched for a fight that is expected (a blow lately, a strike on, enemy fighters at the hill), never for the economy
    bool hatch_for_squad{false};         // (with hatches) an egg is hatched while the harassment squad hunts: hatch_extra ants more than at the start (workers that can join the squad: harass_workers)
    uint32_t hatch_floor{4};             // the bot keeps at least this many ants up when it is hit (the replacements of a fight): the number of ants of its start counts too
    uint32_t hatch_extra{0};             // ants more than at the start that it hatches while a strike is out (the strength of a fight that it chose)
    uint32_t hatch_hits{3};              // a fight: this many own ants hit (or one lost) within hatch_window ticks; the replacements of the ants that a fight cost are hatched
    uint32_t hatch_window{400};
    uint32_t hatch_reserve{50};          // points that must be left over after the 200 of the egg
    uint32_t hatch_min_left{600};        // ticks the match must still last (the newborn takes orders 171 ticks after the click and stands at the hill after 247)
    uint32_t fire_extra{0};              // Fire Ants besides the one of the own side that the bot takes (by theft at Hard) once an enemy plays: the one for the sabotage of another gate, so that the own walls keep theirs
    bool sabotage{false};                // (Hard) the Fire Ant of the bot walls in the gate of the best opponent when it has no work of its own (SabotageTask)
    uint32_t sabotage_min_score{100};    // ... a team whose box shows at least this many points
    bool sabotage_spare_keeper{true};    // ... with a Fire Ant that is not the keeper of the own thief hole's walls (false: any Fire Ant that is free; the measurements' first version)
    uint32_t sabotage_after{600};        // ... not before this tick (the opening's own power-ups and walls come first)
    bool sabotage_safe{false};           // (contest batch) no lone Fire Ant fires in a hill whose owner can simply put the fire out (an enemy Fire Ant of the team or its ally in sight, or a Fire power-up that it can
                                         // still take): only with sabotage_escort Combat Ants that stay at the entrance and kill what comes; and a team whose walls were put out sabotage_putout_limit
                                         // times is left alone for sabotage_giveup_ticks
    uint32_t sabotage_escort{2};
    uint32_t sabotage_putout_limit{2};
    uint32_t sabotage_giveup_ticks{3600};
    uint32_t sabotage_escort_ticks{900};  // the escorts leave this long after the ring stands
    uint32_t sabotage_escort_wait{2400};  // a fire-in whose escorts are not at the entrance after this long (no walk gets them there) is given up: the team is left alone for sabotage_giveup_ticks
    // the defence against being fired in (contest batch): fire walls on the ring round the own gate send the fighters at the Fire Ant that lights them, and the walls are put out only when no enemy ant
    // stands on the tile, no enemy Fire Ant is near to light them again and the enemy's force near them is not stronger than the own
    bool fire_defence{false};
    int32_t fire_defence_radius{14};     // an enemy Fire Ant within this many tiles of the own hill is the one that fires it in
    uint32_t fire_defence_extra{1};      // fighters more than the level's defenders go after it
    int32_t fire_defence_wait{10};       // a wall on the ring is not put out while an enemy Fire Ant is within this many tiles of it (it would light it again)
    uint32_t fire_defence_hold{1800};    // ... for this many ticks from the first look at lit walls on the ring: a Fire Ant that the fighters cannot reach (it stands on a wall or a power-up, or no walk gets to it) holds them no longer
    bool harass{false};                  // (Hard) the squad hunts the carriers of the other teams from the start of the match, whatever the score (HarassTask)
    uint32_t harass_workers{0};          // workers that join the Combat Ants of the squad (an ant less at the piles while a target lasts)
    uint32_t harass_reserve{4};          // ants that never join (the economy: four carriers fill a gate)
    uint32_t harass_min_hp{5};           // a member with fewer hit points leaves the squad
    uint32_t harass_strong_defence{0};   // a team that answers the squad with this many ants at once (drawn in their attack clip near a member: a Hard bot sends three) is left alone for harass_pause_ticks
                                         // (0: off): the squad's blows cost more than they take from such a team
    uint32_t harass_retreat_hp{0};       // ... and a member that falls below this (or dies) in a fight with ants of a team makes the squad leave that team alone for harass_pause_ticks (0: off)
    uint32_t harass_pause_ticks{3000};
    uint32_t harass_odds_percent{100};   // the squad's strength against the enemy's near a target (Combat Ant 8, any other ant 4; enemy: Combat 8, drawn attacking lately 4, other ants 1), in percent
    uint32_t harass_idle_weight{1};      // an enemy ant near a target that does not fight (not a Combat Ant, not drawn attacking lately) counts this much against the squad
    int32_t harass_near{6};              // enemy ants within this many tiles of a target count against the squad
    uint32_t harass_leader_bonus{60};    // a carrier of the best opponent (by the score boxes) is worth this much more than another one (the nearest carrier of anybody is worth 100 less 3 per tile)
    uint32_t harass_far_bonus{0};        // ... and this much per tile that it is away from its own hill (nobody comes to help), up to 25 tiles
    uint32_t harass_idle_bonus{60};      // a carrier that stands still (it waits at its gate, or a blow stopped it) can be caught: it is worth this much more than one that walks
    uint32_t harass_dist_cost{3};        // ... and every tile between the member and the target costs this much
    uint32_t harass_stick{40};           // ... and the target that a member already has is worth this much more (no flitting from one carrier to the next)
    uint32_t harass_stop_ticks{600};     // no harassment in the last ticks of the match
    uint32_t harass_idle_release{300};   // workers of the squad that have found no target for this long go back to the economy
    int32_t harass_range{0};             // a member only goes for a target within this many tiles of it (0: any target in sight) and leaves the squad when none is, so that a Combat Ant that harvests (combat_harvests) is a militia
    bool harass_station{true};           // a Combat Ant without a target waits in the middle between the enemy hills (its reflex punches what passes within three tiles)
    uint32_t bench_idle_ticks{0};        // TOURNAMENTS ONLY (bot_arena --tune idle=N): the bot does nothing until this tick, so that it falls behind on purpose (a handicap); 0 in every level
    uint32_t fight_reserve{2};           // no ant is sent to fight while the bot has no more than this many (one more when no egg is left): the last ants stay out of it
    // the bot's own fights (contest batch; docs/BOTS.md, "Fights of its own"): it reads the hit points that the view gives for every ant and plays for the kill ratio, and at Hard it does not wait to be hit
    bool health_aware{false};            // (every level) a fight reads the hit points: the target that dies soonest first, a wounded target is finished (the fight lingers on), an own ant that would
                                         // die before its target is relieved by another, and the blows are counted in the real numbers (10 hit points, a blow takes 1, a Combat Ant's punch 2)
    bool skirmish{false};                // (Hard) with the stronger force at a place it cares about (a carrier on its way, a worker at a pile) enemy ants are attacked without waiting for a blow (FightTask)
    uint32_t skirmish_force{3};          // ants at most in one skirmish
    uint32_t skirmish_min_force{2};      // ... and never fewer: one ant alone is a duel, not a force
    uint32_t skirmish_odds_percent{150}; // the own strength (hit points times damage of the force) against the enemy's near the target, in percent, to start one (100: an even trade)
    uint32_t skirmish_abort_percent{80}; // a skirmish is called off when the own strength falls below this percentage of the enemy's
    int32_t skirmish_reach{9};           // own ants within this many tiles of the target are called
    int32_t skirmish_near{5};            // the enemy ants within this many tiles of the target count against the force (at most skirmish_enemy_cap of them, the strongest)
    uint32_t skirmish_enemy_cap{3};      // ... the ants that answer a blow: the standard bot of Hard sends three
    int32_t skirmish_chase{14};          // the target is not followed further than this from where it was first met
    uint32_t skirmish_ticks{600};        // a skirmish lasts at most this long (a target is chosen again at the next look)
    uint32_t skirmish_pause_ticks{600};  // after a skirmish that cost more ants than it took, none for this long
    // hunting the kill (contest batch, the owner: "I saw lots of opportunities where it could have killed an ant, the ant was 4 HP and it just didn't kill it"): at every look the free ants go for a visible enemy
    // ant that they can kill before help arrives, whether or not it hit anybody first
    bool hunt{false};                    // (every level, scaled) a kill that is available is attacked at once, kept until it is done and the ants are released afterwards (FightTask)
    uint32_t hunt_blows{2};              // a kill is available when the real blows (kill_plan: one lands per hit clip of about 22 ticks, a Combat Ant's punch takes 2 hit points, an ant that is left with one
                                         // walks home) can take the target's hit points in this many blows: 2 is a target of at most 4 hit points with a Combat Ant in the force (1 or 2 without one).
                                         // Hunts of 3 or 4 blows (5 to 8 hit points) mostly chase an ant that walks on (a Hard bot started 8.9 hunts a match on TREASURE, 0.5 ended with the target dead) and
                                         // cost win rate: 28.5 percent with 4 blows, 36.7 with 2 and 35.5 with no hunts (TREASURE, 128 matches each, docs/BOTS.md)
    uint32_t hunt_force{3};              // ants at most
    uint32_t hunt_odds_percent{120};     // the force's strength (hit points times damage) against what answers near the target, in percent: a clear local advantage
    int32_t hunt_reach{10};              // free ants within this many tiles of the target are called
    int32_t hunt_next{3};                // the scope of Easy: an enemy within this many tiles of one of its free ants
    bool hunt_wide{true};                // (Medium, Hard) the scope also holds what is within the leash of the own hill and what is at a pile that the own ants work
    bool hunt_leader_carriers{false};    // (Hard) ... and the carriers of the leading team wherever they are
    uint32_t hunt_ticks{400};            // a hunt lasts at most this long
    // behind the leader and the endgame (contest batch; docs/BOTS.md, "Behind the leader and the endgame"): the bot that is behind escalates, scaled by level; in the last minute the leader guards and the bot
    // behind goes all-in. The pressure is the deficit (the leader's box above the own) in percent of what can still be earned (the food on the field in points or the time left at catchup_earn_milli,
    // the less of the two): a pressure from catchup_tier1 / 2 / 3 is a tier 1 / 2 / 3 (Hard 40, 80, 130 percent; Medium 1.5 times those, Easy 2.5 times: the weaker the level, the later it escalates)
    bool catchup{false};                 // (every level, scaled) the tiers: 1 a strike force of Combat Ants (and raids for a smaller loot), 2 a hill with a guard is raided, a plan that does not raid (Easy)
                                         // raids and wants a Thief and Combat Ants (catchup_lift_tier); 3 all-in: no stop for the strike in the last minutes, the lowest odds. Workers never join the strike
                                         // (catchup_workers_tier)
    uint32_t catchup_tier1{40};
    uint32_t catchup_tier2{80};
    uint32_t catchup_tier3{130};
    uint8_t catchup_workers_tier{4};     // from this tier on workers join the strike and the skirmish opens for every level; 4: never. A worker's blow takes one hit point and one blow lands per hit clip, so workers
                                         // kill nothing above two hit points: with them from tier 2 a bot 2,000 ticks late in TREASURE scored 1,720 points against 1,836 with no catch-up, from tier 3 1,756,
                                         // with Combat Ants only 1,823 (Hard, 96 matches each, docs/BOTS.md)
    uint8_t catchup_lift_tier{2};        // from this tier on a plan that does not raid, take a Thief or take Combat Ants (Easy) does
    bool catchup_wants{true};            // from tier 1 on more Combat Ants (and from tier 2 a Thief) are wanted: more trips for power-ups
    uint32_t catchup_min_leader{300};    // the leader holds at least this many points (below that a fight is not worth the trip)
    uint32_t catchup_earn_milli{160};    // thousandths of a point per tick that a team can earn at most (the best bots bank about 2,200 points in 14,400 ticks)
    bool endgame{false};                 // (every level) in the last endgame_ticks the leader guards (no offence, one defender more) and the bot behind is at tier 3
    uint32_t endgame_ticks{1200};        // the last minute
    bool avoids_guarded_hills{false};    // a raid does not go to a hill that has an enemy Combat Ant near it
    bool ambush{false};                  // (Hard) a thief that finds every hill shut waits near the thief hole of the leading team, outside the reach of anybody's fists, and raids as soon as the hole is open (a wall that burned out
                                         // or is being renewed): a hole stands open for 5 to 15 percent of a match (RaidTask)
    uint32_t ambush_ticks{2400};         // ... a thief that has waited this long without a raid goes back to the economy for ambush_pause ticks
    uint32_t ambush_pause{900};
    int32_t ambush_distance{6};          // ... it waits this many tiles east of the three tiles in front of the hole
    uint32_t ambush_thieves{1};          // ... at most this many thieves wait at a time (the others harvest between raids)
    uint32_t raid_min_loot{30};          // a hill whose score box shows less is not raided (RaidTask)
    uint32_t raid_min_free{1};           // a hole is raided only when at least this many of the three tiles in front of it are free (1: any; 2: the quiet variant, docs/BOTS.md "The can't-go loop")
    uint32_t raid_black_ticks{600};      // a hill that could not be reached is left alone this long
    bool raid_unjam{true};               // two thieves on one hole: when the one that has raided is shut in on the raid tile because an own ant stands on the last free tile in front of the hole, that ant
                                         // steps aside, and no second thief is sent to a hole while an own thief is still on its raid tile (RaidTask::unjam; false, the ablation unjam=0: the bot as it was; it is part of the can't-go fixes: cantgo_aware off, cg=0, switches it off too)
    // the stall detector (StandardBot, docs/BOTS.md; 0 switches a trigger off): stall_ticks without a point, or repeat_limit equal orders within repeat_window with nothing banked, send the seat to the
    // plain economy for fallback_ticks (doubled at every stall, at most 9,600)
    uint32_t stall_ticks{3600};
    uint32_t repeat_limit{12};
    uint32_t repeat_window{1200};
    uint32_t fallback_ticks{2400};
    uint32_t gate_user_fails{16};        // the gate stops guiding for 900 ticks after this many clicks onto the entrance in a row that delivered nothing (0: it never stops)
    // islands (ISLANDS, SMALL: food beyond water; IslandTask, docs/BOTS.md "Islands"): a Swimmer digs bridges, the economy walks over them. Idle where nothing lies beyond water
    bool islands{false};                 // the island task is on (every level of the standard bot; a plan made by hand, as the tests do, has it off)
    uint32_t island_swimmers{1};         // the Swimmers that the bot wants (taken from the map by the power-up task)
    uint32_t island_builders{1};         // Swimmers that dig bridges at a time (a plan made by hand: the level plans have none, a bridge loses to the ferry: docs/BOTS.md "Islands")
    uint32_t island_bridge_ants{1};      // ants that work the piles over one bridge at a time (two that meet head-on on a one-wide bridge stop for good: IslandTask "traffic")
    bool island_guard{true};             // false: the ants on a bridge that goes, and the ants that walk to one, are left alone (the ablation that the tests of the guard use)
    bool island_expedition{false};       // the expedition is on (ExpeditionTask: a crew is flown to the Swimmers that lie beyond water, bomb by bomb); the tests of the island task switch it off
    bool island_ferry{false};            // the Swimmers that dig no bridge carry food across the water (FerryTask)
    uint32_t island_ferry_per_pile{2};   // Swimmers of the ferry at one pile
    bool island_timed_row{false};        // (Hard) the crew takes a Swimmer behind the Fire hats of a corner row with timed clicks, one ant for every Swimmer (`--tune itimed=0`: one ant for every hat, as before)
    bool island_fly_on{true};            // the Bomber of an expedition's leg flies on after the crew (`--tune ifly=0`: it stays behind and every island of the route needs a Bomber to take, as before; the quicker hops stay)
    // the flowers (docs/BOTS.md, "The flowers"; Hard's: Medium and Easy play none): what the bot does with the power-ups that the droppers let fall. `--tune flowers=0` switches all of it off (the bot as it was)
    bool flower_sides{false};            // a landed drop has a side, the nearest team's (MapInfo::flowers: the walking cost of every hill to the drop tile, as for a power-up of the start): the secure_kinds of the own side apply to it
    bool flower_fire{false};             // a bot without a Fire Ant takes a Fire drop that lies within reach, whoever's side it is (the Fire Ant at home: on SMALL, MEDIUM and GAUNTLET Fire only comes from a flower); the contest check decides who goes
    bool flower_recover{false};          // a pick-up trip whose ant took food on the way (a bite does not hear an order) is given up at once and tried again with another ant, instead of after minutes
    bool flower_recall{false};           // a pick-up trip whose power-up changed kind on the way is called back at any distance (the plan takes it anyway only from `call_back_tiles` away: it would take a kind it does not want)
    bool flower_watch{false};            // (Hard) an ant waits beside the drop tile of the own side's flower, arrives about `flower_watch_early` ticks before the landing that the bot has learned to expect (Memory::flower) and takes a kind that the plan lacks
    uint32_t flower_watch_early{60};     // the ticks before the expected landing at which the watcher stands beside the drop tile (3 s)
    uint32_t flower_watch_chance{150};   // the least chance in permille that the next landing is a kind the plan lacks, for the watcher to wait (every kind alike until three landings were seen)
    // the war batch (docs/BOTS.md, "The war batch"): the bots fight over the food and after it. Every field is off in a plan made by hand; `--tune war=0` switches all of them off again (the bot as it was)
    uint32_t war_bombers{0};             // Bomber Ants wanted (at least this many; the wish is the larger of this and the opening's) for the mines (the drops of the flowers and the power-ups on the map; once an enemy plays)
    bool war_free_only{false};           // the Bomber Ants for the mines are fetched and the mines laid only while an ant has nothing to harvest (the economy is not slowed by the war)
    uint32_t war_fires{0};               // Fire Ants wanted besides the one of the walls, for the fire-in and the fire at the piles
    uint32_t mine_per_pile{0};           // mines that stand at a pile an enemy works, at a time (MineTask; 0: no mines)
    uint32_t mine_gate{0};               // ... and, when there is no pile to mine and an ant has nothing to harvest, on the ring round the gate of the best opponent (0: none). These are the tiles that the fire-in lights too:
                                         // a mine leaves its tile to the mine (measured: no loss of score, kills or wins, docs/BOTS.md), the fire-in lights the others
    uint32_t mine_min_units{8};          // a pile with fewer units left is not mined
    uint32_t mine_percent{170};          // an enemy works a pile when its walk there costs at most this percentage of the own (an enemy far further away does not come)
    uint32_t mine_replant_ticks{240};    // a mine that went off or was defused is laid again after this long, while the enemy still comes
    bool raider_hunt{false};             // an enemy Fire or Bomber Ant within raider_radius tiles of the own hill or of a pile the own ants work is hunted by the fighters (the blows stop its work), whether or not walls stand
    int32_t raider_radius{12};
    bool raider_piles{false};            // also one at a pile the own ants work (it may be harvesting there: the hunts there cost the economy)
    uint32_t raider_extra{0};            // fighters more than the level's defenders go after it
    bool assault{false};                 // free ants (nothing to harvest) go for the enemy ants: the food is gone and the score is fixed, so the bot fights (FightTask)
    uint32_t assault_force{2};           // ants at most in one assault, and never fewer than assault_min
    uint32_t assault_min{2};
    uint32_t assault_odds_percent{100};  // the assault force's strength against what answers near the target, in percent (an even trade is taken at 100)
    int32_t assault_reach{40};           // free ants within this many tiles of the target are called
    int32_t assault_chase{24};           // the target is not followed further than this from where it was first met
    uint32_t assault_ticks{900};         // an assault lasts at most this long
    uint32_t assault_after{600};         // no assault before this tick of the match
    // the losers fight harder (docs/BOTS.md, "The war batch"; the owner, 2026-10-09: "especially the players that are losing, the bots should become more aggressive to try to gain a lead because at that
    // point you're not going to out-eat them"). The war tier (Standing::war) is the same pressure as the catch-up tiers - the deficit in percent of what can still be earned - against lower marks, and
    // it raises the tier of the catch-up (raids for a smaller loot, the strike force, the Combat Ants) as well. Off in a plan made by hand; `--tune behind=0` switches it off again
    bool behind_war{false};
    uint32_t behind_tier1{15};           // the pressure, in percent, from which the war tier is 1, 2 and 3 (Hard 15, 35, 70; Medium 25, 55, 105; Easy 45, 100, 180: the weaker the level, the later)
    uint32_t behind_tier2{35};
    uint32_t behind_tier3{70};
    uint32_t behind_min_leader{150};     // the leader's box shows at least this many points (before that nobody leads); the war tier raises the tier of the catch-up from here, below catchup_min_leader (300) too
    uint8_t behind_free_tier{3};         // from this tier on the assault takes the Combat Ants off the piles (and no longer asks for the surplus of free ants) and the mines are laid with a Bomber that the economy needs (they are not only the free ants); 4: never
    uint32_t behind_odds_ease{12};       // the assault asks for this many percent less of the odds at every tier (a bot that is behind takes the even and the worse trade)
    uint32_t behind_assault_after{300};  // the assault of a bot that is behind begins this early at the latest
    uint32_t behind_sabotage_after{1800};  // ... and so does its fire-in (Medium; Hard lights from tick 600 anyway)
    /// The order of the opening's power-up trips (PowerUpTask): by value, Fire first, the Bomber second, the Thief, the Combat Ant and the Swimmer equal (a style or
    /// the bot's own variations may put the equals in another order)
    std::array<sim::AntType, 5> opening_order{sim::AntType::Fire, sim::AntType::Bomber, sim::AntType::Thief, sim::AntType::Combat, sim::AntType::Swimmer};
};

/// The plan of a level (the neutral one: no style, no variations)
LevelPlan plan_for(Level level) noexcept;
/// The plan of a level for a style (never Random: draw_style) with the small variations of a bot's own generator (+- 10 to 25 percent of the thresholds and timings, the order of the
/// equal power-ups of the opening). Deterministic in (level, style, the generator's state). The level's profile is not part of a plan and stays what profile_for says.
LevelPlan plan_for(Level level, Style style, BotRng& rng) noexcept;
/// One of the styles that the level allows, drawn from the generator
Style draw_style(Level level, BotRng& rng) noexcept;
/// The plan without the war batch (docs/BOTS.md, "The war batch"): the assault, the raider hunt, the mines, the extra Bomber and Fire Ants and the fire-in of Medium and Hard are taken out again, so that
/// the plan is what it was before (the fire-in stays where the Hard aggressive style has always had it, and is safe). For `bot_arena --tune war=0` and for the tests whose subject is an older rule.
void without_war_batch(LevelPlan& plan) noexcept;

// ---- the thief hole -------------------------------------------------------------------------------------------------------------------------------------

/// The three tiles in front of a hill's thief hole: (bx + 4, by + 1 .. by + 3), east of the raid tile (bx + 3, by + 2). A thief reaches the raid tile only by stepping from one of them
/// (the other neighbours of the raid tile are tiles of the mound), so a thief cannot raid a hill whose three tiles are all shut. Verified against the engine's own raid order
/// (docs/audit/B4_1_notes.md): with a fire wall on each of them the order ends in "Can't go there." and the victim keeps its points; with two walls, or three one tile further east, the
/// raid goes through.
std::array<sim::TileCoord, 3> east_tiles(const HillInfo& hill) noexcept;

/// What stands on one of the three tiles
enum class EastTile : uint8_t {
    Blocked,    // a thief cannot step there anyway: water, a rock, another solid object (a pile, a power-up, a lunchbox), off the map
    Wall,       // a fire wall (solid for every ant but a fire ant)
    Bomb,       // a bomb (a thief that steps on it is thrown away and its order is gone: it counts as shut for a raid, but one blast opens the tile again)
    Open,       // a thief can step there and a fire wall can be lit on it (grass, sand or dirt with nothing on it)
    Bare        // a thief can step there and no wall can stand on it (mud): the hole cannot be shut
};

struct EastState {
    std::array<EastTile, 3> tile{EastTile::Open, EastTile::Open, EastTile::Open};
    /// Every tile is shut (Blocked, Wall or Bomb): a thief cannot raid this hill now
    bool shut() const noexcept {
        for (const EastTile t : tile) {
            if (t == EastTile::Open || t == EastTile::Bare) return false;
        }
        return true;
    }
    /// A wall can still shut the hole: no tile is Bare
    bool shuttable() const noexcept {
        for (const EastTile t : tile) {
            if (t == EastTile::Bare) return false;
        }
        return true;
    }
    size_t walls() const noexcept {
        size_t n = 0;
        for (const EastTile t : tile) n += t == EastTile::Wall ? 1u : 0u;
        return n;
    }
};

/// What stands on a tile as far as a fire wall or a bomb is concerned (the classification of the three tiles of the thief hole, for any tile)
EastTile classify_tile(const sim::Grid& grid, sim::TileCoord tile) noexcept;

/// The state of the three tiles of `hill` on the map as `grid` shows it now
EastState east_state(const sim::Grid& grid, const HillInfo& hill) noexcept;

// ---- the sides of the map -----------------------------------------------------------------------------------------------------------------------------

/// The team whose side of the map a power-up lies on: the team whose hill reaches it at the least walking cost (the start analysis: PowerUpInfo::approach), strictly less than every
/// other team of the match; -1 for a power-up that no hill reaches or that two hills reach equally well. On TREASURE every base has one power-up of each kind on its side.
/// `present` is the bitmask of the teams in the match. A power-up that a flower dropper makes is not in the analysis of the start: it has no side.
int power_up_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept;
/// The same for a drop of a flower dropper: the team whose hill walks to the drop tile at the least cost, strictly less than every other team of the match (MapInfo::flowers); -1 when `tile` is no
/// flower's drop tile, no hill reaches it or two reach it equally well. (power_up_side knows only the power-ups of the start: a drop is on no list of it.)
int drop_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept;

// ---- the memory ---------------------------------------------------------------------------------------------------------------------------------------

/// An own ant that lost hit points (or died) since the previous look
struct Hit {
    sim::AntType type{sim::AntType::Worker};
    uint32_t ant{0};
    sim::TileCoord tile{};               // where it stands now (where it stood at the last look when it died)
    uint8_t hp_before{0};
    uint8_t hp_now{0};                   // 0: it is gone
    bool carried{false};                 // it held food (or loot)
};

/// An enemy Thief ant that is in sight
struct ThiefSighting {
    uint32_t ant{0};
    uint8_t team{0};
    sim::TileCoord tile{};
    sim::TileCoord before{-1, -1};       // where it stood at the previous look, (-1, -1) when it was not in sight then
    bool carrying{false};
};

/// What the bot has seen of one flower dropper: the droplets that fell in its sight, counted once each. A person at the screen sees the flower, the droplet and what has landed, never the
/// clock behind it, so the cadence and the mix are what the bot has counted (no table of the maps, no interval or percentage of the level file)
struct FlowerLog {
    sim::TileCoord drop{};               // the drop tile (the key)
    uint32_t landings{0};                // droplets seen landing
    uint64_t last{0};                    // the tick of the last landing seen (0: none)
    uint64_t before_last{0};             // the tick of the one before it
    std::array<uint32_t, 6> by_kind{};   // landings seen per kind (index = sim::AntType; the Worker's is never counted)
    /// The ticks between the last two landings seen: the cycle as far as the bot has seen it (0 until two were seen)
    uint32_t cycle() const noexcept { return landings >= 2 ? static_cast<uint32_t>(last - before_last) : 0u; }
    /// The tick of the next landing if the cycle holds (0: not known yet)
    uint64_t next() const noexcept { return landings >= 2 ? last + cycle() : 0u; }
    /// The chance in permille that the next landing is one of `kinds` (a bit per sim::AntType): every one of the five kinds alike until three landings were seen, then what has landed
    uint32_t chance(uint32_t kinds) const noexcept;
};

class Memory {
public:
    /// Called once at every look, before the tasks: compares the view with the previous one
    void update(const BotView& view, const MapInfo& map);

    uint64_t now() const noexcept { return now_; }
    /// The own ants that lost hit points since the previous look, or died (a hit is only seen when the ant was in the previous view)
    const std::vector<Hit>& hits() const noexcept { return hits_; }
    /// The enemy Thief ants in sight now
    const std::vector<ThiefSighting>& thieves() const noexcept { return thieves_; }
    /// The tick at which an enemy Thief ant was last in sight (0: never)
    uint64_t thief_last_seen() const noexcept { return thief_last_seen_; }
    /// The tick at which an enemy Combat Ant was last in sight (0: never)
    uint64_t combat_last_seen() const noexcept { return combat_last_seen_; }
    /// The tick of the last look at which an own ant had lost hit points (0: never)
    uint64_t last_hit() const noexcept { return last_hit_; }
    /// How many own ants were hit (lost hit points) or lost at the looks of the last `window` ticks: the size of the fight that the bot is in
    uint32_t hits_within(uint64_t window) const noexcept {
        uint32_t n = 0;
        for (const auto& h : hit_log_) n += now_ <= h.first + window ? h.second : 0u;
        return n;
    }
    /// The tick of the last look at which an own ant that is not a Thief was hit and an ant of another team that is not a Thief stood within two tiles of it (a blow, not a blast and not
    /// the defenders' answer to the own thief): another team's harassment (0: never)
    uint64_t last_attacked() const noexcept { return last_attacked_; }
    /// The tick of the last look at which an own ant was lost (0: never)
    uint64_t last_loss() const noexcept { return last_loss_; }
    /// The tiles of the ants of the ally that are in their hit clip (flinch or flight) at this look and were not at the previous one: a blow on the ally, as a person sees it
    const std::vector<sim::TileCoord>& ally_hits() const noexcept { return ally_hits_; }
    /// Whether any ant of the team has been seen doing something (not idle) at some look: an idle bot's ants never move, so nothing of that team is a threat
    bool plays(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS && seen_moving_[team] != 0; }
    /// The tick of the first look that saw a wall on the tile (0: no wall there now); the wall burns out 3,600 ticks after it was lit, which is at or before this tick plus 3,600
    uint64_t wall_seen(sim::TileCoord tile) const noexcept;
    /// The own ant's hit points at the previous look (0: it was not in that view)
    uint8_t hp_before(uint32_t ant) const noexcept;
    /// The most ants of `team` that were in sight at one look (0 for the own team): a team that shows few ants now and has shown many has LOST them
    uint32_t ants_peak(uint8_t team) const noexcept { return team < sim::MAX_PLAYERS ? peak_ants_[team] : 0u; }
    /// What the bot has seen of the flower dropper whose drop tile is `drop` (nullptr: no droplet of it has been seen yet)
    const FlowerLog* flower(sim::TileCoord drop) const noexcept;
    /// Whether the ant of another team was drawn in its attack clip within the last `window` ticks (a person sees who fights and who does not)
    bool fought_lately(uint32_t ant, uint64_t window) const noexcept {
        const auto it = attacking_.find(ant);
        return it != attacking_.end() && now_ <= it->second + window;
    }

private:
    struct Last {
        uint8_t hp{0};
        sim::TileCoord tile{};
        bool carried{false};
        sim::AntType type{sim::AntType::Worker};
    };
    uint64_t now_{0};
    std::map<uint32_t, Last> own_;                          // the seat's own ants at the previous look
    std::map<uint32_t, sim::TileCoord> enemy_tile_;         // the Thief ants of other teams at the previous look
    std::vector<Hit> hits_;
    std::vector<ThiefSighting> thieves_;
    uint64_t thief_last_seen_{0};
    uint64_t combat_last_seen_{0};
    uint64_t last_hit_{0};
    uint64_t last_attacked_{0};
    std::array<uint64_t, sim::MAX_PLAYERS> seen_moving_{};
    std::array<uint32_t, sim::MAX_PLAYERS> peak_ants_{};
    std::map<uint32_t, uint64_t> attacking_;                   // ant of another team -> the tick it was last seen in its attack clip
    std::vector<std::pair<uint64_t, uint32_t>> hit_log_;       // (tick of a look, own ants hit or lost at it)
    std::vector<sim::TileCoord> ally_hits_;
    std::map<uint32_t, bool> ally_in_hit_;                     // the ally's ants that were in their hit clip at the previous look
    uint64_t last_loss_{0};
    std::map<std::pair<int32_t, int32_t>, uint64_t> wall_seen_;   // (x, y) of a wall tile of the thief hole -> the tick it was first seen
    std::vector<FlowerLog> flowers_;                               // the flower droppers whose droplets have been seen
};

/// Where the bot stands in the match by the score boxes (what a person reads on the screen): its own box (its score plus its ally's), the leader (the highest box of the teams that are
/// not allies; with an alliance of enemies the member whose hill is nearest), and whether the bot is clearly behind it or not behind at all
struct Standing {
    int32_t mine{0};
    int leader{-1};                      // the team, -1: no enemy plays
    int32_t leader_score{0};
    bool behind{false};                  // the leader's box is above the own by strike_margin points and by strike_margin_percent of itself, and holds strike_min_leader at least
    bool ahead{false};                   // the own box is not below the leader's
    int32_t deficit{0};                  // (contest batch) the leader's box above the own, 0 when not behind
    uint32_t pressure{0};                // the deficit in percent of what can still be earned (plan.catchup_earn_milli, the food on the field), 0 when not behind
    uint8_t tier{0};                     // 0 not behind enough, 1 .. 3 the escalation (plan.catchup_tier1 / 2 / 3, and the war tier below; the last minute puts a bot that is behind at 3: plan.endgame)
    uint8_t war{0};                      // (plan.behind_war) 0 .. 3: the pressure against plan.behind_tier1 / 2 / 3; the rules of the war batch (assault, mines, fire-in) grow with it
    bool guard{false};                   // the last minute and the bot leads: it guards (plan.endgame)
};

Standing standing_of(const LevelPlan& plan, const BotView& view, const MapInfo& map);

/// What every task of the standard bot is handed besides the TaskContext: the level's plan, the memory and the wishes that the bot formed at this look
struct Tactics {
    LevelPlan plan;
    Memory memory;
    /// How many ants of each kind the bot wants at this look (index = sim::AntType): the Fire Ant that lights the walls, the Thief that raids, the Combat Ants that guard. Counted
    /// with the ants that already are of the kind and the pick-ups that are on their way.
    std::array<uint8_t, 6> wants{};
    /// A thief threatens the own hill by the level's rule (WallTrigger): the fire walls are wanted, and a Fire Ant for them
    bool wall_demand{false};
    /// Workers that stood idle with empty hands at the last look and that the economy could send nowhere (HarvestTask::unplaced: no pile left that they can reach in time): they
    /// cost nothing to use otherwise
    size_t surplus{0};
    Standing standing;                   // where the bot stands at this look
    bool guard_stance{false};            // (contest batch, the endgame) the bot leads in the last minute (Standing::guard): no offence (hunt, skirmish, harassment, sabotage, strike) and one defender more answers a blow
    bool strike_active{false};           // the strike force is out (set by the StrikeTask at its last step)
    bool harass_active{false};           // the squad has a target (set by the HarassTask at its last step)
    uint32_t wall_keeper{0};             // the Fire Ant that keeps the walls of the own thief hole (set by the WallTask at its last step; 0: none): the sabotage uses another one
    /// Bridge tiles that no trip may be planned over now (index = y * width + x; empty: none): the bridges that will not last another trip (set by the IslandTask at its last refresh)
    std::vector<uint8_t> shut_tiles;
};

/// Whether the fire walls in front of the own thief hole are wanted at this look, by the level's trigger: an enemy Thief ant has been seen lately (Easy, Medium, Hard), an enemy that
/// plays can reach a Thief power-up (Medium, Hard) or some enemy plays at all (Hard); never for a hole that no wall can shut (mud), never in the last 400 ticks of the match
bool wall_demand(const Tactics& tactics, const BotView& view, const MapInfo& map);

/// Whether the walkers of `team` can reach a tile next to `tile` from their own hill (the analysis of the start: components, so a power-up that is walled in for an enemy is not a
/// thief it can take)
bool reachable_by(const MapInfo& map, uint8_t team, sim::TileCoord tile) noexcept;


}  // namespace ants::ai
