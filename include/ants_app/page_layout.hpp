#pragma once

// The loading screen and the quick help at the start, in the original's own picture (640 x 480) and recomposed for the 16:9 picture of 960 x 540 (widescreen work, after the setup screen;
// the owner approved the mock-ups: "Approved").
//
// LOADING SCREEN (the animation `antslogo`: frame pieces, 33 clay tiles, logo.bmp, credits.bmp and strip.bmp; the program shows it from FUN_010175ad, 0x10175ad). The remake fills the page with
// flat orange (the clay tiles are left out: "authentic solid orange"), draws the frame pieces, the logo, the credits, the strip that masks the credits' subtitle line, and the progress bar.
//   * The ORIGINAL'S ORDER: the original draws an animation's parts in the list's order from its END (Sprite::DrawAt, 0x102b8d7: the part list is walked from the node that was added last, 0x1029924
//     and 0x1029987, and the loader adds the parts in stored order, 0x102a977 with 0x1029a5a), so the LAST stored part is drawn first. `antslogo` stores the strip, the credits and the logo first, then the
//     frame pieces: the frame pieces are drawn before them and the strip is on top. The remake used to draw the frame pieces in the STORED order (145 pixels of the overlaps of the left and right
//     strips came out differently); they are drawn in the original's order now, in the classic picture. (The wide picture's frame is the shared wide frame, whose pieces do not overlap.)
//   * Wide: the wide frame at the canvas's edge on the flat clay (the bitmaps carry flat clay, so a flat field leaves no rectangle visible: a speckled field would show them as rectangles), and the
//     page's composition (logo, credits, strip, bar) moved as ONE group by (+160, +30): the group is centred, nothing is repeated.
//
// QUICK HELP AT THE START (the animation `qh_screen`: frame pieces, qh1.bmp (the left column: How to Win, How to Move, Game Play) and qh2.bmp (the title, the Power Ups table, the steps), which abut
// at x 266 | 267; the button START! is the animations qh_start1 / qh_start2 / qh_start3).
//   * Wide: the page cannot widen (its title and its subtitle line cross the join of the two bitmaps, and every box is a baked block of text), so it is centred in the wide frame ((+160, +30): the two
//     bitmaps still abut) on the flat clay, and START! is anchored to the bottom right corner (+320, +60), where it keeps the relation that the original has to the setup screen's START (3 px right and
//     2 px up: (529, 437) here and (526, 439) there, (849, 497) and (846, 499) in the wide pictures), so a click on START! still lands on the next screen's START.
// The in-match quick help (HUD::render_quick_help) is a window over the map view and is not touched.
//
// All rectangles are half-open (LayoutRect). The geometry is plain numbers; the drawing needs a renderer.

#include <cstdint>
#include <string>

#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_app/wide_page.hpp"
#include "ants_assets/asset_archive.hpp"

namespace ants::app {

/// The loading screen: where its three bitmaps and its progress bar are, in the classic picture and in the wide one (the same group, moved by (160, 30))
struct LoadingLayout {
    /// The ticks of the loading state that fill the bar (the state ends at 30 ticks of 50 ms: the bar is full at 25)
    static constexpr int32_t kBarTicks = 25;
    LayoutRect logo;             // logo.bmp ("Microsoft ants!")
    LayoutRect credits;          // credits.bmp
    LayoutRect strip;            // strip.bmp: the orange plate over the credits' subtitle line
    LayoutRect bar;              // the progress bar's slot: the fill grows from its left edge

    static const LoadingLayout& classic() noexcept;
    static const LoadingLayout& wide() noexcept;
    static const LoadingLayout& of(bool wide) noexcept { return wide ? LoadingLayout::wide() : LoadingLayout::classic(); }
    /// The width of the bar's fill after `ticks` ticks of the loading state
    int32_t bar_fill(int32_t ticks) const noexcept;
};

/// The fill of the progress bar (the original's dark purple, #1F1733)
inline constexpr ants::assets::ColorRGBA kLoadingBarColour{31, 23, 51, 255};

/// Draws the loading screen after `ticks` ticks of the loading state: the classic page when `wide` is false (the original's own picture, 640 x 480), the wide page otherwise
void draw_loading_screen(IRenderer& renderer, const ants::assets::AssetArchive& archive, bool wide, int32_t ticks);

/// THE CATCH-UP SCREEN of a way back (docs/NETWORK_PORT.md): while a machine is given the match from the server's log it shows the loading screen's picture instead of the match, the bar filled to
/// the percent, "Catching up 45%" on the orange strip and "Esc leaves the match" at the strip's right end in small type.
std::string catch_up_text(int32_t percent);
inline constexpr const char* kCatchUpHint = "Esc leaves the match";
/// Where the words stand: the percent's text is centred in the strip, the hint ends 12 px before the strip's right end, both in the strip's middle line
struct CatchUpLayout {
    LayoutRect strip;
    int32_t text_x{0};
    int32_t text_y{0};
    int32_t hint_x{0};
    int32_t hint_y{0};
    LayoutRect bar_fill;
};
CatchUpLayout catch_up_layout(bool wide, int32_t percent, int32_t text_w, int32_t hint_w);
void draw_catch_up_screen(IRenderer& renderer, const ants::assets::AssetArchive& archive, bool wide, int32_t percent);

/// What the button START! shows
enum class QuickHelpStart : uint8_t { Up, Hover, Pressed };

/// The quick help: where its two columns and its button are
struct QuickHelpLayout {
    LayoutRect left;             // qh1.bmp
    LayoutRect right;            // qh2.bmp
    LayoutRect start;            // START! at rest and hovered (qh_start1, qh_start2); the hit test is the rectangle of the picture that shows
    LayoutRect start_pressed;    // START! pressed (qh_start3)

    static const QuickHelpLayout& classic() noexcept;
    static const QuickHelpLayout& wide() noexcept;
    static const QuickHelpLayout& of(bool wide) noexcept { return wide ? QuickHelpLayout::wide() : QuickHelpLayout::classic(); }
};

/// Draws the quick help at the start with the button in the given state: the classic page (the animation qh_screen and the button's, at the original's own numbers) when `wide` is false, the wide page
/// otherwise. The pointer is the application's.
void draw_quick_help_screen(IRenderer& renderer, const ants::assets::AssetArchive& archive, bool wide, QuickHelpStart start);

}  // namespace ants::app
