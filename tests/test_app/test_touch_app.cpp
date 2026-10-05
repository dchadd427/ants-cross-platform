// The touch controls in the application (src/ants_app/application_touch.cpp; the model is test_touch_model): synthetic SDL finger events through the real event loop of a headless application
// (the dummy video driver, a match on GAUNTLET and the setup screens), and what is done with them:
//   * SDL's own touch-to-mouse emulation is off (nothing reaches the game twice); a trackpad's touches and a platform's own are not fingers;
//   * a TAP, a DRAG and a HOLD are the mouse's left click, rubber band and right click: the commands, the selection and the cursor are the same as the mouse's, point by point (the
//     differential groups), and no gesture sends a command that the mouse cannot send;
//   * a hold on a lone powered ant is its special power, a hold on the minimap sends the group there, a hold that is due when the finger lifts still acts, a hold needs a frame to
//     fire when the finger rests, and the second finger turns all of it into a pan and a pinch;
//   * TWO FINGERS pan the view by the same distance on the screen at every zoom and pinch it level by level inside the limits, anchored at the middle point; they do nothing where the
//     wheel does nothing (every dialog and page, the results, a held press), and where they do nothing a tap is still a click; a network match has the same levels as the wheel;
//   * a cancel (the browser took the touch, the window lost the focus) leaves no press held and no finger tracked; a press that waited on the map is not made on a dialog that opened
//     meanwhile; a lifted finger leaves no pointer and no edge scroll behind;
//   * the screens that are not the map view (the quick help, the setup screen, the start menu) take taps and drags as the emulation gave them;
//   * the places that SDL's renderer maps (a window of another shape than the canvas), the slop's size, and `pan_view`.
// Usage: test_touch_app. Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "zoom_scene.hpp"

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/minimap_tables.hpp"
#include "ants_app/options_screen.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/touch_control.hpp"
#include "ants_app/touch_feedback.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;
using namespace zoomtest;

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

struct Pt {
    int32_t x{0};
    int32_t y{0};
};

std::string show(const Pt& p) { return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")"; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The fingers: SDL's events, with the times of a clock that the test owns
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// Puts finger events into SDL's queue the way a touch screen's driver does (normalised over the canvas: what SDL's renderer makes of them), stamped with the test's clock, which is the
/// application's touch clock as well. A position is the picture pixel that it names, at its middle (a fraction of a pixel never moves it to the next one) unless a place inside the pixel is given.
struct Hand {
    explicit Hand(Application& a, int64_t touch_device = 7) : app(a), device(touch_device) {
        app.set_touch_clock([this]() { return now; });
        while (SDL_PollEvent(&stray)) {}                                  // (nothing of an earlier test is left in the queue)
    }
    ~Hand() { app.set_touch_clock(nullptr); }
    Hand(const Hand&) = delete;
    Hand& operator=(const Hand&) = delete;

    void send(uint32_t type, int64_t finger, Pt at, double fx = 0.5, double fy = 0.5) {
        SDL_Event e;
        SDL_zero(e);
        e.type = type;
        e.tfinger.type = type;
        e.tfinger.timestamp = now;
        e.tfinger.touchId = device;
        e.tfinger.fingerId = finger;
        e.tfinger.x = static_cast<float>((static_cast<double>(at.x) + static_cast<double>(app.picture().x) + fx) / static_cast<double>(app.renderer().canvas_w()));
        e.tfinger.y = static_cast<float>((static_cast<double>(at.y) + static_cast<double>(app.picture().y) + fy) / static_cast<double>(app.renderer().canvas_h()));
        SDL_PeepEvents(&e, 1, SDL_ADDEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
    }
    void down(int64_t finger, Pt at, double fx = 0.5, double fy = 0.5) { send(SDL_FINGERDOWN, finger, at, fx, fy); }
    void move(int64_t finger, Pt at, double fx = 0.5, double fy = 0.5) { send(SDL_FINGERMOTION, finger, at, fx, fy); }
    void up(int64_t finger, Pt at, double fx = 0.5, double fy = 0.5) { send(SDL_FINGERUP, finger, at, fx, fy); }
    void wait(uint32_t ms) { now += ms; }
    void frame() { app.run_frame_with_delta(0.016f); }
    /// The time passes with the application's frames running (one every `step` ms): the model knows that the finger is down all the while. wait() alone is a stall.
    void rest(uint32_t ms, uint32_t step = 20) {
        while (ms > 0) {
            const uint32_t d = std::min(step, ms);
            wait(d);
            frame();
            ms -= d;
        }
    }
    /// A tap: down, 80 ms, up, a frame
    void tap(Pt at, int64_t finger = 1) {
        down(finger, at);
        wait(80);
        up(finger, at);
        frame();
    }
    /// A hold: down, a frame at the hold time, the lift `rest` ms later
    void hold(Pt at, Pt lift, uint32_t rest = 120, int64_t finger = 1) {
        down(finger, at);
        frame();
        wait(touch::kHoldMs);
        frame();
        wait(rest);
        up(finger, lift);
        frame();
    }
    /// A drag from a to b in `steps` moves, a frame after each
    void drag(Pt a, Pt b, int steps = 6, int64_t finger = 1) {
        down(finger, a);
        frame();
        for (int i = 1; i <= steps; ++i) {
            wait(16);
            move(finger, Pt{a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps});
            frame();
        }
        wait(16);
        up(finger, b);
        frame();
    }

    Application& app;
    int64_t device;
    uint32_t now{500000};
    SDL_Event stray{};
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scene: a match whose commands are recorded, with ants of the player near the first hill
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Scene {
    explicit Scene(Application& a) : app(a) {
        app.hud().set_command_sink(&sink);
        sim::SimulationEngine& sim = app.sim();
        const auto& world = sim.get_world_state();
        int32_t hx = 20;
        int32_t hy = 20;
        for (const auto& hill : world.anthills) {
            if (hill.team_id == 0) {
                hx = static_cast<int32_t>(hill.x);
                hy = static_cast<int32_t>(hill.y);
            }
        }
        base = sim::TileCoord{hx, hy};
        worker = place(sim::AntType::Worker, 0, 6, 0);
        bomber = place(sim::AntType::Bomber, 0, 8, 0);
        second = place(sim::AntType::Worker, 0, 6, 3);
        foe = place(sim::AntType::Worker, 1, 10, 2);
        sim.tick();
        look();
    }
    /// An ant at a free tile near the hill (the search goes outward from the offset until the engine takes one)
    uint32_t place(sim::AntType type, uint8_t player, int dx, int dy) {
        for (int r = 0; r < 12; ++r) {
            for (int j = -r; j <= r; ++j) {
                for (int i = -r; i <= r; ++i) {
                    const sim::TileCoord tile{base.x + dx + i, base.y + dy + j};
                    if (tile.x < 1 || tile.y < 1 || tile.x >= static_cast<int32_t>(app.sim().grid().width()) - 1 || tile.y >= static_cast<int32_t>(app.sim().grid().height()) - 1) continue;
                    const uint32_t id = app.sim().spawn_unit(player, type, tile);
                    if (id != 0) return id;
                }
            }
        }
        return 0;
    }
    const sim::AntSnapshot* ant(uint32_t id) const {
        for (const auto& a : app.sim().get_world_state().ants) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    /// The view over the scene (the ants are in it, at the zoom 1)
    void look() {
        app.renderer().camera().zoom = 1.0f;
        const sim::AntSnapshot* a = ant(worker);
        const int32_t cx = a != nullptr ? a->px : base.x * 32;
        const int32_t cy = a != nullptr ? a->py : base.y * 32;
        app.renderer().camera().center_on(cx + 120, cy, app.sim().grid().width(), app.sim().grid().height());
    }
    /// The picture pixel of a world point (as the camera has it now)
    Pt screen_of(int32_t wx, int32_t wy) const {
        int32_t sx = 0;
        int32_t sy = 0;
        app.renderer().camera().world_to_screen(wx, wy, sx, sy);
        return Pt{sx, sy};
    }
    /// The pixel of an ant that picks it (a little above its feet, inside its box)
    Pt on_ant(uint32_t id) const {
        const sim::AntSnapshot* a = ant(id);
        return a != nullptr ? screen_of(a->px, a->py - 8) : Pt{};
    }
    /// A pixel of the view over bare ground, `i` tiles from the worker's tile (to the east and the south), where no ant is
    Pt ground(int i, int j) const {
        const sim::AntSnapshot* a = ant(worker);
        const int32_t tx = (a != nullptr ? a->tile_x : base.x) + 3 + i;
        const int32_t ty = (a != nullptr ? a->tile_y : base.y) + 2 + j;
        return screen_of(tx * 32 + 16, ty * 32 + 16);
    }
    sim::TileCoord tile_under(Pt p) const {
        const LayoutRect view = app.layout().view();
        const ViewportCamera& camera = app.renderer().camera();
        return sim::TileCoord{camera.world_x_at(p.x - view.x) / 32, camera.world_y_at(p.y - view.y) / 32};
    }
    void clear() {
        app.hud().clear_selection();
        app.hud().unlatch_pedestals();
        sink.commands.clear();
    }
    void select(std::vector<uint32_t> ids) { app.hud().set_selected_ant_ids(std::move(ids)); }

    Application& app;
    RecordingSink sink;
    sim::TileCoord base{20, 20};
    uint32_t worker{0};
    uint32_t bomber{0};
    uint32_t second{0};
    uint32_t foe{0};
};

/// What a gesture did to the game: the selection and the commands (what a click, a band or a right click can change)
struct Outcome {
    std::vector<uint32_t> selected;
    std::vector<sim::Command> commands;
    bool captured{false};
};

Outcome outcome_of(Scene& s) {
    Outcome o;
    o.selected = s.app.hud().get_selected_ant_ids();
    std::sort(o.selected.begin(), o.selected.end());
    o.commands = s.sink.commands;
    o.captured = s.app.hud().is_input_captured();
    return o;
}

bool same_commands(const std::vector<sim::Command>& a, const std::vector<sim::Command>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].issuer != b[i].issuer || a[i].tile_x != b[i].tile_x || a[i].tile_y != b[i].tile_y || a[i].ants != b[i].ants) return false;
    }
    return true;
}

bool same(const Outcome& a, const Outcome& b) { return a.selected == b.selected && same_commands(a.commands, b.commands) && a.captured == b.captured; }

std::string describe(const Outcome& o) {
    std::string out = "selected [";
    for (const uint32_t id : o.selected) out += std::to_string(id) + " ";
    out += "], commands [";
    for (const sim::Command& c : o.commands) out += "type " + std::to_string(static_cast<int>(c.type)) + " at (" + std::to_string(c.tile_x) + ", " + std::to_string(c.tile_y) + ") ants " + std::to_string(c.ants.size()) + "; ";
    return out + "]" + (o.captured ? ", a press is held" : "");
}

// ---- the mouse, as the window delivers it (the same handlers: Application::handle_mouse_button and handle_mouse_motion) ----

SDL_MouseMotionEvent motion_event(Pt at) {
    SDL_MouseMotionEvent m{};
    m.type = SDL_MOUSEMOTION;
    m.x = at.x;
    m.y = at.y;
    return m;
}

void mouse_press(Application& app, uint8_t button, Pt at) {
    app.handle_mouse_motion(motion_event(at));
    app.handle_mouse_button(button_event(button, SDL_MOUSEBUTTONDOWN, at.x, at.y));
}
void mouse_release(Application& app, uint8_t button, Pt at) { app.handle_mouse_button(button_event(button, SDL_MOUSEBUTTONUP, at.x, at.y)); }

void mouse_click(Application& app, uint8_t button, Pt at, Pt release) {
    mouse_press(app, button, at);
    mouse_release(app, button, release);
}

/// What the pointer says afterwards is the finger's lift: the pointer is gone (no cursor, no edge scroll from where it was)
void forget_pointer(Application& app) {
    app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_ENTER; return we; }());
    app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_LEAVE; return we; }());
}

struct Match {
    Match() : rig(Aspect::Wide16x9, 1.0f, false, std::string(), true, true), ok(rig.ok) {
        if (ok) scene = std::make_unique<Scene>(rig.app);
    }
    AppRig rig;
    bool ok;
    std::unique_ptr<Scene> scene;
    Application& app() { return rig.app; }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The tests
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

void test_setup_of_sdl() {
    group("sdl", "SDL's touch-to-mouse emulation is off, and only a touch screen's fingers count");
    Match m;
    check(m.ok, "the match is up");
    if (!m.ok) return;
    const char* hint = SDL_GetHint(SDL_HINT_TOUCH_MOUSE_EVENTS);
    check(hint != nullptr && std::string(hint) == "0", "SDL_HINT_TOUCH_MOUSE_EVENTS is 0: SDL makes no mouse events of fingers (they would reach the game twice)");
    Application& app = m.app();
    Hand hand(app);
    // a touch that a platform made from the mouse (SDL_MOUSE_TOUCHID) is no finger
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_FINGERDOWN;
        e.tfinger.type = SDL_FINGERDOWN;
        e.tfinger.timestamp = hand.now;
        e.tfinger.touchId = SDL_MOUSE_TOUCHID;
        e.tfinger.fingerId = 1;
        e.tfinger.x = 0.4f;
        e.tfinger.y = 0.4f;
        SDL_PeepEvents(&e, 1, SDL_ADDEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        hand.frame();
        check(app.touch().fingers() == 0, "a finger of the platform's own touch made from the mouse is not tracked");
    }
    hand.down(1, Pt{300, 200});
    hand.frame();
    check(app.touch().fingers() == 1 && app.touch().mode() == TouchControl::Mode::Waiting, "a finger on the map view is tracked and waits");
    hand.up(1, Pt{300, 200});
    hand.wait(500);
    hand.frame();
    check(app.touch().fingers() == 0, "and it ends");
    // the filter itself (a trackpad's device cannot be made here: the function says what SDL's device types mean)
    check(Application::counts_as_finger(7, SDL_TOUCH_DEVICE_DIRECT), "a touch screen's finger counts");
    check(Application::counts_as_finger(7, SDL_TOUCH_DEVICE_INVALID), "a device that SDL does not know counts (a test's)");
    check(!Application::counts_as_finger(7, SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE), "a trackpad's touch (indirect, absolute) is not a place on the picture");
    check(!Application::counts_as_finger(7, SDL_TOUCH_DEVICE_INDIRECT_RELATIVE), "a trackpad's touch (indirect, relative) is not a place on the picture");
    check(!Application::counts_as_finger(SDL_MOUSE_TOUCHID, SDL_TOUCH_DEVICE_DIRECT), "the platform's touch made from the mouse is not a finger, whatever it says it is");
    check(!Application::counts_as_finger(SDL_MOUSE_TOUCHID, SDL_TOUCH_DEVICE_INVALID), "(and when SDL does not know it)");
}

void test_tap_is_the_click() {
    group("tap", "a tap is the mouse's left click: select, order, the same commands, point by point");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    check(s.worker != 0 && s.bomber != 0 && s.second != 0 && s.foe != 0, "the scene's ants were placed");
    Hand hand(app);
    // a tap on an ant selects it
    s.clear();
    hand.tap(s.on_ant(s.worker));
    check(app.hud().get_selected_ant_ids() == std::vector<uint32_t>{s.worker} && s.sink.commands.empty(), "a tap on the worker selects it and sends no command");
    check(app.touch().fingers() == 0 && app.pointer_outside(), "the finger is gone, and the pointer with it (no cursor, no edge scroll)");
    // a tap on the ground orders a move
    const Pt g = s.ground(2, 1);
    hand.tap(g);
    const sim::TileCoord t = s.tile_under(g);
    check(s.sink.commands.size() == 1 && s.sink.commands[0].type == sim::CommandType::GroupMove && s.sink.commands[0].tile_x == t.x && s.sink.commands[0].tile_y == t.y &&
              s.sink.commands[0].ants == std::vector<uint32_t>{s.worker},
          "a tap on the ground orders the selected worker to the tile under the finger");
    // a tap on empty ground with nothing selected deselects (as a click does); a tap on the foe inspects it
    s.clear();
    hand.tap(s.on_ant(s.foe));
    check(app.hud().get_selected_ant_id() == s.foe && s.sink.commands.empty(), "a tap on another player's ant inspects it");

    // THE DIFFERENTIAL: every point of a grid over the view, with a selection of each kind: the tap and the mouse's click make the same selection and the same commands
    const LayoutRect view = app.layout().view();
    uint32_t seed = 4242;
    const std::vector<std::vector<uint32_t>> selections = {{}, {s.worker}, {s.bomber}, {s.worker, s.second}, {s.foe}};
    int compared = 0;
    int different = 0;
    int orders = 0;
    int selected_something = 0;
    for (size_t k = 0; k < selections.size(); ++k) {
        for (int n = 0; n < 40; ++n) {
            Pt p{view.x + 4 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.w - 8)), view.y + 4 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.h - 8))};
            if (n < 6) {                                              // (the first points of each round are on the ants themselves: the tap that selects, adds, inspects)
                const uint32_t ids[6] = {s.worker, s.bomber, s.second, s.foe, s.worker, s.second};
                p = s.on_ant(ids[n]);
            }
            s.clear();
            s.select(selections[k]);
            hand.tap(p);
            const Outcome by_finger = outcome_of(s);
            s.clear();
            s.select(selections[k]);
            mouse_click(app, SDL_BUTTON_LEFT, p, p);
            forget_pointer(app);
            const Outcome by_mouse = outcome_of(s);
            ++compared;
            orders += by_mouse.commands.empty() ? 0 : 1;
            selected_something += by_mouse.selected.empty() ? 0 : 1;
            if (!same(by_finger, by_mouse)) {
                ++different;
                check(false, "a tap at " + show(p) + " with selection " + std::to_string(k) + " is not the mouse's click: " + describe(by_finger) + " against " + describe(by_mouse));
            }
        }
    }
    check(different == 0 && compared == 200, "200 taps over the view, five selections: every one is the mouse's click (" + std::to_string(different) + " differ)");
    check(orders >= 40 && selected_something >= 40, "(the grid has teeth: " + std::to_string(orders) + " of the clicks gave an order, " + std::to_string(selected_something) + " left something selected)");
    // the tap clicks at the DOWN point, not at the lift point (the finger wobbled within the slop)
    s.clear();
    s.select({s.worker});
    const Pt a = s.ground(3, 0);
    hand.down(1, a);
    hand.frame();
    hand.move(1, Pt{a.x + 4, a.y + 3});
    hand.wait(70);
    hand.up(1, Pt{a.x + 5, a.y - 4});
    hand.frame();
    const sim::TileCoord down_tile = s.tile_under(a);
    check(s.sink.commands.size() == 1 && s.sink.commands[0].tile_x == down_tile.x && s.sink.commands[0].tile_y == down_tile.y, "a wobbly tap orders the tile of the down point");
}

void test_drag_is_the_band() {
    group("drag", "a drag is the rubber band: the same selection as the mouse's, no click at its end");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    const sim::AntSnapshot* w = s.ant(s.worker);
    const sim::AntSnapshot* b = s.ant(s.bomber);
    check(w != nullptr && b != nullptr, "the ants are there");
    if (w == nullptr || b == nullptr) return;
    const Pt top_left = s.screen_of(std::min(w->px, b->px) - 60, std::min(w->py, b->py) - 70);
    const Pt bottom_right = s.screen_of(std::max(w->px, b->px) + 60, std::max(w->py, b->py) + 40);
    s.clear();
    hand.drag(top_left, bottom_right);
    const Outcome by_finger = outcome_of(s);
    check(by_finger.selected.size() >= 2 && s.sink.commands.empty() && !by_finger.captured, "a drag over two ants selects them and sends no command: " + describe(by_finger));
    s.clear();
    mouse_press(app, SDL_BUTTON_LEFT, top_left);
    app.handle_mouse_motion(motion_event(bottom_right));
    mouse_release(app, SDL_BUTTON_LEFT, bottom_right);
    forget_pointer(app);
    const Outcome by_mouse = outcome_of(s);
    check(same(by_finger, by_mouse), "the finger's band selects what the mouse's band selects: " + describe(by_finger) + " against " + describe(by_mouse));

    // the band is held while the finger is down, and begins at the origin (not where the finger had gone when the slop was left)
    s.clear();
    const Pt a{top_left.x + 20, top_left.y + 20};
    hand.down(1, a);
    hand.frame();
    check(!app.hud().is_input_captured(), "nothing is held while the finger waits");
    hand.wait(40);
    hand.move(1, Pt{a.x + 60, a.y + 50});
    hand.frame();
    check(app.hud().is_input_captured() && app.touch().mode() == TouchControl::Mode::Left, "the finger left the slop: the band is held");
    hand.up(1, Pt{a.x + 60, a.y + 50});
    hand.frame();
    check(!app.hud().is_input_captured(), "the lift lets it go");

    // many bands, differential (starting points and ends all over the view)
    const LayoutRect view = app.layout().view();
    uint32_t seed = 99;
    int different = 0;
    for (int n = 0; n < 30; ++n) {
        const Pt p{view.x + 4 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.w - 8)), view.y + 4 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.h - 8))};
        const Pt q{view.x + 4 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.w - 8)), view.y + 4 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.h - 8))};
        s.clear();
        s.select({s.worker});
        hand.drag(p, q, 3);
        const Outcome f = outcome_of(s);
        s.clear();
        s.select({s.worker});
        mouse_press(app, SDL_BUTTON_LEFT, p);
        app.handle_mouse_motion(motion_event(q));
        mouse_release(app, SDL_BUTTON_LEFT, q);
        forget_pointer(app);
        const Outcome mo = outcome_of(s);
        if (!same(f, mo)) {
            ++different;
            check(false, "a drag from " + show(p) + " to " + show(q) + " is not the mouse's: " + describe(f) + " against " + describe(mo));
        }
    }
    check(different == 0, "30 drags: every one is the mouse's band");
}

void test_hold_is_the_right_click() {
    group("hold", "a hold is the mouse's right click: a move for a worker, the special power of a lone powered ant, the minimap sends the group there");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    // a worker selected: the right click is a plain move at the tile of the press point
    s.clear();
    s.select({s.worker});
    const Pt g = s.ground(2, 2);
    hand.hold(g, g);
    const sim::TileCoord t = s.tile_under(g);
    check(s.sink.commands.size() == 1 && s.sink.commands[0].type == sim::CommandType::GroupMove && s.sink.commands[0].tile_x == t.x && s.sink.commands[0].tile_y == t.y,
          "a hold with a worker selected moves it to the tile under the finger");
    check(!app.hud().is_input_captured() && app.touch().fingers() == 0 && app.pointer_outside(), "afterwards nothing is held and the pointer is gone");
    check(app.touch().stats().holds == 1, "one hold was counted");
    check(app.touch_feedbacks() == 1, "the hold gave its feedback (the buzz of the web build) once");
    hand.tap(g);
    check(app.touch_feedbacks() == 1, "a tap gives none");
    // a lone powered ant: its special power (a bomb for the bomber), not a move
    s.clear();
    s.select({s.bomber});
    hand.hold(g, g);
    check(s.sink.commands.size() == 1 && s.sink.commands[0].type == sim::CommandType::GroupSpecial && s.sink.commands[0].tile_x == t.x && s.sink.commands[0].tile_y == t.y &&
              s.sink.commands[0].ants == std::vector<uint32_t>{s.bomber},
          "a hold with a lone bomber selected uses its special power at the tile under the finger");
    // the press point decides the tile, a lift that wobbled within the slop does not move the order
    s.clear();
    s.select({s.worker});
    hand.hold(g, Pt{g.x + 3, g.y - 2});
    check(s.sink.commands.size() == 1 && s.sink.commands[0].tile_x == t.x && s.sink.commands[0].tile_y == t.y, "a hold that lifts a little away still orders the tile of the down point");
    // a hold on nothing selected acts like a right click on nothing: no order
    s.clear();
    hand.hold(g, g);
    check(s.sink.commands.empty() && app.hud().get_selected_ant_ids().empty(), "a hold with nothing selected orders nothing and selects nothing");

    // THE DIFFERENTIAL: holds all over the view against right clicks (the press point and the release point: the lift may be elsewhere within the slop)
    const LayoutRect view = app.layout().view();
    uint32_t seed = 777;
    const std::vector<std::vector<uint32_t>> selections = {{}, {s.worker}, {s.bomber}, {s.worker, s.second}, {s.foe}};
    int different = 0;
    int compared = 0;
    int moves = 0;
    int specials = 0;
    for (size_t k = 0; k < selections.size(); ++k) {
        for (int n = 0; n < 16; ++n) {
            Pt p{view.x + 6 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.w - 12)), view.y + 6 + static_cast<int32_t>(lcg(seed) % static_cast<uint32_t>(view.h - 12))};
            if (n < 4) p = s.on_ant(std::vector<uint32_t>{s.worker, s.bomber, s.foe, s.second}[static_cast<size_t>(n)]);
            const Pt lift{p.x + static_cast<int32_t>(lcg(seed) % 5u) - 2, p.y + static_cast<int32_t>(lcg(seed) % 5u) - 2};
            s.clear();
            s.select(selections[k]);
            hand.hold(p, lift);
            const Outcome by_finger = outcome_of(s);
            s.clear();
            s.select(selections[k]);
            mouse_click(app, SDL_BUTTON_RIGHT, p, lift);
            forget_pointer(app);
            const Outcome by_mouse = outcome_of(s);
            ++compared;
            for (const sim::Command& c : by_mouse.commands) {
                moves += c.type == sim::CommandType::GroupMove ? 1 : 0;
                specials += c.type == sim::CommandType::GroupSpecial ? 1 : 0;
            }
            if (!same(by_finger, by_mouse)) {
                ++different;
                check(false, "a hold at " + show(p) + " (lift " + show(lift) + ", selection " + std::to_string(k) + ") is not the mouse's right click: " + describe(by_finger) + " against " + describe(by_mouse));
            }
        }
    }
    check(different == 0 && compared == 80, "80 holds all over the view, five selections: every one is the mouse's right click (" + std::to_string(different) + " differ)");
    check(moves >= 20 && specials >= 5, "(the grid has teeth: " + std::to_string(moves) + " moves and " + std::to_string(specials) + " special powers among the right clicks)");

    // THE MINIMAP: a hold sends the selected ants there, as the mouse's right click on the minimap does
    const LayoutRect mini = app.layout().minimap();
    int mini_different = 0;
    for (int n = 0; n < 6; ++n) {
        const Pt p{mini.x + 10 + n * (mini.w - 20) / 6, mini.y + 10 + (n % 3) * (mini.h - 20) / 3};
        s.clear();
        s.select({s.worker, s.second});
        hand.hold(p, p);
        const Outcome by_finger = outcome_of(s);
        s.clear();
        s.select({s.worker, s.second});
        mouse_click(app, SDL_BUTTON_RIGHT, p, p);
        forget_pointer(app);
        const Outcome by_mouse = outcome_of(s);
        if (!same(by_finger, by_mouse) || by_finger.commands.size() != 1) {
            ++mini_different;
            check(false, "a hold on the minimap at " + show(p) + " is not the mouse's right click: " + describe(by_finger) + " against " + describe(by_mouse));
        }
    }
    check(mini_different == 0, "six holds on the minimap: each sends the group where the mouse's right click does");
    // a short press on the minimap scrolls (the left button, at once) and orders nothing
    s.clear();
    s.select({s.worker});
    const Pt mp{mini.x + mini.w / 2, mini.y + mini.h / 2};
    hand.down(1, mp);
    hand.frame();
    check(app.hud().is_input_captured(), "a finger on the minimap holds it at once (the view follows it)");
    hand.wait(120);
    hand.up(1, mp);
    hand.frame();
    check(!app.hud().is_input_captured() && s.sink.commands.empty(), "a short press on the minimap orders nothing, and ends with the lift");
}

void test_hold_timing() {
    group("timing", "the hold fires when its time is up (a frame is enough), exactly once, and at the lift when no frame came; a lift in between does nothing");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    const Pt g = s.ground(2, 2);
    s.clear();
    s.select({s.worker});
    hand.down(1, g);
    hand.frame();
    hand.wait(touch::kHoldMs - 1);
    hand.frame();
    check(!app.hud().is_input_captured() && app.touch().mode() == TouchControl::Mode::Waiting && app.touch_feedbacks() == 0, "one millisecond before the hold time: nothing is held, no feedback");
    hand.wait(1);
    hand.frame();
    check(app.hud().is_input_captured() && app.touch().mode() == TouchControl::Mode::Right && app.touch_feedbacks() == 1, "at the hold time one frame later: the right button is held, the feedback is given");
    hand.wait(1000);
    hand.frame();
    check(s.sink.commands.empty() && app.touch_feedbacks() == 1, "holding for a second orders nothing (the game acts at the release) and gives the feedback once");
    hand.up(1, g);
    hand.frame();
    check(s.sink.commands.size() == 1 && !app.hud().is_input_captured(), "the lift is the order");

    // no frame between the down and a lift that is stamped 700 ms later (a stall: SDL stamps an event when it SEES it, so a stall makes a short touch look long): nothing says that the finger was
    // down all that time, so the lift is judged 100 ms after the down, a tap. (This block used to pin the opposite: "the hold was made at its time, then released: one order, one more
    // feedback"; the review found that a short tap or the start of a drag turned into a right click that way. The order is still one, a left click on the ground moves the worker.)
    {
        s.clear();
        s.select({s.worker});
        const TouchControl::Stats before = app.touch().stats();
        const uint32_t buzzes = app.touch_feedbacks();
        hand.down(1, g);
        hand.wait(700);
        hand.up(1, g);
        hand.frame();
        check(s.sink.commands.size() == 1 && app.touch().stats().taps == before.taps + 1 && app.touch().stats().holds == before.holds && app.touch_feedbacks() == buzzes,
              "a lift stamped 700 ms after the down, with no frame between: a tap (the one order is the left click's), no hold, no feedback");
    }

    // A STALLED tap: the finger leaves 100 ms after it went down, a 400 ms stall stamps the lift 500 ms after the down. It is a click (it selects the worker), never the right click (which
    // selects nothing and buzzes). A stalled first move begins a drag, never a hold. A slow tap (300 ms) with a 150 ms stall is a tap (it was lost in the dead zone, 438 ms).
    {
        const Pt on = s.on_ant(s.worker);
        s.clear();
        TouchControl::Stats before = app.touch().stats();
        uint32_t buzzes = app.touch_feedbacks();
        hand.down(1, on);
        hand.frame();
        hand.wait(30);
        hand.frame();
        hand.wait(470);
        hand.up(1, on);
        hand.frame();
        check(app.hud().get_selected_ant_ids() == std::vector<uint32_t>{s.worker} && app.touch().stats().taps == before.taps + 1 && app.touch().stats().holds == before.holds &&
                  app.touch_feedbacks() == buzzes && app.touch().fingers() == 0,
              "a tap whose lift a 400 ms stall stamped 500 ms after the down: a click on the worker (it is selected), no hold, no buzz");

        s.clear();
        before = app.touch().stats();
        hand.down(1, g);
        hand.frame();
        hand.wait(30);
        hand.frame();
        hand.wait(470);
        hand.move(1, Pt{g.x + 40, g.y});
        hand.frame();
        check(app.touch().stats().drags == before.drags + 1 && app.touch().stats().holds == before.holds && app.touch().mode() == TouchControl::Mode::Left && app.hud().is_input_captured(),
              "a first move stamped 500 ms after the down, the frame before it at 30 ms: a drag (the band), not a hold");
        hand.up(1, Pt{g.x + 40, g.y});
        hand.frame();
        check(app.touch().fingers() == 0 && !app.hud().is_input_captured() && app.touch_feedbacks() == buzzes, "... which ends with the lift, and nothing buzzed");

        s.clear();
        before = app.touch().stats();
        hand.down(1, on);
        hand.frame();
        for (int i = 0; i < 18; ++i) {
            hand.wait(16);
            hand.frame();
        }
        hand.wait(150);
        hand.up(1, on);
        hand.frame();
        check(app.hud().get_selected_ant_ids() == std::vector<uint32_t>{s.worker} && app.touch().stats().taps == before.taps + 1 && app.touch().stats().holds == before.holds,
              "a slow tap (the finger leaves at 300 ms) whose lift a 150 ms stall stamped at 438 ms: a click (it was a lost tap in the dead zone)");
    }

    // REAL holds at slow frame rates (16 ms, 100 ms, 250 ms between frames): the ring is seen while the hold is coming, the hold is made by the frame that first reaches its time and the
    // feedback is given once, and the lift is the order, as the mouse's right click gives it
    for (const uint32_t gap : {16u, 100u, 250u}) {
        const std::string rate = "frames every " + std::to_string(gap) + " ms: ";
        s.clear();
        s.select({s.worker});
        const TouchControl::Stats before = app.touch().stats();
        const uint32_t buzzes = app.touch_feedbacks();
        hand.down(1, g);
        hand.frame();
        bool ring_seen = false;
        for (uint32_t t = gap; t <= 700; t += gap) {
            hand.wait(gap);
            hand.frame();
            ring_seen = ring_seen || (t < touch::kHoldMs && app.touch().ring(hand.now).has_value());
        }
        check(ring_seen, rate + "the ring was seen while the hold was coming");
        check(app.hud().is_input_captured() && app.touch().mode() == TouchControl::Mode::Right && app.touch().stats().holds == before.holds + 1 && app.touch_feedbacks() == buzzes + 1,
              rate + "the right button is held, the hold counted and the feedback given once");
        check(s.sink.commands.empty(), rate + "nothing is ordered before the lift");
        hand.wait(20);
        hand.up(1, g);
        hand.frame();
        check(s.sink.commands.size() == 1 && s.sink.commands[0].type == sim::CommandType::GroupMove && !app.hud().is_input_captured() && app.touch().fingers() == 0, rate + "the lift is the order");
    }

    // the dead zone between the tap time and the hold time: a lift there does nothing
    s.clear();
    s.select({s.worker});
    hand.down(1, g);
    hand.frame();
    hand.rest(touch::kTapMs + 20);
    hand.up(1, g);
    hand.frame();
    check(s.sink.commands.empty() && app.touch().fingers() == 0, "a finger that lingers 420 ms and lifts: neither a click nor a right click");
    // a tap at the last millisecond before the tap time is a click
    hand.down(1, g);
    hand.frame();
    hand.rest(touch::kTapMs - 1);
    hand.up(1, g);
    hand.frame();
    check(s.sink.commands.size() == 1, "a tap that lifts 399 ms after the down is a click");

    // the events carry their times (a frame may come long after them): a lift that is stamped 300 ms after the down is a tap, though the frame that handles it comes 500 ms later, and a
    // move that is stamped at 100 ms begins a drag, though the frame comes at 800 ms (the hold that is due by then is not made first)
    {
        const TouchControl::Stats before = app.touch().stats();
        hand.down(1, g);
        hand.wait(300);
        hand.up(1, g);
        hand.wait(500);
        hand.frame();
        const TouchControl::Stats after = app.touch().stats();
        check(after.taps == before.taps + 1 && after.holds == before.holds && app.touch().fingers() == 0, "a lift stamped at 300 ms and handled at 800 ms is a tap, not the end of a hold");
        hand.down(1, g);
        hand.wait(100);
        hand.move(1, Pt{g.x + 40, g.y});
        hand.wait(700);
        hand.frame();
        const TouchControl::Stats dragging = app.touch().stats();
        check(dragging.drags == after.drags + 1 && dragging.holds == after.holds && app.touch().mode() == TouchControl::Mode::Left, "a move stamped at 100 ms and handled at 800 ms is a drag, not a hold that was due first");
        hand.up(1, Pt{g.x + 40, g.y});
        hand.frame();
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The ring and the pulse (touch_feedback.hpp): over the match while a hold is coming and after it fired; nothing else of the picture changes
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// A renderer that keeps the rectangles that are filled
class FillRecorder : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override { ++others; }
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override { ++others; }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override { fills.push_back(TouchPaint{x, y, w, h, color}); }
    void draw_rect(int32_t, int32_t, int32_t, int32_t, ants::assets::ColorRGBA) override { ++others; }
    void draw_text(const std::string&, int32_t, int32_t, ants::assets::ColorRGBA) override { ++others; }
    void set_hud_team(uint8_t) override {}
    std::vector<TouchPaint> fills;
    int others{0};
};

bool same_rects(const std::vector<TouchPaint>& a, const std::vector<TouchPaint>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const TouchPaint& p = a[i];
        const TouchPaint& q = b[i];
        if (p.x != q.x || p.y != q.y || p.w != q.w || p.h != q.h || p.color.r != q.color.r || p.color.g != q.color.g || p.color.b != q.color.b || p.color.a != q.color.a) return false;
    }
    return true;
}

/// A frame in which nothing moves (the simulation's time and the animations stand still), read back
Picture still_frame(Application& app) {
    app.run_frame_with_delta(0.0f);
    return read_canvas(app, app.renderer().canvas_w(), app.renderer().canvas_h());
}

/// The pixels of the map view in two pictures are the same (the frame rate in the corner, the minimap and the pedestals run on the wall clock: nothing is said of them)
bool same_view(const Application& app, const Picture& a, const Picture& b) {
    const LayoutRect view = app.layout().view();
    for (int32_t y = view.y + app.picture().y; y < view.y + view.h + app.picture().y; ++y) {
        for (int32_t x = view.x + app.picture().x; x < view.x + view.w + app.picture().x; ++x) {
            const uint8_t* p = a.at(x, y);
            const uint8_t* q = b.at(x, y);
            if (p[0] != q[0] || p[1] != q[1] || p[2] != q[2]) return false;
        }
    }
    return true;
}

/// What a frame shows beyond the still frame before the finger came: the pixels that differ, how many of those lie outside the rectangles that were expected, and how many of the expected
/// pixels differ. The pixels nearer than `ignore_inside` to the finger are not looked at (the pointer's cursor is there once the right button is down).
struct Drawn {
    int differing{0};
    int outside{0};
    int expected{0};
    int hit{0};
    double mean{0.0};
    int32_t left{0};                 // the box of the pixels that differ outside the expected ones (for the message of a failure)
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};
};

Drawn compare_with(const Picture& base, const Picture& frame, const Application& app, const std::vector<TouchPaint>& expected, double cx, double cy, double ignore_inside) {
    std::vector<uint8_t> mask(static_cast<size_t>(base.w) * static_cast<size_t>(base.h), 0);
    for (const TouchPaint& r : expected) {
        for (int32_t y = r.y; y < r.y + r.h; ++y) {
            for (int32_t x = r.x; x < r.x + r.w; ++x) {
                const int32_t px = x + app.picture().x;
                const int32_t py = y + app.picture().y;
                if (px >= 0 && py >= 0 && px < base.w && py < base.h) mask[static_cast<size_t>(py) * static_cast<size_t>(base.w) + static_cast<size_t>(px)] = 1;
            }
        }
    }
    Drawn d;
    double sum = 0.0;
    const LayoutRect view = app.layout().view();
    for (int32_t y = view.y + app.picture().y; y < view.y + view.h + app.picture().y; ++y) {
        for (int32_t x = view.x + app.picture().x; x < view.x + view.w + app.picture().x; ++x) {
            const double r = std::hypot(static_cast<double>(x - app.picture().x) + 0.5 - cx, static_cast<double>(y - app.picture().y) + 0.5 - cy);
            if (r < ignore_inside) continue;
            const bool wanted = mask[static_cast<size_t>(y) * static_cast<size_t>(base.w) + static_cast<size_t>(x)] != 0;
            d.expected += wanted ? 1 : 0;
            const uint8_t* p = base.at(x, y);
            const uint8_t* q = frame.at(x, y);
            if (p[0] == q[0] && p[1] == q[1] && p[2] == q[2]) continue;
            ++d.differing;
            sum += r;
            if (wanted) {
                ++d.hit;
            } else {
                if (d.outside == 0) {
                    d.left = d.right = x;
                    d.top = d.bottom = y;
                }
                ++d.outside;
                d.left = std::min(d.left, x);
                d.right = std::max(d.right, x);
                d.top = std::min(d.top, y);
                d.bottom = std::max(d.bottom, y);
            }
        }
    }
    d.mean = d.differing > 0 ? sum / d.differing : 0.0;
    return d;
}

void test_ring() {
    group("ring", "a ring closes around a finger that may become a hold and a pulse follows the hold: over the match, and nothing else of the picture changes");
    {   // what draw_touch_feedback draws, and in which order
        FillRecorder none;
        draw_touch_feedback(none, std::nullopt, std::nullopt, 9.6);
        check(none.fills.empty() && none.others == 0, "nothing to show: nothing is drawn");
        const TouchControl::Mark ring{200.5, 150.5, 0.4};
        const TouchControl::Mark pulse{320.5, 90.5, 0.2};
        const std::vector<TouchPaint> ring_rects = touch_ring_paint(ring, 9.6);
        const std::vector<TouchPaint> pulse_rects = touch_pulse_paint(pulse, 9.6);
        FillRecorder only_ring;
        draw_touch_feedback(only_ring, ring, std::nullopt, 9.6);
        check(!ring_rects.empty() && only_ring.others == 0 && same_rects(only_ring.fills, ring_rects), "a ring: the rectangles of the ring, filled and nothing else");
        FillRecorder only_pulse;
        draw_touch_feedback(only_pulse, std::nullopt, pulse, 9.6);
        check(!pulse_rects.empty() && only_pulse.others == 0 && same_rects(only_pulse.fills, pulse_rects), "a pulse: the rectangles of the pulse");
        FillRecorder both;
        draw_touch_feedback(both, ring, pulse, 9.6);
        const std::vector<TouchPaint> first(both.fills.begin(), both.fills.begin() + static_cast<std::ptrdiff_t>(std::min(pulse_rects.size(), both.fills.size())));
        const std::vector<TouchPaint> rest(both.fills.begin() + static_cast<std::ptrdiff_t>(std::min(pulse_rects.size(), both.fills.size())), both.fills.end());
        check(both.fills.size() == pulse_rects.size() + ring_rects.size() && same_rects(first, pulse_rects) && same_rects(rest, ring_rects), "both: the pulse first, the ring over it");
    }

    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    s.clear();                                                          // nothing selected: a hold orders nothing
    forget_pointer(app);                                                // (and the cursor is not in the pictures: it is gone, as after a finger)
    app.renderer().pin_animation_clock(1000);                           // (the terrain's animation runs on the wall clock otherwise: the pictures below must be the same where nothing is drawn)
    const Pt g = s.ground(2, 2);
    const double cx = static_cast<double>(g.x) + 0.37;                    // (the finger is not in the middle of its pixel: no pixel of the ring lies exactly on a circle, where a rounding of the
    const double cy = static_cast<double>(g.y) + 0.21;                    // finger's float could change what the ring covers)
    const Picture base = still_frame(app);
    check(app.pointer_outside() && same_view(app, base, still_frame(app)), "(a frame that moves nothing is the same picture again: the comparisons below are exact)");

    // THE RING: nothing for 150 ms (a tap shows none), then it closes around the finger until the hold's time
    hand.down(1, g, 0.37, 0.21);
    const double slop = app.touch_slop();
    check(same_view(app, still_frame(app), base), "a finger that has just landed: nothing is drawn");
    const uint32_t t0 = hand.now;
    hand.wait(touch::kRingStartMs - 1);
    check(same_view(app, still_frame(app), base), "149 ms: nothing is drawn (a tap is over by then)");
    double last_mean = 1.0e9;
    bool closing = true;
    for (const uint32_t ms : {150u, 200u, 300u, 400u, 449u}) {
        hand.wait(t0 + ms - hand.now);
        const Picture frame = still_frame(app);
        const double progress = static_cast<double>(ms - touch::kRingStartMs) / static_cast<double>(touch::kHoldMs - touch::kRingStartMs);
        const std::vector<TouchPaint> wanted = touch_ring_paint(TouchControl::Mark{cx, cy, progress}, slop);
        const Drawn d = compare_with(base, frame, app, wanted, cx, cy, 0.0);
        check(d.outside == 0 && d.expected > 400 && d.hit * 100 >= d.expected * 95, std::to_string(ms) + " ms: the picture differs from the still one at the ring's pixels and nowhere else (" + std::to_string(d.differing) + " pixels differ, " +
                                                                                      std::to_string(d.hit) + " of the ring's " + std::to_string(d.expected) + ", " + std::to_string(d.outside) + " outside it, in the box " + std::to_string(d.left) + "," + std::to_string(d.top) + " - " + std::to_string(d.right) + "," + std::to_string(d.bottom) + ")");
        closing = closing && d.mean < last_mean;
        last_mean = d.mean;
    }
    check(closing, "the ring closes: its pixels come nearer to the finger with every frame");
    check(app.hud().get_selected_ant_ids().empty() && s.sink.commands.empty() && !app.hud().is_input_captured(), "(the ring is a picture only: nothing is held and nothing was ordered)");

    // THE PULSE: the hold fires (one more frame), the ring is gone and a pulse goes out from where the ring ended, for 240 ms
    const double end = touch::ring_end_radius(slop);
    const double hole = end - touch::ring_stroke(slop) - 4.0;        // (the pointer's cursor lives inside; it is the right button's, and it is not ours)
    double last_reach = 0.0;
    bool growing = true;
    for (const uint32_t ms : {0u, 60u, 120u, 180u}) {
        hand.wait(t0 + touch::kHoldMs + ms - hand.now);
        const Picture frame = still_frame(app);
        const std::vector<TouchPaint> wanted = touch_pulse_paint(TouchControl::Mark{cx, cy, static_cast<double>(ms) / static_cast<double>(touch::kPulseMs)}, slop);
        const Drawn d = compare_with(base, frame, app, wanted, cx, cy, hole);
        check(d.outside == 0 && d.expected > 400 && d.hit * 100 >= d.expected * 85, "the pulse " + std::to_string(ms) + " ms after the hold: its pixels and nothing else outside the cursor's own place (" + std::to_string(d.differing) + " differ, " +
                                                                                       std::to_string(d.hit) + " of " + std::to_string(d.expected) + ", " + std::to_string(d.outside) + " outside)");
        growing = growing && d.mean > last_reach;
        last_reach = d.mean;
    }
    check(growing, "the pulse goes out: its pixels get farther from the finger with every frame");
    check(app.hud().is_input_captured() && app.touch().mode() == TouchControl::Mode::Right, "(the right button is held: the hold fired)");
    hand.wait(t0 + touch::kHoldMs + touch::kPulseMs + 5 - hand.now);
    {
        const Drawn d = compare_with(base, still_frame(app), app, {}, cx, cy, hole);
        check(d.differing == 0, "240 ms after the hold: the pulse is gone, nothing but the pointer's cursor is left (" + std::to_string(d.differing) + " pixels differ outside its place)");
    }
    hand.up(1, g);
    check(same_view(app, still_frame(app), base), "after the lift the picture is the still one again, pixel for pixel");
    check(s.sink.commands.empty(), "(and nothing was ordered: nothing was selected)");

    // NO RING where there is no hold coming
    hand.down(1, g);                                                    // a tap
    hand.wait(80);
    check(same_view(app, still_frame(app), base), "a tap: nothing is drawn while the finger is down");
    hand.up(1, g);
    hand.wait(10);
    check(same_view(app, still_frame(app), base), "(and after its lift)");
    // a drag: the finger leaves the slop at once, the band is drawn, and no ring joins it at 300 ms
    hand.down(1, g);
    hand.wait(20);
    hand.move(1, Pt{g.x + 40, g.y + 30});
    const Picture band_early = still_frame(app);
    hand.wait(280);
    check(same_view(app, still_frame(app), band_early), "a finger that left the slop has a band and no ring: the picture at 300 ms is the one at 20 ms");
    hand.up(1, Pt{g.x + 40, g.y + 30});
    hand.wait(10);
    still_frame(app);
    s.clear();
    forget_pointer(app);
    check(same_view(app, still_frame(app), base), "(the band is gone with the lift)");
    // a dialog opens over the map while a hold is coming: nothing of the ring is drawn over it
    hand.down(1, g);
    still_frame(app);                                                   // (the finger is down on the map: it waits)
    hand.wait(300);
    app.hud().open_quit_dialog();
    const Picture over_dialog = still_frame(app);
    check(app.touch().mode() == TouchControl::Mode::Waiting, "(the finger still waits under the dialog)");
    app.cancel_touch();
    const Picture dialog_alone = still_frame(app);
    check(app.touch().fingers() == 0 && same_view(app, over_dialog, dialog_alone), "a dialog that opened over a finger that waits: no ring over the dialog (the picture is the dialog's alone)");
    hand.up(1, g);
    hand.wait(10);
    still_frame(app);
    app.hud().close_quit_dialog();
    forget_pointer(app);
    check(same_view(app, still_frame(app), base), "(the dialog is closed: the picture is the still one again)");
    // a second finger turns the hold that was coming into a pan: the ring goes
    hand.down(1, g);
    hand.wait(300);
    const Pt other{g.x + 60, g.y + 10};
    hand.down(2, other);
    check(same_view(app, still_frame(app), base) && app.touch().mode() == TouchControl::Mode::Two, "a second finger lands while the ring is closing: the ring is gone, the pair waits");
    hand.up(2, other);
    hand.up(1, g);
    hand.wait(10);
    still_frame(app);
    // a cancel (the browser took the touch) takes the ring away
    hand.down(1, g);
    still_frame(app);
    hand.wait(300);
    check(!same_view(app, still_frame(app), base), "(the ring is there before the cancel)");
    app.cancel_touch();
    check(same_view(app, still_frame(app), base), "a cancel: the ring is gone with the finger");
}

void test_pan() {
    group("pan", "two fingers scroll the map by the same distance on the screen, at every zoom, inside the map");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    (void)s;
    Hand hand(app);
    ViewportCamera& camera = app.renderer().camera();
    const uint32_t map_w = app.sim().grid().width();
    const uint32_t map_h = app.sim().grid().height();
    for (const float level : {1.0f, 0.5f, 2.0f}) {
        app.renderer().camera().zoom = 1.0f;
        check(app.set_zoom(level, 100, 100) || level == 1.0f, "the zoom " + zoom::level_name(level) + " is set");
        camera.set_origin(500.0, 500.0, map_w, map_h);
        const int32_t before_x = camera.origin_screen_x();
        const int32_t before_y = camera.origin_screen_y();
        const LayoutRect view = app.layout().view();
        const Pt a{view.x + 300, view.y + 200};
        const Pt b{view.x + 400, view.y + 200};
        hand.down(1, a);
        hand.down(2, b);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Two, "two fingers on the map view are a pair");
        // both fingers move together by (+30, +20): the world follows, so the view goes the other way by the same distance on the screen
        for (int i = 1; i <= 5; ++i) {
            hand.wait(16);
            hand.move(1, Pt{a.x + 6 * i, a.y + 4 * i});
            hand.move(2, Pt{b.x + 6 * i, b.y + 4 * i});
            hand.frame();
        }
        const int32_t moved_x = camera.origin_screen_x() - before_x;
        const int32_t moved_y = camera.origin_screen_y() - before_y;
        check(std::abs(moved_x + 30) <= 1 && std::abs(moved_y + 20) <= 1, "at the zoom " + zoom::level_name(level) + " the view moved (" + std::to_string(moved_x) + ", " + std::to_string(moved_y) + ") screen pixels for fingers that moved (+30, +20): the opposite way, by the same distance");
        check(app.zoom() == level, "the zoom did not change (the distance between the fingers did not)");
        hand.up(1, Pt{a.x + 30, a.y + 20});
        hand.up(2, Pt{b.x + 30, b.y + 20});
        hand.frame();
        check(app.touch().fingers() == 0 && !app.hud().is_input_captured(), "afterwards nothing is held or tracked");
    }
    // the map's edge: the view stops, nothing breaks
    app.renderer().camera().zoom = 1.0f;
    app.set_zoom(1.0f, 100, 100);
    camera.set_origin(0.0, 0.0, map_w, map_h);
    const LayoutRect view = app.layout().view();
    hand.down(1, Pt{view.x + 300, view.y + 200});
    hand.down(2, Pt{view.x + 400, view.y + 200});
    hand.frame();
    hand.move(1, Pt{view.x + 330, view.y + 230});
    hand.move(2, Pt{view.x + 430, view.y + 230});
    hand.frame();
    check(camera.world_x == 0 && camera.world_y == 0, "dragging the world right and down at the map's top left corner moves nothing (the view is held inside the map)");
    hand.up(1, Pt{view.x + 330, view.y + 230});
    hand.up(2, Pt{view.x + 430, view.y + 230});
    hand.frame();
    // pan_view itself: the same as the minimap's and the edge scroll's step (ViewportCamera::scroll_screen), held inside the map
    camera.set_origin(500.0, 500.0, map_w, map_h);
    const float x0 = camera.x;
    const float y0 = camera.y;
    check(app.pan_view(10, -6) && camera.x == x0 - 10.0f && camera.y == y0 + 6.0f, "pan_view(10, -6) moves the view (-10, +6): the world follows the fingers");
    camera.set_origin(0.0, 0.0, map_w, map_h);
    check(!app.pan_view(40, 40) && camera.x == 0.0f && camera.y == 0.0f, "pan_view returns false when the view cannot move");
}

void test_pinch() {
    group("pinch", "two fingers zoom the view level by level inside zoom_limits(), anchored at the middle point");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Hand hand(app);
    ViewportCamera& camera = app.renderer().camera();
    const LayoutRect view = app.layout().view();
    camera.set_origin(450.0, 500.0, app.sim().grid().width(), app.sim().grid().height());
    const std::vector<float> levels = app.zoom_levels();
    check(levels.size() >= 9 && app.zoom() == 1.0f, "the application offers its levels (" + std::to_string(levels.size()) + ") and starts at 1");
    const Pt mid{view.x + 380, view.y + 250};
    const auto world_under = [&](Pt p) { return std::make_pair(camera.world_x_at(p.x - view.x), camera.world_y_at(p.y - view.y)); };
    const auto before = world_under(mid);
    // fingers 100 apart, spread to 200: the ratio 2 is as far in as the levels go; the middle stays where it is
    hand.down(1, Pt{mid.x - 50, mid.y});
    hand.down(2, Pt{mid.x + 50, mid.y});
    hand.frame();
    for (int i = 1; i <= 10; ++i) {
        hand.wait(16);
        hand.move(1, Pt{mid.x - 50 - 5 * i, mid.y});
        hand.move(2, Pt{mid.x + 50 + 5 * i, mid.y});
        hand.frame();
    }
    check(app.zoom() > 1.0f && app.zoom() <= 2.0f, "a spread to twice the distance zooms in: " + zoom::level_name(app.zoom()));
    const auto after = world_under(mid);
    check(std::abs(after.first - before.first) <= 2 && std::abs(after.second - before.second) <= 2, "the world point under the middle of the fingers stayed under it: (" + std::to_string(before.first) + ", " + std::to_string(before.second) + ") then (" + std::to_string(after.first) + ", " + std::to_string(after.second) + ")");
    check(std::find(levels.begin(), levels.end(), app.zoom()) != levels.end(), "the zoom is one of the levels on offer");
    check(app.remembered_zoom() == app.zoom(), "the level is remembered, as the wheel's is");
    hand.up(1, Pt{mid.x - 100, mid.y});
    hand.up(2, Pt{mid.x + 100, mid.y});
    hand.frame();
    check(app.touch().fingers() == 0, "the fingers lift");
    // pinch in: down to the last level (the map's limit), never further
    hand.down(1, Pt{mid.x - 150, mid.y});
    hand.down(2, Pt{mid.x + 150, mid.y});
    hand.frame();
    for (int i = 1; i <= 29; ++i) {
        hand.wait(16);
        hand.move(1, Pt{mid.x - 150 + 5 * i, mid.y});
        hand.move(2, Pt{mid.x + 150 - 5 * i, mid.y});
        hand.frame();
    }
    check(app.zoom() == levels.back(), "a long pinch ends at the last level, the map's limit: " + zoom::level_name(app.zoom()) + ", the limit " + zoom::level_name(levels.back()));
    hand.up(1, Pt{mid.x, mid.y});
    hand.up(2, Pt{mid.x, mid.y});
    hand.frame();
    // the same levels as the wheel's, in both directions
    const float pinched = app.zoom();
    app.set_zoom(1.0f, mid.x, mid.y);
    notch(app, -1);
    const float one_out = app.zoom();
    check(one_out == levels[5] && pinched <= one_out, "the wheel's levels are the pinch's: a notch out of 1 is " + zoom::level_name(one_out));
    // the renderer cannot make its offscreen target: only the level 1 is offered, and a pinch changes nothing (as the wheel)
    app.renderer().set_fail_world_target(true);
    app.run_frame_with_delta(0.016f);
    check(app.zoom() == 1.0f && app.zoom_levels().size() == 1, "(without the offscreen target the zoom is held at 1)");
    hand.down(1, Pt{mid.x - 50, mid.y});
    hand.down(2, Pt{mid.x + 50, mid.y});
    hand.frame();
    for (int i = 1; i <= 10; ++i) {
        hand.wait(16);
        hand.move(1, Pt{mid.x - 50 - 5 * i, mid.y});
        hand.move(2, Pt{mid.x + 50 + 5 * i, mid.y});
        hand.frame();
    }
    check(app.zoom() == 1.0f, "a pinch that the wheel could not make is refused too");
    hand.up(1, Pt{mid.x - 100, mid.y});
    hand.up(2, Pt{mid.x + 100, mid.y});
    hand.frame();
    app.renderer().set_fail_world_target(false);
}

void test_gates() {
    group("gates", "two fingers do nothing where the wheel does nothing, and a tap is still a click there");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    ViewportCamera& camera = app.renderer().camera();
    const LayoutRect view = app.layout().view();
    const uint32_t map_w = app.sim().grid().width();
    const uint32_t map_h = app.sim().grid().height();
    const Pt a{view.x + 300, view.y + 220};
    const Pt b{view.x + 400, view.y + 220};
    // the two fingers' attempt: a pan and a spread; true when anything moved
    const auto attempt = [&]() {
        camera.set_origin(500.0, 500.0, map_w, map_h);
        const float x0 = camera.x;
        const float y0 = camera.y;
        const float z0 = app.zoom();
        hand.down(1, a);
        hand.down(2, b);
        hand.frame();
        for (int i = 1; i <= 8; ++i) {
            hand.wait(16);
            hand.move(1, Pt{a.x - 8 * i + 3 * i, a.y + 3 * i});
            hand.move(2, Pt{b.x + 8 * i + 3 * i, b.y + 3 * i});
            hand.frame();
        }
        const bool moved = camera.x != x0 || camera.y != y0 || app.zoom() != z0;
        hand.up(1, a);
        hand.up(2, b);
        hand.frame();
        return moved;
    };
    check(attempt(), "(the attempt moves the view when nothing is open)");
    camera.zoom = 1.0f;
    app.set_zoom(1.0f, 100, 100);

    struct Case {
        const char* name;
        std::function<void()> open;
        std::function<void()> close;
    };
    const std::vector<Case> cases = {
        {"the options window", [&] { app.hud().open_options(); }, [&] { app.hud().handle_key_down(SDLK_RETURN, app.sim(), app.renderer().camera()); }},
        {"the quick help", [&] { app.hud().open_quick_help(); }, [&] { app.hud().close_quick_help(); }},
        {"the quit dialog", [&] { app.hud().open_quit_dialog(); }, [&] { app.hud().close_quit_dialog(); }},
        {"the get ready dialog", [&] { app.hud().start_match_modal(); }, [&] { app.hud().dismiss_match_start_modal(); }},
        {"the results", [&] { app.scorecard().show(app.sim().get_world_state().match_result, 0); }, [&] { app.scorecard().hide(); }},
        {"the catch-up screen of a network match", [&] { app.force_catch_up_screen_for_test(true); }, [&] { app.force_catch_up_screen_for_test(false); }},
    };
    for (const Case& c : cases) {
        c.open();
        check(!attempt(), std::string("two fingers do nothing with ") + c.name + " open");
        check(app.touch().fingers() == 0, std::string("(and track nothing afterwards, with ") + c.name + " open)");
        c.close();
        camera.zoom = 1.0f;
    }
    check(attempt(), "after all of them: two fingers move the view again");
    camera.zoom = 1.0f;
    app.set_zoom(1.0f, 100, 100);

    // THE CATCH-UP SCREEN of a network match (connecting, loading, catching up: the loading picture is on the screen and the match is not; every mouse path of the game is shut then). The
    // view is not open to touches: two fingers would pan the hidden camera and pinch the remembered zoom, and a resting finger would draw a ring and a pulse over the picture and buzz.
    {
        app.force_catch_up_screen_for_test(true);
        const float remembered = app.remembered_zoom();
        check(!attempt() && app.remembered_zoom() == remembered, "the catch-up screen: two fingers pan and pinch nothing, and the remembered zoom is not touched");
        const uint32_t holds = app.touch().stats().holds;
        const uint32_t buzzes = app.touch_feedbacks();
        hand.down(1, a);
        hand.frame();
        hand.rest(touch::kHoldMs + 100);
        check(app.touch().stats().holds == holds && app.touch_feedbacks() == buzzes && app.touch().mode() == TouchControl::Mode::Left, "a finger that rests on the catch-up screen is no hold: no buzz, a plain press that the screen swallows");
        check(!app.touch().ring(hand.now).has_value() && !app.touch().pulse(hand.now).has_value(), "... and has no ring and no pulse");
        hand.up(1, a);
        hand.frame();
        s.clear();
        hand.tap(s.on_ant(s.worker));
        check(app.hud().get_selected_ant_ids().empty() && app.touch().fingers() == 0, "a tap on an ant of the hidden match selects nothing (the mouse's click does not either)");
        // a finger that holds a HUD button when the screen comes up: a cancel still lets the button go, and it does not fire (the match's own presses end with no act)
        app.force_catch_up_screen_for_test(false);
        const UIButton help = app.hud().help_button();
        const Pt on_help{help.x + help.w / 2, help.y + help.h / 2};
        hand.down(1, on_help);
        hand.frame();
        check(app.hud().help_button().is_pressed, "(the Help button is held by the finger)");
        app.force_catch_up_screen_for_test(true);
        app.cancel_touch();
        hand.frame();
        check(!app.hud().help_button().is_pressed && !app.hud().is_quick_help_open() && app.touch().fingers() == 0, "a cancel on the catch-up screen still lets the button go, and it does not fire");
        app.force_catch_up_screen_for_test(false);
        hand.up(1, on_help);
        hand.frame();
        // a finger that began on the map and waits when the screen comes up: no ring is drawn over the picture, and a second finger that lands then is no pair
        {
            hand.down(1, a);
            hand.frame();
            hand.wait(300);
            app.force_catch_up_screen_for_test(true);
            const Picture over = still_frame(app);
            check(app.touch().mode() == TouchControl::Mode::Waiting, "(the finger still waits when the catch-up screen comes up)");
            const uint32_t pairs = app.touch().stats().two_finger;
            hand.down(2, b);
            hand.frame();
            check(app.touch().mode() == TouchControl::Mode::Waiting && app.touch().ignored() == 1 && app.touch().stats().two_finger == pairs,
                  "a second finger that lands after the catch-up screen came up under the first one is ignored: no pair");
            hand.up(2, b);
            hand.frame();
            app.cancel_touch();
            const Picture alone = still_frame(app);
            check(app.touch().fingers() == 0 && same_view(app, over, alone), "no ring over the catch-up screen: the picture with the waiting finger is the picture without it");
            hand.up(1, a);
            hand.frame();
            app.force_catch_up_screen_for_test(false);
        }
        // the screen comes up in the middle of a pinch (the link drops): the gesture is over, no pan, no zoom, no finger tracked
        camera.set_origin(500.0, 500.0, map_w, map_h);
        const float x0 = camera.x;
        const float y0 = camera.y;
        const float z0 = app.zoom();
        hand.down(1, a);
        hand.down(2, b);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Two, "(the pair is on)");
        app.force_catch_up_screen_for_test(true);
        hand.wait(16);
        hand.move(1, Pt{a.x - 40, a.y + 20});
        hand.move(2, Pt{b.x + 40, b.y + 20});
        hand.frame();
        check(camera.x == x0 && camera.y == y0 && app.zoom() == z0 && app.touch().fingers() == 0, "the catch-up screen that comes up over a pair ends the gesture: no pan, no zoom, no finger is tracked");
        app.force_catch_up_screen_for_test(false);
        hand.up(2, Pt{b.x + 40, b.y + 20});
        hand.up(1, Pt{a.x - 40, a.y + 20});
        hand.frame();
        check(attempt(), "(and when the screen is gone, two fingers move the view again)");
        camera.zoom = 1.0f;
        app.set_zoom(1.0f, 100, 100);
    }

    // a held press: the first finger holds a button, the minimap, the chat log: the second finger is ignored (and the first finger's press goes on)
    {
        const UIButton help = app.hud().help_button();
        const Pt on_help{help.x + help.w / 2, help.y + help.h / 2};
        camera.set_origin(500.0, 500.0, map_w, map_h);
        const float x0 = camera.x;
        hand.down(1, on_help);
        hand.frame();
        check(app.hud().help_button().is_pressed, "(the Help button is pressed by the first finger)");
        hand.down(2, a);
        hand.wait(30);
        hand.move(2, Pt{a.x + 40, a.y + 30});
        hand.frame();
        check(camera.x == x0 && app.touch().mode() == TouchControl::Mode::Left && app.hud().help_button().is_pressed, "a second finger on the map while a button is held: nothing moves, the button is still held");
        hand.up(2, Pt{a.x + 40, a.y + 30});
        hand.up(1, on_help);
        hand.frame();
        check(app.hud().is_quick_help_open(), "the lift of the first finger presses the button: the quick help opens (a button acts at its release)");
        app.hud().close_quick_help();
        hand.wait(1000);
        hand.frame();
    }
    {
        const LayoutRect mini = app.layout().minimap();
        const Pt on_mini{mini.x + mini.w / 2, mini.y + mini.h / 2};
        camera.set_origin(500.0, 500.0, map_w, map_h);
        hand.down(1, on_mini);
        hand.frame();
        const float z0 = app.zoom();
        const uint32_t pairs = app.touch().stats().two_finger;
        hand.down(2, a);
        hand.move(2, Pt{a.x + 80, a.y});
        hand.frame();
        check(app.zoom() == z0 && app.touch().mode() == TouchControl::Mode::MinimapWait && app.touch().stats().two_finger == pairs && app.touch().ignored() == 1 && app.hud().is_input_captured(),
              "a second finger while the first holds the minimap: ignored (no pair), and the minimap press goes on");
        hand.up(2, Pt{a.x + 80, a.y});
        hand.up(1, on_mini);
        hand.frame();
    }

    // a press that a MOUSE holds (a touch screen and a mouse on one computer): two fingers on the map are refused while it is held, whatever it is: a button, the minimap with either button,
    // the chat log's drag; the second finger is ignored (no pair), and when nothing is held the same two fingers are a pair
    {
        struct Held {
            const char* name;
            uint8_t button;
            Pt at;
        };
        const auto middle = [](const UIButton& button) { return Pt{button.x + button.w / 2, button.y + button.h / 2}; };
        const LayoutRect mini = app.layout().minimap();
        const LayoutRect chat = app.layout().chat_view();
        const std::vector<Held> held = {
            {"the Help button", SDL_BUTTON_LEFT, middle(app.hud().help_button())},
            {"the Options button", SDL_BUTTON_LEFT, middle(app.hud().options_button())},
            {"the Quit button", SDL_BUTTON_LEFT, middle(app.hud().quit_button())},
            {"the [All] button", SDL_BUTTON_LEFT, middle(app.hud().send_to_button())},
            {"the team button", SDL_BUTTON_LEFT, middle(app.hud().team_button())},
            {"the minimap with the left button", SDL_BUTTON_LEFT, Pt{mini.x + mini.w / 2, mini.y + mini.h / 2}},
            {"the minimap with the right button", SDL_BUTTON_RIGHT, Pt{mini.x + mini.w / 2, mini.y + mini.h / 2}},
            {"the chat log", SDL_BUTTON_LEFT, Pt{chat.x + chat.w / 2, chat.y + chat.h / 2}},
        };
        const auto pair_forms = [&]() {                                    // two fingers land on the map: do they make a pair?
            hand.down(1, a);
            hand.frame();
            hand.down(2, b);
            hand.frame();
            const bool pair = app.touch().mode() == TouchControl::Mode::Two;
            const size_t ignored = app.touch().ignored();
            hand.up(2, b);
            hand.up(1, a);
            hand.frame();
            return std::make_pair(pair, ignored);
        };
        s.clear();
        check(pair_forms() == std::make_pair(true, size_t{0}), "(with nothing held two fingers on the map are a pair)");
        for (const Held& h : held) {
            app.hud().set_on_team(true);                                   // (the team button is there for a seat that has an ally; every frame sets the flag from the world again)
            mouse_press(app, h.button, h.at);
            check(app.hud().is_input_captured() || app.hud().chat_dragging(), std::string("(the mouse holds ") + h.name + ")");
            const auto got = pair_forms();
            check(got == std::make_pair(false, size_t{1}), std::string("two fingers on the map while the mouse holds ") + h.name + ": no pair, the second finger is ignored");
            mouse_release(app, h.button, Pt{a.x, a.y + 60});               // (let go away from it: nothing fires)
            forget_pointer(app);
            check(!app.hud().is_input_captured() && !app.hud().chat_dragging(), std::string("(the mouse let go of ") + h.name + ")");
            s.clear();
            check(!app.hud().is_modal_open(), std::string("(and nothing opened: ") + h.name + ")");
            check(pair_forms() == std::make_pair(true, size_t{0}), std::string("(and then two fingers are a pair again, after ") + h.name + ")");
        }
    }

    // the second finger lands AFTER something opened under the first: the first finger began on the map, then the results came up (the match ended): no pair
    {
        hand.down(1, a);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Waiting, "(a finger waits on the map)");
        const uint32_t pairs = app.touch().stats().two_finger;
        app.scorecard().show(app.sim().get_world_state().match_result, 0);
        hand.down(2, b);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Waiting && app.touch().ignored() == 1 && app.touch().stats().two_finger == pairs, "a second finger that lands after the results came up under the first one is ignored: no pair");
        hand.up(2, b);
        hand.up(1, a);
        hand.frame();
        app.scorecard().hide();
        check(app.touch().fingers() == 0, "(both fingers are gone)");
    }

    // where two fingers act is decided at their MIDDLE point: a second finger that puts the middle point over the panel is ignored, one over the panel whose middle point is over the map is not
    {
        const Pt edge{view.x + view.w - 12, view.y + 200};
        const Pt far_right{view.x + view.w + 150, view.y + 200};
        hand.down(1, edge);
        hand.frame();
        hand.down(2, far_right);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Waiting && app.touch().ignored() == 1, "a second finger that puts the middle point over the HUD's panel is ignored (no pair)");
        hand.up(2, far_right);
        hand.up(1, edge);
        hand.frame();
        const Pt inner{view.x + view.w - 160, view.y + 200};
        const Pt near_panel{view.x + view.w + 40, view.y + 200};
        hand.down(1, inner);
        hand.frame();
        hand.down(2, near_panel);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Two && app.touch().ignored() == 0, "a second finger over the panel whose middle point with the first is over the map makes a pair");
        hand.up(2, near_panel);
        hand.up(1, inner);
        hand.frame();
        check(app.touch().fingers() == 0, "(both fingers are gone)");
    }

    // a window comes up in the middle of a gesture: the pair began while the view was open, the next move finds a quit dialog over it: the gesture is over (no pan, no zoom), the fingers are dropped
    {
        camera.set_origin(500.0, 500.0, map_w, map_h);
        camera.zoom = 1.0f;
        const float x0 = camera.x;
        const float y0 = camera.y;
        const float z0 = app.zoom();
        hand.down(1, a);
        hand.down(2, b);
        hand.frame();
        check(app.touch().mode() == TouchControl::Mode::Two, "(the pair is on)");
        app.hud().open_quit_dialog();
        hand.wait(16);
        hand.move(1, Pt{a.x - 40, a.y + 20});
        hand.move(2, Pt{b.x + 40, b.y + 20});
        hand.frame();
        check(camera.x == x0 && camera.y == y0 && app.zoom() == z0 && app.touch().fingers() == 0, "a window that opens over a pair ends the gesture: no pan, no zoom, no finger is tracked");
        app.hud().close_quit_dialog();
        hand.up(2, Pt{b.x + 40, b.y + 20});
        hand.up(1, Pt{a.x - 40, a.y + 20});
        hand.frame();
        camera.zoom = 1.0f;
        app.set_zoom(1.0f, 100, 100);
    }

    // taps are still clicks where two fingers do nothing: the quit dialog's No, the options window's Return
    {
        app.hud().open_quit_dialog();
        const UIButton no = app.hud().quit_no_button();
        const LayoutPoint modal = app.layout().modal_offset();
        hand.tap(Pt{modal.x + no.x + no.w / 2, modal.y + no.y + no.h / 2});
        check(!app.hud().is_quit_dialog_open(), "a tap on the quit dialog's No closes the dialog");
        app.hud().open_quit_dialog();
        const UIButton yes = app.hud().quit_yes_button();
        hand.down(1, Pt{modal.x + yes.x + yes.w / 2, modal.y + yes.y + yes.h / 2});
        hand.down(2, a);                                                  // (a second finger rests on the map: a dialog is open, two fingers do nothing)
        hand.wait(60);
        hand.up(2, a);
        hand.up(1, Pt{modal.x + no.x + no.w / 2, modal.y + no.y + no.h / 2});     // (the first finger slid to No before it lifted: nothing is pressed on it)
        hand.frame();
        check(app.hud().is_quit_dialog_open(), "a finger that pressed Yes and lifted on No does not close the dialog (a captured button, as for the mouse)");
        app.hud().close_quit_dialog();
        app.hud().open_options();
        const LayoutPoint page = app.layout().options_offset();
        hand.tap(Pt{page.x + OptionsScreen::RETURN_X + OptionsScreen::RETURN_W / 2, page.y + OptionsScreen::RETURN_Y + OptionsScreen::RETURN_H / 2});
        check(!app.hud().is_modal_open(), "a tap on the options window's Return closes it");
    }
    (void)s;
}

void test_second_finger_cancels() {
    group("takeover", "the second finger ends a held press with no selection and no order, and the pair follows");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    const LayoutRect view = app.layout().view();
    const sim::AntSnapshot* w = s.ant(s.worker);
    if (w == nullptr) return check(false, "the worker is there");
    // a drag over the worker, then a second finger: the band is dropped, nothing is selected
    s.clear();
    const Pt p = s.screen_of(w->px - 50, w->py - 60);
    const Pt q = s.screen_of(w->px + 50, w->py + 30);
    hand.down(1, p);
    hand.frame();
    hand.wait(30);
    hand.move(1, q);
    hand.frame();
    check(app.hud().is_input_captured(), "(the band is held)");
    hand.wait(30);
    hand.down(2, Pt{view.x + view.w - 100, view.y + 80});
    hand.frame();
    check(!app.hud().is_input_captured() && app.touch().mode() == TouchControl::Mode::Two, "the second finger lands: the band is dropped and the pair begins");
    hand.up(1, q);
    hand.up(2, Pt{view.x + view.w - 100, view.y + 80});
    hand.frame();
    check(app.hud().get_selected_ant_ids().empty() && s.sink.commands.empty(), "nothing was selected and no order was given");
    check(app.pointer_outside(), "no finger is the pointer afterwards");
    // a hold, then a second finger: the right press is dropped, no order
    s.clear();
    s.select({s.worker});
    const Pt g = s.ground(2, 1);
    hand.down(1, g);
    hand.frame();
    hand.wait(touch::kHoldMs + 10);
    hand.frame();
    check(app.hud().is_input_captured(), "(the right button is held)");
    hand.down(2, Pt{g.x + 120, g.y});
    hand.frame();
    check(!app.hud().is_input_captured(), "the second finger lands: the right button's press is dropped");
    hand.up(1, g);
    hand.up(2, Pt{g.x + 120, g.y});
    hand.frame();
    check(s.sink.commands.empty(), "no order was given");
    // a waiting finger: nothing at all happens to the game (no click)
    s.clear();
    s.select({s.worker});
    hand.down(1, g);
    hand.frame();
    hand.wait(60);
    hand.down(2, Pt{g.x + 120, g.y});
    hand.frame();
    hand.up(2, Pt{g.x + 120, g.y});
    hand.up(1, g);
    hand.frame();
    check(s.sink.commands.empty() && app.hud().get_selected_ant_ids() == std::vector<uint32_t>{s.worker}, "a waiting finger that a second finger joins clicks nothing");
}

void test_cancel() {
    group("cancel", "a touch that the browser takes away, a lost focus and a page that is hidden leave no press held and no finger tracked");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    Hand hand(app);
    const sim::AntSnapshot* w = s.ant(s.worker);
    if (w == nullptr) return check(false, "the worker is there");
    const Pt p = s.screen_of(w->px - 50, w->py - 60);
    const Pt q = s.screen_of(w->px + 50, w->py + 30);
    const auto begin_band = [&]() {
        s.clear();
        hand.down(1, p);
        hand.frame();
        hand.wait(30);
        hand.move(1, q);
        hand.frame();
    };
    // cancel_touch (the page's touchcancel)
    begin_band();
    check(app.hud().is_input_captured(), "(the band is held)");
    app.cancel_touch();
    hand.frame();
    check(!app.hud().is_input_captured() && app.touch().fingers() == 0, "cancel_touch: the band is dropped and no finger is tracked");
    hand.up(1, q);                                                         // SDL turns a touchcancel into the finger's lift as well: unknown now, nothing
    hand.frame();
    check(app.hud().get_selected_ant_ids().empty() && s.sink.commands.empty(), "the lift that follows selects nothing: the cancel ended the band with no act");
    // the window loses the focus
    begin_band();
    app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_FOCUS_LOST; return we; }());
    hand.frame();
    check(!app.hud().is_input_captured() && app.touch().fingers() == 0, "a lost focus: the band is dropped and no finger is tracked");
    app.handle_window_event([] { SDL_WindowEvent we{}; we.type = SDL_WINDOWEVENT; we.event = SDL_WINDOWEVENT_FOCUS_GAINED; return we; }());
    // the page is hidden
    begin_band();
    app.set_page_hidden(true);
    hand.frame();
    check(!app.hud().is_input_captured() && app.touch().fingers() == 0, "a hidden page: the same");
    app.set_page_hidden(false);
    // the window is minimised or hidden (and brought back)
    for (const auto& [gone, back] : std::vector<std::pair<SDL_WindowEventID, SDL_WindowEventID>>{{SDL_WINDOWEVENT_MINIMIZED, SDL_WINDOWEVENT_RESTORED}, {SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_SHOWN}}) {
        begin_band();
        check(app.hud().is_input_captured(), "(the band is held)");
        SDL_WindowEvent away{};
        away.type = SDL_WINDOWEVENT;
        away.event = static_cast<Uint8>(gone);
        app.handle_window_event(away);
        hand.frame();
        check(!app.hud().is_input_captured() && app.touch().fingers() == 0, std::string("a window that is ") + (gone == SDL_WINDOWEVENT_MINIMIZED ? "minimised" : "hidden") + ": the band is dropped and no finger is tracked");
        SDL_WindowEvent returned{};
        returned.type = SDL_WINDOWEVENT;
        returned.event = static_cast<Uint8>(back);
        app.handle_window_event(returned);
    }
    // a hold's right press
    s.clear();
    s.select({s.worker});
    const Pt g = s.ground(2, 1);
    hand.down(1, g);
    hand.frame();
    hand.wait(touch::kHoldMs + 5);
    hand.frame();
    check(app.hud().is_input_captured(), "(the right button is held)");
    app.cancel_touch();
    hand.frame();
    hand.up(1, g);
    hand.frame();
    check(!app.hud().is_input_captured() && s.sink.commands.empty(), "a cancelled hold gives no order");
    // a finger that never lifts does not block the next
    hand.down(5, g);
    hand.frame();
    app.cancel_touch();
    hand.frame();
    s.clear();
    hand.tap(s.on_ant(s.worker), 6);
    check(app.hud().get_selected_ant_ids() == std::vector<uint32_t>{s.worker}, "after a cancel the next finger is the first one: a tap selects");
    // a cancel with a button held by the first finger: the button is let go of and does not fire
    {
        const UIButton help = app.hud().help_button();
        const Pt on_help{help.x + help.w / 2, help.y + help.h / 2};
        hand.down(1, on_help);
        hand.frame();
        check(app.hud().help_button().is_pressed, "(the Help button is pressed)");
        app.cancel_touch();
        hand.frame();
        check(!app.hud().help_button().is_pressed && !app.hud().is_quick_help_open(), "a cancel lets go of a pressed button, and it does not fire");
    }
}

void test_late_press() {
    group("late", "a press that waited on the map is not made on a dialog that opened meanwhile");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Hand hand(app);
    const LayoutRect view = app.layout().view();
    // a finger goes down on the map; the options window opens (a message, a key); the finger lifts as a tap over the window's Return button: that tap began on the map and must not press Return
    const LayoutPoint page = app.layout().options_offset();
    const Pt on_return{page.x + OptionsScreen::RETURN_X + OptionsScreen::RETURN_W / 2, page.y + OptionsScreen::RETURN_Y + OptionsScreen::RETURN_H / 2};
    check(view.contains(on_return.x, on_return.y), "(the options window's Return lies over the map view)");
    hand.down(1, on_return);
    hand.frame();
    app.hud().open_options();
    hand.wait(80);
    hand.up(1, on_return);
    hand.frame();
    check(app.hud().is_modal_open(), "the tap that began on the map did not press the options window's Return");
    check(app.touch().fingers() == 0, "and the finger is gone");
    // the same finger placed after the window opened is a press at once and closes it
    hand.tap(on_return);
    check(!app.hud().is_modal_open(), "a tap that begins over the open window presses Return");
    // a hold whose time came after a dialog opened: nothing is pressed
    hand.down(1, on_return);
    hand.frame();
    app.hud().open_quit_dialog();
    hand.wait(touch::kHoldMs + 20);
    hand.frame();
    check(!app.hud().is_input_captured() && app.touch().fingers() == 0, "a hold that comes due under a dialog is dropped with its finger");
    hand.up(1, on_return);
    hand.frame();
    app.hud().close_quit_dialog();
}

void test_pointer_gone() {
    group("pointer", "a lifted finger leaves no pointer: no cursor, no edge scroll from where it was");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Hand hand(app);
    ViewportCamera& camera = app.renderer().camera();
    const LayoutRect view = app.layout().view();
    camera.set_origin(500.0, 500.0, app.sim().grid().width(), app.sim().grid().height());
    // a tap in the edge strip of the view's right side (the mouse there would scroll the map east for as long as it stays)
    const Pt edge{view.right() - 2, view.y + view.h / 2};
    hand.tap(edge);
    check(app.pointer_outside(), "after the tap the pointer is outside");
    const float x0 = camera.x;
    for (int i = 0; i < 12; ++i) app.run_frame_with_delta(0.1f);
    check(camera.x == x0, "the map does not scroll from a finger that has lifted");
    // a finger that rests in the strip is a pointer there: the strip scrolls the view as it did for the emulated mouse
    hand.down(1, edge);
    hand.frame();
    hand.wait(touch::kHoldMs + 5);
    for (int i = 0; i < 3; ++i) app.run_frame_with_delta(0.1f);
    check(camera.x == x0, "a resting finger in the strip: the hold's right press is made there and a captured press does not scroll (as for a held right button)");
    hand.up(1, edge);
    hand.frame();
}

void test_other_screens() {
    group("screens", "the screens that are not the map view take taps and drags at once, as the emulation gave them: the quick help, the setup screen, the start menu");
    {   // the quick help: START! is a button (captured at the press, acts at the release)
        Application app;
        SDL_Init(SDL_INIT_VIDEO);
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.lan_port = 0;
        cfg.chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        cfg.maps_dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        cfg.midi_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.MID";
        cfg.screenshot_path = "never_written.png";
        cfg.screenshot_frames = 1 << 30;
        const QuietStdout quiet;
        const bool ok = app.init(cfg);
        check(ok && app.state() == AppState::MapSelect, "the setup screen is up (the classic picture)");
        if (ok) {
            Hand hand(app);
            // the setup screen: a tap on Down moves the selection
            const int before = app.map_select().get_selected_index();
            const int maps = static_cast<int>(app.map_select().get_maps().size());
            const Pt down{MapSelectScreen::BTN_DOWN_X + MapSelectScreen::BTN_DOWN_W / 2, MapSelectScreen::BTN_DOWN_Y + MapSelectScreen::BTN_DOWN_H / 2};
            check(maps > 2, "(the list has maps)");
            hand.tap(down);
            check(app.map_select().get_selected_index() == (before + 1) % maps, "a tap on the setup screen's Down button moves the selection (the list wraps)");
            // a finger that presses Down and slides off it before it lifts does nothing (the button class: leaving cancels the capture)
            hand.down(1, down);
            hand.frame();
            hand.wait(100);
            hand.move(1, Pt{10, 10});
            hand.frame();
            hand.up(1, Pt{10, 10});
            hand.frame();
            check(app.map_select().get_selected_index() == (before + 1) % maps, "a finger that slides off Down before it lifts does not press it");
            // the quick help
            app.finish_loading();
            check(app.state() == AppState::QuickHelp, "(the quick help is up)");
            hand.tap(Pt{580, 450});
            check(app.state() == AppState::MapSelect, "a tap on START! of the quick help closes it");
            check(app.touch().fingers() == 0, "and the finger is gone");
            // a tap that the screen answers with another screen leaves nothing behind: the next tap on the new screen works
            hand.tap(down);
            check(app.map_select().get_selected_index() == (before + 2) % maps, "the next tap, on the new screen, works");
            // a finger that rests on a button for longer than a hold takes is still a press of that button: this screen is not the map (no hold, no right click)
            const uint32_t holds = app.touch().stats().holds;
            hand.down(1, down);
            hand.frame();
            hand.wait(touch::kHoldMs + 150);
            hand.frame();
            check(app.touch().stats().holds == holds && app.touch().mode() == TouchControl::Mode::Left, "a finger that rests on Down for 0.6 s is no hold: the button is held");
            hand.up(1, down);
            hand.frame();
            check(app.map_select().get_selected_index() == (before + 3) % maps && app.touch().stats().holds == holds, "and its lift presses the button: the selection moves");
            // the browser takes the touch away while a finger presses a button of this screen: the press ends as the lift of the finger, where it is (what SDL's emulation made of a cancelled touch)
            hand.down(1, down);
            hand.frame();
            app.cancel_touch();
            hand.frame();
            check(app.touch().fingers() == 0 && app.map_select().get_selected_index() == (before + 4) % maps, "a touch that is cancelled on a button of this screen ends as the lift where the finger is: the button acts");
        }
    }
    {   // the desktop start menu
        Application app;
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.start_menu = true;
        cfg.lan_port = 0;
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.chd_path = std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd";
        cfg.maps_dir = std::string(ORIGINAL_ASSETS_DIR) + "/Maps";
        cfg.midi_path = std::string(ORIGINAL_ASSETS_DIR) + "/INTRO.MID";
        cfg.screenshot_path = "never_written.png";
        cfg.screenshot_frames = 1 << 30;
        const QuietStdout quiet;
        const bool ok = app.init(cfg);
        check(ok, "the start menu's application is up");
        if (ok) {
            app.finish_loading();
            check(app.state() == AppState::StartMenu && app.start_menu().panel() == MenuPanel::Main, "(the menu's first panel is up)");
            app.start_menu().update(0.5f);
            Hand hand(app);
            MenuElement single;
            check(app.start_menu().find_element(MenuId::Single, single), "(Single player is there)");
            hand.tap(Pt{single.rect.x + single.rect.w / 2, single.rect.y + single.rect.h / 2});
            check(app.start_menu().panel() == MenuPanel::Single, "a tap on Single player opens its panel");
            MenuElement back;
            check(app.start_menu().find_element(MenuId::Back, back), "(Back is there)");
            hand.wait(1000);
            hand.tap(Pt{back.rect.x + back.rect.w / 2, back.rect.y + back.rect.h / 2});
            check(app.start_menu().panel() == MenuPanel::Main, "a tap on Back goes back to the first panel");
        }
    }
}

void test_letterbox() {
    group("window", "SDL's renderer puts a finger on the canvas through the window's letterbox: the place that the game gets is the picture pixel under the finger");
    QuietStdout quiet;
    SDL_Init(SDL_INIT_VIDEO);
    ApplicationConfig cfg = base_config(Aspect::Wide16x9);
    cfg.window_w = 1280;                                                // a window that is not the canvas's shape: the canvas (960 x 540) is 1280 x 720 in it, with bars of 280 above and below
    cfg.window_h = 1280;
    cfg.window_width = cfg.window_w;
    cfg.window_height = cfg.window_h;
    cfg.screenshot_path = "never_written.png";
    cfg.screenshot_frames = 1 << 30;
    Application app;
    check(app.init(cfg), "the application is up in a 1280 x 1280 window");
    SDL_Window* window = nullptr;
    int ww = 0;
    int wh = 0;
    for (Uint32 id = 1; id < 8 && window == nullptr; ++id) window = SDL_GetWindowFromID(id);
    if (window != nullptr) SDL_GetWindowSize(window, &ww, &wh);
    check(window != nullptr && ww == 1280 && wh == 1280, "(the window is " + std::to_string(ww) + " x " + std::to_string(wh) + ")");
    if (window == nullptr || ww != 1280) return;
    app.renderer().refit_canvas();
    // SDL_PushEvent runs the renderer's event watch, which turns the window's fractions into the canvas's: the tests' PeepEvents above skip it
    const auto push = [&](uint32_t type, double fx, double fy) {
        SDL_Event e;
        SDL_zero(e);
        e.type = type;
        e.tfinger.type = type;
        e.tfinger.touchId = 7;
        e.tfinger.fingerId = 1;
        e.tfinger.x = static_cast<float>(fx);
        e.tfinger.y = static_cast<float>(fy);
        e.tfinger.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&e);
    };
    const auto where = [&]() {
        app.run_frame_with_delta(0.016f);
        double x = -1.0;
        double y = -1.0;
        app.touch().primary_point(x, y);
        return Pt{static_cast<int32_t>(std::floor(x)), static_cast<int32_t>(std::floor(y))};
    };
    // the middle of the window is the middle of the picture
    push(SDL_FINGERDOWN, 0.5, 0.5);
    Pt mid = where();
    check(std::abs(mid.x - 480) <= 1 && std::abs(mid.y - 270) <= 1, "the middle of the window is the middle of the picture (480, 270): " + show(mid));
    push(SDL_FINGERUP, 0.5, 0.5);
    app.run_frame_with_delta(0.016f);
    // a quarter across and a quarter down the canvas: the canvas (960 x 540) is shown 4 / 3 as large (1280 x 720, bars of 280 above and below), so the picture's (240, 135) is at the window's (320, 280 + 180)
    push(SDL_FINGERDOWN, 320.0 / 1280.0, 460.0 / 1280.0);
    Pt q = where();
    check(std::abs(q.x - 240) <= 1 && std::abs(q.y - 135) <= 1, "a quarter across and down the canvas is (240, 135): " + show(q));
    push(SDL_FINGERUP, 320.0 / 1280.0, 460.0 / 1280.0);
    app.run_frame_with_delta(0.016f);
    // a finger in the bars is held to the picture's edge, as a mouse over a bar is
    push(SDL_FINGERDOWN, 0.5, 0.05);
    Pt top = where();
    check(top.y == 0 && std::abs(top.x - 480) <= 1, "a finger in the bar above the picture is at its top edge: " + show(top));
    push(SDL_FINGERUP, 0.5, 0.05);
    app.run_frame_with_delta(0.016f);
    push(SDL_FINGERDOWN, 0.5, 0.97);
    Pt bottom = where();
    check(bottom.y == 539 && std::abs(bottom.x - 480) <= 1, "a finger in the bar below it is at its bottom edge: " + show(bottom));
    push(SDL_FINGERUP, 0.5, 0.97);
    app.run_frame_with_delta(0.016f);
    check(app.touch().fingers() == 0, "(no finger is left)");
    // the slop for this window: the canvas is 960 wide in 1280 points: 0.75 picture pixels per point, 6 for the 8 points
    check(std::fabs(app.touch_slop() - 6.0) < 0.01, "the slop of a window that shows the picture at 4/3: 8 points are 6 picture pixels: " + std::to_string(app.touch_slop()));
}

void test_inset_picture() {
    group("inset", "a picture smaller than the canvas (the original's layout, centred in the wide canvas): fingers, orders, the pan and the ring are the picture's, not the canvas's");
    Match m;
    if (!m.ok) return check(false, "the match is up");
    Application& app = m.app();
    Scene& s = *m.scene;
    app.set_layout(ScreenLayout::classic());
    s.look();
    check(app.picture() == (LayoutRect{160, 30, 640, 480}), "the classic layout is centred in the 16:9 canvas: (160, 30), 640 x 480 (" + std::to_string(app.picture().x) + ", " + std::to_string(app.picture().y) + ", " +
                                                                std::to_string(app.picture().w) + " x " + std::to_string(app.picture().h) + ")");
    Hand hand(app);
    // a tap on an ant selects it and a hold on the ground orders it there, as the mouse's click and right click do
    const Pt on = s.on_ant(s.worker);
    s.clear();
    hand.tap(on);
    const Outcome tapped = outcome_of(s);
    s.clear();
    mouse_click(app, SDL_BUTTON_LEFT, on, on);
    forget_pointer(app);
    const Outcome clicked = outcome_of(s);
    check(tapped.selected == std::vector<uint32_t>{s.worker} && same(tapped, clicked), "a tap on the worker selects it, as the mouse's click does: " + describe(tapped) + " against " + describe(clicked));
    const Pt g = s.ground(1, 2);
    s.clear();
    s.select({s.worker});
    hand.hold(g, g);
    const Outcome held = outcome_of(s);
    s.clear();
    s.select({s.worker});
    mouse_click(app, SDL_BUTTON_RIGHT, g, g);
    forget_pointer(app);
    const Outcome right_clicked = outcome_of(s);
    check(held.commands.size() == 1 && same(held, right_clicked), "a hold on the ground orders the tile that the mouse's right click does: " + describe(held) + " against " + describe(right_clicked));
    // two fingers pan by the distance they move
    const ViewportCamera& camera = app.renderer().camera();
    const float before_x = camera.x;
    const float before_y = camera.y;
    const Pt a{app.layout().view().x + 150, app.layout().view().y + 150};
    const Pt b{a.x + 60, a.y};
    hand.down(1, a);
    hand.frame();
    hand.down(2, b);
    hand.frame();
    for (int i = 1; i <= 5; ++i) {
        hand.wait(16);
        hand.move(1, Pt{a.x - 8 * i, a.y - 4 * i});
        hand.move(2, Pt{b.x - 8 * i, b.y - 4 * i});
        hand.frame();
    }
    hand.up(2, Pt{b.x - 40, b.y - 20});
    hand.up(1, Pt{a.x - 40, a.y - 20});
    hand.frame();
    check(std::fabs(static_cast<double>(camera.x - before_x) - 40.0) < 1.5 && std::fabs(static_cast<double>(camera.y - before_y) - 20.0) < 1.5, "two fingers that move (-40, -20) in the picture move the view's origin by (+40, +20): (" + std::to_string(static_cast<double>(camera.x - before_x)) + ", " +
                                                                                                                                             std::to_string(static_cast<double>(camera.y - before_y)) + ")");
    // the ring is centred on the finger in the picture (the pulse of the hold above is over by then)
    app.renderer().pin_animation_clock(1000);
    s.clear();
    forget_pointer(app);
    hand.wait(touch::kPulseMs + 20);
    still_frame(app);
    const Pt p{app.layout().view().x + 200, app.layout().view().y + 200};
    const double cx = static_cast<double>(p.x) + 0.37;
    const double cy = static_cast<double>(p.y) + 0.21;
    const Picture base = still_frame(app);
    hand.down(1, p, 0.37, 0.21);
    still_frame(app);
    hand.wait(300);
    const Picture frame = still_frame(app);
    const double slop = app.touch_slop();
    const std::vector<TouchPaint> wanted = touch_ring_paint(TouchControl::Mark{cx, cy, 0.5}, slop);
    const Drawn d = compare_with(base, frame, app, wanted, cx, cy, 0.0);
    check(d.outside == 0 && d.expected > 400 && d.hit * 100 >= d.expected * 95, "the ring is centred on the finger of the inset picture, over the canvas's offset: " + std::to_string(d.hit) + " of " + std::to_string(d.expected) + " pixels, " + std::to_string(d.outside) + " outside, in the box " +
                                                                                                   std::to_string(d.left) + "," + std::to_string(d.top) + " - " + std::to_string(d.right) + "," + std::to_string(d.bottom));
    hand.up(1, p);
    hand.frame();
    // a finger over the canvas's bar beside the picture is held to the picture's edge, as a mouse over a bar is
    double fx = -1.0;
    double fy = -1.0;
    hand.down(1, Pt{-100, 50});
    hand.frame();
    check(app.touch().primary_point(fx, fy) && fx == 0.0 && std::fabs(fy - 50.5) < 0.01, "a finger in the bar left of the picture is at its left edge: (" + std::to_string(fx) + ", " + std::to_string(fy) + ")");
    hand.up(1, Pt{-100, 50});
    hand.frame();
    hand.down(1, Pt{50, -20});
    hand.frame();
    check(app.touch().primary_point(fx, fy) && std::fabs(fx - 50.5) < 0.01 && fy == 0.0, "a finger in the bar above the picture is at its top edge: (" + std::to_string(fx) + ", " + std::to_string(fy) + ")");
    hand.up(1, Pt{50, -20});
    hand.frame();
    hand.down(1, Pt{690, 500});
    hand.frame();
    check(app.touch().primary_point(fx, fy) && fx > 639.9 && fx < 640.0 && fy > 479.9 && fy < 480.0, "a finger in the bar below and right of it is at its last pixel: (" + std::to_string(fx) + ", " + std::to_string(fy) + ")");
    hand.up(1, Pt{690, 500});
    hand.frame();
}

void test_slop() {
    group("slop", "the slop is 8 points of the window (CSS pixels of the box) in picture pixels, whatever the window's size");
    QuietStdout quiet;
    SDL_Init(SDL_INIT_VIDEO);
    for (const auto& c : std::vector<std::tuple<Aspect, int, int, double>>{{Aspect::Wide16x9, 960, 540, 8.0}, {Aspect::Wide16x9, 1920, 1080, 4.0}, {Aspect::Wide16x9, 480, 270, 16.0},
                                                                            {Aspect::Classic4x3, 640, 480, 8.0}, {Aspect::Classic4x3, 1280, 960, 4.0}}) {
        ApplicationConfig cfg = base_config(std::get<0>(c));
        cfg.window_w = std::get<1>(c);
        cfg.window_h = std::get<2>(c);
        cfg.window_width = cfg.window_w;
        cfg.window_height = cfg.window_h;
        Application app;
        const bool ok = app.init(cfg);
        check(ok, "the application is up at " + std::to_string(std::get<1>(c)) + " x " + std::to_string(std::get<2>(c)));
        if (!ok) continue;
        check(std::fabs(app.touch_slop() - std::get<3>(c)) < 0.01, "in a window of " + std::to_string(std::get<1>(c)) + " x " + std::to_string(std::get<2>(c)) + " the slop is " + std::to_string(std::get<3>(c)) + " picture pixels: " + std::to_string(app.touch_slop()));
        if (std::fabs(std::get<3>(c) - 8.0) > 0.5) {                                 // (a window whose slop is not the model's own default: the finger's landing hands it over)
            Hand hand(app);
            hand.down(1, Pt{40, 40});
            hand.frame();
            check(std::fabs(app.touch().slop() - std::get<3>(c)) < 0.01, "the model has the window's slop from the finger's landing: " + std::to_string(app.touch().slop()));
            hand.up(1, Pt{40, 40});
            hand.frame();
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    test_setup_of_sdl();
    test_tap_is_the_click();
    test_drag_is_the_band();
    test_hold_is_the_right_click();
    test_hold_timing();
    test_ring();
    test_pan();
    test_pinch();
    test_gates();
    test_second_finger_cancels();
    test_cancel();
    test_late_press();
    test_pointer_gone();
    test_other_screens();
    test_letterbox();
    test_inset_picture();
    test_slop();
    std::printf("\ntouch application: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
