#pragma once

// The windows of the match in the new look (docs/VIEW_AND_HUD.md, "Windows"): which windows a HUD art set can draw, the pictures that the game cuts out of the
// original's quick help, and the pieces of the original's own corner that the Classic look needs for the Fullscreen button. Look only: nothing here reaches the simulation, the rules or a replay.

#include <array>
#include <cstdint>
#include <vector>

#include "ants_app/hud_art.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app::window_art {

/// The windows that the set draws in the new look: every piece of a window must be in the manifest and must fit the place of the original's piece it replaces, or the window is the original's
struct Pieces {
    bool dialogs{false};        // the quit question, the three alliance questions, "Get ready to play!"
    bool quick_help{false};
    bool countdown{false};      // the countdown card of a room
    bool fullscreen{false};     // the Fullscreen button of the HUD
};

/// What `art` can draw. The footprints of the pieces are compared with the original's animations in `assets` (a piece may be larger by the cast shadow, never smaller, never moved)
Pieces check(const hud_art::Art& art, const assets::AssetArchive& assets);

/// True when the set has the six pictures of the Fullscreen button (the three states, and the three that leave fullscreen)
bool has_fullscreen_button(const hud_art::Art& art) noexcept;

/// True when the set has the pieces of the settings menu's drawn panel (game_menu_panel.hpp): the panel, the wells, the small steel buttons in their three states, the On / Off switches and the slider's thumb.
/// A set without them leaves the panel to plain rectangles
bool has_menu_panel(const hud_art::Art& art);

/// True when the set has the panel and every piece of the new results screen (scorecard_layout.hpp kSprites), each of the size that the layout counts on: a set that lacks one gives the original's page
bool has_results_page(const hud_art::Art& art);

/// True when the art's piece keeps the original's place (`original`): the same corner within a pixel, and the same far edges within a pixel less or `shadow` pixels more (the cast shadow)
bool fits(const LayoutRect& art_piece, const LayoutRect& original, int32_t shadow = 4) noexcept;

/// The rectangles of the windows, in the original's own numbers (the renderer's origin puts them where the picture wants them)
inline constexpr LayoutRect kDialogRect{ScreenLayout::kDialogX, ScreenLayout::kDialogY, ScreenLayout::kDialogW, ScreenLayout::kDialogH};
inline constexpr LayoutRect kQuickHelpRect{0, 0, 640, 480};

/// The sentences of the dialogs: where they are (window numbers: the dialog's own numbers plus its corner), their size (em in tenths of a pixel) and line pitch
struct SentenceStyle {
    int32_t em_tenths;
    int32_t pitch;
    int32_t width;              // the box that words are wrapped in
    int32_t first_baseline;     // from the dialog's top
};
inline constexpr SentenceStyle kQuestion{200, 26, 292, 96};       // the quit question
inline constexpr SentenceStyle kAlliance{200, 26, 292, 30};       // the three alliance questions
inline constexpr SentenceStyle kGetReady{260, 33, 240, 38};       // "Get ready to play!  You are the ... Ants."
inline constexpr SentenceStyle kWaiting{180, 0, 240, 208};        // "Waiting for others..."
inline constexpr assets::ColorRGBA kInk{31, 23, 51, 255};         // the dialogs' own letters (COLORREF 0x33171F)

/// A sentence centred on `centre_x`, wrapped in the style's box, drawn in the windows' face (IRenderer::dialog_text_ready must be true); the dialog's top is at `top`. Returns the lines.
std::vector<std::string> draw_sentence(IRenderer& renderer, const std::string& text, const SentenceStyle& style, int32_t centre_x, int32_t top);

/// The pictures of the original's quick help that the new window keeps (the ants, the power-ups, the bombs, the fire and the four hills): cut out of the original's two bitmaps qh1 and qh2 once, with
/// the ground of their boxes and the clay see-through, and laid over the new wells. No pixel of them is in the HUD art's files.
class KeptPictures {
public:
    /// False when the archive has not the two bitmaps
    bool build(const assets::AssetArchive& assets);
    bool ready() const noexcept { return !pictures_.empty(); }
    void draw(IRenderer& renderer) const;
    struct Picture {
        const char* name;
        int32_t x{0}, y{0}, w{0}, h{0};
        std::vector<uint8_t> rgba;
    };
    const std::vector<Picture>& pictures() const noexcept { return pictures_; }
private:
    std::vector<Picture> pictures_;
};

/// The Classic look's corner with the Fullscreen button: the original's three buttons stand 8 pixels further left, the plate under their old places is laid again from the plate's own end columns, and
/// the new button is made of the original's Quit button (its two ends) with a drawn picture on it. Everything is cut from the original's own sprites, once.
class ClassicCorner {
public:
    static constexpr int32_t kShift = 8;                              // how far the three buttons moved left
    static constexpr int32_t kButtonW = 20;                           // the new button's picture
    static constexpr int32_t kButtonH = 23;
    static constexpr int32_t kRight = 619;                            // the new button's left edge when the picture is 640 wide (it follows the right edge)
    static constexpr int32_t kPlateLeft = 469;                        // the plate's left edge at 640 (the same distance from the right edge)
    /// False when the archive lacks one of the sprites
    bool build(const assets::AssetArchive& assets);
    bool ready() const noexcept { return ready_; }
    /// The plate that covers the old places of the three buttons, to be drawn at (layout.right(kPlateLeft), 7) for the picture's layout
    int32_t plate_w() const noexcept { return plate_w_; }
    const std::vector<uint8_t>& plate() const noexcept { return plate_; }
    /// The new button: state 0 up, 1 hover, 2 pressed; `leave` the picture of leaving fullscreen
    const std::vector<uint8_t>& button(int state, bool leave) const noexcept { return buttons_[(leave ? 3 : 0) + static_cast<size_t>(state)]; }
private:
    bool ready_{false};
    int32_t plate_w_{0};
    std::vector<uint8_t> plate_;
    std::array<std::vector<uint8_t>, 6> buttons_;
};

}  // namespace ants::app::window_art
