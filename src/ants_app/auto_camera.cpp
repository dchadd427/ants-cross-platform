// The Auto camera's model (auto_camera.hpp, docs/REPLAYS.md "The camera"): which events of a match are action, which place has the most of it, whether the camera stays or moves, and how the view
// gets there.
#include "ants_app/auto_camera.hpp"

#include <algorithm>
#include <cmath>

namespace ants::app {

namespace {

constexpr size_t kMostEvents = 400;          // a bound for the work of a look: the oldest events go first

/// What a cue of the engine means for the camera: how much it weighs, whether it is big (the only kind that counts at 4x and 8x), whether it is fighting, whether it belongs to the ant that
/// the cue names (the owner), and `at_hill` that it has no place of its own (the engine gives such cues to the player they are about, `target_player`) and means that player's hill.
struct CueWeight {
    float weight;
    bool big;
    bool fight;
    bool ant;
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
            out = {AutoCamera::kBlow, false, true, true, false};                 // an ant strikes (the blow of its clip, broadcast, at the ant that struck)
            return true;
        case S::CombatNetFairy:
            out = {AutoCamera::kBrawl, false, true, false, false};               // the dust ball of several ants fighting on one tile
            return true;
        case S::ThiefDive:
            out = {AutoCamera::kTheft, true, false, false, false};               // a Thief dives into a hill
            return true;
        case S::ThiefEmerge:
            out = {AutoCamera::kTheftOut, false, false, false, false};           // ... and comes out with the points
            return true;
        case S::BaseScoreDn:
            out = {AutoCamera::kScoreDown, true, false, false, false};           // a hill's score drops (at that hill)
            return true;
        case S::BaseScoreUp:
            out = {AutoCamera::kBank, false, false, false, false};               // food is banked (a hill's score goes up, at that hill)
            return true;
        case S::Anthill:
        case S::BaseAlarmSiren:
            out = {AutoCamera::kRaid, false, false, false, true};                // a Thief is at the hill of the raided player, or a base is under attack
            return true;
        case S::FireBeam:
        case S::FireErupt:
            out = {AutoCamera::kFire, true, false, false, false};                // a Fire ant lights a fire
            return true;
        case S::FireBurnout:
            out = {AutoCamera::kFireOut, false, false, false, false};
            return true;
        case S::BombDetonate:
            out = {AutoCamera::kBomb, true, false, false, false};
            return true;
        case S::BombDefuseGrab:
            out = {AutoCamera::kDefuse, true, false, false, false};              // a bomb is picked up to be defused
            return true;
        case S::BombPick:
            out = {AutoCamera::kBombMoved, false, false, false, false};
            return true;
        case S::AntDrown:
            out = {AutoCamera::kDrown, false, false, false, false};              // an ant falls in the water
            return true;
        case S::PowerUpDrop:
            out = {AutoCamera::kPowerUp, false, false, false, false};
            return true;
        case S::ShovelGravel:
        case S::ShovelWater:
            out = {AutoCamera::kBridge, false, false, false, false};             // a bridge goes up
            return true;
        case S::FoodGrab:
        case S::FoodHarvest:
            out = {AutoCamera::kCarry, false, false, true, false};               // an ant picks food up: it carries it home
            return true;
        default:
            return false;
    }
}

double distance(double ax, double ay, double bx, double by) { return std::hypot(ax - bx, ay - by); }

}  // namespace

void AutoCamera::set_mode(CameraMode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    resume_ = mode_ == CameraMode::Auto;
    if (mode_ == CameraMode::Manual) {
        locked_ = false;
        vel_ = Point{};
    }
}

bool AutoCamera::take() {
    if (mode_ == CameraMode::Manual) return false;
    set_mode(CameraMode::Manual);                                                      // (a move in progress stops where the viewer's hand put the view)
    return true;
}

void AutoCamera::reset() {
    events_.clear();
    sources_.clear();
    seen_.clear();
    fog_ = false;
    fog_revealed_.clear();
    has_last_ = false;
    deaths_ = 0;
    has_best_ = false;
    locked_ = false;
    quiet_since_ms_ = -1.0;
    locks_ = 0;
    has_target_ = false;
    vel_ = Point{};
    resume_ = false;
    look_ms_ = 0.0;
}

void AutoCamera::set_hills(const std::vector<Point>& centres) { hills_ = centres; }

bool AutoCamera::visible(double x, double y) const noexcept {
    if (!fog_) return true;
    if (x < 0.0 || y < 0.0) return false;
    const uint64_t tx = static_cast<uint64_t>(x / 32.0);
    const uint64_t ty = static_cast<uint64_t>(y / 32.0);
    if (tx >= fog_w_ || ty >= fog_h_) return false;
    const uint64_t at = ty * fog_w_ + tx;
    return at < fog_revealed_.size() && fog_revealed_[static_cast<size_t>(at)] != 0;
}

void AutoCamera::add(const Event& e, bool big) {
    if (speed_ >= kBigOnlySpeed && !big) return;
    if (!visible(e.x, e.y)) return;
    events_.push_back(e);
    last_action_ = Point{e.x, e.y};
    has_last_ = true;
    if (events_.size() > kMostEvents) events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(events_.size() - kMostEvents));
}

void AutoCamera::add_at(uint32_t ant, double x, double y, float weight, bool fight, bool big) {
    Event e;
    e.ant = ant;
    e.x = x;
    e.y = y;
    e.weight = weight;
    e.fight = fight;
    e.at_ms = now_ms_;
    if (ant != 0) {
        const auto it = seen_.find(ant);
        if (it != seen_.end()) e.team = it->second.team;
    }
    add(e, big);
}

void AutoCamera::observe_cue(const sim::AudioEvent& cue) {
    if (cue.stop) return;
    CueWeight w{};
    if (!cue_weight(cue.sound_id, w)) return;
    if (w.at_hill) {
        if (cue.target_player >= hills_.size()) return;
        add_at(0, hills_[cue.target_player].x, hills_[cue.target_player].y, w.weight, w.fight, w.big);
        return;
    }
    const uint32_t ant = w.ant ? cue.owner : 0u;                                       // (0: nobody)
    add_at(ant, static_cast<double>(cue.world_x), static_cast<double>(cue.world_y), w.weight, w.fight, w.big);
}

void AutoCamera::observe_world(const sim::WorldState& world) {
    fog_ = world.fog_of_war_enabled;                                                   // (with a Fog of War only the tiles that are revealed are seen)
    if (fog_) {
        fog_w_ = world.width;
        fog_h_ = world.height;
        fog_revealed_ = world.fog_revealed;
    } else {
        fog_revealed_.clear();
    }
    next_.clear();
    next_.reserve(world.ants.size());
    for (const sim::AntSnapshot& ant : world.ants) {                                   // (in the world's order: what is found first is the same in every run)
        Seen now;
        now.hp = ant.hp;
        now.x = ant.px;
        now.y = ant.py;
        now.team = ant.player_id;
        now.dead = ant.hp == 0 || ant.state == sim::UnitState::Dead;
        now.drowning = ant.is_drowning || ant.state == sim::UnitState::Drowning;
        next_[ant.id] = now;
        const auto before = seen_.find(ant.id);
        if (before == seen_.end() || before->second.dead || before->second.drowning || now.drowning) continue;
        if (now.hp < before->second.hp) {
            add_at(ant.id, ant.px, ant.py, kHit, true, false);
        }
        if (now.dead) {                                                                // (it lies there for a moment, then it is gone)
            add_at(ant.id, ant.px, ant.py, kDeath, true, true);
            ++deaths_;
        }
    }
    seen_.swap(next_);
}

double AutoCamera::influence(const Event& e) const noexcept {
    const double age = now_ms_ - e.at_ms;
    if (age >= kWindowMs) return 0.0;
    return static_cast<double>(e.weight) * (1.0 - age / kWindowMs);
}

void AutoCamera::gather() {
    sources_.clear();
    std::unordered_map<uint32_t, size_t> of_ant;                                       // (only looked up, never walked: the order of the sources is the order of the events)
    for (const Event& e : events_) {
        const double inf = influence(e);
        if (inf <= 0.0) continue;
        if (e.ant == 0) {
            sources_.push_back(Source{e.x, e.y, inf, 0.0, 255, false});
            continue;
        }
        const auto found = of_ant.find(e.ant);
        size_t at = 0;
        if (found == of_ant.end()) {
            at = sources_.size();
            of_ant[e.ant] = at;
            sources_.push_back(Source{e.x, e.y, 0.0, 0.0, e.team, true});
        } else {
            at = found->second;
        }
        Source& s = sources_[at];
        s.score += inf;
        if (e.fight) s.fight += inf;
        s.x = e.x;                                                                     // (the place of the ant's latest event ... )
        s.y = e.y;
        const auto here = seen_.find(e.ant);
        if (here != seen_.end()) {                                                     // ... unless the ant is in the world: then where it is now (the camera follows the ants), and its side
            s.x = static_cast<double>(here->second.x);
            s.y = static_cast<double>(here->second.y);
            s.team = here->second.team;
        }
    }
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(), [this](const Source& s) { return !visible(s.x, s.y); }), sources_.end());
}

AutoCamera::Disc AutoCamera::disc(double x, double y) const noexcept {
    Disc d;
    double sx = 0.0;
    double sy = 0.0;
    int fighters = 0;
    uint32_t sides = 0;
    for (const Source& s : sources_) {
        if (distance(s.x, s.y, x, y) > kRadius) continue;
        d.score += s.score;
        sx += s.x * s.score;
        sy += s.y * s.score;
        d.any = true;
        if (s.ant && s.fight >= kFighter) {
            ++fighters;
            if (s.team < 32) sides |= 1u << s.team;
        }
    }
    if (!d.any || d.score <= 0.0) {
        d.any = false;
        return d;
    }
    d.cx = sx / d.score;
    d.cy = sy / d.score;
    if ((sides & (sides - 1u)) != 0u) d.score *= 1.0 + kCrowd * static_cast<double>(std::min(fighters - 1, kCrowdCap));      // a fight of two sides or more
    return d;
}

bool AutoCamera::best_disc(Disc& out, Point& at) const {
    bool found = false;
    for (const Source& s : sources_) {
        const Disc d = disc(s.x, s.y);
        if (!d.any || (found && d.score <= out.score)) continue;
        out = d;
        found = true;
    }
    if (!found) return false;
    const Disc moved = disc(out.cx, out.cy);                                           // (the circle on the middle of its own action, which is the place the camera looks at)
    if (moved.any && moved.score >= out.score) out = moved;
    at = Point{out.cx, out.cy};
    return true;
}

bool AutoCamera::hot_spot(Point& out) const {
    if (!has_best_ || best_.score < kQuietScore) return false;
    out = best_at_;
    return true;
}

bool AutoCamera::target(Point& out) const {
    if (!has_target_) return false;
    out = target_;
    return true;
}

bool AutoCamera::gliding() const noexcept {
    return has_target_ && (distance(pos_.x, pos_.y, target_.x, target_.y) > 6.0 || std::hypot(vel_.x, vel_.y) > 20.0);
}

void AutoCamera::lock_on(const Point& at, const Point& cam) {
    anchor_ = at;
    target_ = at;
    has_target_ = true;
    locked_ = true;
    lock_score_ = has_best_ ? best_.score : 0.0;
    lock_since_ms_ = now_ms_;
    quiet_since_ms_ = -1.0;
    ++locks_;
    pos_ = cam;                                                                        // (the spring begins where the view is, with the speed it has: no snap)
}

// Every kEvalMs: where the action is, and whether the camera stays or moves
void AutoCamera::look(const Point& cam) {
    gather();
    has_best_ = best_disc(best_, best_at_);
    if (mode_ != CameraMode::Auto) return;

    if (resume_) {                                                                     // the button: to the busiest place now, else to where the last thing happened
        resume_ = false;
        if (has_best_ && best_.score >= kEngageScore) {
            lock_on(best_at_, cam);
        } else if (has_last_) {
            target_ = last_action_;
            has_target_ = true;
            pos_ = cam;
        }
        return;
    }
    if (!locked_) {
        if (has_best_ && best_.score >= kEngageScore) lock_on(best_at_, cam);
        return;
    }

    Disc here = disc(anchor_.x, anchor_.y);                                            // the action that is watched: follow its ants
    if (here.any) {
        anchor_.x += (here.cx - anchor_.x) * kFollow;
        anchor_.y += (here.cy - anchor_.y) * kFollow;
        here = disc(anchor_.x, anchor_.y);
    }
    lock_score_ = here.score;
    if (here.score < kQuietScore) {
        if (quiet_since_ms_ < 0.0) quiet_since_ms_ = now_ms_;
    } else {
        quiet_since_ms_ = -1.0;
    }
    if (now_ms_ - lock_since_ms_ < kDwellMs) return;                                   // the minimum stay
    const bool elsewhere = has_best_ && distance(best_at_.x, best_at_.y, anchor_.x, anchor_.y) > kRadius;
    if (elsewhere && best_.score >= kEngageScore && best_.score >= kClearlyBusier * here.score) {
        lock_on(best_at_, cam);                                                        // another place is clearly busier
    } else if (quiet_since_ms_ >= 0.0 && now_ms_ - quiet_since_ms_ >= kDieOutMs) {
        locked_ = false;                                                               // the action has died out: the view stays where it is, and the next action that engages takes it
    }
}

// The view's middle goes to the target with a critically damped spring (the same formula whatever the frame rate), at most kMaxSpeed
void AutoCamera::chase(double dt_s) {
    if (dt_s <= 0.0) return;
    const double smooth = kSmoothMs / 1000.0;
    const double omega = 2.0 / smooth;
    const double x = omega * dt_s;
    const double damp = 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x);
    double cx = pos_.x - target_.x;
    double cy = pos_.y - target_.y;
    const double reach = kMaxSpeed * smooth;
    const double gap = std::hypot(cx, cy);
    if (gap > reach) {
        cx *= reach / gap;
        cy *= reach / gap;
    }
    const double goal_x = pos_.x - cx;
    const double goal_y = pos_.y - cy;
    const double tx = (vel_.x + omega * cx) * dt_s;
    const double ty = (vel_.y + omega * cy) * dt_s;
    vel_.x = (vel_.x - omega * tx) * damp;
    vel_.y = (vel_.y - omega * ty) * damp;
    double nx = goal_x + (cx + tx) * damp;
    double ny = goal_y + (cy + ty) * damp;
    if ((target_.x - pos_.x) * (nx - target_.x) + (target_.y - pos_.y) * (ny - target_.y) > 0.0) {      // it would pass the target: it stops there
        nx = target_.x;
        ny = target_.y;
        vel_ = Point{};
    }
    pos_ = Point{nx, ny};
    if (distance(pos_.x, pos_.y, target_.x, target_.y) < 0.05 && std::hypot(vel_.x, vel_.y) < 0.5) {
        pos_ = target_;
        vel_ = Point{};
    }
}

bool AutoCamera::step(double dt_ms, const Point& cam, bool zoomed, Point& out) {
    now_ms_ += dt_ms;
    events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const Event& e) { return now_ms_ - e.at_ms >= kWindowMs; }), events_.end());
    look_ms_ += dt_ms;
    if (look_ms_ >= kEvalMs) {
        look_ms_ = 0.0;
        look(cam);
    } else if (resume_ && mode_ == CameraMode::Auto) {
        look(cam);                                                                     // (the button does not wait for the next look)
    }
    if (mode_ != CameraMode::Auto || !has_target_) return false;

    if (zoomed) pos_ = cam;                                                            // a zoom moved the middle of the view: the spring goes on from there, back to the place
    if (locked_) target_ = anchor_;
    chase(dt_ms / 1000.0);
    out = pos_;
    return zoomed || std::hypot(vel_.x, vel_.y) > 2.0 || distance(pos_.x, pos_.y, cam.x, cam.y) > 0.75;      // (a view that is where the spring is, and still, is left alone)
}

}  // namespace ants::app
