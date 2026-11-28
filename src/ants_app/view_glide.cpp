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
        Entry& e = entries_[i];
        if (e.hold) {                                                    // a held ant does not slide: it ages, and when it is too old it is carried to the engine's place
            e.age_ms = static_cast<uint32_t>(std::min<uint64_t>(60000u, static_cast<uint64_t>(e.age_ms) + ms));
            if (e.age_ms >= kHoldMaxMs) end_hold(e);
            if (e.hold || (e.dx != 0 || e.dy != 0)) {
                ++i;
                continue;
            }
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (e.left_ms <= ms) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        else {
            e.left_ms -= ms;
            ++i;
        }
    }
}

ViewGlide::Entry* ViewGlide::hold_of(uint32_t ant) {
    for (Entry& e : entries_) {
        if (e.ant == ant && e.hold) return &e;
    }
    return nullptr;
}

bool ViewGlide::held(uint32_t ant) const {
    for (const Entry& e : entries_) {
        if (e.ant == ant && e.hold) return true;
    }
    return false;
}

void ViewGlide::end_hold(Entry& e) {
    e.hold = false;
    if (e.dx == 0 && e.dy == 0) return;
    if (std::max(std::abs(e.dx), std::abs(e.dy)) > kMaxShiftPx) {
        e.dx = 0;
        e.dy = 0;
        return;
    }
    e.total_ms = duration_ms(e.dx, e.dy);
    e.left_ms = e.total_ms;
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

// The way an ant faces (AntSnapshot::facing: 0 north, then clockwise by eighths, 2 east, 4 south, 6 west) as a step on the screen, each axis -1, 0 or 1. False for a value that is no direction.
bool heading_of(uint8_t facing, int32_t& hx, int32_t& hy) noexcept {
    static const int8_t kX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t kY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    if (facing > 7) return false;
    hx = kX[facing];
    hy = kY[facing];
    return true;
}

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
        if (Entry* hold = hold_of(ant.id)) {
            // A held ant: the engine's ant stands for its first ticks and then walks again, now on the heading of the new order. The hold goes on while the place where the ant is drawn is still AHEAD of
            // the engine's ant in that direction (the engine's ant has not passed it); when the engine's ant has passed it, or stands (a Stop), the ant is carried to the engine's place by the slide.
            const int32_t engine_x = ant.px + clip_x;
            const int32_t engine_y = ant.py + clip_y;
            hold->dx = hold->held_x - engine_x;
            hold->dy = hold->held_y - engine_y;
            int32_t head_x = 0;
            int32_t head_y = 0;
            bool end = false;
            if (ant.action == 1 && heading_of(ant.facing, head_x, head_y)) end = hold->dx * head_x + hold->dy * head_y <= 0;
            else if (ant.action != 1 && ant.state == sim::UnitState::Idle) end = true;
            if (end) end_hold(*hold);
            else {
                hold->left_ms = hold->total_ms = kHoldMaxMs;
            }
        }
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
            int32_t old_x = 0;
            int32_t old_y = 0;
            const bool back = heading_of(ant.facing, old_x, old_y) && hop_x * old_x + hop_y * old_y > 0;     // (the engine has not turned the ant yet: this is the way that it WAS walking)
            // A walk can only carry an ant on, so a move back along the way that it walked is no walk, whatever the time between the looks: when the ant has stopped walking but is still in the walking state (the
            // engine's three ticks of standing before the new walk or the stop, which the snap of an order starts), any move back is the snap and the clip's frames that the picture had added. An ant that has
            // ARRIVED is idle in the very tick that it stops walking, and the move back that its arrival shows (the clip's frames: up to a step) is no snap. A move forward is a snap only beyond what the walk of
            // that time covers.
            const bool snapped_back = back && ant.state != sim::UnitState::Idle;
            if (std::max(std::abs(hop_x), std::abs(hop_y)) > (snapped_back ? int64_t{0} : walk_px)) {
                const int32_t held_dx = last->drawn_x - (ant.px + clip_x);      // (where the last frame drew it, less where the engine's ant is drawn: the slide that was left of an earlier hop is over)
                const int32_t held_dy = last->drawn_y - (ant.py + clip_y);
                if (snapped_back && std::max(std::abs(held_dx), std::abs(held_dy)) <= kMaxShiftPx && !hold_of(ant.id)) {
                    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.ant == ant.id; }), entries_.end());
                    Entry held;
                    held.ant = ant.id;
                    held.dx = held_dx;
                    held.dy = held_dy;
                    held.left_ms = held.total_ms = kHoldMaxMs;
                    held.hold = true;
                    held.held_x = last->drawn_x;
                    held.held_y = last->drawn_y;
                    entries_.push_back(held);
                } else {
                    add(ant.id, hop_x, hop_y);
                }
                drawn(now.drawn_x, now.drawn_y);
            }
        }
        looking_.push_back(now);
    }
    for (size_t i = 0; i < entries_.size();) {                           // (a hold of an ant that is not looked at any more, gone or hidden by the fog, is over)
        const uint32_t id = entries_[i].ant;
        const bool looked = std::any_of(looking_.begin(), looking_.end(), [id](const Seen& s) { return s.ant == id; });
        const bool empty = !entries_[i].hold && entries_[i].dx == 0 && entries_[i].dy == 0;       // (a hold that ended with the ant where the engine has it: nothing is left to slide)
        if ((entries_[i].hold && !looked) || empty) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
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
