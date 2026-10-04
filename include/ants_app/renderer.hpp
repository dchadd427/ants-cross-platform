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

#include "ants_app/screen_layout.hpp"
#include "ants_app/view_zoom.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

/// The size of a text is the original's: a GDI font cell height in pixels. Ants.exe creates its "Franklin Gothic Medium" fonts with `lfHeight` = the
/// height given to the label (FUN_0102b05f); a positive lfHeight is the cell height (ascent + descent), which is also the distance between the lines
/// of a multi-line label. The renderer opens its TrueType font so that its cell height is exactly this number. Heights and where they are used:
/// docs/GAME_REVERSE_ENGINEERING.md 5.14.
enum class FontSize : uint8_t {
    Px12 = 12,   // a label's default (FUN_0102ad16): the status line, the chat log
    Px14 = 14,   // the score bars' player labels, the setup screen's prompt
    Px18 = 18,   // the setup screen's names and map name, the results screen's rows
    Px20 = 20,   // "Waiting for scores..."
    Px24 = 24,   // dialog texts (quit, alliance, notices), "Waiting for others..."
    Px35 = 35    // the start dialog's main text
};

/// The cell height of a font size in pixels (the enumerator's value)
inline constexpr int32_t font_cell_height(FontSize size) noexcept { return static_cast<int32_t>(size); }

/**
 * @brief Authentic 1998 cursor modes reverse-engineered from Ants.exe (0x1026c5c & 0x1027e65).
 */
enum class CursorType : uint8_t {
    Normal = 0,      // Mode 1: c_normal (Anim 41)
    Select = 1,      // Mode 2: c_select (Anim 42)
    Move = 2,        // Mode 3: c_mov1 (Anim 44)
    ThiefTarget = 3, // Mode 4: c_targ1 (Anim 43)
    Target = 3,      // Universal Mode 4 alias: c_targ1 (Anim 43)
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

// Authentic Virtual Canvas Constants: the original's screen (ScreenLayout::classic(); the renderer's own canvas is set with Renderer::set_canvas_size)
constexpr int CANVAS_WIDTH  = ScreenLayout::kClassicWidth;
constexpr int CANVAS_HEIGHT = ScreenLayout::kClassicHeight;

// The map view of the original (Ants.exe: the view window's rectangle (16, 21) - (458, 461) set at 0x100a32b and given to the view at 0x100dcbf, docs 5.44): the world
// pixel (camera.x, camera.y) is at the screen pixel (16, 21), the view is 442 x 440 (the camera's largest origin is the map's size less that). The UI shell drawn on top of it
// has a black border at x = 16 / y = 21 and its hole starts at (17, 22), so the visible area is the same as before v0.0.67 (it used to be placed one pixel right and down).
// These are the CLASSIC view (ScreenLayout::classic().view()); what the renderer, the HUD and the pointer use is the layout they were given (Renderer::set_layout).
constexpr int PLAYFIELD_X = ScreenLayout::kClassicViewX;
constexpr int PLAYFIELD_Y = ScreenLayout::kClassicViewY;
constexpr int PLAYFIELD_W = ScreenLayout::kClassicViewW;
constexpr int PLAYFIELD_H = ScreenLayout::kClassicViewH;


constexpr int TILE_SIZE = 32;

// Colour modes accepted by TextureCache::get_sprite_texture (the `team_id` argument).
//   0..3               legacy HUD path: per-team HUD table on palette indices 1..31 plus the +offset ramp shift
//   TEAM_NONE          raw CHD palette. The original never remaps terrain, map objects, effects, plants or cursors.
//   ant_colour(player) the original ant blit (Ants.exe FUN_0101b802 -> FUN_0102cfef): the colour offset
//                      {0,20,40,60} is added to EVERY non-transparent pixel index, except for images whose name
//                      starts with a digit (lunchboxes, snorkel head, death art), which are never recoloured.
constexpr uint8_t TEAM_NONE = 0xFF;
constexpr uint8_t ANT_COLOUR_BASE = 0x10;
constexpr uint8_t ant_colour(uint8_t player_id) noexcept {
    return static_cast<uint8_t>(ANT_COLOUR_BASE + (player_id & 3u));
}

/**
 * @brief Order in which the original draws the parts of one animation frame.
 *
 * Ants.exe FUN_0102b8d7 (Sprite::DrawAt) walks the frame's part list newest-to-oldest, so the LAST part stored in
 * ants.chd is drawn FIRST and the first stored part ends up on top (e.g. shadow.bmp is always the last part).
 * Returns the part indices in drawing order.
 */
inline std::vector<size_t> frame_part_draw_order(size_t part_count) {
    std::vector<size_t> order(part_count);
    for (size_t i = 0; i < part_count; ++i) order[i] = part_count - 1 - i;
    return order;
}

/**
 * @brief Viewport Camera tracking world position with smooth scrolling and bounds clamping.
 *
 * `x`, `y` (and their whole parts `world_x`, `world_y`) are the WORLD point at the view's top left corner. The view is a rectangle of the screen (`view_x`, `view_y`, `viewport_w`,
 * `viewport_h`) and the world it shows is `viewport / zoom` world pixels (view_zoom.hpp; the zoom is 1 unless the player used the wheel): a world pixel is `zoom` screen pixels.
 * At the zoom 1 everything here is what it always was; the origin lies on a grid of one screen pixel at the zoom (half a world pixel at 2, two at 0.5, 0.71 at 1.41).
 */
struct ViewportCamera {
    float x{0.0f}; // Top-left world X in float pixels
    float y{0.0f}; // Top-left world Y in float pixels
    int32_t world_x{0};
    int32_t world_y{0};
    int32_t view_x{PLAYFIELD_X};       // where the view is on the screen: the world pixel (x, y) is at the screen pixel (view_x, view_y)
    int32_t view_y{PLAYFIELD_Y};
    int32_t viewport_w{PLAYFIELD_W};   // the view's size in SCREEN pixels
    int32_t viewport_h{PLAYFIELD_H};
    /// A map that is smaller than the view on an axis (a 16 x 16 map is 512 pixels wide, the 16:9 view 762): true centres the map in the view, with black around it, and the camera stays
    /// fixed on that axis; false (the default) puts it at the view's top left corner. The original's own view is smaller than its smallest map, so nothing of it ever shows there and the
    /// classic camera keeps the old rule (the fingerprints of the classic picture pin it); Renderer::set_layout sets this for every layout that is not the original's.
    bool centre_small_maps{false};
    /// The zoom of the view: screen pixels per world pixel, one of the levels that view_zoom.hpp offers (from the map's limit up to 2; 1 is the original's picture). Everything that converts
    /// between the screen and the world reads it.
    float zoom{zoom::kNormal};

    /// The view of a layout (ScreenLayout::view()): its origin on the screen and its size
    void set_view(const LayoutRect& view) noexcept {
        view_x = view.x;
        view_y = view.y;
        viewport_w = view.w;
        viewport_h = view.h;
    }

    /// The world that the view shows, in world pixels (rounded up)
    int32_t visible_w() const noexcept { return zoom::visible(viewport_w, zoom); }
    int32_t visible_h() const noexcept { return zoom::visible(viewport_h, zoom); }

    /// The world pixel under the screen pixel that is `offset` screen pixels right of / below the view's corner. At the zoom 1 it is the whole origin plus the offset (the
    /// input code has always done that sum with `world_x`, which is what the tests of the classic picture set); at another zoom it is the origin plus the offset over the zoom.
    int32_t world_x_at(int32_t offset) const noexcept { return zoom == zoom::kNormal ? world_x + offset : zoom::world_at(static_cast<double>(x), zoom, offset); }
    int32_t world_y_at(int32_t offset) const noexcept { return zoom == zoom::kNormal ? world_y + offset : zoom::world_at(static_cast<double>(y), zoom, offset); }
    /// The world coordinate of the right / bottom EDGE of the screen pixel at the offset, rounded up (the end of a rubber band: every world pixel the screen pixels cover is in)
    int32_t world_x_edge(int32_t offset) const noexcept { return zoom == zoom::kNormal ? world_x + offset : zoom::world_edge_up(static_cast<double>(x), zoom, offset); }
    int32_t world_y_edge(int32_t offset) const noexcept { return zoom == zoom::kNormal ? world_y + offset : zoom::world_edge_up(static_cast<double>(y), zoom, offset); }
    /// The origin in screen pixels at the zoom (world pixels times the zoom): the edge scroll's and the minimap's numbers (edge_scroll.hpp). At the zoom 1 it is world_x / world_y.
    int32_t origin_screen_x() const noexcept { return zoom == zoom::kNormal ? world_x : static_cast<int32_t>(std::lround(static_cast<double>(x) * static_cast<double>(zoom))); }
    int32_t origin_screen_y() const noexcept { return zoom == zoom::kNormal ? world_y : static_cast<int32_t>(std::lround(static_cast<double>(y) * static_cast<double>(zoom))); }
    /// The world point in the middle of the view (the sound's listener)
    int32_t centre_world_x() const noexcept { return zoom == zoom::kNormal ? world_x + viewport_w / 2 : static_cast<int32_t>(std::floor(static_cast<double>(x) + zoom::visible_exact(viewport_w, zoom) / 2.0)); }
    int32_t centre_world_y() const noexcept { return zoom == zoom::kNormal ? world_y + viewport_h / 2 : static_cast<int32_t>(std::floor(static_cast<double>(y) + zoom::visible_exact(viewport_h, zoom) / 2.0)); }

    /// Moves the view by whole WORLD pixels (the scroll steps of the original's input task at the zoom 1) and keeps it inside the map.
    void scroll_pixels(int32_t dx, int32_t dy, uint32_t map_w, uint32_t map_h) {
        x += static_cast<float>(dx);
        y += static_cast<float>(dy);
        clamp_to_bounds(map_w, map_h);
    }
    /// Moves the view by whole SCREEN pixels: the edge scroll, the minimap and the keys move it by the same distance on the screen at every zoom (world pixels over the zoom)
    void scroll_screen(int32_t dx, int32_t dy, uint32_t map_w, uint32_t map_h) {
        x += static_cast<float>(dx) / zoom;
        y += static_cast<float>(dy) / zoom;
        clamp_to_bounds(map_w, map_h);
    }
    /// Puts the origin at a world point (on the grid of the zoom, inside the map): the start view, a test
    void set_origin(double ox, double oy, uint32_t map_w, uint32_t map_h) {
        x = static_cast<float>(ox);
        y = static_cast<float>(oy);
        clamp_to_bounds(map_w, map_h);
    }
    /// Changes the zoom to `level`, keeping the world point under the screen pixel that is (anchor_dx, anchor_dy) from the view's corner under it as far as the map's edges allow
    /// (view_zoom.hpp zoomed). The level must be a zoom (from zoom::kSmallest to zoom::kIn); the Application chooses it among the levels that the view over the map offers.
    void set_zoom(float level, int32_t anchor_dx, int32_t anchor_dy, uint32_t map_w, uint32_t map_h);
    void center_on(int32_t world_px, int32_t world_py, uint32_t map_w = 60, uint32_t map_h = 60);
    void clamp_to_bounds(uint32_t map_w, uint32_t map_h);

    bool world_to_screen(int32_t wx, int32_t wy, int32_t& sx, int32_t& sy) const noexcept;
    bool screen_to_world(int32_t sx, int32_t sy, int32_t& wx, int32_t& wy) const noexcept;
};


/// The whole world of a level drawn once, 1 world pixel per image pixel: tightly packed RGBA8 (alpha 255), width x height pixels (the map's columns x 32 by its rows x 32)
struct WorldImage {
    int32_t width{0};
    int32_t height{0};
    std::vector<uint8_t> rgba;
    bool valid() const noexcept { return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u; }
};

/**
 * @brief Abstract rendering interface for sprite, shape, and text drawing.
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored = false) = 0;
    virtual void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored = false) = 0;
    /// A part of a sprite, scaled to a rectangle: the source rectangle (sx, sy, sw, sh) of the sprite fills (x, y, w, h). With the same size it is a plain copy of a part; a source
    /// one pixel wide (high) over a wider (taller) destination repeats that column (row): that is how the frame of the match screen grows (shell_layout.hpp). A renderer that does
    /// not draw sprites (the test renderers that only count calls) may ignore it.
    virtual void draw_sprite_region(uint32_t sprite_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
        (void)sprite_id; (void)x; (void)y; (void)w; (void)h; (void)sx; (void)sy; (void)sw; (void)sh;
    }
    /// Everything that is drawn afterwards is moved by (x, y), until set_origin(0, 0): a window of the original's own 640 x 480 screen (the options screen, the quick help, a dialog) is drawn
    /// with its own numbers and put where the picture wants it. Not a clip: nothing is cut. A renderer that records its calls sees them as they are given (without the move).
    virtual void set_origin(int32_t x, int32_t y) { (void)x; (void)y; }
    virtual void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) = 0;
    virtual void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) = 0;
    virtual void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) = 0;
    virtual void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) {
        (void)size;
        draw_text(text, x, y, color);
    }
    /// The text as draw_text draws it, but never wider than `max_width` pixels: a text that is wider is squeezed horizontally to that width (its height stays). The original counts in the digits of its
    /// own face, which are 8 pixels wide at the size of its labels (docs 5.49); the bundled face has wider ones, and this puts them into the same space. A renderer that cannot squeeze, and
    /// every renderer that only records its calls, draws the text as draw_text does.
    virtual void draw_text_squeezed(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size, int32_t max_width) {
        (void)max_width;
        draw_text(text, x, y, color, size);
    }
    virtual int32_t get_text_width(const std::string& text, FontSize size = FontSize::Px12) const {
        (void)size;
        return static_cast<int32_t>(text.size()) * 6;
    }
    virtual int32_t get_text_height(FontSize size = FontSize::Px12) const {
        (void)size;
        return 7;
    }
    virtual void set_hud_team(uint8_t team_id) = 0;
    /// Restricts everything that is drawn afterwards to a rectangle until clear_clip_rect(): the original draws a label into a surface of the label's own
    /// size, so what sticks out of the box (the half line at the edge of the chat log) is not seen. A renderer without clipping draws it all.
    virtual void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
    virtual void clear_clip_rect() {}
    // Blits a tightly packed RGBA8 image of w x h pixels at (x, y) (used by the minimap). The default draws pixel by
    // pixel; Renderer overrides it with a cached streaming texture.
    virtual void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) {
        for (int32_t j = 0; j < h; ++j) {
            for (int32_t i = 0; i < w; ++i) {
                const uint8_t* px = rgba + (static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i)) * 4u;
                fill_rect(x + i, y + j, 1, 1, ants::assets::ColorRGBA{px[0], px[1], px[2], px[3]});
            }
        }
    }
    /// The whole world of `level` as the game draws it (terrain, objects, plants, hills, food, power-ups, ants: the match screen's view of it, no HUD, no fog, no cursor) into `out`, at the
    /// state of `world` / `grid` (a simulation that was started for the level). A renderer that cannot draw offscreen (the test renderers that only count calls; a device without render
    /// targets) returns false and says why in `why` when it is given; Renderer::render_world_image is the real one. `max_side` > 0 limits the side of the offscreen target (0: what the device allows).
    virtual bool render_world_image(const ants::assets::LevelData& level, const ants::sim::WorldState& world, const ants::sim::Grid& grid, WorldImage& out, std::string* why = nullptr,
                                    int32_t max_side = 0) {
        (void)level; (void)world; (void)grid; (void)out; (void)max_side;
        if (why != nullptr) *why = "this renderer draws no offscreen images";
        return false;
    }
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

    SDL_Texture* get_sprite_texture(uint32_t sprite_id, bool mirrored = false, uint8_t team_id = TEAM_NONE);

    // Pure palette selection for one image under a colour mode (see TEAM_NONE / ant_colour above). Public so tests
    // can check the original's per-pixel colour rule without an SDL renderer.
    static std::array<ants::assets::ColorRGBA, 256> compose_palette(const std::array<ants::assets::ColorRGBA, 256>& base,
                                                                    const std::string& image_name,
                                                                    uint8_t team_id);
    void clear();

private:
    SDL_Renderer* renderer_{nullptr};
    const ants::assets::AssetArchive& archive_;
    std::unordered_map<uint64_t, SDL_Texture*> textures_; // Key: (sprite_id << 9) | (team_id << 1) | (mirrored ? 1 : 0)
};

/**
 * @brief Layer-2 object instance (rocks, grass, toys, food ...) anchored at one cell.
 * It draws every part of its Table-4 template (the animation with the same id as the tile), so all instances of one
 * id share one clock exactly like the original's shared template sprites.
 */
struct StaticMapObject {
    int32_t anim_id{-1};        // Table-4 animation id of the tile placed by the map (dictionary index)
    uint16_t anchor_x{0};       // anchor cell column
    uint16_t anchor_y{0};       // anchor cell row
    bool is_food{false};        // a food pile: its stage and footprint are read from the live cells (anchor bytes of the current stage)
};

/**
 * @brief Object-list sprite (Block 1 entries with team 255: plants, clover, flowers). The original keeps these in the
 * y-sorted sprite list with sort key row*32+16 and its position at the cell centre.
 */
struct ObjectListSprite {
    int32_t anim_id{-1};
    int32_t px{0};              // cell centre x
    int32_t py{0};              // cell centre y
};

/**
 * @brief Union of the part rectangles of every frame of one animation, relative to its anchor (for culling).
 */
struct AnimBounds {
    bool valid{false};
    int32_t x0{0}, y0{0}, x1{0}, y1{0};
};

/**
 * @brief Render item representation for depth-sorted playfield drawing.
 */
struct RenderItem {
    int32_t sort_y{0};
    std::function<void(SDL_Renderer*, TextureCache&)> draw_func;
};

/**
 * @brief Overlay sprite drawn after the whole map sprite (fog of war included). In the original these are children of
 * the view container (selection markers, the hill marker, the click marker, score bubbles): they are drawn in
 * creation order, newest on top, and are not sorted by y.
 */
struct OverlayItem {
    int64_t created_ms{0};
    std::function<void()> draw;
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
              const ants::assets::AssetArchive& archive);
    void shutdown();

    /// The size of the picture that the window shows (SDL's logical size: it scales to the window with the largest scale that fits, centred, bars where the shapes differ;
    /// the scaling is SDL's, fractional unless the window is a multiple of the canvas, filtered "nearest"). The original's 640 x 480 until it is told otherwise.
    void set_canvas_size(int32_t w, int32_t h);
    /// SDL's logical size applied again (the window changed size: the application asks on every size event)
    void refit_canvas();
    int32_t canvas_w() const noexcept { return canvas_w_; }
    int32_t canvas_h() const noexcept { return canvas_h_; }

    /// Where the picture that the game draws sits in the canvas: its top left corner and its size. Everything the game draws (the HUD, the pages, the world) is in the
    /// picture's own coordinates and lands at that corner plus them; nothing is drawn beyond the picture (the clip is the picture) and the bars around it stay black. The whole
    /// canvas (no corner, no clip) until it is told otherwise, and again after `set_canvas_size`: a match of the classic layout in a bigger canvas is
    /// `CanvasLayout::centred(640, 480)`. The plate of the frame rate is the canvas's, not the picture's: it is drawn with the picture set to the whole canvas.
    void set_picture(const LayoutRect& picture);
    const LayoutRect& picture() const noexcept { return picture_; }

    /// The geometry of the picture (screen_layout.hpp): the map view, which the world is drawn into and clipped to, comes from it; the camera takes it too. The original's
    /// layout until it is told otherwise.
    void set_layout(const ScreenLayout& layout);
    const ScreenLayout& layout() const noexcept { return layout_; }
    /// The part of the map view that a map of map_w x map_h tiles covers with the camera where it is (view and map intersected, in picture pixels): the clip of the world. The whole view for
    /// a map that is as big as the view or bigger.
    LayoutRect map_view_rect(uint32_t map_w, uint32_t map_h) const;
    /// THE ZOOM (view_zoom.hpp). The camera's `zoom` is 1 (the original's picture: the world is drawn straight into the view, exactly as it always was) or another level, from 2 down to the
    /// map's limit. At another zoom the world is drawn at ONE TEXEL PER WORLD PIXEL into an offscreen target that covers the world that the view shows (and a texel more on each side for the
    /// filter), halved as often as the zoom needs (a zoom below 0.5: every halving is the exact average of 2 x 2 texels), and the last level is scaled into the lattice of whole screen pixels
    /// at the zoom (view_zoom.hpp Pass): exactly at 2 (nearest: every world pixel a crisp 2 x 2 square), 1 and 0.5 (the 2 x 2 average) and at every power of two, with the linear filter and
    /// the fractional position of the lattice in between (a renderer that places a rectangle at a fraction of a pixel; SDL's software renderer truncates it). Everything that is in screen space is
    /// drawn afterwards, on the canvas, at its own size: the hit point digits (Ctrl+L), the tile grid overlay, and (by the HUD and the application) the frame, the cursor and the rubber band.
    bool zoomed() const noexcept { return camera_.zoom != zoom::kNormal; }
    /// A test hook: draw the world through the offscreen target at the zoom 1 too (the result must be the picture that the direct path draws, pixel for pixel)
    void set_force_world_target(bool force) noexcept { force_world_target_ = force; }
    /// Do the levels between 1 and 2 use the linear filter (the default, zoom::kSmoothUpscale) or the nearest? 2 is always nearest, 1 always the exact copy; the zoom-out is always linear.
    void set_smooth_upscale(bool smooth) noexcept { smooth_upscale_ = smooth; }
    /// The most world pixels on one axis that the offscreen target can hold (the device's texture size less the margins of the pass), 0 when the renderer does not say: the limit of the zoom-out
    /// of a map that is bigger than a texture (view_zoom.hpp Fit::max_world)
    int32_t max_world_extent() const noexcept;
    /// A test hook: the device's texture size (texels on a side) as the pass sees it; 0 (the default) is what SDL says, which SDL's software renderer leaves unlimited (max_world_extent() is 0)
    void set_texture_side_for_test(int32_t side) noexcept { texture_side_override_ = side; }
    /// A test hook: the offscreen target cannot be made (what the fall-back to the zoom 1 picture is for); and a count of the world passes that went through the target (the zoom 1
    /// never does, unless it is forced: a test that compares the two paths can see that the pass really took the one it asks for)
    void set_fail_world_target(bool fail) noexcept { fail_world_target_ = fail; }
    /// A test hook for the other failure, the real one: SDL_CreateTexture of a target that is not there yet (or of another size) says no. The failure is then recorded (world_target_error), reported
    /// once, and lasts until retry_world_target() can make the target again.
    void set_fail_world_target_creation(bool fail) noexcept { fail_world_target_creation_ = fail; }
    uint64_t world_target_passes() const noexcept { return world_target_passes_; }
    /// THE OFFSCREEN TARGET CANNOT BE MADE (true while the failure lasts: SDL_CreateTexture / SDL_SetRenderTarget failed, or the test hook above). A zoomed world pass that finds this draws the zoom
    /// 1 picture instead, for that frame; what the player sees must then be what the game says, so the Application reads this every frame (enforce_zoom_limits) and takes the camera to the zoom 1
    /// and offers only that level until it is false again. The failure is reported once (to stderr), when it begins, not at every frame.
    bool world_target_failed() const noexcept { return fail_world_target_ || !world_target_error_.empty(); }
    /// The text of the failure of the target (SDL's own), empty when the last attempt worked; "" for the test hook
    const std::string& world_target_error() const noexcept { return world_target_error_; }
    /// While a real failure lasts the picture is the zoom 1 and no pass tries the target any more, so this is the way back: once every kWorldTargetRetryFrames frames it tries to make the largest
    /// target that a zoom needs (the zoom 0.5's), and clears the failure when that works. Nothing at all when there is no failure.
    void retry_world_target();
    static constexpr int32_t kWorldTargetRetryFrames = 120;
    /// What the pass keeps free of a texture's size (alignment and filter margins): max_world_extent() is the device's texture size less this
    static constexpr int32_t kPassMargin = 64;
    /// The lattice texture is the view and this many pixels more each way (the scaled picture and the fraction of its place)
    static constexpr int32_t kLatticeMargin = 12;

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
                      int32_t selected_base_team_id = -1,
                      float sub_tick_time = 0.0f);
    void end_frame();

    // Hooks used by the render-parity tests and offscreen harnesses
    // Draws only the static map layers (terrain + layer-2 objects) into the playfield clip; no ants, effects or fog.
    void render_map_layers(const ants::sim::Grid& grid, const ants::sim::WorldState* world = nullptr);
    // Draws all parts of one animation frame at screen position (sx, sy) in the original part order.
    void draw_animation_frame(const ants::assets::AnimationSubItem& sub, int32_t sx, int32_t sy,
                              bool mirrored = false, uint8_t colour = TEAM_NONE) {
        draw_frame_parts(sub, sx, sy, mirrored, colour);
    }

    // IRenderer Implementation
    void draw_sprite(uint32_t sprite_id, int32_t x, int32_t y, bool mirrored = false) override;
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool mirrored = false) override;
    void draw_sprite_region(uint32_t sprite_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) override;
    void set_origin(int32_t x, int32_t y) override { origin_ = LayoutPoint{x, y}; }
    const LayoutPoint& origin() const noexcept { return origin_; }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override;
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, ants::assets::ColorRGBA color) override;
    void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color) override;
    void draw_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) override;
    void draw_text_squeezed(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size, int32_t max_width) override;
    int32_t get_text_width(const std::string& text, FontSize size = FontSize::Px12) const override;
    /// The cell height of the size: exactly the original's (the line distance of a label), with or without a TrueType font
    int32_t get_text_height(FontSize size = FontSize::Px12) const override;
    /// The TrueType point size (at the 2x supersampling of draw_text) whose cell height in the 640 x 480 canvas is `cell_px`, for a font whose
    /// ascent + descent is `cell_ratio` em
    static int32_t ttf_point_size(int32_t cell_px, double cell_ratio) noexcept;
    void set_hud_team(uint8_t team_id) override { hud_team_id_ = team_id; }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void clear_clip_rect() override;
    /// Ctrl+L (Ants.exe 0x101b866): every ant is followed by its hit points as a white number at its sprite position. The original draws it with
    /// GDI TextOut and the stock SYSTEM_FIXED_FONT (the 8 x 15 raster "Fixedsys"), top left at the position, not antialiased.
    void set_show_hp(bool show) noexcept { show_hp_ = show; }
    /// The number in that fixed font: a cell is `kFixedCellW` x `kFixedCellH` pixels; only digits and '-' have glyphs (nothing else is drawn)
    void draw_fixed_text(const std::string& text, int32_t x, int32_t y, ants::assets::ColorRGBA color);
    static constexpr int32_t kFixedCellW = 8;
    static constexpr int32_t kFixedCellH = 15;
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override;

    // Camera Accessors
    ViewportCamera& camera() noexcept { return camera_; }

    // Helpers
    SDL_Renderer* get_sdl_renderer() const noexcept { return renderer_; }
    void request_screenshot(const std::string& path) { pending_screenshot_ = path; }
    bool save_screenshot(const std::string& path);
    /// IRenderer::render_world_image with the game's own drawing (render_world) into render-target textures: the level is set up as for a new match (set_level), the camera shows a window
    /// of the world, and the pixels are read back. A world that fits the device's texture size (and `max_side`, 0 = no limit of its own; at most 2048) is drawn in one go, a bigger one in
    /// tiles of 1536 pixels with 256 pixels of the neighbours around each (an object is drawn when its anchor is near the view, so that what crosses a seam is drawn by both
    /// tiles). Nothing of the live game changes: the level that the renderer shows, the camera, the layout, the picture and the clip are put back (the preview of a map does not touch a match).
    bool render_world_image(const ants::assets::LevelData& level, const ants::sim::WorldState& world, const ants::sim::Grid& grid, WorldImage& out, std::string* why = nullptr,
                            int32_t max_side = 0) override;
    static size_t get_anim_subitem_by_time(const ants::assets::AnimationSequence& seq, uint32_t now_ms);

    // Template animation clock (terrain, objects). It starts at set_level(); tests can pin it to a fixed ms value.
    void pin_animation_clock(uint32_t ms_since_map_load) noexcept {
        anim_clock_pin_ms_ = static_cast<int64_t>(ms_since_map_load);
        template_now_ms_ = ms_since_map_load;
        ++frame_counter_;
    }
    void unpin_animation_clock() noexcept { anim_clock_pin_ms_ = -1; }

    // Software Cursor & Transient Effects
    void spawn_transient_effect(const std::string& anim_name, int32_t px, int32_t py, bool is_screen_space = false);
    void update_transient_effects(float dt);
    void render_software_cursor(CursorType type, int32_t screen_x, int32_t screen_y, uint32_t anim_tick = 0);

private:
    void render_terrain_layer1(const ants::sim::Grid& grid);
    void render_terrain_layer2_structures(const ants::sim::Grid& grid, const ants::sim::WorldState* world = nullptr);
    void collect_flower_droppers(const ants::sim::WorldState& world);
    void collect_object_list_sprites();
    void render_fog_of_war(const ants::sim::WorldState& world);
    void collect_visual_effects(const ants::sim::WorldState& world);
    void collect_transient_sprites();
    void collect_selection_markers(const ants::sim::WorldState& world, int32_t selected_unit_id,
                                   const std::vector<uint32_t>& selected_unit_ids);
    void draw_overlay_queue();
    int64_t overlay_now_ms() const;
    void collect_score_bubbles(const ants::sim::WorldState& world);
    void collect_burn_overlays(const ants::sim::WorldState& world);
    void draw_score_number(int32_t amount, int32_t world_x, int32_t world_y);
    void collect_hill_brackets(const ants::sim::Grid& grid, int32_t selected_base_team_id);
    void draw_sorted_queue();
    void collect_ant_units(const ants::sim::WorldState& world);
    /// The sub-tick prediction of an ant's action clip: the frame that the real-time player of the original would show
    /// `sub_tick_ms_` after the last simulation tick, and the displacement of the frames that ended in between (positions change
    /// when a frame ends). Only action clips with pure clip displacement (attack, hit, blown ...) are predicted.
    struct AntClipPrediction {
        size_t frame{0};
        int32_t dx{0};
        int32_t dy{0};
    };
    AntClipPrediction predict_ant_clip(const ants::sim::AntSnapshot& ant, const ants::assets::AnimationSequence* seq) const;
    const ants::assets::AnimationSequence* ant_loco_sequence(const ants::sim::AntSnapshot& ant) const;
    void render_tile_grid(const ants::sim::Grid& grid, int32_t mouse_x, int32_t mouse_y);
    // Draws all parts of one animation frame, last stored part first (original order), at screen position (sx, sy).
    void draw_frame_parts(const ants::assets::AnimationSubItem& sub, int32_t sx, int32_t sy, bool mirrored = false,
                          uint8_t colour = TEAM_NONE);
    // Current frame of the shared template of Table-4 animation `anim_id` (all users of an id show the same frame).
    size_t template_frame_index(int32_t anim_id);
    void draw_template_screen(int32_t anim_id, int32_t sx, int32_t sy, uint8_t colour = TEAM_NONE);
    void draw_template_world(int32_t anim_id, int32_t world_x, int32_t world_y, uint8_t colour = TEAM_NONE);
    const AnimBounds& anim_bounds(int32_t anim_id);
    void draw_static_object(const StaticMapObject& obj, const ants::sim::Grid& grid, const ants::sim::WorldState* world);
    /// The second path of the layer-2 pass (FUN_01008089 0x10082a9 - 0x1008322): a cell of an object that is not its anchor, explored while its anchor is not, draws the
    /// object at the anchor from this cell's turn in the row-major pass (docs 5.57)
    void draw_object_from_body_cell(const ants::sim::Grid& grid, const ants::sim::WorldState& world, int32_t col, int32_t row);
    /// The Table-4 animation of a food stage tile
    int32_t food_stage_anim(uint16_t tile) const;
    void draw_single_ant(const ants::sim::AntSnapshot& ant);
    void draw_anthill_selection_brackets(int32_t cx, int32_t cy, uint32_t elapsed_ms = 0);
    /// Everything that set_level derives from a level (and the clock that starts with it): render_world_image swaps it out and back, so that drawing another level offscreen leaves the shown
    /// level as it was. A member that set_level writes belongs here.
    struct LevelState {
        uint32_t map_width{0};
        uint32_t map_height{0};
        std::vector<int32_t> tile_anim_id;
        std::vector<AnimBounds> anim_bounds;
        std::vector<uint32_t> anim_frame_stamp;
        std::vector<uint16_t> anim_frame_cache;
        uint32_t map_epoch_ms{0};
        uint32_t template_now_ms{0};
        bool level_set{false};
        std::array<SDL_Point, 4> hill_anchor_cell{};
        std::array<SDL_Point, 4> anthill_bases{};
        bool has_anthill_bases{false};
        std::vector<StaticMapObject> static_decor_objects;
        std::vector<int32_t> object_index_by_cell;
        std::vector<ObjectListSprite> object_list_sprites;
    };
    void swap_level_state(LevelState& other) noexcept;

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

    static constexpr size_t kFontSizes = 6;          // the enumerators of FontSize
    static size_t font_index(FontSize size) noexcept;
    TTF_Font* font_for(FontSize size) const noexcept { return fonts_[font_index(size)]; }
    std::array<TTF_Font*, kFontSizes> fonts_{};
    bool ttf_initialized_{false};
    uint32_t text_frame_counter_{0};
    std::unordered_map<TextCacheKey, CachedTextEntry, TextCacheKeyHash> text_cache_;
#endif

    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* rgba_texture_{nullptr};   // streaming texture for draw_rgba_image
    std::array<SDL_Texture*, 11> fixed_glyphs_{};   // draw_fixed_text: '0' .. '9' and '-', built on first use
    int32_t rgba_texture_w_{0};
    int32_t rgba_texture_h_{0};
    const ants::assets::AssetArchive* archive_{nullptr};
    std::unique_ptr<TextureCache> texture_cache_;
    ViewportCamera camera_;

    uint32_t map_width_{0};
    uint32_t map_height_{0};
    // Level tile dictionary index -> Table-4 animation id (the dictionary is positional: entry i is "." or animation i).
    std::vector<int32_t> tile_anim_id_;
    std::vector<AnimBounds> anim_bounds_;            // lazily filled, indexed by animation id
    std::vector<uint32_t> anim_frame_stamp_;         // per-animation cache of the current template frame
    std::vector<uint16_t> anim_frame_cache_;
    uint32_t frame_counter_{0};
    uint32_t map_epoch_ms_{0};                       // template clocks start when the map is loaded
    uint32_t template_now_ms_{0};                    // ms since map_epoch_ms_ for this frame
    int64_t anim_clock_pin_ms_{-1};                  // >= 0: fixed template clock (tests / harness)
    bool level_set_{false};
    int32_t anim_id_plus_{-1};
    int32_t anim_id_minus_{-1};
    int32_t anim_id_digit_[10]{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int32_t anim_id_fire_{-1};
    int32_t anim_id_lunchbox_{-1};
    int32_t anim_id_bomb_[4]{-1, -1, -1, -1};        // green, red, blue, black (remake player ids 0..3)
    int32_t anim_id_powerup_[6]{-1, -1, -1, -1, -1, -1}; // indexed by powerup_type 1..5
    int32_t anim_id_hill_[4]{-1, -1, -1, -1};        // GREENHILL, REDHILL, BLUEHILL, BLACKHILL
    std::array<SDL_Point, 4> hill_anchor_cell_{};    // layer-2 anchor cell of each hill (-1 if absent)
    std::vector<int32_t> object_index_by_cell_;      // anchor cell -> index into static_decor_objects_ (-1)
    std::array<SDL_Point, 4> anthill_bases_{};
    bool has_anthill_bases_{false};
    std::vector<StaticMapObject> static_decor_objects_; // Layer 2 interactive objects
    std::vector<ObjectListSprite> object_list_sprites_;  // plants / clover / flowers from the map object list
    std::vector<RenderItem> render_queue_;
    std::vector<OverlayItem> overlay_queue_;
    // Selection marker ("ears") of each selected ant: its own looping clock, restarted when it is created (selection)
    // and whenever the ant's health changes (Ants.exe FUN_01010373 re-creates it after every damage and heal).
    struct EarsState { uint16_t hp{0}; int64_t start_ms{0}; };
    std::unordered_map<uint32_t, EarsState> ears_state_;
    int32_t hill_marker_team_{-1};
    int64_t hill_marker_start_ms_{0};
    std::string pending_screenshot_;
    uint8_t hud_team_id_{0};
    bool show_hp_{false};
    ScreenLayout layout_{ScreenLayout::classic()};
    int32_t canvas_w_{CANVAS_WIDTH};
    int32_t canvas_h_{CANVAS_HEIGHT};
    LayoutRect picture_{0, 0, CANVAS_WIDTH, CANVAS_HEIGHT};
    bool picture_inset_{false};            // the picture is smaller than the canvas: its corner is added to every position and it is the clip
    LayoutPoint origin_{};                 // set_origin: what a window of the original's own screen is moved by (nothing, outside the HUD's pages and dialogs)
    /// A rectangle of the picture's own coordinates as SDL gets it (the picture's corner added, and the origin of a window)
    SDL_Rect placed(int32_t x, int32_t y, int32_t w, int32_t h) const noexcept { return SDL_Rect{x + picture_.x + origin_.x, y + picture_.y + origin_.y, w, h}; }
    /// The width that a text of `w` pixels is copied into while draw_text_squeezed draws it (its own width when no squeeze is asked for or it is narrower)
    int32_t squeezed(int32_t w) const noexcept { return squeeze_width_ > 0 && w > squeeze_width_ ? squeeze_width_ : w; }
    int32_t squeeze_width_{0};
    void restore_clip();

    // The world pass (see zoomed()). While `in_world_target_` the renderer draws into the target: the camera is a camera of zoom 1 whose view is the target (so every position of the world
    // code is a texel), the picture is the whole target, there is no origin, and world_view() is the target. begin_world_target() sets this up (the plan: view_zoom.hpp Pass), end_world_target()
    // puts everything back, halves the target as often as the plan says and puts the last level into the view in whole screen pixels.
    LayoutRect world_view() const noexcept { return in_world_target_ ? target_view_ : layout_.view(); }
    bool begin_world_target(int64_t map_w_px, int64_t map_h_px);
    void end_world_target();
    struct PassTexture {
        SDL_Texture* texture{nullptr};
        int32_t w{0};
        int32_t h{0};
    };
    bool ensure_texture(PassTexture& t, int32_t tw, int32_t th, bool report);   // a target texture of this size (made if there is none or it is another size); false, with world_target_error_ set (and reported when `report`), when it cannot be made
    void destroy_texture(PassTexture& t) noexcept;
    void note_world_target_error(const char* what);     // the error of a failure: reported (once per failure) and kept in world_target_error_
    std::string world_target_error_;
    int32_t world_target_retry_frames_{0};
    bool in_world_target_{false};
    bool force_world_target_{false};
    bool fail_world_target_{false};
    bool fail_world_target_creation_{false};
    bool smooth_upscale_{zoom::kSmoothUpscale};
    int32_t texture_side_override_{0};        // the test hook of set_texture_side_for_test()
    bool software_{false};                    // SDL's software renderer: it truncates the fractional rectangle of a copy (and cuts a scaled copy that a clip cuts with a rounding of its source)
    uint64_t world_target_passes_{0};
    LayoutRect target_view_{};
    std::vector<PassTexture> pass_levels_;    // [0] the world at one texel per world pixel, [k] the same halved k times
    PassTexture lattice_;                     // the last level scaled into whole screen pixels (the view and a few pixels more), the view is cut out of it
    zoom::Pass plan_;                         // the plan of the pass in progress
    struct TargetPass {
        ViewportCamera camera;                 // the camera that was replaced by the pass's own
        LayoutRect picture{};
        bool picture_inset{false};
        LayoutPoint origin{};
    } pass_;
    /// The hit point digits of the pass: at a zoom they are drawn after the copy, on the canvas, at one size (a world position and the text)
    struct DeferredDigits {
        std::string text;
        int32_t wx{0};
        int32_t wy{0};
    };
    std::vector<DeferredDigits> deferred_digits_;
    void draw_deferred_digits();
    std::vector<TransientEffect> transient_effects_{};
    uint32_t sub_tick_ms_{0};
};

} // namespace ants::app
