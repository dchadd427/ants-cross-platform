#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <functional>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

// Authentic Virtual Canvas Constants
constexpr int CANVAS_WIDTH  = 640;
constexpr int CANVAS_HEIGHT = 480;

// Authentic Playfield Viewport Bounds
constexpr int PLAYFIELD_X = 17;
constexpr int PLAYFIELD_Y = 22;
constexpr int PLAYFIELD_W = 441;
constexpr int PLAYFIELD_H = 439;

// Authentic HUD Subsystem Rectangles
constexpr int MINIMAP_X = 480;
constexpr int MINIMAP_Y = 22;
constexpr int MINIMAP_W = 160;
constexpr int MINIMAP_H = 104;

constexpr int CARD_X = 480;
constexpr int CARD_Y = 126;
constexpr int CARD_W = 160;
constexpr int CARD_H = 128;

constexpr int TILE_SIZE = 32;

/**
 * @brief Viewport Camera tracking world position with smooth scrolling and bounds clamping.
 */
struct ViewportCamera {
    float x{0.0f}; // Top-left world X in float pixels
    float y{0.0f}; // Top-left world Y in float pixels
    int32_t world_x{0};
    int32_t world_y{0};
    int32_t viewport_w{PLAYFIELD_W};
    int32_t viewport_h{PLAYFIELD_H};
    float scroll_speed{480.0f}; // Pixels per second

    void pan(float dx, float dy, float dt, uint32_t map_w, uint32_t map_h);
    void center_on(int32_t world_px, int32_t world_py, uint32_t map_w = 60, uint32_t map_h = 60);
    void clamp_to_bounds(uint32_t map_w, uint32_t map_h);
    void clamp(uint32_t map_w, uint32_t map_h) { clamp_to_bounds(map_w, map_h); }

    bool world_to_screen(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) const noexcept;
    bool screen_to_world(int32_t sx, int32_t sy, int32_t& wx, int32_t& wy) const noexcept;
    bool is_tile_visible(int32_t tx, int32_t ty) const noexcept;
};

using Camera = ViewportCamera;

/**
 * @brief Abstract rendering interface for sprite, shape, and text drawing.
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored = false) = 0;
    virtual void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored = false) = 0;
    virtual void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) = 0;
    virtual void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) = 0;
    virtual void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) = 0;
    virtual void set_hud_team(uint8_t team_id) = 0;
    virtual uint8_t get_hud_team() const = 0;
};

/**
 * @brief Texture cache converting raw paletted CHD sprites to hardware SDL_Textures.
 */
class TextureCache {
public:
    TextureCache(SDL_Renderer* renderer, const ants::assets::AssetArchive& archive);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    SDL_Texture* get_sprite_texture(uint32_t sprite_id, bool mirrored = false, uint8_t team_id = 0);
    SDL_Texture* get_named_sprite_texture(const std::string& name, bool mirrored = false, uint8_t team_id = 0);
    void clear();

private:
    SDL_Renderer* renderer_{nullptr};
    const ants::assets::AssetArchive& archive_;
    std::unordered_map<uint64_t, SDL_Texture*> textures_; // Key: (sprite_id << 4) | (team_id << 1) | (mirrored ? 1 : 0)
};

/**
 * @brief Static map decoration or multi-tile structure instance (e.g. grass, bear, glasses).
 */
struct StaticMapObject {
    int32_t world_x{0};
    int32_t world_y{0};
    int32_t sprite_id{-1};
    int32_t width{32};
    int32_t height{32};
    bool is_food{false};
    std::vector<std::pair<uint16_t, uint16_t>> food_tiles{};
};

/**
 * @brief Render item representation for depth-sorted playfield drawing.
 */
struct RenderItem {
    int32_t sort_y{0};
    std::function<void(SDL_Renderer*, TextureCache&)> draw_func;
};

/**
 * @brief Hardware-accelerated 2D Renderer managing canvas, viewport, terrain, and sprites.
 */
class Renderer : public IRenderer {
public:
    Renderer();
    ~Renderer() override;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(SDL_Window* window,
              const ants::assets::AssetArchive& archive,
              bool integer_scale = true);
    void shutdown();

    void set_level(const ants::assets::LevelData& level);

    // Frame Lifecycle
    void begin_frame();
    void render_world(const ants::sim::WorldState& world,
                      const ants::sim::Grid& grid,
                      int32_t selected_unit_id,
                      const std::vector<uint32_t>& selected_unit_ids = {},
                      bool show_all_health_bars = false,
                      bool show_tile_grid = false,
                      int32_t mouse_x = -1,
                      int32_t mouse_y = -1);
    void render_minimap(const ants::sim::WorldState& world,
                        const ants::sim::Grid& grid);
    void render_hud_chrome(const ants::sim::WorldState& world,
                           int32_t selected_unit_id);
    void end_frame();

    // IRenderer Implementation
    void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored = false) override;
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored = false) override;
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override;
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override;
    void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) override;
    void set_hud_team(uint8_t team_id) override { hud_team_id_ = team_id; }
    uint8_t get_hud_team() const override { return hud_team_id_; }

    // Camera Accessors
    ViewportCamera& camera() noexcept { return camera_; }
    const ViewportCamera& camera() const noexcept { return camera_; }

    // Helpers
    SDL_Renderer* get_sdl_renderer() const noexcept { return renderer_; }
    TextureCache* get_texture_cache() const noexcept { return texture_cache_.get(); }
    const ants::assets::AssetArchive* get_archive() const noexcept { return archive_; }
    void request_screenshot(const std::string& path) { pending_screenshot_ = path; }
    bool save_screenshot(const std::string& path);

private:
    void render_terrain_layer1(const ants::sim::Grid& grid);
    void render_terrain_layer2_structures(const ants::sim::Grid& grid);
    void render_ant_units(const ants::sim::WorldState& world, int32_t selected_unit_id, const std::vector<uint32_t>& selected_unit_ids = {}, bool show_all_health_bars = false);
    void render_tile_grid(const ants::sim::Grid& grid, int32_t mouse_x, int32_t mouse_y);
    void draw_ant_shadow(int32_t anchor_sx, int32_t anchor_sy, int32_t altitude_z);
    void draw_single_ant(const ants::sim::AntSnapshot& ant, bool is_selected, bool show_health_bar = false);

    SDL_Renderer* renderer_{nullptr};
    const ants::assets::AssetArchive* archive_{nullptr};
    std::unique_ptr<TextureCache> texture_cache_;
    ViewportCamera camera_;

    uint32_t map_width_{0};
    uint32_t map_height_{0};
    std::vector<int32_t> tile_sprite_ids_; // Pre-resolved tile dictionary to sprite ID cache
    std::vector<std::pair<int32_t, int32_t>> tile_offsets_; // Pre-resolved Table 4 (dx, dy) anchor offsets
    std::vector<std::vector<int32_t>> tile_anim_frames_; // Pre-resolved animated tile frames
    uint32_t anim_tick_{0};
    std::array<SDL_Point, 4> anthill_bases_{};
    bool has_anthill_bases_{false};
    std::vector<StaticMapObject> static_decor_objects_;
    std::vector<RenderItem> render_queue_;
    std::string pending_screenshot_;
    uint8_t hud_team_id_{0};
};

} // namespace ants::app
