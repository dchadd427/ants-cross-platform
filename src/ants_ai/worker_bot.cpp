#include "ants_ai/worker_bot.hpp"

namespace ants::ai {

void WorkerBot::start(const BotContext& context) {
    seat_ = context.seat;
    profile_ = context.profile;
    map_ = context.map;
}

void WorkerBot::think(const BotView& view, Orders& orders) {
    const uint64_t now = view.tick();

    // 1. An invitation to team up waits for an answer for ever (the simulation never lets it expire), and an alliance of all live teams ends the match at once: decline. The
    // Deny is proposed once; the next look (a Hard bot looks every 4 ticks, the reaction delay is longer) must not propose it again before the first one has left or died.
    if (view.invite_from() < sim::MAX_PLAYERS && now >= deny_after_) {
        orders.deny(view.invite_from());
        ++denials_;
        const uint64_t longest = profile_.reaction_delay + profile_.reaction_delay * profile_.jitter_percent / 100u + profile_.intent_ttl;
        deny_after_ = now + longest + 2u;
    }

    // 2. the economy
    const MapInfo* map = view.map() != nullptr ? view.map() : map_;
    if (map == nullptr) return;                                  // without the analysis of the map there is nothing to plan with (the controller always hands it over)
    ledger_.forget_missing(view.mine());
    TaskContext context{view, orders, ledger_, profile_, *map, seat_};
    harvest_.step(context);
}

void WorkerBot::on_command(const sim::Command& command, Fate fate, uint64_t tick) {
    if (command.type == sim::CommandType::AllianceDeny) {
        deny_after_ = fate == Fate::Sent ? tick + 12u : 0u;      // sent: it takes a few ticks to be applied (a room: 100 - 190 ms); never sent: ask again at the next look
        return;
    }
    harvest_.on_command(command, fate, tick);
}

}  // namespace ants::ai
