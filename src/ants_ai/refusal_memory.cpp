#include "ants_ai/refusal_memory.hpp"

#include <algorithm>

#include "standard_tasks_shared.hpp"

namespace ants::ai {

using standard_detail::find_ant;

namespace {

bool refusable(sim::CommandType t) noexcept {
    return t == sim::CommandType::GroupMove || t == sim::CommandType::GroupSpecial || t == sim::CommandType::GroupAttack;
}

}  // namespace

void RefusalMemory::sent(const sim::Command& command, uint64_t tick) {
    if (!refusable(command.type)) return;
    for (const uint32_t id : command.ants) {
        const auto at = seen_.find(id);
        if (at == seen_.end() || at->second.loaded) continue;                                 // (an ant the last look did not see: nothing to judge by; a carrier queues)
        if (at->second.tile.chebyshev_dist(sim::TileCoord{command.tile_x, command.tile_y}) <= 1) continue;      // (a click on or beside the ant's own tile asks for no walk: standing is no refusal)
        bool in_mound = false;
        for (const sim::TileCoord& h : hills_) in_mound = in_mound || (command.tile_x >= h.x - 1 && command.tile_x <= h.x + 3 && command.tile_y >= h.y - 1 && command.tile_y <= h.y + 3);
        if (in_mound) continue;
        watching_.push_back(Watch{key_of(id, command), tick, at->second.tile});
    }
}

void RefusalMemory::look(const BotView& view) {
    const uint64_t now = view.tick();
    for (auto it = watching_.begin(); it != watching_.end();) {
        const AntView* ant = find_ant(view.mine(), it->key.ant);
        if (ant == nullptr || now >= it->sent + params_.watch_ticks) {
            it = watching_.erase(it);                                                          // dead, or it has walked about long enough to have obeyed
            continue;
        }
        if (now < it->sent + params_.settle_ticks) {
            ++it;
            continue;
        }
        const bool still = ant->tile == it->from;
        const bool standing = ant->state == sim::UnitState::Idle || ant->state == sim::UnitState::GuardIdle || ant->state == sim::UnitState::CantGo || ant->state == sim::UnitState::Swimming;
        if (still && standing && !view.has_pending_path(ant->id)) {
            Record& r = records_[it->key];
            if (r.count != 0 && now >= r.last + params_.window_ticks) r = Record{};            // (the old refusals are forgotten)
            ++r.count;
            r.last = now;
            if (r.count >= 2) {
                uint64_t block = params_.first_block_ticks;
                for (uint32_t k = 2; k < r.count && block < params_.max_block_ticks; ++k) block *= 2u;
                r.until = now + std::min<uint64_t>(block, params_.max_block_ticks);
            }
            ++refusals_;
            it = watching_.erase(it);
        } else if (still && !standing) {
            ++it;                                                                              // (walking in place, or at work: judged at a later look, or not at all)
        } else {
            it = watching_.erase(it);                                                          // it moved: it obeyed
        }
    }
    seen_.clear();
    for (const AntView& a : view.mine()) seen_[a.id] = Seen{a.tile, a.holding || a.carried_points > 0};
    if (hills_.empty() && view.has_grid()) {
        for (const auto& ah : view.grid().anthills()) hills_.push_back(sim::TileCoord{static_cast<int32_t>(ah.x), static_cast<int32_t>(ah.y)});
    }
    if (records_.size() > 512) {                                                               // (the forgotten are dropped)
        for (auto it = records_.begin(); it != records_.end();) it = now >= it->second.last + params_.window_ticks && now >= it->second.until ? records_.erase(it) : std::next(it);
    }
}

bool RefusalMemory::is_blocked(uint32_t ant, sim::CommandType type, sim::TileCoord tile, uint64_t tick) const noexcept {
    const auto it = records_.find(Key{ant, static_cast<uint8_t>(type), static_cast<int16_t>(tile.x), static_cast<int16_t>(tile.y)});
    return it != records_.end() && it->second.until > tick;
}

std::vector<sim::Command> RefusalMemory::filter(const BotView& view, Orders& orders) {
    std::vector<sim::Command> out;
    const uint64_t now = view.tick();
    std::vector<Intent>& intents = orders.edit();
    for (auto it = intents.begin(); it != intents.end();) {
        sim::Command& c = it->command;
        if (!refusable(c.type) || it->chain != 0 || c.ants.empty()) {
            ++it;
            continue;
        }
        sim::Command taken = c;
        taken.ants.clear();
        std::vector<uint32_t> keep;
        for (const uint32_t id : c.ants) {
            if (is_blocked(id, c.type, sim::TileCoord{c.tile_x, c.tile_y}, now)) {
                taken.ants.push_back(id);
                ++blocked_;
            } else {
                keep.push_back(id);
            }
        }
        if (taken.ants.empty()) {
            ++it;
            continue;
        }
        out.push_back(taken);
        if (keep.empty()) {
            it = intents.erase(it);
        } else {
            c.ants = keep;
            ++it;
        }
    }
    return out;
}

}  // namespace ants::ai
