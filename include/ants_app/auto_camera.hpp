#pragma once

// The camera of a replay or a followed live match (docs/REPLAYS.md "The camera"): Auto looks at the biggest action and follows it, Manual leaves the view where the viewer put it. A pure model,
// no SDL and no renderer: the application feeds it what the engine says while the recording plays (the cues with a place, and the ants of the world after every turn), asks it once a frame where
// the view should be, and tells it when the viewer takes the camera. It only reads the match; nothing here reaches the simulation, the recording or any hash. A viewer's zoom is the viewer's own:
// the model moves the middle of the view and never the zoom.
//
// How it decides, in one paragraph. Everything that happens is an EVENT with a weight (a hit, a blow, a kill, a bomb, a fire, a theft, food carried or banked ...) and a place; an event fades away
// over a few seconds. An event of an ant belongs to the ant and so moves with it. The SCORE of a place is the sum of the faded events within seven tiles of it, and a fight between several ants of
// two sides or more counts extra (a team fight beats a lone scuffle). The camera LOCKS on the place with the most action and keeps looking at it: the middle of the place is the middle of its
// events, recomputed every tenth of a second from where the ants are now, so it follows the fight as it moves. It stays at least kDwellMs, and moves on only to a place that is clearly busier
// (kClearlyBusier times as busy) or when the action it watches has died out for kDieOutMs. The view reaches the place with a critically damped spring (no snap, no overshoot, a speed limit).
//
// Every number that tunes this is in the block "The numbers" below, and nowhere else.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ants_sim/sim_engine.hpp"

namespace ants::app {

enum class CameraMode : int { Manual = 0, Auto = 1 };

class AutoCamera {
public:
    // ---- The numbers (the one place that tunes the camera) ---------------------------------------------------------------------------------------------------------------------------------------

    /// Time. All of it is the viewer's own time (the frame's dt), not the match's: at 8x a place still holds for its seconds of looking.
    static constexpr double kWindowMs = 5000.0;      // an event fades away linearly over this long (a "few seconds": a fight that stopped is old news after five)
    static constexpr double kDwellMs = 7000.0;       // the camera stays on a place at least this long before it may move to another one (at 5 s the public recordings had runs of hops at the minimum; at 7 s none)
    static constexpr double kDieOutMs = 3000.0;      // ... or leaves when the action it watches has been quiet (below kQuietScore) for this long, once the dwell is over
    static constexpr double kEvalMs = 100.0;         // the places are looked at again every tenth of a second (not every frame: the camera does not react to a single tick)
    static constexpr double kSmoothMs = 600.0;       // the spring: about how long a move takes to arrive (a critically damped spring, no overshoot)
    static constexpr double kMaxSpeed = 1400.0;      // ... and the fastest the view moves, in world pixels a second (a trip across the map takes a few seconds, not a blink)
    static constexpr double kFollow = 0.4;           // each look moves the locked place this part of the way to where its ants are now (a smooth follow, not a jump)

    /// Space and score
    static constexpr double kRadius = 224.0;         // world pixels (seven tiles): events this close to a place are that place's action
    static constexpr double kEngageScore = 12.0;     // a place needs this much action to be locked on (a hit, a blow, a bomb or a fire alone is not; a kill with its hits, a theft, a brisk fight is). A place that is watched counts for at least this much when another one is compared with it
    static constexpr double kQuietScore = 3.0;       // below this the place the camera watches is quiet (kDieOutMs of it and the camera lets go)
    static constexpr double kClearlyBusier = 1.5;    // another place must beat the watched one by this factor to take the camera after the dwell
    static constexpr double kCrowd = 0.12;           // a fight of several ants of two or more sides: the score grows by this for every fighter after the first ...
    static constexpr int kCrowdCap = 10;             // ... up to this many fighters (a team fight of 11 is 2.2 times a duel of the same blows)
    static constexpr double kFighter = 0.5;          // an ant is a fighter of a place when its own fighting there has at least this much score left
    static constexpr float kBigOnlySpeed = 4.0f;     // at this speed and above only the big events count (kills, thefts, fires, bombs)

    /// Bounds and tolerances
    static constexpr size_t kMostEvents = 400;       // the events that are kept (the oldest go first): a bound for the work of a look
    static constexpr double kStillPx = 0.75;         // a view that is this near the spring (world pixels) and still is left alone
    static constexpr double kStillSpeed = 2.0;       // ... still means slower than this (world pixels a second)
    static constexpr double kGlidingPx = 6.0;        // the view is gliding while it is farther than this from its place ...
    static constexpr double kGlidingSpeed = 20.0;    // ... or faster than this (world pixels a second)

    /// The weights of an event (before it fades). Kills and fights weigh most; food and bridges are the least.
    static constexpr float kHit = 2.0f;              // an ant lost hit points (to a blow, a blast or a fire)
    static constexpr float kBlow = 1.0f;             // an ant struck (the blow's cue, which names the ant)
    static constexpr float kDeath = 14.0f;           // an ant died (killed; an ant that falls in the water has its own cue): a kill with its hit engages the camera alone
    static constexpr float kBomb = 10.0f;            // a bomb went off
    static constexpr float kBombMoved = 3.0f;        // a bomb was picked up (to throw or to defuse)
    static constexpr float kDefuse = 6.0f;           // a bomb is being defused
    static constexpr float kFire = 5.0f;             // a Fire ant lit a fire (each of its two cues, the burst and the beam: a fire alone is enough)
    static constexpr float kFireOut = 1.5f;          // a fire burnt out
    static constexpr float kTheft = 6.0f;            // a Thief dived into a hill
    static constexpr float kTheftOut = 3.0f;         // ... and came out with the points
    static constexpr float kScoreDown = 6.0f;        // a hill lost points
    static constexpr float kBrawl = 3.0f;            // the dust ball of several ants fighting on one tile
    static constexpr float kRaid = 3.0f;             // a base alarm or a raid (at the hill of the seat it is for)
    static constexpr float kDrown = 3.0f;            // an ant fell in the water
    static constexpr float kBank = 2.0f;             // food was banked at a hill (a hill's score went up)
    static constexpr float kCarry = 0.4f;            // an ant picked food up (it is carried home: the ant is followed)
    static constexpr float kPowerUp = 1.5f;          // a power-up dropped
    static constexpr float kBridge = 0.25f;          // a swimmer shovelled (a bridge goes up, a shovel at a time)

    // ---- The model ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

    struct Point {
        double x{0.0};
        double y{0.0};
    };

    CameraMode mode() const noexcept { return mode_; }
    /// Manual stops the moves. Auto starts them again: the view glides to the busiest place now (else to the last action) and locks on it.
    void set_mode(CameraMode mode);
    /// The viewer took the camera (the pointer at the edge, the minimap, two fingers): Manual. True when it was Auto.
    bool take();

    /// A new match, or a jump that played turns nobody saw: every event is forgotten (the mode stays)
    void reset();
    /// The hills of the match (the centre of each, by seat): where a base alarm or a raid on a base is
    void set_hills(const std::vector<Point>& centres);
    /// The speed of the replay (1 is normal): from kBigOnlySpeed up only the big events count
    void set_speed(float speed) noexcept { speed_ = speed; }
    float speed() const noexcept { return speed_; }

    /// One cue of the engine (the sound it starts says what happened, its place where, its owner which ant). Cues that mean nothing for the camera are ignored.
    void observe_cue(const sim::AudioEvent& cue);
    /// The world after a turn: an ant whose hit points fell was hit, an ant that is dead died. Where the world has a Fog of War, only what is on a revealed tile is seen (a replay has none).
    void observe_world(const sim::WorldState& world);

    /// A frame. `cam` is where the middle of the view is and `zoomed` that the zoom changed since the last frame (the middle moved with it: the model takes the middle as it is and glides back).
    /// Returns true when the middle of the view should be put at `out`; false leaves the camera alone.
    bool step(double dt_ms, const Point& cam, bool zoomed, Point& out);

    /// The busiest place now (for the rings of the minimap); false when it is quiet
    bool hot_spot(Point& out) const;
    /// The place the view is going to or watching (false before the first move)
    bool target(Point& out) const;
    /// Is the camera locked on an action (it follows the ants of it)?
    bool locked() const noexcept { return locked_; }
    /// How long it has watched this action (viewer's milliseconds; 0 when it is not locked)
    double dwell_ms() const noexcept { return locked_ ? now_ms_ - lock_since_ms_ : 0.0; }
    /// How many times the camera has locked on an action since the match began (a switch to another place is a new lock)
    uint32_t locks() const noexcept { return locks_; }
    const std::vector<Point>& hills() const noexcept { return hills_; }
    double now_ms() const noexcept { return now_ms_; }
    /// Is the view still on its way to the place (it moves faster than a drift, or is not there yet)?
    bool gliding() const noexcept;
    /// Is the minimum stay on the place not over yet?
    bool holding() const noexcept { return locked_ && now_ms_ - lock_since_ms_ < kDwellMs; }
    /// The events that still count (faded ones are gone)
    size_t events() const noexcept { return events_.size(); }
    /// How many deaths the camera has seen in this match (an ant that died, not one that drowned)
    uint32_t deaths() const noexcept { return deaths_; }

private:
    struct Event {
        uint32_t ant{0};          // 0: a place; else the ant it belongs to (it moves with it while it is in the world)
        double x{0.0};
        double y{0.0};
        float weight{0.0f};
        bool fight{false};        // a hit, a blow or a kill: the ants of it are fighters
        uint8_t team{255};
        double at_ms{0.0};
    };
    struct Seen {
        uint16_t hp{0};
        int32_t x{0};
        int32_t y{0};
        uint8_t team{255};
        bool dead{false};
        bool drowning{false};
    };
    /// One thing that has action at the moment: an ant (all its events together, at the place where it is now) or a place
    struct Source {
        double x{0.0};
        double y{0.0};
        double score{0.0};
        double fight{0.0};        // the part of the score that is fighting
        uint8_t team{255};
        bool ant{false};
    };
    /// A circle of kRadius around a place: its action
    struct Disc {
        double score{0.0};
        double cx{0.0};           // the middle of its action (its sources weighed by their score)
        double cy{0.0};
        bool any{false};
    };

    void add(const Event& e, bool big);
    void add_at(uint32_t ant, double x, double y, float weight, bool fight, bool big);
    bool visible(double x, double y) const noexcept;
    double influence(const Event& e) const noexcept;
    void gather();                              // the sources now
    Disc disc(double x, double y) const noexcept;
    bool best_disc(Disc& out, Point& at) const;
    void look(const Point& cam);                // every kEvalMs: the places, and the decision
    void lock_on(const Point& at, const Point& cam);
    void chase(double dt_s);

    CameraMode mode_{CameraMode::Auto};
    float speed_{1.0f};
    double now_ms_{0.0};
    double look_ms_{0.0};
    std::vector<Event> events_;
    std::vector<Source> sources_;
    std::vector<Point> hills_;
    std::unordered_map<uint32_t, Seen> seen_;
    std::unordered_map<uint32_t, Seen> next_;
    bool fog_{false};
    uint32_t fog_w_{0};
    uint32_t fog_h_{0};
    std::vector<uint8_t> fog_revealed_;
    bool has_last_{false};
    Point last_action_;
    uint32_t deaths_{0};
    // the busiest place now (from the last look)
    bool has_best_{false};
    Disc best_;
    Point best_at_;
    // the lock
    bool locked_{false};
    Point anchor_;                              // the middle of the action that is watched, followed
    double lock_score_{0.0};
    double lock_since_ms_{0.0};
    double quiet_since_ms_{-1.0};
    uint32_t locks_{0};
    // the view
    bool has_target_{false};
    Point target_;
    Point pos_;
    Point vel_;
    bool resume_{false};                        // the button: go to the busiest place now
};

}  // namespace ants::app
