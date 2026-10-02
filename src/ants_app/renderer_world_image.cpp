// Renderer::render_world_image: the whole world of a level, drawn once by the game's own world drawing (Renderer::render_world) into render-target textures and read back as one image.
// It is what the wide setup screen's map preview is made from (map_preview.hpp); nothing of the match that may be running changes (see the declaration).
#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

#include "ants_app/renderer.hpp"

namespace ants::app {

namespace {

// A world that does not fit one render target is drawn in windows: `kTileContent` pixels of the world that the tile keeps, `kTileOverscan` pixels of the neighbours around them that are cut
// off again (the original draws an object when its anchor is within three cells of the view, so an object that crosses a seam is drawn by the tile that holds its anchor and, because the
// overscan is eight cells, by the neighbour too)
constexpr int32_t kTileContent = 1536;
constexpr int32_t kTileOverscan = 256;
constexpr int32_t kDefaultMaxSide = kTileContent + 2 * kTileOverscan;       // 2048: one target of this size is made on any device that has render targets at all
// The image of the whole world is held in memory (RGBA8): a world of more than 2^25 pixels (a map of 181 x 181 cells, 134 MB) is not drawn
constexpr int64_t kMaxWorldPixels = int64_t{1} << 25;
// The map view of every layout is at least the original's 442 x 440 (ScreenLayout::with_size never goes below the original's screen)
constexpr int32_t kMinViewW = ScreenLayout::kClassicViewW;
constexpr int32_t kMinViewH = ScreenLayout::kClassicViewH;

}  // namespace

void Renderer::swap_level_state(LevelState& o) noexcept {
    std::swap(map_width_, o.map_width);
    std::swap(map_height_, o.map_height);
    tile_anim_id_.swap(o.tile_anim_id);
    anim_bounds_.swap(o.anim_bounds);
    anim_frame_stamp_.swap(o.anim_frame_stamp);
    anim_frame_cache_.swap(o.anim_frame_cache);
    std::swap(map_epoch_ms_, o.map_epoch_ms);
    std::swap(template_now_ms_, o.template_now_ms);
    std::swap(level_set_, o.level_set);
    std::swap(hill_anchor_cell_, o.hill_anchor_cell);
    std::swap(anthill_bases_, o.anthill_bases);
    std::swap(has_anthill_bases_, o.has_anthill_bases);
    static_decor_objects_.swap(o.static_decor_objects);
    object_index_by_cell_.swap(o.object_index_by_cell);
    object_list_sprites_.swap(o.object_list_sprites);
}

bool Renderer::render_world_image(const ants::assets::LevelData& level, const ants::sim::WorldState& world, const ants::sim::Grid& grid, WorldImage& out, std::string* why, int32_t max_side) {
    out = WorldImage{};
    auto fail = [&](const std::string& reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (renderer_ == nullptr || archive_ == nullptr || !texture_cache_) return fail("the renderer is not up");
    if (SDL_RenderTargetSupported(renderer_) != SDL_TRUE) return fail("the device has no render targets");
    const int64_t cols = level.width();
    const int64_t rows = level.height();
    if (cols <= 0 || rows <= 0 || cols > 4096 || rows > 4096) return fail("the level has no usable size");
    const int64_t world_w = cols * TILE_SIZE;
    const int64_t world_h = rows * TILE_SIZE;
    if (world_w * world_h > kMaxWorldPixels) return fail("the world is too big to be drawn as one image");

    // how big a target may be: the device's limit, the caller's, and 2048
    SDL_RendererInfo info{};
    int64_t limit = kDefaultMaxSide;
    if (SDL_GetRendererInfo(renderer_, &info) == 0) {
        if (info.max_texture_width > 0) limit = std::min<int64_t>(limit, info.max_texture_width);
        if (info.max_texture_height > 0) limit = std::min<int64_t>(limit, info.max_texture_height);
    }
    if (max_side > 0) limit = std::min<int64_t>(limit, max_side);
    const bool whole = world_w <= limit && world_h <= limit;
    const int64_t content = whole ? 0 : limit - 2 * kTileOverscan;
    if (!whole && content < 64) return fail("the device's render targets are too small");

    WorldImage image;
    image.width = static_cast<int32_t>(world_w);
    image.height = static_cast<int32_t>(world_h);
    image.rgba.assign(static_cast<size_t>(world_w * world_h) * 4u, 0);          // (the one big allocation is made before anything of the live game is touched)

    // what the live game has: put back when this function ends, however it ends. render_world itself changes more than set_level does: the selection markers' clocks (the ears of the
    // selected ants, the hill's brackets) restart when nothing is selected, and the queues are rebuilt; all of it is kept.
    LevelState live;
    swap_level_state(live);
    const ScreenLayout saved_layout = layout_;
    const ViewportCamera saved_camera = camera_;
    const LayoutRect saved_picture = picture_;
    const bool saved_inset = picture_inset_;
    const LayoutPoint saved_origin = origin_;
    const uint8_t saved_team = hud_team_id_;
    const uint32_t saved_sub_tick = sub_tick_ms_;
    const auto saved_ears = ears_state_;
    const int32_t saved_hill_marker_team = hill_marker_team_;
    const int64_t saved_hill_marker_start = hill_marker_start_ms_;
    SDL_Texture* const previous_target = SDL_GetRenderTarget(renderer_);
    SDL_Texture* target = nullptr;
    struct PutBack {
        std::function<void()> action;
        ~PutBack() { action(); }
    } put_back{[&]() {
        SDL_SetRenderTarget(renderer_, previous_target);
        if (target != nullptr) SDL_DestroyTexture(target);
        render_queue_.clear();
        overlay_queue_.clear();
        swap_level_state(live);
        layout_ = saved_layout;
        camera_ = saved_camera;
        picture_ = saved_picture;
        picture_inset_ = saved_inset;
        origin_ = saved_origin;
        hud_team_id_ = saved_team;
        sub_tick_ms_ = saved_sub_tick;
        ears_state_ = saved_ears;
        hill_marker_team_ = saved_hill_marker_team;
        hill_marker_start_ms_ = saved_hill_marker_start;
    }};

    // the level as a new match sets it up, at the clock's start (every template shows its first frame: the same picture every time)
    set_level(level);
    template_now_ms_ = 0;
    ++frame_counter_;
    hud_team_id_ = 0;
    origin_ = LayoutPoint{};
    picture_inset_ = false;

    bool ok = true;
    std::string reason;
    int64_t target_w = 0;
    int64_t target_h = 0;
    std::vector<uint8_t> tile_pixels;
    const int64_t step = whole ? world_w : content;
    const int64_t step_y = whole ? world_h : content;
    for (int64_t ty = 0; ok && ty < world_h; ty += step_y) {
        for (int64_t tx = 0; ok && tx < world_w; tx += step) {
            const int64_t keep_w = std::min(step, world_w - tx);              // the part of the world that this tile keeps
            const int64_t keep_h = std::min(step_y, world_h - ty);
            const int64_t over = whole ? 0 : kTileOverscan;
            const int64_t view_w = std::max<int64_t>(keep_w + 2 * over, kMinViewW);
            const int64_t view_h = std::max<int64_t>(keep_h + 2 * over, kMinViewH);
            if (target == nullptr || target_w != view_w || target_h != view_h) {
                if (target != nullptr) SDL_DestroyTexture(target);
                target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, static_cast<int>(view_w), static_cast<int>(view_h));
                target_w = view_w;
                target_h = view_h;
                if (target == nullptr) {
                    ok = false;
                    reason = std::string("the offscreen target could not be made: ") + SDL_GetError();
                    break;
                }
            }
            if (SDL_SetRenderTarget(renderer_, target) != 0) {
                ok = false;
                reason = std::string("the offscreen target could not be set: ") + SDL_GetError();
                break;
            }
            // the window of the world: the map view is the whole target, its top left corner is the world pixel (tx - over, ty - over)
            ScreenLayout layout = ScreenLayout::with_size(static_cast<int32_t>(view_w) + (ScreenLayout::kClassicWidth - kMinViewW), static_cast<int32_t>(view_h) + (ScreenLayout::kClassicHeight - kMinViewH));
            layout.view_x = 0;
            layout.view_y = 0;
            layout_ = layout;
            camera_.set_view(layout.view());
            camera_.centre_small_maps = false;
            camera_.x = static_cast<float>(tx - over);
            camera_.y = static_cast<float>(ty - over);
            camera_.world_x = static_cast<int32_t>(camera_.x);
            camera_.world_y = static_cast<int32_t>(camera_.y);
            picture_ = LayoutRect{0, 0, static_cast<int32_t>(view_w), static_cast<int32_t>(view_h)};
            SDL_RenderSetClipRect(renderer_, nullptr);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderClear(renderer_);
            render_world(world, grid, -1, {}, false, false, -1, -1, -1, 0.0f);

            // read the part that the tile keeps
            const SDL_Rect keep{static_cast<int>(over), static_cast<int>(over), static_cast<int>(keep_w), static_cast<int>(keep_h)};
            if (whole) {
                if (SDL_RenderReadPixels(renderer_, &keep, SDL_PIXELFORMAT_RGBA32, image.rgba.data(), static_cast<int>(keep_w * 4)) != 0) {
                    ok = false;
                    reason = std::string("the offscreen image could not be read back: ") + SDL_GetError();
                }
            } else {
                tile_pixels.resize(static_cast<size_t>(keep_w * keep_h) * 4u);
                if (SDL_RenderReadPixels(renderer_, &keep, SDL_PIXELFORMAT_RGBA32, tile_pixels.data(), static_cast<int>(keep_w * 4)) != 0) {
                    ok = false;
                    reason = std::string("the offscreen image could not be read back: ") + SDL_GetError();
                } else {
                    for (int64_t row = 0; row < keep_h; ++row) {
                        std::memcpy(&image.rgba[static_cast<size_t>((ty + row) * world_w + tx) * 4u], &tile_pixels[static_cast<size_t>(row * keep_w) * 4u], static_cast<size_t>(keep_w) * 4u);
                    }
                }
            }
        }
    }
    if (!ok) return fail(reason);
    // the image is opaque (the target was cleared to opaque black and every sprite is blended over it): make sure of it for a device whose target has no alpha channel
    for (size_t i = 3; i < image.rgba.size(); i += 4) image.rgba[i] = 255;
    out = std::move(image);
    return true;
}

}  // namespace ants::app
