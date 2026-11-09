// The view of a match on the screen (application.hpp): the zoom, the edge scroll and minimap drag of the INPUT task, the view at the start, and the camera of a replay (docs/REPLAYS.md "Camera").
// What the player's pointer and wheel do to the view; the simulation, the network and every hash never see any of it.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>

#include "ants_app/application.hpp"
#include "ants_app/edge_scroll.hpp"
#include "ants_app/page_layout.hpp"

namespace ants::app {

// --zoom, else the settings' key `zoom` (a number from 0.05 to 2: the remake's own key, written when the player zooms), else 1. A settings file never stops the game: a value that is not a zoom is
// reported and ignored.
void Application::choose_zoom() {
    zoom_wanted_ = zoom::kNormal;
    if (config_.zoom_given) {
        zoom_wanted_ = config_.zoom;
    } else if (config_store_.has("zoom")) {
        const std::string text = config_store_.get_string("zoom", "", 16);
        std::string why;
        float from_settings = zoom::kNormal;
        if (zoom::parse_level(text, from_settings, why)) zoom_wanted_ = from_settings;
        else std::cerr << "[Application] settings: zoom=" << why << " (ignored)" << std::endl;
    }
}

// A match starts (the local game's START, the network's start, the first map of a game that starts at once): the camera takes the nearest level to the one that the player chose last (or gave with
// --zoom) among those that this map offers in this view (1 when the setting is not a zoom). The level that was chosen stays remembered for the next match.
void Application::apply_match_zoom() {
    if (!renderer_) return;
    renderer_->camera().zoom = zoom::level_for_match(zoom_wanted_, zoom_fit(), zoom_limits());
}

// Every player of a match has the same levels (FAIRNESS, application.hpp): the zoom is the view's own. Only a renderer that cannot make the offscreen target is limited: it draws the zoom 1.
zoom::Limits Application::zoom_limits() const noexcept {
    if (renderer_ && renderer_->world_target_failed()) return zoom::Limits::only_normal();      // (a zoom is drawn through the target: with none there is only the original's picture)
    return zoom::Limits::any();
}

zoom::Fit Application::zoom_fit() const noexcept {
    zoom::Fit fit;
    const LayoutRect view = layout_.view();
    fit.view_w = view.w;
    fit.view_h = view.h;
    fit.map_w = static_cast<int64_t>(current_level_.width()) * TILE_SIZE;
    fit.map_h = static_cast<int64_t>(current_level_.height()) * TILE_SIZE;
    fit.max_world = renderer_ ? renderer_->max_world_extent() : 0;
    return fit;
}

// The camera's zoom, in a match, is always inside what is allowed and drawn: at every frame, before anything of it is used. It is never below the map's limit (the view shows nothing outside the
// map; the limit follows the view, which a new picture changes), and when the renderer cannot make the offscreen target the picture it draws is the zoom 1 whatever the camera says, so the camera
// says 1 (the clicks, the minimap's frame and the edge scroll then agree with the picture), only that level is offered, and one line is written when the failure begins (not at every frame). The
// level that the player chose stays remembered (it is not the player's choice that is wrong): the next match starts with it when the target works again. The correction goes as set_zoom's does,
// anchored at the view's centre.
void Application::enforce_zoom_limits() {
    if (!renderer_) return;
    const bool failed = renderer_->world_target_failed();
    if (failed && !zoom_failure_reported_) {
        std::cerr << "[Application] The offscreen target of the zoom cannot be made"
                  << (renderer_->world_target_error().empty() ? "" : " (" + renderer_->world_target_error() + ")")
                  << ": the zoom is 1 until it can be" << std::endl;
    }
    zoom_failure_reported_ = failed;
    renderer_->retry_world_target();                                       // (a failure that lasts is tried again now and then; nothing when there is none)
    if (state_ != AppState::Playing) return;                               // (the map view is drawn in a match only: the setup screens, a room's included, leave the camera as it is; the next match start chooses its level, apply_match_zoom)
    ViewportCamera& camera = renderer_->camera();
    const zoom::Limits limits = zoom_limits();
    const float bottom = std::max(zoom::floor_zoom(zoom_fit()), limits.min_zoom);
    const float top = std::min(limits.max_zoom, zoom::kIn);
    const float held = std::clamp(camera.zoom, std::min(bottom, top), top);
    if (held == camera.zoom) return;
    const LayoutRect view = layout_.view();
    camera.set_zoom(held, view.w / 2, view.h / 2, current_level_.width(), current_level_.height());
}

std::vector<float> Application::zoom_levels() const {
    if (!renderer_) return {};
    return zoom::levels(zoom_fit(), zoom_limits());
}

// The zoom API (application.hpp): to the nearest of the offered levels, keeping the world point under the screen pixel (anchor_x, anchor_y) of the picture under it as far as the map's edges allow
bool Application::set_zoom(float level, int32_t anchor_x, int32_t anchor_y) {
    if (!renderer_ || state_ != AppState::Playing) return false;
    if (!std::isfinite(level) || level <= 0.0f) return false;
    ViewportCamera& camera = renderer_->camera();
    const float target = zoom::nearest(level, zoom::levels(zoom_fit(), zoom_limits()));
    if (target == camera.zoom) return false;
    const LayoutRect view = layout_.view();
    const int32_t dx = std::clamp(anchor_x - view.x, 0, std::max(0, view.w - 1));
    const int32_t dy = std::clamp(anchor_y - view.y, 0, std::max(0, view.h - 1));
    camera.set_zoom(target, dx, dy, current_level_.width(), current_level_.height());
    zoom_wanted_ = target;                                                // remembered: the next match starts with it
    config_store_.set_string("zoom", zoom::level_name(target));
    return true;
}

bool Application::step_zoom(int direction, int32_t anchor_x, int32_t anchor_y) {
    if (!renderer_) return false;
    const float next = zoom::step(renderer_->camera().zoom, direction, zoom_fit(), zoom_limits());
    return set_zoom(next, anchor_x, anchor_y);
}

bool Application::view_zoom_allowed(int32_t x, int32_t y) const {
    return renderer_ != nullptr && state_ == AppState::Playing && !scorecard_.is_open() && !catch_up_screen_active() && !pointer_outside_ && hud_.view_zoom_allowed() && hud_.over_map(x, y);
}

// SDL_MOUSEWHEEL over the map view: the wheel rolled away zooms in, towards the pointer, one level per notch (a trackpad's small deltas add up; zoom::wheel_amount undoes the system's
// "natural scrolling" flip, so rolled away is in whatever the setting). The pointer is the last one that a motion, a press or a release gave.
void Application::handle_mouse_wheel(const SDL_MouseWheelEvent& wheel) {
    if (!view_zoom_allowed(mouse_screen_x_, mouse_screen_y_)) {
        wheel_.reset();                                                   // (what the dialog or the panel gets is not half a notch for the map)
        return;
    }
    const double amount = zoom::wheel_amount(wheel.y, wheel.preciseY, wheel.direction == SDL_MOUSEWHEEL_FLIPPED);
    int steps = wheel_.feed(amount, wheel.timestamp);
    for (; steps > 0; --steps) step_zoom(+1, mouse_screen_x_, mouse_screen_y_);
    for (; steps < 0; ++steps) step_zoom(-1, mouse_screen_x_, mouse_screen_y_);
}

// The INPUT task of the original runs every 50 ms (Ants.exe 0x100ae26): the edge strips and the minimap drag move the view in whole pixel
// steps at that rate (edge_scroll.hpp). Panning is strictly mouse-driven; keyboard keys do not scroll.
void Application::handle_camera_panning(float dt) {
    if (state_ == AppState::MapSelect || scorecard_.is_open()) return;
    input_accumulator_ += dt;
    while (input_accumulator_ >= 0.050f) {
        input_accumulator_ -= 0.050f;
        if (state_ != AppState::Playing || !renderer_ || !mouse_has_moved_ || pointer_outside_ || catch_up_screen_active() || game_menu_open_) continue;
        if (mouse_screen_x_ < 0 || mouse_screen_x_ >= layout_.width || mouse_screen_y_ < 0 || mouse_screen_y_ >= layout_.height) continue;
        ViewportCamera& camera = renderer_->camera();
        const float was_x = camera.x;
        const float was_y = camera.y;
        hud_.input_tick(camera, current_level_.width(), current_level_.height(), mouse_screen_x_, mouse_screen_y_);
        if (replay_mode_ && (camera.x != was_x || camera.y != was_y)) take_camera();            // (the pointer at the edge, or a drag on the minimap, moved the view: it is the viewer's now)
    }
}

// The view at the start (docs 5.44): from the map's corner just far enough to show the square around the anchor tile of the local team's hill (edge_scroll.hpp)
void Application::show_start_view() {
    if (!renderer_) return;
    int32_t tx = 0;
    int32_t ty = 0;
    if (const auto* base = sim_.grid().find_anthill(local_player_id_)) {
        tx = base->x + 1;                                    // the anchor is one tile in from the corner of the 4 x 4 footprint (every shipped map)
        ty = base->y + 1;
    } else if (!current_level_.anthill_spawns.empty()) {
        tx = current_level_.anthill_spawns[0].x;
        ty = current_level_.anthill_spawns[0].y;
    }
    ViewportCamera& camera = renderer_->camera();
    int32_t ox = 0;
    int32_t oy = 0;
    start_view_origin(tx, ty, static_cast<int32_t>(current_level_.width()), static_cast<int32_t>(current_level_.height()), ox, oy, layout_, camera.zoom);      // (the world that the view shows at its zoom)
    camera.x = 0.0f;
    camera.y = 0.0f;
    camera.set_origin(static_cast<double>(ox), static_cast<double>(oy), current_level_.width(), current_level_.height());
}

// ------------------------------------------------------------------------------------------------
// The camera of a replay (auto_camera.hpp, docs/REPLAYS.md "Camera")
// ------------------------------------------------------------------------------------------------

// The viewer took the view: Manual, and the page is told (ReplayValue::CameraTaken counts it, and says so once in a visit)
void Application::take_camera() {
    if (replay_mode_ && auto_camera_.take()) ++camera_taken_;
}

// A frame of a replay: the model says where the view should be (Auto: it is locked on the biggest action and glides along with it; a zoom of the viewer's is followed by a glide back to the place, at
// the viewer's zoom), and the minimap's ring shows the busiest place, whole while the camera follows it and dashed while it is Manual
void Application::update_auto_camera(float dt) {
    if (!replay_mode_ || !renderer_ || state_ != AppState::Playing) return;
    ViewportCamera& camera = renderer_->camera();
    const double view_w = zoom::visible_exact(camera.viewport_w, camera.zoom);
    const double view_h = zoom::visible_exact(camera.viewport_h, camera.zoom);
    const bool zoomed = camera.zoom != camera_zoom_seen_;
    camera_zoom_seen_ = camera.zoom;
    AutoCamera::Point out;
    const AutoCamera::Point middle{static_cast<double>(camera.x) + view_w / 2.0, static_cast<double>(camera.y) + view_h / 2.0};
    if (auto_camera_.step(static_cast<double>(std::min(dt, 0.25f)) * 1000.0, middle, zoomed, out)) {
        const double ox = out.x - view_w / 2.0;
        const double oy = out.y - view_h / 2.0;
        const bool plain = camera.zoom == zoom::kNormal;
        camera.set_origin(plain ? std::floor(ox + 0.5) : ox, plain ? std::floor(oy + 0.5) : oy, current_level_.width(), current_level_.height());
    }
    AutoCamera::Point hot;
    const bool shown = auto_camera_.hot_spot(hot);
    hud_.set_camera_ring(shown, static_cast<int32_t>(hot.x), static_cast<int32_t>(hot.y), auto_camera_.mode() == CameraMode::Auto);
}

void Application::set_layout(const ScreenLayout& layout) {
    layout_ = layout;
    update_picture();
    if (renderer_) renderer_->set_layout(layout_);
    hud_.set_layout(layout_);
}

// The live switch of the picture's shape (application.hpp). The canvas is SDL's logical size, so the next frame is drawn in the new shape and scaled into the window (a page's box changes with it: the
// web page sizes the canvas to the shape in the same breath); the match screen's layout is the new canvas's; the HUD places its rectangles again; every page is composed for the new canvas (the 960 x 540
// page centred in a 16:10 or 21:9 one: update_picture). The simulation, the network and the match's own state are never read or written here. In a match the camera keeps the world point in the middle
// of the view (a view that grew shows more around it, a view that shrank less) and takes the zoom that the player chose, or the nearest level that the new view offers (the limit of the zoom-out follows
// the view's shape): the level that the player chose stays remembered, so that a shape that offers it again gets it back.
bool Application::set_aspect(Aspect aspect) {
    if (!renderer_) return false;
    if (aspect == aspect_) return true;
    ViewportCamera& camera = renderer_->camera();
    const double middle_x = static_cast<double>(camera.x) + zoom::visible_exact(camera.viewport_w, camera.zoom) / 2.0;
    const double middle_y = static_cast<double>(camera.y) + zoom::visible_exact(camera.viewport_h, camera.zoom) / 2.0;
    aspect_ = aspect;
    renderer_->set_canvas_size(canvas_width_of(aspect_), canvas_height_of(aspect_));       // (this forgets the picture: the picture that the application remembers is set again, update_picture compares with it)
    layout_ = ScreenLayout::with_size(canvas_width_of(aspect_), canvas_height_of(aspect_));
    renderer_->set_picture(picture_);
    renderer_->set_layout(layout_);
    hud_.set_layout(layout_);
    update_picture();
    if (state_ == AppState::Playing) {
        const LayoutRect view = layout_.view();
        const float level = zoom::level_for_match(zoom_wanted_, zoom_fit(), zoom_limits());
        camera.zoom = level;
        const double ox = middle_x - zoom::visible_exact(view.w, level) / 2.0;
        const double oy = middle_y - zoom::visible_exact(view.h, level) / 2.0;
        camera.set_origin(level == zoom::kNormal ? std::floor(ox + 0.5) : ox, level == zoom::kNormal ? std::floor(oy + 0.5) : oy, current_level_.width(), current_level_.height());
    }
    return true;
}

// Where the picture that is on screen sits in the canvas: a match is the layout's picture (the whole canvas of its aspect), and so is every other screen: the setup screen and the room, the
// loading screen, the quick help, the results and the start menu of a 960 x 540 canvas are composed for it (setup_layout.hpp, page_layout.hpp, results_layout.hpp, start_menu.hpp), and in the
// 640 x 480 canvas the original's own pages are the whole canvas
LayoutRect Application::picture_for_state() const {
    if (match_running()) return canvas().centred(layout_.width, layout_.height);
    return canvas().page();
}

// A page is on the screen: every screen but the match (the catch-up screen is drawn inside a match: it is a page too, the loading screen of a machine that runs the match from the server's log)
bool Application::page_up() const {
    return !match_running() || catch_up_screen_active();
}

// The frame rate, the version and the network's readout stand in the corner of the picture that is up: the canvas's corner in a match (the viewport: the same in every layout), and on a page the
// page's own corner, which is the canvas's where the page is the canvas (16:9, 4:3) and the 960 x 540 page's in the other wide shapes, so that the readout is never in the black beside a page
LayoutRect Application::corner_area() const {
    return page_up() ? canvas().page() : canvas().rect();
}

// The setup screen has a wide version for the 16:9 canvas of 960 x 540, which is also the page of every bigger canvas of the shape table (CanvasLayout::page: centred in them); any other canvas draws the original's page
bool Application::wide_setup() const {
    const LayoutRect p = canvas().page();
    return SetupLayout::supports(p.w, p.h);
}

// ... and so have the loading screen, the quick help at the start, the results and the start menu: the same canvas
bool Application::wide_pages() const {
    const LayoutRect p = canvas().page();
    return wide_pages_supported(p.w, p.h);
}

// The picture changes when the layout does (a match of another layout: the original's own 640 x 480 picture centred in a 960 x 540 canvas, a test hook; every screen is the whole canvas, so
// no screen changes it). The pointer stays where it is on the canvas, so its coordinates, which are the picture's own, move with the corner; a pointer that was beside the new picture is at
// the picture's nearest edge pixel, which is where the original's one-monitor pointer is when it is pushed against an edge (pointer_clamp.hpp), and not outside the picture, where the
// cursor would vanish. The screens that have a wide version are told which version to be (the canvas decides: wide_pages).
void Application::update_picture() {
    map_select_.set_wide_layout(wide_setup());                                              // (the screen is drawn and answers the pointer as its wide version when the canvas is 960 x 540)
    const bool wide = wide_pages();
    scorecard_.set_wide_layout(wide);                                                       // (so are the results, the start menu and the quick help's button: they stand where the wide pages put them)
    start_menu_.set_wide_layout(wide);
    if (wide != quick_help_wide_) {
        quick_help_wide_ = wide;
        const QuickHelpLayout& q = QuickHelpLayout::of(wide);
        quick_help_start_ = ScreenButton(ButtonRect{q.start.x, q.start.y, q.start.w, q.start.h}, ButtonRect{q.start_pressed.x, q.start_pressed.y, q.start_pressed.w, q.start_pressed.h});
    }
    const LayoutRect want = picture_for_state();
    if (want == picture_) return;
    mouse_screen_x_ = std::clamp(mouse_screen_x_ + picture_.x - want.x, 0, want.w - 1);
    mouse_screen_y_ = std::clamp(mouse_screen_y_ + picture_.y - want.y, 0, want.h - 1);
    picture_ = want;
    if (renderer_) renderer_->set_picture(picture_);
}

// --aspect, else the settings' key `aspect`, else the default of the platform (parse_arguments puts it into the config: kPlatformDefaultAspect in canvas_layout.hpp, 16:9 on a desktop and
// in the web build, whose page passes the shape it shows as `--aspect`); a config that is made by hand keeps its own aspect (the original's 4:3 unless it says otherwise). A settings file
// never stops the game: a value that is no name of the shape table (canvas_layout.hpp) is reported and ignored. The settings of the web build are the browser's local storage (the same key).
void Application::choose_aspect() {
    aspect_ = config_.aspect;
    if (!config_.aspect_given && config_store_.has("aspect")) {
        const std::string text = config_store_.get_string("aspect", "", 16);
        std::string why;
        Aspect from_settings = Aspect::Classic4x3;
        if (parse_aspect(text, from_settings, why)) aspect_ = from_settings;
        else std::cerr << "[Application] settings: aspect=" << why << " (ignored)" << std::endl;
    }
}

// The text of a switch (a command line option, a settings key): on / off, yes / no, true / false, 1 / 0, in any case
bool parse_switch(const std::string& text, bool& out) {
    std::string lower;
    for (const char ch : text) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lower == "on" || lower == "yes" || lower == "true" || lower == "1") {
        out = true;
        return true;
    }
    if (lower == "off" || lower == "no" || lower == "false" || lower == "0") {
        out = false;
        return true;
    }
    return false;
}

// --prediction, else the settings' key `prediction` ("on" or "off": a key that the owner of the settings file writes, nothing in the game does), else off. A settings file never stops the game: a
// value that is not a switch is reported and ignored.
void Application::choose_prediction() {
    prediction_wanted_ = config_.prediction;
    if (!config_.prediction_given && config_store_.has("prediction")) {
        const std::string text = config_store_.get_string("prediction", "", 16);
        bool from_settings = true;
        if (parse_switch(text, from_settings)) prediction_wanted_ = from_settings;
        else std::cerr << "[Application] settings: prediction=" << text << " needs on or off (ignored)" << std::endl;
    }
}

// 0x10122d4: the stamp is the C runtime's `_strdate` ("mm/dd/yy") and `_strtime` ("hh:mm:ss") joined by " @ " (the format string "%s @ %s\n\n")
std::string Application::transcript_stamp(std::time_t time) {
    char date[16] = {0};
    char clock[16] = {0};
    if (const std::tm* local = std::localtime(&time)) {
        std::strftime(date, sizeof(date), "%m/%d/%y", local);
        std::strftime(clock, sizeof(clock), "%H:%M:%S", local);
    }
    return std::string(date) + " @ " + clock;
}

bool Application::write_chat_transcript(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << hud_.chat_transcript(transcript_stamp(std::time(nullptr)));
    return static_cast<bool>(out);
}

} // namespace ants::app
