#include "ants_ai/chore_worth.hpp"

namespace ants::ai {

bool fire_in_worth_it(const BotView& view, const MapInfo& map, uint8_t victim) noexcept {
    if (victim >= sim::MAX_PLAYERS) return false;
    for (const PileView& p : view.piles()) {
        const PileInfo* info = map.pile(p.index);
        if (info == nullptr || info->approach[victim].reachable()) return true;
    }
    for (const AntView& a : view.others()) {
        if (a.team == victim && a.holding) return true;
    }
    return false;
}

}  // namespace ants::ai
