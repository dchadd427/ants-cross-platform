// Touch controls, the model (include/ants_app/touch_control.hpp): finger events in, actions out. No SDL.
#include "ants_app/touch_control.hpp"

#include <algorithm>
#include <cmath>

namespace ants::app {
namespace touch {

double slop_pixels(double picture_per_css_px, double picture_per_device_px) noexcept {
    const double css = std::isfinite(picture_per_css_px) && picture_per_css_px > 0.0 ? kSlopCssPx * picture_per_css_px : 0.0;
    const double device = std::isfinite(picture_per_device_px) && picture_per_device_px > 0.0 ? kSlopFloorDevicePx * picture_per_device_px : 0.0;
    return std::clamp(std::max(css, device), 1.0, 64.0);       // (a box that reports no size must not make everything a tap, nor a hairline a drag)
}

}  // namespace touch

namespace {

using Kind = TouchAction::Kind;

/// Milliseconds from `then` to `now` (the clock wraps after 49 days; a time that is earlier is 0)
uint32_t since(uint32_t now, uint32_t then) noexcept {
    const int32_t d = static_cast<int32_t>(now - then);
    return d < 0 ? 0u : static_cast<uint32_t>(d);
}

int32_t pixel(double v) noexcept {
    return static_cast<int32_t>(std::floor(std::clamp(v, -1.0e9, 1.0e9)));
}

int32_t rounded(double v) noexcept {
    return static_cast<int32_t>(std::lround(std::clamp(v, -1.0e9, 1.0e9)));
}

double distance(double ax, double ay, double bx, double by) noexcept {
    return std::hypot(ax - bx, ay - by);
}

bool finite(double x, double y) noexcept {
    return std::isfinite(x) && std::isfinite(y);
}

/// The level of `levels` (descending) that is nearest to `zoom` in ratio
size_t nearest_level(const std::vector<float>& levels, float zoom) noexcept {
    size_t best = 0;
    double best_d = 1.0e300;
    for (size_t i = 0; i < levels.size(); ++i) {
        const double d = std::fabs(std::log(static_cast<double>(zoom) / static_cast<double>(levels[i])));
        if (d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/// From level `index`, the level that a zoom of `ideal` asks for: a neighbour is taken only when `ideal` is more than (0.5 + kHysteresis) of the way to it, in ratio
size_t pick_level(const std::vector<float>& levels, size_t index, double ideal) noexcept {
    const double threshold = 0.5 + touch::kHysteresis;
    while (index > 0) {                                                 // more zoomed in
        const double here = static_cast<double>(levels[index]);
        const double next = static_cast<double>(levels[index - 1]);
        if (std::log(ideal / here) >= threshold * std::log(next / here)) --index;
        else break;
    }
    while (index + 1 < levels.size()) {                                 // more zoomed out
        const double here = static_cast<double>(levels[index]);
        const double next = static_cast<double>(levels[index + 1]);
        if (std::log(here / ideal) >= threshold * std::log(here / next)) ++index;
        else break;
    }
    return index;
}

}  // namespace

void TouchControl::set_slop(double picture_px) noexcept {
    slop_ = std::isfinite(picture_px) ? std::max(picture_px, 1.0) : 8.0;
}

TouchControl::Finger* TouchControl::find(int64_t touch, int64_t id) noexcept {
    for (Finger& f : fingers_) {
        if (f.touch == touch && f.id == id) return &f;
    }
    return nullptr;
}

TouchControl::Finger* TouchControl::primary() noexcept {
    for (Finger& f : fingers_) {
        if (f.role == Role::Primary) return &f;
    }
    return nullptr;
}

const TouchControl::Finger* TouchControl::primary() const noexcept {
    for (const Finger& f : fingers_) {
        if (f.role == Role::Primary) return &f;
    }
    return nullptr;
}

TouchControl::Finger* TouchControl::secondary() noexcept {
    for (Finger& f : fingers_) {
        if (f.role == Role::Secondary) return &f;
    }
    return nullptr;
}

size_t TouchControl::ignored() const noexcept {
    size_t n = 0;
    for (const Finger& f : fingers_) n += f.role == Role::Ignored ? 1u : 0u;
    return n;
}

bool TouchControl::primary_point(double& x, double& y) const noexcept {
    const Finger* first = primary();
    if (first == nullptr) return false;
    x = first->x;
    y = first->y;
    return true;
}

void TouchControl::emit(Actions& out, Kind kind, double x, double y, uint32_t at, bool late) const {
    TouchAction a;
    a.kind = kind;
    a.x = pixel(x);
    a.y = pixel(y);
    a.at = at;
    a.late = late;
    out.push_back(a);
}

// The clock: a hold that is due fires at its own time, before whatever the call does. An event's stamp is when SDL saw it, which a stall makes late, so while a finger is tracked an event
// is judged no later than kStallMs after the last moment the model knew what the finger did (known_: a frame, or the first finger's arrival). A frame (update) is that knowledge itself: the
// finger is down now, so a hold that is due fires from it; from an event it fires only when the event, so judged, is past the hold's time.
void TouchControl::advance(uint32_t now_ms, bool frame, Actions& out) {
    if (!frame && !fingers_.empty()) {
        const uint32_t limit = known_ + touch::kStallMs;
        if (static_cast<int32_t>(now_ms - limit) > 0) now_ms = limit;
    }
    if (clock_set_ && static_cast<int32_t>(now_ms - clock_) < 0) now_ms = clock_;
    clock_ = now_ms;
    clock_set_ = true;
    if (frame) known_ = clock_;
    if (!fingers_.empty() && (mode_ == Mode::Waiting || mode_ == Mode::MinimapWait) && since(clock_, down_at_) >= touch::kHoldMs) fire_hold(out);
}

// The pair is judged: the pan and the zoom of everything that moved since the last time
void TouchControl::flush_pair(Actions& out) {
    if (mode_ != Mode::Two || !pair_moved_) return;
    pair_moved_ = false;
    move_two(clock_, out);
}

// The hold's time is up: a finger on the map view presses the right button where it went down; on the minimap it first lets go of the left one
void TouchControl::fire_hold(Actions& out) {
    const uint32_t at = down_at_ + touch::kHoldMs;
    if (mode_ == Mode::MinimapWait) emit(out, Kind::LeftUp, down_x_, down_y_, at);
    emit(out, Kind::Motion, down_x_, down_y_, at, mode_ == Mode::Waiting);
    emit(out, Kind::RightDown, down_x_, down_y_, at);
    emit(out, Kind::HoldFired, down_x_, down_y_, at);
    mode_ = Mode::Right;
    pulse_set_ = true;
    pulse_x_ = down_x_;
    pulse_y_ = down_y_;
    pulse_at_ = at;
    ++stats_.holds;
}

// The first finger: on the map view it waits; anywhere else the left button goes down now
void TouchControl::start(const Finger& finger, uint32_t now_ms, Actions& out) {
    down_x_ = finger.x;
    down_y_ = finger.y;
    down_at_ = now_ms;
    gesture_slop_ = slop_;
    const TouchZone zone = env_->zone_at(finger.x, finger.y);
    on_map_ = zone == TouchZone::Map;
    if (on_map_) {
        mode_ = Mode::Waiting;
        return;
    }
    emit(out, Kind::Motion, finger.x, finger.y, now_ms);
    emit(out, Kind::LeftDown, finger.x, finger.y, now_ms);
    mode_ = zone == TouchZone::Minimap ? Mode::MinimapWait : Mode::Left;
}

// The second finger has landed where two are allowed: what the first one held is dropped, and the pan and the pinch begin from where the fingers are
void TouchControl::begin_two(Finger& second, uint32_t now_ms, Actions& out) {
    const Finger* first = primary();
    if (first == nullptr) return;
    if (mode_ == Mode::Left || mode_ == Mode::Right) emit(out, Kind::Cancel, first->x, first->y, now_ms);
    second.role = Role::Secondary;
    mode_ = Mode::Two;
    pair_moved_ = false;
    mid_x0_ = (first->x + second.x) / 2.0;
    mid_y0_ = (first->y + second.y) / 2.0;
    panned_x_ = 0;
    panned_y_ = 0;
    span0_ = std::max(distance(first->x, first->y, second.x, second.y), touch::kMinSpanSlops * gesture_slop_);
    zoom0_ = env_->zoom();
    levels_ = env_->zoom_levels();
    level_index_ = levels_.empty() ? 0u : nearest_level(levels_, zoom0_);
    ++stats_.two_finger;
}

// A finger of the pair moved: the map follows the middle point (whole pixels, the rest kept for the next move), then the level follows the distance, anchored at the middle point
void TouchControl::move_two(uint32_t now_ms, Actions& out) {
    const Finger* first = primary();
    const Finger* second = secondary();
    if (first == nullptr || second == nullptr) return;
    const double mx = (first->x + second->x) / 2.0;
    const double my = (first->y + second->y) / 2.0;
    const int32_t want_x = rounded(mx - mid_x0_);
    const int32_t want_y = rounded(my - mid_y0_);
    if (want_x != panned_x_ || want_y != panned_y_) {
        TouchAction pan;
        pan.kind = Kind::Pan;
        pan.dx = want_x - panned_x_;
        pan.dy = want_y - panned_y_;
        pan.x = pixel(mx);
        pan.y = pixel(my);
        pan.at = now_ms;
        out.push_back(pan);
        panned_x_ = want_x;
        panned_y_ = want_y;
    }
    if (levels_.empty()) return;
    const double ideal = static_cast<double>(zoom0_) * distance(first->x, first->y, second->x, second->y) / span0_;
    if (!(ideal > 0.0) || !std::isfinite(ideal)) return;
    const size_t index = pick_level(levels_, level_index_, ideal);
    if (index == level_index_) return;
    level_index_ = index;
    TouchAction zoom;
    zoom.kind = Kind::Zoom;
    zoom.level = levels_[index];
    zoom.x = pixel(mx);
    zoom.y = pixel(my);
    zoom.at = now_ms;
    out.push_back(zoom);
}

TouchControl::Actions TouchControl::finger_down(int64_t touch, int64_t finger, double x, double y, uint32_t now_ms) {
    Actions out;
    if (!finite(x, y)) return out;
    advance(now_ms, false, out);
    flush_pair(out);                                                    // (what the pair did so far counts before the structure changes)
    if (find(touch, finger) != nullptr) {                               // down again: its lift was lost, so nothing that was held can be trusted
        const Actions ended = cancel();
        out.insert(out.end(), ended.begin(), ended.end());
        advance(now_ms, false, out);                                    // (the new finger is a new gesture, nothing is tracked that could hold its time back: it is at its stamp)
    }
    Finger arrived;
    arrived.touch = touch;
    arrived.id = finger;
    arrived.x = x;
    arrived.y = y;
    if (fingers_.empty()) {
        arrived.role = Role::Primary;
        fingers_.push_back(arrived);
        known_ = clock_;                                                // (the first finger's arrival is a moment that the model knows)
        start(fingers_.back(), clock_, out);
        return out;
    }
    const Finger* first = primary();
    const bool can_pair = first != nullptr && on_map_ && (mode_ == Mode::Waiting || mode_ == Mode::Left || mode_ == Mode::Right);       // (in a pair the mode is Two: a third finger is none of these)
    if (can_pair && env_->two_fingers_allowed((first->x + x) / 2.0, (first->y + y) / 2.0)) {
        fingers_.push_back(arrived);
        begin_two(fingers_.back(), clock_, out);
        return out;
    }
    arrived.role = Role::Ignored;                                       // a third finger, or a second one where two do nothing
    fingers_.push_back(arrived);
    return out;
}

TouchControl::Actions TouchControl::finger_motion(int64_t touch, int64_t finger, double x, double y, uint32_t now_ms) {
    Actions out;
    if (!finite(x, y)) return out;
    advance(now_ms, false, out);
    Finger* f = find(touch, finger);
    if (f == nullptr) return out;
    f->x = x;
    f->y = y;
    if (f->role == Role::Ignored) return out;
    if (mode_ == Mode::Two) {                                           // (judged by update(), once for every finger that moved in the frame)
        pair_moved_ = true;
        return out;
    }
    const bool left_slop = distance(x, y, down_x_, down_y_) > gesture_slop_;
    switch (mode_) {
        case Mode::Waiting:
            if (left_slop) {                                            // a drag: the press where the finger went down, then the finger
                emit(out, Kind::Motion, down_x_, down_y_, clock_, true);
                emit(out, Kind::LeftDown, down_x_, down_y_, clock_);
                emit(out, Kind::Motion, x, y, clock_);
                mode_ = Mode::Left;
                ++stats_.drags;
            }
            break;
        case Mode::MinimapWait:
            emit(out, Kind::Motion, x, y, clock_);
            if (left_slop) mode_ = Mode::Left;                          // it moves: a scroll, never a hold
            break;
        case Mode::Left:
        case Mode::Right:
            emit(out, Kind::Motion, x, y, clock_);
            break;
        default:
            break;
    }
    return out;
}

TouchControl::Actions TouchControl::finger_up(int64_t touch, int64_t finger, double x, double y, uint32_t now_ms) {
    Actions out;
    advance(now_ms, false, out);
    flush_pair(out);                                                    // (the last moves of the pair count before a finger leaves it)
    Finger* f = find(touch, finger);
    if (f == nullptr) return out;
    if (finite(x, y)) {                                                 // (a lift that has no usable position is where the finger last was)
        f->x = x;
        f->y = y;
    }
    const Role role = f->role;
    const double lx = f->x;
    const double ly = f->y;
    const auto forget = [this, touch, finger]() {
        fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(), [touch, finger](const Finger& g) { return g.touch == touch && g.id == finger; }), fingers_.end());
    };
    if (role == Role::Ignored) {
        forget();
        return out;
    }
    if (mode_ == Mode::Two) {                                           // the pair is over; the finger that stays is ignored until it lifts
        for (Finger& g : fingers_) g.role = Role::Ignored;
        forget();
        mode_ = Mode::Idle;
        return out;
    }
    const uint32_t at = clock_;
    switch (mode_) {
        case Mode::Waiting:
            if (distance(lx, ly, down_x_, down_y_) > gesture_slop_) {   // it left the slop in a move that was not heard of: a drag that ends at once
                emit(out, Kind::Motion, down_x_, down_y_, at, true);
                emit(out, Kind::LeftDown, down_x_, down_y_, at);
                emit(out, Kind::LeftUp, lx, ly, at);
                ++stats_.drags;
            } else if (since(at, down_at_) < touch::kTapMs) {           // a tap: both halves of the click at the point where the finger went down
                emit(out, Kind::Motion, down_x_, down_y_, at, true);
                emit(out, Kind::LeftDown, down_x_, down_y_, at);
                emit(out, Kind::LeftUp, down_x_, down_y_, at);
                ++stats_.taps;
            }                                                           // else it lingered: nothing
            break;
        case Mode::Left:
        case Mode::MinimapWait:
            emit(out, Kind::LeftUp, lx, ly, at);
            break;
        case Mode::Right:
            emit(out, Kind::RightUp, lx, ly, at);
            break;
        default:
            break;
    }
    forget();
    mode_ = Mode::Idle;
    return out;
}

TouchControl::Actions TouchControl::update(uint32_t now_ms) {
    Actions out;
    advance(now_ms, true, out);
    flush_pair(out);
    return out;
}

TouchControl::Actions TouchControl::cancel() {
    Actions out;
    if (fingers_.empty()) {
        mode_ = Mode::Idle;
        return out;
    }
    if (mode_ == Mode::Left || mode_ == Mode::Right || mode_ == Mode::MinimapWait) {
        if (const Finger* first = primary()) emit(out, Kind::Cancel, first->x, first->y, clock_);
    }
    fingers_.clear();
    mode_ = Mode::Idle;
    pair_moved_ = false;
    ++stats_.cancels;
    return out;
}

std::optional<TouchControl::Mark> TouchControl::ring(uint32_t now_ms) const noexcept {
    if (fingers_.empty() || (mode_ != Mode::Waiting && mode_ != Mode::MinimapWait)) return std::nullopt;
    const uint32_t held = since(now_ms, down_at_);
    if (held < touch::kRingStartMs || held >= touch::kHoldMs) return std::nullopt;
    return Mark{down_x_, down_y_, static_cast<double>(held - touch::kRingStartMs) / static_cast<double>(touch::kHoldMs - touch::kRingStartMs)};
}

std::optional<TouchControl::Mark> TouchControl::pulse(uint32_t now_ms) const noexcept {
    if (!pulse_set_) return std::nullopt;
    const uint32_t age = since(now_ms, pulse_at_);
    if (age >= touch::kPulseMs) return std::nullopt;
    return Mark{pulse_x_, pulse_y_, static_cast<double>(age) / static_cast<double>(touch::kPulseMs)};
}

}  // namespace ants::app
