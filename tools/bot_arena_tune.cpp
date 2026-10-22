// The tuning keys of bot_arena (--tune K=V,... and the K=V of a seat's `standard+K=V`): the ablations of the standard bot's plan for the tournaments, one key per line of apply_tune.
#include "bot_arena_tune.hpp"

namespace ai = ants::ai;
namespace sim = ants::sim;

bool apply_tune(ai::LevelPlan& p, const std::string& key, int64_t v, std::string& err) {
    const auto flag = [&](bool& f) { f = v != 0; return true; };
    if (key == "defenders") { p.defenders = static_cast<uint32_t>(v); return true; }
    if (key == "leash") { p.leash_tiles = static_cast<int32_t>(v); return true; }
    if (key == "linger") { p.fight_linger_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "aid") return flag(p.carrier_aid);
    if (key == "flowers") { p.flower_sides = p.flower_fire = p.flower_recover = p.flower_recall = p.flower_watch = v != 0; return true; }       // the flower play of docs/BOTS.md, "The flowers" (0: the bot as it was): every rule at once
    if (key == "fside") return flag(p.flower_sides);
    if (key == "ffire") return flag(p.flower_fire);
    if (key == "frecover") return flag(p.flower_recover);
    if (key == "frecall") return flag(p.flower_recall);
    if (key == "fwatch") return flag(p.flower_watch);
    if (key == "cg") return flag(p.cantgo_aware);                                    // the can't-go fixes of docs/BOTS.md, "The can't-go loop" (0: the bot as it was before them)
    if (key == "gatebombs") return flag(p.gate_spare_map_bombs);                     // the counters leave a gate map's own bombs alone (0: they counter them, as before)
    if (key == "field") return flag(p.gate_field);                                   // the field breach (0: the gate goals' shore breach, as before)
    if (key == "styled") return true;                                                // not a field of the plan: a tuned bot of a spec with no style draws its style like the registry's bot (arena_factory)
    if (key == "contest") return flag(p.contest_aware);
    if (key == "clow") { p.contest_low = static_cast<uint32_t>(v); return true; }
    if (key == "chigh") { p.contest_high = static_cast<uint32_t>(v); return true; }
    if (key == "rankrem") return flag(p.rank_by_remaining);
    if (key == "cone") return flag(p.contest_one_first);
    if (key == "creact") return flag(p.contest_reactive);
    if (key == "copen") { p.contest_opening_ants = static_cast<uint32_t>(v); return true; }
    if (key == "race") return flag(p.race);
    if (key == "racegap") { p.race_gap_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "raceslack") { p.race_slack_percent = static_cast<uint32_t>(v); return true; }
    if (key == "racefloor") { p.race_floor = static_cast<uint32_t>(v); return true; }
    if (key == "raceone") return flag(p.race_one);
    if (key == "deny") return flag(p.denial_order);                                // the denial order (Medium, Hard): contested food counts more, the own safe piles are the reserve
    if (key == "denymulti") { p.deny_multi_percent = static_cast<uint32_t>(v); return true; }
    if (key == "denyone") { p.deny_one_percent = static_cast<uint32_t>(v); return true; }
    if (key == "reservethreat") { p.reserve_threat_tiles = static_cast<int32_t>(v); return true; }
    if (key == "denyfade") return flag(p.deny_fade);
    if (key == "health") return flag(p.health_aware);
    if (key == "skirm") return flag(p.skirmish);
    if (key == "skforce") { p.skirmish_force = static_cast<uint32_t>(v); return true; }
    if (key == "skmin") { p.skirmish_min_force = static_cast<uint32_t>(v); return true; }
    if (key == "skodds") { p.skirmish_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "skabort") { p.skirmish_abort_percent = static_cast<uint32_t>(v); return true; }
    if (key == "skreach") { p.skirmish_reach = static_cast<int32_t>(v); return true; }
    if (key == "sknear") { p.skirmish_near = static_cast<int32_t>(v); return true; }
    if (key == "skcap") { p.skirmish_enemy_cap = static_cast<uint32_t>(v); return true; }
    if (key == "skchase") { p.skirmish_chase = static_cast<int32_t>(v); return true; }
    if (key == "skticks") { p.skirmish_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "skpause") { p.skirmish_pause_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "hunt") return flag(p.hunt);
    if (key == "huntblows") { p.hunt_blows = static_cast<uint32_t>(v); return true; }
    if (key == "huntforce") { p.hunt_force = static_cast<uint32_t>(v); return true; }
    if (key == "huntodds") { p.hunt_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "huntreach") { p.hunt_reach = static_cast<int32_t>(v); return true; }
    if (key == "huntnext") { p.hunt_next = static_cast<int32_t>(v); return true; }
    if (key == "huntwide") return flag(p.hunt_wide);
    if (key == "huntleader") return flag(p.hunt_leader_carriers);
    if (key == "huntticks") { p.hunt_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "catchup") return flag(p.catchup);
    if (key == "cu1") { p.catchup_tier1 = static_cast<uint32_t>(v); return true; }
    if (key == "cu2") { p.catchup_tier2 = static_cast<uint32_t>(v); return true; }
    if (key == "cu3") { p.catchup_tier3 = static_cast<uint32_t>(v); return true; }
    if (key == "cuworkers") { p.catchup_workers_tier = static_cast<uint8_t>(v); return true; }
    if (key == "culift") { p.catchup_lift_tier = static_cast<uint8_t>(v); return true; }
    if (key == "cuwants") return flag(p.catchup_wants);
    if (key == "cuearn") { p.catchup_earn_milli = static_cast<uint32_t>(v); return true; }
    if (key == "endgame") return flag(p.endgame);
    if (key == "endticks") { p.endgame_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "sabsafe") return flag(p.sabotage_safe);
    if (key == "sabescort") { p.sabotage_escort = static_cast<uint32_t>(v); return true; }
    if (key == "sabputout") { p.sabotage_putout_limit = static_cast<uint32_t>(v); return true; }
    if (key == "sabgiveup") { p.sabotage_giveup_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "sabwait") { p.sabotage_escort_wait = static_cast<uint32_t>(v); return true; }
    if (key == "firedef") return flag(p.fire_defence);
    if (key == "firedefx") { p.fire_defence_extra = static_cast<uint32_t>(v); return true; }
    if (key == "firehold") { p.fire_defence_hold = static_cast<uint32_t>(v); return true; }
    if (key == "raceticks") { p.race_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "raceants") { p.race_ants = static_cast<uint32_t>(v); return true; }
    if (key == "racearmy") { p.race_army_weight = static_cast<uint32_t>(v); return true; }
    if (key == "racearmypct") { p.race_army_percent = static_cast<uint32_t>(v); return true; }
    if (key == "copenticks") { p.contest_opening_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "copenmin") { p.contest_opening_min_ants = static_cast<uint32_t>(v); return true; }
    if (key == "typedh") return flag(p.typed_harvest);
    if (key == "firew") return flag(p.fire_aware);
    if (key == "secure") return flag(p.secure_side);
    if (key == "securek") { p.secure_kinds = static_cast<uint8_t>(v); return true; }
    if (key == "counters") return flag(p.counters);
    if (key == "bhit") return flag(p.bomb_hit);
    if (key == "chv") return flag(p.combat_harvests);
    if (key == "walls") { p.wall_trigger = v == 0 ? ai::WallTrigger::Never : v == 1 ? ai::WallTrigger::ThiefSeen : v == 2 ? ai::WallTrigger::ThiefPossible : ai::WallTrigger::Early; return true; }
    if (key == "renew") { p.renew_lead_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "combat") { p.takes_combat = v > 0; p.max_combat = static_cast<uint32_t>(v); return true; }
    if (key == "steals") return flag(p.steals);
    if (key == "fiststrict") return flag(p.fists_strict);
    if (key == "ambush") return flag(p.ambush);
    if (key == "ambushticks") { p.ambush_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "ambushpause") { p.ambush_pause = static_cast<uint32_t>(v); return true; }
    if (key == "ambushdist") { p.ambush_distance = static_cast<int32_t>(v); return true; }
    if (key == "ambushn") { p.ambush_thieves = static_cast<uint32_t>(v); return true; }
    if (key == "raidmin") { p.raid_min_loot = static_cast<uint32_t>(v); return true; }
    if (key == "raidfree") {                                                                           // the free tiles in front of a hole that a raid needs (1: any; 2: the quiet variant of "The can't-go loop")
        if (v < 1 || v > 3) { err = "raidfree is the number of free tiles (1 to 3) that a raid needs"; return false; }
        p.raid_min_free = static_cast<uint32_t>(v);
        return true;
    }
    if (key == "raidblack") { p.raid_black_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "unjam") return flag(p.raid_unjam);
    if (key == "sabotage") return flag(p.sabotage);
    if (key == "fireextra") { p.fire_extra = static_cast<uint32_t>(v); return true; }
    if (key == "sabkeeper") return flag(p.sabotage_spare_keeper);
    if (key == "sabscore") { p.sabotage_min_score = static_cast<uint32_t>(v); return true; }
    if (key == "sabafter") { p.sabotage_after = static_cast<uint32_t>(v); return true; }
    if (key == "harass") return flag(p.harass);
    if (key == "hatchsq") return flag(p.hatch_for_squad);
    if (key == "hatchextra") { p.hatch_extra = static_cast<uint32_t>(v); return true; }
    if (key == "harassw") { p.harass_workers = static_cast<uint32_t>(v); return true; }
    if (key == "harassres") { p.harass_reserve = static_cast<uint32_t>(v); return true; }
    if (key == "harasshp") { p.harass_min_hp = static_cast<uint32_t>(v); return true; }
    if (key == "harassodds") { p.harass_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "harassstrong") { p.harass_strong_defence = static_cast<uint32_t>(v); return true; }
    if (key == "harassretreat") { p.harass_retreat_hp = static_cast<uint32_t>(v); return true; }
    if (key == "harasspause") { p.harass_pause_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "harassidlew") { p.harass_idle_weight = static_cast<uint32_t>(v); return true; }
    if (key == "harassnear") { p.harass_near = static_cast<int32_t>(v); return true; }
    if (key == "harassleader") { p.harass_leader_bonus = static_cast<uint32_t>(v); return true; }
    if (key == "harassidle") { p.harass_idle_bonus = static_cast<uint32_t>(v); return true; }
    if (key == "harassstick") { p.harass_stick = static_cast<uint32_t>(v); return true; }
    if (key == "harassdist") { p.harass_dist_cost = static_cast<uint32_t>(v); return true; }
    if (key == "harassfar") { p.harass_far_bonus = static_cast<uint32_t>(v); return true; }
    if (key == "harassstation") return flag(p.harass_station);
    if (key == "harassrange") { p.harass_range = static_cast<int32_t>(v); return true; }
    if (key == "harassrel") { p.harass_idle_release = static_cast<uint32_t>(v); return true; }
    if (key == "combatx") { p.combat_extra = static_cast<uint32_t>(v); return true; }
    if (key == "thief") { p.takes_thief = v > 0; p.max_thief = static_cast<uint32_t>(v); return true; }
    if (key == "intercept") return flag(p.intercepts);
    if (key == "combat_early") { p.combat_when_attacked = v == 0; return true; }
    if (key == "combat_idle") return flag(p.combat_when_idle);
    if (key == "guard") return flag(p.guards);
    if (key == "raid") return flag(p.raids);
    if (key == "strike") return flag(p.strikes);
    if (key == "strikew") return flag(p.strike_workers);
    if (key == "strikef") { p.strike_force = static_cast<uint32_t>(v); return true; }
    if (key == "strikeres") { p.strike_reserve = static_cast<uint32_t>(v); return true; }
    if (key == "strikeodds") { p.strike_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "wipe") return flag(p.wipe_focus);
    if (key == "hatch") return flag(p.hatches);
    if (key == "allyhelp") return flag(p.ally_help);
    if (key == "gate") return flag(p.gate);
    if (key == "gatepred") return flag(p.gate_predictive);
    if (key == "gatelat") { p.gate_latency = static_cast<uint32_t>(v); return true; }
    if (key == "gatehold") {                                                                           // the ticks that the gate's click waits for an ant that leaves over the ramp (0: no wait)
        if (v < 0 || v > 1000) { err = "gatehold is the number of ticks (0 to 1000) that the gate's click waits for an ant that leaves over the ramp"; return false; }
        p.gate_leaver_ticks = static_cast<uint32_t>(v);
        return true;
    }
    if (key == "gatestaged") { p.gate_max_staged = static_cast<uint32_t>(v); return true; }
    if (key == "gatefails") { p.gate_user_fails = static_cast<uint32_t>(v); return true; }
    if (key == "stall") { p.stall_ticks = static_cast<uint32_t>(v); return true; }                 // the stall detector (0: off): ticks without a point banked
    if (key == "repeat") { p.repeat_limit = static_cast<uint32_t>(v); return true; }               // ... the same order sent this many times ...
    if (key == "repwindow") { p.repeat_window = static_cast<uint32_t>(v); return true; }           // ... within this many ticks with nothing banked
    if (key == "fallback") { p.fallback_ticks = static_cast<uint32_t>(v); return true; }           // ... and the plain economy this long
    if (key == "gategap") { p.gate_gap_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "idle") { p.bench_idle_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "avoid") return flag(p.avoids_guarded_hills);
    if (key == "agg") {                                                               // the aggressive plan (Hard's from the start): the squad, Combat Ants in the opening, no guard post at home
        if (v == 0) return true;
        p.harass = true;
        p.combat_when_attacked = false;
        p.guards = false;
        return true;
    }
    if (key == "war") {                                                               // the war batch of docs/BOTS.md, "The war batch" (0: the bot as it was before it; 1: the plan that ships)
        if (v == 0) ai::without_war_batch(p);
        return true;
    }
    if (key == "brawl") return flag(p.brawl187);
    if (key.size() > 1 && key[0] == 'b' && (key == "bretreat" || key == "breturn" || key == "bper" || key == "bcrowd" || key == "bhp" || key == "bregap")) {     // the numbers of the 187 brawl (docs/BOT_187.md)
        if (v < 0 || v > (key == "bretreat" || key == "breturn" ? 10 : 100000)) { err = key + " is a number from 0 to " + (key == "bretreat" || key == "breturn" ? "10 (hit points)" : "100000"); return false; }
        const uint32_t u = static_cast<uint32_t>(v);
        if (key == "bretreat") p.brawl.retreat_hp = static_cast<uint8_t>(u);
        else if (key == "breturn") p.brawl.return_hp = static_cast<uint8_t>(u);
        else if (key == "bper") p.brawl.per_target = u;
        else if (key == "bcrowd") p.brawl.crowd = u;
        else if (key == "bhp") p.brawl.hp_weight = u;
        else p.brawl.regap_ticks = u;
        return true;
    }
    if (key == "behind") return flag(p.behind_war);                                  // the losers fight harder (docs/BOTS.md, "The war batch"; 0: the war batch without it)
    if (key == "bt1") { p.behind_tier1 = static_cast<uint32_t>(v); return true; }
    if (key == "bt2") { p.behind_tier2 = static_cast<uint32_t>(v); return true; }
    if (key == "bt3") { p.behind_tier3 = static_cast<uint32_t>(v); return true; }
    if (key == "bmin") { p.behind_min_leader = static_cast<uint32_t>(v); return true; }
    if (key == "bfree") { p.behind_free_tier = static_cast<uint8_t>(v); return true; }
    if (key == "bease") { p.behind_odds_ease = static_cast<uint32_t>(v); return true; }
    if (key == "bafter") { p.behind_assault_after = static_cast<uint32_t>(v); return true; }
    if (key == "bsab") { p.behind_sabotage_after = static_cast<uint32_t>(v); return true; }
    if (key == "wbomb") { p.war_bombers = static_cast<uint32_t>(v); return true; }
    if (key == "wfire") { p.war_fires = static_cast<uint32_t>(v); return true; }
    if (key == "mines") { p.mine_per_pile = static_cast<uint32_t>(v); return true; }
    if (key == "minegate") { p.mine_gate = static_cast<uint32_t>(v); return true; }
    if (key == "minemin") { p.mine_min_units = static_cast<uint32_t>(v); return true; }
    if (key == "minepct") { p.mine_percent = static_cast<uint32_t>(v); return true; }
    if (key == "minere") { p.mine_replant_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "raider") return flag(p.raider_hunt);
    if (key == "raiderr") { p.raider_radius = static_cast<int32_t>(v); return true; }
    if (key == "warfree") return flag(p.war_free_only);
    if (key == "raiderp") return flag(p.raider_piles);
    if (key == "raiderx") { p.raider_extra = static_cast<uint32_t>(v); return true; }
    if (key == "assault") return flag(p.assault);
    if (key == "asforce") { p.assault_force = static_cast<uint32_t>(v); return true; }
    if (key == "asmin") { p.assault_min = static_cast<uint32_t>(v); return true; }
    if (key == "asodds") { p.assault_odds_percent = static_cast<uint32_t>(v); return true; }
    if (key == "asreach") { p.assault_reach = static_cast<int32_t>(v); return true; }
    if (key == "aschase") { p.assault_chase = static_cast<int32_t>(v); return true; }
    if (key == "asticks") { p.assault_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "asafter") { p.assault_after = static_cast<uint32_t>(v); return true; }
    if (key == "kill") {                                                              // the kill batch of docs/BOT_FIGHT.md (0: the bot as it was before it; 1: the plan that ships)
        if (v == 0) ai::without_kill_batch(p);
        else p.opportunity = p.reach_fights = p.idle_hunt = p.chore_worth = true;
        return true;
    }
    if (key == "opp") return flag(p.opportunity);
    if (key == "oppreach") { p.opportunity_reach = static_cast<int32_t>(v); return true; }
    if (key == "oppjobs") { p.opportunity_jobs = static_cast<uint32_t>(v); return true; }
    if (key == "oppticks") { p.opportunity_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "reach") return flag(p.reach_fights);
    if (key == "idlehunt") return flag(p.idle_hunt);
    if (key == "worth") return flag(p.chore_worth);
    if (key == "stand") {                                                             // the stand batch of docs/BOTS.md, "The stand batch" (0: the bot as it was before it; 1: the plan that ships)
        if (v == 0) ai::without_stand_batch(p);
        return true;
    }
    if (key == "duel") return flag(p.fire_duel);
    if (key == "draft") return flag(p.fire_draft);
    if (key == "duelwalls") return flag(p.fire_duel_walls);
    if (key == "draftfar") { p.fire_draft_far = static_cast<int32_t>(v); return true; }
    if (key == "rampunjam") return flag(p.ramp_unjam);
    if (key == "rampticks") { p.ramp_unjam_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "bmine") { p.behind_mine_tier = static_cast<uint8_t>(v); return true; }
    if (key == "bmgate") { p.behind_mine_gate = static_cast<uint32_t>(v); return true; }
    if (key == "minehome") { p.mine_home = static_cast<uint32_t>(v); return true; }
    if (key == "minehomeapart") { p.mine_home_apart = static_cast<uint32_t>(v); return true; }
    if (key == "homeroute") return flag(p.mine_home_off_route);
    if (key == "mineapart") { p.mine_apart = static_cast<uint32_t>(v); return true; }
    if (key == "minehomeafter") { p.mine_home_after = static_cast<uint32_t>(v); return true; }
    if (key == "rush") return flag(p.rush);
    if (key == "rushtier") { p.rush_tier = static_cast<uint8_t>(v); return true; }
    if (key == "rushworkers") return flag(p.rush_workers);
    if (key == "rushdef") { p.rush_deficit = static_cast<uint32_t>(v); return true; }
    if (key == "rushafter") { p.rush_after = static_cast<uint32_t>(v); return true; }
    if (key == "rushleft") { p.rush_min_left = static_cast<uint32_t>(v); return true; }
    if (key == "rushticks") { p.rush_ticks = static_cast<uint32_t>(v); return true; }
    if (key == "prev") {                                                              // the strategy of v0.5.0: none of the plan rules of the contest batch (the tournaments' comparison; they are the shipped plan now)
        if (v == 0) return true;
        p.race = p.denial_order = false;
        p.health_aware = false;
        p.hunt = p.skirmish = p.sabotage_safe = p.fire_defence = p.catchup = p.endgame = false;
        return true;
    }
    if (key == "old") {                                                               // the conflict tactics as they were shipped before the win-rate measurements: all off, one Thief
        if (v == 0) return true;
        p.contest_aware = false;
        p.contest_reactive = false;
        p.strikes = false;
        p.hatches = false;
        p.wipe_focus = false;
        p.ally_help = false;
        if (p.max_thief > 1) p.max_thief = 1;
        return true;
    }
    if (key == "allon") {                                                             // ALL the conflict tactics on (the mirror of the conflict-rich tournaments): the strict contest order, strikes, hatching for fights, wipe-out focus, the help of the ally, two Thieves
        if (v == 0) return true;
        p.contest_aware = p.strikes = p.hatches = p.wipe_focus = p.ally_help = true;
        if (p.takes_thief && p.max_thief < 2) p.max_thief = 2;
        return true;
    }
    if (key == "islands") return flag(p.islands);                                     // the island machinery as a whole (0: the standard bot as it was before it: the tournaments of docs/BOTS.md "Islands")
    if (key == "expedition") return flag(p.island_expedition);
    if (key == "ferry") return flag(p.island_ferry);
    if (key == "iswim") { p.island_swimmers = static_cast<uint32_t>(v); return true; }
    if (key == "ifly") return flag(p.island_fly_on);
    if (key == "itimed") return flag(p.island_timed_row);
    if (key == "ibuild") { p.island_builders = static_cast<uint32_t>(v); return true; }
    if (key == "ibridge") { p.island_bridge_ants = static_cast<uint32_t>(v); return true; }
    if (key == "iguard") return flag(p.island_guard);
    if (key == "iferrypile") { p.island_ferry_per_pile = static_cast<uint32_t>(v); return true; }
    err = "unknown tuning key '" + key + "' (the keys are those of apply_tune in tools/bot_arena_tune.cpp)";
    return false;
}
