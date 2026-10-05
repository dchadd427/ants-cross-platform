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
    // v0.6 (docs/BOTS.md, "The race for contested food", "Fights of its own", "Fire play", "Behind the leader and the endgame"): the rules of the owner's report of 2026-10-05, at every level, scaled below; bot_arena's `prev` key
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
            p.hunt_blows = 2;
            p.hunt_force = 2;
            p.hunt_reach = 6;
            p.hunt_wide = false;                 // Easy hunts what stands next to its ants and nothing else
            p.hunt_odds_percent = 150;
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
            p.hunt_blows = 3;
            p.hunt_force = 3;
            p.hunt_reach = 10;                   // Medium hunts what is within the leash of its hill and at its piles as well
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
            p.avoids_guarded_hills = false;      // (a Combat Ant of the enemy is a worker that fights, not a guard: the raids of this bot go where the hole is open; measured, docs/BOTS.md "Aggression")
            p.raid_min_loot = 15;                // a raid for 15 points is a swing of 30 and a trip of a few hundred ticks: it pays (raidmin 10 / 30 / 60: 96.5 / 94.1 / 93.1 percent against Medium, Medium, Easy)
            p.hunt_blows = 4;
            p.hunt_force = 3;
            p.hunt_reach = 10;
            p.hunt_leader_carriers = true;       // Hard also hunts the carriers of the leading team wherever they are
            // (p.skirmish, the stronger force at a carrier or a worker at a pile, is built and OFF: against three bots of v0.5.0 on TREASURE a Hard bot with it won 14.1 percent of 96 matches and 28.6
            // without it, 25 being equal; it keeps three ants from the piles for up to 600 ticks at a time and the enemy's defenders come: docs/BOTS.md, "Fights of its own")
            break;
    }
    return p;
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

int power_up_side(const MapInfo& map, sim::TileCoord tile, uint8_t present) noexcept {
    for (const PowerUpInfo& p : map.powerups()) {
        if (p.tile != tile) continue;
        int best = -1;
        int32_t best_cost = 0;
        bool tie = false;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
            if (((present >> t) & 1u) == 0 || !p.approach[t].reachable()) continue;
            const int32_t cost = p.approach[t].cost;
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
    return -1;
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
    }
    if (plan.endgame && view.ticks_left() <= plan.endgame_ticks) {
        if (st.ahead) st.guard = true;                                                        // with the lead and little time left: protect it
        else if (plan.catchup && best >= static_cast<int32_t>(plan.catchup_min_leader)) st.tier = 3;       // behind with little time left: all-in
    }
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
