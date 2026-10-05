// The touch controls' model (include/ants_app/touch_control.hpp), alone: finger events in, actions out, an injected clock, no SDL, no window. The application's own tests
// (test_touch_app) check what is done with the actions; this program checks every rule of the model against expectations that are written out here, not read from the header:
//   * one finger on the map view: the TAP (both halves of the click at the DOWN point, also with a wobble up to the slop, at the last millisecond before the tap time, never after it),
//     the HOLD (one millisecond before the hold time nothing, at the hold time the right button goes down exactly once, up at the lift; a move after it), the DRAG (the press at the
//     origin first, then the motion; the slop is exact), what a finger that lingers between the tap time and the hold time does (nothing), a hold that is due when the next event arrives,
//     an event that a stall stamped late (judged within 100 ms of the last frame: a short touch never becomes a hold, a real hold at 16, 100 and 250 ms frames still fires);
//   * the minimap (the press at once, a scroll, a hold that makes a right click there) and every other place (the press at once, a mouse until it lifts);
//   * the second finger landing in each state (a waiting finger: silent; a drag or a hold: the press is cancelled), where it is refused (nothing happens, the first finger goes on),
//     the pan (whole pixels, nothing lost), the pinch (the ratio to levels, the anchor at the middle point, the hysteresis, the ends of the list, fingers that touch);
//   * the third finger, lifts in either order, a finger that stays, unknown finger ids, a finger that is down again, positions that are not numbers, a cancel in every state, two
//     touch devices, the clock (wrap, backwards), the slop's size and the ring and the pulse;
//   * the feedback's geometry (touch_feedback.hpp, pure too): an annulus is exactly the pixels whose centre lies in it, the sizes follow the slop, the ring closes and brightens, the pulse
//     goes out and fades, and a mark that is no position is nothing.
// Usage: test_touch_model. Exit code 0 when every check passes.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/touch_control.hpp"
#include "ants_app/touch_feedback.hpp"
#include "ants_app/view_zoom.hpp"

using namespace ants::app;
using Kind = TouchAction::Kind;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        static const int cap = std::getenv("ANTS_TEST_FAILURES") != nullptr ? std::atoi(std::getenv("ANTS_TEST_FAILURES")) : 60;
        if (g_failures <= cap) std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

// ---- actions, written and printed ----

TouchAction act(Kind kind, int x, int y, uint32_t at) {
    TouchAction a;
    a.kind = kind;
    a.x = x;
    a.y = y;
    a.at = at;
    return a;
}
TouchAction pan(int dx, int dy, uint32_t at) {                    // (the point of a pan is the middle point: not compared, see same)
    TouchAction a;
    a.kind = Kind::Pan;
    a.dx = dx;
    a.dy = dy;
    a.at = at;
    return a;
}
TouchAction zoom_to(float level, int x, int y, uint32_t at) {
    TouchAction a;
    a.kind = Kind::Zoom;
    a.level = level;
    a.x = x;
    a.y = y;
    a.at = at;
    return a;
}

const char* name_of(Kind k) {
    switch (k) {
        case Kind::Motion: return "Motion";
        case Kind::LeftDown: return "LeftDown";
        case Kind::LeftUp: return "LeftUp";
        case Kind::RightDown: return "RightDown";
        case Kind::RightUp: return "RightUp";
        case Kind::Pan: return "Pan";
        case Kind::Zoom: return "Zoom";
        case Kind::Cancel: return "Cancel";
        case Kind::HoldFired: return "HoldFired";
    }
    return "?";
}

std::string show(const TouchAction& a) {
    char buf[160];
    if (a.kind == Kind::Pan) std::snprintf(buf, sizeof(buf), "Pan(%d,%d)@%u", a.dx, a.dy, a.at);
    else if (a.kind == Kind::Zoom) std::snprintf(buf, sizeof(buf), "Zoom(%.4g at %d,%d)@%u", static_cast<double>(a.level), a.x, a.y, a.at);
    else std::snprintf(buf, sizeof(buf), "%s(%d,%d)@%u", name_of(a.kind), a.x, a.y, a.at);
    return buf;
}

std::string show(const TouchControl::Actions& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + show(v[i]);
    return out + "]";
}

/// A pan is compared by its distance and its time (the point that comes with it is the middle point, checked on its own)
bool same(const TouchAction& got, const TouchAction& want) {
    if (want.kind == Kind::Pan) return got.kind == Kind::Pan && got.dx == want.dx && got.dy == want.dy && got.at == want.at;
    return got == want;
}

void expect(const TouchControl::Actions& got, const TouchControl::Actions& want, const std::string& what) {
    bool ok = got.size() == want.size();
    for (size_t i = 0; ok && i < got.size(); ++i) ok = same(got[i], want[i]);
    check(ok, what + ": got " + show(got) + ", wanted " + show(want));
}

void expect_none(const TouchControl::Actions& got, const std::string& what) {
    check(got.empty(), what + ": nothing, got " + show(got));
}

// ---- the application, as the model sees it ----

struct Env : TouchEnvironment {
    // the map view is [0, 762) x [0, 500); the minimap [780, 900) x [20, 120); everything else is the HUD, a dialog, any other screen
    bool allow_two{true};
    float level{1.0f};
    std::vector<float> list;
    mutable int zone_asked{0};
    mutable int two_asked{0};
    mutable double two_x{-1.0};
    mutable double two_y{-1.0};
    bool everything_direct{false};

    Env() {
        zoom::Fit fit;
        fit.view_w = 762;
        fit.view_h = 500;
        fit.map_w = 1920;
        fit.map_h = 1920;
        list = zoom::levels(fit, zoom::Limits::any());          // (the application's own list: from the most zoomed in to the most zoomed out)
    }
    TouchZone zone_at(double x, double y) const override {
        ++zone_asked;
        if (everything_direct) return TouchZone::Direct;
        if (x >= 0.0 && x < 762.0 && y >= 0.0 && y < 500.0) return TouchZone::Map;
        if (x >= 780.0 && x < 900.0 && y >= 20.0 && y < 120.0) return TouchZone::Minimap;
        return TouchZone::Direct;
    }
    bool two_fingers_allowed(double x, double y) const override {
        ++two_asked;
        two_x = x;
        two_y = y;
        return allow_two;
    }
    float zoom() const override { return level; }
    std::vector<float> zoom_levels() const override { return list; }
};

constexpr uint32_t T0 = 1000;

/// The model with the application's loop around it. The application runs a frame (update) between its events, so an event is never judged late (touch_control.hpp: an event's time is
/// believed only up to touch::kStallMs after the last frame). Most tests are about the other rules and give their events as the application would: the rig runs the frame at the event's own
/// time first (what it does, a hold that falls due, comes first in the actions that the call returns). The moves of a pair are left pending for the frame that judges them. Rig(false) is the
/// model alone, its events raw: test_stall.
struct Rig {
    Env env;
    TouchControl touch{env};
    bool with_frames{true};
    explicit Rig(bool app_frames = true) : with_frames(app_frames) { touch.set_slop(10.0); }

    TouchControl::Actions lead(uint32_t t, bool pair_moves_wait = false) {
        if (!with_frames || (pair_moves_wait && touch.mode() == TouchControl::Mode::Two)) return {};
        return touch.update(t);
    }
    TouchControl::Actions down(int64_t id, double x, double y, uint32_t t, int64_t device = 1) {
        TouchControl::Actions out = lead(t);
        const TouchControl::Actions own = touch.finger_down(device, id, x, y, t);
        out.insert(out.end(), own.begin(), own.end());
        return out;
    }
    TouchControl::Actions move(int64_t id, double x, double y, uint32_t t, int64_t device = 1) {
        TouchControl::Actions out = lead(t, true);
        const TouchControl::Actions own = touch.finger_motion(device, id, x, y, t);
        out.insert(out.end(), own.begin(), own.end());
        return out;
    }
    TouchControl::Actions up(int64_t id, double x, double y, uint32_t t, int64_t device = 1) {
        TouchControl::Actions out = lead(t);
        const TouchControl::Actions own = touch.finger_up(device, id, x, y, t);
        out.insert(out.end(), own.begin(), own.end());
        return out;
    }
    TouchControl::Actions tick(uint32_t t) { return touch.update(t); }
};

bool idle(const Rig& r) { return r.touch.mode() == TouchControl::Mode::Idle && r.touch.fingers() == 0; }

/// A move of a finger of a PAIR and the frame that follows it: the pair is judged once per frame (update), after all the moves that came with it
TouchControl::Actions framed_move(Rig& r, int64_t id, double x, double y, uint32_t t, int64_t device = 1) {
    TouchControl::Actions out = r.move(id, x, y, t, device);
    const TouchControl::Actions judged = r.tick(t);
    out.insert(out.end(), judged.begin(), judged.end());
    return out;
}

/// What a plain tap at (x, y) is, from t to t + ms
TouchControl::Actions tap(Rig& r, int64_t id, double x, double y, uint32_t t, uint32_t ms = 80) {
    r.down(id, x, y, t);
    return r.up(id, x, y, t + ms);
}

/// The application's frames: update() every `step` ms after `from`, the last one at or before `to`; what they did together
TouchControl::Actions frames(Rig& r, uint32_t from, uint32_t to, uint32_t step = 16) {
    TouchControl::Actions out;
    for (uint32_t t = from + step; static_cast<int32_t>(to - t) >= 0; t += step) {
        const TouchControl::Actions a = r.tick(t);
        out.insert(out.end(), a.begin(), a.end());
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// One finger on the map view
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_tap() {
    group("tap", "a tap is the whole click at the DOWN point, up before the tap time and within the slop");
    {
        Rig r;
        expect_none(r.down(1, 100, 100, T0), "the finger's arrival on the map view sends nothing (it waits)");
        check(r.touch.mode() == TouchControl::Mode::Waiting && r.touch.fingers() == 1, "it is waiting");
        expect(r.up(1, 100, 100, T0 + 80), {act(Kind::Motion, 100, 100, T0 + 80), act(Kind::LeftDown, 100, 100, T0 + 80), act(Kind::LeftUp, 100, 100, T0 + 80)},
               "a tap: the pointer arrives, the button goes down and up, at the down point, in this order");
        check(idle(r) && r.touch.stats().taps == 1, "afterwards nothing is tracked and a tap was counted");
    }
    {
        Rig r;                                                  // a wobble that stays within the slop (10): the click is still at the DOWN point, never at the lift point
        r.down(1, 100, 100, T0);
        expect_none(r.move(1, 105, 105, T0 + 20), "a move of 7 px (slop 10) sends nothing");
        expect_none(r.move(1, 110, 100, T0 + 30), "a move of exactly the slop sends nothing (it has not left it)");
        expect_none(r.move(1, 93, 96, T0 + 40), "a move of 8 px the other way sends nothing");
        expect(r.up(1, 96, 104, T0 + 90), {act(Kind::Motion, 100, 100, T0 + 90), act(Kind::LeftDown, 100, 100, T0 + 90), act(Kind::LeftUp, 100, 100, T0 + 90)},
               "a wobbly tap clicks at the down point, not where the finger lifted");
    }
    {
        Rig r;                                                  // the tap time: the last millisecond before it is a tap, it and every millisecond up to the hold time are not
        expect(tap(r, 1, 50, 60, T0, 399), {act(Kind::Motion, 50, 60, T0 + 399), act(Kind::LeftDown, 50, 60, T0 + 399), act(Kind::LeftUp, 50, 60, T0 + 399)}, "399 ms: a tap");
        check(touch::kTapMs == 400 && touch::kHoldMs == 450, "the constants are 400 and 450 ms");
        expect_none(tap(r, 1, 50, 60, T0 + 1000, 400), "400 ms: the finger lingered: no click");
        check(idle(r), "... and it is gone");
        expect_none(tap(r, 1, 50, 60, T0 + 2000, 420), "420 ms: nothing");
        expect_none(tap(r, 1, 50, 60, T0 + 3000, 449), "449 ms: nothing (the hold has not fired either)");
        check(r.touch.stats().taps == 1 && r.touch.stats().holds == 0, "one tap and no hold were counted");
    }
    {
        Rig r;                                                  // the click's coordinates are whole picture pixels (the glass is finer): the pixel under the point
        r.down(1, 100.9, 100.2, T0);
        expect(r.up(1, 100.9, 100.2, T0 + 50), {act(Kind::Motion, 100, 100, T0 + 50), act(Kind::LeftDown, 100, 100, T0 + 50), act(Kind::LeftUp, 100, 100, T0 + 50)}, "100.9, 100.2 is the pixel (100, 100)");
    }
}

void test_hold() {
    group("hold", "a finger that stays within the slop for the hold time presses the right button at the down point; it goes up where the finger lifts");
    {
        Rig r;
        r.down(1, 200, 150, T0);
        expect_none(r.tick(T0 + 449), "one millisecond before the hold time: nothing");
        check(r.touch.mode() == TouchControl::Mode::Waiting, "... still waiting, a tap or a drag or a hold");
        expect(r.tick(T0 + 450), {act(Kind::Motion, 200, 150, T0 + 450), act(Kind::RightDown, 200, 150, T0 + 450), act(Kind::HoldFired, 200, 150, T0 + 450)},
               "at the hold time: the pointer arrives, the right button goes down at the down point, the hold fires (once)");
        check(r.touch.mode() == TouchControl::Mode::Right && r.touch.stats().holds == 1, "it is a held right button");
        expect_none(r.tick(T0 + 451), "the next millisecond: nothing more");
        expect_none(r.tick(T0 + 3000), "much later: nothing more (exactly once)");
        expect(r.up(1, 203, 152, T0 + 3100), {act(Kind::RightUp, 203, 152, T0 + 3100)}, "the lift lets the right button go where the finger is");
        check(idle(r), "afterwards nothing is tracked");
    }
    {
        Rig r;                                                  // a hold that is due when the next event arrives fires first, at its own time (the rig runs the application's frame at the event's time first:
                                                                // this used to be the lift with NO frame before it, which "stall" now judges within 100 ms of the down: a tap)
        r.down(1, 200, 150, T0);
        expect(r.up(1, 200, 150, T0 + 520), {act(Kind::Motion, 200, 150, T0 + 450), act(Kind::RightDown, 200, 150, T0 + 450), act(Kind::HoldFired, 200, 150, T0 + 450), act(Kind::RightUp, 200, 150, T0 + 520)},
               "a lift at 520 ms: the hold fired at 450, then the right button goes up");
        Rig m;
        m.down(1, 200, 150, T0);
        expect(m.move(1, 202, 151, T0 + 500), {act(Kind::Motion, 200, 150, T0 + 450), act(Kind::RightDown, 200, 150, T0 + 450), act(Kind::HoldFired, 200, 150, T0 + 450), act(Kind::Motion, 202, 151, T0 + 500)},
               "a small move at 500 ms: the hold first, then the move");
    }
    {
        Rig r;                                                  // a hold, then a move far beyond the slop: still the right button, not a drag
        r.down(1, 200, 150, T0);
        r.tick(T0 + 450);
        expect(r.move(1, 330, 240, T0 + 600), {act(Kind::Motion, 330, 240, T0 + 600)}, "after the hold a move is a pointer move, whatever its length (no left button)");
        expect(r.move(1, 340, 250, T0 + 620), {act(Kind::Motion, 340, 250, T0 + 620)}, "and another");
        expect(r.up(1, 341, 251, T0 + 700), {act(Kind::RightUp, 341, 251, T0 + 700)}, "the right button goes up where the finger is: the game decides at the release, as for the mouse");
    }
    {
        Rig r;                                                  // the hold time counts from the down, a wobble within the slop does not restart it
        r.down(1, 200, 150, T0);
        r.move(1, 205, 150, T0 + 300);
        r.move(1, 195, 153, T0 + 440);
        expect(r.tick(T0 + 450), {act(Kind::Motion, 200, 150, T0 + 450), act(Kind::RightDown, 200, 150, T0 + 450), act(Kind::HoldFired, 200, 150, T0 + 450)}, "the wobble did not restart the clock");
    }
    {
        Rig r;                                                  // the press is at the point where the finger went down, even when the finger has wandered (within the slop) by then
        r.down(1, 200, 150, T0);
        r.move(1, 207, 156, T0 + 200);
        const auto fired = r.tick(T0 + 460);
        check(fired.size() == 3 && fired[1].kind == Kind::RightDown && fired[1].x == 200 && fired[1].y == 150, "RightDown at (200, 150): " + show(fired));
    }
}

/// A tap at (x, y) judged at `at`: the pointer, the press, the release
TouchControl::Actions click_at(int x, int y, uint32_t at) {
    return {act(Kind::Motion, x, y, at), act(Kind::LeftDown, x, y, at), act(Kind::LeftUp, x, y, at)};
}

/// The hold's actions, at its own time
TouchControl::Actions hold_at(int x, int y, uint32_t down) {
    return {act(Kind::Motion, x, y, down + touch::kHoldMs), act(Kind::RightDown, x, y, down + touch::kHoldMs), act(Kind::HoldFired, x, y, down + touch::kHoldMs)};
}

void test_stall() {
    group("stall", "an event that a stall stamped late is judged within 100 ms of the last time the finger was known to be down (a frame is one): a short touch never becomes a hold, a real hold still fires");
    check(touch::kStallMs == 100, "the tolerance is 100 ms");
    {
        Rig r(false);                                                  // a tap whose lift was stamped 400 ms late (the finger left the glass at about 100 ms): still a tap
        r.down(1, 200, 150, T0);
        expect_none(frames(r, T0, T0 + 32), "two frames");
        expect(r.up(1, 200, 150, T0 + 500), click_at(200, 150, T0 + 132), "a lift stamped 468 ms after the last frame: a tap, judged 100 ms after that frame");
        check(idle(r) && r.touch.stats().taps == 1 && r.touch.stats().holds == 0, "one tap and no hold");
    }
    {
        Rig r(false);                                                  // a first move that was stamped late begins a drag (a box), not a hold that was due first
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 32);
        expect(r.move(1, 260, 150, T0 + 600), {act(Kind::Motion, 200, 150, T0 + 132), act(Kind::LeftDown, 200, 150, T0 + 132), act(Kind::Motion, 260, 150, T0 + 132)},
               "a move stamped 568 ms after the last frame: the drag begins at the down point");
        check(r.touch.mode() == TouchControl::Mode::Left && r.touch.stats().drags == 1 && r.touch.stats().holds == 0, "a drag and no hold");
        expect(r.up(1, 260, 150, T0 + 900), {act(Kind::LeftUp, 260, 150, T0 + 132)}, "its lift, stamped later still, ends it");
    }
    {
        Rig r(false);                                                  // a slow tap (the finger leaves at 300 ms) and a 150 ms stall: the lift is stamped 438, in the dead zone: it was a lost tap
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 288);
        expect(r.up(1, 200, 150, T0 + 438), click_at(200, 150, T0 + 388), "stamped 438: judged at 388, a tap");
        Rig s(false);                                                  // without the stall nothing changes: the lift keeps its own time
        s.down(1, 200, 150, T0);
        frames(s, T0, T0 + 288);
        expect(s.up(1, 200, 150, T0 + 300), click_at(200, 150, T0 + 300), "stamped 300, 12 ms after a frame: a tap at its own time");
    }
    {
        // what a lift that is stamped far too late (2 s) is judged to be, by the last time the finger was known to be down: a tap below 400 ms, nothing from 400 to 449, a hold from 450
        struct Case {
            uint32_t last_known;
            int kind;           // 0 tap, 1 nothing, 2 hold
        };
        const Case cases[] = {{0, 0}, {30, 0}, {250, 0}, {299, 0}, {300, 1}, {349, 1}, {350, 2}, {400, 2}};
        for (const Case& c : cases) {
            Rig r(false);
            r.down(1, 200, 150, T0);
            if (c.last_known > 0) r.tick(T0 + c.last_known);
            const uint32_t judged = c.last_known + touch::kStallMs;
            const TouchControl::Actions got = r.up(1, 200, 150, T0 + 2000);
            const std::string what = "the last frame " + std::to_string(c.last_known) + " ms after the down, the lift stamped at 2000: judged at " + std::to_string(judged) + " ms";
            if (c.kind == 0) {
                expect(got, click_at(200, 150, T0 + judged), what + ": a tap");
            } else if (c.kind == 1) {
                expect_none(got, what + ": the finger lingered");
            } else {
                TouchControl::Actions want = hold_at(200, 150, T0);
                want.push_back(act(Kind::RightUp, 200, 150, T0 + judged));
                expect(got, want, what + ": the hold, then its release");
            }
            check(idle(r), "the finger is gone");
        }
        Rig r(false);                                                  // the first finger's arrival is a known moment too: no frame at all, the lift 500 ms later is judged at 100 ms
        r.down(1, 200, 150, T0);
        expect(r.up(1, 200, 150, T0 + 500), click_at(200, 150, T0 + 100), "no frame between the down and a lift stamped 500 ms later: a tap at 100 ms");
    }
    {
        Rig r(false);                                                  // the tolerance is exact: 100 ms after the last frame is believed, 101 is judged at 100
        r.down(1, 200, 150, T0);
        r.tick(T0 + 32);
        expect(r.up(1, 200, 150, T0 + 132), click_at(200, 150, T0 + 132), "a lift stamped 100 ms after the last frame keeps its time");
        Rig s(false);
        s.down(1, 200, 150, T0);
        s.tick(T0 + 32);
        expect(s.up(1, 200, 150, T0 + 133), click_at(200, 150, T0 + 132), "one millisecond more is judged at 100");
    }
    for (const uint32_t gap : {16u, 100u, 250u}) {
        // a REAL hold at a slow frame rate: the ring is seen, the hold fires from the frame that first reaches its time (at its own time, once), the lift ends it
        const std::string rate = "frames every " + std::to_string(gap) + " ms: ";
        Rig r(false);
        r.down(1, 200, 150, T0);
        TouchControl::Actions fired;
        bool ring_seen = false;
        uint32_t last = T0;
        for (uint32_t t = T0 + gap; t <= T0 + 700; t += gap) {
            const TouchControl::Actions a = r.tick(t);
            fired.insert(fired.end(), a.begin(), a.end());
            ring_seen = ring_seen || (t < T0 + touch::kHoldMs && r.touch.ring(t).has_value());
            last = t;
        }
        check(ring_seen, rate + "the ring was seen while the hold was coming");
        expect(fired, hold_at(200, 150, T0), rate + "the hold fired from a frame, at its own time, once");
        check(r.touch.mode() == TouchControl::Mode::Right && r.touch.stats().holds == 1, rate + "the right button is held");
        expect(r.up(1, 203, 152, last + 20), {act(Kind::RightUp, 203, 152, last + 20)}, rate + "a lift 20 ms after a frame lets it go there");
        check(idle(r), rate + "afterwards nothing is tracked");
    }
    {
        Rig r(false);                                                  // at 4 frames per second a lift 220 ms after the last frame is believed 100 ms after it: after the hold, so the release at that time
        r.down(1, 200, 150, T0);
        r.tick(T0 + 250);
        r.tick(T0 + 500);
        expect(r.up(1, 203, 152, T0 + 720), {act(Kind::RightUp, 203, 152, T0 + 600)}, "the hold had fired from its frame; the lift is judged 100 ms after the last one");
    }
    {
        Rig r(false);                                                  // at 10 frames per second a lift 20 ms after the hold's time is still a hold; the frame before it was 50 ms before the time
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 400, 100);
        TouchControl::Actions want = hold_at(200, 150, T0);
        want.push_back(act(Kind::RightUp, 200, 150, T0 + 470));
        expect(r.up(1, 200, 150, T0 + 470), want, "a real hold of 470 ms at 10 frames per second: the hold at its time, then its release");
        Rig s(false);                                                  // ... and at 4 frames per second the same lift is a tap: the model cannot tell it from a stalled one (documented: below 10 fps a touch that ends within a frame of the hold's time is judged by the tolerance)
        s.down(1, 200, 150, T0);
        s.tick(T0 + 250);
        expect(s.up(1, 200, 150, T0 + 470), click_at(200, 150, T0 + 350), "at 4 frames per second a lift 220 ms after the last frame is judged at 350 ms: a tap");
    }
    {
        Rig r(false);                                                  // a hold that is due when the next event arrives, a frame having come within the tolerance of its time: the event is believed
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 440);
        TouchControl::Actions want = hold_at(200, 150, T0);
        want.push_back(act(Kind::RightUp, 200, 150, T0 + 520));
        expect(r.up(1, 200, 150, T0 + 520), want, "a lift at 520 ms, the last frame at 432: the hold at 450, then the right button goes up");
        Rig m(false);
        m.down(1, 200, 150, T0);
        frames(m, T0, T0 + 440);
        want = hold_at(200, 150, T0);
        want.push_back(act(Kind::Motion, 202, 151, T0 + 500));
        expect(m.move(1, 202, 151, T0 + 500), want, "a small move at 500 ms: the hold first, then the move");
    }
    {
        Rig r(false);                                                  // a frame that comes after a stall, the finger still down (no event came): a real hold, at its own time
        r.down(1, 200, 150, T0);
        r.tick(T0 + 16);
        expect(r.tick(T0 + 600), hold_at(200, 150, T0), "the finger is known to be down at the frame: the hold at 450");
    }
    {
        Rig r(false);                                                  // a second finger that lands late (stamped 570 ms after the last frame): the pair, never a hold that was due first
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 32);
        expect_none(r.down(2, 300, 150, T0 + 600), "the pair begins, nothing is sent (no hold, so no press to cancel)");
        check(r.touch.mode() == TouchControl::Mode::Two && r.touch.stats().holds == 0, "two fingers, no hold");
    }
    {
        Rig r(false);                                                  // the minimap: the left press is made at once; a lift that was stamped late is a plain release, not a hold's right click
        expect(r.down(1, 800, 50, T0), {act(Kind::Motion, 800, 50, T0), act(Kind::LeftDown, 800, 50, T0)}, "the press at once");
        frames(r, T0, T0 + 32);
        expect(r.up(1, 800, 50, T0 + 600), {act(Kind::LeftUp, 800, 50, T0 + 132)}, "a lift stamped 568 ms after the last frame: the left release");
        check(idle(r) && r.touch.stats().holds == 0, "no hold");
    }
    {
        Rig r(false);                                                  // late events do not carry the model forward: three of them, and the lift is still judged at 100 ms after the frame
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 32);
        expect_none(r.move(1, 203, 150, T0 + 300), "a late move within the slop");
        expect_none(r.move(1, 204, 151, T0 + 700), "another");
        expect_none(r.move(1, 205, 150, T0 + 1100), "and another");
        check(r.touch.mode() == TouchControl::Mode::Waiting && r.touch.stats().holds == 0, "still waiting");
        expect(r.up(1, 205, 150, T0 + 1500), click_at(200, 150, T0 + 132), "the lift is a tap at 132");
    }
    {
        Rig r(false);                                                  // a late event and then a frame with the finger still down: the frame is the evidence, and the hold fires
        r.down(1, 200, 150, T0);
        frames(r, T0, T0 + 32);
        expect_none(r.move(1, 203, 150, T0 + 700), "a late move: judged at 132, nothing");
        expect(r.tick(T0 + 716), hold_at(200, 150, T0), "the frame that follows it sees the finger down: the hold");
    }
    {
        Rig r(false);                                                  // across the wrap of the 32-bit clock
        const uint32_t t = 0xFFFFFFF0u;
        r.down(1, 200, 150, t);
        frames(r, t, t + 32);
        expect(r.up(1, 200, 150, t + 500), click_at(200, 150, t + 132), "a stalled tap across the wrap");
        Rig h(false);
        h.down(1, 200, 150, t);
        expect(frames(h, t, t + 500), hold_at(200, 150, t), "a real hold across the wrap");
        Rig w(false);                                                  // the last frame before the wrap, the lift after it, within the tolerance: believed
        w.down(1, 200, 150, t);
        w.tick(t + 10);
        expect(w.up(1, 200, 150, t + 80), click_at(200, 150, t + 80), "80 ms after a frame, over the wrap: the lift keeps its time");
        Rig p(false);                                           // the end of the tolerance falls after the wrap and the lift before it: believed (a comparison that ignores the wrap judges it at the end)
        const uint32_t u = 0xFFFFFFA0u;
        p.down(1, 200, 150, u);
        p.tick(u + 32);
        expect(p.up(1, 200, 150, u + 90), click_at(200, 150, u + 90), "a lift 58 ms after a frame, just before the wrap, the tolerance ending after it: it keeps its time");
    }
    {
        Rig r(false);                                                  // a clock that steps back does not pull the last known moment back
        r.down(1, 200, 150, T0 + 100);
        r.tick(T0);
        expect(r.up(1, 200, 150, T0 + 600), click_at(200, 150, T0 + 200), "an update that went back changed nothing: the lift is judged at 100 + 100");
        Rig s(false);
        s.down(1, 200, 150, T0);
        s.tick(T0 + 80);
        expect(s.up(1, 200, 150, T0 + 50), click_at(200, 150, T0 + 80), "a lift stamped before the last frame is at it");
    }
    {
        Rig r(false);                                                  // a new gesture has its own last known moment, not the cancelled one's
        r.down(1, 200, 150, T0);
        r.tick(T0 + 16);
        r.touch.cancel();
        r.down(1, 300, 200, T0 + 1000);
        expect(r.up(1, 300, 200, T0 + 1050), click_at(300, 200, T0 + 1050), "the tap of the next finger keeps its time");
        Rig s(false);                                                  // the same when the finger went down again without a lift in between
        s.down(1, 200, 150, T0);
        s.tick(T0 + 16);
        s.down(1, 300, 200, T0 + 1000);
        expect(s.up(1, 300, 200, T0 + 1050), click_at(300, 200, T0 + 1050), "a finger that is down again begins afresh");
    }
}

void test_late() {
    group("late", "the first action of a press that WAITED on the map view is marked late (the screen may have changed since); nothing else is");
    {
        Rig r;                                                  // a tap: the pointer's arrival is late, the two halves of the click after it are not
        r.down(1, 100, 100, T0);
        const auto out = r.up(1, 100, 100, T0 + 80);
        check(out.size() == 3 && out[0].late && !out[1].late && !out[2].late, "a tap: [late Motion, LeftDown, LeftUp]: " + show(out));
    }
    {
        Rig r;                                                  // a drag's start
        r.down(1, 100, 100, T0);
        const auto out = r.move(1, 150, 100, T0 + 80);
        check(out.size() == 3 && out[0].late && !out[1].late && !out[2].late, "a drag's start: the first action is late: " + show(out));
        const auto more = r.move(1, 160, 100, T0 + 90);
        check(more.size() == 1 && !more[0].late, "the moves after it are not");
        const auto end = r.up(1, 160, 100, T0 + 100);
        check(end.size() == 1 && !end[0].late, "nor is the release");
    }
    {
        Rig r;                                                  // a lift beyond the slop with no move
        r.down(1, 100, 100, T0);
        const auto out = r.up(1, 160, 100, T0 + 60);
        check(out.size() == 3 && out[0].late && !out[1].late && !out[2].late, "a far lift: the first action is late: " + show(out));
    }
    {
        Rig r;                                                  // a hold on the map view
        r.down(1, 100, 100, T0);
        const auto out = r.tick(T0 + 450);
        check(out.size() == 3 && out[0].late && !out[1].late && !out[2].late, "a hold: the pointer's arrival is late: " + show(out));
        const auto end = r.up(1, 100, 100, T0 + 600);
        check(end.size() == 1 && !end[0].late, "the right release is not");
    }
    {
        Rig r;                                                  // a press that was made at once (the minimap, a control) has nothing late: the game knows the finger
        const auto down = r.down(1, 800, 50, T0);
        check(down.size() == 2 && !down[0].late && !down[1].late, "a minimap press is made at once: nothing is late");
        const auto hold = r.tick(T0 + 450);
        check(hold.size() == 4 && !hold[0].late && !hold[1].late && !hold[2].late && !hold[3].late, "and neither is the hold that ends it and makes the right click: " + show(hold));
        Rig h;
        const auto control = h.down(1, 850, 300, T0);
        check(control.size() == 2 && !control[0].late && !control[1].late, "a control's press: nothing is late");
    }
    {
        Rig r;                                                  // the other actions (pan, zoom, cancel) have nothing to wait for
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        const auto out = framed_move(r, 2, 400, 200, T0 + 20);
        bool any_late = false;
        for (const TouchAction& a : out) any_late = any_late || a.late;
        check(!out.empty() && !any_late, "a pan and a zoom are never late");
        Rig c;
        c.down(1, 100, 100, T0);
        c.move(1, 150, 100, T0 + 20);
        const auto cancel = c.touch.cancel();
        check(cancel.size() == 1 && !cancel[0].late, "a cancel is never late");
    }
}

void test_ring() {
    group("ring", "the ring closes around a finger that may become a hold, and the pulse follows the hold");
    Rig r;
    check(!r.touch.ring(T0).has_value() && !r.touch.pulse(T0).has_value(), "no finger: no ring, no pulse");
    r.down(1, 300, 200, T0);
    check(!r.touch.ring(T0 + 149).has_value(), "149 ms: no ring yet (a tap never shows one)");
    const auto start = r.touch.ring(T0 + 150);
    check(start.has_value() && start->x == 300.0 && start->y == 200.0 && start->progress == 0.0, "150 ms: the ring begins at the down point");
    const auto half = r.touch.ring(T0 + 300);
    check(half.has_value() && std::fabs(half->progress - 0.5) < 1e-9, "300 ms: half closed");
    const auto late = r.touch.ring(T0 + 449);
    check(late.has_value() && late->progress > 0.99 && late->progress < 1.0, "449 ms: nearly closed");
    check(!r.touch.ring(T0 + 450).has_value(), "450 ms: the hold fired, the ring is gone");
    r.tick(T0 + 450);
    const auto pulse = r.touch.pulse(T0 + 450);
    check(pulse.has_value() && pulse->x == 300.0 && pulse->y == 200.0 && pulse->progress == 0.0, "the pulse begins when the hold fires, at the down point");
    const auto mid = r.touch.pulse(T0 + 450 + touch::kPulseMs / 2);
    check(mid.has_value() && std::fabs(mid->progress - 0.5) < 1e-9, "halfway through the pulse");
    check(!r.touch.pulse(T0 + 450 + touch::kPulseMs).has_value(), "the pulse is over after kPulseMs");
    check(!r.touch.ring(T0 + 600).has_value(), "no ring while the right button is held");
    r.up(1, 300, 200, T0 + 700);
    check(!r.touch.ring(T0 + 800).has_value(), "no ring after the lift");
    // a drag shows none: the finger moved
    Rig d;
    d.down(1, 300, 200, T0);
    d.move(1, 330, 200, T0 + 100);
    check(!d.touch.ring(T0 + 300).has_value(), "a finger that moved beyond the slop has no ring");
    // a finger that is not on the map view has none either
    Rig h;
    h.down(1, 850, 300, T0);
    check(!h.touch.ring(T0 + 300).has_value(), "a finger on the HUD has no ring");
    Rig m;
    m.down(1, 800, 50, T0);
    check(m.touch.ring(T0 + 300).has_value(), "a finger on the minimap has one (a hold there is a right click)");
}

// ---- the feedback's geometry (include/ants_app/touch_feedback.hpp) ----

using Paint = std::vector<TouchPaint>;
const ants::assets::ColorRGBA kProbe{1, 2, 3, 4};

/// The pixels of a window that a paint covers, counted (a pixel covered twice is an overlap); `ok` is false when a rectangle is not a span of one row or leaves the window
struct Coverage {
    int32_t x0{0};
    int32_t y0{0};
    int32_t size{0};
    std::vector<int> n;
    bool ok{true};
    Coverage(int32_t left, int32_t top, int32_t side) : x0(left), y0(top), size(side), n(static_cast<size_t>(side) * static_cast<size_t>(side), 0) {}
    int at(int32_t x, int32_t y) const { return n[static_cast<size_t>(y - y0) * static_cast<size_t>(size) + static_cast<size_t>(x - x0)]; }
    void add(const Paint& paint) {
        for (const TouchPaint& r : paint) {
            if (r.h != 1 || r.w < 1 || r.x < x0 || r.y < y0 || r.x + r.w > x0 + size || r.y >= y0 + size) {
                ok = false;
                continue;
            }
            for (int32_t x = r.x; x < r.x + r.w; ++x) ++n[static_cast<size_t>(r.y - y0) * static_cast<size_t>(size) + static_cast<size_t>(x - x0)];
        }
    }
};

struct Box {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};      // one past
    int32_t bottom{0};
    bool empty{true};
};

Box box_of(const Paint& paint) {
    Box b;
    for (const TouchPaint& r : paint) {
        if (b.empty) {
            b = Box{r.x, r.y, r.x + r.w, r.y + r.h, false};
            continue;
        }
        b.left = std::min(b.left, r.x);
        b.top = std::min(b.top, r.y);
        b.right = std::max(b.right, r.x + r.w);
        b.bottom = std::max(b.bottom, r.y + r.h);
    }
    return b;
}

/// The radius that a paint reaches: the half of its box, the box being centred on the mark
double reach_of(const Paint& paint) {
    const Box b = box_of(paint);
    return b.empty ? 0.0 : std::max(static_cast<double>(b.right - b.left), static_cast<double>(b.bottom - b.top)) / 2.0;
}

/// The (most common) alpha of the gold rows and of the dark ones
struct Alphas {
    int gold{-1};
    int edge{-1};
};

Alphas alphas_of(const Paint& paint) {
    Alphas a;
    for (const TouchPaint& r : paint) {
        if (r.color.r == 255 && r.color.g == 214 && r.color.b == 64) a.gold = r.color.a;
        else if (r.color.r == 24 && r.color.g == 16 && r.color.b == 0) a.edge = r.color.a;
        else a.gold = a.edge = -2;          // (a colour that is neither)
    }
    return a;
}

bool same_paint(const Paint& a, const Paint& b, int32_t dx = 0, int32_t dy = 0) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].x + dx != b[i].x || a[i].y + dy != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h) return false;
        if (a[i].color.r != b[i].color.r || a[i].color.g != b[i].color.g || a[i].color.b != b[i].color.b || a[i].color.a != b[i].color.a) return false;
    }
    return true;
}

void test_feedback() {
    group("feedback", "the ring and the pulse: an annulus is exactly the pixels whose centre lies in it, the sizes follow the slop, a closing ring and a growing pulse, nothing for a mark that is no position");
    // ---- the annulus: every pixel whose centre is from `inner` (included) to `outer` (not) away, once ----
    uint32_t seed = 4242;
    const auto unit = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double>(seed >> 8) / 16777216.0;
    };
    int wrong = 0;
    int overlapping = 0;
    int shapes = 0;
    size_t covered_total = 0;
    for (int n = 0; n < 500; ++n) {
        const double cx = unit() * 130.0 - 5.0;
        const double cy = unit() * 130.0 - 5.0;
        const double outer = 0.3 + unit() * 40.0;
        const double inner = n % 5 == 0 ? 0.0 : unit() * outer;
        Paint paint;
        touch_annulus(cx, cy, outer, inner, kProbe, paint);
        const int32_t side = static_cast<int32_t>(std::ceil(2.0 * outer)) + 8;
        const int32_t x_lo = static_cast<int32_t>(std::floor(cx - outer)) - 4;
        const int32_t y_lo = static_cast<int32_t>(std::floor(cy - outer)) - 4;
        Coverage cover(x_lo, y_lo, side);
        cover.add(paint);
        if (!cover.ok) ++shapes;
        for (int32_t y = y_lo; y < y_lo + side; ++y) {
            for (int32_t x = x_lo; x < x_lo + side; ++x) {
                const double dx = static_cast<double>(x) + 0.5 - cx;
                const double dy = static_cast<double>(y) + 0.5 - cy;
                const double d2 = dx * dx + dy * dy;
                const bool inside = d2 >= inner * inner && d2 < outer * outer;
                const int got = cover.at(x, y);
                if (got > 1) ++overlapping;
                if ((got == 1) != inside) ++wrong;
                covered_total += got == 1 ? 1u : 0u;
            }
        }
        for (const TouchPaint& r : paint) {
            if (r.color.r != 1 || r.color.g != 2 || r.color.b != 3 || r.color.a != 4) ++shapes;
        }
    }
    check(wrong == 0, "500 annuli (centres anywhere, fractions included, radii from a speck to 40): every pixel whose centre lies in the ring is covered and no other (" + std::to_string(wrong) + " wrong)");
    check(overlapping == 0, "no pixel is covered twice (the spans of a row never overlap)");
    check(shapes == 0, "every rectangle is one row high, wider than nothing, inside the window and in the colour that was given");
    check(covered_total > 100000, "(the sweep has teeth: " + std::to_string(covered_total) + " covered pixels in all)");
    {
        Paint p;
        touch_annulus(10.5, 10.5, 5.0, 3.0, kProbe, p);              // pixel centres exactly 3 and 5 away exist here: the inner circle's pixels are in, the outer's are out
        Coverage c(0, 0, 24);
        c.add(p);
        check(c.at(10, 8) == 0 && c.at(10, 7) == 1 && c.at(10, 6) == 1 && c.at(10, 5) == 0 && c.at(5, 10) == 0 && c.at(6, 10) == 1 && c.at(7, 10) == 1 && c.at(8, 10) == 0 && c.at(10, 10) == 0,
              "a ring about a pixel's middle with pixels exactly 3 and 5 away: the one 3 away is in (the inner edge is included), the one 5 away is out (the outer edge is not)");
    }
    // ---- what is nothing ----
    {
        Paint p;
        touch_annulus(50, 50, 5.0, 5.0, kProbe, p);
        touch_annulus(50, 50, 4.0, 5.0, kProbe, p);
        touch_annulus(50, 50, 0.0, 0.0, kProbe, p);
        touch_annulus(50, 50, -3.0, 0.0, kProbe, p);
        touch_annulus(std::nan(""), 50, 5.0, 0.0, kProbe, p);
        touch_annulus(50, std::numeric_limits<double>::infinity(), 5.0, 0.0, kProbe, p);
        touch_annulus(50, 50, std::nan(""), 0.0, kProbe, p);
        touch_annulus(50, 50, 5.0, std::nan(""), kProbe, p);
        check(p.empty(), "an annulus that has no width, a radius that is not positive and positions or radii that are not numbers give nothing");
        Paint wide;
        touch_annulus(50, 50, 1.0e9, 0.0, kProbe, wide);
        check(!wide.empty() && wide.size() < 10000, "a radius of a billion is cut to a size that a frame can draw (" + std::to_string(wide.size()) + " rectangles)");
        Paint far;
        touch_annulus(1.0e12, -1.0e12, 5.0, 0.0, kProbe, far);
        const Box fb = box_of(far);
        check(!fb.empty && fb.right <= 1001000 && fb.left >= 999000 && fb.bottom <= -999000 + 100 && fb.top >= -1001000, "a position that is far away is kept inside the 32 bits");
        Paint negative;
        touch_annulus(50, 50, 4.0, -7.0, kProbe, negative);
        Paint solid;
        touch_annulus(50, 50, 4.0, 0.0, kProbe, solid);
        check(same_paint(negative, solid), "a negative inner radius is a disc");
    }
    // ---- the sizes follow the slop ----
    check(std::fabs(touch::ring_end_radius(9.6) - 48.0) < 1e-9 && std::fabs(touch::ring_start_radius(9.6) - 76.8) < 1e-9 && std::fabs(touch::ring_stroke(9.6) - 3.84) < 1e-9,
          "a slop of 9.6 picture pixels (8 CSS pixels of a box shown at 1.2): the ring ends at 48, starts at 76.8 and is 3.84 thick (5, 8 and 0.4 slops)");
    check(touch::ring_end_radius(1.0) == 14.0 && touch::ring_start_radius(1.0) == 24.0 && touch::ring_stroke(1.0) == 3.0, "the smallest slop: 14 and 24 and 3 (a speck is not a ring)");
    check(touch::ring_end_radius(64.0) == 100.0 && touch::ring_start_radius(64.0) == 160.0 && touch::ring_stroke(64.0) == 8.0, "the largest slop: 100 and 160 and 8 (a screen-sized ring is not a ring)");
    bool monotone = true;
    bool gap = true;
    for (double s = 1.0; s <= 64.0; s += 0.25) {
        monotone = monotone && touch::ring_end_radius(s + 0.25) >= touch::ring_end_radius(s) && touch::ring_start_radius(s + 0.25) >= touch::ring_start_radius(s) && touch::ring_stroke(s + 0.25) >= touch::ring_stroke(s);
        gap = gap && touch::ring_start_radius(s) >= touch::ring_end_radius(s) + 10.0 - 1e-9;
    }
    check(monotone, "a bigger slop never makes a smaller ring");
    check(gap, "the ring always starts at least 10 pixels wider than it ends (it closes: every slop)");
    check(touch::kRingEndSlops == 5.0 && touch::kRingStartSlops == 8.0 && touch::kRingStrokeSlops == 0.4 && touch::kPulseGrowth == 0.8, "the proportions are 5 and 8 slops, a line of 0.4 and a pulse that grows by 0.8 of the end radius");

    // ---- the ring closes ----
    const double slop = 9.6;
    const TouchControl::Mark at0{100.5, 80.5, 0.0};
    const TouchControl::Mark at1{100.5, 80.5, 1.0};
    const Paint r0 = touch_ring_paint(at0, slop);
    const Paint r1 = touch_ring_paint(at1, slop);
    check(!r0.empty() && !r1.empty(), "a ring at the start and one at the end are painted");
    check(std::fabs(reach_of(r0) - 77.8) <= 1.0, "the ring starts at 76.8 (and a dark pixel more): " + std::to_string(reach_of(r0)));
    check(std::fabs(reach_of(r1) - 49.0) <= 1.0, "it ends at 48 (and a dark pixel more): " + std::to_string(reach_of(r1)));
    double last = 1.0e9;
    bool closing = true;
    bool brightening = true;
    int last_alpha = -1;
    for (int i = 0; i <= 20; ++i) {
        const Paint p = touch_ring_paint(TouchControl::Mark{100.5, 80.5, i / 20.0}, slop);
        const double reach = reach_of(p);
        closing = closing && reach <= last;
        last = reach;
        const Alphas a = alphas_of(p);
        brightening = brightening && a.gold >= last_alpha && a.gold > 0 && a.edge > 0 && a.edge < a.gold;
        last_alpha = a.gold;
    }
    check(closing, "the ring only closes as the progress goes from 0 to 1");
    check(brightening, "and brightens (the gold's alpha never falls; the dark edge is fainter than the gold)");
    check(alphas_of(r0).gold == 120 && alphas_of(r1).gold == 235, "the gold's alpha is 120 at the start and 235 at the end");
    {
        const Alphas a = alphas_of(touch_ring_paint(TouchControl::Mark{100.5, 80.5, 0.5}, slop));
        check(a.gold == 178 && a.edge == 133, "halfway: 178 for the gold (177.5 rounded) and three quarters of 177.5 for the dark edge: " + std::to_string(a.gold) + ", " + std::to_string(a.edge));
    }
    check(same_paint(touch_ring_paint(TouchControl::Mark{100.5, 80.5, -3.0}, slop), r0) && same_paint(touch_ring_paint(TouchControl::Mark{100.5, 80.5, 7.0}, slop), r1), "a progress outside 0 .. 1 is the nearest end");
    // the colours: gold and a dark edge, nothing else; the hole is empty
    {
        bool colours = true;
        for (const TouchPaint& r : r1) {
            const bool is_gold = r.color.r == 255 && r.color.g == 214 && r.color.b == 64;
            const bool is_edge = r.color.r == 24 && r.color.g == 16 && r.color.b == 0;
            colours = colours && (is_gold || is_edge);
        }
        check(colours, "gold (255, 214, 64) and a dark edge (24, 16, 0), nothing else");
        Coverage c(0, 0, 200);
        c.add(r1);
        int inside = 0;
        for (int32_t y = 0; y < 200; ++y) {
            for (int32_t x = 0; x < 200; ++x) {
                const double d = std::hypot(static_cast<double>(x) + 0.5 - 100.5, static_cast<double>(y) + 0.5 - 80.5);
                if (d < 48.0 - 3.84 - 1.0 - 1e-6 && c.at(x, y) != 0) ++inside;
            }
        }
        check(c.ok && inside == 0, "nothing is painted inside the ring (the finger is seen, not covered)");
        // the line is 3.84 thick: a row through the middle shows the gold across about that
        int gold_across = 0;
        for (const TouchPaint& r : r1) {
            if (r.y == 80 && r.color.r == 255 && r.x < 100) gold_across += r.w;
        }
        check(gold_across >= 3 && gold_across <= 5, "the gold line is 3 or 4 pixels thick on a row through the middle (3.84): " + std::to_string(gold_across));
        // across the line, from the outside in: a dark pixel, the gold, a dark pixel (it is seen on grass, sand and rock alike)
        std::vector<std::pair<int32_t, char>> row;
        for (const TouchPaint& r : r1) {
            if (r.y == 80 && r.x < 100) row.emplace_back(r.x, r.color.r == 255 ? 'g' : 'e');
        }
        std::sort(row.begin(), row.end());
        std::string across;
        for (const auto& piece : row) {
            if (across.empty() || across.back() != piece.second) across += piece.second;
        }
        check(across == "ege", "across the ring on a row through its middle the rectangles are dark, gold, dark (" + across + ")");
    }
    // it moves with the mark and does not change shape, and a pixel-centred mark makes a symmetric ring
    {
        const Paint moved = touch_ring_paint(TouchControl::Mark{107.5, 77.5, 0.3}, slop);
        const Paint here = touch_ring_paint(TouchControl::Mark{100.5, 80.5, 0.3}, slop);
        check(same_paint(here, moved, 7, -3), "a ring moved by whole pixels is the same ring moved");
        Coverage c(0, 0, 220);
        c.add(here);
        bool mirrored = true;
        for (int32_t y = 0; y < 220; ++y) {
            for (int32_t x = 0; x < 220; ++x) {
                const int32_t mx = 200 - x;                           // about the pixel 100 (centre 100.5): the pixel 100 + k and 100 - k
                const int32_t my = 160 - y;
                if (mx < 0 || mx >= 220 || my < 0 || my >= 220) continue;
                if (c.at(x, y) != c.at(mx, y) || c.at(x, y) != c.at(x, my)) mirrored = false;
            }
        }
        check(mirrored, "a ring about a pixel's middle is the same left and right and above and below");
    }
    // the number of rectangles that a frame has to draw
    check(r0.size() < 1200 && r1.size() < 800, "a ring is some hundreds of rectangles (" + std::to_string(r0.size()) + " at the start, " + std::to_string(r1.size()) + " at the end)");
    check(touch_ring_paint(TouchControl::Mark{100.5, 80.5, 0.0}, 64.0).size() < 2600 && touch_ring_paint(TouchControl::Mark{100.5, 80.5, 0.0}, 1.0).size() < 400, "and at the largest slop under two and a half thousand");

    // ---- the pulse goes out ----
    const Paint p0 = touch_pulse_paint(at0, slop);
    check(same_paint(p0, r1), "the pulse begins where the ring ended: the same circle, the same brightness");
    double reach_before = 0.0;
    bool growing = true;
    bool fading = true;
    int alpha_before = 1000;
    for (int i = 0; i <= 18; ++i) {
        const Paint p = touch_pulse_paint(TouchControl::Mark{100.5, 80.5, i / 20.0}, slop);
        if (p.empty()) {
            fading = false;
            break;
        }
        growing = growing && reach_of(p) >= reach_before;
        reach_before = reach_of(p);
        const Alphas a = alphas_of(p);
        fading = fading && a.gold <= alpha_before;
        alpha_before = a.gold;
    }
    check(growing && fading, "the pulse only grows and fades");
    check(std::fabs(reach_of(touch_pulse_paint(TouchControl::Mark{100.5, 80.5, 0.5}, slop)) - (48.0 * 1.4 + 1.0)) <= 1.0, "halfway it is at 1.4 times the end radius (the growth is 0.8, linear)");
    check(touch_pulse_paint(at1, slop).empty(), "at the end of the pulse nothing is left to draw");
    check(alphas_of(touch_pulse_paint(TouchControl::Mark{100.5, 80.5, 0.5}, slop)).gold == 59, "halfway the gold's alpha is a quarter of 235 (the fade is quadratic): " + std::to_string(alphas_of(touch_pulse_paint(TouchControl::Mark{100.5, 80.5, 0.5}, slop)).gold));
    check(same_paint(touch_pulse_paint(TouchControl::Mark{100.5, 80.5, -2.0}, slop), p0), "a progress under 0 is the start of the pulse");

    // ---- what is no mark ----
    const double nan = std::nan("");
    check(touch_ring_paint(TouchControl::Mark{nan, 80.5, 0.5}, slop).empty() && touch_ring_paint(TouchControl::Mark{100.5, nan, 0.5}, slop).empty() && touch_ring_paint(TouchControl::Mark{100.5, 80.5, nan}, slop).empty() &&
              touch_ring_paint(TouchControl::Mark{100.5, 80.5, 0.5}, nan).empty() && touch_ring_paint(TouchControl::Mark{100.5, 80.5, 0.5}, std::numeric_limits<double>::infinity()).empty(),
          "a ring for a position, a progress or a slop that is not a number is nothing");
    check(touch_pulse_paint(TouchControl::Mark{nan, 80.5, 0.5}, slop).empty() && touch_pulse_paint(TouchControl::Mark{100.5, 80.5, nan}, slop).empty() && touch_pulse_paint(TouchControl::Mark{100.5, 80.5, 0.5}, nan).empty(),
          "so is a pulse");
}

void test_drag() {
    group("drag", "a finger that leaves the slop is a left drag: the press at the origin first, then the motion");
    {
        Rig r;
        r.down(1, 100, 100, T0);
        expect_none(r.move(1, 108, 100, T0 + 30), "8 px: still within the slop");
        expect(r.move(1, 115, 100, T0 + 60), {act(Kind::Motion, 100, 100, T0 + 60), act(Kind::LeftDown, 100, 100, T0 + 60), act(Kind::Motion, 115, 100, T0 + 60)},
               "15 px: the left button goes down at the ORIGIN first, then the pointer goes where the finger is");
        check(r.touch.mode() == TouchControl::Mode::Left && r.touch.stats().drags == 1, "it is a left drag");
        expect(r.move(1, 140, 120, T0 + 90), {act(Kind::Motion, 140, 120, T0 + 90)}, "more of the drag: the pointer follows");
        expect(r.move(1, 100, 100, T0 + 120), {act(Kind::Motion, 100, 100, T0 + 120)}, "back at the origin it is still a drag (it never becomes a tap)");
        expect_none(r.tick(T0 + 5000), "a drag never becomes a hold, however long it lasts");
        expect(r.up(1, 160, 130, T0 + 5100), {act(Kind::LeftUp, 160, 130, T0 + 5100)}, "the lift lets the button go where the finger is");
        check(idle(r), "afterwards nothing is tracked");
    }
    {
        Rig r;                                                  // the slop is exact: the distance itself is within it, the least more is out
        r.down(1, 100, 100, T0);
        expect_none(r.move(1, 106, 108, T0 + 10), "distance exactly 10 (6, 8): within the slop");
        const auto out = r.move(1, 106.2, 108.0, T0 + 20);
        check(out.size() == 3 && out[1].kind == Kind::LeftDown, "distance a little over 10: a drag: " + show(out));
        Rig s;                                                  // ... in every direction (negative too)
        s.down(1, 100, 100, T0);
        expect_none(s.move(1, 90, 100, T0 + 10), "10 px to the left: within");
        check(s.move(1, 89.9, 100, T0 + 20).size() == 3, "10.1 px to the left: a drag");
        Rig u;
        u.down(1, 100, 100, T0);
        expect_none(u.move(1, 100, 90, T0 + 10), "10 px up: within");
        check(u.move(1, 100, 89.5, T0 + 20).size() == 3, "10.5 px up: a drag");
    }
    {
        Rig r;                                                  // it leaves the slop after the tap time but before the hold time: a drag; after the hold time the hold came first (see "hold")
        r.down(1, 100, 100, T0);
        const auto out = r.move(1, 130, 100, T0 + 449);
        check(out.size() == 3 && out[1].kind == Kind::LeftDown && out[1].x == 100, "449 ms: a drag from the origin: " + show(out));
    }
    {
        Rig r;                                                  // a lift that is beyond the slop though no move said so: a drag that ends at once, press at the origin first
        r.down(1, 100, 100, T0);
        expect(r.up(1, 160, 100, T0 + 60), {act(Kind::Motion, 100, 100, T0 + 60), act(Kind::LeftDown, 100, 100, T0 + 60), act(Kind::LeftUp, 160, 100, T0 + 60)},
               "down and a far lift: press at the origin, release at the lift");
        check(r.touch.stats().drags == 1 && r.touch.stats().taps == 0, "counted as a drag");
    }
    {
        Rig r;                                                  // the drag begins when the finger has left the slop, not before: a long wobble within it is no drag
        r.down(1, 100, 100, T0);
        for (int i = 0; i < 20; ++i) expect_none(r.move(1, 100 + (i % 2 ? 6 : -6), 100 + (i % 3) * 2, T0 + 10u * static_cast<uint32_t>(i)), "wobble " + std::to_string(i));
        check(r.touch.mode() == TouchControl::Mode::Waiting, "still waiting after twenty small moves");
    }
}

void test_other_places() {
    group("direct", "on the minimap and everywhere else the left button goes down at once; a minimap press that holds still becomes a right click there");
    {
        Rig r;                                                  // the HUD (right of the map view and not on the minimap), a dialog, another screen: a mouse until the lift
        expect(r.down(1, 850, 300, T0), {act(Kind::Motion, 850, 300, T0), act(Kind::LeftDown, 850, 300, T0)}, "a finger on the HUD: the pointer arrives and the left button goes down at once");
        check(r.touch.mode() == TouchControl::Mode::Left, "it is a left press");
        expect(r.move(1, 852, 301, T0 + 10), {act(Kind::Motion, 852, 301, T0 + 10)}, "a move follows");
        expect_none(r.tick(T0 + 5000), "it never becomes a hold");
        expect(r.up(1, 852, 301, T0 + 5001), {act(Kind::LeftUp, 852, 301, T0 + 5001)}, "the lift lets the button go");
        check(r.touch.stats().taps == 0 && idle(r), "no tap was counted");
        Rig quick;                                              // a quick tap on a button is the press and the release at once, as the emulation gave it
        quick.down(1, 850, 300, T0);
        expect(quick.up(1, 850, 300, T0 + 60), {act(Kind::LeftUp, 850, 300, T0 + 60)}, "a tap on a control releases where the finger is");
        Rig dialog;
        dialog.env.everything_direct = true;                    // (a dialog is over the map view: the application says so)
        expect(dialog.down(1, 100, 100, T0), {act(Kind::Motion, 100, 100, T0), act(Kind::LeftDown, 100, 100, T0)}, "with a dialog over the map view even a point of the map is a mouse press");
    }
    {
        Rig r;
        expect(r.down(1, 800, 50, T0), {act(Kind::Motion, 800, 50, T0), act(Kind::LeftDown, 800, 50, T0)}, "the minimap: the press is made at once (it scrolls)");
        check(r.touch.mode() == TouchControl::Mode::MinimapWait, "it is a minimap press that may become a hold");
        expect(r.move(1, 803, 52, T0 + 40), {act(Kind::Motion, 803, 52, T0 + 40)}, "a small move follows");
        expect_none(r.tick(T0 + 449), "one millisecond before the hold time: nothing");
        expect(r.tick(T0 + 450), {act(Kind::LeftUp, 800, 50, T0 + 450), act(Kind::Motion, 800, 50, T0 + 450), act(Kind::RightDown, 800, 50, T0 + 450), act(Kind::HoldFired, 800, 50, T0 + 450)},
               "the hold: the left press ends, the right button goes down at the down point: send the selected ants there");
        expect(r.up(1, 801, 51, T0 + 900), {act(Kind::RightUp, 801, 51, T0 + 900)}, "the lift is the right button's release");
        check(idle(r) && r.touch.stats().holds == 1, "done");
    }
    {
        Rig r;                                                  // a short press on the minimap is the left button, up where the finger lifts
        r.down(1, 800, 50, T0);
        expect(r.up(1, 801, 50, T0 + 200), {act(Kind::LeftUp, 801, 50, T0 + 200)}, "a short minimap press ends with the left button's release");
    }
    {
        Rig r;                                                  // a minimap finger that moves beyond the slop is scrolling the map, never a hold
        r.down(1, 800, 50, T0);
        expect(r.move(1, 830, 60, T0 + 100), {act(Kind::Motion, 830, 60, T0 + 100)}, "a move beyond the slop: the pointer follows");
        check(r.touch.mode() == TouchControl::Mode::Left, "it is a drag of the minimap now");
        expect_none(r.tick(T0 + 5000), "no hold, however long");
        expect(r.up(1, 830, 60, T0 + 5001), {act(Kind::LeftUp, 830, 60, T0 + 5001)}, "the lift is the left release");
        // ... and a move out and back within the slop before the time is still the scroll
        Rig b;
        b.down(1, 800, 50, T0);
        b.move(1, 840, 50, T0 + 100);
        b.move(1, 800, 50, T0 + 200);
        expect_none(b.tick(T0 + 600), "back at the down point it is still no hold");
    }
}

void test_second_finger() {
    group("two", "the second finger: what it does to the first finger's gesture, and where it does nothing");
    {
        Rig r;                                                  // a finger that waits: dropped silently (no click)
        r.down(1, 300, 200, T0);
        r.tick(T0 + 100);
        expect_none(r.down(2, 400, 200, T0 + 120), "a second finger while the first waits: nothing is sent, nothing was held");
        check(r.touch.mode() == TouchControl::Mode::Two && r.touch.fingers() == 2 && r.touch.stats().two_finger == 1, "two fingers: pan and pinch");
        check(r.env.two_asked == 1 && std::fabs(r.env.two_x - 350.0) < 1e-9 && std::fabs(r.env.two_y - 200.0) < 1e-9, "the gate was asked once, with the middle point (350, 200)");
        expect_none(r.up(2, 400, 200, T0 + 200), "the second finger lifts: nothing");
        expect_none(r.up(1, 300, 200, T0 + 210), "the first lifts: no click (the tap was dropped when the second landed)");
        check(idle(r) && r.touch.stats().taps == 0, "all gone, no tap counted");
    }
    {
        Rig r;                                                  // a drag: the band is cancelled, with no selection (the release is never sent)
        r.down(1, 300, 200, T0);
        r.move(1, 340, 210, T0 + 50);
        const auto landed = r.down(2, 450, 210, T0 + 80);
        expect(landed, {act(Kind::Cancel, 340, 210, T0 + 80)}, "the second finger lands during a drag: the press is cancelled, where the first finger is");
        check(landed.size() == 1 && !landed[0].right, "it is the left button's press that ends");
        expect_none(r.up(1, 340, 210, T0 + 400), "the lift of the first finger sends no release");
        expect_none(r.up(2, 450, 210, T0 + 410), "nor does the second");
    }
    {
        Rig r;                                                  // a hold: the right button's press is cancelled, with no order
        r.down(1, 300, 200, T0);
        r.tick(T0 + 450);
        const auto landed = r.down(2, 450, 210, T0 + 700);
        expect(landed, {act(Kind::Cancel, 300, 200, T0 + 700)}, "the second finger lands during a hold: the press is cancelled");
        check(landed.size() == 1 && landed[0].right, "it is the right button's press that ends");
        expect_none(r.up(1, 300, 200, T0 + 900), "the lift sends no RightUp (no order)");
        expect_none(r.up(2, 450, 210, T0 + 910), "nor the second");
    }
    {
        Rig r;                                                  // a hold that was due when the second finger landed fires first, then is cancelled: in this order
        r.down(1, 300, 200, T0);
        expect(r.down(2, 450, 210, T0 + 500), {act(Kind::Motion, 300, 200, T0 + 450), act(Kind::RightDown, 300, 200, T0 + 450), act(Kind::HoldFired, 300, 200, T0 + 450), act(Kind::Cancel, 300, 200, T0 + 500)},
               "the hold was due: it fires, and the second finger cancels it (a press that began is always ended)");
    }
    {
        Rig r;                                                  // not allowed: the second finger does nothing, the first goes on
        r.env.allow_two = false;
        r.down(1, 300, 200, T0);
        expect_none(r.down(2, 400, 200, T0 + 50), "a second finger where two fingers are not allowed: nothing");
        check(r.touch.mode() == TouchControl::Mode::Waiting && r.touch.fingers() == 2 && r.touch.ignored() == 1, "the first finger waits as before; the second is ignored");
        expect_none(r.move(2, 450, 260, T0 + 60), "a move of the ignored finger: nothing");
        expect(r.up(1, 300, 200, T0 + 100), {act(Kind::Motion, 300, 200, T0 + 100), act(Kind::LeftDown, 300, 200, T0 + 100), act(Kind::LeftUp, 300, 200, T0 + 100)}, "the first finger's tap is still a click");
        expect_none(r.up(2, 450, 260, T0 + 150), "the ignored finger's lift: nothing");
        check(idle(r), "all gone");
        // the same with the hold: the hold still fires for the first finger
        Rig h;
        h.env.allow_two = false;
        h.down(1, 300, 200, T0);
        h.down(2, 400, 200, T0 + 50);
        expect(h.tick(T0 + 450), {act(Kind::Motion, 300, 200, T0 + 450), act(Kind::RightDown, 300, 200, T0 + 450), act(Kind::HoldFired, 300, 200, T0 + 450)}, "the first finger's hold fires with a second finger resting");
        // a refused second finger that lifts, and a later one that is allowed: the pair begins then
        Rig later;
        later.env.allow_two = false;
        later.down(1, 300, 200, T0);
        later.down(2, 400, 200, T0 + 50);
        later.up(2, 400, 200, T0 + 100);
        later.env.allow_two = true;
        expect_none(later.down(3, 420, 200, T0 + 150), "a new finger after the refused one has gone");
        check(later.touch.mode() == TouchControl::Mode::Two, "the gate said yes this time: a pair");
    }
    {
        Rig r;                                                  // a finger that started on the minimap or the HUD never pairs, and the gate is not even asked
        r.down(1, 800, 50, T0);
        expect_none(r.down(2, 300, 200, T0 + 50), "a second finger while the first holds the minimap: nothing");
        check(r.env.two_asked == 0 && r.touch.mode() == TouchControl::Mode::MinimapWait && r.touch.ignored() == 1, "the gate was not asked; the minimap press goes on");
        Rig h;
        h.down(1, 850, 300, T0);
        expect_none(h.down(2, 300, 200, T0 + 50), "a second finger while the first holds a control: nothing");
        check(h.env.two_asked == 0 && h.touch.mode() == TouchControl::Mode::Left, "the gate was not asked");
        Rig held;                                               // the minimap hold (a right button) is not a map gesture either
        held.down(1, 800, 50, T0);
        held.tick(T0 + 450);
        expect_none(held.down(2, 300, 200, T0 + 500), "a second finger during a minimap hold: nothing");
        check(held.touch.mode() == TouchControl::Mode::Right, "the right press goes on");
        expect(held.up(1, 800, 50, T0 + 600), {act(Kind::RightUp, 800, 50, T0 + 600)}, "and ends at the lift");
    }
    {
        Rig r;                                                  // the gate is asked with the middle point at the second finger's landing and no other time
        r.down(1, 100, 100, T0);
        r.down(2, 300, 500, T0 + 10);
        check(r.env.two_asked == 1 && r.env.two_x == 200.0 && r.env.two_y == 300.0, "asked once with (200, 300)");
        r.move(1, 120, 100, T0 + 20);
        r.move(2, 320, 500, T0 + 30);
        r.down(3, 10, 10, T0 + 40);
        check(r.env.two_asked == 1, "never asked again: moves and a third finger do not ask");
    }
}

void test_pan() {
    group("pan", "two fingers: the map follows the middle point, in whole pixels, with nothing lost");
    {
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);                           // middle (250, 200), distance 100
        expect(framed_move(r, 1, 210, 200, T0 + 20), {pan(5, 0, T0 + 20)}, "one finger 10 px right: the middle moved 5");
        expect(framed_move(r, 2, 310, 200, T0 + 30), {pan(5, 0, T0 + 30)}, "the other 10 px right: 5 more (the distance is the same: no zoom)");
        expect(framed_move(r, 1, 210, 230, T0 + 40), {pan(0, 15, T0 + 40)}, "one finger 30 px down: the middle moved 15 down (the distance changed a little: no level yet)");
        const auto both = framed_move(r, 2, 316, 210, T0 + 50);
        check(both.size() == 1 && both[0].kind == Kind::Pan && both[0].dx == 3 && both[0].dy == 5, "the other finger (+6, +10): the middle moved (3, 5): " + show(both));
        expect_none(framed_move(r, 1, 210, 230, T0 + 60), "a move that does not move the middle point: nothing");
    }
    {
        Rig r;                                                  // the sum of the pans is the middle point's total movement, rounded: nothing drifts (a finger that creeps 0.3 px a step)
        r.down(1, 200, 200, T0);
        r.down(2, 400, 200, T0 + 10);
        int total = 0;
        double x = 200.0;
        for (int i = 0; i < 100; ++i) {
            x += 0.3;
            for (const TouchAction& a : framed_move(r, 1, x, 200, T0 + 20 + static_cast<uint32_t>(i))) {
                if (a.kind == Kind::Pan) total += a.dx;
            }
        }
        check(total == 15, "100 moves of 0.3 px of one finger (the middle: 15 px in all) pan 15 in all, got " + std::to_string(total));
    }
    {
        Rig r;                                                  // back and forth: the sum returns to 0
        r.down(1, 200, 200, T0);
        r.down(2, 400, 200, T0 + 10);
        int sum_x = 0;
        int sum_y = 0;
        const double steps[][2] = {{40, 10}, {-70, 33}, {5, -90}, {31, 47}, {-5, 12}};
        double ax = 200.0;
        double ay = 200.0;
        for (const auto& s : steps) {
            ax += s[0];
            ay += s[1];
            for (const TouchAction& a : framed_move(r, 1, ax, ay, T0 + 20)) {
                if (a.kind == Kind::Pan) {
                    sum_x += a.dx;
                    sum_y += a.dy;
                }
            }
        }
        check(sum_x == 1 && sum_y == 6, "the pans add up to the middle point's movement, (201 - 200) / 2 = 0.5 rounded and (212 - 200) / 2 = 6: " + std::to_string(sum_x) + ", " + std::to_string(sum_y));
    }
    {
        Rig r;                                                  // the pan carries the middle point (for whoever draws or logs it)
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        const auto out = framed_move(r, 2, 330, 200, T0 + 20);
        check(!out.empty() && out[0].kind == Kind::Pan && out[0].x == 265 && out[0].y == 200, "the pan's point is the middle point (265, 200): " + show(out));
    }
}

bool has(const TouchControl::Actions& v, Kind kind) {
    for (const TouchAction& a : v) {
        if (a.kind == kind) return true;
    }
    return false;
}

void test_pair_per_frame() {
    group("frame", "a pair is judged once per frame: the moves of its fingers say nothing, update() does; a lift, a landing and a cancel see what came before");
    {   // two fingers that move together are two events: judged one at a time the distance is wrong between them
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        expect_none(r.move(1, 230, 200, T0 + 20), "a finger of the pair moves: nothing yet");
        expect_none(r.move(2, 330, 200, T0 + 20), "the other one moves: nothing yet");
        expect(r.tick(T0 + 20), {pan(30, 0, T0 + 20)}, "the frame: ONE pan of 30 and no zoom (the distance is what it was: neither event alone showed that)");
        expect_none(r.tick(T0 + 36), "nothing moved since: nothing");
        expect_none(r.tick(T0 + 52), "and again");
    }
    {   // the reason: the first finger's move judged alone is a pinch to 0.7 of the distance, a level out, and the second one's undoes it
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        const auto alone = framed_move(r, 1, 230, 200, T0 + 20);
        check(has(alone, Kind::Zoom), "(the move of one finger alone, judged at once, would zoom out: that is why the pair waits for the frame)");
    }
    {   // a lift: the last moves of the pair count before the pair ends
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        r.move(1, 230, 200, T0 + 20);
        r.move(2, 330, 200, T0 + 20);
        expect(r.up(1, 230, 200, T0 + 25), {pan(30, 0, T0 + 25)}, "a finger lifts before any frame saw its move: the pair's last pan comes first");
        expect_none(r.tick(T0 + 40), "and then the pair is over");
    }
    {   // a landing (a third finger): the same
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        r.move(1, 230, 200, T0 + 20);
        r.move(2, 330, 200, T0 + 20);
        expect(r.down(3, 500, 400, T0 + 25), {pan(30, 0, T0 + 25)}, "a third finger lands before any frame saw the moves: they count first");
        expect_none(r.tick(T0 + 40), "nothing is left over");
    }
    {   // a cancel judges nothing: what the pair did since the last frame is lost with it
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        r.move(1, 230, 200, T0 + 20);
        r.move(2, 330, 200, T0 + 20);
        expect_none(r.touch.cancel(), "a cancel after moves that no frame saw: nothing");
        expect_none(r.tick(T0 + 40), "and no frame brings them back");
    }
    {   // an ignored finger's moves are no move of the pair
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        r.down(3, 500, 400, T0 + 20);
        r.move(3, 520, 420, T0 + 30);
        expect_none(r.tick(T0 + 40), "the third finger moved: the pair did not");
    }
    {   // one finger rests and the other moves (a pinch with a thumb that stays): the frame judges that
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        r.move(2, 340, 200, T0 + 20);
        const auto out = r.tick(T0 + 20);
        check(out.size() == 2 && out[0].kind == Kind::Pan && out[0].dx == 20 && out[1].kind == Kind::Zoom, "one finger rests, the other goes 40 px out: the pan of the middle (20) and a level in: " + show(out));
    }
    {   // the pair's own frames: every frame judges what moved since the one before
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        int total = 0;
        for (int i = 1; i <= 5; ++i) {
            r.move(1, 200.0 + 6.0 * i, 200.0, T0 + 20u * static_cast<uint32_t>(i));
            r.move(2, 300.0 + 6.0 * i, 200.0, T0 + 20u * static_cast<uint32_t>(i));
            for (const TouchAction& a : r.tick(T0 + 20u * static_cast<uint32_t>(i))) {
                check(a.kind == Kind::Pan && a.dx == 6, "frame " + std::to_string(i) + ": a pan of 6 and nothing else: " + show(a));
                total += a.dx;
            }
        }
        check(total == 30, "five frames of 6: 30 in all");
    }
}

/// The ratio at which a pinch changes level, written from the definition: (0.5 + 0.15) of the way to the next level, in ratio
double threshold_ratio(double from, double to) { return std::pow(to / from, 0.65); }

void test_pinch() {
    group("pinch", "two fingers: the distance's ratio picks a level, with hysteresis, anchored at the middle point");
    const std::vector<float> levels = Env().list;
    check(levels.size() >= 9 && levels.front() == 2.0f && levels[4] == 1.0f && levels[3] > 1.0f && levels[5] < 1.0f, "the application's own levels are used, from the most zoomed in down: 2 ... 1 ... smaller");
    const double up_ratio = threshold_ratio(1.0, static_cast<double>(levels[3]));         // 1 -> 1.19
    const double down_ratio = threshold_ratio(1.0, static_cast<double>(levels[5]));       // 1 -> 0.84
    check(up_ratio > 1.11 && up_ratio < 1.13 && down_ratio > 0.89 && down_ratio < 0.90, "(the thresholds from the definition: " + std::to_string(up_ratio) + " and " + std::to_string(down_ratio) + ")");
    {
        Rig r;                                                  // fingers 100 apart, the middle (250, 200), at the zoom 1
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        TouchControl::Actions out = framed_move(r, 2, 200.0 + 100.0 * (up_ratio - 0.01), 200, T0 + 20);
        bool zoomed = false;
        for (const TouchAction& a : out) zoomed = zoomed || a.kind == Kind::Zoom;
        check(!zoomed, "a spread just under the threshold: no level");
        out = framed_move(r, 2, 200.0 + 100.0 * (up_ratio + 0.01), 200, T0 + 30);
        zoomed = false;
        for (const TouchAction& a : out) {
            if (a.kind == Kind::Zoom) {
                zoomed = true;
                check(a.level == levels[3], "a spread just over it: the next level in, " + show(a));
            }
        }
        check(zoomed, "a spread just over the threshold zooms in one level");
    }
    {
        Rig r;                                                  // zoom out
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        TouchControl::Actions out = framed_move(r, 2, 200.0 + 100.0 * (down_ratio + 0.01), 200, T0 + 20);
        bool zoomed = false;
        for (const TouchAction& a : out) zoomed = zoomed || a.kind == Kind::Zoom;
        check(!zoomed, "a pinch just short of the threshold: no level");
        out = framed_move(r, 2, 200.0 + 100.0 * (down_ratio - 0.01), 200, T0 + 30);
        for (const TouchAction& a : out) {
            if (a.kind == Kind::Zoom) {
                zoomed = true;
                check(a.level == levels[5], "just past it: the next level out, " + show(a));
            }
        }
        check(zoomed, "a pinch just past the threshold zooms out one level");
    }
    {
        Rig r;                                                  // the hysteresis: after the step in, the way back needs the fingers to come well back, and a hand that rests between does not flutter
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        int zooms = 0;
        float last = 1.0f;
        auto distance_to = [&](double d, uint32_t t) {
            for (const TouchAction& a : framed_move(r, 2, 200.0 + d, 200, t)) {
                if (a.kind == Kind::Zoom) {
                    ++zooms;
                    last = a.level;
                }
            }
        };
        distance_to(100.0 * (up_ratio + 0.02), T0 + 20);
        check(zooms == 1 && last == levels[3], "in one level");
        const double back = threshold_ratio(static_cast<double>(levels[3]), 1.0);            // from 1.19 back to 1: the ratio of the zoom that is needed (1 / 1.19)^0.65, times 1.19
        const double back_ideal = static_cast<double>(levels[3]) * back;                    // the ideal zoom at which it goes back
        check(back_ideal > 1.05 && back_ideal < 1.08, "(the way back is at the zoom " + std::to_string(back_ideal) + ", well below the way in at " + std::to_string(up_ratio) + ")");
        for (int i = 0; i < 100; ++i) distance_to(100.0 * (i % 2 ? up_ratio + 0.03 : up_ratio - 0.03), T0 + 30 + static_cast<uint32_t>(i));
        check(zooms == 1, "a hand that wobbles by 0.03 around the threshold: still one zoom, not a hundred (" + std::to_string(zooms) + ")");
        distance_to(100.0 * (back_ideal + 0.01), T0 + 200);
        check(zooms == 1, "just above the way back: still at the level");
        distance_to(100.0 * (back_ideal - 0.01), T0 + 210);
        check(zooms == 2 && last == 1.0f, "just below it: back to 1");
    }
    {
        // fingers that land closer than the floor (3 slops: 30 at the rig's slop of 10): the CURRENT distance is floored as the start one is (review L1). A jitter of a pixel between fingers that
        // landed 10 apart zoomed to the bottom level, and a spread from 10 to 20 zoomed OUT
        const double floor = touch::kMinSpanSlops * 10.0;
        const auto zooms_of = [](const TouchControl::Actions& out) {
            std::vector<float> levels_asked;
            for (const TouchAction& a : out) {
                if (a.kind == Kind::Zoom) levels_asked.push_back(a.level);
            }
            return levels_asked;
        };
        Rig r;
        r.down(1, 295, 200, T0);
        r.down(2, 305, 200, T0 + 10);
        check(zooms_of(framed_move(r, 2, 306, 200, T0 + 20)).empty(), "a jitter of 1 px between fingers that landed 10 apart: no zoom");
        check(zooms_of(framed_move(r, 2, 304, 200, T0 + 25)).empty(), "... the other way: no zoom");
        check(zooms_of(framed_move(r, 2, 315, 200, T0 + 30)).empty(), "a spread from 10 to 20: no zoom (it used to zoom OUT)");
        check(zooms_of(framed_move(r, 2, 295.0 + floor, 200, T0 + 40)).empty(), "to the floor itself: no zoom");
        check(zooms_of(framed_move(r, 2, 295.0 + floor * (up_ratio - 0.01), 200, T0 + 50)).empty(), "just under the threshold above the floor: no zoom");
        const std::vector<float> in = zooms_of(framed_move(r, 2, 295.0 + floor * (up_ratio + 0.01), 200, T0 + 60));
        check(in.size() == 1 && in[0] == levels[3], "just over it: the next level in, as for fingers that landed far apart");
        const std::vector<float> back = zooms_of(framed_move(r, 2, 307, 200, T0 + 70));        // the fingers come back together: the start level, never below it
        check(back.size() == 1 && back[0] == 1.0f, "the fingers come back to 12 apart: the start level (1), not a zoom out");
        Rig t;                                                  // fingers that touch (distance 0) and spread apart: the same floor
        t.down(1, 300, 200, T0);
        t.down(2, 300, 200, T0 + 10);
        check(zooms_of(framed_move(t, 2, 310, 200, T0 + 20)).empty() && zooms_of(framed_move(t, 2, 330, 200, T0 + 30)).empty(), "fingers that landed touching and spread to a floor's width: no zoom");
        const std::vector<float> wide = zooms_of(framed_move(t, 2, 300.0 + floor * 2.5, 200, T0 + 40));
        check(wide.size() == 1 && wide[0] == levels[0], "and on to two and a half floors: the top level (2.5 is past every threshold), in one action");
    }
    {
        Rig r;                                                  // a long spread goes up to the top level in one action and then says nothing more
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        expect(framed_move(r, 2, 600, 200, T0 + 20), {pan(150, 0, T0 + 20), zoom_to(2.0f, 400, 200, T0 + 20)},
               "a spread to four times the distance: the pan of the middle point (250 -> 400), then ONE zoom to the top level (2), anchored at the middle point now");
        int zooms = 0;
        float level = 0.0f;
        bool more = false;
        for (const TouchAction& a : framed_move(r, 2, 800, 200, T0 + 30)) more = more || a.kind == Kind::Zoom;
        check(!more, "spreading further at the top: nothing");
        const auto in = framed_move(r, 2, 210, 200, T0 + 40);            // distance 10 from 100: ratio 0.1: the bottom
        zooms = 0;
        for (const TouchAction& a : in) {
            if (a.kind == Kind::Zoom) {
                ++zooms;
                level = a.level;
            }
        }
        check(zooms == 1 && level == levels.back(), "a pinch to a tenth: one action, to the last level (" + std::to_string(static_cast<double>(levels.back())) + ")");
        bool again = false;
        for (const TouchAction& a : framed_move(r, 2, 205, 200, T0 + 50)) again = again || a.kind == Kind::Zoom;
        check(!again, "pinching further at the bottom: nothing");
    }
    {
        Rig r;                                                  // the anchor is the middle point as it is at the time of the zoom, not where the fingers landed
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        framed_move(r, 1, 150, 260, T0 + 15);
        const auto out = framed_move(r, 2, 420, 300, T0 + 20);          // middle now (285, 280), distance ~ 270 / 100
        const TouchAction* z = nullptr;
        for (const TouchAction& a : out) {
            if (a.kind == Kind::Zoom) z = &a;
        }
        check(z != nullptr && z->x == 285 && z->y == 280, "the zoom is anchored at the middle point now (285, 280): " + show(out));
        // pan comes before the zoom in one move, so that the world under the new middle point is what the zoom keeps
        size_t pan_at = out.size();
        size_t zoom_at = out.size();
        for (size_t i = 0; i < out.size(); ++i) {
            if (out[i].kind == Kind::Pan && pan_at == out.size()) pan_at = i;
            if (out[i].kind == Kind::Zoom && zoom_at == out.size()) zoom_at = i;
        }
        check(pan_at < zoom_at, "the pan comes before the zoom within one move");
    }
    {
        Rig r;                                                  // the level that the zoom starts from is the application's, whatever it is; a zoom between two levels is the nearest one
        r.env.level = 0.8f;                                     // nearest to 0.84, not to 0.71 (a ratio of 1.05 against 1.13)
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        // the ideal zoom is the START zoom (0.8) times the ratio, and the level changes when it passes the threshold of the level it is at (0.84 -> 1): a ratio of that zoom over 0.8
        const double to_in = static_cast<double>(levels[5]) * threshold_ratio(static_cast<double>(levels[5]), static_cast<double>(levels[4])) / 0.8;
        check(to_in > 1.17 && to_in < 1.18, "(the ratio that goes from 0.8 to the level 1: " + std::to_string(to_in) + ")");
        TouchControl::Actions out = framed_move(r, 2, 200.0 + 100.0 * (to_in - 0.01), 200, T0 + 20);
        bool zoomed = false;
        for (const TouchAction& a : out) zoomed = zoomed || a.kind == Kind::Zoom;
        check(!zoomed, "from 0.8 (the level 0.84), just under the way in: nothing");
        out = framed_move(r, 2, 200.0 + 100.0 * (to_in + 0.01), 200, T0 + 30);
        const TouchAction* z = nullptr;
        for (const TouchAction& a : out) {
            if (a.kind == Kind::Zoom) z = &a;
        }
        check(z != nullptr && z->level == levels[4], "just over it: the level 1");
        // the ideal zoom is the start zoom times the ratio: from 0.8, a ratio of 1.5 is 1.2: the level 1.19
        Rig s;
        s.env.level = 0.8f;
        s.down(1, 200, 200, T0);
        s.down(2, 300, 200, T0 + 10);
        const auto big = framed_move(s, 2, 350, 200, T0 + 20);
        const TouchAction* g = nullptr;
        for (const TouchAction& a : big) {
            if (a.kind == Kind::Zoom) g = &a;
        }
        check(g != nullptr && g->level == levels[3], "from 0.8 a ratio of 1.5 (a zoom of 1.2) is the level 1.19, got " + (g ? show(*g) : std::string("nothing")));
    }
    {
        Rig r;                                                  // one level on offer (no offscreen target: only the original's picture) or none: a pinch changes nothing, the pan still works
        r.env.list = {1.0f};
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        bool zoomed = false;
        bool panned = false;
        for (const TouchAction& a : framed_move(r, 2, 500, 230, T0 + 20)) {
            zoomed = zoomed || a.kind == Kind::Zoom;
            panned = panned || a.kind == Kind::Pan;
        }
        check(!zoomed && panned, "one level: no zoom, the pan goes on");
        Rig e;
        e.env.list.clear();
        e.down(1, 200, 200, T0);
        e.down(2, 300, 200, T0 + 10);
        zoomed = false;
        for (const TouchAction& a : framed_move(e, 2, 500, 200, T0 + 20)) zoomed = zoomed || a.kind == Kind::Zoom;
        check(!zoomed, "no levels at all: no zoom, no crash");
    }
    {
        Rig r;                                                  // fingers that land touching: the span is measured from three slops at least, so that they do not zoom by hundreds
        r.down(1, 300, 200, T0);
        r.down(2, 305, 200, T0 + 10);
        int zooms = 0;
        float level = 1.0f;
        for (const TouchAction& a : framed_move(r, 2, 360, 200, T0 + 20)) {
            if (a.kind == Kind::Zoom) {
                ++zooms;
                level = a.level;
            }
        }
        // the span is 3 x 10 = 30: the distance 60 is a ratio of 2: the level 2 is four levels in (the ideal 2.0 reaches it)
        check(zooms == 1 && level == 2.0f, "fingers 5 px apart, spread to 60: the ratio is taken from 30 px (x2), not from 5 px (x12): the top level in one step");
        Rig s;
        s.down(1, 300, 200, T0);
        s.down(2, 305, 200, T0 + 10);
        zooms = 0;
        for (const TouchAction& a : framed_move(s, 2, 330, 200, T0 + 20)) zooms += a.kind == Kind::Zoom ? 1 : 0;
        check(zooms == 0, "fingers 5 px apart, spread to 30 (x1 of the 30 px span): no zoom");
    }
    {
        Rig r;                                                  // the levels are taken once, when the second finger lands
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        r.env.list = {1.0f};
        r.env.level = 0.5f;
        const auto out = framed_move(r, 2, 500, 200, T0 + 20);                  // distance 300: ratio 3, from the zoom 1 that the gesture began at: the top level of the list it began with
        const TouchAction* z = nullptr;
        for (const TouchAction& a : out) {
            if (a.kind == Kind::Zoom) z = &a;
        }
        check(z != nullptr && z->level == 2.0f, "changes of the application's list and zoom during the gesture are not looked at: " + show(out));
    }
}

void test_extra_fingers() {
    group("fingers", "the third finger, lifts in either order, a finger that stays, unknown fingers, a finger that is down again");
    {
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        expect_none(r.down(3, 500, 400, T0 + 20), "a third finger: nothing");
        check(r.touch.fingers() == 3 && r.touch.ignored() == 1 && r.env.two_asked == 1, "it is ignored, and the gate is not asked");
        expect_none(r.move(3, 520, 420, T0 + 30), "its moves: nothing");
        expect(framed_move(r, 1, 210, 200, T0 + 40), {pan(5, 0, T0 + 40)}, "the pair goes on (the third finger does not count in the middle point or the distance)");
        expect_none(r.up(3, 520, 420, T0 + 50), "its lift: nothing");
        check(r.touch.mode() == TouchControl::Mode::Two && r.touch.fingers() == 2, "the pair is still there");
    }
    for (int first = 1; first <= 2; ++first) {                  // lifts in either order
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        framed_move(r, 1, 205, 200, T0 + 20);
        const int other = 3 - first;
        const std::string label = std::string("finger ") + std::to_string(first) + " lifts first: ";
        expect_none(r.up(first, 205, 200, T0 + 30), label + "nothing");
        check(r.touch.mode() == TouchControl::Mode::Idle && r.touch.fingers() == 1 && r.touch.ignored() == 1, label + "the pair is over; the finger that stays is ignored");
        expect_none(r.move(other, 330, 260, T0 + 40), label + "its moves: nothing (no pan, no zoom, no drag)");
        expect_none(r.tick(T0 + 5000), label + "no hold from it");
        // a new finger while it stays: nothing either (every finger of a hand that rests is ignored until all are up)
        expect_none(r.down(3, 100, 100, T0 + 5100), label + "a new finger while one stays");
        expect_none(r.up(3, 100, 100, T0 + 5150), label + "... and its lift: no click");
        expect_none(r.up(other, 330, 260, T0 + 5200), label + "the last finger lifts: nothing");
        check(idle(r), label + "all gone");
        expect(tap(r, 4, 100, 100, T0 + 6000), {act(Kind::Motion, 100, 100, T0 + 6080), act(Kind::LeftDown, 100, 100, T0 + 6080), act(Kind::LeftUp, 100, 100, T0 + 6080)}, label + "afterwards a tap clicks");
    }
    {
        Rig r;                                                  // unknown fingers: nothing, and nothing changes
        expect_none(r.move(9, 10, 10, T0), "a move of a finger that never went down");
        expect_none(r.up(9, 10, 10, T0 + 5), "a lift of one");
        check(idle(r), "still nothing tracked");
        r.down(1, 100, 100, T0 + 10);
        expect_none(r.move(9, 500, 500, T0 + 20), "an unknown finger moves while another waits");
        expect_none(r.up(9, 500, 500, T0 + 30), "an unknown finger lifts while another waits");
        check(r.touch.mode() == TouchControl::Mode::Waiting && r.touch.fingers() == 1, "the waiting finger is untouched");
        expect(r.up(1, 100, 100, T0 + 60), {act(Kind::Motion, 100, 100, T0 + 60), act(Kind::LeftDown, 100, 100, T0 + 60), act(Kind::LeftUp, 100, 100, T0 + 60)}, "and it taps");
        // the same id on another device is a different finger
        Rig d;
        d.down(1, 100, 100, T0, 1);
        expect_none(d.move(1, 500, 500, T0 + 10, 2), "finger 1 of ANOTHER device is unknown");
        expect_none(d.up(1, 500, 500, T0 + 20, 2), "... and its lift too");
        check(d.touch.mode() == TouchControl::Mode::Waiting, "the first device's finger waits");
    }
    {
        Rig r;                                                  // a finger that is down again: its lift was lost: what was held ends (Cancel), and the finger starts afresh
        r.down(1, 100, 100, T0);
        r.move(1, 150, 100, T0 + 30);
        const auto out = r.down(1, 300, 300, T0 + 100);
        expect(out, {act(Kind::Cancel, 150, 100, T0 + 100)}, "a drag, then the same finger down again: the first press is cancelled, the new finger waits");
        check(r.touch.mode() == TouchControl::Mode::Waiting && r.touch.fingers() == 1, "it waits at the new point");
        expect(r.up(1, 300, 300, T0 + 150), {act(Kind::Motion, 300, 300, T0 + 150), act(Kind::LeftDown, 300, 300, T0 + 150), act(Kind::LeftUp, 300, 300, T0 + 150)}, "and taps there");
        Rig two;                                                // in a pair: everything is dropped, the finger is the only one
        two.down(1, 100, 100, T0);
        two.down(2, 200, 100, T0 + 10);
        two.down(1, 120, 120, T0 + 20);
        check(two.touch.mode() == TouchControl::Mode::Waiting && two.touch.fingers() == 1, "a pair, one of them down again: only that finger is tracked");
        expect_none(two.move(2, 250, 100, T0 + 30), "the other finger of the lost pair is unknown now");
    }
    {
        Rig r;                                                  // positions that are not numbers
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        expect_none(r.down(1, nan, 100, T0), "a down at NaN");
        expect_none(r.down(1, 100, inf, T0), "a down at infinity");
        check(idle(r), "neither is tracked");
        r.down(1, 100, 100, T0 + 10);
        expect_none(r.move(1, nan, 100, T0 + 20), "a move to NaN");
        expect_none(r.move(1, -inf, 100, T0 + 30), "a move to -infinity");
        check(r.touch.mode() == TouchControl::Mode::Waiting, "the finger still waits");
        expect(r.up(1, nan, nan, T0 + 60), {act(Kind::Motion, 100, 100, T0 + 60), act(Kind::LeftDown, 100, 100, T0 + 60), act(Kind::LeftUp, 100, 100, T0 + 60)}, "a lift with no usable position is still a lift, where the finger was");
        Rig d;
        d.down(1, 850, 300, T0);
        d.move(1, 860, 310, T0 + 10);
        expect(d.up(1, nan, nan, T0 + 20), {act(Kind::LeftUp, 860, 310, T0 + 20)}, "a control press ends where the finger last was");
        Rig big;                                                // coordinates far outside any picture do not overflow
        big.env.everything_direct = true;
        const auto far = big.down(1, 1.0e300, -1.0e300, T0);
        check(far.size() == 2 && far[0].x == 1000000000 && far[0].y == -1000000000, "huge positions are held to a billion pixels: " + show(far));
    }
}

void test_primary_point() {
    group("point", "where the first finger is (what the application and the page's check read)");
    Rig r;
    double x = -1.0;
    double y = -1.0;
    check(!r.touch.primary_point(x, y), "no finger: no point");
    r.down(1, 120.5, 80.25, T0);
    check(r.touch.primary_point(x, y) && x == 120.5 && y == 80.25, "the first finger's point, with its fractions: (120.5, 80.25) read as (" + std::to_string(x) + ", " + std::to_string(y) + ")");
    r.move(1, 130.0, 90.5, T0 + 10);
    check(r.touch.primary_point(x, y) && x == 130.0 && y == 90.5, "after a move: where it is now");
    r.down(2, 300, 200, T0 + 20);
    x = y = -1.0;
    check(r.touch.mode() == TouchControl::Mode::Two && r.touch.primary_point(x, y) && x == 130.0 && y == 90.5, "in a pair the first finger is still the first (not the second one's point)");
    r.up(1, 130.0, 90.5, T0 + 30);
    check(!r.touch.primary_point(x, y), "after the pair ends the finger that stays is ignored: no first finger");
    r.up(2, 300, 200, T0 + 40);
    check(!r.touch.primary_point(x, y), "and none when all are up");
}

void test_cancel() {
    group("cancel", "a cancel (the browser took the touch, the focus is lost, the page is hidden) clears everything; a press that was held ends with no act");
    {
        Rig r;
        expect_none(r.touch.cancel(), "a cancel with nothing down");
        r.down(1, 100, 100, T0);
        expect_none(r.touch.cancel(), "a cancel of a finger that waits: nothing was sent, nothing to end");
        check(idle(r), "it is gone");
        expect_none(r.up(1, 100, 100, T0 + 50), "its lift comes anyway (SDL sends one for a cancel): unknown, nothing");
    }
    {
        Rig r;
        r.down(1, 100, 100, T0);
        r.move(1, 160, 110, T0 + 40);
        const auto ended = r.touch.cancel();
        expect(ended, {act(Kind::Cancel, 160, 110, T0 + 40)}, "a drag: the press ends where the finger is");
        check(ended.size() == 1 && !ended[0].right, "the left button's");
        check(idle(r), "gone");
        expect_none(r.up(1, 160, 110, T0 + 50), "the lift that SDL sends for the cancel: nothing (no selection)");
        expect_none(r.move(1, 170, 110, T0 + 60), "a move of it: nothing");
    }
    {
        Rig r;
        r.down(1, 100, 100, T0);
        r.tick(T0 + 450);
        const auto out = r.touch.cancel();
        check(out.size() == 1 && out[0].kind == Kind::Cancel && out[0].x == 100 && out[0].y == 100, "a hold: the right press ends: " + show(out));
        check(out.size() == 1 && out[0].right, "and it says that it is the right button's (the application lets that one go as a right release)");
        expect_none(r.up(1, 100, 100, T0 + 800), "the lift: no RightUp, no order");
    }
    {
        Rig r;
        r.down(1, 800, 50, T0);
        const auto out = r.touch.cancel();
        check(out.size() == 1 && out[0].kind == Kind::Cancel && out[0].x == 800 && out[0].y == 50 && !out[0].right, "a minimap press ends (the left button's): " + show(out));
        Rig h;
        h.down(1, 850, 300, T0);
        h.move(1, 855, 305, T0 + 10);
        const auto ended = h.touch.cancel();
        check(ended.size() == 1 && ended[0].kind == Kind::Cancel && ended[0].x == 855 && ended[0].y == 305 && !ended[0].right, "a press on a control ends where the finger is (the left button's): " + show(ended));
    }
    {
        Rig r;
        r.down(1, 200, 200, T0);
        r.down(2, 300, 200, T0 + 10);
        expect_none(r.touch.cancel(), "a pair holds no press: nothing to end");
        check(idle(r), "both gone");
        expect_none(r.move(2, 320, 200, T0 + 20), "the second finger's later move: unknown");
        expect_none(r.up(1, 200, 200, T0 + 30), "the first finger's later lift: unknown");
        check(r.touch.stats().cancels == 1, "one cancel counted");
    }
    {
        Rig r;                                                  // a finger that never lifts must not block the next ones
        r.down(1, 100, 100, T0);
        r.touch.cancel();
        expect(tap(r, 2, 300, 300, T0 + 1000), {act(Kind::Motion, 300, 300, T0 + 1080), act(Kind::LeftDown, 300, 300, T0 + 1080), act(Kind::LeftUp, 300, 300, T0 + 1080)}, "after a cancel the next finger is the first");
        Rig s;                                                  // ... also one that was ignored
        s.down(1, 100, 100, T0);
        s.down(2, 200, 100, T0 + 10);
        s.up(1, 100, 100, T0 + 20);
        s.touch.cancel();
        expect(tap(s, 3, 300, 300, T0 + 1000), {act(Kind::Motion, 300, 300, T0 + 1080), act(Kind::LeftDown, 300, 300, T0 + 1080), act(Kind::LeftUp, 300, 300, T0 + 1080)}, "after a cancel an ignored finger no longer blocks");
    }
    {
        Rig r;                                                  // the clock does not run first: a hold that was due is not fired by a cancel
        r.down(1, 100, 100, T0);
        expect_none(r.touch.cancel(), "a hold that was due and not heard of: nothing fires");
        check(r.touch.stats().holds == 0, "no hold was counted");
    }
}

void test_devices() {
    group("devices", "a finger is its device and its own id");
    Rig r;
    r.down(5, 200, 200, T0, 1);
    expect_none(r.down(5, 300, 200, T0 + 10, 2), "finger 5 of the device 2 is a second finger, not the first one again");
    check(r.touch.mode() == TouchControl::Mode::Two && r.touch.fingers() == 2, "a pair of two devices' fingers");
    expect(framed_move(r, 5, 210, 200, T0 + 20, 1), {pan(5, 0, T0 + 20)}, "the device 1 finger moves: a pan");
    expect(framed_move(r, 5, 310, 200, T0 + 30, 2), {pan(5, 0, T0 + 30)}, "the device 2 finger moves: another");
    expect_none(r.up(5, 210, 200, T0 + 40, 2), "the device 2 finger lifts");
    check(r.touch.mode() == TouchControl::Mode::Idle && r.touch.fingers() == 1, "the device 1 finger stays, ignored");
    expect_none(r.move(5, 400, 200, T0 + 50, 1), "its moves: nothing");
    expect_none(r.up(5, 400, 200, T0 + 60, 1), "its lift: nothing");
    check(idle(r), "all gone");
}

void test_clock() {
    group("clock", "the clock wraps, goes backwards, and the slop is the one a gesture began with");
    {
        Rig r;                                                  // a hold across the wrap of the 32-bit millisecond clock
        const uint32_t t = 0xFFFFFF00u;
        r.down(1, 100, 100, t);
        expect_none(r.tick(t + 449), "449 ms later, across the wrap: nothing");
        expect(r.tick(t + 450), {act(Kind::Motion, 100, 100, t + 450), act(Kind::RightDown, 100, 100, t + 450), act(Kind::HoldFired, 100, 100, t + 450)}, "450 ms later: the hold");
        Rig s;
        s.down(1, 100, 100, t);
        expect(s.up(1, 100, 100, t + 399), {act(Kind::Motion, 100, 100, t + 399), act(Kind::LeftDown, 100, 100, t + 399), act(Kind::LeftUp, 100, 100, t + 399)}, "a tap that ends 399 ms later, across the wrap");
    }
    {
        Rig r;                                                  // a time that goes backwards is the last time
        r.down(1, 100, 100, T0 + 100);
        expect_none(r.tick(T0), "an update with an earlier time: nothing");
        expect(r.up(1, 100, 100, T0 + 50), {act(Kind::Motion, 100, 100, T0 + 100), act(Kind::LeftDown, 100, 100, T0 + 100), act(Kind::LeftUp, 100, 100, T0 + 100)}, "a lift with an earlier time than the down: a tap at the time that the model has");
        Rig s;
        s.down(1, 100, 100, T0);
        s.tick(T0 + 300);
        expect_none(s.tick(T0 + 100), "a clock that stepped back does not delay or repeat the hold");
        expect(s.tick(T0 + 450), {act(Kind::Motion, 100, 100, T0 + 450), act(Kind::RightDown, 100, 100, T0 + 450), act(Kind::HoldFired, 100, 100, T0 + 450)}, "the hold is at 450 ms of the down");
    }
    {
        Rig r;                                                  // the slop of a finger is the one at its arrival
        r.touch.set_slop(20.0);
        r.down(1, 100, 100, T0);
        r.touch.set_slop(5.0);
        expect_none(r.move(1, 115, 100, T0 + 10), "a move of 15 within the slop of 20 that the finger began with");
        check(r.move(1, 125, 100, T0 + 20).size() == 3, "a move of 25 is out of it");
        Rig s;
        s.touch.set_slop(5.0);
        s.down(1, 100, 100, T0);
        check(s.move(1, 107, 100, T0 + 10).size() == 3, "slop 5: a move of 7 is a drag");
        Rig t;
        t.touch.set_slop(std::numeric_limits<double>::quiet_NaN());
        check(t.touch.slop() == 8.0, "a slop that is not a number is the default 8");
        t.touch.set_slop(0.2);
        check(t.touch.slop() == 1.0, "a slop under one picture pixel is 1");
        t.touch.set_slop(12.5);
        check(t.touch.slop() == 12.5, "a slop is kept");
    }
}

void test_slop_size() {
    group("slop", "the slop is a size on the glass: 8 CSS pixels of the game box, at least 6 device pixels, between 1 and 64 picture pixels");
    // (the constants are named in one place, touch_control.hpp, to be tuned on the owner's phone: a tuned value is changed here at the same time)
    check(touch::kSlopCssPx == 8.0 && touch::kSlopFloorDevicePx == 6.0, "the constants: 8 CSS px (Android's own 8 dp), 6 device px");
    check(touch::kTapMs == 400 && touch::kHoldMs == 450 && touch::kRingStartMs == 150 && touch::kPulseMs == 240, "the times: tap 400 ms, hold 450 ms, the ring from 150 ms, the pulse 240 ms");
    check(touch::kHysteresis == 0.15 && touch::kMinSpanSlops == 3.0, "the pinch: hysteresis 0.15 of a step, a least span of 3 slops");
    // a phone: the box 800 CSS px wide for the 960 picture (1.2 picture px per CSS px) at the device ratio 2.6 (0.4615 picture px per device px)
    check(std::fabs(touch::slop_pixels(1.2, 1.2 / 2.6) - 9.6) < 1e-9, "a phone: 8 CSS px of a box that shows 1.2 picture px per CSS px are 9.6 picture px");
    // a desktop window at 1x: 960 CSS px for 960
    check(std::fabs(touch::slop_pixels(1.0, 1.0) - 8.0) < 1e-9, "a window at 1x: 8");
    // the classic 640 canvas in a 1280 wide window: half a picture px per CSS px: the slop is 4 picture px (the same size on the glass)
    check(std::fabs(touch::slop_pixels(0.5, 0.5) - 4.0) < 1e-9, "640 picture px in 1280 CSS px: 4");
    // a page zoomed far out: 0.5 device pixels per... the CSS px are small (dpr 0.5): 8 CSS px are 4 device px, the floor of 6 device px wins
    check(std::fabs(touch::slop_pixels(1.2, 1.2 / 0.5) - 6.0 * (1.2 / 0.5)) < 1e-9, "a page zoomed out to a device ratio of 0.5: the floor of 6 device px: 14.4");
    check(touch::slop_pixels(0.05, 0.05) == 1.0, "never under one picture pixel");
    check(touch::slop_pixels(100.0, 100.0) == 64.0, "never over 64");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    check(touch::slop_pixels(nan, nan) == 1.0 && touch::slop_pixels(0.0, 0.0) == 1.0 && touch::slop_pixels(-3.0, -1.0) == 1.0, "garbage is 1");
    check(std::fabs(touch::slop_pixels(nan, 2.0) - 12.0) < 1e-9 && std::fabs(touch::slop_pixels(1.5, nan) - 12.0) < 1e-9, "a scale that is not a number counts for nothing: the other decides");
}

void test_sequence() {
    group("order", "a whole session in the order that it happens");
    Rig r;
    TouchControl::Actions all;
    auto add = [&all](const TouchControl::Actions& a) { all.insert(all.end(), a.begin(), a.end()); };
    add(r.down(1, 100, 100, T0));                               // a tap
    add(r.up(1, 100, 100, T0 + 60));
    add(r.down(1, 850, 300, T0 + 200));                         // a press of a control
    add(r.up(1, 850, 300, T0 + 260));
    add(r.down(1, 300, 300, T0 + 400));                         // a drag
    add(r.move(1, 340, 300, T0 + 450));
    add(r.up(1, 360, 310, T0 + 600));
    add(r.down(1, 300, 300, T0 + 700));                         // a hold
    add(r.tick(T0 + 1160));
    add(r.up(1, 301, 301, T0 + 1300));
    expect(all,
           {act(Kind::Motion, 100, 100, T0 + 60), act(Kind::LeftDown, 100, 100, T0 + 60), act(Kind::LeftUp, 100, 100, T0 + 60),
            act(Kind::Motion, 850, 300, T0 + 200), act(Kind::LeftDown, 850, 300, T0 + 200), act(Kind::LeftUp, 850, 300, T0 + 260),
            act(Kind::Motion, 300, 300, T0 + 450), act(Kind::LeftDown, 300, 300, T0 + 450), act(Kind::Motion, 340, 300, T0 + 450), act(Kind::LeftUp, 360, 310, T0 + 600),
            act(Kind::Motion, 300, 300, T0 + 1150), act(Kind::RightDown, 300, 300, T0 + 1150), act(Kind::HoldFired, 300, 300, T0 + 1150), act(Kind::RightUp, 301, 301, T0 + 1300)},
           "tap, control press, drag, hold");
    check(idle(r) && r.touch.stats().taps == 1 && r.touch.stats().drags == 1 && r.touch.stats().holds == 1, "counted: one tap, one drag, one hold");
    // every press that is made is ended: the left and the right presses are balanced in the whole list
    int left = 0;
    int right = 0;
    for (const TouchAction& a : all) {
        left += a.kind == Kind::LeftDown ? 1 : a.kind == Kind::LeftUp ? -1 : 0;
        right += a.kind == Kind::RightDown ? 1 : a.kind == Kind::RightUp ? -1 : 0;
    }
    check(left == 0 && right == 0, "every press has its release");
}

/// A deterministic soak: random fingers, moves, lifts, cancels and ticks over a long time. Whatever happens, every press that is made is ended (by a release or a Cancel), no two buttons are
/// ever down at once, no action comes after the end of its press, and the model ends idle once every finger is up.
void test_soak() {
    group("soak", "random fingers over a long time: every press ends, never two buttons down, the model always comes back to idle");
    uint32_t seed = 12345u;
    auto rnd = [&seed](uint32_t n) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % n;
    };
    int sessions = 0;
    int bad = 0;
    for (int round = 0; round < 300; ++round) {
        Rig r(rnd(2) == 0);                                     // (half of the sessions have the application's frames between the events, half the raw events of a stalled one)
        r.env.allow_two = rnd(4) != 0;
        r.touch.set_slop(4.0 + static_cast<double>(rnd(12)));
        uint32_t t = 1000u + rnd(100000u);
        bool left = false;
        bool right = false;
        auto account = [&](const TouchControl::Actions& acts) {
            for (const TouchAction& a : acts) {
                switch (a.kind) {
                    case Kind::LeftDown: if (left || right) ++bad; left = true; break;
                    case Kind::LeftUp: if (!left) ++bad; left = false; break;
                    case Kind::RightDown: if (left || right) ++bad; right = true; break;
                    case Kind::RightUp: if (!right) ++bad; right = false; break;
                    case Kind::Cancel: if (!left && !right) ++bad; left = false; right = false; break;
                    default: break;
                }
            }
        };
        for (int step = 0; step < 150; ++step) {
            t += rnd(260);
            const uint32_t what = rnd(10);
            const int64_t id = static_cast<int64_t>(1 + rnd(4));
            const double x = static_cast<double>(rnd(1000));
            const double y = static_cast<double>(rnd(560));
            if (what <= 2) {
                account(r.down(id, x, y, t));
            } else if (what <= 5) {
                account(r.move(id, x, y, t));
            } else if (what <= 8) {
                account(r.up(id, x, y, t));
            } else if (rnd(6) == 0) {
                account(r.touch.cancel());
            } else {
                account(r.tick(t));
            }
        }
        for (int64_t id = 1; id <= 4; ++id) account(r.up(id, 10, 10, t + 1));
        account(r.tick(t + 2));
        if (left || right || r.touch.fingers() != 0 || r.touch.mode() != TouchControl::Mode::Idle) ++bad;
        ++sessions;
    }
    check(bad == 0, std::to_string(sessions) + " random sessions: " + std::to_string(bad) + " broken rules");
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;                                     // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    test_tap();
    test_hold();
    test_stall();
    test_late();
    test_primary_point();
    test_ring();
    test_feedback();
    test_drag();
    test_other_places();
    test_second_finger();
    test_pan();
    test_pair_per_frame();
    test_pinch();
    test_extra_fingers();
    test_cancel();
    test_devices();
    test_clock();
    test_slop_size();
    test_sequence();
    test_soak();
    std::printf("\ntouch model: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
