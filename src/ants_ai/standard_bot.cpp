#include "ants_ai/standard_bot.hpp"

namespace ants::ai {

namespace {
constexpr uint8_t kRankHarvest = 1;
constexpr uint8_t kRankFight = 5;
}  // namespace

void StandardBot::start(const BotContext& context) {
    seat_ = context.seat;
    profile_ = context.profile;
    map_ = context.map;
    ledger_.set_rank(kHarvest, kRankHarvest);
    ledger_.set_rank(kFight, kRankFight);
}

void StandardBot::think(const BotView& view, Orders& orders) {
    const uint64_t now = view.tick();

    // 1. An invitation to team up waits for an answer for ever, and an alliance of all live teams ends the match at once: decline, once (see WorkerBot::think)
    if (view.invite_from() < sim::MAX_PLAYERS && now >= deny_after_) {
        orders.deny(view.invite_from());
        ++denials_;
        const uint64_t longest = profile_.reaction_delay + profile_.reaction_delay * profile_.jitter_percent / 100u + profile_.intent_ttl;
        deny_after_ = now + longest + 2u;
    }

    const MapInfo* map = view.map() != nullptr ? view.map() : map_;
    if (map == nullptr) return;                                  // without the analysis of the map there is nothing to plan with (the controller always hands it over)
    ledger_.forget_missing(view.mine());
    tactics_.memory.update(view, *map);
    TaskContext context{view, orders, ledger_, profile_, *map, seat_};

    // 2. the tasks, the one that takes ants from the others first
    fight_.step(context);
    harvest_.step(context);
}

void StandardBot::on_command(const sim::Command& command, Fate fate, uint64_t tick) {
    if (command.type == sim::CommandType::AllianceDeny) {
        deny_after_ = fate == Fate::Sent ? tick + 12u : 0u;
        return;
    }
    fight_.on_command(command, fate, tick);
    harvest_.on_command(command, fate, tick);
}

}  // namespace ants::ai
