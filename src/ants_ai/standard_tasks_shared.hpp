#pragma once

// What the tasks of the standard bot share: the helpers that more than one of the standard_tasks*.cpp files uses. Only those files include it.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ants_ai/bot_view.hpp"
#include "ants_sim/grid.hpp"

namespace ants::ai::standard_detail {

constexpr uint8_t kMinFightHp = 4;            // an ant with fewer hit points than this is not sent to fight (it heals at its hill: below 2 the engine sends it there itself)

inline const AntView* find_ant(const std::vector<AntView>& ants, uint32_t id) noexcept {
    const auto it = std::lower_bound(ants.begin(), ants.end(), id, [](const AntView& a, uint32_t want) { return a.id < want; });
    return it != ants.end() && it->id == id ? &*it : nullptr;
}

// Whether an ant of any team, the seat's own included, stands on the tile (one that dies on its clip too): no special order may name such a tile (BotController::allowed: a click on an ant
// selects it or attacks it, the target cursor shows only where no ant is under the pointer)
inline bool occupied(const BotView& v, sim::TileCoord tile) noexcept {
    for (const AntView& a : v.mine()) {
        if (a.tile == tile) return true;
    }
    for (const AntView& a : v.others()) {
        if (a.tile == tile) return true;
    }
    return v.dying_at(tile);
}

inline size_t tile_index(const sim::Grid& grid, sim::TileCoord t) noexcept { return static_cast<size_t>(t.y) * static_cast<size_t>(grid.width()) + static_cast<size_t>(t.x); }

}  // namespace ants::ai::standard_detail
