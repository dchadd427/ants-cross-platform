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

#ifdef ANTS_ENABLE_SDL_TTF
  #if defined(__has_include)
    #if __has_include(<SDL_ttf.h>)
      #include <SDL_ttf.h>
    #elif __has_include(<SDL2/SDL_ttf.h>)
      #include <SDL2/SDL_ttf.h>
    #endif
  #else
    #include <SDL2/SDL_ttf.h>
  #endif
#endif

#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

enum class FontSize : uint8_t {
    Small = 0,   // ~12px (authentic HUD chat, status labels, unit badges)
    Medium = 1,  // ~14px (buttons, scorecard rows, dialogs)
    Large = 2    // ~18px (screen titles, victory headers)
};

/**
 * @brief Authentic 1998 cursor modes reverse-engineered from Ants.exe (0x1026c5c & 0x1027e65).
 */
enum class CursorType : uint8_t {
    Normal = 0,      // Mode 1: c_normal (Anim 41)
    Select = 1,      // Mode 2: c_select (Anim 42)
    Move = 2,        // Mode 3: c_mov1 (Anim 44)
    ThiefTarget = 3, // Mode 4: c_targ1 (Anim 43)
    Attack = 4,      // Mode 5: c_attack (Anim 33)
    ScrollN = 5,     // Mode 6 dir 0: CUR_N (Anim 51)
    ScrollNE = 6,    // Mode 6 dir 1: CUR_NE (Anim 46)
    ScrollE = 7,     // Mode 6 dir 2: CUR_E (Anim 45)
    ScrollSE = 8,    // Mode 6 dir 3: CUR_SE (Anim 49)
    ScrollS = 9,     // Mode 6 dir 4: CUR_S (Anim 48)
    ScrollSW = 10,   // Mode 6 dir 5: CUR_SW (Anim 52)
    ScrollW = 11,    // Mode 6 dir 6: CUR_W (Anim 50)
    ScrollNW = 12,   // Mode 6 dir 7: CUR_NW (Anim 47)
    Food = 13        // Mode 7: c_food (Anim 54)
};

/**
 * @brief Client-side transient visual effect (e.g. xmarks ground click indicator).
 */
struct TransientEffect {
    std::string anim_name;
    int32_t px{0};
    int32_t py{0};
    float elapsed_sec{0.0f};
    bool is_screen_space{false};
};

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
    virtual void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) {
        (void)size;
        draw_text(text, x, y, color);
    }
    virtual int32_t get_text_width(const std::string& text, FontSize size = FontSize::Small) const {
        (void)size;
        return static_cast<int32_t>(text.size()) * 6;
    }
    virtual int32_t get_text_height(FontSize size = FontSize::Small) const {
        (void)size;
        return 7;
    }
    virtual void set_hud_team(uint8_t team_id) = 0;
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
    bool is_base_bomb_sprite(uint32_t sprite_id) const noexcept;
    uint32_t get_team_bomb_sprite_index(uint8_t team_id) const noexcept;
    void clear();

private:
    SDL_Renderer* renderer_{nullptr};
    const ants::assets::AssetArchive& archive_;
    std::unordered_map<uint64_t, SDL_Texture*> textures_; // Key: (sprite_id << 4) | (team_id << 1) | (mirrored ? 1 : 0)
    int32_t base_bomb_sprite_id_{-1};
    int32_t team_bomb_sprite_ids_[4]{-1, -1, -1, -1};
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
    uint16_t anchor_x{0};
    uint16_t anchor_y{0};
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
    void set_fullscreen(bool fullscreen);

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
                      int32_t mouse_y = -1,
                      int32_t selected_base_team_id = -1);
    void end_frame();

    // IRenderer Implementation
    void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored = false) override;
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored = false) override;
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override;
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override;
    void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) override;
    void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) override;
    int32_t get_text_width(const std::string& text, FontSize size = FontSize::Small) const override;
    int32_t get_text_height(FontSize size = FontSize::Small) const override;
    void set_hud_team(uint8_t team_id) override { hud_team_id_ = team_id; }

    // Camera Accessors
    ViewportCamera& camera() noexcept { return camera_; }
    const ViewportCamera& camera() const noexcept { return camera_; }

    // Helpers
    SDL_Renderer* get_sdl_renderer() const noexcept { return renderer_; }
    TextureCache* get_texture_cache() const noexcept { return texture_cache_.get(); }
    const ants::assets::AssetArchive* get_archive() const noexcept { return archive_; }
    void request_screenshot(const std::string& path) { pending_screenshot_ = path; }
    bool save_screenshot(const std::string& path);
    static size_t get_anim_subitem_by_time(const ants::assets::AnimationSequence& seq, uint32_t now_ms);

    // Software Cursor & Transient Effects
    void spawn_transient_effect(const std::string& anim_name, int32_t px, int32_t py, bool is_screen_space = false);
    void update_transient_effects(float dt);
    void render_transient_effects();
    void render_software_cursor(CursorType type, int32_t screen_x, int32_t screen_y, uint32_t anim_tick = 0);
    void set_cursor(CursorType type) noexcept { current_cursor_ = type; }
    CursorType get_cursor() const noexcept { return current_cursor_; }

private:
    void render_terrain_layer1(const ants::sim::Grid& grid);
    void render_terrain_layer2_structures(const ants::sim::Grid& grid, const ants::sim::WorldState* world = nullptr);
    void render_flower_droppers(const ants::sim::WorldState& world);
    void render_terrain_layer3_canopy();
    void render_fog_of_war(const ants::sim::WorldState& world);
    void render_visual_effects(const ants::sim::WorldState& world);
    void render_ant_units(const ants::sim::WorldState& world, int32_t selected_unit_id, const std::vector<uint32_t>& selected_unit_ids = {}, bool show_all_health_bars = false);
    void render_tile_grid(const ants::sim::Grid& grid, int32_t mouse_x, int32_t mouse_y);
    void draw_ant_shadow(int32_t anchor_sx, int32_t anchor_sy, int32_t altitude_z);
    void draw_single_ant(const ants::sim::AntSnapshot& ant, bool is_selected, bool show_health_bar = false, bool is_under_battle = false);
    void draw_anthill_selection_brackets(int32_t x, int32_t y, int32_t w = 128, int32_t h = 128);

#ifdef ANTS_ENABLE_SDL_TTF
    struct CachedTextEntry {
        SDL_Texture* texture{nullptr};
        int32_t width{0};
        int32_t height{0};
        uint32_t last_frame{0};
    };

    struct TextCacheKey {
        std::string text;
        uint32_t color{0};
        uint8_t size{0};

        bool operator==(const TextCacheKey& o) const noexcept {
            return color == o.color && size == o.size && text == o.text;
        }
    };

    struct TextCacheKeyHash {
        size_t operator()(const TextCacheKey& k) const noexcept {
            size_t h1 = std::hash<std::string>{}(k.text);
            size_t h2 = std::hash<uint32_t>{}(k.color);
            return h1 ^ (h2 << 1) ^ (static_cast<size_t>(k.size) << 2);
        }
    };

    TTF_Font* font_small_{nullptr};
    TTF_Font* font_medium_{nullptr};
    TTF_Font* font_large_{nullptr};
    bool ttf_initialized_{false};
    uint32_t text_frame_counter_{0};
    std::unordered_map<TextCacheKey, CachedTextEntry, TextCacheKeyHash> text_cache_;
#endif

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
    std::vector<StaticMapObject> static_decor_objects_; // Layer 2 interactive objects
    std::vector<StaticMapObject> layer3_canopy_objects_; // Layer 3 overhanging canopy decorations
    std::vector<RenderItem> render_queue_;
    std::string pending_screenshot_;
    uint8_t hud_team_id_{0};
    bool integer_scale_{true};
    bool is_fullscreen_{false};
    CursorType current_cursor_{CursorType::Normal};
    std::vector<TransientEffect> transient_effects_{};
};

} // namespace ants::app
