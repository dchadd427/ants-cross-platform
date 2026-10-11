// The Auto camera's model (auto_camera.hpp, docs/REPLAYS.md "Camera"): which moments of a match are worth watching, where the busiest place is, when the view moves there and when it stays.
#include "ants_app/auto_camera.hpp"

#include <algorithm>
#include <cmath>

namespace ants::app {

namespace {

/// What a cue of the engine means for the camera: how much it weighs, whether it is a big moment (the only kind that counts at 4x and 8x), and where it happens. `at_hill` says the cue has no
/// place of its own (the engine gives such cues to the player they are about, `target_player`) and means that player's hill.
struct CueWeight {
    float weight;
    bool big;
    bool at_hill;
};

bool cue_weight(uint32_t sound, CueWeight& out) {
    namespace S = sim::SoundID;
    switch (sound) {
        case S::MeleeAttack:
        case S::AttackAlt:
        case S::HeavyPunch:
        case S::WaterAttack:
        case S::ThiefWhip:
            out = {AutoCamera::kHit, false, false};                                     // an ant hits another (the blow of its clip, broadcast, at the ant: the cue of every seat)
            return true;
        case S::ThiefDive:
            out = {AutoCamera::kTheft, true, false};                                    // a Thief dives into a hill
            return true;
        case S::ThiefEmerge:
            out = {4.0f, false, false};                                                 // ... and comes out with the points
            return true;
        case S::BaseScoreDn:
            out = {6.0f, true, false};                                                  // a hill's score drops (at that hill)
            return true;
        case S::Anthill:
            out = {4.0f, false, true};                                                  // the raided player's cue: a Thief is at the hill
            return true;
        case S::FireBeam:
        case S::FireErupt:
            out = {AutoCamera::kFire, true, false};                                     // a Fire ant lights a fire
            return true;
        case S::FireBurnout:
            out = {3.0f, false, false};
            return true;
        case S::BombDetonate:
            out = {AutoCamera::kBomb, true, false};
            return true;
        case S::BombDefuseGrab:
            out = {6.0f, true, false};                                                  // a bomb is picked up to be defused
            return true;
        case S::BombPick:
            out = {3.0f, false, false};
            return true;
        case S::BaseAlarmSiren:
            out = {3.0f, false, true};                                                  // a base is under attack
            return true;
        case S::AntDrown:
            out = {3.0f, false, false};                                                 // an ant falls in the water
            return true;
        case S::PowerUpDrop:
            out = {2.0f, false, false};
            return true;
        case S::ShovelGravel:
        case S::ShovelWater:
            out = {2.0f, false, false};                                                 // a bridge goes up
            return true;
        default:
            return false;
    }
}

/// Is (ax, ay) within `radius` of (bx, by)? (squared: the camera asks it for every pair of moments, every frame)
bool within(double ax, double ay, double bx, double by, double radius) {
    const double dx = ax - bx;
    const double dy = ay - by;
    return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

void AutoCamera::set_mode(CameraMode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    if (mode_ == CameraMode::Auto) return_pending_ = true;
    else return_pending_ = false;
}

bool AutoCamera::take() {
    if (mode_ == CameraMode::Manual) return false;
    mode_ = CameraMode::Manual;
    return_pending_ = false;
    glide_ms_ = kGlideMs;                                                              // (a move in progress stops where the viewer's hand put the view)
    return true;
}

void AutoCamera::reset() {
    moments_.clear();
    seen_.clear();
    have_seen_ = false;
    has_target_ = false;
    has_last_ = false;
    deaths_ = 0;
    hold_until_ms_ = 0.0;
    glide_ms_ = kGlideMs;
    return_pending_ = false;
}

void AutoCamera::set_hills(const std::vector<Point>& centres) { hills_ = centres; }

void AutoCamera::add(double x, double y, float weight, bool big) {
    if (speed_ >= kBigOnlySpeed && !big) return;
    moments_.push_back(Moment{x, y, weight, now_ms_});
    last_action_ = Point{x, y};
    has_last_ = true;
    constexpr size_t kMost = 256;                                                      // (a bound for the work of a frame: the oldest go first)
    if (moments_.size() > kMost) moments_.erase(moments_.begin(), moments_.begin() + static_cast<std::ptrdiff_t>(moments_.size() - kMost));
}

void AutoCamera::observe_cue(const sim::AudioEvent& cue) {
    if (cue.stop) return;
    CueWeight w{};
    if (!cue_weight(cue.sound_id, w)) return;
    if (w.at_hill) {
        if (cue.target_player >= hills_.size()) return;
        add(hills_[cue.target_player].x, hills_[cue.target_player].y, w.weight, w.big);
        return;
    }
    add(static_cast<double>(cue.world_x), static_cast<double>(cue.world_y), w.weight, w.big);
}

void AutoCamera::observe_world(const sim::WorldState& world) {
    next_.clear();
    next_.reserve(world.ants.size());
    for (const sim::AntSnapshot& ant : world.ants) next_[ant.id] = Seen{ant.hp, ant.px, ant.py, ant.is_drowning};
    if (have_seen_) {
        for (const auto& [id, before] : seen_) {
            const auto now = next_.find(id);
            if (now == next_.end()) {
                if (!before.drowning) {
                    add(before.x, before.y, kDeath, true);
                    ++deaths_;
                }           // an ant that is gone and did not drown died (a drowning one has its cue)
            }
        }
    }
    seen_.swap(next_);
    have_seen_ = true;
}

double AutoCamera::influence(const Moment& m) const noexcept {
    const double age = now_ms_ - m.at_ms;
    if (age >= kWindowMs) return 0.0;
    return static_cast<double>(m.weight) * (1.0 - age / kWindowMs);
}

double AutoCamera::score_at(const Point& p) const noexcept {
    double sum = 0.0;
    for (const Moment& m : moments_) {
        if (within(m.x, m.y, p.x, p.y, kRadius)) sum += influence(m);
    }
    return sum;
}

bool AutoCamera::best_spot(Spot& out) const {
    double best = 0.0;
    const Moment* at = nullptr;
    for (const Moment& m : moments_) {
        const double s = score_at(Point{m.x, m.y});
        if (s > best) {
            best = s;
            at = &m;
        }
    }
    if (at == nullptr || best < kMinScore) return false;
    double sx = 0.0;
    double sy = 0.0;
    double sw = 0.0;
    for (const Moment& m : moments_) {
        if (!within(m.x, m.y, at->x, at->y, kRadius)) continue;
        const double w = influence(m);
        sx += m.x * w;
        sy += m.y * w;
        sw += w;
    }
    out.at = sw > 0.0 ? Point{sx / sw, sy / sw} : Point{at->x, at->y};
    out.score = best;
    return true;
}

bool AutoCamera::hot_spot(Point& out) const {
    Spot spot;
    if (!best_spot(spot)) return false;
    out = spot.at;
    return true;
}

bool AutoCamera::target(Point& out) const {
    if (!has_target_) return false;
    out = target_;
    return true;
}

void AutoCamera::go_to(const Point& to, const Point& from) {
    target_ = to;
    from_ = from;
    has_target_ = true;
    glide_ms_ = 0.0;
    hold_until_ms_ = now_ms_ + kGlideMs + kHoldMs;                                     // (the hold counts from the arrival)
}

bool AutoCamera::step(double dt_ms, const Point& cam, const Point& view, bool zoomed, Point& out) {
    now_ms_ += dt_ms;
    moments_.erase(std::remove_if(moments_.begin(), moments_.end(), [&](const Moment& m) { return now_ms_ - m.at_ms >= kWindowMs; }), moments_.end());
    if (mode_ != CameraMode::Auto) return false;

    if (return_pending_) {                                                             // the button: back to the action, from wherever the viewer left the view
        return_pending_ = false;
        Spot spot;
        if (has_last_) go_to(last_action_, cam);
        else if (best_spot(spot)) go_to(spot.at, cam);
    } else if (!holding()) {
        Spot spot;
        if (best_spot(spot)) {
            const double here = has_target_ ? score_at(target_) : 0.0;
            const bool inside = std::fabs(spot.at.x - cam.x) <= kStayInside * view.x && std::fabs(spot.at.y - cam.y) <= kStayInside * view.y;
            if (!inside && spot.score > here * kClearlyBusier) go_to(spot.at, cam);
        }
    }
    if (!has_target_) return false;

    if (glide_ms_ < kGlideMs) {
        glide_ms_ = std::min(kGlideMs, glide_ms_ + dt_ms);
        const double t = glide_ms_ / kGlideMs;
        const double s = t * t * (3.0 - 2.0 * t);                                      // (eases in and out)
        out = Point{from_.x + (target_.x - from_.x) * s, from_.y + (target_.y - from_.y) * s};
        return true;
    }
    if (zoomed) {                                                                      // a zoom keeps Auto: the view is put back at the place, at the viewer's zoom
        out = target_;
        return true;
    }
    return false;
}

}  // namespace ants::app
