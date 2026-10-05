#include "ants_app/page_layout.hpp"

#include <algorithm>

#include "ants_app/ui_anim.hpp"

namespace ants::app {

namespace {

// The ids (indices of ants.chd) of what `antslogo` stores that the remake does not draw as a frame piece: the two clay tiles (dclay48, dclay96: the remake fills flat orange), strip.bmp, credits.bmp
// and logo.bmp (drawn on their own, after the frame)
constexpr uint32_t kClayTile48 = 0;
constexpr uint32_t kClayTile96 = 2;
constexpr uint32_t kStripSprite = 160;
constexpr uint32_t kCreditsSprite = 161;
constexpr uint32_t kLogoSprite = 162;

constexpr LoadingLayout kLoadingClassic{LayoutRect{25, 23, 593, 270}, LayoutRect{32, 299, 573, 172}, LayoutRect{40, 315, 478, 31}, LayoutRect{229, 448, 234, 8}};
// the page's composition, moved as one group by (160, 30): the wide page's logo is at (185, 53), the credits at (192, 329), the strip at (200, 345), the bar at (389, 478)
constexpr LoadingLayout kLoadingWide{kLoadingClassic.logo.moved(160, 30), kLoadingClassic.credits.moved(160, 30), kLoadingClassic.strip.moved(160, 30), kLoadingClassic.bar.moved(160, 30)};

constexpr QuickHelpLayout kQuickHelpClassic{LayoutRect{10, 9, 257, 461}, LayoutRect{267, 10, 362, 463}, LayoutRect{529, 437, 98, 27}, LayoutRect{528, 438, 97, 24}};
// the two columns are centred (+160, +30: they still abut), START! is anchored to the bottom right corner (+320, +60)
constexpr QuickHelpLayout kQuickHelpWide{kQuickHelpClassic.left.moved(160, 30), kQuickHelpClassic.right.moved(160, 30), kQuickHelpClassic.start.moved(kWidePageDx, kWidePageDy),
                                         kQuickHelpClassic.start_pressed.moved(kWidePageDx, kWidePageDy)};

const char* start_animation(QuickHelpStart start) {
    switch (start) {
        case QuickHelpStart::Pressed: return "qh_start3";
        case QuickHelpStart::Hover: return "qh_start2";
        case QuickHelpStart::Up: break;
    }
    return "qh_start1";
}

}  // namespace

const LoadingLayout& LoadingLayout::classic() noexcept { return kLoadingClassic; }
const LoadingLayout& LoadingLayout::wide() noexcept { return kLoadingWide; }

int32_t LoadingLayout::bar_fill(int32_t ticks) const noexcept {
    return std::clamp((std::max(ticks, 0) * bar.w) / kBarTicks, 0, bar.w);
}

void draw_loading_screen(IRenderer& renderer, const ants::assets::AssetArchive& archive, bool wide, int32_t ticks) {
    const LoadingLayout& l = LoadingLayout::of(wide);
    if (wide) {
        // the wide frame at the canvas's edge on flat clay; the group of the page (logo, credits, strip, bar) is moved as one
        draw_wide_background(renderer, archive, PageClay::Flat);
    } else {
        // 1. the page (the original's 640 x 480 screen) filled with the authentic solid orange #DB4B13
        renderer.fill_rect(0, 0, ScreenLayout::kClassicWidth, ScreenLayout::kClassicHeight, kFlatClay);

        // 2. the frame pieces of antslogo (without the clay tiles and the three bitmaps), in the ORIGINAL's order: the last stored part first (Sprite::DrawAt, 0x102b8d7)
        const auto* seq = archive.find_animation("antslogo");
        if (seq != nullptr && !seq->subitems.empty()) {
            const auto& frames = seq->subitems[0].frames;
            for (size_t k = frames.size(); k-- > 0;) {
                const uint32_t sprite = frames[k].sprite_index;
                if (sprite == kClayTile48 || sprite == kClayTile96 || sprite == kStripSprite || sprite == kCreditsSprite || sprite == kLogoSprite) continue;
                renderer.draw_sprite(sprite, frames[k].dx, frames[k].dy);
            }
        }
    }

    // 3. logo.bmp, 4. credits.bmp, 5. strip.bmp on top of the credits (it masks their subtitle line)
    renderer.draw_named_sprite("logo.bmp", l.logo.x, l.logo.y);
    renderer.draw_named_sprite("credits.bmp", l.credits.x, l.credits.y);
    renderer.draw_named_sprite("strip.bmp", l.strip.x, l.strip.y);

    // 6. the progress bar inside its slot, in the original's dark purple
    const int32_t fill_w = l.bar_fill(ticks);
    if (fill_w > 0) renderer.fill_rect(l.bar.x, l.bar.y, fill_w, l.bar.h, kLoadingBarColour);
}

std::string catch_up_text(int32_t percent) { return "Catching up " + std::to_string(std::clamp(percent, 0, 100)) + "%"; }

CatchUpLayout catch_up_layout(bool wide, int32_t percent, int32_t text_w, int32_t hint_w) {
    const LoadingLayout& l = LoadingLayout::of(wide);
    CatchUpLayout out;
    out.strip = l.strip;
    out.text_x = l.strip.x + (l.strip.w - text_w) / 2;
    out.text_y = l.strip.y + (l.strip.h - font_cell_height(FontSize::Px24)) / 2;
    out.hint_x = l.strip.right() - 12 - hint_w;
    out.hint_y = l.strip.y + (l.strip.h - font_cell_height(FontSize::Px14)) / 2;
    out.bar_fill = LayoutRect{l.bar.x, l.bar.y, std::clamp(percent, 0, 100) * l.bar.w / 100, l.bar.h};
    return out;
}

void draw_catch_up_screen(IRenderer& renderer, const ants::assets::AssetArchive& archive, bool wide, int32_t percent) {
    draw_loading_screen(renderer, archive, wide, 0);                              // (the picture; the bar is filled below, to the percent)
    const std::string text = catch_up_text(percent);
    const std::string hint = kCatchUpHint;
    const CatchUpLayout where = catch_up_layout(wide, percent, renderer.get_text_width(text, FontSize::Px24), renderer.get_text_width(hint, FontSize::Px14));
    if (where.bar_fill.w > 0) renderer.fill_rect(where.bar_fill.x, where.bar_fill.y, where.bar_fill.w, where.bar_fill.h, kLoadingBarColour);
    const ants::assets::ColorRGBA ink{7, 11, 15, 255};                            // (the black of the original's labels on the orange)
    renderer.draw_text(text, where.text_x, where.text_y, ink, FontSize::Px24);
    renderer.draw_text(hint, where.hint_x, where.hint_y, ink, FontSize::Px14);
}

const QuickHelpLayout& QuickHelpLayout::classic() noexcept { return kQuickHelpClassic; }
const QuickHelpLayout& QuickHelpLayout::wide() noexcept { return kQuickHelpWide; }

void draw_quick_help_screen(IRenderer& renderer, const ants::assets::AssetArchive& archive, bool wide, QuickHelpStart start) {
    if (!wide) {
        // qh_screen composite (last part first) and the START button animations qh_start1 / qh_start2 (hover) / qh_start3 (pressed) with absolute coordinates
        draw_animation_frame0(renderer, archive, "qh_screen");
        draw_animation_frame0(renderer, archive, start_animation(start));
        return;
    }
    // the wide frame on flat clay, the two columns centred (qh1 under qh2, as the composite stacks them), START! in the bottom right corner
    const QuickHelpLayout& l = QuickHelpLayout::wide();
    draw_wide_background(renderer, archive, PageClay::Flat);
    renderer.draw_named_sprite("qh1.bmp", l.left.x, l.left.y);
    renderer.draw_named_sprite("qh2.bmp", l.right.x, l.right.y);
    draw_animation_frame0(renderer, archive, start_animation(start), kWidePageDx, kWidePageDy);
}

}  // namespace ants::app
