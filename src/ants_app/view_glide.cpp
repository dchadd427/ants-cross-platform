#include "ants_app/view_glide.hpp"

#include "ants_app/health_changes.hpp"

#include <algorithm>
#include <cstdlib>

namespace ants::app {

int32_t ViewGlide::scaled(int32_t value, uint32_t left_ms) noexcept {
    const int64_t scaled = static_cast<int64_t>(value) * static_cast<int64_t>(left_ms);
    return static_cast<int32_t>(scaled >= 0 ? (scaled + kDurationMs / 2) / kDurationMs : -((-scaled + kDurationMs / 2) / kDurationMs));
}

void ViewGlide::add(uint32_t ant, int32_t dx, int32_t dy) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].ant != ant) continue;
        const int32_t sum_x = scaled(entries_[i].dx, entries_[i].left_ms) + dx;
        const int32_t sum_y = scaled(entries_[i].dy, entries_[i].left_ms) + dy;
        if (std::max(std::abs(sum_x), std::abs(sum_y)) > kMaxShiftPx || (sum_x == 0 && sum_y == 0)) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            entries_[i] = Entry{ant, sum_x, sum_y, kDurationMs};
        }
        return;
    }
    if (std::max(std::abs(dx), std::abs(dy)) > kMaxShiftPx || (dx == 0 && dy == 0)) return;
    entries_.push_back(Entry{ant, dx, dy, kDurationMs});
}

void ViewGlide::advance(uint32_t ms) {
    for (size_t i = 0; i < entries_.size();) {
        if (entries_[i].left_ms <= ms) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        else {
            entries_[i].left_ms -= ms;
            ++i;
        }
    }
}

bool ViewGlide::offset_of(uint32_t ant, int32_t& dx, int32_t& dy) const {
    dx = 0;
    dy = 0;
    for (const Entry& e : entries_) {
        if (e.ant != ant) continue;
        dx = scaled(e.dx, e.left_ms);
        dy = scaled(e.dy, e.left_ms);
        return dx != 0 || dy != 0;
    }
    return false;
}

sim::AntSnapshot glided_ant(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant) {
    int32_t dx = 0;
    int32_t dy = 0;
    if (!glide.offset_of(ant.id, dx, dy)) return ant;
    sim::AntSnapshot drawn = ant;
    drawn.px += dx;
    drawn.py += dy;
    drawn.tile_x = drawn.px / 32;                                    // (the gate of the picture looks at the tile: the one that the ant is drawn on)
    drawn.tile_y = drawn.py / 32;
    if (!health::ant_drawn(world, viewer_team, drawn)) return ant;
    drawn.tile_x = ant.tile_x;                                       // (everything else reads the engine's tile)
    drawn.tile_y = ant.tile_y;
    return drawn;
}

}  // namespace ants::app
