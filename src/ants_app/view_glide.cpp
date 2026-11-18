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

bool glide_offset(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy) {
    int32_t whole_x = 0;
    int32_t whole_y = 0;
    dx = 0;
    dy = 0;
    if (!glide.offset_of(ant.id, whole_x, whole_y)) return false;
    for (int32_t quarters = 4; quarters > 0; --quarters) {               // the whole slide, then three quarters of it, a half, a quarter: the most of it that does not draw the ant on covered ground
        sim::AntSnapshot drawn = ant;
        drawn.px += whole_x * quarters / 4;
        drawn.py += whole_y * quarters / 4;
        drawn.tile_x = drawn.px / 32;                                    // (the gate of the picture looks at the tile: the one that the ant is drawn on)
        drawn.tile_y = drawn.py / 32;
        if (!health::ant_drawn(world, viewer_team, drawn)) continue;
        dx = drawn.px - ant.px;
        dy = drawn.py - ant.py;
        return dx != 0 || dy != 0;
    }
    return false;
}

sim::AntSnapshot glided_ant(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant) {
    sim::AntSnapshot drawn = ant;                                        // (everything but the place reads the engine's ant: its tile too)
    int32_t dx = 0;
    int32_t dy = 0;
    if (glide_offset(glide, world, viewer_team, ant, dx, dy)) {
        drawn.px += dx;
        drawn.py += dy;
    }
    return drawn;
}

}  // namespace ants::app
