#include "ants_ai/standard_bot.hpp"

#include "ants_ai/team_up.hpp"

namespace ants::ai {

namespace {
constexpr uint8_t kRankHarvest = 1;
constexpr uint8_t kRankGuard = 2;
constexpr uint8_t kRankPowerUps = 3;
constexpr uint8_t kRankWalls = 4;
constexpr uint8_t kRankFight = 5;
constexpr uint8_t kRankGates = 6;                 // (a gate goal exists only while the food is walled off for the seat; a Sealed vault (HELLRAISER) is raided again and again, so the goal repeats while the vault has food: the Fire ant or Bomber is worth more to the bot than to the mines, the walls or a fight)
constexpr uint64_t kBreachTicks = 2400;           // (the opening of a gate map: the fire walls against a thief and the trips to the power-ups of the own side wait this long at most, 2 minutes)
}  // namespace

void StandardBot::start(const BotContext& context) {
    seat_ = context.seat;
    profile_ = context.profile;
    map_ = context.map;
    if (styled_) {                                              // the style (the pinned one, or one drawn from the seat's own generator) and the bot's own variations
        BotRng rng(mix64(context.rng_seed ^ 0x57A1E57A1Eull));
        style_ = requested_style_ != Style::Random ? requested_style_ : draw_style(level_, rng);
        tactics_.plan = plan_for(level_, style_, rng);
        if (tune_) tune_(tactics_.plan);
        harvest_.set_params(harvest_params(tactics_.plan));
        gate_.set_params(gate_params(tactics_.plan));
        island_.set_params(island_params(tactics_.plan));
        expedition_.set_params(expedition_params(tactics_.plan));
        ferry_.set_params(ferry_params(tactics_.plan));
        gate_goals_.set_params(gate_goal_params(tactics_.plan));
        brawl_.set_params(tactics_.plan.brawl);
    }
    ledger_.set_rank(kHarvest, kRankHarvest);
    ledger_.set_rank(kFight, kRankFight);
    ledger_.set_rank(kRush, kRankFight);                          // (the same rank as a fight: the defenders of a fight at home stay, and the rush takes the walls', the mines' and the economy's ants)
    ledger_.set_rank(kWalls, kRankWalls);
    ledger_.set_rank(kPowerUps, kRankPowerUps);
    ledger_.set_rank(kRaids, kRankPowerUps);
    ledger_.set_rank(kBombs, kRankWalls);
    ledger_.set_rank(kMines, kRankWalls);
    ledger_.set_rank(kGuard, kRankGuard);
    ledger_.set_rank(kStrike, kRankWalls);
    ledger_.set_rank(kHarass, kRankWalls);
    ledger_.set_rank(kSabotage, kRankPowerUps);                   // (below the walls of the own thief hole: the Fire Ant is theirs first)
    ledger_.set_rank(kIslands, kRankPowerUps);                    // (the Swimmers that dig, and the ants that fetch one: nobody else uses them)
    ledger_.set_rank(kExpedition, kRankPowerUps);                 // (the crew that is flown to the Swimmers and the Bombers that fly it)
    ledger_.set_rank(kGateGoals, kRankGates);                     // (the Fire ant and the Bomber of a gate goal)
    ledger_.set_rank(kField, kRankGates);                         // (the ant of the field breach)
    ledger_.set_rank(kOpportunity, kRankFight);                   // (the kill that one blow gives: an ant is taken from the chores and the economy, never from a pick-up, a raid or a fight)
    ledger_.set_rank(kFerry, kRankGuard);                         // (the Swimmers that carry food: the island task takes one when a bridge is wanted)
}

void StandardBot::think(const BotView& view, Orders& orders) {
    const uint64_t now = view.tick();

    // 1. An invitation to team up waits for an answer for ever: answer it once (see WorkerBot::think), by the accept rule
    if (view.invite_from() < sim::MAX_PLAYERS && now >= deny_after_) {
        if (accepts_invitation(view, view.invite_from())) {
            orders.accept(view.invite_from());
            ++accepts_;
        } else {
            orders.deny(view.invite_from());
            ++denials_;
        }
        const uint64_t longest = profile_.reaction_delay + profile_.reaction_delay * profile_.jitter_percent / 100u + profile_.intent_ttl;
        deny_after_ = now + longest + 2u;
    }

    if (now < tactics_.plan.bench_idle_ticks) return;            // (the tournaments' handicap: asleep)
    const MapInfo* map = view.map() != nullptr ? view.map() : map_;
    if (map == nullptr) return;                                  // without the analysis of the map there is nothing to plan with (the controller always hands it over)
    ledger_.forget_missing(view.mine());
    TaskContext context{view, orders, ledger_, profile_, *map, seat_};

    // 0. the game type 187 has no food: the whole fight is the brawl's (docs/BOTS.md, "187: the brawl")
    if (tactics_.plan.brawl187 && view.game_mode() == sim::GameMode::Kills187) {
        brawl_.step(context);
        return;
    }
    tactics_.memory.update(view, *map);

    // 1a. the gate play (Tactics::gate_map): decided once, from the analysis of the start
    if (!gate_decided_) decide_gate_map(context);

    // 1b. the stall detector: the score is the only progress; a stall sends the bot to the plain economy for a while
    update_progress(view);
    if (fallback_until_ != 0 && now >= fallback_until_) end_fallback();
    if (fallback_until_ == 0 && detect_stall(view)) begin_fallback(now);
    const bool fallback = fallback_until_ != 0;

    // 2. what the bot wants at this look: the fire walls (and a Fire Ant for them) when a thief threatens, the Combat Ants and the Thief of the level's plan once an enemy plays
    const LevelPlan& plan = tactics_.plan;
    tactics_.wants.fill(0);
    tactics_.surplus = harvest_.unplaced();
    tactics_.standing = standing_of(plan, view, *map);
    tactics_.guard_stance = tactics_.standing.guard;
    const Standing& st = tactics_.standing;
    if (last_look_ != 0 && now > last_look_) war_ticks_[st.war] += static_cast<uint32_t>(std::min<uint64_t>(now - last_look_, 1000u));    // (the ticks the bot spent at every war tier, for the arena's report)
    last_look_ = now;
    const bool escalating = plan.catchup && st.tier >= 1;                                    // behind the leader enough to escalate (the tiers: tactics.hpp, Standing)
    // In the opening of a gate map, while the bot reaches no food and has banked nothing, the fire walls against a thief and the trips to the power-ups of its side only take ants from the gate goal
    bool any_reachable_pile = false;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map->pile(p.index);
        if (info == nullptr || info->approach[seat_].reachable()) any_reachable_pile = true;
    }
    tactics_.no_work = !any_reachable_pile;
    const bool gate_breaching = tactics_.gate_map && !tactics_.gate_beside && !fallback && !gate_goals_.exhausted() && !any_reachable_pile && now < kBreachTicks && (view.score() == 0 || gate_goals_.phase() != GateGoalTask::Phase::Idle);
    tactics_.wall_demand = !gate_breaching && wall_demand(tactics_, view, *map);
    if (tactics_.wall_demand) tactics_.wants[static_cast<size_t>(sim::AntType::Fire)] = 1;
    uint8_t present = 0;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) present = static_cast<uint8_t>(present | (view.rows()[t].present ? 1u << t : 0u));
    if (plan.secure_side && !gate_breaching) {                    // the power-ups of the own side are taken early: an enemy that steals the Fire can wall the piles in, the Bomber can mine the base, the Thief can raid twice
        for (const PowerUpView& p : view.powerups()) {
            if (((plan.secure_kinds >> static_cast<unsigned>(p.kind)) & 1u) != 0 && power_up_side(*map, p.tile, present) == seat_) tactics_.wants[static_cast<size_t>(p.kind)] = 1;
        }
    }
    if (plan.flower_sides || plan.flower_fire) {  // the drops of the flowers (docs/BOTS.md, "The flowers"): a landed drop has a side like a power-up of the start, and a bot without a Fire Ant wants the Fire that lands
        for (const PowerUpView& p : view.powerups()) {
            const FlowerInfo* flower = map->flower_at(p.tile);
            if (flower == nullptr) continue;
            uint8_t& want = tactics_.wants[static_cast<size_t>(p.kind)];
            if (plan.flower_sides && plan.secure_side && ((plan.secure_kinds >> static_cast<unsigned>(p.kind)) & 1u) != 0 && drop_side(*map, p.tile, present) == seat_) want = std::max<uint8_t>(want, 1);
            if (plan.flower_fire && p.kind == sim::AntType::Fire && flower->approach[seat_].reachable()) want = std::max<uint8_t>(want, 1);
        }
    }
    bool enemy_plays = false;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (t != seat_ && row.present && !row.dropped && !(view.ally() < sim::MAX_PLAYERS && t == view.ally()) && tactics_.memory.plays(t)) enemy_plays = true;
    }
    if (enemy_plays && view.ticks_left() > 2400) {
        // the war batch: Bomber Ants for the mines, Fire Ants for the fire-in (the power-ups that the flowers let fall and those on the map; the pick-up trips are the ordinary ones)
        const bool war_now = !plan.war_free_only || tactics_.surplus > 0 || (plan.behind_war && st.war >= 1 && st.war >= plan.behind_free_tier);
        const bool mine_now = war_now || (plan.behind_war && st.war >= 1 && st.war >= plan.behind_mine_tier)        // (the stand batch: a loser wants its Bomber from an earlier tier ...
                              || (plan.mine_home > 0 && now >= plan.mine_home_after);                              // ... and every bot for the mines round its hill)
        if (mine_now && (plan.mine_per_pile > 0 || plan.mine_gate > 0)) tactics_.wants[static_cast<size_t>(sim::AntType::Bomber)] = static_cast<uint8_t>(std::max<uint32_t>(tactics_.wants[static_cast<size_t>(sim::AntType::Bomber)], plan.war_bombers));
        if (plan.war_fires > 0) tactics_.wants[static_cast<size_t>(sim::AntType::Fire)] = static_cast<uint8_t>(std::max<uint32_t>(tactics_.wants[static_cast<size_t>(sim::AntType::Fire)], plan.war_fires + (tactics_.wall_demand ? 1u : 0u)));
        if (plan.sabotage && plan.fire_extra > 0) tactics_.wants[static_cast<size_t>(sim::AntType::Fire)] = static_cast<uint8_t>(std::max<size_t>(tactics_.wants[static_cast<size_t>(sim::AntType::Fire)], 1u + plan.fire_extra));
        if (plan.takes_thief) tactics_.wants[static_cast<size_t>(sim::AntType::Thief)] = static_cast<uint8_t>(plan.max_thief);
        // Combat Ants pay when the enemy fights (an own ant was hit lately, an enemy Combat Ant or Thief has been seen): against a passive economy they are an ant that does not harvest
        const Memory& m = tactics_.memory;
        // (or when the economy has workers that stand idle with nothing to harvest: they cost nothing)
        const bool fists = plan.fists_strict ? (m.last_attacked() != 0 && now <= m.last_attacked() + 2400u)
                                             : (m.last_hit() != 0 && now <= m.last_hit() + 2400u) || m.combat_last_seen() != 0 || m.thief_last_seen() != 0 || (plan.strikes && st.behind) || escalating;
        const bool free_ants = plan.combat_when_idle && tactics_.surplus > 0;
        const bool attacked = m.last_attacked() != 0 && now <= m.last_attacked() + 2400u;
        if (plan.takes_combat && (fists || free_ants || !plan.combat_when_attacked)) {
            tactics_.wants[static_cast<size_t>(sim::AntType::Combat)] = static_cast<uint8_t>(plan.max_combat + (attacked ? plan.combat_extra : 0u));
        }
        // behind the leader, a strike needs its Combat Ants: as many as the force is (from the lift tier on a level that does not take them otherwise takes them too, and a Thief for the raids)
        if ((plan.takes_combat || st.tier >= plan.catchup_lift_tier) && ((plan.strikes && !plan.strike_workers && st.behind) || (escalating && plan.catchup_wants))) {
            tactics_.wants[static_cast<size_t>(sim::AntType::Combat)] = static_cast<uint8_t>(std::max<uint32_t>(std::max<uint32_t>(plan.max_combat, plan.strike_force), tactics_.wants[static_cast<size_t>(sim::AntType::Combat)]));
        }
        if (!plan.takes_thief && st.tier >= plan.catchup_lift_tier && plan.catchup_wants) tactics_.wants[static_cast<size_t>(sim::AntType::Thief)] = 1;
    }

    // 3. the tasks, the one that takes ants from the others first
    if (plan.islands) island_.step(context);                                                // (idle where nothing lies beyond water; it wants a Swimmer, so it comes before the power-up task)
    if (plan.islands && plan.island_expedition) expedition_.step(context);                  // (idle while a Swimmer lies within a walk, or the bot has the Swimmers it wants)
    if ((plan.islands && plan.island_ferry && island_.active()) || tactics_.gate_map) ferry_.step(context);  // (only where food lies beyond water; idle without a Swimmer that the island task does not hold)
    if (tactics_.gate_map && !fallback) gate_goals_.step(context);                          // (Medium and Hard on a walled map: the right ant for a wall of fire or bombs, wanted before the power-up task chooses its trips)
    if (tactics_.field_map && !fallback) field_.step(context);                              // (a pile behind a field: one ant opens it)
    tactics_.gate_goals_live = tactics_.gate_map && !fallback && (!gate_goals_.exhausted() || (tactics_.field_map && field_.live()));    // (the counters spare the map's own bombs only while the goals are there to deal with them)
    opportunity_.step(context);                                                              // (before the fights and every chore: an enemy that one blow kills is struck at once; nothing when the plan has none)
    fight_.step(context);
    if (plan.rush || rush_.active()) rush_.step(context);                                   // (when the plan has no rush the task only gives back what it held)
    if (!gate_breaching) walls_.step(context);
    powerups_.step(context);
    bombs_.step(context);
    if (plan.mine_per_pile > 0 || plan.mine_gate > 0 || plan.mine_home > 0 || mines_.jobs() != 0) mines_.step(context);       // (when the plan has no mines the task only gives back what it held)
    const bool raiding = plan.raids || (plan.catchup && st.tier >= plan.catchup_lift_tier);
    raids_.set_launching(raiding);
    if ((raiding || ledger_.count(kRaids) != 0) && !fallback) raids_.step(context);         // (when the pressure falls the raid under way is seen out: the thief is not kept for ever)
    if (plan.guards) guard_.step(context);
    if (plan.strikes || plan.wipe_focus || plan.catchup) strike_.step(context);
    if (plan.harass) harass_.step(context);
    if (plan.sabotage) sabotage_.step(context);
    if (plan.hatches) hatch_.step(context);
    if (plan.gate && !fallback) gate_.step(context);
    if (plan.gate && !fallback) {                                                      // (review experiment) the economy's rescue is only off while the gate really guides
        HarvestTask::Params hp = harvest_.params();
        hp.rescue = !gate_.usable();
        harvest_.set_params(hp);
    }
    if (!plan.gate || !gate_.usable() || fallback) aid_.step(context);                                              // (the gate task owns every carrier, a hit one included)
    if (plan.islands) {
        harvest_.set_closed_piles(island_.closed_piles());                                  // the piles over a bridge that will not last a round trip
        harvest_.set_pile_limits(island_.pile_limits());                                    // and one ant at a time on a bridge
        if (island_.take_reask()) harvest_.reask_soon();                                    // a bridge was finished: the economy asks the map again now
    }
    harvest_.step(context);
}

// The gate play is on where the start analysis (GateInfo) finds every pile walled off from the seat's hill, also with the power-ups taken, and the island play (water, bridges, flights over channels)
// does not own the map. Medium and Hard play it; Easy never does. The decision is made once: a map that is open at the start stays open for the bot when its free piles are eaten, so an open map is
// played exactly as it was. A bot that is created in the middle of a match (a server that restores a match after a restart, room.cpp) takes this decision at its first look on the grid of that moment,
// not of the start: fire walls and bombs that exist then are the map it sees (low impact; not redesigned, docs/BOTS.md "Walled maps"). With the decision goes Tactics::gate_beside: an own worker that
// stands in a component next to a pile at that look (Ghetto: the start workers stand at the central pile). The plain economy has food to harvest there, so the opening is not held back.
void StandardBot::decide_gate_map(TaskContext& context) {
    if (!context.view.has_grid()) return;                                                   // (no grid, no analysis: asked again at the next look)
    gate_decided_ = true;
    tactics_.gate_map = false;
    tactics_.gate_beside = false;
    tactics_.field_map = false;
    tactics_.field_piles.clear();
    if (level_ == Level::Easy) return;
    const GateInfo gates = GateInfo::analyse(context.map, context.view.grid(), seat_);
    if (!gates.walled()) return;
    {   // a wall of power-ups (Running Ants: the pile lies in a ring of them) is cleared by the plain economy, which takes them: the map must be walled without them
        sim::Grid bare = context.view.grid();
        for (uint32_t y = 0; y < bare.height(); ++y) {
            for (uint32_t x = 0; x < bare.width(); ++x) {
                if (bare.has_powerup_at(sim::TileCoord{static_cast<int32_t>(x), static_cast<int32_t>(y)})) bare.clear_powerup(static_cast<int32_t>(x), static_cast<int32_t>(y));
            }
        }
        if (!GateInfo::analyse(context.map, bare, seat_).walled()) return;
    }
    if (tactics_.plan.islands && island_.probe(context)) return;                           // the island play owns the water maps (ISLANDS)
    tactics_.gate_map = true;
    if (tactics_.plan.gate_field) {
        const FieldPlan field = plan_field(context.view, context.map, seat_, FieldHints{});
        tactics_.field_map = field.feasible() && field_startable(field, context.view, context.map, seat_);
        FerryTask::Params ferry = ferry_.params();
        ferry.reorder_ended = tactics_.field_map;
        ferry_.set_params(ferry);
        for (const GateTarget& pile : gates.piles()) {
            if (tactics_.field_map && (pile.gate == GateKind::Sealed || pile.gate == GateKind::Mixed)) tactics_.field_piles.push_back(pile.index);
        }
    }
    for (const AntView& a : context.view.mine()) {
        if (a.type != context.view.default_ant_type()) continue;
        const int32_t comp = gates.component(a.tile);
        if (comp < 0) continue;
        for (const GateTarget& pile : gates.piles()) {
            for (const int32_t pile_comp : pile.comps) tactics_.gate_beside = tactics_.gate_beside || pile_comp == comp;
        }
    }
}

bool StandardBot::accepts_invitation(const BotView& view, uint8_t from) {
    return team_up_answer(view, from) == TeamUpAnswer::Accept;                       // the one rule (team_up.hpp): the application tells the player the reason of a refusal from the same function
}

// ---- the stall detector ------------------------------------------------------------------------------------------------------------------------------

// An order that left (Fate::Sent): the same type, tile and first ant again and again with nothing banked is an order that the world does not carry out. Attacks are left out: an attack order
// is one blow by design, and a fight repeats it for the same ant and tile.
void StandardBot::note_repeat(const sim::Command& command, uint64_t tick) {
    const LevelPlan& plan = tactics_.plan;
    if (plan.repeat_limit == 0 || command.ants.empty() || command.type == sim::CommandType::GroupAttack || fallback_until_ != 0) return;
    std::deque<uint64_t>& q = repeats_[std::make_tuple(static_cast<uint8_t>(command.type), command.tile_x, command.tile_y, command.ants[0])];
    q.push_back(tick);
    while (!q.empty() && q.front() + plan.repeat_window < tick) q.pop_front();
}

// Whether some order was sent repeat_limit times within the last repeat_window ticks
bool StandardBot::hammered(uint64_t now) {
    const LevelPlan& plan = tactics_.plan;
    if (plan.repeat_limit == 0) return false;
    for (auto& e : repeats_) {
        std::deque<uint64_t>& q = e.second;
        while (!q.empty() && q.front() + plan.repeat_window < now) q.pop_front();
        if (q.size() >= plan.repeat_limit) return true;
    }
    return false;
}

void StandardBot::update_progress(const BotView& view) {
    const uint64_t now = view.tick();
    const int32_t score = view.score();
    if (!progress_known_) {
        progress_known_ = true;
        progress_tick_ = now;
    } else if (score > last_score_) {
        progress_tick_ = now;                                                    // something was banked: whatever was repeated worked
    }
    last_score_ = score;
    if (now >= next_prune_) {                                                    // keep the memory small: a key that has not been sent within the window is forgotten
        next_prune_ = now + std::max<uint32_t>(tactics_.plan.repeat_window, 200u);
        for (auto it = repeats_.begin(); it != repeats_.end();) {
            if (it->second.empty() || it->second.back() + tactics_.plan.repeat_window < now) it = repeats_.erase(it);
            else ++it;
        }
    }
}

bool StandardBot::detect_stall(const BotView& view) {
    const LevelPlan& plan = tactics_.plan;
    // an order hammered repeat_limit times within the last window while nothing was banked for that whole window (a jam of a few seconds that clears by itself is no stall: a fallback costs the gate for minutes)
    if (view.tick() >= progress_tick_ + plan.repeat_window && hammered(view.tick())) return true;
    if (plan.stall_ticks == 0 || view.tick() < progress_tick_ + plan.stall_ticks || view.mine().empty()) return false;
    // food that the own hill can reach lies on the map (a map whose last reachable pile is gone leaves nothing to bank, and nothing to stall at)
    const MapInfo* map = view.map() != nullptr ? view.map() : map_;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map != nullptr ? map->pile(p.index) : nullptr;
        if (info == nullptr || info->approach[seat_].reachable()) return true;
    }
    return false;
}

void StandardBot::begin_fallback(uint64_t now) {
    const LevelPlan& plan = tactics_.plan;
    const uint64_t base = std::max<uint32_t>(plan.fallback_ticks, 1u);
    const uint64_t span = std::min<uint64_t>(base << std::min<uint32_t>(stalls_, 16u), std::max<uint64_t>(base, kMaxFallbackTicks));
    ++stalls_;
    fallback_until_ = now + span;
    progress_tick_ = now;                                                        // the clock of the next stall starts here
    ledger_.release_all(kRaids);                                                 // a thief that was kept for a raid goes back to the economy
    gate_goals_.cancel(ledger_);                                                 // so does the Fire ant or Bomber of a gate goal (its timers would run out unseen while the task sleeps)
    field_.cancel(ledger_);                                                      // and the ant of the field breach
    HarvestTask::Params plain;                                                   // the worker's harvest: no contested piles, no typed ants, the rescue on
    plain.cantgo_aware = plan.cantgo_aware;                                      // (and still no carrier sent into a hill that no walk joins: the fixes of "The can't-go loop" stay on)
    harvest_.set_params(plain);
}

void StandardBot::end_fallback() {
    fallback_until_ = 0;
    harvest_.set_params(harvest_params(tactics_.plan));
}

void StandardBot::on_command(const sim::Command& command, Fate fate, uint64_t tick) {
    if (command.type == sim::CommandType::AllianceDeny || command.type == sim::CommandType::AllianceAccept) {
        deny_after_ = fate == Fate::Sent ? tick + 12u : 0u;
        return;
    }
    if (fate == Fate::Sent) note_repeat(command, tick);
    fight_.on_command(command, fate, tick);
    walls_.on_command(command, fate, tick);
    powerups_.on_command(command, fate, tick);
    bombs_.on_command(command, fate, tick);
    mines_.on_command(command, fate, tick);
    raids_.on_command(command, fate, tick);
    guard_.on_command(command, fate, tick);
    strike_.on_command(command, fate, tick);
    rush_.on_command(command, fate, tick);
    harass_.on_command(command, fate, tick);
    sabotage_.on_command(command, fate, tick);
    island_.on_command(command, fate, tick);
    expedition_.on_command(command, fate, tick);
    ferry_.on_command(command, fate, tick);
    gate_goals_.on_command(command, fate, tick);
    field_.on_command(command, fate, tick);
    brawl_.on_command(command, fate, tick);
    opportunity_.on_command(command, fate, tick);
    gate_.on_command(command, fate, tick);
    aid_.on_command(command, fate, tick);
    harvest_.on_command(command, fate, tick);
}

}  // namespace ants::ai
