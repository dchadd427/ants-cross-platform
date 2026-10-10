#include "ants_ai/tactics.hpp"

#include <algorithm>

#include "ants_sim/movement_tables.hpp"

namespace ants::ai {

// ---- what a level does ---------------------------------------------------------------------------------------------------------------------------------

namespace {
// the own side's power-ups that Medium and Hard take in the opening, in the order of value: the Fire Ant (walls, and what an enemy would burn the piles with), the Bomber (what an
// enemy would mine the base with), the Thief (the enemy's raids, and the own: a thief that steals the own Thief power-up has two thieves at the start)
constexpr uint8_t kSecureOpening = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Bomber)) | (1u << static_cast<unsigned>(sim::AntType::Thief)));
}  // namespace

LevelPlan plan_for(Level level) noexcept {
    LevelPlan p;
    p.level = level;
    // The contest batch (docs/BOTS.md, "The race for contested food", "Fights of its own", "Fire play", "Behind the leader and the endgame"): the rules of the owner's report of 2026-10-05, at every level, scaled below; bot_arena's `prev` key
    // switches these flags off again (the strategy of v0.5.0, for the tournaments' comparison; the controller's refusal of special orders onto an ant is no plan field and stays)
    p.race = true;
    p.health_aware = true;
    p.hunt = true;
    p.sabotage_safe = true;
    p.fire_defence = true;
    p.catchup = true;
    p.endgame = true;
    switch (level) {
        case Level::Easy:
            p.defenders = 1;
            p.leash_tiles = 8;
            p.fight_linger_ticks = 200;
            p.wall_trigger = WallTrigger::ThiefSeen;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 0;
            p.contest_opening_ants = 0;
            p.catchup_tier1 = 100;               // Easy escalates 2.5 times later than Hard, Medium 1.5 times
            p.catchup_tier2 = 200;
            p.catchup_tier3 = 325;
            p.hunt_force = 2;
            p.hunt_reach = 6;
            p.hunt_wide = false;                 // Easy hunts what stands next to its ants and nothing else
            p.hunt_odds_percent = 150;
            p.islands = true;
            p.island_expedition = true;
            p.island_ferry = true;
            p.island_swimmers = 2;                                  // they carry food; no builder at any level (a Swimmer that ferries earns 700 to 850 points a match, a bridge carries one ant at a time: docs/BOTS.md)
            p.island_builders = 0;
            p.island_fly_on = false;             // Easy looks every 100 ticks: a leg that waits for the Bomber to fly on costs more than the flight it saves (docs/BOTS.md, "The expedition batch")
            break;
        case Level::Medium:
            p.defenders = 2;
            p.leash_tiles = 10;
            p.fight_linger_ticks = 100;
            p.wall_trigger = WallTrigger::ThiefPossible;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 200;
            p.contest_opening_ants = 1;
            p.secure_side = true;
            p.secure_kinds = kSecureOpening;
            p.takes_combat = true;
            p.takes_thief = true;
            p.intercepts = false;
            p.guards = false;                    // the Combat Ant is a worker that fights: it harvests, its reflex punches what comes near, a fight takes it first (measured: docs/BOTS.md, "Aggression")
            p.combat_harvests = true;
            p.combat_when_attacked = false;      // taken in the opening, with the other power-ups of the side
            p.raids = true;
            p.max_combat = 1;
            p.max_thief = 1;
            p.hatch_extra = 1;
            p.strike_force = 3;                  // (strikes, wipe_focus and hatches are OFF at every level: measured, they cost score, docs/BOTS.md; the numbers are for the flags)
            p.catchup_tier1 = 60;
            p.catchup_tier2 = 120;
            p.catchup_tier3 = 195;
            p.hunt_force = 3;
            p.hunt_reach = 10;                   // Medium hunts what is within the leash of its hill and at its piles as well
            p.islands = true;
            p.island_expedition = true;
            p.island_ferry = true;
            p.island_swimmers = 3;                                  // (the row has three Swimmers; with one more the bot scored a third more: docs/BOTS.md)
            p.island_builders = 0;
            break;
        case Level::Hard:
            p.defenders = 3;
            p.leash_tiles = 12;
            p.fight_linger_ticks = 60;
            p.wall_trigger = WallTrigger::Early;
            p.wall_latch_ticks = 3600;
            p.renew_lead_ticks = 130;
            p.contest_opening_ants = 2;
            p.secure_side = true;
            p.secure_kinds = kSecureOpening;
            p.flower_sides = true;
            p.flower_fire = true;
            p.flower_recover = true;
            p.flower_recall = true;
            p.flower_watch = true;
            p.takes_combat = true;
            p.takes_thief = true;
            p.intercepts = false;
            p.guards = false;                    // the Combat Ant is a worker that fights: it harvests, its reflex punches what comes near, a fight takes it first (measured: docs/BOTS.md, "Aggression")
            p.combat_harvests = true;
            p.combat_when_attacked = false;      // taken in the opening, with the other power-ups of the side
            p.raids = true;
            p.max_combat = 1;
            p.combat_extra = 1;                  // the second Combat Ant (a theft) once the bot is attacked
            p.max_thief = 2;                     // the double-thief opening: the own side's Thief and, when an enemy plays and its Thief power-up is not guarded, another one
            p.steals = true;
            p.strike_force = 4;
            p.hatch_extra = 2;
            p.gate = true;                       // guiding for eating: +4 to +23 percent alone on every shipped map (docs/BOTS.md)
            p.gate_leaver_ticks = 60;            // the click waits for the ant that leaves over the ramp (a small gain: 170 to 140 refused gate clicks in 240 matches; docs/BOTS.md, "The can't-go loop")
            p.avoids_guarded_hills = false;      // (a Combat Ant of the enemy is a worker that fights, not a guard: the raids of this bot go where the hole is open; measured, docs/BOTS.md "Aggression")
            p.raid_min_loot = 15;                // a raid for 15 points is a swing of 30 and a trip of a few hundred ticks: it pays (raidmin 10 / 30 / 60: 96.5 / 94.1 / 93.1 percent against Medium, Medium, Easy)
            p.hunt_force = 3;
            p.hunt_reach = 10;
            p.hunt_leader_carriers = true;       // Hard also hunts the carriers of the leading team wherever they are
            // (p.skirmish, the stronger force at a carrier or a worker at a pile, is built and OFF: against three bots of v0.5.0 on TREASURE a Hard bot with it won 14.1 percent of 96 matches and 28.6
            // without it, 25 being equal; it keeps three ants from the piles for up to 600 ticks at a time and the enemy's defenders come: docs/BOTS.md, "Fights of its own")
            p.islands = true;
            p.island_expedition = true;
            p.island_timed_row = true;
            p.island_ferry = true;
            p.island_swimmers = 3;
            p.island_builders = 0;
            break;
    }
    // The war batch (docs/BOTS.md, "The war batch"; the owner, 2026-10-09: "they just don't fight, they just eat ... they need to start more fights with each other, fire at their opponent, and try to kill their
    // fire ant if they try to come for them ... the bomber ... should be bombing up the food"). Every level fights over the food that is left and, with the food gone and an ant of nothing to do, goes for the
    // enemy ants; the weaker the level the later it starts and the better the odds it asks for. Medium and Hard also light the ring round the gate of the best opponent whether or not he can put the fire out (a
    // Fire Ant is cheap, and the walls that stand cost the owner his way out), hunt the Fire and Bomber Ants that come near the own hill, and lay mines at the piles an enemy works and round his gate while
    // an ant has nothing to harvest. `--tune war=0` switches all of it off again (tools/bot_arena.cpp)
    switch (level) {
        case Level::Easy:
            p.assault = true;
            p.assault_force = 2;
            p.assault_min = 2;
            p.assault_odds_percent = 200;
            p.assault_after = 1800;
            p.raider_hunt = true;
            p.raider_radius = 4;
            p.behind_war = true;
            p.behind_tier1 = 45;
            p.behind_tier2 = 100;
            p.behind_tier3 = 180;
            p.behind_free_tier = 3;
            p.behind_odds_ease = 8;
            p.behind_assault_after = 900;
            p.fire_duel = true;                      // (the stand batch: the Fire Ant strikes the Fire Ant that fires the gate in, the ramp is cleared; Easy drafts nobody, mines nowhere and never rushes)
            p.ramp_unjam = true;
            break;
        case Level::Medium:
            p.assault = true;
            p.assault_force = 2;
            p.assault_min = 2;
            p.assault_odds_percent = 100;
            p.assault_after = 900;
            p.sabotage = true;
            p.sabotage_safe = false;
            p.sabotage_after = 5400;                 // (Hard lights from tick 600: the walls cost the victim the food that he would still bring in; Medium's economy is slower, it waits until tick 5,400: the last 3.5 minutes on SMALL, 1.5 on TINY, 7.5 on TREASURE; from tick 1,800 when it is behind)
            p.war_fires = 2;
            p.raider_hunt = true;
            p.raider_radius = 5;
            p.war_free_only = true;
            p.war_bombers = 1;
            p.mine_gate = 2;
            p.behind_war = true;
            p.behind_tier1 = 25;
            p.behind_tier2 = 55;
            p.behind_tier3 = 105;
            p.behind_free_tier = 3;
            p.behind_odds_ease = 10;
            p.behind_assault_after = 600;
            p.fire_duel = true;
            p.ramp_unjam = true;
            p.behind_mine_tier = 2;
            p.behind_mine_gate = 3;
            p.mine_home = 8;
            p.mine_home_apart = 2;
            p.rush = true;
            p.rush_deficit = 300;
            break;
        case Level::Hard:
            p.assault = true;
            p.assault_force = 3;
            p.assault_min = 2;
            p.assault_odds_percent = 60;
            p.assault_after = 600;
            p.sabotage = true;
            p.sabotage_safe = false;
            p.war_fires = 2;
            p.raider_hunt = true;
            p.raider_radius = 6;
            p.war_free_only = true;
            p.war_bombers = 1;                       // (two Bombers wanted cost the ISLANDS expedition 5 percent of its food: the crew needs the Bombers of its row)
            p.mine_per_pile = 10;                    // (David, 2026-10-10: "the bomber only places three bombs, it could bomb up a whole area": 10 at a pile, mines that touch, and 12 round the own hill cost no food and win kills; the gate keeps 3 (5 for a loser) so that the fire-in still has tiles to light: docs/BOTS.md, "The stand batch")
            p.mine_apart = 1;
            p.mine_gate = 3;
            p.behind_war = true;                     // (tiers 15, 35, 70; the odds 12 percent less at every tier, the Combat Ants off the piles from tier 3: from tier 2 they cost Hard 4 percent of its food four against four)
            p.fire_duel = true;                      // the stand batch (docs/BOTS.md, "The stand batch"; fire_draft stays off: the draft cost food and won nothing the duel did not)
            p.ramp_unjam = true;
            p.behind_mine_tier = 2;                  // (tier 1 cost four Hard bots against four 10 percent of their food on TREASURE, tier 2 four percent, tier 3 nothing)
            p.behind_mine_gate = 5;
            p.mine_home = 12;
            p.mine_home_apart = 2;
            p.rush = true;
            p.rush_deficit = 200;
            break;
    }
    return p;
}

void without_stand_batch(LevelPlan& p) noexcept {
    p.fire_duel = p.fire_draft = p.ramp_unjam = p.rush = false;
    p.behind_mine_tier = 4;
    p.behind_mine_gate = p.mine_home = 0;
    p.mine_per_pile = std::min<uint32_t>(p.mine_per_pile, 3);                  // (the field of mines at the piles is the batch's: three were all there was)
    p.mine_apart = 2;
    p.mine_home_apart = 3;
}

void without_war_batch(LevelPlan& p) noexcept {
    without_stand_batch(p);
    p.war_bombers = p.war_fires = p.mine_per_pile = p.mine_gate = p.raider_extra = p.assault_force = 0;
    p.raider_hunt = p.raider_piles = p.assault = p.war_free_only = false;
    p.sabotage = p.level == Level::Hard && p.style == Style::Aggressive;
    p.sabotage_safe = true;
    p.sabotage_after = LevelPlan{}.sabotage_after;
    p.behind_war = false;
}

namespace {

// v changed by up to +- pct percent (an integer number of percent, drawn from the generator); never below 1 for a positive v
uint32_t jitter(uint32_t v, uint32_t pct, BotRng& rng) noexcept {
    if (v == 0 || pct == 0) return v;
    const int64_t d = static_cast<int64_t>(rng.below(2u * pct + 1u)) - static_cast<int64_t>(pct);
    return static_cast<uint32_t>(std::max<int64_t>(1, static_cast<int64_t>(v) * (100 + d) / 100));
}

}  // namespace

Style draw_style(Level level, BotRng& rng) noexcept {
    static const Style all[4] = {Style::Aggressive, Style::Economic, Style::Raider, Style::Defensive};
    if (level == Level::Hard) return rng.below(2) == 0 ? Style::Aggressive : Style::Raider;       // "Hard bots should be really aggressive"
    return all[rng.below(4)];
}

LevelPlan plan_for(Level level, Style style, BotRng& rng) noexcept {
    LevelPlan p = plan_for(level);
    if (style == Style::Random) style = draw_style(level, rng);
    p.style = style;
    // a level that is not Easy plays its style (Easy keeps its plan: a style changes little there, only the numbers move); Hard plays Aggressive or Raider (draw_style, style_allowed). A style
    // picks among what the level unlocks and says how early and how hard: it never unlocks a tactic (theft is Hard's, the gate is Hard's) and never touches the profile
    if (level != Level::Easy) {
        const bool hard = level == Level::Hard;
        switch (style) {
            case Style::Aggressive:
                p.contest_opening_ants += 1;
                p.raid_min_loot = p.raid_min_loot * 2 / 3;
                p.defenders += 1;
                p.harass = true;                                                             // the visible part: Combat Ants go for the carriers of the best opponent that are near them (a militia: it harvests when nothing is near)
                p.harass_idle_release = 20;
                p.harass_range = 8;                                                          // (14 tiles lost 35 points of the group wins in the cross table against the raider, 8 tiles and the learning below 12: docs/BOTS.md "Styles")
                p.harass_retreat_hp = 7;                                                     // a member that is hurt leaves, and its team is left alone for a while (twice as long every time)
                p.harass_pause_ticks = 3000;
                p.harass_strong_defence = 3;                                                 // so is a team that answers with three ants at once (a Hard bot's defenders)
                if (hard) {
                    p.max_combat = 2;                                                        // (the second Combat Ant is a theft: Hard's)
                    p.combat_extra = 0;
                    p.sabotage = true;                                                       // "they could fire your whole basin and then you cannot eat": a stolen Fire Ant walls in the gate of the best opponent
                    p.fire_extra = 1;
                    p.strikes = true;                                                        // behind the leader, a force goes for its carriers
                } else {                                                                     // Medium: a milder one, careful odds
                    p.harass_odds_percent = 150;
                }
                break;
            case Style::Economic:
                p.contest_opening_ants = 0;
                p.race = false;                                                               // (no contest of the middle: that is the style)
                p.wall_trigger = WallTrigger::Early;
                p.raid_min_loot = p.raid_min_loot * 3;                                       // (it defends like the others: the Combat Ant that harvests costs nothing and is its defence)
                break;
            case Style::Raider:
                p.secure_kinds = static_cast<uint8_t>((1u << static_cast<unsigned>(sim::AntType::Fire)) | (1u << static_cast<unsigned>(sim::AntType::Thief)));    // (Fire and Thief first of the opening's trips: the Bomber is not secured, so its place in the order does not matter)
                p.raid_min_loot = p.raid_min_loot / 2;
                p.raid_black_ticks = 300;
                p.avoids_guarded_hills = false;
                break;
            case Style::Defensive:
                p.wall_trigger = WallTrigger::Early;                                         // (no interception of thieves: it measured 15 percent of the wins against the mix, docs/BOTS.md "Styles")
                p.defenders += 1;
                p.ally_help = true;
                p.contest_opening_ants = 0;
                p.race = false;                                                               // (no contest of the middle)
                break;
            case Style::Random:
                break;
        }
    }
    // the bot's own variations: thresholds and timings move by 10 to 25 percent, the equal power-ups of the opening come in an order of their own
    const uint32_t big = level == Level::Easy ? 10u : 25u;
    const uint32_t small = level == Level::Easy ? 10u : 15u;
    p.raid_min_loot = jitter(p.raid_min_loot, big, rng);
    p.raid_black_ticks = jitter(p.raid_black_ticks, big, rng);
    p.wall_latch_ticks = jitter(p.wall_latch_ticks, small, rng);
    p.renew_lead_ticks = jitter(p.renew_lead_ticks, big, rng);
    p.fight_linger_ticks = jitter(p.fight_linger_ticks, big, rng);
    p.leash_tiles = static_cast<int32_t>(jitter(static_cast<uint32_t>(p.leash_tiles), small, rng));
    p.contest_low = jitter(p.contest_low, 10u, rng);
    p.contest_high = jitter(p.contest_high, 10u, rng);
    p.strike_margin = jitter(p.strike_margin, big, rng);
    p.hatch_window = jitter(p.hatch_window, big, rng);
    if (rng.below(2) == 1) {                                               // Thief, Combat and Swimmer are worth the same to a person: which comes first is a matter of taste
        for (size_t i = 0; i + 2 < p.opening_order.size(); ++i) {
            if (p.opening_order[i] == sim::AntType::Thief && p.opening_order[i + 1] == sim::AntType::Combat) std::swap(p.opening_order[i], p.opening_order[i + 1]);
        }
    }
    return p;
}

// ---- the thief hole -------------------------------------------------------------------------------------------------------------------------------------

std::array<sim::TileCoord, 3> east_tiles(const HillInfo& hill) noexcept {
    return {sim::TileCoord{hill.origin.x + 4, hill.origin.y + 1}, sim::TileCoord{hill.origin.x + 4, hill.origin.y + 2}, sim::TileCoord{hill.origin.x + 4, hill.origin.y + 3}};
}

namespace {

EastTile classify_east(const sim::Grid& grid, sim::TileCoord t) noexcept {
    if (!grid.in_bounds(t)) return EastTile::Blocked;
    const sim::TileCell& c = grid.get_cell(t);
    if (c.has_fire()) return EastTile::Wall;
    if (c.has_bomb()) return EastTile::Bomb;
    const uint8_t cls = grid.terrain_class_at(t);
    if (!sim::movement::terrain_walkable(cls)) return EastTile::Blocked;                // water (a bridge is mud: walkable)
    if (grid.is_solid_object(t)) return EastTile::Blocked;                              // rocks and every other solid object: a pile, a lunchbox, a power-up, a wall that is being lit
    if (cls == sim::movement::kTerrainMud) return EastTile::Bare;                       // the engine lights no fire wall and plants no bomb on mud
    return EastTile::Open;
}

}  // namespace

EastTile classify_tile(const sim::Grid& grid, sim::TileCoord tile) noexcept { return classify_east(grid, tile); }

EastState east_state(const sim::Grid& grid, const HillInfo& hill) noexcept {
    EastState st;
    if (!hill.present) return st;
    const std::array<sim::TileCoord, 3> tiles = east_tiles(hill);
    for (size_t i = 0; i < 3; ++i) st.tile[i] = classify_east(grid, tiles[i]);
    return st;
}

// ---- the sides of the map -----------------------------------------------------------------------------------------------------------------------------

namespace {
// The team that reaches a target at the least cost, strictly less than every other team of the match (-1: none reaches it, or two reach it equally well)
int nearest_team(const std::array<Approach, sim::MAX_PLAYERS>& approach, uint8_t present) noexcept {
    int best = -1;
    int32_t best_cost = 0;
    bool tie = false;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        if (((present >> t) & 1u) == 0 || !approach[t].reachable()) continue;
        const int32_t cost = approach[t].cost;
        if (best < 0 || cost < best_cost) {
            best = t;
            best_cost = cost;
            tie = false;
        } else if (cost == best_cost) {
            tie = true;
        }
    }
    return tie ? -1 : best;
}
}  // namespace

int power_up_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept {
    for (const PowerUpInfo& p : map.powerups()) {
        if (p.tile == tile) return nearest_team(p.approach, present);
    }
    return -1;
}

int drop_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept {
    const FlowerInfo* f = map.flower_at(tile);
    return f != nullptr ? nearest_team(f->approach, present) : -1;
}

// ---- the memory ---------------------------------------------------------------------------------------------------------------------------------------

uint64_t Memory::wall_seen(sim::TileCoord tile) const noexcept {
    const auto it = wall_seen_.find({tile.x, tile.y});
    return it != wall_seen_.end() ? it->second : 0u;
}

uint8_t Memory::hp_before(uint32_t ant) const noexcept {
    const auto it = own_.find(ant);
    return it != own_.end() ? it->second.hp : uint8_t{0};
}

void Memory::update(const BotView& v, const MapInfo& map) {
    now_ = v.tick();
    hits_.clear();
    thieves_.clear();

    // the seat's own ants: who lost hit points since the last look (an ant that is not in the previous view cannot be compared), and who is gone
    std::map<uint32_t, Last> now_own;
    for (const AntView& a : v.mine()) {
        Last l;
        l.hp = a.hp;
        l.tile = a.tile;
        l.carried = a.holding || a.carried_points > 0;
        l.type = a.type;
        const auto it = own_.find(a.id);
        if (it != own_.end() && a.hp < it->second.hp) hits_.push_back(Hit{a.type, a.id, a.tile, it->second.hp, a.hp, it->second.carried});
        now_own[a.id] = l;
    }
    for (const auto& e : own_) {
        if (now_own.count(e.first) == 0) hits_.push_back(Hit{e.second.type, e.first, e.second.tile, e.second.hp, 0, e.second.carried});     // gone: it was killed (or drowned)
    }
    own_ = std::move(now_own);
    if (!hits_.empty()) {
        last_hit_ = v.tick();
        hit_log_.emplace_back(v.tick(), static_cast<uint32_t>(hits_.size()));
        for (const Hit& h : hits_) {
            if (h.hp_now == 0) last_loss_ = v.tick();
            if (h.type == sim::AntType::Thief) continue;
            for (const AntView& e : v.others()) {                                        // a blow: an ant of another team that is no Thief stands next to the victim
                if (e.type != sim::AntType::Thief && e.tile.chebyshev_dist(h.tile) <= 2) {
                    last_attacked_ = v.tick();
                    break;
                }
            }
        }
    }
    while (!hit_log_.empty() && v.tick() > hit_log_.front().first + 2400u) hit_log_.erase(hit_log_.begin());

    // the ally's ants that were hit since the previous look (the hit clip: the flinch or the flight of the ant that was struck)
    ally_hits_.clear();
    std::map<uint32_t, bool> hit_now;
    if (v.ally() < sim::MAX_PLAYERS) {
        for (const AntView& a : v.others()) {
            if (a.team != v.ally() || (a.state != sim::UnitState::Flinch && a.state != sim::UnitState::Knockback)) continue;
            hit_now[a.id] = true;
            if (ally_in_hit_.count(a.id) == 0) ally_hits_.push_back(a.tile);
        }
    }
    ally_in_hit_ = std::move(hit_now);

    // the other teams: who moves, how many ants each shows, and the Thief ants in sight
    std::map<uint32_t, sim::TileCoord> thief_now;
    std::array<uint32_t, sim::MAX_PLAYERS> shown{};
    for (const AntView& a : v.others()) ++shown[a.team];
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) peak_ants_[t] = std::max(peak_ants_[t], shown[t]);
    for (const AntView& a : v.others()) {
        if (a.state == sim::UnitState::Attacking) attacking_[a.id] = v.tick();
    }
    for (const AntView& a : v.others()) {
        if (a.state != sim::UnitState::Idle && a.state != sim::UnitState::GuardIdle && seen_moving_[a.team] == 0) seen_moving_[a.team] = v.tick();
        if (a.type == sim::AntType::Combat) combat_last_seen_ = v.tick();
        if (a.type != sim::AntType::Thief) continue;
        ThiefSighting t;
        t.ant = a.id;
        t.team = a.team;
        t.tile = a.tile;
        const auto it = enemy_tile_.find(a.id);
        if (it != enemy_tile_.end()) t.before = it->second;
        t.carrying = a.holding;
        thieves_.push_back(t);
        thief_now[a.id] = a.tile;
        thief_last_seen_ = v.tick();
    }
    enemy_tile_ = std::move(thief_now);

    // the flowers: a droplet in the air is counted once (the landing tick it implies is about the same at every look of its fall)
    for (const FlowerView& f : v.flowers()) {
        if (!f.falling) continue;
        const uint64_t lands = v.tick() + kFlowerFallTicks - std::min<uint64_t>(f.age, kFlowerFallTicks);
        FlowerLog* log = nullptr;
        for (FlowerLog& l : flowers_) log = l.drop == f.drop ? &l : log;
        if (log == nullptr) {
            flowers_.emplace_back();
            log = &flowers_.back();
            log->drop = f.drop;
        }
        if (lands <= log->last) continue;
        log->before_last = log->last;
        log->last = lands;
        ++log->landings;
        ++log->by_kind[static_cast<size_t>(f.kind)];
    }

    // the walls in front of the own thief hole: the tick of the first look that saw each
    const HillInfo& hill = map.hill(v.seat());
    if (hill.present && v.has_grid()) {
        for (const sim::TileCoord& t : east_tiles(hill)) {
            const std::pair<int32_t, int32_t> key{t.x, t.y};
            if (v.grid().has_fire_at(t)) wall_seen_.emplace(key, v.tick());              // (keeps the first sighting)
            else wall_seen_.erase(key);
        }
    }
}

const FlowerLog* Memory::flower(sim::TileCoord drop) const noexcept {
    for (const FlowerLog& l : flowers_) {
        if (l.drop == drop) return &l;
    }
    return nullptr;
}

uint32_t FlowerLog::chance(uint32_t kinds) const noexcept {
    uint32_t hit = 0;
    uint32_t all = 0;
    for (size_t k = 1; k < by_kind.size(); ++k) {                                   // Bomber .. Swimmer
        const bool wanted = ((kinds >> k) & 1u) != 0;
        const uint32_t n = landings < 3 ? 1u : by_kind[k];
        all += n;
        hit += wanted ? n : 0u;
    }
    return all == 0 ? 0u : hit * 1000u / all;
}

// ---- the standing -------------------------------------------------------------------------------------------------------------------------------------------

Standing standing_of(const LevelPlan& plan, const BotView& view, const MapInfo& map) {
    Standing st;
    st.mine = view.score();
    const uint8_t ally = view.ally();
    int32_t best = -1;
    int32_t best_distance = 0;
    const HillInfo& own = map.hill(view.seat());
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (t == view.seat() || !row.present || row.dropped || (ally < sim::MAX_PLAYERS && t == ally)) continue;
        const HillInfo& hill = map.hill(t);
        const int32_t d = own.present && hill.present ? own.queue.chebyshev_dist(hill.queue) : 0;
        if (row.score > best || (row.score == best && d < best_distance)) {                // (an alliance of enemies shows one box twice: the member whose hill is nearer)
            best = row.score;
            best_distance = d;
            st.leader = t;
        }
    }
    if (st.leader < 0) return st;
    st.leader_score = best;
    const int32_t margin = std::max<int32_t>(static_cast<int32_t>(plan.strike_margin), best * static_cast<int32_t>(plan.strike_margin_percent) / 100);
    st.behind = best >= static_cast<int32_t>(plan.strike_min_leader) && st.mine + margin <= best;
    st.ahead = st.mine >= best;
    // the pressure: how far behind, as a share of what can still be earned (a margin that is fixed in points, as `behind` has, is almost never reached in a close match and is far too large near the end)
    if (best > st.mine) {
        st.deficit = best - st.mine;
        uint64_t food = 0;
        for (const PileView& p : view.piles()) food += static_cast<uint64_t>(p.remaining) * p.value;
        const uint64_t time_points = static_cast<uint64_t>(view.ticks_left()) * plan.catchup_earn_milli / 1000u;
        const uint64_t earnable = std::max<uint64_t>(std::min(food, time_points), 100u);
        st.pressure = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(st.deficit) * 100u / earnable, 100000u));
        if (plan.catchup && best >= static_cast<int32_t>(plan.catchup_min_leader)) {
            st.tier = st.pressure >= plan.catchup_tier3 ? 3 : st.pressure >= plan.catchup_tier2 ? 2 : st.pressure >= plan.catchup_tier1 ? 1 : 0;
        }
        if (plan.behind_war && best >= static_cast<int32_t>(plan.behind_min_leader)) {
            st.war = st.pressure >= plan.behind_tier3 ? 3 : st.pressure >= plan.behind_tier2 ? 2 : st.pressure >= plan.behind_tier1 ? 1 : 0;
        }
    }
    if (plan.endgame && view.ticks_left() <= plan.endgame_ticks) {
        if (st.ahead) st.guard = true;                                                        // with the lead and little time left: protect it
        else if (plan.catchup && best >= static_cast<int32_t>(plan.catchup_min_leader)) st.tier = 3;       // behind with little time left: all-in
        if (!st.ahead && plan.behind_war && best >= static_cast<int32_t>(plan.behind_min_leader)) st.war = 3;
    }
    st.tier = std::max(st.tier, st.war);                                                      // (the war tier is a tier of the catch-up too: raids for a smaller loot, the strike, the Combat Ants)
    return st;
}

// ---- the threat to the thief hole ---------------------------------------------------------------------------------------------------------------------

bool reachable_by(const MapInfo& map, uint8_t team, sim::TileCoord tile) noexcept {
    const int32_t home = map.hill_component(team);
    if (home < 0) return false;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (map.component(team, sim::TileCoord{tile.x + dx, tile.y + dy}) == home) return true;
        }
    }
    return false;
}

bool wall_demand(const Tactics& tactics, const BotView& view, const MapInfo& map) {
    const HillInfo& hill = map.hill(view.seat());
    if (!hill.present || !view.has_grid() || view.ticks_left() < 400) return false;
    if (!east_state(view.grid(), hill).shuttable()) return false;                       // a mud tile cannot take a wall: the hole cannot be shut
    const Memory& m = tactics.memory;
    const uint64_t now = view.tick();
    const LevelPlan& plan = tactics.plan;
    if (plan.wall_trigger == WallTrigger::Never) return false;
    const bool seen = m.thief_last_seen() != 0 && now <= m.thief_last_seen() + plan.wall_latch_ticks;
    if (seen) return true;
    if (plan.wall_trigger == WallTrigger::ThiefSeen) return false;
    // an enemy that can reach a Thief power-up that lies on the map: Medium waits until the enemy plays (the idle bot's ants never move: nothing of it is a threat), Hard does not wait
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (t == view.seat() || !row.present || row.dropped || (view.ally() < sim::MAX_PLAYERS && t == view.ally())) continue;
        if (plan.wall_trigger == WallTrigger::ThiefPossible && !m.plays(t)) continue;
        for (const PowerUpView& p : view.powerups()) {
            if (p.kind == sim::AntType::Thief && reachable_by(map, t, p.tile)) return true;
        }
    }
    return false;
}

}  // namespace ants::ai
