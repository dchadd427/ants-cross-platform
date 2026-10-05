#pragma once

// Touch controls: what one finger or two do on the match screen. A pure model: no SDL, no window; positions are picture pixels, times are the caller's milliseconds.
// It turns finger events into ACTIONS that the application runs with the calls that mouse events use (and the zoom API), so the game sees nothing a mouse cannot send.
//
// ONE finger on the map view waits to be understood: a TAP (up before kTapMs, within the slop) is a left click at the DOWN point; a finger that leaves the slop is a left DRAG
// (the rubber band); a finger that stays within the slop for kHoldMs is a HOLD, a right click (down at the down point when the time is up, up where the finger lifts: the game acts
// at the release). On the minimap and on everything else (the HUD's controls, dialogs, every other screen) the left press is made at once, as SDL's touch-to-mouse emulation made it;
// a finger that holds still on the minimap turns that press into a right click there ("send the selected ants there").
// TWO fingers on the map view PAN (the middle point's movement scrolls the map by the same distance) and ZOOM (the fingers' distance picks a level, with hysteresis) together. The pair is
// judged once per frame (update), never after one finger's event: two fingers that move together arrive as two events, and between them the distance is wrong by the whole step of one.
// A second finger that lands where that is not allowed does nothing, and neither does a third; a finger that stays after the other lifts is ignored until it lifts.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ants::app {
namespace touch {

/// A tap is up before this (a lift between it and kHoldMs does nothing: the finger lingered, and neither a click nor a right click was meant). Android's long-press time is 400 ms.
inline constexpr uint32_t kTapMs = 400;
/// A finger within the slop this long is a hold. To be tuned on the owner's phone.
inline constexpr uint32_t kHoldMs = 450;
/// The ring around a held finger starts to close here, so that a plain tap (a tenth of a second or two) never shows one
inline constexpr uint32_t kRingStartMs = 150;
/// The pulse after the hold fired
inline constexpr uint32_t kPulseMs = 240;
/// The slop is a size on the glass, not on the picture: 8 CSS pixels of the game box (Android's own touch slop is 8 dp), never less than 6 device pixels (a page zoomed far out)
inline constexpr double kSlopCssPx = 8.0;
inline constexpr double kSlopFloorDevicePx = 6.0;
/// A level changes when the fingers have gone this far past the middle of the step to the next one (a fraction of the step in ratio): 0.15 leaves a dead band of 0.3 of a step
/// around the middle, so a pinch that rests at a level does not flutter between two
inline constexpr double kHysteresis = 0.15;
/// Two fingers that land closer than this many slops are measured from this distance (a pinch that starts from touching fingers would otherwise zoom by hundreds)
inline constexpr double kMinSpanSlops = 3.0;
/// An event is stamped when SDL SAW it, which a stall makes late (the web: when the browser handed it to the page; native: when the game read the queue). It is judged no later than this
/// long after the last moment the model knew what the finger did (a frame, or the first finger's arrival): a frame every 100 ms or faster loses nothing, a stall of any length cannot
/// make a short touch long. 100 ms is also the exactness of the hold at 10 frames per second.
inline constexpr uint32_t kStallMs = 100;

/// The slop in picture pixels, from how many picture pixels one CSS pixel (one point of the window, on a desktop) and one device pixel of the game box cover. A scale that is
/// not a positive number counts for nothing; the result is between 1 and 64 picture pixels.
double slop_pixels(double picture_per_css_px, double picture_per_device_px) noexcept;

}  // namespace touch

/// Where a finger went down
enum class TouchZone : uint8_t {
    Map,        // the map view of a match, nothing open over it: the press waits (tap, drag, hold)
    Minimap,    // the minimap: the left press is made at once; a hold that did not move makes it a right click
    Direct,     // anything else: the left press is made at once, and the finger is a mouse until it lifts
};

/// What the model does for the finger: the mouse's events (a pointer arrives, a button goes down or up), the map's pan and zoom, the end of a press, and the hold's feedback
struct TouchAction {
    enum class Kind : uint8_t {
        Motion,         // the pointer is here (it arrives before a press, and follows a finger that holds a press)
        LeftDown,
        LeftUp,
        RightDown,
        RightUp,
        Pan,            // dx, dy: how far the fingers' middle point moved; the map moves with it (the view scrolls the other way)
        Zoom,           // level: the zoom to go to; x, y: the point of the picture that keeps its world point (the middle point)
        Cancel,         // the press that the finger holds ends with no act (no selection, no order, nothing fires: a release where no control is); x, y: where the finger is; `right`: it was the right button's
        HoldFired,      // the hold's time is up: the feedback (a pulse, a buzz); the right button's press comes with it
    };
    Kind kind{Kind::Motion};
    int32_t x{0};
    int32_t y{0};
    int32_t dx{0};
    int32_t dy{0};
    float level{1.0f};
    uint32_t at{0};             // when it happened (the caller's ms: a hold fires at down + kHoldMs, whenever the model got to hear of it)
    bool late{false};           // the first action of a press that WAITED on the map view: the game hears of the finger only now, and the screen may have changed since (a dialog opened over the map)
    bool right{false};          // a Cancel of the right button's press (a hold); the left button's otherwise

    /// (what an action does; `late` and `right` say where it comes from and which button a Cancel ends, and are not part of it)
    bool operator==(const TouchAction& o) const noexcept { return kind == o.kind && x == o.x && y == o.y && dx == o.dx && dy == o.dy && level == o.level && at == o.at; }
    bool operator!=(const TouchAction& o) const noexcept { return !(*this == o); }
};

/// What the model asks of the application (it knows no screen)
class TouchEnvironment {
public:
    virtual ~TouchEnvironment() = default;
    /// Where a finger that goes down at this point is
    virtual TouchZone zone_at(double x, double y) const = 0;
    /// May two fingers whose middle point is here pan and zoom the map now? (A match is on, the map view is under the point, no dialog or page is open, no press is held but the
    /// first finger's own on the map, no results screen.) Asked once, when the second finger lands.
    virtual bool two_fingers_allowed(double x, double y) const = 0;
    /// The map's zoom now, and the levels on offer (from the most zoomed in to the most zoomed out: Application::zoom_levels)
    virtual float zoom() const = 0;
    virtual std::vector<float> zoom_levels() const = 0;
};

class TouchControl {
public:
    using Actions = std::vector<TouchAction>;

    explicit TouchControl(const TouchEnvironment& environment) : env_(&environment) {}

    /// The slop of the next finger that goes down, in picture pixels (touch::slop_pixels); a gesture keeps the one it began with
    void set_slop(double picture_px) noexcept;
    double slop() const noexcept { return slop_; }

    /// A finger is identified by the touch device and its own id. Positions are picture pixels, with fractions (the glass is finer than the picture). Every call first brings the
    /// clock to `now_ms` (a hold that is due fires before what the call does); a time that goes backwards is taken as the last one. An EVENT's time is believed only up to
    /// touch::kStallMs after the last frame (update) or the first finger's arrival: SDL stamps an event when it sees it, so a stall must not turn a short touch into a hold.
    /// The moves of a PAIR say nothing: update() does (the pan and the zoom of everything that moved since the last one), and so does the next lift, landing or cancel of a finger,
    /// before it acts.
    Actions finger_down(int64_t touch, int64_t finger, double x, double y, uint32_t now_ms);
    Actions finger_motion(int64_t touch, int64_t finger, double x, double y, uint32_t now_ms);
    Actions finger_up(int64_t touch, int64_t finger, double x, double y, uint32_t now_ms);
    /// The clock, and the pair: a hold that is due fires (a frame knows that the finger is down now, whatever stalled before it), and two fingers that moved pan and zoom (call it
    /// once per frame, after the frame's events)
    Actions update(uint32_t now_ms);
    /// The browser took the touch away, the window lost the focus, the page was hidden, a screen changed under the fingers: nothing is tracked any more (a finger that never lifts
    /// must not block the next ones) and a press that was held ends with no act. The clock does not run first: nothing fires.
    Actions cancel();

    /// The ring that closes around a finger that may become a hold (nothing otherwise), and the pulse after one fired: where, and how far (0 .. 1)
    struct Mark {
        double x{0.0};
        double y{0.0};
        double progress{0.0};
    };
    std::optional<Mark> ring(uint32_t now_ms) const noexcept;
    std::optional<Mark> pulse(uint32_t now_ms) const noexcept;

    /// What it is doing, for the tests and the page's browser check
    enum class Mode : uint8_t {
        Idle,           // no finger is tracked
        Waiting,        // one finger on the map view, nothing sent: tap, drag or hold
        Left,           // the left button is down (a drag, a control, a minimap press that moved)
        Right,          // the right button is down (a hold)
        MinimapWait,    // the left button is down on the minimap, the finger has not left the slop: a hold makes it a right click
        Two,            // two fingers: pan and zoom
    };
    Mode mode() const noexcept { return fingers_.empty() ? Mode::Idle : mode_; }
    size_t fingers() const noexcept { return fingers_.size(); }
    /// Fingers that start nothing (a third finger, a finger left after a gesture, a second finger where two are not allowed)
    size_t ignored() const noexcept;
    /// Where the first finger is now (picture pixels); false when none is tracked as the first
    bool primary_point(double& x, double& y) const noexcept;

    struct Stats {
        uint32_t taps{0};
        uint32_t drags{0};
        uint32_t holds{0};
        uint32_t two_finger{0};
        uint32_t cancels{0};
    };
    const Stats& stats() const noexcept { return stats_; }

private:
    enum class Role : uint8_t { Primary, Secondary, Ignored };
    struct Finger {
        int64_t touch{0};
        int64_t id{0};
        Role role{Role::Primary};
        double x{0.0};
        double y{0.0};
    };
    Finger* find(int64_t touch, int64_t id) noexcept;
    Finger* primary() noexcept;
    Finger* secondary() noexcept;
    const Finger* primary() const noexcept;
    void advance(uint32_t now_ms, bool frame, Actions& out);
    void flush_pair(Actions& out);
    void fire_hold(Actions& out);
    void start(const Finger& finger, uint32_t now_ms, Actions& out);
    void begin_two(Finger& second, uint32_t now_ms, Actions& out);
    void move_two(uint32_t now_ms, Actions& out);
    void emit(Actions& out, TouchAction::Kind kind, double x, double y, uint32_t at, bool late = false) const;

    const TouchEnvironment* env_;
    double slop_{8.0};
    std::vector<Finger> fingers_;
    Mode mode_{Mode::Idle};
    uint32_t clock_{0};
    bool clock_set_{false};
    uint32_t known_{0};                     // the last moment that the model knew what the finger did: a frame, or the first finger's arrival (an event is judged within kStallMs of it)

    // the primary finger's gesture
    double down_x_{0.0};
    double down_y_{0.0};
    uint32_t down_at_{0};
    double gesture_slop_{8.0};
    bool on_map_{false};                    // it began on the map view: a second finger may take it over

    // the two-finger gesture
    double mid_x0_{0.0};
    double mid_y0_{0.0};
    int32_t panned_x_{0};
    int32_t panned_y_{0};
    bool pair_moved_{false};                // a finger of the pair moved since the pair was last judged
    double span0_{1.0};
    float zoom0_{1.0f};
    std::vector<float> levels_;
    size_t level_index_{0};

    // the last hold, for the pulse
    bool pulse_set_{false};
    double pulse_x_{0.0};
    double pulse_y_{0.0};
    uint32_t pulse_at_{0};

    Stats stats_{};
};

}  // namespace ants::app
