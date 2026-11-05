// The Yard in the application (docs/YARD.md): the real game with a picture of the map alone, a puppet world that the batches of the Yard's server rewrite, and a sink that turns the HUD's orders into
// intents. The sibling of the replay mode (application_replay.cpp): nothing of the simulation ticks, and the HUD, the renderer, the pointer and the touch code run as they are. Nothing here is reached
// in a game that is not the Yard (`yard_mode_` is false until start_yard).
#include <algorithm>
#include <cmath>

#include "ants_app/application.hpp"
#include "ants_app/yard_puppet.hpp"
#include "ants_app/yard_sink.hpp"

#if defined(__EMSCRIPTEN__)
  #include <emscripten.h>
#endif

namespace ants::app {

namespace {

constexpr int32_t kYardMaxSide = 4096;       // a canvas of the Yard is at most this many pixels on a side (the least is ScreenLayout::kYardMinWidth x kYardMinHeight)
constexpr int32_t kYardMaxScale = 3;         // ... and is shown at a whole scale of 1 .. 3

bool yard_size_ok(int32_t width, int32_t height, int32_t scale) noexcept {
    return width >= ScreenLayout::kYardMinWidth && height >= ScreenLayout::kYardMinHeight && width <= kYardMaxSide && height <= kYardMaxSide && scale >= 1 && scale <= kYardMaxScale;
}

}  // namespace

// The world of a Yard: nobody plays (no ants, no eggs), and the hills of the picture are the open games' (the renderer draws every place that the grid lists with the game's hill), so the map
// file's own start markers and hills do not count. The engine is never ticked again (update_simulation calls run_yard).
void Application::prepare_yard_world() {
    current_level_ = current_level_.for_roster(0);
    sim_.init(current_level_, config_.random_seed, 0);
}

bool Application::start_yard_from_config() {
    return start_yard(config_.has_window_size ? config_.window_w : 960, config_.has_window_size ? config_.window_h : 540, 1);
}

bool Application::start_yard(int32_t width, int32_t height, int32_t scale) {
    if (!renderer_ || !yard_size_ok(width, height, scale)) return false;
    yard_mode_ = true;
    prepare_yard_world();
    renderer_->set_level(current_level_);
    yard_puppet_ = std::make_unique<YardPuppet>(sim_);
    yard_sink_ = std::make_unique<YardSink>(*yard_puppet_);
    hud_.set_command_sink(yard_sink_.get());
    hud_.set_on_menu_request(nullptr);                        // (no Esc menu)
    hud_.set_on_play_sfx(nullptr);                            // (silent: no audio device is opened; a sound button of the page gives the voices back)
    set_yard_size(width, height, scale);
    hud_.init(0, false);                                      // a new selection, no start news
    hud_.options().chat = false;                              // the option "Participate In Chat", off. Not written to the store: only the options window's callbacks write the options.
    close_music();
    zoom_wanted_ = config_.zoom_given ? config_.zoom : zoom::kNormal;       // (the zoom that the game remembers is the game's: the Yard starts at the normal scale)
    state_ = AppState::Playing;
    match_started_ = true;
    apply_match_zoom();
    update_picture();
    yard_center_on(static_cast<int32_t>(current_level_.width()) * TILE_SIZE / 2, static_cast<int32_t>(current_level_.height()) * TILE_SIZE / 2);
    mouse_screen_x_ = layout_.width / 2;
    mouse_screen_y_ = layout_.height / 2;
    return true;
}

// The picture's size follows the page's box (the live switch of set_aspect, for a canvas of any size). The window is the canvas times a WHOLE scale: SDL scales a logical canvas into the window by
// the largest scale that fits, and a fraction of one leaves seams between the tiles of the map and bars beside it.
bool Application::set_yard_size(int32_t width, int32_t height, int32_t scale) {
    if (!renderer_ || !yard_mode_ || !yard_size_ok(width, height, scale)) return false;
    ViewportCamera& camera = renderer_->camera();
    const double middle_x = static_cast<double>(camera.x) + zoom::visible_exact(camera.viewport_w, camera.zoom) / 2.0;
    const double middle_y = static_cast<double>(camera.y) + zoom::visible_exact(camera.viewport_h, camera.zoom) / 2.0;
    if (window_ != nullptr) SDL_SetWindowSize(window_, width * scale, height * scale);
    yard_scale_ = scale;
    renderer_->set_canvas_size(width, height);                // (this forgets the picture: the picture that the application remembers is set again)
    layout_ = ScreenLayout::map_only(width, height);
    renderer_->set_picture(picture_);
    renderer_->set_layout(layout_);
    hud_.set_layout(layout_);
    update_picture();
    const LayoutRect view = layout_.view();
    if (state_ == AppState::Playing) camera.zoom = zoom::level_for_match(zoom_wanted_, zoom_fit(), zoom_limits());       // (the limit of the zoom-out follows the view's shape)
    const double ox = middle_x - zoom::visible_exact(view.w, camera.zoom) / 2.0;
    const double oy = middle_y - zoom::visible_exact(view.h, camera.zoom) / 2.0;
    const bool plain = camera.zoom == zoom::kNormal;
    camera.set_origin(plain ? std::floor(ox + 0.5) : ox, plain ? std::floor(oy + 0.5) : oy, current_level_.width(), current_level_.height());
    return true;
}

// Every 50 ms of real time is one step of the HUD's clock (the order ring, the status line, the selection's texts); nothing else steps. The batches are applied by the page's calls between frames.
void Application::run_yard(float dt) {
    tick_accumulator_ += dt;
    while (tick_accumulator_ >= 0.050f) {
        tick_accumulator_ -= 0.050f;
        hud_.update(sim_.get_world_state(), 1);
    }
}

size_t Application::yard_apply(const yard::Batch& batch) {
    if (!yard_mode_ || !yard_puppet_) return 0;
    yard_tick_ = batch.tick;
    std::vector<uint32_t> held = hud_.get_selected_ant_ids();          // (what the selection points at keeps its slot: it never turns into a stranger)
    if (hud_.get_selected_ant_id() != 0) held.push_back(hud_.get_selected_ant_id());
    return yard_puppet_->apply(batch.ants, held);
}

size_t Application::yard_set_hills(const std::vector<yard::Hill>& hills) {
    return yard_mode_ && yard_puppet_ ? yard_puppet_->set_hills(hills) : 0u;
}

std::vector<yard::Intent> Application::yard_take_intents() {
    return yard_mode_ && yard_sink_ ? yard_sink_->take() : std::vector<yard::Intent>{};
}

void Application::yard_center_on(int32_t world_x, int32_t world_y) {
    if (!renderer_ || !yard_mode_) return;
    renderer_->camera().center_on(world_x, world_y, current_level_.width(), current_level_.height());
}

bool Application::yard_show_own() {
    int32_t x = 0;
    int32_t y = 0;
    if (!yard_mode_ || !yard_puppet_ || !yard_puppet_->own_center(x, y)) return false;
    yard_center_on(x, y);
    return true;
}

int Application::yard_value(int what) const {
    if (!yard_mode_ || !renderer_ || !yard_puppet_ || !yard_sink_) return -1;
    switch (what) {
        case 0: return renderer_->canvas_w();
        case 1: return renderer_->canvas_h();
        case 2: return yard_scale_;
        case 3: return static_cast<int>(yard_puppet_->shown());
        case 4: return static_cast<int>(yard_puppet_->own_shown());
        case 5: return static_cast<int>(sim_.grid().anthills().size());
        case 6: return static_cast<int>(hud_.get_selected_ant_ids().size());
        case 7: return static_cast<int>(yard_sink_->queued().size());
        case 8: return static_cast<int>(renderer_->camera().zoom * 100.0f + 0.5f);
        case 9: return renderer_->camera().centre_world_x();
        case 10: return renderer_->camera().centre_world_y();
        case 11: return static_cast<int>(std::min<uint32_t>(yard_tick_, 0x7FFFFFFFu));
        default: return -1;
    }
}

}  // namespace ants::app

#if defined(__EMSCRIPTEN__)
extern ants::app::Application* g_web_app;

// For the page (docs/YARD.md): the box that the Yard fills has changed. `scale` is the whole number (1 .. 3) that the canvas is shown at: the window becomes the canvas times it. Returns 1 when the Yard
// has that size now, 0 for a size that it refuses (nothing changes), -1 when the game is not the Yard.
extern "C" EMSCRIPTEN_KEEPALIVE int ants_yard_set_size(int width, int height, int scale) {
    if (g_web_app == nullptr || !g_web_app->yard_mode()) return -1;
    return g_web_app->set_yard_size(width, height, scale) ? 1 : 0;
}

// ... what the page reads of the Yard, as numbers (Application::yard_value: the canvas, the scale, the ants that are shown, the orders that wait, the zoom, the view's middle, the last tick); -1 for
// anything else and for a game that is not the Yard
extern "C" EMSCRIPTEN_KEEPALIVE int ants_yard_probe(int what) {
    return g_web_app == nullptr ? -1 : g_web_app->yard_value(what);
}

// ... and the view goes to the middle of the visitor's own ants, as the page asks when they first come. Returns 1 when it did, 0 when no ant of the visitor's is shown, -1 when the game is not the Yard.
extern "C" EMSCRIPTEN_KEEPALIVE int ants_yard_show_own() {
    if (g_web_app == nullptr || !g_web_app->yard_mode()) return -1;
    return g_web_app->yard_show_own() ? 1 : 0;
}
#endif
