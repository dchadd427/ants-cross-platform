#include "ants_ai/standard_bot.hpp"

namespace ants::ai {

namespace {
constexpr uint8_t kRankHarvest = 1;
constexpr uint8_t kRankGuard = 2;
constexpr uint8_t kRankPowerUps = 3;
constexpr uint8_t kRankWalls = 4;
constexpr uint8_t kRankFight = 5;
}  // namespace

void StandardBot::start(const BotContext& context) {
    seat_ = context.seat;
    profile_ = context.profile;
    map_ = context.map;
    ledger_.set_rank(kHarvest, kRankHarvest);
    ledger_.set_rank(kFight, kRankFight);
    ledger_.set_rank(kWalls, kRankWalls);
    ledger_.set_rank(kPowerUps, kRankPowerUps);
    ledger_.set_rank(kRaids, kRankPowerUps);
    ledger_.set_rank(kBombs, kRankWalls);
    ledger_.set_rank(kGuard, kRankGuard);
    ledger_.set_rank(kHarass, kRankWalls);
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

    const MapInfo* map = view.map() != nullptr ? view.map() : map_;
    if (map == nullptr) return;                                  // without the analysis of the map there is nothing to plan with (the controller always hands it over)
    ledger_.forget_missing(view.mine());
    tactics_.memory.update(view, *map);
    TaskContext context{view, orders, ledger_, profile_, *map, seat_};

    // 2. what the bot wants at this look: the fire walls (and a Fire Ant for them) when a thief threatens, the Combat Ants and the Thief of the level's plan once an enemy plays
    const LevelPlan& plan = tactics_.plan;
    tactics_.wants.fill(0);
    tactics_.surplus = harvest_.unplaced();
    tactics_.wall_demand = wall_demand(tactics_, view, *map);
    if (tactics_.wall_demand) tactics_.wants[static_cast<size_t>(sim::AntType::Fire)] = 1;
    if (plan.secure_side) {                                       // the power-ups of the own side are taken early (the owner's playbook): an enemy that steals the Fire can wall the piles in, the Bomber can mine the base, the Thief can raid twice
        uint8_t present = 0;
        for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) present = static_cast<uint8_t>(present | (view.rows()[t].present ? 1u << t : 0u));
        for (const PowerUpView& p : view.powerups()) {
            if (((plan.secure_kinds >> static_cast<unsigned>(p.kind)) & 1u) != 0 && power_up_side(*map, p.tile, present) == seat_) tactics_.wants[static_cast<size_t>(p.kind)] = 1;
        }
    }
    bool enemy_plays = false;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (t != seat_ && row.present && !row.dropped && !(view.ally() < sim::MAX_PLAYERS && t == view.ally()) && tactics_.memory.plays(t)) enemy_plays = true;
    }
    if (enemy_plays && view.ticks_left() > 2400) {
        if (plan.takes_thief) tactics_.wants[static_cast<size_t>(sim::AntType::Thief)] = static_cast<uint8_t>(plan.max_thief);
        // Combat Ants pay when the enemy fights (an own ant was hit lately, an enemy Combat Ant or Thief has been seen): against a passive economy they are an ant that does not harvest
        const Memory& m = tactics_.memory;
        // (or when the economy has workers that stand idle with nothing to harvest: they cost nothing)
        const bool fists = (m.last_hit() != 0 && now <= m.last_hit() + 2400u) || m.combat_last_seen() != 0 || m.thief_last_seen() != 0;
        const bool free_ants = plan.combat_when_idle && tactics_.surplus > 0;
        if (plan.takes_combat && (fists || free_ants || !plan.combat_when_attacked)) tactics_.wants[static_cast<size_t>(sim::AntType::Combat)] = static_cast<uint8_t>(plan.max_combat);
    }

    // 3. the tasks, the one that takes ants from the others first
    fight_.step(context);
    walls_.step(context);
    powerups_.step(context);
    bombs_.step(context);
    if (plan.raids) raids_.step(context);
    if (plan.guards) guard_.step(context);
    if (plan.harasses) harass_.step(context);
    aid_.step(context);
    harvest_.step(context);
}

bool StandardBot::accepts_invitation(const BotView& view, uint8_t from) {
    if (from >= sim::MAX_PLAYERS || from == view.seat()) return false;
    if (view.ally() < sim::MAX_PLAYERS) return false;                                // never break an alliance
    const TeamRow& inviter = view.rows()[from];
    if (!inviter.present || inviter.dropped || inviter.ally < sim::MAX_PLAYERS) return false;
    std::array<bool, sim::MAX_PLAYERS> has_ant{};
    has_ant[view.seat()] = !view.mine().empty();
    for (const AntView& a : view.others()) has_ant[a.team] = true;
    size_t live = 0;
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        const TeamRow& row = view.rows()[t];
        if (row.present && !row.dropped && has_ant[t]) ++live;
    }
    return live >= 3;                                                                // with only two live teams the alliance would unite all of them: the match would end at once
}

void StandardBot::on_command(const sim::Command& command, Fate fate, uint64_t tick) {
    if (command.type == sim::CommandType::AllianceDeny || command.type == sim::CommandType::AllianceAccept) {
        deny_after_ = fate == Fate::Sent ? tick + 12u : 0u;
        return;
    }
    fight_.on_command(command, fate, tick);
    walls_.on_command(command, fate, tick);
    powerups_.on_command(command, fate, tick);
    bombs_.on_command(command, fate, tick);
    raids_.on_command(command, fate, tick);
    guard_.on_command(command, fate, tick);
    harass_.on_command(command, fate, tick);
    aid_.on_command(command, fate, tick);
    harvest_.on_command(command, fate, tick);
}

}  // namespace ants::ai
