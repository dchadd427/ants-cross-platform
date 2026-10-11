#pragma once

// The camera of a replay or a followed live match (docs/REPLAYS.md "Camera"): Auto moves the view to the busiest place, Manual leaves it where the viewer put it. A pure model, no SDL and no
// renderer: the application feeds it what the engine says while the recording plays (the cues with a place on the map, and the ants that were hit or died, read off the world), asks it once a
// frame where the view should be, and tells it when the viewer takes the camera. It only reads the match; nothing here reaches the simulation, the recording or any hash.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ants_sim/sim_engine.hpp"

namespace ants::app {

enum class CameraMode : int { Manual = 0, Auto = 1 };

class AutoCamera {
public:
    /// Times are the viewer's own seconds (the frame's dt), not the match's: at 8x a moment still holds for 3 s of looking.
    static constexpr double kHoldMs = 3000.0;        // the view stays at least this long once it has arrived
    static constexpr double kGlideMs = 800.0;        // a move takes this long
    static constexpr double kWindowMs = 4000.0;      // a moment counts for this long, fading away
    static constexpr double kRadius = 224.0;         // world pixels (seven tiles): moments this close are one place
    static constexpr double kMinScore = 1.5;         // less than this is quiet: the view stays where it is
    static constexpr double kClearlyBusier = 1.5;    // another place must beat the one the view is at by this much to take it
    static constexpr double kStayInside = 0.3;       // a place already inside this part of the view (each way from its middle) needs no move
    static constexpr float kBigOnlySpeed = 4.0f;     // at this speed and above only the big moments count

    /// The weights (defaults, in one place): a hit is medium, a death, a theft, a fire and a bomb are high, the rest low
    static constexpr float kHit = 2.0f;
    static constexpr float kDeath = 8.0f;
    static constexpr float kTheft = 8.0f;
    static constexpr float kFire = 8.0f;
    static constexpr float kBomb = 10.0f;

    struct Point {
        double x{0.0};
        double y{0.0};
    };

    CameraMode mode() const noexcept { return mode_; }
    /// Manual stops the moves. Auto starts them again: the view glides to the last action (else to the busiest place), and holds there.
    void set_mode(CameraMode mode);
    /// The viewer took the camera (the pointer at the edge, the minimap, two fingers): Manual. True when it was Auto.
    bool take();

    /// A new match, or a jump that played turns nobody saw: every moment is forgotten (the mode stays)
    void reset();
    /// The hills of the match (the centre of each, by seat): where a base alarm or a raid on a base is
    void set_hills(const std::vector<Point>& centres);
    /// The speed of the replay (1 is normal): from 4 up only the big moments count
    void set_speed(float speed) noexcept { speed_ = speed; }

    /// One cue of the engine (the sound it starts says what happened, its place where). Cues that mean nothing for the camera are ignored.
    void observe_cue(const sim::AudioEvent& cue);
    /// The world after a turn: an ant whose hit points fell was hit, an ant that is gone died (the engine tells deaths in the news, with no place)
    void observe_world(const sim::WorldState& world);

    /// A frame. `cam` is where the middle of the view is, `view` its size in world pixels, `zoomed` that the zoom changed since the last frame. Returns true when the view should be put at `out`
    /// (while it glides, and after a zoom, which keeps Auto at the viewer's zoom); false leaves the camera alone.
    bool step(double dt_ms, const Point& cam, const Point& view, bool zoomed, Point& out);

    /// The busiest place now (for the rings of the minimap); false when it is quiet
    bool hot_spot(Point& out) const;
    /// The place the view is at or going to (false before the first move)
    bool target(Point& out) const;
    const std::vector<Point>& hills() const noexcept { return hills_; }
    double now_ms() const noexcept { return now_ms_; }
    bool gliding() const noexcept { return has_target_ && glide_ms_ < kGlideMs; }
    bool holding() const noexcept { return has_target_ && now_ms_ < hold_until_ms_; }
    size_t moments() const noexcept { return moments_.size(); }

private:
    struct Moment {
        double x{0.0};
        double y{0.0};
        float weight{0.0f};
        double at_ms{0.0};
    };
    struct Seen {
        uint16_t hp{0};
        int32_t x{0};
        int32_t y{0};
        bool drowning{false};
    };
    struct Spot {
        Point at;
        double score{0.0};
    };

    void add(double x, double y, float weight, bool big);
    double influence(const Moment& m) const noexcept;
    double score_at(const Point& p) const noexcept;
    bool best_spot(Spot& out) const;
    void go_to(const Point& to, const Point& from);

    CameraMode mode_{CameraMode::Auto};
    float speed_{1.0f};
    double now_ms_{0.0};
    std::vector<Moment> moments_;
    std::vector<Point> hills_;
    std::unordered_map<uint32_t, Seen> seen_;
    std::unordered_map<uint32_t, Seen> next_;
    bool have_seen_{false};
    bool has_target_{false};
    Point target_;
    Point from_;
    double glide_ms_{0.0};
    double hold_until_ms_{0.0};
    bool has_last_{false};
    Point last_action_;
    bool return_pending_{false};
};

}  // namespace ants::app
