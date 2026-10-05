// Touch controls in the application (touch_control.hpp is the model): the fingers of a touch screen in, the mouse events and the pan and zoom of the map view out. SDL's own touch-to-mouse
// emulation is off (Application::init), so every finger comes here as SDL_FINGERDOWN / SDL_FINGERMOTION / SDL_FINGERUP, in coordinates that SDL's renderer has already put on the logical canvas
// (0 .. 1 over the whole canvas); what the model says is queued and the event loop (Application::handle_events) does it in order, as mouse events through the very code that the mouse uses.

#include <algorithm>
#include <cmath>

#include "ants_app/application.hpp"
#include "ants_app/touch_feedback.hpp"

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>
  #include <emscripten/html5.h>

// A short buzz where the browser has one (Android's Chrome; an iPhone's Safari has none): never required, and nothing here can throw. (EM_JS and not EM_ASM: see application.cpp.)
extern "C" {
EM_JS(void, ants_touch_buzz, (int milliseconds), {
    try { if (navigator.vibrate) navigator.vibrate(milliseconds); } catch (e) {}
});
}
#endif

namespace ants::app {

// What a finger that goes down here is: the map view of a match with nothing open over it waits to be understood, the minimap and everything else is a mouse at once
TouchZone Application::touch_zone_at(double x, double y) const {
    if (!touch_view_open()) return TouchZone::Direct;
    const int32_t px = static_cast<int32_t>(std::floor(std::clamp(x, -1.0e6, 1.0e6)));
    const int32_t py = static_cast<int32_t>(std::floor(std::clamp(y, -1.0e6, 1.0e6)));
    if (hud_.over_minimap(px, py)) return TouchZone::Minimap;
    if (hud_.over_map(px, py)) return TouchZone::Map;
    return TouchZone::Direct;
}

// Two fingers pan and zoom where the wheel zooms (view_zoom_allowed): a match, the map view under their middle point, no dialog or page, no results, no press held but the first finger's own on
// the map (the gesture ends that one). The pointer's being "outside" is a mouse's business: every lifted finger leaves it so.
bool Application::touch_two_fingers_allowed(double x, double y) const {
    if (!touch_view_open() || !hud_.view_gesture_allowed()) return false;
    return hud_.over_map(static_cast<int32_t>(std::floor(std::clamp(x, -1.0e6, 1.0e6))), static_cast<int32_t>(std::floor(std::clamp(y, -1.0e6, 1.0e6))));
}

// The match is on the screen with nothing over it (no results, no dialog): its HUD holds the presses that a cancel ends. A network match's catch-up screen is this too, though no match is drawn.
bool Application::touch_match_screen() const {
    return renderer_ != nullptr && state_ == AppState::Playing && !scorecard_.is_open() && !hud_.is_modal_open();
}

// The map view is open to touches: the match screen, and not its catch-up picture (every mouse path is shut there as well: nothing may pan, zoom, wait for a hold or buzz over it)
bool Application::touch_view_open() const {
    return touch_match_screen() && !catch_up_screen_active();
}

// 8 CSS pixels of the game box on the glass (Android's own touch slop is 8 dp), at least 6 device pixels, in picture pixels. The page's box is what the browser shows the canvas in (its CSS size, which
// Emscripten's SDL reads as its window's); on a desktop the window's size in points stands for it (and the output's size in pixels for the device's).
double Application::touch_slop() const {
    const double canvas_w = renderer_ ? static_cast<double>(renderer_->canvas_w()) : static_cast<double>(ScreenLayout::kClassicWidth);
    double per_css = 1.0;
    double per_device = 1.0;
#if defined(__EMSCRIPTEN__)
    double css_w = 0.0;
    double css_h = 0.0;
    if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) == EMSCRIPTEN_RESULT_SUCCESS && css_w > 0.0) {
        per_css = canvas_w / css_w;
        const double ratio = emscripten_get_device_pixel_ratio();
        per_device = ratio > 0.0 ? per_css / ratio : per_css;
    }
#else
    int window_w = 0;
    int window_h = 0;
    if (window_ != nullptr) SDL_GetWindowSize(window_, &window_w, &window_h);
    const double canvas_h = renderer_ ? static_cast<double>(renderer_->canvas_h()) : static_cast<double>(ScreenLayout::kClassicHeight);
    if (window_w > 0 && window_h > 0 && canvas_w > 0.0 && canvas_h > 0.0) {
        const double scale = std::min(static_cast<double>(window_w) / canvas_w, static_cast<double>(window_h) / canvas_h);     // (SDL's logical size: the largest scale that fits)
        per_css = 1.0 / scale;
        int out_w = 0;
        int out_h = 0;
        if (renderer_ && renderer_->get_sdl_renderer() != nullptr && SDL_GetRendererOutputSize(renderer_->get_sdl_renderer(), &out_w, &out_h) == 0 && out_w > 0) {
            per_device = per_css * static_cast<double>(window_w) / static_cast<double>(out_w);
        } else {
            per_device = per_css;
        }
    }
#endif
    return touch::slop_pixels(per_css, per_device);
}

bool Application::pan_view(int32_t dx, int32_t dy) {
    if (!renderer_ || state_ != AppState::Playing) return false;
    ViewportCamera& camera = renderer_->camera();
    const float before_x = camera.x;
    const float before_y = camera.y;
    camera.scroll_screen(-dx, -dy, current_level_.width(), current_level_.height());
    return camera.x != before_x || camera.y != before_y;
}

void Application::cancel_touch() {
    queue_touch(touch_.cancel());
}

void Application::queue_touch(const TouchControl::Actions& actions) {
    touch_queue_.insert(touch_queue_.end(), actions.begin(), actions.end());
}

bool Application::counts_as_finger(SDL_TouchID device, SDL_TouchDeviceType kind) noexcept {
    if (device == SDL_MOUSE_TOUCHID) return false;                         // a platform's own touch made from the mouse
    return kind != SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE && kind != SDL_TOUCH_DEVICE_INDIRECT_RELATIVE;     // a trackpad's touches are not places on the picture (an unknown device, which a test's is, counts)
}

// A finger's event: its place (SDL gives it as a fraction of the canvas, the renderer's logical size having been taken off already) in the picture's pixels, and its time (SDL's own ticks)
void Application::feed_touch(const SDL_TouchFingerEvent& finger) {
    if (!counts_as_finger(finger.touchId, SDL_GetTouchDeviceType(finger.touchId))) return;
    if (!renderer_) return;
    update_picture();
    const double canvas_w = static_cast<double>(renderer_->canvas_w());
    const double canvas_h = static_cast<double>(renderer_->canvas_h());
    const double x = std::clamp(static_cast<double>(finger.x) * canvas_w - static_cast<double>(picture_.x), 0.0, std::max(0.0, static_cast<double>(picture_.w) - 0.001));
    const double y = std::clamp(static_cast<double>(finger.y) * canvas_h - static_cast<double>(picture_.y), 0.0, std::max(0.0, static_cast<double>(picture_.h) - 0.001));
    switch (finger.type) {
        case SDL_FINGERDOWN:
            touch_.set_slop(touch_slop());
            queue_touch(touch_.finger_down(finger.touchId, finger.fingerId, x, y, finger.timestamp));
            break;
        case SDL_FINGERMOTION:
            queue_touch(touch_.finger_motion(finger.touchId, finger.fingerId, x, y, finger.timestamp));
            break;
        case SDL_FINGERUP:
            queue_touch(touch_.finger_up(finger.touchId, finger.fingerId, x, y, finger.timestamp));
            break;
        default:
            break;
    }
}

// A press that waited on the map view reaches the game only now. If the screen has changed meanwhile (a dialog opened over the map, the results came up) it would land on whatever is there: it ends.
bool Application::touch_late_press_still_fits(const TouchAction& action) const {
    return touch_zone_at(static_cast<double>(action.x), static_cast<double>(action.y)) == TouchZone::Map;
}

// The next queued action. A mouse event is returned as the event that SDL's emulation made of a touch (`which` is SDL_TOUCH_MOUSEID: a lifted finger takes the pointer away, pointer_gone_after), in the
// canvas's coordinates (the loop takes the picture's corner off again); everything else is done here and false is returned.
bool Application::take_touch_event(SDL_Event& event) {
    const TouchAction action = touch_queue_.front();
    touch_queue_.pop_front();
    using Kind = TouchAction::Kind;
    switch (action.kind) {
        case Kind::Motion:
        case Kind::LeftDown:
        case Kind::LeftUp:
        case Kind::RightDown:
        case Kind::RightUp: {
            if (action.late && !touch_late_press_still_fits(action)) {
                (void)touch_.cancel();                                     // (the finger's press is not made, and nothing of the finger is kept)
                touch_queue_.clear();
                return false;
            }
            SDL_zero(event);
            const Uint32 window_id = window_ != nullptr ? SDL_GetWindowID(window_) : 0u;
            const int32_t x = action.x + picture_.x;
            const int32_t y = action.y + picture_.y;
            if (action.kind == Kind::Motion) {
                event.type = SDL_MOUSEMOTION;
                event.motion.timestamp = action.at;
                event.motion.windowID = window_id;
                event.motion.which = SDL_TOUCH_MOUSEID;
                event.motion.x = x;
                event.motion.y = y;
            } else {
                const bool down = action.kind == Kind::LeftDown || action.kind == Kind::RightDown;
                event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                event.button.timestamp = action.at;
                event.button.windowID = window_id;
                event.button.which = SDL_TOUCH_MOUSEID;
                event.button.button = (action.kind == Kind::LeftDown || action.kind == Kind::LeftUp) ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
                event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
                event.button.clicks = 1;
                event.button.x = x;
                event.button.y = y;
            }
            return true;
        }
        default:
            run_touch_action(action);
            return false;
    }
}

void Application::run_touch_action(const TouchAction& action) {
    using Kind = TouchAction::Kind;
    switch (action.kind) {
        case Kind::Pan:
        case Kind::Zoom:
            if (!touch_view_open()) {                                       // a dialog or the results came up under the fingers: the gesture is over
                (void)touch_.cancel();
                break;
            }
            if (action.kind == Kind::Pan) pan_view(action.dx, action.dy);
            else set_zoom(action.level, action.x, action.y);
            break;
        case Kind::Cancel:
            hud_.cancel_press();                                            // (the match's own presses end with no act; where the HUD holds none this changes nothing)
            if (!touch_match_screen()) {                                    // a screen, a dialog or a page holds the press: it is let go of where no control is, so that no button fires (a button acts at a release inside it)
                TouchAction release;
                release.kind = action.right ? Kind::RightUp : Kind::LeftUp;
                release.x = 0;
                release.y = 0;
                release.at = action.at;
                touch_queue_.push_front(release);
            }
            pointer_outside_ = true;                                        // (no finger is the pointer now: no cursor, no edge scroll from where it was)
            break;
        case Kind::HoldFired:
            touch_feedback();
            break;
        default:
            break;
    }
}

void Application::touch_feedback() {
    ++touch_feedbacks_;
#if defined(__EMSCRIPTEN__)
    ants_touch_buzz(12);
#endif
}

// The ring closes around a finger that waits to become a hold, and a pulse goes out where one fired: drawn only while the model has one to show (and only over the map view of a match,
// nothing open over it), so that no other picture changes; the renderer keeps what is drawn inside the picture
void Application::render_touch_feedback() {
    if (!touch_view_open()) return;
    const uint32_t now = touch_now();
    const std::optional<TouchControl::Mark> ring = touch_.ring(now);
    const std::optional<TouchControl::Mark> pulse = touch_.pulse(now);
    if (!ring && !pulse) return;
    draw_touch_feedback(*renderer_, ring, pulse, touch_.slop());
}

}  // namespace ants::app
