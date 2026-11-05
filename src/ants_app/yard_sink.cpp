// Where the HUD's orders go in the Yard: see yard_sink.hpp.
#include "ants_app/yard_sink.hpp"

#include <algorithm>

namespace ants::app {

sim::CommandResult YardSink::submit(const sim::Command& command) {
    sim::CommandResult result;
    yard::Intent intent;
    switch (command.type) {
        case sim::CommandType::GroupMove: intent.kind = yard::IntentKind::Move; break;
        case sim::CommandType::GroupAttack: intent.kind = yard::IntentKind::Attack; break;
        case sim::CommandType::Stop: intent.kind = yard::IntentKind::Stop; break;
        default:
            result.status = sim::CommandResult::Status::RejectedNotAllowed;
            return result;
    }
    if (intent.kind != yard::IntentKind::Stop) {
        if (!puppet_.on_map(command.tile_x, command.tile_y)) {
            result.status = sim::CommandResult::Status::RejectedMalformed;
            return result;
        }
        intent.tile_x = command.tile_x;
        intent.tile_y = command.tile_y;
    }
    // The visitor's own ants, by the server's ids (at most kMaxOrderAnts: the HUD sends the nearest first)
    uint32_t first = 0;
    for (const uint32_t slot : command.ants) {
        if (intent.ants.size() >= yard::kMaxOrderAnts) break;
        if (!puppet_.is_own(slot)) continue;
        const uint16_t server = static_cast<uint16_t>(puppet_.server_id(slot));
        if (std::find(intent.ants.begin(), intent.ants.end(), server) != intent.ants.end()) continue;
        if (first == 0) first = slot;
        intent.ants.push_back(server);
    }
    if (intent.ants.empty()) return result;                          // (Ignored: no ant of the visitor's is named)
    if (intent.kind == yard::IntentKind::Attack) {
        intent.target = static_cast<uint16_t>(puppet_.foreign_ant_at(command.tile_x, command.tile_y));
        if (intent.target == 0) return result;                       // (Ignored: nobody stands there any more: an attack on nothing is not sent)
    }
    result.status = sim::CommandResult::Status::Applied;
    result.ack_ant = first;                                          // (the engine answers with the ant that is nearest: the voice is the same for every worker)
    result.needing_order = static_cast<uint32_t>(intent.ants.size());
    result.ants_ordered = result.needing_order;
    if (queue_.size() >= kMaxQueued) queue_.erase(queue_.begin());
    queue_.push_back(std::move(intent));
    return result;
}

std::vector<yard::Intent> YardSink::take() {
    std::vector<yard::Intent> out;
    out.swap(queue_);
    return out;
}

}  // namespace ants::app
