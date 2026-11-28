#include "ants_app/view_glide.hpp"

#include "ants_app/health_changes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ants::app {

int32_t ViewGlide::scaled(int32_t value, uint32_t left_ms, uint32_t total_ms) noexcept {
    const int64_t scaled = static_cast<int64_t>(value) * static_cast<int64_t>(left_ms);
    const int64_t half = static_cast<int64_t>(total_ms) / 2;
    return static_cast<int32_t>(scaled >= 0 ? (scaled + half) / total_ms : -((-scaled + half) / total_ms));
}

uint32_t ViewGlide::duration_ms(int32_t dx, int32_t dy) noexcept {
    const double length = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
    const double ms = std::ceil(length * 1000.0 / static_cast<double>(kWalkPxPerSecond));
    return static_cast<uint32_t>(std::clamp(ms, static_cast<double>(kMinDurationMs), static_cast<double>(kMaxDurationMs)));
}

void ViewGlide::add(uint32_t ant, int32_t dx, int32_t dy) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].ant != ant) continue;
        const int32_t sum_x = scaled(entries_[i].dx, entries_[i].left_ms, entries_[i].total_ms) + dx;
        const int32_t sum_y = scaled(entries_[i].dy, entries_[i].left_ms, entries_[i].total_ms) + dy;
        if (std::max(std::abs(sum_x), std::abs(sum_y)) > kMaxShiftPx || (sum_x == 0 && sum_y == 0)) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            const uint32_t total = duration_ms(sum_x, sum_y);
            entries_[i] = Entry{ant, sum_x, sum_y, total, total};
        }
        return;
    }
    if (std::max(std::abs(dx), std::abs(dy)) > kMaxShiftPx || (dx == 0 && dy == 0)) return;
    const uint32_t total = duration_ms(dx, dy);
    entries_.push_back(Entry{ant, dx, dy, total, total});
}

void ViewGlide::advance(uint32_t ms) {
    since_follow_ms_ = static_cast<uint32_t>(std::min<uint64_t>(60000u, static_cast<uint64_t>(since_follow_ms_) + ms));       // (a look that is a minute away is as far as one that is more)
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
        dx = scaled(e.dx, e.left_ms, e.total_ms);
        dy = scaled(e.dy, e.left_ms, e.total_ms);
        return dx != 0 || dy != 0;
    }
    return false;
}

namespace {

bool on_tile_middle(int32_t v) noexcept { return v >= 0 && v % ViewGlide::kTilePx == ViewGlide::kTilePx / 2; }

}  // namespace

void ViewGlide::follow(const sim::WorldState& world, uint8_t viewer_team, const ClipShift& clip) {
    // The time between the two looks: the frames' own, or more of the match when a replay runs at speed or a catch-up runs ticks in one go (the match clock moves by 50 ms a tick, and a tick's step is the one
    // that kStepPx counts, so only what is more than one tick is a walk of its own)
    const uint32_t match_ms = have_match_ms_ && match_ms_ > world.match_time_remaining_ms ? match_ms_ - world.match_time_remaining_ms : 0u;
    const uint32_t elapsed = std::max(since_follow_ms_, match_ms > kTickMs ? match_ms - kTickMs : 0u);
    since_follow_ms_ = 0;
    match_ms_ = world.match_time_remaining_ms;
    have_match_ms_ = true;
    // What a walk of the time between the two looks, and one more step of its clip, can move the drawn ant by: more is no walk
    const int64_t walk_px = (static_cast<int64_t>(kWalkPxPerSecond) * elapsed + 999) / 1000 + kStepPx;
    looking_.clear();
    looking_.reserve(world.ants.size());
    for (const sim::AntSnapshot& ant : world.ants) {
        if (!health::ant_drawn(world, viewer_team, ant)) continue;       // (an ant of the fog is not looked at: when it is drawn again it has no last place)
        int32_t clip_x = 0;
        int32_t clip_y = 0;
        if (clip) clip(ant, clip_x, clip_y);
        const auto drawn = [&](int32_t& x, int32_t& y) {
            int32_t gx = 0;
            int32_t gy = 0;
            glide_offset(*this, world, viewer_team, ant, gx, gy);
            x = ant.px + gx + clip_x;
            y = ant.py + gy + clip_y;
        };
        Seen now{ant.id, ant.px, ant.py, ant.action, 0, 0};
        drawn(now.drawn_x, now.drawn_y);
        const auto last = std::lower_bound(seen_.begin(), seen_.end(), ant.id, [](const Seen& s, uint32_t id) { return s.ant < id; });
        if (last != seen_.end() && last->ant == ant.id && last->action == 1 && ant.action != 1 && on_tile_middle(ant.px) && on_tile_middle(ant.py)) {
            const int32_t hop_x = last->drawn_x - now.drawn_x;           // (the way back to where the last frame drew it)
            const int32_t hop_y = last->drawn_y - now.drawn_y;
            if (std::max(std::abs(hop_x), std::abs(hop_y)) > walk_px) {
                add(ant.id, hop_x, hop_y);
                drawn(now.drawn_x, now.drawn_y);
            }
        }
        looking_.push_back(now);
    }
    if (!std::is_sorted(looking_.begin(), looking_.end(), [](const Seen& a, const Seen& b) { return a.ant < b.ant; })) {
        std::sort(looking_.begin(), looking_.end(), [](const Seen& a, const Seen& b) { return a.ant < b.ant; });
    }
    seen_.swap(looking_);
}

bool glide_offset(const ViewGlide& glide, const sim::WorldState& world, uint8_t viewer_team, const sim::AntSnapshot& ant, int32_t& dx, int32_t& dy) {
    int32_t whole_x = 0;
    int32_t whole_y = 0;
    dx = 0;
    dy = 0;
    if (!glide.offset_of(ant.id, whole_x, whole_y)) return false;
    if (!health::ant_drawn(world, viewer_team, ant)) return false;       // (an ant that the picture does not draw where the engine has it is drawn nowhere, and nothing picks it: its slide may have begun on explored ground)
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
