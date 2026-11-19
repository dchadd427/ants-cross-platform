#pragma once

// The HUD art: the new pictures of the match screen's frame (docs/VIEW_AND_HUD.md, "HUD art"), as sprite sheets that tools/hud_art makes and assets/hud_art/ holds. The original's art stays: the
// setting "HUD art" chooses between this set (New, the default) and the original's own pieces (Classic, drawn exactly as before). Look only: nothing here reaches the simulation, the rules or a
// replay. No SDL in this header: the manifest, the layout of the pieces at every shape of the picture and the choice of the sheet are plain numbers, so that a test can run them.
//
// The files: `hud_art.txt` (the manifest, one record a line) and a PNG sheet for each scale, 1x, 2x and 3x (the same drawing, drawn again at 1, 2 and 3 pixels for each canvas pixel). All numbers
// of the manifest are canvas pixels, in the 960 x 540 layout (the sheet's own pixels are that times the scale). Its records:
//     ants-hud-art 1
//     canvas 960 540
//     sheetsize <w> <h>                                 the 1x sheet's size (the others are that times the scale)
//     derive2x <rows>                                   optional: the first <rows> rows of the 2x sheet are not in its file: sheet_pixels makes them from the same rows of the 3x sheet (derive_two_thirds)
//     sheet <scale> <png file>
//     sprite <name> <w> <h> <sheet x> <sheet y> <x> <y> <anchor x> <anchor y> [<axis> <start> <tile from> <tile to> <end>]
//     anim <name> [cursor]                              the original's animation that this set replaces
//     frame <ms> <sprite> <x> <y> [<sprite> <x> <y> ...]  one frame of it: the original's time, and the pieces that make the frame
// A sprite's anchor says how its place (x, y) follows a picture of another size: `right` moves x by (width - 960), `bottom` moves y by (height - 540), `left`, `top` keep it, `stretch` (with a
// slice) makes the piece itself longer: along `axis` the first `start` pixels are kept, the run [tile from, tile to) is repeated (the last copy cut) and the last `end` pixels are kept.
// An animation marked `cursor` is one of the original's pointers (c_normal, c_attack, CUR_N ...): the place of each of its pieces is where the sprite's upper left corner goes from the click point
// (the original's own offset for that frame), the anchors do not apply, and the piece has the size of the original's sprite. cursor_fits() says whether a replacement keeps all of that.
// The windows over the match (docs/VIEW_AND_HUD.md, "Windows") use the same files: the panel is nine sprites (panel_draws), a window's fixed pieces are one animation whose parts stand in the window's own
// numbers (animation_draws_at moves them to where a window is), and window_art.hpp says which windows a set can draw.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ants_app/png_decode.hpp"
#include "ants_app/screen_layout.hpp"

namespace ants::app {
class IRenderer;
}

namespace ants::app::hud_art {

enum class AnchorX : uint8_t { Left, Right, Stretch };
enum class AnchorY : uint8_t { Top, Bottom, Stretch };
enum class Axis : uint8_t { None, X, Y };

struct Slice {
    Axis axis{Axis::None};
    int32_t start{0};
    int32_t tile_from{0};
    int32_t tile_to{0};
    int32_t end{0};
};

struct Sprite {
    std::string name;
    int32_t w{0};
    int32_t h{0};
    int32_t sheet_x{0};         // where it is in the 1x sheet (the other sheets: times the scale)
    int32_t sheet_y{0};
    int32_t x{0};               // its place in the 960 x 540 layout
    int32_t y{0};
    AnchorX anchor_x{AnchorX::Left};
    AnchorY anchor_y{AnchorY::Top};
    Slice slice{};
};

struct Part {
    int32_t sprite{-1};         // index into sprites()
    int32_t x{0};               // at 960 x 540
    int32_t y{0};
};

struct Frame {
    int32_t ms{0};
    std::vector<Part> parts;    // in the order they are drawn
};

struct Animation {
    std::string name;
    bool cursor{false};         // a pointer: its pieces are placed from the click point (cursor_draws)
    std::vector<Frame> frames;
};

/// One frame of one of the original's cursors as the archive holds it: its time, the offset of its sprite's upper left corner from the click point and the sprite's size
struct OriginalCursorFrame {
    int32_t ms{0};
    int32_t dx{0};
    int32_t dy{0};
    int32_t w{0};
    int32_t h{0};
};

/// One copy of a sprite: the rectangle `src` of the sprite (canvas pixels of the sprite itself) fills the rectangle `dst` of the picture
struct Draw {
    int32_t sprite{-1};
    LayoutRect dst{};
    LayoutRect src{};
};

class Art {
public:
    static constexpr std::array<int32_t, 3> kScales = {1, 2, 3};
    static constexpr int32_t kReferenceWidth = 960;
    static constexpr int32_t kReferenceHeight = 540;

    /// Reads the manifest text. False (what() says why) when it is not one, names a sprite twice, an animation that uses a sprite that is not there, or lacks a piece of the frame.
    bool parse(const std::string& manifest);
    /// Reads `<folder>/hud_art.txt` and looks at the header of each sheet (it must be there, 8-bit RGBA, the manifest's size times its scale); the sheets are decoded when a texture needs them (sheet_pixels)
    bool load(const std::string& folder);
    const std::string& what() const noexcept { return what_; }
    bool loaded() const noexcept { return loaded_; }
    const std::string& folder() const noexcept { return folder_; }

    const std::vector<Sprite>& sprites() const noexcept { return sprites_; }
    const std::vector<Animation>& animations() const noexcept { return animations_; }
    const Sprite* sprite_at(int32_t index) const noexcept { return index >= 0 && static_cast<size_t>(index) < sprites_.size() ? &sprites_[static_cast<size_t>(index)] : nullptr; }
    int32_t find_sprite(const std::string& name) const noexcept;
    const Animation* find_animation(const std::string& name) const noexcept;
    const std::string& sheet_file(int32_t scale) const;

    /// The pixels of the sheet of a scale, read and decoded from its file (the first `derive2x` rows of the 2x sheet: made from the 3x sheet, of which only those rows are decoded). False (what() says why)
    /// when a file is missing or damaged, or is not the size that the manifest's sprites need
    bool sheet_pixels(int32_t scale, DecodedImage& out) const;
    /// How many rows (canvas pixels, from the top) of the 2x sheet the game makes from the 3x sheet: the lettering of the windows, whose 2x pictures would only repeat the 3x ones (0: none)
    int32_t derived_rows() const noexcept { return derived_rows_; }
    /// The size of a sheet in canvas pixels (the manifest's sprites end inside it)
    int32_t sheet_width() const noexcept { return sheet_w_; }
    int32_t sheet_height() const noexcept { return sheet_h_; }

    /// A number that changes with every manifest that was read (a renderer that keeps textures of a set knows it by this, not by the address)
    uint64_t generation() const noexcept { return generation_; }

    /// The sheet to draw from when the output shows a canvas pixel as `output_scale` device pixels: the nearest scale at or above it (a window smaller than the canvas, or exactly 1: 1x; 1.4: 2x;
    /// 2.5: 3x; more than 3: 3x). A sheet that has more pixels than the output is scaled down by the graphics card, one that is the same size is copied as it is.
    static int32_t scale_for(double output_scale) noexcept;

    /// The pieces of the frame, by what they are called (-1 when the manifest has none): the frame's eight pieces are required by parse()
    int32_t top_bar{-1};
    int32_t left_strip{-1};
    int32_t bottom_strip{-1};
    int32_t score_window{-1};          // not a piece of the frame: drawn at a score box while the team plays
    int32_t lower_panel{-1};
    int32_t panel_top{-1};
    int32_t minimap_frame{-1};
    int32_t card{-1};
    int32_t status_box{-1};

private:
    std::string what_;
    std::string folder_;
    bool loaded_{false};
    uint64_t generation_{0};
    int32_t sheet_w_{0};
    int32_t sheet_h_{0};
    int32_t derived_rows_{0};
    std::vector<Sprite> sprites_;
    std::vector<Animation> animations_;
    std::unordered_map<std::string, int32_t> sprite_index_;
    std::unordered_map<std::string, size_t> animation_index_;
    std::array<std::string, 3> sheets_;
};

/// Where a sprite stands in a picture of `layout`'s size: its place at 960 x 540 moved by the anchors
LayoutPoint place_of(const Sprite& sprite, int32_t x, int32_t y, const ScreenLayout& layout) noexcept;

/// The copies that make a sprite of the sheet into `length` canvas pixels along its slice axis (the repeated run), at the place given: for the frame's long pieces. A sprite without a slice
/// is one copy. A piece shorter than start + end draws the start, then the end over it.
std::vector<Draw> sliced_draws(const Art& art, int32_t sprite, int32_t x, int32_t y, int32_t length);

/// The pieces of the frame in a picture of `layout`'s size, in the order they are drawn: the top bar, the left strip, the bottom strip, the lower panel, the panel's top, the minimap's
/// frame, the card and the status box. Together they cover the whole picture but the map view.
std::vector<Draw> frame_draws(const Art& art, const ScreenLayout& layout);

/// The copies of one frame of an animation of the original that this set replaces (empty when the set has no such animation or frame)
std::vector<Draw> animation_draws(const Art& art, const std::string& name, size_t frame, const ScreenLayout& layout);

/// The same, for an animation that the caller has found already
std::vector<Draw> frame_part_draws(const Art& art, const Frame& frame, const ScreenLayout& layout);

/// The copies that draw one frame of a cursor animation with the click point at (x, y): the corner of each piece is at the click point plus the piece's own offset
std::vector<Draw> cursor_draws(const Art& art, const Animation& cursor, size_t frame, int32_t x, int32_t y);

/// Which frame an animation shows `ms` into its loop (the frame times of the manifest, which are the original's): the frames follow one another, each for its time, and the loop starts again. The
/// original's cursors do the same with their own times, so a replacement that keeps the times is on the same frame at every moment.
size_t frame_at(const Animation& animation, uint32_t ms) noexcept;

/// True when `art` replaces a cursor without moving its click point: it is marked `cursor`, has the original's number of frames, and in each the original's time and one piece of the original's size at the
/// original's offset from the click point. A replacement that does not fit is not drawn (the original's cursor is).
bool cursor_fits(const Art& art, const Animation& art_cursor, const std::vector<OriginalCursorFrame>& original) noexcept;

/// The copies of one frame of an animation with every part moved by (dx, dy), anchors ignored: the pieces of a window that stands where the caller puts it (the countdown card's numbers are
/// the card's own, and the card is in the middle of the map view)
std::vector<Draw> animation_draws_at(const Art& art, const std::string& name, size_t frame, int32_t dx, int32_t dy);

/// The pieces of a window's panel (docs/VIEW_AND_HUD.md, "Windows"): the nine sprites panel_tl, panel_t, panel_tr, panel_l, panel_c, panel_r, panel_bl, panel_b and panel_br make a panel of any size
/// at or above twice the corner's size. The centre tile (opaque: the clay with its tooth) is repeated over the whole rectangle from its top left, the last copy cut; the frame's pieces lie over
/// it, as overlays: the edges repeated along their sides, the last copy cut, and the four corners. Empty when the set has no panel (has_panel) or the rectangle is smaller than the two corners.
std::vector<Draw> panel_draws(const Art& art, const LayoutRect& rect);

/// The 2x picture that a 3x picture gives (`derive2x`): every 3 x 3 pixels give 2 x 2, an output pixel covering 3 / 2 source pixels along each axis (the source pixels are weighted 2, 1, 0 for the first
/// output pixel of a pair and 0, 1, 2 for the second), averaged over premultiplied color, in whole numbers rounded half up (tools/hud_art/derive.py does the same). The width and height of `three`
/// must be multiples of 3. False (and `two` empty) when they are not
bool derive_two_thirds(const DecodedImage& three, DecodedImage& two);

/// True when the manifest has all nine pieces of the panel
bool has_panel(const Art& art) noexcept;

/// The rectangle that the parts of one frame of an animation cover together, at the places the manifest gives (anchors ignored); an empty rectangle when the animation or the frame is missing
LayoutRect animation_bounds(const Art& art, const std::string& name, size_t frame);

/// Draws copies through the renderer
void draw_all(IRenderer& renderer, const Art& art, const std::vector<Draw>& draws);

/// Draws the frame of the picture, or one frame of an animation; true when the set had it
void draw_frame(IRenderer& renderer, const Art& art, const ScreenLayout& layout);
bool draw_animation(IRenderer& renderer, const Art& art, const std::string& name, size_t frame, const ScreenLayout& layout);

}  // namespace ants::app::hud_art
